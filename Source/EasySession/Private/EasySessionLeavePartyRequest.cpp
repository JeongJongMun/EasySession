// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionLeavePartyRequest.h"

#include "EasySession.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionMessages.h"
#include "EasySessionParty.h"

FEasySessionLeavePartyRequest::FEasySessionLeavePartyRequest(FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::LeaveParty)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionLeavePartyRequest::Execute()
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

	UE_LOG(LogEasySession, Log, TEXT("Leaving the party."));

	// The beacon closes first, so no member can log in to a party whose session is going away.
	GetContext().Party.Close();

	RunSubRequest(MakeShared<FEasySessionDestroyRequest>(
		FEasySessionCompleteDelegate::CreateSP(this, &FEasySessionLeavePartyRequest::HandleDestroyComplete)));
}

void FEasySessionLeavePartyRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionLeavePartyRequest::HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage)
{
	Complete(Result, ErrorMessage);
}
