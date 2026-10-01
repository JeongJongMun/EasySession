// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionRequest.h"

#include "EasySession.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

FEasySessionRequest::~FEasySessionRequest()
{
	if (SubRequestStartHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SubRequestStartHandle);
		SubRequestStartHandle.Reset();
	}
}

void FEasySessionRequest::Initialize(FEasySessionRequestContext& InContext, FName InSessionName)
{
	Context = &InContext;
	SessionName = InSessionName;
}

void FEasySessionRequest::Start()
{
	check(Context != nullptr);
	StartTimeSeconds = FPlatformTime::Seconds();
	bStarted = true;
	Execute();
}

void FEasySessionRequest::Complete(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (!IsRunning() || bCompleting)
	{
		return;
	}
	bCompleting = true;

	if (!bNotified && Result != EEasySessionResult::Success && Result != EEasySessionResult::Canceled)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Session request failed: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage);
	}

	// Stopping may release the only other reference to this request.
	const TSharedRef<FEasySessionRequest> KeepAlive = AsShared();

	// This request must keep running while a sub-request's online subsystem call runs, so a running sub-request is canceled first.
	if (RunningSubRequest.IsValid())
	{
		const TSharedRef<FEasySessionRequest> SubRequest = RunningSubRequest.ToSharedRef();
		SubRequest->Cancel();
	}

	// The sub-request could not stop. The requester is notified now, and this request stops running when the sub-request ends.
	if (RunningSubRequest.IsValid())
	{
		NotifyOnce(Result, ErrorMessage);
		return;
	}

	StopRunning();
	NotifyOnce(Result, ErrorMessage);

	// Create, Join, Start, End and Destroy all leave a new session state behind, and the requester may read it in the delegate above.
	GetContext().Subsystem.RefreshSessionState();
}

void FEasySessionRequest::Cancel()
{
	if (bCompleting || bNotified)
	{
		return;
	}

	if (bStarted)
	{
		HandleCancel();
		return;
	}

	// Nothing runs yet, so the request only has to leave the place it waits in.
	// That place may hold the only other reference to this request.
	const TSharedRef<FEasySessionRequest> KeepAlive = AsShared();
	bCompleting = true;
	if (const TSharedPtr<FEasySessionRequest> Parent = ParentRequest.Pin(); Parent.IsValid() && Parent->RunningSubRequest.Get() == this)
	{
		Parent->RunningSubRequest.Reset();
	}
	else if (!bIsSubRequest)
	{
		GetContext().Queue.Remove(*this);
	}
	NotifyOnce(EEasySessionResult::Canceled, TEXT("The request was canceled before it started."));
}

bool FEasySessionRequest::IsRunning() const
{
	if (!bStarted || Context == nullptr)
	{
		return false;
	}

	if (bIsSubRequest)
	{
		const TSharedPtr<FEasySessionRequest> Parent = ParentRequest.Pin();
		return Parent.IsValid() && Parent->RunningSubRequest.Get() == this && Parent->IsRunning();
	}

	return Context->Queue.GetActiveRequest().Get() == this;
}

EEasySessionActivity FEasySessionRequest::GetActivity() const
{
	switch (Type)
	{
		case EType::Create:			return EEasySessionActivity::Creating;
		case EType::Find:			return EEasySessionActivity::Searching;
		case EType::Join:			return EEasySessionActivity::Joining;
		case EType::Destroy:		return EEasySessionActivity::Leaving;
		case EType::Update:			return EEasySessionActivity::Updating;
		case EType::Start:			return EEasySessionActivity::Starting;
		case EType::End:			return EEasySessionActivity::Ending;
		case EType::Matchmaking:	return EEasySessionActivity::Matchmaking;
		case EType::FriendSessions:	return EEasySessionActivity::Searching;
		case EType::ReadFriends:		return EEasySessionActivity::Searching;
		case EType::CreateParty:	return EEasySessionActivity::Creating;
		case EType::LeaveParty:		return EEasySessionActivity::Leaving;
		default:					return EEasySessionActivity::None;
	}
}

const TCHAR* FEasySessionRequest::GetTypeName() const
{
	switch (Type)
	{
		case EType::Create:			return TEXT("Create");
		case EType::Find:			return TEXT("Find");
		case EType::Join:			return TEXT("Join");
		case EType::Destroy:		return TEXT("Destroy");
		case EType::Update:			return TEXT("Update");
		case EType::Start:			return TEXT("Start");
		case EType::End:			return TEXT("End");
		case EType::Matchmaking:	return TEXT("Matchmaking");
		case EType::FriendSessions:	return TEXT("FriendSessions");
		case EType::ReadFriends:		return TEXT("ReadFriends");
		case EType::CreateParty:	return TEXT("CreateParty");
		case EType::LeaveParty:		return TEXT("LeaveParty");
		default:					return TEXT("Unknown");
	}
}

FString FEasySessionRequest::GetStatusText() const
{
	FString Text = FString(GetTypeName()) + GetProgressText();
	if (RunningSubRequest.IsValid())
	{
		Text += TEXT(" > ") + RunningSubRequest->GetStatusText();
	}
	return Text;
}

void FEasySessionRequest::HandleCancel()
{
	NotifyOnce(EEasySessionResult::Canceled, TEXT("The request was canceled."));
}

FString FEasySessionRequest::GetProgressText() const
{
	// The requester already has the result, and the request only waits for a call it could not stop.
	if (bNotified)
	{
		return TEXT(" (waiting for the online subsystem to finish)");
	}

	if (!bStarted)
	{
		return FString();
	}

	return FString::Printf(TEXT(" (running %.1fs)"), FPlatformTime::Seconds() - StartTimeSeconds);
}

void FEasySessionRequest::RunSubRequest(TSharedRef<FEasySessionRequest> SubRequest)
{
	check(!RunningSubRequest.IsValid());

	SubRequest->Initialize(GetContext(), SessionName);
	SubRequest->ParentRequest = AsShared();
	SubRequest->bIsSubRequest = true;
	RunningSubRequest = SubRequest;

	// The online subsystem may still be finishing the previous call when its completion delegate returns.
	SubRequestStartHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FEasySessionRequest::StartRunningSubRequest));
}

UWorld* FEasySessionRequest::GetWorld() const
{
	const UGameInstance* GameInstance = GetContext().Subsystem.GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

IOnlineSessionPtr FEasySessionRequest::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetWorld());
}

bool FEasySessionRequest::ShouldForceLAN() const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	return OnlineSub != nullptr && OnlineSub->GetSubsystemName() == NULL_SUBSYSTEM;
}

bool FEasySessionRequest::StartRunningSubRequest(float DeltaTime)
{
	SubRequestStartHandle.Reset();

	// The sub-request was canceled before it started, or this request completed meanwhile.
	if (!RunningSubRequest.IsValid() || RunningSubRequest->bStarted || !IsRunning())
	{
		return false;
	}

	const TSharedRef<FEasySessionRequest> SubRequest = RunningSubRequest.ToSharedRef();
	SubRequest->Start();
	return false;
}

void FEasySessionRequest::StopRunning()
{
	Cleanup();

	if (!bIsSubRequest)
	{
		GetContext().Queue.ClearActive();
		return;
	}

	const TSharedPtr<FEasySessionRequest> Parent = ParentRequest.Pin();
	if (!Parent.IsValid())
	{
		return;
	}

	Parent->RunningSubRequest.Reset();

	// The parent completed while this sub-request ran and only waited for it to end.
	if (Parent->bCompleting && Parent->bNotified)
	{
		Parent->StopRunning();
	}
}

void FEasySessionRequest::NotifyOnce(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (bNotified)
	{
		return;
	}
	bNotified = true;

	// A completing parent no longer waits for this sub-request.
	if (bIsSubRequest)
	{
		const TSharedPtr<FEasySessionRequest> Parent = ParentRequest.Pin();
		if (!Parent.IsValid() || Parent->bCompleting)
		{
			return;
		}
	}

	Notify(Result, ErrorMessage);
}
