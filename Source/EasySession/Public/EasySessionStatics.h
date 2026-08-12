// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EasySessionTypes.h"
#include "EasySessionStatics.generated.h"

class UEasySessionSubsystem;

/**
 * Blueprint function library for quick access to EasySession state.
 * Session operations themselves are async nodes (Create/Find/Join/Destroy/Update Easy Session).
 */
UCLASS()
class EASYSESSION_API UEasySessionStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Get the EasySession subsystem. Use this to bind session events like On Session Failure. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static UEasySessionSubsystem* GetEasySessionSubsystem(const UObject* WorldContextObject);

	/**
	 * Check whether the local player is currently in a session.
	 *
	 * This and the queries below it are all about the game session - the one players find, join and play in.
	 * There is one per process, so none of them take a session argument.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasySession(const UObject* WorldContextObject);

	/**
	 * Check whether the local player is hosting the current session.
	 * Always false on a dedicated server, which has no local player - use Is Easy Session Authority there instead.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionHost(const UObject* WorldContextObject);

	/**
	 * Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 * Do not use Is Easy Session Host instead - it is false on a dedicated server.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionAuthority(const UObject* WorldContextObject);

	/**
	 * Get the lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 * The host reports its own state; a client reports the host's replicated state once it has arrived, and its own until then.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionState GetEasySessionState(const UObject* WorldContextObject);

	/**
	 * Get the password this game's session was created with, e.g. to show it so the host can share it.
	 * Empty on clients and for password-less sessions - the password never leaves the host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionPassword(const UObject* WorldContextObject);

	/** Check whether a Quick Match run is in progress. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyMatchmaking(const UObject* WorldContextObject);

	/** Get which step a Quick Match run is on: Searching, Joining, Hosting, Complete. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasyMatchmakingState GetEasyMatchmakingState(const UObject* WorldContextObject);

	/**
	 * Get a display-friendly label for the current session state that pairs the player-facing meaning with the raw state.
	 * For example "Waiting (Pending)", "In Match (InProgress)", "Waiting (Ended)".
	 * Pending and Ended both mean "in the lobby, ready to (re)start" - only the history differs.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionStateLabel(const UObject* WorldContextObject);

	/**
	 * Check whether any session operation is in progress.
	 * That covers a request running or queued, a Quick Match working through its steps, and a travel this plugin started that has not reached its map yet.
	 * Bind session buttons to this to disable them while an operation runs; Is Easy Matchmaking asks specifically about Quick Match.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionBusy(const UObject* WorldContextObject);

	/** Get the results of the most recent session search. Empty while a new search is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionSearchResult> GetLastEasySearchResults(const UObject* WorldContextObject);

	/**
	 * Get the display names of all players currently in the session, including the local player.
	 * Names come from the replicated player states, so the list is available on both the host and clients.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FString> GetEasySessionPlayerNames(const UObject* WorldContextObject);

	/** Get the display name of the current session. Empty when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionDisplayName(const UObject* WorldContextObject);

	/**
	 * Get per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionPlayerInfo> GetEasySessionPlayerInfos(const UObject* WorldContextObject);

	/** Get the number of players currently in the session. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionPlayerCount(const UObject* WorldContextObject);

	/** Get the maximum number of players allowed in the current session. Returns 0 when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionMaxPlayers(const UObject* WorldContextObject);

	/** Check whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool HasPendingEasyDisconnectInfo(const UObject* WorldContextObject);

	/** Get the last disconnect info and clear the pending flag. Survives map travel. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasyDisconnectInfo ConsumeLastEasyDisconnectInfo(const UObject* WorldContextObject);

	/** Get the name of the online subsystem currently in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FName GetOnlineSubsystemName(const UObject* WorldContextObject);

	/** Whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsOnlineSubsystemAvailable(const UObject* WorldContextObject);

	/**
	 * Get what the session queue is doing right now, e.g. "Create (running 2.4s of 30s), queued: Start" or "Idle".
	 * Meant for status UI and bug reports.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionQueueStatus(const UObject* WorldContextObject);

	/**
	 * Get the parameters the current session was created with, so one field can be changed and passed to Update Easy Session.
	 * Host only. Returns defaults on a client, and when there is no session.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasySessionHostParams GetEasySessionHostParams(const UObject* WorldContextObject);

	/** Cancel the running Quick Match. Does nothing when no matchmaking is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyMatchmaking(const UObject* WorldContextObject);

	/** Invite a friend to the current session. Not supported on the NULL (LAN) subsystem. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static bool SendEasySessionInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/** Open the platform invite overlay (e.g. Steam) for the current session. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static bool ShowEasyInviteUI(const UObject* WorldContextObject);

	/** Open the platform profile overlay (e.g. Steam) for the given friend. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static bool ShowEasyProfileUI(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/** Open the platform profile overlay (e.g. Steam) for a player in the session. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static bool ShowEasyProfileUIForPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player);

	/** Convert a session result value to a human readable string. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (DisplayName = "To String (EasySessionResult)", CompactNodeTitle = "->"))
	static FString ResultToString(EEasySessionResult Result);
};
