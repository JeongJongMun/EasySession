# Guide - Quick Match Matchmaking

*[한국어](Guide-QuickMatch.ko.md)*

`Quick Match Easy Session` is the one-node path into a game: **search -> join the best session -> host a new one if nothing is found**.

## Parameters (`FEasyQuickMatchParams`)

| Field | Default | Notes |
|---|---|---|
| Search | (defaults) | Same filters as Find Easy Sessions |
| Host | **Map Name required** | Used when falling back to hosting. See below |
| Allow Host Fallback | true | **Set false for dedicated-server games** - clients then only search & join, and fail with `NoSessionsFound` when empty |
| Max Search Passes | 3 | How many search passes to run before giving up or hosting. 3 means three searches |
| Delay Between Passes | 2.0s | How long to rest before the next search |

### Host > Map Name is required

Matchmaking cannot pick where the match is played, so this one has no default. Hosting
with an empty Map Name skips the travel, and that travel is what turns the host into a
listen server - the session would be advertised with nobody able to connect. Quick Match
refuses to start in that case and fails with `InvalidParams`.

That check only runs while `Allow Host Fallback` is on. Turn it off if this game should
only ever join, and an empty Map Name is then fine.

Progress can be shown by taking the policy from `Get Easy Session Subsystem` -> `Get Active Matchmaking Policy` and binding its `OnStateChanged`.

The states are `Searching`, `Joining`, `Hosting` and `Complete`. They are not a straight line: finding candidates moves to `Joining`, and having them all refuse comes back to `Searching` for the next pass. `Hosting` only shows up once the passes run out and this player creates the session.

Cancel anytime with `Cancel Easy Matchmaking` - the run finishes with the `Canceled` result.

After `OnSuccess`, use `Is Easy Session Host` to know whether you joined someone or became the host.

## How "the best session" is chosen

The default policy narrows the search results down to candidates, then scores those and joins in score order.

**Left out of the candidates**

- **Password protected sessions** - Quick Match has no password to offer, so it skips them.
- **Sessions that already refused** - a session that rejected a join is never tried again for the rest of the run.

**How the rest are ordered**

1. **Ping buckets** - ping is grouped into tiers (50ms or better, 100ms or better, 150ms or better, worse). A lower tier always wins.
2. **Fill ratio** - within the same tier, fuller sessions win, so matches start sooner and the player pool doesn't spread across half-empty rooms.
3. **Full sessions go last** - a session with no open slot takes a large penalty and always sorts last, but it is not dropped. The player count is a snapshot from the search, so someone may have left since - worth one knock before hosting a second session.
4. **Randomized top picks** - once ordered, the best 3 candidates are shuffled, so players searching at the same moment don't all pile onto the same room and bounce off `JoinSessionFull`.

## Custom scoring - one function override

The scoring lives in `UEasyMatchmakingPolicy::ScoreSession`, a **BlueprintNativeEvent**. Subclass the policy (Blueprint or C++), override one function, and pass your class to Quick Match's `Policy Class` pin:

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
- Different *flow* (e.g. party-size aware retries, region failover) -> you can also drive `Find` / `Join` / `Create` nodes yourself; Quick Match is a convenience, not a cage.
