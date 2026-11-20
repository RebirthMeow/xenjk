# What the frame-time tail is made of (2026-10-04)

Owner: look into the tail (p99 about 52-56 ms against the 25 ms frame; records/2026-10-04-bodyms-and-search-alone.md).
Retained-evidence analysis only, no new build: the P2-default confirmation worlds (`alone-build-20261004-b`,
`.codex-research/alone-20261004-b/default/`, frame meter on, four worlds at a time, so microseconds are indicative and
steps exact). Scripts in `.codex-research/tail-20261004-a/` (`slow.py`, `landing.py`).

## Slow bot-frames by what the bot planned in them

Every `[FRAMESLOW]` client entry over 25 ms, joined to the `[MOVE]` plan events that client printed in the same frame.
Dantooine and Kejim seeds 1-2: 4,648 slow bot-frames, 182 s, median 1,832 simulated steps.

| in the slow frame | share of slow time | steps each |
|---|---|---|
| straight search that commits (alone or with a path replan) | ~58% | 1,700-1,950 |
| straight search that finds nothing (`reason=none`) | ~18% | 1,550-1,760 |
| state-plan hop commits | ~6% | 1,750-1,950 |
| gap-edge search (with or without commit) | ~11% | 2,300-3,150 |
| climb | ~2% | 4,000-4,500 |
| path planning alone (A*) | ~3% | (not steps) |

The tail is the movement searches' rollouts, as the perf counters said; the planner itself is not.

## Most slow straight searches are not at a landing

The straight search runs at once on a landing (where a delay costs the kept speed: LESSONS, "kept speed dies at the
landing") and otherwise on a 200 ms cadence while the bot walks (`StraightEligible`). The commit line says which
(`landing=`). Of the straight searches in slow frames (Dantooine and Kejim seed 1):

| search | share of slow straight-search time | speed p25/p50/p75 |
|---|---|---|
| walking (cadence), commits | 73% | 66/333/426 |
| landing, commits | 10% | 462/660/746 |
| finds nothing (no landing field; speeds like the walking ones) | 17% | 66/355/426 |

About four fifths of the slow straight-search time, roughly two thirds of all slow-frame time, is a search the bot
starts while walking, on its cadence. A walking bot keeps walking whether its next hop is chosen on this frame or a
few frames later; no kept speed is at stake there.

## Why the early search does not cover these

The early search (`bot_moveSearchBudget`) predicts the next boundary when the last search returns: a landing, or the
cadence/retry time 200-500 ms ahead. Over that long a horizon in a 4v4 the prediction is mostly refused or overtaken
(19% served before P2; 89% of misses had no early search at all), and P2 now stands it down within 2048 u of another
player, which in a 4v4 is nearly always.

## Proposal: deferred walking search (not built)

**What:** when a walking (non-landing) straight search falls due and its cost is unknown, do not run it whole. Predict
the bot's own state K frames ahead (the walk is the forecast's exact case), spread the search's rollouts over those K
frames with the early search's chunk machinery, and at frame T+K take it through the existing exact checks (own state
and executor as predicted, candidate set rebuilt and equal, winner re-validated by one rollout from the live state);
anything refused falls back to the whole search on that frame. Landing searches stay in their frame. New cvar, default
off until measured.

**Why:** it moves about two thirds of the slow-frame time (walking searches, with the empty ones) off single frames at
the cost of a K-frame (50-100 ms) later launch from a walk, where no kept speed is lost. A short horizon keeps the
prediction fresh where the 200-500 ms early search was not.

**How:** reuse `SearchPredict`/`SearchChunk`/`SearchCacheTake` with the target set to now + K instead of the cadence
time, only for non-landing searches, and exempt from P2's player range (the stale-body window is K frames, not
hundreds of ms). K = 3 spreads a 1,800-step search to about 600 steps (~8 ms) a frame.

**Cost and risk:** decisions change (a walking hop starts K frames later, and candidates are judged against bodies up
to K frames old); a refused take still pays the whole search, so the gain depends on the take rate at short K, which
is unmeasured. Gap-edge and climb searches (~13%) and landing searches (~10% of straight time) keep the tail they have.

**Measure:** `[BUDGET]` take/refusal tallies and over-25 ms frames and p99 in one-at-a-time timing worlds (Dantooine
seeds 1-2, K = 2/3/4); then 16 paired laps worlds a map for outcomes (captures, falls, pickups), as for P2.

## Check: does a short prediction get taken more often? (owner: "ok check")

The deferred walking search is the early search's machinery with a shorter horizon, so it only helps if short
predictions are taken far more often than long ones. Build `horizon-build-20261004-a` adds a diagnostic tally to
`SearchCacheTake`, printed with the `[BUDGET]` lines: takes by the prediction's horizon (frames from prediction to
boundary), landing or cadence, and whether each was served or refused on the bot's own state/executor or on the
candidate set/context. Decision-identical: Dantooine seed 1 replays `bodyms-20261004-a/off/dan-1` exactly. Four
worlds with P2 off (`bot_moveSearchAlone 0`), early search 4, Dantooine and Kejim seeds 1-2
(`.codex-research/horizon-20261004-a/`, `horizon.py`, `horizon.txt`).

| prediction | horizon (frames) | taken | served | refused: own state | refused: candidates/context |
|---|---|---|---|---|---|
| walking (cadence) | 5-8 | 2,775 | **44%** | 25% | 27% |
| walking (cadence) | 9-12 | 3,813 | **38%** | 25% | 36% |
| landing | 13-20 | 80 | 21% | 21% | 54% |
| landing | 21+ | 3,463 | 4% | 34% | 62% |

(Walking predictions shorter than 5 frames and landing predictions shorter than 13 are too few to read: 2 and 37.)
Of 19,160 straight searches, 8,990 had no prediction ready at all.

**Result:** halving the horizon from 9-12 to 5-8 frames raises the walking take rate only from 38% to 44%. The
refusals do not shrink with the horizon the way staleness would: a quarter are the bot's own state (its walk did not go
as predicted) and a quarter to a third a changed candidate set, at either horizon. Extrapolated to 2-4 frames, a
deferred walking search would be served about half the time, and every refused one pays the spread-out work and the
whole search on its frame: at best about a third of the slow-frame time off single frames, with the mean cost up.
That is the early search's trade again. **Not recommended as proposed.** Open, if wanted: why a 5-8 frame walk
prediction diverges a quarter of the time and why the candidate set changes; a fixable cause would change this.

## Why short predictions fail, and the candidate id fix (owner list item 1)

`bot_moveSearchWhy 1` (diagnostic, default 0) prints every refused take with its first difference: context field,
player-state or executor field (with the origin and velocity difference), or candidate index and plan field, plus the
nearest other player; a tracker records the first live frame that left the predicted walk and whether the command
before it was the predicted one, and which command fields differed. Builds `why-build-20261004-a/-b/-c`, worlds
`.codex-research/why-20261004-a/-b/-c/` (`why.py`, `cmd.py`), P2 off, early search 4, Dantooine and Kejim seeds 1-2.
With the cvar on and before the fix, Dantooine seed 1 replays `bodyms-20261004-a/off/dan-1` exactly.

**Candidate refusals were bookkeeping.** All of them (51% of walking and 65% of landing refusals) differed only in
the plan's `id`, which is `spikePlans + 1` from one counter shared by every bot: any other bot's commit between the
prediction and the take changed every candidate's id. Nothing a rollout or the judge computes reads it. Fix
(`CandidateSame`, bot2_move.c): the compare leaves the id out, and a taken set is given the live id a search on that
frame would have assigned. Exact; a lone bot is unchanged (no other commit can intervene). Same four worlds:

| | before | after |
|---|---|---|
| early-search takes served | 2,829 | 6,784 |
| walking, 5-8 frames | 44% | 69% |
| landing, 21+ frames | 4% | 59% |

**What is left is behaviour.** Own-state refusals are now most of the rest (origin 56-61%, then velocity, view and
animation fields); 13-21% are the winner's re-validation. Of the own-state refusals whose divergence the tracker caught,
2,159 of 2,171 had a different live command on the frame before: the chaser, defender and camper roles aim (yaw,
pitch) and remap their move keys to keep travelling, which the forecast's movement policy does not model; the runner
had 21. Exact prediction there would need a forecast of the aim layer and its targets.

**Timing with the fix** (`.codex-research/idfix-20261004-a/`, one world at a time, Dantooine seeds 1-2):

| setting | mean ms | p99 ms | over 25 ms | early-search hits |
|---|---|---|---|---|
| early search everywhere (`bot_moveSearchAlone 0`) | 7.72 / 6.30 | 56.5 / 48.5 | 1,773 / 1,347 | 1,729 / 1,617 |
| P2 default (2048) | 6.03 / 5.51 | 54.2 / 51.0 | 1,722 / 1,545 | 131 / 98 |
| no early search (budget 0) | 5.50 / 5.38 | 51.5 / 50.0 | 1,585 / 1,535 | - |

Serving 2.2 times as many searches early still does not move the tail: over-25 ms frames and p99 stay within the
spread of no early search, and the mean is higher. P2's default stands. The fix is kept (exact, and it is what the
early search was designed to do); with P2 at 2048 it serves about 100-130 searches a world early instead of 50-70.

## Where a forecast step's time goes in the 4v4 (owner list item 2)

`bot_moveProfile 1` (diagnostic, with the frame meter on) runs the cost capture's section timers and counters in any
world and prints `[PROFILE]` beside `[FRAMECOST]` (bot2_movecost.c). Build `prof-build-20261004-b`; Dantooine seed 1
with it on replays `idfix-20261004-a/r2048/w-1` exactly. One world at a time, P2 default, Dantooine and Kejim seed 1
(`.codex-research/prof-20261004-a/`, `prof.py`, `prof.txt`). A profiled step costs 15.0-16.8 us (the per-phase clock
reads inflate it a little).

| part of a step | Dantooine | Kejim |
|---|---|---|
| Pmove | 49% (8.3 us) | 42% (6.3 us) |
| - its collision traces (cached and not) | 29% | 21% |
| walk corridor test (in the executor step) | 17% (2.9 us) | 15% (2.2 us) |
| - its traces (engine, not through the trace cache) | 14% | 10% |
| steering search (`Bot2_MoveControl`) | 12% (1.9 us) | 15% (2.2 us) |
| world copy, pre- and post-guard compare | 8% | 12% |
| trigger test | 2% | 2% |
| loop, replay matching, hashes | 5% | 7% |

Counters (Dantooine): 13.8 trace calls a step through the forecast's trace cache, 56% answered by it (6 engine traces
a step); 0.6 corridor calls a step, 28% answered by the whole-corridor memo, the rest 5.7 engine traces each, about
0.7 us a trace, none cached per trace.

**Reading:** no part of the step is large enough for one exact change to halve it. The largest exact candidate is the
corridor test's traces through the scope's trace cache (the world is frozen inside a guard scope, as for Pmove's
traces): 10-14% of a step, of which only the hit rate would be saved (Pmove's traces hit 44-56%), so about 5-7%. The
steering search was already cut from 6.8 to about 2 us (2026-09-22) and the copy/guard from 7 to under 2. Every exact
step saving found here together is perhaps 10-20%: a 50 ms search would still be about 40 ms.

## Deferred walking search (owner list item 4; owner: agree, build it)

`bot_moveWalkDefer K` (default 0: off) and `bot_moveWalkPool` (rollouts a server frame shared by every bot, default
24), bot2_move.c, committed c9e42a22. When a walking bot's cadence straight search falls due, it predicts its own walk
K frames ahead and keeps walking; the search's rollouts run from the predicted state on the frames in between out of
the shared pool (client order, deterministic); at the predicted frame the result is taken through the early search's
exact checks. A refused or unfinished result is deferred anew from where the bot is, never searched whole on that
frame. A search that takes one of its silent exits (no straight ahead, too short, a gap refusal) restarts the cadence
as a live search's exit does. Landings, planned takeoff windows, gap, climb and knock searches are unchanged; an early
search already ready for the frame is taken the same way. Off replays the previous build exactly
(`walk-20261004-a/off/w-1` against `idfix-20261004-a/r2048/w-1`).

Development worlds, one at a time, Dantooine (`walk-20261004-a/`, `walk-20261004-b/`): the first version left 67% of
deferrals unfinished (silent exits never finished; fixed), then deferred only the two runners (defenders and campers
are `MoveGuest`; the exclusion had no reason and was removed). Sweep, seed 1:

| setting | mean ms | p99 ms | worst ms | over 25 ms | slow single-bot frames |
|---|---|---|---|---|---|
| off | 5.88 | 52.8 | 146 | 1,683 | 1,389 (51.7 s) |
| K = 3, pool 24 | 6.80 | 44.0 | 108 | 1,576 | |
| K = 4, pool 12 | 6.68 | 41.0 | 120 | 1,035 | |
| K = 6, pool 12 | 6.53 | 40.8 | 117 | 958 | 543 (19.2 s) |
| K = 6, pool 8 | 6.76 | 41.8 | 111 | 1,025 | |

Seed 2, K = 6 pool 12 against off: over 25 ms 1,545 -> 1,044, p99 51.0 -> 41.0 ms, mean 5.51 -> 6.88 ms. A large pool
makes slow frames of its own (24 rollouts is itself about 25-35 ms). What is left in the slow single-bot frames is
mostly gap-edge searches (about half), then landing and fallback straight searches, climbs and state-plan hops.

**Outcomes**, K = 6 pool 12 against defaults, 16 paired worlds a map (`walk-20261004-c/`, `compare.txt`):

| | Dantooine | Kejim |
|---|---|---|
| team captures | 7.95 -> 6.85 (-1.11 +- 1.02) | 7.83 -> 6.65 (-1.17 +- 0.88) |
| camper captures | 2.28 -> 1.66 (-0.62 +- 0.41) | 3.51 -> 2.22 (-1.30 +- 0.56) |
| pickups | equivalent | equivalent |
| runners' falls | 4.01 -> 3.33 (-0.68 +- 0.70) | 7.89 -> 8.44 (+0.56 +- 0.84) |

Team captures fall on both maps (neither decided; together about 1.7 standard errors) and camper captures on both
(Kejim 2.3 se). Take rates by role: runner 70%, defender 55%, camper 41%, chaser 40% of deferrals that reached their
frame; each refused or late one walks the bot another K frames (150 ms) before its hop, and the aiming roles' own
commands are what refuse them (item 1). Default stays off.

### Options for the owner

1. **Bounded rescue of a refused take (recommended next):** when a deferral is refused on the bot's own state, run the
   ready winner alone from the live state through the judge (one candidate, its rollout and walk reference, a few
   rollouts) and commit it if admitted; only if that fails defer anew. Removes most repeated 150 ms delays for the
   aiming roles at a bounded cost. Not exact: the winner was ranked from the predicted state. Measure: take/rescue
   tallies, then the same 16-seed outcome pair.
2. **Shorter K for every bot (K = 3, pool 12):** halves each delay; refusal rates barely depend on the horizon (item 1),
   so the repeated delays remain; the tail gain at K = 3 was smaller.
3. **Keep it off:** today's tail and captures.

## Rescue of refused deferrals (owner: "do 1")

`bot_moveWalkRescue` (default 1; acts only with `bot_moveWalkDefer` on), bot2_move.c. When a deferred walking search's
take is refused on the bot's own state or executor only: a ready set with no winner is taken as no hop (the cadence's
next search follows); a ready winner from the construction is rebuilt by the same recipe (its slot in the live
construction) from the live state and judged alone - its rollout, walk references and reroute - and committed if
admitted. Anything else is deferred anew as before. Not exact (the winner was ranked from the predicted state). Build
`rescue-build-20261004-a`; with deferral off, Dantooine seed 1 replays `walk-20261004-a/off/w-1` exactly.
Research directory `.codex-research/rescue-20261004-a/` (`run.sh`, `timing.py`, `compare.txt`).

Timing, K = 6 pool 12 with rescue, one world at a time, Dantooine:

| | off (seed 1) | deferred + rescue (seeds 1 / 2) |
|---|---|---|
| mean ms | 6.03 | 5.53 / 5.06 |
| p99 ms | 56.5 | 34.2 / 33.5 |
| worst ms | 202 | 90 / 82 |
| frames over 25 ms | 1,719 (seed 2 earlier: 1,545) | 671 / 604 |

Refused deferrals fall from about 1,000 to about 150 a world; rescues: 300-340 no-hop takes and 150-200 winners judged
again a world, 76-80% of them committed. The mean is now below the default's: refused deferrals no longer repeat
their work.

Outcomes, 16 paired worlds a map against the same control (`walk-20261004-c/control`, identical code with deferral
off):

| | Dantooine | Kejim |
|---|---|---|
| team captures | 7.95 -> 7.64 (-0.31 +- 1.03) | 7.83 -> 7.58 (-0.25 +- 0.91) |
| camper captures | 2.28 -> 2.84 (+0.56 +- 0.47) | 3.51 -> 2.77 (-0.74 +- 0.50) |
| pickups | equivalent | equivalent |
| runners' falls | 4.01 -> 4.38 (+0.37 +- 0.61) | 7.89 -> 9.36 (+1.48 +- 0.90) |
| runners' weapon deaths | equivalent | equivalent |

Nothing moves beyond its spread; the capture loss of deferral without rescue is gone. Kejim's falls lean up (1.6 se)
and camper falls with them (+0.80 +- 0.78), worth a look before a default.

**Owner, 2026-10-05: on by default.** `bot_moveWalkDefer` defaults to 6 and `bot_moveWalkPool` to 12 (0 / any value
overrides); build `walkdefault-build-20261005-a`; Dantooine seed 1 with the cvars unset replays
`rescue-20261004-a/timing/w-1` (set explicitly) exactly. Not re-measured on this default: the solo bench, the level-1
and level-2 floors and gates (a lone bot's walking searches are deferred too), and Kejim's falls.

## Frame cost by team size on the default (2026-10-05)

Owner: the target is **4v4 p99 under 25 ms**; quick tests of the smaller sizes. Default build
`walkdefault-build-20261005-a`, Dantooine seeds 1-2, one world at a time, same defender arguments
(`.codex-research/sizes-20261005-a/`, `run.sh`, `sizes.py`, `sizes.txt`). "Solo" is mislabelled: `defense_laps.py`
always adds its `--roles` (default base,chase), so that row is the runner against a base defender and a chaser, three
bots (corrected 2026-10-05); 2v2 a runner and a chaser a side; 3v3 a runner, a base defender and a chaser a side; 4v4 adds a camper (the `rescue-20261004-a/timing`
worlds, identical to this default).

| size | mean ms | p99 ms | worst ms | frames over 25 ms (of ~24,600) |
|---|---|---|---|---|
| "solo" (runner vs base D + chase D) | 1.68 / 1.71 | 20.5 / 18.8 | 84 / 53 | 152 / 100 |
| 2v2 | 2.17 / 2.42 | 24.5 / 26.0 | 77 / 74 | 231 / 270 |
| 3v3 | 3.10 / 3.31 | 25.8 / 26.0 | 73 / 93 | 276 / 282 |
| 4v4 | 5.53 / 5.06 | 34.2 / 33.5 | 90 / 82 | 671 / 604 |

p99 under 25 ms means at most about 1% of frames (246) over it. The three-bot world meets it, 2v2 and 3v3 sit on the line, the 4v4 is
2.5 times over. Even one bot has 100-150 slow frames: a single search of 2,000-4,000 steps is 30-55 ms whatever the
team size. In every size the slow single-bot frames are mostly gap-edge searches (`edge`, 2,900-3,600 steps), landing
straight searches with their reroute, and climbs (about 4,100 steps each), then state-plan hops.

## The remaining slow searches on the default (2026-10-05; owner: check into them)

Slow single-bot frames (over 25 ms) joined to the search that ran in them, both 4v4 default worlds
(`rescue-20261004-a/timing`), `.codex-research/tail-20261004-a/kinds.py`:

| search in the slow frame | share of slow time | steps (median) | candidates |
|---|---|---|---|
| planned drop edge (`bot_moveDropPlan`), finds nothing | 44% | 2,900-3,200 | 29-30 |
| planned drop edge, commits | 11% | 2,600-2,800 | 26-27 |
| straight search at a landing, commits | 17% | 1,200-2,000 | 12-24 |
| climb | 8% | 4,000-4,600 | 66 |
| walking straight and state-plan searches run whole | 13% | 1,700-2,400 | 26-28 |
| gap search | 5% | 1,400 | 15 |

**Drop searches.** `DropPlanSearch` runs while the bot walks toward a ledge on its path (`PlannedEligible`, within
`MOVE_EDGE_APPROACH`): up to 5 points x 2 lines x 4 launches, each rolled out from the bot through the run-up (median
86 frames). In the 146 slow failed ones of seed 1 the candidates ended `no_path` 44% (the walk after the landing had no
path: landed off the route), `no_progress` 26%, refused 20% (a player's body 57%, a trigger 43%), `no_onward` 5%.
The failure counterfactual (`SearchFailed`, the same search with players hidden) rarely ran (about 28 rollouts a
frame, one search). Seed 1 had 1,039 failed drop searches and 152 commits in all; most failures are cheap (every
candidate refused at once) and repeat every 50 ms on two edges, which is not the tail.

**Landing searches** stay in their frame (kept speed). Letting the early search run near players again
(`bot_moveSearchAlone 0`) with deferral on halves landing slow frames but is worse overall (seed 1: 811 frames over
25 ms, p99 37.0 ms, against 671 and 34.2): P2 stands.

**Climbs** roll out all 66 script timings and keep the fewest frames that reach the floor (`ClimbSearch`). Once one
reaches, a later rollout cut at that frame count cannot win (it must be strictly fewer): an exact bound.

### Options for the owner

1. **Defer drop and other edge searches like walking searches** (largest: about 55% of slow time). The bot walks toward
   the ledge for several frames before it reaches the takeoff, so the search can run from a predicted state K frames
   ahead in pool-sized chunks, be taken through the exact checks, and be rescued by rerunning its winner from the live
   state. Needs the chunk/take machinery generalised from the straight search to `DropPlanSearch`'s loop (and the gap
   and planned-edge searches). Changes decision timing; measure as for the walking search.
2. **Climb bound** (exact, about 8% of slow time, part of it saved): stop each climb rollout at the best frame count so
   far. Decision-identical; check with `laps_identity.py`.
3. **Why drops fail** (quality and cost): most failed drop candidates land off the route or make no progress; finding
   out whether the search is offered at edges the bot is not really about to take could remove the searches outright.
4. Walking searches whose deferral prediction failed (about 400-460 a world) and state-plan takeoff windows: smaller.

## Drop deferral and the climb bound (owner: "Ok do", 2026-10-05)

Build `drop-build-20261005-a`, committed dd97cdae; research directories `.codex-research/drop-20261005-a/` (identity,
timing) and `drop-20261005-b/` (outcomes, `compare.txt`).

- **Refactor:** `DropPlanSearch` is now build / evaluate / finish (`DropBuild`, `DropEvaluate`, `DropFinish`). With
  both new cvars off, Dantooine seed 1 replays the default (`walkdefault-20261005-a/w-1`) exactly.
- **Climb bound** (`bot_moveClimbBound`, default 1): climb rollouts stop at the best frame count so far. Decision-
  identical: the game log is identical and all 161,864 decision lines match once timings and the climb line's
  admitted/refused/short counts (truncated rollouts now count as refused) are masked.
- **Drop deferral** (`bot_moveDropDefer K`, default 0): the drop search runs from a state predicted K frames ahead in
  chunks from the shared pool; at the predicted frame the winner is rebuilt by its recipe and rolled out once from the
  live state. A failure with the state as predicted is charged without the counterfactual (a contact with a player or
  missile counts as transient); anything else is no hop and is offered again after MOVE_GAP_RETRY.

Timing at K = 6, Dantooine, one world at a time:

| | default (seeds 1 / 2) | + drop deferral |
|---|---|---|
| frames over 25 ms | 656 / 604 | 413 / 352 |
| p99 ms | 34.3 / 33.5 | 28.8 / 27.5 |
| mean ms | 5.51 / 5.06 | 5.16 / 4.76 |
| worst ms | 91 / 82 | 173 / 71 |

Seed 1's 173 ms frame is a drop search run whole after its walk prediction failed (the bot had just had a jump
refused), with the failure counterfactual on top (5,720 steps); 46-50 drop predictions a world fail that way.

Outcomes, 16 paired worlds a map against the default (same build, deferral off):

| | Dantooine | Kejim |
|---|---|---|
| team captures | 7.64 -> 6.16 (-1.47 +- 0.79) | 7.58 -> 7.57 (identical worlds) |
| runner captures | 5.17 -> 4.01 (-1.16 +- 0.57) | identical |
| runners' falls | 4.37 -> 2.34 (-2.03 +- 0.57) | identical |
| runners' weapon deaths | 29.7 -> 32.5 (+2.8 +- 1.3) | identical |
| frames over 25 ms (four at a time) | 760 -> 408 | 456 -> 454 |

Kejim's worlds are unchanged: its planned edges are not drop edges, so the deferral never acts there. On Dantooine the
runners fall half as often but are shot more and capture about 1.2 less a world (about 2 standard errors): deferred
drops arrive later (each drift, refusal or late result costs at least a K-frame wait and often MOVE_GAP_RETRY), and a
runner waiting at a ledge is a target. Default stays off.

**Kejim's tail is a different search.** In four Kejim default worlds 69% of the slow single-bot time is a straight
search that finds nothing, run whole while walking, at a planned strafe edge's takeoff window (`state_candidates=4`):
`WalkDeferrable` excludes the first frame in a takeoff window, so these still run on their frame. Kejim also starves the
pool (one world: 1,723 starved chunk frames, 393 late deferrals of 7,376).

### Solo bench: drop deferral off and on (owner: judge by solo cap times, 2026-10-05)

Captures in the 4v4 also measure the defenders. The solo bench measures the runner alone: all 36 Dantooine v3 cases
(outbound and return legs, levels 1-3, stand and respawn starts), pinned classes 50 and 150, `--edges --parity 256
--linger 4 --set bot_navmeshOff=1`, drop-build-20261005-a with walk deferral and the climb bound on in both arms
(`.codex-research/dropsolo-20261005-a/`, `run.sh`, `compare.py`, `drops.py`, `compare.txt`).

- Every case passes in both arms (36/36 per class).
- 72 paired legs: 58 identical, 12 slower with deferral, 2 faster. Every difference is a stand start: +100 to +1,050
  ms (L1 stands +725 to +1,050, L2/L3 +100 to +600), two L1 stands -300 ms. Total +5.75 s over 1,073.7 s (+0.5%);
  +80 ms a leg on average, about +410 ms on the legs that changed.
- Drops: 112 whole-search commits off; 110 deferred commits on, with no refusal, drift, late or failed deferral.

Alone, the deferred drop is taken every time and costs a few hundred milliseconds on stand legs (the 150 ms deferral
and a drop chosen from the predicted state); there is no stall. The 4v4's deferred drops are different: per world about
130 taken against 186 failures charged, 80 drifted, 76 late and 10 refused, each of the last three a no-hop wait of at
least MOVE_GAP_RETRY near other players.

## Drop deferral on by default, with no ledge waits (owner, 2026-10-05)

Owner: no real solo cap-time impact and a performance saving, so default on; fix the ledge waits. Build
`drop-build-20261005-b`, `.codex-research/dropfix-20261005-a/` (`run.sh`, `compare.txt`, `laps-compare.txt`, `timing/`).

- `bot_moveDropDefer` defaults to 6 (0 turns it off).
- The take tries the best three ready recipes, best first, each rebuilt from the live state and rolled out once, and
  commits the first admitted. A stale result (changed path, drifted state with no winner, every recipe refused or no
  longer offered) defers anew on the next frame instead of waiting MOVE_GAP_RETRY; a failure with the state as
  predicted still waits it, as a live failure does.
- Identity: with deferral off the solo bench's leg times match `dropsolo-20261005-a/off-c150` in all 36 cases (some
  internal counts, straight commits and a jump-pad owner, differ in 6 cases with identical times: not yet explained);
  the 4v4 Dantooine seed 1 replays `drop-20261005-b/control/dan-1` exactly, so that control arm is reused.
- Solo bench: identical to the version before the fix (no stale result occurs alone): 36/36 a class, total +0.5%.

4v4 outcomes against the same control, 16 paired worlds a map:

| Dantooine | before the fix | with the fix |
|---|---|---|
| team captures | -1.47 +- 0.79 | -0.42 +- 0.99 |
| runner captures | -1.16 +- 0.57 | -0.73 +- 0.69 |
| runners' weapon deaths | +2.82 +- 1.29 | +1.09 +- 1.02 (equivalent) |
| runners' falls | -2.03 +- 0.57 | -0.80 +- 0.53 |

Kejim, where the deferral never acts, moves +0.26 +- 0.90 team captures (the run-to-run spread). Per Dantooine world
the deferral still ends 159 deferrals drifted and 84 late (now re-deferred at once) and charges 339 failures.

Timing: the campaign's own frame figures are not usable (Kejim, unchanged code, rose from 456 to 637 frames over 25 ms
and wall time a world from 120 to 139 s: the machine was slower). One world at a time in one session, Dantooine: off
854 frames over 25 ms, p99 38.75 ms, mean 6.08 (seed 1); default 537 / 543, p99 32.0 / 31.0, mean 5.51 / 5.55 (seeds
1 / 2). The machine is slower than earlier sessions (off seed 1 was 656 and 34.25 ms on 2026-10-05 04:20), so compare
within the session only.
