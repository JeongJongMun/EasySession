// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionRequest.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
