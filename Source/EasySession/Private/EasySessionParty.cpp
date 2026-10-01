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

namespace
{
	/** Seconds between two checks whether the member list holds the local player. */
	constexpr float ConnectTickIntervalSeconds = 0.1f;
}

FEasySessionParty::FEasySessionParty(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, BeaconPort(InBeaconPort)
{
}

FEasySessionParty::~FEasySessionParty()
{
	Close();
	FTSTicker::GetCoreTicker().RemoveTicker(MembersChangedHandle);
}

bool FEasySessionParty::StartHosting(const FEasyPartyParams& Params)
{
	UWorld* World = GetWorld();
	const FUniqueNetIdRepl LeaderId = GetLocalPlayerId();
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

	// Bound before the registration, so the first login the beacon receives is decided here.
	Beacon->OnApproveMember().BindRaw(this, &FEasySessionParty::ApproveMember);

	if (!BeaconPort.Register(*Beacon))
	{
		Beacon->Destroy();
		return false;
	}

	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
	if (!Beacon->StartParty(Params.MaxMembers, LeaderId, Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString()))
	{
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
		return false;
	}

	BeaconHost = Beacon;
	Privacy = Params.Privacy;
	BindStateEvents();

	UE_LOG(LogEasySession, Log, TEXT("Party beacon started for %d members."), Params.MaxMembers);
	return true;
}

bool FEasySessionParty::ConnectToLeader(const FString& ConnectString, const FString& PartySessionId, FEasyPartyConnectComplete OnComplete)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	Close();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AEasySessionPartyBeaconClient* Client = World->SpawnActor<AEasySessionPartyBeaconClient>(SpawnParams);
	if (Client == nullptr)
	{
		return false;
	}

	Client->OnHostConnectionFailure().BindRaw(this, &FEasySessionParty::HandleConnectionFailure);
	Client->OnLoginComplete().BindRaw(this, &FEasySessionParty::HandleLoginComplete);
	Client->OnJoinRefused().BindLambda([this](const FText& Reason) { JoinRefusal = Reason; });
	Client->OnLeftParty().BindLambda([this](EEasyPartyLeaveReason Reason, const FText& ReasonText)
	{
		// The connection closes right after, and that failure reports this reason instead of a lost connection.
		PendingLeave.Emplace(Reason, ReasonText);
	});

	ConnectComplete = MoveTemp(OnComplete);
	JoinRefusal = FText::GetEmpty();
	PendingLeave.Reset();

	if (!Client->ConnectToParty(ConnectString, PartySessionId))
	{
		ConnectComplete.Unbind();
		Client->DestroyBeacon();
		return false;
	}

	UE_LOG(LogEasySession, Log, TEXT("Connecting to the party leader at %s."), *ConnectString);
	BeaconClient = Client;
	return true;
}

void FEasySessionParty::Close()
{
	FTSTicker::GetCoreTicker().RemoveTicker(ConnectTickHandle);
	ConnectTickHandle.Reset();
	ConnectComplete.Unbind();

	if (AEasySessionPartyBeaconState* State = BoundState.Get())
	{
		State->OnPlayerLobbyStateAdded().RemoveAll(this);
		State->OnPlayerLobbyStateRemoved().RemoveAll(this);
	}
	BoundState.Reset();

	if (AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get())
	{
		Beacon->TellMembersPartyEnds(NSLOCTEXT("EasySession", "PartyLeaderLeft", "The party leader left."));
		Beacon->OnApproveMember().Unbind();
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
	}
	BeaconHost.Reset();

	// Unbound first, so closing this connection on purpose reports no failure.
	if (AEasySessionPartyBeaconClient* Client = BeaconClient.Get())
	{
		Client->OnHostConnectionFailure().Unbind();
		Client->OnLoginComplete().Unbind();
		Client->OnJoinRefused().Unbind();
		Client->OnLeftParty().Unbind();
		Client->DestroyBeacon();
	}
	BeaconClient.Reset();
}

void FEasySessionParty::HandlePartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText)
{
	KickedPlayers.Reset();
	AllowedPlayers.Reset();
	PendingLeave.Reset();
	Privacy = EEasyPartyPrivacy::InviteOnly;

	UE_LOG(LogEasySession, Log, TEXT("Left the party: %s"), *UEnum::GetValueAsString(Reason));
	Owner.OnPartyLeft.Broadcast(Reason, ReasonText);
	Owner.OnPartyMembersChanged.Broadcast();
}

EEasySessionResult FEasySessionParty::KickMember(const FUniqueNetIdRepl& PlayerId, const FText& Reason)
{
	AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get();
	if (Beacon == nullptr || !IsLeader())
	{
		return EEasySessionResult::RequiresPartyLeader;
	}

	if (!PlayerId.IsValid() || PlayerId == GetLocalPlayerId())
	{
		return EEasySessionResult::InvalidParams;
	}

	// Kept out before the connection closes, so a quick rejoin is refused too.
	KickedPlayers.AddUnique(PlayerId);
	if (!Beacon->RemoveMember(PlayerId, EEasyPartyLeaveReason::Kicked, Reason))
	{
		return EEasySessionResult::InvalidParams;
	}

	UE_LOG(LogEasySession, Log, TEXT("Kicking '%s' from the party."), *PlayerId.ToString());
	return EEasySessionResult::Success;
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

	const FUniqueNetIdRepl LocalId = GetLocalPlayerId();
	for (const ALobbyBeaconPlayerState* Member : State->GetMembers())
	{
		// A member whose id has not replicated yet cannot be told apart from the others.
		if (!Member->UniqueId.IsValid())
		{
			continue;
		}

		FEasyPartyMemberInfo& Info = Members.AddDefaulted_GetRef();
		Info.PlayerName = Member->DisplayName.ToString();
		Info.PlayerId = Member->UniqueId;
		Info.bIsLocalPlayer = LocalId.IsValid() && Member->UniqueId == LocalId;
		Info.bIsLeader = Member->UniqueId == Member->PartyOwnerUniqueId;
	}

	return Members;
}

bool FEasySessionParty::IsPartySession(const FOnlineSessionSettings& Settings)
{
	int32 bIsParty = 0;
	return Settings.Get(EasySession::SettingKey_Party, bIsParty) && bIsParty != 0;
}

bool FEasySessionParty::ApproveMember(const FUniqueNetIdRepl& PlayerId, FText& OutReason) const
{
	const AEasySessionPartyBeaconState* State = GetPartyState();
	if (State == nullptr || !PlayerId.IsValid())
	{
		return false;
	}

	if (State->GetMembers().ContainsByPredicate([&PlayerId](const ALobbyBeaconPlayerState* Member) { return Member->UniqueId == PlayerId; }))
	{
		OutReason = NSLOCTEXT("EasySession", "AlreadyInParty", "You are already in this party.");
		return false;
	}

	if (KickedPlayers.Contains(PlayerId))
	{
		OutReason = NSLOCTEXT("EasySession", "KickedFromParty", "The party leader removed you from this party.");
		return false;
	}

	if (State->GetNumPlayers() >= State->GetMaxPlayers())
	{
		OutReason = NSLOCTEXT("EasySession", "PartyFull", "The party is full.");
		return false;
	}

	if (Privacy == EEasyPartyPrivacy::InviteOnly && !AllowedPlayers.Contains(PlayerId))
	{
		OutReason = NSLOCTEXT("EasySession", "PartyInviteOnly", "This party only admits players the leader invited.");
		return false;
	}

	return true;
}

void FEasySessionParty::HandleLoginComplete(bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		FinishConnect(false, JoinRefusal.IsEmpty() ? NSLOCTEXT("EasySession", "PartyLoginFailed", "The party leader refused the join.") : JoinRefusal);
		return;
	}

	// The login completes before the member list replicates, so the join waits until the list holds this player.
	FTSTicker::GetCoreTicker().RemoveTicker(ConnectTickHandle);
	ConnectTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEasySessionParty::HandleConnectTick), ConnectTickIntervalSeconds);
}

bool FEasySessionParty::HandleConnectTick(float DeltaTime)
{
	const FUniqueNetIdRepl LocalId = GetLocalPlayerId();
	const bool bListed = GetMembers().ContainsByPredicate([&LocalId](const FEasyPartyMemberInfo& Member) { return Member.PlayerId == LocalId; });
	if (!bListed)
	{
		return true;
	}

	ConnectTickHandle.Reset();
	BindStateEvents();
	FinishConnect(true, FText::GetEmpty());
	return false;
}

void FEasySessionParty::FinishConnect(bool bSuccess, const FText& Reason)
{
	// Moved out first, because the requester may close this party inside the call.
	FEasyPartyConnectComplete Complete = MoveTemp(ConnectComplete);
	ConnectComplete.Unbind();
	Complete.ExecuteIfBound(bSuccess, Reason);
}

void FEasySessionParty::HandleConnectionFailure()
{
	// A failure during the join belongs to the join request, which leaves the party session itself.
	if (ConnectComplete.IsBound())
	{
		FinishConnect(false, JoinRefusal.IsEmpty() ? NSLOCTEXT("EasySession", "PartyUnreachable", "Could not reach the party leader.") : JoinRefusal);
		return;
	}

	const TPair<EEasyPartyLeaveReason, FText> Leave = PendingLeave.Get(TPair<EEasyPartyLeaveReason, FText>(
		EEasyPartyLeaveReason::ConnectionLost, NSLOCTEXT("EasySession", "LostConnectionToParty", "Lost connection to the party.")));

	UE_LOG(LogEasySession, Log, TEXT("The connection to the party leader closed: %s"), *Leave.Value.ToString());
	Owner.HandlePartyEnded(Leave.Key, Leave.Value);
}

void FEasySessionParty::BindStateEvents()
{
	AEasySessionPartyBeaconState* State = const_cast<AEasySessionPartyBeaconState*>(GetPartyState());
	if (State == nullptr || BoundState.Get() == State)
	{
		return;
	}

	State->OnPlayerLobbyStateAdded().AddRaw(this, &FEasySessionParty::HandleMemberListChanged);
	State->OnPlayerLobbyStateRemoved().AddRaw(this, &FEasySessionParty::HandleMemberListChanged);
	BoundState = State;

	// The members already listed arrived before the binding.
	HandleMemberListChanged(nullptr);
}

void FEasySessionParty::HandleMemberListChanged(ALobbyBeaconPlayerState* Member)
{
	if (MembersChangedHandle.IsValid())
	{
		return;
	}

	MembersChangedHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		MembersChangedHandle.Reset();
		Owner.OnPartyMembersChanged.Broadcast();
		return false;
	}));
}

const AEasySessionPartyBeaconState* FEasySessionParty::GetPartyState() const
{
	if (const AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get())
	{
		return Beacon->GetPartyState();
	}

	const AEasySessionPartyBeaconClient* Client = BeaconClient.Get();
	return Client != nullptr ? Cast<AEasySessionPartyBeaconState>(Client->LobbyState) : nullptr;
}

FUniqueNetIdRepl FEasySessionParty::GetLocalPlayerId() const
{
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	return FUniqueNetIdRepl(Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr);
}

UWorld* FEasySessionParty::GetWorld() const
{
	return Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
}
