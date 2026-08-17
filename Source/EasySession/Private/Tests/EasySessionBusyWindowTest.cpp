// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionBusyWindowTest
{
	/** Maximum time to wait for each queued operation before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> CreateResult;

		/** What Is Busy answered inside the completion callback, where a graph re-enables its buttons. */
		bool bBusyInsideCallback = true;

		/** Whether the travel failure was reported before the completion callback ran. */
		bool bFailureBeforeCallback = false;

		bool bCleanupIssued = false;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionCheckBusyWindow, TSharedPtr<EasySessionBusyWindowTest::FTestState>, State);
bool FEasySessionCheckBusyWindow::Update()
{
	using namespace EasySessionBusyWindowTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->CreateResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the create to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("Create succeeded"), State->CreateResult.GetValue(), EEasySessionResult::Success);

		// This world has no player controller, so the travel was aborted before the
		// completion callback ran. No map load follows, and the callback has to hear
		// that: Is Busy false, with the failure already reported.
		CurrentTest->TestFalse(TEXT("Is Busy is false in the callback when the travel was aborted"), State->bBusyInsideCallback);
		CurrentTest->TestTrue(TEXT("The travel failure was reported before the callback"), State->bFailureBeforeCallback);
		CurrentTest->TestFalse(TEXT("Is Busy is not stuck afterwards"), Subsystem->IsBusy());

		TSharedPtr<FTestState> CleanupState = State;
		Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
			[CleanupState](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
			{
			}));
		State->bCleanupIssued = true;
		State->StartTime = FPlatformTime::Seconds();
		return false;
	}

	if (Subsystem->IsBusy() || Subsystem->IsInSession())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the cleanup destroy."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * The completion callback hears the truth about the travel that follows.
 *
 * The travel is requested before the request completes, so a graph reading Is Busy on the
 * success pin sees true while a map load is coming. This world has no player controller,
 * which is the other side of that contract: the travel aborts first, the failure is
 * reported first, and the callback correctly reads Is Busy false with nothing coming.
 *
 * The happy path - Is Busy true through the callback while a travel is pending - needs a
 * player controller, so it stays on the on-device checklist.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBusyWindowTest, "EasySession.Subsystem.BusyThroughTheCompletionCallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBusyWindowTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionBusyWindowTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// The abort path reports through OnSessionFailure, and it has to arrive before the completion callback.
	TStrongObjectPtr<UEasySessionTestEventListener> Listener(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionFailure.AddDynamic(Listener.Get(), &UEasySessionTestEventListener::HandleSessionFailure);

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Busy Window Test");
	HostParams.bIsLANMatch = true;
	// A map name makes the create end in a travel, which is the case the busy contract is about.
	HostParams.MapName = TEXT("ES_BusyWindowTestMap");

	State->StartTime = FPlatformTime::Seconds();
	Subsystem->CreateEasySession(HostParams, FEasySessionCompleteDelegate::CreateLambda(
		[State, Subsystem, Listener](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->bBusyInsideCallback = Subsystem->IsBusy();
			State->bFailureBeforeCallback = Listener->FailureReasons.Num() > 0;
			State->CreateResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionCheckBusyWindow(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
