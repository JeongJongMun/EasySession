// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSocial.h"

#include "EasySession.h"
#include "EasySessionSettings.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionSocial::~FEasySessionSocial()
{
	Shutdown();
}

UWorld* FEasySessionSocial::GetWorld() const
{
	return Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
}

void FEasySessionSocial::BindInviteDelegates()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid() || InviteAcceptedHandle.IsValid())
	{
		return;
	}

	InviteAcceptedHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateRaw(this, &FEasySessionSocial::HandleSessionUserInviteAccepted));
}

void FEasySessionSocial::Shutdown()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid())
	{
		if (InviteAcceptedHandle.IsValid())
		{
			Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
		}
	}

	InviteAcceptedHandle.Reset();
}

void FEasySessionSocial::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("An invite was accepted but its session data is not valid."));
		return;
	}

	const FEasySessionSearchResult Session = FEasySessionSearchResult::FromNative(InviteResult);
	UE_LOG(LogEasySession, Log, TEXT("Invite accepted for session '%s'."), *Session.SessionDisplayName);
	Owner.OnSessionInviteAccepted.Broadcast(Session);

	if (GetDefault<UEasySessionSettings>()->bAutoJoinAcceptedInvites)
	{
		JoinInvitedSession(Session);
	}
}

void FEasySessionSocial::JoinInvitedSession(const FEasySessionSearchResult& Session)
{
	// The request queue serializes these, so the destroy always finishes first.
	if (Owner.IsInSession())
	{
		Owner.DestroyEasySession();
	}

	Owner.JoinEasySession(Session, /*bTravelOnSuccess*/ true);
}

bool FEasySessionSocial::SendInviteToFriend(const FEasySessionFriend& Friend)
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid() || !Friend.IsValid())
	{
		return false;
	}

	if (!Owner.IsInSession())
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend: there is no session to invite to."));
		return false;
	}

	if (!Sessions->SendSessionInviteToFriend(0, NAME_GameSession, *Friend.NativeId))
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend failed. The online subsystem may not support invites (e.g. NULL/LAN)."));
		return false;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session invite sent to '%s'."), *Friend.DisplayName);
	return true;
}

bool FEasySessionSocial::ShowInviteUI() const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineExternalUIPtr ExternalUI = OnlineSub ? OnlineSub->GetExternalUIInterface() : nullptr;

	if (!ExternalUI.IsValid() || !ExternalUI->ShowInviteUI(0, NAME_GameSession))
	{
		UE_LOG(LogEasySession, Warning, TEXT("ShowInviteUI is not supported by the current online subsystem (e.g. NULL/LAN)."));
		return false;
	}

	return true;
}

bool FEasySessionSocial::ShowProfileUI(const FUniqueNetIdPtr& TargetId) const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineExternalUIPtr ExternalUI = OnlineSub ? OnlineSub->GetExternalUIInterface() : nullptr;
	const IOnlineIdentityPtr Identity = OnlineSub ? OnlineSub->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;

	if (!ExternalUI.IsValid() || !LocalId.IsValid() || !TargetId.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("ShowProfileUI is not supported by the current online subsystem (e.g. NULL/LAN)."));
		return false;
	}

	return ExternalUI->ShowProfileUI(*LocalId, *TargetId, FOnProfileUIClosedDelegate());
}

void FEasySessionSocial::ReadFriends(FEasyFriendsCompleteDelegate OnComplete)
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineFriendsPtr Friends = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;

	if (!Friends.IsValid())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::NoOnlineSubsystem, TEXT("The current online subsystem does not support friends lists (e.g. NULL/LAN)."), {});
		return;
	}
	
	const FString ListName = EFriendsLists::ToString(EFriendsLists::Default);

	// Bound to the owning UObject: the read can outlive the world it started in, and
	// a weak lambda drops the callback instead of writing into a destroyed subsystem.
	Friends->ReadFriendsList(0, ListName, FOnReadFriendsListComplete::CreateWeakLambda(&Owner,
		[this, UserDelegate = MoveTemp(OnComplete)](int32 /*LocalUserNum*/, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
		{
			if (!bWasSuccessful)
			{
				UserDelegate.ExecuteIfBound(EEasySessionResult::UnknownFailure, ErrorStr, {});
				return;
			}

			const IOnlineSubsystem* CallbackSub = Online::GetSubsystem(GetWorld());
			const IOnlineFriendsPtr CallbackFriends = CallbackSub ? CallbackSub->GetFriendsInterface() : nullptr;

			TArray<TSharedRef<FOnlineFriend>> FriendList;
			if (CallbackFriends.IsValid())
			{
				CallbackFriends->GetFriendsList(0, ListName, FriendList);
			}

			TArray<FEasySessionFriend> Result;
			Result.Reserve(FriendList.Num());
			for (const TSharedRef<FOnlineFriend>& OnlineFriend : FriendList)
			{
				FEasySessionFriend& Entry = Result.AddDefaulted_GetRef();
				Entry.DisplayName = OnlineFriend->GetDisplayName();
				Entry.bIsOnline = OnlineFriend->GetPresence().bIsOnline;
				Entry.bIsPlayingThisGame = OnlineFriend->GetPresence().bIsPlayingThisGame;
				Entry.NativeId = OnlineFriend->GetUserId();
			}

			UE_LOG(LogEasySession, Log, TEXT("Friends list read: %d friend(s)."), Result.Num());
			UserDelegate.ExecuteIfBound(EEasySessionResult::Success, FString(), Result);
		}));
}
