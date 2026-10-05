// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionReservations.h"

#include "EasySession.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	// A response with this result, and the message a refused player sees.
	FEasyReservationResponse MakeResponse(EEasyReservationResult Result, const FText& Reason = FText::GetEmpty())
	{
		FEasyReservationResponse Response;
		Response.Result = Result;
		Response.ReasonText = Reason.ToString();
		return Response;
	}
}

FEasySessionReservations::FEasySessionReservations(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, BeaconPort(InBeaconPort)
{
	PreLoginHandle = FGameModeEvents::GameModePreLoginEvent.AddRaw(this, &FEasySessionReservations::HandlePreLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &FEasySessionReservations::HandleLogout);
}

FEasySessionReservations::~FEasySessionReservations()
{
	StopBeacon();

	FGameModeEvents::GameModePreLoginEvent.Remove(PreLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
}

void FEasySessionReservations::OnSessionCreated(const FEasySessionHostParams& Params, const TArray<FUniqueNetIdRepl>& GroupMembers)
{
	SessionPassword = Params.Password.TrimStartAndEnd();
	bFriendsBypassPassword = Params.bFriendsBypassPassword;
	HostGroup = GroupMembers;
}

void FEasySessionReservations::OnSettingsUpdated(const FEasySessionSettings& Settings)
{
	SessionPassword = Settings.Password.TrimStartAndEnd();
	bFriendsBypassPassword = Settings.bFriendsBypassPassword;
	SetMaxReservations(Settings.MaxPlayers);
}

void FEasySessionReservations::OnSessionDestroyed()
{
	SessionPassword.Empty();
	bFriendsBypassPassword = false;
	KickedPlayers.Reset();
	HostGroup.Reset();

	// A new session must never start on the reservations of the one before it.
	KeptReservations.Reset();
	StopBeacon();
}

void FEasySessionReservations::OnServerTravelStarted()
{
	const AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get();
	KeptReservations.Reset(Beacon != nullptr ? Beacon->GetState() : nullptr);

	StopBeacon();
}

void FEasySessionReservations::StartBeacon()
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr || !UsesReservationBeacon(NamedSession->SessionSettings))
	{
		return;
	}

	if (BeaconHost.IsValid() && BeaconHost->GetWorld() == World)
	{
		return;
	}

	StopBeacon();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AEasySessionReservationBeaconHost* Beacon = World->SpawnActor<AEasySessionReservationBeaconHost>(SpawnParams);
	if (Beacon == nullptr)
	{
		return;
	}

	// Takes the reservations a server travel kept, and returns false when nothing was kept.
	const bool bRestoredReservations = Beacon->InitFromBeaconState(KeptReservations.Get());
	KeptReservations.Reset();

	// One team, because this plugin never splits a session into sides.
	// MaxPlayers counts the host too, so the same number is the team size and the reservation count.
	const int32 MaxPlayers = NamedSession->SessionSettings.NumPublicConnections;
	if (bRestoredReservations)
	{
		Beacon->WaitForEveryoneToArrive();
	}
	else if (!Beacon->InitHostBeacon(1, MaxPlayers, MaxPlayers, NAME_GameSession, 0, true))
	{
		// Without that state the parent refuses every reservation, so a beacon left running here would refuse every join.
		UE_LOG(LogEasySession, Error, TEXT("The reservation beacon cannot hold reservations for %d players, so it is not running. A refused join is now reported after the travel instead of before it."), MaxPlayers);
		Beacon->Destroy();
		return;
	}

	// Bound before the registration, so the first request the beacon receives is decided here.
	Beacon->OnApproveJoin().BindRaw(this, &FEasySessionReservations::ApproveJoin);

	if (!BeaconPort.Register(*Beacon))
	{
		UE_LOG(LogEasySession, Error, TEXT("The reservation beacon is not running. A refused join is now reported after the travel instead of before it."));
		Beacon->Destroy();
		return;
	}

	// After the registration, because the parent refuses every reservation until the listener owns this actor.
	// Kept reservations already include the host's, and a dedicated server is not a player, so it reserves none.
	if (!bRestoredReservations && Owner.IsHost())
	{
		AddHostReservation(*Beacon, *NamedSession);
	}

	BeaconHost = Beacon;
	CheckAdvertisedPort(NamedSession->SessionSettings);
}

void FEasySessionReservations::StopBeacon()
{
	if (AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get())
	{
		// Unbound before the destroy, so the actor can never call back into this object once it is gone.
		Beacon->OnApproveJoin().Unbind();
		BeaconPort.Unregister(*Beacon);
		Beacon->Destroy();
	}
	BeaconHost.Reset();
}

FEasyReservationResponse FEasySessionReservations::ApproveJoin(const FString& Password, const FUniqueNetIdRepl& Requester, const TArray<FUniqueNetIdRepl>& GroupMembers) const
{
	if (Requester.IsValid() && KickedPlayers.Contains(Requester))
	{
		UE_LOG(LogEasySession, Warning, TEXT("Reservations: refusing '%s' - the host removed this player from the session."), *Requester.ToString());
		return MakeResponse(EEasyReservationResult::Refused, NSLOCTEXT("EasySession", "RemovedFromSession", "The host removed you from this session."));
	}

	// The whole group travels on this one reservation, so approving it would let every player in except the removed one.
	const FUniqueNetIdRepl* KickedMember = GroupMembers.FindByPredicate([this](const FUniqueNetIdRepl& Member) { return KickedPlayers.Contains(Member); });
	if (KickedMember != nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Reservations: refusing '%s' - the host removed '%s', who travels with this player."), *Requester.ToString(), *KickedMember->ToString());
		return MakeResponse(EEasyReservationResult::Refused, NSLOCTEXT("EasySession", "GroupMemberRemoved", "The host removed a player who travels with you from this session."));
	}

	// This player was approved before: over this beacon, or as a player of the group whose leader asked for the reservation.
	// Reservations are kept across a map change, so this also lets in the players a hard travel reconnects.
	if (PlayerHasReservation(Requester))
	{
		return MakeResponse(EEasyReservationResult::Approved);
	}

	UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;

	// Searching already hides a started session, but a result fetched before the match started and a direct connect both get past that.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(OwnWorld);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession != nullptr && !NamedSession->SessionSettings.bAllowJoinInProgress)
	{
		// This runs on the host, where GetSessionState returns the local state rather than a replicated one.
		const EEasySessionState LocalState = Owner.GetSessionState();
		if (LocalState == EEasySessionState::Starting || LocalState == EEasySessionState::InProgress)
		{
			UE_LOG(LogEasySession, Warning, TEXT("Reservations: refusing '%s' - the match is in progress and join-in-progress is disabled."), *Requester.ToString());
			return MakeResponse(EEasyReservationResult::Refused, NSLOCTEXT("EasySession", "MatchInProgress", "The match is already in progress."));
		}
	}

	if (IsSessionFull())
	{
		UE_LOG(LogEasySession, Warning, TEXT("Reservations: refusing '%s' - the session is full."), *Requester.ToString());
		return MakeResponse(EEasyReservationResult::SessionFull, NSLOCTEXT("EasySession", "SessionFull", "The session is full."));
	}

	if (SessionPassword.IsEmpty())
	{
		return MakeResponse(EEasyReservationResult::Approved);
	}

	if (Password.TrimStartAndEnd().Equals(SessionPassword, ESearchCase::CaseSensitive))
	{
		return MakeResponse(EEasyReservationResult::Approved);
	}

	// Invited players arrive without the password, and invites only go to friends, so being a friend of the host counts as knowing it.
	if (bFriendsBypassPassword && Requester.IsValid())
	{
		const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(OwnWorld);
		const IOnlineFriendsPtr Friends = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
		if (Friends.IsValid() && Friends->IsFriend(0, *Requester.GetUniqueNetId(), EFriendsLists::ToString(EFriendsLists::Default)))
		{
			UE_LOG(LogEasySession, Log, TEXT("Reservations: '%s' joins without the password - friend of the host."), *Requester.ToString());
			return MakeResponse(EEasyReservationResult::Approved);
		}
	}

	// Never log the password: on a listen server the log file is on a player's machine.
	UE_LOG(LogEasySession, Warning, TEXT("Reservations: refusing '%s' - the session password did not match."), *Requester.ToString());
	return MakeResponse(EEasyReservationResult::WrongPassword, NSLOCTEXT("EasySession", "WrongPassword", "Wrong session password."));
}

void FEasySessionReservations::AddKickedPlayer(const FUniqueNetIdRepl& PlayerId)
{
	if (PlayerId.IsValid())
	{
		KickedPlayers.AddUnique(PlayerId);
		RemovePlayerReservation(PlayerId);
	}
}

bool FEasySessionReservations::UsesReservationBeacon(const FOnlineSessionSettings& Settings)
{
	int32 bUsesBeacon = 0;
	return Settings.Get(EasySession::SettingKey_Reservations, bUsesBeacon) && bUsesBeacon != 0;
}

void FEasySessionReservations::HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage)
{
	if (!IsOwnWorld(GameMode))
	{
		return;
	}

	// Another handler may already be refusing this player, so its reason is kept.
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// Only the reservation beacon receives the password, so a player without a reservation is checked as if they sent none.
	FEasyReservationResponse Response = ApproveJoin(FString(), NewPlayer);
	if (Response.Result == EEasyReservationResult::WrongPassword)
	{
		// This player sent no password at all, so "wrong password" would mislead them.
		UE_LOG(LogEasySession, Warning, TEXT("Reservations: '%s' arrived without a reservation, and only the reservation beacon checks the password."), *NewPlayer.ToString());
		Response.ReasonText = NSLOCTEXT("EasySession", "PasswordVerifyFailed", "Could not verify the session password.").ToString();
	}

	if (Response.Result != EEasyReservationResult::Approved)
	{
		ErrorMessage = RefusalMark + Response.ReasonText;
	}
}

void FEasySessionReservations::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!IsOwnWorld(GameMode))
	{
		return;
	}

	// A controller without a player state, such as the gameplay debugger's camera, never held a reservation.
	const APlayerState* PlayerState = Exiting != nullptr ? Exiting->PlayerState : nullptr;
	if (PlayerState == nullptr)
	{
		return;
	}

	RemovePlayerReservation(PlayerState->GetUniqueId());
}

void FEasySessionReservations::RemovePlayerReservation(const FUniqueNetIdRepl& PlayerId)
{
	if (AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get())
	{
		Beacon->RemovePlayerReservation(PlayerId);
	}
}

bool FEasySessionReservations::PlayerHasReservation(const FUniqueNetIdRepl& PlayerId) const
{
	const AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get();
	return Beacon != nullptr && PlayerId.IsValid() && Beacon->PlayerHasReservation(*PlayerId.GetUniqueNetId());
}

bool FEasySessionReservations::IsSessionFull() const
{
	if (const AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get())
	{
		return Beacon->GetMaxReservations() <= Beacon->GetNumConsumedReservations();
	}

	// Without the beacon the only number the host has is the players that already arrived, so an approved player still traveling is not counted.
	const UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	const AGameModeBase* GameMode = OwnWorld ? OwnWorld->GetAuthGameMode() : nullptr;
	return GameMode != nullptr && GameMode->GameSession != nullptr && GameMode->GameSession->AtCapacity(/*bSpectator=*/ false);
}

bool FEasySessionReservations::IsOwnWorld(const AGameModeBase* GameMode) const
{
	const UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	return GameMode != nullptr && OwnWorld != nullptr && GameMode->GetWorld() == OwnWorld;
}

void FEasySessionReservations::SetMaxReservations(int32 MaxPlayers)
{
	AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get();
	if (Beacon == nullptr)
	{
		return;
	}

	if (!Beacon->ReconfigureTeamAndPlayerCount(1, MaxPlayers, MaxPlayers))
	{
		UE_LOG(LogEasySession, Warning, TEXT("The reservation beacon still holds %d of %d reservations, which is more than the new Max Players of %d, so it keeps the previous one."),
			Beacon->GetNumConsumedReservations(), Beacon->GetMaxReservations(), MaxPlayers);
	}
}

void FEasySessionReservations::CheckAdvertisedPort(const FOnlineSessionSettings& Settings) const
{
	const int32 BoundPort = BeaconPort.GetListenPort();
	int32 AdvertisedPort = 0;
	Settings.Get(SETTING_BEACONPORT, AdvertisedPort);
	if (BoundPort == AdvertisedPort)
	{
		return;
	}

	UE_LOG(LogEasySession, Warning,
		TEXT("The reservation beacon listens on port %d but this session advertises %d, so joining players reach no beacon and a refusal arrives after the travel instead of before it. Free port %d, or move the beacon with -BeaconPort= or ListenPort under [/Script/OnlineSubsystemUtils.OnlineBeaconHost]."),
		BoundPort, AdvertisedPort, AdvertisedPort);
}

void FEasySessionReservations::AddHostReservation(AEasySessionReservationBeaconHost& Beacon, const FNamedOnlineSession& NamedSession) const
{
	const FUniqueNetIdRepl HostId(NamedSession.OwningUserId);
	if (!HostId.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("This session has no owning player id, so the reservation beacon holds no reservation for the host and one player more than Max Players can join."));
		return;
	}

	FPartyReservation Reservation;
	Reservation.TeamNum = 0;
	Reservation.PartyLeader = HostId;

	// The group follows after the host, and ApproveJoin skips the password and free slot checks for a reservation holder.
	Reservation.PartyMembers = EasySessionReservation::MakeReservations(HostId, HostGroup);

	// APartyBeaconHost::Tick never expires the owner of the session, so the host stays in this reservation for as long as the beacon runs.
	const EPartyReservationResult::Type Result = Beacon.AddPartyReservation(Reservation);
	if (Result != EPartyReservationResult::ReservationAccepted)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The reservation beacon holds no reservation for the host (%s), so one player more than Max Players can join."), EPartyReservationResult::ToString(Result));
	}
}
