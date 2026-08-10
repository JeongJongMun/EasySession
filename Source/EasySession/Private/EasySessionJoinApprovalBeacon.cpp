// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionJoinApprovalBeacon.h"

#include "EasySession.h"
#include "EasySessionServerGate.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AEasySessionJoinApprovalBeaconClient::AEasySessionJoinApprovalBeaconClient()
{
}

bool AEasySessionJoinApprovalBeaconClient::RequestApproval(const FString& Address, int32 Port, const FString& Password)
{
	PendingPassword = Password;

	FURL Url(nullptr, *Address, TRAVEL_Absolute);
	Url.Port = Port;

	if (!InitClient(Url))
	{
		UE_LOG(LogEasySession, Warning, TEXT("Approval request could not open a beacon connection to %s:%d."), *Address, Port);
		return false;
	}

	return true;
}

void AEasySessionJoinApprovalBeaconClient::OnConnected()
{
	ServerRequestJoin(PendingPassword);
}

void AEasySessionJoinApprovalBeaconClient::ServerRequestJoin_Implementation(const FString& Password)
{
	// Runs on the host, on the copy of this actor that AOnlineBeaconHostObject::
	// SpawnBeaconActor created for this connection. GetUniqueId is the id the joiner
	// presented at beacon login - the engine already refused the connection if it was invalid.
	FString Reason;
	bool bApproved = false;

	if (const AEasySessionJoinApprovalBeaconHostObject* HostObject = Cast<AEasySessionJoinApprovalBeaconHostObject>(GetBeaconOwner()))
	{
		bApproved = HostObject->ApproveJoin(GetUniqueId(), Password, Reason);
	}
	else
	{
		Reason = TEXT("The host is not answering join requests.");
	}

	ClientReceiveResponse(bApproved, Reason);
}

void AEasySessionJoinApprovalBeaconClient::ClientReceiveResponse_Implementation(bool bApproved, const FString& Reason)
{
	if (!bResponded)
	{
		bResponded = true;
		OnResponse.ExecuteIfBound(bApproved, Reason);
	}
}

void AEasySessionJoinApprovalBeaconClient::OnFailure()
{
	Super::OnFailure();

	// Covers refused/unreachable/timed-out connections. Deliberately after the Super
	// call, which marks the connection Invalid and destroys the beacon's net driver
	// (CleanupNetDriver). The one-shot guard keeps a late failure from double-reporting.
	if (!bResponded)
	{
		bResponded = true;
		OnResponse.ExecuteIfBound(false, TEXT("Could not reach the host to ask about joining."));
	}
}

AEasySessionJoinApprovalBeaconHostObject::AEasySessionJoinApprovalBeaconHostObject()
{
	ClientBeaconActorClass = AEasySessionJoinApprovalBeaconClient::StaticClass();
	BeaconTypeName = ClientBeaconActorClass->GetName();
}

bool AEasySessionJoinApprovalBeaconHostObject::ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& Password, FString& OutReason) const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UEasySessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UEasySessionSubsystem>() : nullptr;
	if (Subsystem == nullptr || !Subsystem->ServerGate.IsValid())
	{
		OutReason = TEXT("The host is not answering join requests.");
		return false;
	}

	return Subsystem->ServerGate->ApproveJoin(PlayerId, Password, OutReason);
}
