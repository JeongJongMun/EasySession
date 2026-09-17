// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionServerGate.h"
#include "EasySessionTypes.h"
#include "GameFramework/OnlineReplStructs.h"
#include "OnlineBeaconClient.h"
#include "OnlineBeaconHostObject.h"
#include "EasySessionJoinApprovalBeacon.generated.h"

/** What a joining player asks the host before traveling. */
USTRUCT()
struct FEasyJoinApprovalRequest
{
	GENERATED_BODY()

	/** The players asking to join. One entry today, the local player. An array so a party fits later. */
	UPROPERTY()
	TArray<FUniqueNetIdRepl> PartyMembers;

	/** The password the joining player supplies. Empty for open sessions. */
	UPROPERTY()
	FString Credential;
};

/** The host's response, delivered before any travel starts. */
USTRUCT()
struct FEasyJoinApprovalResponse
{
	GENERATED_BODY()

	/** Whether the join was approved, and if not, why. Starts as Unreachable so a response that never arrives reads correctly. */
	UPROPERTY()
	EEasyJoinApprovalResult Result = EEasyJoinApprovalResult::Unreachable;

	/** Shown to the player when the join is refused. */
	UPROPERTY()
	FString ReasonText;

	/** Unused today. It will identify a reserved player slot once reservations are added. */
	UPROPERTY()
	FString Token;
};

/** Fires exactly once per RequestApproval, with Unreachable when the host never responded. */
DECLARE_DELEGATE_OneParam(FEasyJoinApprovalComplete, const FEasyJoinApprovalResponse&);

/**
 * Asks the host "may this player join?" over a beacon, before any travel starts.
 *
 * The game connection cannot ask it early enough, because it only exists once the client is already traveling.
 * Without the beacon, a PreLogin refusal reached the player seconds after the Join node had already reported success.
 * A beacon is a second, lightweight connection made for exactly this kind of pre-travel exchange.
 * It is also the mechanism a party seat reservation will use later.
 *
 * This is a minimal beacon rather than the engine's APartyBeaconClient.
 * That class carries reservation lists, party members and timeouts built for a matchmaking backend, while this exchange is one request and one response.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconClient : public AOnlineBeaconClient
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconClient();

	/**
	 * Resolve Target's beacon address, connect, and ask to join.
	 * OnComplete fires exactly once, with Unreachable when the address does not resolve, the connection fails, or the host never responds.
	 * A failure inside this call is reported the same way, so the caller only has one path to handle.
	 */
	bool RequestApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	/** Sends the request to the host once the beacon connects. */
	UFUNCTION(Server, Reliable)
	void ServerRequestJoinApproval(const FEasyJoinApprovalRequest& Request);

	/** Delivers the host's response to the joining player. */
	UFUNCTION(Client, Reliable)
	void ClientReceiveJoinApproval(const FEasyJoinApprovalResponse& Response);

	//~ Begin AOnlineBeaconClient Interface
	virtual void OnConnected() override;
	virtual void OnFailure() override;
	virtual void DestroyBeacon() override;
	//~ End AOnlineBeaconClient Interface

private:

	/** The engine's timeout covers connecting. This one covers a host that never responds. */
	void HandleResponseTimeout();

	/** Deliver the response once. A later failure after the response is ignored. */
	void Signal(const FEasyJoinApprovalResponse& Response);

	/** Deliver Unreachable once, logging why. */
	void SignalUnreachable(const TCHAR* LogWhy);

	/** Held between RequestApproval and OnConnected, then sent to the host. */
	FEasyJoinApprovalRequest PendingRequest;

	/** The caller's callback. Cleared as it is executed, so it can only run once. */
	FEasyJoinApprovalComplete CompleteDelegate;

	/** Timer for a host that connected but never responded. */
	FTimerHandle ResponseTimeoutHandle;

	/** Whether a response has already been delivered. Later failures are then ignored. */
	bool bCompleted = false;
};

/**
 * Host side of the approval request.
 * This actor only carries the request over the beacon. The decision belongs to FEasySessionServerGate.
 * PreLogin asks that same object, so the beacon's response and the one a joining client gets on arrival can never disagree.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconHostObject : public AOnlineBeaconHostObject
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconHostObject();

	/**
	 * Ask FEasySessionServerGate whether this player may join.
	 * Refuses when there is no subsystem to ask.
	 *
	 * @param OutReason Set to the message shown to the refused player.
	 */
	EEasyJoinApprovalResult ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& Password, FString& OutReason) const;
};
