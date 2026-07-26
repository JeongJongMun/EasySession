// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasyMatchmakingTest
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

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitHostFallback, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitHostFallback::Update()
{
	using namespace EasyMatchmakingTest;

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
		CurrentTest->TestFalse(TEXT("Matchmaking no longer running"), Subsystem->IsMatchmaking());

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

	FEasyQuickMatchParams Params;
	Params.Search.bLANQuery = true;
	Params.Search.TimeoutSeconds = 5.0f;
	Params.Host.SessionDisplayName = TEXT("EasySession QuickMatch Test");
	Params.Host.bIsLANMatch = true;
	Params.Host.bStartListening = false;
	Params.MaxSearchPasses = 1;
	Params.DelayBetweenPassesSeconds = 0.0f;

	Subsystem->StartQuickMatch(Params, nullptr, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& ErrorMessage)
		{
			State->QuickMatchResult = Result;
		}));

	TestTrue(TEXT("Matchmaking is running"), Subsystem->IsMatchmaking());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitHostFallback(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasyMatchmakingWaitNoFallback, TSharedPtr<EasyMatchmakingTest::FTestState>, State);
bool FEasyMatchmakingWaitNoFallback::Update()
{
	using namespace EasyMatchmakingTest;

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
	ADD_LATENT_AUTOMATION_COMMAND(FEasyMatchmakingWaitNoFallback(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
