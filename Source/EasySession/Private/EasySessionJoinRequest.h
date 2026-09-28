// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionReservationBeaconClient;
struct FEasyReservationResponse;

/**
 * FEasySessionJoinRequest joins a session a search returned and travels the local player to the host.
 *
 * The subsystem creates it for Join Easy Session and for an accepted invite.
 * Matchmaking runs it as a sub-request to join each candidate.
 *
 * The request first asks the host for a reservation over the reservation beacon, when the session advertises one.
 * A refusal then adds no reservation, starts no travel and keeps the session this player is in.
 *
 * A player in another session leaves it once the join is approved, with a Destroy sub-request, and a host tells its clients why first.
 * A join that fails after leaving travels the player to the menu, because the session they left is destroyed.
 * The beacon client actor exists for this request only, from the reservation request to its response or to Cleanup.
 * An unreachable beacon does not fail the join of an open session, because PreLogin runs ApproveJoin when the joining player arrives.
 * It fails the join of a password-protected session, because PreLogin admits no player without a reservation there.
 */
class FEasySessionJoinRequest final : public FEasySessionRequest
{
public:

	FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, const FString& InPassword, const FString& InTravelOptions, FEasySessionCompleteDelegate InOnComplete);

	/** Destroys the beacon client actor if the request is destroyed while it waits for the response. */
	virtual ~FEasySessionJoinRequest() override;

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** Spawn the beacon client actor and ask the host for a reservation. */
	void RequestReservation();

	/** The beacon's response: join the session, or complete with the reason for the refusal. */
	void HandleReservationResponse(const FEasyReservationResponse& Response);

	/** Destroy the beacon client actor, so a late response cannot reach a completed request. */
	void DestroyReservationClient();

	/** Join without a reservation, or complete with JoinRefused when the session is password-protected. */
	void JoinWithoutReservation();

	/** Ask the online subsystem to join. Every join path ends here. A player in another session leaves it first. */
	void JoinOnlineSession();

	/** Leaving the session this player was in completed. Joins on success. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The online subsystem finished joining a session. Sessions with another name are ignored. */
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult);

	/** The session to join, as returned by a search. */
	FEasySessionSearchResult Target;

	/** The password sent to the host's reservation beacon. */
	FString Password;

	/** Extra options appended to the client travel URL. */
	FString TravelOptions;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** The beacon client actor asking the host for a reservation. Only valid while the request waits for the response. */
	TWeakObjectPtr<AEasySessionReservationBeaconClient> ReservationClient;

	/** Handle for the online subsystem's join completion, bound while the request runs. */
	FDelegateHandle JoinCompleteHandle;

	/** Did this player leave a session to join this one. */
	bool bLeftSession = false;
};
