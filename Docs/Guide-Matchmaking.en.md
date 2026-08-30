# Guide - Matchmaking

*[한국어](Guide-Matchmaking.ko.md)*

`Start Easy Matchmaking` is the one-node path into a game: **search -> join the best session -> host a new one if nothing is found**.

## Parameters (`FEasyMatchmakingParams`)

| Field | Default | Notes |
|---|---|---|
| Search | (defaults) | Same filters as Find Easy Sessions |
| Host | (defaults) | Used when falling back to hosting. Map Name is covered below |
| Allow Host Fallback | false | The default only searches and joins, failing with `NoSessionsFound` when nothing is there. Turn it on to host instead |
| Max Search Passes | 3 | How many search passes to run before giving up or hosting. 3 means three searches |
| Delay Between Passes | 2.0s | How long to rest before the next search |
| Join Password | (empty) | Sent when joining a password protected candidate. Without one, protected sessions are never candidates |

### Whether to fill in Host > Map Name

Matchmaking takes the same host params `Create Easy Session` takes. Leaving Map Name empty
makes the host fallback open a listen server on the map this player is already on. It is
not refused.

**Fill it in anyway, most of the time.** Matchmaking usually sits on a menu widget, and an
empty Map Name there turns the menu into the arena: a player who asked to find a game ends
up receiving strangers in their own menu.

`Allow Host Fallback` is off by default, so Map Name does not matter until you turn it on.
The bundled example leaves it off and only searches and joins.

When the fallback does host, it inherits the search's filters: the session is created on
the network the search looked at (`LAN Query`), and every `Required Custom Settings` pair
is advertised on it, overwriting the same key in Host > Custom Settings. A searched
`Region` is advertised the same way. The room a run opens is one its own search would
have found.

### Matchmaking one specific room

The targeted queries `Find Easy Sessions` takes work here too: set `Search > Join Code`
(or, from C++, a session id, friend or owner) and the passes hunt for that one room -
hidden sessions included - joining it the moment it appears. `Join Password` rides along
for protected rooms. With `Allow Host Fallback` off, this is "keep trying to get into
my friends' room" in one call.

Progress comes from four events on the subsystem itself, so a widget can bind once,
before any run exists:

- `On Matchmaking Started` - a run was accepted; from here `Get Active Matchmaking Policy` returns it
- `On Matchmaking State Changed` (`OldState`, `NewState`) - every state transition
- `On Matchmaking Updated` (`State`, `ElapsedSeconds`) - every state change plus once a second while the run is active. Enough for a "Searching... 0:42" label without a timer of your own
- `On Matchmaking Complete` (`Result`, `ErrorMessage`) - the run ended, however it ended. A canceled run arrives here with `Result` = `Canceled`; there is no separate cancel event

They always arrive as Started first and Complete last, a run refused at the door
included. The policy object still exposes `OnStateChanged` and `OnUpdated` for code
that holds a specific run.

The states are `Searching`, `Joining`, `Hosting` and `Complete`. They are not a straight line: finding candidates moves to `Joining`, and having them all refuse comes back to `Searching` for the next pass. `Hosting` only shows up once the passes run out and this player creates the session.

Cancel anytime with `Cancel Easy Matchmaking` - the run finishes with the `Canceled` result. A join or host that succeeds after the cancel is undone.

After `OnSuccess`, use `Is Easy Session Host` to know whether you joined someone or became the host.

## How "the best session" is chosen

The default policy narrows the search results down to candidates, then scores those and joins in score order.

**Left out of the candidates**

- **Password protected sessions** - skipped, unless this run carries a `Join Password` to offer.
- **Sessions that already refused** - a session that rejected a join is never tried again for the rest of the run.

**How the rest are ordered**

1. **Ping buckets** - ping is grouped into tiers (50ms or better, 100ms or better, 150ms or better, worse). A lower tier always wins.
2. **Fill ratio** - within the same tier, fuller sessions win, so matches start sooner and the player pool doesn't spread across half-empty rooms.
3. **Full sessions go last** - a session with no open slot takes a large penalty and always sorts last, but it is not dropped. The player count is a snapshot from the search, so someone may have left since - worth one knock before hosting a second session.
4. **Randomized top picks** - once ordered, the best 3 candidates are shuffled, so players searching at the same moment don't all pile onto the same room and bounce off `JoinSessionFull`.

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

The ping-bucket thresholds (`Ping Buckets Ms`) and shuffle width (`Top Candidate Randomization`) are also editable defaults on your policy subclass.

## When to write your own policy vs. your own flow

- Tweaking *which* session wins -> override `ScoreSession`. Done.
- Different *flow* (e.g. party-size aware retries, region failover) -> you can also drive `Find` / `Join` / `Create` nodes yourself; Matchmaking is a convenience, not a cage.
