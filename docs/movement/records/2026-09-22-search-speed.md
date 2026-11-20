# Search speed: where a rollout's time goes, and the trace cache

Recorded 2026-09-22. The owner asked for faster whole searches without losing
quality; the first choice is exact changes, which leave every decision the same.
All worlds use the seed and the library, with one bot. Worlds are under
`.codex-research/search-speed-20260922-a/` and `.codex-research/trace-cache-20260922-{a,b}/`.

> **1. Repeated work.** Over 47 retained worlds (1,259 searches),
> `prefix_share.py` finds that 28.8 percent of candidate-loop steps are shared
> prefixes. Candidates with the same first-hop aim and run-up issue identical
> commands until their first landing. Another 56 percent of reroute-pass steps
> re-fly a flight from the root.
>
> **2. The trace cache is exact, and hits 54 to 72 percent, but saves only
> about 10 percent.** `bot_forecastTraceCache` (default off) answers a
> repeated collision trace from the first result inside one bot's Pmove
> boundary.
> - Decisions are identical on the six reference cells.
> - One bot: 358,500 of 500,586 traces hit at budget 0, and 54 percent at
>   budget 4. Forecast rollout time falls about 10 percent (557-592 ms to
>   514-517 ms a world).
> - The tail is unchanged: p99 at budget 4 is 12.7 to 13.4 ms, three frames
>   over 25 ms.
>
> **3. The rollout's time is mostly not collision.** Under a cost capture the
> forecast now profiles itself (build `row5-build-20260922-i`, accounting
> columns `fc_*`). One bot, budget 0:
>
> | Part | Share of forecast time |
> | --- | ---: |
> | Command policy (`MoveStep` each step) | 39 to 41 % |
> | Pmove (all its collision traces included) | 21 to 25 % |
> | Guard's baseline compare, whole `g_entities` `memcmp` per rollout | 18 % |
> | World copy, whole `g_entities` `memcpy` per rollout | 15.5 % |
> | Trigger test per step | 2 % |
> | Other | 1 % |
>
> The policy's cost is the walk's own corridor safety test. `WalkStep` runs
> `Bot2_GroundCorridor` at least twice on every grounded step (the steering
> chord and the momentum chord). Each call samples the ground about every 24
> units with engine traces made directly, outside the forecast's trace hook,
> so they were neither counted nor cached.

## The profile, one level down (builds j and k2)

The owner asked whether our own trace and Pmove would pay. Two profiling
builds answer it. `bg_pmove.c` got phase marks that are inert unless a
profiled rollout runs. The corridor test and the executor are timed inside
rollouts. Worlds are under `.codex-research/search-speed-20260922-b/`, read by
`profile.py`.

| Part of a rollout | Share of forecast time |
| --- | ---: |
| `Bot2_MoveControl`, the steering search: 72 angles x 9 keys, `fmodf` / trig, 6.8 us a step | about 29 % |
| Guard compare, whole `g_entities` | 16 to 17 % |
| Pmove logic, excluding its traces | about 15 % |
| World copy, whole `g_entities` | 13 % |
| Pmove's own collision traces (about 500,000 at 0.20 to 0.24 us) | about 12 % |
| Corridor test (its traces 0.27 us each, area queries 0.05 us each) | about 9 % |
| Trigger test and the rest | about 3 % |

All collision traces together, Pmove's and the corridor's, are 17 to 19
percent of rollout time. **A game-side trace would buy at most that much
speed.** Its larger value is thread safety, for the threading backup. **A
trimmed Pmove** has about 15 percent to work with. Within Pmove: setup 14 to
16 percent, the two ground traces 11 to 27 percent each, the move 23 to 27
percent, weapon 5 percent, animation 2 percent.

The largest single cost is the steering search. It is pure arithmetic with
no world access, and `tools/strafe_movecontrol_check.py` already checks a
rewrite of it bitwise against the unpruned scan. One exact reordering is
visible on inspection: for the stop key (key 8), the `angle!=0` skip runs after
two `Wrap` calls on all 72 angles, where it could run first.

## Steps 1 to 3, landed and measured (builds l and m)

All four changes are exact:

1. **The steering search, rewritten** (`bot2_movecontrol.c`, always on). It
   uses:
   - `Mod360`, which is `fmodf(x,360)` to the bit for |x| < 720, because those
     subtractions are exact (Sterbenz's lemma), falling back to `fmodf`
     otherwise;
   - the stop key's view and facing test computed once instead of wrapped at
     all 72 angles;
   - the key offsets as a table of constants;
   - ground friction and the per-key wish speeds hoisted out of the loop.

   `tools/strafe_movecontrol_check.py`, whose reference now keeps its own
   `fmodf` `Wrap`, passes: `Mod360` equals `fmodf` on 4,000,000 values and
   every boundary neighbourhood, and the controller is bitwise identical to
   the unpruned reference on 2,000,000 cases. Cost: 6.8 to 2.6-3.0 us a step.
2. **Scope reuse** (`bot_forecastScopeReuse`, default off). Inside a guard
   scope the rollout copy keeps every slot but the rollout's own, the
   per-rollout guard checks that slot, its client and `level`, and the
   whole-world compare runs once at the scope's end.
   - Verify mode (`bot_forecastScopeVerify`) runs the old full copy and guard
     beside it. It found **zero** copy and zero guard disagreements over the
     six cells and two cost worlds.
   - Lone rollouts (walk lookahead, the budget's prediction, permits, the
     parity build) had been checked by hashing the whole entity array byte by
     byte before and after. Under the same switch they now run in a scope of
     their own (`ScopedForecastRun`).
3. **Corridor memo** (`bot_moveCorridorMemo`, default off). It is keyed on
   the entity, its hull and both endpoints, and stores the verdict, the
   standing point, the refusal, the sample and `corridorHit`.
4. **The trace cache** from before, on in these arms.

**Identity.** The six cells are identical to the seed gate's worlds in every
arm: everything off (the controller alone), verify, and everything on (builds
l and m). The only difference is `traces` on `red_ret_L1_stand` in two arms,
29,940 against 32,469, the same pair seen before, which follows the world's
pre-release history.

**Cost, one bot, everything on (build m).** Forecast time per step falls
from 23.3-24.4 us to **11.6-13.6 us**.

| | Before | After |
| --- | ---: | ---: |
| Steering search | 6.8 us a step | 2.6-2.9 |
| World copy | 3.1-3.3 | 0.17-0.43 |
| Guard compare | 3.8-4.0 | 0.34-0.41 |
| Corridor test | 2.2 | 1.4-1.7 |
| Pmove | 6.5-7.0 | 5.9-7.0 |

| World | p95 | p99 | Worst | Frames over 25 ms |
| --- | ---: | ---: | ---: | ---: |
| budget 0, before (build e) | 4.6 ms | 38.2-39.1 ms | 153-165 ms | 12-14 |
| budget 0, after | 1.2 ms | **22.2-25.1 ms** | 132-135 ms | 8-9 |
| budget 4, before (build f) | 6.1-7.9 ms | 12.5-13.9 ms | 88-164 ms | 3 |
| budget 4, after | 4.7-5.6 ms | **11.1-11.6 ms** | 84-135 ms | 2-3 |

**The worst frames that remain** are whole searches nothing could prepare
for:
- the search after the fixture's shove at 2.5 s: 55 to 68 rollouts (43 to
  61 ms) plus 7 to 14 state-planner calls (38 to 71 ms; 27,500 to 49,700
  expansions, about 5 ms a call on the seed);
- the first search at spawn: 31 rollouts, 31 ms.

In the heaviest search the planner now costs as much as the rollouts. Pmove
is half of what a rollout still costs.

## Promoted (build n)

The owner promoted all three: scope reuse (with scoped lone rollouts), the
corridor memo and the trace cache are now unconditional. Their cvars
(`bot_forecastScopeReuse`, `bot_forecastScopeVerify`, `bot_moveCorridorMemo`,
`bot_forecastTraceCache`), verify mode and the runner's `--trace-cache` are
removed. The runner keeps a generic `--set CVAR=VALUE`.

The check on build `row5-build-20260922-n` (`.codex-research/search-speed-20260922-e/`):
- **Identity.** The six cells are identical to the seed gate's worlds,
  commit for commit.
- **Floor.** 36 of 36, zero deaths. Owners are `move`, plus the accepted
  `jumppad` frames.
- **Cost, one bot:**
  - budget 0: p99 **22.3 to 22.8 ms**, 8 frames over 25 ms;
  - budget 4: p99 **11.4 to 11.7 ms**, 2 frames over 25 ms.

The profiling hooks stay; they are inert outside a cost capture:
- the Pmove phase marks in `bg_pmove.c`;
- the corridor and executor timing;
- the `fc_*`, `pm_*` and `pol_*` columns.

**Next session: the state planner in the reroute pass.** In the worst
remaining frames it costs as much as the rollouts: 7 to 14 calls at about
5 ms each on the seed.

## What each exact change is worth, from these shares

1. **Memoise `Bot2_GroundCorridor` within the scope.** Its result is a pure
   function of its endpoints in a static world; the same argument holds as for
   the trace cache. The same repeats that give the trace cache its hit rate
   would answer a comparable share of the policy's 40 percent.
2. **Copy the world once per scope, not once per rollout.** Restore only the
   bot's own slot between rollouts. It is exact if a rollout writes no other
   entity. That is to be verified with a debug arm that compares the whole
   copy after every rollout. About 15 percent.
3. **Run the guard's full compare once per scope.** Keep a per-rollout check of
   the bot's own slot and `level`. It is identical unless a rollout
   contaminates the world; retained logs are to be checked for any
   contamination ever. About 18 percent.

The trace cache stays default off until those land. On its own it is worth
too little to change the default.

## What this does not establish

- The shares are from one bot at budget 0 on the seed, with profiling clock
  reads inside them.
- Items 1 to 3 are estimates from these shares, not measurements.
