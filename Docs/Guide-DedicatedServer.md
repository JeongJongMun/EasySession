# Guide — Dedicated Servers

EasySession supports dedicated servers end to end: the server advertises itself, clients find and join it like any other session.

> Status note: dedicated flow validation on Steam is scheduled after the 1.0 release. The LAN/NULL flow below reflects the current implementation.

## What you need

A **server build target** (`MyGameServer.Target.cs`, `Type = TargetType.Server`). Building server targets requires a **source-built engine** — the Epic Games Launcher distribution cannot build them. This is an engine limitation, not a plugin one.

## Server side — zero code required

With **Project Settings → Plugins → EasySession**:

- **Auto Host On Dedicated Server** (default: on) — when the server process boots and its map is up, EasySession automatically creates and advertises a non-presence session using **Dedicated Server Host Params**.
- The server keeps the map it was launched with (`MyGameServer.exe YourMap -log`); the params' Map Name is intentionally ignored.

Prefer manual control? Turn the setting off and call `Create Easy Session` yourself with `Host Mode = Dedicated Server`.

## Client side

Nothing special: `Find Easy Sessions` returns dedicated sessions with `bIsDedicatedServer = true`, and `Join Easy Session` connects normally.

For a dedicated-only game, run Quick Play with **`Allow Host Fallback = false`** — clients will never accidentally become listen hosts, and an empty server list fails cleanly with `NoSessionsFound`.

## Differences from listen servers (handled for you)

| Concern | What EasySession does |
|---|---|
| No local player on the server | Creates the session without a hosting player, skips player registration |
| Presence | Forced off — dedicated sessions are never presence sessions |
| Invites | Forced off |
| Listen ensure / travel | Skipped — the server is already listening from boot |

## LAN test recipe

1. Source-built engine: build the `Server` target.
2. Start the server: `MyGameServer.exe TestArena -log`
3. Verify the log: `Dedicated server detected. Auto hosting session ...` then `Session created successfully.`
4. Start a client on the same network, `Find Easy Sessions` (or `EasySession.Find` in the console) — the session appears with the dedicated flag; join it.
