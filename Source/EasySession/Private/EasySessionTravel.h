// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"

class UEasySessionSubsystem;
class UWorld;

/**
 * Travels players to the map the session is played on: the host to its own map, clients to the host's address.
 * Both travel URLs pass through the modify delegates first.
 *
 * Also tracks whether a travel this plugin started is still running, so session requests report busy while the map loads.
 *
 * Owned by the subsystem and destroyed with it.
 * The map load delegate is bound raw because this object cannot outlive the owner that destroys it.
 * Travel failures arrive through the subsystem, whose failure handler owns the disconnect recovery and calls NotifyTravelFailed as one part of it.
 */
class FEasySessionTravel
{
	//~ FEasySessionTestAccess sets bSkipHostTravel for the tests.
	friend class FEasySessionTestAccess;

public:

	/** Starts watching map loads, which are what end a travel. */
	explicit FEasySessionTravel(UEasySessionSubsystem& InOwner);

	/** Stops watching map loads. */
	~FEasySessionTravel();

public:

	/**
	 * Travel the host to InitialMapName after the session is created.
	 * The ?listen option is added unless the map name already has it.
	 * The current world is destroyed, so players who were connected before the session existed are disconnected.
	 */
	void TravelToOwnSession(const FEasySessionHostParams& HostParams);

	/**
	 * Client side, after joining: travel to the host address in the connect string.
	 * The URL carries no password, because the reservation beacon already checked it and the engine logs every travel URL.
	 */
	void TravelToJoinedSession(const FString& ConnectString, const FString& AdditionalTravelOptions);

	/**
	 * Travel the session to another map with a server travel, so the connected players travel with the host.
	 * The URL gets the session's current NumPublicConnections as MaxPlayers, because an Update request may have changed it since the last travel.
	 *
	 * @return Whether the engine accepted the travel.
	 */
	bool ServerTravelToMap(const FString& MapName);

	/** Travel to the project's Game Default Map, the engine's main menu. A second call while that travel runs does nothing. */
	void ReturnToMenu();

public:

	/** @return Whether a travel this plugin started has not loaded its map yet. */
	bool IsTraveling() const { return bTravelInFlight; }

	/** The travel is over even though no map was loaded. */
	void NotifyTravelFailed();

public:

	/** Append a travel option string ("A=1?B=2") to a URL, normalizing the '?' separators. */
	static void AppendTravelOptions(FString& InOutURL, const FString& Options);

private:

	/**
	 * Build the URL for TravelToOwnSession and ServerTravelToMap.
	 * Adds the ?listen option unless the map name has it or this game is a dedicated server, then the additional travel options and MaxPlayers.
	 * OnModifyServerTravelURL is broadcast last, so a bound delegate receives the complete URL.
	 */
	FString MakeServerTravelURL(const FString& MapName, const FString& AdditionalTravelOptions, int32 MaxPlayers) const;

	/** Remember that this plugin started a travel. */
	void MarkStarted(const TCHAR* Reason);

	/** A map load ended the travel. Loads of other game instances' worlds (PIE) are ignored. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

private:

	UEasySessionSubsystem& Owner;

	/** Handle for the engine's map load delegate. Bound for this object's lifetime. */
	FDelegateHandle PostLoadMapHandle;

	/** Whether a travel this plugin started is still waiting for its map to load. */
	bool bTravelInFlight = false;

	/**
	 * Is the host travel skipped, so TravelToOwnSession returns without starting it.
	 * Only the automation tests set it, because their world has no player controller to travel with.
	 */
	bool bSkipHostTravel = false;
};
