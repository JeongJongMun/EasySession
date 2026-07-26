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

## What it looks like

Hosting, from a menu widget:

```
[Button Clicked] -> [Create Easy Session]
                      Session Display Name = "My First Session"
                      Map Name = "/Game/Maps/Lobby"
                      OnSuccess -> you are hosting, already traveled as a listen server
                      OnFailure -> Result enum + human-readable Error Message
```

Finding and joining:

```
[Button Clicked] -> [Find Easy Sessions] -> OnSuccess (Results) -> build your server list
[Row Clicked]    -> [Join Easy Session]  -> OnSuccess -> traveling to the host
```

Or skip the browser entirely:

```
[Button Clicked] -> [Quick Match Easy Session]   <- searches, joins the best match, hosts if none
```

The same API in C++:

```cpp
UEasySessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();

FEasySessionHostParams Params;
Params.SessionDisplayName = TEXT("My First Session");
Params.MapName = TEXT("/Game/Maps/Lobby");

Sessions->CreateEasySession(Params, FEasySessionCompleteDelegate::CreateLambda(
    [](EEasySessionResult Result, const FString& ErrorMessage)
    {
        // Result tells you exactly what happened; ErrorMessage is safe to show a player.
    }));
```

The Blueprint nodes are thin wrappers over that subsystem, so both paths behave identically.

## Highlights

- **One node to play** - `Quick Match Easy Session` searches, joins the best session it finds, and hosts one when it finds none.
- **Calls are sequenced, not rejected** - the online subsystem refuses a second call of the same kind, and does nothing about *different* operations overlapping. EasySession queues every operation and runs them one at a time, so a player mashing buttons gets their actions in order instead of errors.
- **Busy means busy** - `Is Busy` covers queued work, multi-step Quick Match, and the level load that follows a host or join, so a UI bound to it stays correct for the whole operation a player perceives.
- **Fails loudly and helpfully** - every operation reports a result enum and a message written for a player, instead of a silent 20-second timeout. A request that the online service never answers is failed by a watchdog rather than blocking everything behind it.
- **Password and join-in-progress are enforced on the host** - checked in `PreLogin`, so a stale search result or a direct connect cannot walk into a running match.
- **Extensible matchmaking** - override one `ScoreSession` function in Blueprint or C++ for custom criteria.

## Supported online subsystems

| Subsystem | Status |
|---|---|
| NULL (LAN) | Supported |
| Steam | Supported |
| EOS | Not supported |

## Limitations

- **One session at a time.** Everything uses the engine's `NAME_GameSession` slot; parties and multiple simultaneous sessions are not supported.
- **Local player 0 only.** Split-screen is not supported.
- **Map changes during a match must use seamless travel.** The host-side join gate treats a new connection as a new player, so a hard travel mid-match would lock your own players out. The plugin's own travels already do the right thing.
- **Rolling your own `ClientTravel` into a password session** means appending the password option yourself; the plugin only adds it on the travels it performs.
- **Dedicated servers** have working code paths but are not validated yet - listen servers are the tested configuration.

## Engine support

- Primary development: **UE 5.8**
- Target support: UE 5.5 - 5.8

## Documentation

- [Quick Start](Docs/QuickStart.md) - host and join in 5 minutes
- [Concepts](Docs/Concepts.md) - sessions, OSS, listen vs dedicated, without the confusion
- Setup: [LAN](Docs/Setup-LAN.md) | [Steam](Docs/Setup-Steam.md)
- Guides: [Sessions](Docs/Guide-Sessions.md) | [Quick Match matchmaking](Docs/Guide-QuickMatch.md) | [Dedicated servers](Docs/Guide-DedicatedServer.md)
- [API Reference](Docs/API.md)
- [FAQ & Troubleshooting](Docs/FAQ.md)

## Modules

| Module | Type | Description |
|---|---|---|
| `EasySession` | Runtime | Core subsystem, session/matchmaking API, Blueprint nodes |
| `EasySessionEditor` | Editor | Settings validation and editor tooling |

## License

[MIT](LICENSE) - free to use in commercial and non-commercial projects.
