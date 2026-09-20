// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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
	/** Hard limit on the whole run before the test gives up. */
	static constexpr double MaxWaitSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> PendingResult;
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
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

namespace EasySessionFailedSearchTest
{
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		bool bSearchFailed = false;
		bool bRecoveryStarted = false;
		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;
	};

	static void Finish(FTestState& State)
	{
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

	// The failure is injected while the online subsystem holds the running search, the way a synchronous LAN failure does it.
	// The search is marked Failed and the completion delegate fires, and the online subsystem still holds the search.
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
		CurrentTest->TestEqual(TEXT("The failed search completes with Search Failure"), Result, EEasySessionResult::SearchFailure);

		State->bRecoveryStarted = true;
		State->StartTime = FPlatformTime::Seconds();
		Subsystem->FindSessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult InResult, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				Shared->PendingResult = InResult;
			}));
		return false;
	}

	// Success specifically: an online subsystem still holding the failed search refuses this one, and the drop detection reports SearchFailure instead.
	CurrentTest->TestEqual(TEXT("A search after a synchronously failed one still reaches the online subsystem"), Result, EEasySessionResult::Success);

	Finish(*State);
	return true;
}

/**
 * Searching has to survive a search that fails synchronously.
 *
 * The online subsystem marks such a search Failed but keeps holding it.
 * Its cancel only takes a search it believes is running, and nothing else ever releases it.
 * So one failed search would block every search after it until the game restarts.
 *
 * The failure is injected the way the online subsystem reports it, because the real trigger, a LAN broadcast that fails to send, needs a machine with no network.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFailedSearchRecoveryTest, "EasySession.Search.RecoversFromASynchronouslyFailedSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFailedSearchRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFailedSearchTest;
	using EasySessionSearchRecoveryTest::MakeParams;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->FindSessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
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

	// Ignoring the foreign completion must not lose our own: the real one still arrives and still ends the request.
	CurrentTest->TestEqual(TEXT("Our own search still completes"), State->PendingResult.GetValue(), EEasySessionResult::Success);
	Finish(*State);
	return true;
}

/**
 * The find-complete delegate belongs to the online subsystem, not to this plugin: every search finishing anywhere in the process fires it.
 * A completion that arrives while our own search has not finished belongs to another search and must not end our request.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionForeignSearchTest, "EasySession.Search.IgnoresAForeignCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionForeignSearchTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSearchRecoveryTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->FindSessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
		{
			State->PendingResult = Result;
		}));

	// Requests start on a later tick, so the foreign completion has to be faked from a latent command rather than from here.
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
		TOptional<EEasySessionResult> MatchmakingResult;
		bool bCanceled = false;
		bool bQueueingSeen = false;
		int32 TicksSinceSearch = 0;
		TOptional<EEasySessionResult> SearchResult;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForCanceledSearch, TSharedPtr<EasySessionCanceledSearchTest::FTestState>, State);
bool FEasySessionWaitForCanceledSearch::Update()
{
	using namespace EasySessionCanceledSearchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
	{
		CurrentTest->AddError(TEXT("Timed out waiting for a search to finish."));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	if (!State->bCanceled)
	{
		// Once the search sub-request is at the online subsystem, cancel the run the way a Steam search is canceled: the online subsystem keeps running it.
		if (!FEasySessionTestAccess::HasActiveSearch(*Subsystem))
		{
			return false;
		}

		FEasySessionTestAccess::MarkActiveSearchAsInternet(*Subsystem);
		Subsystem->CancelMatchmaking();
		CurrentTest->TestEqual(TEXT("The requester hears Canceled inside the call"), State->MatchmakingResult.Get(EEasySessionResult::Success), EEasySessionResult::Canceled);
		CurrentTest->TestFalse(TEXT("Matchmaking no longer runs"), Subsystem->IsMatchmakingRunning());
		CurrentTest->TestFalse(TEXT("Nothing is busy any more"), Subsystem->IsBusy());
		CurrentTest->TestTrue(TEXT("While the online subsystem still runs the search"), FEasySessionTestAccess::IsActiveRequestCanceled(*Subsystem));

		State->bCanceled = true;
		State->StartTime = FPlatformTime::Seconds();

		FEasySessionSearchParams Params;
		Params.bLANQuery = true;
		TSharedPtr<FTestState> Shared = State;
		Subsystem->FindSessions(Params, FEasySessionFindCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				Shared->SearchResult = Result;
			}));
		return false;
	}

	// A few ticks in, the next search is waiting: the queue is busy for it, but the canceled run is still the active request.
	++State->TicksSinceSearch;
	if (!State->bQueueingSeen && State->TicksSinceSearch >= 3 && !State->SearchResult.IsSet())
	{
		State->bQueueingSeen = true;
		CurrentTest->TestTrue(TEXT("The next search makes the queue busy"), Subsystem->IsBusy());
		CurrentTest->TestTrue(TEXT("And waits behind the canceled run"), FEasySessionTestAccess::IsActiveRequestCanceled(*Subsystem));
	}
	if (!State->SearchResult.IsSet())
	{
		return false;
	}

	// Success specifically: the online subsystem refuses a search while it holds another, so this one only got through after the canceled one ended.
	CurrentTest->TestEqual(TEXT("The next search ran once the canceled one ended in the online subsystem"), State->SearchResult.GetValue(), EEasySessionResult::Success);
	CurrentTest->TestFalse(TEXT("And nothing is left running"), Subsystem->IsBusy());
	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * A matchmaking run canceled while its search sub-request still runs in the online subsystem.
 *
 * Steam cannot stop a lobby query: its cancel only forgets the search, and a query started meanwhile shares the old one's state and breaks.
 * So the requester is notified with Canceled inside the cancel, but the run stays the active request until the online subsystem completes the search.
 * It does not count as busy meanwhile, and the next search runs after it.
 * NULL only runs LAN searches, which it does stop, so the search is marked as an internet one to reach that path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCanceledSearchTest, "EasySession.Search.QueuesBehindACanceledInternetSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCanceledSearchTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionCanceledSearchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.MaxSearchPasses = 1;
	Params.bAllowHostFallback = false;
	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->MatchmakingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForCanceledSearch(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
