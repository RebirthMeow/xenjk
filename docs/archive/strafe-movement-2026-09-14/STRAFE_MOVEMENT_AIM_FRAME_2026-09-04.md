# Airborne steering during combat aim turns

This follows the corrected fixture investigation in
`STRAFE_MOVEMENT_RECOVERY_FORENSICS_2026-09-04.md`.

## Intervention

`bot_aimMoveFrame 1` makes the AIRBORNE movement state project its existing
target direction relative to the packed view yaw actually submitted to Pmove,
including `ps.delta_angles[YAW]`. The same small helper is used by phantom
prediction after its simulated view command is packed. Mode 0 retains the
previous live and simulated implementations for paired comparison.

Previously, live axes used the final desired aim yaw before view smoothing.
The simulator used its smoothed, unquantized yaw. A player turning from 170
degrees toward world +X submits approximately 140 degrees on the first 25 ms
frame, so constructing axes for zero yaw rotates their world direction by
approximately 140 degrees. This change fixes that reference-frame discrepancy.

The change does not alter view turn rate, shared Bot2 state, the cached route
kernel, strafe-side selection, force policy, or prediction budgets. The existing
hard-strafe policy still suppresses forward input; it does not preserve a
general target world direction. Ordinary soft strafing does. No route-specific
direction or successful command sequence was added.

This is not full simulator/runtime parity: the phantom still updates controls
at its simulation cadence, uses different animation/weapon assumptions and
landing margins, and smooths pitch toward the horizon. WALK and special
traversal states retain their existing movement policies. The current native
experiment uses level world +X aim, not tracking or shooting a moving opponent.

## Evidence and reproduction

The correction remains **opt-in** (`bot_aimMoveFrame` defaults to 0). It improved
completion in this fixed-direction recovery set, but some successful routes
became slower; repeated aim changes, vertical aim, other movement states and
live contact remain untested.

### Seven paired bounded native runs

Artifact: `.codex-research/strafe-aimframe-bounded-matrix-20260904`.
The corrected steering picked up in **7/7**, compared with **4/7** for legacy
steering. These are seven deterministic development cases, not an estimate of
match success probability. Both arms use bounded planning and decision seed 1.

| Aim handoff / disturbance | Legacy steering | Submitted-yaw steering |
|---|---|---|
| 1.500 s | Pickup 18.275 s | Pickup 18.750 s |
| 2.500 s | Pickup 17.325 s | Pickup 16.000 s |
| 3.500 s | Timeout 20.000 s | Pickup 14.150 s |
| 2.500 s, phase 3 | Death 14.800 s | Pickup 15.000 s |
| 2.500 s, phase 6 | Pickup 13.300 s | Pickup 17.300 s |
| 2.500 s, +50 X velocity | Pickup 14.200 s | Pickup 14.450 s |
| 2.500 s, -50 Y velocity | Timeout 20.000 s | Pickup 13.925 s |

All seven pairs have byte-identical command prefixes and full Bot2 states at
handoff. The 3.500-second and phase-3 cases also have byte-identical **complete
player states**, establishing two improvements from identical captured roots.
The other five differ only in `pmove_framecount` at the first differing command,
due to pre-setup server timing. Those pairs are not described as full-state
identical; their differences remain in the audit. This field participates in
jump-pad bookkeeping, so it is not silently excluded from the comparison.

In phase 3, the same random strafe choice (+1) now succeeds. At the first
handoff command, the requested world direction is 36.887 degrees and the
submitted view is 139.999 degrees. Legacy axes `(101,-76)` produce a world
direction of 176.959 degrees; corrected axes `(-28,123)` produce 37.174 degrees.
The direction error falls from 140.073 to 0.288 degrees. No alternative seed,
extra planning interval or forced continuation was supplied.

All 14 runs verified their staged module and native settings. Each retained
the 2,048-call dispatch / 4,096-call frame limits. Corrected arms' worst measured
dispatches ranged from 5.487 to 6.889 ms. These are solo fixture measurements,
not evidence that a server can afford that cost for every bot every frame.

The initial two-case exhaustive/capped recheck is also preserved at
`.codex-research/strafe-aimframe-fixed-pairs-20260904`: corrected exhaustive
planning picked up at 19.825 s for the 1.500-second handoff and 15.075 s for
phase 3, with worst dispatches 65.402 and 54.648 ms. Corrected bounded repeats
picked up at 18.750 and 15.000 s, with worst dispatches 7.808 and 9.136 ms.
More exhaustive work was not necessary to obtain either pickup here.

### Build and geometry

The final clean-route regression, with the correction enabled, passed all 12
cases with exact commands, position, velocity, pickup and clock equality at
all **3,248 checkpoints**. Nominal pickup remains **6.700 s**. Artifact:
`.codex-research/strafe-aimframe-clean-matrix-20260904`.

The clean build, 36 strafe Python tests, four server-launcher tests, geometry
check and whitespace check passed. No native servers remained afterward.
`.codex-research/strafe-aimframe-audit-20260904/audit.json` verifies all 14 paired
recovery arms and 12 clean arms, source snapshots and hashes, staged module
hashes, operation limits and the two complete-state-identical improvement
roots. No installed DLLs or server configurations were changed.

Fresh dedicated engine/game build with symbols:
`.codex-research/strafe-build-aimframe-20260904`.

`tools/strafe_steering_check.py` compiles the production helper against a minimal
engine API. Its independent world-vector reconstruction passed 158,720 cases
covering headings, command wrapping and spawn angle offsets. Maximum ordinary
strafe direction error was 0.444013 degrees from integer movement axes. It also
checks preserved command fields, the hard-strafe restriction, and cvar updates
once per server frame/map. This geometric test does not establish gameplay
success.

`tools/strafe_aimframe_matrix.py` runs seven paired native cases with the same
bounded planner, fixed per-frame decision/native random streams, and full state
captures. Its snapshot preserves source files and hashes. Each pair differs
only in the steering cvar. It reports full state equality rather than assuming
that nominally identical spawns contain identical structs.

```powershell
python -B tools/strafe_fixture_build.py --out .codex-research/aimframe-build-NEW
python -B tools/strafe_steering_check.py --out .codex-research/aimframe-geometry-NEW
python -B tools/strafe_aimframe_matrix.py --build .codex-research/aimframe-build-NEW --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/aimframe-pairs-NEW --port 29930
python -B tools/strafe_runtime_matrix.py --build .codex-research/aimframe-build-NEW --assets 'game_directory/Jedi Academy/GameData' --reference .codex-research/strafe-flag-final-matrix-20260904 --out .codex-research/aimframe-clean-NEW --port 29950 --aim-move-frame 1
```

Native conditions remain Kejim red-i2, sv_fps 40, fixed 8 ms floating movement,
g_speed 250, gravity 800, explicit XenJK regen 200, initial saber/Speed setup,
and actual live enemy-flag pickup within 20 seconds. No other players interfere
in these isolated tests; impulse arms are explicitly declared disturbances.

## Next experiment

Separate the intended world movement vector from view orientation throughout
ordinary strafing. A remaining source-level issue is that free-view soft
strafing sends W plus a side key, while aim-locked strafing treats `tYaw` as the
world movement direction. That loses the diagonal key offset: correcting the
view reference alone does not make both policies request the same acceleration.
Hard strafing has an even more restrictive forward-axis rule. First measure
these policies from the same state under differing aim directions, then test
preserving their movement vector while aim turns. This is a small constant-cost
steering experiment before adding search, route caches, or learned choices.
