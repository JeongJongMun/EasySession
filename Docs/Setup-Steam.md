# Setup — Steam

Run your sessions on Steam: internet play, friends, invites, and presence.

> Verified on UE 5.8 with two machines and two Steam accounts (sessions, search, join, passwords, invites, friends, overlay). If something misbehaves, run `EasySession.Diagnose` in the console — it checks this exact setup and prints copy-paste fixes.

## Prerequisites

- Steam client installed, running, and logged in — on **every** machine that tests.
- An AppId. For development you can use **480** (Spacewar, Valve's public test AppId). For release you need your own AppId from Steamworks.

## 1. Enable the plugins

Enable both in Edit → Plugins (restart required):

- **Online Subsystem Steam** — sessions, friends, invites, presence.
- **Steam Sockets** — game traffic over Steam's network (NAT traversal included).

> Why both? Steam session addresses look like `steam.7656...`, which only the Steam Sockets net driver can connect to. The legacy `SteamNetDriver` that older guides mention was removed from the engine — if your config references it, the engine **silently** falls back to the IP driver and every join fails with "connection to the host lost".

## 2. DefaultEngine.ini

Copy-paste and adjust the AppId:

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
; Set to your own AppId for release builds
bInitServerOnClient=true

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
```

The `!NetDriverDefinitions=ClearArray` line matters: the engine's base config already defines a `GameNetDriver` entry and the **first** matching entry wins, so appending without clearing leaves your Steam driver as dead config.

## 3. Test checklist

- Editor PIE **cannot** represent two Steam users. Test with **packaged builds (or `-game`) on two machines with two different Steam accounts**.
- Two machines on the same account will not see each other's sessions.
- With AppId 480 you share the session space with everyone else testing on Spacewar — use a unique custom setting (e.g. `GameName = MyGameDev`) in Host Params and the same value in Search Params `Required Custom Settings` to filter to your own sessions.
- `steam_appid.txt` containing your AppId next to the executable is required when launching outside the Steam client during development. Non-Shipping builds generate it automatically.
- Package in **Development** configuration for device testing; Shipping builds must be launched through the Steam client.

## Common pitfalls

| Symptom | Cause |
|---|---|
| `LogOnline: STEAM: Steam API failed to initialize` | Steam client not running, or missing `steam_appid.txt` |
| Sessions not found between two PCs | Same Steam account on both, different AppIds, or a stale build on one machine |
| Invite/overlay works but join fails with "connection to the host has been lost" | Net driver is not Steam Sockets — plugin disabled, `ClearArray` line missing, or config still references the removed legacy `SteamNetDriver`. Run `EasySession.Diagnose` |
| Everything works in PIE but not packaged | PIE was silently using NULL — check the log line `EasySessionSubsystem initialized. Online subsystem: STEAM` |

When in doubt: check the startup log for the `===== EasySession diagnostics =====` block, or run `EasySession.Diagnose` at any time. It verifies the active subsystem, every ini key above, that the configured net driver class actually loads, and the Steam login state.
