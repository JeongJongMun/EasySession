// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionPartyBeacon.h"

#include "EasySession.h"
#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	// Characters a member name keeps, the same limit AGameModeBase::InitNewPlayer applies to player names in a game session.
	constexpr int32 MaxMemberNameLength = 20;
}

void AEasySessionPartyBeaconPlayerState::SetReady(bool bInReady)
{
	if (!HasAuthority() || bReady == bInReady)
	{
		return;
	}

	bReady = bInReady;
	ForceNetUpdate();

	// OnRep only runs on members, so the leader reports its own change here.
	OnRep_Ready();
}

void AEasySessionPartyBeaconPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEasySessionPartyBeaconPlayerState, bReady);
}

void AEasySessionPartyBeaconPlayerState::OnRep_Ready()
{
	ReadyChangedEvent.Broadcast();
}

AEasySessionPartyBeaconState::AEasySessionPartyBeaconState()
{
	LobbyBeaconPlayerStateClass = AEasySessionPartyBeaconPlayerState::StaticClass();
}

TArray<AEasySessionPartyBeaconPlayerState*> AEasySessionPartyBeaconState::GetMembers() const
{
	TArray<AEasySessionPartyBeaconPlayerState*> Members;
	for (const FLobbyPlayerStateActorInfo& Info : Players.GetAllPlayers())
	{
		// A member added on the host arrives in two steps on a client, and the first one carries no actor yet.
		if (AEasySessionPartyBeaconPlayerState* Member = Cast<AEasySessionPartyBeaconPlayerState>(Info.LobbyPlayerState))
		{
			Members.Add(Member);
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

bool AEasySessionPartyBeaconClient::ConnectToParty(const FString& ConnectString, const FString& PartySessionId)
{
	// The host compares this id with its own party session before it lets the player log in.
	DestSessionId = PartySessionId;
	FURL URL(nullptr, *ConnectString, TRAVEL_Absolute);
	return InitClient(URL);
}

void AEasySessionPartyBeaconClient::ClientJoinRefused_Implementation(const FText& Reason)
{
	JoinRefusedDelegate.ExecuteIfBound(Reason);
}

void AEasySessionPartyBeaconClient::ClientLeftParty_Implementation(EEasyPartyLeaveReason Reason, const FText& ReasonText)
{
	LeftPartyDelegate.ExecuteIfBound(Reason, ReasonText);
}

void AEasySessionPartyBeaconClient::ClientFollowHost_Implementation(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	FollowHostDelegate.ExecuteIfBound(HostId, bLANQuery);
}

void AEasySessionPartyBeaconClient::ServerSetReady_Implementation(bool bInReady)
{
	// The engine sets PlayerState at the login, so a connection that never logged in changes nothing.
	if (AEasySessionPartyBeaconPlayerState* Member = Cast<AEasySessionPartyBeaconPlayerState>(PlayerState))
	{
		Member->SetReady(bInReady);
	}
}

AEasySessionPartyBeaconHost::AEasySessionPartyBeaconHost()
{
	ClientBeaconActorClass = AEasySessionPartyBeaconClient::StaticClass();
	LobbyStateClass = AEasySessionPartyBeaconState::StaticClass();
}

bool AEasySessionPartyBeaconHost::StartParty(int32 MaxMembers, const FUniqueNetIdRepl& InLeaderId, const FString& LeaderName)
{
	SetupLobbyState(MaxMembers);
	if (LobbyState == nullptr || !InLeaderId.IsValid())
	{
		return false;
	}

	// The leader plays on this machine and has no beacon connection, so its entry is added here instead of at a login.
	ALobbyBeaconPlayerState* Leader = LobbyState->AddPlayer(FText::FromString(LeaderName.Left(MaxMemberNameLength)), InLeaderId);
	if (Leader == nullptr)
	{
		return false;
	}

	LeaderId = InLeaderId;
	Leader->PartyOwnerUniqueId = LeaderId;

	// NULL counts the open slots a search reports from the registered players, and the leader has no login that registers it.
	// Registering again after a map change changes nothing, because a registered player is not counted twice.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid())
	{
		Sessions->RegisterPlayer(NAME_PartySession, *InLeaderId, false);
	}
	return true;
}

bool AEasySessionPartyBeaconHost::RemoveMember(const FUniqueNetIdRepl& PlayerId, EEasyPartyLeaveReason Reason, const FText& ReasonText)
{
	AEasySessionPartyBeaconClient* Client = FindMemberClient(PlayerId);
	if (Client == nullptr)
	{
		return false;
	}

	// The engine's kick drops its reason on the member, so the reason goes first on the same connection.
	Client->ClientLeftParty(Reason, ReasonText);
	KickPlayer(Client, ReasonText);
	return true;
}

void AEasySessionPartyBeaconHost::TellMembersPartyEnds(const FText& ReasonText)
{
	for (AOnlineBeaconClient* ExistingClient : ClientActors)
	{
		if (AEasySessionPartyBeaconClient* Client = Cast<AEasySessionPartyBeaconClient>(ExistingClient))
		{
			Client->ClientLeftParty(EEasyPartyLeaveReason::LeaderLeft, ReasonText);
		}
	}
}

void AEasySessionPartyBeaconHost::TellMembersToFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	for (AOnlineBeaconClient* ExistingClient : ClientActors)
	{
		if (AEasySessionPartyBeaconClient* Client = Cast<AEasySessionPartyBeaconClient>(ExistingClient))
		{
			Client->ClientFollowHost(HostId, bLANQuery);
		}
	}
}

AEasySessionPartyBeaconState* AEasySessionPartyBeaconHost::GetPartyState() const
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

void AEasySessionPartyBeaconHost::NotifyClientDisconnected(AOnlineBeaconClient* LeavingClientActor)
{
	// The engine's version needs a game mode, which the menu world of a party may not have, and it logs the player out of the game session.
	const ALobbyBeaconPlayerState* Member = LobbyState != nullptr ? LobbyState->GetPlayer(LeavingClientActor) : nullptr;
	if (Member != nullptr && Member->bInLobby)
	{
		HandlePlayerLogout(Member->UniqueId);
	}

	AOnlineBeaconHostObject::NotifyClientDisconnected(LeavingClientActor);
}

void AEasySessionPartyBeaconHost::HandlePlayerLogout(const FUniqueNetIdRepl& InUniqueId)
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid() && InUniqueId.IsValid())
	{
		Sessions->UnregisterPlayer(NAME_PartySession, *InUniqueId);
	}

	Super::HandlePlayerLogout(InUniqueId);
}

ALobbyBeaconPlayerState* AEasySessionPartyBeaconHost::HandlePlayerLogin(ALobbyBeaconClient* ClientActor, const FUniqueNetIdRepl& InUniqueId, const FString& Options)
{
	if (LobbyState == nullptr)
	{
		return nullptr;
	}

	FText Reason;
	if (!ApproveMemberDelegate.IsBound() || !ApproveMemberDelegate.Execute(InUniqueId, Reason))
	{
		if (Reason.IsEmpty())
		{
			Reason = NSLOCTEXT("EasySession", "PartyNotAnswering", "The party leader could not decide the join.");
		}

		UE_LOG(LogEasySession, Log, TEXT("Party: refusing '%s' - %s"), *InUniqueId.ToString(), *Reason.ToString());

		// A null player state fails the login, and the engine closes the connection after this reason.
		if (AEasySessionPartyBeaconClient* Client = Cast<AEasySessionPartyBeaconClient>(ClientActor))
		{
			Client->ClientJoinRefused(Reason);
		}
		return nullptr;
	}

	// The open slot count of the party session follows the registered players, and a search reports it.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid())
	{
		Sessions->RegisterPlayer(NAME_PartySession, *InUniqueId, false);
	}

	FString MemberName = UGameplayStatics::ParseOption(Options, TEXT("Name")).Left(MaxMemberNameLength);
	if (MemberName.IsEmpty())
	{
		MemberName = InUniqueId.ToString();
	}

	ALobbyBeaconPlayerState* NewMember = LobbyState->AddPlayer(FText::FromString(MemberName), InUniqueId);
	if (NewMember != nullptr)
	{
		NewMember->PartyOwnerUniqueId = LeaderId;
		UE_LOG(LogEasySession, Log, TEXT("Party: '%s' joined."), *MemberName);
	}
	return NewMember;
}

AEasySessionPartyBeaconClient* AEasySessionPartyBeaconHost::FindMemberClient(const FUniqueNetIdRepl& PlayerId) const
{
	for (AOnlineBeaconClient* ExistingClient : ClientActors)
	{
		AEasySessionPartyBeaconClient* Client = Cast<AEasySessionPartyBeaconClient>(ExistingClient);
		if (Client != nullptr && Client->PlayerState != nullptr && Client->PlayerState->UniqueId == PlayerId)
		{
			return Client;
		}
	}
	return nullptr;
}
