# Handoff (2026-10-09; replan budget, Open and item 5 updated 2026-11-09)

Replaced in place each session; keep it to the current state. Roadmap and gates: [PLAN.md](PLAN.md). Detailed
results go in `records/`, not here. Session history up to 2026-10-05:
[archive/movement-2026-10-05/HANDOFF.md](../archive/movement-2026-10-05/HANDOFF.md).

## Current state

- **Tickrate-independent horizons** (2026-10-21, [record](records/2026-10-21-tickrate-horizons.md), uncommitted on
  `claude/vigilant-sanderson-aa0512`, build `tickfree-build-20261021-a`). Bot2's frame-counted horizons are now in ms
  (`Bot2_StepsFor`). Its per-frame budgets are counted per 25 ms of frame (`Bot2_WorkPerFrame`). The forecast's
  capacity went from 256 to 800 steps (+36 MiB BSS, not touched at 40). At sv_fps 40 the Kejim stand classes and the
  pinned ctf4 climbs are decision-identical to HEAD. At sv_fps 125, HEAD stalls in 4/4 ctf4 cases and the change
  passes 4/4 with 2.25 s climb rollouts. Bot2 command cadence at other tickrates (climb script timings, controller,
  `bot2_finecmd.c`) is the next tickrate decision; it was left in frames on purpose. Note: `addrunner` forces
  sv_fps 40, so a test at another tickrate switches with a bench `--start-command 'sv_fps N'`.
- **Branch** `master` (`movement/level1-gate` was merged and deleted 2026-10-13; renderer work since 2026-10-09
  is on the same line, docs in renderer-vulkan-in-xenjk.md). Latest build `pooldefault-build-20261007-a` (d1b05162). On by default since
  2026-10-07 (owner): route tables (`bot_movePathTables` 1), reroute joins (`bot_moveRerouteJoin` 1, first join) and
  the pool at the frame's end (`bot_moveWalkPoolFrameEnd` 700). Together, by the work meter on 16-32 paired seeds:
  estimated frames over 25 ms about -65% Kejim and -45% Dantooine against the morning's defaults (the three measured
  cuts compounded, each on the defaults before it). **Timed 2026-10-07** (pinned two cores, seeds 1-2, record "HEAD
  timing on the 2026-10-07 defaults"): frames over 25 ms -53% on both maps; p99 Kejim 19.5/21.25 ms (meets the 25 ms
  target), Dantooine 27.5/27.75 ms (does not; about 350 frames a world over 25 ms). Kejim worst frames still ~144 ms.
- **Review fixes change worlds** (2026-10-08, owner: commit all): area 5's fixes (docs/review/FINDINGS.md M1-M8;
  frame-end pool missile hide, flag-pass replay key, cache-validation root index among them) moved laps captures
  Dantooine +1.11 +- 0.37 a world (8 seeds) and Kejim -0.49 +- 0.36 (16, undecided). Watch Kejim captures; controls
  for later arms must be on a build with these fixes. Area 6's fixes (D1-D6: missiles fired mid-frame now hidden too,
  the chase D's rearm-anywhere works) then moved Kejim +0.59 +- 0.35 and Dantooine -0.00 +- 0.47. Area 7's (O1-O14)
  moved neither (-0.03 +- 0.40, -0.06 +- 0.55). The classic Bot2 (macro AI, off-mesh states, phantom Pmove, wallrun
  tools) was then removed (owner, 2026-10-08) and replays those worlds exactly; a declined Move frame stands still.
  Area 8's fixes replay them too except Kejim 11 (fall damage counted once); the cost fixture
  (`movement_cost_runner.py`) starts again, with `bot_movecost_start`.
  Its open items then (F15: the climb entry tests the command's yaw and the frame's reach) left Kejim equal and moved
  Dantooine runner falls 4.07 -> 2.47 (undecided); `route_approve.py` pins a class with `bot_cornerDefer`; a laps
  world's `setup_late_commands` counts setup commands a stall delayed.
- **Competitive Force is the default** (owner, 2026-10-08): base_enhanced's drain rework 2, grip rework 2, grip
  cooldowns and distance, pushes on knocked-down players, team energize to the living, protect 3 at 0.70, TH/TE bind
  redirect, force-timer rounding and timer messages ([competitive-ctf-research.md](../competitive-ctf-research.md)).
  Worlds and controls from before 8cf28730 ran stock Force: compare new arms against controls on the new defaults, or
  pass `tools/fixtures/competitive/stock_force_profile.cfg`'s values (`--set`) for a stock arm. Measured with the
  profile on: runner falls Dantooine 2.47 -> 3.70, Kejim 7.40 -> 8.81 (each within noise, all one way). Since the
  next commit the defaults also carry base_enhanced's fight, weapon and spawn changes (be_combat.c); their laps
  comparison is deferred to the end of the port work (owner, 2026-10-08).
- **C: drive** fell from 220 to 188 GB free during 2026-10-06 while the play staging folder held 854 MB: something
  else is filling C:. Check both drives before launching.
- **Milestone B** ([MILESTONE_B.md](MILESTONE_B.md)): the 4v4 (offense, camper, base, chase a team) runs on Kejim
  (~8.8 captures a world) and Dantooine v3 (~9.3), red/blue balanced. Run it with
  `defense_laps.py --two-sided --camper --roles base,chase` (`--map ctf_dantooine_v3` for Dantooine).
- **Frame cost** (owner target: 4v4 p99 under 25 ms). Solo p99 19-20 ms, 2v2 24.5-26, 4v4 28 ms on Dantooine and
  ~37 ms on Kejim. Defaults that got it there: no early search near other players (`bot_moveSearchAlone` 2048),
  deferred walking search with rescue (`bot_moveWalkDefer` 6, `bot_moveWalkPool` 12, `bot_moveWalkRescue` 1),
  deferred drop search (`bot_moveDropDefer` 6), exact climb bound (`bot_moveClimbBound` 1), and since 2026-10-06 the
  takeoff-window entry search deferred too (`bot_moveWindowDefer` 1, owner): Kejim 4v4 p99 35.5-40.5 -> 25.8-27.2 ms
  alone, frames over 25 ms halved; Dantooine camper captures -0.64 +- 0.23 a world, accepted. The walk deferral chains
  fewer hops (about 22 fewer a Dantooine world); owner accepted that for the performance gain.
  Kejim 4v4: the deferrals trade about 1.5 runner falls a world for 2 fewer weapon deaths (total flat).
  Records: [tail composition](records/2026-10-04-tail-composition.md),
  [deferral behaviour](records/2026-10-05-deferral-behaviour.md).

## Next action (2026-11-10, review follow-up)

Branch `claude/bot2-review-next` (not merged, pushed or installed): `renderer/pbr-materials` with
`claude/bot2-replan-budget-be5722` and `claude/bot2-exec-deep-bab45d` merged in, then the 2026-11-09 review's next
actions 2-5. Status per finding: [docs/review/2026-11-09-bot2.md](../review/2026-11-09-bot2.md). Waiting on the
owner, in this order:
1. **Timed runs:** one repetition each done 2026-11-10 ([record](records/2026-11-10-timed-pairs.md)): pool scopes
   0/1 and the guide part (with and without the bound) are within noise of the defaults; the guide part stays 0
   (captures settled, no visible wall-clock gain), pool scopes stays 1. Dantooine p99 is 28.5-29.5 ms (target 25).
   Still to run, when the owner has an idle machine: repetitions, and a sampling profile of one Kejim world for BP-2.
2. **Default on since 2026-11-10 (owner, 1fb110d0):** `bot_defenderDropReach` + `bot_defenderReachEdge` 1 (BA-2/9;
   Kejim team captures +0.68 +- 0.32, undecided; [record](records/2026-11-09-defender-reach.md)), `bot_moveAvoidFull`
   1 (the standoff loop; [record](records/2026-11-09-frame-cost-lossless.md)), `bot_carrierSkHomeMs` 5000 (BA-1,
   never fires; [record](records/2026-11-09-carrier-sk-guard.md)). Watch Dantooine runners' falls and Kejim captures;
   controls for later arms need a build with these defaults (the guide campaign's ctl worlds are on the old ones).
   `bot_moveChargeFlown` and `bot_moveArriveReroute` are unchanged (0).
3. Latent fixes BP-3/4/10/11 and BA-3/O19 and the lossless BM-11/12/13, BA-12 are on by default and replay the
   control ([records](records/2026-11-09-latent-graph-fixes.md)). A profile config holding `seta bot_telemetry 1`
   keeps it archived; remove that line once by hand.
4. **New-defaults control, 2026-11-10** ([record](records/2026-11-10-new-defaults-control.md)): 48 Kejim + 16
   Dantooine worlds on build -g, against the old defaults: captures undecided (Kejim +0.20 +- 0.22, Dantooine
   +0.26 +- 0.31), Dantooine runners' falls lean up (+0.93 +- 0.56), Dantooine's work tail -13%.
   `ndctl-20261110-a/worlds/nd-*` is the control for later arms.
5. **Guide-table spreading, 2026-11-10** ([record](records/2026-11-10-guide-spread.md)): `bot_movePlanGuideSpread`
   (default 0, 22bd8a5b), arm at 8000: captures undecided (Kejim -0.29 +- 0.21, Dantooine +0.37 +- 0.35); Kejim's
   largest frame -21% of guide cells, est frames over 25 ms -6.5%, Dantooine -3%. Stays 0. Next, the owner's call:
   spread folds too (same control), or BP-2's profile (timed) for Dantooine's tail.
6. Structure work for between campaigns: [structure-plan.md](../review/2026-11-09/bot2/structure-plan.md).
7. **Review batch 1, 2026-11-10** ([record](records/2026-11-10-review-batch1.md)): twelve findings that leave
   bot-only worlds unchanged (BP-8, BP-15 comment, BP-16 part, BX-6, BX-7, BM-16, BM-17, BA-8, BA-15, BA-19, BA-22 part,
   BA-24), ce7f0cd9, replay on four worlds. BP-7 needs an edge-writer audit; BA-10 moves to batch 2 (outcome arms).
8. **Review batch 2, 2026-11-10** ([record](records/2026-11-10-review-batch2.md)): fifteen outcome fixes behind cvars
   (default 0, 8a2b00c3; `bot_moveFix*`, `bot_defenderFix*`, `bot_protectFixBA11`). All on against the new-defaults
   control: team captures undecided (Kejim -0.22 +- 0.22, Dantooine +0.57 +- 0.39), runners' falls read down; a
   Dantooine blue-ward shift splits evenly between the movement and defender halves and looks like the control's
   seeds. **Default on since 2026-11-10 (owner, a971ed85)**; 0 turns a fix off. The control for later arms is now
   `batch2-20261110-a/worlds/b2-*` (build -k's defaults replay it).
9. **2026-11-10, stopped by the owner ("pc too slow"):** 438291cc adds `bot_movePlanGuideFoldCap` and the fold meter
   (`fold_pops*` in [FRAMEWORK]), BP-7's dirty-list autosave, and `bot_moveFixBM8/9/10`, all default 0; build -l
   replays `b2-kej/dan-35/36` exactly (`foldbm-20261110-a/identity-*.txt`). Measured there: folds are 52-64% of guide
   cells and 38,000-51,000 of the largest frame's 60,000-66,000. Dantooine control extended to 48 seeds
   ([record](records/2026-11-10-dantooine-control-extension.md)): batch 2 is a null on captures, the side shift was
   the seeds, but the blue camper is pushed off about twice as often in red's end (+0.78 ± 0.26). Not run yet, in
   this order: the camper-fall attribution (`.codex-research/camperfall/`, `nomove` and `nobm5` on Dantooine 51-82),
   then the `fc` and `bm` arms (`.codex-research/foldbm/`, `arms` stage; the four `fc-kej-35..38` worlds were cut off
   are moved to `foldbm-20261110-a/stopped/` and rerun on resume). Launch one runner at a time: two alive deadlock.
   Cleanup 2026-11-10 (owner: back in a week+): runner scripts copied to
   `.codex-research/bot2-review-next-runners-20261110/` (the worktree copies are git-ignored); builds other than -k,
   -l and replan-budget -d, and danx/foldbm graph snapshots and demos, are in
   `.codex-trash/20261110T161555-bot2-review-next` (restore: `tools/research_shed.py --restore` with that stamp, or
   rebuild from the commit in each build's `*.sources.json`). BP-7 audit: [bp7-audit.md](../review/2026-11-09/bot2/bp7-audit.md).

## Next session (written 2026-10-09, end of the orchestrated day)

Today's state: `movement/level1-gate` at 2763e8ce (not pushed). Merged today: the Kejim replay fix and review
leftovers (fc1187ba; setup commands run before the next game frame, D7-D11, M10/M13/D8, T12-T15), M11 forbidden
volume + D10 late release + M9 removed (ece0796c; the nine-body lattice removed in 2763e8ce), the level-2 guard
(a66b7c10; default on in 2763e8ce), the renderer bench camera (56910513). Competitive defaults' extra runner weapon
deaths accepted by the owner, no further tests (records/2026-10-09-competitive-defaults-vs-stock.md). 2026-10-13: the first
laps worlds on master (master-build-20261013-a) match the 10-09 control in frame cost and outcomes
(records/2026-10-13-master-first-worlds.md); finer checks go to the overnight queue (docs/overnight-tests.md). Orchestration log:
`.codex-research/orchestration-20261009/` (PLAN.md lock rules v2, status.md).

In order:
1. **Frame speed** (owner: the level-2 fix stays; "we will have to find the frame speed elsewhere"). Its cost: Kejim
   est p99 +1 ms, frames over 25 ms +3% (records/2026-10-09-level2-speed-stale-library.md). Candidates:
   - Kejim's cross-map replans (about 28,000 expansions, ~85 ms each, ~42 a world; Open 1 below, options 2a/2b).
   - Dantooine's ~1,000 climb searches a world that find nothing (256 u wall climbs by campers and defenders, each
     charged as a world failure, ~13 s of rollouts a world; records/2026-10-09-kejim-replay.md, side finding).
   - Narrow the level-2 guard to the first search after a respawn or Speed purchase (not built).
   - **Replan budget** (2026-11-09, [record](records/2026-11-09-replan-budget.md); branch
     `claude/bot2-replan-budget-be5722`, not merged; from the 2026-11-09 review's BP-1, BP-5, BM-1). Three cvars,
     measured by work counts with every default off on builds `replan-budget-build-20261109-a/-b/-c`:
     - `bot_movePoolScopes` (frame-end pool chunks inside the trace cache and corridor memo): replays the
       control, 10 of 10 pairs; engine traces -22 to -29% a world (about 19 million), corridor evaluations
       -12 to -14%. **Default 1 on the branch since the owner's word on 2026-11-09** (81d3cf4a, build `-d`; unset
       replays the measured on-worlds, Kejim 3 and Dantooine 3). Next action: its timed pair on both maps, since
       only that says what the traces are in milliseconds.
     - `bot_movePlanGuideLong` 4000 (an unguided state search still running after 4,000 expansions gets its goal's
       table and starts over): Kejim replans of 25,000 expansions or more -72% / -83% (two sets of 16 seeds), worst
       frame's expansions -18 to -25%, estimated frames over 25 ms and p99 unchanged; Dantooine no gain, table work
       +7%. Team captures undecided and down on both maps (Kejim -0.28 +- 0.25 over 32 seeds, Dantooine
       -0.28 +- 0.36 over 16). Stays off: needs the timed Kejim pair (frames over 50/100 ms) and
       `laps_campaign.py` on the captures.
     - `bot_movePlanBound` 4000 + `bot_movePlanBoundPct` 300 (a long search stops once its lowest key passes 300%
       of the cell route's cost): with the one above, largest replan a further -27 to -29% on both maps and
       Dantooine's worlds replay; alone, nothing decided. Off; goes with `bot_movePlanGuideLong` if that is taken.
       Not below 300: the state route of a long search costs a median 120-153% of the cell route.
     - Found on the way: Dantooine's replan tail is 320 already-guided searches a world of 4,000 expansions or more
       (not first asks); guidance table work (12-15 million settled cells a world, up to 77,000 in one frame) was
       outside the work meter until now (`guide_pops` on `[FRAMEWORK]`; `est_*` reads slightly higher from this
       branch on); `tools/laps_work.py` scores a paired work-count arm; `tools/laps_identity.py` had stopped at
       the guide preload's wall-clock field on every pair since 2026-10-30 (fixed on the branch).
2. ~~Level-1 gate leftovers~~ **Closed 2026-10-14** (records/2026-10-14-overnight-items-6-7.md): the re-explored
   library `reexplore-chain-20261014-b` met row 2 (14,450 against 18,000) and row 4, Kejim floor 36/36, parity 0; the
   owner made it production (`tools/play_vs_runner.py` `LIBRARY`, option A). Rows 1, 3, 5 and level 2 not re-taken.
3. ~~Client checks~~ **Done 2026-10-14** (records/2026-10-14-client-checks.md): no prediction misses; drain icon,
   spectator timers and CTF stats work.
4. Renderer items are tracked in [renderer-bench.md](../renderer-bench.md) "Next session" (status 2026-10-13), not
   here.
5. **Review 2026-11-09, executor** (branch `claude/bot2-exec-deep-bab45d`, not merged; see Open). Owner decisions
   wanted: the defaults of `bot_moveChargeFlown` and `bot_moveArriveReroute` (both 0, both measured undecided).

## Open

**Edge charges for flights never tried (review BM-3, 2026-11-09, [record](records/2026-11-09-edge-charge-flown.md)).**
`StateAvoid` charges the shared graph for "walked" and "declined" edges no flight was tried on: 41.7 charges a Kejim
4v4 world and 20.7 on Dantooine, about two thirds of all 30 s strafe-edge offs; never a `disabled=1` on the prepared
seeds. `bot_moveChargeFlown 1` (default 0) keeps those on the bot's own avoid list only. Pair of 20 seeds a map on
build `chargeflown-build-20261109-b`: the charges go (0.4 and 0.05 a world left, flights that landed short), team
captures -0.25 +- 0.32 Kejim and -0.12 +- 0.40 Dantooine (undecided), falls and hops within noise, Kejim's work
flat, Dantooine's forecast steps +3.2% +- 1.8 (undecided). The second read of the executor that went with it
(`.codex-research/review-bot2-20261109/exec-deep.md`, BX-1..10, none fixed): **BX-1 (P2)** the arrival safety check
(`bot_moveArriveSafe`) rolls a rerouted arrival only to its off-path landing, so what follows the pickup is never
judged (a fifth of arrival commits; 4 of 55 mid-air second-hop pickups followed by a world death within 6 s against
6 of 300 not rerouted); the rest are P3 (a walking deferral orphaned by another search's commit, a cached rerouted
arrival that is always refused, a hop hit in flight charged as "walked", a guest's commit rollout outside a guard
scope).
**BX-1 fixed behind `bot_moveArriveReroute`** (default 0, owner asked for it; [record](records/2026-11-09-arrive-reroute.md)):
the check now rolls the plan as it will be flown and refuses about one rerouted arrival candidate a world the old one
admitted; 20 seeds a map against the reused BM-3 control (which this build replays on 4 of 4 seeds): team captures
-0.12 +- 0.29 Kejim, -0.18 +- 0.49 Dantooine (undecided), falls and work within noise.

**Level-2 speed row (2026-10-09, [record](records/2026-10-09-level2-speed-stale-library.md), branch
`research/l2speed-20261009`).** Failed on the defaults (14,800 / 15,175 / 14,875 / 16,050 ms against 13,000): not the
library (selected, never consulted; identical times without it) and not the frame-end pool, only `bot_moveWindowDefer`:
the deferred first search at the stand returns after the bot has walked off the lip 44 u away, `StateAvoid` "walked"
kills the stand's only strafe edge, the first hop comes at 6.9 s instead of 3.3 s. Fix, **default on since 2026-10-09
(owner: "the level 2 fix is good to take, we will have to find the frame speed elsewhere")**:
`bot_moveDeferGround 3` + `bot_moveDeferGroundWindow 80` + `bot_moveDeferGroundShorten 1` (take brought forward to before
the lip, only near a planned takeoff). With them and every other default on: 10,600 / 11,000 / 9,675 / 8,200 ms, twelve
level-2 cases pass, parity 0 mismatches; Dantooine frame cost unchanged, Kejim +3% frames over 25 ms (undecided) and
+1 ms est p99 (2.3 se), accepted; frame speed is to be found elsewhere (Open 1).

Next (owner, 2026-10-07): the reroute state searches (`RerouteSearch`, bot2_move.c; about 55% of Dantooine's
expansions in frames of 10,000 or more), then the replans' share. Measure by work counts. Tried 2026-10-07 and closed:
skipping a candidate's reroute search when a walk-price floor already fails the route gate (`bot_moveReroutePrune`,
default 0, 72139548; record, "Reroute price floors"): exact but saves 0.3-3.2% of reroute expansions for more table
work than that. Then (owner's choice) reroute searches that rejoin the route being left (`bot_moveRerouteJoin`,
default 0, 7426077f/c1b799e5/12a6c04e; record, "Reroute searches that rejoin"): ending at the first join cuts
reroute expansions to 1-3% and estimated frames over 25 ms by 21-23% on both maps with hops unchanged, but captures
trend down on both (16 paired seeds: all captures -1.48 +- 0.72 Kejim, -1.24 +- 0.83 Dantooine). The owner's fix
(option 1, 5eee4657: joins only for the speed and Speed state the route left planned, never a route planned around
bodies) gives back most of the work (Kejim frames over 25 ms -7%, Dantooine no change) while Dantooine's captures
still fall (-1.29 +- 0.87, conversion 0.19 -> 0.17); now optional (`bot_moveRerouteJoinValid`, default 0). Looking
for the mechanism (owner, option 2): carry legs unchanged (leg time, deaths at home, home waits), the loss moves
between roles run to run, and 16 fresh seeds (19-34) show none: over 32 seeds first join reads Dantooine team
captures -0.06 +- 0.32 (no change within 15%), Kejim -0.34 +- 0.24 (all captures -0.75 +- 0.47, unresolved), frames
over 25 ms -20% Kejim / -23% Dantooine, p99 -6 / -14 ms, hops unchanged (record, "Where the captures went").
Kejim at 48 seeds: team captures -0.27 +- 0.20 (-7%), runner -0.09 +- 0.17, camper -0.18 +- 0.13; still undecided.
**Enabled by default** (owner, 2026-10-07; b9addd2e, build `joindefault-build-20261007-a`): `bot_moveRerouteJoin` 1,
slack 1000000 (the first join). Watch the Kejim camper captures in later outcome runs.
**Next session** (owner: resume in a new session): (1) done 2026-10-07: pinned timing of HEAD, Kejim p99 under 25 ms,
Dantooine 27.5-27.75 (see Current state); (2) why Kejim worlds do not replay on one build; (3) then the replans' share
(Kejim's cross-map replans, its 144 ms worst frames); Dantooine's remaining tail is landing straight searches and pool
chunks by the last `[FRAMESRC]` read (2026-10-06, before today's defaults). Earlier: option 2a (split state searches, `bot_movePlanSplit`, default 0;
record, "State searches split over frames" and "Timing noise, a work meter, and where the planner expansions go") is
built and measured: it removes the replan from the tail but the frames' planner work stays where it was. Measure by
work counts (`[FRAMEWORK]`, `[FRAMEEXP]`, worlds four at once): single-world wall-clock tails swung 2.4 times between
two identical runs. Route tables for the defenders' cell queries (`bot_movePathTables`, default 1 since 2026-10-07, 7261c506; record,
"Route tables for the defenders' cell queries") are built and measured: estimated frames over 25 ms -10% Kejim (16
seeds) and -14% Dantooine (8), p99 expansions -20-26%, captures and falls no worse, hops unchanged; enabled by default
(owner); a review fix (2026-10-07) folds queued edge changes as one batch, since one at a time could lose routes
(record, same section). Done 2026-10-07: option 2c (`bot_movePlanEnclosed`, default 1,
318ff612; record, "Enclosed goals skip the body pass"): a replan whose goal the bodies enclose skips the pass around
them; exact (Kejim worlds replay), Kejim's 2-5 body fallbacks a world 110-196 ms -> 1 ms. The worst Kejim replans
left are the cross-map searches (options 2a/2b). Measured 2026-10-06 (record, every section
from "Two-core spec" on): 74% of candidate rollout
work is in searches that commit nothing, failing on safety and geometry rather than score; a failure memory carried
between games saves only about 3-6% at about 0.5% of chosen hops changed, because (state, candidate) pairs seldom
recur. Planner guidance with preloaded goal tables (`bot_movePlanGuide`, `bot_movePlanGuideMax`) is **on by default
since 2026-10-30 (owner)**. On 2026-10-06 (record, "preloaded goal tables") it cut Kejim frames over 100 ms 55 -> 10
over two pinned worlds, but its routes cost the runners about 7% of hops (9% of chained hops) and 15 u/s on Kejim,
so it was left off under the keep-quality rule. Re-measured on 2026-10-30 (2026-10-30 offline study,
[records/2026-10-30-hop-room-offline.md](records/2026-10-30-hop-room-offline.md): the planner's cost predicts almost none
of the runner's hop flow; at equal planned pace the most-room fifth of stretches runs +36-45 u/s, +21-26% hops,
+49-80% chained against the least-room fifth; mostly a place effect. Native guide off/on pair the same day, Kejim
seeds 3-10 on HEAD 97c25de3: the hop loss does not reproduce (hops -6.9 +- 15.9, chained -2.8 +- 9.6, speed -3.7 +- 8.9,
team captures +0.01 +- 0.42), the guided routes have the same hop room, and expansions are 1.63x fewer. Re-enable
checks the same day: pinned Kejim timing, run alone, frames over 100 ms 24.5/25.5 -> 3.0/1.5 and over 50 ms
41/43.5 -> 8.5/19 (seeds 1/2), p99 about -1 ms; Dantooine outcome pair (seeds 3-10) shows no cost (team captures
+0.68 +- 0.58, hops -11 +- 23) and no frame gain. Owner: make it the default. Untested: maps without a `.guide`
goal list (only Kejim and Dantooine have one; elsewhere tables are built in play after 3 asks, the upkeep that
cancelled the first version's gain) and pinned Dantooine timing). Open: Kejim's cross-map replans (options 2a/2b);
exact early stops; the thread spike at its re-estimated cost; the frame-end pool.

1. **Frame cost** ([record](records/2026-10-06-frame-cost.md)). **Spec (owner, 2026-10-06): two cores of the dev
   CPU**, route quality kept; timing worlds pass `defense_laps.py --cpus spec` and need an idle machine. The pinned
   baseline replays the unpinned worlds exactly. The reentrancy survey makes the thread spike several sessions with
   upstream edits (simulated `level.time`, live animation fields, Pmove globals, replay order), for at most about
   1.4 times on searches at two cores with one trace lock. HEAD reads Dantooine p99 32.5-34 ms (600-750 frames
   a world over 25 ms; target about 246), Kejim 24.5-28.5. Slow time by source (`[FRAMESRC]` diagnostic, b6aaaa6f):
   Dantooine landing straight searches 36%, pool chunks 33%, drops/climbs 18%, gaps 11%; Kejim the state planner 40%
   (cross-map replans of about 28,000 expansions, ~85 ms each, ~42 a world), pool 30%, straight 23%. Choices, each
   written out in the record:
   - **Parallel rollouts** (record, "Proposal"): worker threads run a search's candidate rollouts in parallel inside
     the frame; decision-identical, so judged by replay identity and timing alone. Spike first; blockers listed.
   - **Pool at the frame's end** (`bot_moveWalkPoolFrameEnd`, built, default 700): outcomes at 550 measured
     2026-10-07 (record, "Outcomes at S = 550"; 16 paired seeds a map on the current defaults): est. frames over 25 ms
     -53% Kejim / -22% Dantooine, captures, falls and weapon deaths hold, but hops -6% and airborne -1.8 points on
     both maps (late walking deferrals 2.4-2.8x). At 700: Kejim -52%, Dantooine -14%, hops -2 to -2.5%
     (within noise), late deferrals 1.2-1.6x, outcomes hold (Kejim runner falls +1.2 +- 0.9 to watch).
     **Enabled by default** (owner, 2026-10-07; d1b05162): unset replays the measured 700 world (Dantooine seed 3).
     Watch Kejim runner falls.
   - **Kejim worlds do not replay**: cause found 2026-10-09 ([record](records/2026-10-09-kejim-replay.md)): a map
     load over ~1.5 s (Kejim alone 1.1-1.3 s, up to 1.9 s four at once) let the first catch-up batch pass botat's due
     levels, so setup ran late. Server fix 387e69e9 (branch `review/kejim-replay-20261009`, not merged) replays a 3.8 s
     load exactly and the control's worlds when the load is short. Until it is merged, a world with
     `setup_late_commands` > 0 is not a replay of its seed.
   - **Kejim's planner** ("Options" 2a-b): spread over frames with an interim cell route, or weighted A*. 2c (skip
     the body pass when the bodies enclose the goal) is done and default on.
2. ~~Re-explore the level-1 library at the current defaults.~~ **Done 2026-10-14**
   (`reexplore-chain-20261014-b`, production since; records/2026-10-14-overnight-items-6-7.md). The gates on HEAD (`gate-20261005-b`,
   [record](records/2026-10-05-defaults-retake-and-base-walkway.md)): safety, level 2 and cost met; level-1 speed met
   only just (median 16,212.5 against 16,500, was 13,087.5) and row 3 missed at two classes, because the
   skill-90 library predates the walk deferral and now buys almost nothing. Re-explore as on 2026-09-26
   (`skill90-chain-20260926-a`). `.codex-research/defer-chain-20261005-a.sh` is that recipe, stopped after two cells on
   `gate-build-20261005-a`, which predates the edge probe and window deferral: copy it with the current build and new
   explorer names (the `explorer-defer-*` cells are not reusable); confirm with its own stand worlds, no gate re-take.
3. ~~Retire legacy movement owners~~ **Done 2026-10-07** (owner decision): the pre-rewrite runner stack, the archived research benches and `bot_moveRewrite` are removed; the rewrite is unconditional (code map: `codemp/game/ai_bot2_README.md`).

## Closed: do not reopen without a new reason

Built, measured, and left off or rejected by the owner. Their cvars stay at the off default.

- **Base D's walkway falls** (2026-10-06, [record](records/2026-10-06-base-walkway-goals.md)): the saber walk
  (`Melee`) replaced the movement's command and its brake, and ran over the lip at chase speed. `bot_defenderMeleeEdge`
  (default on, a321a3ef) gives it the guest brake test: Kejim base D falls 3.38 -> 1.31 a world (both teams), chase D
  -2.12; captures dip as defenders stay to fight. Chase goals on the runner's footprints (`bot_defenderFootprintGoal`):
  no change, left off (removed 2026-10-07).
- Base support (`bot_baseSupport`): Force delivered, no captures (removed 2026-10-07).
- Camper displacement throw (`bot_camperDisplace`), runner hand-off self-kill (`bot_runnerHandoff`), defender
  spent-reset self-kill (`bot_defenderSpentReset`): harmful or no gain in laps worlds (removed 2026-10-07).
- Rollouts ignoring player bodies after T ms (`bot_forecastBodyMs`): more runner falls (removed 2026-10-07).
- Early search budgets (`bot_moveSearchBudget`): no tail gain. Walk cadence from the deferral's start: removed.
- Guest run-up hold (`bot_moveGuestRunupHold`): more falls (removed 2026-10-07).
- Hopping less near pits under a puller; trading thrown falls for speed (owner, 2026-10-03).
- Knowledge leaks (sight cone, hearing through walls, dropped-flag positions, teammate Force): accepted as they are.
- Chase D reacting to an unseen theft: done (`bot_defenderTheftKnown`, `bot_defenderTheftGoal`, default on).
- Narrow-hull edge probe on every walk (reverted 2026-10-05): stalled the runner at its own hop's takeoff into a
  fall; owner: a change that breaks offense comes out for every bot. The kept form skips walks toward a planned crossing.
- Reroute price floors (`bot_moveReroutePrune`, 2026-10-07): exact, saves 0.3-3.2% of reroute expansions, tables cost more. (removed 2026-10-07)
- Reroute join validity test (`bot_moveRerouteJoinValid`, 2026-10-07): most of the work returns, no capture gain. (removed 2026-10-07)
- Runner travel falls: body contacts fixed (`bot_moveContactKnock`, default on); falls while camping accepted.
