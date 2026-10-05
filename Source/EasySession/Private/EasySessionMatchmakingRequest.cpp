// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionMatchmakingRequest.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionHost.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "HAL/PlatformTime.h"

namespace
{
	// Seconds a follow keeps searching for its host.
	// A host that created its session opens its map only after telling the group, so the session appears once that map has loaded.
	constexpr double FollowTimeLimitSeconds = 30.0;

	// Seconds between two search passes of a follow.
	constexpr float FollowPassDelaySeconds = 1.0f;
}

FEasySessionMatchmakingRequest::FEasySessionMatchmakingRequest(const FEasyMatchmakingParams& InParams, UEasyMatchmakingPolicy& InPolicy, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Matchmaking)
	, Params(InParams)
	, Policy(&InPolicy)
	, OnComplete(MoveTemp(InOnComplete))
{
}

TSharedRef<FEasySessionMatchmakingRequest> FEasySessionMatchmakingRequest::MakeFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery, UEasyMatchmakingPolicy& InPolicy, FEasySessionCompleteDelegate InOnComplete)
{
	FEasyMatchmakingParams FollowParams;
	FollowParams.Search.OwnerId = HostId;
	FollowParams.Search.bLANQuery = bLANQuery;
	FollowParams.MaxSearchPasses = MAX_int32;
	FollowParams.DelayBetweenPassesSeconds = FollowPassDelaySeconds;
	FollowParams.bAllowHostFallback = false;

	TSharedRef<FEasySessionMatchmakingRequest> Follow = MakeShared<FEasySessionMatchmakingRequest>(FollowParams, InPolicy, MoveTemp(InOnComplete));
	Follow->bFollowsHost = true;
	return Follow;
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

	// A run is meant for a player in no session, so a session the game's own Create or Join or an accepted invite made refuses the run instead of being destroyed.
	// A follow starts inside a session on purpose, because its join destroys that session only after the reservation beacon approved the join.
	// So does a host whose match has not started, because each of its joins takes the session's players along.
	const EEasySessionState LocalState = GetContext().Subsystem.GetSessionState();
	// A party leader takes the party along too, and a party leader is never in a game session.
	const bool bHostsLobby = GetContext().Subsystem.IsSessionAuthority() && LocalState != EEasySessionState::Starting && LocalState != EEasySessionState::InProgress;
	if (bHostsLobby || GetContext().Party.IsLeader())
	{
		Params.Search.MinOpenSlots = FMath::Max(Params.Search.MinOpenSlots, GetGroupMembers().Num() + 1);
	}
	else if (!bFollowsHost && GetContext().Subsystem.IsSessionAuthority())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("This player hosts a match in progress, and leaving would end it for every player. End the match or call Leave Easy Session first."));
		return;
	}
	else if (!bFollowsHost && GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a session. Call Leave Easy Session first, or use Join Easy Session to switch to a session you found."));
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

	// Searching or waiting for the next pass: nothing needs undoing, so the run completes inside this call.
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
	if (bFollowsHost)
	{
		UE_LOG(LogEasySession, Log, TEXT("Follow search pass %d (up to %.0f seconds)"), PassesCompleted + 1, FollowTimeLimitSeconds);
	}
	else
	{
		UE_LOG(LogEasySession, Log, TEXT("Matchmaking search pass %d/%d"), PassesCompleted + 1, Params.MaxSearchPasses);
	}
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
	// A host searching from its own session can find that session, and joining it would only fail.
	FString CurrentSessionId;
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* CurrentSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr;
	if (CurrentSession != nullptr && CurrentSession->SessionInfo.IsValid())
	{
		CurrentSessionId = CurrentSession->SessionInfo->GetSessionId().ToString();
	}

	Candidates.Empty();
	for (const FEasySessionSearchResult& Result : Results)
	{
		if (!CurrentSessionId.IsEmpty() && Result.NativeResult.GetSessionIdStr() == CurrentSessionId)
		{
			continue;
		}

		// A password-protected session is only tried when this run carries a password to offer, or a reservation that needs none.
		if (!bFollowsHost && Result.bPasswordProtected && Params.JoinPassword.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		if (!FailedSessionKeys.Contains(GetSessionKey(Result)))
		{
			Candidates.Add(Result);
		}
	}

	// A Blueprint policy runs its ScoreSession in the Blueprint VM, so every search result is scored once and the sort reads the score it got.
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

	// A follow ends on its time limit, because a slow map load on the host decides how many passes it needs.
	const bool bOutOfTime = bFollowsHost && FPlatformTime::Seconds() - RunStartTimeSeconds >= FollowTimeLimitSeconds;
	if (PassesCompleted >= Params.MaxSearchPasses || bOutOfTime)
	{
		// A host that moves its players already has a session, so it stays in it rather than hosting another.
		if (Params.bAllowHostFallback && !GetContext().Subsystem.IsInSession())
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
