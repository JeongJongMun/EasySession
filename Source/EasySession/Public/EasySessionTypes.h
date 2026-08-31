// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "OnlineSessionSettings.h"
#include "EasySessionTypes.generated.h"

struct FEasySessionSearchResult;

/**
 * Which call a search makes to the online service.
 * Default describes the sessions to look for; the others name one exact session and read Search Target Id instead.
 */
UENUM(BlueprintType)
enum class EEasySessionSearchMode : uint8
{
	/** Search for sessions matching the filters. */
	Default,

	/** Ask for the session this friend is in. */
	ByFriend UMETA(DisplayName = "By Friend"),

	/** Ask for the session with this id. */
	BySessionId UMETA(DisplayName = "By Session Id")
};

/**
 * How the session host runs the game.
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

	/** The host refused the join because the supplied session password did not match. */
	WrongPassword,

	/** The host refused the join for another reason. The error message says which. */
	JoinRefused,

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

	/** The online service never answered and the request timed out. See Request Timeout Seconds. */
	Timeout,

	/** Only the game that created the session can do this. Show the button only when Is Easy Session Authority is true. */
	RequiresSessionAuthority,

	/** A friend session search is already running. One runs at a time - wait for its completion. */
	FriendSearchAlreadyInProgress
};

/**
 * Lifecycle state of the current session: EOnlineSessionState::Type as a UENUM, for Blueprint pins and the replicated host state.
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

	/** Custom session setting key holding the advertised region, as an EEasySessionRegion value. */
	EASYSESSION_API extern const FName SettingKey_Region;

	/** Custom session setting key marking a session whose match is in progress. Kept current by Start and End. */
	EASYSESSION_API extern const FName SettingKey_MatchInProgress;

	/** Custom session setting key holding the shareable join code. Empty when the host advertises none. */
	EASYSESSION_API extern const FName SettingKey_JoinCode;

	/** Make a six character join code from an alphabet without look-alike characters (no 0/O, 1/I/L, 8/B). */
	EASYSESSION_API FString GenerateJoinCode();

	/**
	 * Custom session setting key marking a session whose host answers join approval over a beacon.
	 * Approval covers every joining rule - password, room, joinable state - not just passwords.
	 * It is written for every session this plugin hosts, so there is no per-session switch.
	 */
	EASYSESSION_API extern const FName SettingKey_JoinApproval;

	/**
	 * The port a join approval beacon listens on, read from the AOnlineBeaconHost config so a project can move it in DefaultEngine.ini.
	 * Advertised on the session, because the beacon does not exist yet when the session is created.
	 */
	EASYSESSION_API int32 GetJoinApprovalBeaconPort();

	/**
	 * Whether this key is one the plugin writes for itself rather than one the game put in Custom Settings.
	 * Reserved keys are kept out of Custom Settings in both directions, because a game that reads them back and passes them to Update Easy Session would rewrite them as strings and break the code that reads them as numbers.
	 */
	EASYSESSION_API bool IsReservedSettingKey(FName Key);

	/** Travel URL option carrying the password a client supplies when joining. */
	EASYSESSION_API extern const TCHAR* TravelOption_Password;
}

/** Native hook fired before a travel URL is used, allowing C++ code to modify it in place. */
DECLARE_MULTICAST_DELEGATE_OneParam(FEasyModifyTravelURLDelegate, FString& /*TravelURL*/);

/**
 * Coarse world regions a session can advertise and a search can filter by, cut so that one region means playable latency.
 * A game that needs its own split (country servers, one home region) leaves this at Any and filters with a Custom Settings key instead.
 */
UENUM(BlueprintType)
enum class EEasySessionRegion : uint8
{
	/** No region: the session matches only searches that do not filter by region. */
	Any,

	/** USA and Canada, eastern half. */
	NorthAmericaEast UMETA(DisplayName = "North America East"),

	/** USA and Canada, western half. */
	NorthAmericaWest UMETA(DisplayName = "North America West"),

	/** South and Central America. */
	SouthAmerica UMETA(DisplayName = "South America"),

	/** Europe. */
	Europe,

	/** Middle East and Africa. */
	MiddleEastAfrica UMETA(DisplayName = "Middle East & Africa"),

	/** India and its neighbors. */
	SouthAsia UMETA(DisplayName = "South Asia"),

	/** Singapore and its neighbors. */
	SoutheastAsia UMETA(DisplayName = "Southeast Asia"),

	/** Korea, Japan, Hong Kong. */
	EastAsia UMETA(DisplayName = "East Asia"),

	/** Australia and New Zealand. */
	Oceania
};

/**
 * What a session advertises about itself, and everything Update Easy Session can change while players are in it.
 * Hosting starts from these: FEasySessionHostParams adds the fields that only mean anything while the session is being created.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionSettings
{
	GENERATED_BODY()

	/** Display name of the session, shown to other players in search results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FString SessionDisplayName = TEXT("My Session");

	/** Maximum number of players allowed in the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = 1))
	int32 MaxPlayers = 4;

	//~ The fields below are folded behind the Make node's advanced arrow, and the markers are what keep the fold line here.
	//~ UK2Node_MakeStruct folds on its own from five fields up, but only while no field carries AdvancedDisplay - so without the markers, adding a field would move the line instead.

	/** Whether the session is advertised to other players. Disable for private sessions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bShouldAdvertise = true;

	/**
	 * Hidden sessions are advertised to the online service but excluded from Find Easy Sessions results.
	 * They can only be joined through invites or a direct search result.
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
	 * Invites can only be sent to friends, so an accepted invite always comes from one and is let through.
	 * Without this, invited players would be rejected because the invite flow never asks for a password.
	 * Verified host-side against the platform friends list; no effect on NULL/LAN (no friends there).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bFriendsBypassPassword = true;

	/**
	 * Whether players can join while the match is already in progress.
	 * Leave this on for Steam: Steam closes the lobby as soon as the first player joins and never reopens it, so everyone after that is refused even before the match starts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowJoinInProgress = true;

	/** Whether players can invite friends to the session. Ignored on dedicated servers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowInvites = true;

	/** The region advertised with the session. Searches filtering by region only see sessions advertising the same one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	EEasySessionRegion Region = EEasySessionRegion::Any;

	/**
	 * Advertise a generated six character join code with the session, readable with Get Easy Session Join Code.
	 * Players reach it with the code in a search's Join Code filter: Find Easy Sessions previews the room, matchmaking joins it, hidden sessions included - Hidden plus a code makes a friends-only room.
	 * The code identifies the room but does not protect it; protection is Password, and the two combine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bUseJoinCode = false;

	/** Custom key-value data advertised with the session (e.g. GameMode = Deathmatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	TMap<FString, FString> CustomSettings;

	/** @return Whether these settings can be advertised as they stand. */
	bool IsValid() const;
};

/**
 * Parameters for hosting a session: the settings above, plus how to open the server that runs it.
 * All values have sensible defaults - an empty FEasySessionHostParams hosts a public 4 player listen session.
 * The fields added here are read once, while the session is created. Update Easy Session takes the settings alone, because a live session cannot change them.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionHostParams : public FEasySessionSettings
{
	GENERATED_BODY()

	/**
	 * Map to travel to once the session is created (e.g. /Game/Maps/Lobby).
	 * Leave empty to stay on the current map. Additional travel options can be appended with '?'.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FString MapName;

	/** Whether the hosting player's game acts as the server, or a dedicated server hosts the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	EEasySessionHostMode HostMode = EEasySessionHostMode::ListenServer;

	/**
	 * Host on the local network instead of the online service.
	 * Automatically enabled when the NULL (LAN) subsystem is active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bIsLANMatch = false;

	//~ Folded behind the Make node's advanced arrow, for the reason described in FEasySessionSettings.

	/**
	 * Open a listen server as part of hosting, so clients can connect.
	 * Travels to Map Name with the ?listen option, or starts listening on the current map when Map Name is empty.
	 *
	 * Turning this off still advertises the session, but there is no server for players to connect to until you open one yourself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bStartListening = true;

	/**
	 * Whether the session uses platform presence (friends can see and join it).
	 * Ignored on dedicated servers and LAN matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bUsePresence = true;

	/**
	 * Extra options appended to the travel URL when hosting (e.g. "GameMode=Deathmatch?MyOption=1").
	 * Read them on the server with Parse Option / Get Game Mode option parsing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString AdditionalTravelOptions;
};

/**
 * Parameters for searching sessions.
 * All values have sensible defaults - an empty FEasySessionSearchParams finds all public sessions.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionSearchParams
{
	GENERATED_BODY()

	//~ Advanced fields are folded behind the Make node's advanced arrow, for the reason described in FEasySessionHostParams.

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

	/** Only return sessions advertising this region. Any applies no region filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	EEasySessionRegion Region = EEasySessionRegion::Any;

	/** Whether sessions whose match already started are returned. Sessions that refuse join-in-progress never appear either way - they stop answering searches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bIncludeInProgressSessions = true;

	/**
	 * Let hidden sessions into the results. C++ only. Set automatically for every targeted query below, because those name their room, hidden or not.
	 * Results of such a search stay off the public surfaces - no OnSessionsFound broadcast, no Get Last Search Results cache.
	 */
	bool bIncludeHiddenSessions = false;

	/**
	 * Which call the search makes. Anything but Default names one exact session through Search Target Id.
	 * The discovery params (Max Results, LAN Query, Timeout Seconds) are then ignored; the filters above still apply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	EEasySessionSearchMode SearchMode = EEasySessionSearchMode::Default;

	/** The friend or session the mode above asks about. Ignored while the mode is Default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FUniqueNetIdRepl SearchTargetId;

	/**
	 * Only return sessions hosted by this player.
	 * Runs as a normal search with an owner filter, so Max Results still bounds what the filter gets to see.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FUniqueNetIdRepl OwnerId;

	/** Only return the session advertising this join code, hidden or not. Case does not matter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString JoinCode;

	/** Returns true if the search params are valid. */
	bool IsValid() const;

	/** @return Whether these params name one specific session (a search mode, an owner or a join code) rather than describing the sessions to look for. */
	bool IsSpecificSessionQuery() const { return SearchMode != EEasySessionSearchMode::Default || OwnerId.IsValid() || !JoinCode.IsEmpty(); }

	/** @return Whether this session clears every filter above, and so belongs in the results. */
	bool ShouldInclude(const FEasySessionSearchResult& Result) const;
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

	/** Map this session is played on. Empty when the host started listening on the map they were already on. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString MapName;

	/** Whether a password is required to join this session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bPasswordProtected = false;

	/** Whether the session is hidden from searches. Hidden sessions are filtered out of Find results, so Blueprint never receives one. */
	bool bIsHidden = false;

	/** The region the session advertises. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	EEasySessionRegion Region = EEasySessionRegion::Any;

	/** Whether the session's match is in progress right now. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bMatchInProgress = false;

	/** The session's join code. C++ only - the Join Code filter compares it, and keeping it off Blueprint means a session browser cannot list other rooms' codes. */
	FString JoinCode;

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
 * State of a running Matchmaking pass.
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
 * Parameters for Matchmaking.
 * The search filters default to "any public session".
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyMatchmakingParams
{
	GENERATED_BODY()

	/** Filters describing which sessions to search for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FEasySessionSearchParams Search;

	/**
	 * Session to host when no session is found. Ignored while Allow Host Fallback is off.
	 * The fallback inherits the search's filters: it hosts on the searched network (LAN Query) and advertises every Required Custom Settings pair, overwriting the same key in Custom Settings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FEasySessionHostParams Host;

	/** Whether to host our own session when no session is found. False by default. Turning it on with an empty Host Map Name hosts on the map this player is already on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bAllowHostFallback = false;

	//~ Advanced fields are folded behind the Make node's advanced arrow, for the reason described in FEasySessionHostParams.

	/** Password sent when joining a password protected candidate. Without one, protected sessions are never candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString JoinPassword;

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

	/** The friend's unique id. Hand it to a search's Search Target Id to find the session they are in. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FUniqueNetIdRepl NativeId;

	/** Returns true if this friend can be used with invite functions. */
	bool IsValid() const { return NativeId.IsValid(); }
};

/**
 * A friend together with the session they are in, as returned by Find Easy Friend Sessions.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyFriendSession
{
	GENERATED_BODY()

	/** The friend. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FEasySessionFriend Friend;

	/** Whether a joinable session was found for this friend. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bHasSession = false;

	/** The friend's session, joinable with Join Easy Session. Only valid while bHasSession is true. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FEasySessionSearchResult Session;
};

/**
 * One advertised custom setting on the wire. A TMap cannot replicate, so Custom Settings ride as an array of these.
 */
USTRUCT()
struct EASYSESSION_API FEasySessionReplicatedSetting
{
	GENERATED_BODY()

	UPROPERTY()
	FString Key;

	UPROPERTY()
	FString Value;

	bool operator==(const FEasySessionReplicatedSetting& Other) const
	{
		return Key == Other.Key && Value == Other.Value;
	}
};

/**
 * The settings a session member is allowed to see, replicated to every client after an update.
 * That includes the join code, so anyone in the room can share it. Only the password and its friends exception stay on the host.
 * Not exposed to Blueprint - clients read the values through the regular session getters.
 */
USTRUCT()
struct EASYSESSION_API FEasySessionReplicatedSettings
{
	GENERATED_BODY()

	UPROPERTY()
	FString SessionDisplayName;

	UPROPERTY()
	FString JoinCode;

	UPROPERTY()
	int32 MaxPlayers = 0;

	UPROPERTY()
	bool bShouldAdvertise = true;

	UPROPERTY()
	bool bAllowJoinInProgress = true;

	UPROPERTY()
	bool bAllowInvites = true;

	UPROPERTY()
	bool bHidden = false;

	UPROPERTY()
	bool bPasswordProtected = false;

	UPROPERTY()
	EEasySessionRegion Region = EEasySessionRegion::Any;

	UPROPERTY()
	TArray<FEasySessionReplicatedSetting> CustomSettings;

	/** Distinguishes a payload the host wrote from the property's defaults. */
	UPROPERTY()
	bool bValid = false;

	bool operator==(const FEasySessionReplicatedSettings& Other) const
	{
		return SessionDisplayName == Other.SessionDisplayName
			&& JoinCode == Other.JoinCode
			&& MaxPlayers == Other.MaxPlayers
			&& bShouldAdvertise == Other.bShouldAdvertise
			&& bAllowJoinInProgress == Other.bAllowJoinInProgress
			&& bAllowInvites == Other.bAllowInvites
			&& bHidden == Other.bHidden
			&& bPasswordProtected == Other.bPasswordProtected
			&& Region == Other.Region
			&& CustomSettings == Other.CustomSettings
			&& bValid == Other.bValid;
	}
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

	/** The player's id on the online service. Names can repeat between players; this cannot. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FUniqueNetIdRepl PlayerId;
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

	/** The host destroyed the session and sent everyone back to the menu. */
	HostDestroyedSession,

	/** Traveling to the session's map failed. */
	TravelFailure,

	/** The host refused the connection (wrong password, not joinable) and Reason Text says why. */
	Rejected
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

/** Delegate fired when finding friend sessions completes. */
DECLARE_DELEGATE_ThreeParams(FEasyFriendSessionsCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasyFriendSession>& /*FriendSessions*/);
