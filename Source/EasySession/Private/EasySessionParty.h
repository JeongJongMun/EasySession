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
class FEasySessionRequest;
class FOnlineSessionSettings;
class UEasySessionSubsystem;
class UWorld;

/** Delegate fired once per ConnectToLeader: with true when the member list holds the local player, with false and the reason otherwise. */
DECLARE_DELEGATE_TwoParams(FEasyPartyConnectComplete, bool /** bSuccess */, const FText& /** Reason */);

/**
 * FEasySessionParty is responsible for the party: a small group of players who stay together outside game sessions.
 * The party is an online session named NAME_PartySession, and its member list lives on the leader's party beacon.
 *
 * The leader hosts the party beacon on the shared beacon listener, next to the reservation beacon of a game session.
 * Each member connects to it with a party beacon client once the member joined the party session.
 * The party session requests call this object once the party session is created or joined, and before it is destroyed.
 *
 * A map change destroys the beacons but keeps the party session.
 * In the new map the leader starts the party beacon again, and each member connects to it again.
 * Entering a game session closes the party, and with bRestorePartyAfterMatch on, the next map without a game session creates or joins it again.
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
	 * Start the party beacon of the party this process leads, after the party session is created and again after a map change.
	 * Spawns the party beacon in the current world and adds the leader to it.
	 *
	 * @return Whether the party beacon runs. False when no player is logged in or the beacon could not start.
	 */
	bool StartHosting(const FEasyPartySettings& InPartySettings);

	/**
	 * Connect to the leader's party beacon, after the party session is joined and again after a map change.
	 * The login on that beacon adds the local player to the party.
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
	 * The party is over for this player, and the party session is destroyed.
	 * Keeps the party for the restore when it entered a game session, forgets it otherwise, and broadcasts OnPartyLeft and OnPartyMembersChanged.
	 */
	void HandlePartyLeft(EEasyPartyLeaveReason Reason, const FText& ReasonText);

	/** @return The ids of every member but the local player, on the leader. Empty on a member and outside a party. */
	TArray<FUniqueNetIdRepl> GetOtherMemberIds() const;

	/** Leader only: tell every member to follow into the session of this host, which holds a reservation for each of them. */
	void TellMembersToFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/**
	 * Change whether the local player is ready: directly on the leader, and through the leader on a member.
	 *
	 * @return Success, or NoSessionExists outside a party.
	 */
	EEasySessionResult SetReady(bool bReady);

	/**
	 * Remove a member from the party, and keep them out of this party.
	 *
	 * @return Success, RequiresPartyLeader, or InvalidParams for the local player or a player who is not a connected member.
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

	/** @return Whether the party of the last match is being restored: created again on the leader, or searched for on a member. */
	bool IsRestoring() const { return bRestoring; }

	/**
	 * Stop restoring the party of the last match, because the player chose something else.
	 * It also forgets a party that waits for its restore, so the next map without a game session does not restore it.
	 */
	void CancelRestore();

private:

	/** The party this player was in before it entered a game session. */
	struct FLastParty
	{
		/** The leader, whose party the members search for. */
		FUniqueNetIdRepl LeaderId;

		/** The settings the leader creates the party with again. */
		FEasyPartySettings Settings;

		/** Was the party a LAN session. */
		bool bIsLANMatch = false;
	};

	/** A map finished loading. Starts the party beacon again, connects to the leader again, or restores the party of the last match. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** Keep connecting to the leader, whose party beacon a map change destroyed, until the reconnect time runs out. */
	void StartReconnect();

	/** Connect to the leader's party beacon once. */
	void TryReconnect();

	/** A reconnect finished. Tries again after a second, until the reconnect time runs out. */
	void HandleReconnectComplete(bool bSuccess, const FText& Reason);

	/** Create the party of the last match again on the leader, or search for it and join it on a member. */
	void StartRestore();

	/** Create the party of the last match once, on the leader. */
	void TryRestoreCreate();

	/** Look for the leader's party once, and join it when it is found. */
	void TryRestoreJoin();

	/**
	 * A create, search or join of the restore failed.
	 * Tries again after a pause, until PartyRestoreWaitSeconds runs out, then broadcasts OnPartyLeft with ConnectionLost.
	 */
	void RetryRestore(const FText& Reason);

	/** The restore ended, with the party back or without it. */
	void FinishRestore();

	/** Run a request of the restore in the queue, and remember it so a cancel can stop it. */
	void RunRestoreRequest(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Decide whether a player may join the party this process leads.
	 * Refuses a member, a kicked player, and any player while the party is full.
	 * Hidden and the join code only decide who finds the party, so they refuse nobody here.
	 */
	bool ApproveMember(const FUniqueNetIdRepl& PlayerId, FText& OutReason) const;

	/** The leader takes the party into a game session. Calls FollowHost on the subsystem, and keeps MovedToGameSession as the reason the party ends. */
	void HandleFollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery);

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
	 * Broadcasts OnPartyMembersChanged on the next tick, once per frame however many changes arrived.
	 */
	void HandleMemberListChanged(ALobbyBeaconPlayerState* Member);

	/** @return The party beacon state this game reads the members from, or null outside a party. */
	AEasySessionPartyBeaconState* GetPartyState() const;

	/** @return The id of the local player, or an invalid id while nobody is logged in. */
	FUniqueNetIdRepl GetLocalPlayerId() const;

	/** The world of the subsystem's game instance, or null before one exists. */
	UWorld* GetWorld() const;

	/** The subsystem that owns this object. */
	UEasySessionSubsystem& Owner;

	/** The shared beacon port the party beacon registers on. */
	FEasySessionBeaconPort& BeaconPort;

	/** The leader's party beacon. Only valid on the leader, while the party exists. */
	TWeakObjectPtr<AEasySessionPartyBeaconHost> BeaconHost;

	/** A member's connection to the leader. Only valid on a member, while the party exists. */
	TWeakObjectPtr<AEasySessionPartyBeaconClient> BeaconClient;

	/** The party beacon state whose member list events are bound. */
	TWeakObjectPtr<AEasySessionPartyBeaconState> BoundState;

	/** The settings of the party this process leads, which a map change starts the party beacon with again. */
	FEasyPartySettings PartySettings;

	/** The leader of the party this player is in. */
	FUniqueNetIdRepl LeaderId;

	/** Is the party this player is in a LAN session. */
	bool bIsLANParty = false;

	/** The party before the last game session, which the next map without a game session restores. */
	TOptional<FLastParty> LastParty;

	/** Is the party of the last match being restored. */
	bool bRestoring = false;

	/** When the restore started, in FPlatformTime seconds. */
	double RestoreStartSeconds = 0.0;

	/** The request of the restore that runs or waits in the queue. */
	TWeakPtr<FEasySessionRequest> RestoreRequest;

	/** When the reconnect started, in FPlatformTime seconds. Zero while no reconnect runs. */
	double ReconnectStartSeconds = 0.0;

	/** Players the leader kicked. They cannot join again while this party exists. */
	TArray<FUniqueNetIdRepl> KickedPlayers;

	/** The completion of the running ConnectToLeader. Unbound while none runs. */
	FEasyPartyConnectComplete ConnectComplete;

	/** Why the leader refused the join, when it said so. */
	FText JoinRefusal;

	/** Why this member's membership ends, when the leader said so before the connection closed. The first reason is kept. */
	TOptional<TPair<EEasyPartyLeaveReason, FText>> PendingLeave;

	/** Ticker that waits for the local player in the member list. */
	FTSTicker::FDelegateHandle ConnectTickHandle;

	/** Ticker that broadcasts OnPartyMembersChanged once per frame however many changes arrived. */
	FTSTicker::FDelegateHandle MembersChangedHandle;

	/** Ticker that starts the next reconnect or restore attempt. */
	FTSTicker::FDelegateHandle RetryHandle;

	/** Handle for the map load notification. */
	FDelegateHandle PostLoadMapHandle;

	/** Reset when this object is destroyed, so a completion of a restore request that arrives later reaches nothing. */
	TSharedRef<bool> Lifetime = MakeShared<bool>(true);
};
