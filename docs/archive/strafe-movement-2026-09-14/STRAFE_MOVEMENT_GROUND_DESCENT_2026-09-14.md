# Supported descent and interruption recovery — 2026-09-14

## Result and scope

A general ground-corridor geometry correction removes both retained
post-interruption confinement episodes in the repeated eight-case body cohort.
All eight extended worlds capture alive, with no audited five-second confinement,
six expected body-guard refusals and no unexpected execution/world mismatch.
This is a corrected movement evaluator, not a new escape maneuver or a route
coordinate exception. Dynamic obstruction recovery is still incomplete.

In comparable first-60-second observations, sampled grounded idle falls
38.000 to 15.750 seconds, but walking rises 163.000 to 186.500 seconds.
Combined idle/walking does not improve (201.000 to 202.250 seconds). Coverage
is 478.450/479.125 seconds due to native CSV sampling and intervention phases.
Do not turn the confinement improvement into a claim of general walking success.

## Diagnosed causes and production change

The prior C01-air episode lasts 5.500 seconds, level 15825–21325, around
(244, -698, 1912). Recovery proposals at levels 17325 and 19075 abort shortly
after activation with `direct_blocked sample=1`; a lateral third proposal
finally continues. The prior C07-takeoff36 episode lasts 6.750 seconds,
level 13225–19975, around (2461, 5277, 1925). The direct ground corridor is
repeatedly refused at sample 1 while local navigation oscillates sideways.
Its 16-unit displacement reset prevents the recovery grace from maturing.

`GroundCorridor` found the lower supported floor, then traced a diagonal
from the previous height to that floor. At a descending lip, this diagonal
cuts through the upper floor even though crossing at the previous height
and settling down are clear. This also explains why a coarse admitted
recovery segment could be refused from its next actual position.

`codemp/game/ai_bot2_localnav.c` now tests that horizontal crossing and vertical
settling shape when the descending diagonal is blocked. Both sweeps must be
clear. Existing world-floor, slope, bounded step/drop, liquid, body and
trigger_hurt checks remain. All candidates and live recovery steps use the
same check. No resource conditions, hop eligibility, recovery timers, progress
thresholds, fixture coordinates or owner priorities were changed.

The correction is supported by a synthetic collision-scene regression that
fails against the preserved old source and passes against current source.
Native repeat outcomes support its usefulness in the retained cases. They
do not establish every dynamic or geometric recovery situation as solved.

## Extended body outcomes

Same starts, commands, resources, late-body declarations and assets as
`hop-body-20260914-a/clear-body-c`; game duration extended to 90 seconds.
Capture times are seconds after the acknowledged corner intervention. Native
runner shutdown can retain a short tail beyond the requested duration.

| Case | First capture | Second capture | Alive | Confinement |
| --- | ---: | ---: | --- | ---: |
| C01-none | 5.425 | 38.400 | yes | 0 |
| C01-pre_takeoff | 8.175 | 49.800 | yes | 0 |
| C01-takeoff36 | 6.075 | 46.175 | yes | 0 |
| C01-air | 9.025 | 58.250 | yes | 0 |
| C07-none | 10.875 | 44.125 | yes | 0 |
| C07-pre_takeoff | 13.825 | 53.650 | yes | 0 |
| C07-takeoff36 | 11.575 | 51.250 | yes | 0 |
| C07-air | 14.150 | 56.775 | yes | 0 |

All six injected bodies are created in clear poses and cause one expected
native body guard. The two uninterrupted controls rejoin 11/11 admitted hops
across the extended recordings. Guarded aborts correctly return to ordinary
ownership; they are not expected to emit `rejoined`. First-capture timings are
mixed and several are slightly slower. C01-air's second capture is also slightly
later than the preceding cohort despite removal of its confinement episode.

## Persistent moving-body limits

The retained pursuit declarations are repeated with unchanged 30-second windows.
All eight worlds survive; six capture, including both red fast-pursuer cases
that previously did not capture. The blue fast-pursuer cases remain uncaptured
within this window. Five worlds have audited confinement.

| Case | Runner capture | Hop supplement capture |
| --- | ---: | ---: |
| C01, pursuit 120 | 32.100 (tail) | 10.675 |
| C01, pursuit 300 | 20.550 | 20.325 |
| C07, pursuit 120 | 18.000 | 17.750 |
| C07, pursuit 300 | not observed | not observed |

C01-120-runner regresses from the preceding 13.000-second capture. It is
confined for 16 seconds, level 2175–18175. Eight similar eastward recovery
proposals occur; seven are interrupted as the moving body occupies the
corridor. Recovery continues choosing a freshly clear but soon blocked leg.
This is evidence for dynamic obstruction-aware replanning and failed-proposal
memory, not permission to bypass body checks or blindly jump. The two red
fast-pursuer cases still have a 7.500-second confinement episode despite their
later captures. Retained exact episode rows are in `results.json`.

Two 90-second C01-120-runner repeats capture at 20.625 seconds each,
with confinement in both: slower than the earlier control, but not the
32.100-second first repetition. Blue fast-pursuer extended repeats capture at
25.150/24.900 seconds (runner/supplement), with two/one confinement episodes.
Since these starts and observation phases vary, the earlier uncaptured
30-second runs are not simply the first 30 seconds of these later recordings.
All four repeated worlds survive. Preserve phase-sensitive outcomes; no broad
dynamic-obstruction pass is claimed. Across all 20 worlds there are no observed
fatalities, but nine worlds have at least one audited confinement episode.

## Verification and retained evidence

Root: `.codex-research/recovery-descent-20260914-a/`.
Fresh native build: `.codex-research/strafe-build-recovery-descent-20260914-a/`.
Source manifest: `20260914T194911801099Z.sources.json`.

- `before-source/`: preserved pre-change local navigation source.
- `descent-check/`: descending lip, flat floor, deep drop, body and liquid checks.
- `descent-negative-control/`: expected failure against the old source.
- `recovery-check/`, `sidestep-check/`: existing affected regression checks pass.
- `native-a/`, `pursuit-a/`: declarations, private homepaths, native recordings,
  loaded-binary/source/settings bindings and outcome summaries.
- `pressure-repeat-b/`: two slower-pursuer control repeats and two extended blue
  fast-pursuer cases, with all declarations and failure episodes retained.
- `verification.json`: all 1,109 current native inputs and all 20 copied
  binary/source/provenance bindings verified after collection.
- `common60.json`: per-case before/after locomotion and confinement observations.
- `analyze.py`, `results.json`: retained full-run outcomes, guard commands and stalls.

Sources remain frozen through native collection. No installed-game change,
ordinary fallback activation, broad qualification or CPU timing claim.

## Next implementation

Preserve this evaluator fix. Use the moving-body regression and blue pursuit
failures to implement actual-state continuation that can predict obstruction
of a proposed leg and choose an alternative, retaining failed-leg evidence.
Do not merely shorten grace periods or add another map-specific escape.
Useful-progress detection must also distinguish oscillation from departure;
the unchanged 16-unit reset is still a structural limitation even though the
two retained ground-lip cases no longer trigger it.

Score the same body cohort, pursuit cases and complete trips, including controls
without the hop supplement. CPU is separate optimization/provisioning work,
not a veto on a correct useful solver. Earlier cached-route falls, four legacy
prediction discrepancies, C11 ground fall and broader movement acceptance remain
open. See the [handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) and
[preceding body record](STRAFE_MOVEMENT_HOP_BODY_2026-09-14.md).
