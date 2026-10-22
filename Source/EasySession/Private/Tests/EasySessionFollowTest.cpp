// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionMatchmakingRequest.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSessionSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionFollowTest
{
	// Maximum time to wait for each step before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The host's session to follow into. Its info is copied before it is destroyed, so the session the player then holds is a different one. */
		FOnlineSessionSearchResult HostResult;

		enum class EStep { AwaitingHostCreate, AwaitingHostDestroy, AwaitingCurrentCreate, AwaitingLeave, AwaitingSearch, AwaitingJoin, AwaitingCancel };
		EStep Step = EStep::AwaitingHostCreate;
		double StartTime = 0.0;
	};

	// Whether the step timed out. Reports the error and destroys the game instance when it did.
	bool TimedOut(TSharedPtr<FTestState> State, const TCHAR* What)
	{
		if (FPlatformTime::Seconds() - State->StartTime <= TimeoutSeconds)
		{
			return false;
		}

		FAutomationTestFramework::Get().GetCurrentTest()->AddError(FString::Printf(TEXT("Timed out waiting for %s."), What));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	// Move to the next step and restart its timeout.
	void MoveTo(TSharedPtr<FTestState> State, FTestState::EStep Step)
	{
		State->Step = Step;
		State->StartTime = FPlatformTime::Seconds();
	}

	void CreateSession(UEasySessionSubsystem& Subsystem, const TCHAR* DisplayName)
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = DisplayName;
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		Subsystem.CreateSession(HostParams);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionFollowStep, TSharedPtr<EasySessionFollowTest::FTestState>, State);
bool FEasySessionFollowStep::Update()
{
	using namespace EasySessionFollowTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	const TSharedPtr<FEasySessionMatchmakingRequest> Run = FEasySessionTestAccess::GetMatchmakingRequest(*Subsystem);

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingHostCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the host's create"));
			}

			// A password-protected host, which a follow still tries to join because the host holds a reservation for this player.
			State->HostResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			State->HostResult.Session.SessionSettings.Set(EasySession::SettingKey_PasswordProtected, 1, EOnlineDataAdvertisementType::ViaOnlineService);

			Subsystem->DestroySession();
			MoveTo(State, FTestState::EStep::AwaitingHostDestroy);
			return false;
		}

		case FTestState::EStep::AwaitingHostDestroy:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the host's destroy"));
			}

			// The session the player is in when it is told to follow the host.
			CreateSession(*Subsystem, TEXT("EasySession Follow Current"));
			MoveTo(State, FTestState::EStep::AwaitingCurrentCreate);
			return false;
		}

		case FTestState::EStep::AwaitingCurrentCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the current create"));
			}

			// The player joined this lobby rather than hosting it, as a player told to follow its host has.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);
			CurrentTest->TestFalse(TEXT("The player does not host the session it is in"), Subsystem->IsHost());

			Subsystem->FollowHost(State->HostResult.Session.OwningUserId, true);
			MoveTo(State, FTestState::EStep::AwaitingLeave);
			return false;
		}

		case FTestState::EStep::AwaitingLeave:
		{
			// The player leaves the lobby first, and then the follow runs.
			if (Subsystem->IsInSession() || !Run.IsValid() || FEasySessionTestAccess::IsWaitingToTravelToGameDefaultMap(*Subsystem))
			{
				return TimedOut(State, TEXT("the player to leave the lobby"));
			}

			CurrentTest->TestTrue(TEXT("The player travels to the Game Default Map"), FEasySessionTestAccess::IsTraveling(*Subsystem));
			CurrentTest->TestFalse(TEXT("Leaving to follow records no disconnect"), Subsystem->HasPendingDisconnectInfo());
			MoveTo(State, FTestState::EStep::AwaitingSearch);
			return false;
		}

		case FTestState::EStep::AwaitingSearch:
		{
			// The queue starts the run on its next tick, and the run then starts its first search.
			if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
			{
				if (Run.IsValid())
				{
					return TimedOut(State, TEXT("the follow's first search"));
				}

				CurrentTest->AddError(TEXT("The follow ended before it searched."));
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			if (Run.IsValid())
			{
				const FEasyMatchmakingParams& Params = FEasySessionTestAccess::GetMatchmakingParams(*Run);
				CurrentTest->TestTrue(TEXT("It searches for the host's session only"), Params.Search.OwnerId == State->HostResult.Session.OwningUserId);
				// A follow ends on its time limit rather than on a pass count, because the host's map load decides how many passes it needs.
				CurrentTest->TestTrue(TEXT("It keeps searching until its time limit"), Params.MaxSearchPasses > 1);
				CurrentTest->TestFalse(TEXT("It never hosts a session of its own"), Params.bAllowHostFallback);
			}

			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->HostResult });
			MoveTo(State, FTestState::EStep::AwaitingJoin);
			return false;
		}

		case FTestState::EStep::AwaitingJoin:
		{
			// No reservation beacon answers for the copied host session, so the join is refused and the run moves on to its next pass.
			if (!Run.IsValid() || FEasySessionTestAccess::GetFailedJoinCount(*Run) < 1)
			{
				return TimedOut(State, TEXT("the follow's join"));
			}

			CurrentTest->TestTrue(TEXT("The password-protected host was a candidate and its join ran"), FEasySessionTestAccess::GetFailedJoinCount(*Run) == 1);
			CurrentTest->TestFalse(TEXT("The player is in no session while it follows"), Subsystem->IsInSession());

			Subsystem->CancelMatchmaking();
			MoveTo(State, FTestState::EStep::AwaitingCancel);
			return false;
		}

		case FTestState::EStep::AwaitingCancel:
		{
			if (Run.IsValid())
			{
				return TimedOut(State, TEXT("the follow to be canceled"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A player of the group follows its host with a matchmaking run that searches for that one host and joins it.
 * A player in the lobby leaves it first and travels to the Game Default Map, where the follow searches.
 * The old host leaving then never reaches the player, so nothing but the follow decides where the player goes.
 * A password-protected host is still tried, because the reservation the party host or the game host asked for lets the player in without the password.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFollowTest, "EasySession.Group.AFollowSearchesForTheHostAndJoins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFollowTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFollowTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	CreateSession(*Subsystem, TEXT("EasySession Follow Host"));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionFollowStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
