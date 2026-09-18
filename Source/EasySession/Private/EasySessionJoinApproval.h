// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "UObject/WeakObjectPtr.h"

class FEasySessionBeaconPort;
class FOnlineSessionSettings;
class UEasySessionSubsystem;
struct FEasySessionSearchResult;

/**
 * Runs the connection the join approval beacon uses.
 * The host side stays up for the life of the session. The client side sends one request and closes.
 *
 * Beacons are actors, so they are destroyed with their world.
 * FEasySessionHost calls EnsureHost again in every world the session reaches, and StopHost before a server travel and when the session is destroyed.
 * The host object registers on the shared FEasySessionBeaconPort, so this object never spawns a listener of its own.
 * Whether a request is approved is decided by FEasySessionServerGate, not here.
 *
 * Owned by FEasySessionHost and destroyed with it.
 */
class FEasySessionJoinApproval
{
public:

	/**
	 * The beacon port is where the host object registers.
	 * Every beacon family of the plugin shares it.
	 */
	FEasySessionJoinApproval(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
		: Owner(InOwner)
		, BeaconPort(InBeaconPort)
	{
	}

	/** Stops the host side and cancels a pending request. */
	~FEasySessionJoinApproval();

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

	/** @return Whether a session with these settings runs the join approval beacon, so a joining player requests join approval before traveling. */
	static bool IsAdvertisedBy(const FOnlineSessionSettings& Settings);

private:

	/** Warn when the beacon port bound another port number than the session advertises, because joining players connect to the advertised port. */
	void WarnIfPortMismatch(const FOnlineSessionSettings& Settings) const;

	UEasySessionSubsystem& Owner;

	/**
	 * The shared beacon port the host object registers on.
	 * Owned by the subsystem and destroyed after this object.
	 */
	FEasySessionBeaconPort& BeaconPort;

	/** Host side of the beacon. Lives exactly as long as the session, per world. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconHostObject> BeaconHostObject;

	/** Joining player side. Lives for one request, from RequestJoinApproval to its response or StopClient. */
	TWeakObjectPtr<AEasySessionJoinApprovalBeaconClient> BeaconClient;
};
