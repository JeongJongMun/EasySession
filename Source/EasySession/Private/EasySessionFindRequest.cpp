// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionFindRequest.h"

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionFindRequest::FEasySessionFindRequest(const FEasySessionSearchParams& InSearchParams, FEasySessionFindCompleteDelegate InOnFindComplete)
	: FEasySessionRequest(EType::Find)
	, SearchParams(InSearchParams)
	, OnFindComplete(MoveTemp(InOnFindComplete))
{
	// A targeted query names one session, hidden or not.
	// Notify does not broadcast or store the results of a search that included hidden sessions.
	if (SearchParams.IsSpecificSessionQuery())
	{
		SearchParams.bIncludeHiddenSessions = true;
	}
}

TSharedPtr<FEasySessionFindRequest> FEasySessionFindRequest::Cast(const TSharedPtr<FEasySessionRequest>& Request)
{
	return Request.IsValid() && Request->Type == EType::Find ? StaticCastSharedPtr<FEasySessionFindRequest>(Request) : nullptr;
}

bool FEasySessionFindRequest::Cancel(const UObject* Requester)
{
	if (!IsActive() || !OnFindComplete.IsBoundToObject(Requester))
	{
		return false;
	}

	// A LAN search can be stopped, so the request is abandoned here and Cleanup tells the online subsystem.
	if (Search.IsValid() && Search->bIsLanQuery)
	{
		Complete(EEasySessionResult::Canceled, TEXT("The search was canceled."), /*bAbandoned*/ true);
		return true;
	}

	// The online subsystem cannot stop an internet search, so the request keeps the active slot and only the requester's delegate is unbound.
	bCanceled = true;
	FEasySessionFindCompleteDelegate CanceledDelegate = MoveTemp(OnFindComplete);
	OnFindComplete.Unbind();
	CanceledDelegate.ExecuteIfBound(EEasySessionResult::Canceled, TEXT("The search was canceled."), TArray<FEasySessionSearchResult>());
	return true;
}

void FEasySessionFindRequest::Execute()
{
	// A new search invalidates the last search results.
	// They are emptied here rather than when the search completes, so nothing can list sessions from the previous search while this one runs.
	GetContext().Subsystem.SetLastSearchResults(TArray<FEasySessionSearchResult>());

	if (!SearchParams.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Search params are invalid: Max Results must be above 0, Timeout Override Seconds must not be negative, and Search Mode and Search Target Id must be set together."));
		return;
	}

	if (SearchParams.SearchMode == EEasySessionSearchMode::ByFriend)
	{
		StartFriendSessionSearch();
		return;
	}

	StartSessionSearch();
}

void FEasySessionFindRequest::StartFriendSessionSearch()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	// NULL completes every friend query with "no session" without searching, so that result would look like a real one.
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetContext().GetWorld());
	if (OnlineSub == nullptr || !OnlineSub->GetFriendsInterface().IsValid())
	{
		Complete(EEasySessionResult::NotSupportedByService, TEXT("This online subsystem has no friends to look up (e.g. NULL/LAN)."));
		return;
	}

	FindFriendCompleteHandle = Sessions->AddOnFindFriendSessionCompleteDelegate_Handle(0,
		FOnFindFriendSessionCompleteDelegate::CreateSP(this, &FEasySessionFindRequest::HandleFindFriendSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Searching for a friend's session."));

	// A refused call can complete the request inside it, before it returns.
	// Only a false return while the request is still active is a failure.
	if (!Sessions->FindFriendSession(0, *SearchParams.SearchTargetId.GetUniqueNetId()) && IsActive())
	{
		Complete(EEasySessionResult::SearchFailure, TEXT("FindFriendSession request was rejected by the online subsystem."));
	}
}

void FEasySessionFindRequest::StartSessionSearch()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	Search->MaxSearchResults = SearchParams.MaxResults;
	Search->bIsLanQuery = SearchParams.bLANQuery || GetContext().ShouldForceLAN();

	if (!Search->bIsLanQuery)
	{
		// Steam only searches lobbies when this key is present.
		// Without it the query goes to the dedicated server master list and never returns lobby sessions.
		Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateSP(this, &FEasySessionFindRequest::HandleFindSessionsComplete));

	UE_LOG(LogEasySession, Log, TEXT("Searching for sessions (MaxResults=%d, LAN=%d)"), SearchParams.MaxResults, Search->bIsLanQuery ? 1 : 0);

	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Complete(EEasySessionResult::SearchFailure, TEXT("FindSessions request was rejected by the online subsystem."));
		return;
	}

	// The online subsystem drops a search it cannot start and still returns true, so no completion delegate fires.
	// It marks the search it accepted as InProgress, so an unchanged state means it dropped ours.
	if (IsActive() && Search.IsValid() && Search->SearchState != EOnlineAsyncTaskState::InProgress)
	{
		Complete(EEasySessionResult::SearchFailure,
			TEXT("Another session search is already running, so this one was dropped by the online subsystem."));
	}
}

void FEasySessionFindRequest::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!IsActive())
	{
		return;
	}

	// Every search in the process fires this delegate.
	// The online subsystem sets a search's state before firing it, so ours still InProgress means another search completed.
	if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		UE_LOG(LogEasySession, Verbose, TEXT("Ignoring a search completion that belongs to another search."));
		return;
	}

	if (!bWasSuccessful || !Search.IsValid())
	{
		Complete(EEasySessionResult::SearchFailure, TEXT("The online subsystem failed to search for sessions."));
		return;
	}

	// Release the finished search before the results are filtered.
	const TArray<FOnlineSessionSearchResult> NativeResults = MoveTemp(Search->SearchResults);
	Search.Reset();

	FinishSearch(NativeResults);
}

void FEasySessionFindRequest::HandleFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendResults)
{
	if (!IsActive())
	{
		return;
	}

	// False means the friend is not in a joinable session right now, which is a result and not a failure.
	FinishSearch(bWasSuccessful ? FriendResults : TArray<FOnlineSessionSearchResult>());
}

void FEasySessionFindRequest::FinishSearch(const TArray<FOnlineSessionSearchResult>& NativeResults)
{
	for (const FOnlineSessionSearchResult& NativeResult : NativeResults)
	{
		if (!NativeResult.IsValid())
		{
			continue;
		}

		FEasySessionSearchResult Result = FEasySessionSearchResult::FromNative(NativeResult);
		if (SearchParams.ShouldInclude(Result))
		{
			Results.Add(MoveTemp(Result));
		}
	}

	UE_LOG(LogEasySession, Log, TEXT("Search complete. %d session(s) found after filtering."), Results.Num());
	Complete(EEasySessionResult::Success);
}

void FEasySessionFindRequest::Cleanup(bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Search.Reset();
		return;
	}

	// Unbind first: an abandoned search is canceled below, and the online subsystem fires the completion delegate while it cancels.
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(0, FindFriendCompleteHandle);

	if (!Search.IsValid())
	{
		return;
	}

	// An abandoned search keeps running in the online subsystem, which refuses new ones until it ends.
	// InProgress means the one it still holds is ours.
	if (bAbandoned && Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Cancelling the abandoned search so later searches are not refused."));
		Sessions->CancelFindSessions();
	}
	// A search that failed inside the call is still held too.
	// CancelFindSessions only releases a search marked InProgress, so ours is marked InProgress again first.
	else if (Search->SearchState == EOnlineAsyncTaskState::Failed)
	{
		UE_LOG(LogEasySession, Log, TEXT("Releasing the failed search so later searches are not refused."));
		Search->SearchState = EOnlineAsyncTaskState::InProgress;
		Sessions->CancelFindSessions();
	}

	Search.Reset();
}

void FEasySessionFindRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	// Stored before the requester's delegate fires, so Get Last Easy Search Results already returns them inside that delegate.
	GetContext().Subsystem.SetLastSearchResults(Results);
	OnFindComplete.ExecuteIfBound(Result, ErrorMessage, Results);

	// A search that included hidden sessions is not broadcast and not kept as the last search results.
	// Neither is a canceled one, whose results the requester no longer wants.
	if (SearchParams.bIncludeHiddenSessions || bCanceled)
	{
		GetContext().Subsystem.SetLastSearchResults(TArray<FEasySessionSearchResult>());
		return;
	}

	GetContext().Subsystem.OnSessionsFound.Broadcast(Result, ErrorMessage, Results);
}
