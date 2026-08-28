// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApproval.h"

#include "EasySession.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "OnlineBeaconHost.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionJoinApproval::~FEasySessionJoinApproval()
{
	Shutdown();
}

void FEasySessionJoinApproval::Initialize()
{
	GameModeInitializedHandle = FGameModeEvents::GameModeInitializedEvent.AddRaw(this, &FEasySessionJoinApproval::HandleGameModeInitialized);
}

void FEasySessionJoinApproval::Shutdown()
{
	if (GameModeInitializedHandle.IsValid())
	{
		FGameModeEvents::GameModeInitializedEvent.Remove(GameModeInitializedHandle);
		GameModeInitializedHandle.Reset();
	}

	FTSTicker::GetCoreTicker().RemoveTicker(DeferredEnsureHostHandle);
	DeferredEnsureHostHandle.Reset();

	StopHost();
	StopClient();
}

void FEasySessionJoinApproval::EnsureHost()
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return;
	}

	// Run the beacon only when the session advertised it
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	int32 bJoinApproval = 0;
	if (NamedSession == nullptr || !NamedSession->SessionSettings.Get(EasySession::SettingKey_JoinApproval, bJoinApproval) || bJoinApproval == 0)
	{
		return;
	}

	if (BeaconHost.IsValid() && BeaconHost->GetWorld() == World)
	{
		return;
	}

	StopHost();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;

	// A beacon host is one shared listener per process, so an existing one is reused instead of binding a second port joiners never ask.
	AOnlineBeaconHost* Host = nullptr;
	for (TActorIterator<AOnlineBeaconHost> It(World); It; ++It)
	{
		Host = *It;
		break;
	}
	bOwnsBeaconHost = Host == nullptr;

	if (Host == nullptr)
	{
		Host = World->SpawnActor<AOnlineBeaconHost>(SpawnParams);
		if (Host == nullptr)
		{
			return;
		}

		// ListenPort is left as the class default so a project can move the beacon from DefaultEngine.ini.
		if (!Host->InitHost())
		{
			UE_LOG(LogEasySession, Error,
				TEXT("Could not start the join approval beacon - a refused join is now reported after the travel instead of before it."));
			UE_LOG(LogEasySession, Error,
				TEXT("Likely causes: no BeaconNetDriver definition (clearing NetDriverDefinitions removes the engine's), or a plain ServerTravel kept the previous beacon's port - change maps with Server Travel Easy Session."));
			Host->DestroyBeacon();
			return;
		}
	}

	AEasySessionJoinApprovalBeaconHostObject* HostObject = World->SpawnActor<AEasySessionJoinApprovalBeaconHostObject>(SpawnParams);
	if (HostObject == nullptr)
	{
		if (bOwnsBeaconHost)
		{
			Host->DestroyBeacon();
		}
		return;
	}

	Host->RegisterHost(HostObject);

	// The project's own host keeps the pause state the project chose.
	if (bOwnsBeaconHost)
	{
		Host->PauseBeaconRequests(false);
	}

	BeaconHost = Host;
	BeaconHostObject = HostObject;

	if (bOwnsBeaconHost)
	{
		UE_LOG(LogEasySession, Log, TEXT("Join approval beacon listening on port %d."), Host->GetListenPort());
	}
	else
	{
		UE_LOG(LogEasySession, Log, TEXT("Registered the join approval on the project's beacon host (port %d)."), Host->GetListenPort());
	}

	// Joiners reach the beacon at the advertised port, so a beacon that bound elsewhere is unreachable.
	const int32 BoundPort = Host->GetListenPort();
	int32 AdvertisedPort = 0;
	NamedSession->SessionSettings.Get(SETTING_BEACONPORT, AdvertisedPort);
	if (BoundPort != AdvertisedPort)
	{
		UE_LOG(LogEasySession, Warning,
			TEXT("The join approval beacon listens on port %d, but this session advertises %d - another process holds the advertised port, or this project's own beacon uses a different one."),
			BoundPort, AdvertisedPort);
		UE_LOG(LogEasySession, Warning,
			TEXT("Joiners will ask %d and not reach this beacon, so passwords and full-room checks move to after the travel. Give each instance its own port with -BeaconPort=, or set ListenPort under [/Script/OnlineSubsystemUtils.OnlineBeaconHost]."),
			AdvertisedPort);
	}
}

void FEasySessionJoinApproval::StopHost()
{
	if (AEasySessionJoinApprovalBeaconHostObject* HostObject = BeaconHostObject.Get())
	{
		HostObject->Unregister();
		HostObject->Destroy();
	}
	BeaconHostObject.Reset();

	if (AOnlineBeaconHost* Host = BeaconHost.Get())
	{
		// The project's own host stays up for the project - only one this plugin spawned is torn down.
		if (bOwnsBeaconHost)
		{
			Host->DestroyBeacon();
		}
	}
	BeaconHost.Reset();
	bOwnsBeaconHost = false;
}

AOnlineBeaconHost* FEasySessionJoinApproval::GetBeaconHost() const
{
	return BeaconHost.Get();
}

void FEasySessionJoinApproval::RequestJoinApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete)
{
	StopClient();

	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	AEasySessionJoinApprovalBeaconClient* Client = nullptr;
	if (World != nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Client = World->SpawnActor<AEasySessionJoinApprovalBeaconClient>(SpawnParams);
	}

	if (Client == nullptr)
	{
		FEasyJoinApprovalResponse Response;
		Response.Result = EEasyJoinApprovalResult::Unreachable;
		Response.ReasonText = TEXT("Could not reach the host to ask about joining.");
		OnComplete.ExecuteIfBound(Response);
		return;
	}

	BeaconClient = Client;
	if (!Client->RequestApproval(Target, Password, OnComplete))
	{
		// The delegate already fired with Unreachable - only the actor is left to clean up.
		StopClient();
	}
}

void FEasySessionJoinApproval::StopClient()
{
	if (AEasySessionJoinApprovalBeaconClient* Client = BeaconClient.Get())
	{
		Client->DestroyBeacon();
	}
	BeaconClient.Reset();
}

void FEasySessionJoinApproval::HandleGameModeInitialized(AGameModeBase* GameMode)
{
	// Fires on the server for every world, ours or another PIE instance's.
	const UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (GameMode == nullptr || OwnWorld == nullptr || GameMode->GetWorld() != OwnWorld)
	{
		return;
	}

	if (Owner.IsSessionAuthority())
	{
		// One tick later, so a beacon host the project spawns in RegisterServer or BeginPlay exists first and gets reused.
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredEnsureHostHandle);
		DeferredEnsureHostHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			DeferredEnsureHostHandle.Reset();
			EnsureHost();
			return false;
		}));
	}
}
