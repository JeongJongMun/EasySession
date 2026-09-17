// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionConfig.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSearchRecoveryTest
{
	/**
	 * Short enough that the deadline passes while the search is still running.
	 * The engine's own LAN search lasts a fixed five seconds (LANBeacon.h, LAN_QUERY_TIMEOUT), so this has to stay under that.
	 * The searches that must finish normally get the real deadline back first.
	 */
	static constexpr float TestRequestTimeoutSeconds = 0.5f;

	/** Hard limit on the whole run before the test gives up. */
	static constexpr double MaxWaitSeconds = 30.0;

	enum class EStep : uint8
	{
		AwaitingAbandonedSearch,
		AwaitingRecoverySearch,
		Done
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		EStep Step = EStep::AwaitingAbandonedSearch;
		TOptional<EEasySessionResult> PendingResult;
		float OriginalTimeout = 30.0f;
		double StartTime = 0.0;
	};

	static FEasySessionSearchParams MakeParams()
	{
		FEasySessionSearchParams Params;
		Params.bLANQuery = true;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionConfig>()->RequestTimeoutSeconds = State.OriginalTimeout;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForSearchRecovery, TSharedPtr<EasySessionSearchRecoveryTest::FTestState>, State);
bool FEasySessionWaitForSearchRecovery::Update()
{
	using namespace EasySessionSearchRecoveryTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

	if (!State->PendingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for a search to finish."));
			Finish(*State);
			return true;
		}
		return false;
	}

	const EEasySessionResult Result = State->PendingResult.GetValue();
	State->PendingResult.Reset();

	if (State->Step == EStep::AwaitingAbandonedSearch)
	{
		CurrentTest->TestEqual(TEXT("The watchdog gave up on the first search"), Result, EEasySessionResult::Timeout);
		CurrentTest->TestFalse(TEXT("Giving up releases the search object"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

		// The point of the whole test. The online subsystem refuses to start a search
		// while it believes one is running, and returns true anyway, so a search abandoned
		// without telling it would block every search after it.
		//
		// This one is given the real deadline back: a LAN search needs its five
		// seconds, and the short deadline above exists only to cut the first one off.
		GetMutableDefault<UEasySessionConfig>()->RequestTimeoutSeconds = State->OriginalTimeout;

		State->Step = EStep::AwaitingRecoverySearch;
		State->StartTime = FPlatformTime::Seconds();
		Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult InResult, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				Shared->PendingResult = InResult;
			}));
		return false;
	}

	// Success specifically, not just "not a timeout": an online subsystem holding an
	// abandoned search refuses this one, which ExecuteFind reports inside the call
	// rather than after another deadline.
	CurrentTest->TestEqual(TEXT("A search after an abandoned one still reaches the online subsystem"), Result, EEasySessionResult::Success);
	CurrentTest->TestFalse(TEXT("The recovered search releases its search object too"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

	State->Step = EStep::Done;
	Finish(*State);
	return true;
}

/**
 * Searching has to survive the watchdog giving up on a search.
 *
 * The online subsystem keeps running a search until it is told to stop, refuses
 * another while one is running, and reports that refusal as success. So a request
 * abandoned without a cancel leaves every later search waiting for a callback that
 * never comes, until the game restarts.
 *
 * The second search here is the assertion that matters: it can only reach the
 * online subsystem if the first one was canceled when it was abandoned.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSearchRecoveryTest, "EasySession.Search.RecoversFromAnAbandonedSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSearchRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSearchRecoveryTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->OriginalTimeout = Settings->RequestTimeoutSeconds;
	Settings->RequestTimeoutSeconds = TestRequestTimeoutSeconds;

	Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSearchRecovery(State));
	return true;
}

namespace EasySessionFailedSearchTest
{
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		bool bSearchFailed = false;
		bool bRecoveryStarted = false;
		TOptional<EEasySessionResult> PendingResult;
		float OriginalTimeout = 30.0f;
		double StartTime = 0.0;
	};

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionConfig>()->RequestTimeoutSeconds = State.OriginalTimeout;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForFailedSearchRecovery, TSharedPtr<EasySessionFailedSearchTest::FTestState>, State);
bool FEasySessionWaitForFailedSearchRecovery::Update()
{
	using namespace EasySessionFailedSearchTest;
	using EasySessionSearchRecoveryTest::MakeParams;
	using EasySessionSearchRecoveryTest::MaxWaitSeconds;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

	if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out. Queue: %s"), *Subsystem->GetQueueStatus()));
		Finish(*State);
		return true;
	}

	// The failure is injected while the online subsystem holds the running search, which is the
	// state a synchronous LAN failure leaves: SearchState Failed, slot still taken.
	if (!State->bSearchFailed)
	{
		if (FEasySessionTestAccess::FailActiveSearch(*Subsystem))
		{
			State->bSearchFailed = true;
		}
		return false;
	}

	if (!State->PendingResult.IsSet())
	{
		return false;
	}

	const EEasySessionResult Result = State->PendingResult.GetValue();
	State->PendingResult.Reset();

	if (!State->bRecoveryStarted)
	{
		CurrentTest->TestEqual(TEXT("The watchdog gave up on the failed search"), Result, EEasySessionResult::Timeout);

		// The recovery search gets the real deadline back, same as the abandoned-search test.
		GetMutableDefault<UEasySessionConfig>()->RequestTimeoutSeconds = State->OriginalTimeout;

		State->bRecoveryStarted = true;
		State->StartTime = FPlatformTime::Seconds();
		Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult InResult, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				Shared->PendingResult = InResult;
			}));
		return false;
	}

	// Success specifically: an online subsystem still holding the failed search refuses this one
	// and the drop detection reports SearchFailure instead.
	CurrentTest->TestEqual(TEXT("A search after a synchronously failed one still reaches the online subsystem"), Result, EEasySessionResult::Success);

	Finish(*State);
	return true;
}

/**
 * Searching has to survive a search that fails synchronously.
 *
 * The online subsystem marks such a search Failed but keeps holding it. Its cancel only
 * takes a search it believes is running, and nothing else ever releases it, so one
 * failed search would block every search after it until the game restarts.
 *
 * The failure is injected by flipping the running search's state, because the real
 * trigger, a LAN broadcast that fails to send, needs a machine with no network.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFailedSearchRecoveryTest, "EasySession.Search.RecoversFromASynchronouslyFailedSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFailedSearchRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFailedSearchTest;
	using EasySessionSearchRecoveryTest::MakeParams;
	using EasySessionSearchRecoveryTest::TestRequestTimeoutSeconds;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->OriginalTimeout = Settings->RequestTimeoutSeconds;
	Settings->RequestTimeoutSeconds = TestRequestTimeoutSeconds;

	Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForFailedSearchRecovery(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionInterruptOwnSearch, TSharedPtr<EasySessionSearchRecoveryTest::FTestState>, State);
bool FEasySessionInterruptOwnSearch::Update()
{
	using namespace EasySessionSearchRecoveryTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	// Wait for the search to actually be running before interrupting it.
	if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
	{
		if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
		{
			CurrentTest->AddError(TEXT("The search never started."));
			return true;
		}
		return false;
	}

	// Stand in for any other search in the process finishing while ours runs.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(State->GameInstance->GetWorld());
	if (CurrentTest->TestTrue(TEXT("Session interface is available"), Sessions.IsValid()))
	{
		Sessions->TriggerOnFindSessionsCompleteDelegates(true);
	}

	CurrentTest->TestFalse(TEXT("A foreign completion does not end our search"), State->PendingResult.IsSet());
	CurrentTest->TestTrue(TEXT("Our search is still running"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

	State->StartTime = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForOwnSearch, TSharedPtr<EasySessionSearchRecoveryTest::FTestState>, State);
bool FEasySessionWaitForOwnSearch::Update()
{
	using namespace EasySessionSearchRecoveryTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->PendingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for our own search to finish."));
			Finish(*State);
			return true;
		}
		return false;
	}

	// Ignoring the foreign completion must not lose our own: the real one still
	// arrives and still ends the request.
	CurrentTest->TestNotEqual(TEXT("Our own search still completes"), State->PendingResult.GetValue(), EEasySessionResult::Timeout);
	Finish(*State);
	return true;
}

/**
 * The find-complete delegate belongs to the online subsystem, not to this plugin: every
 * search finishing anywhere in the process fires it. A completion that arrives while our
 * own search has not finished belongs to another search and must not end our request.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionForeignSearchTest, "EasySession.Search.IgnoresAForeignCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionForeignSearchTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSearchRecoveryTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// The watchdog must not interfere here. This search is meant to run to the end.
	State->OriginalTimeout = GetMutableDefault<UEasySessionConfig>()->RequestTimeoutSeconds;

	Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
		{
			State->PendingResult = Result;
		}));

	// Requests start on a later tick, so the foreign completion has to be faked from
	// a latent command rather than from here.
	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionInterruptOwnSearch(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForOwnSearch(State));
	return true;
}

namespace EasySessionCanceledSearchTest
{
	/** Hard limit on each step before the test gives up. */
	static constexpr double MaxWaitSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		/** Stands in for the matchmaking policy: the requester the search's delegate is bound to. */
		TStrongObjectPtr<UEasySessionTestEventListener> Requester;
		bool bCanceled = false;
		bool bQueueingSeen = false;
		int32 TicksSinceStart = 0;
		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;
	};

	static void StartSearch(TSharedPtr<FTestState> State, UEasySessionSubsystem& Subsystem)
	{
		State->PendingResult.Reset();
		State->TicksSinceStart = 0;
		State->StartTime = FPlatformTime::Seconds();

		FEasySessionSearchParams Params;
		Params.bLANQuery = true;
		Subsystem.FindEasySessions(Params, FEasySessionFindCompleteDelegate::CreateWeakLambda(State->Requester.Get(),
			[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				State->PendingResult = Result;
			}));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForCanceledSearch, TSharedPtr<EasySessionCanceledSearchTest::FTestState>, State);
bool FEasySessionWaitForCanceledSearch::Update()
{
	using namespace EasySessionCanceledSearchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	++State->TicksSinceStart;

	if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
	{
		CurrentTest->AddError(TEXT("Timed out waiting for a search to finish."));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	if (!State->bCanceled)
	{
		// Once the search is at the online subsystem, cancel it the way a Steam search is canceled: the online subsystem keeps running it.
		if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
		{
			return false;
		}

		FEasySessionTestAccess::MarkActiveSearchAsInternet(*Subsystem);
		CurrentTest->TestTrue(TEXT("The requester's search was found"), Subsystem->CancelSearch(State->Requester.Get()));
		CurrentTest->TestEqual(TEXT("The requester hears Canceled inside the call"), State->PendingResult.Get(EEasySessionResult::Success), EEasySessionResult::Canceled);
		CurrentTest->TestFalse(TEXT("Nothing is busy any more"), Subsystem->IsBusy());
		CurrentTest->TestTrue(TEXT("While the online subsystem still runs the search"), FEasySessionTestAccess::IsActiveRequestCanceled(*Subsystem));

		State->bCanceled = true;
		StartSearch(State, *Subsystem);
		return false;
	}

	// A few ticks in, the next search is waiting: the queue is busy for it, but the slot still belongs to the canceled search.
	if (!State->bQueueingSeen && State->TicksSinceStart >= 3)
	{
		State->bQueueingSeen = true;
		CurrentTest->TestTrue(TEXT("The next search makes the queue busy"), Subsystem->IsBusy());
		CurrentTest->TestTrue(TEXT("And waits behind the canceled search"), FEasySessionTestAccess::IsActiveRequestCanceled(*Subsystem));
	}
	if (!State->PendingResult.IsSet())
	{
		return false;
	}

	// Success specifically: the online subsystem refuses a search while it holds another, so this one only got through after the canceled one ended.
	CurrentTest->TestEqual(TEXT("The next search ran once the canceled one ended in the online subsystem"), State->PendingResult.GetValue(), EEasySessionResult::Success);
	CurrentTest->TestFalse(TEXT("And nothing is left running"), Subsystem->IsBusy());
	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * A search whose requester cancels it while the online subsystem still runs it.
 *
 * Steam cannot stop a lobby query: its cancel only forgets the search, and a query
 * started meanwhile shares the old one's state and breaks. So the canceled search
 * keeps its slot in the queue until the online subsystem completes it, with no requester
 * and not counting as busy, and the next search runs after it. NULL only runs LAN searches,
 * which it does stop, so the search is marked as an internet one to reach that path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCanceledSearchTest, "EasySession.Search.QueuesBehindACanceledInternetSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCanceledSearchTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionCanceledSearchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Requester = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	StartSearch(State, *Subsystem);
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForCanceledSearch(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
