# R6 execution record, 2026-09-08

Executes the R6a–R6d packets of the [K07 review](STRAFE_MOVEMENT_K07_REVIEW_2026-09-08.md).
Every number below is read from retained evidence named in the section; the
R6d chain results are appended as they complete. Nothing here installs or
promotes a build.

## R6a — the M2 difference is deterministic and has three named sources

Read-only pairing of all 780 frozen cases (`.codex-research/strafe-r6a-m2-pairing-20260908-v1/`,
`pairing.py`, `pairing.csv`, `summary.json`; input hashes recorded):

| Category | Cases |
|---|---|
| pass on both builds | 734 |
| shared failure | 17 |
| new failure (K07 fails, reference passed) | 16 |
| recovered (reference failed, K07 passed) | 5 |
| invalid on K07 (impulse never applied) | 8 |

The review's "24 new" counted the eight invalid cases as new failures; the
audited split is 16 new plus 8 invalid. Both builds ran exactly one module
each (`555b6264…` K07, `6589e82d…` reference). The K07 build is faster on
14 of 26 starts by median first-trip time and slower on 2; blue_respawn_5 is
325 ms faster in the median, which is why its eight approach cases reached
the flag at 9,775 ms, 25 ms before the frozen 9,800 ms impulse.

Paired reproduction on the retained binaries
(`strafe-r6a-m2-repro-{k07,ref}-20260908-v1/`, 12 cases each, ports
33000/33100): **every case reproduced its original outcome, elapsed time and
frame count on both builds.** The difference is build behaviour, not variance.

Sources, established by trace and source diff against the reference case's
retained source snapshot:

1. **Shared controller semantics changed.** `bot2_strafe.c` is byte-identical
   but calls `Bot2_MoveControl`, and `bot2_movecontrol.c` gained the
   travel-facing limit and the analog magnitude cap between the reference
   build (09-05) and K07. First divergent frame for blue_respawn_5 is 4,825 ms:
   identical state, different view yaw and key choice. This makes most starts
   faster and costs three cases where the kernel cannot rejoin the guide
   after a disturbance: both `skip_first_jump` deaths (envelope release at
   index 8 after resume) and `red_respawn_4__impulse_takeoff_y_-50`.
2. **The jump-reserve release.** `[STRAFEROUTE] jump_resource` fired in 15
   K07 cases at Force 5–8 against a required 10 and released route ownership;
   12 of the 16 new failures are those cases. The reference build issued the
   same jumps and completed all of them. Zero such events on the reference.
3. **Setup jitter, not a build difference.** 71 of 780 cases start on a
   different settle frame between the two builds (sub-unit position, small
   velocity); the variants appear on both builds for the same start. One case
   (`red_respawn_1__offset_y_+1`) started above the follower's 80 unit/s
   acquisition limit on K07, never acquired a route, and stalled in localnav.

Correction applied (`codemp/game/ai_bot2_route.c`): the reserve is observed
(`jump_reserve_low`, once per jump) and no longer releases. Verified on the
diagnostic builds: all 12 reserve cases pass; the three controller-rejoin
cases still fail; the eight blue_respawn_5 cases still finish at 9,775 ms and
need the re-derived suite (R6a.4, below). No change to the controller: the
travel-facing behaviour is a plan requirement, the flight artifact was baked
against it, and the residual cost is within the M2 gate as projected
(760/780, mid-flight 203/208, skipped-jump 24/26).

## R6b/R6c — both stalls are one localnav behaviour

Telemetry trace of every distant stall in both K07 continuity arms (read-only):

- All nine `stand_entry_resource` stalls: runner carrying at the enemy stand,
  Speed active, Force 24–26 pinned, and the BASE defender pressed against the
  runner at **30 units** for most of the window. Route entry owned two samples;
  the zero commands came from localnav, supported and recovery. Every one ends
  with the runner killed. `StandEntry` was not holding ownership; the sticky
  `release` column mislabelled the wait.
- The red 5x r2 "unexplained stop" (802,-1275,1760): no enemy within 1,600
  units, Force 100, no Speed. Localnav held a valid navmesh corner 2,700 units
  away almost due north with a wall 16 units away; corner recovery inched the
  runner 64 units east every 2.5 s.

Both reproduce from a clean corner pose on the K07 build (`strafe-r6c-stop-repro-20260908-v1/`,
`strafe-r6b-standentry-repro-20260908-v1/`). Traced refusal reasons on the
diagnostic build: `direct_blocked` at the first corridor sample (lower stop),
`floor_startsolid` (body proxy). A static body 30 units away embeds the runner
in native physics and is not the live case; at 36 units it is.

Correction applied (`codemp/game/ai_bot2_localnav.c`), general and
coordinate-free:

- `SupportedWalk` reports its refusal reason; `walk_refused`, `walk_stuck`,
  `waypoint_stale` and `waypoint_none` traces under `bot_supportedTrace`.
- A refused straight corridor no longer yields a zero command: first the
  nearest admissible navmesh path point (24–96 units), then a bounded fan of
  headings around the waypoint direction on the same native corridor check,
  side hysteresis, never opening the waypoint distance by more than 8 units.
- Commanded grounded movement with no displacement for 600 ms names itself
  (`walk_stuck`) and excludes that heading for one second.
- A momentarily failed waypoint query keeps the last admitted waypoint for up
  to one second instead of braking.
- Recovery's scored scan is unchanged and remains the backstop.

Declared corner matrix on the third diagnostic build
(`strafe-r6b-corner-matrix-20260908-v1/diag3/`, `run_matrix.sh`,
`audit-wave1.json`), 30 game seconds each, no enemies unless a body proxy:

| Case | Result |
|---|---|
| red carrying at blue stand, Speed active, Force 24 / 5 / 100, Speed expired | capture, no confinement |
| same with body proxy 36 units in the travel direction | capture, no confinement |
| blue carrying at red stand, Speed active, Force 24, with and without proxy | capture, no confinement |
| red lower level (802,-1275) Force 100 / Force 24 with Speed | 6,000+ units in 30 s, no confinement |
| blue mirror lower level | 6,000+ units, no confinement |

Pure and mocked checks on the changed tree (`strafe-r6-checks-20260908-v1/`):
adapter 82, guard 17, movecontrol 200,000 bitwise, flight check, all pass.
Python suites (run from the repo root with `PYTHONPATH=tools`): all pass.

## R6d — qualification chain

`.codex-research/strafe-r6d-chain-20260908/chain.sh`, one fresh build
`strafe-build-r6-freeze-20260908`, stages sequential, four worlds at most, no
timing arms (owner decision of 2026-09-08 stands). Stages: M0 runtime matrix
against the frozen 09-04 step-3 reference; M1 26-start qualification; M1
qualified summary derived by `m1_qualified.py` (self-tested against the 09-05
summary field for field); supported matrix 30, adapter armed; lifecycle phase
2; full-route phases 1/4/7 (regression, not holdout); M2 on the frozen v2
suite with the 09-05 audit inputs (comparable to K07's 739); M2 v3 suite
frozen from the new M1 clean traces (R6a.4: same offsets, impulses and skip
durations, event times re-derived) with its own audit; defended continuity
candidate, baseline and a corners-2/flight-off ablation arm, 12 runs each.

### Freeze 1 (`strafe-build-r6-freeze-20260908`), aborted at M2 v3

Retained under `strafe-r6-*-20260908` and `strafe-r6d-chain-20260908/`:

| Stage | Result |
|---|---|
| M0 runtime matrix | 12/12 pickups with route ownership, nominal 6,700 ms, all within 1.2; exact-trace false as since 09-05 |
| M1 | passed, 26/26, nominal max 1.019, worst phase 1.111 |
| Supported, adapter armed | passed, 30/30 |
| Lifecycle phase 2 | 10 pass, 2 boundary, 2 control; **4 red cases `failed_input`**: the runner's rcon reply carrying the declaration line timed out under four simultaneous starts, while the fixture fired identically to K07 (same levels, 29 blocker contacts). A reply-capture flake, rerun in chain b |
| Full-route 1/4/7 | pass, identical to K07 (10.27% red, 11.86% blue, 85 exits, 2 censored) |
| M2 frozen v2 | **744/780**, 9 recovered against K07, **4 regressed**: `temporary_body` on red_initial_0/3 and blue_initial_1/2 |

The regression: the 500 ms body fixture blocks the route start; the new
continuation immediately detoured 100 units around it, the route resumed at
index 2 from that displaced pose and the runner fell into the pit at
~3,100 ms. K07 waited in place and re-acquired at index 0. Correction: a
refused corridor is waited out for 750 ms before any alternative
(`refusedSince`). Verified on the fourth diagnostic build: all four body cases
pass at their K07 times, the reserve cases pass, and the corner matrix is
ten for ten with no confinement. The chain was stopped, and restarted as
freeze 2 (`strafe-build-r6-freeze-20260908b`, `strafe-r6d-chain-20260908b/`).

### Freeze 2 results (`strafe-build-r6-freeze-20260908b`, `strafe-r6-*-20260908b`)

| Stage | Result |
|---|---|
| M0 runtime matrix | 12/12 pickups with ownership, nominal 6,700 ms, max 6,750 ms; exact-trace false as since 09-05 |
| M1 | passed, 26/26, nominal max 1.019, worst phase 1.111; qualified summary derived |
| Supported, adapter armed | passed, 30/30 |
| Lifecycle phase 2 | passed: 14 interventions, 2 boundary, 2 controls; all declarations captured |
| Full-route 1/4/7 | pass, 10.27% red / 11.86% blue, 85 exits, 2 censored (identical to K07) |
| M2 frozen v2 suite | **746/780 (95.64%)**, every family floor met (blocker 26/26, approach 200, mid-flight 199, takeoff 195, offset 102/104, skipped 24/26). Audit: overall and family targets pass; `all_cases_valid` fails on the same eight preempted blue_respawn_5 impulses; `regression_build_compatible`/`m0_retained` are the provenance checks against the 09-05 build. Against K07: 9 recovered, 2 regressed (`blue_respawn_0__offset_y_-1`, `red_respawn_0__offset_y_+1`, both setup-jitter starts above the follower's 80 unit/s acquisition limit that never acquired a route; follow-up item, within the gate) |
| M2 v3 suite (re-derived from the freeze-2 M1 traces, same offsets/impulses/skip durations) | **768/780 (98.46%)**, every family floor met, **every audit check passes** including M0/M1 retention and build compatibility (same build). 12 failures: 9 takeoff and 2 mid-flight 150 unit/s impulses plus one approach impulse, all deaths but one timeout; the takeoff set is the reference build's own shared-failure set |

Both M2 suites and both results are retained; the v2 suite remains the
historical benchmark on the 09-05 references, the v3 suite is the same
protocol with event times valid for this controller.

#### Defended continuity, three arms on freeze 2 (12 runs each, all valid)

| Cell | Baseline idle / walking / stalls | Corners 2, flight off | Candidate (corners 2 + adapter) |
|---|---|---|---|
| red 1x | 6.86 / 38.58 / 0 | 6.33 / 35.62 / 0 | **7.50** / 31.48 / 0 |
| red 5x | 8.90 / 33.02 / 0 | 6.75 / 39.14 / 1 | 5.81 / 32.43 / 0 |
| blue 1x | 8.84 / 33.61 / 1 | 7.58 / 38.93 / 2 | 6.55 / 33.59 / 1 |
| blue 5x | 7.08 / 33.10 / 0 | 7.11 / 36.46 / 2 | 6.86 / 31.64 / 1 |
| pooled | 7.97 / 34.67 / 1 | 6.94 / 37.54 / 5 | 6.68 / 32.28 / 2 |

Percentages are the audit's travel idle and travel walking (classified goal
waits excluded from both numerator and denominator).

- **Rule 3, walking:** passes in every cell for the candidate; K07's red 5x
  regression (+5.6 points) is gone. The ablation attributes the K07 walking
  increase to corners mode under the supported owner (every cell walks more
  with corners alone), which the flight adapter more than offsets.
- **Rule 3, idle:** three cells improve; red 1x is 0.65 points worse. On the
  alive basis the candidate's red 1x idle is lower (7.56% vs 8.47%); the
  baseline's travel idle is lower only because its two identical red 1x runs
  each contain a 15.25 s classified goal wait that the metric excludes. The
  gate is as written, so this cell fails it.
- **Rule 2, stalls:** candidate 2 versus baseline 1 (K07: 4 versus 6). The
  candidate's are a 5.75 s supported-owner wait at (3174,1951,1825) with no
  enemy within 3,400 units (blue 1x r3, 217,450 ms) and a 5.0 s defender-at-30-
  units carry at the blue base (blue 5x r1, 508,075 ms; K07's equivalents
  lasted 5–10.5 s and every one ended in death). Both are named below.
- **Repetition validity:** in freeze 2 the red 1x r1 and r3 runs are
  byte-identical in every arm, so that cell has two independent samples. K07's
  arms did not duplicate. The cause is not established; the world seeds for
  that cell should be checked before the next collection.
- Captures/deaths per run are in `reps.py` output; they are context, not gates.

## Verdict on the R6 revision (freeze 2)

| Gate | Result |
|---|---|
| M0 behaviour | retained (12/12, ownership, ratios) |
| M1 | pass |
| M2 frozen v2 | 746/780, all family floors; overall target met; 8 preempted impulses invalid on this controller |
| M2 v3 (same protocol, re-derived events) | **pass, 768/780, every audit check** |
| Supported 30, lifecycle 18, full-route 12 | pass |
| Defended continuity, walking | pass in every cell (K07: three cells failed) |
| Defended continuity, idle | three cells pass; red 1x fails by 0.65 points on the travel metric |
| Defended continuity, stalls | two (K07: four); one supported-owner wait unexplained, one defender-body carry at 5.0 s |
| Timing | not collected; deferred by the owner decision, still pending |
| Delivery | nothing installed; opt-in configuration unchanged |

## What remains, in order

1. **Supported-owner wait** (blue 1x r3 at 217,450 ms): a supported prefix
   was published at 216,650 ms, the runner stopped at 217,700 ms under the
   supported owner for 5.75 s, no enemy within 3,400 units. The clean-pose
   reproduction (`strafe-r6c-supported-stall-repro-20260908-v1/`) does not
   stall, so the wait depends on supported's preparation state, not the pose.
   Next: one defended blue 1x collection with `--supported-trace` to name the
   preparation-owned wait, then the same explicit reason/expiry/fallback
   contract localnav now has.
2. **Defender pressed at 30 units while carrying**: 5.0 s now (K07 5–10.5 s,
   all fatal). The static-body matrix passes; the live defender moves with
   the runner. Needs a moving-body fixture or the traced defended collection
   above.
3. **Red 1x idle** is a classification artefact of two identical baseline
   runs; fix the duplicated repetition seed before re-collecting.
4. **Controller-rejoin cases** (two skipped-jump deaths, one takeoff impulse)
   are the travel-facing controller's cost; within the M2 gate, unchanged.
5. **Route acquisition above 80 unit/s** after a jittered setup: two v2
   offset cases; a dispatch item, within the gate.
6. **Timing** as deferred; bind the payload at map load in K08.

The M1 fixture README, the K07 protocol's reproduction section and
`chain.sh` give the exact commands; every arm is bound to
`strafe-build-r6-freeze-20260908b` (1,102 sources).
