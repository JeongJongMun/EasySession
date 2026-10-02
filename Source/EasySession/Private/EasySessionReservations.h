// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionReservationBeacon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class AController;
class AGameModeBase;
class FEasySessionBeaconPort;
class FNamedOnlineSession;
class FOnlineSessionSettings;
class UEasySessionSubsystem;
struct FEasySessionHostParams;
struct FEasySessionSettings;

/**
 * FEasySessionReservations is responsible for who may join the session, and for the reservation each of them holds.
 * Every join is decided in ApproveJoin, which the reservation beacon asks before the travel and PreLogin asks again on arrival.
 * A reservation is added when a join is approved, kept across a map change, and removed when the player logs out.
 *
 * The beacon host this class starts in every world keeps the reservations.
 * The session password is kept here too, because ApproveJoin is the only place it is ever compared.
 *
 * Runs on the host only, because game modes and the beacon host do not exist on clients.
 * Owned by FEasySessionHost and destroyed with it.
 * Delegates are bound raw because this object unbinds them in its destructor, and destroys the beacon host it binds to first.
 *
 * @see AEasySessionReservationBeaconHost
 */
class FEasySessionReservations
{
	//~ FEasySessionTestAccess reads the beacon host and removes reservations for the tests.
	friend class FEasySessionTestAccess;

public:

	/** Starts watching PreLogin and Logout. */
	FEasySessionReservations(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort);

	/** Stops the beacon, and stops watching PreLogin and Logout. */
	~FEasySessionReservations();

	/**
	 * This process created the session. Remembers the password joining players must send.
	 *
	 * @param GroupMembers The players the host brings, who follow after the host. The host's reservation holds them too.
	 */
	void OnSessionCreated(const FEasySessionHostParams& Params, const TArray<FUniqueNetIdRepl>& GroupMembers);

	/** The session settings changed. Takes the new password, and resizes the reservations to the new Max Players. */
	void OnSettingsUpdated(const FEasySessionSettings& Settings);

	/** The session was destroyed. Forgets the password and the kept reservations, and stops the beacon. */
	void OnSessionDestroyed();

	/**
	 * A server travel was requested.
	 * Keeps the reservations for the beacon of the next world, because their players are traveling there, and stops the beacon.
	 */
	void OnServerTravelStarted();

	/**
	 * Start the reservation beacon in the current world, taking the reservations a server travel kept.
	 * Only starts when the session advertises the reservations key.
	 * Safe to call repeatedly; a beacon already running in this world is kept.
	 */
	void StartBeacon();

	/** Stop the reservation beacon. Safe when none is running. */
	void StopBeacon();

	/**
	 * Decide whether a player may join.
	 * Refuses a player the host removed, and a group with such a player in it, because the group moves together.
	 * Approves a player holding a reservation at once, so the members of a group need no password and no free slot of their own.
	 * Checks any other player against the join-in-progress policy first, then whether the session is full, then the password, which friends of the host may skip.
	 * Never returns Unreachable, which only the beacon client produces.
	 *
	 * @param Password What the joining player sent over the reservation beacon. PreLogin passes none, because the password is never in the travel URL.
	 * @param Requester The id the engine checked at beacon login or at PreLogin, which is the only id this decision trusts.
	 * @param GroupMembers The players who travel with the requester in the same reservation. PreLogin passes none.
	 */
	FEasyReservationResponse ApproveJoin(const FString& Password, const FUniqueNetIdRepl& Requester, const TArray<FUniqueNetIdRepl>& GroupMembers = TArray<FUniqueNetIdRepl>()) const;

	/** @return The password joining players must send. Empty when the session is open. */
	const FString& GetSessionPassword() const { return SessionPassword; }

	/** @return Whether friends of the host may join a password session without it. */
	bool GetFriendsBypassPassword() const { return bFriendsBypassPassword; }

	/** Keep this player out until the session is destroyed, and remove their reservation. The list is kept across a map change. */
	void AddKickedPlayer(const FUniqueNetIdRepl& PlayerId);

	/** @return Whether a session with these settings uses the reservation beacon, so a joining player asks for a reservation before traveling. */
	static bool UsesReservationBeacon(const FOnlineSessionSettings& Settings);

	/**
	 * Prefix on every refusal message PreLogin writes.
	 * The engine reports a refusal and a lost host connection with the same failure type, so the client checks this prefix to know which one it received.
	 */
	static constexpr const TCHAR* RefusalMark = TEXT("EasySession: ");

private:

	/** Refuse the arriving player when ApproveJoin says no, by writing the reason into ErrorMessage. */
	void HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);

	/** Remove the reservation of a player whose controller logged out. */
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);

	/**
	 * Remove a player's reservation.
	 * Only the beacon of the current world is told, so the logouts a map change causes leave the kept reservations alone.
	 */
	void RemovePlayerReservation(const FUniqueNetIdRepl& PlayerId);

	/** @return Whether this player holds a reservation on the beacon of the current world. */
	bool PlayerHasReservation(const FUniqueNetIdRepl& PlayerId) const;

	/** @return Whether the session has no reservation left for another player. */
	bool IsSessionFull() const;

	/** @return Whether the event belongs to the world this subsystem runs in. Ignores PIE instances other than this one. */
	bool IsOwnWorld(const AGameModeBase* GameMode) const;

	/** Follow a Max Players change. Refused while more players hold reservations than the new Max Players allows. */
	void SetMaxReservations(int32 MaxPlayers);

	/** Check the port the session advertises against the one the listener bound, and warn when joining players would reach no beacon. */
	void CheckAdvertisedPort(const FOnlineSessionSettings& Settings) const;

	/** Add a reservation for the host and the group it brings, because Max Players counts the host too. */
	void AddHostReservation(AEasySessionReservationBeaconHost& Beacon, const FNamedOnlineSession& NamedSession) const;

	UEasySessionSubsystem& Owner;

	/**
	 * The shared beacon port the beacon host registers on.
	 * Owned by the subsystem and destroyed after this object.
	 */
	FEasySessionBeaconPort& BeaconPort;

	/** The beacon host of the current world, which holds the reservations. Null while no beacon runs. */
	TWeakObjectPtr<AEasySessionReservationBeaconHost> BeaconHost;

	/**
	 * The reservations between a server travel and the beacon of the next world.
	 * Held strongly, because the beacon host that owned it is destroyed with its world and nothing else keeps it alive.
	 * Empty at every other time.
	 */
	TStrongObjectPtr<UPartyBeaconState> KeptReservations;

	/** Password joining players must send, or empty for an open session. */
	FString SessionPassword;

	/** Players the host kicked. ApproveJoin refuses them until the session is destroyed. */
	TArray<FUniqueNetIdRepl> KickedPlayers;

	/** The players the host brought when it created the session. Its reservation holds them until the session is destroyed. */
	TArray<FUniqueNetIdRepl> HostGroup;

	FDelegateHandle PreLoginHandle;

	FDelegateHandle LogoutHandle;

	/** Whether friends of the host may join a password session without it. */
	bool bFriendsBypassPassword = false;
};
