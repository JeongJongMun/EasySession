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
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

FEasySessionServerGate::~FEasySessionServerGate()
{
	Shutdown();
}

void FEasySessionServerGate::Initialize()
{
	PreLoginHandle = FGameModeEvents::GameModePreLoginEvent.AddRaw(this, &FEasySessionServerGate::HandlePreLogin);
	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddRaw(this, &FEasySessionServerGate::HandlePostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &FEasySessionServerGate::HandleLogout);
}

void FEasySessionServerGate::Shutdown()
{
	if (PreLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePreLoginEvent.Remove(PreLoginHandle);
		PreLoginHandle.Reset();
	}

	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
		PostLoginHandle.Reset();
	}

	if (LogoutHandle.IsValid())
	{
		FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
		LogoutHandle.Reset();
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

	// Join-in-progress gate. Searching already hides a started session - NULL
	// refuses to answer LAN queries for one (IsSessionJoinable, called from
	// OnValidQueryPacketReceived in OnlineSessionInterfaceNull.cpp) - but that
	// leaves two ways in: a result fetched before the match started still joins
	// fine (a NULL join never asks the host), and a direct connect skips searching
	// entirely. The host is the authority on its own session and rejects here.
	// New connections only: in-session map changes must use seamless travel (the
	// plugin's own travels do), or reconnecting players would be caught by this
	// gate too.
	const IOnlineSessionPtr PreLoginSessions = Online::GetSessionInterface(OwnWorld);
	const FNamedOnlineSession* PreLoginSession = PreLoginSessions.IsValid() ? PreLoginSessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (PreLoginSession != nullptr && !PreLoginSession->SessionSettings.bAllowJoinInProgress)
	{
		const EEasySessionState LocalState = Owner.GetLocalSessionState();
		if (LocalState == EEasySessionState::Starting || LocalState == EEasySessionState::InProgress)
		{
			UE_LOG(LogEasySession, Warning, TEXT("PreLogin: rejecting '%s' - the match is in progress and join-in-progress is disabled."), *NewPlayer.ToString());
			ErrorMessage = NSLOCTEXT("EasySession", "MatchInProgress", "The match is already in progress.").ToString();
			return;
		}
	}

	if (SessionPassword.IsEmpty())
	{
		return;
	}

	// The engine sets Connection->PlayerId before PreLogin, so the pending
	// connection can be found by id and its travel URL inspected.
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

	const FString SuppliedPassword = EasySessionAddress::DecodeTravelOptionValue(
		EasySessionAddress::ParseTravelOption(PendingConnection->RequestURL, EasySession::TravelOption_Password));

	if (!SuppliedPassword.Equals(SessionPassword, ESearchCase::CaseSensitive))
	{
		// Invited players never carry the password (the invite flow has no password prompt),
		// but platform invites can only be sent to friends - so a host-side friends check
		// lets them in. Platform-verified: the joining id cannot fake being a friend.
		if (bFriendsBypassPassword && NewPlayer.IsValid())
		{
			const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(OwnWorld);
			const IOnlineFriendsPtr Friends = OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
			if (Friends.IsValid() && Friends->IsFriend(0, *NewPlayer.GetUniqueNetId(), EFriendsLists::ToString(EFriendsLists::Default)))
			{
				UE_LOG(LogEasySession, Log, TEXT("PreLogin: '%s' joins without the password - friend of the host."), *NewPlayer.ToString());
				return;
			}
		}

		// The password and the URL carrying it stay out of the log - on a listen
		// server that file belongs to a player. Which failure it was is enough.
		UE_LOG(LogEasySession, Warning, TEXT("PreLogin: rejecting '%s' - %s."),
			*NewPlayer.ToString(),
			SuppliedPassword.IsEmpty()
				? TEXT("no session password was supplied")
				: TEXT("the supplied session password did not match"));
		ErrorMessage = NSLOCTEXT("EasySession", "WrongPassword", "Wrong session password.").ToString();
	}
}

void FEasySessionServerGate::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	// Fires on the server only (game modes do not exist on clients).
	if (!IsOwnWorld(GameMode) || NewPlayer == nullptr)
	{
		return;
	}

	if (NewPlayer->IsLocalController())
	{
		// The hosting player registers itself right after creating the session.
		return;
	}

	// Register the remote player so the advertised open slot count stays accurate.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(Owner.GetGameInstance()->GetWorld());
	const APlayerState* PlayerState = NewPlayer->PlayerState;
	const FUniqueNetIdRepl PlayerId = PlayerState ? PlayerState->GetUniqueId() : FUniqueNetIdRepl();

	if (Sessions.IsValid() && PlayerId.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		if (Sessions->RegisterPlayers(NAME_GameSession, { PlayerId.GetUniqueNetId().ToSharedRef() }))
		{
			UE_LOG(LogEasySession, Log, TEXT("Registered remote player '%s' in the session."), *NewPlayer->GetName());
		}
	}
}

void FEasySessionServerGate::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	// Fires on the server only. Unregister remote players so their slot frees up.
	if (!IsOwnWorld(GameMode) || Exiting == nullptr || Exiting->IsLocalController())
	{
		return;
	}

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(Owner.GetGameInstance()->GetWorld());
	const APlayerState* PlayerState = Exiting->PlayerState;
	const FUniqueNetIdRepl PlayerId = PlayerState ? PlayerState->GetUniqueId() : FUniqueNetIdRepl();

	if (Sessions.IsValid() && PlayerId.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		if (Sessions->UnregisterPlayers(NAME_GameSession, { PlayerId.GetUniqueNetId().ToSharedRef() }))
		{
			UE_LOG(LogEasySession, Log, TEXT("Unregistered remote player '%s' from the session."), *Exiting->GetName());
		}
	}
}
