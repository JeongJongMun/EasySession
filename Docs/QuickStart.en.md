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
5. Press **CREATE SESSION** and create in one window. In the other, press **FIND SESSIONS**, **SEARCH**, then **JOIN**.

The widgets behind it live in `/EasySession/Examples/UI/`. Each one does one job and calls the
nodes for that job, so you can read one on its own or copy it into your game. `WBP_MainMenu`
only lays out the screens and switches between them. The status line at its bottom is
`Modules/WBP_SessionStatus`, a widget that binds the plugin's events once and narrates
whatever runs, whoever started it. Drop it on any screen that should show session progress.

| Widget | What it does | Main nodes |
|---|---|---|
| `WBP_MainMenu` | Home, Create Session, Find Sessions, Create Party and Find Parties screens. Joins whatever the browser asks for | Start Easy Matchmaking, Create Easy Session, Join Easy Session, Create Easy Party, Join Easy Party |
| `Modules/WBP_SessionSettingsForm` | The session settings inputs, shared by Create Session and the update popup | Make / Break Easy Session Settings |
| `Modules/WBP_SessionBrowser` | Find Sessions with Public, Friends and Code tabs, and Find Parties | Find Easy Sessions, Find Easy Friend Sessions, Find Easy Parties |
| `Modules/WBP_PartyCard` | The local party: create, join by code, find, ready and leave | Join Easy Party, Set Easy Party Ready, Leave Easy Party, Get Easy Party Settings, Get Easy Party Join Code |
| `Modules/WBP_CreatePartyForm` | Max members, hidden and join code of a new party | Make Easy Party Settings |
| `Modules/WBP_PartyMemberList` | The party members. An open slot opens the invite overlay, a member opens their profile | Get Easy Party Members, Show Easy Party Invite UI, Show Easy Profile UI For Party Member, Kick Easy Party Member |
| `WBP_Lobby` | The lobby: ready, start the match, session settings, leave | Set Easy Session Ready, Start Easy Session, Server Travel Easy Session, Leave Easy Session |
| `Modules/WBP_PlayerList` | The session players, with the same open slot and profile clicks | Get Easy Session Player Infos, Show Easy Invite UI, Show Easy Profile UI For Player, Kick Easy Session Player |
| `Modules/WBP_SessionInfo` | The session settings at a glance | Get Easy Session Settings, Get Easy Session Join Code, Get Easy Session State |
| `Modules/WBP_SessionStatus` | The status line | Get Easy Session Activity, Get Activity Message, On Session Failure |
| `WBP_InGame`, `Popups/WBP_EscPopup` | The match and its Esc menu: leave, return to the lobby, end the session | Leave Easy Session, End Easy Session, Server Travel Easy Session, Destroy Easy Session For Everyone |
| `Popups/WBP_UpdateSessionPopup` | Changes the session settings from the lobby | Update Easy Session |
| `Popups/WBP_JoinPasswordPopup`, `Popups/WBP_DisconnectPopup` | The password prompt, and why the last session or party ended | Join Easy Session, Consume Pending Easy Disconnect Info (called by the main menu) |

The rest of `Modules/` are shared parts with no plugin nodes: `WBP_MenuButton`, `WBP_TabBar`,
`WBP_InfoRow`, `WBP_PopupFrame`, `WBP_PlayerRow` and the input widgets. A game without parties
can drop `WBP_PartyCard` from the main menu, and everything else keeps working.

## 3. Host a session

In any Blueprint (a menu widget button, or the Level Blueprint for a quick test):

```
[Button Clicked] -> [Create Easy Session]
                      HostParams:
                        Session Display Name = "My First Session"
                        Initial Map Name = "/Game/Maps/Lobby"   <- your map here
                      OnSuccess -> (you are now hosting)
                      OnFailure -> [Print String: ErrorMessage]
```

`Create Easy Session` does everything a host needs:

- Creates and advertises the session
- Travels to Initial Map Name with `?listen` added, which is what makes this game the server
- **Initial Map Name** is required: without a map there is no server to join, so the create fails with `InvalidParams`
- Registers you as a player, so your session shows correct player counts

Listen servers are the supported configuration. Hosting a session on a dedicated server
is not supported yet.

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

A wrong password, a full session or a match that stopped taking players fails the node right here, with
`Result` saying which and `ErrorMessage` carrying the host's reason - no loading screen
first. See [asking the host first](Guide-Sessions.en.md#asking-the-host-first).

## 5. Or skip all of that with Matchmaking

```
[Button Clicked] -> [Start Easy Matchmaking]
                      MatchmakingParams:
                        Host -> Initial Map Name = "/Game/Maps/Lobby"   <- your map here
                      OnSuccess -> (joined the best session, or hosting a new one)
                      OnFailure -> [Print String: ErrorMessage]
```

Matchmaking searches, joins the best session (good ping, fuller sessions first), and hosts a
new session if nothing is found. Use `Is Easy Session Host` to check which outcome you got.

**Leaving Host > Initial Map Name empty** makes the fallback open a listen server on the map this
player is already on. Called from a menu, that turns the menu into the arena, so fill it in
most of the time. Turn **Allow Host Fallback** on to host at all - it is off by default.

## 6. Test in PIE

1. **Edit -> Editor Preferences -> Level Editor -> Play**: set **Number of Players = 2**
   and **Net Mode = Play Standalone**.
2. Press Play - you get two windows.
3. Host in window 1, find and join in window 2.

Turn off **Run Under One Process** in the same settings. With it on, both windows share
one process and one LAN beacon port, so they find each other only some of the time.
Separate processes use the same networking path as packaged builds.

> Tip: you can test everything without any UI using console commands (`~` key):
> `EasySession.Host /Game/Maps/Lobby`, `EasySession.Find`, `EasySession.Join 0`, `EasySession.Matchmaking`,
> `EasySession.Destroy`, `EasySession.Status`. They exist in development builds and are
> compiled out of shipping builds.

## Next steps

- [Concepts](Concepts.en.md) - what a session actually is, and what NULL and Steam mean
- [LAN setup](Setup-LAN.en.md) - what breaks local discovery, and how to test on one machine
- [Steam setup](Setup-Steam.en.md) - the two plugins and the ini block internet play needs
- [Session guide](Guide-Sessions.en.md) - custom session data, filters, passwords, updating sessions
- [Matchmaking guide](Guide-Matchmaking.en.md) - how Matchmaking picks a session, custom scoring
- [API reference](API.en.md) - every node, query, struct and setting
- [FAQ](FAQ.en.md) - the problems people run into
