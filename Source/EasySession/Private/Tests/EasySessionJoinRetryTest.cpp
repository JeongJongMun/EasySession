// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSessionSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionJoinRetryTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The unreachable session to join. It shares the setup session's info, which outlives that session's destruction. */
		FEasySessionSearchResult FakeResult;

		TOptional<EEasySessionResult> FirstResult;
		TOptional<EEasySessionResult> RetryResult;

		enum class EStep { AwaitingSetupCreate, AwaitingSetupDestroy, AwaitingJoins };
		EStep Step = EStep::AwaitingSetupCreate;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionJoinRetryStep, TSharedPtr<EasySessionJoinRetryTest::FTestState>, State);
bool FEasySessionJoinRetryStep::Update()
{
	using namespace EasySessionJoinRetryTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingSetupCreate:
		{
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

			State->FakeResult.NativeResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted search result is joinable"), State->FakeResult.NativeResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			Subsystem->DestroyEasySession();
			State->Step = FTestState::EStep::AwaitingSetupDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingSetupDestroy:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
				{
					CurrentTest->AddError(TEXT("Timed out waiting for the setup destroy."));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}
				return false;
			}

			TSharedPtr<FTestState> Shared = State;
			UEasySessionSubsystem* SubsystemForRetry = Subsystem;
			Subsystem->JoinEasySession(State->FakeResult, FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared, SubsystemForRetry](EEasySessionResult Result, const FString& /*ErrorMessage*/)
				{
					Shared->FirstResult = Result;

					// The retry the guide invites: started inside the failure callback, before this callstack unwinds.
					SubsystemForRetry->JoinEasySession(Shared->FakeResult, FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
						[Shared](EEasySessionResult RetryResultValue, const FString& /*ErrorMessage*/)
						{
							Shared->RetryResult = RetryResultValue;
						}));
				}));

			State->Step = FTestState::EStep::AwaitingJoins;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingJoins:
		{
			if (!State->RetryResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
				{
					CurrentTest->AddError(FString::Printf(TEXT("Timed out waiting for the join and its retry. Queue: %s"), *Subsystem->GetQueueStatus()));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}
				return false;
			}

			CurrentTest->TestEqual(TEXT("The first join fails on address resolve"),
				State->FirstResult.Get(EEasySessionResult::Success), EEasySessionResult::ResolveFailure);
			CurrentTest->TestNotEqual(TEXT("The retry is not thrown away on the half-joined session"),
				State->RetryResult.GetValue(), EEasySessionResult::SessionAlreadyExists);
			CurrentTest->TestEqual(TEXT("The retry fails for the honest reason instead"),
				State->RetryResult.GetValue(), EEasySessionResult::ResolveFailure);
			CurrentTest->TestFalse(TEXT("No session is left behind"), Subsystem->IsInSession());

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A join that fails on address resolve leaves a half-joined session for one queued cleanup.
 * That cleanup has to enter the queue before the failure callback runs, or a retry the
 * callback starts lands in front of it and dies on SessionAlreadyExists - against the
 * guide's promise that the player can retry right away.
 *
 * The unreachable session is real: created without listening, it advertises this process's
 * address with port 0, and its live session info is borrowed into a search result before
 * the setup session is destroyed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinRetryTest, "EasySession.Join.RetryAfterResolveFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinRetryTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionJoinRetryTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Join Retry Test");
	HostParams.bIsLANMatch = true;
	// No map and no listening: the session advertises this process's address with port 0, the unreachable-host shape.
	HostParams.bStartListening = false;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionJoinRetryStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
