# Guide - Sessions

*[한국어](Guide-Sessions.ko.md)*

Everything about creating, finding, joining, starting a match and leaving sessions. Every async node here shares the same shape: inputs on the left, `OnSuccess` / `OnFailure` exec pins with a `Result` enum and an `ErrorMessage` string.

All requests are **queued and executed one at a time** - you can call them in any order, even in the same frame, and they will never corrupt the online service.

Players who stay together between matches, such as friends in the main menu, are a party. The [Party guide](Guide-Party.en.md) covers it, including how a party leader brings the party into a session.

## Create Session

`Create Easy Session` with `FEasySessionHostParams`. The table follows the order the pins appear in on the Make node.

| Field | Default | Notes |
|---|---|---|
| Session Display Name | "My Session" | Shown in search results |
| Initial Map Name | (empty) | Required. The host travels there with `?listen` once the session is created, which makes this game the server. Empty fails the create with `InvalidParams`. The map loads from scratch, so players who were connected before the session existed are disconnected and have to join it. The session does not advertise its map |
| Max Players | 4 | Public connections. The engine's own login cap ("Server full") follows this value |
| Is LAN Match | false | Forced on automatically under the NULL subsystem |
| Should Advertise | true | Off does not advertise the session at all |
| Hidden | false | Advertised, but left out of `Find Easy Sessions` results - reachable through invites only |
| Password | (empty) | See [password protected sessions](#password-protected-sessions) below |
| Friends Bypass Password | true | Friends join without the password, same section |
| Additional Travel Options | (empty) | Option string appended to the host's travel URL as written |
| Allow Join In Progress | true | Leave on for Steam, which closes the lobby at the first join when this is off ([FAQ](FAQ.en.md)) |
| Allow Invites / Use Presence | true | Use Presence is ignored on LAN |
| Custom Settings | (empty) | Advertised key-value data, see below |

### Custom session data

`Custom Settings` is a string map advertised with the session. Use it for game mode, region, difficulty - anything searchers should filter or display:

```
CustomSettings = { "GameMode": "CTF", "Region": "AS" }
```

Searchers read it back from each `FEasySessionSearchResult.CustomSettings`, and can filter them out as part of the search with `Required Custom Settings` (exact match on every pair).

The map you hand to `Update Easy Session` replaces the session's custom data: a key you leave out is removed from the session too. Change the map you got from `Get Easy Session Settings` rather than building a new one.

## Find Sessions

`Find Easy Sessions` with `FEasySessionSearchParams`:

| Field | Default | Notes |
|---|---|---|
| Max Results | 50 | How many results to take at most |
| LAN Query | false | Forced on automatically under NULL |
| Min Open Slots | 0 | Only sessions with at least this many free slots |
| Max Ping Ms | 0 | 0 = no limit |
| Required Custom Settings | (empty) | Exact-match filters against advertised custom data |
| Region | Any | Only sessions advertising this region ([regions](#regions)) |
| Include In Progress Sessions | true | Turn off to hide sessions whose match already started. Sessions refusing join-in-progress never appear either way |

`Find Easy Friend Sessions` is the friends-flavored search: it reads the friends list
and finds the session each friend playing this game is in, one entry per friend. The
entries come back ordered for a list: friends in a joinable session first, then the rest
by presence (playing this game, online, offline) and name. A friend session joins like
any other search result. `Cancel Easy Friend Search` stops a friend session search a menu no longer needs,
and `Is Easy Session Busy` tells a Refresh button to wait, like every other session call. Like the friends list
itself, it is not supported on NULL/LAN.

Results arrive on `OnSuccess`.

Each `FEasySessionSearchResult` exposes: display name, host name, ping, max players, open slots, dedicated flag, password flag, region, in-progress flag, party flag, and the custom settings map.

## Join Session

`Join Easy Session` takes a search result. On success it resolves the host address and client-travels there. Joining always connects to the host, so the joining player leaves whatever map they were on - even a map with the same name as the host's.

EasySession validates the host address **before** reporting success - if the host is not actually reachable (see [FAQ: port 0](FAQ.en.md)), you get an immediate `ResolveFailure` with an explanation instead of a 20-second connection timeout, and the half-joined session is cleaned up so you can retry right away.

### Asking the host first

Before the travel, the joining player asks the host for a reservation. The host checks that the match still takes players, that the session is not full, and the password.

A match that no longer takes players fails the node with `JoinRefused`, a full session with `JoinSessionFull`, and a wrong password with `WrongPassword`. In all three cases no map has started loading, `ErrorMessage` carries the host's own sentence, and the player can retry immediately:

```
Join Easy Session
  OnFailure -> Result == WrongPassword ?
                 true  -> reopen the password prompt, show ErrorMessage
                 false -> show ErrorMessage
```

The example's password popup does exactly this - it stays available for a retype and shows the reason under the input (`WBP_JoinPasswordPopup`).

### Reservations

An approved player still spends a few seconds loading the map, and the host holds a reservation for them for that whole time, so two players are never approved for the last free slot. The reservation stays until that player arrives, and the host removes it when they do not arrive within 45 seconds. A player who leaves the session loses their reservation as they log out, so the next player, or the same player again, can join.

The host holds a reservation for itself, because Max Players counts the host.

Search results count only the players already in the session, not the reservations of players still loading. A search result can therefore show a free slot that a joining player has already reserved, and joining it fails with `JoinSessionFull`. Matchmaking moves on to its next candidate in that case.

A map change sends everyone traveling again, the players already in the session included, and the host keeps every reservation for the new map. The host waits 45 seconds for each player to arrive, which is the engine's own `TravelSessionTimeoutSecs`. Raise it in `DefaultEngine.ini` when a map takes longer than that to load. This example raises it to 90 seconds:

```ini
[/Script/OnlineSubsystemUtils.PartyBeaconHost]
TravelSessionTimeoutSecs=90
```

### When the host cannot be asked

The reservation request travels over a beacon, a second lightweight connection to the host. If the project already runs its own beacon host, the reservation beacon registers on it instead of opening a second port. When that beacon cannot be reached - the port is blocked, another instance on this machine took it first, or the project removed the engine's `BeaconNetDriver` definition ([Steam setup](Setup-Steam.en.md) shows the line that restores it, and `EasySession.Diagnose` checks for it) - the join of an open session proceeds directly, and the host checks the player as they arrive instead. A password-protected session fails the node with `JoinRefused`, because only the beacon carries the password.

The beacon uses the engine's own port, 15000 by default. Move it with `ListenPort` under `[/Script/OnlineSubsystemUtils.OnlineBeaconHost]` in `DefaultEngine.ini`, or with `-BeaconPort=` on the command line. Only the host binds that port; joining players connect from a port the operating system picks, so two instances on one machine only collide when both of them host. Give each host its own `-BeaconPort=` then. When the port is already taken, the engine binds the next free one while the session keeps advertising the configured one, and the host logs a warning naming both ports.

That late refusal is a disconnect, which sends the player back to the menu level (`bAutoReturnToMenuOnDisconnect`, on by default). Read it there:

```
Event Construct
  Has Pending Easy Disconnect Info ?
    Consume Pending Easy Disconnect Info  ->  Break Easy Disconnect Info
                                             Reason      == Rejected
                                             Reason Text == "The match is already in progress."
```

The information survives the travel precisely so the menu can show it. Check `Reason` rather than matching the text. There are five of them:

| Reason | When |
|---|---|
| `ConnectionLost` | The host quit, crashed, or the network dropped - a host that died while you were joining lands here too |
| `HostDestroyedSession` | The host sent everyone out with `Destroy Easy Session For Everyone` |
| `TravelFailure` | Traveling to the session's map failed |
| `Rejected` | The host refused the connection when it arrived - a closed match, or a password session reached without the reservation beacon. `Reason Text` says which |
| `Kicked` | The host removed this player with `Kick Easy Session Player`. `Reason Text` is the host's reason |

Keep this handler even with the beacon working: it is the safety net for every way a connection can end.

### Bringing the other players

The host of a session whose match has not started can take everyone along to another session, for example from a lobby session into a match:

| Host calls | What happens |
|---|---|
| `Join Easy Session` | The host asks the new host for room for every player in its session. Without room the join fails with `JoinSessionFull`, and everyone stays |
| `Start Easy Matchmaking` | The search only considers sessions with room for everyone. The host fallback is skipped, because the host is still in its own session, so a run that finds nothing completes with `NoSessionsFound` and everyone stays |

The other players are told to follow, and each one keeps searching for the new session for up to 30 seconds. They need no password, because the host's reservation already holds them. The host waits up to 10 seconds for them to leave and then leaves too, so its session is not destroyed under them.

Only a host brings players. A client that joins another session leaves alone, once the new host approved the join. The host of a match in progress is refused with `SessionAlreadyExists`, because leaving would end the match for every player.

The players a host brings must be reachable over the reservation beacon. When the new host cannot be asked, the join fails with `JoinRefused` and everyone stays where they are.

## Password protected sessions

### Locking a session

Set `Password` on the host params. That is the whole setup.

```
Create Easy Session
  Host Params > Password = "1234"
```

The password itself is never advertised. Only a "password protected" flag goes out with the session, which `Find Easy Sessions` returns as `Password Protected` on each search result - use it to decide whether to prompt.

### Joining a locked session

Pass the player's answer to the `Password` pin on `Join Easy Session`. The host checks it over the reservation beacon before the travel ([Asking the host first](#asking-the-host-first)), and it never goes into the travel URL, so the engine's travel logs never show it. A player the beacon did not approve is refused on arrival, so a direct connect such as the `open` console command cannot enter a password session.

### Friends skip the password

`Friends Bypass Password` defaults to **true**. Platform invites carry no password prompt, so an invited friend would otherwise be turned away by the session they were invited to. The host verifies friendship against the platform friends list, which the joining player cannot fake. This has no effect on NULL/LAN, where there are no friends.

## Start Session / End Session

`Start Easy Session` moves the session to InProgress. With `Allow Join In Progress` off, new players are refused from here until the match ends - except on Steam, which already refused them from the first join onwards ([FAQ](FAQ.en.md)).

`End Easy Session` finishes the match and returns the session to a joinable state.

Both keep the advertised in-progress flag current, which is what the search's
`Include In Progress Sessions` filter and the result's `bMatchInProgress` read.

Both are host only. A client gets `RequiresSessionAuthority`, so show the buttons only when `Is Easy Session Authority` is true.

## Update Session

`Update Easy Session` re-advertises the session from a fresh `FEasySessionSettings`. Host only.

That struct is the answer to "what can I change?" - it holds the display name, max
players, advertise flag, hidden flag, join-in-progress flag, invites flag, region, join
code, the password (and its friends exception) and custom settings, and nothing else.
`Get Easy Session Settings` hands you the current ones to edit.

The hosting-only fields live one level up, in `FEasySessionHostParams`, and never reach
Update:

| Field | Why |
|---|---|
| Initial Map Name | Only read when the session is created. Move maps with `Server Travel Easy Session` instead |
| Is LAN Match | Whether the session lives on the LAN or on the online service is decided at create time |
| Use Presence | Steam refuses it on a live session - it logs `Can't change presence settings on existing session` and keeps the old value |
| Additional Travel Options | Used once for the travel at create time, never read again |

> On LAN (NULL), changing the advertise flag leaves the LAN beacon as it was: the engine's `FOnlineSessionNull::UpdateSession` swaps the settings without recomputing the beacon.

Joined players receive the update automatically. The plugin replicates the member-visible
settings - display name, max players, the flags, region, join code and custom settings -
to every client, patches their local session copy so the regular getters return the new
values, and fires `OnSessionSettingsChanged` for the UI to refresh on. The host fires the
same event for its own UI. Only the password and its friends exception stay on the host.

`OnSessionStateChanged` covers the other half: it fires on the host and on every client
whenever the session's state changes, so a client sees the host start or end the match
without polling.

## Destroy Session

`Destroy Easy Session` removes this game's named session - and only that. The player
stays on their current map, which is what a host between matches wants. A client
leaving the session wants `Leave Easy Session` instead: it destroys the named session and
then returns to the menu map. A host pressing Leave closes the session for everyone, with
"The host has left the game." as the reason clients read. After either one you can
immediately host or join again.

When the host calls it, clients see the connection drop and return to the menu with `ConnectionLost`. To tell them why it ended, pass a reason to `Destroy Easy Session For Everyone` instead: it sends that sentence to everyone before taking the session down, and they read it as `HostDestroyedSession`.

## Traveling mid-session

`Server Travel Easy Session` (host only) moves the whole session to a new map, appending `?listen` unless the server is dedicated or you wrote the option yourself. Clients follow automatically. Unlike the other nodes this one is not async - it returns success as a bool right away.

Always change maps with this node: it stops the reservation beacon before the map changes and keeps the reservations for the new map, and after a plain `ServerTravel` the new map cannot start its own beacon because the port is still held.

If the map fails to load (a typo, a map missing from the cook), the session and its players stay exactly where they were. The failure arrives on `OnSessionFailure` - call again with the right map name.

## Regions

Hosting advertises `Region` (`Any` by default) and searching filters by it: set the same
region in both places and players only see sessions they can play in. `Any` on the search
lists every region; `Any` on the host matches only searches that do not filter. The
regions are coarse on purpose - one region means playable latency. A game that needs its
own split (country servers, a single home region) leaves the field at `Any` and filters
with a `Custom Settings` key through `Required Custom Settings` instead.

## Join codes

Turn on `Use Join Code` when hosting and the session advertises a generated six character
code, readable with `Get Easy Session Join Code` by everyone in the session. Joining is a
search away: `Find Easy Sessions` with `Join Code` set returns that one session, hidden or
not - show it, then pass it to `Join Easy Session`. Matchmaking with the same `Join
Code` does both in one call, retrying while the session comes up. `Hidden` plus a code is
a friends-only session: no browser lists it, anyone with the code walks in.

The code identifies the session but does not protect it. Protection is `Password`, and the
two combine: the code finds the session, the password still gates the door.

## Players: ready and kick

`Get Easy Session Player Infos` lists everyone in the session with their name, whether they are the local player or the host, whether they are ready, and their player id. `OnSessionPlayersChanged` fires on the host and on every client when a player joins, leaves or changes whether they are ready, so a player list reads it again then.

`Set Easy Session Ready` changes whether the local player is ready. The plugin only shares the value: the game decides what being ready allows, for example enabling the host's Start button. Ready is unset again in every map the session travels to.

`Kick Easy Session Player` removes a player from the session and keeps them out until the session is destroyed. The player travels to the menu, where `Consume Pending Easy Disconnect Info` returns `Kicked` with the host's reason. Session authority only.

## Events and state queries

The result of each request arrives on its node's output pins. For UI that watches the session as a whole,
bind these on the subsystem (`Get Easy Session Subsystem`):

- `OnSessionPlayersChanged` - a player joined, left or changed whether they are ready
- `OnBusyChanged` - a request started or everything finished. `Get Easy Session Activity` names the activity, so a spinner can say what it waits for
- `OnSessionFailure` - something failed outside any node's result: the connection dropped, a travel or listen server EasySession started failed (e.g. a wrong Initial Map Name), or the join of an accepted invite failed. A client that lost its session is sent back to the menu, where `Consume Pending Easy Disconnect Info` has the reason to show the player
- `OnMatchmakingStarted`, `OnMatchmakingStateChanged`, `OnMatchmakingUpdated`, `OnMatchmakingComplete` - a Matchmaking run's progress, from acceptance to the end. Details in the [Matchmaking guide](Guide-Matchmaking.en.md)

Pure state queries are usable anywhere: `Is In Easy Session`, `Is Easy Session Host`, `Is Easy Session Busy` and `Get Easy Session State` for status, `Get Easy Session Display Name`, `Get Easy Session Player Infos`, `Get Easy Session Player Count`, `Get Easy Session Max Players` for contents, and `Get Online Subsystem Name (EasySession)` for the environment. The [API reference](API.en.md) has the full list.

## C++ API

Everything above is a thin wrapper over `UEasySessionSubsystem` - C++ users call the same functions with native delegates:

```cpp
UEasySessionSubsystem* Session = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();
Session->CreateSession(HostParams,
	FEasySessionCompleteDelegate::CreateUObject(this, &UMyClass::OnHosted));
```

Blueprint and C++ take the identical code path, so behavior never diverges between them.
