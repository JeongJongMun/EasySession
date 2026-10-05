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

namespace
{
	// Seconds between two checks whether GetGroupMembers still lists a player of the group.
	constexpr float GroupWaitIntervalSeconds = 0.1f;

	// Seconds a party leader or a host waits for the group before it joins without the rest.
	constexpr double GroupWaitTimeoutSeconds = 10.0;
}

FEasySessionJoinRequest::FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, const FString& InPassword, const FString& InTravelOptions, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Join)
	, Target(InTarget)
	, Password(InPassword)
	, TravelOptions(InTravelOptions)
	, OnComplete(MoveTemp(InOnComplete))
{
}

FEasySessionJoinRequest::~FEasySessionJoinRequest()
{
	DestroyReservationClient();
}

void FEasySessionJoinRequest::Execute()
{
	if (!Target.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("The search result to join is invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	// Destroying the session to join it again would only disconnect this player.
	const FNamedOnlineSession* CurrentSession = Sessions->GetNamedSession(SessionName);
	if (CurrentSession != nullptr && CurrentSession->SessionInfo.IsValid() && CurrentSession->SessionInfo->GetSessionId().ToString() == Target.NativeResult.GetSessionIdStr())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player is already in this session."));
		return;
	}

	// A host that destroys its session to join another would end a match in progress for every player.
	const EEasySessionState LocalState = GetContext().Subsystem.GetSessionState();
	if (GetContext().Subsystem.IsSessionAuthority() && (LocalState == EEasySessionState::Starting || LocalState == EEasySessionState::InProgress))
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player hosts a match in progress, and leaving would end it for every player. End the match or call Leave Easy Session first."));
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

	// The reservation request may still be waiting for a response.
	DestroyReservationClient();
}

void FEasySessionJoinRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	// The session this player was in is destroyed, so a failed join would keep them in its map with no session.
	// Requested before the completion below, the same order every travel in this plugin uses.
	if (Result != EEasySessionResult::Success && bLeftSession)
	{
		GetContext().Travel.ReturnToMenu();
	}

	OnComplete.ExecuteIfBound(Result, ErrorMessage);
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

		case EEasyReservationResult::WrongPassword:
			Complete(EEasySessionResult::WrongPassword, Response.ReasonText);
			break;

		case EEasyReservationResult::SessionFull:
			Complete(EEasySessionResult::JoinSessionFull, Response.ReasonText);
			break;

		default:
			Complete(EEasySessionResult::JoinRefused, Response.ReasonText);
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
	// A party leader closes the party with a LeaveParty request after the join succeeds, which also ends the party for a member who did not follow.
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
		Complete(EEasySessionResult::JoinRefused, TEXT("Could not reach the host to check the password."));
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
	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		// The Destroy sub-request destroys the session of a host, so its clients are told why before their connection closes.
		if (GetContext().Subsystem.IsSessionAuthority())
		{
			GetContext().Host.TellEveryoneToReturnToMenu(EasySession::GetHostLeftSessionReason());
		}

		RunSubRequest(MakeShared<FEasySessionDestroyRequest>(
			FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleDestroyComplete)));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateSP(this, &FEasySessionJoinRequest::HandleJoinSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Joining session '%s' hosted by '%s'"), *Target.SessionDisplayName, *Target.HostName);

	if (!Sessions->JoinSession(0, SessionName, Target.NativeResult))
	{
		Complete(EEasySessionResult::JoinFailure, TEXT("JoinSession request was rejected by the online subsystem."));
	}
}

void FEasySessionJoinRequest::HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	// The session is still there, so this player stays in it and no travel starts.
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

	switch (JoinResult)
	{
		case EOnJoinSessionCompleteResult::Success:
			break;

		case EOnJoinSessionCompleteResult::SessionIsFull:
			Complete(EEasySessionResult::JoinSessionFull, TEXT("The session is full."));
			return;

		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Complete(EEasySessionResult::JoinSessionDoesNotExist, TEXT("The session no longer exists."));
			return;

		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			Complete(EEasySessionResult::ResolveFailure, TEXT("Could not retrieve the host address."));
			return;

		case EOnJoinSessionCompleteResult::AlreadyInSession:
			Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player is already in the session. Destroy the current session before joining it again."));
			return;

		// UnknownError, and anything the engine adds to this enum later.
		default:
			Complete(EEasySessionResult::JoinFailure, TEXT("The online subsystem failed to join the session."));
			return;
	}

	// Resolve the host address before completing with Success.
	// A host that is not listening then fails here, instead of the joining player waiting for a connection timeout.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FString ConnectString;
	const bool bResolved = Sessions.IsValid() && Sessions->GetResolvedConnectString(SessionName, ConnectString) && !ConnectString.IsEmpty();

	if (!bResolved || EasySessionAddress::HasZeroPort(ConnectString))
	{
		const FString Message = FString::Printf(
			TEXT("The host address '%s' is not connectable. The host is not running as a listen server, because its travel to Initial Map Name did not open one. Check the map path on the host."),
			*ConnectString);

		// The joined session stays until it is destroyed, and a retry or the next matchmaking search result would fail against it.
		RunSubRequest(MakeShared<FEasySessionDestroyRequest>(FEasySessionCompleteDelegate::CreateSPLambda(this,
			[this, Message](EEasySessionResult /*DestroyResult*/, const FString& /*DestroyError*/)
			{
				Complete(EEasySessionResult::ResolveFailure, Message);
			})));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session joined successfully."));

	// A party lives outside game sessions, so HandlePartyEnded queues a LeaveParty request.
	// The leader waiting for this player then no longer lists them in GetGroupMembers.
	if (GetContext().Party.IsInParty())
	{
		GetContext().Subsystem.HandlePartyEnded(EEasyPartyLeaveReason::MovedToGameSession, EasySession::GetPartyMovedReason());
	}

	// Requested before the request completes, so IsBusy is already true for the travel when the completion delegate fires.
	GetContext().Travel.TravelToJoinedSession(ConnectString, TravelOptions);

	Complete(EEasySessionResult::Success);
}
