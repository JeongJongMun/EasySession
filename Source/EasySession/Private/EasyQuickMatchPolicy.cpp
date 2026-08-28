// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasyQuickMatchPolicy.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"

float UEasyQuickMatchPolicy::ScoreSession_Implementation(const FEasySessionSearchResult& Session) const
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

	// Preferring fuller sessions scores a full one highest, and a full one always
	// rejects the join. Ranked last rather than dropped: the count is a search
	// snapshot, so it is worth one attempt before hosting a second session.
	const float NoRoomPenalty = (Session.MaxPlayers > 0 && Session.OpenSlots <= 0)
		? (PingBucketsMs.Num() + 1) * 1000.0f
		: 0.0f;

	return BucketScore + FillRatio * 100.0f - NoRoomPenalty;
}

void UEasyQuickMatchPolicy::Start(UEasySessionSubsystem& InSubsystem, const FEasyQuickMatchParams& InParams, FEasySessionCompleteDelegate InOnComplete)
{
	if (State != EEasyQuickMatchState::Idle)
	{
		InOnComplete.ExecuteIfBound(EEasySessionResult::QuickMatchAlreadyInProgress, TEXT("This quick match policy is already running."));
		return;
	}

	Subsystem = &InSubsystem;
	Params = InParams;
	OnComplete = MoveTemp(InOnComplete);
	PassesCompleted = 0;
	FailedSessionKeys.Empty();
	bCancelRequested = false;

	UE_LOG(LogEasySession, Log, TEXT("QuickMatch started (MaxPasses=%d, HostFallback=%d)"), Params.MaxSearchPasses, Params.bAllowHostFallback ? 1 : 0);

	SetState(EEasyQuickMatchState::Searching);
	RunSearchPass();
}

void UEasyQuickMatchPolicy::Cancel()
{
	if (State == EEasyQuickMatchState::Idle || State == EEasyQuickMatchState::Complete)
	{
		return;
	}

	bCancelRequested = true;

	// If we are only waiting for the next pass, finish right away.
	if (PassDelayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
		PassDelayTickerHandle.Reset();
		Complete(EEasySessionResult::Canceled, TEXT("Quick Match was canceled."));
	}
}

void UEasyQuickMatchPolicy::SetState(EEasyQuickMatchState NewState)
{
	if (State == NewState)
	{
		return;
	}

	State = NewState;
	OnStateChanged.Broadcast(NewState);
}

void UEasyQuickMatchPolicy::RunSearchPass()
{
	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	if (CompleteIfAlreadyInSession(*SubsystemPtr))
	{
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickMatch search pass %d/%d"), PassesCompleted + 1, Params.MaxSearchPasses);
	SubsystemPtr->FindEasySessions(Params.Search, FEasySessionFindCompleteDelegate::CreateUObject(this, &UEasyQuickMatchPolicy::HandleSearchComplete));
}

void UEasyQuickMatchPolicy::HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Quick Match was canceled."));
		return;
	}

	if (Result != EEasySessionResult::Success || Results.IsEmpty())
	{
		FinishPassAndContinue();
		return;
	}

	BuildCandidateListAndJoin(Results);
}

void UEasyQuickMatchPolicy::BuildCandidateListAndJoin(const TArray<FEasySessionSearchResult>& Results)
{
	Candidates.Empty();
	for (const FEasySessionSearchResult& Result : Results)
	{
		// QuickMatch cannot supply a password, so protected sessions are never candidates.
		if (Result.bPasswordProtected)
		{
			continue;
		}

		if (!FailedSessionKeys.Contains(GetSessionKey(Result)))
		{
			Candidates.Add(Result);
		}
	}

	if (Candidates.IsEmpty())
	{
		FinishPassAndContinue();
		return;
	}

	// Best candidates first.
	Candidates.StableSort([this](const FEasySessionSearchResult& A, const FEasySessionSearchResult& B)
	{
		return ScoreSession(A) > ScoreSession(B);
	});

	// Shuffle the best N so that concurrent searchers spread across equally good sessions.
	const int32 ShuffleCount = FMath::Min(TopCandidateRandomization, Candidates.Num());
	for (int32 Index = 0; Index < ShuffleCount - 1; ++Index)
	{
		const int32 SwapIndex = FMath::RandRange(Index, ShuffleCount - 1);
		if (SwapIndex != Index)
		{
			Candidates.Swap(Index, SwapIndex);
		}
	}

	NextCandidateIndex = 0;
	SetState(EEasyQuickMatchState::Joining);
	TryJoinNextCandidate();
}

void UEasyQuickMatchPolicy::TryJoinNextCandidate()
{
	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Quick Match was canceled."));
		return;
	}

	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	if (CompleteIfAlreadyInSession(*SubsystemPtr))
	{
		return;
	}

	if (!Candidates.IsValidIndex(NextCandidateIndex))
	{
		FinishPassAndContinue();
		return;
	}

	const FEasySessionSearchResult& Candidate = Candidates[NextCandidateIndex];
	UE_LOG(LogEasySession, Log, TEXT("QuickMatch joining candidate %d/%d ('%s')"), NextCandidateIndex + 1, Candidates.Num(), *Candidate.SessionDisplayName);
	SubsystemPtr->JoinEasySession(Candidate, FString(), FString(), FEasySessionCompleteDelegate::CreateUObject(this, &UEasyQuickMatchPolicy::HandleJoinComplete));
}

void UEasyQuickMatchPolicy::HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (bCancelRequested)
	{
		CompleteAsCanceled(Result);
		return;
	}

	if (Result == EEasySessionResult::Success)
	{
		Complete(EEasySessionResult::Success, FString());
		return;
	}

	// Not this candidate's refusal: a session appeared on this machine, so every remaining step would fail the same way.
	if (Result == EEasySessionResult::SessionAlreadyExists)
	{
		Complete(Result, ErrorMessage);
		return;
	}

	// Never retry a session that already rejected us in this run.
	if (Candidates.IsValidIndex(NextCandidateIndex))
	{
		FailedSessionKeys.Add(GetSessionKey(Candidates[NextCandidateIndex]));
	}

	++NextCandidateIndex;
	TryJoinNextCandidate();
}

void UEasyQuickMatchPolicy::FinishPassAndContinue()
{
	++PassesCompleted;

	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Quick Match was canceled."));
		return;
	}

	if (PassesCompleted >= Params.MaxSearchPasses)
	{
		if (Params.bAllowHostFallback)
		{
			HostFallbackSession();
		}
		else
		{
			Complete(EEasySessionResult::NoSessionsFound, TEXT("No joinable session was found."));
		}
		return;
	}

	SetState(EEasyQuickMatchState::Searching);

	if (Params.DelayBetweenPassesSeconds <= 0.0f)
	{
		RunSearchPass();
		return;
	}

	PassDelayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
	{
		PassDelayTickerHandle.Reset();
		RunSearchPass();
		return false;
	}), Params.DelayBetweenPassesSeconds);
}

void UEasyQuickMatchPolicy::HostFallbackSession()
{
	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	if (CompleteIfAlreadyInSession(*SubsystemPtr))
	{
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickMatch found no session - hosting our own."));
	SetState(EEasyQuickMatchState::Hosting);
	SubsystemPtr->CreateEasySession(MakeFallbackHostParams(), FEasySessionCompleteDelegate::CreateUObject(this, &UEasyQuickMatchPolicy::HandleHostComplete));
}

FEasySessionHostParams UEasyQuickMatchPolicy::MakeFallbackHostParams() const
{
	// The fallback room must be one this run's own search would find: on the searched network, with every required key advertised at the required value.
	FEasySessionHostParams FallbackParams = Params.Host;
	FallbackParams.bIsLANMatch = Params.Search.bLANQuery;
	FallbackParams.CustomSettings.Append(Params.Search.RequiredCustomSettings);
	return FallbackParams;
}

void UEasyQuickMatchPolicy::HandleHostComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (bCancelRequested)
	{
		CompleteAsCanceled(Result);
		return;
	}

	Complete(Result, ErrorMessage);
}

void UEasyQuickMatchPolicy::CompleteAsCanceled(EEasySessionResult StepResult)
{
	// The step finished after the cancel. If it succeeded, undo it, so Canceled always means "not in a match".
	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (StepResult == EEasySessionResult::Success && SubsystemPtr != nullptr)
	{
		// Still the same frame as the travel request, so the map has not started loading.
		SubsystemPtr->CancelPendingTravel();
		SubsystemPtr->DestroyEasySession();
	}

	Complete(EEasySessionResult::Canceled, TEXT("Quick Match was canceled."));
}

bool UEasyQuickMatchPolicy::CompleteIfAlreadyInSession(UEasySessionSubsystem& InSubsystem)
{
	// A failed join's leftover session is queued for destruction and gone before the next candidate runs - only a session that stays counts.
	if (!InSubsystem.IsInSession() || InSubsystem.IsSessionBeingDestroyed())
	{
		return false;
	}

	// An accepted invite or the game's own Create or Join got here first. Every remaining step would fail against it.
	Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Quick Match stopped."));
	return true;
}

void UEasyQuickMatchPolicy::Complete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (State == EEasyQuickMatchState::Complete)
	{
		return;
	}

	if (PassDelayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
		PassDelayTickerHandle.Reset();
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickMatch complete: %s"), *EasySession::ResultToString(Result));
	SetState(EEasyQuickMatchState::Complete);
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

FString UEasyQuickMatchPolicy::GetSessionKey(const FEasySessionSearchResult& Session)
{
	return Session.NativeResult.IsValid() ? Session.NativeResult.GetSessionIdStr() : Session.SessionDisplayName;
}
