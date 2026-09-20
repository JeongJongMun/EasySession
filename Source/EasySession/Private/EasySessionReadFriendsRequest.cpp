// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionReadFriendsRequest.h"

#include "EasySession.h"
#include "EasySessionMessages.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionReadFriendsRequest::FEasySessionReadFriendsRequest(FEasyFriendsCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::ReadFriends)
	, OnComplete(MoveTemp(InOnComplete))
{
}

bool FEasySessionReadFriendsRequest::HasFriendsList(const UWorld* World)
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	return OnlineSub != nullptr && OnlineSub->GetFriendsInterface().IsValid();
}

bool FEasySessionReadFriendsRequest::FriendComesFirst(const FEasySessionFriend& A, const FEasySessionFriend& B)
{
	const int32 RankA = A.bIsPlayingThisGame ? 2 : (A.bIsOnline ? 1 : 0);
	const int32 RankB = B.bIsPlayingThisGame ? 2 : (B.bIsOnline ? 1 : 0);
	if (RankA != RankB)
	{
		return RankA > RankB;
	}
	return A.DisplayName.Compare(B.DisplayName, ESearchCase::IgnoreCase) < 0;
}

void FEasySessionReadFriendsRequest::SortFriends(TArray<FEasySessionFriend>& InFriends)
{
	InFriends.StableSort(&FEasySessionReadFriendsRequest::FriendComesFirst);
}

void FEasySessionReadFriendsRequest::Execute()
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineFriendsPtr FriendsInterface = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
	if (!FriendsInterface.IsValid())
	{
		Complete(EEasySessionResult::NotSupportedByService, EasySession::NoFriendsListMessage);
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Reading the friends list."));

	// A refused call can complete the request inside it, before it returns.
	// Only a false return while the request is still running is a failure.
	if (!FriendsInterface->ReadFriendsList(0, EFriendsLists::ToString(EFriendsLists::Default),
		FOnReadFriendsListComplete::CreateSP(this, &FEasySessionReadFriendsRequest::HandleReadFriendsListComplete)) && IsRunning())
	{
		Complete(EEasySessionResult::UnknownFailure, TEXT("ReadFriendsList request was rejected by the online subsystem."));
	}
}

void FEasySessionReadFriendsRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage, Friends);
}

void FEasySessionReadFriendsRequest::HandleReadFriendsListComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
{
	if (!IsRunning())
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::UnknownFailure, ErrorStr);
		return;
	}

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineFriendsPtr FriendsInterface = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
	TArray<TSharedRef<FOnlineFriend>> FriendList;
	if (FriendsInterface.IsValid())
	{
		FriendsInterface->GetFriendsList(0, ListName, FriendList);
	}

	Friends.Reserve(FriendList.Num());
	for (const TSharedRef<FOnlineFriend>& OnlineFriend : FriendList)
	{
		FEasySessionFriend& Friend = Friends.AddDefaulted_GetRef();
		Friend.DisplayName = OnlineFriend->GetDisplayName();
		Friend.bIsOnline = OnlineFriend->GetPresence().bIsOnline;
		Friend.bIsPlayingThisGame = OnlineFriend->GetPresence().bIsPlayingThisGame;
		Friend.NativeId = FUniqueNetIdRepl(OnlineFriend->GetUserId());
	}
	SortFriends(Friends);

	UE_LOG(LogEasySession, Log, TEXT("Friends list read: %d friend(s)."), Friends.Num());
	Complete(EEasySessionResult::Success);
}
