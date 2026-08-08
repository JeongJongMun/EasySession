// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Reading the address and URL strings that the online subsystem and the travel
 * APIs hand us. Their shape is decided by the subsystem, not by us, so every
 * check here reports "nothing wrong" for a string it cannot understand: a wrong
 * complaint would block a join that was going to work.
 */
namespace EasySessionAddress
{
	/**
	 * Whether a resolved connect string advertises port 0, which means the host
	 * never opened a game net driver - the session is advertised but nothing is
	 * listening behind it. See Trouble_Shooting #04.
	 *
	 * Only judges addresses whose port position is unambiguous: "host:port" and
	 * "[ipv6]:port". A bare IPv6 address, or a connect string that is not an
	 * address at all, returns false.
	 */
	bool HasZeroPort(const FString& ConnectString);

	/**
	 * Whether a travel URL already carries the ?listen option. Matched as a URL
	 * option, so it is case insensitive and "?listenport=7777" does not count.
	 */
	bool HasListenOption(const FString& TravelURL);

	/**
	 * The value of one option in a travel URL, trimmed, or empty when the option
	 * is absent. Accepts the whole request URL: the map path in front is skipped,
	 * as the engine does before handing it to PreLogin.
	 *
	 * Returned as it appears in the URL. A value that went through
	 * EncodeTravelOptionValue comes back still encoded.
	 */
	FString ParseTravelOption(const FString& RequestURL, const TCHAR* Key);

	/**
	 * Make a value safe to carry as a travel option. The engine splits options on
	 * '?' and '#' (FURL.cpp, ValidNetChar), so a value holding either one arrives
	 * cut in half; '%' goes with them to keep decoding unambiguous. Everything
	 * else, spaces and '=' included, is left alone.
	 */
	FString EncodeTravelOptionValue(const FString& Value);

	/** Undo EncodeTravelOptionValue. Leaves anything it did not write untouched. */
	FString DecodeTravelOptionValue(const FString& Value);
}
