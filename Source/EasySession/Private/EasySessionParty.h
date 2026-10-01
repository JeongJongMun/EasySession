// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionTypes.h"
#include "UObject/WeakObjectPtr.h"

class AEasySessionPartyBeaconClient;
class AEasySessionPartyBeaconHost;
class AEasySessionPartyBeaconState;
class ALobbyBeaconPlayerState;
class FEasySessionBeaconPort;
class FOnlineSessionSettings;
class UEasySessionSubsystem;
class UWorld;

/** Fired once per ConnectToLeader: with true when the member list holds the local player, with false and the reason otherwise. */
DECLARE_DELEGATE_TwoParams(FEasyPartyConnectComplete, bool /** bSuccess */, const FText& /** Reason */);

/**
 * FEasySessionParty is responsible for the party: a small group of players who stay together outside game sessions.
 * The party is an online session named NAME_PartySession, and its member list lives on the leader's party beacon.
 *
 * The leader hosts the party beacon on the shared beacon listener, next to the reservation beacon of a game session.
 * Each member connects to it with a party beacon client once the member joined the party session.
 * The party session requests call this object once the party session is created or joined, and before it is destroyed.
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
	bool StartHosting(const FEasyPartyParams& Params);

	/**
	 * The party session was joined.
	 * Connects to the leader's party beacon, which logs the local player in to the party.
	 *
	 * @param ConnectString The address of the leader's party beacon.
	 * @param PartySessionId The id of the party session, which the leader checks at the login.
	 * @param OnComplete Fired once, unless Close runs first.
	 * @return Whether the connection started.
	 */
	bool ConnectToLeader(const FString& ConnectString, const FString& PartySessionId, FEasyPartyConnectComplete OnComplete);

	/**
	 * Close the party beacon, before the party session is destroyed.
	 * A leader tells every member first that the party ends.
	 */
	void Close();

	/**
	 * The party session was destroyed, or the party ended for this player.
	 * Forgets this party and broadcasts On Party Left with the reason.
	 */
	void HandlePartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText);

	/**
	 * Change whether the local player is ready: directly on the leader, and through the leader on a member.
	 *
	 * @return Success, or NoSessionExists outside a party.
	 */
	EEasySessionResult SetReady(bool bReady);

	/**
	 * Remove a member from the party, and keep them out of this party.
	 *
	 * @return Success, RequiresPartyLeader, or InvalidParams for a player who is not a connected member.
	 */
	EEasySessionResult KickMember(const FUniqueNetIdRepl& PlayerId, const FText& Reason);

	/** @return Whether the local player is in a party. */
	bool IsInParty() const;

	/** @return Whether the local player leads the party. False outside a party. */
	bool IsLeader() const;

	/** @return Every member of the party, the leader included. Empty outside a party. */
	TArray<FEasyPartyMemberInfo> GetMembers() const;

	/** @return Whether these settings belong to a party session rather than a game session. */
	static bool IsPartySession(const FOnlineSessionSettings& Settings);

private:

	/**
	 * Decide whether a player may join the party this process leads.
	 * Refuses a member, a kicked player, a full party, and a player an invite-only party does not expect.
	 */
	bool ApproveMember(const FUniqueNetIdRepl& PlayerId, FText& OutReason) const;

	/** The leader finished the login of the local player. */
	void HandleLoginComplete(bool bWasSuccessful);

	/** Wait until the member list holds the local player, then complete the connection. */
	bool HandleConnectTick(float DeltaTime);

	/** Complete the running ConnectToLeader. */
	void FinishConnect(bool bSuccess, const FText& Reason);

	/** The connection to the leader failed or closed. */
	void HandleConnectionFailure();

	/** Bind the member list events of the party beacon state, once it exists here. */
	void BindStateEvents();

	/**
	 * A member was added to or removed from the member list, or changed whether they are ready.
	 * Broadcasts On Party Members Changed on the next tick, after the list changed.
	 */
	void HandleMemberListChanged(ALobbyBeaconPlayerState* Member);

	/** @return The party beacon state this game reads the members from, or null outside a party. */
	AEasySessionPartyBeaconState* GetPartyState() const;

	/** @return The id of the local player, or an invalid id while nobody is logged in. */
	FUniqueNetIdRepl GetLocalPlayerId() const;

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	/** The shared beacon port the party beacon registers on. */
	FEasySessionBeaconPort& BeaconPort;

	/** The leader's party beacon. Only valid on the leader, while the party exists. */
	TWeakObjectPtr<AEasySessionPartyBeaconHost> BeaconHost;

	/** A member's connection to the leader. Only valid on a member, while the party exists. */
	TWeakObjectPtr<AEasySessionPartyBeaconClient> BeaconClient;

	/** The party beacon state whose member list events are bound. */
	TWeakObjectPtr<AEasySessionPartyBeaconState> BoundState;

	/** Who may join the party this process leads. */
	EEasyPartyPrivacy Privacy = EEasyPartyPrivacy::InviteOnly;

	/** Players the leader kicked. They cannot join again while this party exists. */
	TArray<FUniqueNetIdRepl> KickedPlayers;

	/** Players an invite-only party admits: the ones the leader invited, and the members before a match. */
	TArray<FUniqueNetIdRepl> AllowedPlayers;

	/** The completion of the running ConnectToLeader. Unbound while none runs. */
	FEasyPartyConnectComplete ConnectComplete;

	/** Why the leader refused the join, when it said so. */
	FText JoinRefusal;

	/** Why the leader ended this member's membership, when it said so before the connection closed. */
	TOptional<TPair<EEasyPartyLeaveReason, FText>> PendingLeave;

	/** Ticker that waits for the local player in the member list. */
	FTSTicker::FDelegateHandle ConnectTickHandle;

	/** Ticker that broadcasts On Party Members Changed once for every change in a frame. */
	FTSTicker::FDelegateHandle MembersChangedHandle;
};
