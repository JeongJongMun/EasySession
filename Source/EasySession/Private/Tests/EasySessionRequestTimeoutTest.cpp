// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionRequest.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Timeout rules of a queued request.
 *
 * The decision is a pure function of "now", so it can be checked directly with
 * fabricated timestamps. That matters because the NULL subsystem completes every
 * operation synchronously and can never produce a real timeout - without this
 * test the watchdog would ship unverified.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionRequestTimeoutTest, "EasySession.Subsystem.RequestTimeoutRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionRequestTimeoutTest::RunTest(const FString& Parameters)
{
	constexpr double StartTime = 1000.0;
	constexpr float ConfiguredTimeout = 30.0f;

	// A normal request uses the configured timeout as-is.
	{
		FEasySessionRequest Request(FEasySessionRequest::EType::Create);
		Request.MarkStarted(StartTime, ConfiguredTimeout);

		TestEqual(TEXT("Create uses the configured timeout"), Request.TimeoutSeconds, 30.0);
		TestFalse(TEXT("Not timed out right after starting"), Request.HasTimedOut(StartTime));
		TestFalse(TEXT("Not timed out just before the deadline"), Request.HasTimedOut(StartTime + 29.9));
		TestTrue(TEXT("Timed out exactly on the deadline"), Request.HasTimedOut(StartTime + 30.0));
		TestTrue(TEXT("Timed out past the deadline"), Request.HasTimedOut(StartTime + 60.0));
		TestEqual(TEXT("Elapsed time is reported"), Request.GetElapsedSeconds(StartTime + 5.0), 5.0);
	}

	// Searching adds the online service's own search budget on top.
	{
		FEasySessionRequest Request(FEasySessionRequest::EType::Find);
		Request.SearchParams.TimeoutSeconds = 15.0f;
		Request.MarkStarted(StartTime, ConfiguredTimeout);

		TestEqual(TEXT("Find adds the search timeout to the grace period"), Request.TimeoutSeconds, 45.0);
		TestFalse(TEXT("A healthy long search is not failed early"), Request.HasTimedOut(StartTime + 40.0));
		TestTrue(TEXT("A search past both budgets times out"), Request.HasTimedOut(StartTime + 45.0));
	}

	// A non-positive setting disables the deadline entirely.
	{
		FEasySessionRequest Request(FEasySessionRequest::EType::Join);
		Request.MarkStarted(StartTime, 0.0f);

		TestEqual(TEXT("Timeout is disabled"), Request.TimeoutSeconds, 0.0);
		TestFalse(TEXT("Never times out when disabled"), Request.HasTimedOut(StartTime + 100000.0));
	}

	// Only operations that can leave a session behind are worth cleaning up after.
	{
		TestTrue(TEXT("Create can leave a session behind"), FEasySessionRequest(FEasySessionRequest::EType::Create).CouldHaveCreatedSession());
		TestTrue(TEXT("Join can leave a session behind"), FEasySessionRequest(FEasySessionRequest::EType::Join).CouldHaveCreatedSession());
		TestFalse(TEXT("Destroy cannot"), FEasySessionRequest(FEasySessionRequest::EType::Destroy).CouldHaveCreatedSession());
		TestFalse(TEXT("Find cannot"), FEasySessionRequest(FEasySessionRequest::EType::Find).CouldHaveCreatedSession());
	}

	return true;
}

namespace EasySessionAbandonedCreateTest
{
	/** Maximum time to wait for each queued operation before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> RetryResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForAbandonedCreate, TSharedPtr<EasySessionAbandonedCreateTest::FTestState>, State);
bool FEasySessionWaitForAbandonedCreate::Update()
{
	using namespace EasySessionAbandonedCreateTest;

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

			// The session stands in for one a late create left behind after its deadline.
			FEasySessionTestAccess::CleanupAsAbandoned(*Subsystem, FEasySessionRequest::EType::Create);

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

			// The point of the branch: the leftover is gone and a new create is not blocked.
			FEasySessionHostParams Params;
			Params.SessionDisplayName = TEXT("EasySession Abandoned Create Retry");
			Params.bIsLANMatch = true;
			Params.bStartListening = false;
			Subsystem->CreateEasySession(Params, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->RetryResult = Result;
				}));

			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (!State->RetryResult.IsSet() || Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("A create after the abandoned one is not blocked"), State->RetryResult.GetValue(), EEasySessionResult::Success);

			Subsystem->DestroyEasySession();
			State->Phase = 3;
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
 * A create abandoned by the watchdog can leave a session behind, and CleanupRequest
 * destroys it so the next create starts clean - the behavior EasySessionSettings
 * promises for Request Timeout Seconds. NULL completes creates synchronously and can
 * never abandon one for real, so the cleanup is entered directly with the state the
 * watchdog would arrive with: a create-typed request, abandoned, session existing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAbandonedCreateTest, "EasySession.Subsystem.AbandonedCreateLeavesNoSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAbandonedCreateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionAbandonedCreateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasySessionHostParams Params;
	Params.SessionDisplayName = TEXT("EasySession Abandoned Create Test");
	Params.bIsLANMatch = true;
	Params.bStartListening = false;
	Subsystem->CreateEasySession(Params);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForAbandonedCreate(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
