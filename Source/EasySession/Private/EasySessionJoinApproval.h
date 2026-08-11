// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "UObject/WeakObjectPtr.h"

class AGameModeBase;
class AOnlineBeaconHost;
class UEasySessionSubsystem;
struct FEasySessionSearchResult;

/**
 * The join approval beacon's transport: keeping the host side alive, and later
 * carrying the client's pre-travel question.
 *
 * Beacons are actors, so they die with their world. This object re-creates the host
 * beacon in every world the session reaches by watching game mode initialization,
 * which fires on the server once per map. Whether a request is approved is not
 * decided here - that is the server gate's job.
 *
 * Owned by the subsystem and destroyed with it. Delegates are bound raw because this
 * object cannot outlive the owner that unbinds them in Shutdown.
 */
class FEasySessionJoinApproval
{
public:

	explicit FEasySessionJoinApproval(UEasySessionSubsystem& InOwner)
		: Owner(InOwner)
	{
	}

	~FEasySessionJoinApproval();

	/** Start watching game mode initialization, which re-creates the beacon per world. */
	void Initialize();

	/** Stop watching and tear the beacon down. */
	void Shutdown();

	/**
	 * Host: start the beacon that answers join requests before a client travels.
	 * Only runs when the session advertises the join approval key.
	 */
	bool EnsureHost();

	/** Host: stop the beacon. Safe when none is running. */
	void StopHost();

	/**
	 * Joiner: request Target's host to approve the local player joining. The answer
	 * arrives through OnComplete exactly once, as Unreachable when the host cannot be
	 * reached. A new request cancels a pending one.
	 */
	void RequestJoinApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	/** Joiner: cancel a pending request, so its answer never arrives. Safe when none is running. */
	void StopClient();

private:

	/** Re-creates the beacon after a travel replaced the world. Server only. */
	void HandleGameModeInitialized(AGameModeBase* GameMode);

	/**
	 * Point the session at the port the beacon actually bound. Retries every frame until
	 * the request queue is idle: EnsureHost can run while a request is still active, and
	 * this update must not land then.
	 */
	bool TickAdvertiseBoundPort(float DeltaTime);

	UEasySessionSubsystem& Owner;

	FDelegateHandle GameModeInitializedHandle;

	/** Valid only while a re-advertisement is waiting for the queue. */
	FTSTicker::FDelegateHandle AdvertiseTickerHandle;

	/** Host side of the beacon. Lives exactly as long as the session, per world. */
	TWeakObjectPtr<AOnlineBeaconHost> BeaconHost;
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconHostObject> BeaconHostObject;

	/** Joiner side. Lives for one request, from RequestJoinApproval to its answer or StopClient. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconClient> BeaconClient;
};
