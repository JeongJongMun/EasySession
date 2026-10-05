// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionReservationBeaconClient;
struct FEasyReservationResponse;

/**
 * FEasySessionJoinRequest joins a session a search returned and travels the local player to the host.
 *
 * The subsystem creates it for JoinSession, which an accepted invite also calls.
 * Matchmaking runs it as a sub-request to join each search result.
 *
 * The request first asks the host for a reservation over the reservation beacon, when the session advertises one.
 * A refusal then adds no reservation, starts no travel and keeps the session this player is in.
 *
 * A player in another session destroys it with a Destroy sub-request once the join is approved, and a host tells its clients why first.
 * A join that fails after that destroy travels the player to the menu, because the session they were in no longer exists.
 * The beacon client actor exists for this request only, from the reservation request to its response or to Cleanup.
 * An unreachable beacon fails the join of a password-protected session, because PreLogin admits no player without a reservation there.
 * It also fails the join of a player in a session, who would otherwise leave it before any host approved the join.
 * The same happens for a party leader with a group, because only a reservation holds the group.
 * Any other join continues, and PreLogin runs ApproveJoin when the joining player arrives.
 *
 * A party leader brings the other party members, and a host whose match has not started brings every other player of its session.
 * The reservation holds this group too, and the group is told to follow before JoinOnlineSession runs.
 */
class FEasySessionJoinRequest final : public FEasySessionRequest
{
	//~ FEasySessionTestAccess passes the reservation beacon's approval to this request for the tests.
	friend class FEasySessionTestAccess;

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

	/** Handle the beacon's response: join on approval, join without a reservation when the beacon is unreachable, or complete with the refusal reason. */
	void HandleReservationResponse(const FEasyReservationResponse& Response);

	/** Destroy the beacon client actor, so a late response cannot reach a completed request. */
	void DestroyReservationClient();

	/** Tell the group to follow, and join once GetGroupMembers lists none of them or the wait timed out. */
	void JoinOnlineSessionWithGroup();

	/** Check whether GetGroupMembers still lists a player of the group, and join when it lists none or the wait timed out. */
	bool HandleGroupWaitTick(float DeltaTime);

	/** Join without a reservation, or complete with JoinRefused when the session is password-protected, this player has a group, or this player is in a session. */
	void JoinWithoutReservation();

	/** Ask the online subsystem to join. Every join path ends here. A player in another session destroys it first with a Destroy sub-request. */
	void JoinOnlineSession();

	/** The Destroy sub-request for the session this player was in completed. Joins on success. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The online subsystem finished joining a session. Sessions with another name are ignored. */
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult);

	/** The session to join, as returned by a search. */
	FEasySessionSearchResult Target;

	/** The password sent to the host's reservation beacon. */
	FString Password;

	/** Extra options appended to the client travel URL. */
	FString TravelOptions;

	/**
	 * The group, read by GetGroupMembers in Execute: the party members on a party leader, the other players on a host.
	 * The reservation holds each of them too.
	 */
	TArray<FUniqueNetIdRepl> GroupMembers;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** The beacon client actor asking the host for a reservation. Only valid while the request waits for the response. */
	TWeakObjectPtr<AEasySessionReservationBeaconClient> ReservationClient;

	/** Handle for the online subsystem's join completion, bound while the request runs. */
	FDelegateHandle JoinCompleteHandle;

	/** Ticker handle for the wait until the group left this session. */
	FTSTicker::FDelegateHandle GroupWaitHandle;

	/** When the wait for the group started, in FPlatformTime seconds. */
	double GroupWaitStartSeconds = 0.0;

	/** Has this player destroyed the session they were in to join this one. */
	bool bLeftSession = false;
};
