# API Reference

*[한국어](API.ko.md)*

Every public surface, one line each. Tooltips in the editor carry the same information;
this page is here so you can see the whole set at once instead of finding the nodes one
at a time.

Behavior lives in the guides - this page gives you the names and shapes, and links out.

**New to the plugin?** [Quick Start](QuickStart.en.md) gets a host and a joiner talking with
three nodes; come back here for the rest.

**Where the nodes are.** Most of them appear when you right-click in a Blueprint graph and
type the name - nothing to set up first. The rest are called on the subsystem: place a
`Get Easy Session Subsystem` node and drag off its output pin. The tables below separate
the two.

**In C++**, get the subsystem with
`GetGameInstance()->GetSubsystem<UEasySessionSubsystem>()`; it carries the same queries
under the shorter names in the C++ column. The standalone nodes are `UEasySessionStatics`
functions - the node name without spaces, taking any actor or widget as their first
argument so they can find the world.

1. [Async Blueprint nodes](#1-async-blueprint-nodes) - create, find, join, matchmaking
2. [Query Blueprint nodes](#2-query-blueprint-nodes) - session state, players, search results
3. [Action Blueprint nodes](#3-action-blueprint-nodes) - travel, cancel, invites
4. [Events](#4-events) | 5. [Structs](#5-structs) | 6. [Enums](#6-enums)
7. [UEasyMatchmakingPolicy](#7-ueasymatchmakingpolicy) | 8. [UEasySessionConfig](#8-ueasysessionconfig-project-settings---plugins---easysession) | 9. [C++ notes](#9-c-notes) | 10. [Console commands](#10-console-commands-development-builds-only)

## 1. Async Blueprint nodes

These nodes pass their work to the [online subsystem](Concepts.en.md), which answers later. Reaching a service across the internet, such as Steam, can take seconds. That is
why none of them return a value directly: each one finishes through its `OnSuccess` or
`OnFailure` execution pin, both carrying `Result` (`EEasySessionResult`) and
`ErrorMessage` (String). A request rejected before it ever reaches the service - no
online subsystem, a parameter that cannot work - fails on `OnFailure` immediately.

EasySession runs its own operations one at a time, so pressing a button repeatedly
produces results in order instead of errors. The queue covers what goes through EasySession; the engine's
own session nodes still reach the service on their own ([FAQ](FAQ.en.md)).

| Node | Inputs | Notes |
|---|---|---|
| **Create Easy Session** | `HostParams` | Calls `CreateSession` with your params as the advertised `FOnlineSessionSettings`. On a listen server it then travels to Map Name with `?listen` so this game becomes the server, or starts listening on the current map when Map Name is empty. Dedicated servers keep the map they launched with |
| **Find Easy Sessions** | `SearchParams` | Calls `FindSessions` and caches the results. `OnSuccess` carries the `Results` array; hidden sessions are filtered out |
| **Join Easy Session** | `SearchResult`, `Password`, `AdditionalTravelOptions` | Asks the host for approval, then calls `JoinSession`, resolves the host address, and travels there. A wrong password or a closed match fails the node with `WrongPassword` / `JoinRefused` before any map load; only when the host cannot be asked does the refusal arrive later, as a `Rejected` disconnect ([guide](Guide-Sessions.en.md)) |
| **Start Easy Session** | - | Calls `StartSession`: Pending -> InProgress. With Allow Join In Progress off, this is the moment the session stops taking new players - except on Steam, which stopped at the first join ([FAQ](FAQ.en.md)). Session authority only |
| **End Easy Session** | - | Calls `EndSession`: InProgress -> Ended, so Start can run another match on the same session. Session authority only |
| **Update Easy Session** | `NewSettings` | Calls `UpdateSession`: rewrites the advertised `FOnlineSessionSettings` from a `FEasySessionSettings` and re-advertises. The struct holds exactly the fields a live session can change, so there is nothing here that gets ignored. Session authority only |
| **Destroy Easy Session** | - | Calls `DestroySession`: removes this game's named session and stays on the current map. Both the host and the client can host or join again right after |
| **Leave Easy Session** | - | Destroy Easy Session plus the trip home: destroys the named session, then returns to the menu map (Game Default Map). A leaving host closes the room for everyone with "The host has left the game." |
| **Start Easy Matchmaking** | `MatchmakingParams`, `PolicyClass` (optional) | Find, join the best result, and create one when nothing is found. This node runs Find, Join and Create for you ([guide](Guide-Matchmaking.en.md)) |
| **Read Easy Friends** | - | Calls `ReadFriendsList`. `OnSuccess` carries a `FEasySessionFriend` array, ordered for display: playing this game, then online, then offline, each by name. NULL/LAN has no friends, so it fails there |
| **Find Easy Friend Sessions** | - | Reads the friends list, then calls `FindFriendSession` for each friend playing this game. `OnSuccess` carries a `FEasyFriendSession` array - every friend listed, the ones in a joinable session carrying it and sorted to the top. Fails on NULL/LAN |

> **Session authority only** means the game that created the session: the host player's
> game on a listen server, or the server itself on a dedicated server. Anyone else gets
> a `RequiresSessionAuthority` failure. `Server Travel Easy Session` and `Destroy Easy Session
> For Everyone` need it too.
>
> `Is Easy Session Authority` answers whether this game has that authority. In a game a
> player is running, `Is Easy Session Host` gives the same answer, so either node works
> for disabling a menu button. On a dedicated server there is no local player to be the
> host, which makes `Is Easy Session Host` false even though the server created the
> session - so logic that also runs on a dedicated server has to use
> `Is Easy Session Authority`.

## 2. Query Blueprint nodes

These answer immediately and have no execution pins. They change nothing, so they are
safe to call every frame and safe to bind straight to a widget.

### 2.1 Standalone nodes (`UEasySessionStatics`)

The C++ column is not the static's name - it is the subsystem method answering the same
thing, which is the shorter call once you already have the subsystem. The static is the
node name without spaces.

| Node | C++ | Answers |
|---|---|---|
| Is In Easy Session | `IsInSession` | Is there a session at all |
| Is Easy Session Host | `IsHost` | Is the local player the one hosting. False on a dedicated server, which has no local player |
| Is Easy Session Authority | `IsSessionAuthority` | Did this game create the session it is in, so it may Start, End, Update, travel or destroy it. True on a dedicated server too, unlike Is Easy Session Host |
| Get Easy Session State | `GetSessionState` | Where the session is in its lifecycle. Clients read the host's replicated value, so every player sees the same thing |
| Get Easy Session State Label | - | The same state ready to display, e.g. "In Match (InProgress)" |
| Is Easy Session Busy | `IsBusy` | An operation or the level load after it is running. Bind a button's Is Enabled to this |
| Get Easy Session Activity | `GetActivity` | Which operation is running: Creating, Searching, Joining, Leaving, Updating, Starting, Ending, Matchmaking or Traveling. None exactly when Is Easy Session Busy is false. Names invite joins and recoveries too, which no menu started |
| Get Easy Session Display Name | `GetSessionDisplayName` | The name the session is advertised under |
| Get Easy Session Password | `GetSessionPassword` | The password this game is hosting with, to show the host. **Empty on clients** - it never leaves the host |
| Get Easy Session Player Names | `GetSessionPlayerNames` | Everyone in the session, names only. Works on the host and on clients |
| Get Easy Session Player Infos | `GetSessionPlayerInfos` | The same list with host and local-player flags, for a player list UI |
| Get Easy Session Player Count | `GetSessionPlayerCount` | How many players are in the session right now |
| Get Easy Session Max Players | `GetSessionMaxPlayers` | How many it holds. 0 when there is no session |
| Get Last Easy Search Results | `GetLastSearchResults` | The last search's results, readable anywhere. Empty while a new search runs |
| Is Easy Matchmaking Running | `IsMatchmakingRunning` | Is a Matchmaking run in progress |
| Is Easy Friend Search Running | `IsFriendSearchRunning` | Is Find Easy Friend Sessions in progress. Not part of Is Easy Session Busy: it only reads |
| Get Easy Matchmaking State | `GetMatchmakingState` | Which step it is on: Searching, Joining, Hosting, Complete |
| Has Pending Easy Disconnect Info | `HasPendingDisconnectInfo` | Is a disconnect reason waiting. Check this on the menu's Event Construct |
| Get Online Subsystem Name (EasySession) | `GetOnlineSubsystemName` | Which service is active: `NULL` for LAN, `STEAM`, ... |
| Is Online Subsystem Available (EasySession) | `IsOnlineSubsystemAvailable` | Is a subsystem loaded with a valid session interface |
| Get Easy Session Queue Status | `GetQueueStatus` | What the request queue is doing, as a string for status UI and bug reports |
| Get Easy Session Settings | `GetSessionSettings` | The settings the session advertises, so Update can change one field. Works for every member; the password is only filled on the host |
| Get Easy Session Join Code | `GetSessionJoinCode` | The join code the session advertises, or empty. Every player in the room can read and share it |

`To String (EasySessionResult)` (C++ `ResultToString`) turns a result enum into text.

### 2.2 On the subsystem (`UEasySessionSubsystem`)

Call these on `Get Easy Session Subsystem`. The name is the same in Blueprint and C++, so
there is no C++ column.

| Node | Answers |
|---|---|
| Get Active Matchmaking Policy | The running policy object. Progress is also relayed on the subsystem's own events, which need no policy in hand |

> **Which session are these about?** The game session - the one players find,
> join and play in. There is exactly one per process (see Limitations in the README),
> so nothing here takes a session argument. Should a later version add a second kind
> of session, such as a party that lives alongside the match, it will come with its
> own nodes rather than change the meaning of these: `Is In Session` will keep
> answering about the game session for as long as it exists.
>
> Some queries here are not about a session at all: `Is Easy Session Busy` and `Get Easy
> Session Queue Status` describe the operation queue, and `Is Easy Matchmaking Running`,
> `Get Easy Matchmaking State`, `Get Online Subsystem Name (EasySession)` and
> `Is Online Subsystem Available (EasySession)` describe the process. Those keep their meaning whatever sessions exist.

### 2.3 UI text helpers (`UEasySessionUIStatics`)

Pure functions for the text a session UI shows. None of them touch session state, so a menu needs no string assembly of its own.

| Node | C++ | Returns |
|---|---|---|
| Get Result Message | `GetResultMessage` | A player facing sentence for a result, e.g. "The session is full". Success reads "Done" |
| Get Activity Message | `GetActivityMessage` | "Creating the session..." style line for a Get Easy Session Activity value. None gives empty text, so a status line clears itself with it |
| Format Matchmaking Status | `FormatMatchmakingStatus` | "Searching... 12s" style line from a matchmaking state and elapsed seconds. Idle reads "Ready" |
| Format Session Slots | `FormatSessionSlots` | "1/4   ping 32ms" for a search result |
| Get Region Display Name | `GetRegionDisplayName` | "North America East" for a region |
| Get Region Options | `GetRegionOptions` | Every region's display name in enum order, for a combo box |
| Region From Index | `RegionFromIndex` | The region behind a combo box index. Out of range gives Any |

## 3. Action Blueprint nodes

Not async - these return right away. They change state and have execution pins.

What they return means the request was accepted, not that it finished. `Cancel Easy
Matchmaking` stops the run only after the online call already running has come back, and
`Server Travel Easy Session` returns before the new map has loaded.

### 3.1 Standalone nodes (`UEasySessionStatics`)

Same convention as 2.1: the C++ column is the subsystem method, not the static's name.

| Node | C++ | Does |
|---|---|---|
| Consume Last Easy Disconnect Info | `ConsumeLastDisconnectInfo` | Reads the disconnect reason and clears it. Survives map travel, so the menu can show it |
| Cancel Easy Matchmaking | `CancelMatchmaking` | Ends a Matchmaking run with `Canceled`, undoing a join or host that succeeds after the cancel |
| Cancel Easy Friend Search | `CancelFriendSearch` | Ends Find Easy Friend Sessions with `Canceled`. The lookup in flight is ignored |
| Send Easy Session Invite To Friend | `SendSessionInviteToFriend` | Platform invite |
| Show Easy Invite UI | `ShowInviteUI` | Platform invite overlay |
| Show Easy Profile UI | `ShowProfileUI` | Profile overlay for a friend |
| Show Easy Profile UI For Player | `ShowProfileUIForPlayer` | Profile overlay for someone in the session |
| Server Travel Easy Session | `ServerTravelToMap` | Moves the whole session to a new map. Session authority only |
| Destroy Easy Session For Everyone | `DestroyEasySessionForEveryone` | Ends the session and sends every client back to the menu with a reason. Session authority only |

The invite and profile nodes need a platform service. They return false on NULL/LAN.

The last two check session authority themselves: a client that calls either one changes
nothing and gets a warning in the log, so show the button only when
`Is Easy Session Authority` is true rather than relying on the call being harmless.

## 4. Events

Assignable on the subsystem. They fire regardless of who started the operation, so UI
bound to them stays correct even when something else in your game drives the session.

| Event | Payload | Fires when |
|---|---|---|
| `OnSessionCreated` | `Result`, `ErrorMessage` | Create Easy Session finished |
| `OnSessionsFound` | `Result`, `ErrorMessage`, `Results` | Find Easy Sessions finished. The only one that carries the search results |
| `OnSessionJoined` | `Result`, `ErrorMessage` | Join Easy Session finished |
| `OnSessionStarted` | `Result`, `ErrorMessage` | Start Easy Session finished - the match is running |
| `OnSessionEnded` | `Result`, `ErrorMessage` | End Easy Session finished - the match is over, the session is not |
| `OnSessionUpdated` | `Result`, `ErrorMessage` | Update Easy Session finished |
| `OnSessionSettingsChanged` | - | Fired on a client when the host's updated settings arrive. The regular getters already return the new values - refresh the UI from them |
| `OnSessionDestroyed` | `Result`, `ErrorMessage` | Destroy Easy Session finished, both on the host and on a client that left |
| `OnMatchmakingStarted` | - | A Matchmaking run was accepted and its policy registered. Always the first event of a run |
| `OnMatchmakingStateChanged` | `OldState`, `NewState` | The Matchmaking state moved (`Searching`, `Joining`, `Hosting`, `Complete`) |
| `OnMatchmakingUpdated` | `State`, `ElapsedSeconds` | Every Matchmaking state change plus once a second while it runs - drives elapsed-time labels |
| `OnMatchmakingComplete` | `Result`, `ErrorMessage` | A Matchmaking run finished, whether it joined, ended up hosting, or was canceled (`Result` = `Canceled`). Ask `Is Easy Session Host` which |
| `OnSessionFailure` | `Reason` (String) | Not an operation finishing - the connection died or a network error hit. The dead session is cleaned up for you |
| `OnBusyChanged` | `bBusy` | Is Easy Session Busy flipped. Bind once and enable or disable session buttons from the flag instead of polling every tick. On the true edge, Get Easy Session Activity says which operation began |
| `OnSessionInviteAccepted` | `Session` (`FEasySessionSearchResult`) | The player accepted an invite in the platform overlay. With Auto Join Accepted Invites on, the join follows on its own - unless this player is already in a session, which needs `bAcceptInvitesWhileInSession` |

`Result` and `ErrorMessage` are the same values the node's own pins would have given you.

## 5. Structs

### 5.1 FEasySessionSettings
`SessionDisplayName` (String), `MaxPlayers` (int), `bShouldAdvertise`, `bHidden`, `Password` (String), `bFriendsBypassPassword`, `bAllowJoinInProgress`, `bAllowInvites`, `Region` (`EEasySessionRegion`), `bUseJoinCode`, `CustomSettings` (Map String->String)

What a session advertises about itself. `Update Easy Session` takes exactly this struct,
so every field here is one a live session can change.

### 5.2 FEasySessionHostParams *(FEasySessionSettings plus)*
`MapName` (String), `HostMode` (`EEasySessionHostMode`), `bIsLANMatch`, `bStartListening`, `bUsePresence`, `AdditionalTravelOptions` (String)

Hosting is the settings above plus how to bring the server up. These added fields are
read once, while the session is created, which is why Update cannot change them.

Field behavior is in the [session guide](Guide-Sessions.en.md). `bHidden` advertises the
session but keeps it out of Find results, so it can only be reached through an invite.
`Password` and `bFriendsBypassPassword` are covered under
[password protected sessions](Guide-Sessions.en.md#password-protected-sessions).
`AdditionalTravelOptions` is appended
to the host's travel URL (e.g. `GameMode=Deathmatch?MyOption=1`), readable on the server
with `Parse Option`. `Region` and `bUseJoinCode` are covered in the guide's
[regions](Guide-Sessions.en.md#regions) and [join codes](Guide-Sessions.en.md#join-codes)
sections.

### 5.3 FEasySessionSearchParams
`MaxResults` (int), `bLANQuery`, `TimeoutSeconds` (float), `MinOpenSlots` (int), `MaxPingMs` (int), `RequiredCustomSettings` (Map String->String), `Region` (`EEasySessionRegion`), `bIncludeInProgressSessions`, `JoinCode` (String), `SearchMode` (`EEasySessionSearchMode`), `SearchTargetId` (Unique Net Id), `OwnerId` (Unique Net Id)

Four of these name one specific session instead of describing what to look for. `JoinCode` and `OwnerId` are filters over a normal search, so they combine with everything above. `SearchMode` picks a different call to the service - By Friend or By Session Id - and `SearchTargetId` says who or which; the discovery fields are then ignored while the filters still apply. A search naming one session also sees hidden ones, and its results stay off `On Sessions Found` and `Get Last Easy Search Results`.

### 5.4 FEasySessionSearchResult *(read-only)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `bPasswordProtected`, `Region`, `bMatchInProgress`, `CustomSettings`

This struct connects the two nodes: `Find Easy Sessions` returns them and
`Join Easy Session` takes one back. Keep the whole struct - a server-browser row should
store it, not just the name it displays.

### 5.5 FEasyFriendSession
`Friend` (`FEasySessionFriend`), `bHasSession`, `Session` (`FEasySessionSearchResult`)

One entry per friend from `Find Easy Friend Sessions`. `Session` is only valid while
`bHasSession` is true, and joins like any other search result.

`bPasswordProtected` is how you decide whether to prompt before `Join Easy Session`.

### 5.6 FEasyMatchmakingParams
`Search` (SearchParams), `Host` (HostParams - read only while `bAllowHostFallback` is on), `bAllowHostFallback`, `JoinPassword` (String), `MaxSearchPasses` (int), `DelayBetweenPassesSeconds` (float)

### 5.7 FEasySessionPlayerInfo *(read-only)*
`PlayerName`, `bIsLocalPlayer`, `bIsHost` (always false on dedicated servers), `PlayerId` (the online service id - names can repeat, this cannot)

### 5.8 FEasySessionFriend *(read-only)*
`DisplayName`, `bIsOnline`, `bIsPlayingThisGame`, `NativeId` (Unique Net Id)

Returned by `Read Easy Friends`; pass one back to the invite and profile functions.
`NativeId` goes into a search's `SearchTargetId` to find the session that friend is in.

### 5.9 FEasyDisconnectInfo *(read-only)*
`Reason` (`EEasyDisconnectReason`), `ReasonText` (Text)

## 6. Enums

### 6.1 EEasySessionResult

Every node's `Result` pin. The ones worth branching on are marked.

| Value | Means |
|---|---|
| `Success` | It worked |
| **`SessionAlreadyExists`** | You are already in a session. `Destroy Easy Session` first |
| **`NoSessionExists`** | There is no session to act on |
| **`NoSessionsFound`** | The search ran fine and found nothing. Not an error - offer to host |
| **`JoinSessionFull`** | The room is full - refused by the host before the travel, or by the online service after it |
| **`JoinSessionDoesNotExist`** | The room was gone by the time you joined. Search again |
| **`WrongPassword`** | The host refused the join: the password did not match. Let the player retype it |
| **`JoinRefused`** | The host refused the join for another reason, e.g. the match no longer takes players. `ErrorMessage` is the host's own sentence, safe to show |
| **`ResolveFailure`** | Joined, but the host address does not work - usually a host that never became a listen server ([FAQ](FAQ.en.md)) |
| **`RequiresSessionAuthority`** | Only the game that created the session may do this. Show the button only when `Is Easy Session Authority` is true |
| **`Timeout`** | The online service never answered. The outcome is unknown, so anything it left behind is cleaned up. See `RequestTimeoutSeconds` |
| **`Canceled`** | `Cancel Easy Matchmaking` stopped a Matchmaking run |
| `NoOnlineSubsystem` | No subsystem is configured. Check `DefaultEngine.ini` |
| `InvalidParams` | A parameter cannot work, e.g. Matchmaking with no fallback Map Name |
| `MatchmakingAlreadyInProgress` | A Matchmaking is already running |
| `CreateFailure`, `SearchFailure`, `JoinFailure`, `DestroyFailure`, `UpdateFailure`, `StateChangeFailure` | The online service refused that specific call. `ErrorMessage` carries what it said |
| `UnknownFailure` | Nothing more specific was available |

### 6.2 EEasySessionState

`NoSession`, `Creating`, `Pending`, `Starting`, `InProgress`, `Ending`, `Ended`, `Destroying`

`Pending` is a session waiting to start; `Ended` is a finished match that `Start Easy Session` can play again. The `-ing` values are the moments an operation is still running.

### 6.3 EEasyDisconnectReason

Read with `Consume Last Easy Disconnect Info`. Branch on `Reason`, show `ReasonText`.

| Value | Means |
|---|---|
| `None` | Nothing was recorded |
| `ConnectionLost` | The link died - the host quit, crashed, or the network dropped |
| `HostDestroyedSession` | The host deliberately sent everyone back, via `Destroy Easy Session For Everyone` |
| `TravelFailure` | The session's map failed to load |
| `Rejected` | The host refused the connection and said why: a wrong password, a match no longer taking players. `ReasonText` is the host's own sentence, safe to show |

### 6.4 EEasyMatchmakingState

`Idle`, `Searching`, `Joining`, `Hosting`, `Complete` - the phases of one Matchmaking run, reported through `OnStateChanged`.

### 6.5 EEasySessionHostMode

`ListenServer` (the hosting player's game is the server) or `DedicatedServer` (code path present, not validated in 1.0).

### 6.6 EEasySessionRegion

`Any` plus nine coarse world regions, from `NorthAmericaEast` to `Oceania`, cut so that one region means playable latency. A game that needs its own split leaves this at `Any` and filters with a `CustomSettings` key instead ([guide](Guide-Sessions.en.md#regions)).

### 6.7 EEasySessionSearchMode

`Default` searches for the sessions the filters describe. `ByFriend` and `BySessionId` ask the service for one exact session instead, reading `SearchTargetId` for who or which.

### 6.8 EEasySessionActivity

`None`, `Creating`, `Searching`, `Joining`, `Leaving`, `Updating`, `Starting`, `Ending`, `Matchmaking`, `Traveling` - what the plugin is doing right now, from `Get Easy Session Activity`. It names the operation whoever started it, so a status widget can narrate an invite join or a disconnect recovery the menu never asked for. `Get Activity Message` turns it into a sentence.

## 7. UEasyMatchmakingPolicy

The object behind `Start Easy Matchmaking`: it searches, joins the best result it finds, and hosts when it finds none.

Make a subclass in Blueprint or C++ and override **`ScoreSession(Session) -> float`** (higher = joined first) for custom pick-a-session criteria. Editable defaults: `PingBucketsMs` (default `[50, 100, 150]`), `TopCandidateRandomization` (default 3). Query with `GetState` and `GetElapsedSeconds`, or bind `OnStateChanged` / `OnUpdated`.

## 8. UEasySessionConfig (Project Settings -> Plugins -> EasySession)

| Setting | Default | Effect |
|---|---|---|
| `bAutoReturnToMenuOnDisconnect` | true | On disconnect or a failed travel, clean up the session and browse to the project's **Game Default Map**, keeping the reason for that map to read. Off leaves the player where they are |
| `bAutoJoinAcceptedInvites` | true | Accepting a platform invite joins that session immediately. Off gives you only `OnSessionInviteAccepted` |
| `bAcceptInvitesWhileInSession` | false | An accepted invite may destroy the session this player is in and join the invited one. Off by default so one click in the overlay cannot end a running match; `OnSessionInviteAccepted` still fires, so you can ask first |
| `RequestTimeoutSeconds` | 30 | How long a request waits for the online service before failing with `Timeout`. **0 waits forever.** Searching adds its own Timeout Seconds on top of this |
| `bAutoHostOnDedicatedServer` | true | A dedicated server advertises itself once its map is up |
| `DedicatedServerHostParams` | - | Params used by that auto host. Map Name is ignored - the server keeps its launch map |

## 9. C++ notes

Operations are callable natively with delegate callbacks: `CreateEasySession`,
`FindEasySessions`, `JoinEasySession`, `DestroyEasySession`, `UpdateEasySession`,
`StartMatchmaking`. Blueprint and C++ take the same code path.

`OnModifyServerTravelURL` and `OnModifyClientTravelURL` are C++ only delegates on the
subsystem. They give you the travel URL just before travel so you can append your own
options. Bind once at startup: the hook fires before that operation's completion callback,
so a handler bound inside the callback misses its own travel. For anything expressible as
a static string, prefer `AdditionalTravelOptions`.

## 10. Console commands *(development builds only)*

`EasySession.Host [Map]`, `EasySession.Find`, `EasySession.Join [Index] [Password]`, `EasySession.Matchmaking [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`, `EasySession.Friends`, `EasySession.InviteUI`, `EasySession.Diagnose`
