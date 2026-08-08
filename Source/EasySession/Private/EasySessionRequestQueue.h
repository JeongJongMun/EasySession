// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"

/**
 * Serializes session requests: one runs at a time, the rest wait in order.
 * Why the serialization exists at all is documented on FEasySessionRequest.
 *
 * This class owns the mechanics only - the pending list, the active slot, the
 * watchdog that notices a request whose deadline passed, and the scheduling that
 * starts every request outside the callstack that asked for it. What a
 * request *does* stays with the subsystem, reaching the queue through two
 * callbacks: one to execute the request that just became active, one to react
 * to a deadline. Completion is driven from the subsystem side too (it has to
 * clear its OSS delegate handles and broadcast its events), by popping the
 * active request and letting the queue move on.
 */
class FEasySessionRequestQueue
{
public:

	/** Runs the request that just became active. The request is read via GetActive. */
	using FExecuteActive = TFunction<void()>;

	/** Reacts to the active request outliving its deadline. Must complete it. */
	using FDeadlineReached = TFunction<void()>;

	FEasySessionRequestQueue(FExecuteActive InExecuteActive, FDeadlineReached InDeadlineReached);

	/** Tickers bound to a raw class do not expire with it - they are removed here. */
	~FEasySessionRequestQueue();

	/** Adds a request. It starts on the next tick, never inside this call. */
	void Enqueue(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Takes the active request out of its slot and schedules the next one, so
	 * completion callbacks never nest OSS calls. Returns what was active so the
	 * caller can finish delivering its callbacks.
	 */
	TSharedPtr<FEasySessionRequest> PopActive();

	const TSharedPtr<FEasySessionRequest>& GetActive() const { return Active; }

	bool IsIdle() const { return !Active.IsValid() && Pending.IsEmpty(); }

	/** One line for status UI and bug reports, e.g. "Create (running 2.4s of 30s), 1 queued". */
	FString DescribeStatus(bool bIdleButTraveling) const;

private:

	void ProcessNext();
	void ScheduleNext();
	void StartWatchdog();
	void StopWatchdog();
	bool TickWatchdog(float DeltaTime);

	FExecuteActive ExecuteActive;
	FDeadlineReached DeadlineReached;

	TSharedPtr<FEasySessionRequest> Active;
	TArray<TSharedRef<FEasySessionRequest>> Pending;

	FTSTicker::FDelegateHandle WatchdogHandle;
	FTSTicker::FDelegateHandle NextRequestHandle;
};
