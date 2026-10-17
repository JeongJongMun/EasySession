// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionHostWithoutPlayerTest
{
	/** Maximum time to wait for the session to be created and destroyed before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		int32 Phase = 0;
		TOptional<EEasySessionResult> CreateResult;
		double StartTime = 0.0;
	};

	/** Builds host params that create a named session without opening a server. */
	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Host Without Player");
		Params.bIsLANMatch = true;
		Params.InitialMapName = EasySessionTest::SessionMapName;
		return Params;
	}

	static void Finish(FTestState& State)
	{
		EasySessionTest::DestroyGameInstance(State.GameInstance.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForHostWithoutPlayer, TSharedPtr<EasySessionHostWithoutPlayerTest::FTestState>, State);
bool FEasySessionWaitForHostWithoutPlayer::Update()
{
	using namespace EasySessionHostWithoutPlayerTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(
			TEXT("Timed out in phase %d. A game that created the session must still be able to destroy it when it has no local player."),
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

			// A test game instance has no local player, and the process still created the session, so it hosts it.
			CurrentTest->TestEqual(TEXT("There is no local player"), State->GameInstance->GetNumLocalPlayers(), 0);
			CurrentTest->TestTrue(TEXT("The process that created the session is the host"), Subsystem->IsHost());
			CurrentTest->TestTrue(TEXT("Session still exists"), Subsystem->IsInSession());

			Subsystem->DestroySessionForEveryone(FText::FromString(TEXT("Server shutting the match down")));
			State->Phase = 1;
			return false;

		default:
			// The point of the test: this used to be refused with a warning, because the host check asked whether a local player owned the session.
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
 * Hosting must not depend on a local player.
 * A test game instance has none, and the process that created the session still hosts it.
 * The host is FNamedOnlineSession's bHosting, which the create request sets when the create completes, so no local player is involved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostWithoutPlayerTest, "EasySession.Host.HostingNeedsNoLocalPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostWithoutPlayerTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionHostWithoutPlayerTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->CreateSession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForHostWithoutPlayer(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
