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
 * The host side stays up for the life of the session. The client side sends one request and closes.
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

	/** Stop watching and destroy the beacon. */
	void Shutdown();

	/**
	 * Host: start the beacon that handles join approval requests in the current world.
	 * Only starts when the session advertises the join approval key.
	 * Safe to call repeatedly; a beacon already running in this world is kept.
	 */
	void EnsureHost();

	/** Host: stop the beacon. Safe when none is running. */
	void StopHost();

	/**
	 * Joining player: ask Target's host to approve the local player joining.
	 * OnComplete fires exactly once, with Unreachable when the host cannot be reached.
	 * A new request cancels a pending one.
	 */
	void RequestJoinApproval(const FEasySessionSearchResult& Target, const FString& Password, const FEasyJoinApprovalComplete& OnComplete);

	/** Joining player: cancel a pending request, so its response never arrives. Safe when none is running. */
	void StopClient();

	/** @return The beacon host handling approvals, owned by this plugin or registered by the project. Null while none runs. */
	AOnlineBeaconHost* GetBeaconHost() const;

private:

	/** Re-creates the beacon after a travel replaced the world. Server only. */
	void HandleGameModeInitialized(AGameModeBase* GameMode);

	UEasySessionSubsystem& Owner;

	/** Handle for the game mode initialization event, which is what re-creates the beacon per world. */
	FDelegateHandle GameModeInitializedHandle;

	/** Handle for the one-tick delay between the game mode initializing and the beacon starting. */
	FTSTicker::FDelegateHandle DeferredEnsureHostHandle;

	/** Host side of the beacon. Lives exactly as long as the session, per world. */
	TWeakObjectPtr<AOnlineBeaconHost> BeaconHost;
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconHostObject> BeaconHostObject;

	/** Whether this plugin spawned BeaconHost and may destroy or unpause it. A host the project spawned is only registered on. */
	bool bOwnsBeaconHost = false;

	/** Joining player side. Lives for one request, from RequestJoinApproval to its response or StopClient. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconClient> BeaconClient;
};
