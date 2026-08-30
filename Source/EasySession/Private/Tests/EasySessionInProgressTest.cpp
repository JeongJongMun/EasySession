// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionInProgressTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** A joinable result borrowed from the session, taken fresh after each state change. */
		FOnlineSessionSearchResult BaseResult;

		/** What the current find delivered. Unset while it runs. */
		TOptional<TArray<FEasySessionSearchResult>> Delivered;

		int32 Phase = 0;
		double StartTime = 0.0;
	};

	/** Start a find whose completion lands in State->Delivered. The injection happens in the wait that follows. */
	void StartFind(UEasySessionSubsystem& Subsystem, const TSharedPtr<FTestState>& State, bool bIncludeInProgress)
	{
		State->Delivered.Reset();

		FEasySessionSearchParams Params;
		Params.bLANQuery = true;
		Params.bIncludeInProgressSessions = bIncludeInProgress;
		Subsystem.FindEasySessions(Params, FEasySessionFindCompleteDelegate::CreateLambda(
			[State](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
			{
				State->Delivered = Result == EEasySessionResult::Success ? Results : TArray<FEasySessionSearchResult>();
			}));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForInProgressRun, TSharedPtr<EasySessionInProgressTest::FTestState>, State);
bool FEasySessionWaitForInProgressRun::Update()
{
	using namespace EasySessionInProgressTest;

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

			CurrentTest->TestEqual(TEXT("A fresh session advertises no running match"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_MatchInProgress), 0);

			Subsystem->StartEasySession();
			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 1:
		{
			if (Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Start flips the advertised flag on"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_MatchInProgress), 1);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			StartFind(*Subsystem, State, /*bIncludeInProgress*/ true);
			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (!State->Delivered.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			CurrentTest->TestEqual(TEXT("The default search lists the in-match session"), State->Delivered->Num(), 1);
			if (State->Delivered->Num() == 1)
			{
				CurrentTest->TestTrue(TEXT("And the result says the match is running"), (*State->Delivered)[0].bMatchInProgress);
			}

			StartFind(*Subsystem, State, /*bIncludeInProgress*/ false);
			State->Phase = 3;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 3:
		{
			if (!State->Delivered.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			CurrentTest->TestEqual(TEXT("Excluding in-progress sessions drops it"), State->Delivered->Num(), 0);

			Subsystem->EndEasySession();
			State->Phase = 4;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 4:
		{
			if (Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("End flips the advertised flag back off"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_MatchInProgress), 0);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			StartFind(*Subsystem, State, /*bIncludeInProgress*/ false);
			State->Phase = 5;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 5:
		{
			if (!State->Delivered.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			CurrentTest->TestEqual(TEXT("The ended session is listed again without the filter tripping"), State->Delivered->Num(), 1);

			Subsystem->DestroyEasySession();
			State->Phase = 6;
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
 * The session state never rides the wire, so the advertised in-match key stands in for
 * it: Create writes it off, Start flips it on, End flips it back, and the search filter
 * plus the result flag read it. The results are injected because one process cannot
 * find its own LAN session.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionInProgressFilterTest, "EasySession.Search.FiltersInProgressSessions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionInProgressFilterTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionInProgressTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession In Progress Host");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForInProgressRun(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
