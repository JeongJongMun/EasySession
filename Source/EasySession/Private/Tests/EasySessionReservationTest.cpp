// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionReservationBeacon.h"
#include "EasySessionReservations.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionReservationTest
{
	// Maximum time to wait for each request before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	/** Which request the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingUpdate,
		AwaitingStart,
		AwaitingDestroy
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The game mode a test spawns to send PreLogin from. Null in the tests that never send one. */
		TWeakObjectPtr<AGameModeBase> GameMode;

		TOptional<EEasySessionResult> PendingResult;
		EStep Step = EStep::AwaitingCreate;
		double StartTime = 0.0;
	};

	/** What a wait produced. */
	enum class EWait : uint8
	{
		/** The request is still running, so the latent command runs again on the next tick. */
		Waiting,

		/** The request finished and the step can go on. */
		Ready,

		/** The request took too long. The test has failed and the world is already destroyed. */
		TimedOut
	};

	static FEasySessionHostParams MakeParams(int32 MaxPlayers)
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Reservation");
		Params.MaxPlayers = MaxPlayers;
		Params.bIsLANMatch = true;
		Params.InitialMapName = EasySessionTest::SessionMapName;
		return Params;
	}

	// The callback that stores a request's result for the latent command.
	static FEasySessionCompleteDelegate MakeCallback(TSharedPtr<FTestState> State)
	{
		return FEasySessionCompleteDelegate::CreateLambda(
			[State](EEasySessionResult Result, const FString&)
			{
				State->PendingResult = Result;
			});
	}

	static EWait WaitForRequest(FTestState& State, FAutomationTestBase& Test, const TCHAR* What)
	{
		if (State.PendingResult.IsSet())
		{
			return EWait::Ready;
		}

		if (FPlatformTime::Seconds() - State.StartTime <= TimeoutSeconds)
		{
			return EWait::Waiting;
		}

		Test.AddError(FString::Printf(TEXT("Timed out waiting for the %s."), What));
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
		return EWait::TimedOut;
	}

	// Take the result the last request produced, and empty it for the next one.
	static EEasySessionResult ConsumeResult(FTestState& State)
	{
		const EEasySessionResult Result = State.PendingResult.GetValue();
		State.PendingResult.Reset();
		State.StartTime = FPlatformTime::Seconds();
		return Result;
	}

	// Destroy the session, which every test does before it destroys its world.
	// The online subsystem outlives these worlds, so a session left behind makes the next test's create fail with SessionAlreadyExists.
	static void StartDestroy(TSharedPtr<FTestState> State, UEasySessionSubsystem& Subsystem)
	{
		State->Step = EStep::AwaitingDestroy;
		State->StartTime = FPlatformTime::Seconds();
		Subsystem.DestroySession(MakeCallback(State));
	}

	// Create the game instance and start the CreateSession every one of these tests begins with.
	static UEasySessionSubsystem* Begin(TSharedPtr<FTestState> State, FAutomationTestBase& Test, const FEasySessionHostParams& Params)
	{
		State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
		EasySessionTest::InitializeGameInstance(State->GameInstance);

		UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
		if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
		{
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return nullptr;
		}

		Subsystem->CreateSession(Params, MakeCallback(State));
		State->StartTime = FPlatformTime::Seconds();
		return Subsystem;
	}

	static UEasySessionSubsystem* Begin(TSharedPtr<FTestState> State, FAutomationTestBase& Test, int32 MaxPlayers)
	{
		return Begin(State, Test, MakeParams(MaxPlayers));
	}

	// An id for a made-up player, which the NULL subsystem creates for any name.
	static FUniqueNetIdRepl MakePlayerId(UWorld* World, const TCHAR* PlayerName)
	{
		const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
		return FUniqueNetIdRepl(Identity.IsValid() ? Identity->CreateUniquePlayerId(PlayerName) : nullptr);
	}

	// Set the ElapsedTime of every player holding a reservation, as if they had been out of the session this long.
	static void AgeEveryReservation(APartyBeaconHost& Beacon, float Seconds)
	{
		for (FPartyReservation& Reservation : Beacon.GetState()->GetReservations())
		{
			for (FPlayerReservation& Member : Reservation.PartyMembers)
			{
				Member.ElapsedTime = Seconds;
			}
		}
	}

	// The longest ElapsedTime of any player holding a reservation.
	static float LongestReservationWait(const APartyBeaconHost& Beacon)
	{
		float Longest = 0.0f;
		for (const FPartyReservation& Reservation : Beacon.GetState()->GetReservations())
		{
			for (const FPlayerReservation& Member : Reservation.PartyMembers)
			{
				Longest = FMath::Max(Longest, Member.ElapsedTime);
			}
		}
		return Longest;
	}

	// Add a reservation for this player, the way an approved join does before that player arrives.
	// Returns whether the beacon accepted it.
	static bool AddReservationFor(APartyBeaconHost& Beacon, const FUniqueNetIdRepl& PlayerId)
	{
		FPartyReservation Reservation;
		Reservation.TeamNum = 0;
		Reservation.PartyLeader = PlayerId;

		Reservation.PartyMembers.Add(EasySessionReservation::MakeReservation(PlayerId));

		return Beacon.AddPartyReservation(Reservation) == EPartyReservationResult::ReservationAccepted;
	}

	// Ask the beacon for one reservation that holds a requester and the group traveling with them, the way the join of a player with a group does.
	// Returns the parent's result, before this plugin turns it into a response.
	static EPartyReservationResult::Type AddGroupReservation(APartyBeaconHost& Beacon, const FUniqueNetIdRepl& LeaderId, const TArray<FUniqueNetIdRepl>& GroupMembers)
	{
		FPartyReservation Reservation;
		Reservation.TeamNum = 0;
		Reservation.PartyLeader = LeaderId;
		Reservation.PartyMembers = EasySessionReservation::MakeReservations(LeaderId, GroupMembers);
		return Beacon.AddPartyReservation(Reservation);
	}

	// The refusal PreLogin writes for this player, or empty when it lets them in.
	static FString SendPreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& PlayerId)
	{
		FString ErrorMessage;
		FGameModeEvents::GameModePreLoginEvent.Broadcast(GameMode, PlayerId, ErrorMessage);
		return ErrorMessage;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionHostReservationStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionHostReservationStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	const EWait Wait = WaitForRequest(*State, *CurrentTest, State->Step == EStep::AwaitingCreate ? TEXT("create") : TEXT("destroy"));
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	if (State->Step == EStep::AwaitingDestroy)
	{
		CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

	// The beacon is started where the host initializes the game mode of the session's map.
	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	const AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
	{
		CurrentTest->TestEqual(TEXT("The beacon holds one reservation per Max Players"), Beacon->GetMaxReservations(), 4);
		CurrentTest->TestEqual(TEXT("The host holds one of them"), Beacon->GetNumConsumedReservations(), 1);

		// Nothing in this plugin sets this wait, so it has to reach this subclass from the engine's own config section.
		const float EngineMissingPlayerWait = GetDefault<APartyBeaconHost>()->GetSessionTimeoutSecs(FUniqueNetIdRepl());
		CurrentTest->TestTrue(TEXT("The engine config gives a wait for a missing player"), EngineMissingPlayerWait > 0.0f);
		CurrentTest->TestEqual(TEXT("This subclass waits as long as the engine config says"),
			Beacon->GetSessionTimeoutSecs(FUniqueNetIdRepl()), EngineMissingPlayerWait);
		CurrentTest->TestEqual(TEXT("The three reservations left are open to joining players"),
			FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString()), EEasyReservationResult::Approved);
	}

	StartDestroy(State, *Subsystem);
	return false;
}

/**
 * The host holds one of the MaxPlayers reservations, like any joining player.
 *
 * The beacon refuses every reservation until it is registered on the shared listener, so the host's reservation is added after the registration.
 * Reserved before it, the request came back denied and the session took one player too many.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostReservationTest, "EasySession.Reservation.TheHostHoldsAReservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostReservationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, 4) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionHostReservationStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionHeldReservationStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionHeldReservationStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const EWait Wait = WaitForRequest(*State, *CurrentTest, State->Step == EStep::AwaitingCreate ? TEXT("create") : TEXT("destroy"));
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	if (State->Step == EStep::AwaitingDestroy)
	{
		CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
	{
		CurrentTest->TestEqual(TEXT("The last reservation is free while only the host holds one"),
			FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString()), EEasyReservationResult::Approved);

		CurrentTest->TestTrue(TEXT("An approved player takes the last reservation"),
			AddReservationFor(*Beacon, MakePlayerId(World, TEXT("EasySessionApprovedPlayer"))));
		CurrentTest->TestEqual(TEXT("Both reservations are held"), Beacon->GetNumConsumedReservations(), 2);

		// The player holding the reservation never arrived, so counting the players in the session would find an open slot and let this one in.
		CurrentTest->TestTrue(TEXT("Fewer players arrived than Max Players"), FEasySessionTestAccess::GetRegisteredPlayerCount(*Subsystem) < 2);
		CurrentTest->TestEqual(TEXT("The next player is refused while the reservation is held"),
			FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString()), EEasyReservationResult::SessionFull);
	}

	StartDestroy(State, *Subsystem);
	return false;
}

/**
 * A reservation is added when a join is approved and stays held while that player travels, so the next player is refused meanwhile.
 * The game mode counts a player only after that player arrives.
 * Before the beacon held reservations, two players traveling to a session with one open slot left were both approved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHeldReservationTest, "EasySession.Reservation.AReservationRefusesTheNextPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHeldReservationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();

	// Two players: the host, and one reservation for someone to hold.
	if (Begin(State, *this, 2) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionHeldReservationStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionReservationResizeStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionReservationResizeStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	const TCHAR* What = TEXT("create");
	switch (State->Step)
	{
		case EStep::AwaitingUpdate: What = TEXT("update"); break;
		case EStep::AwaitingDestroy: What = TEXT("destroy"); break;
		default: break;
	}

	const EWait Wait = WaitForRequest(*State, *CurrentTest, What);
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

			FEasySessionSettings Raised = Subsystem->GetSessionSettings();
			Raised.MaxPlayers = 6;

			State->Step = EStep::AwaitingUpdate;
			Subsystem->UpdateSession(Raised, MakeCallback(State));
			return false;
		}

		case EStep::AwaitingUpdate:
		{
			CurrentTest->TestEqual(TEXT("Raising Max Players succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			const AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
			if (CurrentTest->TestNotNull(TEXT("The session still runs the reservation beacon"), Beacon))
			{
				CurrentTest->TestEqual(TEXT("The beacon holds reservations for the new Max Players"), Beacon->GetMaxReservations(), 6);
				CurrentTest->TestEqual(TEXT("The host still holds exactly one"), Beacon->GetNumConsumedReservations(), 1);
			}

			StartDestroy(State, *Subsystem);
			return false;
		}

		case EStep::AwaitingDestroy:
		{
			CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionKeptReservationStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionKeptReservationStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const EWait Wait = WaitForRequest(*State, *CurrentTest, State->Step == EStep::AwaitingCreate ? TEXT("create") : TEXT("destroy"));
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	if (State->Step == EStep::AwaitingDestroy)
	{
		CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	const FUniqueNetIdRepl TravelingPlayer = MakePlayerId(World, TEXT("EasySessionTravelingPlayer"));
	AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
	{
		CurrentTest->TestTrue(TEXT("An approved player holds a reservation"), AddReservationFor(*Beacon, TravelingPlayer));
		CurrentTest->TestEqual(TEXT("The host and that player hold one each"), Beacon->GetNumConsumedReservations(), 2);

		// The ElapsedTime they have in this world, which the map change must clear.
		AgeEveryReservation(*Beacon, 30.0f);
	}

	// What the host does when a server travel starts: it keeps the reservations and destroys the beacon, and the next world starts a new one.
	FEasySessionTestAccess::DestroyHostSideActors(*Subsystem);
	CurrentTest->TestNull(TEXT("The map change took the beacon down"), FEasySessionTestAccess::GetReservationBeacon(*Subsystem));

	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	const AEasySessionReservationBeaconHost* NextBeacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The next world runs a beacon again"), NextBeacon))
	{
		CurrentTest->TestEqual(TEXT("It still holds both reservations"), NextBeacon->GetNumConsumedReservations(), 2);
		CurrentTest->TestEqual(TEXT("On the same Max Players"), NextBeacon->GetMaxReservations(), 4);
		CurrentTest->TestTrue(TEXT("The traveling player still holds theirs"), FEasySessionTestAccess::PlayerHasReservation(*Subsystem, TravelingPlayer));
		CurrentTest->TestEqual(TEXT("Everyone waits from zero again"), LongestReservationWait(*NextBeacon), 0.0f);
	}

	StartDestroy(State, *Subsystem);
	return false;
}

/**
 * A map change destroys the beacon, and the players it approved are traveling to that very map.
 * Their reservations move to the beacon of the next world, or the map change would let the session take more players than MaxPlayers.
 *
 * The wait starts over there, because the map change makes every player travel again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionKeptReservationTest, "EasySession.Reservation.ReservationsSurviveTheMapChange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionKeptReservationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, 4) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionKeptReservationStep(State));
	return true;
}

/**
 * The beacon decides whether the session is full, so a MaxPlayers change has to reach it.
 * Left alone, the beacon would keep refusing joins on the reservation count the session was created with.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReservationResizeTest, "EasySession.Reservation.MaxPlayersUpdateResizesTheReservations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReservationResizeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, 4) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionReservationResizeStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionRemovedReservationStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionRemovedReservationStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const EWait Wait = WaitForRequest(*State, *CurrentTest, State->Step == EStep::AwaitingCreate ? TEXT("create") : TEXT("destroy"));
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	if (State->Step == EStep::AwaitingDestroy)
	{
		CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	const FUniqueNetIdRepl LeavingPlayer = MakePlayerId(World, TEXT("EasySessionLeavingPlayer"));
	AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
	{
		CurrentTest->TestTrue(TEXT("A player takes the last reservation"), AddReservationFor(*Beacon, LeavingPlayer));
		CurrentTest->TestEqual(TEXT("The session is full"),
			FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString()), EEasyReservationResult::SessionFull);

		// What the host does when that player's controller logs out.
		FEasySessionTestAccess::RemovePlayerReservation(*Subsystem, LeavingPlayer);

		CurrentTest->TestFalse(TEXT("The player who left holds no reservation"), FEasySessionTestAccess::PlayerHasReservation(*Subsystem, LeavingPlayer));
		CurrentTest->TestEqual(TEXT("Only the host's reservation is left"), Beacon->GetNumConsumedReservations(), 1);
		CurrentTest->TestEqual(TEXT("The next player is let in at once"),
			FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString()), EEasyReservationResult::Approved);

		// This player never arrived, so they were still in PlayersPendingJoin when their reservation was removed.
		CurrentTest->TestTrue(TEXT("The same player can take a reservation again"), AddReservationFor(*Beacon, LeavingPlayer));
	}

	StartDestroy(State, *Subsystem);
	return false;
}

/**
 * A player whose controller logs out loses their reservation, so the next player is let in without waiting for a timeout.
 * The player is also removed from PlayersPendingJoin, or their next join would be refused as a player already in the session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionRemovedReservationTest, "EasySession.Reservation.LeavingRemovesTheReservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionRemovedReservationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();

	// Two players: the host, and one reservation for the player who logs out.
	if (Begin(State, *this, 2) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionRemovedReservationStep(State));
	return true;
}

/**
 * Every result the parent can return for a reservation request maps to a response here.
 * A duplicate is the one whose name misleads: the parent found the reservation this player already holds and kept it.
 *
 * Read as a refusal, every player who logged out and joined the session again was refused until their reservation timed out.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReservationResultTest, "EasySession.Reservation.ResultTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReservationResultTest::RunTest(const FString& Parameters)
{
	auto Answer = [](EPartyReservationResult::Type Result)
	{
		return AEasySessionReservationBeaconClient::MakeResponseFromReservationResult(Result).Result;
	};

	TestEqual(TEXT("A new reservation lets the player through"), Answer(EPartyReservationResult::ReservationAccepted), EEasyReservationResult::Approved);
	TestEqual(TEXT("A returning player keeps the reservation they hold"), Answer(EPartyReservationResult::ReservationDuplicate), EEasyReservationResult::Approved);
	TestEqual(TEXT("No reservation left is a full session"), Answer(EPartyReservationResult::PartyLimitReached), EEasyReservationResult::SessionFull);
	TestEqual(TEXT("A party that does not fit is a full session too"), Answer(EPartyReservationResult::IncorrectPlayerCount), EEasyReservationResult::SessionFull);
	TestEqual(TEXT("A host that never answered is unreachable"), Answer(EPartyReservationResult::RequestTimedOut), EEasyReservationResult::Unreachable);
	TestEqual(TEXT("Players already in the session are refused"), Answer(EPartyReservationResult::ReservationDenied_ContainsExistingPlayers), EEasyReservationResult::Refused);
	TestEqual(TEXT("A paused beacon is refused"), Answer(EPartyReservationResult::ReservationDenied), EEasyReservationResult::Refused);
	TestEqual(TEXT("A banned player is refused"), Answer(EPartyReservationResult::ReservationDenied_Banned), EEasyReservationResult::Refused);
	TestEqual(TEXT("An unknown result is refused rather than let through"), Answer(EPartyReservationResult::GeneralError), EEasyReservationResult::Refused);

	// The join node shows this sentence, so a refused player is never left without one.
	TestFalse(TEXT("A refusal says why"), AEasySessionReservationBeaconClient::MakeResponseFromReservationResult(EPartyReservationResult::GeneralError).ReasonText.IsEmpty());
	TestTrue(TEXT("An approved join has nothing to say"), AEasySessionReservationBeaconClient::MakeResponseFromReservationResult(EPartyReservationResult::ReservationDuplicate).ReasonText.IsEmpty());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPreLoginStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionPreLoginStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const TCHAR* What = TEXT("create");
	switch (State->Step)
	{
		case EStep::AwaitingStart: What = TEXT("start"); break;
		case EStep::AwaitingDestroy: What = TEXT("destroy"); break;
		default: break;
	}

	const EWait Wait = WaitForRequest(*State, *CurrentTest, What);
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	const FUniqueNetIdRepl ReservationHolder = MakePlayerId(World, TEXT("EasySessionReservationHolder"));
	const FUniqueNetIdRepl Stranger = MakePlayerId(World, TEXT("EasySessionStranger"));

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
			State->GameMode = World->SpawnActor<AGameModeBase>(SpawnParams);

			AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
			if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
			{
				CurrentTest->TestTrue(TEXT("The beacon approved a player and holds their reservation"), AddReservationFor(*Beacon, ReservationHolder));
			}

			CurrentTest->TestEqual(TEXT("A player holding a reservation is let in without the password"),
				SendPreLogin(State->GameMode.Get(), ReservationHolder), FString());

			// The password is never in the travel URL, so an arrival without a reservation went around the beacon.
			const FString Refusal = SendPreLogin(State->GameMode.Get(), Stranger);
			CurrentTest->TestTrue(TEXT("A player without a reservation is refused"), Refusal.StartsWith(FEasySessionReservations::RefusalMark));
			CurrentTest->TestTrue(TEXT("The refusal does not claim a wrong password"), Refusal.Contains(TEXT("Could not verify the session password.")));

			State->Step = EStep::AwaitingStart;
			Subsystem->StartSession(MakeCallback(State));
			return false;
		}

		case EStep::AwaitingStart:
		{
			CurrentTest->TestEqual(TEXT("Starting the match succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			// Approved before the match started, and arriving after it, or reconnected by a hard travel.
			CurrentTest->TestEqual(TEXT("A player holding a reservation is let into a started match"),
				SendPreLogin(State->GameMode.Get(), ReservationHolder), FString());
			CurrentTest->TestFalse(TEXT("A started match still refuses a player without a reservation"),
				SendPreLogin(State->GameMode.Get(), Stranger).IsEmpty());

			StartDestroy(State, *Subsystem);
			return false;
		}

		default:
		{
			CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * PreLogin lets in a player the reservation beacon approved, and runs the join checks again only for a player without a reservation.
 * The password never travels in the URL, so a password-protected session refuses every arrival without a reservation.
 * A reservation also outranks join-in-progress, because the beacon approved that player before the match started.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPreLoginTest, "EasySession.Reservation.PreLoginLetsInReservationHolders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPreLoginTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	FEasySessionHostParams Params = MakeParams(4);
	Params.Password = TEXT("hunter2");
	Params.bAllowJoinInProgress = false;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, Params) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPreLoginStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionHolderChecksStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionHolderChecksStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const TCHAR* What = TEXT("create");
	switch (State->Step)
	{
		case EStep::AwaitingStart: What = TEXT("start"); break;
		case EStep::AwaitingDestroy: What = TEXT("destroy"); break;
		default: break;
	}

	const EWait Wait = WaitForRequest(*State, *CurrentTest, What);
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	const FUniqueNetIdRepl Member = MakePlayerId(World, TEXT("EasySessionGroupMember"));
	const FUniqueNetIdRepl Stranger = MakePlayerId(World, TEXT("EasySessionStranger"));

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

			AEasySessionReservationBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
			if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
			{
				// The host and this player take both slots, the way a group's reservation would hold this player.
				CurrentTest->TestTrue(TEXT("The member holds a reservation"), AddReservationFor(*Beacon, Member));
			}

			CurrentTest->TestEqual(TEXT("A member holding a reservation needs no password, even in a full session"),
				FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Member), EEasyReservationResult::Approved);
			CurrentTest->TestEqual(TEXT("A player without one is refused on the full session"),
				FEasySessionTestAccess::AskApproveJoin(*Subsystem, TEXT("hunter2"), Stranger), EEasyReservationResult::SessionFull);

			State->Step = EStep::AwaitingStart;
			Subsystem->StartSession(MakeCallback(State));
			return false;
		}

		case EStep::AwaitingStart:
		{
			CurrentTest->TestEqual(TEXT("Starting the match succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			CurrentTest->TestEqual(TEXT("A member holding a reservation is approved into a started match"),
				FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Member), EEasyReservationResult::Approved);
			CurrentTest->TestEqual(TEXT("A player without one is refused on the started match"),
				FEasySessionTestAccess::AskApproveJoin(*Subsystem, TEXT("hunter2"), Stranger), EEasyReservationResult::Refused);

			StartDestroy(State, *Subsystem);
			return false;
		}

		default:
		{
			CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * A reservation holder is approved before the join-in-progress, open slot and password checks, so a player of a group needs no password and no open slot.
 * The password, the open slots and join-in-progress still decide for every other player.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHolderChecksTest, "EasySession.Reservation.HoldersSkipTheJoinChecks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHolderChecksTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	FEasySessionHostParams Params = MakeParams(2);
	Params.Password = TEXT("hunter2");
	Params.bAllowJoinInProgress = false;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, Params) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionHolderChecksStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionGroupReservationStep, TSharedPtr<EasySessionReservationTest::FTestState>, State);
bool FEasySessionGroupReservationStep::Update()
{
	using namespace EasySessionReservationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	const EWait Wait = WaitForRequest(*State, *CurrentTest, State->Step == EStep::AwaitingCreate ? TEXT("create") : TEXT("destroy"));
	if (Wait != EWait::Ready)
	{
		return Wait == EWait::TimedOut;
	}

	if (State->Step == EStep::AwaitingDestroy)
	{
		CurrentTest->TestEqual(TEXT("The cleanup destroy succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	CurrentTest->TestEqual(TEXT("Creating the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);
	FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

	const FUniqueNetIdRepl Leader = MakePlayerId(World, TEXT("EasySessionGroupLeader"));
	const FUniqueNetIdRepl MemberA = MakePlayerId(World, TEXT("EasySessionGroupMemberA"));
	const FUniqueNetIdRepl MemberB = MakePlayerId(World, TEXT("EasySessionGroupMemberB"));
	const FUniqueNetIdRepl MemberC = MakePlayerId(World, TEXT("EasySessionGroupMemberC"));

	const TArray<FPlayerReservation> Listed = EasySessionReservation::MakeReservations(Leader, { MemberA, Leader, FUniqueNetIdRepl(), MemberA, MemberB });
	if (CurrentTest->TestEqual(TEXT("The leader, then each member once, with no invalid id"), Listed.Num(), 3))
	{
		CurrentTest->TestTrue(TEXT("The leader is listed first"), Listed[0].UniqueId == Leader);
	}

	APartyBeaconHost* Beacon = FEasySessionTestAccess::GetReservationBeacon(*Subsystem);
	if (CurrentTest->TestNotNull(TEXT("The session runs the reservation beacon"), Beacon))
	{
		// Four players, and the host already holds one of the four slots.
		const EPartyReservationResult::Type TooLarge = AddGroupReservation(*Beacon, Leader, { MemberA, MemberB, MemberC });
		CurrentTest->TestEqual(TEXT("A group of four does not fit the three slots left"),
			AEasySessionReservationBeaconClient::MakeResponseFromReservationResult(TooLarge).Result, EEasyReservationResult::SessionFull);
		CurrentTest->TestEqual(TEXT("A refused group holds no slot"), Beacon->GetNumConsumedReservations(), 1);

		CurrentTest->TestEqual(TEXT("A group of three takes the three slots left in one reservation"),
			AddGroupReservation(*Beacon, Leader, { MemberA, MemberB }), EPartyReservationResult::ReservationAccepted);
		CurrentTest->TestEqual(TEXT("Every member of the group holds a slot"), Beacon->GetNumConsumedReservations(), 4);

		// A player of the group who follows the requester asks for a reservation of their own, and the parent moves them out of the group's.
		CurrentTest->TestEqual(TEXT("A member's own request is accepted on the full session"),
			AddGroupReservation(*Beacon, MemberB, {}), EPartyReservationResult::ReservationAccepted);
		CurrentTest->TestEqual(TEXT("It takes no second slot"), Beacon->GetNumConsumedReservations(), 4);
	}

	StartDestroy(State, *Subsystem);
	return false;
}

/**
 * One reservation holds the requester and every player of the group, or none of them, so a group never splits over a session with too few open slots.
 * A player of the group who then asks for their own reservation keeps the slot the group held, so they can follow the requester with a normal join.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionGroupReservationTest, "EasySession.Reservation.AGroupTakesItsSlotsTogether", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionGroupReservationTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this, 4) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionGroupReservationStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
