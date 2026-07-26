// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "Nodes/EasyCreateSessionNode.h"
#include "Nodes/EasyDestroySessionNode.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionNodesTest
{
	/** Maximum time to wait for the async nodes before failing the test. */
	static constexpr double TimeoutSeconds = 15.0;

	/** State shared between the test body and its latent commands. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitNodeCreate, TSharedPtr<EasySessionNodesTest::FTestState>, State);
bool FEasySessionWaitNodeCreate::Update()
{
	using namespace EasySessionNodesTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (Subsystem->IsBusy())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the create node to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	CurrentTest->TestTrue(TEXT("In session after create node"), Subsystem->IsInSession());
	CurrentTest->TestTrue(TEXT("Statics report in-session"), UEasySessionStatics::IsInEasySession(State->GameInstance.Get()));
	CurrentTest->TestTrue(TEXT("Statics report host"), UEasySessionStatics::IsEasySessionHost(State->GameInstance.Get()));

	// Destroy the session through the node, exactly as a Blueprint graph would.
	UEasyDestroySessionNode* DestroyNode = UEasyDestroySessionNode::DestroyEasySession(State->GameInstance.Get());
	DestroyNode->Activate();

	State->StartTime = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitNodeDestroy, TSharedPtr<EasySessionNodesTest::FTestState>, State);
bool FEasySessionWaitNodeDestroy::Update()
{
	using namespace EasySessionNodesTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (Subsystem->IsBusy() || Subsystem->IsInSession())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the destroy node to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	CurrentTest->TestFalse(TEXT("Not in session after destroy node"), Subsystem->IsInSession());
	CurrentTest->TestFalse(TEXT("Statics report not in-session"), UEasySessionStatics::IsInEasySession(State->GameInstance.Get()));

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * Node plumbing test: drive the create and destroy async nodes exactly like a Blueprint
 * graph would (factory function + Activate) and verify the session state transitions.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNodesTest, "EasySession.Nodes.CreateAndDestroy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNodesTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionNodesTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Nodes Test");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;

	UEasyCreateSessionNode* CreateNode = UEasyCreateSessionNode::CreateEasySession(State->GameInstance.Get(), HostParams);
	if (!TestNotNull(TEXT("Create node was constructed"), CreateNode))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}
	CreateNode->Activate();

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitNodeCreate(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitNodeDestroy(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
