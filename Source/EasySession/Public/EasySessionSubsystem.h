// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "EasySessionTypes.h"
#include "EasyMatchmakingPolicy.h"
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
class FEasySessionJoinApproval;
class FEasySessionRequest;
class FEasySessionRequestQueue;
class FEasySessionServerGate;
class FEasySessionSocial;
class FEasySessionTravel;
struct FEasyJoinApprovalResponse;

/** Multicast event fired when a session operation completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionEvent, EEasySessionResult, Result, const FString&, ErrorMessage);

/** Multicast event fired when a session search completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasySessionFindEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionSearchResult>&, Results);

/** Multicast event fired when the connection to the session is lost. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionFailureEvent, const FString&, Reason);

/** Multicast event fired when the player accepts an invite from the platform overlay. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionInviteAcceptedEvent, const FEasySessionSearchResult&, Session);

/** Multicast event fired when a matchmaking run is accepted and its policy is registered. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasyMatchmakingStartedEvent);

/** Multicast event fired when reading the friends list completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionFriend>&, Friends);

/**
 * Core subsystem of the EasySession plugin.
 * Automatically created for each game instance - no custom GameInstance class required.
 *
 * All operations are queued and executed one at a time, so they can be called in any order without breaking the underlying online subsystem.
 * Each operation reports its result through the optional completion delegate and the matching multicast event.
 */
UCLASS()
class EASYSESSION_API UEasySessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	//~ Internal collaborators, not separate systems.
	//~ They read the private queries this subsystem already has rather than forcing those onto the public API.
	//~ On the public API users would have to tell them apart from the queries meant for them.
	friend class FEasySessionSocial;
	friend class FEasySessionServerGate;
	friend class FEasySessionJoinApproval;
	friend class AEasySessionJoinApprovalBeaconHostObject;
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

	/** Fired when a Create Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionCreated;

	/** Fired when a Find Sessions operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFindEvent OnSessionsFound;

	/** Fired when a Join Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionJoined;

	/** Fired when a Destroy Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionDestroyed;

	/** Fired when an Update Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionUpdated;

	/** Fired when a Start Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionStarted;

	/** Fired when an End Session operation completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnSessionEnded;

	/** Fired when a Matchmaking run is accepted and its policy is registered - the first of that run's events, and the moment Get Active Matchmaking Policy starts returning it. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStartedEvent OnMatchmakingStarted;

	/** Relays the active policy's OnStateChanged, so progress UI can bind here once instead of chasing each run's policy. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStateEvent OnMatchmakingStateChanged;

	/** Relays the active policy's OnUpdated: every state change plus a once-a-second heartbeat carrying the elapsed whole seconds. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingUpdatedEvent OnMatchmakingUpdated;

	/** Fired when a Matchmaking run completes, a canceled one included - cancellation arrives as the Canceled result, never as a separate event. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnMatchmakingComplete;

	/** Fired when the connection to the session is lost or a network error occurs. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFailureEvent OnSessionFailure;

	/**
	 * Fired when the player accepts an invite from the platform overlay.
	 * With Auto Join Accepted Invites on, this player joins the invited session right after this event, unless they are already in one.
	 * A player who is already in a session joins only when Accept Invites While In Session is on, and their current session is destroyed first - which disconnects everyone if they were hosting it.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionInviteAcceptedEvent OnSessionInviteAccepted;

public:

	/**
	 * Create a new session and optionally travel to the session map.
	 * For listen servers the map is opened with the ?listen option automatically.
	 *
	 * @param HostParams Parameters describing the session to create.
	 * @param OnComplete Called when the operation completes.
	 */
	void CreateEasySession(const FEasySessionHostParams& HostParams, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Search for sessions matching the given filters.
	 * Results are also cached and can be read back via GetLastSearchResults.
	 * Starting a search clears the previous results, so nothing can display sessions from an older search while this one is running.
	 *
	 * @param SearchParams Parameters describing what to search for.
	 * @param OnComplete Called with the filtered results when the search completes.
	 */
	void FindEasySessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete = FEasySessionFindCompleteDelegate());

	/**
	 * Join the given session and travel to the host.
	 *
	 * @param SearchResult A search result returned by FindEasySessions.
	 * @param Password Password for password protected sessions. Ignored otherwise.
	 * @param AdditionalTravelOptions Extra options appended to the client travel URL (e.g. "Name=Player?Team=1").
	 * @param OnComplete Called when the operation completes.
	 */
	void JoinEasySession(const FEasySessionSearchResult& SearchResult, const FString& Password = FString(), const FString& AdditionalTravelOptions = FString(), FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Find the session advertising this join code and join it. Hidden sessions count - a code names one exact room.
	 * The lookup search stays off the public surfaces: no OnSessionsFound broadcast, no GetLastSearchResults cache.
	 *
	 * @param JoinCode The code the host reads from GetSessionJoinCode. Case does not matter.
	 * @param Password Password for password protected sessions. Ignored otherwise.
	 * @param OnComplete Called when the operation completes. NoSessionsFound when no session advertises the code.
	 */
	void JoinEasySessionByCode(const FString& JoinCode, const FString& Password = FString(), FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start the match: transitions the session to InProgress.
	 * When Allow Join In Progress is disabled, new players are refused from here until the match ends - except on Steam, which already refused them from the first join onwards.
	 * Needs session authority - only the game that created the session can start the match.
	 *
	 * @param OnComplete Called when the operation completes.
	 */
	void StartEasySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * End the match: transitions the session back to Ended so a new match can be started.
	 * Needs session authority - only the game that created the session can end the match.
	 *
	 * @param OnComplete Called when the operation completes.
	 */
	void EndEasySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Destroy the current session, leaving it if we are a client.
	 *
	 * @param OnComplete Called when the operation completes.
	 */
	void DestroyEasySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Leave the session: destroy this game's named session, then return to the menu map.
	 * A leaving host takes the room with it, so it closes the room the polite way instead - every client hears "The host has left the game." before the connection dies.
	 * The menu travel runs whatever the destroy reported - the player asked to leave, and a named session that failed to delete is no reason to keep them on the map.
	 *
	 * @param OnComplete Called with the destroy's result, after the menu travel was requested.
	 */
	void LeaveEasySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Update the advertised properties of the current session.
	 * Needs session authority - only the game that created the session can update it.
	 *
	 * Every field is applied as given, including Password.
	 * Pass params from GetSessionHostParams and change only what you mean to change.
	 * Otherwise the fields you left at their defaults overwrite the session with those defaults.
	 *
	 * @param NewHostParams New parameters to advertise. Map Name, Host Mode, Start
	 *        Listening, LAN Match, Use Presence and Additional Travel Options are
	 *        fixed when the session is created and are ignored here.
	 * @param OnComplete Called when the operation completes.
	 */
	void UpdateEasySession(const FEasySessionHostParams& NewHostParams, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start Matchmaking: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 *
	 * @param MatchmakingParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when null.
	 * @param OnComplete Called when matchmaking completes.
	 */
	void StartMatchmaking(const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

public:

	/** Cancel the running Matchmaking. A join or host that succeeds after the cancel is undone. Does nothing when no matchmaking is running. */
	void CancelMatchmaking();

	/** Check whether Matchmaking is currently running. */
	bool IsMatchmakingRunning() const;

	/** Get the state of the running Matchmaking. Idle when none is running. */
	EEasyMatchmakingState GetMatchmakingState() const;

	/** Get the running matchmaking policy. Use this to bind its On State Changed event. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	UEasyMatchmakingPolicy* GetActiveMatchmakingPolicy() const { return ActiveMatchmakingPolicy; }

	/**
	 * Check whether the local player is currently in a session.
	 *
	 * This and the queries below it are all about the game session and take no session argument - a future party gets its own queries, because these bodies read the world and the match lifecycle, which a party does not have.
	 */
	bool IsInSession() const;

	/**
	 * Get the lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 * The host reports its own state; a client reports the host's replicated state once it has arrived, and its own until then.
	 */
	EEasySessionState GetSessionState() const;

	/**
	 * @return The parameters the current session is running with, so a change can be made without restating everything else.
	 *         Get these, edit the one field, pass them to Update Easy Session - building fresh params instead resets every field you did not fill in.
	 *         Host only. Returns defaults on a client, when there is no session, and for the fields Update cannot change.
	 */
	FEasySessionHostParams GetSessionHostParams() const;

	/**
	 * @return The join code the current session advertises, or empty when it advertises none.
	 *         Works for every player in the session, so anyone in the room can share the code.
	 */
	FString GetSessionJoinCode() const;

	/**
	 * Internal: receive the host's replicated session state (called by the state actor).
	 * Clients cache it for display and reconcile their local session copy.
	 */
	void HandleReplicatedHostSessionState(EEasySessionState HostState);

	/**
	 * Check whether the local player is hosting the current session.
	 * Always false on a dedicated server, which has no local player - call IsSessionAuthority there instead.
	 */
	bool IsHost() const;

	/**
	 * Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 * Do not use Is Host instead - it is false on a dedicated server.
	 */
	bool IsSessionAuthority() const;

	/**
	 * Get the display names of all players currently in the session, including the local player.
	 * Names come from the replicated player states, so the list is available on both the host and clients.
	 */
	TArray<FString> GetSessionPlayerNames() const;

	/** Get the display name of the current session. Empty when no session exists. */
	FString GetSessionDisplayName() const;

	/**
	 * Get the password of the session this game is hosting, e.g. to display it so the host can share it.
	 * Empty on clients and for password-less sessions - the password never leaves the host.
	 */
	FString GetSessionPassword() const;

	/**
	 * Get per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host.
	 */
	TArray<FEasySessionPlayerInfo> GetSessionPlayerInfos() const;

	/** Get the number of players currently in the session. */
	int32 GetSessionPlayerCount() const;

	/** Get the maximum number of players allowed in the current session. Returns 0 when no session exists. */
	int32 GetSessionMaxPlayers() const;

	/**
	 * Check whether any session operation is in progress.
	 * That covers a request running or queued, a Matchmaking working through its steps, and a travel this plugin started that has not reached its map yet.
	 * Bind session buttons to this to disable them while an operation runs; use IsMatchmakingRunning to ask specifically about Matchmaking.
	 */
	bool IsBusy() const;

	/**
	 * Describe what the session queue is doing right now, e.g. "Create (running 2.4s of 30s), queued: Start" or "Idle".
	 * Meant for status UI, the EasySession.Status console command and bug reports.
	 */
	FString GetQueueStatus() const;

	/** Get the results of the most recent session search. */
	const TArray<FEasySessionSearchResult>& GetLastSearchResults() const { return LastSearchResults; }

	/** Get the name of the online subsystem currently in use (e.g. NULL, STEAM, EOS). */
	FName GetOnlineSubsystemName() const;

	/** Check whether an online subsystem is available and its session interface is valid. */
	bool IsOnlineSubsystemAvailable() const;

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'. The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Needs session authority - only the game that created the session can travel it, and it returns false for other games.
	 */
	bool ServerTravelToMap(const FString& MapName);

	/**
	 * Drop a travel this plugin requested that has not started loading its map.
	 * Matchmaking uses it when a cancel lands after a join or host already succeeded, right before destroying that session.
	 */
	void CancelPendingTravel();

	/**
	 * @return Whether a destroy for the current session is already running or waiting.
	 * Matchmaking reads it to tell a session on its way out from one that is here to stay.
	 */
	bool IsSessionBeingDestroyed() const;

	/**
	 * Destroy the session for every player.
	 * Remote clients record Reason as a Host Destroyed Session disconnect and return to the menu, where reading it with Consume Last Easy Disconnect Info is what shows it to the player.
	 * Needs session authority - only the game that created the session can do this.
	 *
	 * @param OnComplete Called with the destroy's result, after the host's own menu travel was requested.
	 */
	void DestroyEasySessionForEveryone(FText Reason, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/** Invite a friend to the current session. Not supported on the NULL (LAN) subsystem. */
	bool SendSessionInviteToFriend(const FEasySessionFriend& Friend);

	/** Open the platform invite overlay (e.g. Steam) for the current session. */
	bool ShowInviteUI();

	/** Open the platform profile overlay (e.g. Steam) for the given friend. */
	bool ShowProfileUI(const FEasySessionFriend& Friend);

	/** Open the platform profile overlay (e.g. Steam) for a player in the session. */
	bool ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player);

	/**
	 * Read the local player's friends list. Not supported on the NULL (LAN) subsystem.
	 *
	 * @param OnComplete Called with the friends when the read completes.
	 */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete = FEasyFriendsCompleteDelegate());

	/** Check whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	bool HasPendingDisconnectInfo() const { return bHasPendingDisconnectInfo; }

	/** Get the last disconnect info and clear the pending flag. Survives map travel. */
	FEasyDisconnectInfo ConsumeLastDisconnectInfo();

	/**
	 * Record a disconnect and run the recovery flow.
	 * The dead session is destroyed, then the game returns to the project's Game Default Map when Auto Return To Menu On Disconnect is enabled.
	 * Called automatically on network and travel failures; call it manually only if you detect disconnects yourself.
	 * The first reason recorded is kept until it is consumed, so a follow-up failure cannot replace the real cause.
	 */
	void NotifyDisconnectedFromSession(EEasyDisconnectReason Reason, const FText& ReasonText);

public:

	/**
	 * C++ hook: modify the server travel URL (hosting / server travel) before it is used.
	 * Bind at startup. The hook fires before the completion callback of the operation that travels, so binding inside that callback misses its own travel.
	 * For one operation's options, use Additional Travel Options on the params instead.
	 */
	FEasyModifyTravelURLDelegate OnModifyServerTravelURL;

	/**
	 * C++ hook: modify the client travel URL (joining a host) before it is used.
	 * This URL carries the session password as an option - do not log it.
	 * Bind at startup. The hook fires before the completion callback of the operation that travels, so binding inside that callback misses its own travel.
	 * For one operation's options, use Additional Travel Options on the params instead.
	 */
	FEasyModifyTravelURLDelegate OnModifyClientTravelURL;

private:

	/** Resolve the session interface for the current world context. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Whether LAN mode must be forced because the NULL subsystem is active. */
	bool ShouldForceLAN() const;

	/** Add a request to the queue and start processing if idle. */
	void EnqueueRequest(TSharedRef<FEasySessionRequest> Request);

	/** The request the queue is running right now. Null while the queue is idle. */
	const TSharedPtr<FEasySessionRequest>& GetActiveRequest() const;

	/** Queue callback: dispatch the request that just became active to its executor. */
	void ExecuteActiveRequest();

	/**
	 * Queue callback: the active request passed its deadline.
	 * Online services are not guaranteed to report completion, and a request that never completes would stall every queued request behind it.
	 * This fails it with Timeout so the queue can continue, then cleans up any session the operation may still create afterwards.
	 */
	void HandleRequestDeadline();

	/**
	 * Finish the active request and schedule the next one.
	 *
	 * bAbandoned says the request is being given up on rather than reporting back, which is what the watchdog does.
	 * See CleanupRequest for what changes.
	 */
	void CompleteActiveRequest(EEasySessionResult Result, const FString& ErrorMessage = FString(), bool bAbandoned = false);

	/**
	 * Undo what the request left behind, so the next one starts clean.
	 * Every completion passes through here, which is what stops a request type from being cleaned up on one path and forgotten on the other.
	 *
	 * A request that reported back has already told the online service it is over.
	 * One that was abandoned has not, and the online service is still working on it - the only case where the service itself has to be told to stop.
	 */
	void CleanupRequest(const FEasySessionRequest& Request, bool bAbandoned);

	/** Per-operation entry points, called by ExecuteActiveRequest. */
	void ExecuteCreate();
	void ExecuteFind();
	void ExecuteJoin();
	void ExecuteDestroy();
	void ExecuteUpdate();
	void ExecuteStart();
	void ExecuteEnd();

	/** Build the settings a new session is created and advertised with. */
	FOnlineSessionSettings MakeCreateSettings(const FEasySessionHostParams& Params, bool bIsDedicated);

	/** Ask the host's approval beacon whether the local player may join. */
	void RequestJoinApproval();

	/** The beacon's answer: join the session, or fail the request with the reason. */
	void HandleJoinApprovalResponse(const FEasyJoinApprovalResponse& Response);

	/** Ask the online service to join. Every join path ends in this step. */
	void JoinOnlineSession();

	/** Online subsystem delegate handlers. */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type JoinResult);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleStartSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleEndSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** Relay the active policy's state change to OnMatchmakingStateChanged. */
	UFUNCTION()
	void RelayMatchmakingStateChanged(EEasyMatchmakingState OldState, EEasyMatchmakingState NewState);

	/** Relay the active policy's update to OnMatchmakingUpdated. */
	UFUNCTION()
	void RelayMatchmakingUpdated(EEasyMatchmakingState MatchmakingState, int32 ElapsedSeconds);

	/** Hand control back to the engine's main-menu flow (browses to the Game Default Map). */
	void ReturnToMenu();

	/** Session state as derived from the local online subsystem session copy. */
	EEasySessionState GetLocalSessionState() const;

	/**
	 * Whether this process is the server of the current world.
	 * Every net mode below NM_Client is a kind of server, so a listen server, a dedicated server and a standalone game all answer true.
	 */
	bool IsNetworkServer() const;

	/**
	 * Server: spawn the replicated state actor if the current world has none, then push the current session state into it.
	 * Clients never spawn it.
	 */
	void EnsureStateActor();

	/** Server: push the current local session state to the replicated state actor. */
	void PushHostSessionState();

	/** Spawn/refresh the state actor after every map load while hosting. */
	void HandleWorldInitializedActors(const struct FActorsInitializedParams& Params);

	/** Create the automatic session when running as a dedicated server. */
	void AutoHostDedicatedServerSession();

private:

	/** The matchmaking policy currently running Matchmaking. Null when idle. */
	UPROPERTY()
	TObjectPtr<UEasyMatchmakingPolicy> ActiveMatchmakingPolicy;

	/**
	 * Whether the session that exists now was created by this process.
	 * Neither value the engine offers can answer that.
	 * Steam never writes FNamedOnlineSession's bHosting - only some services do, NULL among them.
	 * Comparing the session owner against the local player fails on a dedicated server, which has none.
	 * Every request passes through this queue, so creating a session is a fact this plugin already knows and can simply record.
	 *
	 * Read it through IsSessionAuthority, never directly.
	 * That pairs it with the session actually existing, so losing the session by any route also clears the authority.
	 */
	bool bCreatedActiveSession = false;

	/** The native search object of the running find operation. */
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;

	/** Cached results of the most recent search. */
	TArray<FEasySessionSearchResult> LastSearchResults;

	/** Delegate handles for the running operation. Cleared when it completes. */
	FDelegateHandle CreateCompleteHandle;
	FDelegateHandle FindCompleteHandle;
	FDelegateHandle JoinCompleteHandle;
	FDelegateHandle DestroyCompleteHandle;
	FDelegateHandle UpdateCompleteHandle;
	FDelegateHandle StartCompleteHandle;
	FDelegateHandle EndCompleteHandle;

	/** Replicated session-wide state actor. Spawned by the host, observed by clients. */
	TWeakObjectPtr<class AEasySessionStateActor> StateActor;

	/** Latest host session state received through replication (clients only). */
	EEasySessionState ReplicatedHostSessionState = EEasySessionState::NoSession;

	/** Whether a replicated host state has been received for the current session. */
	bool bHasReplicatedHostSessionState = false;

	/** Delegate handle for per-map state actor respawns. */
	FDelegateHandle WorldInitializedActorsHandle;

	/** Ticker that waits for the session interface before binding the invite delegates. */
	FTSTicker::FDelegateHandle InviteBindTickerHandle;

	/** Info about the most recent disconnect. Kept until consumed so it survives map travel. */
	FEasyDisconnectInfo LastDisconnectInfo;

	/** Whether LastDisconnectInfo has not been consumed yet. */
	bool bHasPendingDisconnectInfo = false;

	/** Delegate handle for engine-level network failures. Bound for the subsystem lifetime. */
	FDelegateHandle NetworkFailureHandle;

	/** Delegate handle for engine-level travel failures. Bound for the subsystem lifetime. */
	FDelegateHandle TravelFailureHandle;

	/** Ticker waiting for a valid world before auto hosting on a dedicated server. */
	FTSTicker::FDelegateHandle DedicatedAutoHostTickerHandle;

	/**
	 * Internal collaborators.
	 * Each owns the state and the engine delegates for one job, keeping that code out of this subsystem.
	 * Created in Initialize and destroyed in Deinitialize, which is what unbinds them.
	 */
	TUniquePtr<FEasySessionRequestQueue> RequestQueue;
	TUniquePtr<FEasySessionTravel> Travel;
	TUniquePtr<FEasySessionSocial> Social;
	TUniquePtr<FEasySessionServerGate> ServerGate;
	TUniquePtr<FEasySessionJoinApproval> JoinApproval;
};
