// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionTypes.h"

class UEasySessionSubsystem;

/**
 * Moves players to where the session happens: the host to its own map (turning it
 * into a listen server on the way), clients to the host's address, and everyone's
 * travel URL through the modify hooks. Also remembers that a travel is in flight -
 * the online call that precedes one can finish in the same frame (the NULL
 * subsystem completes Start, End and Join inside the call itself), so the queue
 * alone would report "idle" while the player is still watching a level load.
 *
 * The subsystem keeps the engine delegate bindings (map loads, travel failures)
 * and forwards them here, so the callbacks stay bound to a UObject.
 */
class FEasySessionTravel
{
public:

	explicit FEasySessionTravel(UEasySessionSubsystem& InOwner);

	/** The listen-check ticker is bound to this raw class - it is removed here. */
	~FEasySessionTravel();

	/**
	 * Host side, after creating a session: travel to the session map, or start
	 * listening in place when no map is given, then verify shortly after that this
	 * game really became a listen server - the most common beginner pitfall is a
	 * session that is advertised but not connectable.
	 */
	void EnsureHostIsListening(const FEasySessionHostParams& HostParams);

	/** Client side, after joining: travel to the host the connect string points at. */
	void TravelToJoinedSession(const FString& ConnectString, const FString& Password, const FString& AdditionalTravelOptions);

	/** Remember that this plugin sent the player somewhere. */
	void MarkStarted(const TCHAR* Reason);

	/**
	 * A map load ended the travel. Fires for every load, including a failed one, so
	 * the flag cannot outlive the travel that set it. A load nobody here asked for
	 * clears it just the same - whatever we were waiting for is over either way.
	 */
	void NotifyMapLoaded();

	/** The travel is over even though no map was loaded. */
	void NotifyTravelFailed();

	/** Whether a travel this plugin started is still on its way to a loaded map. */
	bool IsTraveling() const { return bTravelInFlight; }

	/** Append a travel option string ("A=1?B=2") to a URL, normalizing the '?' separators. */
	static void AppendTravelOptions(FString& InOutURL, const FString& Options);

private:

	/** Host with a target map: travel there, adding ?listen when the mode needs it. */
	void TravelToOwnSession(const FEasySessionHostParams& HostParams);

	UEasySessionSubsystem& Owner;

	bool bTravelInFlight = false;

	/** Ticker that verifies the host became a listen server shortly after creating a session. */
	FTSTicker::FDelegateHandle ListenCheckTickerHandle;
};
