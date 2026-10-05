// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "OnlineSessionSettings.h"
#include "EasySessionTypes.generated.h"

struct FEasySessionSearchResult;

/**
 * Which call a search makes to the online subsystem.
 * Default describes the sessions to look for; the others name one exact session and read Search Target Id instead.
 */
UENUM(BlueprintType)
enum class EEasySessionSearchMode : uint8
{
	/** Search for sessions matching the filters. */
	Default,

	/** Ask for the session this friend is in. */
	ByFriend UMETA(DisplayName = "By Friend")
};

/**
 * Result of an EasySession request.
 * Always check against Success.
 * The other values describe why a request failed.
 */
UENUM(BlueprintType)
enum class EEasySessionResult : uint8
{
	/** The request completed successfully. */
	Success,

	/** No online subsystem is available. Check DefaultEngine.ini configuration. */
	NoOnlineSubsystem,

	/** The given parameters were invalid. */
	InvalidParams,

	/**
	 * This player is already in a game session or a party, so call Leave Easy Session or Leave Easy Party first.
	 * Join Easy Session switches game sessions on its own, except for the host of a match in progress.
	 */
	SessionAlreadyExists,

	/** There is no session to act upon. */
	NoSessionExists,

	/** The online subsystem failed to create the session. */
	CreateFailure,

	/** The online subsystem failed to search for sessions. */
	SearchFailure,

	/** The search completed but no joinable session was found. */
	NoSessionsFound,

	/** Matchmaking is already running. Cancel it before starting a new one. */
	MatchmakingAlreadyInProgress,

	/** The online subsystem failed to join the session. */
	JoinFailure,

	/** Could not join because the session is full. */
	JoinSessionFull,

	/** Could not join because the session no longer exists. */
	JoinSessionDoesNotExist,

	/** The host refused the join because the session password did not match. */
	WrongPassword,

	/** The host refused the join for another reason. The error message says which. */
	JoinRefused,

	/** Joined the session but could not resolve the host address to travel to. */
	ResolveFailure,

	/** The online subsystem failed to destroy the session. */
	DestroyFailure,

	/** The online subsystem failed to update the session. */
	UpdateFailure,

	/** The online subsystem failed to start or end the session. */
	StateChangeFailure,

	/** The request was canceled. */
	Canceled,

	/** The request failed for an unknown reason. */
	UnknownFailure,

	/** Only the game that created the session can do this. Show the button only when Is Easy Session Authority is true. */
	RequiresSessionAuthority,

	/** A friend session search is already running. One runs at a time. Wait for it to complete. */
	FriendSearchAlreadyInProgress,

	/** The online subsystem in use does not offer this feature. Friends and invites need Steam. NULL (LAN) has no friends, which is not a configuration problem. */
	NotSupportedByService,

	/** Only the party leader can do this. Show the button only when Is Easy Party Leader is true. */
	RequiresPartyLeader,

	/** A party member cannot create, join or matchmake alone, because the leader decides where the party goes. Leave Easy Party first to play alone. */
	InParty
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

/**
 * What keeps Is Easy Session Busy true, including requests the game did not start itself.
 * Is Easy Session Busy only says whether something runs.
 * This says what runs, so a status line can name a join from an invite or a session destroyed without the game asking.
 */
UENUM(BlueprintType)
enum class EEasySessionActivity : uint8
{
	/** Nothing is running. */
	None,

	/** A session is being created. */
	Creating,

	/** A session search is running. */
	Searching,

	/** A session is being joined. */
	Joining,

	/** The current session is being destroyed. */
	Leaving,

	/** The session settings are being updated. */
	Updating,

	/** The match is being started. */
	Starting,

	/** The match is being ended. */
	Ending,

	/** A matchmaking run is searching, joining or hosting. */
	Matchmaking,

	/** A travel this plugin started has not loaded its map yet. */
	Traveling
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

	/** Make a six character join code from an alphabet without look-alike characters (no 0, O, 1, I, L or B). */
	EASYSESSION_API FString GenerateJoinCode();

	/**
	 * Custom session setting key marking a session whose host runs the reservation beacon.
	 * A joining player that finds it asks the reservation beacon first, which refuses a wrong password, a full session and a match in progress that allows no join.
	 * It is written for every game session this plugin hosts, so there is no per-session switch.
	 */
	EASYSESSION_API extern const FName SettingKey_Reservations;

	/**
	 * Custom session setting key holding the host's unique id as a string.
	 * A search for one host filters on it in the online subsystem, so that host's session is found however many other sessions exist.
	 */
	EASYSESSION_API extern const FName SettingKey_OwnerId;

	/**
	 * Custom session setting key marking a party session with 1 and a game session with 0.
	 * Both kinds are advertised, so every search asks for one kind and never returns the other.
	 */
	EASYSESSION_API extern const FName SettingKey_Party;

	/**
	 * The port the reservation beacon asks for: -BeaconPort= when the command line carries it, and the AOnlineBeaconHost config otherwise.
	 * Advertised on the session, because the beacon does not exist yet when the session is created.
	 * A listener that ends up on another port is logged as a warning, and the advertised port is not changed.
	 * Joining players then reach no beacon, so PreLogin checks them on arrival and a password-protected session cannot be joined.
	 */
	EASYSESSION_API int32 GetReservationBeaconPort();

	/**
	 * Whether this key is one the plugin writes for itself rather than one the game put in Custom Settings.
	 * Reserved keys are kept out of Custom Settings in both directions.
	 * A game that read them back and passed them to Update Easy Session would rewrite them as strings, and the code that reads them as numbers would break.
	 */
	EASYSESSION_API bool IsReservedSettingKey(FName Key);
}

/** Native delegate fired before a travel URL is used, so C++ code can change it in place. */
DECLARE_MULTICAST_DELEGATE_OneParam(FEasyModifyTravelURLDelegate, FString& /*TravelURL*/);

/**
 * Coarse world regions a session can advertise and a search can filter by, sized so that players in one region have playable latency.
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
 * FEasySessionHostParams adds the fields that are only read while the session is created.
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

	//~ The fields below are folded behind the Make node's advanced arrow, and the AdvancedDisplay markers keep the fold at this line.
	//~ UK2Node_MakeStruct folds on its own from five fields up, but only while no field carries AdvancedDisplay.
	//~ Without the markers, adding a field would move the fold.

	/** Whether the session is advertised to other players. Turn it off for a session players join only by invite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bShouldAdvertise = true;

	/**
	 * Whether the session is left out of the Find Easy Sessions results, while it stays advertised to the online subsystem.
	 * Players reach a hidden session through an invite or a search with a Join Code, an Owner Id or a friend.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bHidden = false;

	/**
	 * Password required to join the session.
	 * Leave empty for no password.
	 * Only a password protected flag is advertised.
	 * The password itself never leaves the host.
	 * Joining players pass the password to Join Easy Session, and a wrong one is refused before the map loads.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString Password;

	/**
	 * Whether platform friends of the host join a password protected session without the password.
	 * The invite flow never asks for a password, so with this off an invited player is refused.
	 * Only friends of the host get through, so a player invited by another member still needs the password.
	 * No effect on NULL (LAN), which has no friends.
	 * Get Easy Session Settings fills it only on the host, so a client always reads false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bFriendsBypassPassword = true;

	/**
	 * Whether players can join while the match is already in progress.
	 * Leave this on for Steam.
	 * Steam closes the lobby as soon as the first player joins and never reopens it.
	 * With this off, every later player is refused even before the match starts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowJoinInProgress = true;

	/** Whether players can invite friends to the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bAllowInvites = true;

	/** The region advertised with the session. Searches filtering by region only see sessions advertising the same one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	EEasySessionRegion Region = EEasySessionRegion::Any;

	/**
	 * Whether the session advertises a generated six character join code, readable with Get Easy Session Join Code.
	 * A search with the code in Join Code returns the session, hidden or not, so Find Easy Sessions lists it and matchmaking joins it.
	 * Anyone with the code can find the session, so set Password as well to keep others out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bUseJoinCode = false;

	/** Custom key-value data advertised with the session (e.g. GameMode = Deathmatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	TMap<FString, FString> CustomSettings;

	/** @return Whether these settings can be advertised as they stand. */
	bool IsValid() const;

	/**
	 * Apply these settings to the online subsystem's settings.
	 * Create applies them to empty settings, and Update applies them over the advertised ones.
	 * An advertised join code is kept while bUseJoinCode stays on, and a custom setting left out of CustomSettings is removed.
	 */
	void ApplyTo(FOnlineSessionSettings& OutSettings) const;

	/**
	 * Read these settings from the online subsystem's settings, the other direction of ApplyTo.
	 * Password and bFriendsBypassPassword are not advertised, so they stay untouched and GetSessionSettings fills them from the reservations on the host.
	 */
	void ReadFrom(const FOnlineSessionSettings& Settings);
};

/**
 * Parameters for hosting a session: the settings above, plus how to open the listen server that runs it.
 * Every value has a default except Initial Map Name.
 * With a map name alone, FEasySessionHostParams hosts a public 4 player listen server session.
 * The fields added here are read once, while the session is created.
 * Update Easy Session takes FEasySessionSettings alone, because these fields cannot change on a live session.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasySessionHostParams : public FEasySessionSettings
{
	GENERATED_BODY()

	/**
	 * Map the host travels to with the ?listen option once the session is created (e.g. /Game/Maps/Lobby).
	 * Create Easy Session fails with Invalid Params while this is empty, because that travel is what opens the listen server.
	 * The session does not advertise the map.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FString InitialMapName;

	/**
	 * Host on the local network instead of through the online subsystem.
	 * Forced on when the online subsystem is NULL, which only does LAN.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bIsLANMatch = false;

	//~ Folded behind the Make node's advanced arrow, for the reason described in FEasySessionSettings.

	/**
	 * Whether the session uses platform presence (friends can see and join it).
	 * Ignored on LAN matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bUsePresence = true;

	/**
	 * Extra options appended to the travel URL when hosting (e.g. "GameMode=Deathmatch?MyOption=1").
	 * Read them on the host with Parse Option.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString AdditionalTravelOptions;

	/** @return Whether a session can be hosted with these params. The settings must be valid and Initial Map Name must be set. */
	bool IsValid() const;
};

/**
 * Parameters for searching sessions.
 * Every value has a default.
 * An empty FEasySessionSearchParams finds every public session.
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
	 * Search the local network instead of through the online subsystem.
	 * Forced on when the online subsystem is NULL, which only does LAN.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bLANQuery = false;

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

	/**
	 * Whether sessions whose match already started are returned.
	 * Sessions that refuse join-in-progress stop being advertised once their match starts, so they never appear either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	bool bIncludeInProgressSessions = true;

	/**
	 * Include hidden sessions in the results.
	 * C++ only.
	 * Set automatically for every targeted query below, because those name one session, hidden or not.
	 */
	bool bIncludeHiddenSessions = false;

	/**
	 * Which call the search makes.
	 * By Friend asks for the session the friend in Search Target Id is in, and needs an online subsystem with friends, such as Steam.
	 * Max Results and LAN Query are then ignored, and the filters above still apply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	EEasySessionSearchMode SearchMode = EEasySessionSearchMode::Default;

	/** The friend the mode above asks about. Ignored while the mode is Default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FUniqueNetIdRepl SearchTargetId;

	/**
	 * Only return sessions hosted by this player.
	 * The online subsystem filters on it, so the host's session is found however many other sessions exist.
	 * NULL (LAN) filters the returned results instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FUniqueNetIdRepl OwnerId;

	/** Only return the session advertising this join code, hidden or not. Case does not matter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString JoinCode;

	/** @return Whether these params can run as a search. */
	bool IsValid() const;

	/** @return Whether these params name one specific session (a search mode, an owner or a join code) rather than describing the sessions to look for. */
	bool IsSpecificSessionQuery() const { return SearchMode != EEasySessionSearchMode::Default || OwnerId.IsValid() || !JoinCode.IsEmpty(); }

	/** @return Whether this search result passes every filter above. */
	bool ShouldInclude(const FEasySessionSearchResult& Result) const;
};

/**
 * A single session found by a search.
 * Pass this to Join Easy Session to join it, or to Join Easy Party when Is Party is set.
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

	/** Whether the session is hidden from searches. Hidden sessions are filtered out of Find results, so Blueprint never receives one. */
	bool bIsHidden = false;

	/** The region the session advertises. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	EEasySessionRegion Region = EEasySessionRegion::Any;

	/** Whether the session's match is in progress right now. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bMatchInProgress = false;

	/** Whether this is a party rather than a game session. An accepted invite can be either, and a party is joined with Join Easy Party. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsParty = false;

	/**
	 * The session's join code.
	 * C++ only.
	 * The Join Code filter compares it, and keeping it off Blueprint means a session browser cannot list other sessions' codes.
	 */
	FString JoinCode;

	/** Custom key-value data advertised with the session. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	TMap<FString, FString> CustomSettings;

	/** The underlying online subsystem search result. Not exposed to Blueprint. */
	FOnlineSessionSearchResult NativeResult;

	/** @return Whether this search result can be joined. */
	bool IsValid() const;

	/** Build an EasySession search result from a native online subsystem result. */
	static FEasySessionSearchResult FromNative(const FOnlineSessionSearchResult& InNativeResult);
};

/**
 * State of a matchmaking run.
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

	/** No session was found, so this player is hosting one. */
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
	 * Session to host when no session is found.
	 * Ignored while Allow Host Fallback is off.
	 * The fallback inherits the search's filters.
	 * It hosts on the searched network (LAN Query) and advertises every Required Custom Settings pair, overwriting the same key in Custom Settings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	FEasySessionHostParams Host;

	/**
	 * Whether to host a session when no session is found.
	 * With this on, matchmaking fails with Invalid Params before the first search while Host has no Initial Map Name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bAllowHostFallback = false;

	//~ Advanced fields are folded behind the Make node's advanced arrow, for the reason described in FEasySessionHostParams.

	/** Password sent when joining a password protected session. Without one, protected sessions are never tried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "EasySession")
	FString JoinPassword;

	/** How many search passes to run before the run fails or hosts. */
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

	/** The friend's unique id. Set it as a search's Search Target Id to find the session they are in. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FUniqueNetIdRepl NativeId;

	/** @return Whether this friend has an id, which invites and friend searches need. */
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
 * One advertised custom setting, as replicated.
 * A TMap cannot replicate, so Custom Settings replicate as an array of these.
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
 * That includes the join code, so any session member can share it.
 * Only the password and its friends exception stay on the host.
 * Clients read the values through the regular session getters.
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

	/** Whether the host wrote this payload. False on the property's defaults. */
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

	/**
	 * Is this player ready, as Set Easy Session Ready last set it.
	 * Every travel of the session sets it back to false.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsReady = false;

	/** The player's id on the online subsystem. Names can repeat between players. This cannot. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FUniqueNetIdRepl PlayerId;
};

/**
 * The settings of a party: how many players it holds and who finds it.
 * Create Easy Party takes them, and Get Easy Party Settings reads them back.
 * Every party is advertised, so a member can find the leader's party again after a match.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyPartySettings
{
	GENERATED_BODY()

	/** The most players the party holds, the leader included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession", meta = (ClampMin = "2", UIMin = "2"))
	int32 MaxMembers = 4;

	/**
	 * Whether the party is left out of the Find Easy Parties results, while it stays advertised.
	 * Players reach a hidden party through an invite or a search with a Join Code, an Owner Id or a friend.
	 * A hidden party still admits every player who reaches it, unless it is full or the leader kicked that player.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bHidden = true;

	/**
	 * Whether the party advertises a generated six character join code, readable with Get Easy Party Join Code.
	 * Find Easy Parties with the code in Join Code returns the party, hidden or not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EasySession")
	bool bUseJoinCode = false;

	/** @return Whether these settings can create a party. */
	bool IsValid() const { return MaxMembers >= 2; }
};

/**
 * Information about a single member of the party.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyPartyMemberInfo
{
	GENERATED_BODY()

	/** Display name of the member. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FString PlayerName;

	/** Whether this member is the local player on this machine. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsLocalPlayer = false;

	/** Whether this member leads the party. The leader hosts it and decides where the party goes. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsLeader = false;

	/** Whether this member is ready, as Set Easy Party Ready set it. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	bool bIsReady = false;

	/** The member's id on the online subsystem. Names can repeat between players. This cannot. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FUniqueNetIdRepl PlayerId;
};

/**
 * Why the local player is no longer in the party.
 */
UENUM(BlueprintType)
enum class EEasyPartyLeaveReason : uint8
{
	/** The local player called Leave Easy Party. */
	Left,

	/** The leader removed the local player, who cannot join this party again. Reason Text is the leader's reason. */
	Kicked,

	/** The leader left, which ends the party for every member. */
	LeaderLeft UMETA(DisplayName = "Leader Left"),

	/**
	 * The party could not continue, and Reason Text says why.
	 * A member gets it when the connection to the leader is lost, and the leader when the party beacon cannot start again after a map change.
	 * Either gets it when the restore after a match still fails once Party Restore Wait Seconds runs out.
	 */
	ConnectionLost UMETA(DisplayName = "Connection Lost"),

	/** The party entered a game session, which closes the party. */
	MovedToGameSession UMETA(DisplayName = "Moved To Game Session")
};

/**
 * Why the local player was disconnected from a session.
 */
UENUM(BlueprintType)
enum class EEasyDisconnectReason : uint8
{
	/** No disconnect has been recorded. */
	None,

	/** The connection to the host was lost, before or after the map loaded: the host quit, crashed, or the network failed. */
	ConnectionLost,

	/** The host destroyed the session and every client traveled back to the menu. */
	HostDestroyedSession,

	/** Traveling to the session's map failed. */
	TravelFailure,

	/**
	 * PreLogin on the host refused this player when the connection arrived, and Reason Text is the refusal message.
	 * It refuses a kicked player, a full session, a match in progress that allows no join, and a password session joined without the reservation beacon.
	 */
	Rejected,

	/**
	 * The host removed this player from the session, and Reason Text is the host's reason.
	 * The host refuses this player again until the session is destroyed.
	 */
	Kicked
};

/**
 * Information about the most recent disconnect from a session.
 * Kept across map travel, so the menu map can read it and show a popup.
 */
USTRUCT(BlueprintType)
struct EASYSESSION_API FEasyDisconnectInfo
{
	GENERATED_BODY()

	/** Why the player was disconnected. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	EEasyDisconnectReason Reason = EEasyDisconnectReason::None;

	/** Human readable description. Safe to show to the player or write to a log without changes. */
	UPROPERTY(BlueprintReadOnly, Category = "EasySession")
	FText ReasonText;
};

/** Delegate fired when a session request completes. */
DECLARE_DELEGATE_TwoParams(FEasySessionCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/);

/** Delegate fired when a session search completes. */
DECLARE_DELEGATE_ThreeParams(FEasySessionFindCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& /*Results*/);

/** Delegate fired when reading the friends list completes. */
DECLARE_DELEGATE_ThreeParams(FEasyFriendsCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasySessionFriend>& /*Friends*/);

/** Delegate fired when finding friend sessions completes. */
DECLARE_DELEGATE_ThreeParams(FEasyFriendSessionsCompleteDelegate, EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/, const TArray<FEasyFriendSession>& /*FriendSessions*/);
