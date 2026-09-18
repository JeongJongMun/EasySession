// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionOperation.h"
#include "EasySessionRequest.h"

/**
 * Serializes session requests: one runs at a time, the rest wait in order.
 * Why the serialization exists at all is documented on FEasySessionRequest.
 *
 * This class owns only the pending list, the active slot and the deadline watchdog.
 * It also schedules every request to start outside the callstack that queued it.
 *
 * What a request does is in the request itself, see FEasySessionRequest.
 * The queue starts the request that became active and abandons the one that passed its deadline.
 * A request leaves the active slot by itself, because FEasySessionRequest::Complete calls PopActive between its Cleanup and its Notify.
 * The queue knows nothing else of the plugin, not even the subsystem that owns it.
 *
 * The queue also keeps the list of multi-step operations, see IEasySessionOperation.
 * An operation submits its own requests; the list only says which operations exist, so busy, already-running, cancel and the status line agree.
 */
class FEasySessionRequestQueue
{
public:

	FEasySessionRequestQueue() = default;

	/** Tickers bound to a raw class do not expire with it, so they are removed here. */
	~FEasySessionRequestQueue();

	/** Adds a request. It starts on the next tick, never inside this call. */
	void Enqueue(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Takes the active request out of its slot and schedules the next one for a later tick.
	 * A completion callback may call the online subsystem freely, but the next queued request must not start inside that callstack, so it starts on a later tick.
	 * Returns what was active.
	 */
	TSharedPtr<FEasySessionRequest> PopActive();

	/** @return The request running right now, or null while the queue is idle. */
	const TSharedPtr<FEasySessionRequest>& GetActive() const { return Active; }

	/** @return Whether nothing is running and nothing is waiting. Operations do not count; they occupy no slot. */
	bool IsIdle() const { return !Active.IsValid() && Pending.IsEmpty(); }

	/** @return Whether a request someone waits for is running or waiting, or an operation that counts as busy is running. A canceled request is not busy. */
	bool IsBusy() const;

	/**
	 * Register a multi-step operation. One of each type runs at a time, so a second of the same type is refused.
	 *
	 * @return Whether the operation was registered.
	 */
	bool BeginOperation(TSharedRef<IEasySessionOperation> Operation);

	/** Forget an operation. The operation calls this itself when it completes or is canceled. */
	void EndOperation(const IEasySessionOperation& Operation);

	/** @return The running operation of this type, or null. */
	TSharedPtr<IEasySessionOperation> FindOperation(EEasySessionOperationType Type) const;

	/** @return The running operation that counts as busy, or null. */
	TSharedPtr<IEasySessionOperation> FindBusyOperation() const;

	/** Cancel every operation. Each cancel ends its operation from inside the call, so the loop runs over a copy of the list. */
	void CancelOperations();

	/** @return The type of the active request, or of the first waiting one while nothing is active yet. Unset when idle. */
	TOptional<FEasySessionRequest::EType> GetCurrentType() const;

	/** @return Whether a request of this type is running or waiting. */
	bool Contains(FEasySessionRequest::EType Type) const;

	/** One line for status UI and bug reports, e.g. "Create (running 2.4s of 30s), queued: Start; Friend search 3/7" or "Idle, 1 queued". */
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

	/**
	 * Watchdog tick: abandon the active request once it has passed its deadline.
	 * The online subsystem is not guaranteed to fire the completion delegate, and a request that never completes would block every request behind it.
	 */
	bool TickWatchdog(float DeltaTime);

	/** The request running right now, or null while the queue is idle. */
	TSharedPtr<FEasySessionRequest> Active;

	/** Requests waiting their turn, oldest first. */
	TArray<TSharedRef<FEasySessionRequest>> Pending;

	/** Multi-step operations in progress, in registration order. At most one per type. */
	TArray<TSharedRef<IEasySessionOperation>> Operations;

	/** Ticker handle for the deadline watchdog, which runs once a second while a request is active. */
	FTSTicker::FDelegateHandle WatchdogHandle;

	/** Ticker handle for the scheduled ProcessNext. Valid only while one is pending. */
	FTSTicker::FDelegateHandle NextRequestHandle;
};
