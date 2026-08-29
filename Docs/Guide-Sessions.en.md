# Guide - Sessions

*[한국어](Guide-Sessions.ko.md)*

Everything about creating, finding, joining, starting a match and leaving sessions. Every async node here shares the same shape: inputs on the left, `OnSuccess` / `OnFailure` exec pins with a `Result` enum and an `ErrorMessage` string.

All operations are **queued and executed one at a time** - you can call them in any order, even in the same frame, and they will never corrupt the online service.

## Create Session

`Create Easy Session` with `FEasySessionHostParams`. The table follows the order the pins appear in on the Make node.

| Field | Default | Notes |
|---|---|---|
| Session Display Name | "My Session" | Shown in search results |
| Map Name | (empty) | Travels there with `?listen`. Empty = start listening on the current map |
| Host Mode | Listen Server | Or Dedicated Server - code path present, not validated in 1.0 |
| Max Players | 4 | Public connections. The engine's own login cap ("Server full") follows this value |
| Is LAN Match | false | Forced on automatically under the NULL subsystem |
| Start Listening | true | Off still advertises the session, but nothing is there to connect to. Turn it off only when you open the listen server yourself |
| Should Advertise | true | Off does not advertise the session at all |
| Hidden | false | Advertised, but left out of `Find Easy Sessions` results - reachable through invites only |
| Password | (empty) | See [password protected sessions](#password-protected-sessions) below |
| Friends Bypass Password | true | Friends join without the password, same section |
| Additional Travel Options | (empty) | Option string appended to the host's travel URL as written |
| Allow Join In Progress | true | Leave on for Steam, which closes the lobby at the first join when this is off ([FAQ](FAQ.en.md)) |
| Allow Invites / Use Presence | true | These settings are ignored on LAN and dedicated servers |
| Custom Settings | (empty) | Advertised key-value data, see below |

### Custom session data

`Custom Settings` is a string map advertised with the session. Use it for game mode, region, difficulty - anything searchers should filter or display:

```
CustomSettings = { "GameMode": "CTF", "Region": "AS" }
```

Searchers read it back from each `FEasySessionSearchResult.CustomSettings`, and can filter them out as part of the search with `Required Custom Settings` (exact match on every pair).

## Find Sessions

`Find Easy Sessions` with `FEasySessionSearchParams`:

| Field | Default | Notes |
|---|---|---|
| Max Results | 50 | How many results to take at most |
| LAN Query | false | Forced on automatically under NULL |
| Timeout Seconds | 15 | How long to wait for results before giving up |
| Min Open Slots | 0 | Only sessions with at least this many free slots |
| Max Ping Ms | 0 | 0 = no limit |
| Required Custom Settings | (empty) | Exact-match filters against advertised custom data |

Results arrive on `OnSuccess` and are also cached - `Get Last Easy Search Results` returns them anywhere, anytime (useful for server browser UIs).

Each `FEasySessionSearchResult` exposes: display name, map name, host name, ping, max players, open slots, dedicated flag, password flag, hidden flag, and the custom settings map.

## Join Session

`Join Easy Session` takes a search result. On success it resolves the host address and client-travels there. Joining always connects to the host, so the joining player leaves whatever map they were on - even a map with the same name as the host's.

EasySession validates the host address **before** reporting success - if the host is not actually reachable (see [FAQ: port 0](FAQ.en.md)), you get an immediate `ResolveFailure` with an explanation instead of a 20-second connection timeout, and the half-joined session is cleaned up so you can retry right away.

## Password protected sessions

### Locking a session

Set `Password` on the host params. That is the whole setup.

```
Create Easy Session
  Host Params > Password = "1234"
```

The password itself is never advertised. Only a "password protected" flag goes out with
the session, which `Find Easy Sessions` returns as `Password Protected` on each search
result - use it to decide whether to prompt.

### Joining a locked session

Pass the player's answer to the `Password` pin on `Join Easy Session`. The plugin appends
it to the travel URL for the travels it performs.

### Reading the result

The host is asked for approval before anything else happens. A wrong password fails the
node with `WrongPassword`, a full room with `JoinSessionFull`, and a match that no longer
takes players with `JoinRefused` -
in both cases no map has started loading, `ErrorMessage` carries the host's own sentence,
and the player can retry immediately:

```
Join Easy Session
  OnFailure -> Result == WrongPassword ?
                 true  -> reopen the password prompt, show ErrorMessage
                 false -> show ErrorMessage
```

The example's password popup does exactly this - it stays available for a retype and
shows the reason under the input (`WBP_JoinPasswordPopup`).

### When the host cannot be asked

Approval travels over a beacon, a second lightweight connection to the host. If the
project already runs its own beacon host, approval registers on it instead of opening a
second port. When that
beacon cannot be reached - the port is blocked, another instance on this machine took it
first, or the project removed the engine's
`BeaconNetDriver` definition ([Steam setup](Setup-Steam.en.md) shows the line that restores
it, and `EasySession.Diagnose` checks for it) - the join proceeds directly and the host
refuses the connection as it arrives instead.

That late refusal is a disconnect, which sends the player back to the menu level
(`bAutoReturnToMenuOnDisconnect`, on by default). Read it there:

```
Event Construct
  Has Pending Easy Disconnect Info ?
    Consume Last Easy Disconnect Info  ->  Break Easy Disconnect Info
                                             Reason      == Rejected
                                             Reason Text == "Wrong session password."
```

The information survives the travel precisely so the menu can show it. Check `Reason`
rather than matching the text. There are four of them:

| Reason | When |
|---|---|
| `ConnectionLost` | The host quit, crashed, or the network dropped |
| `HostDestroyedSession` | The host sent everyone out with `Destroy Easy Session For Everyone` |
| `TravelFailure` | Traveling to the session's map failed |
| `Rejected` | The host refused the connection - wrong password, or a closed match. `Reason Text` says which |

Keep this handler even with the beacon working: it is the safety net for every way a
connection can end.

### Friends skip the password

`Friends Bypass Password` defaults to **true**. Platform invites carry no password prompt,
so an invited friend would otherwise be turned away by the session they were invited to.
The host verifies friendship against the platform friends list, which the joining player
cannot fake. This has no effect on NULL/LAN, where there are no friends.

## Start Session / End Session

`Start Easy Session` moves the session to InProgress. With `Allow Join In Progress` off, new players are refused from here until the match ends - except on Steam, which already refused them from the first join onwards ([FAQ](FAQ.en.md)).

`End Easy Session` finishes the match and returns the session to a joinable state.

Both are host only. A client gets `RequiresSessionAuthority`, so show the buttons only when `Is Easy Session Authority` is true.

## Update Session

`Update Easy Session` re-advertises the session from a fresh set of host params. Host only.

You can change the display name, max players, advertise flag, hidden flag, join-in-progress flag, invites flag, the password (and its friends exception), and custom settings.

These options cannot be changed:

| Field | Why |
|---|---|
| Map Name | Move maps with `Server Travel Easy Session` instead |
| Host Mode | Listen or dedicated is how the process was started, not a live setting |
| Is LAN Match | Whether the session lives on the LAN or on the online service is decided at create time |
| Use Presence | The plugin does not pass it, and Steam refuses it anyway - it logs `Can't change presence settings on existing session` and keeps the old value |
| Start Listening / Additional Travel Options | Used once for the travel at create time, never read again |

> On LAN (NULL), changing the advertise flag leaves the LAN beacon as it was: the engine's `FOnlineSessionNull::UpdateSession` swaps the settings without recomputing the beacon.

## Destroy Session

`Destroy Easy Session` removes this game's named session - and only that. The player
stays on their current map, which is what a host between matches wants. A client
leaving the room wants `Leave Easy Session` instead: it destroys the named session and
then returns to the menu map. A host pressing Leave closes the room for everyone, with
"The host has left the game." as the reason clients read. After either one you can
immediately host or join again.

When the host calls it, clients see the connection drop and return to the menu with `ConnectionLost`. To tell them why it ended, pass a reason to `Destroy Easy Session For Everyone` instead: it sends that sentence to everyone before taking the session down, and they read it as `HostDestroyedSession`.

## Traveling mid-session

`Server Travel Easy Session` (host only) moves the whole session to a new map, appending `?listen` unless the server is dedicated or you wrote the option yourself. Clients follow automatically. Unlike the other nodes this one is not async - it returns success as a bool right away.

Always change maps with this node: it stops the join approval beacon before the map changes, and after a plain `ServerTravel` the new map cannot start its own beacon because the port is still held.

If the map fails to load (a typo, a map missing from the cook), the room and its players stay exactly where they were. The failure arrives on `On Session Failure` - call again with the right map name.

## Regions

Hosting advertises `Region` (`Any` by default) and searching filters by it: set the same
region in both places and players only see rooms they can play in. `Any` on the search
lists every region; `Any` on the host matches only searches that do not filter. The
regions are coarse on purpose - one region means playable latency. A game that needs its
own split (country servers, a single home region) leaves the field at `Any` and filters
with a `Custom Settings` key through `Required Custom Settings` instead.

## Join codes

Turn on `Use Join Code` when hosting and the session advertises a generated six character
code, readable with `Get Easy Session Join Code` by everyone in the room. `Join Easy
Session By Code` finds the room advertising that code and joins it - hidden sessions
included, so `Hidden` plus a code is a friends-only room: no browser lists it, anyone
with the code walks in.

The code identifies the room but does not protect it. Protection is `Password`, and the
two combine: the code finds the room, the password still gates the door.

## Events and state queries

Bind these on the subsystem (`Get Easy Session Subsystem`) for UI updates:

- `OnSessionCreated`, `OnSessionsFound`, `OnSessionJoined`, `OnSessionUpdated`, `OnSessionStarted`, `OnSessionEnded`, `OnSessionDestroyed` - fired as each operation completes, regardless of who initiated it
- `OnSessionFailure` - the connection to the session was lost or a network error occurred. EasySession automatically destroys the dead session so the player can rejoin immediately
- `OnQuickMatchStarted`, `OnQuickMatchStateChanged`, `OnQuickMatchUpdated`, `OnQuickMatchComplete` - a Quick Match run's progress, from acceptance to the end. Details in the [Quick Match guide](Guide-QuickMatch.en.md)

Pure state queries are usable anywhere: `Is In Easy Session`, `Is Easy Session Host`, `Is Easy Session Busy` and `Get Easy Session State` for status, `Get Easy Session Display Name`, `Get Easy Session Player Infos`, `Get Easy Session Player Count`, `Get Easy Session Max Players` for contents, and `Get Online Subsystem Name (EasySession)` for the environment. The [API reference](API.en.md) has the full list.

## C++ API

Everything above is a thin wrapper over `UEasySessionSubsystem` - C++ users call the same functions with native delegates:

```cpp
UEasySessionSubsystem* Session = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();
Session->CreateEasySession(HostParams,
	FEasySessionCompleteDelegate::CreateUObject(this, &UMyClass::OnHosted));
```

Blueprint and C++ take the identical code path, so behavior never diverges between them.
