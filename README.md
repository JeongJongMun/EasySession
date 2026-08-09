# EasySession

Beginner-friendly sessions and matchmaking for Unreal Engine, built on the Online Subsystem (OSS).

Host, find, join and quick-play with a few Blueprint nodes - no custom `GameInstance`, no C++, no config files to start.

> Still in development.

*[한국어 README](README.ko.md)*

## Install

1. Copy this folder into your project's `Plugins/` directory, so you have `YourProject/Plugins/EasySession/`.
2. Open the project, go to **Edit -> Plugins**, search for **EasySession**, enable it and restart.
3. For C++ projects, add `"EasySession"` to `PublicDependencyModuleNames` in your `.Build.cs`.

That is the whole setup for LAN play. The NULL online subsystem needs no accounts or keys. For Steam, follow [Steam setup](Docs/Setup-Steam.md).

## Features

- **Drops into an existing project** - no custom `GameInstance`, no required parent classes. Enabling the plugin creates the subsystem for you, and LAN play works without touching a config file. Keep the game mode and widgets you already have and add the nodes.
- **Quick match in one node** - `Quick Match Easy Session` searches, joins the best room it finds, and hosts one when it finds none.
- **The whole session lifecycle in Blueprint** - create, find, join, start the match, end it, leave and update settings, all as async nodes. Session state, the player list and open slots are one node away, and the same API is available from C++.
- **Every operation reports its progress and result** - `Is Busy` covers the online work running or queued and the level load that follows hosting or joining, so binding it to a button's Is Enabled keeps that button locked for as long as the player is actually waiting. When the work finishes you get a result enum and a message you can show a player, and a request the online service never answers is failed after a timeout instead of blocking everything behind it.
- **Overlapping calls are handled in order** - every operation goes through a queue and runs one at a time, so mashing a button produces results in order instead of errors.
- **Passwords and join-in-progress enforced by the host** - checked as the player connects, so a stale search result or a direct connect cannot walk into a running match.
- **Disconnect recovery** - when the host leaves or a travel fails, the session is cleaned up and the player is returned to the menu. The reason survives the map change so you can show it there.
- **Steam invites and friends** - accepting Join Game from the overlay joins automatically, plus friend invites, the invite and profile overlays, and the friends list.
- **A working example** - example maps and widgets with the full main menu -> lobby -> match cycle.
- **Extensible matchmaking** - override one `ScoreSession` function to pick rooms your way.

## Limitations

- **One session at a time.** Everything uses the engine's `NAME_GameSession` slot, so running more than one session side by side is not supported.
- **Not implemented yet.** `OnlineBeacon` based parties, seat reservations and reconnect are not in the plugin. The scope is a single session: create it, find it, join it, play.
- **Local player 0 only.** Split-screen is not supported.
- **Map changes during a match must use seamless travel.** The host-side join gate treats a new connection as a new player, so a hard travel mid-match would lock your own players out. The plugin's own travels already do the right thing.
- **Rolling your own `ClientTravel` into a password session** means appending the password option yourself; the plugin only adds it on the travels it performs.
- **Dedicated servers** have working code paths but are not validated yet - listen servers are the tested configuration.
- **The example character borrows an engine asset.** It uses the engine's tutorial mannequin (`/Engine/Tutorial/...`) instead of shipping a mesh of its own, which keeps the plugin small. Everything else in the examples depends only on `/Engine/BasicShapes` and `/Engine/MapTemplates`. Swap in your own character if you build on the examples.

## Supported online subsystems

| Subsystem | Status |
|---|---|
| NULL (LAN) | Supported |
| Steam | Supported |
| EOS | Not supported |

## Engine support

- **UE 5.8** only. This is the version the plugin is built and tested against.
- Support for earlier versions is planned.

## Documentation

These guides are still being written and do not yet cover every feature.

- [Quick Start](Docs/QuickStart.md) - host and join in 5 minutes
- [Concepts](Docs/Concepts.md) - sessions, OSS, listen vs dedicated, without the confusion
- Setup: [LAN](Docs/Setup-LAN.md) | [Steam](Docs/Setup-Steam.md)
- Guides: [Sessions](Docs/Guide-Sessions.md) | [Quick Match](Docs/Guide-QuickMatch.md) | [Dedicated servers](Docs/Guide-DedicatedServer.md)
- [API Reference](Docs/API.md)
- [FAQ & Troubleshooting](Docs/FAQ.md)

## Modules

| Module | Type | Description |
|---|---|---|
| `EasySession` | Runtime | Core subsystem, session/matchmaking API, Blueprint nodes |
| `EasySessionEditor` | Editor | Settings validation and editor tooling |

## License

[MIT](LICENSE) - free to use in commercial and non-commercial projects.
