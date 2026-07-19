# EasySession

Beginner-friendly sessions and matchmaking for Unreal Engine, built on the Online Subsystem (OSS).

Host, find, join and quick-play with just a few Blueprint nodes — no custom GameInstance required.

## Documentation

- [Quick Start](Docs/QuickStart.md) — host and join in 5 minutes
- [Concepts](Docs/Concepts.md) — sessions, OSS, listen vs dedicated, without the confusion
- Setup: [LAN](Docs/Setup-LAN.md) · [Steam](Docs/Setup-Steam.md) · [EOS](Docs/Setup-EOS.md)
- Guides: [Sessions](Docs/Guide-Sessions.md) · [Quick Play matchmaking](Docs/Guide-QuickPlay.md) · [Dedicated servers](Docs/Guide-DedicatedServer.md)
- [API Reference](Docs/API.md)
- [FAQ & Troubleshooting](Docs/FAQ.md)

## Highlights

- **One node to play** — `Quick Play Easy Session`: search, join the best session, or host automatically
- **Cannot be called wrong** — every operation is queued and serialized; overlapping calls never corrupt the online service
- **Fails loudly and helpfully** — rich result enums and messages instead of silent 20-second timeouts
- **Listen server & slot accounting handled** — the classic "session is visible but unjoinable" and "room always looks empty" pitfalls are fixed inside the plugin
- **Extensible matchmaking** — override one `ScoreSession` function (Blueprint or C++) for custom criteria
- **Same path for BP and C++** — the nodes are thin wrappers over `UEasySessionSubsystem`

## Status

In development — session core, Blueprint API and Quick Play matchmaking are implemented and covered by automation tests on the NULL (LAN) subsystem. Steam/EOS validation and example content are in progress.

## Engine support

- Primary development: **UE 5.8**
- Target support: UE 5.5 – 5.8

## Modules

| Module | Type | Description |
|---|---|---|
| `EasySession` | Runtime | Core subsystem, session/matchmaking API, Blueprint nodes |
| `EasySessionEditor` | Editor | Settings validation and editor tooling |

## License

TBD (free release on Fab planned).
