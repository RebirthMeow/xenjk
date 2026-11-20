# M1: native spawn coverage baseline

Active requirements: `STRAFE_MOVEMENT_PROJECT_PLAN.md`. Current status:
`STRAFE_MOVEMENT_PROGRESS.md`.

## Experiment COV-01

Hypothesis: the current controller's coverage gap can be measured independently
of aiming, and separated into fast guided traversal and incumbent fallback.
This is a census before tuning additional routes, not an acceptance claim.

The Kejim BSP contains 24 distinct team start poses: six initial and six
respawn entities for each side. `tools/strafe_spawn_coverage.py` freezes their
class/index, authored origin and yaw, and the source BSP hash. Each trial then
matches that pose against the **native game's entity list and map checksum**.
The indexed class order is verified through native metadata rather than
assuming BSP entity numbers equal runtime entity numbers.

The isolated fixture sets up the player at the selected native entity origin
and yaw, allows native settling, then executes ordinary Bot2 movement dispatch
toward the actual enemy flag. This is explicit research setup, not a test of
random spawn selection or the complete Bot2 combat/tactics loop. There are no
teleports after setup, no aiming handoff, no opponents, no route retuning and
no disturbance in this initial census.

Settings: 40 Hz; fixed 8 ms floating movement; g_speed 250; gravity 800;
explicit XenJK force regen 200; saber; rank-3 Speed/Levitation with initial
100 force; capped prediction enabled; separate per-frame RNG streams with
decision seed 1. Aim-frame mode is 0. Each fresh server has a 20-second native
deadline and orderly shutdown. The original red route asset stays unchanged;
other starts use fallback when that route's normal entry/team gates reject it.

Acceptance for **the experiment** is a verified native observation for every
inventoried pose, including retained deaths/timeouts. A nominal pickup alone
does not meet M1: same-endpoint planned references, phase trials and speed
targets are separate requirements. Initial authored spawn height differs from
the settled player origin; captures preserve the actual command-time state.

Build: `.codex-research/strafe-build-coverage-20260904`.
Inventory: `.codex-research/strafe-spawn-inventory-20260904`.
Native census: `.codex-research/strafe-coverage-baseline-20260904`.

Result: all 24 selected poses matched the native entity/checksum metadata and
produced valid captures. **10 pickups, 10 timeouts and 4 deaths**. Only red
respawn 2 acquired the existing guided route; it picked up in 6.775 s from its
native yaw 90. The prepared-heading M0 reference remains separate. All other
starts relied on incumbent fallback. `audit.json` checks the 24 manifests,
module/source hashes and native-pose matches. Aiming was disabled throughout.

```powershell
python -B tools/strafe_spawn_coverage.py --build .codex-research/strafe-build-coverage-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/coverage-NEW --port 30100
python -B tools/strafe_progress.py --inventory .codex-research/coverage-NEW/inventory.json --coverage .codex-research/coverage-NEW/summary.json --out .codex-research/progress-NEW --publish
```

## Experiment COV-02: blue-side guide qualification

The smallest extension transports the proven guide to its opposing spawn
layout. All twelve red/blue spawn positions have a half-turn counterpart about
XY (1312,2080). This supplies a route hypothesis; it does not prove collision
symmetry. The actual flags are slightly asymmetric, so success is still native
pickup of the current goal entity. No movement-controller C code was changed.

`tools/strafe_route_halfturn.py` creates separately named candidate guidance
and planned commands. `tools/strafe_spawn_qualify.py` tests a planned reference
and all eight runtime command phases from the same native pose and initial
setup, using the project plan's fixed 110% nominal / 120% worst-phase limits.
The native fixture now supports selected spawns for planned replay as well as
runtime dispatch; guide team and test bot team are explicit independent inputs.
Generated route files are included in the manifest hashes.

Blue respawn 3 timed out at 20 seconds in COV-01. With the candidate guide:

| Trial | Pickup time |
|---|---:|
| Planned native reference | 6.600 s |
| Nominal (phase 0) | 6.800 s |
| Phase 1 | 6.675 s |
| Phase 2 | 6.725 s |
| Phase 3 | 6.700 s |
| Phase 4 | 6.675 s |
| Phase 5 | 6.725 s |
| Phase 6 | 6.750 s |
| Phase 7 | 6.650 s |

All nine runs verified the native spawn, map, module and settings. Runtime
retained route ownership throughout. Nominal and worst-phase ratios are both
1.0303, passing the individual M1 pose gate. The audit also checks equal
initial origin, velocity, view angles, ground entity and force fields between
the planned and nominal runtime captures; full-struct equality is recorded
separately and is not assumed.

Artifact: `.codex-research/strafe-blue-spawn-qualification-20260904`.
Build: `.codex-research/strafe-build-route-coverage-20260904`.
Verified portable assets: `tools/fixtures/strafe_flag/kejim_blue_i3_*`.

This is individual route qualification. The current adapter uses one global
guide, so red and blue route assets were selected in separate configurations.
The 10/24 census is preserved as a baseline, not rewritten as combined current
coverage. Next: select the appropriate verified guide in one runtime
configuration, then qualify further entries. M1 remains incomplete.

## Experiment COV-03: owner-added flagstand runs

Both directions are part of the benchmark set: red stand to blue stand and
blue stand to red stand. They are counted separately from the 24 spawn poses.
The fixture locates the actual own-flag entity and traces the player's native
collision box downward to a standing surface at the same XY. Setup is refused
if the trace cannot establish support. It starts at rest, facing the opposing
stand, with the same force, weapon, physics and timing setup as the spawn
census. Actual enemy-flag pickup is still required. No flag is granted to the
player at setup, so this measures outbound traversal rather than a flag return.

The new `--flagstand` runner option works for runtime and planned-native runs;
`--flagstands-only` adds the two untuned baseline trials without repeating or
changing the frozen spawn census. Native metadata records the source flag
origin, standing root, heading, class, team and checksum. Planned references,
eight-phase checks and disturbance qualification remain required afterward.

Build: `.codex-research/strafe-build-flagstand-20260904`.
Artifact: `.codex-research/strafe-flagstand-baseline-20260904`.

Untuned baseline: **red stand to blue stand timed out at 20.000 s**;
**blue stand to red stand picked up in 18.850 s**. Both trials verified their
native standing roots and headings. Neither acquired the existing red guide;
both used incumbent movement. These results establish the additional baseline,
not reference-speed or eight-phase qualification for the flagstand starts.

The final flagstand build also replayed the original red nominal runtime
case: pickup at 6.700 s, with all 268 movement/command checkpoints identical
to the accepted nominal trace. The additional audit verifies 60 source and
staged-artifact hashes across these three runs; together with the preceding
45-trial audit, this turn preserves 48 native trials. All 42 Python tests pass.
Audit: `.codex-research/strafe-progress-with-flagstands-20260904/audit.json`.

```powershell
python -B tools/strafe_spawn_coverage.py --build .codex-research/strafe-build-flagstand-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/flagstands-NEW --port 30500 --flagstands-only
```
