// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "EasySessionTypes.h"
#include "Containers/Ticker.h"
#include "Templates/SubclassOf.h"
#include "EasySessionSubsystem.generated.h"

namespace ENetworkFailure
{
	enum Type : int;
}

class FEasySessionRequest;
class UEasyMatchmakingPolicy;

/** Multicast event fired when a session operation completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionEvent, EEasySessionResult, Result, const FString&, ErrorMessage);

/** Multicast event fired when a session search completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasySessionFindEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionSearchResult>&, Results);

/** Multicast event fired when the connection to the session is lost. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionFailureEvent, const FString&, Reason);

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

public:

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

	/** Fired when a QuickPlay matchmaking run completes. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnMatchmakingComplete;

	/** Fired when the connection to the session is lost or a network error occurs. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFailureEvent OnSessionFailure;

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
	 * @param OnComplete Called when the operation completes.
	 */
	void JoinEasySession(const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess = true, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

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
	 * Start QuickPlay matchmaking: search for sessions, join the best one, and
	 * optionally host a new session when nothing is found.
	 *
	 * @param QuickPlayParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when null.
	 * @param OnComplete Called when matchmaking completes.
	 */
	void StartQuickPlay(const FEasyQuickPlayParams& QuickPlayParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

public:

	/** Cancel the running QuickPlay matchmaking. Does nothing when no matchmaking is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession")
	void CancelMatchmaking();

	/** Check whether QuickPlay matchmaking is currently running. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsMatchmaking() const;

	/** Get the state of the running QuickPlay matchmaking. Idle when none is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	EEasyMatchmakingState GetMatchmakingState() const;

	/** Get the running matchmaking policy. Use this to bind its On State Changed event. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	UEasyMatchmakingPolicy* GetActiveMatchmakingPolicy() const { return ActiveMatchmakingPolicy; }

	/** Check whether the local player is currently in a session. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsInSession() const;

	/** Check whether the local player is hosting the current session. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsHost() const;

	/** Check whether a session operation is currently running or queued. */
	UFUNCTION(BlueprintPure, Category = "EasySession")
	bool IsBusy() const;

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

	/** Per-operation entry points, called by ProcessNextRequest. */
	void ExecuteCreate();
	void ExecuteFind();
	void ExecuteJoin();
	void ExecuteDestroy();
	void ExecuteUpdate();

	/** Online subsystem delegate handlers. */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type JoinResult);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

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
	void TravelToJoinedSession(const FString& ConnectString);

	/** Create the automatic session when running as a dedicated server. */
	void AutoHostDedicatedServerSession();

private:

	/** The matchmaking policy currently running QuickPlay. Null when idle. */
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

	/** Delegate handle for engine-level network failures. Bound for the subsystem lifetime. */
	FDelegateHandle NetworkFailureHandle;

	/** Ticker waiting for a valid world before auto hosting on a dedicated server. */
	FTSTicker::FDelegateHandle DedicatedAutoHostTickerHandle;

	/** Ticker that verifies the host became a listen server shortly after creating a session. */
	FTSTicker::FDelegateHandle ListenCheckTickerHandle;
};
