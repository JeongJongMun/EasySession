// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyQuickMatchPolicy.h"
#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasyQuickMatchTest
{
	/** Maximum time to wait for a QuickMatch run before failing the test. */
	static constexpr double TimeoutSeconds = 30.0;

	/** State shared between the test body and its latent commands. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> QuickMatchResult;
		bool bCleanupIssued = false;
		double StartTime = 0.0;

		/** Kept alive for the latent commands - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
	};

	/** Build a fake search result for scoring tests. */
	FEasySessionSearchResult MakeFakeResult(int32 PingInMs, int32 MaxPlayers, int32 OpenSlots)
	{
		FEasySessionSearchResult Result;
		Result.PingInMs = PingInMs;
		Result.MaxPlayers = MaxPlayers;
		Result.OpenSlots = OpenSlots;
		return Result;
	}
}

/**
 * Scoring test: the default policy must prefer fuller sessions within the same
 * ping bucket, and lower ping buckets over higher ones regardless of fill.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchScoringTest, "EasySession.QuickMatch.DefaultScoring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchScoringTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

	const UEasyQuickMatchPolicy* Policy = NewObject<UEasyQuickMatchPolicy>();

	// Same bucket (<=50ms): the fuller session wins even with slightly worse ping.
	const FEasySessionSearchResult NearlyEmpty = MakeFakeResult(28, 8, 7);
	const FEasySessionSearchResult NearlyFull = MakeFakeResult(32, 8, 1);
	TestTrue(TEXT("Fuller session wins within the same ping bucket"), Policy->ScoreSession(NearlyFull) > Policy->ScoreSession(NearlyEmpty));

	// Different buckets: the lower bucket wins regardless of fill.
	const FEasySessionSearchResult FastEmpty = MakeFakeResult(40, 8, 8);
	const FEasySessionSearchResult SlowFull = MakeFakeResult(120, 8, 1);
	TestTrue(TEXT("Lower ping bucket wins regardless of fill"), Policy->ScoreSession(FastEmpty) > Policy->ScoreSession(SlowFull));

	// Bucket boundary: 50ms is still the first bucket, 51ms is not.
	const FEasySessionSearchResult OnBoundary = MakeFakeResult(50, 8, 8);
	const FEasySessionSearchResult PastBoundary = MakeFakeResult(51, 8, 1);
	TestTrue(TEXT("Bucket boundary is inclusive"), Policy->ScoreSession(OnBoundary) > Policy->ScoreSession(PastBoundary));

	// A full session cannot be joined. Preferring fuller sessions would otherwise
	// make it the best candidate in its bucket, so it has to lose to any session
	// with room - even one in the worst bucket.
	const FEasySessionSearchResult FastFull = MakeFakeResult(10, 8, 0);
	const FEasySessionSearchResult SlowWithRoom = MakeFakeResult(900, 8, 8);
	TestTrue(TEXT("A full session loses to any session with room"), Policy->ScoreSession(SlowWithRoom) > Policy->ScoreSession(FastFull));

	// Full sessions stay in the list as a last resort, so their order still matters.
	const FEasySessionSearchResult SlowFullSession = MakeFakeResult(900, 8, 0);
	TestTrue(TEXT("Among full sessions the closer one is still preferred"), Policy->ScoreSession(FastFull) > Policy->ScoreSession(SlowFullSession));

	// Capacity is unknown, so nothing says the session is full - no penalty, same as
	// the fill ratio treats it.
	const FEasySessionSearchResult UnknownCapacity = MakeFakeResult(10, 0, 0);
	const FEasySessionSearchResult SlowWithRoomAgain = MakeFakeResult(900, 8, 4);
	TestTrue(TEXT("Unknown capacity is not treated as full"), Policy->ScoreSession(UnknownCapacity) > Policy->ScoreSession(SlowWithRoomAgain));

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitHostFallback, TSharedPtr<EasyQuickMatchTest::FTestState>, State);
bool FEasyQuickMatchWaitHostFallback::Update()
{
	using namespace EasyQuickMatchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->QuickMatchResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for QuickMatch to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("QuickMatch result"), State->QuickMatchResult.GetValue(), EEasySessionResult::Success);
		CurrentTest->TestTrue(TEXT("Fell back to hosting"), Subsystem->IsHost());
		CurrentTest->TestFalse(TEXT("QuickMatch no longer running"), Subsystem->IsQuickMatchRunning());

		Subsystem->DestroyEasySession();
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
 * Host fallback test: with no session on the LAN, QuickMatch must exhaust its
 * search passes and then host its own session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchHostFallbackTest, "EasySession.QuickMatch.HostFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchHostFallbackTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession QuickMatch Test");
	Params.bAllowHostFallback = true;
	// The travel to this map aborts harmlessly - a headless test has no player controller to travel with.
	Params.Host.MapName = TEXT("ES_QuickMatchTestMap");
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	TestTrue(TEXT("QuickMatch is running"), Subsystem->IsQuickMatchRunning());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitHostFallback(State));
	return true;
}

/**
 * Host fallback without a map: Quick Match accepts every parameter set Create accepts.
 *
 * An empty Map Name means "host where this player already is", which Create supports by
 * listening on the current map. Quick Match used to refuse it at the door, so a graph that
 * dropped the node in without filling the params always failed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchHostFallbackWithoutMapTest, "EasySession.QuickMatch.HostFallbackWithoutAMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchHostFallbackWithoutMapTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession QuickMatch No Map Test");
	Params.bAllowHostFallback = true;
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;
	// Map Name is left empty on purpose. That is what a graph gets from the default struct.

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	TestTrue(TEXT("QuickMatch is running"), Subsystem->IsQuickMatchRunning());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitHostFallback(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitNoFallback, TSharedPtr<EasyQuickMatchTest::FTestState>, State);
bool FEasyQuickMatchWaitNoFallback::Update()
{
	using namespace EasyQuickMatchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->QuickMatchResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for QuickMatch to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	CurrentTest->TestEqual(TEXT("QuickMatch result"), State->QuickMatchResult.GetValue(), EEasySessionResult::NoSessionsFound);
	CurrentTest->TestFalse(TEXT("No session was created"), Subsystem->IsInSession());

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * Dedicated-server-client mode: with host fallback disabled, QuickMatch must fail
 * with NoSessionsFound instead of creating a session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchNoFallbackTest, "EasySession.QuickMatch.NoFallbackFails", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchNoFallbackTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.bAllowHostFallback = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitNoFallback(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitCanceledUndo, TSharedPtr<EasyQuickMatchTest::FTestState>, State);
bool FEasyQuickMatchWaitCanceledUndo::Update()
{
	using namespace EasyQuickMatchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->QuickMatchResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for QuickMatch to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("QuickMatch result"), State->QuickMatchResult.GetValue(), EEasySessionResult::Canceled);
		CurrentTest->TestFalse(TEXT("QuickMatch no longer running"), Subsystem->IsQuickMatchRunning());

		// No cleanup of our own: the undo destroy the policy queued is what we wait out below.
		State->bCleanupIssued = true;
		State->StartTime = FPlatformTime::Seconds();
		return false;
	}

	if (Subsystem->IsBusy() || Subsystem->IsInSession())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(FString::Printf(TEXT("The canceled run left state behind: %s"), *Subsystem->GetQueueStatus()));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * Cancel that lands while the fallback create is in flight. The create still succeeds,
 * so honoring the cancel means undoing it: the session is destroyed and the run ends
 * Canceled, never Success. The listener cancels on the Hosting transition, which fires
 * after the create was dispatched and before its completion arrives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchCancelUndoTest, "EasySession.QuickMatch.CancelUndoesTheFallbackHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchCancelUndoTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	State->Listener->CancelQuickMatchOnHosting = Subsystem;

	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession Cancel Undo Test");
	Params.bAllowHostFallback = true;
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	UEasyQuickMatchPolicy* Policy = Subsystem->GetActiveQuickMatchPolicy();
	if (!TestNotNull(TEXT("Active quick match policy is available"), Policy))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}
	Policy->OnStateChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleQuickMatchState);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitCanceledUndo(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitAlreadyInSession, TSharedPtr<EasyQuickMatchTest::FTestState>, State);
bool FEasyQuickMatchWaitAlreadyInSession::Update()
{
	using namespace EasyQuickMatchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->bCleanupIssued)
	{
		// Still creating the session the quick match is supposed to trip over.
		if (!Subsystem->IsInSession() || Subsystem->IsBusy())
		{
			if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
			{
				CurrentTest->AddError(TEXT("Timed out waiting for the setup create."));
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}
			return false;
		}

		TSharedPtr<FTestState> Shared = State;
		Subsystem->StartQuickMatch(FEasyQuickMatchParams(), nullptr, FEasySessionCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult Result, const FString& ErrorMessage)
			{
				Shared->QuickMatchResult = Result;
			}));

		// The refusal happens before the first search, so the result is already here.
		CurrentTest->TestTrue(TEXT("Refused before any step ran"), State->QuickMatchResult.IsSet());
		if (State->QuickMatchResult.IsSet())
		{
			CurrentTest->TestEqual(TEXT("QuickMatch result"), State->QuickMatchResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
		}
		CurrentTest->TestFalse(TEXT("QuickMatch no longer running"), Subsystem->IsQuickMatchRunning());
		CurrentTest->TestTrue(TEXT("The session it refused over is untouched"), Subsystem->IsInSession());

		Subsystem->DestroyEasySession();
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
 * A session that arrived through another door - an accepted invite, or the game's own
 * Create or Join - dooms every quick match step to SessionAlreadyExists. The run must
 * report that once and stop, instead of burning candidates and passes against it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchAlreadyInSessionTest, "EasySession.QuickMatch.RefusesWhenAlreadyInASession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchAlreadyInSessionTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession AlreadyInSession Test");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	// Empty Map Name: the session simply exists here, no travel follows.
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitAlreadyInSession(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
