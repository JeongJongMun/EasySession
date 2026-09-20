// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionFriendSessionsRequest.h"

#include "EasySession.h"
#include "EasySessionFindRequest.h"
#include "EasySessionReadFriendsRequest.h"

FEasySessionFriendSessionsRequest::FEasySessionFriendSessionsRequest(FEasyFriendSessionsCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::FriendSessions)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionFriendSessionsRequest::SortFriendSessions(TArray<FEasyFriendSession>& InFriendSessions)
{
	InFriendSessions.StableSort([](const FEasyFriendSession& A, const FEasyFriendSession& B)
	{
		if (A.bHasSession != B.bHasSession)
		{
			return A.bHasSession;
		}
		return FEasySessionReadFriendsRequest::FriendComesFirst(A.Friend, B.Friend);
	});
}

void FEasySessionFriendSessionsRequest::Execute()
{
	RunSubRequest(MakeShared<FEasySessionReadFriendsRequest>(
		FEasyFriendsCompleteDelegate::CreateSP(this, &FEasySessionFriendSessionsRequest::HandleFriendsRead)));
}

void FEasySessionFriendSessionsRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	int32 InSessionCount = 0;
	for (const FEasyFriendSession& FriendSession : FriendSessions)
	{
		InSessionCount += FriendSession.bHasSession ? 1 : 0;
	}
	UE_LOG(LogEasySession, Log, TEXT("Friend session search complete: %s, %d friend(s), %d in a session."), *EasySession::ResultToString(Result), FriendSessions.Num(), InSessionCount);

	SortFriendSessions(FriendSessions);
	OnComplete.ExecuteIfBound(Result, ErrorMessage, FriendSessions);
}

void FEasySessionFriendSessionsRequest::HandleCancel()
{
	Complete(EEasySessionResult::Canceled, TEXT("The friend search was canceled."));
}

FString FEasySessionFriendSessionsRequest::GetProgressText() const
{
	// The requester already has the result, so the base class reports the wait for the running sub-request.
	if (HasNotified())
	{
		return FEasySessionRequest::GetProgressText();
	}

	// The friends list is still read, and its sub-request shows on the status line.
	if (FriendSessions.IsEmpty())
	{
		return FString();
	}

	const int32 Done = SearchTotal - FriendsToSearch.Num() - (SearchingFriend == INDEX_NONE ? 0 : 1);
	return FString::Printf(TEXT(" %d/%d"), Done, SearchTotal);
}

void FEasySessionFriendSessionsRequest::HandleFriendsRead(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
{
	if (Result != EEasySessionResult::Success)
	{
		Complete(Result, ErrorMessage);
		return;
	}

	// Every friend is listed. Only friends playing this game can be in a session this game finds, so only their sessions are searched for.
	for (const FEasySessionFriend& Friend : Friends)
	{
		FEasyFriendSession& FriendSession = FriendSessions.AddDefaulted_GetRef();
		FriendSession.Friend = Friend;
		if (Friend.bIsPlayingThisGame && Friend.IsValid())
		{
			FriendsToSearch.Add(FriendSessions.Num() - 1);
		}
	}
	SearchTotal = FriendsToSearch.Num();

	FindNextFriendSession();
}

void FEasySessionFriendSessionsRequest::FindNextFriendSession()
{
	if (FriendsToSearch.IsEmpty())
	{
		Complete(EEasySessionResult::Success);
		return;
	}

	SearchingFriend = FriendsToSearch.Pop();

	FEasySessionSearchParams SearchParams;
	SearchParams.SearchMode = EEasySessionSearchMode::ByFriend;
	SearchParams.SearchTargetId = FriendSessions[SearchingFriend].Friend.NativeId;
	RunSubRequest(MakeShared<FEasySessionFindRequest>(SearchParams,
		FEasySessionFindCompleteDelegate::CreateSP(this, &FEasySessionFriendSessionsRequest::HandleFriendSessionFindComplete)));
}

void FEasySessionFriendSessionsRequest::HandleFriendSessionFindComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results)
{
	FEasyFriendSession& FriendSession = FriendSessions[SearchingFriend];
	SearchingFriend = INDEX_NONE;

	// A failure only means no session for that friend.
	if (Result == EEasySessionResult::Success && Results.Num() > 0 && Results[0].IsValid())
	{
		FriendSession.Session = Results[0];
		FriendSession.bHasSession = true;
	}

	FindNextFriendSession();
}
