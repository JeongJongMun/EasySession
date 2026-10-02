// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "LobbyBeaconClient.h"
#include "LobbyBeaconHost.h"
#include "LobbyBeaconPlayerState.h"
#include "LobbyBeaconState.h"
#include "EasySessionPartyBeacon.generated.h"

/** Delegate the party beacon host calls to decide whether a player may join. Returns false and writes the reason to refuse. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FEasyPartyApproveMemberDelegate, const FUniqueNetIdRepl& /** PlayerId */, FText& /** OutReason */);

/** Delegate fired on a member when the leader refused the join, with the reason. */
DECLARE_DELEGATE_OneParam(FEasyPartyJoinRefusedDelegate, const FText& /** Reason */);

/** Delegate fired on a member when the leader takes the party into a game session. */
DECLARE_DELEGATE_TwoParams(FEasyPartyFollowHostDelegate, const FUniqueNetIdRepl& /** HostId */, bool /** bLANQuery */);

/** Delegate fired on a member when the leader ends their membership, before the connection closes. */
DECLARE_DELEGATE_TwoParams(FEasyPartyLeftDelegate, EEasyPartyLeaveReason /** Reason */, const FText& /** ReasonText */);

/**
 * AEasySessionPartyBeaconPlayerState is one member of the party, replicated to every member over the party beacon.
 * The party beacon state spawns one for each member.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconPlayerState : public ALobbyBeaconPlayerState
{
	GENERATED_BODY()

public:

	/** Leader only: change whether this member is ready. */
	void SetReady(bool bInReady);

	/** @return Whether this member is ready. */
	bool IsReady() const { return bReady; }

	/** @return The event fired when the ready state changes, on the leader and on every member. */
	FSimpleMulticastDelegate& OnReadyChanged() { return ReadyChangedEvent; }

	//~ Begin AActor Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

private:

	/** The ready state arrived on a member. */
	UFUNCTION()
	void OnRep_Ready();

	/** Is this member ready. Only the leader writes it. */
	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	/** Fired by OnRep_Ready, and by SetReady on the leader. */
	FSimpleMulticastDelegate ReadyChangedEvent;
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
	TArray<AEasySessionPartyBeaconPlayerState*> GetMembers() const;

	/** Destroy the player state of every member. */
	void DestroyMembers();
};

/**
 * AEasySessionPartyBeaconClient is a member's connection to the party beacon of the leader.
 * FEasySessionParty spawns it once the member joined the party session, and destroys it when the member leaves.
 *
 * The leader sends the reason for a refusal or for the end of a membership on this connection, before it closes it.
 * The engine's own kick sends a fixed text and its client handler does nothing, so these RPCs carry the reason instead.
 */
UCLASS(Transient, NotPlaceable)
class AEasySessionPartyBeaconClient : public ALobbyBeaconClient
{
	GENERATED_BODY()

public:

	/**
	 * Connect to the leader's party beacon and log the local player in to the party session with this id.
	 *
	 * @return Whether the connection started. False when the beacon client could not start.
	 */
	bool ConnectToParty(const FString& ConnectString, const FString& PartySessionId);

	/** Server: tell this player why the leader refused the join. The leader closes the connection right after. */
	UFUNCTION(Client, Reliable)
	void ClientJoinRefused(const FText& Reason);

	/** Server: tell this member that the membership ends, and why. The leader closes the connection right after. */
	UFUNCTION(Client, Reliable)
	void ClientLeftParty(EEasyPartyLeaveReason Reason, const FText& ReasonText);

	/** Server: join the session of the host that holds a reservation for this member, because the leader takes the party there. */
	UFUNCTION(Client, Reliable)
	void ClientFollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/** Client: the member asks the leader to change whether they are ready. */
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bInReady);

	/** @return The delegate fired when the leader refused the join. */
	FEasyPartyJoinRefusedDelegate& OnJoinRefused() { return JoinRefusedDelegate; }

	/** @return The delegate fired when the leader ends this player's membership. */
	FEasyPartyLeftDelegate& OnLeftParty() { return LeftPartyDelegate; }

	/** @return The delegate fired when the leader takes the party into a game session. */
	FEasyPartyFollowHostDelegate& OnFollowHost() { return FollowHostDelegate; }

private:

	/** Fired by ClientJoinRefused. */
	FEasyPartyJoinRefusedDelegate JoinRefusedDelegate;

	/** Fired by ClientLeftParty. */
	FEasyPartyLeftDelegate LeftPartyDelegate;

	/** Fired by ClientFollowHost. */
	FEasyPartyFollowHostDelegate FollowHostDelegate;
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
	bool StartParty(int32 MaxMembers, const FUniqueNetIdRepl& InLeaderId, const FString& LeaderName);

	/**
	 * End the membership of a connected member with this reason, and close the connection.
	 *
	 * @return Whether the player is a connected member.
	 */
	bool RemoveMember(const FUniqueNetIdRepl& PlayerId, EEasyPartyLeaveReason Reason, const FText& ReasonText);

	/** Tell every connected member that the party ends, and why. The caller closes the beacon right after. */
	void TellMembersPartyEnds(const FText& ReasonText);

	/** Tell every connected member to follow the leader into the session of this host. */
	void TellMembersToFollow(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/** @return The party beacon state, or null before StartParty. */
	AEasySessionPartyBeaconState* GetPartyState() const;

	/** @return The delegate this actor asks whether a player may join. Every join is refused while nothing is bound. */
	FEasyPartyApproveMemberDelegate& OnApproveMember() { return ApproveMemberDelegate; }

	//~ Begin AActor Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface

	//~ Begin AOnlineBeaconHostObject Interface
	virtual void NotifyClientDisconnected(AOnlineBeaconClient* LeavingClientActor) override;
	//~ End AOnlineBeaconHostObject Interface

	//~ Begin ALobbyBeaconHost Interface
	virtual void HandlePlayerLogout(const FUniqueNetIdRepl& InUniqueId) override;
	//~ End ALobbyBeaconHost Interface

protected:

	//~ Begin ALobbyBeaconHost Interface
	virtual ALobbyBeaconPlayerState* HandlePlayerLogin(ALobbyBeaconClient* ClientActor, const FUniqueNetIdRepl& InUniqueId, const FString& Options) override;
	//~ End ALobbyBeaconHost Interface

private:

	/** @return The connection of this member, or null. */
	AEasySessionPartyBeaconClient* FindMemberClient(const FUniqueNetIdRepl& PlayerId) const;

	/** Decides every join this actor is asked about. */
	FEasyPartyApproveMemberDelegate ApproveMemberDelegate;

	/** The leader, the party owner of every member. */
	FUniqueNetIdRepl LeaderId;
};
