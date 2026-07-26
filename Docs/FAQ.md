# FAQ & Troubleshooting

Real problems, in the order people usually hit them.

## "Find returns no sessions"

Checked in order of likelihood:

1. **Is anyone actually hosting?** The host must have completed `Create Easy Session` with `Should Advertise = true`.
2. **PIE on one machine** - set Number of Players = 2, Net Mode = *Play Standalone*. If instances still can't see each other, disable *Run Under One Process*.
3. **Firewall / VPN** - LAN discovery is UDP broadcast; allow the game in Windows Firewall, disable VPNs, or pass `-MultiHome=<LAN IP>`.
4. **Steam: same account or same-machine test** - two different Steam accounts on two machines are required. With AppId 480, filter out strangers' test sessions using a custom setting (see [Steam setup](Setup-Steam.md)).
5. **Wrong subsystem is running** - check the log: `EasySessionSubsystem initialized. Online subsystem: NULL/STEAM`. Packaged builds read the packaged ini, not your editor state.

## "Session is found, but joining times out / fails with ResolveFailure"

The session is advertised but its host is **not running as a listen server** - an advertised address with port 0. EasySession fails this immediately with `ResolveFailure` (older/other systems hang for 20 seconds instead).

Fix on the **host** side: keep `Start Listening = true` (default) in Host Params, or provide a `Map Name` so the host travels with `?listen`. If the host log shows `The session is advertised but this game is still not a listen server`, the travel failed - check the map path (`/Game/Maps/YourMap`) and, in PIE, disable *Run Under One Process*.

## "Warning: Player ... is not part of session (GameSession)" during travel

**One occurrence during client travel is normal.** The engine tries to unregister the traveling player twice (logout path + PlayerState teardown); the first succeeds, the second logs this warning. Epic's own samples show the same line. Ignore it - don't lower the `LogOnlineSession` verbosity, or you'll hide real warnings too.

## "Connected fine, but the other player doesn't move on my screen"

Not a session problem - your pawn has no client->server movement replication. The engine's `DefaultPawn` (the flying sphere you get without a GameMode) moves only locally on clients. Use an `ACharacter`-based pawn (`CharacterMovementComponent` has full networked movement built in) - e.g. add the Third Person content pack and set its GameMode on your map.

## "How is this different from the engine's built-in Create Session / Find Sessions nodes?"

The engine ships minimal session nodes (`Create Session`, `Find Sessions`, ...). They work for quick prototypes, but they call the online service directly with almost no options and **no failure reasons** (their OnFailure pin carries nothing). EasySession's nodes route through its subsystem, which adds: operation queueing (calls can never overlap and corrupt the service), automatic listen-server setup, correct player/slot accounting, instant rich errors instead of timeouts, custom session data & filters, and Quick Match matchmaking. Both node sets can coexist in a project, but mixing them on the same session is not recommended - the engine nodes bypass all of the above.

## "SessionAlreadyExists - but I don't think I'm in a session?"

You are - probably a leftover from a previous failed flow. Call `Leave Easy Session` first (safe even mid-confusion), then retry. EasySession also auto-destroys dead sessions when the network connection fails, so this mostly happens after non-network logic bugs (e.g. double hosting).

## "Can I cancel a Quick Match in progress?"

Yes: `Cancel Matchmaking` on the subsystem. The Quick Match node fires `OnFailure` with `Canceled`. In-flight online operations finish first (they cannot be aborted mid-call), so cancellation may take a moment.

## "Dedicated server target won't build"

Server targets require a **source-built engine**; the Launcher distribution cannot build them. See [the dedicated server guide](Guide-DedicatedServer.md).

## "Do I need a custom GameInstance?"

No. EasySession is a `GameInstanceSubsystem` - it exists automatically in every project. That's the point.
