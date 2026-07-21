// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EasySessionTypes.h"
#include "EasySessionStatics.generated.h"

class UEasySessionSubsystem;

/**
 * Blueprint function library for quick access to EasySession state.
 * Session operations themselves are async nodes (Create/Find/Join/Leave/Update Easy Session).
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

	/** Check whether a session operation is currently running or queued. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionBusy(const UObject* WorldContextObject);

	/** Get the results of the most recent session search. */
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

	/** Convert a session result value to a human readable string. */
	UFUNCTION(BlueprintPure, Category = "EasySession", meta = (DisplayName = "To String (EasySessionResult)", CompactNodeTitle = "->"))
	static FString ResultToString(EEasySessionResult Result);
};
