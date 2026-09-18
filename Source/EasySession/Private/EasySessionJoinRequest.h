// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionJoinApprovalBeaconClient;
struct FEasyJoinApprovalResponse;

/**
 * Joins a session a search returned and travels the local player to the host.
 *
 * The request asks the host's join approval beacon first when the session advertises one.
 * A refusal then uses no session slot and starts no travel.
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
	virtual void Cleanup(bool bAbandoned) override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** Spawn the beacon client actor and request join approval from the host. */
	void RequestJoinApproval();

	/** The beacon's response: join the session, or complete with the reason for the refusal. */
	void HandleJoinApprovalResponse(const FEasyJoinApprovalResponse& Response);

	/** Destroy the beacon client actor, so a late response cannot reach a completed request. */
	void StopApprovalClient();

	/** Ask the online subsystem to join. Every join path ends in this step. */
	void JoinOnlineSession();

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
};
