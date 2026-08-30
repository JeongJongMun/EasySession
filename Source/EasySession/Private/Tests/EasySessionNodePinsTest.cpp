// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "Nodes/EasyCreateSessionNode.h"
#include "Nodes/EasyDestroySessionNode.h"
#include "Nodes/EasyEndSessionNode.h"
#include "Nodes/EasyFindSessionsNode.h"
#include "Nodes/EasyJoinSessionNode.h"
#include "Nodes/EasyLeaveSessionNode.h"
#include "EasySessionTestAccess.h"
#include "Nodes/EasyMatchmakingNode.h"
#include "Nodes/EasyReadFriendsNode.h"
#include "Nodes/EasyStartSessionNode.h"
#include "Nodes/EasyUpdateSessionNode.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionNodePinsTest
{
	/** Maximum time to wait for one node before failing the test. */
	static constexpr double NodeTimeoutSeconds = 30.0;

	/** Which output pin a node is expected to fire. */
	enum class EExpectedPin : uint8
	{
		Success,
		Failure
	};

	/** One node to drive, and what its pins are expected to do. */
	struct FPinCase
	{
		/** Node name as a Blueprint author sees it, used in the assertion messages. */
		FString Label;

		/** Builds the node, binds both of its pins to the listener, and activates it. */
		TFunction<void(UGameInstance&, UEasySessionTestNodePinListener&)> Run;

		/** The pin this node has to fire. */
		EExpectedPin ExpectedPin = EExpectedPin::Failure;

		/** The result the pin has to carry. */
		EEasySessionResult ExpectedResult = EEasySessionResult::Success;
	};

	/** State shared between the test body and the latent command that walks the cases. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestNodePinListener> Listener;
		TArray<FPinCase> Cases;

		/** The case being run right now. */
		int32 CaseIndex = 0;

		/** Whether the current case has been started, so the latent command only activates it once. */
		bool bCaseStarted = false;

		double CaseStartTime = 0.0;
	};

	/** Host params that create a session without opening a listen server, so the test needs no network. */
	static FEasySessionHostParams MakeTestHostParams()
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = TEXT("EasySession Node Pins Test");
		HostParams.bIsLANMatch = true;
		HostParams.bStartListening = false;
		return HostParams;
	}
}

/**
 * Walks the case list one node at a time: activate, wait for a pin, assert, move on.
 *
 * One command for the whole list rather than one per node, because the queue can finish a
 * request in the same call or several ticks later, and a test should not have to know which.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionRunPinCases, TSharedPtr<EasySessionNodePinsTest::FTestState>, State);
bool FEasySessionRunPinCases::Update()
{
	using namespace EasySessionNodePinsTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->Cases.IsValidIndex(State->CaseIndex))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	const FPinCase& Case = State->Cases[State->CaseIndex];

	if (!State->bCaseStarted)
	{
		State->Listener->Reset();
		State->CaseStartTime = FPlatformTime::Seconds();
		State->bCaseStarted = true;
		Case.Run(*State->GameInstance, *State->Listener);
		return false;
	}

	if (State->Listener->TotalFired() == 0)
	{
		if (FPlatformTime::Seconds() - State->CaseStartTime > NodeTimeoutSeconds)
		{
			CurrentTest->AddError(FString::Printf(TEXT("%s: neither output pin fired. A Blueprint graph would wait here forever."), *Case.Label));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	CurrentTest->TestEqual(FString::Printf(TEXT("%s fires exactly one pin (saw %s)"), *Case.Label, *State->Listener->Describe()),
		State->Listener->TotalFired(), 1);

	switch (Case.ExpectedPin)
	{
	case EExpectedPin::Success:
		CurrentTest->TestEqual(FString::Printf(TEXT("%s fires On Success (saw %s)"), *Case.Label, *State->Listener->Describe()),
			State->Listener->SuccessResults.Num(), 1);
		if (State->Listener->SuccessResults.Num() == 1)
		{
			CurrentTest->TestEqual(FString::Printf(TEXT("%s reports the expected result"), *Case.Label),
				State->Listener->SuccessResults[0], Case.ExpectedResult);
		}
		break;

	case EExpectedPin::Failure:
		CurrentTest->TestEqual(FString::Printf(TEXT("%s fires On Failure (saw %s)"), *Case.Label, *State->Listener->Describe()),
			State->Listener->FailureResults.Num(), 1);
		if (State->Listener->FailureResults.Num() == 1)
		{
			CurrentTest->TestEqual(FString::Printf(TEXT("%s reports the expected result"), *Case.Label),
				State->Listener->FailureResults[0], Case.ExpectedResult);
		}
		break;
	}

	++State->CaseIndex;
	State->bCaseStarted = false;
	return false;
}

/**
 * Every async node routes a failure to its On Failure pin.
 *
 * Nothing is hosted, so each node hits its own guard clause. That is the cheapest way to
 * reach the failure pin of six nodes without a second machine or a real online service.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNodeFailurePinsTest, "EasySession.Nodes.FailurePinsFire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNodeFailurePinsTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionNodePinsTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();
	State->Listener = TStrongObjectPtr<UEasySessionTestNodePinListener>(NewObject<UEasySessionTestNodePinListener>());

	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), State->GameInstance->GetSubsystem<UEasySessionSubsystem>()))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Cases.Add({ TEXT("Destroy Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyDestroySessionNode* Node = UEasyDestroySessionNode::DestroyEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoSessionExists });

	State->Cases.Add({ TEXT("Start Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyStartSessionNode* Node = UEasyStartSessionNode::StartEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoSessionExists });

	State->Cases.Add({ TEXT("End Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyEndSessionNode* Node = UEasyEndSessionNode::EndEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoSessionExists });

	State->Cases.Add({ TEXT("Update Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyUpdateSessionNode* Node = UEasyUpdateSessionNode::UpdateEasySession(&GameInstance, MakeTestHostParams());
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoSessionExists });

	// An empty search result is what a graph passes when its server list row was never filled in.
	State->Cases.Add({ TEXT("Join Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyJoinSessionNode* Node = UEasyJoinSessionNode::JoinEasySession(&GameInstance, FEasySessionSearchResult());
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::InvalidParams });

	// Host fallback off and nothing on the LAN to join, so the run ends with nothing found.
	// This case must not leave a session behind - the success test that follows creates its own.
	State->Cases.Add({ TEXT("Start Easy Matchmaking"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			FEasyMatchmakingParams MatchmakingParams;
			MatchmakingParams.Search.bLANQuery = true;
			MatchmakingParams.Search.TimeoutSeconds = 5.0f;
			MatchmakingParams.bAllowHostFallback = false;
			MatchmakingParams.MaxSearchPasses = 1;
			MatchmakingParams.DelayBetweenPassesSeconds = 0.0f;
			UEasyMatchmakingNode* Node = UEasyMatchmakingNode::StartEasyMatchmaking(&GameInstance, MatchmakingParams);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoSessionsFound });

	// The NULL subsystem has no friends interface, so this one fails wherever the test runs.
	State->Cases.Add({ TEXT("Read Easy Friends"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyReadFriendsNode* Node = UEasyReadFriendsNode::ReadEasyFriends(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFriendsSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFriendsFailure);
			Node->Activate();
		}, EExpectedPin::Failure, EEasySessionResult::NoOnlineSubsystem });

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionRunPinCases(State));
	return true;
}

/**
 * Every async node routes a success to its On Success pin.
 *
 * The cases run in order and each one needs the session the previous one left behind, so
 * this is the session lifecycle a game walks: create, start the match, change it, end it, leave.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNodeSuccessPinsTest, "EasySession.Nodes.SuccessPinsFire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNodeSuccessPinsTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionNodePinsTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();
	State->Listener = TStrongObjectPtr<UEasySessionTestNodePinListener>(NewObject<UEasySessionTestNodePinListener>());

	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), State->GameInstance->GetSubsystem<UEasySessionSubsystem>()))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Cases.Add({ TEXT("Create Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyCreateSessionNode* Node = UEasyCreateSessionNode::CreateEasySession(&GameInstance, MakeTestHostParams());
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	// A LAN search finds nothing here and still succeeds, with an empty result array.
	// It is the slow case: the NULL subsystem answers only once its own LAN timeout runs out.
	State->Cases.Add({ TEXT("Find Easy Sessions"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			FEasySessionSearchParams SearchParams;
			SearchParams.TimeoutSeconds = 5.0f;
			UEasyFindSessionsNode* Node = UEasyFindSessionsNode::FindEasySessions(&GameInstance, SearchParams);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFindSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFindFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	State->Cases.Add({ TEXT("Start Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyStartSessionNode* Node = UEasyStartSessionNode::StartEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	State->Cases.Add({ TEXT("Update Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			FEasySessionHostParams HostParams = MakeTestHostParams();
			HostParams.SessionDisplayName = TEXT("EasySession Node Pins Test Renamed");
			UEasyUpdateSessionNode* Node = UEasyUpdateSessionNode::UpdateEasySession(&GameInstance, HostParams);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	State->Cases.Add({ TEXT("End Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyEndSessionNode* Node = UEasyEndSessionNode::EndEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	State->Cases.Add({ TEXT("Destroy Easy Session"),
		[](UGameInstance& GameInstance, UEasySessionTestNodePinListener& Listener)
		{
			UEasyDestroySessionNode* Node = UEasyDestroySessionNode::DestroyEasySession(&GameInstance);
			Node->OnSuccess.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(&Listener, &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();
		}, EExpectedPin::Success, EEasySessionResult::Success });

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionRunPinCases(State));
	return true;
}

namespace EasySessionLeaveNodeTest
{
	/** Maximum time to wait before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestNodePinListener> Listener;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForLeaveNode, TSharedPtr<EasySessionLeaveNodeTest::FTestState>, State);
bool FEasySessionWaitForLeaveNode::Update()
{
	using namespace EasySessionLeaveNodeTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d. Queue: %s"), State->Phase, *Subsystem->GetQueueStatus()));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	switch (State->Phase)
	{
		case 0:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			// The ticket's protagonist is the client: it created nothing and only wants out.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			UEasyLeaveSessionNode* Node = UEasyLeaveSessionNode::LeaveEasySession(State->GameInstance.Get());
			Node->OnSuccess.AddDynamic(State->Listener.Get(), &UEasySessionTestNodePinListener::HandleSuccess);
			Node->OnFailure.AddDynamic(State->Listener.Get(), &UEasySessionTestNodePinListener::HandleFailure);
			Node->Activate();

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (State->Listener->TotalFired() == 0)
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Leave fires exactly one pin"), State->Listener->TotalFired(), 1);
			CurrentTest->TestEqual(TEXT("And it is On Success"), State->Listener->SuccessResults.Num(), 1);
			CurrentTest->TestFalse(TEXT("The named session is gone"), Subsystem->IsInSession());
			// The menu map load was requested and has not resolved in this headless world - busy is the honest answer.
			CurrentTest->TestTrue(TEXT("The trip to the menu is on its way"), Subsystem->IsBusy());

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * Leave Easy Session is the whole exit: the named session is destroyed and the menu
 * map load is requested, in one node. Destroy Easy Session covers only the named
 * session, which left a client's leave button stranding the player on the host's map.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionLeaveNodeTest, "EasySession.Nodes.LeaveReturnsToTheMenu", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionLeaveNodeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionLeaveNodeTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Listener = TStrongObjectPtr<UEasySessionTestNodePinListener>(NewObject<UEasySessionTestNodePinListener>());

	FEasySessionHostParams Params;
	Params.SessionDisplayName = TEXT("EasySession Leave Node Test");
	Params.bIsLANMatch = true;
	Params.bStartListening = false;
	Subsystem->CreateEasySession(Params);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForLeaveNode(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
