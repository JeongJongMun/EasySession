// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionRequestFlowTest
{
	/** Maximum time to wait for the canceled run to complete. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The result the requester's delegate received. */
		TOptional<EEasySessionResult> MatchmakingResult;

		/** Was IsMatchmakingRunning still true inside the requester's delegate. */
		bool bRunningInsideCompletion = true;

		/** Was IsBusy still true inside the requester's delegate. */
		bool bBusyInsideCompletion = true;

		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitCanceledMatchmaking, TSharedPtr<EasySessionRequestFlowTest::FTestState>, State);
bool FEasySessionWaitCanceledMatchmaking::Update()
{
	using namespace EasySessionRequestFlowTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->MatchmakingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the canceled matchmaking to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	CurrentTest->TestEqual(TEXT("The run completed as Canceled"), State->MatchmakingResult.GetValue(), EEasySessionResult::Canceled);
	CurrentTest->TestFalse(TEXT("The run no longer counts as running inside the requester's delegate"), State->bRunningInsideCompletion);
	CurrentTest->TestFalse(TEXT("Is Busy no longer counts the run inside the requester's delegate"), State->bBusyInsideCompletion);
	CurrentTest->TestNull(TEXT("Get Active Matchmaking Policy is null after the run"), Subsystem->GetActiveMatchmakingPolicy());
	CurrentTest->TestEqual(TEXT("Get Activity is None after the run"), Subsystem->GetActivity(), EEasySessionActivity::None);

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * Matchmaking and the friend search are requests like Create and Join.
 * A queued matchmaking makes the subsystem busy, sets the activity to Matchmaking, is listed by GetQueueStatus and refuses a second run.
 * A friend search on NULL is refused inside the call, so it leaves the subsystem idle.
 * Both stop counting as running before the requester is notified.
 * A matchmaking canceled before the queue starts it completes with Canceled, and no policy or activity remains afterward.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionRequestFlowTest, "EasySession.Subsystem.RequestsDriveBusyAndActivity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionRequestFlowTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionRequestFlowTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	TestFalse(TEXT("The subsystem starts idle"), Subsystem->IsBusy());

	// NULL has no friends interface, so the search finishes inside the call.
	bool bFriendSearchRunningInsideCompletion = true;
	Subsystem->FindFriendSessions(FEasyFriendSessionsCompleteDelegate::CreateLambda(
		[Subsystem, &bFriendSearchRunningInsideCompletion](EEasySessionResult, const FString&, const TArray<FEasyFriendSession>&)
		{
			bFriendSearchRunningInsideCompletion = FEasySessionTestAccess::HasRequest(*Subsystem, FEasySessionRequest::EType::FriendSessions);
		}));
	TestFalse(TEXT("The friend search no longer counts as running inside its completion"), bFriendSearchRunningInsideCompletion);
	TestFalse(TEXT("A finished friend search does not make the subsystem busy"), Subsystem->IsBusy());

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	TWeakPtr<FTestState> WeakState = State;
	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[WeakState, Subsystem](EEasySessionResult Result, const FString&)
		{
			if (const TSharedPtr<FTestState> Pinned = WeakState.Pin())
			{
				Pinned->MatchmakingResult = Result;
				Pinned->bRunningInsideCompletion = Subsystem->IsMatchmakingRunning();
				Pinned->bBusyInsideCompletion = Subsystem->IsBusy();
			}
		}));

	TestTrue(TEXT("Matchmaking is running"), Subsystem->IsMatchmakingRunning());
	TestNotNull(TEXT("Get Active Matchmaking Policy returns the run's policy"), Subsystem->GetActiveMatchmakingPolicy());
	TestTrue(TEXT("A running matchmaking makes the subsystem busy"), Subsystem->IsBusy());
	TestEqual(TEXT("Get Activity names the matchmaking, not its search sub-request"), Subsystem->GetActivity(), EEasySessionActivity::Matchmaking);
	TestTrue(TEXT("Get Queue Status lists the matchmaking"), Subsystem->GetQueueStatus().Contains(TEXT("Matchmaking")));

	TOptional<EEasySessionResult> SecondResult;
	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[&SecondResult](EEasySessionResult Result, const FString&)
		{
			SecondResult = Result;
		}));
	TestTrue(TEXT("A second run is refused inside the call"), SecondResult.IsSet() && SecondResult.GetValue() == EEasySessionResult::MatchmakingAlreadyInProgress);

	Subsystem->CancelMatchmaking();

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitCanceledMatchmaking(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
