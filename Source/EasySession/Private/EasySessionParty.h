// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionPartyBeaconHost;
class AEasySessionPartyBeaconState;
class FEasySessionBeaconPort;
class FOnlineSessionSettings;
class UEasySessionSubsystem;
class UWorld;

/**
 * FEasySessionParty is responsible for the party: a small group of players who stay together outside game sessions.
 * The party is an online session named NAME_PartySession, and its member list lives on the leader's party beacon.
 *
 * The leader hosts the party beacon on the shared beacon listener, next to the reservation beacon of a game session.
 * The party session requests call this object once the party session is created, and before it is destroyed.
 *
 * Owned by the subsystem and destroyed with it.
 */
class FEasySessionParty
{
	//~ FEasySessionTestAccess reads the party beacon for the tests, as it reads the subsystem.
	friend class FEasySessionTestAccess;

public:

	FEasySessionParty(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort);

	/** Closes the party beacon. */
	~FEasySessionParty();

	/**
	 * The party session was created by this process, which leads the party.
	 * Spawns the party beacon in the current world and adds the leader to it.
	 *
	 * @return Whether the party beacon runs. False when the listener could not start or no player is logged in.
	 */
	bool StartHosting(int32 MaxMembers);

	/** Close the party beacon, before the party session is destroyed. */
	void Close();

	/** @return Whether the local player is in a party. */
	bool IsInParty() const;

	/** @return Whether the local player leads the party. False outside a party. */
	bool IsLeader() const;

	/** @return Every member of the party, the leader included. Empty outside a party. */
	TArray<FEasyPartyMemberInfo> GetMembers() const;

	/** @return Whether these settings belong to a party session rather than a game session. */
	static bool IsPartySession(const FOnlineSessionSettings& Settings);

private:

	/** @return The party beacon state this game reads the members from, or null outside a party. */
	const AEasySessionPartyBeaconState* GetPartyState() const;

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	/** The shared beacon port the party beacon registers on. */
	FEasySessionBeaconPort& BeaconPort;

	/** The leader's party beacon. Only valid on the leader, while the party exists. */
	TWeakObjectPtr<AEasySessionPartyBeaconHost> BeaconHost;
};
