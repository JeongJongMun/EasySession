// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasyFriendSessionOperation.h"

#include "EasySession.h"
#include "EasySessionSocial.h"
#include "EasySessionSubsystem.h"

FEasyFriendSessionOperation::FEasyFriendSessionOperation(UEasySessionSubsystem& InOwner)
	: Owner(InOwner)
{
}

void FEasyFriendSessionOperation::Start(FEasyFriendSessionsCompleteDelegate InOnComplete)
{
	OnComplete = MoveTemp(InOnComplete);
	Owner.ReadFriends(FEasyFriendsCompleteDelegate::CreateSP(this, &FEasyFriendSessionOperation::HandleFriendsRead));
}

void FEasyFriendSessionOperation::Cancel()
{
	if (bFinished)
	{
		return;
	}

	// The lookup in flight keeps running on the queue; its answer arrives after Finish and is dropped.
	Pending.Reset();
	Current = INDEX_NONE;
	Finish(EEasySessionResult::Canceled, TEXT("The friend search was canceled."));
}

FString FEasyFriendSessionOperation::DescribeProgress() const
{
	if (Entries.IsEmpty())
	{
		return TEXT("Friend search (reading friends)");
	}

	const int32 Done = LookupCount - Pending.Num() - (Current == INDEX_NONE ? 0 : 1);
	return FString::Printf(TEXT("Friend search %d/%d"), Done, LookupCount);
}

void FEasyFriendSessionOperation::HandleFriendsRead(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
{
	if (bFinished)
	{
		return;
	}

	if (Result != EEasySessionResult::Success)
	{
		Finish(Result, ErrorMessage);
		return;
	}

	// Every friend is listed. Only friends playing this game can be in a findable session, so only those are asked.
	for (const FEasySessionFriend& Friend : Friends)
	{
		FEasyFriendSession& Entry = Entries.AddDefaulted_GetRef();
		Entry.Friend = Friend;
		if (Friend.bIsPlayingThisGame && Friend.IsValid())
		{
			Pending.Add(Entries.Num() - 1);
		}
	}
	LookupCount = Pending.Num();

	QueryNext();
}

void FEasyFriendSessionOperation::QueryNext()
{
	if (bFinished)
	{
		return;
	}

	if (Pending.IsEmpty())
	{
		Finish(EEasySessionResult::Success, FString());
		return;
	}

	Current = Pending.Pop();

	FEasySessionSearchParams QueryParams;
	QueryParams.SearchMode = EEasySessionSearchMode::ByFriend;
	QueryParams.SearchTargetId = Entries[Current].Friend.NativeId;
	Owner.FindEasySessions(QueryParams, FEasySessionFindCompleteDelegate::CreateSP(this, &FEasyFriendSessionOperation::HandleQueryComplete));
}

void FEasyFriendSessionOperation::HandleQueryComplete(EEasySessionResult Result, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& Results)
{
	if (bFinished || Current == INDEX_NONE)
	{
		return;
	}

	FEasyFriendSession& Entry = Entries[Current];
	Current = INDEX_NONE;

	// Whatever one lookup reported, the search goes on - a failed lookup only means no session for that friend.
	if (Result == EEasySessionResult::Success && Results.Num() > 0 && Results[0].IsValid())
	{
		Entry.Session = Results[0];
		Entry.bHasSession = true;
	}

	QueryNext();
}

void FEasyFriendSessionOperation::Finish(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	int32 InSessionCount = 0;
	for (const FEasyFriendSession& Entry : Entries)
	{
		InSessionCount += Entry.bHasSession ? 1 : 0;
	}
	UE_LOG(LogEasySession, Log, TEXT("Friend session search complete: %s, %d friend(s), %d in a session."), *EasySession::ResultToString(Result), Entries.Num(), InSessionCount);

	// The completion ends this operation on the queue, which drops the queue's reference - this local one keeps the object alive until the call returns.
	const TSharedRef<IEasySessionOperation> Self = AsShared();
	TArray<FEasyFriendSession> Delivered = MoveTemp(Entries);
	Entries.Reset();
	FEasySessionSocial::SortFriendSessions(Delivered);
	OnComplete.ExecuteIfBound(Result, ErrorMessage, Delivered);
}
