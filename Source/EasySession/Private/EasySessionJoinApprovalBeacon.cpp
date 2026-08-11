// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApprovalBeacon.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

namespace
{
	/** How long a connected host gets to answer before the ask completes as Unreachable. */
	constexpr float ResponseTimeoutSeconds = 5.0f;
}

AEasySessionJoinApprovalBeaconClient::AEasySessionJoinApprovalBeaconClient()
{
}

bool AEasySessionJoinApprovalBeaconClient::RequestApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete)
{
	CompleteDelegate = OnComplete;

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	PendingRequest.PartyMembers = { LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl() };
	PendingRequest.Credential = Password;

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(Target.NativeResult, NAME_BeaconPort, ConnectString))
	{
		SignalUnreachable(TEXT("the search result does not resolve to a beacon address"));
		return false;
	}

	FURL ConnectURL(nullptr, *ConnectString, TRAVEL_Absolute);
	if (!InitClient(ConnectURL))
	{
		SignalUnreachable(TEXT("a beacon connection could not be opened"));
		return false;
	}

	return true;
}

void AEasySessionJoinApprovalBeaconClient::OnConnected()
{
	GetWorldTimerManager().SetTimer(ResponseTimeoutHandle, this, &AEasySessionJoinApprovalBeaconClient::HandleResponseTimeout, ResponseTimeoutSeconds, false);

	ServerRequestJoinApproval(PendingRequest);
}

void AEasySessionJoinApprovalBeaconClient::ServerRequestJoinApproval_Implementation(const FEasyJoinApprovalRequest& Request)
{
	// Runs on the host, on the copy of this actor that AOnlineBeaconHostObject::
	// SpawnBeaconActor created for this connection. GetUniqueId is the id the joiner
	// presented at beacon login - the engine already refused the connection if it was invalid.
	FEasyJoinApprovalResponse Response;
	if (const AEasySessionJoinApprovalBeaconHostObject* HostObject = Cast<AEasySessionJoinApprovalBeaconHostObject>(GetBeaconOwner()))
	{
		Response.Result = HostObject->ApproveJoin(GetUniqueId(), Request.Credential, Response.ReasonText);
	}
	else
	{
		Response.Result = EEasyJoinApprovalResult::Refused;
		Response.ReasonText = TEXT("The host is not answering join requests.");
	}

	ClientReceiveJoinApproval(Response);
}

void AEasySessionJoinApprovalBeaconClient::ClientReceiveJoinApproval_Implementation(const FEasyJoinApprovalResponse& Response)
{
	GetWorldTimerManager().ClearTimer(ResponseTimeoutHandle);
	Signal(Response);
}

void AEasySessionJoinApprovalBeaconClient::OnFailure()
{
	// Covers refused/unreachable/timed-out connections. Deliberately after the Super
	// call, which marks the connection Invalid and destroys the beacon's net driver
	// (CleanupNetDriver). Signal keeps a late failure from double-reporting.
	Super::OnFailure();

	GetWorldTimerManager().ClearTimer(ResponseTimeoutHandle);
	SignalUnreachable(TEXT("the beacon connection failed"));
}

void AEasySessionJoinApprovalBeaconClient::DestroyBeacon()
{
	// A destroyed ask must not answer: dropping the delegate here is what makes StopClient a cancel.
	CompleteDelegate.Unbind();
	GetWorldTimerManager().ClearTimer(ResponseTimeoutHandle);

	Super::DestroyBeacon();
}

void AEasySessionJoinApprovalBeaconClient::HandleResponseTimeout()
{
	SignalUnreachable(TEXT("the host did not answer in time"));
}

void AEasySessionJoinApprovalBeaconClient::Signal(const FEasyJoinApprovalResponse& Response)
{
	if (bCompleted)
	{
		return;
	}

	bCompleted = true;

	// The handler may destroy this actor, whose DestroyBeacon unbinds CompleteDelegate.
	// A moved local keeps the executing delegate out of that unbind.
	FEasyJoinApprovalComplete LocalDelegate = MoveTemp(CompleteDelegate);
	LocalDelegate.ExecuteIfBound(Response);
}

void AEasySessionJoinApprovalBeaconClient::SignalUnreachable(const TCHAR* LogWhy)
{
	if (!bCompleted)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Join approval request could not reach the host: %s."), LogWhy);
	}

	FEasyJoinApprovalResponse Response;
	Response.Result = EEasyJoinApprovalResult::Unreachable;
	Response.ReasonText = TEXT("Could not reach the host to ask about joining.");
	Signal(Response);
}

AEasySessionJoinApprovalBeaconHostObject::AEasySessionJoinApprovalBeaconHostObject()
{
	ClientBeaconActorClass = AEasySessionJoinApprovalBeaconClient::StaticClass();
	BeaconTypeName = ClientBeaconActorClass->GetName();
}

EEasyJoinApprovalResult AEasySessionJoinApprovalBeaconHostObject::ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& Password, FString& OutReason) const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
	if (Subsystem == nullptr || !Subsystem->ServerGate.IsValid())
	{
		OutReason = TEXT("The host is not answering join requests.");
		return EEasyJoinApprovalResult::Refused;
	}

	return Subsystem->ServerGate->ApproveJoin(PlayerId, Password, OutReason);
}
