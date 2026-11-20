# Pricing the route by flight cost: refuted, with a mechanism

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The pilot supports keeping this particular change reverted, not a general
> refutation of route pricing or all other continuation strategies. The code
> inspection showing that the state planner already uses carried speed remains
> useful. Small unpinned cohorts and correlations do not establish universal causal
> claims. The current next action is pinned confirmation of the provisional
> exit-speed setting, followed by reassessment of continuation ranking.

Recorded 2026-09-20. Builds `.codex-research/route-price-build-20260920-a`
and `-b`; evidence `.codex-research/route-price-20260920-a` and `-b`. The
source change is **reverted**; the working tree is hash-verified back to the
reference manifest (`bot2_move.c` sha256 `93ab9285...`). Nothing is installed
and no flag survives.

## First, a correction

[The trigger classification record](2026-09-20-trigger-classification.md)
opened "price the route by what it costs to fly, not to walk" and attributed
the walk model to *the planner*. That was too broad and is withdrawn.

Measured across the eight retained confirmation worlds: all **46** in-leg
replans came from `Graph_PlanState`; `Graph_Path` never ran. The state planner
prices every link from the speed actually carried (`StateLink` over
`StateWalk`), not at 250. **The live planner already prices by flight.**

The walk model survives in `PathRemainingMs`, which only the **reroute gate**
uses. That gate is real and the mis-pricing in it is real: `RerouteFrom` plans
the replacement with `Graph_PlanState` and gets its flight-priced `costMs`,
and `StraightJudge` then discards that number and re-prices the same cells with
`PathRemainingMs`. `RerouteInstall` has been logging both side by side all
along: **`cost_ms=10142` against `walk_ms=25335`**. Measured live, the walk
model claimed about **31 seconds** of route remaining on a leg that finished in
**16,450 ms**.

A second hypothesis from that record is also refuted. It predicted that
tap-now candidates fail when the predicted reach `0.695v + 76` overruns the
straight ahead. Over 162 searches the correlation runs the other way: where
the reach fits the straight, tap-now is admitted **28 %** of the time; where it
overruns, **55 %**, and every candidate in the `> 2.5x` bucket was admitted.

## The change

`bot_moveRoutePrice`, default off. `PathRemainingFlightMs` mirrors
`PathRemainingMs` exactly and substitutes one thing: each cell is priced by
`path->speeds[]`, the speed the state planner expects there, instead of 250.
Only a state-planned path is priced this way; the per-edge term is untouched,
so the speed is the single variable. The gate reads it on **both** sides.
Ranking, admission and every safety check are unchanged.

`bot_moveRerouteGain`, default `MOVE_REROUTE_GAIN` (250), was added in build
`-b` after the first arm, to separate the pricing from the fixed margin.

**The control is exact.** With the flag unset, two of three runs reproduce
retained worlds `confirm-1-r7` and `confirm-1-r6` byte for byte, same
`trace_sha256`. The flag is inert when off.

## The pilot, declared before it ran: three runs per arm, alternating

| Run | price | gain | Leg | `slower_route` | `rerouted` | Trace |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| off-r1 | 0 | 250 | 16,450 | 6 | 13 | = `confirm-1-r7` |
| off-r2 | 0 | 250 | 16,450 | 6 | 13 | = `confirm-1-r6` |
| off-r3 | 0 | 250 | 17,200 | 0 | 7 | new |
| on-r1 | 1 | 250 | 16,375 | 4 | 1 | new |
| on-r2 | 1 | 250 | 16,375 | 4 | 1 | new |
| on-r3 | 1 | 250 | 19,700 | 19 | 0 | new |
| gain0-r1 | 1 | 0 | 19,700 | 19 | 0 | new |
| gain0-r2 | 1 | 0 | 19,700 | 19 | 0 | new |
| gain0-r3 | 1 | 0 | 19,700 | 19 | 0 | new |

Off: 16,450 / 16,450 / 17,200. On: 16,375 / 16,375 / 19,700. The median moves
75 ms and the tail moves 2,500 ms the wrong way. Admitted reroutes collapse
from 13 to 0-1.

Removing the margin entirely **does not bring them back**: all three `gain0`
runs are identical at 19 `slower_route` and 0 `rerouted`. So the fixed 250 ms
margin is not the binding constraint, and the confound it was added to test is
not the explanation.

## Why it fails, which is the useful part

Under flight pricing, `routeMs` exceeds `baseRouteMs` structurally, whatever
the margin. The two terms of the comparison are not the same kind of number:

- The candidate's first stretch is charged its **measured** flight,
  `done * 8 ms`, from a native rollout that actually happened.
- The incumbent is charged the **planner's expectation** for the equivalent
  ground, at the optimistic speeds `speeds[]` holds.

So the candidate pays what it really costs and the incumbent pays what it hopes
to cost. Pricing the incumbent at the walk's 250 was pessimistic, and that
pessimism was **compensating** for the asymmetry. Correcting one half of a
two-sided error made the comparison worse, reliably: three identical runs on
the slow outcome with every reroute refused.

**The walk model in this gate is not a defect on its own.** It is a bias that
offsets another bias, and it cannot be removed without giving the incumbent a
measured flight over the same stretch - which the gate has no way to obtain,
because the incumbent is not rolled out.

## What this retires

- **Substituting flight pricing into the reroute gate.** Refuted on this case:
  worse tail, reroutes eliminated, and a mechanism that says the approach is
  wrong in kind rather than in tuning. Do not retry it without first making
  the two sides of the comparison the same kind of number.
- **"The planner prices routes at walk speed."** It does not; `Graph_PlanState`
  produced all 46 in-leg routes and prices links from carried speed.
- **The reach-versus-straight rule** for tap-now refusals: contradicted by the
  data, 55 % admitted when the reach overruns against 28 % when it fits.

## Method notes for whoever is next

- `bot2_move.c` has **mixed line endings within the one file**: 3,473 CRLF and
  115 lone LF. A Python rewrite with `newline="\\n"`, or a blanket
  `\\n` -> `\\r\\n` repair, changes every byte and breaks the build's source
  binding while the content is identical. Restore from
  `.codex-research/short-runup-20260920-a/baseline-source` and check the hash
  against the build manifest; do not normalise.
- Read cvars once per search into locals, the way `RerouteFlags` and
  `TapOnward` are read at the top of `StraightJudge`. The reverted change
  called `RoutePrice()` per candidate, which is against the project's pattern
  even though the off arm still reproduced retained traces exactly.
- Three runs an arm cannot resolve this case. The within-arm spread reached
  3,325 ms while the between-arm difference was 75 ms at the median. This
  pilot is only strong enough to refuse, which is what it did; retaining
  anything here would need the eight counterbalanced pairs and the floors.
