// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApproval.h"

#include "EasySession.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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

	if (AdvertiseTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AdvertiseTickerHandle);
		AdvertiseTickerHandle.Reset();
	}

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

	AOnlineBeaconHost* Host = World->SpawnActor<AOnlineBeaconHost>(SpawnParams);
	if (Host == nullptr)
	{
		return;
	}

	// ListenPort is left as the class default so a project can move the beacon from DefaultEngine.ini.
	if (!Host->InitHost())
	{
		UE_LOG(LogEasySession, Error,
			TEXT("Could not start the join approval beacon. Players will not be told why a join is refused until they have already traveled. Check that a BeaconNetDriver definition exists (clearing NetDriverDefinitions removes the engine's), and change maps with Server Travel Easy Session - a plain ServerTravel leaves the previous beacon's port still held when this world starts."));
		Host->DestroyBeacon();
		return;
	}

	AEasySessionJoinApprovalBeaconHostObject* HostObject = World->SpawnActor<AEasySessionJoinApprovalBeaconHostObject>(SpawnParams);
	if (HostObject == nullptr)
	{
		Host->DestroyBeacon();
		return;
	}

	Host->RegisterHost(HostObject);
	Host->PauseBeaconRequests(false);

	BeaconHost = Host;
	BeaconHostObject = HostObject;

	UE_LOG(LogEasySession, Log, TEXT("Join approval beacon listening on port %d."), Host->GetListenPort());

	// Joiners connect to the advertised port, and the engine binds the next free one when
	// the configured port is taken. Move the advertisement onto the port actually bound.
	int32 AdvertisedPort = 0;
	NamedSession->SessionSettings.Get(SETTING_BEACONPORT, AdvertisedPort);
	if (AdvertisedPort != Host->GetListenPort() && !AdvertiseTickerHandle.IsValid())
	{
		AdvertiseTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEasySessionJoinApproval::TickAdvertiseBoundPort));
	}
}

bool FEasySessionJoinApproval::TickAdvertiseBoundPort(float DeltaTime)
{
	// An active request has its completion delegate bound, and this update would wake
	// that handler and finish the wrong request. Keep ticking until the queue is idle.
	if (!Owner.RequestQueue.IsValid() || !Owner.RequestQueue->IsIdle())
	{
		return true;
	}

	AdvertiseTickerHandle.Reset();

	AOnlineBeaconHost* Host = BeaconHost.Get();
	const UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (Host == nullptr || World == nullptr)
	{
		return false;
	}

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return false;
	}

	// A later world may have bound the configured port after all, which the waiting
	// ticker cannot know until it runs.
	const int32 BoundPort = Host->GetListenPort();
	int32 AdvertisedPort = 0;
	NamedSession->SessionSettings.Get(SETTING_BEACONPORT, AdvertisedPort);
	if (AdvertisedPort == BoundPort)
	{
		return false;
	}

	FOnlineSessionSettings Corrected = NamedSession->SessionSettings;
	Corrected.Set(SETTING_BEACONPORT, BoundPort, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	if (Sessions->UpdateSession(NAME_GameSession, Corrected, true))
	{
		UE_LOG(LogEasySession, Log,
			TEXT("Another process holds port %d, so the join approval beacon bound %d. The session now advertises %d."),
			AdvertisedPort, BoundPort, BoundPort);
	}
	else
	{
		UE_LOG(LogEasySession, Warning,
			TEXT("The join approval beacon bound port %d but the session still advertises %d, and the online service refused the correction. Joiners will ask whichever process holds port %d about joining this one."),
			BoundPort, AdvertisedPort, AdvertisedPort);
	}

	return false;
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
		Host->DestroyBeacon();
	}
	BeaconHost.Reset();
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
		EnsureHost();
	}
}
