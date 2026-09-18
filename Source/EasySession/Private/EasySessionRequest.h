// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineSessionInterface.h"

class FEasySessionHost;
class FEasySessionRequestQueue;
class FEasySessionTravel;
class UEasySessionSubsystem;
class UWorld;

namespace EasySession
{
	/** The fix appended to every RequiresSessionAuthority message. Is Easy Session Host would be wrong here, because it is false on a dedicated server. */
	inline constexpr const TCHAR* RequiresSessionAuthorityFix = TEXT("Show this button only when Is Easy Session Authority is true, so clients do not see it.");

	/** The message of every InvalidParams result that refuses host params. */
	inline constexpr const TCHAR* InvalidHostParamsMessage = TEXT("Host params are invalid: Max Players must be above 0, and Initial Map Name must name the map the session is played on.");
}

/**
 * What a request may use while it runs.
 * The subsystem creates one context after its collaborators and binds it to every request it enqueues.
 * A request holds no other pointer into the plugin, so this struct lists everything a request depends on.
 */
struct FEasySessionRequestContext
{
	/** The subsystem, for its public API and the events a request broadcasts. */
	UEasySessionSubsystem& Subsystem;

	/** The queue the request runs in, which it leaves when it completes. */
	FEasySessionRequestQueue& Queue;

	/** Starts the travels a request needs. */
	FEasySessionTravel& Travel;

	/** The host side of the session, which a request notifies when the session is created, updated or destroyed. */
	FEasySessionHost& Host;

	/** @return The world of the subsystem's game instance. Null before one exists. */
	UWorld* GetWorld() const;

	/** @return Whether the online subsystem is NULL, which only does LAN, so LAN is forced on. */
	bool ShouldForceLAN() const;
};

/**
 * Which online subsystem call a request makes.
 * The queue reads it for the activity and the status line, which name the waiting requests as well as the active one.
 */
enum class EEasySessionRequestType : uint8
{
	Create,
	Find,
	Join,
	Destroy,
	Update,
	Start,
	End
};

/**
 * A single queued call to the online subsystem, as a command object.
 * The queue decides when a request runs, and the request class decides which online subsystem call it makes.
 *
 * Requests run strictly one at a time.
 * The online subsystem already refuses a second call of the same kind, so the queue exists to keep two different calls from overlapping.
 * Steam's DestroySession, for one, only refuses while another destroy is running, so it would destroy a session whose create has not finished.
 * Running requests in order also turns "refused because another call was running" into "runs next", which is what a beginner expects.
 *
 * Each request carries its own deadline.
 * The online subsystem is not required to ever call back, and Steam tasks do not implement CancelWhenTimeout.
 * Without a deadline a request that never completes would block every request behind it.
 *
 * A request class implements three steps, which always run in this order.
 * Execute starts the online subsystem call and binds the delegate that completes it.
 * Cleanup unbinds that delegate and releases what the request still holds.
 * Notify fires the requester's delegate and broadcasts the subsystem's event for the request type.
 * Complete runs Cleanup, takes the request out of the active slot, then runs Notify, so no request class can skip a step.
 *
 * Requests are shared objects because the online subsystem delegates bind to them weakly.
 * A completion that arrives after the request was destroyed is dropped by the delegate.
 */
class FEasySessionRequest : public TSharedFromThis<FEasySessionRequest>
{
	//~ FEasySessionTestAccess runs Cleanup on a request the queue never started.
	friend class FEasySessionTestAccess;

public:

	/** The short name the request code uses for the type enum. */
	using EType = EEasySessionRequestType;

	virtual ~FEasySessionRequest() = default;

	/** Give the request what it may use and the session it acts on. Called by the subsystem before the request is enqueued. */
	void Bind(FEasySessionRequestContext& InContext, FName InSessionName);

	/** Run the request. Called by the queue on the tick the request becomes the active request. */
	void Start();

	/**
	 * Finish the request with this result and schedule the next one.
	 * Does nothing unless this is the active request, so a completion that arrives twice is dropped.
	 *
	 * @param bAbandoned Whether the request is abandoned instead of completed by the online subsystem.
	 *        The online subsystem is then still running the call, which is the only case where Cleanup has to tell it to stop.
	 */
	void Complete(EEasySessionResult Result, const FString& ErrorMessage = FString(), bool bAbandoned = false);

	/** @return Whether this is the queue's active request. False before the queue starts it and after Complete. */
	bool IsActive() const;

	/** Human readable name of the request type, for logs and status output. */
	const TCHAR* GetTypeName() const;

	/** Record the start time and fix the deadline. */
	void MarkStarted(double NowSeconds, float ConfiguredTimeoutSeconds)
	{
		StartTimeSeconds = NowSeconds;
		TimeoutSeconds = ComputeTimeoutSeconds(ConfiguredTimeoutSeconds);
	}

	/** How long this request has been running. */
	double GetElapsedSeconds(double NowSeconds) const
	{
		return NowSeconds - StartTimeSeconds;
	}

	/** Whether the deadline has passed. Always false when the timeout is disabled. */
	bool HasTimedOut(double NowSeconds) const
	{
		return TimeoutSeconds > 0.0 && GetElapsedSeconds(NowSeconds) >= TimeoutSeconds;
	}

	/**
	 * Deadline for this request: the configured timeout, which a request class may replace with its own override.
	 * 0 disables the deadline.
	 */
	double ComputeTimeoutSeconds(float ConfiguredTimeoutSeconds) const
	{
		const float OverrideSeconds = GetTimeoutOverrideSeconds();
		return OverrideSeconds > 0.0f ? OverrideSeconds : FMath::Max(0.0f, ConfiguredTimeoutSeconds);
	}

	/** Which call this request makes. */
	const EType Type;

	/**
	 * The session every step of this request acts on.
	 * Set by Bind and constant afterwards.
	 * It holds one value today, because the plugin hosts a single session per process.
	 */
	FName SessionName;

	/** Time the request started executing. */
	double StartTimeSeconds = 0.0;

	/** Deadline for this run, frozen when the request starts. 0 = no deadline. */
	double TimeoutSeconds = 0.0;

	/**
	 * Whether the requester canceled this request.
	 * It keeps the active slot until the online subsystem completes it, does not count as busy, and its late completion is dropped.
	 */
	bool bCanceled = false;

protected:

	/** Only the request classes construct a request. */
	explicit FEasySessionRequest(EType InType)
		: Type(InType)
	{
	}

	/**
	 * Start the online subsystem call.
	 * The request class must call Complete, inside this call or from the delegate it binds here.
	 */
	virtual void Execute() = 0;

	/**
	 * Unbind the delegates Execute bound and release what the request still holds.
	 * Runs once for every completion, before the request leaves the active slot.
	 */
	virtual void Cleanup(bool bAbandoned) = 0;

	/**
	 * Fire the requester's delegate and broadcast the subsystem's event for this request type.
	 * Runs after the request left the active slot, so the delegate may queue the next request.
	 */
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) = 0;

	/** @return The deadline this request uses in place of the configured one. 0 keeps the configured one. */
	virtual float GetTimeoutOverrideSeconds() const { return 0.0f; }

	/** @return What this request may use. Only valid after Bind. */
	FEasySessionRequestContext& GetContext() const
	{
		check(Context != nullptr);
		return *Context;
	}

	/** @return The session interface of the online subsystem. Null when no online subsystem is loaded. Only valid after Bind. */
	IOnlineSessionPtr GetSessionInterface() const;

	/**
	 * Destroy the session an abandoned Create or Join may still have created.
	 * The online subsystem can complete the call after the deadline, and the next Create or Join would then fail with Session Already Exists.
	 */
	void DestroySessionLeftBehind() const;

private:

	/** Set by Bind. The subsystem owns the context and outlives every request. */
	FEasySessionRequestContext* Context = nullptr;
};
