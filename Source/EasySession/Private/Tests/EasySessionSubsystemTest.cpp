// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSubsystemTest
{
	/** Maximum time to wait for the queued operations before failing the test. */
	static constexpr double TimeoutSeconds = 15.0;

	/** State shared between the test body and its latent commands. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> CreateResult;
		TOptional<EEasySessionResult> UpdateResult;
		TOptional<EEasySessionResult> DestroyResult;
		TArray<FString> CompletionOrder;
		bool bWasInSessionAfterCreate = false;
		bool bWasHostAfterCreate = false;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForQueue, TSharedPtr<EasySessionSubsystemTest::FTestState>, State);
bool FEasySessionWaitForQueue::Update()
{
	using namespace EasySessionSubsystemTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->DestroyResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the queued session operations to complete."));
			State->GameInstance->Shutdown();
			return true;
		}
		return false;
	}

	CurrentTest->TestTrue(TEXT("Create completed"), State->CreateResult.IsSet());
	CurrentTest->TestTrue(TEXT("Update completed"), State->UpdateResult.IsSet());

	if (State->CreateResult.IsSet())
	{
		CurrentTest->TestEqual(TEXT("Create result"), State->CreateResult.GetValue(), EEasySessionResult::Success);
	}
	if (State->UpdateResult.IsSet())
	{
		CurrentTest->TestEqual(TEXT("Update result"), State->UpdateResult.GetValue(), EEasySessionResult::Success);
	}
	CurrentTest->TestEqual(TEXT("Destroy result"), State->DestroyResult.GetValue(), EEasySessionResult::Success);

	CurrentTest->TestTrue(TEXT("Was in session after create"), State->bWasInSessionAfterCreate);
	CurrentTest->TestTrue(TEXT("Was host after create"), State->bWasHostAfterCreate);

	const TArray<FString> ExpectedOrder = { TEXT("Create"), TEXT("Update"), TEXT("Destroy") };
	CurrentTest->TestEqual(TEXT("Operations completed strictly in enqueue order"), State->CompletionOrder, ExpectedOrder);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	CurrentTest->TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (Subsystem != nullptr)
	{
		CurrentTest->TestFalse(TEXT("Not in session after destroy"), Subsystem->IsInSession());
		CurrentTest->TestFalse(TEXT("Not busy after queue drained"), Subsystem->IsBusy());
	}

	State->GameInstance->Shutdown();
	return true;
}

/**
 * Queue test: create, update and destroy are enqueued in a single frame and must
 * execute strictly in order, each completing successfully.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionQueueTest, "EasySession.Subsystem.QueuedCreateUpdateDestroy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionQueueTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSubsystemTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Queue Test");
	HostParams.MaxPlayers = 4;
	HostParams.bIsLANMatch = true;

	Subsystem->CreateEasySession(HostParams, FEasySessionCompleteDelegate::CreateLambda(
		[State, Subsystem](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->CreateResult = Result;
			State->CompletionOrder.Add(TEXT("Create"));
			State->bWasInSessionAfterCreate = Subsystem->IsInSession();
			State->bWasHostAfterCreate = Subsystem->IsHost();
		}));

	FEasySessionHostParams UpdateParams = HostParams;
	UpdateParams.SessionDisplayName = TEXT("EasySession Queue Test (Updated)");
	UpdateParams.MaxPlayers = 8;

	Subsystem->UpdateEasySession(UpdateParams, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->UpdateResult = Result;
			State->CompletionOrder.Add(TEXT("Update"));
		}));

	Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->DestroyResult = Result;
			State->CompletionOrder.Add(TEXT("Destroy"));
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForQueue(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
