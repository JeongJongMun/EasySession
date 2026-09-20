// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionConfig.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionAuthorityTest
{
	/** Maximum time to wait for the session to be created and destroyed before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		int32 Phase = 0;
		TOptional<EEasySessionResult> CreateResult;
		double StartTime = 0.0;
		bool bAutoReturnWasEnabled = true;
	};

	/** Builds host params that create a named session without opening a server. */
	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Authority Host");
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForAuthorityTeardown, TSharedPtr<EasySessionAuthorityTest::FTestState>, State);
bool FEasySessionWaitForAuthorityTeardown::Update()
{
	using namespace EasySessionAuthorityTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(
			TEXT("Timed out in phase %d. A server that created the session must still be able to destroy it when it has no hosting player."),
			State->Phase));
		Finish(*State);
		return true;
	}

	switch (State->Phase)
	{
		case 0:
			if (!State->CreateResult.IsSet())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			// A test game instance has no local player, which is the state a dedicated server is in.
			// The process still created the session, so it keeps the authority over it.
			CurrentTest->TestEqual(TEXT("There is no local player"), State->GameInstance->GetNumLocalPlayers(), 0);
			CurrentTest->TestTrue(TEXT("The process that created the session has the authority"), Subsystem->IsSessionAuthority());
			CurrentTest->TestTrue(TEXT("Session still exists"), Subsystem->IsInSession());

			Subsystem->DestroySessionForEveryone(FText::FromString(TEXT("Server shutting the match down")));
			State->Phase = 1;
			return false;

		default:
			// The point of the test: this used to be refused with a warning, because the authority check asked whether a local player owned the session.
			if (Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Session was destroyed"), Subsystem->GetSessionState(), EEasySessionState::NoSession);
			Finish(*State);
			return true;
	}
}

/**
 * Server authority must not depend on there being a hosting player.
 * A dedicated server has no local player, yet that process is still the server of the session it created.
 * The authority is FNamedOnlineSession's bHosting, which FEasySessionHost sets when the create completes, so no local player is involved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDedicatedAuthorityTest, "EasySession.Authority.ServerKeepsAuthorityWithoutHostingPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDedicatedAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionAuthorityTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Destroying the session ends with a travel to the menu, which a headless test world has no use for.
	// Turning it off leaves the session cleanup, which is what this test checks.
	UEasySessionConfig* Settings = GetMutableDefault<UEasySessionConfig>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateSession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForAuthorityTeardown(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
