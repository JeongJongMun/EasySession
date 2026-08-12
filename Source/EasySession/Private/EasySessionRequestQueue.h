// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"

/**
 * Serializes session requests: one runs at a time, the rest wait in order.
 * Why the serialization exists at all is documented on FEasySessionRequest.
 *
 * This class owns the mechanics only.
 * That means the pending list, the active slot, and the watchdog that notices a request whose deadline passed.
 * It also means the scheduling that starts every request outside the callstack that asked for it.
 *
 * Carrying out a request stays with the subsystem, which the queue reaches through two callbacks.
 * One executes the request that just became active; the other reacts to a deadline.
 * The subsystem drives completion too, by popping the active request and letting the queue move on.
 * It has to, because it owns the delegate handles to clear and the events to broadcast.
 */
class FEasySessionRequestQueue
{
public:

	/** Runs the request that just became active. The request is read via GetActive. */
	using FExecuteActive = TFunction<void()>;

	/** Reacts to the active request passing its deadline. Must complete it. */
	using FDeadlineReached = TFunction<void()>;

	FEasySessionRequestQueue(FExecuteActive InExecuteActive, FDeadlineReached InDeadlineReached);

	/** Tickers bound to a raw class do not expire with it - they are removed here. */
	~FEasySessionRequestQueue();

	/** Adds a request. It starts on the next tick, never inside this call. */
	void Enqueue(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Takes the active request out of its slot and schedules the next one for a later tick.
	 * A completion callback may call the online subsystem freely.
	 * What it must not do is start the next queued request, which is why the next one never begins inside this callstack.
	 * Returns what was active so the caller can finish delivering its callbacks.
	 */
	TSharedPtr<FEasySessionRequest> PopActive();

	/** @return The request running right now, or null while the queue is idle. */
	const TSharedPtr<FEasySessionRequest>& GetActive() const { return Active; }

	/** @return Whether nothing is running and nothing is waiting. */
	bool IsIdle() const { return !Active.IsValid() && Pending.IsEmpty(); }

	/** One line for status UI and bug reports, e.g. "Create (running 2.4s of 30s), queued: Start" or "Idle, 1 queued". */
	FString DescribeStatus(bool bIdleButTraveling) const;

private:

	/** Move the first pending request into the active slot and run it. Does nothing while one is already active. */
	void ProcessNext();

	/** Ask for ProcessNext on the next tick. One pending call covers any number of enqueues. */
	void ScheduleNext();

	/** Begin checking the active request's deadline once a second. */
	void StartWatchdog();

	/** Stop checking deadlines. */
	void StopWatchdog();

	/** Watchdog tick: calls DeadlineReached once the active request has passed its deadline. */
	bool TickWatchdog(float DeltaTime);

	/** Runs the active request. Supplied by the subsystem. */
	FExecuteActive ExecuteActive;

	/** Handles a passed deadline. Supplied by the subsystem. */
	FDeadlineReached DeadlineReached;

	/** The request running right now, or null while the queue is idle. */
	TSharedPtr<FEasySessionRequest> Active;

	/** Requests waiting their turn, oldest first. */
	TArray<TSharedRef<FEasySessionRequest>> Pending;

	/** Ticker handle for the deadline watchdog, which runs once a second while a request is active. */
	FTSTicker::FDelegateHandle WatchdogHandle;

	/** Ticker handle for the scheduled ProcessNext. Valid only while one is pending. */
	FTSTicker::FDelegateHandle NextRequestHandle;
};
