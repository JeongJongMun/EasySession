# Guide - Matchmaking

*[한국어](Guide-Matchmaking.ko.md)*

`Start Easy Matchmaking` is the one-node path into a game: **search -> join the best session -> host a new one if nothing is found**.

## Parameters (`FEasyMatchmakingParams`)

| Field | Default | Notes |
|---|---|---|
| Search | (defaults) | Same filters as Find Easy Sessions |
| Host | (defaults) | Used when falling back to hosting. Initial Map Name is covered below |
| Allow Host Fallback | false | The default only searches and joins, failing with `NoSessionsFound` when nothing is there, or with the search's own result such as `SearchFailure` when the search itself failed. Turn it on to host instead |
| Max Search Passes | 3 | How many search passes to run before giving up or hosting. 3 means three searches |
| Delay Between Passes | 2.0s | How long to rest before the next search |
| Join Password | (empty) | Sent when joining a password protected candidate. Without one, protected sessions are never candidates |

### Host > Initial Map Name

Matchmaking takes the same host params `Create Easy Session` takes, and the host fallback needs
Initial Map Name for the same reason: the travel to that map is what opens the listen server.
With `Allow Host Fallback` on and Initial Map Name empty, matchmaking fails with `InvalidParams`
before the first search.

`Allow Host Fallback` is off by default, so Initial Map Name does not matter until you turn it on.
The bundled example leaves it off and only searches and joins.

When the fallback does host, it inherits the search's filters: the session is created on
the network the search looked at (`LAN Query`), and every `Required Custom Settings` pair
is advertised on it, overwriting the same key in Host > Custom Settings. A searched
`Region` is advertised the same way. The session a run opens is one its own search would
have found, so Host `Password`, `Hidden` and `Should Advertise` do not apply to it: the
fallback always opens a public session.

### Matchmaking one specific session

The targeted queries `Find Easy Sessions` takes work here too: set `Search > Join Code`
(or, from C++, a friend or owner) and the passes hunt for that one session -
hidden sessions included - joining it the moment it appears. `Join Password` rides along
for password-protected sessions. With `Allow Host Fallback` off, this is "keep trying to get into
my friends' session" in one call.

A run holds the session queue from its first search to its last sub-request. A Create, Join or
Find the game asks for meanwhile runs after the run ends, and an accepted invite cancels
a run that is still searching instead of waiting for it.

Progress comes from four events on the subsystem itself, so a widget can bind once,
before any run exists:

- `OnMatchmakingStarted` - a run was accepted; from here `Get Active Easy Matchmaking Policy` returns it
- `OnMatchmakingStateChanged` (`OldState`, `NewState`) - every state transition
- `OnMatchmakingUpdated` (`State`, `ElapsedSeconds`) - every state change plus once a second while the run is active. Enough for a "Searching... 0:42" label without a timer of your own
- `OnMatchmakingComplete` (`Result`, `ErrorMessage`) - the run ended, however it ended. A canceled run arrives here with `Result` = `Canceled`; there is no separate cancel event

They always arrive as Started first and Complete last, a run refused at the door
included. The policy object has no events of its own; these four are the only ones.

The states are `Searching`, `Joining`, `Hosting` and `Complete`. They are not a straight line: finding candidates moves to `Joining`, and having them all refuse comes back to `Searching` for the next pass. `Hosting` only shows up once the passes run out and this player creates the session.

Cancel with `Cancel Easy Matchmaking` while the run is `Searching` - the run finishes with the `Canceled` result. The search stops at once; if the online service cannot stop it, it finishes in the background and a new search queues behind it. While the run is `Joining` or `Hosting` the call does nothing: a join or a host tells the group to follow and destroys the session this player was in, which cannot be undone. Turn your cancel button off in those two states. A run whose joins all fail returns to `Searching`, where it can be canceled again.

An invite the player accepts during a run follows the same rule. While the run is `Searching` it is canceled and the invite is joined. While it is `Joining` or `Hosting` the invite is not joined, and `OnSessionFailure` tells the player to accept it again. To decide this yourself, for example with a confirm dialog, turn **Auto Join Accepted Invites** off, read `Get Easy Matchmaking State` in `OnSessionInviteAccepted`, and call `Cancel Easy Matchmaking` and `Join Easy Session` with the event's result.

After `OnSuccess`, use `Is Easy Session Host` to know whether you joined someone or became the host.

### Matchmaking with a group

Two players bring others along when they matchmake:

- **A party leader.** The run only considers sessions with room for the whole party, and the party follows into the session the run joins or hosts. A party member who calls Start Easy Matchmaking is refused with `InParty` ([Party guide](Guide-Party.en.md)).
- **The host of a session whose match has not started**, such as a lobby. The run only considers sessions with room for every player in it, and they follow. The host fallback is skipped, because the host is still in its own session, so a run that finds nothing completes with `NoSessionsFound` ([Sessions guide](Guide-Sessions.en.md#bringing-the-other-players)).

## How "the best session" is chosen

The default policy narrows the search results down to candidates, then scores those and joins in score order.

**Left out of the candidates**

- **Password protected sessions** - skipped, unless this run carries a `Join Password` to offer.
- **Sessions that already refused** - a session that rejected a join is never tried again for the rest of the run.

**How the rest are ordered**

1. **Ping buckets** - ping is grouped into tiers (50ms or better, 100ms or better, 150ms or better, worse). A lower tier always wins.
2. **Fill ratio** - within the same tier, fuller sessions win, so matches start sooner and the player pool doesn't spread across half-empty sessions.
3. **Full sessions go last** - a session with no open slot takes a large penalty and always sorts last, but it is not dropped. The player count is a snapshot from the search, so someone may have left since - worth one knock before hosting a second session. The count also leaves out the reservations of players still loading, so a session that shows a free slot can still refuse with `JoinSessionFull`, and the run then tries the next candidate.
4. **Randomized top picks** - once ordered, the best 3 candidates are shuffled, so players searching at the same moment don't all pile onto the same session and bounce off `JoinSessionFull`.

## Custom scoring - one function override

The scoring lives in `UEasyMatchmakingPolicy::ScoreSession`, a **BlueprintNativeEvent**. Subclass the policy (Blueprint or C++), override one function, and pass your class to Matchmaking's `Policy Class` pin:

```
ScoreSession(Session) -> float   // higher = joined first
```

Example - prefer sessions running your favorite mode, on top of the default behavior:

```
Override ScoreSession:
    base = Parent: ScoreSession(Session)
    if Session.CustomSettings["GameMode"] == "CTF": return base + 50
    return base
```

The ping-bucket thresholds (`Ping Buckets Ms`) and shuffle width (`Top Candidates To Shuffle`) are also editable defaults on your policy subclass.

## When to write your own policy vs. your own flow

- Tweaking *which* session wins -> override `ScoreSession`. Done.
- Different *flow* (e.g. party-size aware retries, region failover) -> you can also drive `Find` / `Join` / `Create` nodes yourself; Matchmaking is a convenience, not a cage.
