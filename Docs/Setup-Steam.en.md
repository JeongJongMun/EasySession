# Setup - Steam

*[한국어](Setup-Steam.ko.md)*

Run your sessions on Steam: internet play, friends, invites, and presence.

> Verified on UE 5.8 with two machines and two Steam accounts (sessions, search, join, passwords, invites, friends, overlay, parties).

## Prerequisites

The Steam client has to be running on **every** machine that tests, each signed in to a different account.

## 1. Enable the plugins

Enable both in Edit -> Plugins (restart required):

- **Online Subsystem Steam** - sessions, friends, invites, presence.
- **Steam Sockets** - game traffic over Steam's network (NAT traversal included).

Steam session addresses look like `steam.7656...`, and a net driver has to understand that form to connect. Steam Sockets is the one that does it.

> Older guides name `SteamNetDriver` instead. It belongs to another plugin, Socket Subsystem Steam (IP), so leave that plugin off. If a config line still points at that driver, the engine **silently** falls back to the IP driver and every join fails.

## 2. DefaultEngine.ini

Copy-paste this:

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
bInitServerOnClient=true

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="BeaconNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
```

`SteamDevAppId` is only read in Development builds. A Shipping build wants the same value in
a `steam_appid.txt` next to its executable, in `Windows\<Project>\Binaries\Win64\`. **480**
is Valve's public test AppId (Spacewar), so you can leave it as is.

The `!NetDriverDefinitions=ClearArray` line matters: the engine's base config already defines a `GameNetDriver` entry and the **first** matching entry wins, so appending without clearing leaves your Steam driver as dead config.

That same line also wipes the engine's default `BeaconNetDriver`, which is why the block
puts it back. Beacons are separate lightweight connections - the engine and other plugins
create them for lobbies, seat reservations and queries - and creating one fails outright
when no definition is left. Drop the line only if you are certain nothing in your project
uses a beacon.

> Watch the fallback when you test. If SteamSockets cannot start, the engine quietly drops
> to `IpNetDriver` instead of failing. That passes on a LAN and then breaks over the
> internet, so confirm the driver class in the log rather than trusting that it connected.

## 3. Test checklist

- Editor PIE cannot represent two Steam users. Test **on two machines with two different Steam accounts**, running a packaged build or `-game`.
- AppId 480 shares its lobby space with everyone testing on Spacewar, but the engine drops sessions whose build id differs from yours, so a search normally returns only your own. If several teams test the same build, narrow further with a custom setting (e.g. `GameName = MyGameDev`) in Host Params and the same value in Search Params `Required Custom Settings`.

### Parties on Steam

A party is a Steam lobby next to the game session, and the leader runs the party beacon on the same port as the reservation beacon. The two accounts must be Steam friends for the invite-only checks. With the console (`~`) on both machines:

1. **Invite.** A: `EasySession.CreateParty 4 invite`, then `EasySession.PartyInviteUI` and invite B. B accepts in the overlay and joins. B's log shows the leader's Steam address: `Connecting to the party leader at steam.<id>:...`.
2. **Public party.** A: `EasySession.CreateParty 4 public`. B: `EasySession.FindParties`, then `EasySession.JoinParty`.
3. **Join code.** A: `EasySession.CreateParty 4 code`, then `EasySession.Party` prints the code. B: `EasySession.FindParties <code>`, then `EasySession.JoinParty`.
4. **Follow.** A: `EasySession.Host <Map>`. B follows into A's session, and both see `OnPartyLeft` with `MovedToGameSession`.
5. **Restore.** Both leave the match. The party comes back in the menu, with B as a member again.
6. **Invite during a match.** B accepts a party invite while in a game session. B does not join, and the log says `Not joining the invited party during a game session.`

## Common pitfalls

| Symptom | Cause |
|---|---|
| `LogOnline: STEAM: Steam API failed to initialize` | Steam client not running, or no AppId found (`SteamDevAppId` missing, or `steam_appid.txt` missing in Shipping) |
| Sessions not found between two PCs | Same Steam account on both, different AppIds, or a stale build on one machine |
| Invite/overlay works but join fails with "connection to the host has been lost" | Net driver is not Steam Sockets - plugin disabled, `ClearArray` line missing, or config still names the legacy `SteamNetDriver`. Run `EasySession.Diagnose` |
| Everything works in PIE but not packaged | PIE was silently using NULL - check the log line `EasySessionSubsystem initialized. Online subsystem: STEAM` |
| An accepted invite does nothing, and the log says `search already in progress when accepting invite` | The engine ignores an invite accepted while a session search runs. Accept again once the search has finished ([FAQ](FAQ.en.md)) |
| An invite-only party refuses an invited player with "This party only admits players the leader invited." | The two accounts are not Steam friends, and the invite was not sent with Send Easy Party Invite To Friend |

When in doubt: check the startup log for the `===== EasySession diagnostics =====` block, or run `EasySession.Diagnose` at any time. It verifies the active subsystem, every ini key above, that the configured net driver class actually loads, and the Steam login state.
