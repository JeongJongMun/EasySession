// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionCreateRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionMessages.h"
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
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a session. Call Leave Easy Session first. A host changes maps with Server Travel Easy Session."));
		return;
	}

	const FOnlineSessionSettings Settings = MakeSessionSettings(HostParams, ShouldForceLAN());

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

void FEasySessionCreateRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
}

void FEasySessionCreateRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

FOnlineSessionSettings FEasySessionCreateRequest::MakeSessionSettings(const FEasySessionHostParams& Params, bool bForceLAN)
{
	FOnlineSessionSettings Settings;
	Params.ApplyTo(Settings);

	Settings.bIsLANMatch = Params.bIsLANMatch || bForceLAN;
	Settings.bUsesPresence = !Settings.bIsLANMatch && Params.bUsePresence;
	Settings.bAllowJoinViaPresence = Settings.bUsesPresence;
	Settings.bUseLobbiesIfAvailable = Settings.bUsesPresence;

	// Whether the match is running.
	// The session state never leaves the host, so searches read this key instead.
	// Start and End update it.
	Settings.Set(EasySession::SettingKey_MatchInProgress, 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The port joining players reach the reservation beacon on.
	// Read from config rather than from a running beacon, because none exists yet.
	// One is created per world, after each travel.
	// GetResolvedConnectString reads this key to build the beacon address.
	Settings.Set(SETTING_BEACONPORT, EasySession::GetReservationBeaconPort(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether this host runs a reservation beacon.
	// Joining players that find the key ask it for a reservation before traveling.
	// The host reads the key back after each travel to decide whether the new world needs a beacon.
	Settings.Set(EasySession::SettingKey_Reservations, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	return Settings;
}

void FEasySessionCreateRequest::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsRunning() || InSessionName != SessionName)
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
