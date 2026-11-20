# Use two layers of movement control

This implements item 3 from the project discussion: map navigation chooses a
useful destination; a small controller chooses commands from current position
and momentum; a forecast must execute that same controller. It is an
architecture deliverable, separate from M1 map coverage, M2 recovery and M3 cost.

Status: **complete for the contract below**. Accepted audit:
`.codex-research/strafe-layers-evaluation-20260904/summary.json`.

## Contract and frozen completion checks

1. Route guidance and navmesh waypoint guidance both issue motion intent to
   one pure local controller. That controller owns command axes and submitted
   yaw, has no map queries, and keeps no global per-bot state.
2. A movement-only runtime mode must use these two layers without invoking
   the legacy speculative jump planner on unsupported route entries. Unverified
   off-mesh maneuvers request braking; unsupported coverage is reported.
3. Native ordinary-movement forecasts independently regenerate controller
   commands at server cadence from their own evolving state. Exercise jump /
   landing / arrival and waypoint change / braking, including a changed initial
   velocity and cadence. Require exact commands, controller state and the
   existing movement-state contract through the full forecast. Keep the
   forecast-only bias negative control, which must fail. Force/world events
   outside the fixture contract remain explicitly unsupported.
4. Keep the existing 12 red route traces exact, and preserve blue's native
   eight-phase speed qualification. Verify a native waypoint dispatch and a
   route-to-waypoint handoff without speculative planner dispatch.
5. Clean build and existing tests pass; record actual arrival outcomes and
   unchanged limitations. This does not declare all 26 starts qualified.

Implementation: `bot2_movecontrol.c/.h` owns local command selection;
`bot2_strafe.c` produces route-derived intent; `ai_bot2_localnav.c` produces
navmesh waypoint intent. `bot_moveLayers=1` selects the new movement-only
fallback. Empty/unsupported paths brake instead of launching unverified jumps.
The legacy mode remains available as a comparison, outside this new control
contract. No installed server configuration is changed.

## Development failures retained

The initial validation is preserved in
`.codex-research/strafe-layers-validation-20260904`. Positive forecasts matched
commands and movement exactly, but 30 Hz stopped 28.3 units short of the goal,
outside the unchanged 24-unit acceptance radius. Waypoint arrival gain was
increased to account for the minimum useful discrete movement command.
Walking-only tests also exposed an old fixture assumption that every test
must jump; walking now has an explicit arrival gate while jump cases still
require takeoff and landing. The forecast-bias control correctly failed at
its first divergent command; its expected early stop must not be mistaken for
a one-step Pmove failure.

An initial route-off handoff at 2.500 s died at 7.325 s. The airborne navmesh
query had no start polygon, and braking discarded the route's supported
landing. The new navigation layer now retains a recent route landing hint
until actual touchdown, then resumes waypoint queries. Ground-only fallback
checks nearby support within step/drop height and stopping distance. This
is a concrete handoff correction, not a general M2 recovery claim.

The local-controller diagnostic also found four differing commands between
fast and exhaustive enumeration in 4,096 synthetic states. At least the first
case is a large-error float scoring tie whose yaw penalty rounds away. The
two enumerations are not universally command-equivalent, and we make no such
claim. Production and forecast both use the same fast controller; the actual
contract is deterministic commands from equal input/state and native forecast
agreement. The independent 4,096-state check passes purity, repeatability,
axis limits, packed angles, turn-rate limits, neutral arrival and speed caps.
The earlier failing exhaustive-equivalence diagnostic is retained.

## Accepted results

Clean build: `.codex-research/strafe-build-layers-validated-20260904`.
The final audit checks 28 native trials and 587 source/staged-artifact hashes,
using one game module across the accepted batches. The expected negative
control and the two runtime timeouts are preserved, not counted as pickups.

| Local-controller case | Forecast frames | Command/movement differences | Final distance | Final speed |
|---|---:|---:|---:|---:|
| Jump, landing, arrival; 40 Hz | 160 | 0 | 11.109 | 0 |
| Waypoint turn and arrival; 40 Hz | 160 | 0 | 12.495 | 0 |
| Same turn with initial velocity (50,-50); 40 Hz | 160 | 0 | 13.238 | 0 |
| Jump, landing, arrival; 30 Hz | 122 | 0 | 9.968 | 0 |

All **602** forecast frames were generated before live execution, with no
intermediate state resets. The comparison covers commands, controller state,
contacts and the existing ordinary-movement state contract. Force-regeneration
debounce and server event-sequence bookkeeping remain separately reported;
full server/player-state prediction and dynamic force/world events are not
claimed. The forecast-only bias changes the predicted command and fails the
test, while the native one-step Pmove comparisons remain exact.

Native waypoint dispatch moved 813.225 horizontal units from the red stand,
then braked at unsupported traversal. It timed out alive at 20 s. Disabling
the route at 2.500 s now retains its landing hint, lands at **3.025 s**, and
remains alive and grounded at 20 s; it does not pick up the flag. Neither
runtime case invokes the legacy jump planner. This establishes the layer
handoff and conservative stop behavior, not successful general recovery.

With the new movement-only mode enabled, all 12 original red regression cases
retain **3,248 exact movement/command checkpoints** and the 6.700 s nominal
pickup. Blue respawn 3 retains all **2,148 checkpoints** across eight runtime
phases, with 6.800 s nominal/worst versus a 6.600 s planned reference. Existing
Python tests (42), the compiled catalog check and the 4,096-state compiled
controller contract check pass.

Accepted batches and checks:

- `.codex-research/strafe-layers-validation-v2-20260904`
- `.codex-research/strafe-layers-red-regression-20260904`
- `.codex-research/strafe-layers-blue-qualification-20260904`
- `.codex-research/strafe-movecontrol-contract-check-20260904`
- `.codex-research/strafe-layers-catalog-final-check-20260904`

## Remaining project work

The architecture is ready to accept more useful navigation/maneuver guidance.
Only two spawn poses are speed-qualified. Both fast flagstand routes, broader
recovery and multi-bot performance remain open. The new mode deliberately does
not replace missing maneuvers with the old simulator's divergent command
policy. Future short prediction must use the shared controller and explicitly
validate any extension beyond the current static ordinary-movement contract.
