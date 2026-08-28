// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSettings.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionQueueTimeoutTest
{
	/** Request timeout used for the test - short enough to keep the test quick. */
	static constexpr float TestTimeoutSeconds = 1.0f;

	/** Hard limit on how long the test waits before failing. */
	static constexpr double MaxWaitSeconds = 15.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> StartResult;
		TOptional<EEasySessionResult> DestroyResult;
		float OriginalTimeout = 30.0f;
		double WaitStartTime = 0.0;
	};
}

/**
 * A failing request must not stall the queue - the next request still runs.
 *
 * This covers the drain behavior only. The watchdog's own timeout rules are
 * verified separately in EasySessionRequestTimeoutTest, because the NULL
 * subsystem completes every operation synchronously and therefore cannot
 * simulate a service that never answers.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForQueueDrain, TSharedPtr<EasySessionQueueTimeoutTest::FTestState>, State);
bool FEasySessionWaitForQueueDrain::Update()
{
	using namespace EasySessionQueueTimeoutTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	const bool bFinished = State->DestroyResult.IsSet();
	const bool bTimedOut = (FPlatformTime::Seconds() - State->WaitStartTime) > MaxWaitSeconds;

	if (!bFinished && !bTimedOut)
	{
		return false;
	}

	if (CurrentTest != nullptr)
	{
		// Draining is not enough on its own - the failed request must still have answered
		// its caller, or a node would hang exactly when the watchdog was meant to save it.
		CurrentTest->TestTrue(TEXT("The failed request answered its caller"), State->StartResult.IsSet());
		if (State->StartResult.IsSet())
		{
			CurrentTest->TestEqual(TEXT("And with the result its state deserved"), State->StartResult.GetValue(), EEasySessionResult::NoSessionExists);
		}

		CurrentTest->TestTrue(TEXT("The queue kept draining and the follow-up request completed"), bFinished);

		if (UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>())
		{
			CurrentTest->TestFalse(TEXT("The queue is idle once every request finished"), Subsystem->IsBusy());
		}
	}

	// Restore the project setting the test overrode.
	if (UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>())
	{
		Settings->RequestTimeoutSeconds = State->OriginalTimeout;
	}

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionQueueDrainTest, "EasySession.Subsystem.QueueDrainsAfterFailedRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionQueueDrainTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionQueueTimeoutTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();
	State->WaitStartTime = FPlatformTime::Seconds();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Shorten the watchdog so a stalled request fails quickly.
	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->OriginalTimeout = Settings->RequestTimeoutSeconds;
	Settings->RequestTimeoutSeconds = TestTimeoutSeconds;

	// Queue a request that fails immediately (no session to start) followed by a
	// destroy: whatever happens to the first one, the queue must reach the second.
	Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->StartResult = Result;
		}));

	Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->DestroyResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForQueueDrain(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
