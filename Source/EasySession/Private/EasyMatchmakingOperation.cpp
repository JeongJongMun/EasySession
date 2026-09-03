// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasyMatchmakingOperation.h"

#include "EasyMatchmakingPolicy.h"

FEasyMatchmakingOperation::FEasyMatchmakingOperation(UEasyMatchmakingPolicy& InPolicy)
	: Policy(&InPolicy)
{
}

void FEasyMatchmakingOperation::Cancel()
{
	// The policy completes as Canceled, now or once its running step answers, and the subsystem ends this operation from that completion.
	if (UEasyMatchmakingPolicy* Running = Policy.Get())
	{
		Running->Cancel();
	}
}

FString FEasyMatchmakingOperation::DescribeProgress() const
{
	const UEasyMatchmakingPolicy* Running = Policy.Get();
	if (Running == nullptr)
	{
		return TEXT("Matchmaking");
	}

	const UEnum* StateEnum = StaticEnum<EEasyMatchmakingState>();
	return FString::Printf(TEXT("Matchmaking (%s, %ds)"), *StateEnum->GetNameStringByValue(static_cast<int64>(Running->GetState())), Running->GetElapsedSeconds());
}
