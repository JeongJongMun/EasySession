// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinRequest.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionHost.h"
#include "EasySessionReservations.h"
#include "EasySessionReservationBeacon.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	// Seconds between two checks whether GetGroupMembers still lists a player of the group.
	constexpr float GroupWaitIntervalSeconds = 0.1f;

	// Seconds a party leader or a host waits for the group before it joins without the rest.
	constexpr double GroupWaitTimeoutSeconds = 10.0;

	// Seconds the leader gets to complete the login, from the start of the connection until the member list holds the local player.
	constexpr float LoginTimeoutSeconds = 10.0f;
}

FEasySessionJoinRequest::FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, const FString& InPassword, const FString& InTravelOptions, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionJoinRequest(NAME_GameSession, InTarget, MoveTemp(InOnComplete))
{
	Password = InPassword;
	TravelOptions = InTravelOptions;
}

FEasySessionJoinRequest::FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionJoinRequest(NAME_PartySession, InTarget, MoveTemp(InOnComplete))
{
}

FEasySessionJoinRequest::FEasySessionJoinRequest(FName InSessionName, const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Join, InSessionName)
	, Target(InTarget)
	, OnComplete(MoveTemp(InOnComplete))
{
}

FEasySessionJoinRequest::~FEasySessionJoinRequest()
{
	DestroyReservationClient();
}

EEasySessionResult FEasySessionJoinRequest::GetRefusalResult(EEasyReservationResult Refusal)
{
	switch (Refusal)
	{
		case EEasyReservationResult::PasswordRequired:	return EEasySessionResult::PasswordRequired;
		case EEasyReservationResult::WrongPassword:		return EEasySessionResult::WrongPassword;
		case EEasyReservationResult::SessionFull:		return EEasySessionResult::JoinSessionFull;
		default:										return EEasySessionResult::JoinRefused;
	}
}

void FEasySessionJoinRequest::Execute()
{
	// A failed check completed the request with the refusal.
	if (!CheckCanJoin())
	{
		return;
	}

	// A party has no reservation beacon: the leader decides the join at the login, after the online subsystem joined the party session.
	if (IsPartyRequest())
	{
		JoinOnlineSession();
		return;
	}

	// A party leader takes the party along, and a host whose match has not started the players of its session, in the same reservation.
	GroupMembers = GetGroupMembers();

	if (FEasySessionReservations::UsesReservationBeacon(Target.NativeResult.Session.SessionSettings))
	{
		RequestReservation();
		return;
	}

	JoinWithoutReservation();
}

void FEasySessionJoinRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}

	FTSTicker::GetCoreTicker().RemoveTicker(GroupWaitHandle);
	GroupWaitHandle.Reset();

	FTSTicker::GetCoreTicker().RemoveTicker(TimeoutHandle);
	TimeoutHandle.Reset();

	// The reservation request may still be waiting for a response.
	DestroyReservationClient();
}

void FEasySessionJoinRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	// The game session this player was in is destroyed, so a failed join would keep them in its map with no session.
	// Requested before the completion below, the same order every travel in this plugin uses.
	if (Result != EEasySessionResult::Success && bLeftSession)
	{
		GetContext().Travel.ReturnToMainMenu();
	}

	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

bool FEasySessionJoinRequest::CheckCanJoin()
{
	if (!Target.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("The search result to join is invalid."));
		return false;
	}

	// A party has no game server to travel to, and a game session no leader to log in with, so the other kind would wait for a timeout.
	// A session of another project advertises no kind at all.
	if (EasySession::GetAdvertisedSessionType(Target.NativeResult.Session.SessionSettings) != SessionName)
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("The search result to join is not an EasySession session. Use one Find Easy Sessions returned."));
		return false;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return false;
	}

	return IsPartyRequest() ? CheckCanJoinPartySession(Sessions) : CheckCanJoinGameSession(Sessions);
}

bool FEasySessionJoinRequest::CheckCanJoinGameSession(const IOnlineSessionPtr& Sessions)
{
	// Destroying the session to join it again would only disconnect this player.
	const FNamedOnlineSession* CurrentSession = Sessions->GetNamedSession(NAME_GameSession);
	if (CurrentSession != nullptr && CurrentSession->SessionInfo.IsValid() && CurrentSession->SessionInfo->GetSessionId().ToString() == Target.NativeResult.GetSessionIdStr())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player is already in this session."));
		return false;
	}

	// A host that destroys its session to join another would end a match in progress for every player.
	const EEasySessionState LocalState = GetContext().Subsystem.GetSessionState();
	if (GetContext().Subsystem.IsSessionAuthority() && (LocalState == EEasySessionState::Starting || LocalState == EEasySessionState::InProgress))
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player hosts a match in progress, and leaving would end it for every player. End the match or call Leave Easy Session first."));
		return false;
	}

	return true;
}

bool FEasySessionJoinRequest::CheckCanJoinPartySession(const IOnlineSessionPtr& Sessions)
{
	if (Sessions->GetNamedSession(NAME_PartySession) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a party. Leave it with Leave Easy Session first."));
		return false;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A party is joined outside a game session. Call Leave Easy Session first."));
		return false;
	}

	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	if (!Identity.IsValid() || !Identity->GetUniquePlayerId(0).IsValid())
	{
		Complete(EEasySessionResult::JoinFailure, TEXT("No player is logged in to the online subsystem, so the party leader could not tell who joins."));
		return false;
	}

	return true;
}

void FEasySessionJoinRequest::RequestReservation()
{
	const FEasyReservationRequestComplete OnResponse = FEasyReservationRequestComplete::CreateSP(this, &FEasySessionJoinRequest::HandleReservationResponse);

	AEasySessionReservationBeaconClient* Client = nullptr;
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Client = World->SpawnActor<AEasySessionReservationBeaconClient>(SpawnParams);
	}

	if (Client == nullptr)
	{
		OnResponse.ExecuteIfBound(FEasyReservationResponse::Unreachable());
		return;
	}

	ReservationClient = Client;
	if (!Client->RequestJoin(Target, Password, GroupMembers, OnResponse))
	{
		// The delegate already fired with Unreachable.
		// Only the actor is left to destroy.
		DestroyReservationClient();
	}
}

void FEasySessionJoinRequest::HandleReservationResponse(const FEasyReservationResponse& Response)
{
	// The request may have been canceled while the beacon was waiting.
	if (!IsRunning())
	{
		return;
	}

	switch (Response.Result)
	{
		case EEasyReservationResult::Approved:
			if (GroupMembers.IsEmpty())
			{
				JoinOnlineSession();
			}
			else
			{
				JoinOnlineSessionWithGroup();
			}
			break;

		case EEasyReservationResult::Unreachable:
			UE_LOG(LogEasySession, Warning, TEXT("Could not reach the reservation beacon."));
			JoinWithoutReservation();
			break;

		default:
			Complete(GetRefusalResult(Response.Result), Response.ReasonText);
			break;
	}
}

void FEasySessionJoinRequest::DestroyReservationClient()
{
	if (AEasySessionReservationBeaconClient* Client = ReservationClient.Get())
	{
		Client->DestroyBeacon();
	}
	ReservationClient.Reset();
}

void FEasySessionJoinRequest::JoinOnlineSessionWithGroup()
{
	UE_LOG(LogEasySession, Log, TEXT("Moving %d players to '%s' before this host leaves."), GroupMembers.Num(), *Target.SessionDisplayName);

	// The group searches for the new host, which holds a reservation for each of its players.
	const FUniqueNetIdRepl NewHostId(Target.NativeResult.Session.OwningUserId);
	TellGroupToFollow(GroupMembers, NewHostId, Target.NativeResult.Session.SessionSettings.bIsLANMatch);

	// A player leaves the old group as soon as the follow notice arrives: a party member leaves the party, and a lobby player leaves the session.
	// The wait for that confirms the notice arrived, so the party or the session that carried it is not closed before it reached every player.
	GroupWaitStartSeconds = FPlatformTime::Seconds();
	GroupWaitHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleGroupWaitTick), GroupWaitIntervalSeconds);
}

bool FEasySessionJoinRequest::HandleGroupWaitTick(float DeltaTime)
{
	if (!IsRunning())
	{
		return false;
	}

	const TArray<FUniqueNetIdRepl> StillHere = GetGroupMembers();
	const bool bAnyStillHere = GroupMembers.ContainsByPredicate([&StillHere](const FUniqueNetIdRepl& Member) { return StillHere.Contains(Member); });
	if (bAnyStillHere && FPlatformTime::Seconds() - GroupWaitStartSeconds < GroupWaitTimeoutSeconds)
	{
		return true;
	}

	// A host destroys its session in JoinOnlineSession, so a player who did not follow in time is sent to the menu with the reason.
	// A party leader ends the party with a Leave request after the join succeeds, which also ends it for a member who did not follow.
	if (bAnyStillHere)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Not every player left to follow within %.0f seconds. The rest return to the menu when this host leaves."), GroupWaitTimeoutSeconds);
	}

	GroupWaitHandle.Reset();
	JoinOnlineSession();
	return false;
}

void FEasySessionJoinRequest::JoinWithoutReservation()
{
	// Only the reservation beacon checks the password, so the travel would only end in a refusal from PreLogin.
	if (Target.bPasswordProtected)
	{
		Complete(EEasySessionResult::JoinRefused, TEXT("Could not reach the host's reservation beacon, which checks the password, so this password-protected session cannot be joined."));
		return;
	}

	// Only a reservation holds the group, so a host that cannot be asked would split it.
	if (!GroupMembers.IsEmpty())
	{
		Complete(EEasySessionResult::JoinRefused, TEXT("Could not reach the host to ask for room for the group, so the group stays together."));
		return;
	}

	// Destroying the current session is safe only once the host approved the join, so a player in a session stays in it.
	if (GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::JoinRefused, TEXT("Could not reach the host to ask for a reservation, so this player stays in the current session."));
		return;
	}

	// PreLogin runs ApproveJoin when this player arrives, so a refusal arrives after the travel instead of before it.
	UE_LOG(LogEasySession, Log, TEXT("Joining without a reservation. The host decides when this player arrives."));
	JoinOnlineSession();
}

void FEasySessionJoinRequest::JoinOnlineSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	// JoinSession fails while a session with this name exists, so this player destroys theirs first.
	// A party join never starts inside a party.
	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		// The Destroy sub-request destroys the session of a host, so its clients are told why before their connection closes.
		if (GetContext().Subsystem.IsSessionAuthority())
		{
			GetContext().Host.TellEveryoneToReturnToMainMenu(EasySession::GetHostLeftSessionReason());
		}

		RunSubRequest(MakeShared<FEasySessionDestroyRequest>(
			FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleDestroyComplete)));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleJoinSessionComplete));

	if (IsPartyRequest())
	{
		UE_LOG(LogEasySession, Log, TEXT("Joining the party of '%s'"), *Target.SessionDisplayName);
	}
	else
	{
		UE_LOG(LogEasySession, Log, TEXT("Joining session '%s' hosted by '%s'"), *Target.SessionDisplayName, *Target.HostName);
	}

	if (!Sessions->JoinSession(0, SessionName, Target.NativeResult))
	{
		Complete(EEasySessionResult::JoinFailure, TEXT("JoinSession request was rejected by the online subsystem."));
	}
}

void FEasySessionJoinRequest::HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	// Joining without having left would fail with AlreadyInSession, so the join ends with the destroy's result instead.
	if (Result != EEasySessionResult::Success)
	{
		Complete(Result, TEXT("This player could not leave the session they were in."));
		return;
	}

	bLeftSession = true;
	JoinOnlineSession();
}

void FEasySessionJoinRequest::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	const FString Kind = SessionName.ToString();
	switch (JoinResult)
	{
		case EOnJoinSessionCompleteResult::Success:
			break;

		case EOnJoinSessionCompleteResult::SessionIsFull:
			Complete(EEasySessionResult::JoinSessionFull, FString::Printf(TEXT("The %s is full."), *Kind));
			return;

		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Complete(EEasySessionResult::JoinSessionDoesNotExist, FString::Printf(TEXT("The %s no longer exists."), *Kind));
			return;

		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			Complete(EEasySessionResult::ResolveFailure, TEXT("Could not retrieve the host address."));
			return;

		case EOnJoinSessionCompleteResult::AlreadyInSession:
			Complete(EEasySessionResult::SessionAlreadyExists, FString::Printf(TEXT("This player is already in the %s. Destroy the current session before joining it again."), *Kind));
			return;

		// UnknownError, and anything the engine adds to this enum later.
		default:
			Complete(EEasySessionResult::JoinFailure, FString::Printf(TEXT("The online subsystem failed to join the %s."), *Kind));
			return;
	}

	// The address is resolved before the request completes, so a host that is not listening fails here instead of the joining player waiting for a timeout.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FString ConnectString;
	const FName PortType = IsPartyRequest() ? NAME_BeaconPort : NAME_GamePort;
	const bool bResolved = Sessions.IsValid() && Sessions->GetResolvedConnectString(SessionName, ConnectString, PortType) && !ConnectString.IsEmpty();

	if (IsPartyRequest())
	{
		if (!bResolved)
		{
			LeaveAndComplete(EEasySessionResult::ResolveFailure, TEXT("Could not retrieve the address of the party leader."));
			return;
		}

		ConnectToLeader(ConnectString);
		return;
	}

	if (!bResolved || EasySessionAddress::HasZeroPort(ConnectString))
	{
		LeaveAndComplete(EEasySessionResult::ResolveFailure, FString::Printf(
			TEXT("The host address '%s' is not connectable. The host is not running as a listen server, because its travel to Initial Map Name did not open one. Check the map path on the host."),
			*ConnectString));
		return;
	}

	TravelToHost(ConnectString);
}

void FEasySessionJoinRequest::TravelToHost(const FString& ConnectString)
{
	UE_LOG(LogEasySession, Log, TEXT("Session joined successfully."));

	// A party exists only outside game sessions, so HandlePartyEnded queues a Leave request.
	if (GetContext().Party.IsInParty())
	{
		GetContext().Subsystem.HandlePartyEnded(EEasyPartyLeaveReason::MovedToGameSession, EasySession::GetPartyMovedReason());
	}

	// Requested before the request completes, so IsBusy is already true for the travel when the completion delegate fires.
	GetContext().Travel.TravelToJoinedSession(ConnectString, TravelOptions);

	Complete(EEasySessionResult::Success);
}

void FEasySessionJoinRequest::ConnectToLeader(const FString& ConnectString)
{
	const FString PartySessionId = Target.NativeResult.Session.SessionInfo.IsValid() ? Target.NativeResult.Session.SessionInfo->GetSessionId().ToString() : FString();
	if (!GetContext().Party.ConnectToLeader(ConnectString, PartySessionId, FEasyPartyConnectComplete::CreateSP(this, &FEasySessionJoinRequest::HandleConnectComplete)))
	{
		LeaveAndComplete(EEasySessionResult::ResolveFailure, TEXT("Could not reach the party leader."));
		return;
	}

	TimeoutHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleTimeout), LoginTimeoutSeconds);
}

void FEasySessionJoinRequest::HandleConnectComplete(bool bSuccess, const FText& Reason)
{
	if (!IsRunning())
	{
		return;
	}

	if (!bSuccess)
	{
		LeaveAndComplete(EEasySessionResult::JoinRefused, Reason.ToString());
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Joined the party."));
	Complete(EEasySessionResult::Success);
}

bool FEasySessionJoinRequest::HandleTimeout(float DeltaTime)
{
	TimeoutHandle.Reset();
	if (IsRunning())
	{
		LeaveAndComplete(EEasySessionResult::JoinRefused, TEXT("The party leader did not answer."));
	}
	return false;
}

void FEasySessionJoinRequest::LeaveAndComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	FTSTicker::GetCoreTicker().RemoveTicker(TimeoutHandle);
	TimeoutHandle.Reset();

	// The joined session stays until it is destroyed, and a retry or the next matchmaking search result would fail against it.
	if (IsPartyRequest())
	{
		GetContext().Party.Close();
	}

	RunSubRequest(MakeShared<FEasySessionDestroyRequest>(FEasySessionCompleteDelegate::CreateSPLambda(this,
		[this, Result, ErrorMessage](EEasySessionResult /*DestroyResult*/, const FString& /*DestroyError*/)
		{
			Complete(Result, ErrorMessage);
		})));
}
