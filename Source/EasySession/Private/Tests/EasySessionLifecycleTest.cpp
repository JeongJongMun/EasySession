// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionLifecycleTest
{
	/** Maximum time to wait for the queued operations before failing the test. */
	static constexpr double TimeoutSeconds = 15.0;

	/** State shared between the test body and its latent commands. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> CreateResult;
		TOptional<EEasySessionResult> StartResult;
		TOptional<EEasySessionResult> EndResult;
		TOptional<EEasySessionResult> DestroyResult;
		EEasySessionState StateAfterCreate = EEasySessionState::NoSession;
		EEasySessionState StateAfterStart = EEasySessionState::NoSession;
		EEasySessionState StateAfterEnd = EEasySessionState::NoSession;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForLifecycle, TSharedPtr<EasySessionLifecycleTest::FTestState>, State);
bool FEasySessionWaitForLifecycle::Update()
{
	using namespace EasySessionLifecycleTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->DestroyResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the session lifecycle operations to complete."));
			State->GameInstance->Shutdown();
			return true;
		}
		return false;
	}

	CurrentTest->TestEqual(TEXT("Create result"), State->CreateResult.Get(EEasySessionResult::UnknownFailure), EEasySessionResult::Success);
	CurrentTest->TestEqual(TEXT("Start result"), State->StartResult.Get(EEasySessionResult::UnknownFailure), EEasySessionResult::Success);
	CurrentTest->TestEqual(TEXT("End result"), State->EndResult.Get(EEasySessionResult::UnknownFailure), EEasySessionResult::Success);
	CurrentTest->TestEqual(TEXT("Destroy result"), State->DestroyResult.Get(EEasySessionResult::UnknownFailure), EEasySessionResult::Success);

	CurrentTest->TestEqual(TEXT("Pending after create"), State->StateAfterCreate, EEasySessionState::Pending);
	CurrentTest->TestEqual(TEXT("InProgress after start"), State->StateAfterStart, EEasySessionState::InProgress);
	CurrentTest->TestEqual(TEXT("Ended after end"), State->StateAfterEnd, EEasySessionState::Ended);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (Subsystem != nullptr)
	{
		CurrentTest->TestEqual(TEXT("NoSession after destroy"), Subsystem->GetSessionState(), EEasySessionState::NoSession);
	}

	State->GameInstance->Shutdown();
	return true;
}

/**
 * Lifecycle test: create, start, end and destroy are enqueued in a single frame and
 * the session state must transition Pending -> InProgress -> Ended -> NoSession.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionLifecycleTest, "EasySession.Subsystem.StartEndLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionLifecycleTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		State->GameInstance->Shutdown();
		return false;
	}

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Lifecycle Test");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;

	Subsystem->CreateEasySession(HostParams, FEasySessionCompleteDelegate::CreateLambda(
		[State, Subsystem](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
			State->StateAfterCreate = Subsystem->GetSessionState();
		}));

	Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State, Subsystem](EEasySessionResult Result, const FString&)
		{
			State->StartResult = Result;
			State->StateAfterStart = Subsystem->GetSessionState();
		}));

	Subsystem->EndEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State, Subsystem](EEasySessionResult Result, const FString&)
		{
			State->EndResult = Result;
			State->StateAfterEnd = Subsystem->GetSessionState();
		}));

	Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->DestroyResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForLifecycle(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
