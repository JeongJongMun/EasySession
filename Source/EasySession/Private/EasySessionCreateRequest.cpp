// Copyright (c) 2026 Langerak.

#include "EasySessionCreateRequest.h"

#include "EasySession.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionHost.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionCreateRequest::FEasySessionCreateRequest(const FEasySessionHostParams& InHostParams, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionCreateRequest(NAME_GameSession, MoveTemp(InOnComplete))
{
	HostParams = InHostParams;
}

FEasySessionCreateRequest::FEasySessionCreateRequest(const FEasySessionSettings& InSettings, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionCreateRequest(NAME_PartySession, MoveTemp(InOnComplete))
{
	// A party has no listen server, so the fields FEasySessionHostParams adds stay at their defaults: no map, not LAN, with presence.
	static_cast<FEasySessionSettings&>(HostParams) = InSettings;
}

FEasySessionCreateRequest::FEasySessionCreateRequest(FName InSessionName, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Create, InSessionName)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionCreateRequest::Execute()
{
	// A failed check completed the request with the refusal.
	if (!CheckCanCreate())
	{
		return;
	}

	const FOnlineSessionSettings Settings = MakeSessionSettings();

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateSP(this, &FEasySessionCreateRequest::HandleCreateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Creating %s '%s' (MaxPlayers=%d, LAN=%d)"), *SessionName.ToString(), *HostParams.SessionDisplayName, HostParams.MaxPlayers, Settings.bIsLANMatch ? 1 : 0);

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

bool FEasySessionCreateRequest::CheckCanCreate()
{
	if (!HostParams.FEasySessionSettings::IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Max Players must be above 0."));
		return false;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return false;
	}

	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		Complete(EEasySessionResult::SessionAlreadyExists, IsPartyRequest()
			? TEXT("Already in a party. Leave it with Leave Easy Session first.")
			: TEXT("Already in a session. Call Leave Easy Session first. A host changes maps with Server Travel Easy Session."));
		return false;
	}

	// The session is created for local player 0 in Execute, so a search for this host filters on that player's id.
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	OwnerId = FUniqueNetIdRepl(Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr);

	// A session with no name is named after the player who creates it.
	if (HostParams.SessionDisplayName.TrimStartAndEnd().IsEmpty())
	{
		HostParams.SessionDisplayName = Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString();
	}

	return IsPartyRequest() ? CheckCanCreatePartySession() : CheckCanCreateGameSession();
}

bool FEasySessionCreateRequest::CheckCanCreateGameSession()
{
	if (!HostParams.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, EasySession::InvalidHostParamsMessage);
		return false;
	}

	// A party host brings the party, and the host's reservation holds every member, so MaxPlayers must count every member and the host.
	GroupMembers = GetGroupMembers();
	if (HostParams.MaxPlayers < GroupMembers.Num() + 1)
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Max Players is smaller than the party."));
		return false;
	}

	return true;
}

bool FEasySessionCreateRequest::CheckCanCreatePartySession()
{
	// The party beacon approves members without a password, so a password would protect nothing.
	if (!HostParams.Password.TrimStartAndEnd().IsEmpty())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("A party has no password. Hide the party or share its join code instead."));
		return false;
	}

	// Members find the party again after a match by searching for its host, and a search returns only advertised sessions.
	if (!HostParams.bShouldAdvertise)
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Every party is advertised. Set Hidden to keep it out of a party search."));
		return false;
	}

	if (GetContext().Subsystem.IsInSession(EEasySessionType::GameSession))
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A party is created outside a game session. Call Leave Easy Session first."));
		return false;
	}

	if (!OwnerId.IsValid())
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("No player is logged in to the online subsystem, so the party would have no host."));
		return false;
	}

	return true;
}

FOnlineSessionSettings FEasySessionCreateRequest::MakeSessionSettings() const
{
	FOnlineSessionSettings Settings;
	HostParams.ApplyTo(Settings);

	// Steam keeps a presence session in a lobby, and it treats these flags as one.
	Settings.bIsLANMatch = HostParams.bIsLANMatch || ShouldForceLAN();
	Settings.bUsesPresence = !Settings.bIsLANMatch && HostParams.bUsePresence;
	Settings.bAllowJoinViaPresence = Settings.bUsesPresence;
	Settings.bUseLobbiesIfAvailable = Settings.bUsesPresence;

	Settings.Set(EasySession::SettingKey_SessionType, SessionName.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The party beacon registers on the reservation beacon's listener, so both kinds advertise one port.
	// Read from config rather than from a running beacon, because the host creates one per world after each travel.
	Settings.Set(SETTING_BEACONPORT, EasySession::GetReservationBeaconPort(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	if (OwnerId.IsValid())
	{
		Settings.Set(EasySession::SettingKey_OwnerId, OwnerId.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// A party runs no match and no reservation beacon, so only a game session advertises these two keys.
	if (SessionName == NAME_GameSession)
	{
		Settings.Set(EasySession::SettingKey_MatchInProgress, 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Settings.Set(EasySession::SettingKey_Reservations, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

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
		Complete(EEasySessionResult::CreateFailure, FString::Printf(TEXT("The online subsystem failed to create the %s."), *SessionName.ToString()));
		return;
	}

	if (IsPartyRequest())
	{
		HostPartySession();
	}
	else
	{
		HostGameSession();
	}
}

void FEasySessionCreateRequest::HostGameSession()
{
	UE_LOG(LogEasySession, Log, TEXT("Session created successfully."));

	GetContext().Host.OnSessionCreated(HostParams, GroupMembers);

	// The members search for this host until its map is open, so they are told before the travel.
	// A party exists only outside game sessions, so HandlePartyEnded queues a Leave request that destroys the party session.
	if (GetContext().Subsystem.IsInSession(EEasySessionType::PartySession))
	{
		if (!GroupMembers.IsEmpty())
		{
			UE_LOG(LogEasySession, Log, TEXT("Bringing %d party members to this session."), GroupMembers.Num());
			TellGroupToFollow(GroupMembers, OwnerId, HostParams.bIsLANMatch || ShouldForceLAN());
		}
		GetContext().Subsystem.HandlePartyEnded(EEasyPartyLeaveReason::MovedToGameSession, EasySession::GetPartyMovedReason());
	}

	// Requested before the request completes, so IsBusy is already true for the travel when the completion delegate fires.
	GetContext().Travel.TravelToOwnSession(HostParams);

	Complete(EEasySessionResult::Success);
}

void FEasySessionCreateRequest::HostPartySession()
{
	// This process created the party, so it hosts it.
	// NULL already sets bHosting in CreateSession, but Steam never does.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr)
	{
		NamedSession->bHosting = true;
	}

	if (!GetContext().Party.StartHosting(HostParams))
	{
		// The party session would advertise a party that nobody can join, so it is destroyed again.
		RunSubRequest(MakeShared<FEasySessionDestroyRequest>(FEasySessionCompleteDelegate::CreateSPLambda(this,
			[this](EEasySessionResult /*DestroyResult*/, const FString& /*DestroyError*/)
			{
				Complete(EEasySessionResult::CreateFailure, TEXT("The party beacon could not start, so the party was removed again."));
			})));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Party created."));
	Complete(EEasySessionResult::Success);
}
