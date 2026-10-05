// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionParty.h"

#include "EasySession.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionConfig.h"
#include "EasySessionCreatePartyRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinPartyRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionPartyBeacon.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// Seconds between two checks whether the member list holds the local player.
	constexpr float ConnectTickIntervalSeconds = 0.1f;

	// Seconds between two attempts to connect to the leader again.
	constexpr float ReconnectIntervalSeconds = 1.0f;

	// Seconds between two attempts to restore the party of the last match.
	constexpr float RestoreIntervalSeconds = 2.0f;

	// The OnPartyLeft text when the connection to the leader was lost.
	FText GetLostConnectionReason()
	{
		return NSLOCTEXT("EasySession", "LostConnectionToParty", "Lost connection to the party.");
	}
}

FEasySessionParty::FEasySessionParty(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, BeaconPort(InBeaconPort)
{
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FEasySessionParty::HandlePostLoadMap);
}

FEasySessionParty::~FEasySessionParty()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	Close();
	FTSTicker::GetCoreTicker().RemoveTicker(MembersChangedHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
}

bool FEasySessionParty::StartHosting(const FEasyPartySettings& InPartySettings)
{
	UWorld* World = GetWorld();
	const FUniqueNetIdRepl LocalId = GetLocalPlayerId();
	if (World == nullptr || !LocalId.IsValid())
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
	if (!Beacon->StartParty(InPartySettings.MaxMembers, LocalId, Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString()))
	{
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
		return false;
	}

	BeaconHost = Beacon;
	PartySettings = InPartySettings;
	LeaderId = LocalId;

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr;
	bIsLANParty = NamedSession != nullptr && NamedSession->SessionSettings.bIsLANMatch;

	BindStateEvents();

	UE_LOG(LogEasySession, Log, TEXT("Party beacon started for %d members."), InPartySettings.MaxMembers);
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
		// A member told to follow keeps MovedToGameSession, although the leader closes the party right after.
		if (!PendingLeave.IsSet())
		{
			PendingLeave.Emplace(Reason, ReasonText);
		}
	});
	Client->OnFollowHost().BindRaw(this, &FEasySessionParty::HandleFollowHost);

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
		for (AEasySessionPartyBeaconPlayerState* Member : State->GetMembers())
		{
			Member->OnReadyChanged().RemoveAll(this);
		}
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
		Client->OnFollowHost().Unbind();
		Client->DestroyBeacon();
	}
	BeaconClient.Reset();
}

void FEasySessionParty::HandlePartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText)
{
	// Only a party that entered a game session comes back, because every other reason means the party is over for this player.
	if (Reason == EEasyPartyLeaveReason::MovedToGameSession && LeaderId.IsValid())
	{
		FLastParty& Last = LastParty.Emplace();
		Last.LeaderId = LeaderId;
		Last.Settings = PartySettings;
		Last.bIsLANMatch = bIsLANParty;
	}
	else
	{
		LastParty.Reset();
	}

	KickedPlayers.Reset();
	PendingLeave.Reset();
	PartySettings = FEasyPartySettings();
	LeaderId = FUniqueNetIdRepl();
	bIsLANParty = false;
	ReconnectStartSeconds = 0.0;

	UE_LOG(LogEasySession, Log, TEXT("Left the party: %s"), *UEnum::GetValueAsString(Reason));
	Owner.OnPartyLeft.Broadcast(Reason, ReasonText);
	Owner.OnPartyMembersChanged.Broadcast();
}

TArray<FUniqueNetIdRepl> FEasySessionParty::GetOtherMemberIds() const
{
	TArray<FUniqueNetIdRepl> MemberIds;
	if (!IsLeader())
	{
		return MemberIds;
	}

	for (const FEasyPartyMemberInfo& Member : GetMembers())
	{
		if (!Member.bIsLocalPlayer)
		{
			MemberIds.Add(Member.PlayerId);
		}
	}
	return MemberIds;
}

void FEasySessionParty::TellMembersToFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	if (AEasySessionPartyBeaconHost* Beacon = BeaconHost.Get())
	{
		Beacon->TellMembersToFollow(HostId, bLANQuery);
	}
}

EEasySessionResult FEasySessionParty::SetReady(bool bReady)
{
	if (!IsInParty())
	{
		return EEasySessionResult::NoSessionExists;
	}

	if (AEasySessionPartyBeaconClient* Client = BeaconClient.Get())
	{
		Client->ServerSetReady(bReady);
		return EEasySessionResult::Success;
	}

	const AEasySessionPartyBeaconState* State = GetPartyState();
	if (State == nullptr)
	{
		return EEasySessionResult::NoSessionExists;
	}

	const FUniqueNetIdRepl LocalId = GetLocalPlayerId();
	for (AEasySessionPartyBeaconPlayerState* Member : State->GetMembers())
	{
		if (Member->UniqueId == LocalId)
		{
			Member->SetReady(bReady);
			return EEasySessionResult::Success;
		}
	}

	return EEasySessionResult::NoSessionExists;
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
	for (const AEasySessionPartyBeaconPlayerState* Member : State->GetMembers())
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
		Info.bIsReady = Member->IsReady();
	}

	return Members;
}

bool FEasySessionParty::IsPartySession(const FOnlineSessionSettings& Settings)
{
	int32 bIsParty = 0;
	return Settings.Get(EasySession::SettingKey_Party, bIsParty) && bIsParty != 0;
}

void FEasySessionParty::CancelRestore()
{
	if (!bRestoring && !LastParty.IsSet())
	{
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("The party of the last match is not restored, because the player chose something else."));

	// Finished first, so the canceled request's completion finds no restore to continue.
	const TSharedPtr<FEasySessionRequest> Running = RestoreRequest.Pin();
	FinishRestore();
	if (Running.IsValid())
	{
		Running->Cancel();
	}
}

void FEasySessionParty::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (LoadedWorld == nullptr || LoadedWorld->GetGameInstance() != Owner.GetGameInstance())
	{
		return;
	}

	// The map change destroyed the beacons, and the party session outlives them.
	// A map with a game session is one the party entered, and the party closes there instead.
	if (IsInParty())
	{
		if (Owner.IsInSession())
		{
			return;
		}

		if (IsLeader() && !BeaconHost.IsValid())
		{
			UE_LOG(LogEasySession, Log, TEXT("Starting the party beacon again after the map change."));
			if (!StartHosting(PartySettings))
			{
				Owner.HandlePartyEnded(EEasyPartyLeaveReason::ConnectionLost, NSLOCTEXT("EasySession", "PartyBeaconRestartFailed", "The party could not continue after the map change."));
			}
		}
		else if (!IsLeader() && !BeaconClient.IsValid())
		{
			StartReconnect();
		}
		return;
	}

	// A map without a game session is where a match ended, so the party of that match is restored here.
	if (LastParty.IsSet() && !bRestoring && !Owner.IsInSession() && GetDefault<UEasySessionConfig>()->bRestorePartyAfterMatch)
	{
		StartRestore();
	}
}

void FEasySessionParty::StartReconnect()
{
	if (ReconnectStartSeconds > 0.0)
	{
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Connecting to the party leader again."));
	ReconnectStartSeconds = FPlatformTime::Seconds();

	// Started on the next tick, because the failure that leads here runs inside the connection that is going away.
	FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
	RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		RetryHandle.Reset();
		TryReconnect();
		return false;
	}));
}

void FEasySessionParty::TryReconnect()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr;

	FString ConnectString;
	const bool bStarted = NamedSession != nullptr && NamedSession->SessionInfo.IsValid()
		&& Sessions->GetResolvedConnectString(NAME_PartySession, ConnectString, NAME_BeaconPort)
		&& ConnectToLeader(ConnectString, NamedSession->SessionInfo->GetSessionId().ToString(), FEasyPartyConnectComplete::CreateRaw(this, &FEasySessionParty::HandleReconnectComplete));
	if (!bStarted)
	{
		HandleReconnectComplete(false, FText::GetEmpty());
	}
}

void FEasySessionParty::HandleReconnectComplete(bool bSuccess, const FText& Reason)
{
	if (bSuccess)
	{
		UE_LOG(LogEasySession, Log, TEXT("Connected to the party leader again."));
		ReconnectStartSeconds = 0.0;
		return;
	}

	// A leader that refused the login ended the membership on purpose, so no second attempt follows.
	const bool bRefused = !JoinRefusal.IsEmpty();
	const bool bOutOfTime = FPlatformTime::Seconds() - ReconnectStartSeconds >= GetDefault<UEasySessionConfig>()->PartyReconnectSeconds;
	const FText EndReason = bRefused ? Reason : GetLostConnectionReason();

	// The next attempt, or the end of the party, runs on the next tick, because this completion can run inside the connection that failed.
	FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
	RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, bGiveUp = bRefused || bOutOfTime, EndReason](float)
	{
		RetryHandle.Reset();
		if (!bGiveUp)
		{
			TryReconnect();
			return false;
		}

		UE_LOG(LogEasySession, Warning, TEXT("Could not connect to the party leader again: %s"), *EndReason.ToString());
		ReconnectStartSeconds = 0.0;
		Close();
		Owner.HandlePartyEnded(EEasyPartyLeaveReason::ConnectionLost, EndReason);
		return false;
	}), bRefused || bOutOfTime ? 0.0f : ReconnectIntervalSeconds);
}

void FEasySessionParty::StartRestore()
{
	bRestoring = true;
	RestoreStartSeconds = FPlatformTime::Seconds();

	if (LastParty->LeaderId == GetLocalPlayerId())
	{
		UE_LOG(LogEasySession, Log, TEXT("Creating the party of the last match again."));
		TryRestoreCreate();
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Looking for the party of the last match, led by '%s'."), *LastParty->LeaderId.ToString());
	TryRestoreJoin();
}

void FEasySessionParty::TryRestoreCreate()
{
	const FLastParty& Last = LastParty.GetValue();

	const TWeakPtr<bool> WeakLifetime = Lifetime;
	RunRestoreRequest(MakeShared<FEasySessionCreatePartyRequest>(Last.Settings, FEasySessionCompleteDelegate::CreateLambda(
		[this, WeakLifetime](EEasySessionResult Result, const FString& ErrorMessage)
		{
			if (!WeakLifetime.IsValid() || !bRestoring)
			{
				return;
			}

			if (Result == EEasySessionResult::Success)
			{
				FinishRestore();
				return;
			}
			RetryRestore(FText::FromString(ErrorMessage));
		})));
}

void FEasySessionParty::TryRestoreJoin()
{
	FEasySessionSearchParams Search;
	Search.OwnerId = LastParty->LeaderId;
	Search.bLANQuery = LastParty->bIsLANMatch;

	const TWeakPtr<bool> WeakLifetime = Lifetime;
	RunRestoreRequest(MakeShared<FEasySessionFindRequest>(Search, FEasySessionFindCompleteDelegate::CreateLambda(
		[this, WeakLifetime](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
		{
			if (!WeakLifetime.IsValid() || !bRestoring)
			{
				return;
			}

			// The leader may still be in the match, so not finding the party is no failure yet.
			if (Result != EEasySessionResult::Success || Results.IsEmpty())
			{
				RetryRestore(NSLOCTEXT("EasySession", "PartyLeaderNotBack", "The party leader did not come back."));
				return;
			}

			RunRestoreRequest(MakeShared<FEasySessionJoinPartyRequest>(Results[0], FEasySessionCompleteDelegate::CreateLambda(
				[this, WeakLifetime](EEasySessionResult JoinResult, const FString& JoinError)
				{
					if (!WeakLifetime.IsValid() || !bRestoring)
					{
						return;
					}

					if (JoinResult == EEasySessionResult::Success)
					{
						FinishRestore();
						return;
					}
					RetryRestore(FText::FromString(JoinError));
				})));
		})));
}

void FEasySessionParty::RetryRestore(const FText& Reason)
{
	if (FPlatformTime::Seconds() - RestoreStartSeconds >= GetDefault<UEasySessionConfig>()->PartyRestoreWaitSeconds)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The party of the last match did not come back: %s"), *Reason.ToString());
		FinishRestore();
		Owner.OnPartyLeft.Broadcast(EEasyPartyLeaveReason::ConnectionLost, Reason);
		return;
	}

	FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
	RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		RetryHandle.Reset();
		if (bRestoring && LastParty->LeaderId == GetLocalPlayerId())
		{
			TryRestoreCreate();
		}
		else if (bRestoring)
		{
			TryRestoreJoin();
		}
		return false;
	}), RestoreIntervalSeconds);
}

void FEasySessionParty::FinishRestore()
{
	bRestoring = false;
	LastParty.Reset();
	RestoreRequest.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
	RetryHandle.Reset();
}

void FEasySessionParty::RunRestoreRequest(TSharedRef<FEasySessionRequest> Request)
{
	RestoreRequest = Request;
	Owner.EnqueuePartyRequest(Request);
}

bool FEasySessionParty::ApproveMember(const FUniqueNetIdRepl& PlayerId, FText& OutReason) const
{
	const AEasySessionPartyBeaconState* State = GetPartyState();
	if (State == nullptr || !PlayerId.IsValid())
	{
		return false;
	}

	if (State->GetMembers().ContainsByPredicate([&PlayerId](const AEasySessionPartyBeaconPlayerState* Member) { return Member->UniqueId == PlayerId; }))
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

	return true;
}

void FEasySessionParty::HandleFollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	if (!PendingLeave.IsSet())
	{
		PendingLeave.Emplace(EEasyPartyLeaveReason::MovedToGameSession, EasySession::GetPartyMovedReason());
	}

	UE_LOG(LogEasySession, Log, TEXT("The party leader takes the party to host '%s'."), *HostId.ToString());
	Owner.FollowHost(HostId, bLANQuery);
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

	// The leader's id is what a member looks for when the party comes back after a match.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr)
	{
		LeaderId = FUniqueNetIdRepl(NamedSession->OwningUserId);
		bIsLANParty = NamedSession->SessionSettings.bIsLANMatch;
	}

	BindStateEvents();
	FinishConnect(true, FText::GetEmpty());
	return false;
}

void FEasySessionParty::FinishConnect(bool bSuccess, const FText& Reason)
{
	// Moved out first, because the callback may call Close, which unbinds ConnectComplete.
	FEasyPartyConnectComplete Complete = MoveTemp(ConnectComplete);
	ConnectComplete.Unbind();
	Complete.ExecuteIfBound(bSuccess, Reason);
}

void FEasySessionParty::HandleConnectionFailure()
{
	// A failure while ConnectToLeader runs goes to its OnComplete: the join request, or the reconnect.
	if (ConnectComplete.IsBound())
	{
		FinishConnect(false, JoinRefusal.IsEmpty() ? NSLOCTEXT("EasySession", "PartyUnreachable", "Could not reach the party leader.") : JoinRefusal);
		return;
	}

	// Without a reason from the leader the leader may only be changing maps, so the member connects again first.
	if (!PendingLeave.IsSet())
	{
		UE_LOG(LogEasySession, Log, TEXT("The connection to the party leader closed without a reason."));
		StartReconnect();
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("The connection to the party leader closed: %s"), *PendingLeave->Value.ToString());
	Owner.HandlePartyEnded(PendingLeave->Key, PendingLeave->Value);
}

void FEasySessionParty::BindStateEvents()
{
	AEasySessionPartyBeaconState* State = GetPartyState();
	if (State == nullptr || BoundState.Get() == State)
	{
		return;
	}

	State->OnPlayerLobbyStateAdded().AddRaw(this, &FEasySessionParty::HandleMemberListChanged);
	State->OnPlayerLobbyStateRemoved().AddRaw(this, &FEasySessionParty::HandleMemberListChanged);
	BoundState = State;

	// The members already listed arrived before the binding.
	for (AEasySessionPartyBeaconPlayerState* Member : State->GetMembers())
	{
		HandleMemberListChanged(Member);
	}
	HandleMemberListChanged(nullptr);
}

void FEasySessionParty::HandleMemberListChanged(ALobbyBeaconPlayerState* Member)
{
	// A member's ready state changes on the member's own entry, so each entry is bound once, when it is added.
	AEasySessionPartyBeaconPlayerState* PartyMember = Cast<AEasySessionPartyBeaconPlayerState>(Member);
	if (PartyMember != nullptr && !PartyMember->OnReadyChanged().IsBoundToObject(this))
	{
		PartyMember->OnReadyChanged().AddRaw(this, &FEasySessionParty::HandleMemberListChanged, static_cast<ALobbyBeaconPlayerState*>(nullptr));
	}

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

AEasySessionPartyBeaconState* FEasySessionParty::GetPartyState() const
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
