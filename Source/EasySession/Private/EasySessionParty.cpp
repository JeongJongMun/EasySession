// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionParty.h"

#include "EasySession.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionPartyBeacon.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionParty::FEasySessionParty(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, BeaconPort(InBeaconPort)
{
}

FEasySessionParty::~FEasySessionParty()
{
	Close();
}

bool FEasySessionParty::StartHosting(int32 MaxMembers)
{
	UWorld* World = GetWorld();
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
	const FUniqueNetIdRepl LeaderId(Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr);
	if (World == nullptr || !LeaderId.IsValid())
	{
		return false;
	}

	Close();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AEasySessionPartyBeaconHost* Beacon = World->SpawnActor<AEasySessionPartyBeaconHost>(SpawnParams);
	if (Beacon == nullptr || !Beacon->Init(NAME_PartySession))
	{
		if (Beacon != nullptr)
		{
			Beacon->Destroy();
		}
		return false;
	}

	if (!BeaconPort.Register(*Beacon))
	{
		Beacon->Destroy();
		return false;
	}

	if (!Beacon->StartParty(MaxMembers, LeaderId, Identity->GetPlayerNickname(0)))
	{
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
		return false;
	}

	BeaconHost = Beacon;
	UE_LOG(LogEasySession, Log, TEXT("Party beacon started for %d members."), MaxMembers);
	return true;
}

void FEasySessionParty::Close()
{
	if (AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get())
	{
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
	}
	BeaconHost.Reset();
}

bool FEasySessionParty::IsInParty() const
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_PartySession) != nullptr;
}

bool FEasySessionParty::IsLeader() const
{
	// The party session request sets bHosting when the create completes, because Steam never does.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr;
	return NamedSession != nullptr && NamedSession->bHosting;
}

TArray<FEasyPartyMemberInfo> FEasySessionParty::GetMembers() const
{
	TArray<FEasyPartyMemberInfo> Members;

	const AEasySessionPartyBeaconState* State = GetPartyState();
	if (State == nullptr || !IsInParty())
	{
		return Members;
	}

	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	const FUniqueNetIdRepl LocalId(Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr);

	for (const ALobbyBeaconPlayerState* Member : State->GetMembers())
	{
		FEasyPartyMemberInfo& Info = Members.AddDefaulted_GetRef();
		Info.PlayerName = Member->DisplayName.ToString();
		Info.PlayerId = Member->UniqueId;
		Info.bIsLocalPlayer = LocalId.IsValid() && Member->UniqueId == LocalId;
		Info.bIsLeader = Member->UniqueId.IsValid() && Member->UniqueId == Member->PartyOwnerUniqueId;
	}

	return Members;
}

bool FEasySessionParty::IsPartySession(const FOnlineSessionSettings& Settings)
{
	int32 bIsParty = 0;
	return Settings.Get(EasySession::SettingKey_Party, bIsParty) && bIsParty != 0;
}

const AEasySessionPartyBeaconState* FEasySessionParty::GetPartyState() const
{
	const AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get();
	return Beacon != nullptr ? Beacon->GetPartyState() : nullptr;
}

UWorld* FEasySessionParty::GetWorld() const
{
	return Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
}
