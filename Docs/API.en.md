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

1. [Async Blueprint nodes](#1-async-blueprint-nodes) - create, find, join, quick match
2. [Query Blueprint nodes](#2-query-blueprint-nodes) - session state, players, search results
3. [Action Blueprint nodes](#3-action-blueprint-nodes) - travel, cancel, invites
4. [Events](#4-events) | 5. [Structs](#5-structs) | 6. [Enums](#6-enums)
7. [UEasyQuickMatchPolicy](#7-ueasyquickmatchpolicy) | 8. [UEasySessionSettings](#8-ueasysessionsettings-project-settings---plugins---easysession) | 9. [C++ notes](#9-c-notes) | 10. [Console commands](#10-console-commands-development-builds-only)

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
| **Update Easy Session** | `NewHostParams` | Calls `UpdateSession`: rewrites the advertised `FOnlineSessionSettings` - player cap, advertise, join-in-progress, invites, display name, hidden, password, custom settings - and re-advertises. Map Name / Host Mode ignored. Session authority only |
| **Destroy Easy Session** | - | Calls `DestroySession`: the host destroys the session, a client only leaves it. Both the host and the client can host or join again right after |
| **Quick Match Easy Session** | `QuickMatchParams`, `PolicyClass` (optional) | Find, join the best result, and create one when nothing is found. This node runs Find, Join and Create for you ([guide](Guide-QuickMatch.en.md)) |
| **Read Easy Friends** | - | Calls `ReadFriendsList`. `OnSuccess` carries a `FEasySessionFriend` array. NULL/LAN has no friends, so it fails there |

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
| Get Easy Session Display Name | `GetSessionDisplayName` | The name the session is advertised under |
| Get Easy Session Password | `GetSessionPassword` | The password this game is hosting with, to show the host. **Empty on clients** - it never leaves the host |
| Get Easy Session Player Names | `GetSessionPlayerNames` | Everyone in the session, names only. Works on the host and on clients |
| Get Easy Session Player Infos | `GetSessionPlayerInfos` | The same list with host and local-player flags, for a player list UI |
| Get Easy Session Player Count | `GetSessionPlayerCount` | How many players are in the session right now |
| Get Easy Session Max Players | `GetSessionMaxPlayers` | How many it holds. 0 when there is no session |
| Get Last Easy Search Results | `GetLastSearchResults` | The last search's results, readable anywhere. Empty while a new search runs |
| Is Easy Quick Match Running | `IsQuickMatchRunning` | Is a Quick Match run in progress |
| Get Easy Quick Match State | `GetQuickMatchState` | Which step it is on: Searching, Joining, Hosting, Complete |
| Has Pending Easy Disconnect Info | `HasPendingDisconnectInfo` | Is a disconnect reason waiting. Check this on the menu's Event Construct |
| Get Online Subsystem Name | `GetOnlineSubsystemName` | Which service is active: `NULL` for LAN, `STEAM`, ... |
| Is Online Subsystem Available | `IsOnlineSubsystemAvailable` | Is a subsystem loaded with a valid session interface |
| Get Easy Session Queue Status | `GetQueueStatusDescription` | What the request queue is doing, as a string for status UI and bug reports |
| Get Easy Session Host Params | `GetEasySessionHostParams` | The params the session was created with, so Update can change one field. Host only |

`To String (EasySessionResult)` (C++ `ResultToString`) turns a result enum into text.

### 2.2 On the subsystem (`UEasySessionSubsystem`)

Call these on `Get Easy Session Subsystem`. The name is the same in Blueprint and C++, so
there is no C++ column.

| Node | Answers |
|---|---|
| Get Active Quick Match Policy | The policy object, for binding `OnStateChanged` |

> **Which session are these about?** The game session - the one players find,
> join and play in. There is exactly one per process (see Limitations in the README),
> so nothing here takes a session argument. Should a later version add a second kind
> of session, such as a party that lives alongside the match, it will come with its
> own nodes rather than change the meaning of these: `Is In Session` will keep
> answering about the game session for as long as it exists.
>
> Some queries here are not about a session at all: `Is Easy Session Busy` and `Get Queue
> Status Description` describe the operation queue, and `Is Easy Quick Match Running`,
> `Get Easy Quick Match State`, `Get Online Subsystem Name` and `Is Online Subsystem Available`
> describe the process. Those keep their meaning whatever sessions exist.

## 3. Action Blueprint nodes

Not async - these return right away. They change state and have execution pins.

What they return means the request was accepted, not that it finished. `Cancel Easy
Quick Match` stops the run only after the online call already running has come back, and
`Server Travel Easy Session` returns before the new map has loaded.

### 3.1 Standalone nodes (`UEasySessionStatics`)

Same convention as 2.1: the C++ column is the subsystem method, not the static's name.

| Node | C++ | Does |
|---|---|---|
| Consume Last Easy Disconnect Info | `ConsumeLastDisconnectInfo` | Reads the disconnect reason and clears it. Survives map travel, so the menu can show it |
| Cancel Easy Quick Match | `CancelQuickMatch` | Ends a Quick Match run with `Canceled` |
| Send Easy Session Invite To Friend | `SendSessionInviteToFriend` | Platform invite |
| Show Easy Invite UI | `ShowInviteUI` | Platform invite overlay |
| Show Easy Profile UI | `ShowProfileUI` | Profile overlay for a friend |
| Show Easy Profile UI For Player | `ShowProfileUIForPlayer` | Profile overlay for someone in the session |
| Server Travel Easy Session | `ServerTravelToMap` | Moves the whole session to a new map. Session authority only |
| Destroy Easy Session For Everyone | `DestroyEasySessionForEveryone` | Ends the session and sends every client back to the menu with a reason. Session authority only |

The invite and profile nodes need a platform service. They return false on NULL/LAN.

The last two check session authority themselves: a client that calls either one changes
nothing and gets a warning in the log, so gate the button with `Is Easy Session Authority`
rather than relying on the call being harmless.

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
| `OnSessionDestroyed` | `Result`, `ErrorMessage` | Destroy Easy Session finished, both on the host and on a client that left |
| `OnQuickMatchComplete` | `Result`, `ErrorMessage` | A Quick Match run finished, whether it joined or ended up hosting. Ask `Is Easy Session Host` which |
| `OnSessionFailure` | `Reason` (String) | Not an operation finishing - the connection died or a network error hit. The dead session is cleaned up for you |
| `OnSessionInviteAccepted` | `Session` (`FEasySessionSearchResult`) | The player accepted an invite in the platform overlay. With Auto Join Accepted Invites on, the join follows on its own - unless this player is already in a session, which needs `bAcceptInvitesWhileInSession` |

`Result` and `ErrorMessage` are the same values the node's own pins would have given you.

## 5. Structs

### 5.1 FEasySessionHostParams
`SessionDisplayName` (String), `MapName` (String), `HostMode` (`EEasySessionHostMode`), `MaxPlayers` (int), `bIsLANMatch`, `bStartListening`, `bShouldAdvertise`, `bHidden`, `Password` (String), `bFriendsBypassPassword`, `AdditionalTravelOptions` (String), `bAllowJoinInProgress`, `bAllowInvites`, `bUsePresence`, `CustomSettings` (Map String->String)

Field behavior is in the [session guide](Guide-Sessions.en.md). `bHidden` advertises the
session but keeps it out of Find results, so it can only be reached through an invite.
`Password` and `bFriendsBypassPassword` are covered under
[password protected sessions](Guide-Sessions.en.md#password-protected-sessions).
`AdditionalTravelOptions` is appended
to the host's travel URL (e.g. `GameMode=Deathmatch?MyOption=1`), readable on the server
with `Parse Option`.

### 5.2 FEasySessionSearchParams
`MaxResults` (int), `bLANQuery`, `TimeoutSeconds` (float), `MinOpenSlots` (int), `MaxPingMs` (int), `RequiredCustomSettings` (Map String->String)

### 5.3 FEasySessionSearchResult *(read-only)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `bPasswordProtected`, `CustomSettings`

This struct connects the two nodes: `Find Easy Sessions` returns them and
`Join Easy Session` takes one back. Keep the whole struct - a server-browser row should
store it, not just the name it displays.

`bPasswordProtected` is how you decide whether to prompt before `Join Easy Session`.

### 5.4 FEasyQuickMatchParams
`Search` (SearchParams), `Host` (HostParams - read only while `bAllowHostFallback` is on), `bAllowHostFallback`, `MaxSearchPasses` (int), `DelayBetweenPassesSeconds` (float)

### 5.5 FEasySessionPlayerInfo *(read-only)*
`PlayerName`, `bIsLocalPlayer`, `bIsHost` (always false on dedicated servers), `PlayerId` (the online service id - names can repeat, this cannot)

### 5.6 FEasySessionFriend *(read-only)*
`DisplayName`, `bIsOnline`, `bIsPlayingThisGame`

Returned by `Read Easy Friends`; pass one back to the invite and profile functions.

### 5.7 FEasyDisconnectInfo *(read-only)*
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
| **`JoinSessionFull`** | The room filled up between the search and the join |
| **`JoinSessionDoesNotExist`** | The room was gone by the time you joined. Search again |
| **`WrongPassword`** | The host refused the join: the password did not match. Let the player retype it |
| **`JoinRefused`** | The host refused the join for another reason, e.g. the match no longer takes players. `ErrorMessage` is the host's own sentence, safe to show |
| **`ResolveFailure`** | Joined, but the host address does not work - usually a host that never became a listen server ([FAQ](FAQ.en.md)) |
| **`RequiresSessionAuthority`** | Only the game that created the session may do this. Show the button only when `Is Easy Session Authority` is true |
| **`Timeout`** | The online service never answered. The outcome is unknown, so anything it left behind is cleaned up. See `RequestTimeoutSeconds` |
| **`Canceled`** | `Cancel Easy Quick Match` stopped a Quick Match run |
| `NoOnlineSubsystem` | No subsystem is configured. Check `DefaultEngine.ini` |
| `InvalidParams` | A parameter cannot work, e.g. Quick Match with no fallback Map Name |
| `QuickMatchAlreadyInProgress` | A Quick Match is already running |
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

### 6.4 EEasyQuickMatchState

`Idle`, `Searching`, `Joining`, `Hosting`, `Complete` - the phases of one Quick Match run, reported through `OnStateChanged`.

### 6.5 EEasySessionHostMode

`ListenServer` (the hosting player's game is the server) or `DedicatedServer` (code path present, not validated in 1.0).

## 7. UEasyQuickMatchPolicy

The object behind `Quick Match Easy Session`: it searches, joins the best result it finds, and hosts when it finds none.

Make a subclass in Blueprint or C++ and override **`ScoreSession(Session) -> float`** (higher = joined first) for custom pick-a-session criteria. Editable defaults: `PingBucketsMs` (default `[50, 100, 150]`), `TopCandidateRandomization` (default 3). Query with `GetState`, or bind `OnStateChanged`.

## 8. UEasySessionSettings (Project Settings -> Plugins -> EasySession)

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
`StartQuickMatch`. Blueprint and C++ take the same code path.

`OnModifyServerTravelURL` and `OnModifyClientTravelURL` are C++ only delegates on the
subsystem. They give you the travel URL just before travel so you can append your own
options. Bind once at startup: the hook fires before that operation's completion callback,
so a handler bound inside the callback misses its own travel. For anything expressible as
a static string, prefer `AdditionalTravelOptions`.

## 10. Console commands *(development builds only)*

`EasySession.Host [Map]`, `EasySession.Find`, `EasySession.Join [Index] [Password]`, `EasySession.QuickMatch [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`, `EasySession.Friends`, `EasySession.InviteUI`, `EasySession.Diagnose`
