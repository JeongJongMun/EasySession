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

	UPROPERTY()
	EEasyJoinApprovalResult Result = EEasyJoinApprovalResult::Unreachable;

	/** Shown to the player when the join is refused. */
	UPROPERTY()
	FString ReasonText;

	/** Unused in v1.0. Becomes the proof of a seat hold once reservations arrive. */
	UPROPERTY()
	FString Token;
};

/** Fires exactly once per RequestApproval, with Unreachable when the host never answered. */
DECLARE_DELEGATE_OneParam(FEasyJoinApprovalComplete, const FEasyJoinApprovalResponse&);

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
	 * Resolve Target's beacon address, connect, and ask to join. The answer arrives
	 * through OnComplete exactly once - as Unreachable when the address does not
	 * resolve, the connection fails, or the host never answers. Failures inside this
	 * call are reported the same way, so the caller only handles one path.
	 */
	bool RequestApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	UFUNCTION(Server, Reliable)
	void ServerRequestJoinApproval(const FEasyJoinApprovalRequest& Request);

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

	FEasyJoinApprovalComplete CompleteDelegate;

	FTimerHandle ResponseTimeoutHandle;

	bool bCompleted = false;
};

/**
 * Host side of the approval request. This actor only carries the question over
 * the beacon - the decision belongs to FEasySessionServerGate, the same object
 * PreLogin asks, so the two answers can never disagree.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionJoinApprovalBeaconHostObject : public AOnlineBeaconHostObject
{
	GENERATED_BODY()

public:

	AEasySessionJoinApprovalBeaconHostObject();

	/**
	 * Ask the ServerGate whether this player may join. Returns the denial reason
	 * through OutReason. Refuses when there is no subsystem to ask.
	 */
	EEasyJoinApprovalResult ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& Password, FString& OutReason) const;
};
