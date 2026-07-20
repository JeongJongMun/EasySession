// Copyright Langerak. All Rights Reserved.

#include "EasyMatchmakingPolicy.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"

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

	// Within the same bucket, fuller sessions win so that matches start sooner.
	const float FillRatio = Session.MaxPlayers > 0
		? static_cast<float>(Session.MaxPlayers - Session.OpenSlots) / static_cast<float>(Session.MaxPlayers)
		: 0.0f;

	return (PingBucketsMs.Num() - BucketIndex) * 1000.0f + FillRatio * 100.0f;
}

void UEasyMatchmakingPolicy::Start(UEasySessionSubsystem& InSubsystem, const FEasyQuickPlayParams& InParams, FEasySessionCompleteDelegate InOnComplete)
{
	if (State != EEasyMatchmakingState::Idle)
	{
		InOnComplete.ExecuteIfBound(EEasySessionResult::MatchmakingAlreadyInProgress, TEXT("This matchmaking policy is already running."));
		return;
	}

	Subsystem = &InSubsystem;
	Params = InParams;
	OnComplete = MoveTemp(InOnComplete);
	PassesCompleted = 0;
	FailedSessionKeys.Empty();
	bCancelRequested = false;

	UE_LOG(LogEasySession, Log, TEXT("QuickPlay started (MaxPasses=%d, HostFallback=%d)"), Params.MaxSearchPasses, Params.bAllowHostFallback ? 1 : 0);

	SetState(EEasyMatchmakingState::Searching);
	RunSearchPass();
}

void UEasyMatchmakingPolicy::Cancel()
{
	if (State == EEasyMatchmakingState::Idle || State == EEasyMatchmakingState::Complete)
	{
		return;
	}

	bCancelRequested = true;

	// If we are only waiting for the next pass, finish right away.
	if (PassDelayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
		PassDelayTickerHandle.Reset();
		Complete(EEasySessionResult::Canceled, TEXT("Matchmaking was canceled."));
	}
}

void UEasyMatchmakingPolicy::SetState(EEasyMatchmakingState NewState)
{
	if (State == NewState)
	{
		return;
	}

	State = NewState;
	OnStateChanged.Broadcast(NewState);
}

void UEasyMatchmakingPolicy::RunSearchPass()
{
	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickPlay search pass %d/%d"), PassesCompleted + 1, Params.MaxSearchPasses);
	SubsystemPtr->FindEasySessions(Params.Search, FEasySessionFindCompleteDelegate::CreateUObject(this, &UEasyMatchmakingPolicy::HandleSearchComplete));
}

void UEasyMatchmakingPolicy::HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Matchmaking was canceled."));
		return;
	}

	if (Result != EEasySessionResult::Success || Results.IsEmpty())
	{
		FinishPassAndContinue();
		return;
	}

	BuildCandidateListAndJoin(Results);
}

void UEasyMatchmakingPolicy::BuildCandidateListAndJoin(const TArray<FEasySessionSearchResult>& Results)
{
	Candidates.Empty();
	for (const FEasySessionSearchResult& Result : Results)
	{
		// QuickPlay cannot supply a password, so protected sessions are never candidates.
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
	SetState(EEasyMatchmakingState::Joining);
	TryJoinNextCandidate();
}

void UEasyMatchmakingPolicy::TryJoinNextCandidate()
{
	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Matchmaking was canceled."));
		return;
	}

	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	if (!Candidates.IsValidIndex(NextCandidateIndex))
	{
		FinishPassAndContinue();
		return;
	}

	const FEasySessionSearchResult& Candidate = Candidates[NextCandidateIndex];
	UE_LOG(LogEasySession, Log, TEXT("QuickPlay joining candidate %d/%d ('%s')"), NextCandidateIndex + 1, Candidates.Num(), *Candidate.SessionDisplayName);
	SubsystemPtr->JoinEasySession(Candidate, true, FString(), FString(), FEasySessionCompleteDelegate::CreateUObject(this, &UEasyMatchmakingPolicy::HandleJoinComplete));
}

void UEasyMatchmakingPolicy::HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (Result == EEasySessionResult::Success)
	{
		Complete(EEasySessionResult::Success, FString());
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

void UEasyMatchmakingPolicy::FinishPassAndContinue()
{
	++PassesCompleted;

	if (bCancelRequested)
	{
		Complete(EEasySessionResult::Canceled, TEXT("Matchmaking was canceled."));
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

	SetState(EEasyMatchmakingState::Searching);

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

void UEasyMatchmakingPolicy::HostFallbackSession()
{
	UEasySessionSubsystem* SubsystemPtr = Subsystem.Get();
	if (SubsystemPtr == nullptr)
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is no longer available."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickPlay found no session - hosting our own."));
	SetState(EEasyMatchmakingState::Hosting);
	SubsystemPtr->CreateEasySession(Params.Host, FEasySessionCompleteDelegate::CreateUObject(this, &UEasyMatchmakingPolicy::HandleHostComplete));
}

void UEasyMatchmakingPolicy::HandleHostComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	Complete(Result, ErrorMessage);
}

void UEasyMatchmakingPolicy::Complete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (State == EEasyMatchmakingState::Complete)
	{
		return;
	}

	if (PassDelayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
		PassDelayTickerHandle.Reset();
	}

	UE_LOG(LogEasySession, Log, TEXT("QuickPlay complete: %s"), *EasySession::ResultToString(Result));
	SetState(EEasyMatchmakingState::Complete);
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

FString UEasyMatchmakingPolicy::GetSessionKey(const FEasySessionSearchResult& Session)
{
	return Session.NativeResult.IsValid() ? Session.NativeResult.GetSessionIdStr() : Session.SessionDisplayName;
}
