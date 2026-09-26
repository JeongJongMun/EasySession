// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AOnlineBeaconHost;
class AOnlineBeaconHostObject;
class UWorld;

/**
 * The one beacon port of this process, shared by every beacon family of the plugin.
 * It keeps the one beacon listener that binds the port, and counts the host objects registered on it.
 *
 * A beacon listener (AOnlineBeaconHost) binds one port, and each beacon family registers a host object on it.
 * The reservation beacon is the only family today.
 * The party beacon registers on the same listener later, so neither family spawns a listener of its own.
 * A project that already runs a listener keeps it.
 * Host objects register on the project's listener, and this object never pauses, resumes or destroys it.
 *
 * The listener is an actor, so it is destroyed with its world, together with every host object registered on it.
 * Each family registers again in the next world.
 *
 * Owned by the subsystem and destroyed with it.
 */
class FEasySessionBeaconPort
{
public:

	~FEasySessionBeaconPort();

	/**
	 * Register a host object in its own world, spawning the listener when that world has none.
	 *
	 * @return Whether the host object now receives requests. False when the listener could not start.
	 */
	bool Register(AOnlineBeaconHostObject& HostObject);

	/**
	 * Unregister a host object.
	 * A listener this object spawned is destroyed when its last host object unregisters.
	 */
	void Unregister(AOnlineBeaconHostObject& HostObject);

	/**
	 * Destroy the listener this object spawned, whatever is still registered on it, but never a listener the project spawned.
	 * Called before a server travel, because the listener of the next world can only bind the port after this one released it.
	 */
	void ReleaseListener();

	/** @return The port the listener bound, or 0 while no host object is registered. */
	int32 GetListenPort() const;

	/** @return The listener the host objects are registered on, this object's own or the project's. Null while no host object is registered. */
	AOnlineBeaconHost* GetListener() const;

private:

	/** @return The listener of this world: one that already exists, or a new one that started listening. Null when it could not start. */
	AOnlineBeaconHost* FindOrSpawnListener(UWorld& World);

	/**
	 * The listener the host objects are registered on.
	 * Invalid after a travel destroyed the listener, which Register checks.
	 */
	TWeakObjectPtr<AOnlineBeaconHost> Listener;

	/**
	 * Whether this object spawned Listener and may unpause or destroy it.
	 * A listener the project spawned is only registered on.
	 */
	bool bOwnsListener = false;

	/** How many host objects are registered on Listener. */
	int32 RegisteredCount = 0;
};
