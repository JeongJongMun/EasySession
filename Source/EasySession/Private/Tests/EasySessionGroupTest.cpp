// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionMatchmakingRequest.h"
#include "EasySessionPlayerComponent.h"
#include "EasySessionReservationBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionGroupTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> PendingResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};

	/** @return Whether the phase timed out, reporting it and taking the game instance down when it did. */
	bool TimedOut(TSharedPtr<FTestState> State, const TCHAR* What)
	{
		if (FPlatformTime::Seconds() - State->StartTime <= TimeoutSeconds)
		{
			return false;
		}

		FAutomationTestFramework::Get().GetCurrentTest()->AddError(FString::Printf(TEXT("Timed out in phase %d waiting for %s."), State->Phase, What));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	/** Move to the next phase and restart its timeout. */
	void NextPhase(TSharedPtr<FTestState> State)
	{
		++State->Phase;
		State->PendingResult.Reset();
		State->StartTime = FPlatformTime::Seconds();
	}

	FEasySessionHostParams MakeHostParams()
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = TEXT("EasySession Group");
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		return HostParams;
	}

	/** The callback that stores a request's result for the latent command. */
	FEasySessionCompleteDelegate MakeCallback(TSharedPtr<FTestState> State)
	{
		return FEasySessionCompleteDelegate::CreateLambda([State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		});
	}

	/** A one-pass run that would host a session of its own when it finds none. */
	void StartMatchmaking(TSharedPtr<FTestState> State, UEasySessionSubsystem& Subsystem)
	{
		FEasyMatchmakingParams Params;
		Params.Search.bLANQuery = true;
		Params.MaxSearchPasses = 1;
		Params.DelayBetweenPassesSeconds = 0.0f;
		Params.bAllowHostFallback = true;
		Params.Host = MakeHostParams();
		Subsystem.StartMatchmaking(Params, nullptr, MakeCallback(State));
	}

	/** An id for a made-up player, which the NULL subsystem creates for any name. */
	FUniqueNetIdRepl MakePlayerId(UWorld* World, const TCHAR* PlayerName)
	{
		const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
		return FUniqueNetIdRepl(Identity.IsValid() ? Identity->CreateUniquePlayerId(PlayerName) : nullptr);
	}

	UEasySessionSubsystem* Begin(TSharedPtr<FTestState> State, FAutomationTestBase& Test)
	{
		State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
		EasySessionTest::InitializeGameInstance(State->GameInstance);

		UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
		if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
		{
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return nullptr;
		}

		State->StartTime = FPlatformTime::Seconds();
		return Subsystem;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionHostMatchmakingStep, TSharedPtr<EasySessionGroupTest::FTestState>, State);
bool FEasySessionHostMatchmakingStep::Update()
{
	using namespace EasySessionGroupTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Phase)
	{
		case 0:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create"));
			}

			StartMatchmaking(State, *Subsystem);
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (State->PendingResult.IsSet())
			{
				CurrentTest->AddError(FString::Printf(TEXT("The host's run ended before it searched: %s."), *EasySession::ResultToString(State->PendingResult.GetValue())));
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// The queue starts the run on its next tick, and the run then starts its search.
			if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
			{
				return TimedOut(State, TEXT("the host's search"));
			}

			if (const TSharedPtr<FEasySessionMatchmakingRequest> Run = FEasySessionTestAccess::GetMatchmakingRequest(*Subsystem))
			{
				// No player joined this headless session, so the group is the host alone.
				CurrentTest->TestEqual(TEXT("It looks for room for the host and its group"), FEasySessionTestAccess::GetMatchmakingParams(*Run).Search.MinOpenSlots, 1);
			}

			// The host's own session comes back from the search, and is no candidate.
			// The run completes inside this call, so the phase moves on first.
			NextPhase(State);
			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem) });
			return false;
		}

		case 2:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the host's run"));
			}

			CurrentTest->TestEqual(TEXT("A host that found no other session ends with nothing found"), State->PendingResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestTrue(TEXT("It stays in its session instead of hosting another"), Subsystem->IsInSession());

			Subsystem->StartSession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 3:
		{
			if (Subsystem->GetSessionState() != EEasySessionState::InProgress || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the match start"));
			}

			StartMatchmaking(State, *Subsystem);
			NextPhase(State);
			return false;
		}

		case 4:
		{
			if (!State->PendingResult.IsSet())
			{
				return TimedOut(State, TEXT("the refusal"));
			}

			CurrentTest->TestEqual(TEXT("The host of a match in progress is refused"), State->PendingResult.GetValue(), EEasySessionResult::SessionAlreadyExists);

			Subsystem->DestroySession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		default:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the cleanup destroy"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * A host whose match has not started may matchmake from its own session, and each join then takes the session's players along.
 * The run looks for room for the whole group, skips the host's own session, and never hosts a second session when it finds none.
 * The host of a match in progress is still refused, because its leaving would end the match for everyone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostMatchmakingTest, "EasySession.Group.AHostMatchmakesFromItsSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostMatchmakingTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionGroupTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	UEasySessionSubsystem* Subsystem = Begin(State, *this);
	if (Subsystem == nullptr)
	{
		return false;
	}

	Subsystem->CreateSession(MakeHostParams());
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionHostMatchmakingStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionKickedPlayerStep, TSharedPtr<EasySessionGroupTest::FTestState>, State);
bool FEasySessionKickedPlayerStep::Update()
{
	using namespace EasySessionGroupTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	FEasySessionPlayerInfo Kicked;
	Kicked.PlayerId = MakePlayerId(World, TEXT("EasySessionKickedPlayer"));
	const FUniqueNetIdRepl Other = MakePlayerId(World, TEXT("EasySessionOtherPlayer"));

	switch (State->Phase)
	{
		case 0:
		{
			CurrentTest->TestEqual(TEXT("Only the host can kick"), Subsystem->KickPlayer(Kicked, FText::GetEmpty()), EEasySessionResult::RequiresSessionAuthority);

			Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create"));
			}

			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			CurrentTest->TestEqual(TEXT("A player who is not connected cannot be kicked"), Subsystem->KickPlayer(Kicked, FText::GetEmpty()), EEasySessionResult::InvalidParams);

			FEasySessionTestAccess::AddKickedPlayer(*Subsystem, Kicked.PlayerId);
			CurrentTest->TestEqual(TEXT("A kicked player is refused"), FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Kicked.PlayerId), EEasyReservationResult::Refused);
			CurrentTest->TestEqual(TEXT("Everyone else still joins"), FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Other), EEasyReservationResult::Approved);
			CurrentTest->TestEqual(TEXT("A group with a kicked player is refused as a whole"),
				FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Other, TArray<FUniqueNetIdRepl>{ Kicked.PlayerId }), EEasyReservationResult::Refused);

			Subsystem->DestroySession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 2:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the destroy"));
			}

			Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 3:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the second create"));
			}

			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			CurrentTest->TestEqual(TEXT("A kick ends with the session it was made in"), FEasySessionTestAccess::AskApproveJoin(*Subsystem, FString(), Kicked.PlayerId), EEasyReservationResult::Approved);

			Subsystem->DestroySession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		default:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the cleanup destroy"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * A kicked player is refused for as long as the session they were kicked from exists, and a new session lets them in again.
 * A group with a kicked player in it is refused as a whole, so its host never leaves that player behind.
 * Only the host kicks, and only a connected remote player.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionKickedPlayerTest, "EasySession.Group.AKickedPlayerStaysOutUntilTheSessionEnds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionKickedPlayerTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionGroupTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionKickedPlayerStep(State));
	return true;
}

namespace EasySessionGroupTest
{
	/** Spawn a PlayerState in the world, the way a login or a PlayerState swap in a seamless travel does. */
	APlayerState* SpawnPlayerState(UWorld* World)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		return World->SpawnActor<APlayerState>(SpawnParams);
	}

	/** @return The replicated player component on this PlayerState, or null. */
	UEasySessionPlayerComponent* GetPlayerComponent(const APlayerState* PlayerState)
	{
		UEasySessionPlayerComponent* Component = PlayerState ? PlayerState->FindComponentByClass<UEasySessionPlayerComponent>() : nullptr;
		return Component != nullptr && Component->GetIsReplicated() ? Component : nullptr;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPlayerComponentStep, TSharedPtr<EasySessionGroupTest::FTestState>, State);
bool FEasySessionPlayerComponentStep::Update()
{
	using namespace EasySessionGroupTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	switch (State->Phase)
	{
		case 0:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create"));
			}

			// A seamless travel swaps the PlayerStates before the host sets the new world up.
			APlayerState* AlreadyHere = SpawnPlayerState(World);
			CurrentTest->TestNull(TEXT("A PlayerState gets no component before the host sets the world up"), GetPlayerComponent(AlreadyHere));

			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			CurrentTest->TestNotNull(TEXT("A PlayerState already in the world gets one when the host sets it up"), GetPlayerComponent(AlreadyHere));

			UEasySessionPlayerComponent* Component = GetPlayerComponent(SpawnPlayerState(World));
			if (CurrentTest->TestNotNull(TEXT("A PlayerState spawned later gets one when it spawns"), Component))
			{
				CurrentTest->TestFalse(TEXT("A new player starts not ready"), Component->IsReady());
				Component->SetReady(true);
				CurrentTest->TestTrue(TEXT("The host sets a player ready directly"), Component->IsReady());
			}

			// A headless world has no local player controller, so the local player has no PlayerState to be ready on.
			CurrentTest->TestEqual(TEXT("Without a local PlayerState the ready state cannot change"), Subsystem->SetSessionReady(true), EEasySessionResult::NoSessionExists);

			Subsystem->DestroySession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		default:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the destroy"));
			}

			CurrentTest->TestNull(TEXT("Without a session no PlayerState gets one"), GetPlayerComponent(SpawnPlayerState(World)));

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The host adds a player component to every PlayerState of its world, which carries a message to one player and the player's ready state.
 * A PlayerState spawned before the host set the world up gets one too, because a seamless travel swaps the PlayerStates that early.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPlayerComponentTest, "EasySession.Group.EveryPlayerStateGetsAPlayerComponent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPlayerComponentTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionGroupTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	UEasySessionSubsystem* Subsystem = Begin(State, *this);
	if (Subsystem == nullptr)
	{
		return false;
	}

	Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPlayerComponentStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
