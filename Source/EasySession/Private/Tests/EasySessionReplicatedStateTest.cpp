// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionConfig.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
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
		TOptional<EEasySessionResult> CreateResult;
		TOptional<EEasySessionResult> DestroyResult;
		bool bCleanupIssued = false;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Replicated State");
		Params.bIsLANMatch = true;
		Params.InitialMapName = EasySessionTest::SessionMapName;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForReplicatedState, TSharedPtr<EasySessionReplicatedStateTest::FTestState>, State);
bool FEasySessionWaitForReplicatedState::Update()
{
	using namespace EasySessionReplicatedStateTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	// Second pass onwards: waiting for the cleanup issued at the end of the first.
	if (State->bCleanupIssued)
	{
		if (!State->DestroyResult.IsSet())
		{
			if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
			{
				CurrentTest->AddError(TEXT("Timed out waiting for the session to be destroyed."));
				Finish(*State);
				return true;
			}
			return false;
		}

		CurrentTest->TestEqual(TEXT("Session destroyed"), State->DestroyResult.GetValue(), EEasySessionResult::Success);
		Finish(*State);
		return true;
	}

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

	// Stand in for a client: a game that holds a session another process created.
	// The join path is what normally clears this, and a headless test has no host to join, so the outcome of that path is set directly.
	FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

	// The host started the match, and the state actor replicated that to this client.
	Subsystem->HandleReplicatedSessionState(EEasySessionState::InProgress);

	CurrentTest->TestEqual(TEXT("A replicated match start is cached for the client to read"),
		FEasySessionTestAccess::GetReplicatedSessionState(*Subsystem), EEasySessionState::InProgress);

	// And then the host ended it.
	// A client that never saw InProgress locally records Ended directly, without replaying the states in between.
	Subsystem->HandleReplicatedSessionState(EEasySessionState::Ended);

	CurrentTest->TestEqual(TEXT("A replicated match end is cached for the client to read"),
		FEasySessionTestAccess::GetReplicatedSessionState(*Subsystem), EEasySessionState::Ended);

	// Destroying the game instance does not take the session with it.
	// The online subsystem holds sessions per process, so one left behind fails the next test's create with SessionAlreadyExists.
	// Waited on rather than started and ignored: requests start on a later tick, so returning here would end the test first.
	TSharedPtr<FTestState> Shared = State;
	State->bCleanupIssued = true;
	State->StartTime = FPlatformTime::Seconds();
	Subsystem->DestroySession(FEasySessionCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&)
		{
			Shared->DestroyResult = Result;
		}));

	return false;
}

/**
 * A client only reads the host's match state.
 * It never acts on it.
 * Passing the replicated state to StartSession or EndSession would queue a request the client has no authority to run.
 * That request would fail with RequiresSessionAuthority, although the game asked for no match change.
 *
 * HandleReplicatedSessionState records the host's state in any net mode, and only GetSessionState checks for NM_Client.
 * A headless test world is standalone, so this test only checks the recorded value through FEasySessionTestAccess::GetReplicatedSessionState.
 * The failure itself needs two connected games and is checked by hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReplicatedStateTest, "EasySession.Replication.ClientDoesNotActOnTheHostsMatchState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReplicatedStateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReplicatedStateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateSession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForReplicatedState(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
