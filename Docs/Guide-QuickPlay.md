# Guide — Quick Play Matchmaking

`Quick Play Easy Session` is the one-node path into a game: **search → join the best session → host a new one if nothing is found**.

## Parameters (`FEasyQuickPlayParams`)

| Field | Default | Notes |
|---|---|---|
| Search | (defaults) | Same filters as Find Easy Sessions |
| Host | (defaults) | Used when falling back to hosting |
| Allow Host Fallback | true | **Set false for dedicated-server games** — clients then only search & join, and fail with `NoSessionsFound` when empty |
| Max Search Passes | 3 | Search retries before giving up / hosting |
| Delay Between Passes | 2.0s | |

Progress can be shown by binding `OnStateChanged` on the policy (`Get Active Matchmaking Policy`): `Searching → Joining → Hosting → Complete`.

Cancel anytime with `Cancel Matchmaking` — the run finishes with the `Canceled` result.

After `OnSuccess`, use `Is Easy Session Host` to know whether you joined someone or became the host.

## How "the best session" is chosen

The default policy scores every result and joins in score order:

1. **Ping buckets** — ping is grouped into tiers (≤50ms, ≤100ms, ≤150ms, worse). A lower tier always wins. Within the same tier, a 5ms difference is ignored — it isn't meaningful.
2. **Fill ratio** — within the same tier, fuller sessions win, so matches start sooner and the player pool doesn't spread across half-empty rooms.
3. **Randomized top picks** — the best 3 candidates are shuffled, so players searching at the same moment don't all pile onto the same room and bounce off `SessionIsFull`.
4. **No dead retries** — a session that rejected a join is excluded for the rest of the run.

## Custom scoring — one function override

The scoring lives in `UEasyMatchmakingPolicy::ScoreSession`, a **BlueprintNativeEvent**. Subclass the policy (Blueprint or C++), override one function, and pass your class to Quick Play's `Policy Class` pin:

```
ScoreSession(Session) → float   // higher = joined first
```

Example — prefer sessions running your favorite mode, on top of the default behavior:

```
Override ScoreSession:
    base = Parent: ScoreSession(Session)
    if Session.CustomSettings["GameMode"] == "CTF": return base + 50
    return base
```

The ping-bucket thresholds (`Ping Buckets Ms`) and shuffle width (`Top Candidate Randomization`) are also editable defaults on your policy subclass.

## When to write your own policy vs. your own flow

- Tweaking *which* session wins → override `ScoreSession`. Done.
- Different *flow* (e.g. party-size aware retries, region failover) → you can also drive `Find` / `Join` / `Create` nodes yourself; Quick Play is a convenience, not a cage.
