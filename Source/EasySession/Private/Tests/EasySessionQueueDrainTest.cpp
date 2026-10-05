// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionQueueDrainTest
{
	/** Hard limit on how long the test waits before failing. */
	static constexpr double MaxWaitSeconds = 15.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> StartResult;
		TOptional<EEasySessionResult> DestroyResult;
		double WaitStartTime = 0.0;
	};
}

/**
 * A failing request must not block the queue.
 * The next request still runs.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForQueueDrain, TSharedPtr<EasySessionQueueDrainTest::FTestState>, State);
bool FEasySessionWaitForQueueDrain::Update()
{
	using namespace EasySessionQueueDrainTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	const bool bFinished = State->DestroyResult.IsSet();
	const bool bTimedOut = (FPlatformTime::Seconds() - State->WaitStartTime) > MaxWaitSeconds;

	if (!bFinished && !bTimedOut)
	{
		return false;
	}

	if (CurrentTest != nullptr)
	{
		// Moving on is not enough on its own.
		// The failed request must still notify its requester, or its node would never fire a pin.
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

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionQueueDrainTest, "EasySession.Subsystem.QueueDrainsAfterFailedRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionQueueDrainTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionQueueDrainTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);
	State->WaitStartTime = FPlatformTime::Seconds();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Queue a request that fails as soon as it runs (no session to start), followed by a destroy.
	// Whatever happens to the first one, the queue must reach the second.
	Subsystem->StartSession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->StartResult = Result;
		}));

	Subsystem->DestroySession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->DestroyResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForQueueDrain(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
