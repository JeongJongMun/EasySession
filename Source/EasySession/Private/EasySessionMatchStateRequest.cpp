// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionMatchStateRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionMessages.h"
#include "EasySessionSubsystem.h"
#include "OnlineSessionSettings.h"

FEasySessionMatchStateRequest::FEasySessionMatchStateRequest(EType InType, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(InType)
	, OnComplete(MoveTemp(InOnComplete))
{
	check(InType == EType::Start || InType == EType::End);
}

void FEasySessionMatchStateRequest::Execute()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	if (Sessions->GetNamedSession(SessionName) == nullptr)
	{
		Complete(EEasySessionResult::NoSessionExists, IsStart() ? TEXT("There is no session to start.") : TEXT("There is no session to end."));
		return;
	}

	if (!GetContext().Subsystem.IsSessionAuthority())
	{
		Complete(EEasySessionResult::RequiresSessionAuthority, EasySession::RequiresSessionAuthorityMessage);
		return;
	}

	bool bAccepted = false;
	if (IsStart())
	{
		StateChangeCompleteHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(
			FOnStartSessionCompleteDelegate::CreateSP(this, &FEasySessionMatchStateRequest::HandleStateChangeComplete));

		UE_LOG(LogEasySession, Log, TEXT("Starting session."));
		bAccepted = Sessions->StartSession(SessionName);
	}
	else
	{
		StateChangeCompleteHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(
			FOnEndSessionCompleteDelegate::CreateSP(this, &FEasySessionMatchStateRequest::HandleStateChangeComplete));

		UE_LOG(LogEasySession, Log, TEXT("Ending session."));
		bAccepted = Sessions->EndSession(SessionName);
	}

	if (!bAccepted)
	{
		Complete(EEasySessionResult::StateChangeFailure, IsStart()
			? TEXT("StartSession request was rejected by the online subsystem. The session may already be in progress.")
			: TEXT("EndSession request was rejected by the online subsystem. The session may not be in progress."));
	}
}

void FEasySessionMatchStateRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	if (IsStart())
	{
		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StateChangeCompleteHandle);
	}
	else
	{
		Sessions->ClearOnEndSessionCompleteDelegate_Handle(StateChangeCompleteHandle);
	}
	Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(AdvertiseCompleteHandle);
}

void FEasySessionMatchStateRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionMatchStateRequest::HandleStateChangeComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::StateChangeFailure, IsStart()
			? TEXT("The online subsystem failed to start the session.")
			: TEXT("The online subsystem failed to end the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("%s"), IsStart() ? TEXT("Session started.") : TEXT("Session ended."));

	// NULL completes the update inside the call, which already finished this request.
	// Only a refusal with the request still running is left to complete here.
	if (!AdvertiseMatchInProgress() && IsRunning())
	{
		CompleteStateChange(false);
	}
}

bool FEasySessionMatchStateRequest::AdvertiseMatchInProgress()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr;
	if (NamedSession == nullptr)
	{
		return false;
	}

	AdvertiseCompleteHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateSP(this, &FEasySessionMatchStateRequest::HandleAdvertiseComplete));

	NamedSession->SessionSettings.Set(EasySession::SettingKey_MatchInProgress, IsStart() ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	return Sessions->UpdateSession(SessionName, NamedSession->SessionSettings, true);
}

void FEasySessionMatchStateRequest::HandleAdvertiseComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	CompleteStateChange(bWasSuccessful);
}

void FEasySessionMatchStateRequest::CompleteStateChange(bool bAdvertised)
{
	// The match state itself already changed, so a refused re-advertise is a stale advertisement, not a failed Start or End.
	if (!bAdvertised)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The match state changed, but advertising it failed. Searching players see the old value until the next update."));
	}

	GetContext().Host.OnMatchStateChanged();

	Complete(EEasySessionResult::Success);
}
