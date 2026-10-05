// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionHostTest
{
	// Maximum time to wait for each step before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	// The password the test session is created with, so the test can check that the destroy clears it.
	static const TCHAR* TestPassword = TEXT("host-test");

	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingRespawn,
		AwaitingDestroy
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> CreateResult;
		/** Does the test change the world after the create step, before it destroys the session. */
		bool bChangeWorld = false;
		double StartTime = 0.0;
	};

	// Check the four parts of the host side: the authority, the session password, the state actor and the reservation beacon.
	static void TestHostSide(FAutomationTestBase& Test, UEasySessionSubsystem& Subsystem, bool bExpected, const TCHAR* When)
	{
		Test.TestEqual(FString::Printf(TEXT("%s: session authority"), When), Subsystem.IsSessionAuthority(), bExpected);
		Test.TestEqual(FString::Printf(TEXT("%s: the host holds the password"), When),
			FEasySessionTestAccess::GetEnforcedSessionPassword(Subsystem), bExpected ? FString(TestPassword) : FString());
		Test.TestEqual(FString::Printf(TEXT("%s: the state actor exists"), When), FEasySessionTestAccess::HasStateActor(Subsystem), bExpected);
		Test.TestEqual(FString::Printf(TEXT("%s: the reservation beacon is registered"), When),
			FEasySessionTestAccess::GetReservationListener(Subsystem) != nullptr, bExpected);
	}

	static bool StartTest(FAutomationTestBase& Test, bool bChangeWorld);
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionHostStep, TSharedPtr<EasySessionHostTest::FTestState>, State);
bool FEasySessionHostStep::Update()
{
	using namespace EasySessionHostTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(TEXT("Timed out waiting for a host test step."));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			if (!State->CreateResult.IsSet() || Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			// The create sets the authority and the credentials.
			// The world actors are spawned in the session's map.
			CurrentTest->TestFalse(TEXT("The create alone spawns no state actor"), FEasySessionTestAccess::HasStateActor(*Subsystem));
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			TestHostSide(*CurrentTest, *Subsystem, true, TEXT("After arriving in the session's map"));

			if (State->bChangeWorld)
			{
				// A travel destroys the actors of the previous world.
				// The host then initializes the game mode of the next world.
				FEasySessionTestAccess::DestroyHostSideActors(*Subsystem);
				CurrentTest->TestFalse(TEXT("The state actor is destroyed after the world change"), FEasySessionTestAccess::HasStateActor(*Subsystem));
				CurrentTest->TestNull(TEXT("The reservation beacon is unregistered after the world change"), FEasySessionTestAccess::GetReservationListener(*Subsystem));

				FActorSpawnParameters SpawnParams;
				SpawnParams.ObjectFlags |= RF_Transient;
				AGameModeBase* GameMode = State->GameInstance->GetWorld()->SpawnActor<AGameModeBase>(SpawnParams);
				FGameModeEvents::GameModeInitializedEvent.Broadcast(GameMode);

				State->Step = EStep::AwaitingRespawn;
				State->StartTime = FPlatformTime::Seconds();
				return false;
			}

			Subsystem->DestroySession();
			State->Step = EStep::AwaitingDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case EStep::AwaitingRespawn:
		{
			// SpawnWorldActors runs one tick after the game mode initialized.
			if (!FEasySessionTestAccess::HasStateActor(*Subsystem))
			{
				return false;
			}

			TestHostSide(*CurrentTest, *Subsystem, true, TEXT("In the next world"));
			Subsystem->DestroySession();
			State->Step = EStep::AwaitingDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case EStep::AwaitingDestroy:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			TestHostSide(*CurrentTest, *Subsystem, false, TEXT("After the destroy"));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

namespace EasySessionHostTest
{
	static bool StartTest(FAutomationTestBase& Test, bool bChangeWorld)
	{
		TSharedPtr<FTestState> State = MakeShared<FTestState>();
		State->bChangeWorld = bChangeWorld;
		State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
		EasySessionTest::InitializeGameInstance(State->GameInstance);

		UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
		if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
		{
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return false;
		}

		FEasySessionHostParams HostParams;
		HostParams.SessionDisplayName = TEXT("EasySession Host Test");
		HostParams.Password = TestPassword;
		HostParams.bIsLANMatch = true;
		HostParams.InitialMapName = EasySessionTest::SessionMapName;
		Subsystem->CreateSession(HostParams, FEasySessionCompleteDelegate::CreateLambda(
			[State](EEasySessionResult Result, const FString&)
			{
				State->CreateResult = Result;
			}));

		State->StartTime = FPlatformTime::Seconds();
		ADD_LATENT_AUTOMATION_COMMAND(FEasySessionHostStep(State));
		return true;
	}
}

/**
 * Hosting a session sets four things on the host.
 * The create sets the authority and the session password, and the session's map spawns the state actor and the reservation beacon.
 * Destroying the session has to undo all four, because a later session would otherwise start with the previous one's password or actors.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostDestroyTest, "EasySession.Host.DestroyUndoesWhatCreateDid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostDestroyTest::RunTest(const FString& Parameters)
{
	return EasySessionHostTest::StartTest(*this, /*bChangeWorld*/ false);
}

/**
 * The state actor and the reservation beacon are actors, so a travel destroys them.
 * HandleGameModeInitialized has to spawn both again in the next world, not only one of them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionHostNextWorldTest, "EasySession.Host.SpawnsTheWorldActorsAgainInTheNextWorld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionHostNextWorldTest::RunTest(const FString& Parameters)
{
	return EasySessionHostTest::StartTest(*this, /*bChangeWorld*/ true);
}

#endif // WITH_DEV_AUTOMATION_TESTS
