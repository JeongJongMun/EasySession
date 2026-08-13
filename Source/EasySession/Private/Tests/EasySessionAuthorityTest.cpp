// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSettings.h"
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

	/** Builds host params that create a session record without opening a server. */
	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Authority Host");
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		return Params;
	}

	/**
	 * Reproduce what a dedicated server on Steam looks like from this plugin's side.
	 *
	 * Two independent facts make IsHost() false there, and this clears the only one a
	 * headless test would otherwise still have: NULL sets bHosting when it creates a
	 * session (OnlineSessionInterfaceNull.cpp, CreateSession) while Steam never writes
	 * the field at all. The other fact needs no setup - a test game instance has no
	 * local player either, so the owner-id comparison cannot succeed.
	 */
	static bool ClearHostingFlag(UGameInstance* GameInstance)
	{
		const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GameInstance->GetWorld());
		if (!Sessions.IsValid())
		{
			return false;
		}

		FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(NAME_GameSession);
		if (NamedSession == nullptr)
		{
			return false;
		}

		NamedSession->bHosting = false;
		return true;
	}

	static void Finish(FTestState& State)
	{
		GetMutableDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect = State.bAutoReturnWasEnabled;
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

			// Take away the flag the online service would not have set anyway.
			CurrentTest->TestTrue(TEXT("Hosting flag cleared"), ClearHostingFlag(State->GameInstance.Get()));

			// With no flag and no local player there is no hosting player to find, which
			// is exactly the state a dedicated server is in. Losing this must not cost
			// the process its authority over the session it created.
			CurrentTest->TestFalse(TEXT("No hosting player is reported"), Subsystem->IsHost());
			CurrentTest->TestTrue(TEXT("Session still exists"), Subsystem->IsInSession());

			Subsystem->DestroyEasySessionForEveryone(FText::FromString(TEXT("Server shutting the match down")));
			State->Phase = 1;
			return false;

		default:
			// The point of the test: this used to be refused with a warning, because the
			// authority check asked whether a local player owned the session.
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
 * Server authority must not depend on there being a hosting player. A dedicated server
 * has no local player, and Steam never sets the session's hosting flag, so the two
 * things IsHost() looks at are both absent there - yet that process is still the
 * server of the session it created.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDedicatedAuthorityTest, "EasySession.Authority.ServerKeepsAuthorityWithoutHostingPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDedicatedAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionAuthorityTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// Tearing the session down ends with a return to the menu, which a headless test
	// world has no use for. Turning it off leaves the session cleanup on trial.
	UEasySessionSettings* Settings = GetMutableDefault<UEasySessionSettings>();
	State->bAutoReturnWasEnabled = Settings->bAutoReturnToMenuOnDisconnect;
	Settings->bAutoReturnToMenuOnDisconnect = false;

	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForAuthorityTeardown(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
