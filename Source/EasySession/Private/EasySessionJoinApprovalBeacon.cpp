// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApprovalBeacon.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

namespace
{
	/**
	 * How long the whole approval gets before the request completes as Unreachable: reaching the host's beacon and its answer.
	 * A host that advertises a beacon port nothing listens on never refuses the connection, so the joining player would wait for the engine's own retries.
	 */
	constexpr float ApprovalTimeoutSeconds = 5.0f;
}

FEasyJoinApprovalResponse FEasyJoinApprovalResponse::Unreachable()
{
	FEasyJoinApprovalResponse Response;
	Response.Result = EEasyJoinApprovalResult::Unreachable;
	Response.ReasonText = TEXT("Could not reach the host to ask about joining.");
	return Response;
}

FEasyJoinApprovalResponse FEasyJoinApprovalResponse::NotAnswering()
{
	FEasyJoinApprovalResponse Response;
	Response.Result = EEasyJoinApprovalResult::Refused;
	Response.ReasonText = TEXT("The host is not answering join requests.");
	return Response;
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

	// Started here rather than on connect, because reaching the beacon at all is the part that can hang.
	GetWorldTimerManager().SetTimer(ApprovalTimeoutHandle, this, &AEasySessionJoinApprovalBeaconClient::HandleApprovalTimeout, ApprovalTimeoutSeconds, false);
	return true;
}

void AEasySessionJoinApprovalBeaconClient::OnConnected()
{
	ServerRequestJoinApproval(PendingRequest);
}

void AEasySessionJoinApprovalBeaconClient::ServerRequestJoinApproval_Implementation(const FEasyJoinApprovalRequest& Request)
{
	// Runs on the host, on the copy of this actor that AOnlineBeaconHostObject::SpawnBeaconActor created for this connection.
	// GetUniqueId is the id the joining player presented at beacon login.
	// The engine already refused the connection if it was invalid.
	FEasyJoinApprovalResponse Response;
	if (const AEasySessionJoinApprovalBeaconHostObject* HostObject = Cast<AEasySessionJoinApprovalBeaconHostObject>(GetBeaconOwner()))
	{
		Response = HostObject->ApproveJoin(Request, GetUniqueId());
	}
	else
	{
		Response = FEasyJoinApprovalResponse::NotAnswering();
	}

	ClientReceiveJoinApproval(Response);
}

void AEasySessionJoinApprovalBeaconClient::ClientReceiveJoinApproval_Implementation(const FEasyJoinApprovalResponse& Response)
{
	GetWorldTimerManager().ClearTimer(ApprovalTimeoutHandle);
	Signal(Response);
}

void AEasySessionJoinApprovalBeaconClient::OnFailure()
{
	// Covers refused, unreachable and timed out connections.
	// Deliberately after the Super call, which marks the connection Invalid and destroys the beacon's net driver.
	// Signal keeps a late failure from reporting twice.
	Super::OnFailure();

	GetWorldTimerManager().ClearTimer(ApprovalTimeoutHandle);
	SignalUnreachable(TEXT("the beacon connection failed"));
}

void AEasySessionJoinApprovalBeaconClient::DestroyBeacon()
{
	// A destroyed request must not respond. Dropping the delegate here means that destroying the beacon cancels the request.
	CompleteDelegate.Unbind();
	GetWorldTimerManager().ClearTimer(ApprovalTimeoutHandle);

	Super::DestroyBeacon();
}

void AEasySessionJoinApprovalBeaconClient::HandleApprovalTimeout()
{
	SignalUnreachable(TEXT("the host's beacon did not answer in time"));
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

	Signal(FEasyJoinApprovalResponse::Unreachable());
}

AEasySessionJoinApprovalBeaconHostObject::AEasySessionJoinApprovalBeaconHostObject()
{
	ClientBeaconActorClass = AEasySessionJoinApprovalBeaconClient::StaticClass();
	BeaconTypeName = ClientBeaconActorClass->GetName();
}

FEasyJoinApprovalResponse AEasySessionJoinApprovalBeaconHostObject::ApproveJoin(const FEasyJoinApprovalRequest& Request, const FUniqueNetIdRepl& Requester) const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
	return Subsystem != nullptr ? Subsystem->ApproveJoin(Request, Requester) : FEasyJoinApprovalResponse::NotAnswering();
}
