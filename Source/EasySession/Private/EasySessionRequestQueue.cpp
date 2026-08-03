// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionRequestQueue.h"

#include "EasySessionSettings.h"
#include "HAL/PlatformTime.h"

FEasySessionRequestQueue::FEasySessionRequestQueue(FExecuteActive InExecuteActive, FDeadlineReached InDeadlineReached)
	: ExecuteActive(MoveTemp(InExecuteActive))
	, DeadlineReached(MoveTemp(InDeadlineReached))
{
}

FEasySessionRequestQueue::~FEasySessionRequestQueue()
{
	StopWatchdog();

	if (DeferredKickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredKickHandle);
		DeferredKickHandle.Reset();
	}
}

void FEasySessionRequestQueue::Enqueue(TSharedRef<FEasySessionRequest> Request)
{
	Pending.Add(Request);
	if (!Active.IsValid())
	{
		ProcessNext();
	}
}

TSharedPtr<FEasySessionRequest> FEasySessionRequestQueue::PopActive()
{
	TSharedPtr<FEasySessionRequest> Popped = Active;
	Active.Reset();

	// Defer the next request to the next tick so completion callbacks never nest
	// OSS calls. One pending kick is enough however many completions land before
	// it fires: ProcessNext is a no-op while a request is running, and the next
	// completion schedules a fresh kick.
	if (Popped.IsValid() && !DeferredKickHandle.IsValid())
	{
		DeferredKickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			DeferredKickHandle.Reset();
			ProcessNext();
			return false;
		}));
	}

	return Popped;
}

FString FEasySessionRequestQueue::DescribeStatus(bool bIdleButTraveling) const
{
	if (!Active.IsValid())
	{
		if (Pending.Num() > 0)
		{
			return FString::Printf(TEXT("Idle, %d queued"), Pending.Num());
		}

		// Say so rather than reporting "Idle" while Is Busy answers true.
		return bIdleButTraveling ? FString(TEXT("Idle, traveling")) : FString(TEXT("Idle"));
	}

	const double Elapsed = Active->GetElapsedSeconds(FPlatformTime::Seconds());
	FString Status = Active->TimeoutSeconds > 0.0
		? FString::Printf(TEXT("%s (running %.1fs of %.0fs)"), Active->GetTypeName(), Elapsed, Active->TimeoutSeconds)
		: FString::Printf(TEXT("%s (running %.1fs, no timeout)"), Active->GetTypeName(), Elapsed);

	if (Pending.Num() > 0)
	{
		TArray<FString> QueuedNames;
		QueuedNames.Reserve(Pending.Num());
		for (const TSharedPtr<FEasySessionRequest>& Queued : Pending)
		{
			QueuedNames.Add(Queued->GetTypeName());
		}
		Status += FString::Printf(TEXT(", queued: %s"), *FString::Join(QueuedNames, TEXT(", ")));
	}

	return Status;
}

void FEasySessionRequestQueue::ProcessNext()
{
	if (Active.IsValid())
	{
		return;
	}

	if (Pending.IsEmpty())
	{
		StopWatchdog();
		return;
	}

	Active = Pending[0];
	Pending.RemoveAt(0);
	Active->MarkStarted(FPlatformTime::Seconds(), GetDefault<UEasySessionSettings>()->RequestTimeoutSeconds);
	StartWatchdog();

	ExecuteActive();
}

void FEasySessionRequestQueue::StartWatchdog()
{
	if (!WatchdogHandle.IsValid())
	{
		WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEasySessionRequestQueue::TickWatchdog), 1.0f);
	}
}

void FEasySessionRequestQueue::StopWatchdog()
{
	if (WatchdogHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WatchdogHandle);
		WatchdogHandle.Reset();
	}
}

bool FEasySessionRequestQueue::TickWatchdog(float DeltaTime)
{
	if (!Active.IsValid())
	{
		WatchdogHandle.Reset();
		return false;
	}

	if (Active->HasTimedOut(FPlatformTime::Seconds()))
	{
		DeadlineReached();
	}

	return true;
}
