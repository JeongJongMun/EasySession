// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSettings.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionReplicatedStateTest
{
	/** Maximum time to wait for the session to come up before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
		TOptional<EEasySessionResult> CreateResult;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Replicated State");
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForReplicatedState, TSharedPtr<EasySessionReplicatedStateTest::FTestState>, State);
bool FEasySessionWaitForReplicatedState::Update()
{
	using namespace EasySessionReplicatedStateTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->CreateResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the session to be created."));
			Finish(*State);
			return true;
		}
		return false;
	}

	CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

	// Stand in for a client: a game that holds a session someone else serves. The
	// join path is what normally clears this, and a headless test has no host to
	// join, so the outcome of that path is set directly.
	Subsystem->SetCreatedActiveSessionForTesting(false);

	// Watch the events a game binds to. Nothing below is a user request, so nothing
	// below should reach these.
	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionStarted.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleStarted);
	Subsystem->OnSessionEnded.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleEnded);

	// The host started the match, and the state actor replicated that to this client.
	Subsystem->HandleReplicatedHostSessionState(EEasySessionState::InProgress);

	CurrentTest->TestEqual(TEXT("A replicated match start is cached for the client to read"),
		Subsystem->GetReplicatedHostSessionStateForTesting(), EEasySessionState::InProgress);
	CurrentTest->TestTrue(
		FString::Printf(TEXT("A replicated match start raises no match events (saw: %s)"), *State->Listener->Describe()),
		State->Listener->TotalEvents() == 0);

	// And then the host ended it. A client that never saw InProgress locally used to
	// replay the whole history here, raising a started and an ended event back to back.
	Subsystem->HandleReplicatedHostSessionState(EEasySessionState::Ended);

	CurrentTest->TestEqual(TEXT("A replicated match end is cached for the client to read"),
		Subsystem->GetReplicatedHostSessionStateForTesting(), EEasySessionState::Ended);
	CurrentTest->TestTrue(
		FString::Printf(TEXT("A replicated match end raises no match events (saw: %s)"), *State->Listener->Describe()),
		State->Listener->TotalEvents() == 0);

	// Destroying the game instance does not take the session with it - the online
	// service holds sessions per process, so one left behind fails the next test's
	// create with SessionAlreadyExists. Leaving is allowed without authority, which
	// is what a client does anyway.
	Subsystem->DestroyEasySession();

	Finish(*State);
	return true;
}

/**
 * A client only reads the host's match state - it never acts on it. Feeding the
 * replicated state back into Start/End would put a request on the queue that the
 * client has no authority to run, and the failure would leave through the public
 * match events, telling a game its match failed to start when nobody asked for one.
 *
 * What this can and cannot show: the handler ignores anything that is not an actual
 * client (net mode NM_Client), and a headless test world is standalone, so this
 * guards the contract - the host's state is recorded, and recording it raises no
 * match events - rather than reproducing the failure. The failure itself needs two
 * connected games and is checked by hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReplicatedStateTest, "EasySession.Replication.ClientDoesNotActOnTheHostsMatchState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReplicatedStateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReplicatedStateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForReplicatedState(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
