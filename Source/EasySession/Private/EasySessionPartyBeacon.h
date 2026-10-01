// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "LobbyBeaconClient.h"
#include "LobbyBeaconHost.h"
#include "LobbyBeaconPlayerState.h"
#include "LobbyBeaconState.h"
#include "EasySessionPartyBeacon.generated.h"

/**
 * AEasySessionPartyBeaconPlayerState is one member of the party, replicated to every member over the party beacon.
 * The party beacon state spawns one for each member.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconPlayerState : public ALobbyBeaconPlayerState
{
	GENERATED_BODY()
};

/**
 * AEasySessionPartyBeaconState holds the member list of the party, replicated to every member over the party beacon.
 * The party beacon host spawns it and destroys it with itself.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconState : public ALobbyBeaconState
{
	GENERATED_BODY()

public:

	/** Spawns AEasySessionPartyBeaconPlayerState for each member. */
	AEasySessionPartyBeaconState();

	/** @return The player state of every member, the leader included. */
	TArray<const ALobbyBeaconPlayerState*> GetMembers() const;

	/** Destroy the player state of every member. */
	void DestroyMembers();
};

/**
 * AEasySessionPartyBeaconClient is a member's connection to the party beacon of the leader.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconClient : public ALobbyBeaconClient
{
	GENERATED_BODY()
};

/**
 * AEasySessionPartyBeaconHost is the leader's side of the party beacon.
 * It holds the party beacon state, and the leader's own entry in it, which needs no connection.
 *
 * FEasySessionParty spawns it when the party session is created, and registers it on the shared beacon listener.
 * The engine's lobby beacon assumes the game session in places, and this class works on NAME_PartySession instead.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconHost : public ALobbyBeaconHost
{
	GENERATED_BODY()

public:

	/** Uses the party beacon client and state classes. */
	AEasySessionPartyBeaconHost();

	/**
	 * Spawn the party beacon state and add the leader to it.
	 * Call after the host is registered on a listener, because the state takes the listener's net driver.
	 *
	 * @return Whether the state exists and holds the leader.
	 */
	bool StartParty(int32 MaxMembers, const FUniqueNetIdRepl& LeaderId, const FString& LeaderName);

	/** @return The party beacon state, or null before StartParty. */
	const AEasySessionPartyBeaconState* GetPartyState() const;

	//~ Begin AActor Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface
};
