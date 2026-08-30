// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionRegionTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	/** The searches to run against the EastAsia session, and what each should list. */
	struct FSearchCase
	{
		EEasySessionRegion Filter;
		int32 ExpectedCount;
		const TCHAR* What;
	};

	static const FSearchCase SearchCases[] =
	{
		{ EEasySessionRegion::Any,      1, TEXT("No region filter lists the session") },
		{ EEasySessionRegion::EastAsia, 1, TEXT("The matching region filter lists it") },
		{ EEasySessionRegion::Europe,   0, TEXT("A different region filter drops it") },
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** A joinable result borrowed from the session, injected as what a search would have returned. */
		FOnlineSessionSearchResult BaseResult;

		/** Which entry of SearchCases is running. */
		int32 CaseIndex = 0;

		/** Whether the current case's find was started. */
		bool bFindStarted = false;

		/** How many sessions the current case's find delivered. Unset while it runs. */
		TOptional<int32> DeliveredCount;

		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForRegionRun, TSharedPtr<EasySessionRegionTest::FTestState>, State);
bool FEasySessionWaitForRegionRun::Update()
{
	using namespace EasySessionRegionTest;

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

			CurrentTest->TestEqual(TEXT("The advertised region reads back"), Subsystem->GetSessionSettings().Region, EEasySessionRegion::EastAsia);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted result is joinable"), State->BaseResult.IsValid()))
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
			// One search case per pass: start the find, feed it the crafted result once it is
			// actually running (the queue executes on its own tick), then judge what it listed.
			if (!State->bFindStarted)
			{
				FEasySessionSearchParams Params;
				Params.bLANQuery = true;
				Params.Region = SearchCases[State->CaseIndex].Filter;
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

			const FSearchCase& Case = SearchCases[State->CaseIndex];
			CurrentTest->TestEqual(Case.What, State->DeliveredCount.GetValue(), Case.ExpectedCount);

			State->bFindStarted = false;
			State->DeliveredCount.Reset();
			State->StartTime = FPlatformTime::Seconds();
			if (++State->CaseIndex < UE_ARRAY_COUNT(SearchCases))
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
 * The advertised region round-trips and the search filter honors it: no filter and the
 * matching filter list the session, any other region drops it. The results are injected
 * because one process cannot find its own LAN session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionRegionFilterTest, "EasySession.Search.FiltersByRegion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionRegionFilterTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionRegionTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Region Host");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	HostParams.Region = EEasySessionRegion::EastAsia;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForRegionRun(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
