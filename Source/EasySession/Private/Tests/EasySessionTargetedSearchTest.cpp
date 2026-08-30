// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionTargetedSearchTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		TOptional<EEasySessionResult> LookupResult;
		int32 DeliveredCount = -1;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForSessionIdLookup, TSharedPtr<EasySessionTargetedSearchTest::FTestState>, State);
bool FEasySessionWaitForSessionIdLookup::Update()
{
	using namespace EasySessionTargetedSearchTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out. Queue: %s"), *Subsystem->GetQueueStatus()));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	if (!State->LookupResult.IsSet() || Subsystem->IsBusy())
	{
		return false;
	}

	// NULL answers the by-id query inside the call with "no session" - the queued request completes cleanly instead of erroring.
	CurrentTest->TestEqual(TEXT("The queued by-id lookup completes"), State->LookupResult.GetValue(), EEasySessionResult::Success);
	CurrentTest->TestEqual(TEXT("With no session for the id"), State->DeliveredCount, 0);
	CurrentTest->TestEqual(TEXT("And off the public search event"), State->Listener->FoundBroadcasts(), 0);
	CurrentTest->TestEqual(TEXT("And off the public cache"), Subsystem->GetLastSearchResults().Num(), 0);

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * The by-id lookup is a queue request like any other search: it occupies the queue
 * while it runs, completes through the request completion path, and - as a targeted
 * query - never reaches the public search event or cache.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSessionIdLookupTest, "EasySession.Search.SessionIdLookupRidesTheQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSessionIdLookupTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionTargetedSearchTest;

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
	Subsystem->OnSessionsFound.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSessionsFound);

	UWorld* World = State->GameInstance->GetWorld();
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
	const FUniqueNetIdPtr SessionId = Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionFakeSessionId")) : nullptr;
	if (!TestTrue(TEXT("A fake session id could be made"), SessionId.IsValid()))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	TSharedPtr<FTestState> Shared = State;
	FEasySessionSearchParams LookupParams;
	LookupParams.SessionId = SessionId;
	Subsystem->FindEasySessions(LookupParams, FEasySessionFindCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
		{
			Shared->LookupResult = Result;
			Shared->DeliveredCount = Results.Num();
		}));

	TestTrue(TEXT("The lookup occupies the queue"), Subsystem->IsBusy());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSessionIdLookup(State));
	return true;
}

namespace EasySessionOwnerFilterTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** A joinable result borrowed from the session, injected as what a search would have returned. */
		FOnlineSessionSearchResult BaseResult;

		/** 0 = matching owner, 1 = different owner. */
		int32 CaseIndex = 0;

		/** Whether the current case's find was started. */
		bool bFindStarted = false;

		/** How many sessions the current case's find delivered. Unset while it runs. */
		TOptional<int32> DeliveredCount;

		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForOwnerFilterRun, TSharedPtr<EasySessionOwnerFilterTest::FTestState>, State);
bool FEasySessionWaitForOwnerFilterRun::Update()
{
	using namespace EasySessionOwnerFilterTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d, case %d. Queue: %s"), State->Phase, State->CaseIndex, *Subsystem->GetQueueStatus()));
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
			if (!CurrentTest->TestTrue(TEXT("The crafted result carries an owner"), State->BaseResult.Session.OwningUserId.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 1:
		{
			if (!State->bFindStarted)
			{
				const UWorld* World = State->GameInstance->GetWorld();
				const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);

				FEasySessionSearchParams Params;
				Params.bLANQuery = true;
				Params.OwnerId = State->CaseIndex == 0
					? State->BaseResult.Session.OwningUserId
					: (Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionSomeoneElse")) : nullptr);
				Subsystem->FindEasySessions(Params, FEasySessionFindCompleteDelegate::CreateLambda(
					[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
					{
						Shared->DeliveredCount = Result == EEasySessionResult::Success ? Results.Num() : -1;
					}));
				State->bFindStarted = true;
				return false;
			}

			if (!State->DeliveredCount.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			if (State->CaseIndex == 0)
			{
				CurrentTest->TestEqual(TEXT("The matching owner filter lists the session"), State->DeliveredCount.GetValue(), 1);
			}
			else
			{
				CurrentTest->TestEqual(TEXT("A different owner filter drops it"), State->DeliveredCount.GetValue(), 0);
			}

			State->bFindStarted = false;
			State->DeliveredCount.Reset();
			State->StartTime = FPlatformTime::Seconds();
			if (++State->CaseIndex < 2)
			{
				return false;
			}

			Subsystem->DestroyEasySession();
			State->Phase = 2;
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
 * The owner filter narrows a discovery search to one host's sessions: the matching
 * owner id lists the session, any other drops it. The results are injected because
 * one process cannot find its own LAN session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionOwnerFilterTest, "EasySession.Search.FiltersByOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionOwnerFilterTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionOwnerFilterTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Owner Host");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForOwnerFilterRun(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
