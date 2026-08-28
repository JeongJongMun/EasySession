// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSettings.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Disconnect info test: recording, first-reason-wins overwrite protection,
 * and consume-once semantics.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDisconnectInfoTest, "EasySession.Recovery.DisconnectInfoStoreConsume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDisconnectInfoTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	TestFalse(TEXT("No pending info initially"), Subsystem->HasPendingDisconnectInfo());

	// Record a disconnect. There is no session and no menu map configured, so the
	// recovery flow only stores the info.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(TEXT("First reason")));
	TestTrue(TEXT("Pending after first notify"), Subsystem->HasPendingDisconnectInfo());

	// A follow-up failure must not overwrite the original cause.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::TravelFailure, FText::FromString(TEXT("Second reason")));

	const FEasyDisconnectInfo Info = Subsystem->ConsumeLastDisconnectInfo();
	TestEqual(TEXT("First reason wins"), Info.Reason, EEasyDisconnectReason::ConnectionLost);
	TestEqual(TEXT("First reason text preserved"), Info.ReasonText.ToString(), FString(TEXT("First reason")));

	TestFalse(TEXT("Not pending after consume"), Subsystem->HasPendingDisconnectInfo());

	const FEasyDisconnectInfo EmptyInfo = Subsystem->ConsumeLastDisconnectInfo();
	TestEqual(TEXT("Second consume returns None"), EmptyInfo.Reason, EEasyDisconnectReason::None);

	// A new disconnect after consuming records fresh info again.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostDestroyedSession, FText::FromString(TEXT("Third reason")));
	TestTrue(TEXT("Pending again after re-notify"), Subsystem->HasPendingDisconnectInfo());
	TestEqual(TEXT("New reason recorded"), Subsystem->ConsumeLastDisconnectInfo().Reason, EEasyDisconnectReason::HostDestroyedSession);

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

namespace EasySessionSecondDisconnectTest
{
	/** Maximum time to wait for the whole two-disconnect sequence before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		int32 Phase = 0;
		TOptional<EEasySessionResult> FirstCreateResult;
		TOptional<EEasySessionResult> SecondCreateResult;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	/** Builds host params that create a session record without opening a server. */
	static FEasySessionHostParams MakeParams(const TCHAR* DisplayName)
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = DisplayName;
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForSecondDisconnect, TSharedPtr<EasySessionSecondDisconnectTest::FTestState>, State);
bool FEasySessionWaitForSecondDisconnect::Update()
{
	using namespace EasySessionSecondDisconnectTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(
			TEXT("Timed out in phase %d. A second disconnect must still destroy the session even though the first reason was never consumed."),
			State->Phase));
		Finish(*State);
		return true;
	}

	switch (State->Phase)
	{
		case 0:
			// Wait for the first session to exist.
			if (!State->FirstCreateResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("First session created"), State->FirstCreateResult.GetValue(), EEasySessionResult::Success);
			Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(TEXT("Host A left")));
			State->Phase = 1;
			return false;

		case 1:
			// The first disconnect cleans up, as it always did.
			if (Subsystem->IsInSession())
			{
				return false;
			}
			CurrentTest->TestTrue(TEXT("Reason is pending and deliberately left unconsumed"), Subsystem->HasPendingDisconnectInfo());

			// Join another host. Nothing consumed the first reason, which is what a
			// project that never shows the popup looks like.
			Subsystem->CreateEasySession(MakeParams(TEXT("EasySession Second Host")), FEasySessionCompleteDelegate::CreateLambda(
				[State = State](EEasySessionResult Result, const FString&)
				{
					State->SecondCreateResult = Result;
				}));
			State->Phase = 2;
			return false;

		case 2:
			if (!State->SecondCreateResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Second session created"), State->SecondCreateResult.GetValue(), EEasySessionResult::Success);
			Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostDestroyedSession, FText::FromString(TEXT("Host B left")));
			State->Phase = 3;
			return false;

		default:
			// The point of the test: this cleanup used to be skipped entirely.
			if (Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Second disconnect destroyed the session"), Subsystem->GetSessionState(), EEasySessionState::NoSession);
			CurrentTest->TestTrue(TEXT("Reason still pending after the second disconnect"), Subsystem->HasPendingDisconnectInfo());
			CurrentTest->TestEqual(TEXT("First reason still wins"), Subsystem->ConsumeLastDisconnectInfo().Reason, EEasyDisconnectReason::ConnectionLost);

			Finish(*State);
			return true;
	}
}

/**
 * Recovery must not depend on the game consuming the disconnect reason. Reading the
 * reason is optional - a project can rely on the automatic cleanup alone - so a
 * second disconnect has to destroy the dead session even while the first reason is
 * still sitting there unread.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSecondDisconnectTest, "EasySession.Recovery.SecondDisconnectCleansUpWithoutConsume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSecondDisconnectTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSecondDisconnectTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Returning to the menu means browsing to a map, which a headless test world has
	// no use for. Turning it off leaves the session cleanup, which is what is on trial.
	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateEasySession(MakeParams(TEXT("EasySession First Host")), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->FirstCreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSecondDisconnect(State));
	return true;
}

namespace EasySessionCleanupOnceTest
{
	/** Maximum time to wait for the whole sequence before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		TOptional<EEasySessionResult> CreateResult;
		int32 Phase = 0;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForCleanupOnce, TSharedPtr<EasySessionCleanupOnceTest::FTestState>, State);
bool FEasySessionWaitForCleanupOnce::Update()
{
	using namespace EasySessionCleanupOnceTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d. Queue: %s"), State->Phase, *Subsystem->GetQueueStatus()));
		Finish(*State);
		return true;
	}

	switch (State->Phase)
	{
		case 0:
			if (!State->CreateResult.IsSet() || Subsystem->IsBusy())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			// Two failures land in the same recovery: the host dying, then the net
			// driver closing. The first queues the cleanup destroy; the second finds
			// it already queued.
			Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(TEXT("Host died")));
			Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(TEXT("Net driver closed")));
			State->Phase = 1;
			return false;

		default:
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(FString::Printf(TEXT("The cleanup destroy ran exactly once (saw %d)"), State->Listener->DestroyedResults.Num()),
				State->Listener->DestroyedResults.Num(), 1);
			if (State->Listener->DestroyedResults.Num() > 0)
			{
				CurrentTest->TestEqual(TEXT("And it succeeded"), State->Listener->DestroyedResults[0], EEasySessionResult::Success);
			}
			CurrentTest->TestEqual(TEXT("The first failure's reason survived"), Subsystem->ConsumeLastDisconnectInfo().ReasonText.ToString(), FString(TEXT("Host died")));

			Finish(*State);
			return true;
	}
}

/**
 * Two failures inside one recovery run one cleanup. The connection dropping while the
 * dead session is being destroyed re-enters the disconnect path, and a second queued
 * destroy would complete as NoSessionExists - a failure popup right after a clean
 * recovery, over a session that was already gone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCleanupOnceTest, "EasySession.Recovery.SecondFailureDuringCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCleanupOnceTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionCleanupOnceTest;
	using namespace EasySessionSecondDisconnectTest;

	TSharedPtr<EasySessionCleanupOnceTest::FTestState> State = MakeShared<EasySessionCleanupOnceTest::FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Returning to the menu means browsing to a map, which a headless test world has
	// no use for. Turning it off leaves the session cleanup, which is what is on trial.
	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionDestroyed.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleDestroyed);

	TSharedPtr<EasySessionCleanupOnceTest::FTestState> Shared = State;
	Subsystem->CreateEasySession(MakeParams(TEXT("EasySession Cleanup Once Test")), FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForCleanupOnce(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
