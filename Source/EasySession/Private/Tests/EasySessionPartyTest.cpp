// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionPartyBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
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

	FEasySessionHostParams MakeHostParams()
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = TEXT("EasySession Party");
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		return HostParams;
	}

	/** @return The integer the party session advertises under this key, or -1 when it advertises none. */
	int32 GetPartySettingInt(UEasySessionSubsystem& Subsystem, FName Key)
	{
		const FOnlineSessionSearchResult Result = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(Subsystem, NAME_PartySession);
		int32 Value = -1;
		Result.Session.SessionSettings.Get(Key, Value);
		return Value;
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
			FEasyPartyParams Params;
			Params.MaxMembers = 3;
			Params.Privacy = EEasyPartyPrivacy::JoinCode;
			Subsystem->CreateParty(Params, MakeCallback(State));
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

			FEasySessionSearchParams Search;
			Search.bLANQuery = true;
			Subsystem->FindSessions(Search, FEasySessionFindCompleteDelegate::CreateLambda(
				[Found = State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
				{
					Found->PendingResult = Result;
					Found->FoundCount = Results.Num();
				}));
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
			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem, NAME_PartySession) });
			NextPhase(State);
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

			FEasyPartyParams TooSmall;
			TooSmall.MaxMembers = 1;
			Subsystem->CreateParty(TooSmall, MakeCallback(State));
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
 * A party is created with the local player as its leader and only member, and leaving it closes the party beacon.
 * A second party, a party of one and a party inside a game session are refused.
 * A search for game sessions skips the party even when the online subsystem returns it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyLifecycleTest, "EasySession.Party.CreateAndLeave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// A party needs a logged in leader, and a headless test world starts with nobody logged in.
	const IOnlineIdentityPtr Identity = GetIdentity(State);
	if (Identity.IsValid() && !Identity->GetUniquePlayerId(0).IsValid())
	{
		State->bLoggedIn = Identity->Login(0, FOnlineAccountCredentials(FString(), TEXT("EasySessionPartyTest"), FString()));
	}
	if (!TestTrue(TEXT("A player is logged in"), Identity.IsValid() && Identity->GetUniquePlayerId(0).IsValid()))
	{
		End(State);
		return false;
	}

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyLifecycleStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
