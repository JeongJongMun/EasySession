// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

namespace EasySession
{
	/** The message of every RequiresSessionAuthority result. */
	inline constexpr const TCHAR* RequiresSessionAuthorityMessage = TEXT("Only the game that created the session can do this. Show this button only when Is Easy Session Authority is true, so clients do not see it.");

	/** The message of every InvalidParams result that refuses host params. */
	inline constexpr const TCHAR* InvalidHostParamsMessage = TEXT("Host params are invalid: Max Players must be above 0, and Initial Map Name must name the map the session is played on.");

	/** The message of every NoOnlineSubsystem result a request completes with. */
	inline constexpr const TCHAR* NoOnlineSubsystemMessage = TEXT("No online subsystem available.");

	/** The message of a Blueprint node that finds no EasySession subsystem for its world context. */
	inline constexpr const TCHAR* NoEasySessionSubsystemMessage = TEXT("No EasySession subsystem for this node's world context.");

	/** The message of every NotSupportedByService result that needs a friends list. */
	inline constexpr const TCHAR* NoFriendsListMessage = TEXT("This online subsystem has no friends list, for example NULL, which only does LAN.");

	/** The message of every InParty result. */
	inline constexpr const TCHAR* InPartyMessage = TEXT("The party leader decides where the party goes. Call Leave Easy Party to play alone.");

	/** The message of every Canceled result of a matchmaking run. */
	inline constexpr const TCHAR* MatchmakingCanceledMessage = TEXT("Matchmaking was canceled.");

	/** The reason every client receives when its host destroys the session, by LeaveSession or by joining another session. */
	inline FText GetHostLeftSessionReason()
	{
		return NSLOCTEXT("EasySession", "HostLeftSession", "The host has left the game.");
	}

	/** The OnPartyLeft text when the party entered a game session. */
	inline FText GetPartyMovedReason()
	{
		return NSLOCTEXT("EasySession", "PartyMovedToGameSession", "The party moved to a game session.");
	}
}
