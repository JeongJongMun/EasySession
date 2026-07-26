# Guide - Sessions

Everything about hosting, finding, joining, updating and leaving sessions. All six async nodes share the same shape: inputs on the left, `OnSuccess` / `OnFailure` exec pins with a `Result` enum and an `ErrorMessage` string.

All operations are **queued and executed one at a time** - you can call them in any order, even in the same frame, and they will never corrupt the online service.

## Hosting

`Create Easy Session` with `FEasySessionHostParams`:

| Field | Default | Notes |
|---|---|---|
| Session Display Name | "My Session" | Shown in search results |
| Map Name | (empty) | Travels there with `?listen`. Empty = start listening on the current map |
| Host Mode | Listen Server | Or Dedicated Server (see [dedicated guide](Guide-DedicatedServer.md)) |
| Max Players | 4 | Public connections |
| Is LAN Match | false | Forced on automatically under the NULL subsystem |
| Start Listening | true | Leave on unless you manage the listen server yourself |
| Should Advertise | true | Off = private/invisible session |
| Allow Join In Progress | true | |
| Allow Invites / Use Presence | true | Ignored on LAN and dedicated servers |
| Custom Settings | (empty) | Advertised key-value data, see below |

### Custom session data

`Custom Settings` is a String->String map advertised with the session. Use it for game mode, region, difficulty - anything searchers should filter or display:

```
CustomSettings = { "GameMode": "CTF", "Region": "AS" }
```

Searchers read it back from each `FEasySessionSearchResult.CustomSettings`, and can filter server-side of the search with `Required Custom Settings` (exact match on every pair).

## Finding

`Find Easy Sessions` with `FEasySessionSearchParams`:

| Field | Default | Notes |
|---|---|---|
| Max Results | 50 | |
| LAN Query | false | Forced on automatically under NULL |
| Timeout Seconds | 15 | |
| Min Open Slots | 0 | Only sessions with at least this many free slots |
| Max Ping Ms | 0 | 0 = no limit |
| Required Custom Settings | (empty) | Exact-match filters against advertised custom data |

Results arrive on `OnSuccess` and are also cached - `Get Last Easy Search Results` returns them anywhere, anytime (useful for server browser UIs).

Each `FEasySessionSearchResult` exposes: display name, host name, ping, max players, open slots, dedicated flag, and the custom settings map.

## Joining

`Join Easy Session` takes a search result. On success it resolves the host address and client-travels there automatically (`Travel On Success` can turn that off).

EasySession validates the host address **before** reporting success - if the host is not actually reachable (see [FAQ: port 0](FAQ.md)), you get an immediate `ResolveFailure` with an explanation instead of a 20-second connection timeout, and the half-joined session is cleaned up so you can retry right away.

## Updating

`Update Easy Session` re-advertises the current session with new display name, max players, advertise flag, join-in-progress flag and custom settings. Host only. Map Name and Host Mode are ignored - you cannot change those live (travel instead).

## Leaving

`Destroy Easy Session` destroys the session on the host (kicking everyone back to their own worlds is the engine's default behavior) and leaves it on a client. After leaving you can immediately host or join again.

## Traveling mid-session

`Server Travel To Map` (host only) moves the whole session to a new map, automatically keeping the `?listen` option. Clients follow automatically.

## Events and state queries

Bind these on the subsystem (`Get Easy Session Subsystem`) for UI updates:

- `OnSessionCreated`, `OnSessionsFound`, `OnSessionJoined`, `OnSessionUpdated`, `OnSessionDestroyed` - mirror of every operation, fired regardless of who initiated it
- `OnSessionFailure` - the connection died (host quit, network dropped). EasySession automatically destroys the dead session so the player can rejoin immediately
- `OnMatchmakingComplete` - Quick Match finished

Pure state queries, usable anywhere: `Is In Easy Session`, `Is Easy Session Host`, `Is Easy Session Busy`, `Get Online Subsystem Name`.

## C++ API

Everything above is a thin wrapper over `UEasySessionSubsystem` - C++ users call the same functions with native delegates:

```cpp
UEasySessionSubsystem* Session = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();
Session->CreateEasySession(HostParams,
	FEasySessionCompleteDelegate::CreateUObject(this, &UMyClass::OnHosted));
```

Blueprint and C++ take the identical code path, so behavior never diverges between them.
