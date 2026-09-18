// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionCreateRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"

FEasySessionCreateRequest::FEasySessionCreateRequest(const FEasySessionHostParams& InHostParams, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Create)
	, HostParams(InHostParams)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionCreateRequest::Execute()
{
	if (!HostParams.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, EasySession::InvalidHostParamsMessage);
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it first."));
		return;
	}

	const FOnlineSessionSettings Settings = MakeSessionSettings(HostParams, GetContext().ShouldForceLAN());

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateSP(this, &FEasySessionCreateRequest::HandleCreateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Creating session '%s' (MaxPlayers=%d, LAN=%d)"),
		*HostParams.SessionDisplayName,
		HostParams.MaxPlayers,
		Settings.bIsLANMatch ? 1 : 0);

	if (!Sessions->CreateSession(0, SessionName, Settings))
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("CreateSession request was rejected by the online subsystem."));
	}
}

void FEasySessionCreateRequest::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsActive() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("The online subsystem failed to create the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session created successfully."));

	GetContext().Host.OnSessionCreated(HostParams);

	// Requested before the request completes, so Is Busy is already true for the travel when the completion delegate fires.
	GetContext().Travel.TravelToOwnSession(HostParams);

	Complete(EEasySessionResult::Success);
}

void FEasySessionCreateRequest::Cleanup(bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}

	if (bAbandoned)
	{
		DestroySessionLeftBehind();
	}
}

void FEasySessionCreateRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
	GetContext().Subsystem.OnSessionCreated.Broadcast(Result, ErrorMessage);
}

FOnlineSessionSettings FEasySessionCreateRequest::MakeSessionSettings(const FEasySessionHostParams& Params, bool bForceLAN)
{
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = Params.MaxPlayers;
	Settings.bIsLANMatch = Params.bIsLANMatch || bForceLAN;
	Settings.bShouldAdvertise = Params.bShouldAdvertise;
	Settings.bAllowJoinInProgress = Params.bAllowJoinInProgress;
	Settings.bAllowInvites = Params.bAllowInvites;
	Settings.bUsesPresence = !Settings.bIsLANMatch && Params.bUsePresence;
	Settings.bAllowJoinViaPresence = Settings.bUsesPresence;
	Settings.bUseLobbiesIfAvailable = Settings.bUsesPresence;

	// The title a session browser lists. The online subsystem's own name field is the host account.
	Settings.Set(EasySession::SettingKey_DisplayName, Params.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether Find skips this session.
	// Written even when false, because an advertised key cannot be deleted later.
	Settings.Set(EasySession::SettingKey_Hidden, Params.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether joining needs a password.
	// The joining game reads it back as FEasySessionSearchResult::bPasswordProtected and asks the player for one before joining.
	// Trimmed like the server gate's copy, because a whitespace-only password enforces nothing.
	Settings.Set(EasySession::SettingKey_PasswordProtected, Params.Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether the match is running.
	// The session state never leaves the host, so searches read this key instead.
	// Start and End update it.
	Settings.Set(EasySession::SettingKey_MatchInProgress, 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The advertised region.
	// Written even at Any, because an advertised key cannot be deleted later.
	Settings.Set(EasySession::SettingKey_Region, static_cast<int32>(Params.Region), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The session's join code.
	// Generated rather than chosen, because a join code has to be short and unambiguous.
	if (Params.bUseJoinCode)
	{
		Settings.Set(EasySession::SettingKey_JoinCode, EasySession::GenerateJoinCode(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// The port joining players reach the join approval beacon on.
	// Read from config rather than from a running beacon, because none exists yet.
	// One is created per world, after each travel.
	// GetResolvedConnectString reads this key to build the beacon address.
	Settings.Set(SETTING_BEACONPORT, EasySession::GetJoinApprovalBeaconPort(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether this host runs a join approval beacon.
	// Joining players that find the key request join approval before traveling.
	// The host reads the key back after each travel to decide whether the new world needs a beacon.
	Settings.Set(EasySession::SettingKey_JoinApproval, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	for (const TPair<FString, FString>& Custom : Params.CustomSettings)
	{
		const FName Key(*Custom.Key);
		if (EasySession::IsReservedSettingKey(Key))
		{
			UE_LOG(LogEasySession, Warning, TEXT("Custom Setting '%s' is a key this plugin uses for itself. It was not written."), *Custom.Key);
			continue;
		}

		Settings.Set(Key, Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	return Settings;
}
