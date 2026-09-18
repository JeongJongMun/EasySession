// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinRequest.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "Engine/World.h"

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
	StopApprovalClient();
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
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it before joining another one."));
		return;
	}

	// Sessions without the approval key are joined directly.
	// The server gate still decides, after the travel instead of before it.
	if (FEasySessionJoinApproval::IsAdvertisedBy(Target.NativeResult.Session.SessionSettings))
	{
		RequestJoinApproval();
		return;
	}

	JoinOnlineSession();
}

void FEasySessionJoinRequest::RequestJoinApproval()
{
	const FEasyJoinApprovalComplete OnResponse = FEasyJoinApprovalComplete::CreateSP(this, &FEasySessionJoinRequest::HandleJoinApprovalResponse);

	AEasySessionJoinApprovalBeaconClient* Client = nullptr;
	if (UWorld* World = GetContext().GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Client = World->SpawnActor<AEasySessionJoinApprovalBeaconClient>(SpawnParams);
	}

	if (Client == nullptr)
	{
		OnResponse.ExecuteIfBound(FEasyJoinApprovalResponse::Unreachable());
		return;
	}

	ApprovalClient = Client;
	if (!Client->RequestApproval(Target, Password, OnResponse))
	{
		// The delegate already fired with Unreachable.
		// Only the actor is left to destroy.
		StopApprovalClient();
	}
}

void FEasySessionJoinRequest::HandleJoinApprovalResponse(const FEasyJoinApprovalResponse& Response)
{
	// The request may have passed its deadline while the beacon was waiting.
	if (!IsActive())
	{
		return;
	}

	switch (Response.Result)
	{
		case EEasyJoinApprovalResult::Approved:
			JoinOnlineSession();
			break;

		case EEasyJoinApprovalResult::Unreachable:
			// The join continues without the approval, because the server gate runs the same ApproveJoin when the joining player arrives.
			// An unreachable beacon can only delay a refusal, never skip one.
			UE_LOG(LogEasySession, Warning, TEXT("Could not ask the join approval beacon - joining directly. A refusal will now arrive after the travel instead of before it."));
			JoinOnlineSession();
			break;

		case EEasyJoinApprovalResult::WrongPassword:
			Complete(EEasySessionResult::WrongPassword, Response.ReasonText);
			break;

		case EEasyJoinApprovalResult::SessionFull:
			Complete(EEasySessionResult::JoinSessionFull, Response.ReasonText);
			break;

		default:
			Complete(EEasySessionResult::JoinRefused, Response.ReasonText);
			break;
	}
}

void FEasySessionJoinRequest::StopApprovalClient()
{
	if (AEasySessionJoinApprovalBeaconClient* Client = ApprovalClient.Get())
	{
		Client->DestroyBeacon();
	}
	ApprovalClient.Reset();
}

void FEasySessionJoinRequest::JoinOnlineSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
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

void FEasySessionJoinRequest::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	if (!IsActive() || InSessionName != SessionName)
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
		// Queued before the request completes, so a retry started in the completion delegate runs after this destroy.
		// Without it the retry would fail, because the session this join created still exists.
		GetContext().Subsystem.DestroyEasySession();

		Complete(EEasySessionResult::ResolveFailure, FString::Printf(
			TEXT("The host address '%s' is not connectable - the host is not running as a listen server. The host's travel to its Initial Map Name did not open one. Check the map path on the host."),
			*ConnectString));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session joined successfully."));

	// Requested before the request completes, so Is Busy is already true for the travel when the completion delegate fires.
	GetContext().Travel.TravelToJoinedSession(ConnectString, Password, TravelOptions);

	Complete(EEasySessionResult::Success);
}

void FEasySessionJoinRequest::Cleanup(bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}

	// The approval request may still be waiting for a response.
	StopApprovalClient();

	if (bAbandoned)
	{
		DestroySessionLeftBehind();
	}
}

void FEasySessionJoinRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
	GetContext().Subsystem.OnSessionJoined.Broadcast(Result, ErrorMessage);
}
