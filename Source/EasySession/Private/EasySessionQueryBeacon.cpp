// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionQueryBeacon.h"

#include "EasySession.h"

AEasySessionQueryBeaconClient::AEasySessionQueryBeaconClient()
{
}

bool AEasySessionQueryBeaconClient::Query(const FString& Address, int32 Port, const FString& Password)
{
	PendingPassword = Password;

	FURL Url(nullptr, *Address, TRAVEL_Absolute);
	Url.Port = Port;

	if (!InitClient(Url))
	{
		UE_LOG(LogEasySession, Warning, TEXT("Join query could not open a beacon connection to %s:%d."), *Address, Port);
		return false;
	}

	return true;
}

void AEasySessionQueryBeaconClient::OnConnected()
{
	ServerRequestJoin(PendingPassword);
}

void AEasySessionQueryBeaconClient::ServerRequestJoin_Implementation(const FString& Password)
{
	// Runs on the host's mirror of this actor, spawned by the host object.
	FString Reason;
	bool bApproved = false;

	if (const AEasySessionQueryBeaconHostObject* HostObject = Cast<AEasySessionQueryBeaconHostObject>(GetBeaconOwner()))
	{
		bApproved = HostObject->ApproveJoin(Password, Reason);
	}
	else
	{
		Reason = TEXT("The host is not answering join queries.");
	}

	ClientReceiveResponse(bApproved, Reason);
}

void AEasySessionQueryBeaconClient::ClientReceiveResponse_Implementation(bool bApproved, const FString& Reason)
{
	if (!bResponded)
	{
		bResponded = true;
		OnResponse.ExecuteIfBound(bApproved, Reason);
	}
}

void AEasySessionQueryBeaconClient::OnFailure()
{
	Super::OnFailure();

	// Covers refused/unreachable/timed-out connections. Deliberately after the
	// Super call: the base class tears the connection down either way, and the
	// one-shot guard keeps a late failure from double-reporting.
	if (!bResponded)
	{
		bResponded = true;
		OnResponse.ExecuteIfBound(false, TEXT("Could not reach the host to ask about joining."));
	}
}

AEasySessionQueryBeaconHostObject::AEasySessionQueryBeaconHostObject()
{
	ClientBeaconActorClass = AEasySessionQueryBeaconClient::StaticClass();
	BeaconTypeName = ClientBeaconActorClass->GetName();
}

bool AEasySessionQueryBeaconHostObject::ApproveJoin(const FString& Password, FString& OutReason) const
{
	if (!ExpectedPassword.IsEmpty() && ExpectedPassword != Password.TrimStartAndEnd())
	{
		OutReason = NSLOCTEXT("EasySession", "WrongPassword", "Wrong session password.").ToString();
		return false;
	}

	return true;
}
