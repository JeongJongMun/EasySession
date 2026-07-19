# Quick Start — Host and Join in 5 Minutes

This guide takes you from an empty project to two game instances playing together on LAN, using only Blueprint. No custom GameInstance, no C++, no configuration files.

## 1. Enable the plugin

1. Open **Edit → Plugins**, search for **EasySession** and enable it.
2. Restart the editor when prompted.

That's it for setup — EasySession runs on the NULL (LAN) online subsystem out of the box, which needs no accounts or keys.

## 2. Host a session

In any Blueprint (a menu widget button, or the Level Blueprint for a quick test):

```
[Button Clicked] → [Create Easy Session]
                      HostParams:
                        Session Display Name = "My First Session"
                        Map Name = "/Game/Maps/Lobby"   ← your map here
                      OnSuccess → (you are now hosting)
                      OnFailure → [Print String: ErrorMessage]
```

`Create Easy Session` does everything a host needs:

- Creates and advertises the session
- Travels to the map as a **listen server** (`?listen` is added automatically)
- If you leave **Map Name** empty, it starts listening on the current map instead
- Registers you as a player, so your session shows correct player counts

## 3. Find and join from another instance

```
[Button Clicked] → [Find Easy Sessions]
                      OnSuccess (Results) → [ForEach] → add a row to your server list UI
                      OnFailure → [Print String: ErrorMessage]

[Row Clicked] → [Join Easy Session]
                   SearchResult = (the row's result)
                   OnSuccess → (traveling to the host automatically)
                   OnFailure → [Print String: ErrorMessage]
```

Every failure pin gives you a `Result` enum and a human-readable `ErrorMessage` — no silent failures.

## 4. Or skip all of that with Quick Play

```
[Button Clicked] → [Quick Play Easy Session]
                      QuickPlayParams:
                        Host → Map Name = "/Game/Maps/Lobby"
                      OnSuccess → (joined the best session, or hosting a new one)
                      OnFailure → [Print String: ErrorMessage]
```

Quick Play searches, joins the best session (good ping, fuller rooms first), and hosts a new session if nothing is found. Use `Is Easy Session Host` to check which outcome you got.

## 5. Test in PIE

1. **Editor Preferences → Play**: set **Number of Players = 2** and **Net Mode = Play Standalone**.
2. Press Play — you get two windows.
3. Host in window 1, find & join in window 2.

If the windows cannot see each other's sessions, disable **Run Under One Process** in the same settings — separate processes use the same networking path as packaged builds.

> Tip: you can test everything without any UI using console commands (`~` key):
> `EasySession.Host`, `EasySession.Find`, `EasySession.Join 0`, `EasySession.QuickPlay`, `EasySession.Leave`, `EasySession.Status`.
> They are available in development builds and automatically stripped from shipping.

## Next steps

- [Concepts](Concepts.md) — what a session actually is, and what NULL/Steam/EOS mean
- [Steam setup](Setup-Steam.md) / [EOS setup](Setup-EOS.md) — go online beyond LAN
- [Session guide](Guide-Sessions.md) — custom session data, filters, updating sessions
- [Quick Play guide](Guide-QuickPlay.md) — how the matchmaking picks a session, custom scoring
- [FAQ](FAQ.md) — the questions everyone asks
