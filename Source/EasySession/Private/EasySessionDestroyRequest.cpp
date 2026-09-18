// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionDestroyRequest.h"

#include "EasySession.h"
#include "EasySessionHost.h"
#include "EasySessionSubsystem.h"

FEasySessionDestroyRequest::FEasySessionDestroyRequest(FEasySessionCompleteDelegate InOnComplete)
	: FEasySessionRequest(EType::Destroy)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FEasySessionDestroyRequest::Execute()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		Complete(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(SessionName) == nullptr)
	{
		Complete(EEasySessionResult::NoSessionExists, TEXT("There is no session to destroy."));
		return;
	}

	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateSP(this, &FEasySessionDestroyRequest::HandleDestroySessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Destroying session."));

	if (!Sessions->DestroySession(SessionName))
	{
		Complete(EEasySessionResult::DestroyFailure, TEXT("DestroySession request was rejected by the online subsystem."));
	}
}

void FEasySessionDestroyRequest::HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (!IsActive() || InSessionName != SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		Complete(EEasySessionResult::DestroyFailure, TEXT("The online subsystem failed to destroy the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session destroyed successfully."));

	GetContext().Host.OnSessionDestroyed();

	// A client also clears the host state it received through replication.
	GetContext().Subsystem.ClearReplicatedHostSessionState();

	Complete(EEasySessionResult::Success);
}

void FEasySessionDestroyRequest::Cleanup(bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
}

void FEasySessionDestroyRequest::Notify(EEasySessionResult Result, const FString& ErrorMessage)
{
	OnComplete.ExecuteIfBound(Result, ErrorMessage);
	GetContext().Subsystem.OnSessionDestroyed.Broadcast(Result, ErrorMessage);
}
