// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionJoinApprovalBeaconClient;
struct FEasyJoinApprovalResponse;

/**
 * FEasySessionJoinRequest joins a session a search returned and travels the local player to the host.
 *
 * The subsystem creates it for Join Easy Session and for an accepted invite.
 * Matchmaking runs it as a sub-request to join each candidate.
 *
 * The request asks the host's join approval beacon first when the session advertises one.
 * A refusal then uses no player slot, starts no travel and keeps the session this player is in.
 *
 * A player in another session leaves it after the approval, with a Destroy sub-request, and a host tells its clients why first.
 * A join that fails after leaving travels the player to the menu, because the session they left is destroyed.
 * The beacon client actor exists for this request only, from the join approval request to its response or to Cleanup.
 * An unreachable beacon does not fail the join, because the server gate runs the same check when the joining player arrives.
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

	/** Spawn the beacon client actor and request join approval from the host. */
	void RequestJoinApproval();

	/** The beacon's response: join the session, or complete with the reason for the refusal. */
	void HandleJoinApprovalResponse(const FEasyJoinApprovalResponse& Response);

	/** Destroy the beacon client actor, so a late response cannot reach a completed request. */
	void DestroyApprovalClient();

	/** Ask the online subsystem to join. Every join path ends here. A player in another session leaves it first. */
	void JoinOnlineSession();

	/** Leaving the session this player was in completed. Joins on success. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The online subsystem finished joining a session. Sessions with another name are ignored. */
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult);

	/** The session to join, as returned by a search. */
	FEasySessionSearchResult Target;

	/** The password sent to the host's join approval beacon, and carried in the travel URL for the server gate. */
	FString Password;

	/** Extra options appended to the client travel URL. */
	FString TravelOptions;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** The beacon client actor asking for join approval. Only valid while the request waits for the response. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconClient> ApprovalClient;

	/** Handle for the online subsystem's join completion, bound while the request runs. */
	FDelegateHandle JoinCompleteHandle;

	/** Did this player leave a session to join this one. */
	bool bLeftSession = false;
};
