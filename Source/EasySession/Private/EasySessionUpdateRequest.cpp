// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionUpdateRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionMessages.h"
#include "EasySessionSubsystem.h"
#include "OnlineSessionSettings.h"

FEasySessionUpdateRequest::FEasySessionUpdateRequest(const FEasySessionSettings& InSettings, FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Update)
	, Settings(InSettings)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionUpdateRequest::Execute()
{
	if (!Settings.IsValid())
	{
		Complete(EEasySessionResult::InvalidParams, TEXT("Session settings are invalid: Max Players must be above 0."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, EasySession::NoOnlineSubsystemMessage);
		return;
	}

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(SessionName);
	if (NamedSession == nullptr)
	{
		Complete(EEasySessionResult::NoSessionExists, TEXT("There is no session to update."));
		return;
	}

	if (!GetContext().Subsystem.IsSessionAuthority())
	{
		Complete(EEasySessionResult::RequiresSessionAuthority, EasySession::RequiresSessionAuthorityMessage);
		return;
	}

	// Only the password flag is advertised here.
	// The host takes the password itself once the update succeeds, so the two never differ if this request fails.
	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	Settings.ApplyTo(UpdatedSettings);

	UpdateCompleteHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateSP(this, &FEasySessionUpdateRequest::HandleUpdateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Updating session."));

	if (!Sessions->UpdateSession(SessionName, UpdatedSettings, true))
	{
		Complete(EEasySessionResult::UpdateFailure, TEXT("UpdateSession request was rejected by the online subsystem."));
	}
}

void FEasySessionUpdateRequest::Cleanup()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle);
	}
}

void FEasySessionUpdateRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
}

void FEasySessionUpdateRequest::HandleUpdateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsRunning() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::UpdateFailure, TEXT("The online subsystem failed to update the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session updated successfully."));

	// The host side gets the settings only now, so a refused update leaves it matching what is advertised.
	GetContext().Host.OnSettingsUpdated(Settings);

	Complete(EEasySessionResult::Success);
}
