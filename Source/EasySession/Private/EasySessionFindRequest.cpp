// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionFindRequest.h"

#include "EasySession.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionReadFriendsRequest.h"
#include "EasySessionSubsystem.h"
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
	if (SearchParams.IsSpecificSessionQuery())
	{
		SearchParams.bIncludeHiddenSessions = true;
	}
}

void FEasySessionFindRequest::Execute()
{
	if (!SearchParams.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Search params are invalid: Max Results must be above 0, and Search Mode and Search Target Id must be set together."));
		return;
	}

	if (SearchParams.SearchMode == EEasySessionSearchMode::ByFriend)
	{
		FindFriendSession();
		return;
	}

	FindSessions();
}

void FEasySessionFindRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Search.Reset();
		return;
	}

	// Unbind first: a running search is canceled below, and the online subsystem fires the completion delegate while it cancels.
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(0, FindFriendCompleteHandle);

	if (!Search.IsValid())
	{
		return;
	}

	// A search still InProgress was canceled, and the online subsystem refuses new searches until it ends.
	// A search that completed is already marked Done, so InProgress means the one it still holds is ours.
	if (Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Cancelling the unfinished search so later searches are not refused."));
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
	OnFindComplete.ExecuteIfBound(Result, ErrorMessage, Results);
}

void FEasySessionFindRequest::HandleCancel()
{
	// A LAN search can be stopped, so the request completes now and Cleanup tells the online subsystem.
	if (Search.IsValid() && Search->bIsLanQuery)
	{
		Complete(EEasySessionResult::Canceled, TEXT("The search was canceled."));
		return;
	}

	FEasySessionRequest::HandleCancel();
}

void FEasySessionFindRequest::FindFriendSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	// NULL completes every friend query with "no session" without searching, so that result would look like a real one.
	if (!FEasySessionReadFriendsRequest::HasFriendsList(GetWorld()))
	{
		Complete(EEasySessionResult::NotSupportedByService, EasySession::NoFriendsListMessage);
		return;
	}

	FindFriendCompleteHandle = Sessions->AddOnFindFriendSessionCompleteDelegate_Handle(0,
		FOnFindFriendSessionCompleteDelegate::CreateSP(this, &FEasySessionFindRequest::HandleFindFriendSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Searching for a friend's session."));

	// A refused call can complete the request inside it, before it returns.
	// Only a false return while the request is still running is a failure.
	if (!Sessions->FindFriendSession(0, *SearchParams.SearchTargetId.GetUniqueNetId()) && IsRunning())
	{
		Complete(EEasySessionResult::SearchFailure, TEXT("FindFriendSession request was rejected by the online subsystem."));
	}
}

void FEasySessionFindRequest::HandleFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendResults)
{
	if (!IsRunning())
	{
		return;
	}

	// False means the friend is not in a joinable session right now, which is a result and not a failure.
	CompleteWithResults(bWasSuccessful ? FriendResults : TArray<FOnlineSessionSearchResult>());
}

void FEasySessionFindRequest::FindSessions()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	Search->MaxSearchResults = SearchParams.MaxResults;
	Search->bIsLanQuery = SearchParams.bLANQuery || ShouldForceLAN();

	if (!Search->bIsLanQuery)
	{
		// Steam only searches lobbies when this key is present.
		// Without it the query goes to the dedicated server master list and never returns lobby sessions.
		Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}

	// Parties and game sessions are both advertised, so the search asks for the kind this request looks for.
	Search->QuerySettings.Set(EasySession::SettingKey_Party, IsPartySearch() ? 1 : 0, EOnlineComparisonOp::Equals);

	// A search for one session filters on the online service, so that session is found however many others exist.
	// The filter on the returned results still runs, because NULL (LAN) ignores these query settings.
	if (SearchParams.OwnerId.IsValid())
	{
		Search->QuerySettings.Set(EasySession::SettingKey_OwnerId, SearchParams.OwnerId.ToString(), EOnlineComparisonOp::Equals);
	}
	if (!SearchParams.JoinCode.IsEmpty())
	{
		// Codes are advertised in upper case, and the online service compares them as written.
		Search->QuerySettings.Set(EasySession::SettingKey_JoinCode, SearchParams.JoinCode.ToUpper(), EOnlineComparisonOp::Equals);
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
	if (IsRunning() && Search.IsValid() && Search->SearchState != EOnlineAsyncTaskState::InProgress)
	{
		Complete(EEasySessionResult::SearchFailure,
			TEXT("Another session search is already running, so this one was dropped by the online subsystem."));
	}
}

void FEasySessionFindRequest::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!IsRunning())
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

	CompleteWithResults(NativeResults);
}

void FEasySessionFindRequest::CompleteWithResults(const TArray<FOnlineSessionSearchResult>& NativeResults)
{
	for (const FOnlineSessionSearchResult& NativeResult : NativeResults)
	{
		// NULL ignores the query settings, and a friend's session may be either kind, so the kind is checked here too.
		if (!NativeResult.IsValid() || FEasySessionParty::IsPartySession(NativeResult.Session.SessionSettings) != IsPartySearch())
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

bool FEasySessionFindRequest::IsPartySearch() const
{
	return SessionName == NAME_PartySession;
}
