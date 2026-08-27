// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "EasySessionTypes.h"
#include "EasyQuickMatchPolicy.generated.h"

class UEasySessionSubsystem;

/** Multicast event fired when the quick match state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasyQuickMatchStateEvent, EEasyQuickMatchState, NewState);

/**
 * Quick Match policy: search for sessions, join the best one, and optionally host a new session when nothing is found.
 *
 * The best session is chosen by ScoreSession.
 * The default implementation groups sessions into ping buckets and prefers fuller sessions within the same bucket.
 * Subclass this policy (in Blueprint or C++) and override ScoreSession to use your own criteria such as skill or map preference.
 */
UCLASS(Blueprintable, BlueprintType)
class EASYSESSION_API UEasyQuickMatchPolicy : public UObject
{
	GENERATED_BODY()

public:

	/** Fired whenever the quick match state changes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyQuickMatchStateEvent OnStateChanged;

	/**
	 * Ping thresholds (in milliseconds) used to group sessions into buckets.
	 * Sessions in a lower bucket always win over sessions in a higher bucket.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EasySession|Scoring")
	TArray<int32> PingBucketsMs = { 50, 100, 150 };

	/**
	 * The best N candidates are shuffled before join attempts, so that players searching at the same time do not all try the same session first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EasySession|Scoring", meta = (ClampMin = 1))
	int32 TopCandidateRandomization = 3;

	/**
	 * Score a session found by the search. Higher scores are joined first.
	 * The default implementation scores by ping bucket first, then by fill ratio, and ranks sessions with no open slot below every session that has room.
	 * Override this to use custom criteria such as skill or map preference.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "EasySession")
	float ScoreSession(const FEasySessionSearchResult& Session) const;
	virtual float ScoreSession_Implementation(const FEasySessionSearchResult& Session) const;

	/** Get the current quick match state. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	EEasyQuickMatchState GetState() const { return State; }

public:

	/** Start the quick match run. Called by the EasySessionSubsystem. */
	void Start(UEasySessionSubsystem& InSubsystem, const FEasyQuickMatchParams& InParams, FEasySessionCompleteDelegate InOnComplete);

	/** Cancel the quick match run. The run finishes with the Canceled result, and a join or host that succeeds after the cancel is undone. */
	void Cancel();

private:

	/** Move to a new state and notify listeners. */
	void SetState(EEasyQuickMatchState NewState);

	/** Run one search pass. */
	void RunSearchPass();

	/** Called when a search pass finished. */
	void HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** Sort candidates by score, shuffle the best N and try joining them in order. */
	void BuildCandidateListAndJoin(const TArray<FEasySessionSearchResult>& Results);

	/** Try to join the next candidate in the list. */
	void TryJoinNextCandidate();

	/** Called when a join attempt finished. */
	void HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Schedule the next search pass, or fall back to hosting / failure when out of passes. */
	void FinishPassAndContinue();

	/** Host our own session because no session could be joined. */
	void HostFallbackSession();

	/** Called when the host fallback finished. */
	void HandleHostComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Finish as Canceled. A step that succeeded after the cancel is undone: its pending travel is dropped and its session destroyed. */
	void CompleteAsCanceled(EEasySessionResult StepResult);

	/**
	 * Finish as SessionAlreadyExists when this player is already in a session - an accepted invite, or the game's own Create or Join, got there first.
	 *
	 * @return Whether the run finished.
	 */
	bool CompleteIfAlreadyInSession(UEasySessionSubsystem& InSubsystem);

	/** Finish the run and report the result. */
	void Complete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Build a stable identifier for a search result, used by the exclusion list. */
	static FString GetSessionKey(const FEasySessionSearchResult& Session);

private:

	/** The subsystem running this policy. */
	TWeakObjectPtr<UEasySessionSubsystem> Subsystem;

	/** Parameters of this run. */
	FEasyQuickMatchParams Params;

	/** Fired once when the run completes. */
	FEasySessionCompleteDelegate OnComplete;

	/** Current state of the run. */
	EEasyQuickMatchState State = EEasyQuickMatchState::Idle;

	/** Search passes executed so far. */
	int32 PassesCompleted = 0;

	/** Candidates of the current pass, best first. */
	TArray<FEasySessionSearchResult> Candidates;

	/** Index of the next candidate to try. */
	int32 NextCandidateIndex = 0;

	/** Sessions that failed to join during this run. Never retried. */
	TSet<FString> FailedSessionKeys;

	/** Whether Cancel was requested. Checked between async steps. */
	bool bCancelRequested = false;

	/** Ticker handle for the delay between search passes. */
	FTSTicker::FDelegateHandle PassDelayTickerHandle;
};
