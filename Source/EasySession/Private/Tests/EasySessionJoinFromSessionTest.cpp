// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSessionSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionJoinFromSessionTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The session to join. Its info is copied before it is destroyed, so the second session gets a different session id. */
		FEasySessionSearchResult TargetResult;

		TOptional<EEasySessionResult> JoinResult;

		enum class EStep { AwaitingTargetCreate, AwaitingTargetDestroy, AwaitingCurrentCreate, AwaitingJoin };
		EStep Step = EStep::AwaitingTargetCreate;
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

	/** Create a session this test can leave or join, named after the step that needs it. */
	void CreateSession(UEasySessionSubsystem& Subsystem, const TCHAR* DisplayName)
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = DisplayName;
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		Subsystem.CreateSession(HostParams);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionJoinFromSessionStep, TSharedPtr<EasySessionJoinFromSessionTest::FTestState>, State);
bool FEasySessionJoinFromSessionStep::Update()
{
	using namespace EasySessionJoinFromSessionTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingTargetCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the target create"));
			}

			State->TargetResult.NativeResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted search result is joinable"), State->TargetResult.NativeResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			Subsystem->DestroySession();
			State->Step = FTestState::EStep::AwaitingTargetDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingTargetDestroy:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the target destroy"));
			}

			// The session the player is in when the join starts. The online subsystem gives it a new session id.
			CreateSession(*Subsystem, TEXT("EasySession Join From Session Test"));
			State->Step = FTestState::EStep::AwaitingCurrentCreate;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingCurrentCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the current create"));
			}

			CurrentTest->TestNotEqual(TEXT("The two sessions have different ids, so this is not a join of the session already held"),
				FEasySessionTestAccess::GetCurrentSessionIdString(*Subsystem), State->TargetResult.NativeResult.GetSessionIdStr());

			TSharedPtr<FTestState> Shared = State;
			Subsystem->JoinSession(State->TargetResult, FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString& /*ErrorMessage*/)
				{
					Shared->JoinResult = Result;
				}));

			State->Step = FTestState::EStep::AwaitingJoin;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingJoin:
		{
			if (!State->JoinResult.IsSet() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the join"));
			}

			CurrentTest->TestNotEqual(TEXT("The join is not refused for holding a session"),
				State->JoinResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestEqual(TEXT("It leaves the current session and fails on the address instead"),
				State->JoinResult.GetValue(), EEasySessionResult::ResolveFailure);
			CurrentTest->TestFalse(TEXT("No session is left behind"), Subsystem->IsInSession());

			// A player who left a session for a join that then failed has no map to stay on, so the request travels to the menu.
			CurrentTest->TestTrue(TEXT("The trip to the menu is on its way"), Subsystem->IsBusy());

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * Joining from inside another session leaves that session first, rather than refusing the join.
 * One node covers both cases, which is why an accepted invite needs no second node and no "may leave" flag.
 *
 * The target session is unreachable on purpose: created without listening, it advertises this process's address with port 0.
 * Its info is copied into a search result before it is destroyed, so the session the player then holds has a different session id.
 * That difference is what separates this from joining the session already held, which stays refused with SessionAlreadyExists.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinFromSessionTest, "EasySession.Join.LeavesTheCurrentSessionFirst", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinFromSessionTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionJoinFromSessionTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	CreateSession(*Subsystem, TEXT("EasySession Join Target Test"));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionJoinFromSessionStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
