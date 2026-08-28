// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "OnlineBeaconHost.h"
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
	const FString ApprovalType = GetDefault<AEasySessionJoinApprovalBeaconHostObject>()->GetBeaconType();

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

			AOnlineBeaconHost* ProjectHost = State->ProjectHost.Get();
			CurrentTest->TestEqual(TEXT("No second beacon host was spawned"), CountBeaconHosts(World), 1);
			CurrentTest->TestTrue(TEXT("The approval registered on the project's host"),
				FEasySessionTestAccess::GetJoinApprovalBeaconHost(*Subsystem) == ProjectHost);
			CurrentTest->TestNotNull(TEXT("The project's host answers for the approval type"),
				ProjectHost != nullptr ? ProjectHost->GetHost(ApprovalType) : nullptr);

			Subsystem->DestroyEasySession();
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
				CurrentTest->TestNull(TEXT("Only the approval type was unregistered"), ProjectHost->GetHost(ApprovalType));
				ProjectHost->DestroyBeacon();
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A beacon host is one shared listener per process, so a project that already runs one
 * keeps it: the join approval must register its host object there instead of spawning a
 * second host, and must take only its own type off again when the session ends.
 *
 * Before this behavior, the second host bound a different port than the session advertised
 * and every approval ask ended Unreachable - join approval was silently off for the whole
 * session, in both directions of who spawned first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionBeaconShareTest, "EasySession.JoinApproval.SharesAnExistingBeaconHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionBeaconShareTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionBeaconShareTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

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
	HostParams.bStartListening = false;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionBeaconShareStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
