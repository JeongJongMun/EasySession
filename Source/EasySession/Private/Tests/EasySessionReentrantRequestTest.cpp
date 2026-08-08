// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionReentrantRequestTest
{
	/** Hard limit on the whole run before the test gives up. */
	static constexpr double MaxWaitSeconds = 30.0;

	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingFirstStart,
		AwaitingReentrantPair,
		AwaitingDestroy,
		Done
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> PendingResult;

		/** The request started from inside the rejected one's failure callback. */
		TOptional<EEasySessionResult> ReentrantResult;
		FString ReentrantError;

		double StartTime = 0.0;
	};

	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Reentrant Request");
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		return Params;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForReentrantRequest, TSharedPtr<EasySessionReentrantRequestTest::FTestState>, State);
bool FEasySessionWaitForReentrantRequest::Update()
{
	using namespace EasySessionReentrantRequestTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

	if (!State->PendingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for a session operation."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	const EEasySessionResult Result = State->PendingResult.GetValue();
	State->PendingResult.Reset();
	State->StartTime = FPlatformTime::Seconds();

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			CurrentTest->TestEqual(TEXT("Session created"), Result, EEasySessionResult::Success);

			State->Step = EStep::AwaitingFirstStart;
			Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult InResult, const FString&) { Shared->PendingResult = InResult; }));
			return false;
		}

		case EStep::AwaitingFirstStart:
		{
			CurrentTest->TestEqual(TEXT("Match started"), Result, EEasySessionResult::Success);

			// Starting an already started match is rejected - reported by a callback
			// first and a false return after, so the plugin is still inside its own
			// StartSession call when the callback below runs.
			State->Step = EStep::AwaitingReentrantPair;
			Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared, Subsystem](EEasySessionResult InResult, const FString&)
				{
					Shared->PendingResult = InResult;

					// The shape every Blueprint uses: start something from On Failure.
					// A search because NULL does not finish that one on the spot, so it
					// is still active when the rejected start reports itself.
					FEasySessionSearchParams SearchParams;
					SearchParams.bLANQuery = true;
					Subsystem->FindEasySessions(SearchParams, FEasySessionFindCompleteDelegate::CreateLambda(
						[Shared](EEasySessionResult FindResult, const FString& FindError, const TArray<FEasySessionSearchResult>&)
						{
							Shared->ReentrantResult = FindResult;
							Shared->ReentrantError = FindError;
						}));
				}));
			return false;
		}

		case EStep::AwaitingReentrantPair:
		{
			CurrentTest->TestNotEqual(TEXT("Starting an already started match fails"), Result, EEasySessionResult::Success);

			if (!State->ReentrantResult.IsSet())
			{
				if (FPlatformTime::Seconds() - State->StartTime > MaxWaitSeconds)
				{
					CurrentTest->AddError(TEXT("The request started from the failure callback never completed."));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}

				// Put the result back so the guard at the top keeps waiting.
				State->PendingResult = Result;
				return false;
			}

			// The whole point. The rejected start must not land on the request that
			// began inside its failure callback.
			CurrentTest->TestFalse(
				FString::Printf(TEXT("The search does not carry the start's error (got '%s')"), *State->ReentrantError),
				State->ReentrantError.Contains(TEXT("StartSession")));
			CurrentTest->TestEqual(TEXT("The search runs on its own terms"), State->ReentrantResult.GetValue(), EEasySessionResult::Success);

			State->Step = EStep::AwaitingDestroy;
			Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult InResult, const FString&) { Shared->PendingResult = InResult; }));
			return false;
		}

		case EStep::AwaitingDestroy:
		default:
		{
			CurrentTest->TestEqual(TEXT("Session destroyed"), Result, EEasySessionResult::Success);
			State->Step = EStep::Done;
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * Starting a request from a failure callback is the first thing a Blueprint does.
 * The online service reports a rejected operation by calling back and only then
 * returning false, so the plugin is still inside its own online call when the new
 * request arrives - and the rejection it is about to report belongs to a request
 * that already finished.
 *
 * The request that starts from the callback must therefore never be the one that
 * receives it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReentrantRequestTest, "EasySession.Subsystem.RequestStartedFromAFailureCallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReentrantRequestTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReentrantRequestTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForReentrantRequest(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
