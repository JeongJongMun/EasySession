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
class FEasySessionRequest;
class FEasySessionServerGate;
class FEasySessionSocial;
class UEasyMatchmakingPolicy;

/** Multicast event fired when a session operation completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionEvent, EEasySessionResult, Result, const FString&, ErrorMessage);

/** Multicast event fired when a session search completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasySessionFindEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionSearchResult>&, Results);

/** Multicast event fired when the connection to the session is lost. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionFailureEvent, const FString&, Reason);

/** Multicast event fired when the player accepts an invite from the platform overlay. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionInviteAcceptedEvent, const FEasySessionSearchResult&, Session);

/** Multicast event fired when reading the friends list completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionFriend>&, Friends);

/**
 * Core subsystem of the EasySession plugin.
 * Automatically created for each game instance - no custom GameInstance class required.
 *
 * All operations are queued and executed one at a time, so they can be called in any
 * order without breaking the underlying online subsystem. Each operation reports its
 * result through the optional completion delegate and the matching multicast event.
 */
UCLASS()
class EASYSESSION_API UEasySessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	// Internal collaborators, not separate systems: they read the private queries this
	// subsystem already has rather than forcing those onto the public API, where they
	// would sit next to the ones users are meant to call and invite the wrong choice.
	friend class FEasySessionSocial;
	friend class FEasySessionServerGate;

public:

	/**
	 * Declared here and defined in the .cpp on purpose. The collaborators below are
	 * held by TUniquePtr to types this header only forward declares, and a compiler
	 * generated constructor or destructor would have to instantiate their deleters
	 * where those types are still incomplete - which is exactly where UHT puts the
	 * constructors it generates, including the hot reload one. The engine's own
	 * pimpl holders (UPrimitiveComponent, ULocalPlayer, UNetConnection) declare the
	 * same three for the same reason.
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

	/** Fired when a QuickMatch matchmaking run completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnMatchmakingComplete;

	/** Fired when the connection to the session is lost or a network error occurs. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFailureEvent OnSessionFailure;

	/**
	 * Fired when the player accepts an invite from the platform overlay.
	 * When Auto Join Accepted Invites is enabled the session is joined automatically
	 * right after this event.
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
	 * Starting a search clears the previous results, so nothing can display
	 * sessions from an older search while this one is running.
	 *
	 * @param SearchParams Parameters describing what to search for.
	 * @param OnComplete Called with the filtered results when the search completes.
	 */
	void FindEasySessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete = FEasySessionFindCompleteDelegate());

	/**
	 * Join the given session and optionally travel to the host.
	 *
	 * @param SearchResult A search result returned by FindEasySessions.
	 * @param bTravelOnSuccess Whether to client travel to the host once joined.
	 * @param Password Password for password protected sessions. Ignored otherwise.
	 * @param AdditionalTravelOptions Extra options appended to the client travel URL (e.g. "Name=Player?Team=1").
	 * @param OnComplete Called when the operation completes.
	 */
	void JoinEasySession(const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess = true, const FString& Password = FString(), const FString& AdditionalTravelOptions = FString(), FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start the match: transitions the session to InProgress. When Allow Join In Progress is
	 * disabled, new players can no longer join until the match ends.
	 *
	 * @param OnComplete Called when the operation completes.
	 */
	void StartEasySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * End the match: transitions the session back to Ended so a new match can be started.
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
	 * Update the advertised properties of the current session.
	 * Only the host can update the session.
	 *
	 * @param NewHostParams New parameters to advertise. Map Name and Host Mode are ignored.
	 * @param OnComplete Called when the operation completes.
	 */
	void UpdateEasySession(const FEasySessionHostParams& NewHostParams, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start QuickMatch matchmaking: search for sessions, join the best one, and
	 * optionally host a new session when nothing is found.
	 *
	 * @param QuickMatchParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when null.
	 * @param OnComplete Called when matchmaking completes.
	 */
	void StartQuickMatch(const FEasyQuickMatchParams& QuickMatchParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

public:

	/** Cancel the running QuickMatch matchmaking. Does nothing when no matchmaking is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession")
	void CancelMatchmaking();

	/** Check whether QuickMatch matchmaking is currently running. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsMatchmaking() const;

	/** Get the state of the running QuickMatch matchmaking. Idle when none is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	EEasyMatchmakingState GetMatchmakingState() const;

	/** Get the running matchmaking policy. Use this to bind its On State Changed event. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	UEasyMatchmakingPolicy* GetActiveMatchmakingPolicy() const { return ActiveMatchmakingPolicy; }

	/** Check whether the local player is currently in a session. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsInSession() const;

	/**
	 * Get the lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 * On the host this is the authoritative local state; on clients it is the host's
	 * replicated state, so every player always sees the same value.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	EEasySessionState GetSessionState() const;

	/**
	 * Internal: receive the host's replicated session state (called by the state
	 * actor). Clients cache it for display and reconcile their local session copy.
	 */
	void HandleReplicatedHostSessionState(EEasySessionState HostState);

	/** Check whether the local player is hosting the current session. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsHost() const;

	/**
	 * Get the display names of all players currently in the session, including the local player.
	 * Names come from the replicated player states, so the list is available on both the host and clients.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	TArray<FString> GetSessionPlayerNames() const;

	/** Get the display name of the current session. Empty when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	FString GetSessionDisplayName() const;

	/**
	 * Get the password of the session this game is hosting, e.g. to display it so the
	 * host can share it. Empty on clients and for password-less sessions - the
	 * password never leaves the host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	FString GetSessionPassword() const;

	/**
	 * Get per-player info for everyone in the session: name, whether it is the
	 * local player on this machine, and whether it is the session host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	TArray<FEasySessionPlayerInfo> GetSessionPlayerInfos() const;

	/** Get the number of players currently in the session. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	int32 GetSessionPlayerCount() const;

	/** Get the maximum number of players allowed in the current session. Returns 0 when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	int32 GetSessionMaxPlayers() const;

	/**
	 * Check whether any session operation is in progress - a request running or queued,
	 * a Quick Match still working through its steps, or a travel this plugin started
	 * that has not reached its map yet.
	 * Bind session buttons to this to disable them while an operation runs; use
	 * Is Matchmaking to ask specifically about Quick Match.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsBusy() const;

	/**
	 * Describe what the session queue is doing right now, e.g.
	 * "Create (running 2.4s of 30s), 1 queued" or "Idle".
	 * Meant for status UI, the EasySession.Status console command and bug reports.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	FString GetQueueStatusDescription() const;

	/** Get the results of the most recent session search. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	const TArray<FEasySessionSearchResult>& GetLastSearchResults() const { return LastSearchResults; }

	/** Get the name of the online subsystem currently in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	FName GetOnlineSubsystemName() const;

	/** Check whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsOnlineSubsystemAvailable() const;

	/**
	 * ServerTravel the current session to a new map. Only the host can travel the session.
	 * Additional travel options can be appended with '?'. The ?listen option is added
	 * automatically when hosting a listen server session.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "EasySession")
	bool ServerTravelToMap(const FString& MapName);

	/**
	 * Destroy the session for every player: remote clients are sent back to the menu
	 * with the given reason (shown by their menu popup), then the host destroys the
	 * session as well. Like Destroy Easy Session, the session is gone afterwards -
	 * use End Easy Session to only finish the match and keep the session alive.
	 * Only the session host can call this.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "EasySession")
	void DestroyEasySessionForEveryone(FText Reason);

	/** Invite a friend to the current session. Not supported on the NULL (LAN) subsystem. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites")
	bool SendSessionInviteToFriend(const FEasySessionFriend& Friend);

	/** Open the platform invite overlay (e.g. Steam) for the current session. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites")
	bool ShowInviteUI();

	/** Open the platform profile overlay (e.g. Steam) for the given friend. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites")
	bool ShowProfileUI(const FEasySessionFriend& Friend);

	/** Open the platform profile overlay (e.g. Steam) for a player in the session. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites")
	bool ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player);

	/**
	 * Read the local player's friends list. Not supported on the NULL (LAN) subsystem.
	 *
	 * @param OnComplete Called with the friends when the read completes.
	 */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete = FEasyFriendsCompleteDelegate());

	/** Check whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool HasPendingDisconnectInfo() const { return bHasPendingDisconnectInfo; }

	/** Get the last disconnect info and clear the pending flag. Survives map travel. */
	UFUNCTION(BlueprintCallable, Category = "EasySession")
	FEasyDisconnectInfo ConsumeLastDisconnectInfo();

	/**
	 * Record a disconnect and run the recovery flow: destroy the dead session and travel
	 * back to the menu map configured in the EasySession project settings.
	 * Called automatically on network and travel failures; call it manually only if you
	 * detect disconnects yourself. The first recorded reason wins until it is consumed.
	 */
	void NotifyDisconnectedFromSession(EEasyDisconnectReason Reason, const FText& ReasonText);

public:

	/** C++ hook: modify the server travel URL (hosting / server travel) before it is used. */
	FEasyModifyTravelURLDelegate OnModifyServerTravelURL;

	/** C++ hook: modify the client travel URL (joining a host) before it is used. */
	FEasyModifyTravelURLDelegate OnModifyClientTravelURL;

private:

	/** Resolve the session interface for the current world context. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Whether LAN mode must be forced because the NULL subsystem is active. */
	bool ShouldForceLAN() const;

	/** Add a request to the queue and start processing if idle. */
	void EnqueueRequest(TSharedRef<FEasySessionRequest> Request);

	/** Start the next queued request if no request is active. */
	void ProcessNextRequest();

	/** Finish the active request and schedule the next one. */
	void CompleteActiveRequest(EEasySessionResult Result, const FString& ErrorMessage = FString());

	/**
	 * Watchdog for the active request. Online services are not guaranteed to report
	 * completion, and a request that never completes would stall every queued
	 * request behind it - the watchdog fails it with Timeout so the queue drains.
	 */
	void StartRequestWatchdog();
	void StopRequestWatchdog();
	bool TickRequestWatchdog(float DeltaTime);

	/** Per-operation entry points, called by ProcessNextRequest. */
	void ExecuteCreate();
	void ExecuteFind();
	void ExecuteJoin();
	void ExecuteDestroy();
	void ExecuteUpdate();
	void ExecuteStart();
	void ExecuteEnd();

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

	/**
	 * Remember that this subsystem sent the player somewhere, and forget it once the
	 * map is up. The online call that precedes a travel can finish in the same frame
	 * - the NULL subsystem completes Start, End and Join inside the call itself - so
	 * the queue alone reports "idle" while the player is still watching a level load.
	 */
	void MarkTravelStarted(const TCHAR* Reason);
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** Hand control back to the engine's main-menu flow (browses to the Game Default Map). */
	void ReturnToMenu();

	/** Session state as derived from the local online subsystem session copy. */
	EEasySessionState GetLocalSessionState() const;

	/**
	 * Host: make sure the replicated state actor exists in the current world and
	 * carries the current session state. Clients never spawn it.
	 */
	void EnsureStateActor();

	/** Host: push the current local session state to the replicated state actor. */
	void PushHostSessionState();

	/** Spawn/refresh the state actor after every map load while hosting. */
	void HandleWorldInitializedActors(const struct FActorsInitializedParams& Params);

	/**
	 * Register / unregister the local player in the current session.
	 * Registration is what drives the advertised open slot count of a session.
	 */
	void RegisterLocalPlayerInSession();
	void UnregisterLocalPlayerFromSession();

	/** Make sure the host is (or becomes) a listen server after creating a session. */
	void EnsureHostIsListening(const FEasySessionHostParams& HostParams);

	/** Travel helpers executed after successful create/join. */
	void TravelToOwnSession(const FEasySessionHostParams& HostParams);
	void TravelToJoinedSession(const FString& ConnectString, const FString& Password, const FString& AdditionalTravelOptions);

	/** Append a travel option string ("A=1?B=2") to a URL, normalizing the '?' separators. */
	static void AppendTravelOptions(FString& InOutURL, const FString& Options);

	/** Create the automatic session when running as a dedicated server. */
	void AutoHostDedicatedServerSession();

private:

	/** The matchmaking policy currently running QuickMatch. Null when idle. */
	UPROPERTY()
	TObjectPtr<UEasyMatchmakingPolicy> ActiveMatchmakingPolicy;

	/** The request currently being executed. Only one request runs at a time. */
	TSharedPtr<FEasySessionRequest> ActiveRequest;

	/** Requests waiting for the active request to finish. */
	TArray<TSharedRef<FEasySessionRequest>> PendingRequests;

	/** The native search object of the find operation in flight. */
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;

	/** Cached results of the most recent search. */
	TArray<FEasySessionSearchResult> LastSearchResults;

	/** Delegate handles for the operation in flight. Cleared when the operation completes. */
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

	/** Ticker that verifies the host became a listen server shortly after creating a session. */
	FTSTicker::FDelegateHandle ListenCheckTickerHandle;

	/** Ticker watching the active request for a timeout. Runs only while the queue is busy. */
	FTSTicker::FDelegateHandle RequestWatchdogHandle;

	/** Whether a travel this subsystem started is still on its way to a loaded map. */
	bool bTravelInFlight = false;

	/** Delegate handle for map loads, which end a travel. Bound for the subsystem lifetime. */
	FDelegateHandle PostLoadMapHandle;

	/**
	 * Internal collaborators. Each owns the state and the engine delegates for one job,
	 * so this subsystem stays the place users call and not the place everything lives.
	 * Created in Initialize and destroyed in Deinitialize, which is what unbinds them.
	 */
	TUniquePtr<FEasySessionSocial> Social;
	TUniquePtr<FEasySessionServerGate> ServerGate;
};
