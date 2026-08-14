# Concepts - Sessions and Connections

*[한국어](Concepts.ko.md)*

If you are new to Unreal's online systems, read this first. It will help.

## What is a session?

A **session** is a record the online service holds for one running game. It carries the host's address, a display name, how many players it holds and how many slots are free, and any custom values you attach - the map, the game mode, whether a password is required.

`Find Easy Sessions` returns copies of those records. `Join Easy Session` reads the host address out of the one you picked and connects to it.

A session is data, not a connection. It tells players where a game is; moving game data between machines is a separate system, the [NetDriver](#what-is-a-netdriver). Most "it does not work" reports come from that gap: the session is advertised, but the host never became a server, so everyone trying to join hits a dead address.

The free-slot count is part of that record, so it is only right if someone keeps it current. Each player who arrives is registered in the session and each one who leaves is unregistered. Without that, a session that is already full keeps advertising free space.

## What is the Online Subsystem (OSS)?

The **Online Subsystem** is Unreal's abstraction over platform online services. The same `Create Easy Session` call works on any of them:

- **NULL** - LAN only. No accounts, no setup. Sessions are found via UDP broadcast on the local network. This is the default and perfect for development.
- **Steam** - sessions become Steam lobbies / server list entries. Requires the Steam client running and an AppId. See [Steam setup](Setup-Steam.en.md).

Which one is active is decided by `DefaultEngine.ini` (`[OnlineSubsystem] DefaultPlatformService=...`), not by code. Your Blueprint graphs stay identical.

## Session, lobby, beacon - three words that get mixed up

- A **session** is the advertisement and the online service's record of a running game - the thing Create / Find / Join manage. It says where the game is; it carries no gameplay.
- A **lobby** means two unrelated things, which is why it confuses everyone. Inside your game it is just a map where players gather before the match (the example's `L_Example_Lobby`) - an ordinary map as far as the online service is concerned. On Steam it is the name of the backend object that stores a presence session - EasySession creates and destroys those for you, so you never handle one directly.
- A **beacon** is a second, lightweight connection to a host, made for questions that must be answered without loading a map. No pawn spawns and no level loads over it. EasySession uses one so `Join Easy Session` can ask "may this player join?" and fail cleanly before any travel starts ([guide](Guide-Sessions.en.md#password-protected-sessions)).

## What "traveling" means

Unreal calls it **traveling** whenever a game switches worlds - loading a map, or connecting to another machine's map.

- **Client travel** - this game goes somewhere: to a map, or to a host's address. Joining a session ends with a client travel to the host.
- **Server travel** - the server takes the whole session to a new map together. `Server Travel Easy Session` is this; clients follow automatically.
- `?listen` is a travel option meaning "open this map as a server, so others can travel to me." It is how `Create Easy Session` makes the host a server.

Traveling does not destroy the session. The session lives on the online service, not in the map, so the advertisement stays up while the map changes and searching players keep finding it.

### Seamless travel

A server travel comes in two forms, and the difference matters once players are already in your session.

- **Hard travel** (the default) disconnects every player and reconnects them on the new map. Reconnecting means the host runs its join checks again, so a password session would turn away the players who are already inside.
- **Seamless travel** carries players through a small transition map without ever dropping the connection. No reconnect, no second round of join checks.

**Use seamless travel for map changes during a session.** Turn it on with `bUseSeamlessTravel` on your GameMode, and set a **Transition Map** in Project Settings -> Maps & Modes. `Server Travel Easy Session` follows that setting.

One exception is handled for you: the first travel, the one that turns the host into a server, is always a hard load. Seamless travel drops the `?listen` option, and the host would never become a server.

## What is a NetDriver?

The **NetDriver** is the engine object that actually moves game data: it opens the socket, makes or accepts the connection, and replicates actors. One is created when a map opens with `?listen` (the server side) and when a client travels to an address (the client side).

You almost never touch it directly. The one place you will meet the name is configuration: the [Steam setup](Setup-Steam.en.md) replaces the default NetDriver definitions so connections go through Steam's network, and the beacon has its own `BeaconNetDriver` definition.

## Listen server vs dedicated server

- **Listen server** - the hosting player's game *is* the server. No server machines to rent or run; ideal for co-op and small games. This is EasySession's default (`Host Mode = Listen Server`).
- **Dedicated server** - a server process with no graphics and no local player hosts the game; every player is a client. Requires building a server target (needs a source-built engine). EasySession supports it via `Host Mode = Dedicated Server` and can auto-create the session when the server boots (Project Settings -> Plugins -> EasySession).

## Presence, LAN, and why some settings are ignored

- **Presence** means "friends can see what you're playing and join you." It only exists on a platform service such as Steam - on LAN it is meaningless, so EasySession ignores it there. In code and settings it appears as `bUsePresence`.
- **LAN match** means the session is advertised by local broadcast instead of an online service. When the NULL subsystem is active, EasySession forces LAN mode automatically, so searching finds LAN sessions without any settings.
- Dedicated server sessions never use presence (there is no "player" hosting them).

## One session, many matches

A **match** is one round of play: it begins when everyone is ready and ends when someone wins, the timer runs out, or the objective is done. A deathmatch round, a race, a dungeon run.

A match happens inside a session, and the session outlives it. It is there before, during and after, so the same session can run one match, then another.

The session cannot work out where the match stands on its own - you tell it:

- `Start Easy Session` marks the match as running. With **Allow Join In Progress** off, this is the moment the session stops taking new players. Steam is stricter and stops at the first join instead, which is why the setting is best left on there ([FAQ](FAQ.en.md)).
- `End Easy Session` marks it finished, so `Start` can run the next match on the same session.

Neither node starts or stops your gameplay - they move the session's state. If your game has no notion of a match starting (a sandbox, a social space), you never need to call either.

## Where players go when a session ends

Leaving a session, losing the host, or being refused all end the same way: the
engine loads the **Game Default Map** (Project Settings -> Maps & Modes). That
map is your "after the session" screen, so point it at your menu map. EasySession
preserves why the player came back - read it there with
`Consume Last Easy Disconnect Info` and show the reason.

## The one rule that prevents most bugs

**One session at a time.** You must leave your current session before creating or joining another one. EasySession enforces this with a clear `SessionAlreadyExists` error instead of strange failures later, and its operation queue makes sure two requests never overlap inside the online service.
