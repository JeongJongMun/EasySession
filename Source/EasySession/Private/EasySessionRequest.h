// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "Containers/Ticker.h"
#include "Interfaces/OnlineSessionInterface.h"

class FEasySessionHost;
class FEasySessionParty;
class FEasySessionRequestQueue;
class FEasySessionTravel;
class UEasySessionSubsystem;
class UWorld;

/**
 * What a request may use while it runs.
 * The subsystem creates one context after its collaborators and passes it to every request it enqueues.
 * A request holds no other pointer into the plugin, so this struct lists everything a request depends on.
 */
struct FEasySessionRequestContext
{
	/** The subsystem, for its public API and the events a request broadcasts. */
	UEasySessionSubsystem& Subsystem;

	/** The queue that runs the request, whose active request StopRunning clears. */
	FEasySessionRequestQueue& Queue;

	/** Starts the travels a request needs. */
	FEasySessionTravel& Travel;

	/** The host side of the session, which a request notifies when the session is created, updated or destroyed. */
	FEasySessionHost& Host;

	/** The party, which the party requests start and close. */
	FEasySessionParty& Party;
};

/** What a request does. The queue reads it for the activity, the status line and the checks for a request that is already running. */
enum class EEasySessionRequestType : uint8
{
	/** Create the session and travel the host to its map. */
	Create,

	/** Search for sessions. */
	Find,

	/** Join a session and travel to its host. */
	Join,

	/** Destroy the game session, or the party session for a party request. */
	Destroy,

	/** Advertise new session settings. */
	Update,

	/** Start the match. */
	Start,

	/** End the match. */
	End,

	/** One matchmaking run: searches and joins, then a host fallback. */
	Matchmaking,

	/** The friend session search: the friends list, then one session search per friend. */
	FriendSessions,

	/** Read the local player's friends list. */
	ReadFriends,

	/** Create the party session and start the party beacon. */
	CreateParty,

	/** Join a party session and log in on the leader's party beacon. */
	JoinParty,

	/** Close the party beacon and destroy the party session. */
	LeaveParty
};

/**
 * FEasySessionRequest is responsible for one thing the session subsystem does, such as creating a session or running matchmaking.
 * The subsystem creates a request for each call of its public API, and FEasySessionRequestQueue runs the requests one at a time.
 *
 * The queue exists because the online subsystem refuses a second call of the same kind but lets two different calls overlap.
 * For example, Steam's DestroySession only refuses while another destroy is running, so it would destroy a session whose create has not finished.
 * Running requests in order also turns "refused because another call was running" into "runs next", which is what a beginner expects.
 *
 * A request makes one online subsystem call, or runs other requests one after another as its sub-requests.
 * A sub-request runs while the request that started it keeps running, so no other request runs between two sub-requests.
 *
 * A request class implements Execute, Cleanup and Notify.
 * Start calls Execute, and Complete calls Cleanup and then Notify.
 * Notify runs before Cleanup when the requester was notified while the request kept running, as after a cancel the online subsystem cannot stop.
 * Execute starts the work and binds the delegate that completes it.
 * Cleanup unbinds that delegate and releases what the request still holds.
 * Notify fires the requester's delegate, which is the only place the result goes.
 * Between Cleanup and Notify the request stops running, so the requester's delegate may queue the next request.
 *
 * Requests are shared objects because the online subsystem delegates bind to them weakly.
 * A completion that arrives after the request was destroyed is dropped by the delegate.
 *
 * @see FEasySessionRequestQueue
 */
class FEasySessionRequest : public TSharedFromThis<FEasySessionRequest>
{
public:

	/** The short name the request code uses for the type enum. */
	using EType = EEasySessionRequestType;

	/** Removes the ticker that starts a sub-request on the next tick. */
	virtual ~FEasySessionRequest();

	/** Give the request what it may use and the session it acts on. Called before the request is enqueued or run as a sub-request. */
	void Initialize(FEasySessionRequestContext& InContext, FName InSessionName);

	/**
	 * Run the request.
	 * Called by the queue on the tick the request becomes the active request, and by the parent for a sub-request.
	 */
	void Start();

	/**
	 * Finish the request with this result.
	 * Does nothing unless the request is running, so a completion that arrives twice is dropped.
	 * A running sub-request is canceled first.
	 * When the sub-request cannot stop, the requester is notified inside this call and the request stops running when the sub-request ends.
	 */
	void Complete(EEasySessionResult Result, const FString& ErrorMessage = FString());

	/**
	 * Cancel the request for its requester.
	 * A request still waiting in the queue is removed from it, and the requester is notified with Canceled inside this call.
	 * A running request decides in HandleCancel whether it can stop.
	 */
	void Cancel();

	/** @return Whether the request runs now: it started and has not stopped running. */
	bool IsRunning() const;

	/**
	 * @return Whether Notify already ran, so the requester has the result.
	 *         The request may still be running, while an online subsystem call it cannot stop runs to its end.
	 */
	bool HasNotified() const { return bNotified; }

	/** @return Whether UEasySessionSubsystem::IsBusy counts this request. A request that already notified its requester does not count. */
	bool CountsAsBusy() const { return !bNotified; }

	/** @return The sub-request this request runs, or null. */
	const TSharedPtr<FEasySessionRequest>& GetRunningSubRequest() const { return RunningSubRequest; }

	/** @return What UEasySessionSubsystem::GetActivity reports while this request runs. */
	EEasySessionActivity GetActivity() const;

	/** @return The human readable name of the request type, for logs and the status line. */
	const TCHAR* GetTypeName() const;

	/** @return The status line text of this request and its running sub-requests, e.g. "Matchmaking (Searching, 12s) > Find (running 1.2s)". */
	FString GetStatusText() const;

	/** What this request does. */
	const EType Type;

protected:

	/** Only the request classes construct a request. */
	explicit FEasySessionRequest(EType InType)
		: Type(InType)
	{
	}

	/**
	 * Start the work.
	 * The request class must call Complete, inside this call or from the delegate it binds here.
	 */
	virtual void Execute() = 0;

	/**
	 * Unbind the delegates Execute bound and release what the request still holds.
	 * Runs before the request stops running, so a completion that arrives late reaches no handler.
	 */
	virtual void Cleanup() {}

	/**
	 * Fire the requester's delegate.
	 * Runs once, usually after the request stopped running, so the delegate may queue the next request.
	 * A cancel the online subsystem cannot stop, or a Complete that waits for a running sub-request, runs it while the request still runs.
	 */
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) = 0;

	/**
	 * Called by Cancel while the request runs.
	 * The online subsystem cannot stop most calls, so the default notifies the requester with Canceled inside this call.
	 * The request keeps running until the call completes.
	 */
	virtual void HandleCancel();

	/** @return What follows the type name on the status line, e.g. " (running 2.4s)". */
	virtual FString GetProgressText() const;

	/**
	 * Run another request as a sub-request, while this request keeps running.
	 * Build the sub-request with a completion delegate bound to this request, the same way a caller of the public API does.
	 * The sub-request starts on the next tick, never inside this call, the same rule the queue follows between requests.
	 */
	void RunSubRequest(TSharedRef<FEasySessionRequest> SubRequest);

	/** @return What this request may use. Only valid after Initialize. */
	FEasySessionRequestContext& GetContext() const
	{
		check(Context != nullptr);
		return *Context;
	}

	/** @return The world of the subsystem's game instance. Null before one exists. Only valid after Initialize. */
	UWorld* GetWorld() const;

	/** @return The session interface of the online subsystem. Null when no online subsystem is loaded. Only valid after Initialize. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** @return Whether the online subsystem is NULL, which only does LAN, so LAN is forced on. Only valid after Initialize. */
	bool ShouldForceLAN() const;

	/**
	 * @return The players who move with the local player when it joins or creates a game session.
	 *         The other party members on a party leader, and the other players of a lobby on its host, which never overlap.
	 *         A party leader is never in a game session, because entering one closes the party.
	 */
	TArray<FUniqueNetIdRepl> GetGroupMembers() const;

	/** Tell the group to follow into the session of this host: over the party beacon on a party leader, through the player components on a lobby host. */
	void TellGroupToFollow(const TArray<FUniqueNetIdRepl>& Members, const FUniqueNetIdRepl& HostId, bool bLANQuery) const;

	/**
	 * The session this request and its sub-requests act on.
	 * Set by Initialize and constant afterwards.
	 * It is the game session, except for the party requests, which act on the party session.
	 */
	FName SessionName;

private:

	/** Start the sub-request RunSubRequest stored. Runs once, on the tick after RunSubRequest. */
	bool StartRunningSubRequest(float DeltaTime);

	/**
	 * Run Cleanup, then take the request out of the queue's active request, or out of the parent's running sub-request.
	 * A parent that already notified its requester and waited only for this sub-request stops running too.
	 */
	void StopRunning();

	/** Run Notify once. A sub-request whose parent is completing skips Notify, because the parent no longer waits for it. */
	void NotifyOnce(EEasySessionResult Result, const FString& ErrorMessage);

	/** Set by Initialize. The subsystem owns the context and outlives every request. */
	FEasySessionRequestContext* Context = nullptr;

	/** The request that runs this request as a sub-request. Unset for a request the queue runs. */
	TWeakPtr<FEasySessionRequest> ParentRequest;

	/** The sub-request this request runs. Set by RunSubRequest and reset when the sub-request stops running. */
	TSharedPtr<FEasySessionRequest> RunningSubRequest;

	/** Handle for the ticker that starts the sub-request RunSubRequest stored. Valid only until the sub-request starts. */
	FTSTicker::FDelegateHandle SubRequestStartHandle;

	/** Time the request started, for the status line. */
	double StartTimeSeconds = 0.0;

	/** Is this request a sub-request of another request. */
	bool bIsSubRequest = false;

	/** Has the request started. */
	bool bStarted = false;

	/** Has Complete been called, or has Cancel removed the request before it started. */
	bool bCompleting = false;

	/** Has Notify run. */
	bool bNotified = false;
};
