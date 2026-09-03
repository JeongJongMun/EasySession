// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionRequestQueue.h"

#include "EasySession.h"
#include "EasySessionConfig.h"
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

TOptional<FEasySessionRequest::EType> FEasySessionRequestQueue::GetCurrentType() const
{
	if (Active.IsValid())
	{
		return Active->Type;
	}
	if (!Pending.IsEmpty())
	{
		return Pending[0]->Type;
	}
	return TOptional<FEasySessionRequest::EType>();
}

bool FEasySessionRequestQueue::IsBusy() const
{
	return !IsIdle() || FindBusyOperation().IsValid();
}

bool FEasySessionRequestQueue::BeginOperation(TSharedRef<IEasySessionOperation> Operation)
{
	if (FindOperation(Operation->GetType()).IsValid())
	{
		return false;
	}

	UE_LOG(LogEasySession, Verbose, TEXT("Operation started: %s"), EasySession::OperationTypeToString(Operation->GetType()));
	Operations.Add(MoveTemp(Operation));
	return true;
}

void FEasySessionRequestQueue::EndOperation(const IEasySessionOperation& Operation)
{
	const int32 Removed = Operations.RemoveAll([&Operation](const TSharedRef<IEasySessionOperation>& Entry)
	{
		return &Entry.Get() == &Operation;
	});

	if (Removed > 0)
	{
		UE_LOG(LogEasySession, Verbose, TEXT("Operation ended: %s"), EasySession::OperationTypeToString(Operation.GetType()));
	}
}

TSharedPtr<IEasySessionOperation> FEasySessionRequestQueue::FindOperation(EEasySessionOperationType Type) const
{
	for (const TSharedRef<IEasySessionOperation>& Operation : Operations)
	{
		if (Operation->GetType() == Type)
		{
			return Operation;
		}
	}

	return nullptr;
}

TSharedPtr<IEasySessionOperation> FEasySessionRequestQueue::FindBusyOperation() const
{
	for (const TSharedRef<IEasySessionOperation>& Operation : Operations)
	{
		if (Operation->CountsAsBusy())
		{
			return Operation;
		}
	}

	return nullptr;
}

void FEasySessionRequestQueue::CancelOperations()
{
	// Each cancel calls EndOperation, which edits the list this loop would be walking.
	const TArray<TSharedRef<IEasySessionOperation>> Snapshot = Operations;
	for (const TSharedRef<IEasySessionOperation>& Operation : Snapshot)
	{
		Operation->Cancel();
	}
}

bool FEasySessionRequestQueue::Contains(FEasySessionRequest::EType Type) const
{
	if (Active.IsValid() && Active->Type == Type)
	{
		return true;
	}

	for (const TSharedRef<FEasySessionRequest>& Request : Pending)
	{
		if (Request->Type == Type)
		{
			return true;
		}
	}

	return false;
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
	FString Status;
	if (!Active.IsValid())
	{
		if (Pending.Num() > 0)
		{
			Status = FString::Printf(TEXT("Idle, %d queued"), Pending.Num());
		}
		else
		{
			// Say so rather than reporting "Idle" while Is Busy answers true.
			Status = bIdleButTraveling ? TEXT("Idle, traveling") : TEXT("Idle");
		}
	}
	else
	{
		const double Elapsed = Active->GetElapsedSeconds(FPlatformTime::Seconds());
		Status = Active->TimeoutSeconds > 0.0
			? FString::Printf(TEXT("%s (running %.1fs of %.0fs)"), Active->GetTypeName(), Elapsed, Active->TimeoutSeconds)
			: FString::Printf(TEXT("%s (running %.1fs, no timeout)"), Active->GetTypeName(), Elapsed);

		if (Pending.Num() > 0)
		{
			TArray<FString> QueuedNames;
			QueuedNames.Reserve(Pending.Num());
			for (const TSharedRef<FEasySessionRequest>& Queued : Pending)
			{
				QueuedNames.Add(Queued->GetTypeName());
			}
			Status += FString::Printf(TEXT(", queued: %s"), *FString::Join(QueuedNames, TEXT(", ")));
		}
	}

	for (const TSharedRef<IEasySessionOperation>& Operation : Operations)
	{
		Status += TEXT("; ") + Operation->DescribeProgress();
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
	Active->MarkStarted(FPlatformTime::Seconds(), GetDefault<UEasySessionConfig>()->RequestTimeoutSeconds);
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
