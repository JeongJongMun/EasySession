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

namespace EasySessionAuthorityGateTest
{
	/** Maximum time to wait for the whole sequence before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		int32 Phase = 0;
		TOptional<EEasySessionResult> CreateResult;
		TOptional<EEasySessionResult> StartResult;
		TOptional<EEasySessionResult> EndResult;
		TOptional<EEasySessionResult> StartWithoutAuthorityResult;
		TOptional<EEasySessionResult> UpdateWithoutAuthorityResult;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	/** Builds host params that create a named session without opening a server. */
	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Gate Host");
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForAuthorityGates, TSharedPtr<EasySessionAuthorityGateTest::FTestState>, State);
bool FEasySessionWaitForAuthorityGates::Update()
{
	using namespace EasySessionAuthorityGateTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d."), State->Phase));
		Finish(*State);
		return true;
	}

	switch (State->Phase)
	{
		case 0:
			// Baseline first: while this process holds the session it created, StartSession and EndSession must complete with Success.
			if (!State->CreateResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			Subsystem->StartSession(FEasySessionCompleteDelegate::CreateLambda(
				[State = State](EEasySessionResult Result, const FString&)
				{
					State->StartResult = Result;
				}));
			State->Phase = 1;
			return false;

		case 1:
			if (!State->StartResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Authority can start the match"), State->StartResult.GetValue(), EEasySessionResult::Success);

			Subsystem->EndSession(FEasySessionCompleteDelegate::CreateLambda(
				[State = State](EEasySessionResult Result, const FString&)
				{
					State->EndResult = Result;
				}));
			State->Phase = 2;
			return false;

		case 2:
			if (!State->EndResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Authority can end the match"), State->EndResult.GetValue(), EEasySessionResult::Success);

			// Now the same calls from a game that holds a session it did not create, which is what a joined client looks like.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			Subsystem->StartSession(FEasySessionCompleteDelegate::CreateLambda(
				[State = State](EEasySessionResult Result, const FString&)
				{
					State->StartWithoutAuthorityResult = Result;
				}));
			State->Phase = 3;
			return false;

		case 3:
			if (!State->StartWithoutAuthorityResult.IsSet())
			{
				return false;
			}

			// The point of the test: this used to complete with Success while it only changed the state of the local session copy and started nothing.
			CurrentTest->TestEqual(TEXT("Start without authority is refused"),
				State->StartWithoutAuthorityResult.GetValue(), EEasySessionResult::RequiresSessionAuthority);

			Subsystem->UpdateSession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
				[State = State](EEasySessionResult Result, const FString&)
				{
					State->UpdateWithoutAuthorityResult = Result;
				}));
			State->Phase = 4;
			return false;

		case 4:
			if (!State->UpdateWithoutAuthorityResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Update without authority is refused"),
				State->UpdateWithoutAuthorityResult.GetValue(), EEasySessionResult::RequiresSessionAuthority);

			// The synchronous path has to refuse too.
			// Nothing outside the function stops a client from reaching it.
			CurrentTest->TestFalse(TEXT("ServerTravel without authority is refused"),
				Subsystem->ServerTravel(TEXT("ES13_NoSuchMap")));

			// DestroySession on a session another process created must stay allowed, because that is how a client destroys its session.
			// The last phase waits until no session exists, so a refused destroy fails the test on the timeout.
			Subsystem->DestroySession();
			State->Phase = 5;
			return false;

		default:
			if (Subsystem->IsInSession())
			{
				return false;
			}
			Finish(*State);
			return true;
	}
}

/**
 * Match control needs session authority, not a hosting player.
 * A game that did not create the session is a joined client, and it must get a clear refusal.
 * What it used to get was a Success that only changed its local session copy.
 * The refusal must name the reason, because StateChangeFailure would read as the online subsystem refusing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAuthorityGateTest, "EasySession.Authority.MatchControlNeedsSessionAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAuthorityGateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionAuthorityGateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// A refused travel must not browse anywhere, and a headless world has no menu to return to either way.
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateSession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForAuthorityGates(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
