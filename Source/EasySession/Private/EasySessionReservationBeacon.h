// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "GameFramework/OnlineReplStructs.h"
#include "PartyBeaconClient.h"
#include "PartyBeaconHost.h"
#include "EasySessionReservationBeacon.generated.h"

/** Result of a reservation request. */
UENUM()
enum class EEasyReservationResult : uint8
{
	/** The player may join. */
	Approved,

	/** The session password did not match. An empty one never does. */
	WrongPassword,

	/** The session has no reservation left for another player. */
	SessionFull,

	/** Refused for another reason. The reason text says which. */
	Refused,

	/** The joining player could not reach the host. The host never sends this, and the beacon client sets it itself. */
	Unreachable
};

/** The host's response to a reservation request, sent before any travel starts. */
USTRUCT()
struct FEasyReservationResponse
{
	GENERATED_BODY()

	/** Whether the join was approved, and if not, why. Starts as Unreachable so a response that never arrives reads correctly. */
	UPROPERTY()
	EEasyReservationResult Result = EEasyReservationResult::Unreachable;

	/** Shown to the player when the join is refused. */
	UPROPERTY()
	FString ReasonText;

	/** The response when the host could not be reached. */
	static FEasyReservationResponse Unreachable();

	/** The response of a host that has nothing bound to decide the join. */
	static FEasyReservationResponse NotAnswering();
};

/** Fires exactly once per RequestJoin, with Unreachable when the host never responded. */
DECLARE_DELEGATE_OneParam(FEasyReservationRequestComplete, const FEasyReservationResponse&);

/** Delegate the beacon host calls to decide whether a player may join. */
DECLARE_DELEGATE_RetVal_TwoParams(FEasyReservationResponse, FEasyApproveJoinDelegate, const FString& /** Password */, const FUniqueNetIdRepl& /** Requester */);

/** The reservations the beacon client and FEasySessionReservations both build. */
namespace EasySessionReservation
{
	/**
	 * Make the reservation for one player.
	 * It carries the local platform, because the parent refuses a member without one and no setting turns that check off.
	 */
	FPlayerReservation MakeReservation(const FUniqueNetIdRepl& PlayerId);

	/**
	 * Make the reservations for a leader and the group that travels with them, the leader first.
	 * Invalid ids and a second entry for the same player are left out, because the parent refuses a reservation that holds either.
	 */
	TArray<FPlayerReservation> MakeReservations(const FUniqueNetIdRepl& LeaderId, const TArray<FUniqueNetIdRepl>& GroupMembers);
}

/**
 * Asks the host "may this player join?" over a beacon, before any travel starts.
 *
 * The game connection cannot ask it early enough, because it only exists once the client is already traveling.
 * A beacon is a second, lightweight connection to the host that exists before the travel.
 *
 * It derives from the engine's APartyBeaconClient, which requests the reservation, and adds the question whether the player may join.
 * A beacon connection carries one beacon type, so the two on separate beacons would take two connections to the same host.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionReservationBeaconClient : public APartyBeaconClient
{
	GENERATED_BODY()

public:

	AEasySessionReservationBeaconClient();

	/**
	 * Resolve Target's beacon address, connect, and ask to join.
	 * OnComplete fires exactly once, with Unreachable when the address does not resolve, the connection fails, or the host never responds.
	 * A failure inside this call is reported the same way, so the caller only has one path to handle.
	 *
	 * @param GroupMembers The players who travel with the local player, without the local player. One reservation holds them all, or none of them.
	 */
	bool RequestJoin(const FEasySessionSearchResult& Target, const FString& Password, const TArray<FUniqueNetIdRepl>& GroupMembers, const FEasyReservationRequestComplete& OnComplete);

	/**
	 * Sends the password ahead of the reservation request the parent sends on the same connection.
	 * FPlayerReservation carries a string for this, but the engine prints it to the log, so the password travels on its own reliable RPC instead.
	 * Reliable RPCs on this actor arrive in order, so the host has the password before it decides the reservation request.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSendPassword(const FString& Password);

	/** Sends the joining player a refusal the parent's results have no value for, such as a wrong password. */
	UFUNCTION(Client, Reliable)
	void ClientReceiveRefusal(EEasyReservationResult Result, const FString& Reason);

	/** @return The password that arrived on this connection. Read on the host, before the join is decided. */
	const FString& GetReceivedPassword() const { return ReceivedPassword; }

	/**
	 * Turn one of the parent's reservation results into this plugin's response.
	 * A duplicate is the parent finding a reservation the player already holds, so it reads as approved here.
	 */
	static FEasyReservationResponse MakeResponseFromReservationResult(EPartyReservationResult::Type Result);

	//~ Begin APartyBeaconClient Interface
	virtual void OnConnected() override;
	virtual void OnFailure() override;
	virtual void DestroyBeacon() override;
	//~ End APartyBeaconClient Interface

private:

	/** Complete the request with the response for the parent's result. */
	void HandleReservationComplete(EPartyReservationResult::Type Result);

	/** The request ends as Unreachable, whether the beacon connection never opened or the host never responded. */
	void HandleRequestTimeout();

	/** Complete the request with this response. Only the first call completes it. */
	void Signal(const FEasyReservationResponse& Response);

	/** Complete the request as Unreachable through Signal, logging why. */
	void SignalUnreachable(const TCHAR* LogWhy);

	/** Held between RequestJoin and OnConnected, then sent to the host. Empty on the host's copy of this actor. */
	FString PasswordToSend;

	/** What the joining player sent, on the host's copy of this actor. Empty on the joining player's own copy. */
	FString ReceivedPassword;

	/** The caller's callback. Cleared as it is executed, so it can only run once. */
	FEasyReservationRequestComplete CompleteDelegate;

	/** Completes the request as Unreachable after RequestTimeoutSeconds. */
	FTimerHandle RequestTimeoutHandle;

	/** Has the request completed. Later failures are ignored once it has. */
	bool bCompleted = false;
};

/**
 * AEasySessionReservationBeaconHost is the host side of the reservation beacon, and holds the reservations of the session.
 * It adds a reservation only for a player OnApproveJoin approves.
 * FEasySessionReservations binds that delegate, and its PreLogin lets in the players this beacon holds a reservation for.
 *
 * The parent APartyBeaconHost keeps the reservations, and removes one when its player never arrives.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class AEasySessionReservationBeaconHost : public APartyBeaconHost
{
	GENERATED_BODY()

public:

	AEasySessionReservationBeaconHost();

	/** @return The delegate this actor asks whether a player may join. Every join is refused while nothing is bound. */
	FEasyApproveJoinDelegate& OnApproveJoin() { return ApproveJoinDelegate; }

	/**
	 * Wait for every player holding a reservation to arrive again, because a map change makes them all travel.
	 * Each wait starts over on the parent's TravelSessionTimeoutSecs, rather than on the shorter SessionTimeoutSecs meant for a player who left the session.
	 */
	void WaitForEveryoneToArrive();

	/** Remove the reservation of a player who logged out. */
	void RemovePlayerReservation(const FUniqueNetIdRepl& PlayerId);

	//~ Begin APartyBeaconHost Interface
	virtual void ProcessReservationRequest(APartyBeaconClient* Client, const FString& SessionId, const FPartyReservation& ReservationRequest) override;
	//~ End APartyBeaconHost Interface

private:

	/** Decides every join this actor is asked about. */
	FEasyApproveJoinDelegate ApproveJoinDelegate;
};
