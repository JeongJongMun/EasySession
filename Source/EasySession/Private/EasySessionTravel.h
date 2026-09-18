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
 * Also tracks whether a travel this plugin started is still running, so session operations report busy while the map loads.
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

	/**
	 * Travel the host to Initial Map Name after the session is created.
	 * The ?listen option is added unless the map name already has it.
	 * The current world is destroyed, so players who were connected before the session existed are disconnected.
	 */
	void TravelToOwnSession(const FEasySessionHostParams& HostParams);

	/** Client side, after joining: travel to the host address in the connect string. */
	void TravelToJoinedSession(const FString& ConnectString, const FString& Password, const FString& AdditionalTravelOptions);

	/** Travel to the project's Game Default Map, the engine's main menu. A second call while that travel runs does nothing. */
	void ReturnToMenu();

	/** Remember that this plugin started a travel. */
	void MarkStarted(const TCHAR* Reason);

	/** The travel is over even though no map was loaded. */
	void NotifyTravelFailed();

	/**
	 * Cancel a travel that was requested but has not started loading its map.
	 * Travel URLs load on the engine tick after they are requested, so this works within the same frame as the request.
	 */
	void CancelPendingTravel();

	/** @return Whether a travel this plugin started has not loaded its map yet. */
	bool IsTraveling() const { return bTravelInFlight; }

	/** Append a travel option string ("A=1?B=2") to a URL, normalizing the '?' separators. */
	static void AppendTravelOptions(FString& InOutURL, const FString& Options);

private:

	/** A map load ended the travel. Loads of other game instances' worlds (PIE) are ignored. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	UEasySessionSubsystem& Owner;

	/** Handle for the engine's map load delegate. Bound for this object's lifetime. */
	FDelegateHandle PostLoadMapHandle;

	/** Whether a travel this plugin started is still waiting for its map to load. */
	bool bTravelInFlight = false;

	/**
	 * Whether TravelToOwnSession returns without starting the travel.
	 * Only the automation tests set it, because their world has no player controller to travel with.
	 */
	bool bSkipHostTravel = false;
};
