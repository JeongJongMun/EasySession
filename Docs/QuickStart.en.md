# Quick Start - Host and Join in 5 Minutes

*[한국어](QuickStart.ko.md)*

This guide takes you from an empty project to two game instances playing together on LAN, using only Blueprint. No custom GameInstance, no C++, no config file editing.

## 1. Enable the plugin

1. Open **Edit -> Plugins**, search for **EasySession** and enable it.
2. Restart the editor when prompted.

That is the whole setup. EasySession runs on the NULL (LAN) online subsystem out of the box, which needs no accounts or keys.

## 2. Play the example first

The plugin ships a working main menu, lobby and match. Running it takes less time than
wiring your first node, and it shows what the finished flow looks like.

1. In the Content Browser, turn on **Settings -> Show Plugin Content**.
2. **Project Settings -> Maps & Modes -> Game Default Map** = `L_Example_MainMenu`.
   Leaving a session - or being disconnected - returns the player to the Game Default
   Map. Point it at the example menu so the round trip ends where it started. The same
   applies to your own game later: its menu map belongs here.
3. Open `/EasySession/Examples/Maps/L_Example_MainMenu`.
4. Set up two players as described in [step 6](#6-test-in-pie), then press Play.
5. Host in one window, Find and Join in the other.

The widgets behind it live in `/EasySession/Examples/UI/`. `WBP_MainMenu` is the one to
read first - it uses every node in the steps below.

## 3. Host a session

In any Blueprint (a menu widget button, or the Level Blueprint for a quick test):

```
[Button Clicked] -> [Create Easy Session]
                      HostParams:
                        Session Display Name = "My First Session"
                        Map Name = "/Game/Maps/Lobby"   <- your map here
                      OnSuccess -> (you are now hosting)
                      OnFailure -> [Print String: ErrorMessage]
```

`Create Easy Session` does everything a host needs:

- Creates and advertises the session
- Travels to Map Name with `?listen` added, which is what makes this game the server
- Leaving **Map Name** empty starts listening on the current map instead
- Registers you as a player, so your session shows correct player counts

Both travel steps assume the default **Host Mode = Listen Server**. A dedicated server
keeps the map it was launched with - see the [dedicated server guide](Guide-DedicatedServer.md).

## 4. Find and join from another instance

```
[Button Clicked] -> [Find Easy Sessions]
                      OnSuccess (Results) -> [ForEach] -> add a row to your server list UI
                      OnFailure -> [Print String: ErrorMessage]

[Row Clicked] -> [Join Easy Session]
                   SearchResult = (the row's result)
                   OnSuccess -> (traveling to the host automatically)
                   OnFailure -> [Print String: ErrorMessage]
```

Every failure pin gives you a `Result` enum and a message you can show a player.

Keep the whole `SearchResult` on each row, not just the name it displays - `Join Easy
Session` needs it back.

A wrong password or a match that stopped taking players fails the node right here, with
`Result` saying which and `ErrorMessage` carrying the host's reason - no loading screen
first. See [password protected sessions](Guide-Sessions.en.md#password-protected-sessions).

## 5. Or skip all of that with Quick Match

```
[Button Clicked] -> [Quick Match Easy Session]
                      QuickMatchParams:
                        Host -> Map Name = "/Game/Maps/Lobby"   <- required
                      OnSuccess -> (joined the best session, or hosting a new one)
                      OnFailure -> [Print String: ErrorMessage]
```

Quick Match searches, joins the best session (good ping, fuller rooms first), and hosts a
new session if nothing is found. Use `Is Easy Session Host` to check which outcome you got.

**Host > Map Name has no default.** Matchmaking cannot pick where the match is played, so
leaving it empty fails immediately with `InvalidParams` rather than hosting a session
nobody can connect to. Turn off **Allow Host Fallback** if this game should only ever join.

## 6. Test in PIE

1. **Edit -> Editor Preferences -> Level Editor -> Play**: set **Number of Players = 2**
   and **Net Mode = Play Standalone**.
2. Press Play - you get two windows.
3. Host in window 1, find and join in window 2.

Turn off **Run Under One Process** in the same settings. With it on, both windows share
one process and one LAN beacon port, so they find each other only some of the time.
Separate processes use the same networking path as packaged builds.

> Tip: you can test everything without any UI using console commands (`~` key):
> `EasySession.Host`, `EasySession.Find`, `EasySession.Join 0`, `EasySession.QuickMatch`,
> `EasySession.Destroy`, `EasySession.Status`. They exist in development builds and are
> compiled out of shipping builds.

## Next steps

- [Concepts](Concepts.en.md) - what a session actually is, and what NULL and Steam mean
- [LAN setup](Setup-LAN.en.md) - what breaks local discovery, and how to test on one machine
- [Steam setup](Setup-Steam.en.md) - the two plugins and the ini block internet play needs
- [Session guide](Guide-Sessions.en.md) - custom session data, filters, passwords, updating sessions
- [Quick Match guide](Guide-QuickMatch.md) - how the matchmaking picks a session, custom scoring
- [API reference](API.en.md) - every node, query, struct and setting
- [FAQ](FAQ.en.md) - the problems people run into
