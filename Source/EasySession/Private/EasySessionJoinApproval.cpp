// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApproval.h"

#include "EasySession.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionJoinApproval::~FEasySessionJoinApproval()
{
	StopHost();
}

bool FEasySessionJoinApproval::IsAdvertisedBy(const FOnlineSessionSettings& Settings)
{
	int32 bJoinApproval = 0;
	return Settings.Get(EasySession::SettingKey_JoinApproval, bJoinApproval) && bJoinApproval != 0;
}

void FEasySessionJoinApproval::EnsureHost()
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return;
	}

	// Run the beacon only when the session advertised it.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr || !IsAdvertisedBy(NamedSession->SessionSettings))
	{
		return;
	}

	if (BeaconHostObject.IsValid() && BeaconHostObject->GetWorld() == World)
	{
		return;
	}

	StopHost();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AEasySessionJoinApprovalBeaconHostObject* HostObject = World->SpawnActor<AEasySessionJoinApprovalBeaconHostObject>(SpawnParams);
	if (HostObject == nullptr)
	{
		return;
	}

	if (!BeaconPort.Register(*HostObject))
	{
		UE_LOG(LogEasySession, Error, TEXT("The join approval beacon is not running. A refused join is now reported after the travel instead of before it."));
		HostObject->Destroy();
		return;
	}

	BeaconHostObject = HostObject;
	CheckAdvertisedPort(NamedSession->SessionSettings);
}

void FEasySessionJoinApproval::StopHost()
{
	if (AEasySessionJoinApprovalBeaconHostObject* HostObject = BeaconHostObject.Get())
	{
		BeaconPort.Unregister(*HostObject);
		HostObject->Destroy();
	}
	BeaconHostObject.Reset();
}

void FEasySessionJoinApproval::CheckAdvertisedPort(const FOnlineSessionSettings& Settings) const
{
	// Joining players connect to the advertised port, so a listener that bound another port is unreachable.
	const int32 BoundPort = BeaconPort.GetListenPort();
	int32 AdvertisedPort = 0;
	Settings.Get(SETTING_BEACONPORT, AdvertisedPort);
	if (BoundPort == AdvertisedPort)
	{
		return;
	}

	UE_LOG(LogEasySession, Warning,
		TEXT("The join approval beacon listens on port %d but this session advertises %d, so joining players reach no beacon and a refusal arrives after the travel instead of before it. Free port %d, or move the beacon with -BeaconPort= or ListenPort under [/Script/OnlineSubsystemUtils.OnlineBeaconHost]."),
		BoundPort, AdvertisedPort, AdvertisedPort);
}
