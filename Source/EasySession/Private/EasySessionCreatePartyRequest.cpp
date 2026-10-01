// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionCreatePartyRequest.h"

#include "EasySession.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionCreatePartyRequest::FEasySessionCreatePartyRequest(const FEasyPartyParams& InPartyParams, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::CreateParty)
	, PartyParams(InPartyParams)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionCreatePartyRequest::Execute()
{
	if (!PartyParams.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Max Members must be at least 2."));
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
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("Already in a party. Call Leave Easy Party first."));
		return;
	}

	// A party closes when its players enter a game session, so none is created inside one.
	if (GetContext().Subsystem.IsInSession())
	{
		Complete(EEasySessionResult::SessionAlreadyExists, TEXT("A party is created outside a game session. Call Leave Easy Session first."));
		return;
	}

	// Members are told apart by their ids, so the leader needs one.
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
	const FUniqueNetIdRepl LeaderId(Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr);
	if (!LeaderId.IsValid())
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("No player is logged in to the online subsystem, so the party would have no leader."));
		return;
	}

	const FOnlineSessionSettings Settings = MakePartySettings(PartyParams, ShouldForceLAN(), LeaderId, Identity->GetPlayerNickname(0));

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateSP(this, &FEasySessionCreatePartyRequest::HandleCreateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Creating a party (MaxMembers=%d, LAN=%d)"), PartyParams.MaxMembers, Settings.bIsLANMatch ? 1 : 0);

	if (!Sessions->CreateSession(0, SessionName, Settings))
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("CreateSession request was rejected by the online subsystem."));
	}
}

void FEasySessionCreatePartyRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
}

void FEasySessionCreatePartyRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

FOnlineSessionSettings FEasySessionCreatePartyRequest::MakePartySettings(const FEasyPartyParams& Params, bool bForceLAN, const FUniqueNetIdRepl& LeaderId, const FString& LeaderName)
{
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = Params.MaxMembers;
	Settings.bIsLANMatch = bForceLAN;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;

	// Steam keeps a party in a lobby, which needs presence, and it treats these two flags as one.
	Settings.bUsesPresence = !Settings.bIsLANMatch;
	Settings.bUseLobbiesIfAvailable = Settings.bUsesPresence;

	// Every party is advertised, so a member can find the leader's party again after a match.
	// The hidden key keeps the parties that are not public out of Find Easy Parties.
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinViaPresence = Settings.bUsesPresence && Params.Privacy == EEasyPartyPrivacy::Public;

	Settings.Set(EasySession::SettingKey_Party, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(EasySession::SettingKey_DisplayName, LeaderName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(EasySession::SettingKey_Hidden, Params.Privacy == EEasyPartyPrivacy::Public ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(EasySession::SettingKey_OwnerId, LeaderId.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	if (Params.Privacy == EEasyPartyPrivacy::JoinCode)
	{
		Settings.Set(EasySession::SettingKey_JoinCode, EasySession::GenerateJoinCode(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// The party beacon registers on the same listener as the reservation beacon, so both use one port.
	Settings.Set(SETTING_BEACONPORT, EasySession::GetReservationBeaconPort(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	return Settings;
}

void FEasySessionCreatePartyRequest::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::CreateFailure, TEXT("The online subsystem failed to create the party session."));
		return;
	}

	// This process created the party, so it leads it. NULL already sets bHosting in CreateSession, but Steam never does.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr)
	{
		NamedSession->bHosting = true;
	}

	if (!GetContext().Party.StartHosting(PartyParams.MaxMembers))
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
