// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyQuickMatchPolicy.h"
#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitFallbackFilters, TSharedPtr<EasyQuickMatchTest::FTestState>, State);
bool FEasyQuickMatchWaitFallbackFilters::Update()
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

		// The live session must advertise what the search filtered on, or the next player's identical search skips this room.
		const FEasySessionHostParams ReadBack = Subsystem->GetSessionHostParams();
		CurrentTest->TestEqual(TEXT("The filtered key overwrote the host value"), ReadBack.CustomSettings.FindRef(TEXT("GameMode")), FString(TEXT("Deathmatch")));
		CurrentTest->TestEqual(TEXT("The filter-only key was added"), ReadBack.CustomSettings.FindRef(TEXT("Region")), FString(TEXT("KR")));
		CurrentTest->TestEqual(TEXT("The host-only key survived"), ReadBack.CustomSettings.FindRef(TEXT("MOTD")), FString(TEXT("Hello")));

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
 * The fallback host inherits the search filters. Host params used to be passed to Create
 * as given, so a LAN search could fall back to an online session, and a run filtering on
 * GameMode=Deathmatch could open a room its own search would never return.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchFallbackFiltersTest, "EasySession.QuickMatch.FallbackHostMatchesTheFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchFallbackFiltersTest::RunTest(const FString& Parameters)
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
	Params.Search.RequiredCustomSettings.Add(TEXT("GameMode"), TEXT("Deathmatch"));
	Params.Search.RequiredCustomSettings.Add(TEXT("Region"), TEXT("KR"));
	Params.Host.SessionDisplayName = TEXT("EasySession Fallback Filters Test");
	// On purpose: the search filter must win over this stale host value.
	Params.Host.CustomSettings.Add(TEXT("GameMode"), TEXT("Warmup"));
	Params.Host.CustomSettings.Add(TEXT("MOTD"), TEXT("Hello"));
	// Also on purpose: the LAN search must pull the fallback onto the LAN.
	Params.Host.bIsLANMatch = false;
	Params.Host.bStartListening = false;
	Params.bAllowHostFallback = true;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	// The NULL service forces LAN on every created session, so the network half of the
	// fold is only observable here, on the params the fallback hands to Create.
	UEasyQuickMatchPolicy* Policy = Subsystem->GetActiveQuickMatchPolicy();
	if (TestNotNull(TEXT("The quick match policy is active"), Policy))
	{
		const FEasySessionHostParams Folded = FEasySessionTestAccess::MakeQuickMatchFallbackHostParams(*Policy);
		TestTrue(TEXT("The LAN search pulls the fallback onto the LAN"), Folded.bIsLANMatch);
		TestEqual(TEXT("The folded params carry the filtered value"), Folded.CustomSettings.FindRef(TEXT("GameMode")), FString(TEXT("Deathmatch")));
	}

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitFallbackFilters(State));
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

namespace EasySessionCandidateTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		/** A joinable-but-unreachable result borrowed from the seed session, cloned into every candidate. */
		FOnlineSessionSearchResult BaseResult;

		/** Whether the first real search pass was seen running. Its release opens the injection window. */
		bool bFirstSearchRan = false;

		/** Destroys seen before the injection - the seed session's own cleanup is one of them. */
		int32 DestroysBeforeInjection = 0;

		TOptional<EEasySessionResult> QuickMatchResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForCandidateRun, TSharedPtr<EasySessionCandidateTest::FTestState>, State);
bool FEasySessionWaitForCandidateRun::Update()
{
	using namespace EasySessionCandidateTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

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

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted base result is joinable"), State->BaseResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			Subsystem->DestroyEasySession();
			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 1:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			FEasyQuickMatchParams Params;
			Params.Search.bLANQuery = true;
			// A short first pass and a long inter-pass delay open a quiet window to inject into.
			Params.Search.TimeoutSeconds = 0.5f;
			Params.MaxSearchPasses = 2;
			Params.DelayBetweenPassesSeconds = 10.0f;
			Params.bAllowHostFallback = false;

			Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->QuickMatchResult = Result;
				}));

			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (State->QuickMatchResult.IsSet())
			{
				CurrentTest->AddError(TEXT("The run finished before anything was injected - the injection window was missed."));
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// The quiet window: the first pass's search ran and was released, and the next
			// pass is 10s away. IsBusy cannot spot it - it stays true for the whole run.
			if (!State->bFirstSearchRan)
			{
				State->bFirstSearchRan = FEasySessionTestAccess::HasActiveSearch(*Subsystem);
				return false;
			}
			if (FEasySessionTestAccess::HasActiveSearch(*Subsystem) || Subsystem->GetQuickMatchState() != EEasyQuickMatchState::Searching)
			{
				return false;
			}

			UEasyQuickMatchPolicy* Policy = Subsystem->GetActiveQuickMatchPolicy();
			if (!CurrentTest->TestNotNull(TEXT("The quick match policy is active"), Policy))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// One candidate per assertion, ordered worst-first on purpose. They share the
			// seed session's id, so the retry ledger ends with one key for all of them.
			Policy->TopCandidateRandomization = 1;
			auto MakeCandidate = [Shared](const TCHAR* Name, int32 Ping, bool bLocked)
			{
				FEasySessionSearchResult Candidate;
				Candidate.NativeResult = Shared->BaseResult;
				Candidate.SessionDisplayName = Name;
				Candidate.PingInMs = Ping;
				Candidate.MaxPlayers = 4;
				Candidate.OpenSlots = 2;
				Candidate.bPasswordProtected = bLocked;
				return Candidate;
			};

			State->DestroysBeforeInjection = State->Listener->DestroyedResults.Num();

			TArray<FEasySessionSearchResult> Crafted;
			Crafted.Add(MakeCandidate(TEXT("Far"), 200, false));
			Crafted.Add(MakeCandidate(TEXT("Locked"), 5, true));
			Crafted.Add(MakeCandidate(TEXT("Near"), 20, false));
			Crafted.Add(MakeCandidate(TEXT("Mid"), 90, false));
			FEasySessionTestAccess::DriveQuickMatchSearch(*Policy, Crafted);

			const TArray<FEasySessionSearchResult> Candidates = FEasySessionTestAccess::GetQuickMatchCandidates(*Policy);
			CurrentTest->TestEqual(TEXT("The locked room is not a candidate"), Candidates.Num(), 3);
			if (Candidates.Num() == 3)
			{
				CurrentTest->TestEqual(TEXT("The nearest room is tried first"), Candidates[0].SessionDisplayName, FString(TEXT("Near")));
				CurrentTest->TestEqual(TEXT("The middle bucket is second"), Candidates[1].SessionDisplayName, FString(TEXT("Mid")));
				CurrentTest->TestEqual(TEXT("The far room is last"), Candidates[2].SessionDisplayName, FString(TEXT("Far")));
			}

			State->Phase = 3;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (!State->QuickMatchResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Exhausting every candidate without fallback ends in NoSessionsFound"),
				State->QuickMatchResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestEqual(TEXT("Every candidate was actually tried - each join left one cleanup destroy"),
				State->Listener->DestroyedResults.Num() - State->DestroysBeforeInjection, 3);

			UEasyQuickMatchPolicy* Policy = Subsystem->GetActiveQuickMatchPolicy();
			if (Policy != nullptr)
			{
				// One key, not three: the crafted candidates share the seed session's id.
				CurrentTest->TestEqual(TEXT("The refused session is on the no-retry ledger"),
					FEasySessionTestAccess::GetQuickMatchFailedSessionKeys(*Policy).Num(), 1);
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The half of Quick Match after a search returns rooms: password rooms are excluded,
 * candidates are tried best score first, and a refused room lands on the no-retry
 * ledger. None of it ran under automation before, because the only way results entered
 * the policy was a real search, and one process cannot find its own LAN session.
 *
 * The crafted candidates borrow a real session's info with port 0, so every join
 * genuinely runs and fails on address resolve - which is exactly what drives the
 * try-next-candidate loop to the end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCandidateTest, "EasySession.QuickMatch.TriesCandidatesInScoreOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionCandidateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionCandidateTest;

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
	Subsystem->OnSessionDestroyed.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleDestroyed);

	FEasySessionHostParams SeedParams;
	SeedParams.SessionDisplayName = TEXT("EasySession Candidate Seed");
	SeedParams.bIsLANMatch = true;
	SeedParams.bStartListening = false;
	Subsystem->CreateEasySession(SeedParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForCandidateRun(State));
	return true;
}

namespace EasyQuickMatchEventsTest
{
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		TOptional<EEasySessionResult> FirstResult;
		TOptional<EEasySessionResult> SecondResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyQuickMatchWaitEvents, TSharedPtr<EasyQuickMatchEventsTest::FTestState>, State);
bool FEasyQuickMatchWaitEvents::Update()
{
	using namespace EasyQuickMatchEventsTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UEasySessionTestEventListener* Listener = State->Listener.Get();

	if (FPlatformTime::Seconds() - State->StartTime > EasyQuickMatchTest::TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d."), State->Phase));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	switch (State->Phase)
	{
		case 0:
		{
			if (!State->FirstResult.IsSet() || Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The run ended in NoSessionsFound"), State->FirstResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestEqual(TEXT("Started fired once"), Listener->CountJournal(TEXT("Started")), 1);
			CurrentTest->TestEqual(TEXT("Completed fired once"), Listener->CountJournal(TEXT("Completed")), 1);
			CurrentTest->TestEqual(TEXT("Completed is the last entry"), Listener->QuickMatchJournal.Last(), FString(TEXT("Completed=NoSessionsFound")));
			CurrentTest->TestTrue(TEXT("The Complete state is broadcast before Completed"),
				Listener->QuickMatchJournal.Num() >= 2 && Listener->QuickMatchJournal[Listener->QuickMatchJournal.Num() - 2].EndsWith(TEXT(">Complete")));

			CurrentTest->TestTrue(TEXT("The heartbeat fired"), Listener->QuickMatchElapsedSeen.Num() >= 2);
			bool bNonDecreasing = true;
			for (int32 Index = 1; Index < Listener->QuickMatchElapsedSeen.Num(); ++Index)
			{
				bNonDecreasing &= Listener->QuickMatchElapsedSeen[Index] >= Listener->QuickMatchElapsedSeen[Index - 1];
			}
			CurrentTest->TestTrue(TEXT("Elapsed seconds never go backwards"), bNonDecreasing);
			CurrentTest->TestTrue(TEXT("Elapsed seconds actually count"), Listener->QuickMatchElapsedSeen.Last() >= 1);

			// Seed a session so the next run is refused at the door.
			FEasySessionHostParams SeedParams;
			SeedParams.SessionDisplayName = TEXT("EasySession Events Seed");
			SeedParams.bIsLANMatch = true;
			SeedParams.bStartListening = false;
			Subsystem->CreateEasySession(SeedParams);

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 1:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			// A refused run must still pair Started with Completed, or a spinner shown on Started never goes away.
			TSharedPtr<FTestState> Shared = State;
			FEasyQuickMatchParams Params;
			Params.Search.bLANQuery = true;
			Params.bAllowHostFallback = false;
			Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->SecondResult = Result;
				}));

			CurrentTest->TestTrue(TEXT("The refused run completed synchronously"), State->SecondResult.IsSet());
			CurrentTest->TestEqual(TEXT("It was refused as SessionAlreadyExists"), State->SecondResult.Get(EEasySessionResult::Success), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestEqual(TEXT("Started fired for it too"), Listener->CountJournal(TEXT("Started")), 2);
			CurrentTest->TestEqual(TEXT("And Completed followed"), Listener->CountJournal(TEXT("Completed")), 2);
			CurrentTest->TestEqual(TEXT("With the refusal as its result"), Listener->QuickMatchJournal.Last(), FString(TEXT("Completed=SessionAlreadyExists")));

			Subsystem->DestroyEasySession();
			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The subsystem's quick match events tell the whole story to a listener that bound
 * before any run existed: Started first, state changes and a once-a-second elapsed
 * heartbeat in between, Completed last. A run refused at the door keeps the pairing,
 * so a spinner shown on Started always sees the end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyQuickMatchEventsTest, "EasySession.QuickMatch.EventsFireInOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyQuickMatchEventsTest::RunTest(const FString& Parameters)
{
	using namespace EasyQuickMatchEventsTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Bound before any run exists - the point of subsystem-level events.
	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnQuickMatchStarted.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleQuickMatchStarted);
	Subsystem->OnQuickMatchStateChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleQuickMatchTransition);
	Subsystem->OnQuickMatchUpdated.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleQuickMatchUpdated);
	Subsystem->OnQuickMatchComplete.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleQuickMatchCompleted);

	TSharedPtr<FTestState> Shared = State;
	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;
	Params.bAllowHostFallback = false;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->FirstResult = Result;
		}));

	TestEqual(TEXT("Started is the first entry"),
		State->Listener->QuickMatchJournal.Num() > 0 ? State->Listener->QuickMatchJournal[0] : FString(), FString(TEXT("Started")));
	TestEqual(TEXT("The first transition leaves Idle"),
		State->Listener->QuickMatchJournal.Num() > 1 ? State->Listener->QuickMatchJournal[1] : FString(), FString(TEXT("State=Idle>Searching")));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyQuickMatchWaitEvents(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
