// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionBeaconPort.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineBeaconHost.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionPartyPortTest
{
	// Maximum time to wait for the create before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		/** The game instance whose world holds the configured beacon port, in place of another program. */
		TStrongObjectPtr<UGameInstance> PortHolder;

		/** The game instance that creates the party. */
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The listener that holds the configured beacon port. */
		TWeakObjectPtr<AOnlineBeaconHost> HeldPort;

		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;

		/** Did the test log the local player in, so it logs them out again at the end. */
		bool bLoggedIn = false;
	};

	// Log out the player the test logged in, release the held port, and destroy both game instances.
	void End(TSharedPtr<FTestState> State)
	{
		const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(State->GameInstance->GetWorld());
		if (State->bLoggedIn && Identity.IsValid())
		{
			Identity->Logout(0);
		}
		if (AOnlineBeaconHost* HeldPort = State->HeldPort.Get())
		{
			HeldPort->DestroyBeacon();
		}
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		EasySessionTest::DestroyGameInstance(State->PortHolder.Get());
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionPartyPortStep, TSharedPtr<EasySessionPartyPortTest::FTestState>, State);
bool FEasySessionPartyPortStep::Update()
{
	using namespace EasySessionPartyPortTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (!State->PendingResult.IsSet() || EasySessionTest::HasActivity(Subsystem))
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for the party create."));
			End(State);
			return true;
		}
		return false;
	}

	CurrentTest->TestEqual(TEXT("The party create fails"), State->PendingResult.GetValue(), EEasySessionResult::CreateFailure);
	CurrentTest->TestFalse(TEXT("No party session is left behind"), Subsystem->IsInSession(EEasySessionType::PartySession));
	CurrentTest->TestEqual(TEXT("The listener on the next free port is gone"), FEasySessionTestAccess::GetBeaconPort(*Subsystem).GetListenPort(), 0);

	End(State);
	return true;
}

/**
 * When another program holds the beacon port, the engine opens the party beacon on the next free port.
 * The party session still advertises the configured port, so every member reaches no beacon and the join ends with JoinRefused.
 * Create Easy Party must fail with CreateFailure instead of creating a party that nobody can join.
 *
 * A listener in a second world stands in for the other program, because a listener in the same world would be shared instead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPartyPortTest, "EasySession.Party.RefusedWhenTheBeaconPortIsTaken", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPartyPortTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPartyPortTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->PortHolder = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->PortHolder);
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UWorld* HolderWorld = State->PortHolder->GetWorld();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("The port holder's world is available"), HolderWorld) || !TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		End(State);
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AOnlineBeaconHost* HeldPort = HolderWorld->SpawnActor<AOnlineBeaconHost>(SpawnParams);
	if (!TestNotNull(TEXT("The port holder's listener spawned"), HeldPort) || !TestTrue(TEXT("The port holder's listener listens"), HeldPort->InitHost()))
	{
		End(State);
		return false;
	}
	State->HeldPort = HeldPort;

	// A party needs a host with an id, and a headless test world starts with nobody logged in.
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(State->GameInstance->GetWorld());
	if (Identity.IsValid() && !Identity->GetUniquePlayerId(0).IsValid())
	{
		State->bLoggedIn = Identity->Login(0, FOnlineAccountCredentials(FString(), TEXT("EasySessionPartyPortTest"), FString()));
	}
	if (!TestTrue(TEXT("A player is logged in"), Identity.IsValid() && Identity->GetUniquePlayerId(0).IsValid()))
	{
		End(State);
		return false;
	}

	FEasySessionSettings Settings;
	Settings.MaxPlayers = 4;
	Subsystem->CreateParty(Settings, FEasySessionCompleteDelegate::CreateLambda([State](EEasySessionResult Result, const FString&)
	{
		State->PendingResult = Result;
	}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionPartyPortStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
