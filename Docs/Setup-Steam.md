# Setup — Steam

Run your sessions on Steam: internet play, friends, invites, and presence.

> Status note: EasySession's Steam validation is in progress (Phase 4 of the roadmap). The configuration below is the standard OnlineSubsystemSteam setup; report issues you hit with it.

## Prerequisites

- Steam client installed, running, and logged in — on **every** machine that tests.
- An AppId. For development you can use **480** (Spacewar, Valve's public test AppId). For release you need your own AppId from Steamworks.

## 1. Enable the plugin

Enable **Online Subsystem Steam** in Edit → Plugins (restart required).

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

[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName="OnlineSubsystemSteam.SteamNetConnection"

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="OnlineSubsystemSteam.SteamNetDriver",DriverClassNameFallback="OnlineSubsystemUtils.IpNetDriver")
```

The `SteamNetDriver` block routes game traffic through Steam's networking (NAT traversal included). Without it, sessions are found but clients cannot connect across the internet.

## 3. Test checklist

- Editor PIE **cannot** represent two Steam users. Test with **packaged builds (or `-game`) on two machines with two different Steam accounts**.
- Two machines on the same account will not see each other's sessions.
- With AppId 480 you share the session space with everyone else testing on Spacewar — use a unique custom setting (e.g. `GameName = MyGameDev`) in Host Params and the same value in Search Params `Required Custom Settings` to filter to your own sessions.
- `steam_appid.txt` containing your AppId next to the executable is required when launching outside the Steam client during development.

## Common pitfalls

| Symptom | Cause |
|---|---|
| `LogOnline: STEAM: Steam API failed to initialize` | Steam client not running, or missing `steam_appid.txt` |
| Sessions not found between two PCs | Same Steam account on both, or different AppIds |
| Found but cannot connect | Missing `SteamNetDriver` NetDriverDefinitions block |
| Everything works in PIE but not packaged | PIE was silently using NULL — check the log line `EasySessionSubsystem initialized. Online subsystem: STEAM` |
