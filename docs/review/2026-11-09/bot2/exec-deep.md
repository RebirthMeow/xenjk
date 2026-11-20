# Bot2 review 2026-11-09, second read: the executor's search, cache, reroute and deferral code

Target: `codemp/game/bot2_move.c` at 011e4501 (the same file as the reviewed 6fe7bb2f), with its callers and
`bot2_graph.c`'s failure charging. Regions read line by line: the reroute search, memo and install (5630-5857),
`StraightJudge` (5877-6151), `StraightSearchCore` (6417-6865), the search cache (`SearchCacheContext`,
`SearchCacheValidate`, `SearchCacheTake`, `SearchPredictAt`, `SearchChunkWith`; 6881-7197), the drop search and its
deferral (4594-4813, 7276-7367), the walk deferral with the 2026-10-09 ground guard (7369-7468), the pool and
`SearchBudgetStep` (7488-7646), the boundary driver (7902-8044), and `StateAvoid`, `DeclineHeld`, the "walked" test,
`Replan`, `HopStep` and `WalkStep` as callers. All `file:line` references are at 011e4501.

How each finding was checked is stated with it: a trace through the code named, and where a retained world could
count it, a count. Retained worlds: `.codex-research/hoproom-20261030-a` (build 97c25de3, the 4v4, guide on and off,
Kejim seeds 3-10 in `worlds/`, Dantooine seeds 3-10 in `dantooine/`; 32 worlds). New worlds: the BM-3 pair,
`.codex-research/chargeflown-20261109-b` (build 1d2e7dea, this branch; 20 seeds an arm a map; its 40 control worlds
are the defaults), which also carries one diagnostic tally for BX-2. Scripts:
`.codex-research/review-bot2-20261109/bx-scratch/` and the campaign folder.

Counts: 10 findings. No P0 or P1. P2: 1 (BX-1). P3: 9. Not repeated here: `docs/review/FINDINGS.md`, `move.md`
(BM-1..19), `plan.md` (BP-9 covers the pre-search's simulated clock reaching `OffExpire`, which this read met again
through `RerouteKey` and `Graph_Reaches`).

## Findings, most severe first

### BX-1 bug, P2, confirmed by trace, reach counted: the arrival safety check never sees what a rerouted arrival does after its off-path landing

- Where: `bot2_move.c:5980-5983` (reroute pass, first continuation), `6007-6011` (the roll with the replacement
  route), `6914-6918` (`SearchCacheValidate`); `5592-5597` (`StraightArrives`), `5570-5580` (`ArriveCheckCommand`),
  `4984-5000` (`StraightCommand`'s path stop).
- Defect: `bot_moveArriveSafe` rolls an arrival on past the flag to see whether it ends standing. It does so through
  `StraightCommand`, which ends a rollout at the first landing off the path unless `straightPathFree` is set. The
  reroute pass sets `straightPathFree` for its own rollout and clears it on the line before it calls
  `StraightArrives` (5982, 6010; 6917 in the validation), and at 6010 it also drops the replacement route. A
  candidate is in the reroute pass because one of its landings is off the path, and that landing comes before the
  pickup (a pickup before it would have been an arrival in the first pass). So for every rerouted arrival the
  continuation stops issuing the plan's commands at that landing and coasts to a stop there (`arriveCoasting`).
  What the committed plan does next, the second hop or the onward walk through the flag and beyond, is never rolled.
- Failure: a two-hop candidate lands its first hop beside the path, takes the flag in the air on its second hop and
  carries on over the lip behind the stand. The check judges "braked on the floor at the first landing", calls it
  safe, and the bot flies the second hop. This is the case the check was written for (`records/2026-09-25-arrive-
  safe.md`: a pickup mid-hop at 571 u/s into the base pit). The reverse also happens: the coast from a fast landing
  slides off a ledge the real second hop clears, and a good arrival is refused `arrives_unsafe`.
- Reach (the 32 retained worlds and the 40 new control worlds, `bx-scratch/arrive_after.py`): 2,263 arrival
  commits, 463 of them rerouted (6.4 a world, 20%); 55 take the flag in the air on the second hop after an off-path
  first landing. A world death (fall, hurt trigger) within 6 s of the commit: 4 of those 55 (7.3%), against 6 of 300
  (2.0%) for two-hop air arrivals that were not rerouted and 53 of 1,908 (2.8%) for all the rest. Small numbers
  (Fisher's exact p 0.05 for 4/55 against 6/300): consistent with the trace, not proof of the cost.
- Fix: keep `straightPathFree` (and, at 6011 and 6918, `rolloutPathOverride`) set across `StraightArrives`, so the
  continuation rolls the plan that will be flown.
- Outcomes: changes worlds (which rerouted arrivals are admitted). A laps pair on the usual seeds; watch runner falls
  and captures.

### BX-2 perf, P3, confirmed by trace, counted in the new worlds (rare in the 4v4): a walking deferral left behind by another search's commit blocks the landing's pre-search

- Where: `bot2_move.c:7586-7594` (`SearchBudgetStep`), `7524-7543` (`PoolChunk`), `7433` (`WalkDefer`, the only place
  that ends it), `8016-8032` (the dispatch), `7352-7355` (`DropDefer` returning 0).
- Defect: review M13 ended a drop deferral (`deferred` 2) that outlives its take frame. The walking deferral
  (`deferred` 1) has the same hole. It is ended only by `WalkDefer`, which runs only on a frame the straight search
  is eligible. When the gap search, a jump or climb edge's planned search, or a live drop search commits a sequence
  while a walking deferral is pending (150 ms of every 350-400 ms a bot walks), the straight search is
  not eligible again until the landing. For the whole flight `SearchBudgetStep` returns in its deferral branch
  (7587-7593): the stale deferral's remaining chunks are still rolled out of the shared pool from a state the bot
  has left, and no landing pre-search is predicted. At the landing `WalkDefer` clears it (counted as `abandoned`)
  and the landing search runs whole on that frame.
- Cost: one whole straight search on a landing frame (the frame the search budget exists to protect) per such
  commit, and up to five frames of wasted pool rollouts. Timing only: the landing's decision is the live search's.
- Count (the diagnostic tally in the 40 new control worlds): 8.0 such landings a Kejim world (of about 65 gap and
  edge commits) and 15.1 on Dantooine (of about 290 gap, edge and climb commits). With no other player inside
  `bot_moveSearchAlone` (2048 u), where the early search would have run: 0.1 a world or fewer on both maps. So in
  the 4v4 it costs wasted pool chunks and almost no pre-search; alone (the level gates' cost row, a 1v1) every one
  of these landings loses its pre-search.
- Fix: in `SearchBudgetStep`, end a walking deferral that is past its take frame without having been taken, or
  whose bot has a sequence in flight (`m->plan.count && m->executor.stage<Onward(&m->plan)`), then fall through to
  the early search as M13's line does for drops.
- Outcomes: latent for decisions (work counts move; a replay should hold where the early search is not used, and
  shift takes where it is).

### BX-3 bug, P3, confirmed by trace for one case, plausible for the rest: a cached rerouted arrival is validated by a rollout the judge never ran, and refused

- Where: `bot2_move.c:6910-6922` (`SearchCacheValidate`), `5724` (`RerouteSearchFor`'s landing test), `5983-5988` and
  `6032` (the judge marks a path-free arrival `rerouted` with `routeCells` 0), `6805` (the commit asks for a route
  only when `routeCells>0`).
- Defect: an arrival found by the reroute pass's first continuation needs no route: the judge keeps the measure of
  that rollout (the current path, no path stop) and sets `rerouted` with `routeCells` 0. The validation tests only
  `rerouted`: it plans a route from the last landing and re-rolls the candidate along it from path index 0. When the
  pickup came before the last hop landed (`land[h]<0`), `RerouteSearchFor` returns 0 at 5724 and the take is refused
  outright; that case is certain. Otherwise the re-roll walks its onward stage along a different path than the
  measured one, so the byte compare of the measure fails unless the arrival came with no onward frames.
- Failure: a pre-searched winner of this kind is never taken. At a landing the frame pays a whole search; a walking
  deferral is refused (not a state refusal, so no rescue), deferred anew, and refused again for as long as the same
  candidate wins. A live search commits the same candidate without a route, so nothing is lost but frames and, for
  a walking bot, the hop.
- Reach: 6.4 rerouted arrival commits a world; how many takes they cost is not in the logs (`refused_validate` is 95
  a Kejim world for every cause together; `bot_moveSearchWhy` would split it).
- Fix: validate by the rollout that produced the measure: `rerouted && routeCells>0` as now; `rerouted` alone with
  `straightPathFree` set, the frame's own `before` and no override.
- Outcomes: changes worlds slightly (takes where there were whole searches or none).

### BX-4 bug, P3, confirmed by arithmetic, latent: the ground guard looks one frame past the take, not `N-1`

- Where: `bot2_move.c:7448-7463` (`WalkDefer`), `7113` (`SearchPredictAt`'s horizon).
- Defect: the guard scans the predicted frames `1 .. targetFrames+ground-1` for the first one off the ground, and
  its comment promises the take frame and `N-1` frames after it. The prediction it scans was built with a horizon of
  `(boundary-level.time)/step+2` frames, and frame 0 is the present, so it holds the take frame and one frame
  after it. With the default `bot_moveDeferGround 3` the scan is cut at `searchBuildResult.count`: a walk that
  leaves the ground two frames after the take is not seen. (6 frames: target 6, count 8, scanned 1..7, promised
  1..8.) The shortened second prediction is consistent with this: it cannot see the lip it was shortened for.
- Effect: the measured guard (2026-10-09) is what the code does, so nothing regresses; `N` means `N-1` from 3 up,
  and 1 and 2 both mean "one frame after".
- Fix: build the guard's prediction `ground` frames past the boundary (a horizon argument to `SearchPredictAt`), or
  correct the comment and the cvar's description.
- Outcomes: the first changes worlds a little (more shortened deferrals near lips); the second none.

### BX-5 bug, P3, confirmed and counted: a planned hop that is hit in flight, or lands short, has its edge charged as "walked"

- Where: `bot2_move.c:3267-3269` ("walked"), `2443-2450` (`LiveCause`: a contact counts as transient for 1.5 s),
  `2532-2539` (`PlanCompleted`: every edge of a completed chain is `Graph_EdgeResult(e,1)` with its measured time,
  wherever it landed), `396-413` (`PathProgress` keeps the index at the takeoff for a bot beside a chord it had
  reached).
- Defect, hit: a bot launches a planned edge, is hit, and its knock recovery puts it down on the chord. The plan is
  gone, the bot is more than 64 u along the chord with no sequence, and the edge is avoided as "walked". By then the
  hit is usually more than 1.5 s old, so the cause reads world and the edge every bot plans over is charged for a
  defender's throw.
- Defect, short: a planned hop that lands on its chord more than 48 u short of the landing cell keeps its path index
  at the takeoff. The plan completes after the onward walk and the edge is recorded traversed; on the next frame
  the same edge is charged as "walked". One flight, a success and a failure.
- Count (`bx-scratch/flew_then_walked.py`, 20 control worlds a map, charged "walked" avoids naming the edge the
  bot's own chain was committed for under 3 s before): Kejim 53 of 834, of which 45 hit in flight, 5 completed and
  short, 3 after an objective change; Dantooine 16 of 414 (10, 2, 4).
- Fix: for the hit, carry the knock as the cause until the bot has replanned (or do not run the "walked" test on
  the edge a knock interrupted). For the short landing, decide what it is: the flight's failure (do not book the
  success; charge it at the landing as "short") or fine (advance the index along the flown chord).
  `bot_moveChargeFlown 1` already stops the hit case (the plan is gone, so the edge counts as not flown) and keeps
  the short case charged.
- Outcomes: changes worlds; small.

### BX-6 perf, P3, confirmed by reading: a failure that turns nothing off still invalidates every reach set

- Where: `bot2_graph.c:747` (`Graph_EdgeFail`: `graphGeneration++;reachTopology++` before the failure count is
  tested), `1357-1396` (`Graph_Reaches` rebuilds a goal's set when `reachTopology` moved).
- Defect: the first world failure of an edge changes no topology (`failures` 1, not off, not disabled), yet it bumps
  `reachTopology`, so the next `Graph_Reaches` of every goal floods again; the judge calls it per admitted landing
  (`bot2_move.c:6127-6131`).
- Count: 79 charged failures in a Kejim world of which 16 turned an edge off (retained `on-3`), so 63 needless
  invalidations; 88 and 24 on Dantooine (`dantooine/on-3`), 64 needless. With `bot_moveChargeFlown 1` three fifths
  of Kejim's failure lines and a third of Dantooine's go (any edge type: 71.0 -> 27.8 and 97.2 -> 66.5 a world).
- Fix: bump `reachTopology` only where the edge goes off or is disabled (the `PathChange` calls mark the places).
- Outcomes: latent (same sets).

### BX-7 bug, P3, confirmed by reading, latent (no contaminated search in 8,538 printed): a search cut short by contamination charges the edge to the world

- Where: `bot2_move.c:6080`, `6613-6638` (the judge marks unjudged candidates "refused", none is admitted, and the
  planned edge is avoided as "refused"); `4757-4758`, `7292`, `7315-7316`, `4783-4785` (a deferred drop search that
  stopped at contamination is `PRE_READY` with no winner; an exact take charges it without the counterfactual).
- Defect: `v.best` is -2 only when the guard could not begin. A guard contaminated part-way leaves `v.best` -1, and
  the planned strafe edge (or the drop edge) is charged for candidates that were never rolled.
- Fix: treat a contaminated judge as `guard_busy` (no charge, retry), and a contaminated deferred drop search as a
  stale result.
- Outcomes: latent; every gap, drop, climb and spike search line in the eight retained Kejim worlds prints
  `contaminated=0`.

### BX-8 quality, P3, plausible: the cache's context does not hold the avoid list or the graph's generation

- Where: `bot2_move.c:6881-6895` (`SearchCacheContext`), against `6127-6131` (route check by `Graph_Reaches`) and
  `6110-6112` (the route gate on a route planned under the avoid list of the chunk that judged it).
- Defect: an edge charged off, an edge back, or an avoid lapsing (`AvoidExpire`, which only asks for a replan, and a
  replan waits for the sequence to end) between the prediction and the take changes what `no_route` and
  `slower_route` would say, and the taken set keeps the old verdicts. The winner's route itself is planned again at
  the validation and at the commit, so no wrong route is installed; what can differ from a live search is which
  candidates were refused on route grounds.
- Fix: store `Graph_Generation()` and the avoid count in the context (a refusal costs a whole search, so count
  refusals first with `bot_moveSearchWhy`).
- Outcomes: would change worlds by the refusals it adds.

### BX-9 quality, P3, confirmed: a stale cached selection is not searched live on the next frame

- Where: `bot2_move.c:6788-6803` and `6855-6856` (comment: "hands the boundary to a live search on the next frame,
  past the lookahead cadence too"), against `7380-7385` and `7427-7467`.
- Defect: written before the walk deferral. The next frame is no longer a landing, so `WalkDefer` starts a 6-frame
  deferral instead: the kept speed the comment is about is gone either way, but the hop comes 150 ms later than the
  comment says, and the planned edge may be "walked" by then. Rare: it needs `StatePreferred`, the tape or the
  forced override to pick a candidate other than the cached winner and that candidate to fail its validation.
  Related text that no longer holds: the drop deferral's header (7271-7274) says a stale result waits
  `MOVE_GAP_RETRY`; the take (7298-7301) and `DropDefer` defer anew at once. `searchStateField` in the
  `cache_stale` line is whichever refusal last wrote it, any client's, when the validation failed on its route or
  guard.
- Fix: let a frame after `cacheStale` search whole (a flag `WalkDefer` reads), or correct the comment.
- Outcomes: latent in practice.

### BX-10 perf and bug, P3, confirmed by reading: a guest's commit rolls its plan once more outside any guard scope, with the last rollout's stop state

- Where: `bot2_move.c:6246-6252` (`GuestTraceRecord`, called from `StraightCommit` at 6288), `4984-5005`
  (`StraightCommand`'s statics), `bot2_forecast.c:1255-1257` and `1503` (a rollout with no scope open hashes the whole
  entity array and `level` before and after).
- Defect, cost: with `bot_moveGuestTeammate` (default on) every straight or planned commit by a guest (six of the
  eight bots in the 4v4; 1,021 commits in the retained Kejim world `on-3`) runs `RunCandidate` after the judge's
  guard has closed. `records/2026-09-22-search-speed.md` moved every other lone rollout into `ScopedForecastRun`
  because those two hashes were most of a lone rollout's cost; this one came later and was not moved. It lands on
  the commit frame, which a taken pre-search otherwise makes cheap.
- Defect, state: the call does not reset `straightStopTime`, `straightLandings` or `straightOffPath` and leaves
  `straightPathFree` 0. The rollout's hold after the last landing therefore lasts until whatever stop time the
  previous rollout (often another candidate's) set, and its landings are tested against the path only past the
  previous rollout's landing count. After a rerouted commit the path is already the new route while
  `straightRootIndex` is the old route's index: a landing that is tested is off that window and the rollout ends
  there. The frames feed `TeammatePredict`, so a teammate's hop is judged against a flight that can stop short.
- Fix: reset the three statics, set `straightPathFree`, and run it through `ScopedForecastRun` (or keep the judge's
  frames for the winner instead of rolling again).
- Outcomes: the state part changes guests' `teammate` refusals slightly; the scope part is latent (same frames).

## BM-3, as measured (fixed behind `bot_moveChargeFlown`, default 0)

See `docs/movement/records/2026-11-09-edge-charge-flown.md`. Facts the lane report did not have:

- Size (20 control worlds a map): "walked" charges the shared graph 41.7 times a Kejim world and 20.7 on Dantooine,
  and is about two thirds of all 30 s strafe-edge offs (4.8 and 6.5 a world); "declined" 1.5 and 0.1. No edge was
  disabled for the map (`disabled=1`) after either in 72 default worlds: the seeds' strafe edges are validated, so
  the second failure is a timed off. On a map with no prepared seed it would be a disable.
- Half of the charged "walked" avoids follow a straight hop committed under 3 s before (it came down on a later
  chord), a fifth a planned chain that flew another edge, a tenth a rerouted hop.
- "declined" charges whatever edge is the next crossing within four path entries, of any type (the event line says
  `edge=strafe` regardless): a drop, jump, pad or lift edge can be charged for a corridor refusal on the approach.
- The six guests of the 4v4 already work without the shared charge. No per-frame avoid-and-replan run exists for
  them in 16 retained worlds, which is why the cvar's arm was expected not to loop, and it did not ("walked" avoids
  275.0 against 273.4 a Kejim world, replans -5 +- 28).
- Result of the pair: charges gone as intended, outcomes undecided on both maps (team captures -0.25 +- 0.32 Kejim,
  -0.12 +- 0.40 Dantooine), Kejim's work flat, Dantooine's steps +3.2% +- 1.8. Default left 0.

## Checked and found sound

- The reroute memo's key (`RerouteKey`, 5793-5808) misses the path's own cells and `straightRootIndex`, which
  `RerouteJoins` reads. Both are constant between two `RerouteMemoClear` calls (one `StraightSearchCore` entry, live
  or one chunk), and the avoid list and graph generation, which the search does change mid-call (`StateAvoid` at
  6637), are in the key. No stale hit.
- `RerouteInstall` against the plan: the route is installed before the commit and `pathIndex` set after it; the
  run-up and flight (`HopStep`) read neither until the last landing. A pending `path.replan` it clears was asked
  under the avoid list and box the reroute was planned with. A rerouted hop that misses leaves a path that starts
  at a landing never made: `PathProgress` then returns -1 (or a rejoin within 128 u), the walk declines, and the bot
  replans once `MOVE_PATH_RETRY` has passed; no loop.
- `SearchChunkWith` restores everything the pre-search writes outside its own cache (movement state, player state,
  animation, level time, the parity fields); `straightExit`, `takeRefusal` and `walkDeferTake` are each reset before
  their next read. The judge's resume state (`judgeState_t`) carries every local a suspension can cross; the walk
  references are rolled in the last chunk only.
- A live judge cannot be suspended (the `pre->` writes at 6592 with `pre` NULL): both budgets are zero outside
  `PoolChunk` and `SearchBudgetStep`'s chunk.
- `DropTake`'s rebuild from the live state, its three recipes and the pool accounting; `Move_PoolFrameEnd`'s order,
  `poolOwed` and the dead-bot case.
- The "walked" test measures from the cell centre and the takeoff window from the edge's own takeoff: on the Kejim
  seed the two differ by 13 u at the 5th and 95th percentile and never more than 23 u.
- A walked or declined edge that is only avoided by its bot (a guest today, a runner under `bot_moveChargeFlown`)
  could in principle replan every frame when the cell fallback, which ignores avoid lists, routes back through it.
  No such run exists in 16 retained worlds, where six of eight bots already work that way (no two avoids of one
  edge by one bot within 100 ms, except seven pairs with a transient cause).
- Nothing replans a route when another bot's failure turns one of its edges off; the bot arrives, is offered no
  planned hop (`PlannedStrafe` refuses an edge that is off) and walks the chord. Rare: 12 "walked" avoids of an
  already-off edge in the 32 retained worlds (`bx-scratch/off_edges.py`).

## Not covered

- `AuditOffPath`, the guest trace's compare, the decision tape's matching, `ExploreStep` (diagnostics and fixtures, off in
  laps worlds). `KnockSearch`, `ClimbSearch`, `GapSearch` bodies. The forecast's guard and replay internals.
- No finding here was confirmed by a native run built for it: BX-1's and BX-5's counts are from logs of default
  worlds and BX-2's from a tally added to the BM-3 build. None of BX-1..10 is fixed on the branch.
