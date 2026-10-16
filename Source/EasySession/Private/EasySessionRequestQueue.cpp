// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionRequestQueue.h"

FEasySessionRequestQueue::~FEasySessionRequestQueue()
{
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

void FEasySessionRequestQueue::Remove(const FEasySessionRequest& Request)
{
	Pending.RemoveAll([&Request](const TSharedRef<FEasySessionRequest>& Waiting)
	{
		return &Waiting.Get() == &Request;
	});
}

void FEasySessionRequestQueue::ClearActive()
{
	if (!ActiveRequest.IsValid())
	{
		return;
	}

	ActiveRequest.Reset();
	ScheduleNext();
}

bool FEasySessionRequestQueue::IsBusy() const
{
	return GetBusyRequest().IsValid();
}

TSharedPtr<FEasySessionRequest> FEasySessionRequestQueue::GetBusyRequest() const
{
	if (ActiveRequest.IsValid() && ActiveRequest->CountsAsBusy())
	{
		return ActiveRequest;
	}

	for (const TSharedRef<FEasySessionRequest>& Request : Pending)
	{
		if (Request->CountsAsBusy())
		{
			return Request;
		}
	}

	return nullptr;
}

TSharedPtr<FEasySessionRequest> FEasySessionRequestQueue::Find(FEasySessionRequest::EType Type, FName SessionName) const
{
	const auto Matches = [Type, SessionName](const FEasySessionRequest& Request)
	{
		return Request.Type == Type && !Request.HasNotified() && (SessionName.IsNone() || Request.GetSessionName() == SessionName);
	};

	if (ActiveRequest.IsValid() && Matches(*ActiveRequest))
	{
		return ActiveRequest;
	}

	for (const TSharedRef<FEasySessionRequest>& Request : Pending)
	{
		if (Matches(*Request))
		{
			return Request;
		}
	}

	return nullptr;
}

FString FEasySessionRequestQueue::GetStatusText(bool bTraveling) const
{
	// A travel keeps IsBusy true while no request runs, so the status line names it instead of reporting only Idle.
	FString Status = ActiveRequest.IsValid() ? ActiveRequest->GetStatusText() : FString(bTraveling ? TEXT("Idle, traveling") : TEXT("Idle"));

	if (!Pending.IsEmpty())
	{
		TArray<FString> QueuedNames;
		QueuedNames.Reserve(Pending.Num());
		for (const TSharedRef<FEasySessionRequest>& Queued : Pending)
		{
			QueuedNames.Add(Queued->GetTypeName());
		}
		Status += FString::Printf(TEXT(", queued: %s"), *FString::Join(QueuedNames, TEXT(", ")));
	}

	return Status;
}

void FEasySessionRequestQueue::ScheduleNext()
{
	// Never start inside the caller's callstack.
	// A completion callback can enqueue just as the active request stops running.
	// The online subsystem call still returning would then run against the new active request.
	if (NextRequestHandle.IsValid())
	{
		return;
	}

	NextRequestHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
	{
		NextRequestHandle.Reset();
		StartNext();
		return false;
	}));
}

void FEasySessionRequestQueue::StartNext()
{
	if (ActiveRequest.IsValid() || Pending.IsEmpty())
	{
		return;
	}

	ActiveRequest = Pending[0];
	Pending.RemoveAt(0);

	ActiveRequest->Start();
}
