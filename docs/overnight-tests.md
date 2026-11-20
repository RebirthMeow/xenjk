# Overnight tests (queue)

Started 2026-10-08. Owner: keep daytime checks minimal (an inert replay, the movement bench); the long runs below wait
for an overnight slot. Run them in order, at most four worlds at once, after checking both drives' free space. Record
results in a dated record under `docs/movement/records/` and tick the item here.

Each laps arm is 8 Dantooine (`ctf_dantooine_v3`) + 16 Kejim (`mp/ctf_kejim`) worlds with the settings of
`.codex-research/be-combat-20261008-a/run.sh` (C/D strings), compared with `tools/laps_compare.py`. About 20 minutes
per arm on one fresh build.

## Queue

1. **Competitive defaults against stock, one build.** Arm `stock` passes every value in
   `tools/fixtures/competitive/stock_force_profile.cfg` (`--set name=value`); arm `default` passes nothing. Answers:
   what the whole base_enhanced port (Force, fights, weapons, spawns, movement) does to the bots: captures, runner
   falls, weapon deaths, carrier kills. Replaces the partial `be-combat-20261008-a` campaign (stopped after 4 stock
   batches, 2026-10-08).
   **Done 2026-10-09** ([record](movement/records/2026-10-09-competitive-defaults-vs-stock.md)): captures undecided on
   both maps; runners' weapon deaths +1.73 +- 1.09 (Dantooine), +2.65 +- 0.97 (Kejim, past the cut-off); base-defender
   carrier kills about +2 se on each map. **Accepted by the owner, no further tests.**
2. **The three new layers separately**, if item 1 moves anything beyond noise: arms with only the movement settings at
   stock (`g_beHeadFriction` ... `g_fix5AmmoSniping`), only the fight/weapon/spawn settings at stock
   (`g_locationBasedDamage 1` ... `g_beSaberOneshotsExplosives 0`), and only the Force set at stock. Expected movers:
   improved homing (defender alt rockets home more), the disarm-firing fix (a runner's pull stops a defender's held
   fire), knocked-down pushes (runner pitted more), the force-land lock removal (drop routes).
   **Cancelled by the owner 2026-10-09 (not needed)**; the arms that had finished are kept and noted in the record.
3. **Movement bench on Dantooine and the level gates** on the new defaults (`movement_bench.py` with the PLAN.md
   "Cheap comparisons" recipe): the Kejim bench is green (24/24, zero parity mismatches, 2026-10-08); the level-1/2
   gates have not been re-run since the competitive defaults.
   **Done 2026-10-09** ([record](movement/records/2026-10-09-night-items-3-5.md)): bench 18/18 at classes 50 and 150; row 3 class 150 now met; level-2 speed row **fails** (14.8-16.1 s against 13 s; 9.1-11.0 s on 2026-10-05), row 2 tail 18,250 and one row 4 case miss.
4. **Map prep and tapes after the movement switches.** Prepared seeds and explored tapes were built under stock
   force-land lock and wall-grab rules. Rebuild one map's seed with the new defaults and compare edge counts and
   bench times against the old seed; only rebuild all maps if it finds new routes.
   **Done 2026-10-09** (same record): old seed still loads; new seed 597 edges against 537 (+58 drops, +2 jumps), class 100 bench median 14,525 ms for old, new and no seed. Other maps not rebuilt.

5. **Renderer bench** ([renderer-bench.md](renderer-bench.md)): `python tools/renderer_bench.py build`, then
   `run --demo kejim-runner`: six candidates (our vanilla and rend2, SomaZ rend2, TaystJK rend2 and Vulkan, JKSunny's
   Vulkan PBR) x default/max x 3 reps plus screenshots. Builds about an hour; runs depend on the fps (a 10-minute demo
   at a few hundred fps is a minute or two each). Not yet tried: the first build or run may need fixes.
   **Done 2026-10-09** (same record): four candidates in the default set, fps 520-760; runner fixed to find forks' console logs.

6. **Level-1 gate rows 2 and 4 on master** (added 2026-10-13): `python .codex-research/gate-rows-20261013-a/run.py`
   from the repo root (class 150 stand runs, library and control, and the diag arm; red_out_L1_stand library on/off x3;
   about 15 worlds on `master-build-20261013-a`). If row 2's tail (18,250 against 18,000) or row 4's red_out_L1_stand
   (19,325 against 18,250) still miss, go to item 7.
   **Done 2026-10-14** ([record](movement/records/2026-10-14-overnight-items-6-7.md), fresh build
   `night-build-20261014-a`: the 10-13 build was refused as stale): row 4 now met (library median 15,925 against control
   16,350); row 2 still 18,250 against 18,000 in every world, library and control alike. Item 7 started.
7. **Re-explore the level-1 library on the current defaults** (HANDOFF Open 2): copy
   `.codex-research/defer-chain-20261005-a.sh` with the current build and new explorer names (the `explorer-defer-*`
   cells are not reusable), confirm with its own stand worlds. A few hours, unattended.
   **Done 2026-10-14** (same record): eight cells in an hour; level-1 stands 13,375-14,450, level-2 8,200-9,675;
   Kejim floor 36/36. Item 6's rows re-run with it: row 2 14,450 (met), row 4 met. Owner switched production to
   the new library (option A).
8. **Frame-speed timing confirmation**, only once a frame-speed change looks good by the work meter: pinned two-core
   timing worlds (`defense_laps.py --cpus spec`), one at a time, idle machine (owner off the PC), about 30 minutes.

9. **Every movement/bot check on HEAD with the production library** (owner, 2026-10-14). One hidden chain,
   `.codex-research/night-20261014-c.sh` (launch with Git Bash `-l`, Start-Process hidden; log
   `night-20261014-c.log`): fresh build `night-build-20261014-c`; the full level-1/level-2 gate `gate-20261014-c`
   (copy of gate-20261009-a: floors ctf4 and dantooine_v3, rows 1-4, level-2 rows, row 5 cost, source verify before
   and after, `analyse.py`) with `reexplore-chain-20261014-b`; then the 4v4 laps check `laps-check-20261014-c`
   (Dantooine 1-8, Kejim 1-8, settings of master-check-20261013-a; score against it with `tools/laps_compare.py`;
   watch Kejim captures). About an hour or two. Do not edit sources between the build and the end of the gate.

## Client checks (need a person or a scripted listen server, short)

Owner 2026-10-13: agents do these (stats judged from a demo), not the owner.
**Done 2026-10-14** ([record](movement/records/2026-10-14-client-checks.md)): no prediction misses in any scenario; drain
icon and spectator timers shown; stats tables, `wpn all`, `acc` and team-switch rows work. Wall grab tested from a
jumpless fall (the ledge-drop state), not a scripted walk-off.


- Prediction: play a minute on a listen server with `cg_showmiss 1`: no prediction errors on landing from a high
  force jump, rolling, wall grabs after a ledge drop, standing on a bot's head.
- The viewed player's drain-debuff icon (be drained by someone) and force timers while spectating a player.
- Stats with real play: a CTF match with bots to intermission (tables print automatically), `/stats wpn all` and
  `/stats acc`, and a player who switches team keeps their rows.
