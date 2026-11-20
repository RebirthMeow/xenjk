# What the boundary tail is: cache writes at the top, whole searches below it

Recorded 2026-09-22. Read-only analysis of retained evidence; nothing was
built, run or changed for it. It answers the question the
[search-budget record](2026-09-22-search-budget.md) left open ("whatever
costs 100 ms to 2 s at that boundary ... is not the candidate rollout loop";
"naming it is the next measurement") for
[level-1 exit gate](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21) row 5.

> **The frames of half a second and more are not search at all.** In the four
> production-cache cost worlds (`.codex-research/search-budget-20260922-c/`
> `C-budget-off`, `C-budget-on`, `C2-budget-off`, `C2-budget-on`; below
> off-1, on-1, off-2, on-2) there are six frames of 0.5 s or more, and the
> worst frame of every world is one of them. **Four of the six are in
> `dispatch_us`** - `Bot2_ExecuteMovement`, where no straight search runs:
> 2,411,857 us (off-1), 1,488,735 (on-2), 661,869 (off-1) and 555,437 us
> (off-2), each with 12 to 17,902 us of boundary work beside it. **The same
> event at the same elapsed time and bot costs 52,478 and 75,663 us in the two
> sibling worlds** where the 2.41 s frame occurred, and 24,805 us where the
> 555 ms one did: the work is deterministic, the extra 0.5 to 2.4 s is not.
> The two boundary members (3,422,908 us on-1, 1,721,012 us on-2) also fall
> between the two edge prints of one learn, and the 3.42 s one is pinned
> exactly: the log prints edge 706
> (`tap=0`, so hop 0) at 09:30:22 and edge 707 (hop 1, taking off where 706
> lands) at 09:30:26, **two prints of one `LearnStrafe` call four seconds
> apart, with nothing between them but `Graph_Save`**, and the 3,430 ms hitch
> follows. The same pair of edges is learned in off-2 with both prints in one
> second and no hitch.
>
> **`Graph_Save` is an unbudgeted full rewrite of the graph cache on every
> learned edge.** `bot2_graph.c` calls it (`autosave` is 1 by default) from
> `Graph_AddEdge`, from `Graph_AddStrafeEdge` for every new edge and every
> faster merge, and from `Graph_EdgeResult` on an edge's first validation; it
> writes all 14,249 cells, 1,398 nodes and every edge, about 0.9 MB on cache
> v1 and 1.27 MB on v2. It is reached inside the timed frame both from the
> dispatch (plan completion in `Move_Command`) and from the boundary
> (`StraightCommit` -> `FinishPlan` -> `LearnStrafe` on every commit from a
> landing). Its normal price is measured twice: the non-stall dispatch totals
> cluster at **24.3 to 28.4 ms** in all four worlds (a two-edge learn, about
> 12.3 ms a save), and in the one world that logs activity a
> learn-only frame costs **10.7 ms for one new edge and 24.1 to 27.5 ms for
> two**. The worlds learn 43 to 57 new edges each (plus up to four first
> validations), so the writes cost about 0.55 to 0.75 s per world when they
> behave and **3.07 s (off-1), 3.35 s (on-1), 0.56 s (off-2) and 3.21 s
> (on-2) in the stall frames alone** when they do not - 6 to 27 percent of each
> world's whole movement cost.
>
> **Below the stalls, the p95-p99 frames (p95 72 to 104 ms, p99 183 to
> 244 ms, one bot's share up to 358 ms) are one bot's whole landing search in
> one frame, dominated by rollouts, not by the planner.**
> The only world with activity per frame (`cost-telemetry`, 10-edge graph,
> timing invalid by its own report) shows every client-frame over 25 ms
> carrying a straight search (53 of 66), a gap search (11) or a learn-only
> write (2), and none of the 3,024 client-frames under 1 ms carrying any of
> them. Rollouts are 71 percent of its search frames. What is left fits
> `3.0 ms + 13.3 ms per new edge learned + 1.3 ms per reroute call`
> (R^2 0.88, 64 search frames). A reroute call adds two full rollouts
> (about 3.3 ms together) and one `Graph_PlanState`; the planner itself is
> **about 1 to 3 ms a call** - 1,162 to 2,472 expansions for a whole-leg
> replan on cache v1, 12,214 to 12,743 in 8 to 10 ms on the 10-edge graph -
> and **in 1,417 retained reroute passes and 229 replans over 60 logged worlds
> no state search failed to route, fell back to the cell search or
> overflowed**, so its worst case never ran where it could be seen. The
> suspected term is real, but on v1 it is about a tenth of a search, not the
> tail.
>
> **Why the rollout budget could not move either part.** The stalls are file
> I/O, which a rollout count does not see. And the budget suspends only the
> candidate loop: the finish phase - the control walk, the reroute pass (a
> path-free rollout, a `Graph_PlanState` and a second rollout per off-path
> candidate, up to 17 calls in one search, i.e. more rollouts than the
> candidate loop's 12 to 24), the extension walk - runs whole on whichever
> frame finishes the loop, and the commit's two cache writes stay on the
> boundary frame. That is the whole of the measured "tail does not move".
>
> **The cheapest decision-identical fix for the dominant term** is to take the
> cache write out of the frame: snapshot the edge array where `Graph_Save` is
> called today and write the last snapshot at shutdown (section 3). It
> changes no in-memory state, so no decision can change; the saved file can be
> made byte-identical to today's last write. It is **not implemented**.

## What was used, and its limits

- **Per-frame samples**: `home/base/movement_cost.csv` of each of the four
  worlds, as `codemp/game/bot2_movecost.c` writes it - one row per recorded
  server frame (800 per world): `level_time`, `elapsed_ms`, `movement_us`,
  `dispatch_us` (the fixture's clock around `Bot2_ExecuteMovement`),
  `prepare_us` (everything charged through `Bot2_MoveCostPreparation`, which
  `Bot2_MoveParityBeforePmove` in `bot2_move.c` calls on every exit: the gap,
  planned and straight searches, the commit, the parity build and the budget
  step) and `client0_us` .. `client3_us` (each client's dispatch plus its
  preparation). The remaining columns carry nothing here: `route_us`,
  `localnav_us`, `load_us`, `forecast_us` and the admitted, deferred, library
  and refused counters are zero in every row, `plan_us` (section 0,
  `Bot2_PlanBegin`) is at most 55 us, and `prepare_calls` is 8 in every row
  (two boundary charges per client per frame). There is no per-frame field
  for any search activity.
- **Server frames**: `home/base/server_movement_cost.csv` (`level_time`,
  `simulation_advance_ms`, `server_frame_us`). A row with a 25 or 50 ms
  advance is one or two game frames; larger advances are catch-up after a
  stall and hold many.
- **Log lines**: `server.log` of each world. With `bot_telemetry 0` the only
  movement lines are `[GRAPH] edge_added`, `[GRAPH] edge_result`, the engine's
  `Hitch warning: N msec frame time`, `[MOVECOST] IMPULSE/PICKUP`. They carry a
  one-second wall-clock stamp and no level time, so they order events but
  cannot be joined to a frame; the join below is by the hitch value against
  `server_frame_us` and by the log order around it. The `[GRAPH]` prints are
  `trap->Print` calls inside the timed interval, not the fixture's buffered
  prints.
- **What the production worlds cannot say**: telemetry is off in them (a print
  inside the search measures a different bot), so there is no per-frame count
  of candidates, reroutes, planner calls, walks or commits. For that the only
  source is `.codex-research/graph-binding-20260922-b/cost-telemetry`: the
  same fixture, four bots, 800 frames, `bot_telemetry 1`, **no `--graph`, so
  the 10-edge graph the map builds at load**, on the graph-binding build, and
  `timing_claims_valid` false in its own report because the `[MOVE]`,
  `[SPIKE]` and `[REROUTE]` prints run inline. Its lines carry `client=` and
  `level=`, so they join exactly to its CSV rows; its costs are inflated by
  the prints and its planner works on a different graph. It is the activity
  proxy, not a cost reading.
- **Per-search counts on the production cache** come from the single-bot bench
  worlds of the same build (`W-c125`, `W-c75`: cache v1, harness off, budget
  off) and the other logged worlds under
  `.codex-research/search-budget-20260922-c/`: the commit and no-commit
  `[MOVE] event=plan` lines (`candidates`, `rollout_ms`, `traces`, the outcome
  counts), `[REROUTE] ... pass_outcome=` lines (`route_cells`,
  `route_state`), and replan lines (`event=plan edge=none reason=path`,
  `expanded`, `overflow`, `elapsed_ms`, the last measured with
  `trap->Milliseconds` around `Replan` and so 1 ms resolution). `rollout_ms`
  is the forecast's own `timing.totalUs` summed over every `RunCandidate` of a
  search, including the reroute passes and the successor-aim first-landing
  rollouts; it excludes the walk references, `Graph_PlanState` and
  `Graph_Save`.
- The four cost worlds ran alone: C pair 09:29:34 to 09:30:32, C2 pair
  09:35:36 to 09:36:33, the bench campaign between them (09:31 to 09:35:18,
  `campaign.done`).

Scratch scripts for every number here were run from outside the repository
(`%TEMP%/bca/*.py`) and are not retained in the tree.

## 1. The worst 50 frames

### 1a. The four production-cache cost worlds

Worst 50 of the 3,200 recorded frames, by `movement_us`. "Dominant bot" is
the client with the largest `clientN_us` in that frame. "Server frame" names
the engine hitch whose `server_frame_us` contains the frame, with its advance
and the last `[GRAPH]` line the log printed before the hitch line; it is
meaningful only for 25 or 50 ms advances, where the hitch is this one frame.
The last column is the same bot at the same `elapsed_ms` in the other three
worlds: the workload is deterministic until the worlds diverge, so a value
that recurs is the same work, and a value that does not is not.

| # | world | level | elapsed ms | frame us | dispatch / prepare us | dominant bot (us) | server frame containing it | same bot, same elapsed, other worlds (ms) |
|---:|---|---:|---:|---:|---|---|---|---|
| 1 | on-1 | 18600 | 10200 | 3423038 | 130 / 3422908 | 1 (3353544) | hitch 3430 ms (advance 50 ms), preceded by edge added 707 (strafe) | off-1 0 / off-2 186 / on-2 7 |
| 2 | off-1 | 13100 | 4700 | 2411868 | 2411856 / 12 | 2 (2411789) | hitch 2412 ms (advance 25 ms), preceded by edge added 686 (strafe) | on-1 76 / off-2 52 / on-2 0 |
| 3 | on-2 | 17850 | 9450 | 1721149 | 137 / 1721012 | 2 (1721051) | hitch 1721 ms (advance 25 ms), preceded by edge added 710 (strafe) | off-1 0 / on-1 0 / off-2 0 |
| 4 | on-2 | 20400 | 12000 | 1506638 | 1488735 / 17902 | 0 (1491511) | hitch 3584 ms (advance 1925 ms), preceded by edge result 223 (strafe) | off-1 0 / on-1 5 / off-2 0 |
| 5 | off-1 | 22775 | 14375 | 669571 | 661869 / 7702 | 2 (661778) | hitch 670 ms (advance 25 ms), preceded by edge added 718 (strafe) | on-1 0 / off-2 0 / on-2 0 |
| 6 | off-2 | 21925 | 13525 | 555452 | 555437 / 15 | 1 (555312) | hitch 556 ms (advance 25 ms), preceded by edge added 730 (hop) | off-1 0 / on-1 25 / on-2 0 |
| 7 | on-1 | 18850 | 10450 | 435624 | 134 / 435489 | 2 (227860) | hitch 3531 ms (advance 3425 ms), preceded by edge added 721 (hop) | off-1 0 / off-2 0 / on-2 0 |
| 8 | on-2 | 12850 | 4450 | 378841 | 148 / 378692 | 3 (208197) | - | off-1 0 / on-1 0 / off-2 0 |
| 9 | on-2 | 15800 | 7400 | 358585 | 227 / 358357 | 0 (358398) | - | off-1 208 / on-1 244 / off-2 145 |
| 10 | off-1 | 10900 | 2500 | 337245 | 143 / 337102 | 3 (175672) | - | on-1 233 / off-2 0 / on-2 182 |
| 11 | on-1 | 8450 | 50 | 334335 | 244 / 334091 | 2 (159417) | - | off-1 109 / off-2 98 / on-2 12 |
| 12 | on-1 | 10900 | 2500 | 320085 | 143 / 319941 | 3 (233439) | hitch 701 ms (advance 775 ms), preceded by no graph line | off-1 176 / off-2 0 / on-2 182 |
| 13 | off-1 | 16875 | 8475 | 318323 | 183 / 318139 | 1 (318179) | - | on-1 24 / off-2 0 / on-2 0 |
| 14 | off-2 | 16850 | 8450 | 298521 | 123 / 298398 | 1 (298434) | - | off-1 0 / on-1 295 / on-2 0 |
| 15 | on-1 | 16850 | 8450 | 295392 | 165 / 295226 | 1 (295261) | - | off-1 0 / off-2 298 / on-2 0 |
| 16 | off-2 | 14075 | 5675 | 286354 | 121 / 286233 | 0 (286279) | - | off-1 280 / on-1 276 / on-2 8 |
| 17 | on-2 | 14750 | 6350 | 283906 | 112 / 283793 | 3 (261816) | - | off-1 0 / on-1 0 / off-2 0 |
| 18 | on-2 | 14500 | 6100 | 280518 | 145 / 280372 | 2 (158710) | - | off-1 0 / on-1 0 / off-2 0 |
| 19 | off-1 | 14075 | 5675 | 279629 | 108 / 279521 | 0 (279558) | hitch 1091 ms (advance 2400 ms), preceded by edge added 694 (strafe) | on-1 276 / off-2 286 / on-2 8 |
| 20 | on-1 | 14075 | 5675 | 275867 | 111 / 275755 | 0 (275791) | - | off-1 280 / off-2 286 / on-2 8 |
| 21 | on-1 | 16350 | 7950 | 260799 | 174 / 260624 | 2 (260660) | - | off-1 206 / off-2 0 / on-2 3 |
| 22 | on-2 | 18150 | 9750 | 254519 | 118 / 254401 | 3 (248693) | hitch 1928 ms (advance 1700 ms), preceded by edge added 717 (strafe) | off-1 0 / on-1 0 / off-2 0 |
| 23 | on-2 | 10900 | 2500 | 254061 | 125 / 253936 | 3 (182193) | - | off-1 176 / on-1 233 / off-2 0 |
| 24 | on-1 | 11675 | 3275 | 246730 | 119 / 246611 | 1 (243887) | hitch 699 ms (advance 700 ms), preceded by edge added 683 (strafe) | off-1 0 / off-2 183 / on-2 0 |
| 25 | off-1 | 20450 | 12050 | 244628 | 128 / 244499 | 2 (244536) | - | on-1 0 / off-2 0 / on-2 0 |
| 26 | on-1 | 15800 | 7400 | 244111 | 186 / 243925 | 0 (243961) | - | off-1 208 / off-2 145 / on-2 358 |
| 27 | on-2 | 14050 | 5650 | 242457 | 131 / 242326 | 0 (242374) | - | off-1 0 / on-1 0 / off-2 0 |
| 28 | off-2 | 18600 | 10200 | 238369 | 147 / 238221 | 1 (186347) | - | off-1 0 / on-1 3354 / on-2 7 |
| 29 | off-1 | 8450 | 50 | 237145 | 187 / 236958 | 3 (112620) | - | on-1 150 / off-2 100 / on-2 12 |
| 30 | off-1 | 15275 | 6875 | 235536 | 191 / 235344 | 2 (235400) | hitch 1091 ms (advance 2400 ms), preceded by edge added 694 (strafe) | on-1 203 / off-2 120 / on-2 0 |
| 31 | on-1 | 15100 | 6700 | 232400 | 182 / 232217 | 1 (232258) | - | off-1 203 / off-2 183 / on-2 0 |
| 32 | on-1 | 8650 | 250 | 232257 | 201 / 232056 | 0 (120391) | - | off-1 81 / off-2 77 / on-2 81 |
| 33 | off-1 | 11725 | 3325 | 226990 | 107 / 226883 | 1 (226928) | - | on-1 7 / off-2 0 / on-2 199 |
| 34 | on-1 | 11550 | 3150 | 223556 | 690 / 222865 | 0 (222902) | hitch 699 ms (advance 700 ms), preceded by edge added 683 (strafe) | off-1 189 / off-2 177 / on-2 0 |
| 35 | on-2 | 9100 | 700 | 219209 | 187 / 219022 | 3 (76987) | - | off-1 0 / on-1 6 / off-2 0 |
| 36 | on-1 | 19525 | 11125 | 219134 | 111 / 219022 | 1 (219054) | hitch 3531 ms (advance 3425 ms), preceded by edge added 721 (hop) | off-1 0 / off-2 180 / on-2 5 |
| 37 | on-1 | 15275 | 6875 | 216717 | 196 / 216521 | 2 (203133) | - | off-1 235 / off-2 120 / on-2 0 |
| 38 | off-2 | 16150 | 7750 | 215376 | 122 / 215253 | 2 (215291) | - | off-1 0 / on-1 3 / on-2 0 |
| 39 | on-1 | 21375 | 12975 | 213737 | 154 / 213583 | 2 (147074) | hitch 3531 ms (advance 3425 ms), preceded by edge added 721 (hop) | off-1 0 / off-2 0 / on-2 0 |
| 40 | off-2 | 8450 | 50 | 213115 | 178 / 212936 | 3 (100196) | - | off-1 113 / on-1 150 / on-2 12 |
| 41 | on-1 | 17175 | 8775 | 212412 | 89 / 212323 | 2 (212366) | - | off-1 0 / off-2 0 / on-2 0 |
| 42 | on-1 | 17450 | 9050 | 210210 | 105 / 210104 | 0 (210152) | - | off-1 170 / off-2 139 / on-2 121 |
| 43 | off-1 | 15800 | 7400 | 208472 | 179 / 208293 | 0 (208333) | hitch 584 ms (advance 1100 ms), preceded by edge added 697 (strafe) | on-1 244 / off-2 145 / on-2 358 |
| 44 | off-1 | 16350 | 7950 | 206169 | 171 / 205997 | 2 (206030) | hitch 584 ms (advance 1100 ms), preceded by edge added 697 (strafe) | on-1 261 / off-2 0 / on-2 3 |
| 45 | on-1 | 13525 | 5125 | 204849 | 120 / 204728 | 2 (201517) | - | off-1 202 / off-2 150 / on-2 0 |
| 46 | off-1 | 15100 | 6700 | 203053 | 183 / 202869 | 1 (202909) | hitch 1091 ms (advance 2400 ms), preceded by edge added 694 (strafe) | on-1 232 / off-2 183 / on-2 0 |
| 47 | off-1 | 13525 | 5125 | 202302 | 103 / 202199 | 2 (202233) | hitch 1091 ms (advance 2400 ms), preceded by edge added 694 (strafe) | on-1 202 / off-2 150 / on-2 0 |
| 48 | on-2 | 11725 | 3325 | 202145 | 112 / 202032 | 1 (198660) | - | off-1 227 / on-1 7 / off-2 0 |
| 49 | on-2 | 20450 | 12050 | 198187 | 175 / 198011 | 1 (112224) | hitch 3584 ms (advance 1925 ms), preceded by edge result 223 (strafe) | off-1 0 / on-1 4 / off-2 0 |
| 50 | on-1 | 13400 | 5000 | 197577 | 1011 / 196566 | 1 (179950) | - | off-1 0 / off-2 137 / on-2 0 |

### 1b. The six frames over half a second

| # | world | elapsed | frame us | where | same bot, same elapsed, elsewhere | the log around it |
| ---: | --- | ---: | ---: | --- | --- | --- |
| 1 | on-1 | 10,200 | 3,423,038 | boundary, bot 1 | off-2 186 ms | edge 706 (hop 0, `tap=0`, 12951->10817) at 09:30:22, edge 707 (hop 1 from 706's landing, 10817->2373) at 09:30:26, then the 3,430 ms hitch. off-2 learns the same pair (its edges 713, 714) in one second, no hitch |
| 2 | off-1 | 4,700 | 2,411,868 | **dispatch**, bot 2 (boundary 12 us) | on-1 **75.6 ms**, off-2 **52.4 ms**, both dispatch | edge 685 (4924->1377) at 09:29:46, edge 686 (1377->8495, from 685's landing) at 09:29:49, then the 2,412 ms hitch |
| 3 | on-2 | 9,450 | 1,721,149 | boundary, bot 2 | diverged | edge 709 at 09:35:54, edge 710 (from 709's landing) at 09:35:55, then the 1,721 ms hitch |
| 4 | on-2 | 12,000 | 1,506,638 | **dispatch**, bot 0 (boundary 17.9 ms) | diverged | inside a 1,925 ms catch-up; the last graph lines are two first validations (`edge_result ... success=1`; `Graph_EdgeResult` saves on an edge's first validation) |
| 5 | off-1 | 14,375 | 669,571 | **dispatch**, bot 2 (boundary 7.7 ms) | diverged | edges 716, 717 (hop, another bot), 718 at 09:29:56, then the 670 ms hitch |
| 6 | off-2 | 13,525 | 555,452 | **dispatch**, bot 1 (boundary 15 us) | on-1 **24.8 ms**, dispatch | hop edge 730 (2827->2242, `validated=1`) then the 556 ms hitch; on-1 learns the same hop (its edge 721) |

The argument, in order of strength:

1. **Rows 2, 4, 5 and 6 are not search.** No straight, gap, planned or spike
   search runs in `Bot2_ExecuteMovement`; they run only at the boundary. What
   the dispatch does in the rewrite (`Move_Plan`, then `Move_Command`) is
   `MoveStep`, `SpeedEdge`, `PadObserve`, `StateAvoid`, `Replan` and plan
   completion (`PlanCompleted`: `LearnStrafe`, `Graph_AddEdge`,
   `Graph_EdgeResult`). `Replan` is one `Graph_PlanState` whose table is
   capped at 131,072 entries (section 2); on this cache it expands 1,162 to
   2,472 nodes and prints `elapsed_ms` 0 to 3. That leaves the cache write.
2. **The same work costs 20 to 46 times less in a sibling world.** Row 2's
   bot, frame and dispatch are 75.6 and 52.4 ms in on-1 and off-2; row 6's
   hop learn is 24.8 ms in on-1. Computation replays at the same price across
   these worlds (see 1a: bot 0 at 5,675 ms is 279.6, 286.3 and 275.8 ms in
   three of them); a factor of 30 on the same call is waiting, not work.
3. **Row 1 isolates the call.** `LearnStrafe` adds hop 0 then hop 1 of one
   plan; edge 706 prints `tap=0`, which only hop 0 of a plan not committed
   with a landing-frame tap can carry, and edge 707 takes off at 706's recorded landing
   (956.4, 918.7, 1368.1) with `tap=1`. Between the two prints the code runs
   `Graph_Save` for 706, an `Event` that prints nothing with telemetry off,
   two `Graph_CellAt` lookups and the merge scan of 707's cell. The stamps are
   four seconds apart. The frame (`level_time` 18600, advance 50 ms) is the
   3,430,136 us server frame, and 3,422,908 us of it is bot 1's boundary.
4. **The normal price is measured.** Leaving out the stalls and the first
   frame's four initial replans, the dispatch totals over 5 ms fall in two
   groups: 13 frames at **24.3 to 28.4 ms** (24,339 and 24,462 us in off-1;
   24,572, 24,951, 25,247 and 28,358 in off-2; 24,805, 25,147, 27,367 and
   27,931 in on-1; 24,613, 24,905 and 28,163 in on-2) and 8 at 51.0 to
   75.7 ms, two to three times that (what else those frames did is not
   logged). A fixed-size write gives a quantum; the telemetry world's
   learn-only frames put one edge at 10.7 ms and two at 24.1 to 27.5 ms
   (section 1c), so the 24 to 25 ms group is a two-edge `LearnStrafe` at about
   12.3 ms a save on the 0.9 MB v1 cache.

The mechanism of the multi-second waits (the fixture's file open and
truncate, the buffered write of about 17,000 lines, the close, and whatever
the host does to a 0.9 MB file rewritten fifty times in twenty seconds) is not
established here, and it does not need to be: what matters is that it is a
synchronous disk write inside the timed frame, repeated on every learned edge.
The console print before it (`trap->Print` of the `edge_added` line into the
runner's pipe) is the only other I/O on that path; it is one line, and the
telemetry world prints tens of lines on each searching frame (2,330 lines in
its window) with no frame over 272 ms.

### 1c. The one world that logs what each frame did

`cost-telemetry`, worst 50 of its 3,200 client-frames by `clientN_us`, each
joined to that client's lines at that `level`. "search" is a straight search
(`[MOVE] event=plan` with `kind=straight`), "gap" a gap search
(`reason=gap`). "reroute calls" counts `[REROUTE] ... pass_outcome=` lines
plus `no_route` outcomes - each one `RerouteFrom`, so one `Graph_PlanState`
and two rollouts - with their `route_cells` summed. "planner calls" adds the
winner's second `RerouteFrom` at the commit (`event=plan edge=reroute
reason=path`). "walk refs" is the control walk plus the extension walk when a
reroute was admitted (the arm-B walk is off in this configuration). "new
edges learned" counts `edge=strafe reason=learn ... merged=0`, each one
`Graph_Save`. "residual" is the client's cost minus the search's
`rollout_ms`. The frame columns are whole-frame, so a frame where two bots
searched shows the sum.

| # | level | bot | client us | frame dispatch / prepare us | search | candidates | rollout us | reroute calls (route cells, sum) | planner calls | walk refs | new edges learned (saves) | replans | residual us |
|---:|---:|---:|---:|---|---:|---:|---:|---|---:|---:|---:|---:|---:|
| 1 | 8700 | 3 | 132187 | 35 / 132174 | search | 18 | 104200 | 12 (2654) | 12 | 2 | 0 | 0 | 27987 |
| 2 | 15150 | 0 | 123170 | 142 / 123050 | search | 18 | 80400 | 12 (651) | 13 | 2 | 2 | 0 | 42770 |
| 3 | 16350 | 3 | 117201 | 302 / 117040 | search | 18 | 71500 | 8 (778) | 9 | 2 | 2 | 0 | 45701 |
| 4 | 8450 | 2 | 116097 | 174 / 271099 | search | 18 | 79500 | 13 (2824) | 14 | 2 | 0 | 0 | 36597 |
| 5 | 12350 | 3 | 113720 | 77 / 113682 | search | 18 | 72800 | 15 (2376) | 16 | 2 | 2 | 0 | 40920 |
| 6 | 8950 | 3 | 112590 | 204 / 112521 | search | 18 | 94300 | 8 (1764) | 8 | 2 | 0 | 0 | 18290 |
| 7 | 12275 | 2 | 110456 | 189 / 110369 | search | 18 | 70400 | 12 (1938) | 12 | 2 | 2 | 0 | 40056 |
| 8 | 13100 | 2 | 100506 | 91 / 100465 | search | 17 | 69600 | 17 (2563) | 18 | 2 | 1 | 0 | 30906 |
| 9 | 10550 | 1 | 89481 | 292 / 248432 | search | 18 | 49700 | 5 (710) | 6 | 2 | 2 | 0 | 39781 |
| 10 | 10550 | 0 | 89231 | 292 / 248432 | search | 17 | 54000 | 1 (142) | 1 | 2 | 2 | 0 | 35231 |
| 11 | 17575 | 2 | 83931 | 171 / 83864 | search | 15 | 54100 | 0 (0) | 0 | 1 | 2 | 0 | 29831 |
| 12 | 17750 | 0 | 80636 | 97 / 80572 | search | 18 | 37700 | 1 (22) | 1 | 2 | 2 | 0 | 42936 |
| 13 | 18425 | 1 | 77119 | 76 / 77082 | search | 18 | 40300 | 3 (106) | 4 | 2 | 2 | 0 | 36819 |
| 14 | 20550 | 3 | 76702 | 453 / 76493 | search | 15 | 30900 | 0 (0) | 0 | 1 | 2 | 0 | 45802 |
| 15 | 20000 | 3 | 76178 | 228 / 76054 | search | 17 | 44400 | 0 (0) | 0 | 1 | 2 | 0 | 31778 |
| 16 | 15625 | 2 | 76152 | 93 / 76095 | search | 18 | 40400 | 4 (408) | 5 | 2 | 2 | 0 | 35752 |
| 17 | 8450 | 3 | 73276 | 174 / 271099 | search | 18 | 59100 | 3 (666) | 3 | 2 | 0 | 0 | 14176 |
| 18 | 13975 | 2 | 71893 | 122 / 71815 | search | 18 | 51800 | 0 (0) | 0 | 1 | 1 | 0 | 20093 |
| 19 | 23125 | 3 | 71204 | 20325 / 51052 | gap | 0 | 0 | 0 (0) | 0 | 0 | 0 | 0 | 71204 |
| 20 | 10550 | 2 | 69996 | 292 / 248432 | search | 15 | 39500 | 0 (0) | 0 | 1 | 2 | 0 | 30496 |
| 21 | 11625 | 0 | 69032 | 50 / 69011 | search | 18 | 52000 | 4 (482) | 5 | 2 | 1 | 0 | 17032 |
| 22 | 16700 | 1 | 67723 | 134 / 67640 | search | 15 | 44200 | 6 (425) | 7 | 2 | 1 | 0 | 23523 |
| 23 | 18875 | 0 | 64344 | 153 / 64258 | search | 18 | 55100 | 0 (0) | 0 | 1 | 0 | 0 | 9244 |
| 24 | 20075 | 1 | 63578 | 104 / 63519 | search | 13 | 28500 | 2 (34) | 3 | 2 | 2 | 0 | 35078 |
| 25 | 14375 | 3 | 61607 | 179 / 61530 | search | 18 | 60500 | 0 (0) | 0 | 1 | 0 | 0 | 1107 |
| 26 | 17075 | 0 | 61134 | 113 / 61049 | search | 15 | 35900 | 0 (0) | 0 | 1 | 1 | 0 | 25234 |
| 27 | 13500 | 0 | 59206 | 94 / 59154 | search | 15 | 33100 | 6 (553) | 7 | 2 | 2 | 0 | 26106 |
| 28 | 9200 | 3 | 57746 | 232 / 57663 | search | 18 | 56500 | 0 (0) | 0 | 1 | 0 | 0 | 1246 |
| 29 | 18350 | 3 | 57443 | 65 / 57421 | search | 18 | 32400 | 0 (0) | 0 | 1 | 2 | 0 | 25043 |
| 30 | 19300 | 2 | 56560 | 24186 / 56534 | search | 17 | 28700 | 0 (0) | 0 | 1 | 2 | 0 | 27860 |
| 31 | 20375 | 2 | 54103 | 143 / 54059 | search | 18 | 25600 | 0 (0) | 0 | 1 | 2 | 0 | 28503 |
| 32 | 22050 | 1 | 53838 | 6175 / 53794 | search | 18 | 52900 | 0 (0) | 0 | 1 | 0 | 0 | 938 |
| 33 | 10150 | 3 | 53540 | 282 / 53441 | search | 15 | 52400 | 0 (0) | 0 | 1 | 0 | 0 | 1140 |
| 34 | 22675 | 3 | 53162 | 349 / 53027 | search | 15 | 43500 | 0 (0) | 0 | 1 | 0 | 0 | 9662 |
| 35 | 18100 | 3 | 47538 | 148 / 47459 | search | 13 | 39700 | 0 (0) | 0 | 1 | 0 | 0 | 7838 |
| 36 | 12250 | 1 | 47488 | 214 / 47387 | search | 12 | 37600 | 0 (0) | 0 | 1 | 0 | 0 | 9888 |
| 37 | 20050 | 2 | 47130 | 59 / 47104 | search | 18 | 27000 | 0 (0) | 0 | 1 | 1 | 0 | 20130 |
| 38 | 16800 | 0 | 47114 | 89 / 47068 | search | 18 | 20300 | 0 (0) | 0 | 1 | 2 | 0 | 26814 |
| 39 | 18175 | 0 | 46380 | 106 / 46314 | search | 17 | 26900 | 0 (0) | 0 | 1 | 1 | 0 | 19480 |
| 40 | 17325 | 2 | 46318 | 305 / 46120 | search | 12 | 37400 | 0 (0) | 0 | 1 | 0 | 0 | 8918 |
| 41 | 22500 | 2 | 44970 | 144 / 52648 | gap | 0 | 0 | 0 (0) | 0 | 0 | 0 | 0 | 44970 |
| 42 | 15750 | 1 | 44375 | 521 / 43886 | search | 18 | 39000 | 4 (409) | 4 | 2 | 0 | 1 (1400 exp) | 5375 |
| 43 | 8450 | 1 | 41246 | 174 / 271099 | search | 16 | 33300 | 0 (0) | 0 | 1 | 0 | 0 | 7946 |
| 44 | 19575 | 0 | 40780 | 67 / 40741 | search | 15 | 40100 | 0 (0) | 0 | 1 | 0 | 0 | 680 |
| 45 | 8450 | 0 | 40652 | 174 / 271099 | search | 16 | 31900 | 0 (0) | 0 | 1 | 0 | 0 | 8752 |
| 46 | 9450 | 3 | 39372 | 105 / 39335 | search | 18 | 31200 | 0 (0) | 0 | 1 | 0 | 0 | 8172 |
| 47 | 23500 | 2 | 38630 | 186 / 46174 | gap | 0 | 0 | 0 (0) | 0 | 0 | 0 | 0 | 38630 |
| 48 | 14625 | 3 | 37821 | 38 / 37807 | search | 18 | 35200 | 0 (0) | 0 | 1 | 0 | 0 | 2621 |
| 49 | 10400 | 3 | 36678 | 51 / 36659 | search | 16 | 31200 | 2 (366) | 3 | 2 | 0 | 0 | 5478 |
| 50 | 22450 | 2 | 36314 | 230 / 36254 | search | 18 | 35100 | 0 (0) | 0 | 1 | 0 | 0 | 1214 |

Over all 3,200 client-frames:

| class | client-frames | median us | p90 us | max us | total us |
| --- | ---: | ---: | ---: | ---: | ---: |
| nothing logged | 3,123 | 21 | 65 | 71,204 (a gap search's frame) | 1,157,447 |
| straight search | 34 | 28,444 | 53,838 | 64,344 | 1,136,868 |
| search + reroute + learn | 14 | 89,232 | 117,201 | 123,170 | 1,237,217 |
| search + learn | 12 | 61,134 | 76,702 | 83,931 | 748,568 |
| search + reroute | 5 | 112,590 | 132,188 | 132,188 | 470,831 |
| learn only | 6 | 24,945 | 27,454 | 27,454 | 123,386 |
| replan only | 5 | 8,350 | 10,718 | 10,718 | 35,723 |
| search + reroute + replan | 1 | 44,375 | 44,375 | 44,375 | 44,375 |

("nothing logged" includes the 23 gap searches, which print one line and whose
rollouts are not in any `rollout_ms`.) Rollouts are 2,585,600 of the
3,637,858 us in search frames (71 percent). A least-squares fit of the
residual over the 64 search frames with no replan and no gap search gives
**3,033 us + 13,333 us per new edge learned + 1,315 us per reroute call**
(R^2 0.88); the rollouts themselves fit **1,995 us per candidate and
3,265 us per reroute call** (R^2 0.59). The learn-only frames give the save
directly: **10,723 and 10,694 us for one new edge, 24,130 to 27,454 us for
two**.

### 1d. What every expensive frame has and no cheap frame has

In the telemetry world every client-frame over 25 ms carries one of three
things: **a straight search (53 of 66), a gap search (11) or a cache write
from a learned edge (2)**; the 3,024 client-frames at or under 1 ms carry
none of them (one carries a 40-node replan). Among straight-search frames
the median is 40.8 ms without a reroute and 89.2 ms with one, and every search
frame over 100 ms carries 8 to 17 reroute calls. In the production worlds the
same split holds where it can be seen: every frame of 0.5 s or more has a
learned edge written in it and four of the six have no search at all, while
the frames from about 70 to 440 ms are boundary frames of one bot that recur
at the same elapsed time in sibling worlds at the same price, which is what a
deterministic search does.

## 2. What one landing search costs, from the code

The production configuration of the cost worlds (`cost_start.cfg`):
`bot_moveReroute 1` (reroute pass on, route ranking off),
`bot_moveSuccessorAim 1`, `bot_moveSuccessorFan 0`, `bot_moveHeadingSweep 0`,
`bot_moveReplay 0`, no `bot_moveHeadingOffer`, no `bot_moveTapOnward`,
`bot_moveSearchBudget 0`. `StraightSearchCore` and `StraightJudge` in
`bot2_move.c` then do, on the frame that searches:

| Stage | Work | Count per search | Unit cost (from the samples) | Typical total |
| --- | --- | --- | --- | --- |
| Construction | 12 straight candidates (`straightHops` 2 x `straightAims` 3 x `straightRunups` 2); up to 4 planned-edge candidates at a planned takeoff (arm B off); `LandingAimOffers`: one rollout per eligible two-hop original that stops at the first landing (`FirstLandingCommand`), each yielding at most one re-aimed offer | 12 to 24 candidates (bench max 24, telemetry world 12 to 18); up to 8 short rollouts | a short rollout, about a third to a half of a full one | a few ms |
| Candidate loop | `RunCandidate` with `StraightCommand`: up to `MOVE_PARITY_MAX_HORIZON` 256 steps, stopped `MOVE_STRAIGHT_HOLD` 48 frames after the onward stage or at a landing off the path; per candidate `StraightArrives`, `NearPathCell` over at most 49 path cells, `Graph_StrafeAdmits` for planned chains | one rollout per candidate, 80 to 94 steps on average (max 171) | **about 2.0 ms a rollout** (fit), from about 11 traces a step at **1.5 to 1.7 us a trace** (462,348 traces in 696.5 ms over W-c125's 16 commit searches); each rollout also copies the whole entity array and, under the guard, compares it back | 25 to 50 ms |
| Finish: control walk | `StraightWalk` to the furthest admitted done frame | 1 | one rollout | about 2 ms |
| Finish: reroute pass | per candidate whose landing left the path: a path-free rollout; if admitted and not an arrival, `RerouteFrom` (one `Graph_PlanState`, `Graph_Path` if it finds nothing) and a second rollout on the replacement route | 0 to 17 calls (bench production up to 12; telemetry world up to 17) | **3.3 ms of rollouts a call** (fit) plus the planner below | 0 to 60 ms of rollouts, 0 to 40 ms of planner |
| Finish: extension walk | `StraightWalkInto` for rerouted winners' reference | 0 or 1 | one rollout | about 2 ms |
| Commit | `RerouteFrom` **again** for a rerouted winner (the judge's route was in `rerouteScratch`, overwritten by every later call); `StraightCommit` -> `FinishPlan` -> `PlanCompleted` -> `LearnStrafe`: up to two `Graph_AddStrafeEdge`, **each a full `Graph_Save`**; `Reissue` (one `MoveStep`) | 0 or 1 planner call; 0 to 2 saves | planner below; **about 12.3 ms a save** on v1, with waits of 0.5 to 3.4 s observed | about 25 ms on a landing commit that learns |

The measured sums agree: on cache v1 one search's `rollout_ms` is 40 to 50 ms
at the median, 62 to 99 ms at p90 and 72 to 159 ms at worst (single-bot
bench worlds W-c125, W-c75, V-c125-a2, V-c75); in the telemetry world a
search with 12 reroute calls and a two-edge learn is 110 to 123 ms for one
client.

**`Graph_PlanState`** (`bot2_graph.c`), the planner call the reroute pass
makes per candidate:

- **Fixed cost per call**: `memset` of the open-addressing slot table,
  `GRAPH_STATE_MAX * 2` = 262,144 ints = **1 MiB**, on every call.
- **Per expansion**: four link relaxations, each running `StateWalk`'s
  friction-and-acceleration loop over 32 units (3 to 8 iterations of the
  25 ms law, bounded at 400); up to four diagonal relaxations; the cell's own
  non-strafe edges; and **nine** edge-list scans for strafe edges
  (`StateStrafeEdges` for the cell, its four linked neighbours and their four
  diagonal neighbours), each edge paying `StateAvoided` (linear in the avoid
  list, at most 32) and `Graph_StrafeAdmits` (reach, band and a cone test);
  each relaxation a hash probe in `StateEntry` and a binary-heap push.
  Measured: **0.65 to 0.8 us an expansion** (whole-leg replans of 12,214 to
  12,743 expansions in 8 to 10 ms on the 10-edge graph, including the
  memset); on v1, whole-leg replans expand 1,162 to 2,472 in 0 to 3 ms.
- **The open set and its bound**: the state is the cell plus the speed band
  (32 units/s), the landing-frame bit, the octant when on a landing frame and
  whether Speed is running - up to 576 keys a cell (32 bands x (1 + 8
  landing octants) x 2). The table stops at
  131,072 entries (`overflow`), the heap at 262,144. **There is no other
  bound: no cost cap, no expansion cap, no deadline.**
- **When no path exists it runs to exhaustion** - the loop ends only on the
  goal, an empty heap or the overflow - and then `RerouteFrom` runs the cell
  A* (`Graph_Path`) over up to 14,249 cells, also to exhaustion. Worst case
  per call: about 131,072 x 0.75 us, **about 0.1 s**, plus a few ms of cell
  search.
- **It is not memoised within a search.** Every off-path candidate calls it
  from its own landing state, candidates that land identically call it
  again, and the winner's call is repeated at the commit. In the 60 logged
  worlds 84 of the 1,417 reroute calls repeat another call of the same search
  exactly (landing cell, speed and position as printed; the exact float key is
  not printed), and 139 winners were planned a second time at the commit.
- **The heuristic** is straight distance at 450 units/s; learned strafe edges
  are priced at about 580 to 900 units/s (edges 680, 681 and 684 of off-1:
  751 units in 1,300 ms, 667 and 736 units in 825 ms), so it overestimates
  wherever strafe edges lead, is not admissible on this graph, and the search
  runs greedily along them; with the long cell jumps those edges make, that
  is why whole-leg replans on v1 expand only one to two thousand nodes.

**`Graph_Nearest`** scans (2 x ceil(r / 32) + 1)^2 = 25 hash buckets for the
48 and 64 unit radii it is called with; it is called in `RerouteFrom` only
when a landing has no cell, twice per `Replan`, and at root lookup.
Microseconds. `CellReach` is constant time.

**Which term reaches 100 ms to 2 s.** Only the cache write has been seen
there: 0.5 to 3.4 s in six frames. The rollouts of one full search reach 100
to 160 ms by themselves, and on a reroute-heavy search more than half of them
are in the finish phase (up to 34 rollouts against 12 to 24 in the candidate
loop). The planner is 1 to 3 ms a call on v1, 15 to 40 ms for a 12 to 17-call
search including the memsets; it would reach 0.1 s a call and 1 to 2 s a
search only if every reroute of a search exhausted or overflowed the table,
which none of 1,417 retained passes did. On v2 the per-expansion scans and
the number of distinct exit states per cell both grow with the edges, and
nothing here measures by how much.

## 3. The change that removes the dominant term

### The cache write leaves the frame (the dominant term)

**What.** In `codemp/game/bot2_graph.c`:

1. Split `Graph_Save` into a writer over a given edge array -
   `static qboolean SaveEdges(const graphEdge_t *edges,int edgeCount,int capped)`,
   the present body with `graph.edge`, `graph.edgeCount` and `graph.capped`
   replaced - and keep `Graph_Save(void)` as
   `SaveEdges(graph.edge,graph.edgeCount,graph.capped)` for the explicit
   callers (`Move_PrepareMap`'s build, `Svcmd_MoveGraph_f`, `ExploreFinish`).
2. Add a snapshot: `static graphEdge_t saveEdges[GRAPH_MAX_EDGES]`,
   `static int saveEdgeCount,saveCapped,savePending`, and
   `static void SaveRequest(void)` that copies `graph.edge[0 .. edgeCount)`
   (96 bytes an edge: about 70 KB on v1 and 330 KB on v2, tens of
   microseconds; the static array is another 12.6 MB of BSS beside the
   graph's own) and the two counters, and sets `savePending`.
3. Replace the five autosave calls - `Graph_AddEdge`, the faster merge and the
   append in `Graph_AddStrafeEdge`, the first validation in
   `Graph_EdgeResult`, and `Graph_ResetLearned` - by `SaveRequest()` when a new
   cvar `bot_moveGraphSaveDefer` is 1, and leave them as they are when it is 0
   (default 0 until the regression below passes, so one build carries both
   arms).
4. Add `qboolean Graph_SaveFlush(void)`: if `savePending`, write the snapshot
   with `SaveEdges` and clear it, printing `[GRAPH] cache_write_failed` on
   failure as today. Call it from `G_ShutdownGame` (one line in `g_main.c`;
   both runners end a world with `rcon quit` - `tools/movement_cost_runner.py`
   line 323, `tools/movement_bench.py` line 314 - and fall back to
   `terminate()` only when that fails), from `Graph_Autosave` before it changes
   the flag (so an exploration that turns autosave off writes the pending
   snapshot first), and at the top of `Move_PrepareMap` before a rebuild or a
   load. In production, where a crash would lose the session's learning, an
   optional periodic flush from `G_RunFrame` outside any client think (for
   example every 60 s of level time) bounds that loss; the cost fixture's
   20 s window would then see at most one.

**Why no decision can change.** `Graph_Save` only reads the graph and writes
a file. Nothing in a running session reads that file: its only reader is
`Load`, from `Move_PrepareMap` at map load, and the runners stage a fresh copy
of the declared cache into each world. The in-memory graph - cells, nodes,
edges, the per-cell chains, the touched-edge copies `Graph_ResetLearned`
restores - is written by exactly the same calls in exactly the same order with
the change on, so every planner call, judge, learn and replan reads the same
bytes. The search reads no clock and the fixture advances `level.time` in
fixed steps, so frame wall time enters no decision; the only wall-clock input
is the bench's release level, which the reference cells pin by
`--corner-class` and compare from the release.

**Why the saved file is the same.** The cells, nodes and the other header
fields `Graph_Save` writes are fixed after the load; the edge records and
`capped` are the only things that change, and the snapshot copies them at the
moment the write happens today. The last snapshot is therefore byte-identical
to today's last write, including today's staleness: a `Graph_EdgeCost` update
or a failure after the last save is not written, as today. (The cheaper
variant - a dirty flag and a write of the live graph at shutdown, no snapshot
- is equally decision-identical but writes those later updates, so its file
differs from today's; the [production-graph-v2 fold](2026-09-22-production-graph-v2.md)
reads saved caches, so that difference would have to be accepted explicitly.)

**What it removes.** Every frame in section 1b's table and the 24 to 28 ms
learn quantum: about 0.55 to 0.75 s of ordinary writes per cost world and, in
three of the four, 3.1 to 3.4 s of waiting. It does not touch the p95-p99
search frames, which are the next item.

### The planner, second and smaller: one call per distinct landing

Decision-identical and small on v1; worth doing before v2 is costed, because
the planner's share grows with the edges and nothing yet measures it there.

- **A within-search memo in `RerouteFrom`** (`bot2_move.c`). Key: the landing
  cell passed to `Graph_PlanState`, the `graphState_t exit` fields bit for bit
  (`speed`, `dir[0]`, `dir[1]`, `tap`, `speedMs`), `m->path.to`,
  `m->state.avoidCount` and the avoid list, and a graph mutation generation (a
  counter `bot2_graph.c` bumps in `AddEdgeRecord`, `Graph_EdgeResult`,
  `Graph_EdgeCost`, the strafe merge, `ResetLearned` and the corridor load and
  clear). Value: the whole `bot2_movePath_t` `RerouteFrom` produced and its
  return value. Storage: a per-search table keyed by candidate slot, emptied
  where `StraightJudge` enters its finish phase. Why identical:
  `Graph_PlanState` and `Graph_Path` are pure functions of exactly those
  inputs plus the graph and corridor state the generation covers, and they
  write only their own statics and the caller's `out`.
- **Reuse the winner's route at the commit.** Keep each rerouted candidate's
  path from the judge (a copy per slot, or the memo) and let the commit's
  `RerouteFrom` take it **only when the key still matches**. Between the judge
  and the commit `StraightSearchCore` may call `StateAvoid`, which appends to
  the avoid list and may disable an edge through `Graph_EdgeResult`; then the
  key differs and the route is planned again exactly as today. With the check,
  the installed route is always the one today's code computes.
- **Clear the slot table by stamp instead of `memset`**: a per-slot
  generation compared on probe, instead of zeroing 1 MiB per call. Same probe
  sequence, same entries, same result.

There is no bound the caller already applies that the A* could take: the
reroute gate prices the returned path with `PathRemainingMs` (the walk model),
not with the planner's `g`, so no cost cutoff on the search follows from it,
and cutting the search would change which path it returns.

### The regression either change must pass

On one fresh build carrying the change behind its default-off cvar, with the
source manifest verified before the first world and after the last:

- **The four reference cells, cvar on**, each compared decision by decision
  with the same cell cvar off on the same build, by the rule the search-budget
  campaign's `analyse.py` applies (offset from the release, plan kind, hop
  count, both landing cells, both edge indices, the candidate and admitted
  counts of the deciding search): seeded class 125, harness on
  (`--parity 256`, the 2026-09-16 seed): **15,925 ms / 13 straight / 5 state**;
  seeded class 75, harness on: **21,125 ms / 16 / 8**; production cache v1
  class 125, harness off: **19,050 ms / 14 / 4**; production cache v1 class 75,
  harness off: **19,150 ms / 14 / 5**. All four identical; for the cache-write
  change also the saved `bot2-graph` file of each world byte-identical between
  the arms.
- **The cost pair**: `tools/movement_cost_runner.py --bots 4 --graph <cache>`
  with the cvar off and on, at least two pairs, the arms alternated across
  sequential runs, no other world running. Report p50, p95, p99, the worst
  frame, frames over 5,000 and 25,000 us, frames of 0.5 s or more, and the
  dispatch and boundary totals. Expected for the cache-write change: no frame
  of 0.5 s or more, no 24 to 28 ms dispatch quantum, the worst frame down to
  the search tail (the largest stall-free frame here is 436 ms), the totals
  down by the write time, p95 and p99 about unchanged. On cache v1 first, to
  compare with this record, then on v2, which the chain now running uses and
  which has no cost reading at all.
- The 36-case floor is not required by the identity argument; it costs one
  `--parallel 4` call and is worth running once on the new build.

## 4. What this does not establish

- **Nothing was measured natively for this record.** Every number is read
  from retained worlds; the attribution of the stall frames to the cache
  write is an inference from four independent observations (section 1b), not
  a measurement of `Graph_Save` itself, and the cause of the multi-second
  waits inside it (file open, write, close, the host) is not identified.
- **The production cost worlds log no search activity.** Which frames of the
  p95-p99 band carried how many candidates, reroutes and planner calls is
  known only for the 10-edge telemetry world, whose timings are invalid by its
  own report, and by analogy with the single-bot bench worlds on v1. The
  largest stall-free boundary frames in the four-bot worlds (up to 358 ms for
  one bot) exceed any single-bot search's `rollout_ms` (at most 159 ms) by
  more than the static model explains, and that gap is open. Per-frame
  counters (rollouts, rollout steps, planner calls and expansions, cache
  writes) added to the fixture's row and printed after recording would close
  it without a print or a clock inside the search.
- **One map, one workload, four bots, two sample pairs, cache v1.** v2 has
  five times the edges, a 1.27 MB file (so a larger write, if it scales with
  bytes) and a different planner workload; none of it is measured.
- **The planner's worst case is a bound, not a reading.** No retained state
  search failed, fell back or overflowed; the 0.1 s a call and 1 to 2 s a
  search are what the table cap permits, not what has happened.
- The proposed changes are described, not implemented, built or tested.
