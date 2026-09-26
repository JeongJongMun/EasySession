// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionReservationBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "OnlineBeaconHost.h"
#include "OnlineBeaconHostObject.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionBeaconShareTest
{
	/** Maximum time to wait for each step before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** The beacon host the test spawns in the project's stead, before any session exists. */
		TWeakObjectPtr<AOnlineBeaconHost> ProjectHost;

		enum class EStep { AwaitingCreate, AwaitingDestroy };
		EStep Step = EStep::AwaitingCreate;
		double StartTime = 0.0;
	};

	/** @return How many beacon hosts are alive in the world. The whole point is that this stays at one. */
	int32 CountBeaconHosts(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AOnlineBeaconHost> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionBeaconShareStep, TSharedPtr<EasySessionBeaconShareTest::FTestState>, State);
bool FEasySessionBeaconShareStep::Update()
{
	using namespace EasySessionBeaconShareTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();
	const FString ReservationBeaconType = GetDefault<AEasySessionReservationBeaconHost>()->GetBeaconType();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
				{
					CurrentTest->AddError(TEXT("Timed out waiting for the create."));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}
				return false;
			}

			// The reservation beacon is registered when the host initializes the game mode of the session's map.
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);

			AOnlineBeaconHost* ProjectHost = State->ProjectHost.Get();
			CurrentTest->TestEqual(TEXT("No second beacon host was spawned"), CountBeaconHosts(World), 1);
			CurrentTest->TestTrue(TEXT("The reservation beacon registered on the project's host"),
				FEasySessionTestAccess::GetReservationListener(*Subsystem) == ProjectHost);
			CurrentTest->TestNotNull(TEXT("The project's host answers for the reservation beacon type"),
				ProjectHost != nullptr ? ProjectHost->GetHost(ReservationBeaconType) : nullptr);

			Subsystem->DestroySession();
			State->Step = FTestState::EStep::AwaitingDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingDestroy:
		{
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
				{
					CurrentTest->AddError(TEXT("Timed out waiting for the cleanup destroy."));
					EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
					return true;
				}
				return false;
			}

			AOnlineBeaconHost* ProjectHost = State->ProjectHost.Get();
			if (CurrentTest->TestNotNull(TEXT("The project's host survives the session"), ProjectHost))
			{
				CurrentTest->TestNull(TEXT("Only the reservation beacon type was unregistered"), ProjectHost->GetHost(ReservationBeaconType));
				ProjectHost->DestroyBeacon();
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A beacon host is one shared listener per process, so a project that already runs one keeps it.
 * The reservation beacon must register its beacon host there instead of spawning a second listener.
 * It must also take only its own type off again when the session ends.
 *
 * Before this behavior, the second listener bound a different port than the session advertised and every reservation request ended Unreachable.
 * The reservation beacon was silently off for the whole session, whichever side spawned first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBeaconShareTest, "EasySession.Beacon.SharesAnExistingHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBeaconShareTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionBeaconShareTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem) || !TestNotNull(TEXT("Test world is available"), World))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	// The project's stand-in: a beacon host that exists before any session, holding the configured port.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AOnlineBeaconHost* ProjectHost = World->SpawnActor<AOnlineBeaconHost>(SpawnParams);
	if (!TestNotNull(TEXT("The project's beacon host spawned"), ProjectHost) || !TestTrue(TEXT("The project's beacon host listens"), ProjectHost->InitHost()))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}
	ProjectHost->PauseBeaconRequests(false);
	State->ProjectHost = ProjectHost;

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Beacon Share Test");
	HostParams.bIsLANMatch = true;
	HostParams.InitialMapName = EasySessionTest::SessionMapName;
	Subsystem->CreateSession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionBeaconShareStep(State));
	return true;
}

namespace EasySessionBeaconPortTest
{
	/** A world, the subsystem of its game instance, and two host objects of different beacon types. */
	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		UEasySessionSubsystem* Subsystem = nullptr;
		UWorld* World = nullptr;
		AOnlineBeaconHostObject* First = nullptr;
		AOnlineBeaconHostObject* Second = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
			EasySessionTest::InitializeGameInstance(GameInstance);
			Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
			World = GameInstance->GetWorld();
			if (!Test.TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem) || !Test.TestNotNull(TEXT("Test world is available"), World))
			{
				return false;
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
			// The plain engine class has an empty beacon type, which is all the second family needs to differ from the first.
			First = World->SpawnActor<AEasySessionReservationBeaconHost>(SpawnParams);
			Second = World->SpawnActor<AOnlineBeaconHostObject>(SpawnParams);
			return Test.TestNotNull(TEXT("The first host object spawned"), First) && Test.TestNotNull(TEXT("The second host object spawned"), Second);
		}

		~FFixture()
		{
			EasySessionTest::DestroyGameInstance(GameInstance.Get());
		}
	};
}

/**
 * The listener is shared by every beacon family, so it has to outlive any one of them.
 * The party beacon will depend on this: a game session ending unregisters the reservation beacon, and the party's connection must stay up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBeaconPortSharedTest, "EasySession.Beacon.ListenerOutlivesOneOfTwoHostObjects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBeaconPortSharedTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionBeaconPortTest;
	using EasySessionBeaconShareTest::CountBeaconHosts;

	FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	FEasySessionBeaconPort& BeaconPort = FEasySessionTestAccess::GetBeaconPort(*Fixture.Subsystem);
	TestNull(TEXT("No listener runs before anything registers"), BeaconPort.GetListener());

	TestTrue(TEXT("The first host object registers"), BeaconPort.Register(*Fixture.First));
	TestTrue(TEXT("The second host object registers"), BeaconPort.Register(*Fixture.Second));
	TestEqual(TEXT("Both share one listener"), CountBeaconHosts(Fixture.World), 1);

	BeaconPort.Unregister(*Fixture.First);
	TestNotNull(TEXT("The listener stays up for the host object still registered"), BeaconPort.GetListener());

	BeaconPort.Unregister(*Fixture.Second);
	TestNull(TEXT("Unregistering the last host object releases the listener"), BeaconPort.GetListener());
	TestEqual(TEXT("The plugin's own listener was destroyed"), CountBeaconHosts(Fixture.World), 0);
	return true;
}

/**
 * A listener the project spawned belongs to the project.
 * The plugin registers on it, and neither the last unregister nor a server travel may destroy it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBeaconPortProjectTest, "EasySession.Beacon.ProjectListenerIsNeverDestroyed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBeaconPortProjectTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionBeaconPortTest;
	using EasySessionBeaconShareTest::CountBeaconHosts;

	FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AOnlineBeaconHost* ProjectListener = Fixture.World->SpawnActor<AOnlineBeaconHost>(SpawnParams);
	if (!TestNotNull(TEXT("The project's listener spawned"), ProjectListener) || !TestTrue(TEXT("The project's listener listens"), ProjectListener->InitHost()))
	{
		return false;
	}

	FEasySessionBeaconPort& BeaconPort = FEasySessionTestAccess::GetBeaconPort(*Fixture.Subsystem);
	TestTrue(TEXT("The host object registers"), BeaconPort.Register(*Fixture.First));
	TestTrue(TEXT("On the project's listener"), BeaconPort.GetListener() == ProjectListener);

	BeaconPort.Unregister(*Fixture.First);
	TestNull(TEXT("No listener is held after the last host object unregistered"), BeaconPort.GetListener());
	TestEqual(TEXT("The project's listener is still up"), CountBeaconHosts(Fixture.World), 1);

	TestTrue(TEXT("The host object registers again"), BeaconPort.Register(*Fixture.First));
	BeaconPort.ReleaseListener();
	TestEqual(TEXT("ReleaseListener does not destroy the project's listener"), CountBeaconHosts(Fixture.World), 1);

	ProjectListener->DestroyBeacon();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
