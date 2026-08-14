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
 * Runs the connection the join approval beacon uses.
 * The host side stays up for the life of the session; the client side carries one question and closes.
 *
 * Beacons are actors, so they are destroyed with their world.
 * This object re-creates the host beacon in every world the session reaches by watching game mode initialization, which the server runs once per map.
 * Whether a request is approved is decided by FEasySessionServerGate, not here.
 *
 * Owned by the subsystem and destroyed with it.
 * Delegates are bound raw because this object cannot outlive the owner that unbinds them in Shutdown.
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
	 * Host: start the beacon that answers join requests in the current world.
	 * Only starts when the session advertises the join approval key.
	 * Safe to call repeatedly; a beacon already running in this world is kept.
	 */
	void EnsureHost();

	/** Host: stop the beacon. Safe when none is running. */
	void StopHost();

	/**
	 * Joiner: ask Target's host to approve the local player joining.
	 * The answer arrives through OnComplete exactly once, as Unreachable when the host cannot be reached.
	 * A new request cancels a pending one.
	 */
	void RequestJoinApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	/** Joiner: cancel a pending request, so its answer never arrives. Safe when none is running. */
	void StopClient();

private:

	/** Re-creates the beacon after a travel replaced the world. Server only. */
	void HandleGameModeInitialized(AGameModeBase* GameMode);

	UEasySessionSubsystem& Owner;

	/** Handle for the game mode initialization event, which is what re-creates the beacon per world. */
	FDelegateHandle GameModeInitializedHandle;

	/** Host side of the beacon. Lives exactly as long as the session, per world. */
	TWeakObjectPtr<AOnlineBeaconHost> BeaconHost;
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconHostObject> BeaconHostObject;

	/** Joiner side. Lives for one request, from RequestJoinApproval to its answer or StopClient. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconClient> BeaconClient;
};
