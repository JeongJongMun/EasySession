// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionLeaveRequest.h"

#include "EasySession.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionHost.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystemUtils.h"

FEasySessionLeaveRequest::FEasySessionLeaveRequest(const FText& InReason, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionLeaveRequest(NAME_GameSession, MoveTemp(InOnComplete))
{
	ReasonText = InReason;
}

FEasySessionLeaveRequest::FEasySessionLeaveRequest(EEasyPartyLeaveReason InReason, const FText& InReasonText, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionLeaveRequest(NAME_PartySession, MoveTemp(InOnComplete))
{
	PartyReason = InReason;
	ReasonText = InReasonText;
}

FEasySessionLeaveRequest::FEasySessionLeaveRequest(FName InSessionName, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Leave, InSessionName)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionLeaveRequest::Execute()
{
	// A party that does not exist cannot be left.
	// A game session is left whether it still exists or not, so the player reaches the menu either way.
	// The destroy then reports NoSessionExists.
	if (IsPartyRequest())
	{
		const IOnlineSessionPtr Sessions = GetSessionInterface();
		if (!Sessions.IsValid())
		{
			Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
			return;
		}

		if (Sessions->GetNamedSession(SessionName) == nullptr)
		{
			Complete(EEasySessionResult::NoSessionExists, TEXT("Not in a party."));
			return;
		}
	}

	if (IsPartyRequest())
	{
		UE_LOG(LogEasySession, Log, TEXT("Leaving the party."));

		// The beacon closes first, so no member can log in to a party whose session is going away.
		GetContext().Party.Close();
	}
	else if (GetContext().Subsystem.IsHost())
	{
		UE_LOG(LogEasySession, Log, TEXT("Destroying the session for everyone: %s"), *ReasonText.ToString());

		// Every remote client receives the reason before the session is destroyed and its connection closes.
		GetContext().Host.TellEveryoneToReturnToMainMenu(ReasonText);
	}

	RunSubRequest(MakeShared<FEasySessionDestroyRequest>(
		FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionLeaveRequest::HandleDestroyComplete)));
}

void FEasySessionLeaveRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionLeaveRequest::HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	// The leave goes on whatever the destroy reported, because the player asked to leave.
	if (IsPartyRequest())
	{
		// The beacon is already closed, so the player is out of the party even when the session could not be destroyed.
		GetContext().Party.HandlePartyLeft(PartyReason, ReasonText);
	}
	else
	{
		// Requested before the completion below, the same order every travel in this plugin uses.
		GetContext().Travel.ReturnToMainMenu();
	}

	Complete(Result, ErrorMessage);
}
