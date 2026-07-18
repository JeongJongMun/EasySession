// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"

namespace EasySessionSmokeTest
{
	/** Session name used by the smoke test. Must not collide with real game sessions. */
	static const FName TestSessionName = TEXT("EasySessionSmokeTestSession");

	/** Maximum time to wait for an async session operation before failing the test. */
	static constexpr double TimeoutSeconds = 10.0;

	/** State shared between the test body and its latent commands. */
	struct FSmokeState
	{
		IOnlineSessionPtr SessionInterface;
		FDelegateHandle CreateHandle;
		FDelegateHandle DestroyHandle;
		double WaitStartTime = 0.0;
		bool bCreateDone = false;
		bool bCreateSuccess = false;
		bool bDestroyDone = false;
		bool bDestroySuccess = false;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitCreateThenDestroy, TSharedPtr<EasySessionSmokeTest::FSmokeState>, State);
bool FEasySessionWaitCreateThenDestroy::Update()
{
	using namespace EasySessionSmokeTest;

	if (!State->bCreateDone)
	{
		if (FPlatformTime::Seconds() - State->WaitStartTime > TimeoutSeconds)
		{
			FAutomationTestFramework::Get().GetCurrentTest()->AddError(TEXT("Timed out waiting for CreateSession to complete."));
			State->bCreateDone = true;
			State->bDestroyDone = true;
			return true;
		}
		return false;
	}

	FAutomationTestFramework::Get().GetCurrentTest()->TestTrue(TEXT("CreateSession completed successfully"), State->bCreateSuccess);

	State->WaitStartTime = FPlatformTime::Seconds();
	State->SessionInterface->DestroySession(TestSessionName);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitDestroy, TSharedPtr<EasySessionSmokeTest::FSmokeState>, State);
bool FEasySessionWaitDestroy::Update()
{
	using namespace EasySessionSmokeTest;

	if (!State->bDestroyDone)
	{
		if (FPlatformTime::Seconds() - State->WaitStartTime > TimeoutSeconds)
		{
			FAutomationTestFramework::Get().GetCurrentTest()->AddError(TEXT("Timed out waiting for DestroySession to complete."));
			State->bDestroyDone = true;
			return true;
		}
		return false;
	}

	FAutomationTestFramework::Get().GetCurrentTest()->TestTrue(TEXT("DestroySession completed successfully"), State->bDestroySuccess);

	State->SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(State->CreateHandle);
	State->SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(State->DestroyHandle);
	return true;
}

/**
 * Phase 0 smoke test: create and destroy an empty session on the NULL online subsystem.
 * Verifies that the OSS module chain is loaded and the session interface round-trips.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNullSmokeTest, "EasySession.Smoke.NullCreateDestroy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNullSmokeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSmokeTest;

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (!TestNotNull(TEXT("Online subsystem is available"), OnlineSub))
	{
		return false;
	}

	TSharedPtr<FSmokeState> State = MakeShared<FSmokeState>();
	State->SessionInterface = OnlineSub->GetSessionInterface();
	if (!TestTrue(TEXT("Session interface is valid"), State->SessionInterface.IsValid()))
	{
		return false;
	}

	State->CreateHandle = State->SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateLambda([State](FName SessionName, bool bWasSuccessful)
		{
			if (SessionName == TestSessionName)
			{
				State->bCreateDone = true;
				State->bCreateSuccess = bWasSuccessful;
			}
		}));

	State->DestroyHandle = State->SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateLambda([State](FName SessionName, bool bWasSuccessful)
		{
			if (SessionName == TestSessionName)
			{
				State->bDestroyDone = true;
				State->bDestroySuccess = bWasSuccessful;
			}
		}));

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = 2;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = false;
	Settings.bAllowJoinInProgress = true;

	State->WaitStartTime = FPlatformTime::Seconds();
	if (!TestTrue(TEXT("CreateSession request was issued"), State->SessionInterface->CreateSession(0, TestSessionName, Settings)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitCreateThenDestroy(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitDestroy(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
