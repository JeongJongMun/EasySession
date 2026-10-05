// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSocial.h"

#include "EasySession.h"
#include "EasySessionConfig.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionSocial::~FEasySessionSocial()
{
	Shutdown();
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

EEasySessionResult FEasySessionSocial::SendInviteToFriend(const FEasySessionFriend& Friend, FName SessionName)
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend: there is no online subsystem to send through."));
		return EEasySessionResult::NoOnlineSubsystem;
	}

	if (!Friend.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend: the friend to invite is not a friend Read Easy Friends returned."));
		return EEasySessionResult::InvalidParams;
	}

	if (Sessions->GetNamedSession(SessionName) == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend: there is no %s to invite to."), SessionName == NAME_PartySession ? TEXT("party") : TEXT("session"));
		return EEasySessionResult::NoSessionExists;
	}

	if (!Sessions->SendSessionInviteToFriend(0, SessionName, *Friend.NativeId.GetUniqueNetId()))
	{
		UE_LOG(LogEasySession, Warning, TEXT("SendSessionInviteToFriend was refused. The online subsystem may not support invites (e.g. NULL/LAN)."));
		return EEasySessionResult::NotSupportedByService;
	}

	UE_LOG(LogEasySession, Log, TEXT("%s invite sent to '%s'."), SessionName == NAME_PartySession ? TEXT("Party") : TEXT("Session"), *Friend.DisplayName);
	return EEasySessionResult::Success;
}

EEasySessionResult FEasySessionSocial::ShowInviteUI(FName SessionName) const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineExternalUIPtr ExternalUI = OnlineSub ? OnlineSub->GetExternalUIInterface() : nullptr;

	if (!ExternalUI.IsValid() || !ExternalUI->ShowInviteUI(0, SessionName))
	{
		UE_LOG(LogEasySession, Warning, TEXT("ShowInviteUI is not supported by the current online subsystem (e.g. NULL/LAN)."));
		return EEasySessionResult::NotSupportedByService;
	}

	return EEasySessionResult::Success;
}

EEasySessionResult FEasySessionSocial::ShowProfileUI(const FUniqueNetIdPtr& TargetId) const
{
	if (!TargetId.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("ShowProfileUI: the player to show has no online id."));
		return EEasySessionResult::InvalidParams;
	}

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineExternalUIPtr ExternalUI = OnlineSub ? OnlineSub->GetExternalUIInterface() : nullptr;
	const IOnlineIdentityPtr Identity = OnlineSub ? OnlineSub->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;

	if (!ExternalUI.IsValid() || !LocalId.IsValid() || !ExternalUI->ShowProfileUI(*LocalId, *TargetId, FOnProfileUIClosedDelegate()))
	{
		UE_LOG(LogEasySession, Warning, TEXT("ShowProfileUI is not supported by the current online subsystem (e.g. NULL/LAN)."));
		return EEasySessionResult::NotSupportedByService;
	}

	return EEasySessionResult::Success;
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

	if (!GetDefault<UEasySessionConfig>()->bAutoJoinAcceptedInvites)
	{
		return;
	}

	// The invite event carries no session name, so the advertised party key tells the two kinds apart.
	if (Session.bIsParty)
	{
		JoinInvitedParty(Session);
		return;
	}

	// One click in the overlay must not destroy the session this player is in, unless the project allows it.
	if (Owner.IsInSession() && !GetDefault<UEasySessionConfig>()->bAcceptInvitesWhileInSession)
	{
		const FString Reason = TEXT("Not joining the invited session: this player is already in one, and Accept Invites While In Session is disabled.");
		UE_LOG(LogEasySession, Warning, TEXT("%s"), *Reason);
		Owner.OnSessionFailure.Broadcast(Reason);
		return;
	}

	if (!CancelMatchmakingForInvite())
	{
		return;
	}

	// No node waits for this join, so a failure is broadcast.
	UEasySessionSubsystem* OwnerSub = &Owner;
	Owner.JoinSession(Session, FString(), FString(), FEasySessionCompleteDelegate::CreateWeakLambda(OwnerSub,
		[OwnerSub](EEasySessionResult Result, const FString& ErrorMessage)
		{
			if (Result != EEasySessionResult::Success)
			{
				OwnerSub->OnSessionFailure.Broadcast(FString::Printf(TEXT("Joining the invited session failed: %s"), *ErrorMessage));
			}
		}));
}

void FEasySessionSocial::JoinInvitedParty(const FEasySessionSearchResult& Party)
{
	// A party lives outside game sessions, and one click must not end the match this player is in, so the game decides.
	if (Owner.IsInSession())
	{
		UE_LOG(LogEasySession, Log, TEXT("Not joining the invited party during a game session. Call Leave Easy Session, then Join Easy Party with the invite's session."));
		return;
	}

	if (!CancelMatchmakingForInvite())
	{
		return;
	}

	// The queue runs the LeaveParty request first, so the join finds this player in no party.
	if (Owner.IsInParty())
	{
		Owner.LeaveParty();
	}

	// No node waits for this join, so a failure is broadcast.
	UEasySessionSubsystem* OwnerSub = &Owner;
	Owner.JoinParty(Party, FEasySessionCompleteDelegate::CreateWeakLambda(OwnerSub,
		[OwnerSub](EEasySessionResult Result, const FString& ErrorMessage)
		{
			if (Result != EEasySessionResult::Success)
			{
				OwnerSub->OnSessionFailure.Broadcast(FString::Printf(TEXT("Joining the invited party failed: %s"), *ErrorMessage));
			}
		}));
}

bool FEasySessionSocial::CancelMatchmakingForInvite()
{
	Owner.CancelMatchmaking();
	if (!Owner.IsMatchmakingRunning())
	{
		return true;
	}

	// The run ignored the cancel, and it ends with this player in the session it joins or hosts.
	const FString Reason = TEXT("Matchmaking is already joining or hosting a session, so the invite was not joined. Accept the invite again to join it.");
	UE_LOG(LogEasySession, Warning, TEXT("%s"), *Reason);
	Owner.OnSessionFailure.Broadcast(Reason);
	return false;
}

UWorld* FEasySessionSocial::GetWorld() const
{
	return Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
}
