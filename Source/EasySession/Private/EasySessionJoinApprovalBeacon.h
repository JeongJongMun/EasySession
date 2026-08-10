// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "OnlineBeaconClient.h"
#include "OnlineBeaconHostObject.h"
#include "EasySessionJoinApprovalBeacon.generated.h"

/** Answer to a pre-travel approval request, delivered before any map load happens. */
DECLARE_DELEGATE_TwoParams(FEasyJoinApprovalResponse, bool /*bApproved*/, const FString& /*Reason*/);

/**
 * Asks the host "may this player join?" over a beacon, before any travel starts.
 *
 * The game connection cannot answer that question early: it only exists once the
 * client is already traveling, which is why a PreLogin rejection used to arrive
 * seconds after the Join node reported success. A beacon is a second, lightweight
 * connection that exists exactly for pre-travel questions - the same mechanism a
 * party seat reservation will use later.
 *
 * A deliberately minimal beacon rather than the engine's APartyBeaconClient: that
 * class carries reservation lists, party members and timeouts shaped for a
 * matchmaking backend, and this exchange is a single question and answer.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconClient : public AOnlineBeaconClient
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconClient();

	/**
	 * Connect to the host beacon and ask to join once connected.
	 * The answer (or a connection failure) arrives through OnResponse exactly once.
	 */
	bool RequestApproval(const FString& Address, int32 Port, const FString& Password);

	FEasyJoinApprovalResponse OnResponse;

	UFUNCTION(Server, Reliable)
	void ServerRequestJoin(const FString& Password);

	UFUNCTION(Client, Reliable)
	void ClientReceiveResponse(bool bApproved, const FString& Reason);

	//~ Begin AOnlineBeaconClient Interface
	virtual void OnConnected() override;
	virtual void OnFailure() override;
	//~ End AOnlineBeaconClient Interface

private:

	/** Held between RequestApproval and OnConnected, then sent to the host. */
	FString PendingPassword;

	/** The response fires exactly once - a failure after an answer stays silent. */
	bool bResponded = false;
};

/**
 * Host side of the approval request. Owns the policy: whether a given request may join
 * the session this host created.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconHostObject : public AOnlineBeaconHostObject
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconHostObject();

	/**
	 * Decide whether a join request is allowed. Returns the denial reason through
	 * OutReason when it is not.
	 *
	 * SPIKE: compares against ExpectedPassword. The real version asks the
	 * subsystem's server gate, which already owns the session credentials and the
	 * join-in-progress policy.
	 */
	bool ApproveJoin(const FString& Password, FString& OutReason) const;

	/** SPIKE: fixed credential for the round-trip test. Empty approves everyone. */
	FString ExpectedPassword;
};
