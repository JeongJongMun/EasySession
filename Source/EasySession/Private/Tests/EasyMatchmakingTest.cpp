// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasyMatchmakingTest
{
	/** Maximum time to wait for a Matchmaking run before failing the test. */
	static constexpr double TimeoutSeconds = 30.0;

	/** State shared between the test body and its latent commands. */
	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> MatchmakingResult;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingScoringTest, "EasySession.Matchmaking.DefaultScoring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingScoringTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

	const UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>();

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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitHostFallback, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitHostFallback::Update()
{
	using namespace EasyMatchmakingTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->MatchmakingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for Matchmaking to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("Matchmaking result"), State->MatchmakingResult.GetValue(), EEasySessionResult::Success);
		CurrentTest->TestTrue(TEXT("Fell back to hosting"), Subsystem->IsHost());
		CurrentTest->TestFalse(TEXT("Matchmaking no longer running"), Subsystem->IsMatchmakingRunning());

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
 * Host fallback test: with no session on the LAN, Matchmaking must exhaust its
 * search passes and then host its own session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingHostFallbackTest, "EasySession.Matchmaking.HostFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingHostFallbackTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession Matchmaking Test");
	Params.bAllowHostFallback = true;
	// The travel to this map aborts harmlessly - a headless test has no player controller to travel with.
	Params.Host.MapName = TEXT("ES_MatchmakingTestMap");
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->MatchmakingResult = Result;
		}));

	TestTrue(TEXT("Matchmaking is running"), Subsystem->IsMatchmakingRunning());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitHostFallback(State));
	return true;
}

/**
 * Host fallback without a map: Matchmaking accepts every parameter set Create accepts.
 *
 * An empty Map Name means "host where this player already is", which Create supports by
 * listening on the current map. Matchmaking used to refuse it at the door, so a graph that
 * dropped the node in without filling the params always failed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingHostFallbackWithoutMapTest, "EasySession.Matchmaking.HostFallbackWithoutAMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingHostFallbackWithoutMapTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession Matchmaking No Map Test");
	Params.bAllowHostFallback = true;
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;
	// Map Name is left empty on purpose. That is what a graph gets from the default struct.

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->MatchmakingResult = Result;
		}));

	TestTrue(TEXT("Matchmaking is running"), Subsystem->IsMatchmakingRunning());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitHostFallback(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitFallbackFilters, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitFallbackFilters::Update()
{
	using namespace EasyMatchmakingTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->MatchmakingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for Matchmaking to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("Matchmaking result"), State->MatchmakingResult.GetValue(), EEasySessionResult::Success);
		CurrentTest->TestTrue(TEXT("Fell back to hosting"), Subsystem->IsHost());

		// The live session must advertise what the search filtered on, or the next player's identical search skips this room.
		const FEasySessionHostParams ReadBack = Subsystem->GetSessionHostParams();
		CurrentTest->TestEqual(TEXT("The filtered key overwrote the host value"), ReadBack.CustomSettings.FindRef(TEXT("GameMode")), FString(TEXT("Deathmatch")));
		CurrentTest->TestEqual(TEXT("The filter-only key was added"), ReadBack.CustomSettings.FindRef(TEXT("Region")), FString(TEXT("KR")));
		CurrentTest->TestEqual(TEXT("The host-only key survived"), ReadBack.CustomSettings.FindRef(TEXT("MOTD")), FString(TEXT("Hello")));
		CurrentTest->TestEqual(TEXT("The fallback session advertises the searched region"), ReadBack.Region, EEasySessionRegion::EastAsia);

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingFallbackFiltersTest, "EasySession.Matchmaking.FallbackHostMatchesTheFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingFallbackFiltersTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Search.RequiredCustomSettings.Add(TEXT("GameMode"), TEXT("Deathmatch"));
	Params.Search.RequiredCustomSettings.Add(TEXT("Region"), TEXT("KR"));
	Params.Search.Region = EEasySessionRegion::EastAsia;
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

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->MatchmakingResult = Result;
		}));

	// The NULL service forces LAN on every created session, so the network half of the
	// fold is only observable here, on the params the fallback hands to Create.
	UEasyMatchmakingPolicy* Policy = Subsystem->GetActiveMatchmakingPolicy();
	if (TestNotNull(TEXT("The matchmaking policy is active"), Policy))
	{
		const FEasySessionHostParams Folded = FEasySessionTestAccess::MakeMatchmakingFallbackHostParams(*Policy);
		TestTrue(TEXT("The LAN search pulls the fallback onto the LAN"), Folded.bIsLANMatch);
		TestEqual(TEXT("The folded params carry the filtered value"), Folded.CustomSettings.FindRef(TEXT("GameMode")), FString(TEXT("Deathmatch")));
		TestEqual(TEXT("The folded params carry the searched region"), Folded.Region, EEasySessionRegion::EastAsia);
	}

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitFallbackFilters(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitNoFallback, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitNoFallback::Update()
{
	using namespace EasyMatchmakingTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();

	if (!State->MatchmakingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for Matchmaking to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	CurrentTest->TestEqual(TEXT("Matchmaking result"), State->MatchmakingResult.GetValue(), EEasySessionResult::NoSessionsFound);
	CurrentTest->TestFalse(TEXT("No session was created"), Subsystem->IsInSession());

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * Dedicated-server-client mode: with host fallback disabled, Matchmaking must fail
 * with NoSessionsFound instead of creating a session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingNoFallbackTest, "EasySession.Matchmaking.NoFallbackFails", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingNoFallbackTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.bAllowHostFallback = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->MatchmakingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitNoFallback(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitCanceledUndo, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitCanceledUndo::Update()
{
	using namespace EasyMatchmakingTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->MatchmakingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for Matchmaking to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	if (!State->bCleanupIssued)
	{
		CurrentTest->TestEqual(TEXT("Matchmaking result"), State->MatchmakingResult.GetValue(), EEasySessionResult::Canceled);
		CurrentTest->TestFalse(TEXT("Matchmaking no longer running"), Subsystem->IsMatchmakingRunning());

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingCancelUndoTest, "EasySession.Matchmaking.CancelUndoesTheFallbackHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingCancelUndoTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

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
	State->Listener->CancelMatchmakingOnHosting = Subsystem;

	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession Cancel Undo Test");
	Params.bAllowHostFallback = true;
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->MatchmakingResult = Result;
		}));

	UEasyMatchmakingPolicy* Policy = Subsystem->GetActiveMatchmakingPolicy();
	if (!TestNotNull(TEXT("Active matchmaking policy is available"), Policy))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}
	Policy->OnStateChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleMatchmakingState);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitCanceledUndo(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitAlreadyInSession, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitAlreadyInSession::Update()
{
	using namespace EasyMatchmakingTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->bCleanupIssued)
	{
		// Still creating the session the matchmaking is supposed to trip over.
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
		Subsystem->StartMatchmaking(FEasyMatchmakingParams(), nullptr, FEasySessionCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult Result, const FString& ErrorMessage)
			{
				Shared->MatchmakingResult = Result;
			}));

		// The refusal happens before the first search, so the result is already here.
		CurrentTest->TestTrue(TEXT("Refused before any step ran"), State->MatchmakingResult.IsSet());
		if (State->MatchmakingResult.IsSet())
		{
			CurrentTest->TestEqual(TEXT("Matchmaking result"), State->MatchmakingResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
		}
		CurrentTest->TestFalse(TEXT("Matchmaking no longer running"), Subsystem->IsMatchmakingRunning());
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
 * Create or Join - dooms every matchmaking step to SessionAlreadyExists. The run must
 * report that once and stop, instead of burning candidates and passes against it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingAlreadyInSessionTest, "EasySession.Matchmaking.RefusesWhenAlreadyInASession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingAlreadyInSessionTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTest;

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
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitAlreadyInSession(State));
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

		TOptional<EEasySessionResult> MatchmakingResult;
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

			FEasyMatchmakingParams Params;
			Params.Search.bLANQuery = true;
			// A short first pass and a long inter-pass delay open a quiet window to inject into.
			Params.Search.TimeoutSeconds = 0.5f;
			Params.MaxSearchPasses = 2;
			Params.DelayBetweenPassesSeconds = 10.0f;
			Params.bAllowHostFallback = false;

			Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->MatchmakingResult = Result;
				}));

			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (State->MatchmakingResult.IsSet())
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
			if (FEasySessionTestAccess::HasActiveSearch(*Subsystem) || Subsystem->GetMatchmakingState() != EEasyMatchmakingState::Searching)
			{
				return false;
			}

			UEasyMatchmakingPolicy* Policy = Subsystem->GetActiveMatchmakingPolicy();
			if (!CurrentTest->TestNotNull(TEXT("The matchmaking policy is active"), Policy))
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
			FEasySessionTestAccess::DriveMatchmakingSearch(*Policy, Crafted);

			const TArray<FEasySessionSearchResult> Candidates = FEasySessionTestAccess::GetMatchmakingCandidates(*Policy);
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
			if (!State->MatchmakingResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Exhausting every candidate without fallback ends in NoSessionsFound"),
				State->MatchmakingResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestEqual(TEXT("Every candidate was actually tried - each join left one cleanup destroy"),
				State->Listener->DestroyedResults.Num() - State->DestroysBeforeInjection, 3);

			UEasyMatchmakingPolicy* Policy = Subsystem->GetActiveMatchmakingPolicy();
			if (Policy != nullptr)
			{
				// One key, not three: the crafted candidates share the seed session's id.
				CurrentTest->TestEqual(TEXT("The refused session is on the no-retry ledger"),
					FEasySessionTestAccess::GetMatchmakingFailedSessionKeys(*Policy).Num(), 1);
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The half of Matchmaking after a search returns rooms: password rooms are excluded,
 * candidates are tried best score first, and a refused room lands on the no-retry
 * ledger. None of it ran under automation before, because the only way results entered
 * the policy was a real search, and one process cannot find its own LAN session.
 *
 * The crafted candidates borrow a real session's info with port 0, so every join
 * genuinely runs and fails on address resolve - which is exactly what drives the
 * try-next-candidate loop to the end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionCandidateTest, "EasySession.Matchmaking.TriesCandidatesInScoreOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
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

namespace EasyMatchmakingEventsTest
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitEvents, TSharedPtr<EasyMatchmakingEventsTest::FTestState>, State);
bool FEasyMatchmakingWaitEvents::Update()
{
	using namespace EasyMatchmakingEventsTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UEasySessionTestEventListener* Listener = State->Listener.Get();

	if (FPlatformTime::Seconds() - State->StartTime > EasyMatchmakingTest::TimeoutSeconds)
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
			CurrentTest->TestEqual(TEXT("Completed is the last entry"), Listener->MatchmakingJournal.Last(), FString(TEXT("Completed=NoSessionsFound")));
			CurrentTest->TestTrue(TEXT("The Complete state is broadcast before Completed"),
				Listener->MatchmakingJournal.Num() >= 2 && Listener->MatchmakingJournal[Listener->MatchmakingJournal.Num() - 2].EndsWith(TEXT(">Complete")));

			CurrentTest->TestTrue(TEXT("The heartbeat fired"), Listener->MatchmakingElapsedSeen.Num() >= 2);
			bool bNonDecreasing = true;
			for (int32 Index = 1; Index < Listener->MatchmakingElapsedSeen.Num(); ++Index)
			{
				bNonDecreasing &= Listener->MatchmakingElapsedSeen[Index] >= Listener->MatchmakingElapsedSeen[Index - 1];
			}
			CurrentTest->TestTrue(TEXT("Elapsed seconds never go backwards"), bNonDecreasing);
			CurrentTest->TestTrue(TEXT("Elapsed seconds actually count"), Listener->MatchmakingElapsedSeen.Last() >= 1);

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
			FEasyMatchmakingParams Params;
			Params.Search.bLANQuery = true;
			Params.bAllowHostFallback = false;
			Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->SecondResult = Result;
				}));

			CurrentTest->TestTrue(TEXT("The refused run completed synchronously"), State->SecondResult.IsSet());
			CurrentTest->TestEqual(TEXT("It was refused as SessionAlreadyExists"), State->SecondResult.Get(EEasySessionResult::Success), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestEqual(TEXT("Started fired for it too"), Listener->CountJournal(TEXT("Started")), 2);
			CurrentTest->TestEqual(TEXT("And Completed followed"), Listener->CountJournal(TEXT("Completed")), 2);
			CurrentTest->TestEqual(TEXT("With the refusal as its result"), Listener->MatchmakingJournal.Last(), FString(TEXT("Completed=SessionAlreadyExists")));

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
 * The subsystem's matchmaking events tell the whole story to a listener that bound
 * before any run existed: Started first, state changes and a once-a-second elapsed
 * heartbeat in between, Completed last. A run refused at the door keeps the pairing,
 * so a spinner shown on Started always sees the end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingEventsTest, "EasySession.Matchmaking.EventsFireInOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingEventsTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingEventsTest;

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
	Subsystem->OnMatchmakingStarted.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleMatchmakingStarted);
	Subsystem->OnMatchmakingStateChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleMatchmakingTransition);
	Subsystem->OnMatchmakingUpdated.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleMatchmakingUpdated);
	Subsystem->OnMatchmakingComplete.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleMatchmakingCompleted);

	TSharedPtr<FTestState> Shared = State;
	FEasyMatchmakingParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;
	Params.bAllowHostFallback = false;

	Subsystem->StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->FirstResult = Result;
		}));

	TestEqual(TEXT("Started is the first entry"),
		State->Listener->MatchmakingJournal.Num() > 0 ? State->Listener->MatchmakingJournal[0] : FString(), FString(TEXT("Started")));
	TestEqual(TEXT("The first transition leaves Idle"),
		State->Listener->MatchmakingJournal.Num() > 1 ? State->Listener->MatchmakingJournal[1] : FString(), FString(TEXT("State=Idle>Searching")));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitEvents(State));
	return true;
}


namespace EasyMatchmakingTargetedTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		/** A joinable-but-unreachable result borrowed from the coded room. */
		FOnlineSessionSearchResult BaseResult;

		/** The code the room advertises. */
		FString JoinCode;

		/** Cleanup destroys recorded before the matchmaking runs began. */
		int32 DestroysBaseline = 0;

		TOptional<EEasySessionResult> NoPasswordResult;
		TOptional<EEasySessionResult> WithPasswordResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};

	/** Start a targeted run hunting the coded room, reporting into OutResult. */
	void StartTargetedRun(UEasySessionSubsystem& Subsystem, const FString& Code, const FString& Password, const TSharedPtr<FTestState>& State, TOptional<EEasySessionResult>& OutResult)
	{
		FEasyMatchmakingParams Params;
		Params.Search.bLANQuery = true;
		Params.Search.TimeoutSeconds = 5.0f;
		Params.Search.JoinCode = Code;
		Params.JoinPassword = Password;
		Params.MaxSearchPasses = 1;
		Params.DelayBetweenPassesSeconds = 0.0f;
		Params.bAllowHostFallback = false;

		TOptional<EEasySessionResult>* Result = &OutResult;
		Subsystem.StartMatchmaking(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
			[State, Result](EEasySessionResult InResult, const FString&)
			{
				*Result = InResult;
			}));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitTargeted, TSharedPtr<EasyMatchmakingTargetedTest::FTestState>, State);
bool FEasyMatchmakingWaitTargeted::Update()
{
	using namespace EasyMatchmakingTargetedTest;

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

			State->JoinCode = Subsystem->GetSessionJoinCode();
			CurrentTest->TestEqual(TEXT("The room advertises a code"), State->JoinCode.Len(), 6);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted result is joinable"), State->BaseResult.IsValid()))
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
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			State->DestroysBaseline = State->Listener->DestroyedResults.Num();

			// Without the password, the coded room is found but never becomes a candidate.
			StartTargetedRun(*Subsystem, State->JoinCode, FString(), State, State->NoPasswordResult);
			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (!State->NoPasswordResult.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Without a password the run finds nothing to join"), State->NoPasswordResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestEqual(TEXT("And no join was attempted"), State->Listener->DestroyedResults.Num() - State->DestroysBaseline, 0);

			// With the password, the locked room becomes a candidate and a join genuinely runs.
			StartTargetedRun(*Subsystem, State->JoinCode, TEXT("secret"), State, State->WithPasswordResult);
			State->Phase = 3;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (!State->WithPasswordResult.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			// The join fails on address resolve and the run ends empty-handed - but the cleanup destroy proves the room was genuinely tried.
			CurrentTest->TestEqual(TEXT("The run still ends in NoSessionsFound"), State->WithPasswordResult.GetValue(), EEasySessionResult::NoSessionsFound);
			CurrentTest->TestEqual(TEXT("The password opened the coded room for one real join attempt"), State->Listener->DestroyedResults.Num() - State->DestroysBaseline, 1);

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * Targeted matchmaking hunts one specific room: a Join Code in the search params finds
 * the hidden, password protected room, and Join Password decides whether it may become
 * a candidate. The injected result stands in for the search, and the cleanup destroy
 * after the failed resolve is the proof that a join was genuinely attempted.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasyMatchmakingTargetedTest, "EasySession.Matchmaking.PasswordOpensTheCodedRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasyMatchmakingTargetedTest::RunTest(const FString& Parameters)
{
	using namespace EasyMatchmakingTargetedTest;

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

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Targeted Room");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	HostParams.bHidden = true;
	HostParams.bUseJoinCode = true;
	HostParams.Password = TEXT("secret");
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitTargeted(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
