// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/OnlineReplStructs.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class AEasySessionStateActor;
class APlayerController;
class AGameModeBase;
class FEasySessionBeaconPort;
class FEasySessionReservations;
class FOnlineSessionSettings;
class UEasySessionPlayerComponent;
class UEasySessionSubsystem;
class UWorld;
struct FEasySessionHostParams;
struct FEasySessionReplicatedSettings;
struct FEasySessionSettings;

/**
 * FEasySessionHost is responsible for the host side of the session.
 * That is the session's bHosting flag, the replicated state actor, FEasySessionReservations, which decides who may join,
 * and a UEasySessionPlayerComponent on every player controller, which carries what one player receives.
 *
 * The session requests call this object when the session is created, updated or destroyed, when the match state changes, and around a server travel.
 * The state actor and the reservation beacon are actors, so they are destroyed with their world.
 * This object spawns both again in every world the session reaches, when the host initializes the game mode of that world.
 *
 * Owned by the subsystem and destroyed with it.
 * Delegates are bound raw because this object cannot outlive the owner that destroys it.
 */
class FEasySessionHost
{
	//~ FEasySessionTestAccess reads the state actor for the tests, as it reads the subsystem.
	friend class FEasySessionTestAccess;

public:

	/** Creates the reservations, and starts watching game mode initialization. */
	FEasySessionHost(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort);

	/** Stops watching and destroys the reservations. */
	~FEasySessionHost();

public:

	/**
	 * This process created the session.
	 * FNamedOnlineSession's bHosting is set and the reservations remember the password.
	 * The world actors are spawned later, in the map the host travels to.
	 */
	void OnSessionCreated(const FEasySessionHostParams& Params);

	/**
	 * The session settings changed.
	 * The engine's player cap and the open slot count follow the new Max Players.
	 * The reservations take the new password and Max Players, and the state actor replicates the new settings.
	 */
	void OnSettingsUpdated(const FEasySessionSettings& Settings);

	/**
	 * The match started or ended.
	 * The state actor replicates the new session state.
	 */
	void OnMatchStateChanged();

	/**
	 * The session was destroyed.
	 * The reservations forget the password and stop the beacon, and the state actor is destroyed.
	 */
	void OnSessionDestroyed();

	/**
	 * A server travel was requested.
	 * The reservations are kept for the next world, the beacon stops, and the state actor is destroyed.
	 * The beacon listener is released too, so the next world can bind the beacon port.
	 */
	void OnServerTravelStarted();

	/**
	 * A server travel failed, so no new world arrives.
	 * Spawns the world actors again in the world this game stayed in.
	 */
	void OnServerTravelFailed();

public:

	/** Tell every connected client to return to the menu with this reason, through the state actor. */
	void TellEveryoneToReturnToMenu(const FText& Reason);

	/**
	 * @return The players who move with the host when it joins another session: every other player who arrived, while the match has not started.
	 *         Empty on a client, and for the host of a match in progress.
	 */
	TArray<FUniqueNetIdRepl> GetGroupMembers() const;

	/** Tell each member of the group to follow the host into another session, through the member's player component. */
	void TellGroupToFollow(const TArray<FUniqueNetIdRepl>& Members, const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/**
	 * Remove a player from the session and keep them out until it is destroyed.
	 * The reason reaches the player through their player component, and the engine's kick closes the connection right after.
	 *
	 * @return Whether the player is a connected remote player.
	 */
	bool KickPlayer(const FUniqueNetIdRepl& PlayerId, const FText& Reason);

	/** @return The reservations, which hold the password the host reads back. */
	const FEasySessionReservations& GetReservations() const { return *Reservations; }

private:

	/**
	 * The host initialized the game mode of a new world.
	 * Calls SpawnWorldActors there one tick later.
	 */
	void HandleGameModeInitialized(AGameModeBase* GameMode);

	/**
	 * Spawn the state actor, start the reservation beacon and add a player component to every player controller in the current world.
	 * An actor or component that already exists in this world is kept.
	 */
	void SpawnWorldActors();

	/** Destroy the state actor. */
	void DestroyStateActor();

	/**
	 * Bind HandleActorSpawned to the actor spawn notification of the current world, unless that world is bound already.
	 * A spawn is what a login, a reconnect after a hard travel and a controller swap in a seamless travel all have in common.
	 */
	void BindActorSpawnedDelegate();

	/** Unbind HandleActorSpawned. The components already added stay until their controllers are destroyed. */
	void UnbindActorSpawnedDelegate();

	/** A new actor in the bound world. Player controllers get a player component. */
	void HandleActorSpawned(AActor* Actor);

	/** Add a player component to this controller, unless it has one. */
	static void AddPlayerComponent(APlayerController& Controller);

	/** @return The connected remote player controller of this player, or null. */
	APlayerController* FindRemoteController(const FUniqueNetIdRepl& PlayerId) const;

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

	/** @return The settings a session member may see, read from the advertised session settings. */
	static FEasySessionReplicatedSettings MakeReplicatedSettings(const FOnlineSessionSettings& Settings);

private:

	UEasySessionSubsystem& Owner;

	/** The shared beacon port, released before a server travel so the next world can bind it. */
	FEasySessionBeaconPort& BeaconPort;

	/** Decides who may join and holds their reservations, with the reservation beacon of each world. */
	TUniquePtr<FEasySessionReservations> Reservations;

	/**
	 * The replicated state actor of the session.
	 * Spawned here, replicated to every client.
	 */
	TWeakObjectPtr<AEasySessionStateActor> StateActor;

	/** Handle for the game mode initialization event, which spawns the world actors again in each world. */
	FDelegateHandle GameModeInitializedHandle;

	/** Handle for the one-tick delay between the game mode initialization and SpawnWorldActors. */
	FTSTicker::FDelegateHandle DeferredSetUpHandle;

	/** The world whose actor spawn notification HandleActorSpawned is bound to. Null while none is bound. */
	TWeakObjectPtr<UWorld> BoundWorld;

	/** Handle for the actor spawn notification of BoundWorld. */
	FDelegateHandle ActorSpawnedHandle;
};
