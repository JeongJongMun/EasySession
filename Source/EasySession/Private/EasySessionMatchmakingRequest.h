// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"
#include "UObject/StrongObjectPtr.h"

class UEasyMatchmakingPolicy;

/**
 * FEasySessionMatchmakingRequest is responsible for one matchmaking run.
 * The run searches for sessions, joins the best one, and hosts a session of its own when no search pass found one to join.
 * The subsystem creates it for StartMatchmaking, and FollowHost creates one with MakeFollow.
 * The queue runs it like any other request.
 *
 * The searches, the joins and the host are its sub-requests, so no other request runs between two of them.
 * A host whose match has not started may run one from its session: each join takes the session's players along, and no session is hosted.
 * UEasyMatchmakingPolicy decides which session is joined first.
 * The run broadcasts its progress on the subsystem's OnMatchmakingStateChanged, OnMatchmakingUpdated and OnMatchmakingComplete events.
 *
 * A cancel during a search ends the run inside the Cancel call.
 * A join or a host that is running finishes first, and a success is undone, so after Canceled this player is in no session the run joined or hosted.
 *
 * @see UEasyMatchmakingPolicy
 */
class FEasySessionMatchmakingRequest final : public FEasySessionRequest
{
	//~ FEasySessionTestAccess feeds crafted search results into the run and reads what it decided.
	friend class FEasySessionTestAccess;

public:

	FEasySessionMatchmakingRequest(const FEasyMatchmakingParams& InParams, UEasyMatchmakingPolicy& InPolicy, FEasySessionCompleteDelegate InOnComplete);

	/** Removes the tickers of the run. */
	virtual ~FEasySessionMatchmakingRequest() override;

	/**
	 * Make a run that follows a host who already holds a reservation for this player.
	 * It searches for that host's session only, may start while this player is in a session, and never hosts one of its own.
	 * Password-protected sessions stay candidates, because the reservation lets this player in without the password.
	 */
	static TSharedRef<FEasySessionMatchmakingRequest> MakeFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery, UEasyMatchmakingPolicy& InPolicy, FEasySessionCompleteDelegate InOnComplete);

	/** @return The request as a matchmaking request when its type says it is one. Null otherwise. */
	static TSharedPtr<FEasySessionMatchmakingRequest> Cast(const TSharedPtr<FEasySessionRequest>& Request);

	/** @return The current state of the run. */
	EEasyMatchmakingState GetState() const { return State; }

	/** @return Whole seconds since the run started. It stops counting when the run completes. */
	int32 GetElapsedWholeSeconds() const;

	/** @return The policy that scores the sessions of this run. */
	UEasyMatchmakingPolicy* GetPolicy() const { return Policy.Get(); }

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	virtual void HandleCancel() override;
	virtual FString GetProgressText() const override;
	//~ End FEasySessionRequest interface

private:

	/** Run one search pass as a sub-request. */
	void StartSearchPass();

	/** A search pass completed. Joins the best search result, or finishes the pass when there is none. */
	void HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** Fill Candidates with the joinable results, best score first, and shuffle the best few. */
	void BuildCandidates(const TArray<FEasySessionSearchResult>& Results);

	/** Join the next search result as a sub-request, or finish the pass when every one was tried. */
	void JoinNextCandidate();

	/** A join completed. Completes the run, or moves on to the next search result. */
	void HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/**
	 * Start the next search pass, or host or complete the run when no pass is left.
	 *
	 * @param SearchResult The result of this pass's search. When no pass is left and no fallback session is hosted, a failure ends the run with it.
	 * @param SearchError The message that came with SearchResult.
	 */
	void FinishSearchPass(EEasySessionResult SearchResult = EEasySessionResult::Success, const FString& SearchError = FString());

	/** The delay between two search passes is over. */
	bool HandlePassDelayElapsed(float DeltaTime);

	/** Host a session as a sub-request, because no session could be joined. */
	void HostFallbackSession();

	/** @return The run's host params with the search's LAN flag, region and required custom settings copied in, and the password and hidden flag cleared. */
	FEasySessionHostParams MakeFallbackHostParams() const;

	/** The fallback host completed. */
	void HandleHostComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/**
	 * Complete the run as Canceled after the join or host sub-request that ran during the cancel.
	 * A sub-request that succeeded is undone first: its travel is canceled and its session is destroyed.
	 */
	void CompleteAsCanceled(EEasySessionResult SubRequestResult);

	/** Move to a new state and broadcast OnMatchmakingStateChanged and OnMatchmakingUpdated. */
	void SetState(EEasyMatchmakingState NewState);

	/** Broadcast OnMatchmakingUpdated once a second, so elapsed time UI needs no timer of its own. */
	bool BroadcastUpdate(float DeltaTime);

	/** Remove the pass delay and update tickers. */
	void StopTickers();

	/** @return A stable identifier for a search result, used by the failed session list. */
	static FString GetSessionKey(const FEasySessionSearchResult& Session);

	/** Parameters of this run. */
	FEasyMatchmakingParams Params;

	/** Scores the sessions a search pass found. */
	TStrongObjectPtr<UEasyMatchmakingPolicy> Policy;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Current state of the run. Canceling from the requester's cancel until the run completes. */
	EEasyMatchmakingState State = EEasyMatchmakingState::Idle;

	/** Search passes completed so far. */
	int32 PassesCompleted = 0;

	/** Candidates of the current pass, best first. */
	TArray<FEasySessionSearchResult> Candidates;

	/** Index of the search result the running join sub-request tries. */
	int32 NextCandidateIndex = 0;

	/** Sessions whose join failed during this run, for any reason. They are never tried again. */
	TSet<FString> FailedSessionKeys;

	/** Ticker handle for the delay between search passes. */
	FTSTicker::FDelegateHandle PassDelayTickerHandle;

	/** Ticker handle for the once-a-second OnMatchmakingUpdated broadcast. */
	FTSTicker::FDelegateHandle UpdateTickerHandle;

	/** When the run started, in FPlatformTime seconds. Zero before the start. */
	double RunStartTimeSeconds = 0.0;

	/** When the run completed, in FPlatformTime seconds. Elapsed time stops here. */
	double RunEndTimeSeconds = 0.0;

	/** Is this run a follow of a host who holds a reservation for this player. */
	bool bFollowsHost = false;
};
