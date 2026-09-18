// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionStateActor;
class AGameModeBase;
class FEasySessionBeaconPort;
class FEasySessionJoinApproval;
class FOnlineSessionSettings;
class FEasySessionServerGate;
class UEasySessionSubsystem;
class UWorld;
struct FEasyJoinApprovalRequest;
struct FEasyJoinApprovalResponse;
struct FEasySessionHostParams;
struct FEasySessionReplicatedSettings;
struct FEasySessionSettings;
struct FUniqueNetIdRepl;

/**
 * FEasySessionHost is responsible for the host side of the session.
 * That is the session's bHosting flag, the replicated state actor, the server gate's credentials and the join approval beacon.
 *
 * The session requests call this object when the session is created, joined, updated or destroyed, and when the match state changes.
 * The state actor and the beacon host object are actors, so they are destroyed with their world.
 * This object spawns both again in every world the session reaches, when the server initializes the game mode of that world.
 *
 * Owned by the subsystem and destroyed with it.
 * Delegates are bound raw because this object cannot outlive the owner that destroys it.
 */
class FEasySessionHost
{
	//~ FEasySessionTestAccess reads the state actor for the tests, as it reads the subsystem.
	friend class FEasySessionTestAccess;

public:

	/** Creates the server gate and the join approval, and starts watching game mode initialization. */
	FEasySessionHost(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort);

	/** Stops watching and destroys the join approval before the server gate. */
	~FEasySessionHost();

public:

	/**
	 * This process created the session.
	 * FNamedOnlineSession's bHosting is set, the server gate gets the credentials and the state actor is spawned.
	 */
	void OnSessionCreated(const FEasySessionHostParams& Params);

	/**
	 * The session settings changed.
	 * The server gate gets the new credentials and the state actor replicates the new settings.
	 */
	void OnSettingsUpdated(const FEasySessionSettings& Settings);

	/**
	 * The match started or ended.
	 * The state actor replicates the new session state.
	 * Does nothing when this process is not the authority.
	 */
	void OnMatchStateChanged();

	/**
	 * The session was destroyed.
	 * Clears the server gate's credentials and destroys the world actors.
	 */
	void OnSessionDestroyed();

public:

	/**
	 * Spawn the state actor and the join approval host object in the current world.
	 * An actor that already exists in this world is kept.
	 * Called when hosting starts without a travel, in every world the session travels to, and after a failed server travel.
	 */
	void SpawnWorldActors();

	/**
	 * Destroy the state actor and the join approval host object.
	 * Called before a server travel, which frees the beacon port for the next world, and when the session is destroyed.
	 */
	void DestroyWorldActors();

public:

	/** Decide whether the requester may join, as the server gate decides it. */
	FEasyJoinApprovalResponse ApproveJoin(const FEasyJoinApprovalRequest& Request, const FUniqueNetIdRepl& Requester) const;

	/** Tell every connected client to return to the menu with this reason, through the state actor. */
	void TellEveryoneToReturnToMenu(const FText& Reason);

	/** @return The server gate, for the credentials it enforces. */
	const FEasySessionServerGate& GetGate() const { return *Gate; }

	/** @return The join approval. The Join request uses its client side until that side moves into the request. */
	FEasySessionJoinApproval& GetJoinApproval() { return *JoinApproval; }

private:

	/**
	 * The server initialized the game mode of a new world.
	 * Calls SpawnWorldActors there one tick later.
	 */
	void HandleGameModeInitialized(AGameModeBase* GameMode);

	/**
	 * Spawn the state actor if the current world has none, then update it.
	 * Clients never spawn it.
	 */
	void EnsureStateActor();

	/**
	 * Write the current session state and the settings a session member may see into the state actor.
	 * Writing a value that did not change replicates nothing, so every caller updates both.
	 */
	void UpdateStateActor();

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

private:

	/** @return The settings a session member may see, read from the advertised session settings. */
	static FEasySessionReplicatedSettings MakeReplicatedSettings(const FOnlineSessionSettings& Settings);

private:

	UEasySessionSubsystem& Owner;

	/** Decides who may join, and refuses arriving players in PreLogin. */
	TUniquePtr<FEasySessionServerGate> Gate;

	/**
	 * Runs the join approval beacon.
	 * Destroyed before Gate.
	 */
	TUniquePtr<FEasySessionJoinApproval> JoinApproval;

	/**
	 * The replicated state actor of the session.
	 * Spawned here, replicated to every client.
	 */
	TWeakObjectPtr<AEasySessionStateActor> StateActor;

	/** Handle for the game mode initialization event, which spawns the world actors again in each world. */
	FDelegateHandle GameModeInitializedHandle;

	/** Handle for the one-tick delay between the game mode initialization and SpawnWorldActors. */
	FTSTicker::FDelegateHandle DeferredSetUpHandle;
};
