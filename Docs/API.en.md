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

1. [Async Blueprint nodes](#1-async-blueprint-nodes) - create, find, join, matchmaking, parties
2. [Query Blueprint nodes](#2-query-blueprint-nodes) - session state, players, parties, search results
3. [Action Blueprint nodes](#3-action-blueprint-nodes) - travel, cancel, ready, kick, invites
4. [Events](#4-events) | 5. [Structs](#5-structs) | 6. [Enums](#6-enums)
7. [UEasyMatchmakingPolicy](#7-ueasymatchmakingpolicy) | 8. [UEasySessionConfig](#8-ueasysessionconfig-project-settings---plugins---easysession) | 9. [C++ notes](#9-c-notes) | 10. [Console commands](#10-console-commands-development-builds-only)

## 1. Async Blueprint nodes

These nodes pass their work to the [online subsystem](Concepts.en.md), which answers later. Reaching a service across the internet, such as Steam, can take seconds. That is
why none of them return a value directly: each one finishes through its `OnSuccess` or
`OnFailure` execution pin, both carrying `Result` (`EEasySessionResult`) and
`ErrorMessage` (String). A request rejected before it ever reaches the service - no
online subsystem, a parameter that cannot work - fails on `OnFailure` immediately.

EasySession runs its own requests one at a time, so a call made while another is still
running waits its turn instead of failing. The queue covers what goes through EasySession; the engine's
own session nodes still reach the service on their own ([FAQ](FAQ.en.md)).

| Node | Inputs | Notes |
|---|---|---|
| **Create Easy Session** | `HostParams` | Calls `CreateSession` with your params as the advertised `FOnlineSessionSettings`, then travels to Initial Map Name with `?listen` so this game becomes the server. Fails with `InvalidParams` while Initial Map Name is empty, because the session would be advertised with no server behind it |
| **Find Easy Sessions** | `SearchParams` | Calls `FindSessions` and caches the results. `OnSuccess` carries the `Results` array; hidden sessions are filtered out |
| **Join Easy Session** | `SearchResult`, `Password`, `AdditionalTravelOptions` | Asks the host for a reservation first, then calls `JoinSession`, resolves the host address, and travels there. A wrong password or a closed match fails the node with `WrongPassword` / `JoinRefused` before any map load; only when the host cannot be asked by a player outside a session does the refusal arrive later, as a `Rejected` disconnect ([guide](Guide-Sessions.en.md)) |
| **Start Easy Session** | - | Calls `StartSession`: Pending -> InProgress. With Allow Join In Progress off, this is the moment the session stops taking new players - except on Steam, which stopped at the first join ([FAQ](FAQ.en.md)). Session authority only |
| **End Easy Session** | - | Calls `EndSession`: InProgress -> Ended, so Start can run another match on the same session. Session authority only |
| **Update Easy Session** | `NewSettings` | Calls `UpdateSession`: rewrites the advertised `FOnlineSessionSettings` from a `FEasySessionSettings` and re-advertises. The struct holds exactly the fields a live session can change, so there is nothing here that gets ignored. Session authority only |
| **Destroy Easy Session** | - | Calls `DestroySession`: removes this game's named session and stays on the current map. Both the host and the client can host or join again right after |
| **Leave Easy Session** | - | Destroy Easy Session plus the trip home: destroys the named session, then returns to the menu map (Game Default Map). A leaving host closes the session for everyone with "The host has left the game." |
| **Start Easy Matchmaking** | `MatchmakingParams`, `PolicyClass` (optional) | Find, join the best result, and create one when nothing is found. This node runs Find, Join and Create for you ([guide](Guide-Matchmaking.en.md)) |
| **Read Easy Friends** | - | Calls `ReadFriendsList`. `OnSuccess` carries a `FEasySessionFriend` array, ordered for display: playing this game, then online, then offline, each by name. NULL/LAN has no friends, so it fails there with `NotSupportedByService` |
| **Find Easy Friend Sessions** | - | Reads the friends list, then calls `FindFriendSession` for each friend playing this game. `OnSuccess` carries a `FEasyFriendSession` array - every friend listed, the ones in a joinable session carrying it and sorted to the top. Fails with `NotSupportedByService` on NULL/LAN |
| **Create Easy Party** | `PartySettings` | Creates a party with the local player as its leader. Nothing travels ([guide](Guide-Party.en.md)) |
| **Find Easy Parties** | `SearchParams` | Lists the Public parties, or with a Join Code the one party that advertises it. Only parties come back |
| **Join Easy Party** | `SearchResult` | Joins a party Find Easy Parties returned. The leader decides, and a refusal fails with `JoinRefused` and the leader's reason |
| **Leave Easy Party** | - | Leaves the party. A leader who leaves ends it for every member |

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

**Calling a request from inside a session.** A player holds one game session at a time, so these three requests check the current one first:

| Node | What happens in a session |
|---|---|
| **Create Easy Session** | Fails with `SessionAlreadyExists`. Leave the session first. A host changes maps with `Server Travel Easy Session` instead |
| **Start Easy Matchmaking** | Fails with `SessionAlreadyExists`. Leave the session first, or pick a session yourself and join it. The host of a session whose match has not started is the exception: the run brings every player along ([guide](Guide-Sessions.en.md#bringing-the-other-players)) |
| **Join Easy Session** | Switches sessions. The current session is left only after the new host approved the join, so a refused join keeps the player where they were. A host whose match has not started brings every player along. Fails with `SessionAlreadyExists` for the session already held, and for the host of a match in progress, whose leaving would end the match for everyone. Fails with `JoinRefused`, keeping the current session, when the new host cannot be asked |
| **Create Easy Party**, **Join Easy Party** | Fail with `SessionAlreadyExists`, because a party lives outside game sessions |

**Calling a request in a party.** The leader decides where the party goes, so the same requests behave differently for the leader and for a member:

| Node | Party leader | Party member |
|---|---|---|
| **Create Easy Session** | Brings the party. Max Players must fit the party, or `InvalidParams` | `InParty` |
| **Join Easy Session** | Asks for room for the whole party and brings it. No room is `JoinSessionFull` | `InParty` |
| **Start Easy Matchmaking** | Only considers sessions with room for the whole party, and brings it | `InParty` |
| **Create Easy Party**, **Join Easy Party** | `SessionAlreadyExists`. Leave the party first | `SessionAlreadyExists`. Leave the party first |
| **Kick Easy Party Member** | Allowed | `RequiresPartyLeader` |
| The party invite nodes | Allowed | Allowed |

Entering the game session closes the party for everyone, with `OnPartyLeft` and `MovedToGameSession`. The [Party guide](Guide-Party.en.md#the-leader-decides-where-the-party-goes) has the details.

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
| Is Easy Session Busy | `IsBusy` | A request or the level load after it is running. Bind a button's Is Enabled to this |
| Get Easy Session Activity | `GetActivity` | Which activity is running: Creating, Searching, Joining, Leaving, Updating, Starting, Ending, Matchmaking or Traveling. None exactly when Is Easy Session Busy is false. Names invite joins and recoveries too, which no menu started |
| Get Easy Session Display Name | `GetSessionDisplayName` | The name the session is advertised under |
| Get Easy Session Password | `GetSessionSettings().Password` | The password this game is hosting with, to show the host. **Empty on clients** - it never leaves the host |
| Get Easy Session Player Infos | `GetSessionPlayerInfos` | Everyone in the session with host, local-player and ready flags, for a player list UI |
| Get Easy Session Player Count | `GetSessionPlayerCount` | How many players are in the session right now |
| Get Easy Session Max Players | `GetSessionMaxPlayers` | How many it holds. 0 when there is no session |
| Is Easy Matchmaking Running | `IsMatchmakingRunning` | Is a Matchmaking run in progress |
| Get Easy Matchmaking State | `GetMatchmakingState` | Which state it is in: Searching, Joining, Hosting, Canceling, Complete |
| Has Pending Easy Disconnect Info | `HasPendingDisconnectInfo` | Is a disconnect reason waiting. Check this on the menu's Event Construct |
| Get Online Subsystem Name (EasySession) | - | Which service is active: `NULL` for LAN, `STEAM`, ... |
| Is Online Subsystem Available (EasySession) | - | Is a subsystem loaded with a valid session interface |
| Get Easy Session Queue Status | `GetQueueStatus` | What the request queue is doing, as a string for status UI and bug reports. Running requests are appended, e.g. `Idle; Matchmaking (Searching, 12s)` |
| Get Easy Session Settings | `GetSessionSettings` | The settings the session advertises, so Update can change one field. Works for every member; the password and its friends exception are only filled on the host |
| Get Easy Session Join Code | `GetSessionJoinCode` | The join code the session advertises, or empty. Every player in the session can read and share it |
| Is In Easy Party | `IsInParty` | Is the local player in a party |
| Is Easy Party Leader | `IsPartyLeader` | Does the local player lead the party. False outside a party |
| Get Easy Party Members | `GetPartyMembers` | Every member, the leader included, with leader, local-player and ready flags |
| Get Easy Party Settings | `GetPartySettings` | Max Members and Privacy of the party, on the leader and on every member. Default settings outside a party |
| Get Easy Party Join Code | `GetPartyJoinCode` | The code a Join Code party advertises, or empty |
| Is Easy Party Restoring | `IsRestoringParty` | Is the party of the last match being created again or looked for. Show a waiting message while it is true |

### 2.2 On the subsystem (`UEasySessionSubsystem`)

Call these on `Get Easy Session Subsystem`. The name is the same in Blueprint and C++, so
there is no C++ column.

| Node | Answers |
|---|---|
| Get Active Easy Matchmaking Policy | The scoring policy of the running matchmaking. Progress is broadcast on the subsystem's On Matchmaking events, not on the policy |

> **Which session are these about?** The Session nodes answer about the game session - the one players find,
> join and play in. There is exactly one per process (see Limitations in the README),
> so nothing here takes a session argument. The party lives alongside it and has its
> own Party nodes, so a player in a party changes nothing here: `Is In Easy Session`
> keeps answering about the game session only.
>
> Some queries here are not about a session at all: `Is Easy Session Busy` and `Get Easy
> Session Queue Status` describe the request queue, and `Is Easy Matchmaking Running`,
> `Get Easy Matchmaking State`, `Get Online Subsystem Name (EasySession)` and
> `Is Online Subsystem Available (EasySession)` describe the process. Those keep their meaning whatever sessions exist.

### 2.3 UI text helpers (`UEasySessionUIStatics`)

Pure functions for the text a session UI shows. None of them touch session state, so a menu needs no string assembly of its own.

| Node | C++ | Returns |
|---|---|---|
| Get Result Message | `GetResultMessage` | A player facing sentence for a result, e.g. "The session is full". Success reads "Done" |
| Get Session State Message | `GetSessionStateMessage` | A session state as a player facing name, e.g. "In Match". Pending and Ended both read "Waiting" |
| Get Activity Message | `GetActivityMessage` | "Creating the session..." style line for a Get Easy Session Activity value. None gives empty text, so a status line clears itself with it |
| Format Matchmaking Status | `FormatMatchmakingStatus` | "Searching... 12s" style line from a matchmaking state and elapsed seconds. Idle reads "Ready" |
| Format Session Slots | `FormatSessionSlots` | "1/4   ping 32ms" for a search result |
| Get Region Display Name | `GetRegionDisplayName` | "North America East" for a region |
| Get Region Options | `GetRegionOptions` | Every region's display name in enum order, for a combo box |
| Region From Index | `RegionFromIndex` | The region behind a combo box index. Out of range gives Any |

## 3. Action Blueprint nodes

Not async - these return right away. They change state and have execution pins.

What they return means the request was accepted, not that it finished. `Cancel Easy
Matchmaking` ends a search at once but waits for a join or host already running to come
back so it can undo it, and `Server Travel Easy Session` returns before the new map has loaded.

### 3.1 Standalone nodes (`UEasySessionStatics`)

Same convention as 2.1: the C++ column is the subsystem method, not the static's name.

| Node | C++ | Does |
|---|---|---|
| Consume Pending Easy Disconnect Info | `ConsumePendingDisconnectInfo` | Reads the disconnect reason and clears it. Survives map travel, so the menu can show it |
| Cancel Easy Matchmaking | `CancelMatchmaking` | Ends a Matchmaking run with `Canceled`. A search stops at once; a join or host in flight finishes first and is undone |
| Cancel Easy Friend Search | `CancelFriendSearch` | Ends Find Easy Friend Sessions with `Canceled`. The running search for a friend's session is ignored |
| Send Easy Session Invite To Friend | `SendSessionInviteToFriend` | Platform invite, returns a result |
| Show Easy Invite UI | `ShowInviteUI` | Platform invite overlay, returns a result |
| Show Easy Profile UI | `ShowProfileUI` | Profile overlay for a friend, returns a result |
| Show Easy Profile UI For Player | `ShowProfileUIForPlayer` | Profile overlay for someone in the session, returns a result |
| Show Easy Profile UI For Party Member | `ShowProfileUIForPartyMember` | Profile overlay for a member of the party, returns a result |
| Send Easy Party Invite To Friend | `SendPartyInviteToFriend` | Platform invite to the party. Any member, returns a result |
| Show Easy Party Invite UI | `ShowPartyInviteUI` | Platform invite overlay for the party. Any member, returns a result |
| Set Easy Session Ready | `SetSessionReady` | Changes whether the local player is ready, which every player sees in Get Easy Session Player Infos. Unset again in every map the session travels to |
| Set Easy Party Ready | `SetPartyReady` | Changes whether the local player is ready, which every member sees in Get Easy Party Members |
| Kick Easy Session Player | `KickPlayer` | Removes a player and keeps them out until the session is destroyed. They read `Kicked` with your reason in the menu. Session authority only |
| Kick Easy Party Member | `KickPartyMember` | Removes a member and keeps them out of this party. They receive `OnPartyLeft` with `Kicked`. Party leader only |
| Server Travel Easy Session | `ServerTravel` | Moves the whole session to a new map. Session authority only |
| Destroy Easy Session For Everyone | `DestroySessionForEveryone` | Ends the session and sends every client back to the menu with a reason. Session authority only |

The invite, profile, ready and kick nodes return an `EEasySessionResult`. The invite and profile nodes need a platform service, so on NULL/LAN they return `NotSupportedByService`.

`Server Travel Easy Session` and `Destroy Easy Session For Everyone` check session authority themselves: a client that calls either one changes
nothing and gets a warning in the log, so show the button only when
`Is Easy Session Authority` is true rather than relying on the call being harmless.

## 4. Events

Assignable on the subsystem. They report states and matchmaking runs, not single requests:
the result of Create, Find, Join, Update, Start, End or Destroy goes to the node's output pins
(or the C++ completion delegate) and nowhere else.

| Event | Payload | Fires when |
|---|---|---|
| `OnSessionSettingsChanged` | - | The advertised settings changed: on the host when it updates them, on a client when the host's values arrive. The regular getters already return the new values - refresh the UI from them |
| `OnSessionStateChanged` | `OldState`, `NewState` (`EEasySessionState`) | The session's lifecycle state changed, on the host and on every client. Creating a session and leaving one count, from and to `NoSession`. A client sees the host starting or ending the match here |
| `OnMatchmakingStarted` | - | A Matchmaking run was accepted and its policy registered. Always the first event of a run |
| `OnMatchmakingStateChanged` | `OldState`, `NewState` | The Matchmaking state moved (`Searching`, `Joining`, `Hosting`, `Canceling`, `Complete`) |
| `OnMatchmakingUpdated` | `State`, `ElapsedSeconds` | Every Matchmaking state change plus once a second while it runs - drives elapsed-time labels |
| `OnMatchmakingComplete` | `Result`, `ErrorMessage` | A Matchmaking run finished, whether it joined, ended up hosting, or was canceled (`Result` = `Canceled`). Ask `Is Easy Session Host` which |
| `OnSessionFailure` | `Reason` (String) | Something failed outside any node's result: the connection dropped, a travel or listen server EasySession started failed (e.g. a wrong Initial Map Name), or the join of an accepted invite failed. Use `Reason` for a status line or the log. A client that lost its session is sent back to the menu, where `Consume Pending Easy Disconnect Info` has the reason to show the player |
| `OnBusyChanged` | `bBusy` | Is Easy Session Busy flipped. Bind once and enable or disable session buttons from the flag instead of polling every tick. On the true edge, Get Easy Session Activity says which activity began |
| `OnSessionInviteAccepted` | `Session` (`FEasySessionSearchResult`) | The player accepted an invite in the platform overlay. With Auto Join Accepted Invites on, the join follows on its own and cancels a running Matchmaking - unless this player is already in a session, which needs `bAcceptInvitesWhileInSession`. `Session.bIsParty` is true for a party invite, which is joined with Join Easy Party and never during a game session ([guide](Guide-Party.en.md#invites)) |
| `OnSessionPlayersChanged` | - | A player joined or left the session, or changed whether they are ready, on the host and on every client. Get Easy Session Player Infos already returns the new list |
| `OnPartyMembersChanged` | - | A member joined or left the party, or changed whether they are ready, on the leader and on every member. Get Easy Party Members already returns the new list |
| `OnPartyLeft` | `Reason` (`EEasyPartyLeaveReason`), `ReasonText` (Text) | The local player is no longer in the party: they left, were kicked, the leader left, the connection was lost, or the party entered a game session |

`Result` and `ErrorMessage` are the same values the node's own pins would have given you.

## 5. Structs

### 5.1 FEasySessionSettings
`SessionDisplayName` (String), `MaxPlayers` (int), `bShouldAdvertise`, `bHidden`, `Password` (String), `bFriendsBypassPassword`, `bAllowJoinInProgress`, `bAllowInvites`, `Region` (`EEasySessionRegion`), `bUseJoinCode`, `CustomSettings` (Map String->String)

What a session advertises about itself. `Update Easy Session` takes exactly this struct,
so every field here is one a live session can change. On a client, `Get Easy Session Settings` leaves `Password` empty and `bFriendsBypassPassword` false, because only the host holds them.

### 5.2 FEasySessionHostParams *(FEasySessionSettings plus)*
`InitialMapName` (String, required), `bIsLANMatch`, `bUsePresence`, `AdditionalTravelOptions` (String)

Hosting is the settings above plus how to bring the server up. These added fields are
read once, while the session is created, which is why Update cannot change them.
The rule for the two names: Settings are what a session or a party advertises and can be read back, and HostParams adds what starting a server needs.
A party starts no server, so it has Settings only ([5.10](#510-feasypartysettings)).

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
`MaxResults` (int), `bLANQuery`, `MinOpenSlots` (int), `MaxPingMs` (int), `RequiredCustomSettings` (Map String->String), `Region` (`EEasySessionRegion`), `bIncludeInProgressSessions`, `JoinCode` (String), `SearchMode` (`EEasySessionSearchMode`), `SearchTargetId` (Unique Net Id), `OwnerId` (Unique Net Id)

Four of these name one specific session instead of describing what to look for. `JoinCode` and `OwnerId` are filters over a normal search, so they combine with everything above. `SearchMode` picks a different call to the service - By Friend - and `SearchTargetId` says which friend; Max Results and LAN Query are then ignored while the filters still apply. By Friend needs a service with friends such as Steam, and fails with `NotSupportedByService` on NULL/LAN. A search naming one session also sees hidden ones.

### 5.4 FEasySessionSearchResult *(read-only)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `bPasswordProtected`, `Region`, `bMatchInProgress`, `bIsParty`, `CustomSettings`

This struct connects the two nodes: `Find Easy Sessions` returns them and
`Join Easy Session` takes one back. Keep the whole struct - a server-browser row should
store it, not just the name it displays. Find Easy Parties returns the same struct with `bIsParty` set, and Join Easy Party takes it.

### 5.5 FEasyFriendSession
`Friend` (`FEasySessionFriend`), `bHasSession`, `Session` (`FEasySessionSearchResult`)

One entry per friend from `Find Easy Friend Sessions`. `Session` is only valid while
`bHasSession` is true, and joins like any other search result.

`bPasswordProtected` is how you decide whether to prompt before `Join Easy Session`.

### 5.6 FEasyMatchmakingParams
`Search` (SearchParams), `Host` (HostParams - read only while `bAllowHostFallback` is on), `bAllowHostFallback`, `JoinPassword` (String), `MaxSearchPasses` (int), `DelayBetweenPassesSeconds` (float)

### 5.7 FEasySessionPlayerInfo *(read-only)*
`PlayerName`, `bIsLocalPlayer`, `bIsHost` (always false on dedicated servers), `bIsReady` (unset again in every map the session travels to), `PlayerId` (the online service id - names can repeat, this cannot)

### 5.8 FEasySessionFriend *(read-only)*
`DisplayName`, `bIsOnline`, `bIsPlayingThisGame`, `NativeId` (Unique Net Id)

Returned by `Read Easy Friends`; pass one back to the invite and profile functions.
`NativeId` goes into a search's `SearchTargetId`, with `SearchMode` set to By Friend, to find the session that friend is in.

### 5.9 FEasyDisconnectInfo *(read-only)*
`Reason` (`EEasyDisconnectReason`), `ReasonText` (Text)

### 5.10 FEasyPartySettings
`MaxMembers` (int, at least 2, the leader included), `Privacy` (`EEasyPartyPrivacy`)

What `Create Easy Party` takes and `Get Easy Party Settings` returns. A party has no map, password, region or custom data, so these two fields are all of it ([guide](Guide-Party.en.md#who-may-join)).

### 5.11 FEasyPartyMemberInfo *(read-only)*
`PlayerName`, `bIsLocalPlayer`, `bIsLeader`, `bIsReady`, `PlayerId`

Returned by `Get Easy Party Members`; pass one to `Kick Easy Party Member`.

## 6. Enums

### 6.1 EEasySessionResult

Every node's `Result` pin. The ones worth branching on are marked.

| Value | Means |
|---|---|
| `Success` | It worked |
| **`SessionAlreadyExists`** | Create or Matchmaking: you are already in a session, so leave it first. Join: you are already in the session you asked to join, or you host a match in progress ([rules](#1-async-blueprint-nodes)). Create or Join Easy Party: you are already in a party or in a game session |
| **`NoSessionExists`** | There is no session to act on |
| **`NoSessionsFound`** | The search ran fine and found nothing. Not an error - offer to host |
| **`JoinSessionFull`** | The session is full - refused by the host before the travel, or by the online service after it. The host also counts the reservations of players still loading, so a search result with a free slot can end here |
| **`JoinSessionDoesNotExist`** | The session was gone by the time you joined. Search again |
| **`WrongPassword`** | The host refused the join: the password did not match. Let the player retype it |
| **`JoinRefused`** | The host refused the join for another reason, e.g. the match no longer takes players, or the reservation beacon of a password session could not be reached. `ErrorMessage` is safe to show |
| **`ResolveFailure`** | Joined, but the host address does not work - usually a host that never became a listen server ([FAQ](FAQ.en.md)) |
| **`RequiresSessionAuthority`** | Only the game that created the session may do this. Show the button only when `Is Easy Session Authority` is true |
| **`Canceled`** | `Cancel Easy Matchmaking` stopped a Matchmaking run |
| **`NotSupportedByService`** | The online service in use does not offer that feature. Friends and invites need Steam; NULL/LAN has none. Not a configuration problem - hide the button on such a service |
| **`InParty`** | A party member called Create, Join or Matchmaking. The leader decides where the party goes, so disable those buttons for members, or call Leave Easy Party to play alone |
| **`RequiresPartyLeader`** | Only the party leader may do this. Show the button only when `Is Easy Party Leader` is true |
| `NoOnlineSubsystem` | No subsystem is configured. Check `DefaultEngine.ini` |
| `InvalidParams` | A parameter cannot work, e.g. Matchmaking with no fallback Initial Map Name |
| `MatchmakingAlreadyInProgress` | A Matchmaking is already running |
| `CreateFailure`, `SearchFailure`, `JoinFailure`, `DestroyFailure`, `UpdateFailure`, `StateChangeFailure` | The online service refused that specific call. `ErrorMessage` carries what it said |
| `UnknownFailure` | Nothing more specific was available |

### 6.2 EEasySessionState

`NoSession`, `Creating`, `Pending`, `Starting`, `InProgress`, `Ending`, `Ended`, `Destroying`

`Pending` is a session waiting to start; `Ended` is a finished match that `Start Easy Session` can play again. The `-ing` values are the moments a request is still running.

### 6.3 EEasyDisconnectReason

Read with `Consume Pending Easy Disconnect Info`. Branch on `Reason`, show `ReasonText`.

| Value | Means |
|---|---|
| `None` | Nothing was recorded |
| `ConnectionLost` | The link died - the host quit, crashed, or the network dropped. A host that died while you were joining lands here too |
| `HostDestroyedSession` | The host deliberately sent everyone back, via `Destroy Easy Session For Everyone` |
| `TravelFailure` | The session's map failed to load |
| `Rejected` | The host refused the connection when it arrived, and said why: a wrong password, a match no longer taking players. `ReasonText` is the host's own sentence, safe to show |
| `Kicked` | The host removed this player with `Kick Easy Session Player`, which keeps them out until the session is destroyed. `ReasonText` is the host's reason |

### 6.4 EEasyMatchmakingState

`Idle`, `Searching`, `Joining`, `Hosting`, `Canceling`, `Complete` - the phases of one Matchmaking run, reported through `OnMatchmakingStateChanged`. `Canceling` lasts while a join or host that was in flight at the cancel finishes.

### 6.5 EEasySessionRegion

`Any` plus nine coarse world regions, from `NorthAmericaEast` to `Oceania`, cut so that one region means playable latency. A game that needs its own split leaves this at `Any` and filters with a `CustomSettings` key instead ([guide](Guide-Sessions.en.md#regions)).

### 6.6 EEasySessionSearchMode

`Default` searches for the sessions the filters describe. `ByFriend` asks the service for the session one friend is in instead, reading `SearchTargetId` for which friend.

### 6.7 EEasySessionActivity

`None`, `Creating`, `Searching`, `Joining`, `Leaving`, `Updating`, `Starting`, `Ending`, `Matchmaking`, `Traveling` - what the plugin is doing right now, from `Get Easy Session Activity`. It names the activity whoever started it, so a status widget can narrate an invite join or a disconnect recovery the menu never asked for. `Get Activity Message` turns it into a sentence.

### 6.8 EEasyPartyPrivacy

| Value | Find Easy Parties | Players join through |
|---|---|---|
| `InviteOnly` (default) | Hidden | An invite |
| `JoinCode` | Found only with the code | The code, or an invite |
| `Public` | Listed | A search, or an invite |

No privacy refuses a player who reaches the party. The leader refuses only a kicked player, and anyone while the party is full.

### 6.9 EEasyPartyLeaveReason

The `Reason` of `OnPartyLeft`.

| Value | Means |
|---|---|
| `Left` | The local player called Leave Easy Party |
| `Kicked` | The leader removed the local player. `ReasonText` is the leader's reason |
| `LeaderLeft` | The leader left, which ends the party |
| `ConnectionLost` | The connection to the leader was lost, or the leader did not come back after a match |
| `MovedToGameSession` | The party entered a game session, which closes it. The normal end of a party that plays a match, not an error |

## 7. UEasyMatchmakingPolicy

Decides which session a Matchmaking run joins first. The run itself - search passes, joins, the host fallback - belongs to the subsystem.

Make a subclass in Blueprint or C++ and override **`ScoreSession(Session) -> float`** (higher = joined first) for custom pick-a-session criteria. Editable defaults: `PingBucketsMs` (default `[50, 100, 150]`), `TopCandidatesToShuffle` (default 3). The subsystem creates one policy object for each run from the class you pass. Progress is on the subsystem: `Get Easy Matchmaking State` and the On Matchmaking events.

## 8. UEasySessionConfig (Project Settings -> Plugins -> EasySession)

| Setting | Default | Effect |
|---|---|---|
| `bAutoReturnToMenuOnDisconnect` | true | On disconnect or a failed travel, browse to the project's **Game Default Map**, keeping the reason for that map to read. The lost session is destroyed either way; off only leaves the player in the map they are in |
| `bAutoJoinAcceptedInvites` | true | Accepting a platform invite joins that session immediately. Off gives you only `OnSessionInviteAccepted` |
| `bRestorePartyAfterMatch` | true | Gets the party back in the first map without a game session after a match. The leader creates it again and every member looks for it ([guide](Guide-Party.en.md#after-the-match)) |
| `PartyRestoreWaitSeconds` | 120 | How long a member looks for the leader's party after a match |
| `PartyReconnectSeconds` | 15 | How long a member keeps connecting to the leader after the connection closed without a reason, for example while the leader's next map loads |
| `bAcceptInvitesWhileInSession` | false | An automatically accepted invite may destroy the session this player is in and join the invited one. Off by default so one click in the overlay cannot end a running match; `OnSessionInviteAccepted` still fires, so you can ask first and call `Join Easy Session` yourself, which leaves the current session once the new host approved |

## 9. C++ notes

The same requests are callable natively with delegate callbacks: `CreateSession`,
`FindSessions`, `JoinSession`, `DestroySession`, `UpdateSession`,
`StartMatchmaking`, `CreateParty`, `FindParties`, `JoinParty`, `LeaveParty`. Blueprint and C++ take the same code path.

Every session call is one request on a queue that runs them one at a time. Matchmaking
and `FindFriendSessions` run their searches, joins and host as sub-requests inside
their own request, so a request from the game waits until the whole run ends instead of
slipping in between two of its sub-requests. `ReadFriends` is a request as well, so
reading the friends list turns `IsBusy` on while it runs. Every running or waiting
request counts toward `IsBusy`, `GetActivity` and `GetQueueStatus`.

`OnModifyServerTravelURL` and `OnModifyClientTravelURL` are C++ only delegates on the
subsystem. They give you the travel URL just before travel so you can append your own
options. Bind once at startup: the hook fires before that request's completion callback,
so a handler bound inside the callback misses its own travel. For anything expressible as
a static string, prefer `AdditionalTravelOptions`.

## 10. Console commands *(development builds only)*

Sessions: `EasySession.Host <Map>`, `EasySession.Find`, `EasySession.Join [Index] [Password]`, `EasySession.Matchmaking [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`

Players: `EasySession.Players` (numbered for Kick), `EasySession.Kick <Index> [Reason]`, `EasySession.Ready <0|1>`

Party: `EasySession.CreateParty [MaxMembers] [invite|code|public]`, `EasySession.LeaveParty`, `EasySession.Party` (members and join code), `EasySession.PartyReady <0|1>`, `EasySession.FindParties [Code]`, `EasySession.JoinParty [Index]`, `EasySession.KickParty <Index> [Reason]`

Friends and diagnostics: `EasySession.Friends`, `EasySession.InviteUI`, `EasySession.PartyInviteUI`, `EasySession.Diagnose`
