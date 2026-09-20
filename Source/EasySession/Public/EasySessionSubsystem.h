// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "EasySessionTypes.h"
#include "Containers/Ticker.h"
#include "Templates/SubclassOf.h"
#include "EasySessionSubsystem.generated.h"

namespace ENetworkFailure
{
	enum Type : int;
}

namespace ETravelFailure
{
	enum Type : int;
}

class AController;
class AGameModeBase;
class APlayerController;
class FEasySessionBeaconPort;
class FEasySessionHost;
class FEasySessionRequest;
class FEasySessionRequestQueue;
class FEasySessionSocial;
class FEasySessionTravel;
class UEasyMatchmakingPolicy;
struct FEasyJoinApprovalRequest;
struct FEasyJoinApprovalResponse;
struct FEasySessionRequestContext;

/** Multicast event fired with the result of a session request, used by the async nodes and by On Matchmaking Complete. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionEvent, EEasySessionResult, Result, const FString&, ErrorMessage);

/** The Success and Failure pins of the Find Easy Sessions node. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasySessionFindEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionSearchResult>&, Results);

/** Multicast event fired when something fails outside any node's result, such as a lost connection or a failed travel. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionFailureEvent, const FString&, Reason);

/** Multicast event fired when the player accepts an invite from the platform overlay. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionInviteAcceptedEvent, const FEasySessionSearchResult&, Session);

/** Multicast event fired when the session's advertised settings change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasySessionSettingsChangedEvent);

/** Multicast event fired when the session's lifecycle state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionStateEvent, EEasySessionState, OldState, EEasySessionState, NewState);

/** Multicast event fired when Is Busy changes, so a UI can enable and disable its buttons without polling. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionBusyChangedEvent, bool, bBusy);

/** Multicast event fired when a matchmaking run is accepted and its policy is registered. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasyMatchmakingStartedEvent);

/** Multicast event fired when the state of the running matchmaking changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasyMatchmakingStateEvent, EEasyMatchmakingState, OldState, EEasyMatchmakingState, NewState);

/** Multicast event fired on every state change and once a second while the run is active. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasyMatchmakingUpdatedEvent, EEasyMatchmakingState, State, int32, ElapsedSeconds);

/** The Success and Failure pins of the Read Easy Friends node. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionFriend>&, Friends);

/** The Success and Failure pins of the Find Easy Friend Sessions node. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendSessionsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasyFriendSession>&, FriendSessions);

/**
 * The EasySession subsystem is responsible for every session request of the plugin.
 * It is created automatically for each game instance, so no custom GameInstance class is needed.
 *
 * All requests are queued and run one at a time, so they can be called in any order without breaking the underlying online subsystem.
 * Each request reports its result through its completion delegate, or the output pins of its async node.
 * The events below report states and runs, not single requests: the session's state and settings, busy, matchmaking and failures.
 */
UCLASS()
class EASYSESSION_API UEasySessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	//~ FEasySessionTestAccess is the only friend, the attorney of the attorney-client idiom.
	//~ It reads private state for the tests, so no test API ships on the subsystem.
	//~ The internal collaborators are not friends. They call this subsystem's public API only, so the header shows everything they can touch.
	friend class FEasySessionTestAccess;

public:

	/**
	 * Declared here and defined in the .cpp on purpose.
	 * The collaborators below are held by TUniquePtr to types this header only forward declares.
	 * A compiler generated constructor or destructor would have to instantiate their deleters where those types are still incomplete.
	 * That is exactly where UHT puts the constructors it generates, including the hot reload one.
	 * The engine's own pimpl holders (UPrimitiveComponent, ULocalPlayer, UNetConnection) declare the same three for the same reason.
	 */
	UEasySessionSubsystem();
	UEasySessionSubsystem(FVTableHelper& Helper);
	virtual ~UEasySessionSubsystem() override;

	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

public:

	/**
	 * Fired when the advertised session settings change: on the host when it updates them, and on a client when the host's values arrive.
	 * Get Easy Session Settings already returns the new values when this fires, so a UI reads them and refreshes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionSettingsChangedEvent OnSessionSettingsChanged;

	/**
	 * Fired when the session's lifecycle state changes, on the host and on every client.
	 * Creating a session and leaving one are state changes too, from and to No Session.
	 * A client reports the host's replicated state, so this fires there when the host starts or ends the match.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionStateEvent OnSessionStateChanged;

	/**
	 * Fired whenever Is Busy changes, with the new value.
	 * Bind once and enable or disable session buttons from the flag instead of reading Is Busy every tick.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionBusyChangedEvent OnBusyChanged;

	/**
	 * Fired when a matchmaking run is accepted.
	 * It is the first event of that run, and from then on Get Active Easy Matchmaking Policy returns the run's policy.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStartedEvent OnMatchmakingStarted;

	/** Fired when the state of the running matchmaking changes. Progress UI binds here once, for every run. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStateEvent OnMatchmakingStateChanged;

	/** Fired on every state change of the running matchmaking and once a second while it runs, with the elapsed whole seconds. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingUpdatedEvent OnMatchmakingUpdated;

	/** Fired when a matchmaking run completes, a canceled one included. A cancel arrives as the Canceled result, never as a separate event. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnMatchmakingComplete;

	/**
	 * Fired when something fails outside any node's result.
	 * That is a dropped connection, or the join of an accepted invite that fails.
	 * It also covers a travel or listen server started by EasySession that fails, for example on a wrong Initial Map Name.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFailureEvent OnSessionFailure;

	/**
	 * Fired when the player accepts an invite from the platform overlay.
	 * With Auto Join Accepted Invites on, this player joins the invited session right after this event, and a running matchmaking is canceled.
	 * A player who is already in a session joins only when Accept Invites While In Session is on.
	 * Their current session is destroyed first, which disconnects everyone if they were hosting it.
	 * With it off, call Join Easy Session yourself, for example after asking the player.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionInviteAcceptedEvent OnSessionInviteAccepted;

public:

	/**
	 * Create a new session and optionally travel to the session map.
	 * For listen servers the map is opened with the ?listen option automatically.
	 *
	 * @param HostParams Parameters describing the session to create.
	 * @param OnComplete Called when the request completes.
	 */
	void CreateSession(const FEasySessionHostParams& HostParams, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Search for sessions matching the given filters.
	 *
	 * @param SearchParams Parameters describing what to search for.
	 * @param OnComplete Called with the filtered results when the search completes.
	 */
	void FindSessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete = FEasySessionFindCompleteDelegate());

	/**
	 * Join the given session and travel to the host.
	 * A player in another session leaves it first, once the host approved the join, and a host tells its clients why.
	 * A join that fails after leaving travels the player to the menu.
	 * Joining the session this player is already in fails with SessionAlreadyExists.
	 *
	 * @param SearchResult A search result returned by FindSessions.
	 * @param Password Password for password protected sessions. Ignored otherwise.
	 * @param AdditionalTravelOptions Extra options appended to the client travel URL (e.g. "Name=Player?Team=1").
	 * @param OnComplete Called when the request completes.
	 */
	void JoinSession(const FEasySessionSearchResult& SearchResult, const FString& Password = FString(), const FString& AdditionalTravelOptions = FString(), FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start the match.
	 * The session moves to InProgress.
	 * When Allow Join In Progress is off, new players are refused from here until the match ends.
	 * Steam refused them from the first join onwards already.
	 * Needs session authority: only the game that created the session can start the match.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void StartSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * End the match.
	 * The session moves to Ended, so a new match can be started.
	 * Needs session authority: only the game that created the session can end the match.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void EndSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Destroy the current session.
	 * On a client this leaves the session.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void DestroySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Leave the session: destroy this game's named session, then travel to the menu map.
	 * A leaving host takes the session with it, so it destroys the session for everyone and every client receives "The host has left the game." first.
	 * The menu travel runs whatever the destroy reported, because the player asked to leave.
	 *
	 * @param OnComplete Called with the destroy's result, after the menu travel was requested.
	 */
	void LeaveSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Destroy the session for every player.
	 * Clients record Reason as a Host Destroyed Session disconnect and travel back to the menu.
	 * There, Consume Pending Easy Disconnect Info returns it so the menu can show it to the player.
	 * Needs session authority: only the game that created the session can do this.
	 *
	 * @param OnComplete Called with the destroy's result, after the host's own menu travel was requested.
	 */
	void DestroySessionForEveryone(FText Reason, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Update the advertised properties of the current session.
	 * Needs session authority: only the game that created the session can update it.
	 *
	 * Every field is applied as given, including Password.
	 * Pass settings from GetSessionSettings and change only what you mean to change.
	 * Otherwise the fields you left at their defaults overwrite the session with those defaults.
	 *
	 * @param NewSettings The settings to advertise in place of the current ones.
	 * @param OnComplete Called when the request completes.
	 */
	void UpdateSession(const FEasySessionSettings& NewSettings, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'. The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Needs session authority: only the game that created the session can travel it.
	 * Returns false for other games.
	 */
	bool ServerTravel(const FString& MapName);

	/**
	 * Start Matchmaking: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 * The run holds the session queue until it ends, so session requests made meanwhile run after it.
	 *
	 * @param MatchmakingParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when null.
	 * @param OnComplete Called when matchmaking completes.
	 */
	void StartMatchmaking(const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Cancel the running matchmaking.
	 * A search ends inside this call.
	 * A join or host that completes after the cancel is undone.
	 * Does nothing when no matchmaking is running.
	 */
	void CancelMatchmaking();

public:

	/** @return Whether matchmaking is running. */
	bool IsMatchmakingRunning() const;

	/** @return The state of the running matchmaking. Idle when none is running. */
	EEasyMatchmakingState GetMatchmakingState() const;

	/**
	 * @return The policy of the running matchmaking, or null when none is running.
	 *         Progress is broadcast on the On Matchmaking events of this subsystem, not on the policy.
	 */
	UEasyMatchmakingPolicy* GetActiveMatchmakingPolicy() const;

	/** @return Whether the local player is in a session. */
	bool IsInSession() const;

	/**
	 * @return The lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 *         The host reports its own state. A client reports the host's replicated state once it has arrived, and its own until then.
	 */
	EEasySessionState GetSessionState() const;

	/**
	 * Get these, change the one field, and pass them to Update Easy Session.
	 * Building new settings instead resets every field you did not fill in.
	 *
	 * @return The settings the current session is advertising, so a change can be made without restating everything else.
	 *         Works for every player in the session; the password and its friends exception are only filled on the host, the one game that holds them.
	 */
	FEasySessionSettings GetSessionSettings() const;

	/**
	 * @return The join code the current session advertises, or empty when it advertises none.
	 *         Works for every player in the session, so any session member can share the code.
	 */
	FString GetSessionJoinCode() const;

	/**
	 * @return Whether the local player is hosting the current session.
	 *         False on a dedicated server, which has no local player, so ask IsSessionAuthority there.
	 */
	bool IsHost() const;

	/**
	 * @return Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 *         Read from FNamedOnlineSession's bHosting, which this plugin sets when the create completes because Steam never does.
	 *         Is Host is a different question, and false on a dedicated server.
	 */
	bool IsSessionAuthority() const;

	/** @return The display name of the current session. Empty when no session exists. */
	FString GetSessionDisplayName() const;

	/** @return Per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host. */
	TArray<FEasySessionPlayerInfo> GetSessionPlayerInfos() const;

	/** @return The number of players currently in the session. */
	int32 GetSessionPlayerCount() const;

	/** @return The maximum number of players allowed in the current session. 0 when no session exists. */
	int32 GetSessionMaxPlayers() const;

	/**
	 * Session buttons read this to disable themselves.
	 * IsMatchmakingRunning asks about matchmaking alone.
	 *
	 * @return Whether a request is running or queued, matchmaking included,
	 *         or a travel this plugin started has not loaded its map yet.
	 */
	bool IsBusy() const;

	/**
	 * What keeps IsBusy true: the running or waiting request, or a travel this plugin started.
	 * It covers requests the game did not start too, such as the destroy after a lost connection.
	 * Get Activity Message turns it into a status line such as "Joining the session...".
	 *
	 * @return None exactly when IsBusy is false.
	 */
	EEasySessionActivity GetActivity() const;

	/** @return What the session queue is doing right now, for status UI and bug reports, e.g. "Create (running 2.4s), queued: Start" or "Idle". */
	FString GetQueueStatus() const;

public:

	/**
	 * Invite a friend to the current session.
	 *
	 * @return Success, or why not: NotSupportedByService on an online subsystem without invites such as NULL (LAN),
	 *         NoSessionExists with no session to invite to, or InvalidParams for a friend ReadFriends did not return.
	 */
	EEasySessionResult SendSessionInviteToFriend(const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the current session.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowInviteUI();

	/**
	 * Open the platform profile overlay (e.g. Steam) for the given friend.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowProfileUI(const FEasySessionFriend& Friend);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a player in the session.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player);

	/**
	 * Read the local player's friends list, in display order.
	 * Not supported on the NULL (LAN) subsystem.
	 * The read waits in the session queue like every other request.
	 *
	 * @param OnComplete Called with the friends when the read completes.
	 */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete = FEasyFriendsCompleteDelegate());

	/**
	 * Read the friends list and find the session each friend playing this game is in.
	 * Not supported on the NULL (LAN) subsystem.
	 * The search holds the session queue until it ends, so session requests made meanwhile run after it.
	 * One friend search runs at a time.
	 * A second call while one runs fails with FriendSearchAlreadyInProgress.
	 *
	 * @param OnComplete Called with one friend session per friend. Those with bHasSession carry a session joinable with JoinSession.
	 */
	void FindFriendSessions(FEasyFriendSessionsCompleteDelegate OnComplete = FEasyFriendSessionsCompleteDelegate());

	/**
	 * Cancel the running friend session search.
	 * It completes with Canceled inside this call.
	 * Does nothing when none is running.
	 */
	void CancelFriendSearch();

public:

	/** @return Whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	bool HasPendingDisconnectInfo() const { return PendingDisconnectInfo.IsSet(); }

	/** Take the waiting disconnect info. It is kept across map travel until this call, and empty when none is waiting. */
	FEasyDisconnectInfo ConsumePendingDisconnectInfo();

	/**
	 * Record a disconnect, destroy the lost session, and travel to the project's Game Default Map when Auto Return To Menu On Disconnect is on.
	 * Called automatically on network and travel failures.
	 * Call it yourself only if you detect disconnects yourself.
	 * The first reason recorded is kept until it is consumed, so a later failure cannot replace the first cause.
	 */
	void HandleDisconnect(EEasyDisconnectReason Reason, const FText& ReasonText);

public:

	/**
	 * C++ delegate: change the server travel URL (hosting and server travel) before it is used.
	 * Bind at startup.
	 * The delegate fires before the completion callback of the request that travels, so binding inside that callback misses its own travel.
	 * For one request's options, use Additional Travel Options on the params instead.
	 */
	FEasyModifyTravelURLDelegate OnModifyServerTravelURL;

	/**
	 * C++ delegate: change the client travel URL (joining a host) before it is used.
	 * This URL carries the session password as an option.
	 * Do not log it.
	 * Bind at startup.
	 * The delegate fires before the completion callback of the request that travels, so binding inside that callback misses its own travel.
	 * For one request's options, use Additional Travel Options on the params instead.
	 */
	FEasyModifyTravelURLDelegate OnModifyClientTravelURL;

public:

	/**
	 * Internal, called by the join approval beacon: decide whether the requester may join the session, as the server gate decides it.
	 * The beacon asks this before the player travels, so a refused player never starts the travel.
	 * PreLogin enforces the same decision when the player arrives, which also covers a player who never requested join approval.
	 * The beacon is a world actor, and world actors reach this subsystem through its public API rather than through a collaborator.
	 * Refuses the join while no server gate exists.
	 *
	 * @param Request What the joining player sent over the beacon.
	 * @param Requester The id the joining player presented at beacon login.
	 */
	FEasyJoinApprovalResponse ApproveJoin(const FEasyJoinApprovalRequest& Request, const FUniqueNetIdRepl& Requester) const;

	/**
	 * Internal, called by every request when it finishes and by the replicated state below.
	 * Broadcasts OnSessionStateChanged when the session state differs from the last one reported.
	 * A state that lasts less than a frame still reaches the game this way, which a ticker that samples once a frame would miss.
	 */
	void RefreshSessionState();

	/** Internal, called by the Destroy request: clear the host state a client received through replication. */
	void ClearReplicatedSessionState();

	/**
	 * Internal, called by the state actor: receive the host's replicated session state.
	 * Clients store it, and GetSessionState returns it in place of their own session copy's state.
	 */
	void HandleReplicatedSessionState(EEasySessionState HostState);

	/**
	 * Internal, called by the state actor: receive the host's replicated session settings.
	 * Clients write them into their local session copy so the regular getters return the host's values, then broadcast OnSessionSettingsChanged.
	 */
	void HandleReplicatedSessionSettings(const FEasySessionReplicatedSettings& Settings);

private:

	/** Resolve the session interface for the current world context. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Add a request to the queue and start processing if idle. */
	void EnqueueRequest(TSharedRef<FEasySessionRequest> Request);

	/** Session state as derived from the local online subsystem session copy. */
	EEasySessionState GetLocalSessionState() const;

	/** Broadcast On Busy Changed when Is Busy differs from the last value reported. */
	void RefreshBusyState();

	/** Engine delegate handlers. */
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

private:

	/**
	 * Internal collaborators.
	 * Each owns the state and the engine delegates for one job, keeping that code out of this subsystem.
	 * Created in Initialize and destroyed in Deinitialize, which is what unbinds them.
	 */
	TUniquePtr<FEasySessionRequestQueue> RequestQueue;
	TUniquePtr<FEasySessionTravel> Travel;
	TUniquePtr<FEasySessionSocial> Social;
	TUniquePtr<FEasySessionBeaconPort> BeaconPort;
	TUniquePtr<FEasySessionHost> Host;

	/** What every request may use while it runs. Created after the collaborators it refers to. */
	TUniquePtr<FEasySessionRequestContext> RequestContext;

	/** The host's session state as last received through replication, on a client. Unset until it arrives for the current session. */
	TOptional<EEasySessionState> ReplicatedSessionState;

	/** Latest session settings applied through replication (clients only). Guards against re-applying the same payload. */
	FEasySessionReplicatedSettings AppliedReplicatedSessionSettings;

	/** The first disconnect since the game last consumed one. Kept across map travel. */
	TOptional<FEasyDisconnectInfo> PendingDisconnectInfo;

	/** Ticker that waits for the session interface before binding the invite delegates. */
	FTSTicker::FDelegateHandle InviteBindTickerHandle;

	/** Ticker that watches Is Busy for the transitions no single call site sees, such as a travel ending. */
	FTSTicker::FDelegateHandle BusyTickerHandle;

	/** Delegate handle for engine-level network failures. Bound for the subsystem lifetime. */
	FDelegateHandle NetworkFailureHandle;

	/** Delegate handle for engine-level travel failures. Bound for the subsystem lifetime. */
	FDelegateHandle TravelFailureHandle;

	/** The session state On Session State Changed last reported. */
	EEasySessionState LastReportedSessionState = EEasySessionState::NoSession;

	/** The Is Busy value On Busy Changed last reported. */
	bool bLastReportedBusy = false;
};
