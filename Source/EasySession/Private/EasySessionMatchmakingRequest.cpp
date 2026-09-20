// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionMatchmakingRequest.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "HAL/PlatformTime.h"

FEasySessionMatchmakingRequest::FEasySessionMatchmakingRequest(const FEasyMatchmakingParams& InParams, UEasyMatchmakingPolicy& InPolicy, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Matchmaking)
	, Params(InParams)
	, Policy(&InPolicy)
	, OnComplete(MoveTemp(InOnComplete))
{
}

FEasySessionMatchmakingRequest::~FEasySessionMatchmakingRequest()
{
	StopTickers();
}

TSharedPtr<FEasySessionMatchmakingRequest> FEasySessionMatchmakingRequest::Cast(const TSharedPtr<FEasySessionRequest>& Request)
{
	return Request.IsValid() && Request->Type == EType::Matchmaking ? StaticCastSharedPtr<FEasySessionMatchmakingRequest>(Request) : nullptr;
}

int32 FEasySessionMatchmakingRequest::GetElapsedWholeSeconds() const
{
	if (RunStartTimeSeconds <= 0.0)
	{
		return 0;
	}

	const double End = RunEndTimeSeconds > 0.0 ? RunEndTimeSeconds : FPlatformTime::Seconds();
	return static_cast<int32>(End - RunStartTimeSeconds);
}

void FEasySessionMatchmakingRequest::Execute()
{
	RunStartTimeSeconds = FPlatformTime::Seconds();

	// An accepted invite or the game's own Create or Join ran before this run started, and every sub-request would fail against that session.
	if (GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Matchmaking stopped."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Matchmaking started (MaxPasses=%d, HostFallback=%d)"), Params.MaxSearchPasses, Params.bAllowHostFallback ? 1 : 0);

	UpdateTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FEasySessionMatchmakingRequest::BroadcastUpdate), 1.0f);

	StartSearchPass();
}

void FEasySessionMatchmakingRequest::Cleanup()
{
	StopTickers();
}

void FEasySessionMatchmakingRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	// A cancel during an internet search notifies the requester while the search sub-request still runs, so the tickers stop here too.
	StopTickers();

	// Frozen before the Complete state broadcast, so every listener reads the same final time.
	RunEndTimeSeconds = FPlatformTime::Seconds();

	UE_LOG(LogEasySession, Log, TEXT("Matchmaking complete: %s"), *EasySession::ResultToString(Result));
	SetState(EEasyMatchmakingState::Complete);

	// Observers first: the requester's delegate often destroys the very UI that is listening.
	GetContext().Subsystem.OnMatchmakingComplete.Broadcast(Result, ErrorMessage);
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionMatchmakingRequest::HandleCancel()
{
	if (State == EEasyMatchmakingState::Canceling)
	{
		return;
	}
	SetState(EEasyMatchmakingState::Canceling);

	// A join or a host cannot stop and may still succeed. Its completion handler undoes a success and completes the run.
	const TSharedPtr<FEasySessionRequest>& SubRequest = GetRunningSubRequest();
	if (SubRequest.IsValid() && (SubRequest->Type == EType::Join || SubRequest->Type == EType::Create))
	{
		return;
	}

	// Searching or waiting for the next pass: nothing needs undoing, so the run completes now.
	Complete(EEasySessionResult::Canceled, EasySession::MatchmakingCanceledMessage);
}

FString FEasySessionMatchmakingRequest::GetProgressText() const
{
	// The requester already has the result, so the base class reports the wait for the running sub-request.
	if (HasNotified())
	{
		return FEasySessionRequest::GetProgressText();
	}

	const UEnum* StateEnum = StaticEnum<EEasyMatchmakingState>();
	return FString::Printf(TEXT(" (%s, %ds)"), *StateEnum->GetNameStringByValue(static_cast<int64>(State)), GetElapsedWholeSeconds());
}

void FEasySessionMatchmakingRequest::StartSearchPass()
{
	UE_LOG(LogEasySession, Log, TEXT("Matchmaking search pass %d/%d"), PassesCompleted + 1, Params.MaxSearchPasses);
	SetState(EEasyMatchmakingState::Searching);
	RunSubRequest(MakeShared<FEasySessionFindRequest>(Params.Search,
		FEasySessionFindCompleteDelegate::CreateSP(this, &FEasySessionMatchmakingRequest::HandleSearchComplete)));
}

void FEasySessionMatchmakingRequest::HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	if (Result != EEasySessionResult::Success || Results.IsEmpty())
	{
		FinishSearchPass(Result, ErrorMessage);
		return;
	}

	BuildCandidates(Results);
	if (Candidates.IsEmpty())
	{
		FinishSearchPass();
		return;
	}

	NextCandidateIndex = 0;
	SetState(EEasyMatchmakingState::Joining);
	JoinNextCandidate();
}

void FEasySessionMatchmakingRequest::BuildCandidates(const TArray<FEasySessionSearchResult>& Results)
{
	Candidates.Empty();
	for (const FEasySessionSearchResult& Result : Results)
	{
		// A password-protected session is only a candidate when this run carries a password to offer.
		if (Result.bPasswordProtected && Params.JoinPassword.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		if (!FailedSessionKeys.Contains(GetSessionKey(Result)))
		{
			Candidates.Add(Result);
		}
	}

	// A Blueprint policy runs its ScoreSession in the Blueprint VM, so every candidate is scored once and the sort reads the score it got.
	UEasyMatchmakingPolicy* ScoringPolicy = Policy.Get();
	TArray<TPair<float, FEasySessionSearchResult>> ScoredCandidates;
	ScoredCandidates.Reserve(Candidates.Num());
	for (const FEasySessionSearchResult& Candidate : Candidates)
	{
		ScoredCandidates.Emplace(ScoringPolicy->ScoreSession(Candidate), Candidate);
	}

	ScoredCandidates.StableSort([](const TPair<float, FEasySessionSearchResult>& A, const TPair<float, FEasySessionSearchResult>& B)
	{
		return A.Key > B.Key;
	});

	Candidates.Reset();
	for (TPair<float, FEasySessionSearchResult>& Scored : ScoredCandidates)
	{
		Candidates.Add(MoveTemp(Scored.Value));
	}

	// Players searching at the same time would all try the same best session first, so the best few are shuffled.
	const int32 ShuffleCount = FMath::Min(ScoringPolicy->TopCandidatesToShuffle, Candidates.Num());
	for (int32 Index = 0; Index < ShuffleCount - 1; ++Index)
	{
		const int32 SwapIndex = FMath::RandRange(Index, ShuffleCount - 1);
		if (SwapIndex != Index)
		{
			Candidates.Swap(Index, SwapIndex);
		}
	}
}

void FEasySessionMatchmakingRequest::JoinNextCandidate()
{
	if (!Candidates.IsValidIndex(NextCandidateIndex))
	{
		FinishSearchPass();
		return;
	}

	const FEasySessionSearchResult& Candidate = Candidates[NextCandidateIndex];
	UE_LOG(LogEasySession, Log, TEXT("Matchmaking joining candidate %d/%d ('%s')"), NextCandidateIndex + 1, Candidates.Num(), *Candidate.SessionDisplayName);
	RunSubRequest(MakeShared<FEasySessionJoinRequest>(Candidate, Params.JoinPassword, FString(),
		FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionMatchmakingRequest::HandleJoinComplete)));
}

void FEasySessionMatchmakingRequest::HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (State == EEasyMatchmakingState::Canceling)
	{
		CompleteAsCanceled(Result);
		return;
	}

	if (Result == EEasySessionResult::Success)
	{
		Complete(EEasySessionResult::Success);
		return;
	}

	FailedSessionKeys.Add(GetSessionKey(Candidates[NextCandidateIndex]));
	++NextCandidateIndex;
	JoinNextCandidate();
}

void FEasySessionMatchmakingRequest::FinishSearchPass(EEasySessionResult SearchResult, const FString& SearchError)
{
	++PassesCompleted;

	if (PassesCompleted >= Params.MaxSearchPasses)
	{
		if (Params.bAllowHostFallback)
		{
			HostFallbackSession();
		}
		// A failed search found nothing because it never ran, so the run keeps its result.
		else if (SearchResult != EEasySessionResult::Success)
		{
			Complete(SearchResult, SearchError);
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
		StartSearchPass();
		return;
	}

	PassDelayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FEasySessionMatchmakingRequest::HandlePassDelayElapsed), Params.DelayBetweenPassesSeconds);
}

bool FEasySessionMatchmakingRequest::HandlePassDelayElapsed(float DeltaTime)
{
	PassDelayTickerHandle.Reset();
	StartSearchPass();
	return false;
}

void FEasySessionMatchmakingRequest::HostFallbackSession()
{
	UE_LOG(LogEasySession, Log, TEXT("Matchmaking found no session, so it hosts one."));

	// The Create sub-request is requested before the Hosting broadcast, so a cancel from that broadcast finds it and undoes it.
	RunSubRequest(MakeShared<FEasySessionCreateRequest>(MakeFallbackHostParams(),
		FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionMatchmakingRequest::HandleHostComplete)));
	SetState(EEasyMatchmakingState::Hosting);
}

FEasySessionHostParams FEasySessionMatchmakingRequest::MakeFallbackHostParams() const
{
	// The fallback session must be one this run's own search would find: on the searched network, with every required key advertised at the required value.
	FEasySessionHostParams FallbackParams = Params.Host;
	FallbackParams.bIsLANMatch = Params.Search.bLANQuery;
	FallbackParams.CustomSettings.Append(Params.Search.RequiredCustomSettings);
	if (Params.Search.Region != EEasySessionRegion::Any)
	{
		FallbackParams.Region = Params.Search.Region;
	}

	// The fallback session is public, so players running the same search can find and join it.
	FallbackParams.Password.Empty();
	FallbackParams.bHidden = false;
	FallbackParams.bShouldAdvertise = true;
	return FallbackParams;
}

void FEasySessionMatchmakingRequest::HandleHostComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (State == EEasyMatchmakingState::Canceling)
	{
		CompleteAsCanceled(Result);
		return;
	}

	Complete(Result, ErrorMessage);
}

void FEasySessionMatchmakingRequest::CompleteAsCanceled(EEasySessionResult SubRequestResult)
{
	if (SubRequestResult != EEasySessionResult::Success)
	{
		Complete(EEasySessionResult::Canceled, EasySession::MatchmakingCanceledMessage);
		return;
	}

	// Still the same frame as the travel request, so the map has not started loading.
	GetContext().Travel.CancelPendingTravel();

	// The run joined or hosted a session before the cancel arrived, so that session is destroyed first.
	RunSubRequest(MakeShared<FEasySessionDestroyRequest>(FEasySessionCompleteDelegate::CreateSPLambda(this,
		[this](EEasySessionResult /*DestroyResult*/, const FString& /*DestroyError*/)
		{
			Complete(EEasySessionResult::Canceled, EasySession::MatchmakingCanceledMessage);
		})));
}

void FEasySessionMatchmakingRequest::SetState(EEasyMatchmakingState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const EEasyMatchmakingState OldState = State;
	State = NewState;

	UEasySessionSubsystem& Subsystem = GetContext().Subsystem;
	Subsystem.OnMatchmakingStateChanged.Broadcast(OldState, NewState);
	Subsystem.OnMatchmakingUpdated.Broadcast(NewState, GetElapsedWholeSeconds());
}

bool FEasySessionMatchmakingRequest::BroadcastUpdate(float DeltaTime)
{
	GetContext().Subsystem.OnMatchmakingUpdated.Broadcast(State, GetElapsedWholeSeconds());
	return true;
}

void FEasySessionMatchmakingRequest::StopTickers()
{
	if (PassDelayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PassDelayTickerHandle);
		PassDelayTickerHandle.Reset();
	}

	if (UpdateTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(UpdateTickerHandle);
		UpdateTickerHandle.Reset();
	}
}

FString FEasySessionMatchmakingRequest::GetSessionKey(const FEasySessionSearchResult& Session)
{
	return Session.NativeResult.IsValid() ? Session.NativeResult.GetSessionIdStr() : Session.SessionDisplayName;
}
