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

	/** @return The EasySession subsystem, for binding session events like On Session Failure. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static UEasySessionSubsystem* GetEasySessionSubsystem(const UObject* WorldContextObject);

	/**
	 * This and the queries below it are all about the game session - the one players find, join and play in.
	 * There is one per process, so none of them take a session argument.
	 *
	 * @return Whether the local player is currently in a session.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasySession(const UObject* WorldContextObject);

	/** @return Whether the local player is hosting the current session. Always false on a dedicated server, which has no local player - use Is Easy Session Authority there instead. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionHost(const UObject* WorldContextObject);

	/** @return Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it. Is Easy Session Host is not the same question - it is false on a dedicated server. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionAuthority(const UObject* WorldContextObject);

	/**
	 * @return The lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 *         The host reports its own state; a client reports the host's replicated state once it has arrived, and its own until then.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionState GetEasySessionState(const UObject* WorldContextObject);

	/** @return The password this game's session was created with, for the host to share. Empty on clients and for password-less sessions - the password never leaves the host. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionPassword(const UObject* WorldContextObject);

	/** @return Whether a Matchmaking run is in progress. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyMatchmakingRunning(const UObject* WorldContextObject);

	/** @return Which step a Matchmaking run is on: Searching, Joining, Hosting, Complete. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasyMatchmakingState GetEasyMatchmakingState(const UObject* WorldContextObject);

	/**
	 * @return A display-friendly label for the current session state that pairs the player-facing meaning with the raw state.
	 *         For example "Waiting (Pending)", "In Match (InProgress)", "Waiting (Ended)".
	 *         Pending and Ended both mean "in the lobby, ready to (re)start" - only the history differs.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionStateLabel(const UObject* WorldContextObject);

	/**
	 * Session buttons bind here to disable themselves; Is Easy Matchmaking Running asks about Matchmaking alone.
	 *
	 * @return Whether a request is running or queued, a Matchmaking is working through its steps, or a travel this plugin started has not reached its map yet.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionBusy(const UObject* WorldContextObject);

	/**
	 * Names the operation behind Is Easy Session Busy, whoever started it. Get Activity Message turns it into a status line.
	 *
	 * @return Which operation is running. None exactly when Is Easy Session Busy is false.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionActivity GetEasySessionActivity(const UObject* WorldContextObject);

	/** @return The results of the most recent session search. Empty while a new search is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionSearchResult> GetLastEasySearchResults(const UObject* WorldContextObject);

	/** @return The display names of all players currently in the session, including the local player. Read from the replicated player states, so both the host and clients get the list. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FString> GetEasySessionPlayerNames(const UObject* WorldContextObject);

	/** @return The display name of the current session. Empty when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionDisplayName(const UObject* WorldContextObject);

	/** @return Per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionPlayerInfo> GetEasySessionPlayerInfos(const UObject* WorldContextObject);

	/** @return The number of players currently in the session. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionPlayerCount(const UObject* WorldContextObject);

	/** @return The maximum number of players allowed in the current session. 0 when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionMaxPlayers(const UObject* WorldContextObject);

	/** @return Whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool HasPendingEasyDisconnectInfo(const UObject* WorldContextObject);

	/** Get the last disconnect info and clear the pending flag. Survives map travel. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasyDisconnectInfo ConsumeLastEasyDisconnectInfo(const UObject* WorldContextObject);

	/** @return The name of the online subsystem currently in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession", DisplayName = "Get Online Subsystem Name (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static FName GetOnlineSubsystemName(const UObject* WorldContextObject);

	/** @return Whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession", DisplayName = "Is Online Subsystem Available (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static bool IsOnlineSubsystemAvailable(const UObject* WorldContextObject);

	/** @return What the session queue is doing right now, for status UI and bug reports, e.g. "Create (running 2.4s of 30s), queued: Start" or "Idle". */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionQueueStatus(const UObject* WorldContextObject);

	/**
	 * @return The settings the current session is advertising, so one field can be changed and passed to Update Easy Session.
	 *         Works for every player in the session; the password and its friends exception are only filled on the host, the one game that holds them.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasySessionSettings GetEasySessionSettings(const UObject* WorldContextObject);

	/**
	 * @return The join code the current session advertises, or an empty string when it advertises none.
	 *         Works for every player in the session, so anyone in the room can share the code.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionJoinCode(const UObject* WorldContextObject);

	/** Cancel the running Matchmaking. A join or host that succeeds after the cancel is undone. Does nothing when no matchmaking is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyMatchmaking(const UObject* WorldContextObject);

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'. The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Session authority only: on any other game this does nothing and returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName);

	/**
	 * Destroy the session for every player.
	 * Remote clients record Reason as a Host Destroyed Session disconnect and return to the menu, where reading it with Consume Last Easy Disconnect Info is what shows it to the player.
	 * Session authority only: on any other game this does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason);

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
