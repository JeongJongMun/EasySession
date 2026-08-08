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

	if (NextRequestHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NextRequestHandle);
		NextRequestHandle.Reset();
	}
}

void FEasySessionRequestQueue::Enqueue(TSharedRef<FEasySessionRequest> Request)
{
	Pending.Add(Request);
	ScheduleNext();
}

TSharedPtr<FEasySessionRequest> FEasySessionRequestQueue::PopActive()
{
	TSharedPtr<FEasySessionRequest> Popped = Active;
	Active.Reset();

	if (Popped.IsValid())
	{
		ScheduleNext();
	}

	return Popped;
}

void FEasySessionRequestQueue::ScheduleNext()
{
	// Never start inside the caller's callstack: a completion callback enqueues just
	// as the slot empties, and the online call still returning would then land on
	// whatever took the slot. One pending call is enough for any number of requests.
	if (NextRequestHandle.IsValid())
	{
		return;
	}

	NextRequestHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
	{
		NextRequestHandle.Reset();
		ProcessNext();
		return false;
	}));
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
