// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionReservations.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
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
	/** A response with this result, and the message a refused player sees. */
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

void FEasySessionReservations::OnSessionCreated(const FEasySessionHostParams& Params)
{
	SessionPassword = Params.Password.TrimStartAndEnd();
	bFriendsBypassPassword = Params.bFriendsBypassPassword;
}

void FEasySessionReservations::OnSettingsUpdated(const FEasySessionSettings& Settings)
{
	SessionPassword = Settings.Password.TrimStartAndEnd();
	bFriendsBypassPassword = Settings.bFriendsBypassPassword;
	SetMaxPlayerSlots(Settings.MaxPlayers);
}

void FEasySessionReservations::OnSessionDestroyed()
{
	SessionPassword.Empty();
	bFriendsBypassPassword = false;

	// A new session must never start on the slots of the one before it.
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
	if (NamedSession == nullptr || !IsAdvertisedBy(NamedSession->SessionSettings))
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

	// Takes the slots a server travel kept, and returns false when nothing was kept.
	const bool bKeptSlots = Beacon->InitFromBeaconState(KeptReservations.Get());
	KeptReservations.Reset();

	// One team, because this plugin never splits a session into sides.
	// Max Players counts the host too, so the same number is the team size and the slot count.
	const int32 MaxPlayers = NamedSession->SessionSettings.NumPublicConnections;
	if (bKeptSlots)
	{
		Beacon->WaitForEveryoneToArrive();
	}
	else if (!Beacon->InitHostBeacon(1, MaxPlayers, MaxPlayers, NAME_GameSession, 0, true))
	{
		// Without that state the parent refuses every reservation, so a beacon left running here would refuse every join.
		UE_LOG(LogEasySession, Error, TEXT("The reservation beacon cannot hold player slots for %d players, so it is not running. A refused join is now reported after the travel instead of before it."), MaxPlayers);
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
	// Kept slots already include the host's, and a dedicated server is not a player, so it reserves none.
	if (!bKeptSlots && Owner.IsHost())
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

FEasyReservationResponse FEasySessionReservations::ApproveJoin(const FString& Password, const FUniqueNetIdRepl& Requester) const
{
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

	if (IsSessionFull(Requester))
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

bool FEasySessionReservations::IsAdvertisedBy(const FOnlineSessionSettings& Settings)
{
	int32 bRunsBeacon = 0;
	return Settings.Get(EasySession::SettingKey_Reservations, bRunsBeacon) && bRunsBeacon != 0;
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

	UWorld* OwnWorld = Owner.GetGameInstance()->GetWorld();

	// The password arrives in the travel URL.
	// The engine sets Connection->PlayerId before PreLogin, so the joining player's connection can be found by id and its URL read.
	// In-session map changes must use seamless travel, or players already in would be refused here.
	FString PasswordFromTravelURL;
	if (!SessionPassword.IsEmpty())
	{
		const UNetConnection* PendingConnection = nullptr;
		if (const UNetDriver* NetDriver = OwnWorld->GetNetDriver())
		{
			for (const TObjectPtr<UNetConnection>& ClientConnection : NetDriver->ClientConnections)
			{
				if (ClientConnection != nullptr && ClientConnection->PlayerId == NewPlayer)
				{
					PendingConnection = ClientConnection;
					break;
				}
			}
		}

		if (PendingConnection == nullptr)
		{
			UE_LOG(LogEasySession, Warning, TEXT("PreLogin: could not find the pending connection for '%s'. Rejecting to protect the password session."), *NewPlayer.ToString());
			ErrorMessage = RefusalMark + NSLOCTEXT("EasySession", "PasswordVerifyFailed", "Could not verify the session password.").ToString();
			return;
		}

		PasswordFromTravelURL = EasySessionAddress::DecodeTravelOptionValue(
			EasySessionAddress::ParseTravelOption(PendingConnection->RequestURL, EasySession::TravelOption_Password));
	}

	const FEasyReservationResponse Response = ApproveJoin(PasswordFromTravelURL, NewPlayer);
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

	// A controller without a player state, such as the gameplay debugger's camera, never held a slot.
	const APlayerState* PlayerState = Exiting != nullptr ? Exiting->PlayerState : nullptr;
	if (PlayerState == nullptr)
	{
		return;
	}

	ReleasePlayerSlot(PlayerState->GetUniqueId());
}

void FEasySessionReservations::ReleasePlayerSlot(const FUniqueNetIdRepl& PlayerId)
{
	if (AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get())
	{
		Beacon->ReleasePlayerSlot(PlayerId);
	}
}

bool FEasySessionReservations::IsSessionFull(const FUniqueNetIdRepl& PlayerId) const
{
	if (const AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get())
	{
		// This player holds a slot already, so arriving on it takes no other one.
		if (PlayerId.IsValid() && Beacon->PlayerHasReservation(*PlayerId.GetUniqueNetId()))
		{
			return false;
		}

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

void FEasySessionReservations::SetMaxPlayerSlots(int32 MaxPlayers)
{
	AEasySessionReservationBeaconHost* Beacon = BeaconHost.Get();
	if (Beacon == nullptr)
	{
		return;
	}

	if (!Beacon->ReconfigureTeamAndPlayerCount(1, MaxPlayers, MaxPlayers))
	{
		UE_LOG(LogEasySession, Warning, TEXT("The reservation beacon still holds %d of %d player slots, which is more than the new Max Players of %d, so it keeps the previous one."),
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

void FEasySessionReservations::AddHostReservation(AEasySessionReservationBeaconHost& Beacon, const FNamedOnlineSession& NamedSession)
{
	const FUniqueNetIdRepl HostId(NamedSession.OwningUserId);
	if (!HostId.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("This session has no owning player id, so the reservation beacon holds no slot for the host and one player more than Max Players can join."));
		return;
	}

	FPartyReservation Reservation;
	Reservation.TeamNum = 0;
	Reservation.PartyLeader = HostId;

	Reservation.PartyMembers.Add(EasySessionReservation::MakePlayerReservation(HostId));

	// APartyBeaconHost::Tick never expires the owner of the session, so this slot is held for as long as the beacon runs.
	const EPartyReservationResult::Type Result = Beacon.AddPartyReservation(Reservation);
	if (Result != EPartyReservationResult::ReservationAccepted)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The reservation beacon holds no slot for the host (%s), so one player more than Max Players can join."), EPartyReservationResult::ToString(Result));
	}
}
