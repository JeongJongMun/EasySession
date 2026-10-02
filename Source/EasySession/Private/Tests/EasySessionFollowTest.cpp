// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The host's session to follow into. Its info is copied before it is destroyed, so the session the player then holds is a different one. */
		FOnlineSessionSearchResult HostResult;

		enum class EStep { AwaitingHostCreate, AwaitingHostDestroy, AwaitingCurrentCreate, AwaitingSearch, AwaitingJoin, AwaitingCancel, AwaitingCleanup };
		EStep Step = EStep::AwaitingHostCreate;
		double StartTime = 0.0;
	};

	/** @return Whether the step timed out, reporting it and taking the game instance down when it did. */
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

	/** Move to the next step and restart its timeout. */
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

			// A password-protected host, which a follow still joins because the host holds a reservation for this player.
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

			// The session the player is in when the leader moves them along.
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

			Subsystem->FollowHost(State->HostResult.Session.OwningUserId, true);
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

				CurrentTest->AddError(TEXT("The follow did not run while this player was in a session."));
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
			// No beacon runs for the copied host session, so the join is refused before leaving and the run moves on to its next pass.
			if (!Run.IsValid() || FEasySessionTestAccess::GetFailedJoinCount(*Run) < 1)
			{
				return TimedOut(State, TEXT("the follow's join"));
			}

			CurrentTest->TestTrue(TEXT("The password-protected host was a candidate and its join ran"), FEasySessionTestAccess::GetFailedJoinCount(*Run) == 1);
			CurrentTest->TestTrue(TEXT("The player is still in the session it was in"), Subsystem->IsInSession());

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

			// The canceled run kept the session, and the online subsystem outlives this world, so the next test would find it.
			Subsystem->DestroySession();
			MoveTo(State, FTestState::EStep::AwaitingCleanup);
			return false;
		}

		case FTestState::EStep::AwaitingCleanup:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the cleanup destroy"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A group member follows the leader with a matchmaking run that searches for one host and joins it.
 * The run starts while the member is still in a session, because its join leaves that session only once the host approved.
 * A password-protected host stays a candidate, because the reservation the leader asked for lets the member in without the password.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFollowTest, "EasySession.Group.AFollowSearchesForTheHostAndJoins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFollowTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFollowTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
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
