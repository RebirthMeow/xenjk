# First two-maneuver comparison

The proposed continuation scoring now has a small, live-tested distinguishing
case. On development geometry with two gaps and a turn, it selects a first
maneuver that reaches the prescribed second exit. A scorer using only the first
landing selects a more centered arrival but stops partly over the second edge.
This supports continuing the investigation; it does not establish a generally
reliable strafe controller or an intermediate-human rating.

XenJK remains the target. This is a seed-free experiment, independent of
production Bot2. Human demo fitting is still gated on interaction screening;
Two community players are preferred references, with the owner's demos acceptable as the main corpus.

## Fixed experiment

The local JA q3map2 compiler builds a sealed collision-only research map and a
flat-floor control. The runner stages each BSP in a fresh private homepath;
neither installation assets nor the external map project are modified. The
caulk brushes are for headless collision tests, not a finished visible level.

| Region | World X | World Y | Top Z |
|---|---|---|---|
| Start | -192 to 160 | -96 to 96 | 0 |
| First platform | 304 to 400 | -160 to 64 | 0 |
| Second platform | 288 to 448 | -496 to -320 | 0 |

The nominal player center starts at `(0, 0, 24.125)`, facing +X. The first gap
is 144 units; the second is 160 units along -Y. A lower floor catches falls.
The flat control replaces the gaps with continuous floor while retaining the
same target regions and controller. Both maps contain only static collision and
spawn points; no opponents, missiles, powerups or trigger hazards participate.

The eight candidates differ only in when first-jump steering turns toward the
second platform: 2450, 2350, 2250, 2150, 2050, 1950, 1850 and 1750 ms after
setup, in that ID order. They share the neutral preamble, run-up, jump tap at
1600 ms, 360-degree/s yaw cap, first-landing feedback, second takeoff near the
south edge, landing feedback and braking. All commands use the same generator
in prediction and execution. A second jump requires actual first landing and
released jump input; no player state is reset between maneuvers.

Each search forecasts all eight candidates from the same full captured root,
with 160 commands per candidate: **1,280 Pmove calls over a four-second horizon**.
This replaces the earlier provisional 32 candidates x 40 commands: a one-second
horizon would not span the task. Both scorers receive all eight complete
forecasts and identical command/candidate limits.

- Local rank: first valid landing time, squared distance from that landing to
  first-platform center `(352, -48)`, then candidate ID.
- Continuation rank: successful second exit first, earliest completion time,
  then the exact local rank as a tie-break/fallback.

A valid first landing must put the player center inside the first platform's
15-unit inset. A successful second exit requires the second jump's landed phase,
ground contact at the platform height, center X in `[303, 433]` and Y in
`[-481, -335]`, and horizontal speed <=80 units/s for four consecutive 25 ms
steps. It must remain qualified through the episode end. These regions account
for the 30-unit-wide player collision hull. Touching a platform with part of
the hull is deliberately insufficient.

The native scorer selects before the first live move. Every candidate's full
forecast is preserved in a separate ABI-tagged capture. Python independently
recomputes scores and selection, verifies identical root hashes within each
search, and checks that the two scorers' candidate outcomes agree. Paired runs
use separate fresh processes and have different absolute server clocks; they
are not claimed to share byte-identical complete roots across processes.

The selected candidate receives an additional 160-call forecast for the existing
live parity harness. That verification rebuild and live cloned/rolling probes
are outside the 1,280-call selection allowance, and are not hidden as free
production work. World/clock restoration and the same restricted movement-state
contract from the [forecast check](STRAFE_MOVEMENT_FORECAST_CHECK_2026-09-04.md)
remain required. Full PS parity still differs in the two reported server
bookkeeping fields; force-dependent maneuvers remain outside the contract.

## Development result and controller correction

The initial four-run comparison is preserved in
`.codex-research/strafe-route-matrix-20260904`. Both scorers failed the second-exit
criterion on both maps. All eight candidate forecasts failed, and the selected
live paths matched prediction. Position-only second-jump aiming carried too
much sideways momentum into the landing. This result was not a scorer win.

The shared second-jump controller was then changed to steer against projected
landing error: estimate time to the target-height plane from vertical velocity
and gravity, subtract carried horizontal velocity times that duration from the
target displacement, and steer toward the residual. Native Pmove still decides
whether the maneuver works. Geometry, candidate turn times and scoring criteria
were unchanged. The revised controller is development work, not a locked,
held-out evaluation selected independently of these traces.

The revised nominal comparison is in
`.codex-research/strafe-route-matrix-momentum-20260904`. Both first landings occur
at 2250 ms, so the nominal local choice is determined by centering, not a faster
first arrival. Local chooses candidate 0; continuation chooses candidate 5.

| Turn-map start Y | Local selection / second exit | Continuation selection / second exit |
|---|---|---|
| 0 | ID 0 / fails, stops partly over west edge | ID 5 / passes at 3600 ms |
| -16 | ID 4 / passes at 3525 ms | ID 4 / passes at 3525 ms |
| +16 | ID 0 / fails, falls off | ID 7 / passes at 3675 ms |

The signed starting-position probes are preserved in
`.codex-research/strafe-route-lateral-20260904`; candidates, controller and
geometry remained unchanged. These three hand-specified starts are not a
sampled success probability, independent repetitions, or evidence of robust
performance over an entry region. There is only one successful candidate at
the nominal and +16 starts, versus seven at -16.

For the nominal case, local stops at `(280.615, -368.464, 24.125)`; continuation
stops at `(303.386, -368.008, 24.125)`, both with zero velocity. The second
platform's west edge is X=288. The continuation hull therefore fits inside that
edge by only **0.386 units**. This is a narrow nominal success, not a satisfactory
robustness margin. The local bot touches both platforms; its nominal failure
is incomplete support at the exit, not failure to make second contact.

The flat-floor control yields the same nominal choices and exit scores. It
shows that these controllers' endpoints differ even without gaps; it does not
add an independent successful geometry or isolate an obstacle-avoidance effect.

## Verification, cost and artifacts

The revised nominal/control and lateral matrices contain eight live episodes,
all with verified selection and **1,280/1,280 selected-forecast command,
controller, movement and contact checkpoints matching live execution**. The
rejected routes remain valid simulator observations; runner `complete` means
the parity episode completed, while `route.actual.complete` records traversal
success separately.

All 64 candidate forecasts in those episodes have matching native/Python scores;
each paired scorer receives equivalent candidate outcomes. The final independent
audit is `.codex-research/strafe-route-audit-final-20260904/audit.json`.
`nominal_paths.png` in that directory plots captured live paths and final hulls.
Build/run plans retain source, native binary and map hashes. The staged binaries
in each episode preserve both controller revisions even though the build
directory was incrementally updated.

The revised search takes 44.143 to 76.741 ms across eight single searches,
including diagnostic copies, world hashing and candidate capture writing.
It has no wall-clock cutoff and **does not meet the provisional 2 ms target**.
The entire fixture performs much more work than selection alone. This is not a
production performance claim or a multi-bot throughput measurement.

A fresh source build succeeded; the controller correction passed an incremental
native build. All 32 Python tests pass. The original one-jump Kejim fixture also
passes a separate 30 Hz/fixed-integration regression with 122 matched commands
in `.codex-research/strafe-route-regression-20260904`. No native crash occurred
in the 12 route launches or this regression; no test server remains running.

The first map-generation attempt emitted a malformed-entity warning and is
preserved in `strafe-route-geometry-20260904`, excluded from all live results.
The corrected, sealed BSPs and pre-run geometry plan are in
`strafe-route-geometry-v2-20260904`. The generator now rejects that warning and
leaks instead of trusting the compiler's successful exit code alone.

```powershell
python -B tools/strafe_route_map.py --compiler '<path to q3map2.exe>' --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/route-geometry-NEW
python -B tools/strafe_fixture_build.py --out .codex-research/route-build-NEW
python -B tools/strafe_route_matrix.py --build .codex-research/route-build-NEW --assets 'game_directory/Jedi Academy/GameData' --geometry .codex-research/route-geometry-NEW --out .codex-research/route-matrix-NEW
python -B tools/strafe_route_matrix.py --build .codex-research/route-build-NEW --assets 'game_directory/Jedi Academy/GameData' --geometry .codex-research/route-geometry-NEW --out .codex-research/route-probes-NEW --lateral-probes --port 29520
```

Next: improve exit margin and test actual takeoff-state disturbances, then lock
the controller and evaluate a declared disturbance distribution. These starts
do not replace tests of carried launch velocity, timing or interrupted movement.
Automatic route discovery, held-out map transfer, production integration,
incumbent comparison, force powers and human calibration remain untested.
