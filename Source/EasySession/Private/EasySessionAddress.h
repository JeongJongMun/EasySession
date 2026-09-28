// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Reading and building the address and URL strings the online subsystem and the travel APIs use.
 * The online subsystem decides their shape, so a check here that does not recognise a string reports no problem instead of guessing.
 * Guessing wrong would refuse a join that would have worked.
 */
namespace EasySessionAddress
{
	/**
	 * Whether a resolved connect string advertises port 0, which means the host never opened a game net driver.
	 * The session is advertised, but no server is accepting connections.
	 * See "Session is found, but joining times out" in Docs/FAQ.md.
	 *
	 * Only reads addresses whose port position is unambiguous: "host:port" and "[ipv6]:port".
	 * A bare IPv6 address, or a connect string that is not an address at all, returns false.
	 */
	bool HasZeroPort(const FString& ConnectString);

	/**
	 * Whether a travel URL already carries the ?listen option.
	 * Matched as a whole URL option, so it is case insensitive and "?listenport=7777" does not count.
	 */
	bool HasListenOption(const FString& TravelURL);

	/**
	 * Append ?MaxPlayers= unless the URL already carries one, so a game's own travel option wins.
	 * The engine reads this option into AGameSession::MaxPlayers, the cap its "Server full" refusal compares against.
	 */
	void AppendMaxPlayersOption(FString& TravelURL, int32 MaxPlayers);
}
