// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Nodes/EasyCreateSessionNode.h"
#include "Nodes/EasyLeaveSessionNode.h"
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
			State->GameInstance->Shutdown();
			return true;
		}
		return false;
	}

	CurrentTest->TestTrue(TEXT("In session after create node"), Subsystem->IsInSession());
	CurrentTest->TestTrue(TEXT("Statics report in-session"), UEasySessionStatics::IsInEasySession(State->GameInstance.Get()));
	CurrentTest->TestTrue(TEXT("Statics report host"), UEasySessionStatics::IsEasySessionHost(State->GameInstance.Get()));

	// Leave the session through the leave node, exactly as a Blueprint graph would.
	UEasyLeaveSessionNode* LeaveNode = UEasyLeaveSessionNode::LeaveEasySession(State->GameInstance.Get());
	LeaveNode->Activate();

	State->StartTime = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitNodeLeave, TSharedPtr<EasySessionNodesTest::FTestState>, State);
bool FEasySessionWaitNodeLeave::Update()
{
	using namespace EasySessionNodesTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (Subsystem->IsBusy() || Subsystem->IsInSession())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the leave node to complete."));
			State->GameInstance->Shutdown();
			return true;
		}
		return false;
	}

	CurrentTest->TestFalse(TEXT("Not in session after leave node"), Subsystem->IsInSession());
	CurrentTest->TestFalse(TEXT("Statics report not in-session"), UEasySessionStatics::IsInEasySession(State->GameInstance.Get()));

	State->GameInstance->Shutdown();
	return true;
}

/**
 * Node plumbing test: drive the create and leave async nodes exactly like a Blueprint
 * graph would (factory function + Activate) and verify the session state transitions.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNodesTest, "EasySession.Nodes.CreateAndLeave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNodesTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionNodesTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Nodes Test");
	HostParams.bIsLANMatch = true;

	UEasyCreateSessionNode* CreateNode = UEasyCreateSessionNode::CreateEasySession(State->GameInstance.Get(), HostParams);
	if (!TestNotNull(TEXT("Create node was constructed"), CreateNode))
	{
		State->GameInstance->Shutdown();
		return false;
	}
	CreateNode->Activate();

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitNodeCreate(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitNodeLeave(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
