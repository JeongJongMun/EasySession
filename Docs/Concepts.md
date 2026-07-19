# Concepts — Sessions Without the Confusion

If you are new to Unreal's online systems, read this first. Five minutes here saves hours of confusion later.

## What is a session?

A **session** is an advertisement: "there is a game here, this many players, this is how to reach it." It is not the network connection itself — think of it as a row in a server browser.

Three separate systems have to work together for multiplayer to happen:

| Layer | What it does | Who provides it |
|---|---|---|
| **Session** | Advertise, find, and reserve a spot in games | Online Subsystem (this plugin's job) |
| **Connection** | Actually move game data between machines | NetDriver (`?listen`, `ClientTravel`) |
| **Player registry** | Track who is in the session, count open slots | `RegisterPlayers` (this plugin handles it) |

Most "it doesn't work" stories come from these layers being out of sync — a session that is advertised but nobody is listening, or a room that always looks empty. EasySession keeps the three layers in sync for you.

## What is the Online Subsystem (OSS)?

The **Online Subsystem** is Unreal's abstraction over platform online services. The same `Create Easy Session` call works on any of them:

- **NULL** — LAN only. No accounts, no setup. Sessions are found via UDP broadcast on the local network. This is the default and perfect for development.
- **Steam** — sessions become Steam lobbies / server list entries. Requires the Steam client running and an AppId. See [Steam setup](Setup-Steam.md).
- **EOS (Epic Online Services)** — Epic's free cross-platform service. Requires a (free) Dev Portal product. See [EOS setup](Setup-EOS.md).

Which one is active is decided by `DefaultEngine.ini` (`[OnlineSubsystem] DefaultPlatformService=...`), not by code. Your Blueprint graphs stay identical.

## Listen server vs dedicated server

- **Listen server** — the hosting player's game *is* the server. Zero infrastructure; ideal for co-op and small games. This is EasySession's default (`Host Mode = Listen Server`).
- **Dedicated server** — a headless server process hosts the game; every player is a client. Requires building a server target (needs a source-built engine). EasySession supports it via `Host Mode = Dedicated Server` and can auto-create the session when the server boots (Project Settings → Plugins → EasySession).

## Presence, LAN, and why some settings are ignored

- **Presence** means "friends can see what you're playing and join you." It only exists on platform services (Steam/EOS) — on LAN it is meaningless, so EasySession ignores it there.
- **LAN match** means the session is advertised by local broadcast instead of an online service. When the NULL subsystem is active, EasySession forces LAN mode automatically so things just work.
- Dedicated server sessions never use presence (there is no "player" hosting them).

## The one rule that prevents most bugs

**One session at a time.** You must leave your current session before creating or joining another one. EasySession enforces this with a clear `SessionAlreadyExists` error instead of undefined behavior, and its operation queue makes sure two requests never overlap inside the online service.
