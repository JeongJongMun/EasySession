// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionConfig.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/EngineBaseTypes.h"
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

	/** Builds host params that create a named session without opening a server. */
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
		GetMutableDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
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
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
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
		GetMutableDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
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
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
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

namespace EasySessionTravelFailureTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		int32 Phase = 0;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForTravelFailure, TSharedPtr<EasySessionTravelFailureTest::FTestState>, State);
bool FEasySessionWaitForTravelFailure::Update()
{
	using namespace EasySessionTravelFailureTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d. Queue: %s"), State->Phase, *Subsystem->GetQueueStatus()));
		Finish(*State);
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
			CurrentTest->TestNotNull(TEXT("The approval beacon is up before the travel"), FEasySessionTestAccess::GetJoinApprovalBeaconHost(*Subsystem));

			// The engine accepts a travel to a map that does not exist - only the next
			// tick's load fails. The failure broadcast below stands in for that tick.
			CurrentTest->TestTrue(TEXT("ServerTravelToMap accepted the bad map"), Subsystem->ServerTravelToMap(TEXT("/Game/EasySessionTests/ES_NoSuchMap")));
			CurrentTest->TestNull(TEXT("The travel stopped the beacon"), FEasySessionTestAccess::GetJoinApprovalBeaconHost(*Subsystem));
			CurrentTest->TestTrue(TEXT("The travel reports busy"), Subsystem->IsBusy());

			GEngine->BroadcastTravelFailure(World, ETravelFailure::ServerTravelFailure, TEXT("No such map"));

			CurrentTest->TestTrue(TEXT("The host keeps its session"), Subsystem->IsInSession());
			CurrentTest->TestTrue(TEXT("The failure was reported"), State->Listener->FailureReasons.Num() >= 1);
			CurrentTest->TestFalse(TEXT("No disconnect was recorded - nobody disconnected"), Subsystem->HasPendingDisconnectInfo());
			CurrentTest->TestFalse(TEXT("The failed travel no longer reports busy"), Subsystem->IsBusy());
			CurrentTest->TestNotNull(TEXT("The approval beacon is back up"), FEasySessionTestAccess::GetJoinApprovalBeaconHost(*Subsystem));

			// The same broadcast on a client still tears the dead session down.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);
			GEngine->BroadcastTravelFailure(World, ETravelFailure::ServerTravelFailure, TEXT("Could not reach the host"));

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The client's cleanup destroy ran"), State->Listener->DestroyedResults.Num(), 1);
			CurrentTest->TestEqual(TEXT("The disconnect reason is the travel failure"), Subsystem->ConsumeLastDisconnectInfo().Reason, EEasyDisconnectReason::TravelFailure);

			Finish(*State);
			return true;
		}
	}
}

/**
 * A failed server travel leaves the host's world, session and connected players exactly
 * where they were - the engine only clears the pending URL. Treating it as a disconnect
 * destroyed a live room over a map name typo. The host now keeps the session and hears
 * about the failure through On Session Failure; a client still cleans up, because its
 * travel failure really does mean it never reached the host.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionTravelFailureTest, "EasySession.Recovery.HostKeepsSessionOnTravelFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionTravelFailureTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionTravelFailureTest;
	using EasySessionSecondDisconnectTest::MakeParams;

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
	// no use for. Turning it off leaves the recovery behavior, which is what is on trial.
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionFailure.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSessionFailure);
	Subsystem->OnSessionDestroyed.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleDestroyed);

	Subsystem->CreateEasySession(MakeParams(TEXT("EasySession Travel Failure Test")));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForTravelFailure(State));
	return true;
}

namespace EasySessionNetworkFilterTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** A second instance whose world stands in for another PIE instance's. */
		TStrongObjectPtr<UGameInstance> ForeignGameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		int32 Phase = 0;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.ForeignGameInstance.Get());
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForNetworkFilter, TSharedPtr<EasySessionNetworkFilterTest::FTestState>, State);
bool FEasySessionWaitForNetworkFilter::Update()
{
	using namespace EasySessionNetworkFilterTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d. Queue: %s"), State->Phase, *Subsystem->GetQueueStatus()));
		Finish(*State);
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

			// The host hears about a client's dead connection through the same broadcast.
			GEngine->BroadcastNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, TEXT("a client dropped"));
			CurrentTest->TestTrue(TEXT("The host keeps its session over a client's failure"), Subsystem->IsInSession());
			CurrentTest->TestEqual(TEXT("The failure was still reported"), State->Listener->FailureReasons.Num(), 1);
			CurrentTest->TestFalse(TEXT("No disconnect was recorded for the host"), Subsystem->HasPendingDisconnectInfo());

			// Another instance's world fails - PIE neighbors share the engine broadcast.
			GEngine->BroadcastNetworkFailure(State->ForeignGameInstance->GetWorld(), nullptr, ENetworkFailure::ConnectionLost, TEXT("someone else's world"));
			CurrentTest->TestTrue(TEXT("A foreign world's failure changes nothing"), Subsystem->IsInSession());
			CurrentTest->TestEqual(TEXT("And is not reported here"), State->Listener->FailureReasons.Num(), 1);

			// The same broadcast on a client is a lost host.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);
			GEngine->BroadcastNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, TEXT("lost the host"));

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The client's cleanup destroy ran"), State->Listener->DestroyedResults.Num(), 1);
			CurrentTest->TestEqual(TEXT("The disconnect reason is the lost connection"), Subsystem->ConsumeLastDisconnectInfo().Reason, EEasyDisconnectReason::ConnectionLost);

			// With no world and no session there is nothing this failure could mean here.
			const int32 ReasonsBefore = State->Listener->FailureReasons.Num();
			GEngine->BroadcastNetworkFailure(nullptr, nullptr, ENetworkFailure::ConnectionLost, TEXT("stray"));
			CurrentTest->TestEqual(TEXT("A worldless failure outside a session is ignored"), State->Listener->FailureReasons.Num(), ReasonsBefore);

			// A refusal the host wrote reaches the player verbatim.
			GEngine->BroadcastNetworkFailure(World, nullptr, ENetworkFailure::FailureReceived, TEXT("Wrong password."));
			const FEasyDisconnectInfo Refusal = Subsystem->ConsumeLastDisconnectInfo();
			CurrentTest->TestEqual(TEXT("A host-written refusal is recorded as a rejection"), Refusal.Reason, EEasyDisconnectReason::Rejected);
			CurrentTest->TestEqual(TEXT("With the host's own sentence"), Refusal.ReasonText.ToString(), FString(TEXT("Wrong password.")));

			Finish(*State);
			return true;
		}
	}
}

/**
 * What counts as a disconnect is decided in HandleNetworkFailure, and every branch of
 * that decision was previously untested - the recovery tests start below it. The costly
 * misjudgment is the host reading a client's dead connection as losing its own session
 * and tearing the room down; the filter rows for foreign worlds and worldless failures
 * keep PIE neighbors and stray broadcasts from doing the same.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNetworkFilterTest, "EasySession.Recovery.NetworkFailureFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNetworkFilterTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionNetworkFilterTest;
	using EasySessionSecondDisconnectTest::MakeParams;

	// The engine logs every BroadcastNetworkFailure call at Error level, and this test is the caller.
	AddExpectedError(TEXT("UEngine::BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();
	State->ForeignGameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->ForeignGameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		Finish(*State);
		return false;
	}

	// Returning to the menu means browsing to a map, which a headless test world has
	// no use for. Turning it off leaves the filter decisions, which are what is on trial.
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionFailure.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSessionFailure);
	Subsystem->OnSessionDestroyed.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleDestroyed);

	Subsystem->CreateEasySession(MakeParams(TEXT("EasySession Network Filter Test")));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForNetworkFilter(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
