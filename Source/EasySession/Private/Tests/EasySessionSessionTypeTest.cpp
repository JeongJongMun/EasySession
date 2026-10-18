// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSessionSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSessionTypeTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** This test's own session as a search would return it. */
		FOnlineSessionSearchResult GameSession;

		/** The same session without the session type key, as a session of another project looks. */
		FOnlineSessionSearchResult OtherProjectSession;

		TOptional<TArray<FEasySessionSearchResult>> FoundSessions;
		TOptional<EEasySessionResult> JoinResult;

		enum class EStep { AwaitingCreate, AwaitingSearch, AwaitingFind, AwaitingJoin, AwaitingDestroy };
		EStep Step = EStep::AwaitingCreate;
		double StartTime = 0.0;
	};

	// Whether the step timed out. Reports the error and destroys the game instance when it did.
	bool TimedOut(TSharedPtr<FTestState> State, const TCHAR* What)
	{
		if (FPlatformTime::Seconds() - State->StartTime <= TimeoutSeconds)
		{
			return false;
		}

		FAutomationTestFramework::Get().GetCurrentTest()->AddError(FString::Printf(TEXT("Timed out waiting for %s."), What));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	void NextStep(TSharedPtr<FTestState> State, FTestState::EStep Step)
	{
		State->Step = Step;
		State->StartTime = FPlatformTime::Seconds();
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionSessionTypeStep, TSharedPtr<EasySessionSessionTypeTest::FTestState>, State);
bool FEasySessionSessionTypeStep::Update()
{
	using namespace EasySessionSessionTypeTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create"));
			}

			State->GameSession = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem, NAME_GameSession);
			CurrentTest->TestEqual(TEXT("A game session advertises its session type"),
				EasySession::GetAdvertisedSessionType(State->GameSession.Session.SessionSettings), NAME_GameSession);
			CurrentTest->TestTrue(TEXT("The session type key is kept out of Custom Settings"), EasySession::IsReservedSettingKey(EasySession::SettingKey_SessionType));

			State->OtherProjectSession = State->GameSession;
			State->OtherProjectSession.Session.SessionSettings.Remove(EasySession::SettingKey_SessionType);
			CurrentTest->TestEqual(TEXT("A session without the key has no session type"),
				EasySession::GetAdvertisedSessionType(State->OtherProjectSession.Session.SessionSettings), NAME_None);

			FEasySessionSearchParams Params;
			Params.bLANQuery = true;

			TSharedPtr<FTestState> Shared = State;
			Subsystem->FindSessions(EEasySessionType::GameSession, Params, FEasySessionFindCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& Results)
				{
					Shared->FoundSessions = Results;
				}));

			NextStep(State, FTestState::EStep::AwaitingSearch);
			return false;
		}

		case FTestState::EStep::AwaitingSearch:
		{
			// The queue starts the search on its next tick.
			const TSharedPtr<FOnlineSessionSearch> Search = FEasySessionTestAccess::GetActiveSearch(*Subsystem);
			if (!Search.IsValid())
			{
				return TimedOut(State, TEXT("the search to start"));
			}

			FString QueriedType;
			CurrentTest->TestTrue(TEXT("The session type filter goes to the online service"), Search->QuerySettings.Get(EasySession::SettingKey_SessionType, QueriedType));
			CurrentTest->TestEqual(TEXT("It asks for game sessions"), QueriedType, FName(NAME_GameSession).ToString());

			// NULL ignores the query settings, so both sessions come back and only the check on the results can drop one.
			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->GameSession, State->OtherProjectSession });
			NextStep(State, FTestState::EStep::AwaitingFind);
			return false;
		}

		case FTestState::EStep::AwaitingFind:
		{
			if (!State->FoundSessions.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the search to complete"));
			}

			CurrentTest->TestEqual(TEXT("The session of another project is dropped from the results"), State->FoundSessions.GetValue().Num(), 1);

			TSharedPtr<FTestState> Shared = State;
			Subsystem->JoinSession(FEasySessionSearchResult::FromNative(State->OtherProjectSession), FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString& /*ErrorMessage*/)
				{
					Shared->JoinResult = Result;
				}));

			NextStep(State, FTestState::EStep::AwaitingJoin);
			return false;
		}

		case FTestState::EStep::AwaitingJoin:
		{
			if (!State->JoinResult.IsSet())
			{
				return TimedOut(State, TEXT("the join to complete"));
			}

			CurrentTest->TestEqual(TEXT("Join Easy Session refuses a session of another project"), State->JoinResult.GetValue(), EEasySessionResult::InvalidParams);
			CurrentTest->TestTrue(TEXT("The refused join leaves this player in the session"), Subsystem->IsInSession());

			// Destroyed through the plugin and waited on, so a failed destroy shows up as this test's failure.
			Subsystem->DestroySession();
			NextStep(State, FTestState::EStep::AwaitingDestroy);
			return false;
		}

		case FTestState::EStep::AwaitingDestroy:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the destroy"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * Every EasySession session advertises the named session it was created as, and searches and joins ask for it.
 * A session of another project that shares the AppId has no such key.
 * Without the check, a search returned it and the join traveled to a server that never completed the connection.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSessionTypeTest, "EasySession.Search.OtherProjectSessionsAreDropped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSessionTypeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSessionTypeTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasySessionHostParams Params;
	Params.SessionDisplayName = TEXT("EasySession Session Type Test");
	Params.bIsLANMatch = true;
	Params.InitialMapName = EasySessionTest::SessionMapName;
	Subsystem->CreateSession(Params);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionSessionTypeStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
