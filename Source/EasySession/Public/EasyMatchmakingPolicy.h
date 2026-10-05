// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EasySessionTypes.h"
#include "EasyMatchmakingPolicy.generated.h"

/**
 * UEasyMatchmakingPolicy decides which session a matchmaking run joins first.
 * The subsystem creates one policy object for each run, from the policy class passed to Start Easy Matchmaking.
 * The subsystem runs the search passes, the joins and the host fallback itself, and reports the progress on its On Matchmaking events.
 *
 * The best session is chosen by ScoreSession.
 * The default implementation groups sessions into ping buckets and prefers fuller sessions within the same bucket.
 * Subclass this policy (in Blueprint or C++) and override ScoreSession to use your own criteria such as skill or map preference.
 */
UCLASS(Blueprintable, BlueprintType)
class EASYSESSION_API UEasyMatchmakingPolicy : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Ping thresholds (in milliseconds) used to group sessions into buckets.
	 * Sessions in a lower bucket always win over sessions in a higher bucket.
	 * An empty array leaves one bucket for every session, so the fill ratio alone decides.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EasySession|Scoring")
	TArray<int32> PingBucketsMs = { 50, 100, 150 };

	/**
	 * How many of the best scoring sessions are shuffled before the join attempts start.
	 * Players searching at the same time then do not all try the same session first.
	 * 1 always tries the best scoring session first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EasySession|Scoring", meta = (ClampMin = 1))
	int32 TopCandidatesToShuffle = 3;

	/**
	 * Score a session found by the search.
	 * Higher scores are joined first.
	 * The default implementation scores by ping bucket first, then by fill ratio, and ranks sessions with no open slot below every session with one.
	 * Override this to use custom criteria such as skill or map preference.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "EasySession")
	float ScoreSession(const FEasySessionSearchResult& Session) const;
	virtual float ScoreSession_Implementation(const FEasySessionSearchResult& Session) const;
};
