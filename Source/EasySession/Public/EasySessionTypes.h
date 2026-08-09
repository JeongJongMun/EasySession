// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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

	/** The search completed but no joinable session was found. */
	NoSessionsFound,

	/** Matchmaking is already running. Cancel it before starting a new one. */
	MatchmakingAlreadyInProgress,

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
	UnknownFailure,

	/**
	 * The online service never reported completion and the request timed out.
	 * The queue moves on so later requests still run - see Request Timeout Seconds
	 * in the project settings.
	 */
	Timeout,

	/**
	 * Only the game serving the session can do this - the host player's game on a
	 * listen server, or the server itself on a dedicated server. Gate the button
	 * with Is Easy Session Host so other players never see it.
	 */
	RequiresSessionAuthority
};

/**
 * Lifecycle state of the current session, mirroring the online subsystem's session state.
 */
UENUM(BlueprintType)
enum class EEasySessionState : uint8
{
	/** There is no session. */
	NoSession,

	/** The session is being created. */
	Creating,

	/** The session exists but the match has not started yet. */
	Pending,

	/** The match is starting. */
	Starting,

	/** The match is in progress. */
	InProgress,

	/** The match is ending. */
	Ending,

	/** The match has ended. Call Start Easy Session to play again. */
	Ended,

	/** The session is being destroyed. */
	Destroying
};

namespace EasySession
{
	/** Convert a result value to a human readable string. */
	EASYSESSION_API FString ResultToString(EEasySessionResult Result);

	/** Custom session setting key holding the session display name. */
	EASYSESSION_API extern const FName SettingKey_DisplayName;

	/** Custom session setting key marking a hidden session (advertised but excluded from searches). */
	EASYSESSION_API extern const FName SettingKey_Hidden;

	/** Custom session setting key marking a password protected session. The password itself is never advertised. */
	EASYSESSION_API extern const FName SettingKey_PasswordProtected;

	/** Travel URL option carrying the password a client supplies when joining. */
	EASYSESSION_API extern const TCHAR* TravelOption_Password;
}

/** Native hook fired before a travel URL is used, allowing C++ code to modify it in place. */
DECLARE_MULTICAST_DELEGATE_OneParam(FEasyModifyTravelURLDelegate, FString& /*TravelURL*/);

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

	// The fields below are folded behind the Make node's advanced arrow. Marking
	// them is what keeps the fold line where we put it: from five fields up,
	// UK2Node_MakeStruct leaves only the first two visible and folds the rest on
	// its own, but only while no field carries AdvancedDisplay (AllocateDefaultPins
	// in K2Node_MakeStruct.cpp) - so adding a field would otherwise move the line
	// with nobody asking for it. Search and Quick Match params are marked for the
	// same reason. A pin with a connection stays visible either way
	// (SGraphPin::IsPinVisibleAsAdvanced), so folding one never hides live wiring.

	/**
	 * Open a listen server as part of hosting, so clients can connect: travels to
	 * Map Name with the ?listen option, or starts listening on the current map when
	 * Map Name is empty.
	 *
	 * Turning this off still advertises the session, but leaves nothing behind it
	 * for players to connect to until you open a server yourself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bStartListening = true;

	/** Whether the session is advertised to other players. Disable for private sessions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bShouldAdvertise = true;

	/**
	 * Hidden sessions are advertised to the online service but excluded from Find Easy Sessions
	 * results, so they can only be joined through invites or a direct search result.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bHidden = false;

	/**
	 * Password required to join the session. Leave empty for no password.
	 * Only a "password protected" flag is advertised - the password itself never leaves the host.
	 * Clients pass the password to Join Easy Session; mismatches are rejected before entering the map.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString Password;

	/**
	 * Lets platform friends of the host join a password protected session without the password.
	 * Invites can only be sent to friends, so accepted invites always get in - without this,
	 * invited players would be rejected because the invite flow never asks for a password.
	 * Verified host-side against the platform friends list; no effect on NULL/LAN (no friends there).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bFriendsBypassPassword = true;

	/**
	 * Extra options appended to the travel URL when hosting (e.g. "GameMode=Deathmatch?MyOption=1").
	 * Read them on the server with Parse Option / Get Game Mode option parsing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString AdditionalTravelOptions;

	/** Whether players can join while the match is already in progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowJoinInProgress = true;

	/** Whether players can invite friends to the session. Ignored on dedicated servers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowInvites = true;

	/**
	 * Whether the session uses platform presence (friends can see and join it).
	 * Ignored on dedicated servers and LAN matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bUsePresence = true;

	/** Custom key-value data advertised with the session (e.g. GameMode = Deathmatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
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

	// Advanced fields are folded behind the Make node's advanced arrow, for the
	// reason described in FEasySessionHostParams.

	/** Maximum number of search results to return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession", meta = (ClampMin = 1))
	int32 MaxResults = 50;

	/**
	 * Search the local network instead of the online service.
	 * Automatically enabled when the NULL (LAN) subsystem is active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bLANQuery = false;

	/** Maximum time to wait for search results, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession", meta = (ClampMin = 1.0))
	float TimeoutSeconds = 15.0f;

	/** Only return sessions with at least this many open player slots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 0))
	int32 MinOpenSlots = 0;

	/** Only return sessions with a ping below this value. 0 means no ping limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession", meta = (ClampMin = 0))
	int32 MaxPingMs = 0;

	/** Only return sessions whose custom settings match all of these key-value pairs exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
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

	/** Whether a password is required to join this session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bPasswordProtected = false;

	/** Whether the session is hidden from searches. Hidden sessions are filtered out of Find results. */
	bool bIsHidden = false;

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

/**
 * State of a running QuickMatch matchmaking pass.
 */
UENUM(BlueprintType)
enum class EEasyMatchmakingState : uint8
{
	/** No matchmaking is running. */
	Idle,

	/** Searching for sessions. */
	Searching,

	/** Joining the best available session. */
	Joining,

	/** No session was found - creating our own session instead. */
	Hosting,

	/** Matchmaking has finished. Check the completion result for the outcome. */
	Complete
};

/**
 * Parameters for QuickMatch matchmaking.
 * The search filters default to "any public session". Host > Map Name has no default
 * and must be set: matchmaking cannot decide where the match is played, and hosting
 * without a map leaves a session nobody can connect to.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyQuickMatchParams
{
	GENERATED_BODY()

	/** Filters describing which sessions to search for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FEasySessionSearchParams Search;

	/**
	 * Session to host when no session is found. Map Name is required here, unlike in
	 * Create Easy Session where an empty one means "stay put".
	 * Ignored when Allow Host Fallback is false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FEasySessionHostParams Host;

	/**
	 * Whether to host our own session when no session is found.
	 * Disable in dedicated server games - clients should only search and join.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bAllowHostFallback = true;

	// Advanced fields are folded behind the Make node's advanced arrow, for the
	// reason described in FEasySessionHostParams.

	/** How many search passes to run before giving up or hosting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession", meta = (ClampMin = 1))
	int32 MaxSearchPasses = 3;

	/** Delay between search passes, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession", meta = (ClampMin = 0.0))
	float DelayBetweenPassesSeconds = 2.0f;
};

/**
 * An online friend of the local player, as returned by Read Easy Friends.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionFriend
{
	GENERATED_BODY()

	/** Display name of the friend. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString DisplayName;

	/** Whether the friend is currently online. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsOnline = false;

	/** Whether the friend is currently playing this game. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsPlayingThisGame = false;

	/** The friend's unique id. Not exposed to Blueprint. */
	FUniqueNetIdPtr NativeId;

	/** Returns true if this friend can be used with invite functions. */
	bool IsValid() const { return NativeId.IsValid(); }
};

/**
 * Information about a single player in the current session.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionPlayerInfo
{
	GENERATED_BODY()

	/** Display name of the player. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString PlayerName;

	/** Whether this entry is the local player on this machine. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsLocalPlayer = false;

	/** Whether this player is hosting the session. Always false on dedicated servers. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsHost = false;

	/** The player's unique id. Not exposed to Blueprint. */
	FUniqueNetIdPtr NativeId;
};

/**
 * Why the local player was disconnected from a session.
 */
UENUM(BlueprintType)
enum class EEasyDisconnectReason : uint8
{
	/** No disconnect has been recorded. */
	None,

	/** The connection to the host was lost (host quit, crashed, or the network dropped). */
	ConnectionLost,

	/** The host ended the session and sent everyone back to the menu. */
	HostEndedSession,

	/** Traveling to the session's map failed. */
	TravelFailure
};

/**
 * Information about the most recent disconnect from a session.
 * Preserved across map travel, so the menu level can read it and show a popup.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyDisconnectInfo
{
	GENERATED_BODY()

	/** Why the player was disconnected. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	EEasyDisconnectReason Reason = EEasyDisconnectReason::None;

	/** Human readable description, suitable for showing in a popup. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FText ReasonText;
};

/** Delegate fired when a session operation completes. */
DECLARE_DELEGATE_TwoParams(FEasySessionCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/);

/** Delegate fired when a session search completes. */
DECLARE_DELEGATE_ThreeParams(FEasySessionFindCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& /*Results*/);

/** Delegate fired when reading the friends list completes. */
DECLARE_DELEGATE_ThreeParams(FEasyFriendsCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionFriend>& /*Friends*/);
