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

namespace EasySessionJoinFromSessionTest
{
	// Maximum time to wait for each step before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	/** Which join from inside a session a test runs. */
	enum class ECase : uint8
	{
		/** The host's reservation beacon approves the join. */
		Approved,

		/** The session to join advertises no reservation beacon. */
		HostCannotBeAsked,

		/** The joining player hosts a match in progress. */
		HostOfMatchInProgress
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The session to join. Its info is copied before it is destroyed, so the second session gets a different session id. */
		FEasySessionSearchResult TargetResult;

		/** The id of the session the player holds when the join starts. */
		FString CurrentSessionId;

		TOptional<EEasySessionResult> JoinResult;

		ECase Case = ECase::Approved;

		enum class EStep { AwaitingTargetCreate, AwaitingTargetDestroy, AwaitingCurrentCreate, AwaitingStart, AwaitingApproval, AwaitingJoin, AwaitingCleanup };
		EStep Step = EStep::AwaitingTargetCreate;
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

	// Create a session with this display name, as the join target or as the session the join starts from.
	void CreateSession(UEasySessionSubsystem& Subsystem, const TCHAR* DisplayName)
	{
		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = DisplayName;
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		Subsystem.CreateSession(HostParams);
	}

	// Start the join of the target session.
	// The approved case then waits to approve it.
	void StartJoin(TSharedPtr<FTestState> State, UEasySessionSubsystem& Subsystem)
	{
		State->CurrentSessionId = FEasySessionTestAccess::GetCurrentSessionIdString(Subsystem);

		TSharedPtr<FTestState> Shared = State;
		Subsystem.JoinSession(State->TargetResult, FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
			[Shared](EEasySessionResult Result, const FString& /*ErrorMessage*/)
			{
				Shared->JoinResult = Result;
			}));

		State->Step = State->Case == ECase::Approved ? FTestState::EStep::AwaitingApproval : FTestState::EStep::AwaitingJoin;
		State->StartTime = FPlatformTime::Seconds();
	}

	// Check the result and the session state of the finished join, for the case this test runs.
	void CheckJoinResult(const FTestState& State, UEasySessionSubsystem& Subsystem)
	{
		FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
		const EEasySessionResult Result = State.JoinResult.GetValue();

		if (State.Case == ECase::Approved)
		{
			CurrentTest->TestEqual(TEXT("It leaves the current session and fails on the address instead"), Result, EEasySessionResult::ResolveFailure);
			CurrentTest->TestFalse(TEXT("No session is left behind"), Subsystem.IsInSession());

			// A player who destroyed their session for a join that then failed has no map to stay on, so the request travels to the menu.
			CurrentTest->TestTrue(TEXT("The trip to the menu is on its way"), Subsystem.IsBusy());
			return;
		}

		const EEasySessionResult Expected = State.Case == ECase::HostCannotBeAsked ? EEasySessionResult::JoinRefused : EEasySessionResult::SessionAlreadyExists;
		CurrentTest->TestEqual(TEXT("The join is refused before leaving"), Result, Expected);
		CurrentTest->TestEqual(TEXT("The player still holds the session it was in"),
			FEasySessionTestAccess::GetCurrentSessionIdString(Subsystem), State.CurrentSessionId);
	}

	bool Begin(FAutomationTestBase& Test, ECase Case);
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionJoinFromSessionStep, TSharedPtr<EasySessionJoinFromSessionTest::FTestState>, State);
bool FEasySessionJoinFromSessionStep::Update()
{
	using namespace EasySessionJoinFromSessionTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingTargetCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the target create"));
			}

			State->TargetResult.NativeResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted search result is joinable"), State->TargetResult.NativeResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// The approved case asks a reservation beacon first, and a join asks one only for a session that advertises the reservations key.
			if (State->Case == ECase::Approved)
			{
				State->TargetResult.NativeResult.Session.SessionSettings.Set(EasySession::SettingKey_Reservations, 1, EOnlineDataAdvertisementType::ViaOnlineService);
			}

			Subsystem->DestroySession();
			State->Step = FTestState::EStep::AwaitingTargetDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingTargetDestroy:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the target destroy"));
			}

			// The session the player is in when the join starts. The online subsystem gives it a new session id.
			CreateSession(*Subsystem, TEXT("EasySession Join From Session Test"));
			State->Step = FTestState::EStep::AwaitingCurrentCreate;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingCurrentCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the current create"));
			}

			CurrentTest->TestNotEqual(TEXT("The two sessions have different ids, so this is not a join of the session already held"),
				FEasySessionTestAccess::GetCurrentSessionIdString(*Subsystem), State->TargetResult.NativeResult.GetSessionIdStr());

			if (State->Case == ECase::HostOfMatchInProgress)
			{
				Subsystem->StartSession();
				State->Step = FTestState::EStep::AwaitingStart;
				State->StartTime = FPlatformTime::Seconds();
				return false;
			}

			StartJoin(State, *Subsystem);
			return false;
		}

		case FTestState::EStep::AwaitingStart:
		{
			if (Subsystem->GetSessionState() != EEasySessionState::InProgress || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the match start"));
			}

			StartJoin(State, *Subsystem);
			return false;
		}

		case FTestState::EStep::AwaitingApproval:
		{
			// The queue starts the join on its next tick, and only then does the join wait for the reservation beacon.
			if (!FEasySessionTestAccess::ApproveRunningJoin(*Subsystem))
			{
				if (State->JoinResult.IsSet())
				{
					CurrentTest->AddError(TEXT("The join completed without waiting for the reservation beacon."));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}

				return TimedOut(State, TEXT("the join to wait for the reservation beacon"));
			}

			State->Step = FTestState::EStep::AwaitingJoin;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingJoin:
		{
			if (!State->JoinResult.IsSet())
			{
				return TimedOut(State, TEXT("the join"));
			}

			// The approved case destroys its session, so the test waits until the session is gone.
			if (State->Case == ECase::Approved && Subsystem->IsInSession())
			{
				return TimedOut(State, TEXT("the current session to be left"));
			}

			CheckJoinResult(*State, *Subsystem);

			// The refused cases still hold a session, and the online subsystem outlives this world, so the next test would find it.
			if (Subsystem->IsInSession())
			{
				Subsystem->DestroySession();
				State->Step = FTestState::EStep::AwaitingCleanup;
				State->StartTime = FPlatformTime::Seconds();
				return false;
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}

		case FTestState::EStep::AwaitingCleanup:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the cleanup destroy"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

bool EasySessionJoinFromSessionTest::Begin(FAutomationTestBase& Test, ECase Case)
{
	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->Case = Case;
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	CreateSession(*Subsystem, TEXT("EasySession Join Target Test"));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionJoinFromSessionStep(State));
	return true;
}

/**
 * Joining from inside another session destroys that session once the host approved the join, rather than refusing it.
 * JoinSession covers both cases, so an accepted invite needs no second function and no flag that allows destroying the current session.
 *
 * The target session is unreachable on purpose: created without listening, it advertises this process's address with port 0.
 * Its info is copied into a search result before it is destroyed, so the session the player then holds has a different session id.
 * That difference is what separates this from joining the session already held, which stays refused with SessionAlreadyExists.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinFromSessionTest, "EasySession.Join.LeavesTheCurrentSessionAfterApproval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinFromSessionTest::RunTest(const FString& Parameters)
{
	return EasySessionJoinFromSessionTest::Begin(*this, EasySessionJoinFromSessionTest::ECase::Approved);
}

/**
 * A player in a session keeps it when the join cannot ask a reservation beacon.
 * Destroying the session first and being refused on arrival would drop that player from a session the join never replaced.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinUnaskedTest, "EasySession.Join.StaysWhenTheHostCannotBeAsked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinUnaskedTest::RunTest(const FString& Parameters)
{
	return EasySessionJoinFromSessionTest::Begin(*this, EasySessionJoinFromSessionTest::ECase::HostCannotBeAsked);
}

/**
 * The host of a match in progress is refused, because the join would destroy the host's session.
 * That would end the match for every player in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinFromMatchTest, "EasySession.Join.AHostStaysInItsMatchInProgress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinFromMatchTest::RunTest(const FString& Parameters)
{
	return EasySessionJoinFromSessionTest::Begin(*this, EasySessionJoinFromSessionTest::ECase::HostOfMatchInProgress);
}

#endif // WITH_DEV_AUTOMATION_TESTS
