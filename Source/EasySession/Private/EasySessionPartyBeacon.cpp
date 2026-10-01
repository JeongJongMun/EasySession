// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionPartyBeacon.h"

AEasySessionPartyBeaconState::AEasySessionPartyBeaconState()
{
	LobbyBeaconPlayerStateClass = AEasySessionPartyBeaconPlayerState::StaticClass();
}

TArray<const ALobbyBeaconPlayerState*> AEasySessionPartyBeaconState::GetMembers() const
{
	TArray<const ALobbyBeaconPlayerState*> Members;
	for (const FLobbyPlayerStateActorInfo& Info : Players.GetAllPlayers())
	{
		// A member added on the host arrives in two steps on a client, and the first one carries no actor yet.
		if (Info.LobbyPlayerState != nullptr)
		{
			Members.Add(Info.LobbyPlayerState);
		}
	}
	return Members;
}

void AEasySessionPartyBeaconState::DestroyMembers()
{
	for (const FLobbyPlayerStateActorInfo& Info : Players.GetAllPlayers())
	{
		if (Info.LobbyPlayerState != nullptr)
		{
			Info.LobbyPlayerState->Destroy();
		}
	}
}

AEasySessionPartyBeaconHost::AEasySessionPartyBeaconHost()
{
	ClientBeaconActorClass = AEasySessionPartyBeaconClient::StaticClass();
	LobbyStateClass = AEasySessionPartyBeaconState::StaticClass();
}

bool AEasySessionPartyBeaconHost::StartParty(int32 MaxMembers, const FUniqueNetIdRepl& LeaderId, const FString& LeaderName)
{
	SetupLobbyState(MaxMembers);
	if (LobbyState == nullptr || !LeaderId.IsValid())
	{
		return false;
	}

	// The leader plays on this machine and has no beacon connection, so its entry is added here instead of at a login.
	ALobbyBeaconPlayerState* Leader = LobbyState->AddPlayer(FText::FromString(LeaderName), LeaderId);
	if (Leader == nullptr)
	{
		return false;
	}

	// Every member's party owner is the leader, the leader's own entry too.
	Leader->PartyOwnerUniqueId = LeaderId;
	return true;
}

const AEasySessionPartyBeaconState* AEasySessionPartyBeaconHost::GetPartyState() const
{
	return Cast<AEasySessionPartyBeaconState>(LobbyState);
}

void AEasySessionPartyBeaconHost::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A world that ends destroys every actor in it, so only a party closed in a living world destroys its state here.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (AEasySessionPartyBeaconState* State = Cast<AEasySessionPartyBeaconState>(LobbyState))
		{
			State->DestroyMembers();
			State->Destroy();
		}
	}
	LobbyState = nullptr;

	Super::EndPlay(EndPlayReason);
}
