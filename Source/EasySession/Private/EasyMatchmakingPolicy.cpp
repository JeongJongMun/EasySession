// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasyMatchmakingPolicy.h"

float UEasyMatchmakingPolicy::ScoreSession_Implementation(const FEasySessionSearchResult& Session) const
{
	// Lower ping buckets always win over higher ones.
	int32 BucketIndex = PingBucketsMs.Num();
	for (int32 Index = 0; Index < PingBucketsMs.Num(); ++Index)
	{
		if (Session.PingInMs <= PingBucketsMs[Index])
		{
			BucketIndex = Index;
			break;
		}
	}

	const float BucketScore = (PingBucketsMs.Num() - BucketIndex) * 1000.0f;

	// Within the same bucket, fuller sessions win so that matches start sooner.
	const float FillRatio = Session.MaxPlayers > 0
		? static_cast<float>(Session.MaxPlayers - Session.OpenSlots) / static_cast<float>(Session.MaxPlayers)
		: 0.0f;

	// Preferring fuller sessions would score a full one highest, and a full one always refuses the join.
	// It is ranked last rather than dropped, because the count is a search snapshot and worth one attempt before hosting a second session.
	const float NoRoomPenalty = (Session.MaxPlayers > 0 && Session.OpenSlots <= 0)
		? (PingBucketsMs.Num() + 1) * 1000.0f
		: 0.0f;

	return BucketScore + FillRatio * 100.0f - NoRoomPenalty;
}
