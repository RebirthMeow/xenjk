# Bot2 review 2026-11-09, lane: movement executor and strafe control

Target: commit 6fe7bb2f, read at `.codex-research/wt-review-6fe7bb2f/codemp/game/`. Static reading only; nothing
was built or run. All `file:line` references are at 6fe7bb2f.

Scope read: `bot2_move.c` (executor, walk, hop, climb, knock, lift, pad, replan, `Move_Command`, the boundary
driver, deferral and pool, the search entry points), `bot2_move.h`, `bot2_movecontrol.*`, `bot2_finecmd.*`,
`bot2_kick.*`, `bot2_wallprobe.*`, `bot2_corner.c` (start and deferral), `bot2_mover.*`, `ai_bot2_movement.c`,
`ai_bot2_localnav.c`, `ai_bot2.h`. Graph and forecast were read as interfaces only.

**No measured evidence backs any finding here.** The October laps worlds are no longer under `.codex-research`,
so I could not count events in logs. "Confirmed" means confirmed by reading the code path; "plausible" means the
path exists but its reach or effect needs a log count or a replay.

Counts: 19 findings. No P0 or P1. P2: 3. P3: 16. By category: bug 9, perf 4, quality 4, suggestion 2.

## Findings, most severe first

### BM-1 perf, P2, confirmed: frame-end pool chunks run outside the trace cache and the corridor memo

- Where: `bot2_move.c:7548-7577` (`Move_PoolFrameEnd`); the scopes are opened only at `bot2_move.c:7902-7907`
  (`Bot2_MoveParityBeforePmove`). `bot2_forecast.c:249` and `ai_bot2_localnav.c:175` go straight to the engine
  when no scope is active.
- Defect: since `bot_moveWalkPoolFrameEnd` 700 became the default, `SearchBudgetStep` hands every deferred walk
  and drop chunk to the frame's end (`bot2_move.c:7586-7589`). `Move_PoolFrameEnd` calls `PoolChunk` without
  `Bot2_ForecastTraceCacheBegin` / `Bot2_GroundCorridorMemoBegin`, so those rollouts' traces, point contents and
  corridor samples are never cached. The same chunks run at the boundary were inside the scope.
- Cost: up to 700 forecast steps a frame at about 11 traces a step (the figure in `bot2_forecast.c:233`) is up to
  about 7,700 uncached engine traces a frame. The saving depends on the hit rate between sibling candidates, which
  I could not measure.
- Fix: open both scopes per bot inside the loop, after `Bot2_ThreatHideBegin` (a hide changes the world the cache
  saw), and close them before `Bot2_ThreatHideEnd`.
- Outcomes: latent. The cache returns the engine's own answers and the pool budget counts steps, so decisions
  should be identical; a laps replay should confirm identity. One caveat: BM-10 (the memo's mover wrinkle) would
  then also apply at the frame's end.

### BM-2 bug, P2, mask mismatch confirmed, reach plausible: the walk's corridor does not see monster-clip

- Where: `ai_bot2_localnav.c:72,90,100,105,112,115` (`MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME`);
  `bot2_move.c:968` (`LandsWithin`), `2906-2909` (`StepOffCell`), `2995` (stuck bail), `3980` (`LethalLip`);
  `ai_bot2_movement.c:71,78,137`. A bot's Pmove uses `MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP`
  (`g_active.c:2833-2835`).
- Defect: review M7 fixed this for the legacy simulations only. `Bot2_GroundSample` is what the walk and the
  graph's cell admission share, so a monster-clip brush is open floor to both, while the bot's own Pmove and the
  rollouts (which use the bot's mask) collide with it.
- Failure: on a map with a monster-clip brush across a walk link, the planner routes through it, the corridor
  admits it every frame, and the bot pushes against an invisible wall. Nothing ends it: the walk never declines,
  `held` needs `dist<=1`, the stuck bail fires only on `floor_startsolid`/`floor_allsolid`, and the stranded kill
  needs the bot off the graph (see BM-4).
- Fix: one bot-mask constant for every Bot2 world trace, used by `Bot2_GroundSample` and the probes above.
- Outcomes: changes worlds only on maps that have such brushes; the graph cache identity changes there too.
  Whether any of the 12 prepared maps has one is not checked (follow-up 2).

### BM-3 bug, P2, plausible: an edge the bot never attempted is charged to the shared graph

- Where: `bot2_move.c:3267-3270` ("walked"), `2948-2953` ("declined"), `2464-2469` (`StateAvoid`),
  `bot2_graph.c:739-760` (`Graph_EdgeFail`).
- Defect: `StateAvoid` calls `Graph_EdgeFail` with a world cause unless a player or missile is near. Two callers
  charge an edge no flight was tried on:
  - "walked": a bot more than 64 u along a strafe chord with no sequence in progress. That is also the state
    after a straight hop lands inside a later chord (`LandingPathProgress`, `bot2_move.c:416-425`), walks its
    128 u onward stage and the next search admits nothing.
  - "declined": any corridor refusal of the walk with a crossing within 4 path entries charges that crossing,
    whatever the refusal was.
- Failure: each charge counts toward `GRAPH_EDGE_FAILURES` (an unvalidated edge is disabled for the map, a
  validated one is off for 30 s) and bumps `graphGeneration` and `reachTopology`, which invalidates route tables
  for every bot. Over a 30-minute world the shared graph can lose edges to one bot's bad arrival state.
- Fix: for "walked" and "declined", avoid the edge for this bot only (`AvoidEdge`), as a guest already does;
  keep the shared charge for "refused" (candidates were judged) and for a missed flight.
- Outcomes: changes worlds; needs a laps replay. First count `why=walked` and `why=declined` against
  `disabled=1` lines in a retained world (follow-up 3).

### BM-4 suggestion, P3: the walk has no progress watchdog

- Where: `bot2_move.c:772-954` (`WalkStep`); `bot2_move.h` `movement_t.recovery {target, progressTime}` is
  declared and never used.
- Defect: every recovery is tied to a specific signal (a decline, an onward stage, a floor-probe refusal, being
  off the graph). A walk that issues commands and does not move, for any reason the corridor does not see, is
  not detected. BM-2 is one such reason; a body the corridor memo admitted earlier in the frame is another.
- Fix: in `Move_Command`, when the walk owned the frame, is grounded, asked for speed and has moved less than a
  few units in about a second, avoid the next link's edge (or mark the cell pair) for this bot and replan. Time
  it in milliseconds.
- Outcomes: changes worlds only where a bot is already stuck.

### BM-5 bug, P3, confirmed, latent in most worlds: an objective change drops a knock recovery or a lift ride

- Where: `bot2_move.c:2779-2787` (`Move_Plan`), `1832-1847` (`Move_Reset`).
- Defect: the arrive-safe hold keeps the old objective only while a hop sequence is between launch and last
  landing. A committed knock recovery (`knock.active`, its landing chosen by rollout) and a lift ride
  (`lift.edge>=0`) are wiped by the `memset` in `Move_Reset` when the goal moves more than 64 u.
- Failure: the runner is hit and steering in the air for an admitted landing; a flag event changes its goal
  entity; the recovery policy is gone and the walk's air control steers for the new target instead. On a lift,
  the ride's stand-still command is replaced by a walk that cannot plan (the moving lift is not standable).
- Fix: extend the hold condition to `knock.active || knock.pending || lift.edge>=0`.
- Outcomes: changes worlds, rarely.

### BM-6 quality, P3, confirmed write-only, loop plausible: a stale planned edge replans at once with no backoff

- Where: `bot2_move.c:6624-6631`; `state.staleEdge` / `state.staleCount` are written and printed, never read.
- Defect: a planned strafe edge whose entry envelope the live state misses sets `path.replan=1` and
  `retryTime=0` on every straight-search cadence. The file's own comment (`bot2_move.c:247-254`) records seven in
  a row, 250 ms apart, with the planner returning the same chord.
- Cost: one full state search every 200-250 ms for as long as the bot stands at the takeoff.
- Fix: after two stale verdicts on one edge, give this bot a timed avoid on it (`MOVE_TRANSIENT_AVOID_MS`).
- Outcomes: changes worlds where the loop occurs.

### BM-7 bug, P3, confirmed, latent on the prepared maps: the jump-pad state has no validity checks

- Where: `ai_bot2_movement.c:85-95` (`IsInTriggerPush`), `146-232` (`Bot2_JumppadMovement`).
- Defects:
  1. Any linked `trigger_push` whose box the hull overlaps enters the state: an inactive one (`FL_INACTIVE`), a
     wind or conveyor push. The state exits after 500 ms on the ground and re-enters next frame, so the bot
     holds forward at a locked point with no corridor check for as long as it stands in the volume.
  2. The only exit is "grounded and 500 ms old". A bot that lands in water never leaves it.
  3. `Move_Reset` does not touch `bot2_states[].state`. A defender that dies in a pad arc respawns through
     `Move_Reset` only (`bot2_defender.c:3080`), so its first live frame runs the pad command (forward 127)
     toward the previous life's landing before the state returns to walk. The runner is covered by
     `Bot2_ClearState`.
- Fix: require an active trigger with a target; add a time bound and a water exit; reset the state in
  `Move_Reset` or at the defender's respawn.
- Outcomes: item 3 can change a defender world by one command frame; the rest are latent.

### BM-8 bug, P3, confirmed, latent while `addrunner` forces `sv_fps` 40: frame counts and frame time in the executor

- Where: `bot2_move.c:36` (`MOVE_ONWARD_HELD` 40 frames, "one second"), `42` (`MOVE_GAP_LOOKAHEAD` 64), `87`
  (`MOVE_STRAIGHT_HOLD` 48), `56` (`MOVE_PERMIT_HORIZON` 128), `1119` (`MOVE_CLIMB_FRAMES` 160), `4387-4388`
  (climb scripts in command counts), `1185` (`landed>=2`), `7250-7254` (walk deferral 6 frames), `101`
  (`MOVE_MOMENTUM_TIME`, "four at the 25 ms step"); `bot2_movecontrol.c:83-93`.
- Defect: these are frames, not milliseconds, so every one doubles in real time at `sv_fps` 20. The held-side
  strafe computes its wish angle from the server frame's `dt`, while a fixed-step bot's Pmove accelerates in
  `pmove_msec` slices. By the kernel's own formula the 90% skill setting delivers about 88.4% of the best gain
  at 25 ms and about 86.0% at 50 ms, so strafe speed depends on the tick rate.
- Fix: express the bounds in milliseconds divided by the step; pass the Pmove slice length to the strafe branch
  when the bot steps fixed.
- Outcomes: the strafe change alters every flight (replay needed); the constants are identical at `sv_fps` 40.

### BM-9 bug, P3, plausible: a pre-pressed landing is measured from two airborne frames

- Where: `bot2_move.c:992-1000` (prepress branch sets `mark` from the frame after the relaunch), `3711-3719`
  and `3729-3738` (`SpikeMeasure`), `2491-2508` (`LearnStrafe`).
- Defect: with `bot_moveLandingJump` (default on) the landing and relaunch happen inside one command. Hop 0's
  landing is then recorded at the next frame's position (already airborne, up to about 17 u past touchdown at
  700 u/s) and hop 1's takeoff at the prepress frame's position (still airborne, before touchdown). Learned
  strafe edges take their cells and endpoints from those points, and the onward mark is the same displaced point.
- Failure: the two learned edges of a chained plan can name different cells for the one touchdown, so the
  planner cannot chain them as flown; `GUEST_HOP` miss distances carry the same offset.
- Fix: when the prepress branch detects the relaunch, record the touchdown (the rollout frame's substep
  landing, or the trace `LandsWithin` already ran) for both the landing and the next takeoff.
- Outcomes: changes what the graph learns; needs a replay and a check against the merge tolerances in
  `Graph_AddStrafeEdge` (graph lane).

### BM-10 bug, P3, confirmed, latent off lift maps: the corridor memo's key has no simulated time

- Where: `ai_bot2_localnav.c:149,172-200`; the verdict depends on `Bot2_StandableEntity` at `level.time`
  (`ai_bot2_localnav.c:74`, `bot2_mover.c:83-85`).
- Defect: rollouts move `level.time`, and a lift at rest is floor only until its scheduled start. A corridor
  over a lift judged at one simulated time is returned for another inside the same boundary, so the answer
  depends on which rollout asked first. `records/2026-10-06-frame-cost.md` lists this as order dependence for
  the thread spike; it is also a correctness wrinkle today.
- Fix: do not store a result whose floor was not the world (`groundSampleFloor!=ENTITYNUM_WORLD`) or whose
  refusal was `floor_nonworld`.
- Outcomes: can change a lift-map world; none of the 12 maps without lifts.

### BM-11 perf, P3, confirmed: the mover rest memo misses on every simulated step

- Where: `bot2_mover.c:29-48` (`RestUntil`), `64-81`.
- Defect: `RestUntil` reads only entity fields; nothing in it reads `level.time`. The memo is nevertheless keyed
  on `level.time`, so a rollout standing on a lift misses at each simulated step and rescans every entity for
  pending `multi_trigger_run` thinks.
- Cost: about `level.num_entities` pointer tests a step, only while a rollout stands on a mover.
- Fix: key the memo on the real frame and `level.startTime` alone.
- Outcomes: latent (same values).

### BM-12 perf, P3, confirmed: a stranded hold rescans for a step-off cell every frame

- Where: `bot2_move.c:2890-2913` (`StepOffCell`), called from `2977-2979` each held frame and from
  `Move_Stranded` twice a second.
- Cost: 49 grid points, each a `Graph_CellAt` and up to two hull traces: up to about 100 traces a frame for a
  bot that is standing still, for up to 10 s before the stranded kill.
- Fix: keep the answer while the bot stays within a few units of where it was computed.
- Outcomes: latent.

### BM-13 perf, P3, confirmed: `trigger_push` is found by a string scan of every entity, per bot, per frame

- Where: `ai_bot2_movement.c:85-95` (every frame, every bot), `bot2_move.c:2578-2583` and `2643`.
- Cost: about `level.num_entities` `Q_stricmp` calls a bot a frame; small, but it is pure overhead on a 2 vCPU
  server with 8 bots.
- Fix: collect the push triggers once in `Move_PrepareMap`.
- Outcomes: latent.

### BM-14 quality, P3, confirmed: the Speed purchase is requested without asking whether the game will take it

- Where: `bot2_move.c:2721-2760` (`SpeedEdge`).
- Defect: the request checks the pool and the active bit only. It resets parity and revokes the walk permit at
  request time. When the game refuses it (hand extended, saber lock, the power disabled) or a defender layer
  replaces the frame's one `generic_cmd` with a throw (`bot2_defender.c:1839-1842`), the next frame logs
  `speed_refused` and the cycle repeats every second, revoking any permit each time.
- Fix: test `WP_ForcePowerUsable(ent, FP_SPEED)` before requesting; revoke the permit on confirmation.
- Outcomes: latent in the worlds measured so far; changes a world where refusals happen.

### BM-15 bug, P3, confirmed, small: executor timing state goes stale across frames the executor does not run

- Where: `bot2_move.c:938-939`, `1024-1025`; `ai_bot2_movement.c:243-246`.
- Defect: a pad arc returns before `Move_Command`, so `executor.walking` stays 1 and `lastTime` stays at the
  pre-pad frame. The first walk frame after landing uses the pre-pad controller yaw and a `dt` of the whole
  arc: the turn limit is `360*dt` degrees and the predicted ground friction zeroes the velocity for that
  command. The knock and lift branches clear `walking`; the pad path does not.
- Fix: clamp `dt` to twice the step, and clear `walking` whenever `frameTime` is not the previous frame.
- Outcomes: one command frame after each pad; a replay would show whether it matters.

### BM-16 bug, P3, confirmed, latent (no teleporter on a route): a teleport voids nothing

- Where: `bot2_move.c:3214-3216`.
- Defect: the teleport bit only suppresses the knock test. A committed sequence keeps its origin, line and mark
  from the other side, parity is not reset, and the replan waits for the sequence to end.
- Fix: on a teleport-bit change, end the sequence without a charge, reset parity and ask for a replan.
- Outcomes: latent.

### BM-17 quality, P3, confirmed: the step function reads cvars that are not latched per frame

- Where: `bot2_move.c:153-155` (`MoveGuest`, called at `905`, `916`, `926`), `687-691` (`GuestBrakeOn`), `732`
  (`bot_moveEdgeHull`, with `atof`, on every momentum-checked walk step), `956-960` (`LandingJump`).
- Defect: `FlightModeRead` exists so "every rollout of the frame sees one value", and the header says the step
  reads no live state. These four are read inside the step, in every rollout step. The earlier review declined
  the cost (about 24 us a frame); the point here is the contract: a value changed between a rollout and the live
  frame breaks parity silently.
- Fix: read them in `FlightModeRead` with the others.
- Outcomes: latent.

### BM-18 quality, P3, confirmed: dead and write-only state, stale comments

- `movement_t.recovery`, `movement_t.commits` (declared, no use); `objectiveHeld` (only gates one print, never
  cleared when the hold ends); `state.staleEdge/staleCount` (BM-6); `MOVE_SPEND_HOLD` (set, never tested).
- `Bot2_MoveWaypointIntent` has no caller; `fast` in `Bot2_MoveControl` is `qtrue` at every call site.
- `MOVE_DECLINE_OFFMESH` and `MOVE_OFFMESH_YIELD` (`bot2_move.c:21-22`, `797-802`): the "retained off-mesh
  states" they yield to were removed on 2026-10-08; the decline is now simply held.
- Stale text: `bot2_move.h:1-7` describes phase 0; `ai_bot2_movement.c:61-62` names the wallrun staging logic;
  `PadObserve` and `DropRouteMs` price a walk at a literal 250 u/s (`bot2_move.c:2697`, `4636`) instead of
  `g_speed`.

### BM-19 quality, P3: ownership and size

- `StraightSearchCore` is 464 lines, `StraightJudge` 279, `Move_Command` 228, `WalkStep` 183. `Move_Command`
  mixes knock detection, ride ownership, purchase confirmation, chord policing, onward release, replan gating,
  the step and its telemetry; each is a natural function.
- Per-client state outside `movement_t`, which `Move_Reset` therefore does not clear: `padFlight`, `moveForbid`,
  `routePending`, `guestPredict`, `guestTrace`, `dropDeferred`, `poolMove`, `poolOwed`, `split` (cancelled, not
  cleared), `kicks` in `bot2_kick.c` (an armed kick survives a slot's next occupant). I found no live failure
  from them: each is keyed or re-checked on use. They are one missed check away from the class of bug F16 fixed.
- Search scratch shared by all clients (`parity.frames`, `lookaheadResult`, `liveResults`, `rerouteScratch`,
  `straightStopTime` and its siblings, `hopStopTime`, `runCandidateSteps`, `rolloutFallDrop`, `counterfactual`)
  is safe only because searches never interleave. I checked the one cross-client read I suspected (the gap
  lookahead test at `bot2_move.c:6474` during a frame-end chunk): it cannot fire, because the chunk runs with
  `level.time` set to the future take frame.

## Earlier review's open items, re-checked

- M12 (lift rest taken as POS1): still holds, `bot2_move.c:3125-3128`.
- M14 (`Move_Reset` prints the tape census for any client): still holds, `bot2_move.c:1837`.
- M15 (tape paths cut at `MAX_QPATH`): unchanged.
- `KnockSearch` clears `pending` before its guard check: still holds, `bot2_move.c:4509-4511`.
- F4 (FACING lift calls kept, the ride does not aim): unchanged, `bot2_mover.c:105-111`.
- The M10 and M13 fixes read correct (`bot2_move.c:831-834`, `7583`). The M11 forbidden volume
  (`ForbidSync`, `ForbidTarget`, `PathEntersBox`) reads correct; `ForbidTarget`'s cell fallback keeps the far
  target's stop distance, which only brakes later, not earlier.

## Needs a high-effort follow-up

1. BM-1: measure trace calls and cache hits for the same chunks at the boundary and at the frame's end, then a
   timed two-core run with the scopes added. This is the only item here with a likely frame-cost payoff.
2. BM-2: scan the 12 prepared maps' brushes for `CONTENTS_MONSTERCLIP` before deciding whether it is live.
3. BM-3 and BM-6: count `why=walked`, `why=declined`, `reason=stale` and the `disabled=1` lines that follow them
   in one retained laps world per map.
4. A second high-effort read of `StraightJudge`, `SearchCacheValidate`/`SearchCacheTake`, `RerouteSearchFor` and
   install, and the drop deferral (`bot2_move.c:5877-7430`): the densest shared-static code, and the walk
   deferral's ground guard (2026-10-09) changed it after the earlier review.
5. BM-9 with the graph lane: whether displaced endpoints stop chained edges from merging or chaining.
6. BM-4: design of the walk watchdog (what it charges, and how it avoids firing on a deliberate hold).

## Not covered

- Read only at their entry and exit, not line by line: `StraightJudge`, the search cache (`SearchCacheTake`,
  `SearchCacheValidate`, `SearchPredictAt`), reroute search and install, `DropBuild`/`DropEvaluate`/`DropTake`,
  `PlannedSearch`, the body of `ClimbSearch`, `SpikeSearch`, exploration, the decision tape and its library, the
  parity diff tables, the guest trace.
- `bot2_corner.c`'s shadow pursuit and argument parsing above line 200; `bot2_movebench.c`, `bot2_movecost.c`
  (not in this lane's list).
- Format strings were not re-audited (the earlier review did all six slices). I checked only that the longest
  `[SPIKE]` line fits `Bot2_PrintTelemetry`'s 1,024-byte buffer (about 800-900 bytes).
- No compile, no extracted-C test: the strafe figures in BM-8 are hand arithmetic from the kernel's formula.
