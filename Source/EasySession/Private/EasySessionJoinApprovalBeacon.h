// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionServerGate.h"
#include "EasySessionTypes.h"
#include "GameFramework/OnlineReplStructs.h"
#include "OnlineBeaconClient.h"
#include "OnlineBeaconHostObject.h"
#include "EasySessionJoinApprovalBeacon.generated.h"

/** What a joiner asks the host before traveling. */
USTRUCT()
struct FEasyJoinApprovalRequest
{
	GENERATED_BODY()

	/** The players asking to join. One entry in v1.0 - the local player. Sized for a party later. */
	UPROPERTY()
	TArray<FUniqueNetIdRepl> PartyMembers;

	/** The password the joiner supplies. Empty for open sessions. */
	UPROPERTY()
	FString Credential;
};

/** The host's answer, delivered before any map load happens. */
USTRUCT()
struct FEasyJoinApprovalResponse
{
	GENERATED_BODY()

	/** Whether the join was approved, and if not, why. Starts as Unreachable so an answer that never arrives reads correctly. */
	UPROPERTY()
	EEasyJoinApprovalResult Result = EEasyJoinApprovalResult::Unreachable;

	/** Shown to the player when the join is refused. */
	UPROPERTY()
	FString ReasonText;

	/** Unused in v1.0. Will identify a reserved player slot once reservations are added. */
	UPROPERTY()
	FString Token;
};

/** Fires exactly once per RequestApproval, with Unreachable when the host never answered. */
DECLARE_DELEGATE_OneParam(FEasyJoinApprovalComplete, const FEasyJoinApprovalResponse&);

/**
 * Asks the host "may this player join?" over a beacon, before any travel starts.
 *
 * The game connection cannot answer that question early enough, because it only exists once the client is already traveling.
 * That is why a PreLogin rejection used to reach the player seconds after the Join node had already reported success.
 * A beacon is a second, lightweight connection made for exactly this kind of pre-travel exchange.
 * It is also the mechanism a party seat reservation will use later.
 *
 * This is a minimal beacon rather than the engine's APartyBeaconClient.
 * That class carries reservation lists, party members and timeouts shaped for a matchmaking backend, while this exchange is one question and one answer.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconClient : public AOnlineBeaconClient
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconClient();

	/**
	 * Resolve Target's beacon address, connect, and ask to join.
	 * The answer arrives through OnComplete exactly once, as Unreachable when the address does not resolve, the connection fails, or the host never answers.
	 * A failure inside this call is reported the same way, so the caller only has one path to handle.
	 */
	bool RequestApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	/** Sends the request to the host once the beacon connects. */
	UFUNCTION(Server, Reliable)
	void ServerRequestJoinApproval(const FEasyJoinApprovalRequest& Request);

	/** Delivers the host's answer back to the joiner. */
	UFUNCTION(Client, Reliable)
	void ClientReceiveJoinApproval(const FEasyJoinApprovalResponse& Response);

	//~ Begin AOnlineBeaconClient Interface
	virtual void OnConnected() override;
	virtual void OnFailure() override;
	virtual void DestroyBeacon() override;
	//~ End AOnlineBeaconClient Interface

private:

	/** The engine's timeout covers connecting; this one covers a host that never answers. */
	void HandleResponseTimeout();

	/** Deliver the answer once. Later signals (a failure after the response) stay silent. */
	void Signal(const FEasyJoinApprovalResponse& Response);

	/** Deliver Unreachable once, logging why. */
	void SignalUnreachable(const TCHAR* LogWhy);

	/** Held between RequestApproval and OnConnected, then sent to the host. */
	FEasyJoinApprovalRequest PendingRequest;

	/** The caller's callback. Cleared as it is executed, so it can only run once. */
	FEasyJoinApprovalComplete CompleteDelegate;

	/** Timer for a host that connected but never answered. */
	FTimerHandle ResponseTimeoutHandle;

	/** Whether an answer has already been delivered. Later failures are then ignored. */
	bool bCompleted = false;
};

/**
 * Host side of the approval request.
 * This actor only carries the question over the beacon; the decision belongs to FEasySessionServerGate.
 * PreLogin asks that same object, so the beacon's answer and the one a joining client gets on arrival can never disagree.
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
