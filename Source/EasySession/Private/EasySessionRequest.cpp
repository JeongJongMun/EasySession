// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionRequest.h"

#include "EasySession.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

UWorld* FEasySessionRequestContext::GetWorld() const
{
	const UGameInstance* GameInstance = Subsystem.GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

bool FEasySessionRequestContext::ShouldForceLAN() const
{
	return Subsystem.GetOnlineSubsystemName() == NULL_SUBSYSTEM;
}

void FEasySessionRequest::Bind(FEasySessionRequestContext& InContext, FName InSessionName)
{
	Context = &InContext;
	SessionName = InSessionName;
}

void FEasySessionRequest::Start()
{
	check(Context != nullptr);
	Execute();
}

void FEasySessionRequest::Complete(EEasySessionResult Result, const FString& ErrorMessage, bool bAbandoned)
{
	if (!IsActive())
	{
		return;
	}

	if (Result != EEasySessionResult::Success && Result != EEasySessionResult::Canceled)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Session operation failed: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage);
	}

	// The queue holds the only other reference, and PopActive releases it.
	const TSharedRef<FEasySessionRequest> KeepAlive = AsShared();

	Cleanup(bAbandoned);
	Context->Queue.PopActive();
	Notify(Result, ErrorMessage);
}

bool FEasySessionRequest::IsActive() const
{
	return Context != nullptr && Context->Queue.GetActive().Get() == this;
}

const TCHAR* FEasySessionRequest::GetTypeName() const
{
	switch (Type)
	{
		case EType::Create:		return TEXT("Create");
		case EType::Find:		return TEXT("Find");
		case EType::Join:		return TEXT("Join");
		case EType::Destroy:	return TEXT("Destroy");
		case EType::Update:		return TEXT("Update");
		case EType::Start:		return TEXT("Start");
		case EType::End:		return TEXT("End");
		default:				return TEXT("Unknown");
	}
}

IOnlineSessionPtr FEasySessionRequest::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetContext().GetWorld());
}

void FEasySessionRequest::DestroySessionLeftBehind() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && Sessions->GetNamedSession(SessionName) != nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The abandoned request left a session behind - destroying it so the next request starts clean."));
		GetContext().Subsystem.DestroyEasySession();
	}
}
