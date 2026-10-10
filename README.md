# EasySession

Beginner-friendly sessions and matchmaking for Unreal Engine, built on the Online Subsystem (OSS).

Host, find, join and quick-play with a few Blueprint nodes - no custom `GameInstance`, no C++, no config files to start.

*[한국어 README](README.ko.md)*

## Install

1. Copy this folder into your project's `Plugins/` directory, so you have `YourProject/Plugins/EasySession/`.
2. Open the project, go to **Edit -> Plugins**, search for **EasySession**, enable it and restart.
3. For C++ projects, add `"EasySession"` to `PublicDependencyModuleNames` in your `.Build.cs`.

That is the whole setup for LAN play. The NULL online subsystem needs no accounts or keys. For Steam, follow [Steam setup](Docs/Setup-Steam.en.md).

## Features

- **Drops into an existing project** - no custom `GameInstance`, no required parent classes. Enabling the plugin creates the subsystem for you, and LAN play works without touching a config file. Keep the game mode and widgets you already have and add the nodes.
- **Matchmaking in one node** - `Start Easy Matchmaking` searches, joins the best session it finds, and with Allow Host Fallback on hosts one when it finds none.
- **Parties** - friends gather in a party before they play: hidden or listed, with an optional join code, invites, ready state and kick. When the leader hosts, joins or matchmakes, the whole party follows, and after the match the party comes back on its own.
- **Moving together** - the host of a lobby session that joins or matchmakes into another session brings every player along, with room reserved for all of them.
- **The whole session lifecycle in Blueprint** - create, find, join, start the match, end it, leave and update settings, all as async nodes. Session state, the player list and open slots are one node away, and the same API is available from C++.
- **Ready and kick** - every player and party member shares whether they are ready, and the host or party leader can remove a player with a reason they see.
- **Every request reports its progress and result** - `Is Busy` covers the request running or queued and the level load that follows hosting or joining, so binding it to a button's Is Enabled keeps that button locked for as long as the player is actually waiting. When the request finishes you get a result enum and a message you can show a player.
- **Overlapping calls are handled in order** - every request goes through a queue and runs one at a time. A call made while another is still running waits its turn instead of failing, and a repeat of the same call ends with a clear result such as `SessionAlreadyExists`.
- **Passwords and join-in-progress enforced by the host** - checked as the player connects, so a stale search result or a direct connect cannot walk into a running match.
- **Reservations** - an approved player holds a reservation until they arrive, so two players are never approved for the last free slot, and a player who leaves loses theirs so the next one can join.
- **Disconnect recovery** - when the host leaves or a travel fails, the session is cleaned up and the player is returned to the menu. The reason survives the map change so you can show it there.
- **Steam invites and friends** - accepting Join Game from the overlay joins automatically, plus friend invites, the invite and profile overlays, and the friends list.
- **A working example** - example maps and widgets with the full main menu -> lobby -> match cycle.
- **Extensible matchmaking** - override one `ScoreSession` function to pick sessions your way.

## Example

The plugin ships example maps and widgets with the full main menu -> lobby -> match cycle. Each widget does one job, so you can copy just the part you need.

| Screen | Widgets |
|---|---|
| Main menu | `WBP_MainMenu`, holding `WBP_HomeScreen`, `WBP_CreateSessionScreen`, `WBP_FindScreen`, `WBP_CreatePartyScreen` and `WBP_PartyCard` |
| Lobby | `WBP_LobbyScreen`, with `WBP_PlayerList`, `WBP_SessionInfo` and `WBP_UpdateSessionPopup` |
| Match | `WBP_InGameMenu`, with `WBP_EscPopup` |
| Any screen | `WBP_SessionStatus`, the status line that narrates whatever runs |

[Quick Start](Docs/QuickStart.en.md#2-play-the-example-first) shows how to run it and lists the nodes each widget calls.

## Limitations

- **One game session at a time, plus an optional party.** Game sessions use the engine's `NAME_GameSession` slot and the party uses `NAME_PartySession`, so running more than one game session side by side is not supported.
- **A party does not move members between maps.** Each member stays in their own map, and only entering a game session brings the party along. Host a lobby session to put everyone in the same map ([Party guide](Docs/Guide-Party.en.md#changing-maps-outside-a-match)).
- **No reconnect to a game session.** A player who loses the connection to the host is returned to the menu with the reason, and joins again from there.
- **Local player 0 only.** Split-screen is not supported.
- **Change maps with `Server Travel Easy Session`.** It moves the reservations to the new map, so the players a hard travel reconnects are let back in. A plain `ServerTravel` keeps the beacon port busy, and the new map runs no reservation beacon.
- **Password sessions are joined through the plugin only.** The password goes to the host over the reservation beacon and never into the travel URL, so a direct connect or your own `ClientTravel` into a password session is refused.
- **Dedicated servers** are not supported yet - sessions are hosted on listen servers.

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

- [Quick Start](Docs/QuickStart.en.md) - host and join in 5 minutes
- [Concepts](Docs/Concepts.en.md) - sessions, OSS, travel, listen vs dedicated, and how they fit together
- Setup: [LAN](Docs/Setup-LAN.en.md) | [Steam](Docs/Setup-Steam.en.md)
- Guides: [Sessions](Docs/Guide-Sessions.en.md) | [Matchmaking](Docs/Guide-Matchmaking.en.md) | [Party](Docs/Guide-Party.en.md)
- [API Reference](Docs/API.en.md)
- [FAQ & Troubleshooting](Docs/FAQ.en.md)

## Modules

One runtime module, `EasySession`: the core subsystem, the session/matchmaking API, and the Blueprint nodes.

## Support

- **Bugs and feature requests** - [GitHub Issues](https://github.com/JeongJongMun/EasySession/issues)
- **Questions and setup help** - [Discord](https://discord.gg/dR9UM2nCvB)
- **Email** - jjm13.dev@gmail.com

When reporting a problem, include your engine version, the online subsystem you use
(NULL or Steam), and the relevant log lines. `EasySession.Status` in the console prints
the session state and the request queue, which answers most of the first questions.

## License

[MIT](LICENSE) - free to use in commercial and non-commercial projects.
