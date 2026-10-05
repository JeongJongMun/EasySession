// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"

/**
 * Serializes session requests: one runs at a time, the rest wait in order.
 * Why the serialization exists at all is documented on FEasySessionRequest.
 *
 * This class owns only the pending list and the active request.
 * It also schedules every request to start outside the callstack that queued it.
 *
 * What a request does is in the request itself, see FEasySessionRequest.
 * A request stops being the active request by itself, because FEasySessionRequest::Complete calls ClearActive between its Cleanup and its Notify.
 * The queue knows nothing else of the plugin, not even the subsystem that owns it.
 * A request that runs sub-requests stays the active request for all of them, so no other request runs between two of them.
 */
class FEasySessionRequestQueue
{
	//~ FEasySessionTestAccess counts the running and waiting requests of a type.
	friend class FEasySessionTestAccess;

public:

	/** Tickers bound to a raw class do not expire with it, so they are removed here. */
	~FEasySessionRequestQueue();

	/** Add a request. It starts on a later tick, after the requests ahead of it stop running, never inside this call. */
	void Enqueue(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Remove a request that waits and has not started.
	 * The request notifies its requester itself, see FEasySessionRequest::Cancel.
	 */
	void Remove(const FEasySessionRequest& Request);

	/**
	 * Clear the active request and schedule the next one for a later tick.
	 * A completion callback may call the online subsystem freely, but the next queued request must not start inside that callstack, so it starts on a later tick.
	 */
	void ClearActive();

	/** @return The request running right now, or null while the queue is idle. */
	const TSharedPtr<FEasySessionRequest>& GetActiveRequest() const { return ActiveRequest; }

	/** @return Whether a running or waiting request counts as busy. See FEasySessionRequest::CountsAsBusy. */
	bool IsBusy() const;

	/** @return The request that makes the queue busy: the running request, or the first waiting one that counts as busy. Null when the queue is not busy. */
	TSharedPtr<FEasySessionRequest> GetBusyRequest() const;

	/**
	 * @return The running or waiting request of this type whose requester was not notified yet, running first. Null when there is none.
	 *         Sub-requests are not searched, only the requests the queue runs.
	 *         A canceled request that still waits for its online subsystem call is skipped, because its requester already has the result.
	 */
	TSharedPtr<FEasySessionRequest> Find(FEasySessionRequest::EType Type) const;

	/**
	 * Build the status line for status UI and bug reports, e.g. "Create (running 2.4s), queued: Start" or "Idle, queued: Find".
	 *
	 * @param bTraveling Whether a travel runs. The status line names it while no request runs.
	 */
	FString GetStatusText(bool bTraveling) const;

private:

	/** Ask for StartNext on the next tick. One pending call covers any number of enqueues. */
	void ScheduleNext();

	/** Make the first pending request the active request and start it. Does nothing while one is already active. */
	void StartNext();

	/** The request running right now, or null while the queue is idle. */
	TSharedPtr<FEasySessionRequest> ActiveRequest;

	/** Requests waiting their turn, oldest first. */
	TArray<TSharedRef<FEasySessionRequest>> Pending;

	/** Ticker handle for the scheduled StartNext. Valid only while one is pending. */
	FTSTicker::FDelegateHandle NextRequestHandle;
};
