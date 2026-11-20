# Bot2 review 2026-11-09, lane: graph, planner, forecast and routes

Target: commit 6fe7bb2f, read only from `.codex-research/wt-review-6fe7bb2f`. Read in full: `bot2_graph.c/.h`,
`bot2_forecast.c/.h`, `bot2_route_play.c`, `bot2_route_record.c`, `bot2_probe.c`, `tools/movement_graph_merge.py`;
the planner's call sites in `bot2_move.c` as an interface; `bot2_movecost.c` only for its inactive fast paths.
Nothing was built, launched or edited. Line numbers are at 6fe7bb2f.

Evidence beyond reading comes from retained worlds, read only:

- Kejim guide-on world `hoproom-20261030-a/worlds/on-3` and Dantooine `hoproom-20261030-a/dantooine/on-3`
  (`live/server-console.log`), build `guidedefault-build-20261030-a`.
- The Kejim seed as those worlds loaded it
  (`hoproom-20261030-a/inputs/defense-laps-20261008T205829Z-kej-9-b9543f/server/base/bot2-graph/mp_ctf_kejim_-1301924053.txt`,
  14,249 cells, 94,711 edges).
- Three small Python scripts in `bp-scratch/` beside this file (`gstats.py`, `branch.py`, `slow.py`) reproduce
  every number quoted from the seed and the logs.

Counts: 17 findings. Bugs 6 (P2 2, P3 4), perf 7 (P2 3, P3 4), quality 3 (P3), suggestion 1 (P3). No P0 or P1.
No live correctness defect was found in the searches themselves; the two P2 bugs are about the seed file and the
guidance tables' capacity, and both are latent in laps worlds (each world stages a fresh seed for ten minutes).

## Findings, most severe first

### BP-1 perf, P2, confirmed in a retained world: a goal's first two searches run unguided, and those are the 100-138 ms frames

- Where: `bot2_graph.c:2252-2264` (`GuideWanted`, `bot_movePlanGuideAsks` default 3), used at `:2803`.
- Defect: a goal that is not in the map's `.guide` list gets a guidance table only from its third ask. The first
  two state searches toward it use the straight line at 450 u/s, which on Kejim is the 28,000-expansion cross-map
  search the tables were built to remove.
- Measured (Kejim `on-3`, `slow.py`): 3,227 replans; 19 took 25 ms or more (1,509 ms). 18 of the 19 go to goals
  outside the `.guide` list, and 13 of them (1,206 ms) are that goal's first or second ask. Every replan of 100 ms
  or more is one of those: 138, 136, 133, 128, 124, 123, 107, 104 ms. One goal shows the step directly: cell 12446
  cost 136 ms and 124 ms on asks 1 and 2, then 63, 39 and 44 ms once it had a table. 332 of the world's 514
  distinct goals were asked fewer than three times.
- Cost of the alternative: the load builds 152 tables for 76 goals in 600.6 ms, so about 4 ms a table and 8 ms
  for a goal that also needs the Speed table.
- Fix: want guidance at the first ask when the search is going to be long. `HeuristicOf(from,to)` is already
  computed there (`bot_movePlanGuideMinMs` uses it as an AND); make distance an OR, e.g. guide at once when the
  straight line is over about 2 s, keep three asks for near goals. 71 unpinned slots are free on Kejim (147 less
  76 pinned), evicted least recently used.
- Outcomes: changes which route a first search returns (guided and unguided routes differ in ties and under
  `bot_movePlanGuideMax` 1), so it needs a laps pair; the frame gain is readable from work counts.

### BP-2 perf, P2, confirmed offline on the Kejim seed: a landing state relaxes 43 strafe edges, 39% of them into a state key it has already relaxed

- Where: `bot2_graph.c:2347-2357` (the near-list loop in `StateRun`), `:1829-1842` (`StateStrafeEdge`),
  `:1771-1785` (`StateRelax`).
- Defect: the Kejim seed's 94,701 strafe edges sit on 3,683 cells. For a state arriving on a landing frame
  (`branch.py`, 6,000 sampled edge exits): the near list scanned is 345 entries on average (p99 603), 43 pass the
  tap, speed-band and cone tests (p90 89, p99 127), and those lead to only 26 distinct successor keys (cell, speed
  band, octant). Each admitted edge is a full `StateRelax`: clearance, corridor, bodies, standing, an `atan2f`
  for the key, a hash probe, a guide-table read and usually a heap push.
- Cost: planner time per expansion is 3.3 us on Kejim against 0.66 us on Dantooine in the same build (2.14 M
  expansions in 7,068 ms; 3.87 M in 2,543 ms), and Dantooine's seed has no strafe edges. Even a guided cross-map
  replan on Kejim is 39-92 ms.
- Fix, in rising order of effort:
  1. Inside one expansion, keep the cheapest admitted edge per successor key and relax only that one (a small
     stamp table keyed on the successor key). Same cheapest cost per key; heap ties may order differently.
  2. Bucket each cell's near list by tap and entry-speed band, in list order, so the scan reads only edges that
     can pass (a walking state at a listed cell scans 204 entries to admit 2.6).
  3. Offline, in the seed tools: fold edges that share source neighbourhood, landing block, tap, entry band and
     entry octant, keeping the cheapest. A coarse fold of that kind leaves 61,330 of 94,701 (`gstats.py`). This
     also buys back map-load time, guide arcs (BP-4) and memory.
- Outcomes: 1 and 2 should return the same routes but are not bit-identical in heap order, so check with a replay
  and the `[PLANGUIDE]` mode-2 style compare; 3 changes the graph and needs laps worlds.

### BP-3 bug, P2, confirmed by reading, latent in laps worlds: any failed cache load overwrites the seed with a fresh unseeded build

- Where: `bot2_graph.c:3171-3190` (`Move_PrepareMap`: `Load` false, then `Build` and `Graph_Save` to the same
  path), `:3069-3131` (`Load`'s failure returns), `:2966-2998` (`SaveEdges` opens the live file with `FS_WRITE`).
- Defect: the cache path is the map and checksum alone, while `Load` also refuses on version, grid, `g_gravity`,
  `g_speed`, step, a malformed or truncated line, a size over the cap, and a failed `malloc`. Every one of those
  falls through to a rebuild that is saved over the file it could not read. The pre-lift rebuild (`:3182`) does the
  same. The save itself truncates in place, so a server killed or a full drive during the shutdown flush leaves a
  truncated file, which the next load then replaces.
- Failure scenario: a server started once with `g_speed` 275 or a changed `g_gravity` on Kejim replaces a
  94,711-edge seed with a build that holds no learned edge, silently (one `[GRAPH] map_load ... cached=0` line). Setting the cvar
  back does not bring it back. The same after a `GRAPH_VERSION` bump, or a kill during `cache_flush`.
- Fix: when a cache file exists and fails to load, never write over it: print the reason, and save the rebuild
  beside it (for example with the binding in the name, or `.rebuilt`). Make the binding part of the file name so
  two physics settings keep two caches. Write to a temporary name and swap, or keep the previous file as `.bak`.
- Outcomes: none in worlds (each stages its own seed copy). It protects the installed game's seeds.

### BP-4 bug, P2, confirmed by reading and by the seed's numbers, latent for about 60 world-lengths: guidance switches itself off for good when its arc table fills

- Where: `bot2_graph.c:2012-2018` (`GuideArc` sets `overflow`), `:2036-2051` (`GuideMergeArcs` unlinks merged arcs
  but never compacts, so `count` keeps them), `:2117-2118` (the rebuild condition does not include `overflow`),
  `:2144` (logged edges append new arcs), `:2182` (`GuideFor` returns -1 while `overflow` is set).
- Defect: the arc array holds 1,048,576. Kejim's load fills 852,114 of them (303,248 live and 548,866 merged but
  still counted; the log line and `gstats.py` agree). Every learned or cheapened strafe edge appends 7.9 more.
  When the array fills, `overflow` stays set until the next load, guidance returns -1 for every goal, and nothing
  is printed.
- Failure scenario: 196,462 arcs of headroom is about 24,900 edges; the Kejim world learns 388 in ten minutes, so
  one long session reaches it after roughly 64 world-lengths. Learned edges are saved into the seed, so an
  installed seed creeps toward the point where the load itself overflows (`GuidePreload` then builds nothing) and
  the map runs unguided from then on: Kejim's frames over 100 ms go back from about 2 a world to about 25.
- Fix: compact after `GuideMergeArcs` (the merged 64% come back); on an incremental append update an existing
  (cell, predecessor) arc in place; treat `overflow` as a reason to rebuild rather than to stop; print it.
- Outcomes: none until it triggers.

### BP-5 perf, P2, observed once in the retained world, cause plausible: a state search that cannot reach its goal expands everything before the cell search answers

- Where: `bot2_graph.c:2308-2374` (`StateRun` has no bound but the table), `:2393-2401`; caller
  `bot2_move.c:2323-2331`.
- Observed: Kejim `on-3`, level 334300, client 0: goal 19 cells and 3.6 s away, `expanded=35153`, `state=0`,
  `overflow=0`, 104 ms. The state search emptied its heap and found nothing; the cell search then returned a route
  with one strafe edge.
- Plausible cause: the goal is reachable only over a strafe edge the cell search takes from its own cell without a
  state test, and no state the search can reach is inside that edge's entry band and cone. The guidance table
  cannot prune this (it counts the edge as takeable from any speed). The same shape follows from a goal cell on a
  lift that is away (`Graph_CellStands` refuses every relax into it) or a forbidden box around the goal.
- Fix: the cell route is cheap (a table walk, or a 1-2 ms search) and is the fallback anyway. Get it first and
  give the state search a budget from it, for example stop at some multiple of the cell route's length in
  expansions, or when the heap's lowest key passes the cell route's cost by a margin, then install the cell route.
- Outcomes: a budget changes which searches fall back, so laps worlds; one event a world here, 100 ms each.

### BP-6 perf, P3, confirmed from the load lines: the graph costs about a second of every Kejim map load

- Where: `bot2_graph.c:3090-3118` (one `sscanf` of 20 fields per edge line), `:3044-3063` (`Identity` prints every
  record again), `:2210-2243` (`GuidePreload`), called from `BotAILoadMap` (`ai_main.c:1004`) on every load and
  restart whether or not a Bot2 bot will ever join.
- Measured: `[GRAPH] map_load ... elapsed_ms=453` and `guide_preload ... ms=600.6` on Kejim (28 ms and 153.7 ms on
  Dantooine, whose seed has 538 edges). HANDOFF puts Kejim's whole load at 1.1-1.3 s before the preload became
  the default, and names a load over about 1.5 s as what made setup commands run late.
- Fix: parse records with a hand-written integer and float reader instead of `sscanf` (the format stays text and
  tool-readable); build the preloaded tables a few a frame after the load, or on first ask, rather than all 152 in
  the load frame; skip both until the first Bot2 bot is added.
- Outcomes: the parse is exact. Deferring table builds changes the first seconds of a world.

### BP-7 perf, P3, confirmed by reading, cost estimated: every learned edge copies the whole edge array

- Where: `bot2_graph.c:374-380` (`Autosave` under `bot_moveGraphSaveDefer`, the default).
- Defect: the deferred save's snapshot is `memcpy(saveSnapshot.edge, graph.edge, edgeCount*sizeof)`, 9.5 MB on
  Kejim, on the frame that learned: 388 edge adds plus cheaper merges and first validations in a ten-minute world.
- Cost: about 1-2 ms each on a 2 vCPU host, on frames that already carry a commit.
- Fix: track the lowest edge index changed since the last snapshot and copy from there (appends are at the end),
  or copy only a dirty list. Byte-identical to today's snapshot if every mutator marks its index.
- Outcomes: none.

### BP-8 perf, P3, confirmed by reading, saving plausible: the guard scope copies and compares all 1,024 entity slots

- Where: `bot2_forecast.c:1574` (`GuardBegin` snapshot), `:1266` (the scope's first rollout copy), `:130`
  (`BaselineIntact` at `GuardEnd`).
- Defect: all three run over `MAX_GENTITIES` slots. Slots at or above `level.num_entities` are never handed out
  without `num_entities` rising, and the level struct is compared, so the unused tail is dead weight.
- Cost: the 2026-10-04 counters put the three at 85 + 140 + 80 us a scope, about 43,000 scopes a world, 7% of bot
  time. Bounding them by `num_entities` saves in proportion to the unused slots (not measured here).
- Fix: copy and compare `level.num_entities` slots; refuse the scope's result if `num_entities` changed.
- Outcomes: none (the same differences are detected).

### BP-9 bug, P3, confirmed by reading, small: the pre-search's simulated clock expires edge time-outs for real

- Where: `bot2_graph.c:184-198` (`OffExpire`, `Graph_EdgeOff` read `level.time`); `bot2_move.c:7184` sets
  `level.time` to the predicted boundary and runs the search core, whose reroutes call `Graph_Generation`
  (`:5804`) and `Graph_PlanState`.
- Defect: inside a pre-search `OffExpire` clears `offUntil` on edges due before the predicted time, permanently,
  bumps the generation and reach topology, queues route-table changes and prints `edge_back` with a future level.
  Guide slots also stamp `foldAt` with that time. The pre-search is meant to leave no trace; area 8's F3 was the
  same class of leak in the mover memo.
- Failure scenario: an edge returns to every bot's search up to one deferral lead early, and when it returns
  depends on whether some bot happened to pre-search. Timing only; a replay is unaffected because it repeats.
- Fix: pass the real frame's time to the graph (a `Graph_FrameTime` set once in `G_RunFrame`, as `Bot2_MoverFrame`
  is) and read that in `OffExpire`, `Graph_EdgeOff`, `Graph_EdgeFail` and the fold timers.
- Outcomes: moves when timed-off edges return by a few frames; needs a laps replay to confirm it is noise.

### BP-10 bug, P3, confirmed by reading, latent (needs a bad cache file): loaded edges are not validated beyond their cells

- Where: `bot2_graph.c:3108-3116` and `:643-654` (`AddEdgeRecord` checks `from`, `to` and nothing else);
  `tools/movement_graph_merge.py:117-118`.
- Defect: `type`, `costMs`, the floats and a lift edge's `tap` (its inline model) are taken as read. A zero or
  negative `costMs` breaks the route tables' Dijkstra: `PathSettle` (`:2511-2533`) can then improve a settled cell
  again, and its heap is sized for one push per arc, so a negative cycle writes past it. `nan` parses as a float.
  The merge tool writes whatever edge count it folds to: over 131,072 the game refuses the file, which under BP-3
  means the seed is replaced. It also merges caches of different versions (the version is not in its binding).
- Fix: at load refuse `type` outside 1-7, `costMs` under 1, non-finite floats, a lift `tap` outside
  `1..MAX_MODELS-1`; in the tool refuse a result over the cap and mixed versions.
- Outcomes: none on valid seeds (the Kejim seed has costs 50-3,787 and no bad record).

### BP-11 bug, P3, confirmed by reading, latent on the prepared maps: a route longer than 1,024 cells is reported as no route

- Where: `bot2_graph.c:1570` (`PathSearch`), `:2382` (`StateResult`), `:2719` (`PathFromTable`).
- Defect: all three return 0 when the route does not fit `GRAPH_MAX_PATH`, the same answer as unreachable. Callers
  then treat the goal as unreachable (`ReachableDrop`, the defenders' route cost) or replan every
  `MOVE_PATH_RETRY`, each a full search.
- Reach: Kejim's longest cross-map routes are 133-166 cells; a map near the 32,768-cell cap with a winding route
  could pass 1,024.
- Fix: return the first 1,024 cells as a partial route (the bot replans on the way), or report the overflow
  separately as the state table's is.
- Outcomes: none on the prepared maps.

### BP-12 bug, P3, confirmed by reading, conservative: the rollout's Force regeneration ignores the Force boon

- Where: `bot2_forecast.c:1018-1023` against `w_force.c:5452-5461`.
- Defect: natively a client holding `PW_FORCE_BOON` regenerates through `WP_ForcePowerRegenerate(self, 6)`; the
  rollout adds one point a tick. A rollout from a root holding a boon under-predicts Force, so a Force jump or
  Speed purchase late in a rollout can be refused that the game would allow.
- Reach: a boon pickup still refuses a rollout, so this is only while a bot holds one.
- Fix: mirror the boon case (and nothing else: the Jedi Master, holocron, Siege and power-duel branches cannot
  occur in CTF).
- Outcomes: only on maps with a boon; none on Kejim or Dantooine if neither has one (not checked).

### BP-13 quality, P3, confirmed by reading: the cache binding is narrower than ARCHITECTURE says, and edge times are tick-quantised

- Where: `bot2_graph.c:2938-2942` and the header at `:2972`; `docs/movement/ARCHITECTURE.md:56-58`;
  `bot2_move.c:2501` and `:2538` (`frames*Bot2_GameStepMsec()`); `GRAPH_STATE_STEP_MS` and `GRAPH_PAD_STEP_MS` 25.
- Defect: ARCHITECTURE binds the cache to "gravity, base speed, pmove step, Force levels". The header binds
  gravity, `g_speed` and `STEPSIZE` (its `step=` is the stair height, not the pmove step). A seed learned at
  `sv_fps` 40 with 8 ms bot commands loads on any tick rate; its strafe edges carry entry and exit speeds and
  whole-frame costs measured at that rate (every cost in the Kejim seed is a multiple of 25 ms), and the walk
  law and pad arcs are integrated at a fixed 25 ms.
- Why it holds today: `addrunner` forces `sv_fps` 40, and an edge is rolled out before it is flown, so a wrong one
  is refused rather than executed. The cost is searches spent on edges that no longer fit, on a server at
  another tick rate.
- Fix: either add the bot's command step and `sv_fps` to the binding, or record them in the header and say in
  ARCHITECTURE that the cache is tick-specific. Under the owner's tick-independence rule the first is safer.
- Outcomes: none at `sv_fps` 40.

### BP-14 quality, P3, confirmed by reading: split state searches cannot start under the default guidance

- Where: `bot2_graph.c:2897` (`Graph_PlanSplitStart` returns 0 when `GuideMode()` is non-zero), `:2857` (four
  tables of about 11 MB).
- Defect: since guidance became the default (2026-10-30) `bot_movePlanSplit` is a silent no-op unless guidance is
  also turned off, and its 44 MB of tables are never touched. The two are the only remedies for the searches BP-1
  and BP-5 describe, and they exclude each other.
- Fix: decide. Either let a split search carry its guide slot (hold the slot's tables for the search's life), or
  remove the split tables and the cvar.
- Outcomes: none as it stands.

### BP-15 quality, P3, confirmed by reading: dead data and stale text in the graph

- Nodes: `Runs`, `Widths`, `Graph_Node`, `Graph_NodeCount` and the cells' `node[2]` (`bot2_graph.c:594-625`,
  `:264`) have no reader outside the save, the load and the report. They are built, written, parsed and bound into
  the identity for nothing at run time.
- The guidance header (`:1959-1960`) says the tables ignore the clearance penalty; `GuideSettle` adds it (`:2093`)
  and the slot is keyed on it. The code is right, the comment is not.
- `movement_graph_merge.py:58-63` says it folds strafe edges "as the game folds them" but buckets entry speed
  (`entry // 48`), where the game merges within 48 u/s of an existing edge; two edges 2 u/s apart on a bucket
  boundary stay separate. Part of why 10,030 (from, to, tap) pairs in the Kejim seed hold more than one edge.
- `Graph_CorridorLoad`'s name test (`:227`) and `StateAvoided`'s unused arguments are still as area 4 listed.
- Outcomes: removing nodes changes the cache text and so every graph identity; do it only with a version bump.

### BP-16 perf, P3, confirmed by reading, small: whole-graph scans on per-query paths

- `PathMoverStanding` (`bot2_graph.c:2623-2628`) walks every cell on each route-table query and build step
  (6,000-7,800 queries a world) to sum the lift cells. Keep the list of mover cells from the load.
- `Graph_BodiesEncloseGoal` (`:1511-1520`) walks every cell, then every edge record (9.5 MB on Kejim) for
  `to==goal`. The cells' own links and the route tables' `edgeHead` answer both from the goal outward.
- Outcomes: none.

### BP-17 suggestion, P3: decide climb edges at preparation

- Where: `bot2_graph.c:772-831` (`Climbs`: any wall rise from 48 to 440 u becomes an unvalidated edge).
- Context: HANDOFF's frame-speed candidate, about 1,000 climb searches a Dantooine world that find nothing (66
  rollouts each, about 13 s a world), by campers and defenders on 256 u walls. Guests charge nothing to the graph,
  so the same edge is searched again and again.
- Suggestion: have map preparation roll each climb edge once per Force jump level from its takeoff cell and store
  the lowest level that reaches (a field on the edge, or `validated` per level). The planner then skips a climb the
  bot's level cannot make, and the executor searches only those that can succeed. This is offline work of the kind
  `movement_graph_prepare.py` already does for other edges.
- Outcomes: changes routes for bots below a climb's level; laps worlds.

## Earlier review's items, re-checked

- G1 (cell-search heap bound): holds. Each cell closes once, each arc relaxes at most once, so pushes are bounded
  by `cells*8 + edges + 1` as sized.
- G2 (`Graph_LinkedWithin` breadth first) and G3 (`PushTarget` without `rand()`): present and correct.
- F6 (`FallDamage` ring limit), M2 (saber-lock root refused), M4 (flag pass in the replay key): present and
  correct. The counterfactual hide in `SearchFailed` runs between guard scopes, so no replay record crosses it.
- The route-table batch fold (`PathFold`) was re-derived, including an edge that changes while a table is still
  building: correct.
- Area 4 had no open items. Its cleanup candidates (the unused `StateAvoided` arguments, the corridor name test,
  the split tables) are still there; BP-14 and BP-15 carry them.

## Needs a high-effort follow-up

1. BP-2: measure where a Kejim expansion's 3.3 us goes (scan, relax, heap, table reads) with the cost fixture
   before choosing between the per-expansion fold, the banded lists and the offline fold; then a laps pair.
2. BP-1 with BP-5: one change to how a replan is budgeted (guide at first ask by distance; cell route first, state
   search bounded by it). Worth designing together, measured by work counts on 16 paired Kejim seeds.
3. BP-3: a seed-file policy for the installed game (binding in the name, no overwrite of an unreadable file,
   atomic save), since it touches what `tools/` stage and what the install ships.
4. BP-13: what a seed is bound to, against the tick-independence rule. Needs worlds at `sv_fps` 20 and 30 on a
   40-fps seed to see how many planned edges are refused at rollout.
5. Forecast against native Pmove under the competitive Force defaults: I compared the Force update, the generic
   command and regeneration by reading only. A parity run (`bot_forecastReplay` 2 and the parity harness) on a
   world with absorb, protect and a boon in play would settle BP-12 and anything like it.

## Not covered

- `bot2_movecost.c` beyond its inactive fast paths, and `bot2_movecost.h`; area 8 read them on 2026-10-08.
- The fixtures' behaviour in a running world (`bot2_route_play.c`, `bot2_route_record.c`, `bot2_probe.c` were read
  and nothing new was found; nothing was run).
- `tools/movement_graph_enrich.py`, `movement_graph_prepare.py`, `movement_graph_report.py` and
  `movement_guide_goals.py` (only the merge tool was read for the format contract).
- `bot2_mover.c` and `Bot2_GroundSample`, which the graph build depends on; `Bot2_Pmove` slicing.
- Graph build quality on maps other than Kejim and Dantooine (cell caps, pad and lift discovery on the five lift
  maps); no seed for them was inspected.
- Whether `level.num_entities` is small enough for BP-8 to matter, and whether Kejim or Dantooine carries a Force
  boon (BP-12).
- Thread safety and the parallel-rollout proposal.
