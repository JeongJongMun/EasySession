// Copyright Langerak. All Rights Reserved.

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

	/** Check whether the local player is currently in a session. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasySession(const UObject* WorldContextObject);

	/** Check whether the local player is hosting the current session. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionHost(const UObject* WorldContextObject);

	/** Get the lifecycle state of the current session (Pending, InProgress, Ended, ...). */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionState GetEasySessionState(const UObject* WorldContextObject);

	/**
	 * Get a display-friendly label for the current session state that pairs the
	 * player-facing meaning with the raw state, e.g. "Waiting (Pending)",
	 * "In Match (InProgress)", "Waiting (Ended)". Pending and Ended both mean
	 * "in the lobby, ready to (re)start" - only the history differs.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionStateLabel(const UObject* WorldContextObject);

	/**
	 * Check whether any session operation is in progress - a request running or queued,
	 * or a Quick Play still working through its steps.
	 * Bind session buttons to this to disable them while an operation runs.
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
	 * Get per-player info for everyone in the session: name, whether it is the
	 * local player on this machine, and whether it is the session host.
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

	/** Get the session invites received so far. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionInvite> GetPendingEasySessionInvites(const UObject* WorldContextObject);

	/** Join the session an invite points to, leaving the current session first if needed. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static void AcceptEasySessionInvite(const UObject* WorldContextObject, const FEasySessionInvite& Invite);

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
