# Frame cost: what the 4v4 tail is made of now, and the pool at the frame's end (2026-10-06)

Handoff item (owner, 2026-10-06: frame cost next). Target: 4v4 p99 under 25 ms, at most about 246 frames a world
over 25 ms. Timing worlds run one at a time, Dantooine v3 and Kejim seeds 1-2, the usual 4v4 laps options with the
frame meter (`bot_moveFrameMeter 1`, `bot_moveFrameMeterSlow 25000`). Research directory
`.codex-research/frame-20261006-a/` (`timing.sh`, `src.sh`, `pool.sh`, `frames.py`, `src.py`).

## Where HEAD stands

Build `meleedefault-build-20261006-a` (a321a3ef), `timing/`:

| world | p99 ms | frames over 25 ms | worst ms |
| --- | --- | --- | --- |
| Dantooine 1 / 2 | 32.5 / 33.5 | 645 / 676 | 90 / 95 |
| Kejim 1 / 2 | 28.5 / 25.75 | 366 / 261 | 244 / 285 |

## What the slow frames are made of

`[FRAMESLOW]` is a whole frame (every bot's movement time summed). Diagnostic `[FRAMESRC]` (b6aaaa6f, build
`framesrc-build-20261006-a`): on each such frame, each bot's forecast steps by boundary section and its replan
microseconds. Decision-identical: Dantooine seed 1 replays `timing/dan-1` exactly (game log and 141,960 decision
lines). `src.py` splits each slow frame's time over its bots by their measured microseconds, a bot's share over its
planner time and then its step sources by steps (`src/`, two worlds a map; two Kejim frames of 6.5-6.8 s with 344
steps are machine stalls and are left out):

| source | Dantooine | Kejim |
| --- | --- | --- |
| deferred searches' chunks out of the shared pool (`budget`) | 33.0% | 29.8% |
| whole straight searches (landings, mostly) | 35.7% | 23.3% |
| planned edges: drops, climbs (`planned`) | 17.6% | 0.0% |
| gap searches | 11.2% | 5.5% |
| the state planner (replans) | 1.0% | 39.9% |
| slow frames, two worlds | 1,207 | 467 |

- **The pool.** Pool work is in 917 of Dantooine's 1,207 slow frames (387 of Kejim's 467), a median of about 600
  steps (12 ms) of one bot's deferred search; 640 Dantooine frames (267 Kejim) would be under 25 ms without it. The
  pool hands out 12 rollouts a server frame in client order, wherever the frame's other searches fall.
- **The planner** is a Kejim matter: Dantooine's replans never exceed 11 ms; Kejim has about 61 a world over 25 ms
  (up to 179 ms): cross-map state searches of about 28,000 expansions (spawn to the enemy flag: the straight-line
  heuristic at 450 u/s is about half a 25 s route's cost), and a few short routes of 100-200 expansions that still
  take 110-125 ms (unexplained).

## The pool at the frame's end (`bot_moveWalkPoolFrameEnd`)

9cc53399, default 0, build `poolend-build-20261006-a`: with S > 0 a bot owed a deferred walk or drop chunk this frame
runs it after every bot's boundary (`Move_PoolFrameEnd`, last in `G_RunFrame`), in client order, and no pool rollout
starts once the frame's forecast steps over every bot reach S. Step counts, not a clock, so worlds still replay.

Off replays the previous build exactly (`pool/s0-dan-1` against `src/dan-1`: game log and 141,960 decision lines).
1331d29b orders the owed chunks earliest deadline first (build `poolend-build-20261006-b`); against client order it
changed nothing measurable (`e550` rows). Dantooine, one world at a time (`pool/`; the s0, s400 and s550 rows of a
seed were run in one session, and the machine drifts between sessions: the same off world read 579, 645 and 703
frames over 25 ms on three runs):

| S (steps a frame) | frames over 25 ms, seeds 1 / 2 | p99 ms | median ms | walking deferrals taken | late |
| --- | --- | --- | --- | --- | --- |
| off | 703 / 747 | 33.75 / 34.25 | 4.0 / 3.5 | 1,583 / 1,538 | 546 / 448 |
| 700 | 547 / 652 | 31.25 / 32.5 | 4.5 / 4.75 | | |
| 550 | 420 / 424 | 29.25 / 29.25 | 5.0 / 5.0 | 1,350 / 1,344 | 1,289 / 1,283 |
| 550, deadline order | 440 / 389 | 29.5 / 28.75 | 5.25 / 5.25 | 1,443 / 1,341 | 1,428 / 1,239 |
| 400 | 390 / 383 | 29.0 / 28.25 | 7.25 / 7.0 | 1,147 / 1,116 | 3,275 / 3,820 |

Kejim at S = 700: 237 / 227 frames over 25 ms, p99 24.75 / 24.5, against this session's off 226 / 241: no change
(its tail is the planner). Steps are a loose proxy for time (a slow frame's step costs 11-57 us, p10-p90; a least-
squares fit of a bot's frame time on its steps and traces leaves a 14 ms residual), so S buys a smaller tail only by
starving the pool: below about 550 the late deferrals multiply. Each late deferral is a bot walking K more frames
before its hop, the cost that took captures from the walk deferral before its rescue.

Not measured yet: outcomes (captures, falls, hops) at S = 550, and Kejim at 550. Default stays off.

**Outcomes at S = 550** (2026-10-07, owner: study the built option's downsides; build `joindefault-build-20261007-a`,
so route tables and reroute joins on in both arms; paired seeds 3-18 a map, `.codex-research/poolend-outcome-20261007-a/`:
`run.sh`, `paired.py` with the `[BUDGET]` walk and drop tallies, `paired.txt`, `compare.txt`):

| on - off, 16 seeds | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms (work meter) | 1,202 -> 560 (-642 +- 28, -53%) | 2,208 -> 1,726 (-482 +- 48, -22%) |
| est. p99 ms | -6.3 +- 1.1 | -8.7 +- 0.7 |
| walking deferrals taken | 1,884 -> 1,693 (-10%) | 1,510 -> 1,360 (-10%) |
| walking deferrals late | 850 -> 2,005 (2.4x) | 454 -> 1,267 (2.8x) |
| walking deferrals starved (chunk-frames without room) | 3,673 -> 13,109 | 2,067 -> 9,823 |
| drop deferrals late | - | 110 -> 277 |
| hops a world | 577 -> 543 (-34 +- 15, -6%) | 483 -> 452 (-31 +- 9, -6%) |
| chained hops | -15 +- 14 | -13 +- 6 |
| airborne, % alive | -1.9 +- 0.7 | -1.8 +- 0.7 |
| mean speed alive | -0.5 +- 4.9 | -4.5 +- 4.2 |
| team captures | +0.40 +- 0.35 | +0.12 +- 0.43 |
| all captures | +0.73 +- 0.70 | +0.43 +- 0.85 |
| runner falls (both) / weapon deaths | +0.4 / +0.8 | +0.7 +- 0.6 / 0.0 |
| camper falls | -1.24 +- 0.90 | -0.93 +- 0.41 |

**Reading.** The tail gain is real in work counts on both maps, and nothing measured in the outcomes got worse:
captures, falls and weapon deaths hold. The cost is movement: a tenth fewer walking deferrals end in a hop, those
that do are late 2.4-2.8 times as often, and the runners hop about 6% less and spend about 1.8 points less of their
time airborne, with speed unchanged. That is the size of the planner guide's hop loss (7%, left off under the
keep-quality rule) and of the walk deferral's (about 22 fewer hops a Dantooine world, accepted by the owner for its
performance gain). Default left to the owner. A smaller hop cost may be had at a larger S (700: Dantooine about
-20% frames over 25 ms on the timing worlds), not measured for outcomes.

**S = 700** (owner: try 700 first; `run700.sh` against `run.sh`'s off arm, same build and seeds; `paired700.txt`,
`compare700.txt`):

| on - off, 16 seeds | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms | 1,202 -> 572 (-629 +- 27, -52%) | 2,208 -> 1,901 (-307 +- 57, -14%) |
| est. p99 ms | -5.8 +- 1.1 | -7.4 +- 0.7 |
| walking deferrals taken / late | -52 +- 23 / 850 -> 1,048 (1.2x) | -16 +- 18 / 454 -> 717 (1.6x) |
| hops a world / chained | -14 +- 14 (-2.5%) / -10 +- 12 | -10 +- 8 (-2%) / -4 +- 7 |
| airborne, % alive / speed | -0.5 +- 0.7 / -3.5 +- 5.3 | -0.7 +- 0.6 / -1.9 +- 4.6 |
| team captures / all captures | +0.28 +- 0.38 / +0.56 +- 0.74 | +0.12 +- 0.36 / +0.49 +- 0.72 |
| runner falls (both) / weapon deaths | +1.24 +- 0.94 / -0.1 | +0.25 +- 0.66 / +0.5 |

**Reading.** 700 keeps Kejim's whole gain and about two thirds of Dantooine's, at about a third of 550's hop cost
(-2 to -2.5%, inside one and a half standard errors) and a third of its late deferrals; outcomes hold (Kejim runner
falls +1.2 +- 0.9, the one figure to watch). Recommended default: 700.

**Enabled by default** (owner, 2026-10-07; d1b05162, build `pooldefault-build-20261007-a`): unset, Dantooine seed 3
replays `dan/s700-3` exactly (`.codex-research/pooldefault-20261007-a/`).

**Kejim worlds do not replay.** A second off world of Kejim seed 3 on this build (`kej/offrep-3`) leaves `kej/off-3`
at 0:16 of the game log; Dantooine seed 3 replays exactly (`dan/offrep-3`). Kejim comparisons are paired by seed, so
they stand, but with more noise than an exact replay would give; earlier today's Kejim cross-build divergence (join
replay check) may be this rather than the build. Cause not looked for.

## Options (2026-10-06)

1. **Pool at the frame's end, S = 550: measure outcomes.** Dantooine frames over 25 ms down about 40% and p99 about
   34 -> 29 ms, at 2.5 times the late walking deferrals and 14% fewer taken. 16 paired 4v4 seeds a map (about 50 min),
   then a default decision. On its own it does not reach 25 ms on Dantooine (about 400 frames a world over, against
   about 246).
2. **Kejim's planner.** About 42 cross-map replans a world at about 85 ms each (spawn or a pickup to a far flag: about
   28,000 state expansions, because the straight-line heuristic at 450 u/s is about half the cost of a route walked
   mostly at 250 u/s). (a) Spread such a search over frames at a fixed expansion budget, the bot walking the cell
   route (`Graph_Path`) meanwhile and switching when the state route is ready: exact route, about 10 frames on the
   cell route. (b) Weighted A* once a search passes a few thousand expansions: few expansions, routes within a bound
   of optimal, so level-1/level-2 stand times must be checked. Also small and nearly exact: (c) a replan whose goal
   is enclosed by the body it chases (a defender's goal on the carrier) skips the body pass, which today searches the
   whole graph before failing (94-162 ms, a few times a world).
3. **What remains on Dantooine** with option 1: whole straight searches at landings (36% of slow time; they stay in
   their frame for the kept speed), planned drop and climb edges (18%) and gap searches (11%).

## Proposal: parallel rollouts inside the frame (owner asked, 2026-10-06; not started)

Owner: "is it time to add a thread just for bot calcs?" Recommendation: yes, as a fork-join pool of worker threads
that run one search's candidate rollouts in parallel while the main thread waits, not an AI thread running on its own
clock (that would end the replayable worlds every comparison here rests on).

- **Why.** The machine has 20 logical processors and every search runs on the main thread. A search's 12-66
  candidate rollouts are independent (same root state, frozen world); run in parallel and judged in candidate order,
  the decisions are identical, so the speed-up needs no outcome campaign. It also covers what cannot be deferred:
  landing straight searches (36% of Dantooine's slow time), drop/climb and gap searches. A 40 ms search on 8 workers
  would be about 6-8 ms. Step budgets (above) cannot do this: a step costs 11-57 us.
- **Blockers found** (reentrancy): `pm`/`pml` are globals (`bg_pmove.c:52-53`); the forecast's entity copy is one
  static array (`forecastEntities`, bot2_forecast.c) and the deferred chunks overwrite the live player state and
  `level.time` for their duration (`SearchChunkWith`); the engine's collision trace marks brushes and patches with a
  shared check count (`cm_trace.cpp:223-252`, `cmg.checkcount++`), so concurrent traces race (an engine change,
  against the portability rule, or traces answered from a module-owned collision copy); cvar reads and any print
  inside a rollout must move out of the parallel part.
- **Spike (about one session):** thread-local `pm`/`pml`, a per-thread forecast context, the landing straight
  search's candidate rollouts on a fixed worker pool behind a cvar (default 0), engine traces under one global lock
  at first. Pass condition: a threaded world replays the single-threaded one exactly (`laps_identity.py`: game log and
  decision lines), then timing alone. If the trace lock eats the gain, next is a per-thread check count or the
  module-owned collision copy.
- **Kejim's planner** is a single A* and does not split this way; a worker could run it in the background (option
  2a above) against a graph snapshot, since learned edges are added mid-game.


## Two-core spec, pinned baseline and the threading inventory (2026-10-06, later)

**Spec (owner).** Real servers run `sv_fps` 30 with at most 7 bots (one human in a 4v4), on small hosts (2 vCPU
DigitalOcean; 4-core Ampere ARM), about 2.3-3.2 times slower a thread than the dev CPU by Geekbench 6 single-core.
Owner: benchmark against the dev CPU for simplicity, keep route quality, and take the performance spec as **two cores**
of it; testing and development may use more. `defense_laps.py --cpus spec` passes the engine's `com_affinity 14`
(logical CPUs 2 and 4: two separate performance cores, not hyperthread siblings, not efficiency cores 16-19, not CPU
0). A mask set on the process from outside is reset by the engine's init (`Sys_SetProcessorAffinity`, `com_affinity`
default 0), so the first attempt ran unpinned (kept in `.codex-research/frame-20261006-b/unpinned-aborted/`).

**Pinned baseline**, same build and options as "Where HEAD stands" (`.codex-research/frame-20261006-b/`, `timing.sh`,
`compare.py`). Every pinned world replays its unpinned twin exactly over the first 600 game seconds (game log hash; a
world stops a little past 600 s, so its last lines vary). The bots are single-threaded, so pinning should not move
timing; three worlds are within noise (two unpinned runs of Dantooine 1: p99 32.5 / 33.0, over 25 ms 645 / 660).
Dantooine 2 came out worse while a code search ran beside it: a pinned server cannot move away from load on its two
CPUs, so timing runs need an idle machine. Not re-run yet.

| world | p99 ms unpinned / pinned | over 25 ms | worst ms |
| --- | --- | --- | --- |
| Dantooine 1 | 32.5 / 32.25 | 645 / 614 | 90 / 82 |
| Dantooine 2 | 33.5 / 37.5 | 676 / 892 | 95 / 118 |
| Kejim 1 | 28.5 / 27.25 | 366 / 322 | 244 / 221 |
| Kejim 2 | 25.75 / 26.0 | 261 / 271 | 285 / 233 |

**Collision share** (from "Where a forecast step's time goes", records/2026-10-04-tail-composition.md, not re-measured):
traces are about 43% of a rollout step on Dantooine and 31% on Kejim. With engine traces under one lock, two cores
give at most about 1/(0.4 + 0.6/2) = 1.4 times on a search; thread-safe collision about 1.9.

**Reentrancy inventory** of one candidate rollout (`RunCandidate`, read-only survey; no threading exists anywhere in
the game or engine today). Beyond the blockers listed in the proposal above:

- The forecast writes the global `level.time`/`previousTime`/`framenum` every step and restores them; the simulated
  clock is read by the trigger test, mover floors (`Bot2_StandableEntity`, `Bot2_MoverRestUntil`), the corridor's
  ground sample, Pmove's game hooks (`solidHack`, `PM_ClientImpact`, `Client_CheckImpactBBrush`), and `SlotIntact`
  compares the whole `level` after each rollout. Needs a per-thread clock and every reader moved to it.
- The live entity's `s.legsAnim`/`torsoAnim` are written before each simulated Pmove for `BG_StartLegsAnim`; concurrent
  candidates of one client would change each other's results.
- Pmove globals (`pm`, `pml`, `pm_entSelf`, `pm_entVeh`, `gPMDoSlowFall`, `pm_flying`, `pm_cancelOutZoom`) and the
  random stream `holdrand`; bg_pmove.c is shared with cgame.
- Order-dependent state: the replay buffers and rollout ring (a candidate replays from earlier rollouts), the
  corridor memo (keyed without simulated time but valued with it on mover floors), and the frame-end pool's step
  budget (replayed steps are not counted, so the chunk cut depends on hit order).
- Engine entry points besides Trace that need the lock: PointContents, EntityContact, EntitiesInBox, the shared temp
  box model, `G2API_GetBoltMatrix` on the live ghoul2 (idle saber-stance frames on slopes), the navmesh query, and
  `G_TempEntity`'s scratch from Pmove's effects.
- Per thread: `forecastEntities` 1.5 MB, the trace cache about 7 MB, the rollout ring about 10-12 MB.

**Reading.** The spike is several sessions, not one, and it edits shared upstream code (bg_pmove.c, g_active.c,
bg_slidemove.c, bg_panimate.c) as well as the forecast. At the two-core spec it buys at most about 1.4 times on
searches with the trace lock (Dantooine p99 about 32 -> 23-25 ms if the slow frames are all search), and about 1.8
with a module-owned collision copy (2-3 sessions more). That reaches the 25 ms target but leaves little room to undo
the deferrals at two cores. Owner decision pending.

## Where the candidate rollouts go (retained-log estimate, 2026-10-06)

Owner question: instead of deferring searches, can a search find the good routes first? Estimate from the four
pinned timing worlds' logs, no new runs (`.codex-research/frame-20261006-b/`, `prune_estimate.py`, `nowinner.py`,
`fail_tail.py`). Every judged candidate prints `[SPIKE]` (outcome, frames rolled, done frame); the committed one is
named by its plan line (matched by client and plan id, since a deferred search is taken on a later frame). Frames are
rollout steps as logged, before replay savings; the walk reference rollouts are not counted. "Slow" is the searches
judged on a frame where that bot spent at least 5 ms.

| share of candidate frames | Dantooine all / slow | Kejim all / slow |
| --- | --- | --- |
| the committed candidate | 1.9% / 5.1% | 1.8% / 5.2% |
| failed candidates of committing searches | 10.9% / 20.9% | 13.5% / 26.9% |
| losing admitted candidates, to their done frame | 8.4% / 20.8% | 6.7% / 16.7% |
| the same losers after their done frame | 4.4% / 11.4% | 3.9% / 10.6% |
| searches that committed nothing | 74.4% / 41.8% | 74.0% / 40.6% |

About 18 candidates a search (straight searches 20); the committed one sits at index 9-10 on average.

- **Most work is searches that commit nothing**, and they are predictable: after a search that committed nothing,
  the same bot's next search of that kind within 500 ms also commits nothing 83-84% of the time (straight), 92-93%
  (drops, gaps); after a commit, 16-19%.
- **They fail on safety and geometry, not on score.** Their frames are mostly candidates refused at the landing,
  with no path from the landing, nothing onward, off the graph, or falling. Candidates that were merely slower than
  the walk are under 1%.
- **Failing candidates keep rolling after their last landing**: 15% of all candidate frames on Dantooine and 19% on
  Kejim (straight). Some of that is how the verdict is reached (`no_onward` is the onward walk itself); for others the
  verdict may already be fixed at the landing. The logs do not record the frame a verdict became final.

**Reading.** Exact savings (same winner): (1) a losing candidate's frames after its done frame, once its score is
already beaten, 4% of all frames and about 11% on slow frames, if the walk reference is rolled first so a score is
known at the done frame; (2) stopping a failing candidate at the frame its verdict is fixed, up to 15-19%, size
unknown until that frame is logged; (3) an optimistic bound against the incumbent mid-flight, unknown without per-frame
data. The large lever is not exact: remembering that a spot, speed and heading found nothing and re-searching only
when the state changes would skip most of the 74%, but a search after a fruitless one still commits 7-17% of the time,
so it changes behaviour and needs an outcome comparison.

## Do failing candidates fail consistently? A failure memory, simulated (2026-10-06)

Owner question: outcomes depend on speed, heading and the rest, so what would it take to learn the consistently bad
candidates and skip them? Diagnostic ce181d40 appends the search's root state to `[SPIKE]` (position, graph cell,
velocity and route yaw, ground, Force Speed and its time left, Force); build `rootdiag-build-20261006-a`, the four
seeded 4v4 worlds run four at once (`.codex-research/rootdiag-20261006-a/`, `run.sh`). The bot's route destination
is the `to=` of its latest path line. Scripts there: `consistency.py` (a memory learned during one world),
`cross_world.py` (learned on one seed, played on the other), `search_level.py`, `prefill_bound.py`.

The simulation replays each world's searches in game-time order. A candidate is its kind and construction (hops, aim
offsets from the route direction in 1-degree bins, run-ups); a state is a key of increasing detail: graph cell; plus
destination; plus speed (100 or 50 u/s bands) and route direction (30 or 15 degrees); plus velocity angle to the
route, ground and Force Speed; or position in 16-unit bins with speed 25 and route 10 degrees. Policy: skip a
candidate whose (state, candidate) entry has failed N times (never finished its move) and never finished. The memory
learns only after a search is judged (a first version learned from siblings in the same search and over-reported
savings about threefold). The simulation ignores feedback: a changed decision would change the world after it.

| memory | saved (Dantooine / Kejim) | chosen hops changed | searches left with nothing |
| --- | --- | --- | --- |
| one world, cell, N = 1 | 11.2% / 10.3% | 4.0% / 5.1% | 1.2% / 1.8% |
| one world, cell + destination + speed 100 + route 30, N = 2 | 3.4% / 2.1% | 0.5% / 0.1% | 0.2% / 0% |
| one world, finest key, N = 2 | 2.1% / 1.1% | 0.3% / 0% | 0.1% / 0% |
| other seed's world, cell, N = 2 | 11.9% / 10.3% | 2.8% / 3.5% | 0.8% / 1.3% |
| other seed's world, cell + destination + speed 100 + route 30, N = 2 | 5.5% / 3.6% | 0.6% / 0.6% | 0.4% / 0.2% |
| other seed's world, cell + destination + speed 50 + route 15 + velocity + ground + Speed, N = 2 | 4.8% / 2.7% | 0.5% / 0.3% | 0.3% / 0.1% |

- **When a candidate does fail twice from a similar state it keeps failing**: 2-10% of skipped candidates would have
  finished their move (coarse keys up to 14%).
- **But repeats are rare.** A map's two worlds hold about 200,000 candidates in 130,000-160,000 distinct (state,
  candidate) entries at every key tried; most occur once. Frames in entries that never finished anywhere in the two
  worlds: 22-27% in entries seen twice or more, 33-46% in entries seen once (in-sample, an upper bound for a memory
  filled offline).
- **Whole searches repeat even less** (`search_level.py`): skipping a search whose state found nothing before saves
  0-5%. Fruitless searches come in streaks in time (83-93% after a fruitless one), but the bot has moved on between
  them, so the state differs.

**Reading.** A memory carried between games saves about 3-6% of rollout frames at about 0.5% of chosen hops changed,
and about 10-12% if 3% of chosen hops may change. It grows with the games behind it (one ten-minute game measured
here), towards the in-sample bound for a memory filled offline by the explorer, untested. It is not the large lever
the 74% suggested: that work is spread over states that seldom recur.

Builds since `meleedefault-build-20261006-a` (a321a3ef) each give different laps worlds from the same seed (the
FRAMESRC diagnostic, the default-off pool options, this diagnostic); a build replays itself (pinned and unpinned, and
four at once against alone). Cross-build replay identity is therefore not evidence of inertness here.

## Planner guidance: time-to-goal tables for the state search (2026-10-06)

Option 2 above, built (c7a84934, 7704ca56, cf97a955; `bot_movePlanGuide`, default 0, so nothing changes by
default). A per-goal table of time to the goal over a relaxation of the state search's own graph replaces the
straight line at 450 u/s as the heuristic: walk links at the wish speed less a friction potential on speed above it
(provably a lower bound per link), strafe edges at their cost with the potential's change, other edges at their cost,
the clearance penalty included; one table without Speed, one with. Learned edges are folded in incrementally. Mode 2
runs every state search both ways, keeps the straight line's result and prints `[PLANGUIDE]`; the four seeded 4v4
worlds per variant (`.codex-research/guide-20261006-a/`, `guide_check.py`).

| variant (two worlds a map) | Dantooine planner | Kejim planner | Kejim searches over 5,000 expanded |
| --- | --- | --- | --- |
| table, every search | 0.73x (builds) | 0.18x | 1.07x |
| table, long searches to a recurring goal | 1.27x | 0.48x | 1.59x |
| larger of table and straight line, same gate | 1.25x | 0.94x | 1.81x |
| the same without the table upkeep | 1.46x | 1.35x | about 1.8x |

- **The strict lower bound loses on Kejim.** The straight line at 450 u/s overestimates on fast hop routes with Speed,
  which makes the old search greedy there; the table is honest but weak for a Speed state (bounded at 425 u/s
  walking), so long hop-rich searches expanded more (17 s -> 40 s of search over two worlds). Routes: never a route
  lost, h at the root never above the cost; 18-23% of searches came out cheaper (median 53-124 ms), 1.5% dearer by
  about 0.5%. The search merges states by speed band and keeps only whether Speed is active, so its result depends on
  expansion order and no heuristic makes it exact.
- **The larger of the two** keeps the straight line's pull: Kejim's big searches 1.96x fewer expansions and 1.8x
  faster, 71% of routes identical, 13% cheaper (median 69 ms), 1.5% dearer. Not a lower bound, as the straight line
  is not.
- **Table upkeep is what cancels it on Kejim**: 855,000 reverse arcs (strafe edges by takeoff reach); building a
  goal's tables or folding learned edges in cost 9.6 s over two worlds, 385 times over 10 ms (Dantooine 127,000 arcs,
  1.4 s). Searches that find no route (the body-pass failures, 100-220 ms) are untouched: they explore everything
  reachable whatever the guide, and remain the worst planner frames (option 2c).

**Reading.** As built it is not worth enabling. The search side is a real gain (Kejim big searches 1.8x); what
remains is cheap upkeep: tables for the flags and bases built at map load rather than in play, and learned edges
folded in without settling 855,000 arcs (or batched off the hot frame). Guided routes differ from today's in 29% of
Kejim's guided searches and 78% of Dantooine's (there 29% cheaper, 4% dearer, the rest equal-cost alternatives),
so enabling it needs an outcome comparison.

## Planner guidance with preloaded goal tables: timing and outcomes (2026-10-06, later)

Owner: build the goal tables offline instead of in play, and keep them. Built (0407b5bf, 3fff9e16, b4c1e79c;
default off): `tools/movement_guide_goals.py` ranks earlier worlds' searches by time per goal cell and writes the
goals covering 95% of it as positions (`tools/fixtures/movement/guide/<seed>.guide`: Kejim 76 goals, Dantooine 88,
from seeds 1-2); `play_vs_runner.stage` copies the list beside the seed; the game builds those goals' tables at the
map's load (Kejim 0.42 s, Dantooine 0.15 s) and keeps them. Duplicate arcs are merged with precomputed costs
(Kejim 855,000 -> 303,000, a table about 3.6 ms instead of 22), other goals get tables once asked 3 times in the map,
learned edges are folded per table at most once a second. Settings for "on": `bot_movePlanGuide 1`,
`bot_movePlanGuideMax 1`.

**Planner time** (check mode, seeds 3-4, out of sample for the lists, `guide-20261006-a/worlds-h`): Kejim 48.7 ->
29.4 s over two worlds (1.66x), searches over 5,000 expansions 2.7x faster, planner search p99 94 -> 18 ms;
Dantooine 1.43x. Failed searches (no route) are untouched and remain the worst single searches (up to 0.3 s).

**Frame time**, pinned two-core spec, one world at a time, same build off then on (`guide-timing-20261006-a`, seeds
1-2, in sample for the lists):

| world | p99 ms | over 25 ms | over 50 ms | over 100 ms | worst ms |
| --- | --- | --- | --- | --- | --- |
| Kejim 1 | 33.0 -> 26.0 | 469 -> 276 | 90 -> 33 | 23 -> 6 | 214 -> 153 |
| Kejim 2 | 26.25 -> 24.25 | 276 -> 218 | 61 -> 16 | 32 -> 4 | 231 -> 187 |
| Dantooine 1 | 34.5 -> 33.25 | 731 -> 647 | 43 -> 44 | 1 -> 2 | 103 -> 103 |
| Dantooine 2 | 37.25 -> 38.25 | 844 -> 905 | 79 -> 97 | 1 -> 5 | 103 -> 125 |

**Outcomes**, paired seeds 3-10 per map, unpinned, four at once (`guide-outcome-20261006-a`, `laps_compare.py`,
`hops.py` from `flow-20261005-a/flow.py`'s measure, both runners, on - off, paired se):

| | Kejim | Dantooine |
| --- | --- | --- |
| captures, both teams | -1.36 +- 0.91 (8.27 -> 6.91) | +0.62 +- 1.03 |
| runner falls, both | -0.50 +- 1.75 | +0.74 +- 0.95 |
| hops a world | -43.5 +- 12.2 (590 -> 547) | -21 +- 23 |
| chained hops | -34.4 +- 9.9 (382 -> 348) | -14.6 +- 10.8 |
| hops from a stop | +7.5 +- 2.9 | -5.1 +- 5.0 |
| mean speed alive, u/s | -14.9 +- 6.3 (415 -> 400) | -4.9 +- 10.7 |
| airborne, % alive | -1.5 +- 0.7 | -1.1 +- 1.7 |

**Reading.** On Kejim the guide fixes the frame tail (frames over 100 ms 55 -> 10 over two worlds, p99 at or near
25 ms) but its routes, cheaper by the planner's own costs, give the runners about 7% fewer hops, 9% fewer chained
hops and 15 u/s less speed, with captures trending down. The planner's cost model does not price what a route
offers the hop searches that run along it. Against the owner's rule (keep route quality), it stays off. Dantooine
gains nothing measurable in frame time and shows no clear outcome change. Next for it, if pursued: find why guided
Kejim routes hop less (which goals, which corridors), or use the tables only where routes cannot change.

## Enclosed goals skip the body pass (option 2c, 2026-10-07)

Built 318ff612 (`bot_movePlanEnclosed`, **default 1**; 0 off; 2 check), build `enclosed-build-20261006-a`. A replan
with live bodies near first searches around them and, when that finds nothing, searches again without them (the
plan line's `body_fallback=1`). `Graph_BodiesEncloseGoal` tests before the first pass whether it can only fail: every
cell with a walk or diagonal arc into the goal (any cell in the graph) is body-blocked and is not the root, and no edge
of any state lands on the goal. Then the replan goes straight to the second pass. Exact, so routes are unchanged; the
plan line is unchanged too, and a `[BODYSKIP]` line (not a decision line) counts each skip. Worlds in
`.codex-research/enclosed-20261006-a/` (`run.sh`, `fallbacks.py`, `fallbacks.txt`), 4v4 laps options with the frame
meter, unpinned, four at once.

- **Identity**: mode 1 replays mode 0 on Kejim seeds 1 and 2 (`laps_identity.py`: game log and 160,115 / 153,182
  decision lines identical).
- **Exactness** (mode 2 runs the pass anyway): 24 predicted enclosures over Kejim and Dantooine seeds 1-2, none
  found a route (`found=0`).
- **Coverage**: on Kejim every body fallback was an enclosure (8 of 8: a chase defender's goal on the carrier, or a
  keep-out defender's goal inside the flag lattice), 110-196 ms each; skipped they take 1 ms. Dantooine 18 of 26,
  all under 12 ms before.
- **Size**: 2-5 replans a Kejim world, the worst single planner frames there. The other slow replans (about 18-30 a
  Kejim world at 90 ms or more) are the cross-map searches of options 2a/2b and are untouched. No timing world was
  run: the saving is the plan lines' own elapsed time, and two to five frames a world do not move p99.

## State searches split over frames (option 2a, 2026-10-07; in progress, stopped by the owner)

Built 25946210 (`bot_movePlanSplit` N, **default 0**), build `split-build-20261007-c`. The state search is now three
parts over a current table (`StateBegin`, `StateRun` with an expansion budget, `StateResult`); one search run whole is
the old loop unchanged. With N > 0 a replan's state search runs in one of four tables of its own (about 11 MB each)
for N expansions; unfinished, the bot gets the cell route (`Graph_Path`) and the search continues N expansions a frame
at its `Move_Command`, under the bodies and avoided edges it began with. Finished with a route, it asks for a replan,
which installs it if the goal and the route walked meanwhile are unchanged (a commit's reroute supersedes it); the
executor finds the bot's place on it. A replan toward the same goal while one runs keeps it; no cell route means the
search finishes at once; no free table means the whole search, as before; directed (fixture) searches stay whole.
Plan lines carry `split=1` (cell route meanwhile) or `split=2 split_from=` (installed). Expansion budgets, not clocks,
so worlds replay. Not verified: that split 0 replays the previous build (cross-batch Kejim worlds diverge at their first
plan line, as in the enclosed check batch; same-build checks only).

Pinned two-core spec, one world at a time (`.codex-research/split-20261007-a/`, `timing.sh`, `split.py`;
`src.py` from frame-20261006-a for sources):

| world | p99 ms, split 0 / 2000 | over 25 ms | worst ms | replans of 50 ms or more | split installs |
| --- | --- | --- | --- | --- | --- |
| Kejim 1 | 27.25 / 25.25 | 320 / 248 | 223 / 85 | 47 / 0 | 247 |
| Kejim 2 | 27.75 / 29.0 | 340 / 427 | 251 / 295 | 37 / 0 | 452 |
| Dantooine 1 | 35.0 / 36.5 | 762 / 949 | 100 / 127 | 0 / 0 | 948 |
| Dantooine 2 | 37.5 / 34.5 | 899 / 784 | 119 / 98 | 0 / 0 | 860 |
| Kejim 1, split 1000 | 28.0 | 346 | 540 | 0 | 755 |

- **The planner's tail is gone**: no replan reaches 50 ms at 2000, and the planner's share of Kejim slow time falls
  (seed 1: 4.6 -> 0.8 s; seed 2: 4.8 -> 2.5 s).
- **But the frames are not clearly better.** Kejim 2's slow time grew in the deferred-walk pool (5.5 -> 7.8 s) and
  straight searches (3.7 -> 4.8 s); the worlds diverge once routes change, and pairs swing about 15% either way
  (Dantooine 1 worse, 2 better).
- **2000 splits far too much**: about 400 Kejim replans a world exceed 2,000 expansions but only about 80 exceed 8,000;
  Dantooine, whose replans never reached 11 ms, splits about 900 a world, each a stretch of cell-route walking and an
  extra replan. 1000 is worse (755 installs, a 540 ms frame).

**Next** (not run): budgets of 4000 and above (the sweep was stopped after 1000 on Kejim 1), or a larger first part
than later parts so only cross-map searches split; then hops and chained hops on paired seeds (`hops.py`,
guide-outcome-20261006-a), since walking a cell route meanwhile can cost hops as the guided routes did. Aborted worlds
are listed in `timing/ABORTED.txt`.

### Timing noise, a work meter, and where the planner expansions go (2026-10-07, later)

**Wall-clock tails of single worlds cannot separate these arms.** `r8000-kej-1` replays `b8000-kej-1` exactly (game log
and 151,716 decision lines) yet read 383 against 935 frames over 25 ms; a split-off repeat ran at a mean frame of 7.5 ms
against 5.3. Budgets 4000 and 8000 (`timing3.sh`) read anywhere from better to far worse than off; not evidence.

**Work meter** (0b5294b4, `[FRAMEWORK]`): per frame, forecast steps and planner expansions over every bot, as
quantiles; counts replay exactly whatever the machine does, so arms run four at once. Its `est_*` weights (25 us a
step, 3 us an expansion) overstate Dantooine's p99 about 2.5 times; read the counts. Split 0/2000/4000/8000 on both
maps (`.codex-research/split-20261007-b/`, `work.py`, `work.txt`): step tails equal; the 99.9th percentile of
expansions a frame 22,000-45,000 in every arm, worst frames 45,000-138,000; hops, chained hops and speed with no
consistent change. Splitting bounds the replan's search but not the frame's expansions.

**Where the expansions go** (`[FRAMEEXP]`, 9a8fd34b and bfbea90a; `.codex-research/split-20261007-c/exp.py`):
frames of 10,000 expansions or more, by source, split off (`split-20261007-e/`, seeds 1-2):

| map | replan state search | reroute state searches (from candidate landings) | cell searches outside the planner | frames a world |
| --- | --- | --- | --- | --- |
| Kejim | 36-38% | 21-30% | 34-41% | about 190 |
| Dantooine | 19-21% | 41-43% | 37-38% | about 840 |

Every cell-search expansion there is from outside the planner: the defenders' route queries (`InterceptGoal`,
`BlockGoal`, `RouteMs`, the hand-off's route time; bot2_defender.c) and the dropped-flag reachability check
(ai_bot2_objective.c), each a whole cross-map A* toward a fixed goal (a stand, a flag). None from replan or reroute
fallbacks. With split 4000 the replan share falls to 8-11% and the cell share rises to 60-74% of what remains. A
first version gave the bot a fresh cell route meanwhile; keeping the route it is on (same goal, no avoided edge;
6c0f0d68) changed nothing measurable, because those cell searches were never the planner's. An expansion of the cell
search is cheaper than one of the state search, so its share of time is smaller than of expansions (not measured).

**Reading.** Option 2a as built removes the replan from the tail and leaves the frames' planner work about where it
was; `bot_movePlanSplit` stays 0. The larger, nearly exact lever is the defenders' cell queries: their goals are a few
fixed cells, so one reverse table a goal (Dijkstra from the goal over the cell graph, rebuilt when the graph changes)
answers each query by walking down it instead of a cross-map search; equal-cost ties and float rounding can change
a route or a millisecond, so it needs a defender outcome check. Then the reroute state searches (Dantooine's largest
share).

## Route tables for the defenders' cell queries (2026-10-07; owner chose this step)

Built ead82088, 7261c506 (`bot_movePathTables`, **default 1** since the owner's word on 2026-10-07; 0 off; 2 checks), build `table-build-20261007-d`. The five
outside callers (`InterceptGoal`, `BlockGoal`, `RouteMs` in bot2_defender.c; the flag check and the hand-off route time
in ai_bot2_objective.c) call `Graph_PathTable`, which answers from a per-goal table of every cell's cheapest time to the
goal under the cell search's own costs (Dijkstra over reversed walk, diagonal and edge arcs, standing cells only, the
clearance penalty), walking down it. A goal gets a table once asked three times since the last load (32 slots); the
table is built over frames, 4,000 settled cells a frame (`Graph_PathTableStep`, from `Move_Command`), the search
answering meanwhile. Edges added, made cheaper or back from an off are relaxed in; an edge turned off, disabled or made
dearer repairs only the cells whose time ran through it (tight arcs, ties included); a load, a learned-edge reset, the
clearance or a lift's state rebuilds. Under bodies or a corridor the search answers. Table work counts in the work
meter. Research: `.codex-research/table-20261007-a/` (first version, `check.py`), `-b`, `-c`, `table-outcome-20261007-a/`
(`run.sh`, `run2.sh`, `paired.py`, `paired.txt`, `paired-kej16.txt`).

- **Correct**: check mode answers with the search and replays off exactly (Kejim 1, Dantooine 1). Where the table's
  route differed (3,065 Kejim queries, 2,196 Dantooine): never dearer but for ten 1 ms float ties on Dantooine, never
  missing; cheaper in 2,390 / 1,322 (median 278 / 149 ms, up to 2.1 s), because the search's A* heuristic is beaten
  by fast edges and it settles for a dearer route.
- **The first version cost more than it saved** (Kejim 1 big-frame cell work 3.1M -> 8.0M expansions): every new goal
  and every dearer edge rebuilt a whole table inside one frame (399-573 goal builds a world, 83-185 rebuilds). Asking
  three times first, building over frames and repairing instead of rebuilding fixed it (64-88 builds, 75-257 repairs
  of a few hundred cells a world).
- **Work counts**, paired seeds, tables on - off (Kejim 16 seeds 3-18, Dantooine 8 seeds 3-10):

| | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms | 1,656 -> 1,527 (-128 +- 42) | 3,346 -> 2,891 (-455 +- 86) |
| est. p99 ms | -4.5 +- 1.3 | -6.5 +- 1.5 |
| expansions a frame, p99 | 9,631 -> 7,675 (-1,956 +- 490) | 19,825 -> 15,725 (-4,100 +- 417) |
| frames of 10,000 expansions or more | 228 -> 151 | 872 -> 635 |
| outside cell expansions in them | -71% | -73% |

- **Outcomes** (`laps_compare.py`): captures +0.62 +- 0.33 Kejim, +0.43 +- 0.32 Dantooine; runner falls -1.30 +- 0.74 and
  -0.74 +- 0.52; weapon deaths flat. Hops, chained hops, speed and airborne share: no change (Kejim at 8 seeds showed
  -33 +- 16 hops; at 16 seeds -12 +- 14, speed -3.5 +- 6). The defenders take cheaper routes; nothing measured got worse.

**Reading.** Worth enabling: about 10-14% fewer estimated frames over 25 ms on both maps and a quarter off the p99
expansions, with no outcome cost. Owner: enable by default (2026-10-07); build `table-build-20261007-e` confirms tables
answer with the cvar unset (Kejim seed 3: 5,316 queries, 71 builds).

**Review fix (2026-10-07, build `review-fixes-build-20261007-a`).** The repair folded queued changes one at a time,
testing tightness against the final costs while the table still held the earlier ones: two dearer edges in a chain
left cell times no route ran through, and the walk down the table found no route (an edge made cheaper did not even
record its old cost). `PathFold` now folds the queue as one batch against each changed edge's cost before its first
change since (`PathChange` records it; -1 no arc), recomputes the marked cells, relaxes the changes and settles once.
`tools/test_movement_path_fold.py` compiles the shipped code and requires folded tables to equal fresh ones (the
review's chain, a cheaper-then-dearer edge, 20,000 random batches); HEAD's per-change repair fails the chain case.
Check mode, the table-20261007-a configuration, Kejim and Dantooine seeds 1-2 (`.codex-research/fold-check-20261007-a/`,
`check.py` from table-20261007-a): 11,871 differing queries, table never missing where the search found a route, dearer
only by 1 ms float ties (17, all on Dantooine; build c had 10 in one world), cheaper in 8,492. Repaired cells a world:
Kejim 45,839 / 25,920, Dantooine 412,203 / 229,281 (build c: 57,665 Kejim and 177,351 Dantooine in mode 2, 40,916-468,344
in mode 1), inside the earlier worlds' range. Work counts, Dantooine seeds 3-10 paired, tables on - off on the same
build (`.codex-research/fold-outcome-20261007-a/`, `paired.txt`): est. frames over 25 ms 2,485 -> 1,943 (-542 +- 155),
est. p99 -11.2 +- 2.5 ms, expansions p99 -3,888 +- 568, p99.9 -10,013 +- 1,935, frames of 10,000 expansions or more
522 -> 276, their outside cell expansions -79%; hops, chained hops, speed and airborne share unchanged. Repaired cells
on the same seeds: mean 196k against build d's 240k (per seed 25k-467k against 113k-499k). The fold costs no more
than the per-change repair did, and tables save at least as much as measured on build d.

## Reroute price floors (2026-10-07; closed, left off)

The reroute pass runs a state search from each off-path candidate's last landing to the route's goal, then refuses
the candidate unless its rollout time plus the route's walk-model price (`PathRemainingMs`: 2D length at 250 u/s,
250 ms an edge) beats the route it leaves by `MOVE_REROUTE_GAIN` (250 ms). In the retained table-outcome worlds a
third of finished reroute searches ended refused that way (Dantooine seed 3: 1,414 of 4,268; Kejim: 1,303 of 4,168),
while rerouted candidates won 35% of straight commits (379 of 1,083), so only an exact cut was acceptable.

Built 72139548 (`bot_moveReroutePrune`, **default 0**; 2 checks), build `prune-build-20261007-b`: a per-goal price
table (`Graph_PriceFloor`, bot2_graph.c), the least walk-model price to the goal over a superset of the arcs either
reroute search can take (links, diagonals, every edge from its cell whatever its state, strafe edges from every cell
whose StrafeNear owners include the edge's cell, no reach test), so a floor on any route a search returns; geometry
only, edges added are relaxed in. Mode 1 refuses a candidate without its search when landing time plus floor fails the
gate. `PathRemainingMs` now sums `Graph_PriceMs`, the same function. Check worlds, seeds 3-4, 4v4 laps options
(`.codex-research/prune-20261007-a/`, `run.sh`, `check.py`, `check.txt`):

| world | candidates asked | floor refuses | wrong | reroute expansions | saved | horizon changes | table settles / relaxations |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Kejim 3 | 3,850 | 200 | 0 | 2.75M | 87,926 (3.2%) | 46 | 4.5M / 323M |
| Kejim 4 | 4,259 | 176 | 0 | 4.09M | 13,910 (0.3%) | 37 | 4.6M / 329M |
| Dantooine 3 | 4,559 | 567 | 0 | 8.77M | 190,686 (2.2%) | 109 | 4.2M / 32M |
| Dantooine 4 | 4,089 | 399 | 0 | 8.11M | 70,744 (0.9%) | 70 | 4.4M / 33M |

- **The floor holds** (no refused candidate ended admitted) **but is far too loose**: a finished search's route priced
  275-1,640 ms above it, median 550 ms, mostly the 250 ms charged per strafe edge the routes take and the floor's
  cheapest route avoids. Most of what it refuses had already lost to the margin test (`slower`), whose searches are
  cheap; the expensive cross-map searches are the ones it cannot refuse.
- **The tables cost more than they save**: 250-320 builds a world (goals churn with defenders' legs), as many settled
  cells as the saved expansions times 20-50, and on Kejim 323M relaxations from its strafe owners.
- Mode 1 would also change the extension walk's horizon in about one search in twenty (`horizon`).

**Reading.** Not worth enabling or tightening: even a perfect floor reaches only the third of finished searches the
gate refuses, and this one reaches a tenth of those. Left off. The reroute searches' cost needs a cheaper search, not
a skipped one.

## Reroute searches that rejoin (2026-10-07; owner chose this step; left off pending the owner)

Built 7426077f, c1b799e5, 12a6c04e (`bot_moveRerouteJoin`, **default 0**; 2 checks; `bot_moveRerouteJoinSlack` ms,
default 0). Paths now carry the state search's time at each cell (`times`, `timed`). With the join on, a reroute's
state search gets the route being left as joins: its cells from the one nearest the landing on (none before an edge
now off or avoided), each priced by the time that route's own search had left from it. A search that reaches a join
records its time there plus that rest; it ends when its lowest heap key plus the slack reaches the best such total
(slack 0: nothing it holds can still beat it by its own heuristic; a large slack: the first join it reaches), or at
the goal as before, and the rest of the route left is spliced on. Planner directives and required edges disable it.

**Check mode** (both searches run, the whole one's route kept; `.codex-research/join-20261007-a/` slack 0, seeds 3-4;
`-b` slack 2000 and 1000000 seed 3; `-c` the same with joins from the landing's nearest cell; `check.py`):

| slack | Kejim reroute expansions | Dantooine | routes identical | price of differing routes, bounded - whole |
| --- | --- | --- | --- | --- |
| 0 | 0.61-0.67x | 0.97-0.98x | 41-73% | median 0, 90th +0.4-0.8 s |
| 2000 ms | 0.27-0.38x | 0.70x | 36-45% | median 0 to +72 ms, 90th +0.3-0.4 s |
| first join | 0.03x | 0.01x | 20-24% | median 0 to +75 ms, 90th +0.3-0.4 s |

Every search found a route both ways. Slack 0 saves nothing on Dantooine: the straight line at 450 u/s is far under
the time left on its winding routes, so proving a join best explores nearly everything. Joining from the landing's
nearest cell instead of the root's index changed little. The worst planner-cost differences (+15 s) price lower by
the walk model: the route left carries times planned in another state (without Speed, around bodies since gone).

**Outcomes**, first join (slack 1000000) against off, paired seeds 3-18 on both maps, build `join-build-20261007-c`
(`.codex-research/join-outcome-20261007-a/`: `run.sh`, `run2.sh`, `paired.py`, `paired.txt` seeds 3-10,
`paired16.txt`, `compare.txt`, `compare16.txt`):

| on - off, 16 seeds | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms | 1,518 -> 1,196 (-322 +- 31) | 2,880 -> 2,212 (-668 +- 51) |
| est. p99 ms | -5.6 +- 0.7 | -15.2 +- 1.1 |
| expansions p99 / p99.9 | -29% / -15% | -34% / -36% |
| frames of 10,000 expansions or more | 146 -> 95 | 610 -> 254 |
| reroute expansions in them | 1,103k -> 26k | 6,155k -> 6k |
| hops, chained hops, speed, airborne | -1 / +5 / -3 u/s / -0.1 (no change) | +1 / -2 / -3 u/s / -0.2 (no change) |
| all captures (laps_compare, Welch) | 8.39 -> 6.91 (-1.48 +- 0.72) | 10.60 -> 9.36 (-1.24 +- 0.83) |
| team captures | -0.71 +- 0.37 | -0.59 +- 0.41 |
| runner captures, paired | -0.19 +- 0.40 | -0.69 +- 0.52 |
| conversion (captures a pickup) | 0.14 -> 0.12 | 0.19 -> 0.17 |
| runner falls / weapon deaths (both runners) | +0.2 / +0.3 | -0.7 / +1.5 |
| reroute installs over 16 worlds | 7,369 -> 6,629 (-10%) | 6,642 -> 5,866 (-12%) |

- **The work is gone**: reroute searches drop to 1-3% and the estimated frames over 25 ms by 21-23% on both maps,
  the largest single gain measured on Dantooine.
- **The hop measures that ruled out planner guidance do not move.**
- **But captures trend down on both maps** (-12 to -18% of all captures, 2 standard errors on Kejim and 1.5 on
  Dantooine; Kejim red -1.54 +- 0.42), with pickups flat and conversion lower; a tenth fewer reroutes are installed.
  The likely mechanism, not yet shown: a first-join route keeps the rest of a route planned in an older state, where
  the whole search re-plans it from the landing's state (Speed, bodies gone), and its walk-model price is a little
  dearer, so the gate admits fewer reroutes.

**Reading.** Not enabled: a capture cost of this size fails the keep-quality rule even with the frame gain.

### Joins only where the route left still holds (owner: option 1, 2026-10-07)

The rest (time left) cannot be what costs captures: a first-join search ends at the first join it pops, whatever its
rest, and `costMs` is telemetry only. So the refresh was made a validity test instead (5eee4657, build
`join-build-20261007-d`): a join holds only for a search state within `GRAPH_STATE_BAND` (48 u/s) of the speed the
route left expected at that cell and with the same Speed power state (from the route's start window and times); a
route planned around bodies (a replan's first pass) is not joined. Paths carry `speedMs` and `bodies`.

Check, first join (`.codex-research/join-20261007-d/`): among searches that may join, reroute expansions 0.15x Kejim,
0.39-0.44x Dantooine; per world about 2,500-2,800 searches cannot join because the route left was planned around
bodies, and the power test refuses far more joins than the speed test (Dantooine seed 3: 54,686 / 437).

Outcomes, paired seeds 3-18 (`.codex-research/join-outcome-20261007-b/`, `run.sh`, `paired.py`, `paired.txt`,
`compare.txt`); the off arms replay the earlier run's (Dantooine all captures 10.60 both times):

| on - off, 16 seeds | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms | -108 +- 34 (-7%) | -73 +- 153 (no change) |
| est. p99 ms | -2.6 +- 0.7 | -5.4 +- 1.4 |
| reroute expansions in frames of 10,000+ | 1,091k -> 545k | 6,156k -> 3,973k |
| hops, chained hops, speed | no change | +11 +- 8 / +6 +- 6 / +2 |
| all captures | -0.55 +- 0.81 | -1.29 +- 0.87 |
| team captures | -0.28 +- 0.41 | -0.74 +- 0.44 |
| conversion | 0.14 -> 0.13 | 0.19 -> 0.17 |

**Reading.** The test gives back most of the work (Dantooine's frames over 25 ms no longer move) and Dantooine's
captures fall as much as without it, conversion again 0.19 -> 0.17; Kejim's capture loss shrinks but is not resolved.
Whatever costs Dantooine's captures is not the state a join is reached in. Two independent 16-seed runs on the same
off worlds read -1.24 and -1.29 there, so it is unlikely to be noise. Left off (`bot_moveRerouteJoin` 0); joining the
route left is closed unless a capture-side mechanism is found (for example in the carrier's return legs, where a
whole search may find the route a joined one keeps missing).

### Where the captures went, and 16 more seeds (owner: look for the mechanism, 2026-10-07)

**Carry legs** (`.codex-research/join-outcome-20261007-a/carries.py`, `carries.txt`: per carrier episode from the play
CSV, pickup to capture or death): leg time to home (median 15.5-16.5 s), deaths at home, where on the leg carriers die
and time held at home with the home flag away are unchanged in both join arms. The loss moves between roles from run
to run: Dantooine first join mostly the runner (127 -> 112 of about 520 carries), Dantooine with the validity test
mostly the camper (43 -> 28), Kejim first join the camper (42 -> 25); each 1-2 standard errors. No route mechanism.

**Seeds 19-34**, first join against off, build `join-build-20261007-c` (`run3.sh`; `compare32.txt`, `paired32.txt`
over all 32): team captures, paired, Kejim +0.03 +- 0.37 and Dantooine +0.47 +- 0.36 (seeds 3-18 had -0.72 +- 0.30
and -0.59 +- 0.40). Over 32 seeds:

| on - off, 32 seeds | Kejim | Dantooine |
| --- | --- | --- |
| est. frames over 25 ms | 1,521 -> 1,214 (-306 +- 27, -20%) | 2,913 -> 2,256 (-657 +- 43, -23%) |
| est. p99 ms | -6.1 +- 0.9 | -13.9 +- 0.9 |
| frames of 10,000 expansions or more | 157 -> 96 | 621 -> 267 |
| hops, chained hops, speed, airborne | +3 / +3 / -0.1 u/s / -0.2 | +6 / -2 / +3 u/s / +0.3 |
| team captures (laps_compare) | -0.34 +- 0.24 (undecided) | -0.06 +- 0.32 (null: inside +-15%) |
| all captures | -0.75 +- 0.47 | -0.15 +- 0.62 |
| conversion | 0.14 -> 0.13 | 0.18 -> 0.18 |
| runner falls / weapon deaths (both) | -0.1 / +0.4 | -0.1 / +0.9 |
| defenders' enemy-flag pickups | -0.59 +- 0.24 | -0.34 +- 0.23 |

**Reading.** The Dantooine capture loss of seeds 3-18 does not replicate; over 32 seeds Dantooine reads no change
within 15%. Kejim keeps a smaller trend (all captures -0.75 +- 0.47, 1.6 se, mostly campers) that is unresolved.
First join (`bot_moveRerouteJoin 1`, `bot_moveRerouteJoinSlack 1000000`, validity test not used) is a candidate for
the default: the largest frame gain measured on Dantooine with hops unchanged. The owner decides. The validity test
is now optional (`bot_moveRerouteJoinValid`, default 0; e234eb14, build `join-build-20261007-e`), so
`bot_moveRerouteJoin 1` with slack 1000000 is the measured form: Dantooine seed 3 on build e replays build c's world
event for event (`.codex-research/join-replay-20261007-a/`; game logs equal but for the version and shutdown lines);
Kejim seed 3 diverges at 0:13 with joins off as well, the usual cross-build divergence.

**Kejim seeds 35-50** (owner, 2026-10-07; `run4.sh`, build c; `compare48-kej.txt` over all 48): paired team captures
-0.12 +- 0.33 on the new seeds. Over 48 Kejim seeds, paired: team captures -0.27 +- 0.20 (3.95 -> 3.68, -7%), runner
captures -0.09 +- 0.17 (no change), camper captures -0.18 +- 0.13 (1.12 -> 0.95); all captures -0.54 +- 0.37; pickups,
falls and weapon deaths no change; laps_compare still "undecided" (its 95% interval reaches -0.65, -16%). The runner,
whose straight searches the reroute serves most, is unaffected; what remains is a small camper trend under 1.5
standard errors.

**Enabled by default** (owner, 2026-10-07; b9addd2e, build `joindefault-build-20261007-a`): `bot_moveRerouteJoin` 1
and `bot_moveRerouteJoinSlack` 1000000 when unset; `bot_moveRerouteJoinValid` stays 0. With the cvars unset,
Dantooine seed 3 replays the measured form's world (build e, cvars set) exactly; reroute lines that joined: Kejim seed
3 4,450 of 4,516, Dantooine 4,623 of 4,653 (`.codex-research/joindefault-20261007-a/`).

## HEAD timing on the 2026-10-07 defaults (2026-10-07)

Pinned two-core spec (`--cpus spec`), one world at a time, the same options and seeds as the pinned baseline above
("Two-core spec, pinned baseline"); build `pooldefault-build-20261007-a` (d1b05162: route tables, reroute first join,
frame-end pool 700 all default; HEAD 6fd5edb2 differs in docs only). Background at launch: one node process at about
1 core of 20. `.codex-research/pooldefault-timing-20261007-a/` (`timing.sh`, `compare.py`); all four worlds exit 0.

| world | p99 ms base / HEAD | over 25 ms | mean ms | worst ms | captures red+blue |
| --- | --- | --- | --- | --- | --- |
| Dantooine 1 | 32.25 / 27.50 | 614 / 333 | 6.10 / 4.51 | 82 / 77 | 5 / 11 |
| Dantooine 2 | 37.50 / 27.75 | 892 / 364 | 6.55 / 6.04 | 118 / 78 | 12 / 7 |
| Kejim 1 | 27.25 / 19.50 | 322 / 112 | 5.28 / 4.65 | 221 / 143 | 9 / 12 |
| Kejim 2 | 26.00 / 21.25 | 271 / 162 | 5.05 / 4.78 | 233 / 144 | 4 / 10 |

Frames over 25 ms: Dantooine -53% (1,506 -> 697), Kejim -53% (593 -> 274). The work meter's estimate was about -45%
Dantooine and -65% Kejim; one world each per seed, so the split between maps is within the wall-clock noise noted in
"Timing noise, a work meter". **Kejim meets the 25 ms p99 target on both worlds; Dantooine does not (27.5-27.75 ms,
about 350 frames a world over 25 ms against about 246 for the target).** Kejim's worst frames are still 143-144 ms
(the cross-map replans). The worlds do not replay the baseline's (behaviour changed), so captures are one world each
and not a comparison.
