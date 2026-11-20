# Replan budget: guide a long search, bound it by the cell route, cache the frame-end pool (2026-11-09)

Source: the 2026-11-09 Bot2 review (`.codex-research/review-20261109-SUMMARY.md`; lane reports
`review-bot2-20261109/plan.md` BP-1, BP-5, BP-14 and `move.md` BM-1), reviewed at 6fe7bb2f. Branch
`claude/bot2-replan-budget-be5722`. Evidence and scripts: `.codex-research/replan-budget-20261109-a/`.

This record was written in two steps: the design and its expected effect first (this section and the next two),
the measurements after.

## The three review items, confirmed on this branch

- **BP-1** (`GuideWanted`, bot2_graph.c): a goal outside the map's `.guide` list gets a guidance table only from its
  third ask (`bot_movePlanGuideAsks` 3). Its first two state searches use the straight line at 450 u/s.
- **BP-5** (`PlanStateSearch` -> `StateRun(0)`): the state search stops only on an empty heap, an overflow, the goal or
  a join. A goal no state can reach is answered by the cell search (`Replan`, bot2_move.c) only after the state
  search has expanded everything it can reach.
- **BM-1** (`Move_PoolFrameEnd`, bot2_move.c): the frame-end pool chunks open the threat-hide scope only. The trace
  cache, the point-contents cache and the corridor memo are opened only in `Bot2_MoveParityBeforePmove`, so the
  chunks that `bot_moveWalkPoolFrameEnd` 700 (default) moves to the frame's end run every trace on the engine.

## Offline first: what the retained worlds say about BP-1

Eight guide-on Kejim worlds are retained (`hoproom-20261030-a/worlds/on-3..10`, the build of the 2026-10-30 default).
Their `[MOVE] event=plan reason=path` lines carry each replan's cells and expansions; `offline.py` and `offline2.py`
(outputs `offline.txt`, `offline2.txt`) classify them by the goal's ask number, the `.guide` list and the straight
line's time.

**The straight line does not predict a long search.** The brief's first idea was to guide a goal at once when it is
far by the line (the review suggested over about 2 s). In the unguided searches (unpinned goal, ask 1 or 2):

| world | unguided searches | of them 10,000 expansions or more | ... with a line of 2 s or more | tables a 2 s rule would build |
| --- | --- | --- | --- | --- |
| on-3 | 691 | 11 | 4 | 143 searches, 118 goals |
| on-4 | 601 | 2 | 0 | 131 searches, 99 goals |
| on-5 | 655 | 10 | 3 | 155 searches, 124 goals |

Most long searches are to goals under 1 s away by the line (on-3: 5 of 11; on-5: 6 of 10; on-4: both): the goal close
above or below, which is also what the comment on `GuideWanted` says the line is worst at. And of the searches a 2 s
rule would guide, nine in ten finish within about 4,500 expansions unguided (line 2-3 s: 90th percentile about
1,150 expansions; 3-5 s: about 1,600; over 5 s: 4,500-9,300), while a goal's two tables cost about 8 ms on Kejim
(152 tables in 600.6 ms at the load, 14,249 cells: 3.95 ms a table, about 0.28 us a settled cell), which is about
2,700 state expansions at 3 us each. A distance rule builds a hundred tables a world that buy little and misses
most of what it was for.

**What predicts a long search is that it is running long.** So the trigger is the search's own count:

| threshold N | unguided searches reaching N, a world | goals | their expansions past N, share of all replan expansions |
| --- | --- | --- | --- |
| 2,000 | 28-67 | 21-55 | 5-18% |
| 4,000 | 13-42 | 10-31 | 3-14% |
| 8,000 | 3-23 | 3-16 | 2-11% |

The last column is the ceiling on what guidance can save there (the guided search still costs something).

## Design: a replan's state search runs in stages

One rule: **a state search that has not finished after N expansions is long, and a long search is treated
differently from then on.** Short searches (over 95% of replans) run exactly as today, at no added cost. Each part
has its own cvar, default off; with all three unset the code path is the old one.

### Part A: a long unguided search gets its goal's table (`bot_movePlanGuideLong N`, default 0 = off)

In `Graph_PlanState` under `bot_movePlanGuide` 1, when the search starts unguided (`GuideWanted` says no):

1. Run it for N expansions (`StateRun(N)`). Finished: it is the unguided search exactly.
2. Not finished: build the goal's table(s) (`GuideFor`, the slot rule and eviction unchanged), start the search
   again with them, and run it to its end. The route is the one a guided search returns, the same as the default
   already returns from the third ask on. The first N expansions are the price of finding out.
3. A later search toward a goal that still holds a valid table uses it from the start, whatever its ask number, so
   the N expansions are paid once a goal, not once a search.

The ask count stays as it is (three asks still guide a goal whose searches are short); A only adds tables.
`[PLANLONG]` prints one line per escalation. Required-edge searches (fixtures) and `bot_movePlanGuide` 0 or 2 are
untouched.

Why not on distance or on the cell route's cost: the table above for distance; the cell route's cost would need a
cell search before every replan (3,200-3,300 a world), which is the work BP-5's part avoids too.

### Part B: a long search is bounded by the cell route (`bot_movePlanBound N`, `bot_movePlanBoundPct P`, default 0 = off)

In the same staged search, for a search with no required edge:

1. After N expansions without an end, run the cell search (the fallback the replan would need anyway) and keep
   its route.
2. The state search goes on, and stops when the lowest key in its heap passes P% of the cell route's cost. Nothing
   it could still find is then cheaper than that. No cell route: no bound, the search runs whole as today.
3. Stopped: the caller installs the kept cell route (`Graph_PlanBoundRoute`, taken by `Replan` and
   `RerouteSearchFor` in place of their `Graph_Path` call; same search, same bodies and forbidden volume, so the
   same route as today's fallback).

The brief said "run the cell route first". Deferring it to the Nth expansion gives the same bound on every search
that needs one and spares the cell search on the rest.

The two costs are not in the same model (the cell search walks at a fixed price and takes a strafe edge from its
cell without a state test; the state search prices the walk from its speed and enters an edge only from inside its
envelope, and it honours the bot's avoided edges, which the cell search does not), so P is a margin, not an
identity, and a real state route dearer than the bound is replaced by the cell route. `bot_movePlanBoundCheck 1`
measures that without changing a decision: the search runs whole, and `[PLANBOUND]` prints the cell cost, the
expansion count at which each of 100/125/150/200/300% would have stopped it, and what the whole search found. One
control arm gives the whole trade curve; P is chosen from it before any bounded arm runs.

A and B compound: with A, the long search is guided by then, its keys are near true remaining times, and the bound
closes quickly; unguided, keys are the straight line and the bound closes late.

### Part C: frame-end pool chunks inside the cache scopes (`bot_movePoolScopes`, default 0 = off)

`Move_PoolFrameEnd` opens `Bot2_ForecastTraceCacheBegin` and `Bot2_GroundCorridorMemoBegin` per bot after
`Bot2_ThreatHideBegin` and closes them before `Bot2_ThreatHideEnd`, as the boundary does. One scope a bot, as at the
boundary; nothing moves between a chunk's rollouts (the transient hides inside flush both, bot2_move.c:4380).

### Shared fields and consumers

No `bot2_state_t` field changes meaning. `bot2_movePath_t.expanded` (the plan line's `expanded=`) keeps its meaning,
the state expansions the replan spent: under A it includes the N discarded ones. `path.state` 0 with a cell route
after a bound stop is today's fallback state. The kept cell route is cleared by every `Graph_PlanState`,
`Graph_BodiesSet` and `Graph_ForbidSet`, so a caller can only take the one made for its own search. Split searches
(`bot_movePlanSplit`, off; BP-14) do not pass through the staged search and are unchanged.

### Meter additions (so the cost of A and the gain of C can be counted)

The work meter does not count guidance tables (`GuideSettle`), and it counts forecast traces whether or not the
engine ran them. Added, all counts: `guide_pops` (settled cells of table builds and folds; `est_us` adds 0.28 us
each), `engine_traces` (traces the trace cache did not answer), `corridors` (corridor evaluations the memo did not
answer), as totals on the `[FRAMEWORK]` line. `est_*` therefore reads a little higher than on earlier builds in
every arm, the control included.

## Expected effect (written before measuring)

| part | work counts | outcomes |
| --- | --- | --- |
| A, N 4,000 | Kejim: 13-42 escalations a world, 20-60 more tables; the frames holding an unguided 25,000-35,000 expansion replan (2-11 a world, the 100-138 ms frames) fall to N + the guided search; the worst frame's expansions fall from about 35,000 toward the guided worst (19,000-26,000). Total replan expansions about -5%; frames of 10,000 expansions or more fewer. Dantooine: little (538-edge seed, short searches). | Route changes only where a long search now returns the guided route. The 2026-10-30 pair found no outcome cost for guided routes; expect none beyond noise. |
| B | The one unreachable goal a Kejim world (33,000-35,000 expansions, about 100 ms) stops early: under A at a few thousand, alone later. Unknown: how many guided searches over N the bound would cut and at what P a real route is lost; the check mode answers before a bounded arm runs. | None at a P where the check shows no lost route; otherwise a cell route where a slow state route was, counted. |
| C | Engine traces and corridor evaluations fall in the frame-end chunks; by how much is unknown (the hit rate between sibling candidates was never measured). Steps and expansions identical. | None: the worlds must replay exactly. |

## Plan of measurement

One fresh build, sources frozen. Work counts only (no wall-clock runs: owner may be playing). Worlds are the
milestone-B 4v4 (`laps_campaign.COMMON` + `--two-sided`), 600 game s at rate 25, four at once,
`bot_moveFrameMeter 1` in every arm.

1. **C**: identity. Control and `bot_movePoolScopes 1` on the same seeds (Kejim and Dantooine), `laps_identity.py`,
   then the engine-trace totals.
2. **Control with the B check on** (decisions are the control's): 16 Kejim seeds (3-18) and 8 Dantooine (3-10).
3. **A** (N 4,000, B check on): the same seeds. Work counts paired by seed; outcomes by `laps_compare.py`.
4. **A + B** at the P the checks support, the same seeds; B alone on Kejim if the check says it matters there.

---

# Measured (2026-11-09)

Everything above this line was written before the first world ran and is left as it was; where the measurements
changed the design, the change is in the next section.

Builds, each fresh and unique from a commit of this branch: `replan-budget-build-20261109-a` (06a97202), `-b`
(af8caa53), `-c` (83df5d56, the branch's code). 157 worlds, none failed, every `setup_late_commands` 0; C: had
240 GB and D: 132 GB free at the start. All counts, no wall-clock run. Tools: `tools/laps_work.py` (new: paired
work counts and hop flow, `--bounds` for the check curve), `tools/laps_identity.py` (its decision compare stopped
at the guide preload's wall-clock `ms=` on every pair since guidance became the default; fixed),
`tools/laps_compare.py`. In the evidence folder: `identity*.txt`, `work-*.txt`, `compare-*.txt`, `offline*.txt`,
the runners (`runarms.py` for build -a; `runarms-final.py` for builds -b and -c, which shares the machine's four
server slots with another session's run; `pooled.py`) and their logs, `worlds/` (build -a), `worlds-b/`,
`worlds-c/`.

Seeded worlds replay across these builds (build -a's `pure` worlds of seeds 3-4 replay on build -b, 4 of 4), so the
second round used fresh seeds: its worlds are new outcome evidence, not copies of the first.

## What the design got wrong, and what changed

1. **The cell search is not cheap.** Build -a took the cell route at the Nth expansion of every long search. With
   the check on (decisions unchanged: `pure` and `ctl` replay, 4 of 4) that alone cost Kejim +229,800 expansions a
   world (+5.6%; 2,630 a search, 88 searches) and Dantooine +1,690,000 (+21.9%; 4,660 a search, 362 searches), and
   raised the worst frame's expansions 20% and 39%. Since build -b the bound is armed at N but the cell search runs
   only once the heap's lowest key has passed P% of the root's own estimate. In the A+B arms that is 4.6 cell
   searches a Kejim world and 1.5 a Dantooine world.
2. **The two costs differ by more than a margin.** Over the long searches the state route costs a median 120-153%
   of the cell route on both maps. A bound at 150% would swap a state route for the cell route 25 times a Kejim
   world and 179 times a Dantooine world. Only 300% separates: what it stops is a search that finds nothing, or a
   state route 3.2 to 35 times the cell route's cost (median over 13 times on Kejim: a 14-35 s route to a goal
   1-2 s away by the cell route, most of them planned around a body).
3. **The cell route ignores the bot's avoided edges.** A cell route through an edge the search avoids now bounds
   nothing (build -b).
4. **A goal neither search reaches paid for the cell search twice** with the bound on (build -b: three Dantooine
   worlds' worst frame +33,400 expansions, two whole-graph cell searches). The kept answer can now be "no cell
   route" (build -c): those three worlds' worst frames are back at the A arm's, and build -c replays build -b's
   A+B worlds, 5 of 5.
5. **Dantooine's long searches are guided ones.** 354 state searches a world reach 4,000 expansions there, against
   90 on Kejim, and nine in ten of them already have a table. Neither part touches those; see "Left".

## Part C: `bot_movePoolScopes 1`

| | Kejim (seeds 3, 4) | Dantooine (seeds 3, 4) |
| --- | --- | --- |
| replays the control (game log and 148,000-151,000 decision lines) | 2 of 2 | 2 of 2 |
| forecast traces run by the engine, a world | 67.4M -> 48.4M (-28.2%) | 74.8M -> 55.1M (-26.4%) |
| corridor evaluations run, a world | 4.77M -> 4.18M (-12.3%) | 4.89M -> 4.22M (-13.8%) |
| steps, expansions, replans, hops | identical | identical |

On top of A+B (build -b, same seeds): replays 4 of 4, engine traces -28.6% and -22.5%; on build -c, 2 of 2. (One
of those pairs, Dantooine seed 4, is a standoff world whose game log ends at 520 s; compared with
`--min-seconds 500`.) That is about 19 million traces a world, some 780 a frame on average and concentrated in the
frames that run pool chunks. The expected effect held; its size was the unknown. What it is in milliseconds needs
the timed run.

## Part B's check curve (build -a, `bot_movePlanBoundCheck 1`, control arm)

Stopping at P% of the cell route's cost, had the cell route been free:

| P | Kejim, 16 worlds: stops a world / expansions saved a world / state routes lost | Dantooine, 8 worlds |
| --- | --- | --- |
| 150% | 25.4 / 75,300 / 396 in the 16 worlds | 178.9 / 360,800 / 1,423 in the 8 worlds |
| 200% | 7.6 / 42,400 / 111 (their cost 200-3,500% of the cell route) | 11.6 / 25,700 / 85 (200-367%) |
| 300% | 4.0 / 36,600 / 53 (322-3,500%, median over 1,300%) | 1.1 / 17,900 / 1 (367%) |

The control's long searches that found nothing: 11 in 16 Kejim worlds (418,000 expansions, 38,000 each) and 8 in 8
Dantooine worlds (172,800). P is 300 in every bounded arm.

## Part A: `bot_movePlanGuideLong 4000`

Kejim, two independent sets of 16 paired seeds (the first with the check's cell searches in both arms):

| per world, control -> A (paired difference +- se) | seeds 3-18, build -a | seeds 19-34, build -b |
| --- | --- | --- |
| escalations / tables built / expansions discarded | 16.7 / 30.1 / 66,750 | 16.4 / 28.4 / 65,500 |
| replans of 25,000 expansions or more | 5.19 -> 1.44 (-3.75 +- 1.62) | 6.56 -> 1.13 (-5.44 +- 1.84) |
| largest replan | 34,602 -> 24,238 (-10,365 +- 2,364) | 32,526 -> 26,897 (-5,629 +- 2,785) |
| frames of 30,000 expansions or more | 6.6 -> 2.9 (-3.75 +- 1.74) | 2.9 -> 1.4 (-1.50 +- 0.61) |
| frames of 20,000 or more | 14.9 -> 8.8 (-6.2 +- 1.8) | 12.9 -> 8.1 (-4.8 +- 3.1) |
| expansions, worst frame | 49,788 -> 37,583 (-12,205 +- 3,418) | 40,855 -> 33,639 (-7,217 +- 3,052) |
| est. worst frame | 162.8 -> 131.2 ms (-31.6 +- 10.4) | 141.6 -> 119.0 ms (-22.6 +- 8.9) |
| expansions a frame, 99.9th percentile | 16,031 -> 14,113 (-1,919 +- 898) | 14,356 -> 12,800 (-1,556 +- 1,089) |
| est. frames over 25 ms | 577.8 -> 595.6 (+17.8 +- 14.3) | 591.0 -> 574.1 (-16.9 +- 9.4) |
| est. p99 | 31.6 -> 31.9 ms | 31.5 -> 31.3 ms |
| expansions, world total | -0.9% +- 3.1% | -5.2% +- 2.1% |
| guide table cells settled | 11.82M -> 11.83M | 12.15M -> 12.29M (+1.1% +- 1.7%) |
| runner hops / chained hops / speed u/s | -12.5 +- 9.6 / -9.3 +- 7.3 / -2.3 +- 4.5 | -4.1 +- 9.9 / -3.4 +- 8.2 / -5.3 +- 4.9 |

Against the expectation: escalations inside the predicted 13-42 a world; the 25,000-expansion replans mostly gone
(-72%, -83%), as expected; the worst frame's work down 18-25%, but only to 33,600-37,600 expansions and not to the
guided worst of 19,000-26,000, because a worst frame holds more than one search; frames over 25 ms unmoved (the two
sets disagree in sign; together nothing), where I expected "fewer". Not predicted: long searches that find nothing
fell from 11 to 3 over the first set's 16 worlds, since a guided search exhausts sooner.

Dantooine (seeds 11-18, build -b): 39.8 escalations a world, 63.4 tables (guide table work +7.4% +- 2.6%), 159,000
expansions discarded; est. frames over 25 ms 1,658.9 -> 1,724.4 (+65.5 +- 67.1); worst frame's expansions
63,783 -> 54,910 (-8,873 +- 17,944); replans of 25,000 or more 0.75 -> 0.63; hops +15.6 +- 13.9, speed
+7.7 +- 3.9 u/s. Seeds 3-10 on build -a read the same once seed 4 is set aside (its A world is a CTF standoff from
8:40 on, with the camper replanning every frame: est. frames over 25 ms +470 +- 453 with it, +20 +- 55 without).
No gain and a small cost, as expected.

Outcomes, A against its control, `laps_compare` (Welch), both sets pooled (`pooled.py`):

| per 600 game s | Kejim, 32 + 32 worlds | Dantooine, 16 + 16 worlds |
| --- | --- | --- |
| team captures | 3.80 -> 3.53 (-0.28 +- 0.25) | 4.74 -> 4.46 (-0.28 +- 0.36) |
| runner captures | -0.29 +- 0.21 | -0.28 +- 0.33 |
| camper captures | +0.01 +- 0.18 | +0.00 +- 0.23 |
| both runners' falls / weapon deaths | +0.45 +- 0.69 / -0.16 +- 0.93 | +0.55 +- 0.55 / +1.98 +- 1.07 |
| verdict | undecided | undecided |

Undecided on both maps, with both point estimates down (-7% and -6% of team captures). The reroute joins read the
same way at 16 seeds on 2026-10-07; more seeds cleared Dantooine and left Kejim undecided. It is not cleared here.

## Parts A + B: `bot_movePlanGuideLong 4000`, `bot_movePlanBound 4000`, `bot_movePlanBoundPct 300`

Build -b, fresh seeds (Kejim 19-34, Dantooine 11-18), against the same controls:

| per world | Kejim: control -> A+B | Kejim: A -> A+B | Dantooine: A -> A+B |
| --- | --- | --- | --- |
| cell searches for the bound / searches stopped | 4.6 / 4.4 | | 1.5 / 1.1 |
| largest replan | 32,526 -> 19,670 (-12,856 +- 2,336) | -7,227 +- 3,064 | 24,198 -> 17,210 (-6,989 +- 3,138) |
| expansions, worst frame | 40,855 -> 31,620 (-9,236 +- 4,288) | -2,019 +- 3,573 | unchanged on build -c (three worlds checked) |
| est. worst frame | 141.6 -> 111.6 ms (-30.0 +- 12.4) | -7.4 +- 10.9 | |
| est. frames over 25 ms | 591.0 -> 580.4 (-10.6 +- 14.4) | +6.4 +- 10.7 | -0.3 +- 0.6 |
| expansions, world total | -1.9% +- 3.1% | +3.5% +- 3.5% | +0.2% +- 0.3% |
| runner hops / chained hops / speed | -6.1 +- 10.2 / -7.1 +- 8.4 / -5.6 +- 4.2 | | identical |
| team captures (control -> A+B) | 3.91 -> 3.79 (-0.12 +- 0.29) | | 4.37 -> 4.50 (+0.12 +- 0.62) |

On Dantooine the bound stopped only searches that find nothing, each at 4,000 guided expansions, so the A+B worlds
replay the A worlds (same hops, same frames over 25 ms) with those searches' expansions 54,150 -> 34,470 a world.
On Kejim it also swaps about four state routes a world for the cell route (the 3 to 35 times detours). The largest
replan falls a further 27-29% on both maps; no frame percentile shows it.

**B alone** (Kejim seeds 19-26, 8 pairs): 12.8 cell searches and 3.0 stops a world, largest replan 32,585 -> 29,452
(-3,133 +- 2,100), est. frames over 25 ms -14.0 +- 12.9, team captures +0.13 +- 0.56. Nothing decided: unguided,
the keys are the straight line's and the bound closes late.

## Recommendation, per cvar

As measured, every default was off. **`bot_movePoolScopes` is on by default since the owner's word on 2026-11-09**
(81d3cf4a, build `replan-budget-build-20261109-d`): unset, that build replays build -a's scopes-on worlds of Kejim
seed 3 and Dantooine seed 3 (game log and 148,212 / 151,049 decision lines; `identity-d.txt`), with the on arm's
trace counts (Dantooine 54,500,598 engine traces in both, against 74,998,363 with it off). The other two stay off.

| cvar | recommendation | why |
| --- | --- | --- |
| `bot_movePoolScopes` | **default 1** | Lossless: 10 of 10 pairs replay (on the defaults and on A+B, three builds, both maps). 22-29% fewer engine traces a world, 12-14% fewer corridor evaluations. Nothing is traded. |
| `bot_movePlanGuideLong` | **stay 0 for now**; 4000 is the value to carry forward | Kejim's 25,000-expansion replans mostly go and the worst frame's work falls 18-25%; those are the 100-138 ms frames the review named. But estimated frames over 25 ms and p99 do not move, Dantooine gains nothing and pays 7% more table work, and team captures read -0.28 +- 0.25 and -0.28 +- 0.36: undecided, both down. A default needs the timed Kejim pair and the capture question closed. |
| `bot_movePlanBound` 4000 with `bot_movePlanBoundPct` 300 | **stay 0; turn on with A if A is taken** | With A it cuts the largest replan a further 27-29% on both maps for 1.5-4.6 cell searches a world, and replays A's worlds on Dantooine. Alone it does little. P stays 300: at 150 it swaps hundreds of ordinary state routes for cell routes. |

## Left

- **Timed runs** (not done: the owner may be playing). Pinned two-core, run alone, seeds 1-2 as on 2026-10-07 and
  2026-10-30: `bot_movePoolScopes` 0/1 on both maps (the only way to turn 19 million traces a world into
  milliseconds; pool chunks were a third of Dantooine's slow time), then A and A+B on Kejim for frames over 50 and
  100 ms.
- **A's captures.** `laps_campaign.py` on A against the defaults until decided, Kejim first; the 64 Kejim and 32
  Dantooine worlds here are its first rounds.
- **Dantooine's guided long searches** (new): 354 searches a world reach 4,000 expansions, 320 of them already
  guided, about 2.4 million expansions a world in them, 34 replans a world of 10,000 or more. The table's bound
  does not shorten them. This, not a goal's first two asks, is Dantooine's replan tail.
- **Guidance table work was invisible to the work meter** (now `guide_pops` on `[FRAMEWORK]`): 12-15 million
  settled cells a world on the defaults, up to 41,000-77,000 in one frame, about 11-21 ms at 0.28 us each. Builds
  and folds could be spread over frames as the route tables' are (BP-6 asks the same of the load).
- **Standoff worlds**: in a CTF standoff (both carriers waiting at home) the camper replans every frame at a 256 u
  wall climb that finds nothing (2,400 replans a minute in `a-dan-4` and `ab-dan-4`, a short episode in
  `ctl-dan-4`). HANDOFF lists Dantooine's failing climb searches; this is their worst case. `laps_identity.py`
  calls such a world truncated (its game log goes quiet) unless given `--min-seconds`.
- BP-14 (split searches cannot start under guidance) is untouched; the staged search does not use the split tables.
