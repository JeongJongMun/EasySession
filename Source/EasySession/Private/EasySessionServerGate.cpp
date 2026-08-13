// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionServerGate.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionServerGate::~FEasySessionServerGate()
{
	Shutdown();
}

void FEasySessionServerGate::Initialize()
{
	PreLoginHandle = FGameModeEvents::GameModePreLoginEvent.AddRaw(this, &FEasySessionServerGate::HandlePreLogin);
}

void FEasySessionServerGate::Shutdown()
{
	if (PreLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePreLoginEvent.Remove(PreLoginHandle);
		PreLoginHandle.Reset();
	}

	ClearSessionCredentials();
}

void FEasySessionServerGate::SetSessionCredentials(const FString& InPassword, bool bInFriendsBypassPassword)
{
	SessionPassword = InPassword;
	bFriendsBypassPassword = bInFriendsBypassPassword;
}

void FEasySessionServerGate::ClearSessionCredentials()
{
	SessionPassword.Empty();
	bFriendsBypassPassword = false;
}

bool FEasySessionServerGate::IsOwnWorld(const AGameModeBase* GameMode) const
{
	const UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	return GameMode != nullptr && OwnWorld != nullptr && GameMode->GetWorld() == OwnWorld;
}

EEasyJoinApprovalResult FEasySessionServerGate::ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& SuppliedPassword, FString& OutReason) const
{
	UWorld* OwnWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;

	// Searching already hides a started session, but a result fetched before the match
	// started and a direct connect both get past that - so the host rejects here too.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(OwnWorld);
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession != nullptr && !NamedSession->SessionSettings.bAllowJoinInProgress)
	{
		const EEasySessionState LocalState = Owner.GetLocalSessionState();
		if (LocalState == EEasySessionState::Starting || LocalState == EEasySessionState::InProgress)
		{
			UE_LOG(LogEasySession, Warning, TEXT("ServerGate: refusing '%s' - the match is in progress and join-in-progress is disabled."), *PlayerId.ToString());
			OutReason = NSLOCTEXT("EasySession", "MatchInProgress", "The match is already in progress.").ToString();
			return EEasyJoinApprovalResult::Refused;
		}
	}

	if (SessionPassword.IsEmpty())
	{
		return EEasyJoinApprovalResult::Approved;
	}

	if (SuppliedPassword.TrimStartAndEnd().Equals(SessionPassword, ESearchCase::CaseSensitive))
	{
		return EEasyJoinApprovalResult::Approved;
	}

	// Invited players arrive without the password, and invites only go to friends -
	// so being a friend of the host counts as knowing it.
	if (bFriendsBypassPassword && PlayerId.IsValid())
	{
		const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(OwnWorld);
		const IOnlineFriendsPtr Friends = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
		if (Friends.IsValid() && Friends->IsFriend(0, *PlayerId.GetUniqueNetId(), EFriendsLists::ToString(EFriendsLists::Default)))
		{
			UE_LOG(LogEasySession, Log, TEXT("ServerGate: '%s' joins without the password - friend of the host."), *PlayerId.ToString());
			return EEasyJoinApprovalResult::Approved;
		}
	}

	// Never log the password: on a listen server the log file sits on a player's
	// machine. Logging which kind of failure happened is enough.
	UE_LOG(LogEasySession, Warning, TEXT("ServerGate: refusing '%s' - %s."),
		*PlayerId.ToString(),
		SuppliedPassword.IsEmpty()
			? TEXT("no session password was supplied")
			: TEXT("the supplied session password did not match"));
	OutReason = NSLOCTEXT("EasySession", "WrongPassword", "Wrong session password.").ToString();
	return EEasyJoinApprovalResult::WrongPassword;
}

void FEasySessionServerGate::HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage)
{
	// Fires on the server only (game modes do not exist on clients).
	if (!IsOwnWorld(GameMode))
	{
		return;
	}

	// Another handler may already be rejecting this player - do not overwrite the reason.
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	UWorld* OwnWorld = Owner.GetGameInstance()->GetWorld();

	// The password arrives in the travel URL. The engine sets Connection->PlayerId before
	// PreLogin, so the joiner's connection can be found by id and its URL read. In-session
	// map changes must use seamless travel, or players already in would be rejected here.
	FString SuppliedPassword;
	if (!SessionPassword.IsEmpty())
	{
		const UNetConnection* PendingConnection = nullptr;
		if (const UNetDriver* NetDriver = OwnWorld->GetNetDriver())
		{
			for (const TObjectPtr<UNetConnection>& ClientConnection : NetDriver->ClientConnections)
			{
				if (ClientConnection != nullptr && ClientConnection->PlayerId == NewPlayer)
				{
					PendingConnection = ClientConnection;
					break;
				}
			}
		}

		if (PendingConnection == nullptr)
		{
			UE_LOG(LogEasySession, Warning, TEXT("PreLogin: could not find the pending connection for '%s'. Rejecting to protect the password session."), *NewPlayer.ToString());
			ErrorMessage = NSLOCTEXT("EasySession", "PasswordVerifyFailed", "Could not verify the session password.").ToString();
			return;
		}

		SuppliedPassword = EasySessionAddress::DecodeTravelOptionValue(
			EasySessionAddress::ParseTravelOption(PendingConnection->RequestURL, EasySession::TravelOption_Password));
	}

	FString Reason;
	if (ApproveJoin(NewPlayer, SuppliedPassword, Reason) != EEasyJoinApprovalResult::Approved)
	{
		ErrorMessage = Reason;
	}
}
