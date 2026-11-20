# Hop fallback: body interruption and complete-trip validation

2026-09-14. Owner direction: solve movement correctness/usefulness first. CPU
cost is a separate optimization/provisioning target, not a veto. Routes remain
primary; do not hide underlying evaluation defects with fallback behavior.

## What changed

Added `--hop-body-frame` to the existing isolated runner. With a dedicated corner
fixture, `--hop-fallback` and declared `--flight-body-motion`, command 1 introduces
a body just before the first admitted hop command reaches native Pmove; command
12 introduces it during flight. Reserved body window 1000 selects this fixture
without matching a baked flight. Existing dedicated/cheats/movement-only gates
apply. The existing gravity/collision proxy moves independently, remains in the
world, and changes no runner pose, velocity, resources, health or lifecycle.

The legacy body fixture could initially place its hull inside a sloping floor.
Added a native hull check before spawning; solid placements explicitly refuse.
No fallback/controller clearance or admission gate was weakened. This turn's
native changes are diagnostic hooks and fixture validity, not a controller fix.

## Frozen evidence

Local root `.codex-research/hop-body-20260914-a/` contains declarations, collection
scripts, server logs, CSV, demos, summaries, provenance, source checks and hashes.
`analyze.py` produces `results.json`; `remaining-obstructions.json` records all
retained pursuit/current clear-body confinement episodes; `retained-route-failures.json`
preserves full CSV rows near previous deaths plus cached-route release messages.

- `pursuit-a`: eight worlds, two recorded starts (C01 red, C07 blue), blocker
  pursuit at 120 or 300 units/s, fallback on/off. Same physical starts and
  declared bounded floor/collision pursuit with 512-unit leash and 36-unit gap.
  Build: `strafe-build-bounded-hop-20260914-e`, sources verified before collection.
- `late-body-a`: eight worlds, two starts times no body, distant body, 36-unit
  close body at command 1, or close body at command 12; 30 game seconds requested.
- `complete-body-b`: eight 60-second-requested continuations, adding a 32-unit
  horizontal separation for a genuinely pre-command collision. These first two
  cohorts use build `strafe-build-hop-body-20260914-a`.
- `clear-body-c`: eight 60-second-requested worlds with a 16-unit vertical offset
  and native initial-hull validation, build `strafe-build-hop-body-20260914-b`.
- `initial-solid-check`: negative fixture test on build B. The previous C01
  zero-height-offset/32-unit ramp placement explicitly refuses `initial_solid`,
  with no body created. It is a setup-check pass, not a body-behavior pass.
- `extended-clear-d`: longer control/interruption observation for C07's delayed
  continuation. Kept separately rather than overwriting the first observation.

C01/C07 are the exact retained start declarations from the previous comparison.
Private historical timers, animation, health and RNG phase are not replayed.
Timing is relative to native setup and captures are CSV-observed events.
Requested duration is anchored by the harness later than setup; retained shutdown
samples can extend beyond the nominal observation. Do not omit late deaths or
count an end-censored trip as complete. Native sources remain frozen throughout
build/collection; max four isolated worlds, distinct localhost ports/homepaths.

## Pursuit results

All eight worlds remain alive. At 120 units/s, all four capture: red control /
fallback 13.000 / 12.800 s; blue 17.925 / 17.675 s. Both admitted fallback hops
rejoin. This establishes movement with a pursuing physical body, not late-hop
guard coverage by itself.

At 300 units/s, neither controller captures in either start. No fallback is
admitted; both retain prolonged distant confinement under the body repeatedly
holding the path ahead. Two confinement episodes occur in each of these four
worlds. The new tool does not solve this persistent obstruction and does not
turn an unsafe candidate into a permitted jump. Keep this as a separate
obstruction/replanning problem, not a successful fallback qualification.

## Targeted guard and whole-continuation results

In `late-body-a`, each close-body case triggers `native_guard detail=2` (body),
and both distant-body controls avoid false guard triggers and rejoin. However,
the 36-unit command-1 case is detected one native frame after launch on C01;
it is not proof of pre-launch refusal. The closer 32-unit case tests that branch.

The final clear-placement cohort has six body-triggered guards, zero unexpected
execution/world mismatches, and all eight worlds capture and remain alive.
No-body controls have no false guards. On the grounded pre-launch frame, native
`FLIGHTBODY runner after=0` confirms upmove zero. Body collision on the ramp can
still produce transient airborne contact in `after=1`; do not claim the bot
remains grounded merely because the jump command was refused. Airborne guards
return to ordinary movement with live velocity; no reset or teleport occurs.

| Case / intervention | First capture (s) | Second capture (s) | Retained issue |
|---|---:|---:|---|
| C01 none | 5.400 | 39.850 | None audited |
| C01 pre-launch, 32 units | 8.150 | 57.075 | None audited |
| C01 early flight, 36 units | 6.075 | 48.750 | None audited |
| C01 command 12 | 8.675 | 58.025 | 5.500 s distant stall after capture |
| C07 none | 10.725 | 44.875 | None audited |
| C07 pre-launch, 32 units | 13.200 | 62.600 | Second capture in retained tail |
| C07 early flight, 36 units | 11.100 | Not observed in first run | 6.750 s distant stall |
| C07 command 12 | 13.700 | 59.325 | None audited |

An aborted maneuver is not expected to report `rejoined`: normal dispatch resumes
instead. Count successful guard intervention separately from uninterrupted hop
completion. Surviving and eventually capturing also do not erase the two stalls.

The extended C07 repeat stays alive and captures at 11.100 and 68.150 s;
its full post-capture trip is 57.050 s, versus 36.575 s in the paired control
(which captures at 10.425, 47.000 and 83.075 s). Its 6.750 s stall repeats.
Thus the missing second capture was observation-censored, but recovery remains
materially slower. Do not call the delayed continuation a movement-quality pass.

Verification: 34 outcome worlds retain matching source-manifest/staged-binary
bindings; all 1,109 current native inputs match build B. The additional native
initial-solid negative check passes. All final injected cases confirm one body
creation and a body-mask guard. CLI missing-scope/frame/window checks and twelve
existing decision/state-seed tests pass. No native motion source was edited
during a measured build or run. All failures and intermediate evidence remain.

## Earlier failures stay visible

Before initial-placement validation, C01's 36-unit early interruption produced
a later cached-route fall twice. At level 24000 in `complete-body-b/C01-takeoff36`,
route index 67 releases `envelope_or_speed` after the bot is already below the
expected surface; it later dies `MOD_FALLING`. C07's earlier 32-unit case also
has an observed later death. These rows and full logs remain retained.

The C01 zero-offset ramp setup is demonstrably invalid under the new hull check.
Corrected-placement repeats survive, but changing the intervention and resulting
trajectory does not prove every previous route/prediction defect repaired.
No cached-route fix was made here. Preserve these as reproduction leads, alongside
the four prior legacy prediction discrepancies and C11 ground fall. A successful
nearby guard is not evidence that a later route release was correct.

## Decision and next implementation target

The fallback's native body guard is exercised on these starts, with valid
pre-launch and airborne contact cases and successful continued capture. It is
an additional tool, not a replacement for routes or a general obstacle solution.

The concrete next target is route recovery after interruption: avoid the retained
5.500/6.750-second stalls and find a valid alternate continuation when a moving
body persistently occupies the direct path. Reproduce the exact actual-state
handoff and body position; fix the owning evaluation/replanning defect instead
of weakening landing gates or using legacy escape blindly. Keep the route-release
failures as independent controls when changing that logic.

The supplement remains opt-in while these functional recovery limitations are
open. CPU is explicitly not the reason. No cost run, installed-game change,
combat implementation, dropped-flag work or full movement acceptance is claimed.
