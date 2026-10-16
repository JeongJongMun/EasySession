// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionReservationBeaconClient;
enum class EEasyReservationResult : uint8;
struct FEasyReservationResponse;

/**
 * FEasySessionJoinRequest joins a session a search returned: a game session or a party.
 * Both kinds run the same steps: check what may be joined, let the online subsystem join the session, and resolve the address.
 * Then a game session travels the local player to the host, and a party logs the local player in on the leader's beacon.
 * A game session adds a step before the join, where the host's reservation beacon decides and the group is told to follow.
 *
 * The subsystem creates it for JoinSession with a result of either type, which an accepted invite also calls.
 * Matchmaking runs it as a sub-request to join each search result, and FEasySessionParty runs it to restore the party after a match.
 *
 * A game session first asks the host for a reservation over the reservation beacon, when the session advertises one.
 * A refusal then adds no reservation, starts no travel and keeps the session this player is in.
 * A player in another session destroys it with a Destroy sub-request once the join is approved, and a host tells its clients why first.
 * A join that fails after that destroy travels the player to the menu, because the session they were in no longer exists.
 * The beacon client actor exists for this request only, from the reservation request to its response or to Cleanup.
 * An unreachable beacon fails the join of a password-protected session, because PreLogin admits no player without a reservation there.
 * It also fails the join of a player in a session, who would otherwise leave it before any host approved the join.
 * The same happens for a party leader with a group, because only a reservation holds the group.
 * Any other join continues, and PreLogin runs ApproveJoin when the joining player arrives.
 * A party leader brings the other party members, and a host whose match has not started brings every other player of its session.
 * The reservation holds this group too, and the group is told to follow before JoinOnlineSession runs.
 *
 * A party needs no reservation and no map: the leader decides the join at the login on the party beacon, and nothing travels.
 * It completes with Success once the member list holds the local player, so Get Easy Party Members already lists them.
 * A refused, unreachable or timed out login destroys the party session again, so the player is in no party afterwards.
 */
class FEasySessionJoinRequest final : public FEasySessionRequest
{
	//~ FEasySessionTestAccess passes the reservation beacon's approval to this request for the tests.
	friend class FEasySessionTestAccess;

public:

	/**
	 * Join the game session InTarget and travel to its host.
	 *
	 * @param InPassword The password sent to the host's reservation beacon.
	 * @param InTravelOptions Extra options appended to the client travel URL.
	 */
	FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, const FString& InPassword, const FString& InTravelOptions, FEasySessionCompleteDelegate InOnComplete);

	/** Join the party InTarget and log the local player in on the leader's party beacon. */
	FEasySessionJoinRequest(const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete);

	/** Destroys the beacon client actor if the request is destroyed while it waits for the response. */
	virtual ~FEasySessionJoinRequest() override;

	/** @return The result a refused reservation completes the join with. Approved and Unreachable continue the join instead, so they have none. */
	static EEasySessionResult GetRefusalResult(EEasyReservationResult Refusal);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The public constructors delegate here, so every request names its kind of session. */
	FEasySessionJoinRequest(FName InSessionName, const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete);

	/** Whether the search result may be joined. Completes with the refusal and returns false when it may not. */
	bool CheckCanJoin();

	/** For a game session: refuse the session this player is in, and any join while this player hosts a match in progress. */
	bool CheckCanJoinGameSession(const IOnlineSessionPtr& Sessions);

	/** For a party: refuse a player in a party or a game session, and one with no logged in player the leader can name. */
	bool CheckCanJoinPartySession(const IOnlineSessionPtr& Sessions);

	/** For a game session: spawn the beacon client actor and ask the host for a reservation. */
	void RequestReservation();

	/** The reservation beacon's response: join on approval, join without a reservation when the beacon is unreachable, or complete with the refusal. */
	void HandleReservationResponse(const FEasyReservationResponse& Response);

	/** Destroy the beacon client actor, so a late response cannot reach a completed request. */
	void DestroyReservationClient();

	/** With a group: tell the group to follow, and join once GetGroupMembers lists none of them or the wait timed out. */
	void JoinOnlineSessionWithGroup();

	/** Check whether GetGroupMembers still lists a player of the group, and join when it lists none or the wait timed out. */
	bool HandleGroupWaitTick(float DeltaTime);

	/**
	 * Without a reachable beacon: join without a reservation.
	 * Completes with JoinRefused instead when the session is password-protected, this player has a group, or this player is in a session.
	 */
	void JoinWithoutReservation();

	/**
	 * Ask the online subsystem to join. Every join path ends here.
	 * A player in another game session destroys it first with a Destroy sub-request.
	 */
	void JoinOnlineSession();

	/** The Destroy sub-request for the game session this player was in completed. Joins on success. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The online subsystem finished joining a session, and the address is resolved next. Sessions with another name are ignored. */
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult);

	/** For a game session: travel to the host, and close the party this player led. */
	void TravelToHost(const FString& ConnectString);

	/** For a party: log in on the leader's party beacon, which decides the join within the login time limit. */
	void ConnectToLeader(const FString& ConnectString);

	/** The leader's party beacon admitted the local player, or did not. */
	void HandleConnectComplete(bool bSuccess, const FText& Reason);

	/** The leader did not complete the login in time. */
	bool HandleTimeout(float DeltaTime);

	/** Destroy the session the online subsystem joined again, then complete with this result. A party closes its beacon first. */
	void LeaveAndComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The session to join, as returned by a search. */
	FEasySessionSearchResult Target;

	/** The password sent to the host's reservation beacon. Read for a game session only. */
	FString Password;

	/** Extra options appended to the client travel URL. Read for a game session only. */
	FString TravelOptions;

	/**
	 * The group, read by GetGroupMembers in Execute: the party members on a party leader, the other players on a host.
	 * The reservation holds each of them too. Read for a game session only.
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

	/** Ticker that ends a party join when the leader does not complete the login in time. */
	FTSTicker::FDelegateHandle TimeoutHandle;

	/** When the wait for the group started, in FPlatformTime seconds. */
	double GroupWaitStartSeconds = 0.0;

	/** Has this player destroyed the game session they were in to join this one. */
	bool bLeftSession = false;
};
