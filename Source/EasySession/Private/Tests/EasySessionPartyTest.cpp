// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionPartyBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionPartyTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
		TOptional<EEasySessionResult> PendingResult;
		TOptional<int32> FoundCount;
		int32 Phase = 0;
		double StartTime = 0.0;

		/** Did the test log the local player in, so it logs them out again at the end. */
		bool bLoggedIn = false;
	};

	IOnlineIdentityPtr GetIdentity(TSharedPtr<FTestState> State)
	{
		return Online::GetIdentityInterface(State->GameInstance->GetWorld());
	}

	/** Log out the player the test logged in, and take the game instance down. */
	void End(TSharedPtr<FTestState> State)
	{
		const IOnlineIdentityPtr Identity = GetIdentity(State);
		if (State->bLoggedIn && Identity.IsValid())
		{
			Identity->Logout(0);
		}
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	}

	/** @return Whether the phase timed out, reporting it and ending the test when it did. */
	bool TimedOut(TSharedPtr<FTestState> State, const TCHAR* What)
	{
		if (FPlatformTime::Seconds() - State->StartTime <= TimeoutSeconds)
		{
			return false;
		}

		FAutomationTestFramework::Get().GetCurrentTest()->AddError(FString::Printf(TEXT("Timed out in phase %d waiting for %s."), State->Phase, What));
		End(State);
		return true;
	}

	/** Move to the next phase and restart its timeout. */
	void NextPhase(TSharedPtr<FTestState> State)
	{
		++State->Phase;
		State->PendingResult.Reset();
		State->FoundCount.Reset();
		State->StartTime = FPlatformTime::Seconds();
	}

	/** The callback that stores a request's result for the latent command. */
	FEasySessionCompleteDelegate MakeCallback(TSharedPtr<FTestState> State)
	{
		return FEasySessionCompleteDelegate::CreateLambda([State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		});
	}

	/** The callback that stores a search's result and how many sessions it kept. */
	FEasySessionFindCompleteDelegate MakeFindCallback(TSharedPtr<FTestState> State)
	{
		return FEasySessionFindCompleteDelegate::CreateLambda([State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
		{
			State->PendingResult = Result;
			State->FoundCount = Results.Num();
		});
	}

	FEasySessionHostParams MakeHostParams()
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = TEXT("EasySession Party");
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		return HostParams;
	}

	FEasyPartyParams MakePartyParams(EEasyPartyPrivacy Privacy, int32 MaxMembers = 4)
	{
		FEasyPartyParams Params;
		Params.MaxMembers = MaxMembers;
		Params.Privacy = Privacy;
		return Params;
	}

	/** An id for a made-up player, which the NULL subsystem creates for any name. */
	FUniqueNetIdRepl MakePlayerId(TSharedPtr<FTestState> State, const TCHAR* PlayerName)
	{
		const IOnlineIdentityPtr Identity = GetIdentity(State);
		return FUniqueNetIdRepl(Identity.IsValid() ? Identity->CreateUniquePlayerId(PlayerName) : nullptr);
	}

	/** @return The integer the party session advertises under this key, or -1 when it advertises none. */
	int32 GetPartySettingInt(UEasySessionSubsystem& Subsystem, FName Key)
	{
		const FOnlineSessionSearchResult Result = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(Subsystem, NAME_PartySession);
		int32 Value = -1;
		Result.Session.SessionSettings.Get(Key, Value);
		return Value;
	}

	/**
	 * Start a game instance with a logged in local player, because a party needs a leader with an id.
	 * A headless test world starts with nobody logged in.
	 *
	 * @return The subsystem, or null when the test cannot run.
	 */
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

		const IOnlineIdentityPtr Identity = GetIdentity(State);
		if (Identity.IsValid() && !Identity->GetUniquePlayerId(0).IsValid())
		{
			State->bLoggedIn = Identity->Login(0, FOnlineAccountCredentials(FString(), TEXT("EasySessionPartyTest"), FString()));
		}
		if (!Test.TestTrue(TEXT("A player is logged in"), Identity.IsValid() && Identity->GetUniquePlayerId(0).IsValid()))
		{
			End(State);
			return nullptr;
		}

		State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
		Subsystem->OnPartyMembersChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandlePartyMembersChanged);
		Subsystem->OnPartyLeft.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandlePartyLeft);

		State->StartTime = FPlatformTime::Seconds();
		return Subsystem;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPartyLifecycleStep, TSharedPtr<EasySessionPartyTest::FTestState>, State);
bool FEasySessionPartyLifecycleStep::Update()
{
	using namespace EasySessionPartyTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Phase)
	{
		case 0:
		{
			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::JoinCode, 3), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the party create"));
			}

			CurrentTest->TestEqual(TEXT("The party is created"), State->PendingResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestTrue(TEXT("The local player is in the party"), Subsystem->IsInParty());
			CurrentTest->TestTrue(TEXT("The creator leads the party"), Subsystem->IsPartyLeader());
			CurrentTest->TestFalse(TEXT("A party is no game session"), Subsystem->IsInSession());
			CurrentTest->TestNotNull(TEXT("The party beacon runs"), FEasySessionTestAccess::GetPartyBeacon(*Subsystem));

			const TArray<FEasyPartyMemberInfo> Members = Subsystem->GetPartyMembers();
			if (CurrentTest->TestEqual(TEXT("The leader is the only member"), Members.Num(), 1))
			{
				CurrentTest->TestTrue(TEXT("The member is the leader"), Members[0].bIsLeader);
				CurrentTest->TestTrue(TEXT("The member is the local player"), Members[0].bIsLocalPlayer);
			}

			CurrentTest->TestEqual(TEXT("The session is marked as a party"), GetPartySettingInt(*Subsystem, EasySession::SettingKey_Party), 1);
			CurrentTest->TestEqual(TEXT("A join code party is hidden from plain searches"), GetPartySettingInt(*Subsystem, EasySession::SettingKey_Hidden), 1);

			Subsystem->CreateParty(FEasyPartyParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 2:
		{
			if (!State->PendingResult.IsSet())
			{
				return TimedOut(State, TEXT("the second party create"));
			}

			CurrentTest->TestEqual(TEXT("A second party is refused"), State->PendingResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestTrue(TEXT("The new member list was announced"), State->Listener->PartyMembersChangedBroadcasts > 0);

			FEasySessionSearchParams Search;
			Search.bLANQuery = true;
			Subsystem->FindSessions(Search, MakeFindCallback(State));
			NextPhase(State);
			return false;
		}

		case 3:
		{
			if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
			{
				return TimedOut(State, TEXT("the game session search"));
			}

			// A search for game sessions that the party answers, as NULL does, which ignores the query settings.
			NextPhase(State);
			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem, NAME_PartySession) });
			return false;
		}

		case 4:
		{
			if (!State->FoundCount.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the game session search result"));
			}

			CurrentTest->TestEqual(TEXT("A search for game sessions never returns a party"), State->FoundCount.GetValue(), 0);

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 5:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the party leave"));
			}

			CurrentTest->TestEqual(TEXT("The party is left"), State->PendingResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestFalse(TEXT("The local player is out of the party"), Subsystem->IsInParty());
			CurrentTest->TestEqual(TEXT("A left party has no members"), Subsystem->GetPartyMembers().Num(), 0);
			CurrentTest->TestNull(TEXT("The party beacon is closed"), FEasySessionTestAccess::GetPartyBeacon(*Subsystem));
			CurrentTest->TestTrue(TEXT("Leaving is reported once, as Left"), State->Listener->PartyLeftReasons.Num() == 1 && State->Listener->PartyLeftReasons[0] == EEasyPartyLeaveReason::Left);

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 6:
		{
			if (!State->PendingResult.IsSet())
			{
				return TimedOut(State, TEXT("the second party leave"));
			}

			CurrentTest->TestEqual(TEXT("Leaving without a party fails"), State->PendingResult.GetValue(), EEasySessionResult::NoSessionExists);
			CurrentTest->TestEqual(TEXT("A leave that found no party reports nothing"), State->Listener->PartyLeftReasons.Num(), 1);

			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::Public, 1), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 7:
		{
			if (!State->PendingResult.IsSet())
			{
				return TimedOut(State, TEXT("the invalid party create"));
			}

			CurrentTest->TestEqual(TEXT("A party of one is refused"), State->PendingResult.GetValue(), EEasySessionResult::InvalidParams);

			Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 8:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the game session create"));
			}

			Subsystem->CreateParty(FEasyPartyParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 9:
		{
			if (!State->PendingResult.IsSet())
			{
				return TimedOut(State, TEXT("the party create inside a game session"));
			}

			CurrentTest->TestEqual(TEXT("A party is not created inside a game session"), State->PendingResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestFalse(TEXT("No party exists"), Subsystem->IsInParty());

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

			End(State);
			return true;
		}
	}
}

/**
 * A party is created with the local player as its leader and only member, and leaving it closes the party beacon and reports Left.
 * A second party, a party of one and a party inside a game session are refused.
 * A search for game sessions skips the party even when the online subsystem returns it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyLifecycleTest, "EasySession.Party.CreateAndLeave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyLifecycleStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPartyAdmissionStep, TSharedPtr<EasySessionPartyTest::FTestState>, State);
bool FEasySessionPartyAdmissionStep::Update()
{
	using namespace EasySessionPartyTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	const FUniqueNetIdRepl Stranger = MakePlayerId(State, TEXT("EasySessionPartyStranger"));
	FText Reason;

	switch (State->Phase)
	{
		case 0:
		{
			FEasyPartyMemberInfo StrangerInfo;
			StrangerInfo.PlayerId = Stranger;
			CurrentTest->TestEqual(TEXT("Outside a party nobody can kick"), Subsystem->KickPartyMember(StrangerInfo, FText::GetEmpty()), EEasySessionResult::RequiresPartyLeader);
			CurrentTest->TestEqual(TEXT("Outside a party nobody is ready"), Subsystem->SetPartyReady(true), EEasySessionResult::NoSessionExists);

			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::Public, 3), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the public party create"));
			}

			const IOnlineIdentityPtr Identity = GetIdentity(State);
			const FUniqueNetIdRepl LeaderId(Identity->GetUniquePlayerId(0));
			const FUniqueNetIdRepl Kicked = MakePlayerId(State, TEXT("EasySessionPartyKicked"));

			CurrentTest->TestTrue(TEXT("A public party admits anyone"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Stranger, Reason));
			CurrentTest->TestFalse(TEXT("A member cannot join twice"), FEasySessionTestAccess::AskApproveMember(*Subsystem, LeaderId, Reason));

			CurrentTest->TestEqual(TEXT("The leader sets themselves ready"), Subsystem->SetPartyReady(true), EEasySessionResult::Success);
			CurrentTest->TestTrue(TEXT("The member list shows the leader ready"), Subsystem->GetPartyMembers().ContainsByPredicate(
				[](const FEasyPartyMemberInfo& Member) { return Member.bIsLeader && Member.bIsReady; }));

			FEasySessionTestAccess::AddKickedPartyPlayer(*Subsystem, Kicked);
			CurrentTest->TestFalse(TEXT("A kicked player is refused"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Kicked, Reason));
			CurrentTest->TestFalse(TEXT("The refusal says why"), Reason.IsEmpty());

			FEasySessionTestAccess::AddPartyMember(*Subsystem, MakePlayerId(State, TEXT("EasySessionPartyMemberA")));
			FEasySessionTestAccess::AddPartyMember(*Subsystem, MakePlayerId(State, TEXT("EasySessionPartyMemberB")));
			CurrentTest->TestEqual(TEXT("The members are listed"), Subsystem->GetPartyMembers().Num(), 3);
			CurrentTest->TestFalse(TEXT("A full party is refused"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Stranger, Reason));

			FEasyPartyMemberInfo StrangerInfo;
			StrangerInfo.PlayerId = Stranger;
			CurrentTest->TestEqual(TEXT("A player who is not connected cannot be kicked"), Subsystem->KickPartyMember(StrangerInfo, FText::GetEmpty()), EEasySessionResult::InvalidParams);

			FEasyPartyMemberInfo LeaderInfo;
			LeaderInfo.PlayerId = LeaderId;
			CurrentTest->TestEqual(TEXT("The leader cannot kick themselves"), Subsystem->KickPartyMember(LeaderInfo, FText::GetEmpty()), EEasySessionResult::InvalidParams);

			FEasySessionSearchParams Search;
			Search.bLANQuery = true;
			Subsystem->FindParties(Search, MakeFindCallback(State));
			NextPhase(State);
			return false;
		}

		case 2:
		{
			if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
			{
				return TimedOut(State, TEXT("the party search"));
			}

			// The party and a game session both answer, as they do on NULL, which ignores the query settings.
			FOnlineSessionSearchResult Party = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem, NAME_PartySession);
			FOnlineSessionSearchResult GameSession = Party;
			GameSession.Session.SessionSettings.Set(EasySession::SettingKey_Party, 0, EOnlineDataAdvertisementType::ViaOnlineService);

			NextPhase(State);
			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { Party, GameSession });
			return false;
		}

		case 3:
		{
			if (!State->FoundCount.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the party search result"));
			}

			CurrentTest->TestEqual(TEXT("A search for parties returns only the party"), State->FoundCount.GetValue(), 1);

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 4:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the public party leave"));
			}

			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::InviteOnly), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 5:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the invite-only party create"));
			}

			CurrentTest->TestFalse(TEXT("An invite-only party refuses a player it does not expect"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Stranger, Reason));
			FEasySessionTestAccess::AllowPartyPlayer(*Subsystem, Stranger);
			CurrentTest->TestTrue(TEXT("An invite-only party admits an invited player"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Stranger, Reason));

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		default:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInParty())
			{
				return TimedOut(State, TEXT("the invite-only party leave"));
			}

			End(State);
			return true;
		}
	}
}

/**
 * The leader admits a player unless they are a member already, were kicked, would overfill the party, or were not invited to an invite-only party.
 * Only the leader kicks, and only a connected member.
 * A search for parties skips game sessions even when the online subsystem returns them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyAdmissionTest, "EasySession.Party.LeaderDecidesWhoJoins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyAdmissionTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyAdmissionStep(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPartyMoveStep, TSharedPtr<EasySessionPartyTest::FTestState>, State);
bool FEasySessionPartyMoveStep::Update()
{
	using namespace EasySessionPartyTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	const FUniqueNetIdRepl MemberA = MakePlayerId(State, TEXT("EasySessionPartyMoverA"));
	const FUniqueNetIdRepl MemberB = MakePlayerId(State, TEXT("EasySessionPartyMoverB"));

	switch (State->Phase)
	{
		case 0:
		{
			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::Public), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the party create"));
			}

			FEasySessionTestAccess::AddPartyMember(*Subsystem, MemberA);
			FEasySessionTestAccess::AddPartyMember(*Subsystem, MemberB);

			// A member of someone else's party, which is what clearing the leader's host flag makes the local player.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false, NAME_PartySession);
			TOptional<EEasySessionResult> MemberResult;
			const FEasySessionCompleteDelegate StoreMemberResult = FEasySessionCompleteDelegate::CreateLambda([&MemberResult](EEasySessionResult Result, const FString&) { MemberResult = Result; });

			Subsystem->CreateSession(MakeHostParams(), StoreMemberResult);
			CurrentTest->TestTrue(TEXT("A member cannot create a session alone"), MemberResult.IsSet() && MemberResult.GetValue() == EEasySessionResult::InParty);
			MemberResult.Reset();

			Subsystem->JoinSession(FEasySessionSearchResult(), FString(), FString(), StoreMemberResult);
			CurrentTest->TestTrue(TEXT("A member cannot join a session alone"), MemberResult.IsSet() && MemberResult.GetValue() == EEasySessionResult::InParty);
			MemberResult.Reset();

			Subsystem->StartMatchmaking(FEasyMatchmakingParams(), nullptr, StoreMemberResult);
			CurrentTest->TestTrue(TEXT("A member cannot matchmake alone"), MemberResult.IsSet() && MemberResult.GetValue() == EEasySessionResult::InParty);

			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, true, NAME_PartySession);

			FEasySessionHostParams TooSmall = MakeHostParams();
			TooSmall.MaxPlayers = 2;
			Subsystem->CreateSession(TooSmall, MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 2:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create that is too small"));
			}

			CurrentTest->TestEqual(TEXT("A session too small for the party is refused"), State->PendingResult.GetValue(), EEasySessionResult::InvalidParams);
			CurrentTest->TestTrue(TEXT("The party stays"), Subsystem->IsInParty());

			FEasyMatchmakingParams Params;
			Params.Search.bLANQuery = true;
			Params.MaxSearchPasses = 1;
			Params.DelayBetweenPassesSeconds = 0.0f;
			Params.bAllowHostFallback = false;
			Subsystem->StartMatchmaking(Params, nullptr, MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 3:
		{
			if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
			{
				return TimedOut(State, TEXT("the leader's search"));
			}

			if (const TSharedPtr<FEasySessionMatchmakingRequest> Run = FEasySessionTestAccess::GetMatchmakingRequest(*Subsystem))
			{
				CurrentTest->TestEqual(TEXT("The leader looks for room for the whole party"), FEasySessionTestAccess::GetMatchmakingParams(*Run).Search.MinOpenSlots, 3);
			}

			NextPhase(State);
			Subsystem->CancelMatchmaking();
			return false;
		}

		case 4:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the canceled run"));
			}

			Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 5:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the leader's create"));
			}

			CurrentTest->TestEqual(TEXT("The leader creates a session for the party"), State->PendingResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestFalse(TEXT("Entering the game session closes the party"), Subsystem->IsInParty());
			CurrentTest->TestTrue(TEXT("The party ends as moved to a game session"), State->Listener->PartyLeftReasons.Contains(EEasyPartyLeaveReason::MovedToGameSession));

			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			CurrentTest->TestTrue(TEXT("The host's reservation holds the first member"), FEasySessionTestAccess::PlayerHasReservation(*Subsystem, MemberA));
			CurrentTest->TestTrue(TEXT("The host's reservation holds the second member"), FEasySessionTestAccess::PlayerHasReservation(*Subsystem, MemberB));

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

			End(State);
			return true;
		}
	}
}

/**
 * A party member cannot create, join or matchmake alone, and the leader brings the whole party.
 * The leader's matchmaking looks for room for every member, and a session too small for the party is refused.
 * The session the leader creates reserves a place for every member and closes the party.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyMoveTest, "EasySession.Party.TheLeaderBringsTheParty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyMoveTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyMoveStep(State));
	return true;
}
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPartyRestoreStep, TSharedPtr<EasySessionPartyTest::FTestState>, State);
bool FEasySessionPartyRestoreStep::Update()
{
	using namespace EasySessionPartyTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	const FUniqueNetIdRepl Member = MakePlayerId(State, TEXT("EasySessionPartyReturner"));
	FText Reason;

	switch (State->Phase)
	{
		case 0:
		{
			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::InviteOnly), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 1:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the invite-only party create"));
			}

			FEasySessionTestAccess::AddPartyMember(*Subsystem, Member);

			// A map change destroys the beacon and keeps the party session.
			FEasySessionTestAccess::DestroyPartyBeacon(*Subsystem);
			FEasySessionTestAccess::FinishMapLoad(*Subsystem);
			CurrentTest->TestNotNull(TEXT("The leader starts the party beacon again in the new map"), FEasySessionTestAccess::GetPartyBeacon(*Subsystem));
			CurrentTest->TestEqual(TEXT("The new member list holds the leader until the members log in again"), Subsystem->GetPartyMembers().Num(), 1);
			CurrentTest->TestTrue(TEXT("An invite-only party admits its members again"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Member, Reason));

			Subsystem->CreateSession(MakeHostParams(), MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 2:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the leader's create"));
			}

			CurrentTest->TestFalse(TEXT("The game session closes the party"), Subsystem->IsInParty());
			CurrentTest->TestEqual(TEXT("The party of the last match keeps the member"), FEasySessionTestAccess::GetLastPartyMemberCount(*Subsystem), 1);

			FEasySessionTestAccess::FinishMapLoad(*Subsystem);
			CurrentTest->TestFalse(TEXT("A map with a game session gets no party back"), Subsystem->IsRestoringParty());

			Subsystem->DestroySession(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 3:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the end of the match"));
			}

			FEasySessionTestAccess::FinishMapLoad(*Subsystem);
			CurrentTest->TestTrue(TEXT("Back in a map without a game session the party comes back"), Subsystem->IsRestoringParty());
			NextPhase(State);
			return false;
		}

		case 4:
		{
			if (Subsystem->IsRestoringParty() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the party to come back"));
			}

			CurrentTest->TestTrue(TEXT("The leader leads the party again"), Subsystem->IsInParty() && Subsystem->IsPartyLeader());
			CurrentTest->TestTrue(TEXT("The party of the last match admits its member again"), FEasySessionTestAccess::AskApproveMember(*Subsystem, Member, Reason));
			CurrentTest->TestEqual(TEXT("Nothing is left to get back"), FEasySessionTestAccess::GetLastPartyMemberCount(*Subsystem), -1);

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		case 5:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the restored party leave"));
			}

			// A member of a leader who stays in the match longer, which nothing in this world answers.
			FEasySessionTestAccess::SetLastPartyLeader(*Subsystem, MakePlayerId(State, TEXT("EasySessionPartyAbsentLeader")));
			FEasySessionTestAccess::FinishMapLoad(*Subsystem);
			CurrentTest->TestTrue(TEXT("A member looks for the leader's party"), Subsystem->IsRestoringParty());

			Subsystem->CreateParty(MakePartyParams(EEasyPartyPrivacy::Public), MakeCallback(State));
			CurrentTest->TestFalse(TEXT("A party the player creates stops the restore"), Subsystem->IsRestoringParty());
			NextPhase(State);
			return false;
		}

		case 6:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the player's own party"));
			}

			CurrentTest->TestEqual(TEXT("The player's own party is created"), State->PendingResult.GetValue(), EEasySessionResult::Success);

			Subsystem->LeaveParty(MakeCallback(State));
			NextPhase(State);
			return false;
		}

		default:
		{
			if (!State->PendingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInParty())
			{
				return TimedOut(State, TEXT("the last party leave"));
			}

			End(State);
			return true;
		}
	}
}

/**
 * A map change destroys the party beacon, and the leader starts it again with the members it admitted.
 * The party that entered a game session comes back in the next map without one, and an invite-only party admits its members again.
 * A party the player creates while a member looks for the leader stops that restore.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyRestoreTest, "EasySession.Party.ThePartyComesBackAfterAMatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyRestoreTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (Begin(State, *this) == nullptr)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyRestoreStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
