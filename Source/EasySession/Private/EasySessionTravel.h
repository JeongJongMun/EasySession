// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionTypes.h"

class UEasySessionSubsystem;

/**
 * Sends players to the map the session is played on.
 * The host opens its own map as a listen server, clients connect to the host's address, and both travel URLs pass through the modify hooks first.
 *
 * This object also tracks whether such a travel is still running.
 * The online call that precedes a travel can finish in the same frame, because the NULL subsystem completes Start, End and Join inside the call itself.
 * The queue alone would then report "idle" while the player is still watching a level load.
 *
 * The subsystem keeps the engine delegate bindings for map loads and travel failures and forwards them here, so those callbacks stay bound to a UObject.
 */
class FEasySessionTravel
{
public:

	explicit FEasySessionTravel(UEasySessionSubsystem& InOwner);

	/** The listen-check ticker is bound to this raw class - it is removed here. */
	~FEasySessionTravel();

	/**
	 * Host side, after creating a session: travel to the session map, or start listening on the current map when no map is given.
	 * Shortly afterwards it checks that this game really did become a listen server.
	 * A session that is advertised but accepts no connections is the most common setup mistake.
	 */
	void EnsureHostIsListening(const FEasySessionHostParams& HostParams);

	/** Client side, after joining: travel to the host the connect string points at. */
	void TravelToJoinedSession(const FString& ConnectString, const FString& Password, const FString& AdditionalTravelOptions);

	/** Remember that this plugin sent the player somewhere. */
	void MarkStarted(const TCHAR* Reason);

	/**
	 * A map load ended the travel.
	 * Called for every load, including one this plugin did not start, so the flag can never stay set after the travel it belongs to is over.
	 */
	void NotifyMapLoaded();

	/** The travel is over even though no map was loaded. */
	void NotifyTravelFailed();

	/** @return Whether a travel this plugin started has not reached its map yet. */
	bool IsTraveling() const { return bTravelInFlight; }

	/** Append a travel option string ("A=1?B=2") to a URL, normalizing the '?' separators. */
	static void AppendTravelOptions(FString& InOutURL, const FString& Options);

private:

	/** Host with a target map: travel there, adding ?listen when the mode needs it. */
	void TravelToOwnSession(const FEasySessionHostParams& HostParams);

	UEasySessionSubsystem& Owner;

	/** Whether a travel this plugin started is still waiting for its map to load. */
	bool bTravelInFlight = false;

	/** Ticker that verifies the host became a listen server shortly after creating a session. */
	FTSTicker::FDelegateHandle ListenCheckTickerHandle;
};
