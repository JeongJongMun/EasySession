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

	/** The EasySession subsystem, for binding session events like On Session Failure. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static UEasySessionSubsystem* GetEasySessionSubsystem(const UObject* WorldContextObject);

	/**
	 * Whether the local player is in a session.
	 * This and the queries below are about the game session, the one players find, join and play in.
	 * There is one per process, so none of them take a session argument.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasySession(const UObject* WorldContextObject);

	/**
	 * Whether the local player is hosting the current session.
	 * Always false on a dedicated server, which has no local player. Use Is Easy Session Authority there instead.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionHost(const UObject* WorldContextObject);

	/**
	 * Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 * Is Easy Session Host is a different question, and false on a dedicated server.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionAuthority(const UObject* WorldContextObject);

	/**
	 * The lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 * The host reports its own state. A client reports the host's replicated state once it has arrived, and its own until then.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionState GetEasySessionState(const UObject* WorldContextObject);

	/**
	 * The password this game's session was created with, for the host to share.
	 * Empty on clients and for sessions without a password, because the password never leaves the host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionPassword(const UObject* WorldContextObject);

	/** Whether a matchmaking run is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyMatchmakingRunning(const UObject* WorldContextObject);

	/** Whether Find Easy Friend Sessions is running. It is not part of Is Easy Session Busy, because it only reads. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyFriendSearchRunning(const UObject* WorldContextObject);

	/** Which step a matchmaking run is on: Searching, Joining, Hosting, Canceling, Complete. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasyMatchmakingState GetEasyMatchmakingState(const UObject* WorldContextObject);

	/**
	 * A display label for the current session state, for example "Waiting (Pending)" or "In Match (InProgress)".
	 * Pending and Ended both read "Waiting", because both mean the match can be started. Only the history differs.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionStateLabel(const UObject* WorldContextObject);

	/**
	 * Whether a request is running or queued, a matchmaking run is running, or a travel this plugin started has not loaded its map yet.
	 * Session buttons read this to disable themselves. Is Easy Matchmaking Running asks about matchmaking alone.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionBusy(const UObject* WorldContextObject);

	/**
	 * Which operation is running. None exactly when Is Easy Session Busy is false.
	 * It names the operation behind Is Easy Session Busy, including operations the game did not start. Get Activity Message turns it into a status line.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionActivity GetEasySessionActivity(const UObject* WorldContextObject);

	/** The results of the most recent session search. Empty while a new search is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionSearchResult> GetLastEasySearchResults(const UObject* WorldContextObject);

	/**
	 * The display names of all players in the session, including the local player.
	 * Read from the replicated player states, so both the host and clients get the list.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FString> GetEasySessionPlayerNames(const UObject* WorldContextObject);

	/** The display name of the current session. Empty when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionDisplayName(const UObject* WorldContextObject);

	/** Per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionPlayerInfo> GetEasySessionPlayerInfos(const UObject* WorldContextObject);

	/** The number of players in the session. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionPlayerCount(const UObject* WorldContextObject);

	/** The maximum number of players allowed in the current session. 0 when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionMaxPlayers(const UObject* WorldContextObject);

	/** Whether a disconnect reason is waiting to be shown, for example as a popup on the menu. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool HasPendingEasyDisconnectInfo(const UObject* WorldContextObject);

	/** Get the last disconnect info and clear the pending flag. Kept across map travel. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasyDisconnectInfo ConsumeLastEasyDisconnectInfo(const UObject* WorldContextObject);

	/** The name of the online subsystem in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession", DisplayName = "Get Online Subsystem Name (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static FName GetOnlineSubsystemName(const UObject* WorldContextObject);

	/** Whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession", DisplayName = "Is Online Subsystem Available (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static bool IsOnlineSubsystemAvailable(const UObject* WorldContextObject);

	/** What the session queue is doing right now, for status UI and bug reports, e.g. "Create (running 2.4s of 30s), queued: Start" or "Idle". */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionQueueStatus(const UObject* WorldContextObject);

	/**
	 * The settings the current session is advertising, so one field can be changed and passed to Update Easy Session.
	 * Works for every player in the session. The password and its friends exception are only filled on the host, the one game that holds them.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FEasySessionSettings GetEasySessionSettings(const UObject* WorldContextObject);

	/**
	 * The join code the current session advertises, or an empty string when it advertises none.
	 * Works for every player in the session, so any session member can share the code.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionJoinCode(const UObject* WorldContextObject);

	/**
	 * Cancel the running matchmaking. A search ends inside this call.
	 * A join or host that completes after the cancel is undone.
	 * Does nothing when no matchmaking is running.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyMatchmaking(const UObject* WorldContextObject);

	/** Cancel the running Find Easy Friend Sessions. It completes with Canceled. Does nothing when none is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyFriendSearch(const UObject* WorldContextObject);

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'. The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Session authority only: on any other game this does nothing and returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName);

	/**
	 * Destroy the session for every player.
	 * Clients record Reason as a Host Destroyed Session disconnect and travel back to the menu.
	 * There, Consume Last Easy Disconnect Info returns it so the menu can show it to the player.
	 * Session authority only: on any other game this does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static void DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason);

	/**
	 * Invite a friend to the current session.
	 *
	 * @return Success, or why not: Not Supported By Service on an online subsystem without invites such as NULL (LAN),
	 *         No Session Exists with no session to invite to, or Invalid Params for a friend Read Easy Friends did not return.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult SendEasySessionInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the current session.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyInviteUI(const UObject* WorldContextObject);

	/**
	 * Open the platform profile overlay (e.g. Steam) for the given friend.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyProfileUI(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a player in the session.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyProfileUIForPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player);
};
