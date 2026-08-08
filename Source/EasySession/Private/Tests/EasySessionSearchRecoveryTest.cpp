// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSettings.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSearchRecoveryTest
{
	/**
	 * Short enough that the watchdog fires while the search is genuinely running.
	 * A LAN search takes a fixed five seconds whatever the search params ask for
	 * (LANBeacon.h, LAN_QUERY_TIMEOUT), and the deadline is this plus the search
	 * budget below - so the two together have to stay under those five seconds.
	 */
	static constexpr float TestRequestTimeoutSeconds = 0.5f;
	static constexpr float TestSearchTimeoutSeconds = 0.5f;

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
		Params.TimeoutSeconds = TestSearchTimeoutSeconds;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionSettings>()->RequestTimeoutSeconds = State.OriginalTimeout;
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

		// The point of the whole test. The online service refuses to start a search
		// while it believes one is running, and says yes anyway, so a search abandoned
		// without telling it would swallow every search after it.
		//
		// This one is given the real deadline back: a LAN search needs its five
		// seconds, and the short deadline above exists only to cut the first one off.
		GetMutableDefault<UEasySessionSettings>()->RequestTimeoutSeconds = State->OriginalTimeout;

		State->Step = EStep::AwaitingRecoverySearch;
		State->StartTime = FPlatformTime::Seconds();
		Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult InResult, const FString&, const TArray<FEasySessionSearchResult>&)
			{
				Shared->PendingResult = InResult;
			}));
		return false;
	}

	// Success specifically, not just "not a timeout": a service holding an abandoned
	// search refuses this one, which ExecuteFind now reports straight away rather
	// than waiting out another deadline.
	CurrentTest->TestEqual(TEXT("A search after an abandoned one still reaches the online service"), Result, EEasySessionResult::Success);
	CurrentTest->TestFalse(TEXT("The recovered search releases its search object too"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

	State->Step = EStep::Done;
	Finish(*State);
	return true;
}

/**
 * Searching has to survive the watchdog giving up on a search.
 *
 * The online service keeps working on a search until it is told to stop, refuses
 * another while one is running, and reports that refusal as success - so a request
 * abandoned without a cancel leaves every later search waiting for a callback that
 * will never come, until the game restarts.
 *
 * The second search here is the assertion that matters: it can only reach the
 * service if the first one was cancelled on the way out.
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

	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
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

	// Ignoring the foreign completion must not cost us our own: the real one still
	// arrives and still ends the request.
	CurrentTest->TestNotEqual(TEXT("Our own search still completes"), State->PendingResult.GetValue(), EEasySessionResult::Timeout);
	Finish(*State);
	return true;
}

/**
 * The find-complete delegate belongs to the online service, not to us: every search
 * finishing anywhere in the process rings it. A completion that arrives while our
 * own search has not finished is somebody else's and must not end our request.
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

	// The watchdog must not interfere here - this search is meant to run to the end.
	State->OriginalTimeout = GetMutableDefault<UEasySessionSettings>()->RequestTimeoutSeconds;

	Subsystem->FindEasySessions(MakeParams(), FEasySessionFindCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>&)
		{
			State->PendingResult = Result;
		}));

	TestTrue(TEXT("The search is running"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

	// Stand in for any other search in the process finishing while ours runs.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(State->GameInstance->GetWorld());
	if (TestTrue(TEXT("Session interface is available"), Sessions.IsValid()))
	{
		Sessions->TriggerOnFindSessionsCompleteDelegates(true);
	}

	TestFalse(TEXT("A foreign completion does not end our search"), State->PendingResult.IsSet());
	TestTrue(TEXT("Our search is still running"), FEasySessionTestAccess::HasActiveSearch(*Subsystem));

	// Let it finish rather than abandoning it - a search left running would make the
	// online service refuse the next test's.
	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForOwnSearch(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
