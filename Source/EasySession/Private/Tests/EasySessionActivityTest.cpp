// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionActivityTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create, update and start, destroy sequence the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingIdleAfterCreate,
		AwaitingUpdateAndStart,
		AwaitingDestroy
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> CreateResult;
		TOptional<EEasySessionResult> UpdateResult;
		TOptional<EEasySessionResult> StartResult;
		TOptional<EEasySessionResult> DestroyResult;
		double StartTime = 0.0;
	};

	/** Create a game instance with a listener bound to both events, or return false when the subsystem is missing. */
	static bool Begin(FAutomationTestBase& Test, FTestState& State)
	{
		State.GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
		EasySessionTest::InitializeGameInstance(State.GameInstance);

		UEasySessionSubsystem* Subsystem = State.GameInstance->GetSubsystem<UEasySessionSubsystem>();
		if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
		{
			EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
			return false;
		}

		State.Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
		Subsystem->OnBusyChanged.AddDynamic(State.Listener.Get(), &UEasySessionTestEventListener::HandleBusyChanged);
		Subsystem->OnActivityChanged.AddDynamic(State.Listener.Get(), &UEasySessionTestEventListener::HandleActivityChanged);
		State.StartTime = FPlatformTime::Seconds();
		return true;
	}

	static FEasySessionHostParams MakeHostParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Activity Test");
		Params.MaxPlayers = 4;
		Params.bIsLANMatch = true;
		Params.InitialMapName = EasySessionTest::SessionMapName;
		return Params;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForBusyDefinition, TSharedPtr<EasySessionActivityTest::FTestState>, State);
bool FEasySessionWaitForBusyDefinition::Update()
{
	using namespace EasySessionActivityTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UEasySessionTestEventListener* Listener = State->Listener.Get();

	// State is a member of this latent command, and a lambda cannot capture a member.
	TSharedPtr<FTestState> Shared = State;

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(TEXT("Timed out waiting for a session request to complete."));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			if (!State->CreateResult.IsSet())
			{
				return false;
			}

			if (!CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			State->Step = EStep::AwaitingIdleAfterCreate;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case EStep::AwaitingIdleAfterCreate:
		{
			// The ticker reports the end of the create on a later tick, so the next step starts from empty lists.
			if (EasySessionTest::HasActivity(Subsystem) || Listener->BusyValues.IsEmpty() || Listener->BusyValues.Last())
			{
				return false;
			}
			Listener->BusyValues.Reset();
			Listener->Activities.Reset();

			FEasySessionSettings Settings = Subsystem->GetSessionSettings();
			Settings.MaxPlayers += 1;
			Subsystem->UpdateSession(Settings, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->UpdateResult = Result;
				}));

			CurrentTest->TestFalse(TEXT("A queued update does not make the subsystem busy"), Subsystem->IsBusy());
			CurrentTest->TestEqual(TEXT("Get Activity names the update"), Subsystem->GetActivity(), EEasySessionActivity::Updating);
			CurrentTest->TestTrue(TEXT("On Activity Changed fires Updating inside the call that queued it"),
				Listener->Activities == TArray<EEasySessionActivity>{ EEasySessionActivity::Updating });

			Subsystem->StartSession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->StartResult = Result;
				}));

			CurrentTest->TestFalse(TEXT("A start queued behind it does not make the subsystem busy either"), Subsystem->IsBusy());
			CurrentTest->TestEqual(TEXT("On Busy Changed does not fire for the update or the start"), Listener->BusyValues.Num(), 0);

			State->Step = EStep::AwaitingUpdateAndStart;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case EStep::AwaitingUpdateAndStart:
		{
			if (!State->UpdateResult.IsSet() || !State->StartResult.IsSet() || EasySessionTest::HasActivity(Subsystem)
				|| Listener->Activities.IsEmpty() || Listener->Activities.Last() != EEasySessionActivity::None)
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The update succeeded"), State->UpdateResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestEqual(TEXT("The start succeeded"), State->StartResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestEqual(TEXT("On Busy Changed never fired for them"), Listener->BusyValues.Num(), 0);

			// Both requests can complete on one tick, so Starting may be reported or skipped.
			for (const EEasySessionActivity Activity : Listener->Activities)
			{
				CurrentTest->TestTrue(TEXT("Only the update, the start and the idle queue are reported"),
					Activity == EEasySessionActivity::Updating || Activity == EEasySessionActivity::Starting || Activity == EEasySessionActivity::None);
			}

			Listener->BusyValues.Reset();
			Subsystem->DestroySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->DestroyResult = Result;
				}));

			CurrentTest->TestTrue(TEXT("A destroy changes the session, so it makes the subsystem busy"), Subsystem->IsBusy());
			CurrentTest->TestEqual(TEXT("Get Activity names the destroy"), Subsystem->GetActivity(), EEasySessionActivity::Leaving);
			CurrentTest->TestTrue(TEXT("On Busy Changed fires true inside the call that queued it"), Listener->BusyValues == TArray<bool>{ true });

			State->Step = EStep::AwaitingDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case EStep::AwaitingDestroy:
		{
			if (!State->DestroyResult.IsSet() || Listener->BusyValues.Last())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Session destroyed"), State->DestroyResult.GetValue(), EEasySessionResult::Success);
			CurrentTest->TestTrue(TEXT("On Busy Changed fired true, then false"), Listener->BusyValues == TArray<bool>{ true, false });
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}

		default:
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
	}
}

/**
 * Is Busy counts only the requests that change the session or the map.
 * An update and a start queued together leave it false, fire no On Busy Changed, and are still reported by Get Activity and On Activity Changed.
 * A destroy makes the subsystem busy inside the call that queued it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBusyDefinitionTest, "EasySession.Subsystem.BusyCountsOnlySessionAndMapChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBusyDefinitionTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionActivityTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (!Begin(*this, *State))
	{
		return false;
	}

	TSharedPtr<FTestState> Shared = State;
	State->GameInstance->GetSubsystem<UEasySessionSubsystem>()->CreateSession(MakeHostParams(), FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->CreateResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForBusyDefinition(State));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForReentrantActivity, TSharedPtr<EasySessionActivityTest::FTestState>, State);
bool FEasySessionWaitForReentrantActivity::Update()
{
	using namespace EasySessionActivityTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UEasySessionTestEventListener* Listener = State->Listener.Get();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(TEXT("Timed out waiting for the create and the destroy queued from the activity event."));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	// The destroy is queued from the first None, so the test is over once the listener stopped waiting for it and the queue is idle again.
	if (Listener->DestroyOnFirstNone || EasySessionTest::HasActivity(Subsystem) || Listener->BusyValues.IsEmpty() || Listener->BusyValues.Last())
	{
		return false;
	}

	CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.Get(EEasySessionResult::Canceled), EEasySessionResult::Success);
	CurrentTest->TestFalse(TEXT("The destroy queued from the event ran"), Subsystem->IsInSession());
	CurrentTest->TestTrue(TEXT("The activities are the create, the idle queue, the destroy and the idle queue"),
		Listener->Activities == TArray<EEasySessionActivity>{ EEasySessionActivity::Creating, EEasySessionActivity::None, EEasySessionActivity::Leaving, EEasySessionActivity::None });

	// A busy value read before the activity event would have reported false after the destroy had already made the subsystem busy again.
	CurrentTest->TestTrue(TEXT("On Busy Changed fired true once and false once"), Listener->BusyValues == TArray<bool>{ true, false });

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * A handler of On Activity Changed may queue a request inside the event.
 * The busy value reported after that event is the one that includes the new request, so On Busy Changed never reports a stale false.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReentrantActivityTest, "EasySession.Subsystem.ActivityEventMayQueueARequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReentrantActivityTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionActivityTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	if (!Begin(*this, *State))
	{
		return false;
	}

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	State->Listener->DestroyOnFirstNone = Subsystem;

	TSharedPtr<FTestState> Shared = State;
	Subsystem->CreateSession(MakeHostParams(), FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->CreateResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForReentrantActivity(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
