// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSessionSettings.h"
#include "EasySessionTypes.generated.h"

/**
 * How the session host serves the game.
 */
UENUM(BlueprintType)
enum class EEasySessionHostMode : uint8
{
	/** The hosting player's game acts as the server. No extra infrastructure needed. */
	ListenServer,

	/** A standalone server process without local players. Requires a server build. */
	DedicatedServer
};

/**
 * Result of an EasySession operation.
 * Always check against Success. The other values describe why an operation failed.
 */
UENUM(BlueprintType)
enum class EEasySessionResult : uint8
{
	/** The operation completed successfully. */
	Success,

	/** No online subsystem is available. Check DefaultEngine.ini configuration. */
	NoOnlineSubsystem,

	/** The given parameters were invalid. */
	InvalidParams,

	/** A session already exists. Destroy it before creating or joining another one. */
	SessionAlreadyExists,

	/** There is no session to act upon. */
	NoSessionExists,

	/** The online service failed to create the session. */
	CreateFailure,

	/** The online service failed to search for sessions. */
	SearchFailure,

	/** The online service failed to join the session. */
	JoinFailure,

	/** Could not join because the session is full. */
	JoinSessionFull,

	/** Could not join because the session no longer exists. */
	JoinSessionDoesNotExist,

	/** Joined the session but could not resolve the host address to travel to. */
	ResolveFailure,

	/** The online service failed to destroy the session. */
	DestroyFailure,

	/** The online service failed to update the session. */
	UpdateFailure,

	/** The online service failed to start or end the session. */
	StateChangeFailure,

	/** The operation was canceled. */
	Canceled,

	/** The operation failed for an unknown reason. */
	UnknownFailure
};

namespace EasySession
{
	/** Convert a result value to a human readable string. */
	EASYSESSION_API FString ResultToString(EEasySessionResult Result);

	/** Custom session setting key holding the session display name. */
	EASYSESSION_API extern const FName SettingKey_DisplayName;
}

/**
 * Parameters for hosting a session.
 * All values have sensible defaults - an empty FEasySessionHostParams hosts a public 4 player listen session.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionHostParams
{
	GENERATED_BODY()

	/** Display name of the session, shown to other players in search results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FString SessionDisplayName = TEXT("My Session");

	/**
	 * Map to travel to once the session is created (e.g. /Game/Maps/Lobby).
	 * Leave empty to stay on the current map. Additional travel options can be appended with '?'.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FString MapName;

	/** Whether the hosting player's game acts as the server, or a dedicated server hosts the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	EEasySessionHostMode HostMode = EEasySessionHostMode::ListenServer;

	/** Maximum number of players allowed in the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 1))
	int32 MaxPlayers = 4;

	/**
	 * Host on the local network instead of the online service.
	 * Automatically enabled when the NULL (LAN) subsystem is active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bIsLANMatch = false;

	/** Whether the session is advertised to other players. Disable for private sessions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bShouldAdvertise = true;

	/** Whether players can join while the match is already in progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bAllowJoinInProgress = true;

	/** Whether players can invite friends to the session. Ignored on dedicated servers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bAllowInvites = true;

	/**
	 * Whether the session uses platform presence (friends can see and join it).
	 * Ignored on dedicated servers and LAN matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bUsePresence = true;

	/** Custom key-value data advertised with the session (e.g. GameMode = Deathmatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	TMap<FString, FString> CustomSettings;

	/** Returns true if the host params are valid. */
	bool IsValid() const;
};

/**
 * Parameters for searching sessions.
 * All values have sensible defaults - an empty FEasySessionSearchParams finds all public sessions.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionSearchParams
{
	GENERATED_BODY()

	/** Maximum number of search results to return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 1))
	int32 MaxResults = 50;

	/**
	 * Search the local network instead of the online service.
	 * Automatically enabled when the NULL (LAN) subsystem is active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bLANQuery = false;

	/** Maximum time to wait for search results, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 1.0))
	float TimeoutSeconds = 15.0f;

	/** Only return sessions with at least this many open player slots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 0))
	int32 MinOpenSlots = 0;

	/** Only return sessions with a ping below this value. 0 means no ping limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 0))
	int32 MaxPingMs = 0;

	/** Only return sessions whose custom settings match all of these key-value pairs exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	TMap<FString, FString> RequiredCustomSettings;

	/** Returns true if the search params are valid. */
	bool IsValid() const;
};

/**
 * A single session found by a search. Pass this to Join Session to join it.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionSearchResult
{
	GENERATED_BODY()

	/** Display name of the session as advertised by the host. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString SessionDisplayName;

	/** Name of the player or server hosting the session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString HostName;

	/** Round trip time to the host, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	int32 PingInMs = 0;

	/** Maximum number of players allowed in the session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	int32 MaxPlayers = 0;

	/** Number of open player slots remaining. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	int32 OpenSlots = 0;

	/** Whether the session is hosted by a dedicated server. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsDedicatedServer = false;

	/** Custom key-value data advertised with the session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	TMap<FString, FString> CustomSettings;

	/** The underlying online subsystem search result. Not exposed to Blueprint. */
	FOnlineSessionSearchResult NativeResult;

	/** Returns true if this search result can be joined. */
	bool IsValid() const;

	/** Build an EasySession search result from a native online subsystem result. */
	static FEasySessionSearchResult FromNative(const FOnlineSessionSearchResult& InNativeResult);
};

/** Delegate fired when a session operation completes. */
DECLARE_DELEGATE_TwoParams(FEasySessionCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/);

/** Delegate fired when a session search completes. */
DECLARE_DELEGATE_ThreeParams(FEasySessionFindCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& /*Results*/);
