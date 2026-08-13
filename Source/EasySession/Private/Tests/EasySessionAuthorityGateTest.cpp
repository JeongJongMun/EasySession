// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSettings.h"
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

	/** Builds host params that create a session record without opening a server. */
	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Gate Host");
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
			// Baseline first: while this process holds the session it created, match
			// control must keep working exactly as before.
			if (!State->CreateResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
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

			Subsystem->EndEasySession(FEasySessionCompleteDelegate::CreateLambda(
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

			// Now the same calls from the other side of the fence: a game that holds a
			// session it did not create - what a joined client looks like.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			Subsystem->StartEasySession(FEasySessionCompleteDelegate::CreateLambda(
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

			// The point of the test: this used to complete with Success while only
			// flipping the local session copy, starting nothing.
			CurrentTest->TestEqual(TEXT("Start without authority is refused"),
				State->StartWithoutAuthorityResult.GetValue(), EEasySessionResult::RequiresSessionAuthority);

			Subsystem->UpdateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
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

			// The synchronous path has to refuse too - nothing outside the function
			// stops a client from reaching it.
			CurrentTest->TestFalse(TEXT("ServerTravelToMap without authority is refused"),
				Subsystem->ServerTravelToMap(TEXT("ES13_NoSuchMap")));

			// Leaving a session another process created must stay allowed - that is how a
			// client leaves - so this call itself doubles as an assertion.
			Subsystem->DestroyEasySession();
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
 * Match control needs session authority, not a hosting player. A game that did not
 * create the session (a joined client) must get a clear refusal instead of a Success
 * that only flipped its local session copy - and the refusal must name the reason,
 * because StateChangeFailure would read as the online service saying no.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionAuthorityGateTest, "EasySession.Authority.MatchControlNeedsSessionAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionAuthorityGateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionAuthorityGateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// A refused travel must not browse anywhere, and a headless world has no menu to
	// return to either way.
	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForAuthorityGates(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
