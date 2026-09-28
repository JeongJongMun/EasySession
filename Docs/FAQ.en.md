# FAQ & Troubleshooting

*[한국어](FAQ.ko.md)*

Real problems, in the order people usually hit them.

## "Find returns no sessions"

Checked in order of likelihood:

1. **Is anyone actually hosting?** The host must have completed `Create Easy Session` with `Should Advertise = true`.
2. **PIE on one machine** - set Number of Players = 2, Net Mode = *Play Standalone*, and turn *Run Under One Process* off. Sharing one process means sharing the LAN beacon port, which makes discovery unreliable.
3. **Firewall / VPN** - LAN discovery is UDP broadcast; allow the game in Windows Firewall, disable VPNs, or pass `-MultiHome=<LAN IP>`.
4. **Steam: same account or same-machine test** - two different Steam accounts on two machines are required. With AppId 480, filter out strangers' test sessions using a custom setting (see [Steam setup](Setup-Steam.en.md)).
5. **Wrong subsystem is running** - check the log: `EasySessionSubsystem initialized. Online subsystem: NULL/STEAM`. Packaged builds read the packaged ini, not your editor state.

## "Session is found, but joining times out / fails with ResolveFailure"

The session is advertised but its host is **not running as a listen server** - an advertised address with port 0. EasySession fails this immediately with `ResolveFailure` instead of waiting for a connection timeout, and the joining player's log carries the reason:

```
LogEasySession: Warning: Session request failed: ResolveFailure (The host address 'steam.0:0' is not connectable.
The host is not running as a listen server, because its travel to Initial Map Name did not open one.
Check the map path on the host.)
```

Fix on the **host** side: the host travels to `Initial Map Name` with `?listen`, and that travel is what opens the server. If the host still is not a server, the travel failed - check the map path (`/Game/Maps/YourMap`) and, in PIE, that *Run Under One Process* is off.

## "The session shows a free slot, but joining fails with JoinSessionFull"

Another player was approved a moment earlier and is still loading the map. The host holds a reservation for them, but search results count only the players already in the session, so the slot they reserved still shows as free. The host's log names the refusal:

```
LogEasySession: Warning: Reservations: refusing '<player id>' - the session is full.
```

Nothing needs fixing. Once that player arrives, the next search shows the session as full. If they never arrive, the host removes the reservation after 45 seconds, the engine's `TravelSessionTimeoutSecs` ([Reservations](Guide-Sessions.en.md#reservations)). Matchmaking moves on to its next candidate by itself.

## "Steam: only the first player can join, everyone after that fails"

Your Host Params have **Allow Join In Progress off**. On Steam, leave it on.

The log can look like this:

```
LogEasySession: Joining session 'My Session' hosted by 'HostPlayer'
LogOnline: Warning: OSS: Async task 'FOnlineAsyncTaskSteamJoinLobby bWasSuccessful: 0 Session: GameSession LobbyId: Lobby[0x18600003DDB1FE9] Result: '3' k_EChatRoomEnterResponseNotAllowed (General Denied - You don't have the permissions needed to join the chat)' failed in 0.228409 seconds
LogEasySession: Warning: Session request failed: JoinFailure (The online subsystem failed to join the session.)
```

Steam sessions are lobbies, and the engine recomputes whether the lobby accepts players every time someone joins or leaves it. That check multiplies the free-slot test by `bAllowJoinInProgress`, without looking at whether the match has started:

```cpp
// OnlineSessionAsyncLobbySteam.cpp, FillMembersFromLobbyData
bool bLobbyJoinable = Session.SessionSettings.bAllowJoinInProgress && (LobbyMemberCount < MaxLobbyMembers);
```

So with the setting off, the first join closes the lobby and nothing reopens it - not a player leaving, not `End Easy Session`. A newly created lobby is open because the create path never runs this check, which is why exactly one player gets in. Invited friends are refused as well, since a closed lobby refuses everyone.

EasySession does not work around this: it passes the setting to the online service as given, and refuses join-in-progress itself through the reservation beacon and `PreLogin`, which do check the match state. If you need "no joining once the match starts" on Steam, leave Allow Join In Progress on and let those checks do it.

A player whose join fails this way is sent back to the main menu when joining made them leave a session first, so they do not end up in a map with no session.

## "I accepted an invite and nothing happened"

You were already in a session, and **Accept Invites While In Session is off** - the default. The invite is not joined, and the log says so:

```
LogEasySession: Warning: Not joining the invited session: this player is already in one, and Accept Invites While In Session is disabled.
```

Accepting an invite is one click in the platform overlay, and joining would destroy the session this player is in - disconnecting everyone else when they were hosting it. So the default is to do nothing and let the game decide.

`OnSessionInviteAccepted` still fires, so bind it and ask the player first, then call `Join Easy Session` yourself. To go back to joining immediately, turn on **Accept Invites While In Session** in Project Settings -> Plugins -> EasySession.

## "Warning: Player ... is not part of session (GameSession)" during travel

**One or two of these during client travel are normal, and they come from the engine.** When the client leaves its previous map, each `APlayerState` in that map is destroyed and tries to take the local player out of a session the online service cannot find them in. Ignore it - don't lower the `LogOnlineSession` verbosity, or you'll hide real warnings too.

## "Connected fine, but the other player doesn't move on my screen"

Not a session problem - your pawn has no client->server movement replication. The engine's `DefaultPawn` (the flying sphere you get without a GameMode) moves only locally on clients. Use an `ACharacter`-based pawn (`CharacterMovementComponent` has full networked movement built in) - e.g. add the Third Person content pack and set its GameMode on your map.

## "How is this different from the engine's built-in Create Session / Find Sessions nodes?"

The engine ships minimal session nodes (`Create Session`, `Find Sessions`, ...). They work for quick prototypes, but they call the online service directly with almost no options and **no failure reasons** (their OnFailure pin carries nothing). EasySession's nodes route through its subsystem, which adds: request queueing (EasySession's own calls can never overlap and corrupt the service), automatic listen-server setup, correct player/slot accounting, instant rich errors instead of timeouts, custom session data & filters, and Matchmaking.

Pick one set and stay with it. The queueing only covers calls that go through EasySession, so an engine node running at the same time still reaches the service on its own - see the entry below.

## "Another session search is already running, so this one was dropped"

Something outside EasySession has a search running, and the online service only handles one at a time. It drops the second request and reports success, so nothing would ever call back - EasySession notices and fails immediately instead of leaving the node hanging.

Usually the other search is the engine's built-in `Find Sessions` node still wired up in an old widget, another session plugin, or session code left over from before you added EasySession. Search your project for direct `FindSessions` callers and route them through `Find Easy Sessions`, or make sure the two never run at once.

Calling `Find Easy Sessions` several times in a row is not the problem - EasySession queues its own requests and runs them in order.

## "SessionAlreadyExists - but I don't think I'm in a session?"

You are - probably a leftover from a previous failed flow. Call `Destroy Easy Session` first (safe even mid-confusion), then retry. EasySession also auto-destroys dead sessions when the network connection fails, so this mostly happens after non-network logic bugs (e.g. double hosting).

## "Can I cancel a Matchmaking in progress?"

Yes: `Cancel Easy Matchmaking`. The Matchmaking node fires `OnFailure` with `Canceled`. A request already at the online subsystem finishes first (it cannot be canceled mid-call), so cancellation may take a moment.
