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
 * FEasySessionReservations is responsible for who may join the session, and for the player slot each of them holds.
 * Every join is decided in ApproveJoin, which the reservation beacon asks before the travel and PreLogin asks again on arrival.
 * A slot is held from the moment a join is approved until the player arrives, kept across a map change, and released when the player logs out.
 *
 * The beacon host this class starts in every world keeps the slots.
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
	//~ FEasySessionTestAccess reads the beacon host and releases slots for the tests.
	friend class FEasySessionTestAccess;

public:

	/** Starts watching PreLogin and Logout. */
	FEasySessionReservations(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort);

	/** Stops the beacon, and stops watching PreLogin and Logout. */
	~FEasySessionReservations();

	/** This process created the session. Remembers the password joining players must send. */
	void OnSessionCreated(const FEasySessionHostParams& Params);

	/** The session settings changed. Takes the new password, and follows the new Max Players with the held slots. */
	void OnSettingsUpdated(const FEasySessionSettings& Settings);

	/** The session was destroyed. Forgets the password and the kept slots, and stops the beacon. */
	void OnSessionDestroyed();

	/**
	 * A server travel was requested.
	 * Keeps the held slots for the beacon of the next world, because their players are traveling there, and stops the beacon.
	 */
	void OnServerTravelStarted();

	/**
	 * Start the reservation beacon in the current world, taking the slots a server travel kept.
	 * Only starts when the session advertises the reservations key.
	 * Safe to call repeatedly; a beacon already running in this world is kept.
	 */
	void StartBeacon();

	/** Stop the reservation beacon. Safe when none is running. */
	void StopBeacon();

	/**
	 * Decide whether a player may join.
	 * Checks the join-in-progress policy first, then the free player slots, then the password, which friends of the host may skip.
	 * Never returns Unreachable, which only the beacon client produces.
	 *
	 * @param Password What the joining player sent, empty for an open session.
	 * @param Requester The id the engine checked at beacon login or at PreLogin, which is the only id this decision trusts.
	 */
	FEasyReservationResponse ApproveJoin(const FString& Password, const FUniqueNetIdRepl& Requester) const;

	/** @return The password joining players must send. Empty when the session is open. */
	const FString& GetSessionPassword() const { return SessionPassword; }

	/** @return Whether friends of the host may join a password session without it. */
	bool GetFriendsBypassPassword() const { return bFriendsBypassPassword; }

	/** @return Whether a session with these settings runs the reservation beacon, so a joining player asks before traveling. */
	static bool IsAdvertisedBy(const FOnlineSessionSettings& Settings);

	/**
	 * Prefix on every refusal message PreLogin writes.
	 * The engine reports a refusal and a lost host connection with the same failure type, so the client checks this prefix to know which one it received.
	 */
	static constexpr const TCHAR* RefusalMark = TEXT("EasySession: ");

private:

	/** Refuse the arriving player when ApproveJoin says no, by writing the reason into ErrorMessage. */
	void HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);

	/** Release the player slot of a player whose controller logged out. */
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);

	/**
	 * Release a player's slot.
	 * Only the beacon of the current world is told, so the logouts a map change causes leave the kept slots alone.
	 */
	void ReleasePlayerSlot(const FUniqueNetIdRepl& PlayerId);

	/** @return Whether the session has no player slot left for this player. A player already holding one is never refused here. */
	bool IsSessionFull(const FUniqueNetIdRepl& PlayerId) const;

	/** @return Whether the event belongs to the world this subsystem runs in. Ignores PIE instances other than this one. */
	bool IsOwnWorld(const AGameModeBase* GameMode) const;

	/** Follow a Max Players change. Refused while more players hold slots than the new Max Players allows. */
	void SetMaxPlayerSlots(int32 MaxPlayers);

	/** Check the port the session advertises against the one the listener bound, and warn when joining players would reach no beacon. */
	void CheckAdvertisedPort(const FOnlineSessionSettings& Settings) const;

	/** Reserve one player slot for the host, because Max Players counts the host too. */
	static void AddHostReservation(AEasySessionReservationBeaconHost& Beacon, const FNamedOnlineSession& NamedSession);

	UEasySessionSubsystem& Owner;

	/**
	 * The shared beacon port the beacon host registers on.
	 * Owned by the subsystem and destroyed after this object.
	 */
	FEasySessionBeaconPort& BeaconPort;

	/** The beacon host of the current world, which holds the player slots. Null while no beacon runs. */
	TWeakObjectPtr<AEasySessionReservationBeaconHost> BeaconHost;

	/**
	 * The held player slots between a server travel and the beacon of the next world.
	 * Held strongly, because the beacon host that owned it is destroyed with its world and nothing else keeps it alive.
	 * Empty at every other time.
	 */
	TStrongObjectPtr<UPartyBeaconState> KeptReservations;

	/** Password joining players must send, or empty for an open session. */
	FString SessionPassword;

	FDelegateHandle PreLoginHandle;

	FDelegateHandle LogoutHandle;

	/** Whether friends of the host may join a password session without it. */
	bool bFriendsBypassPassword = false;
};
