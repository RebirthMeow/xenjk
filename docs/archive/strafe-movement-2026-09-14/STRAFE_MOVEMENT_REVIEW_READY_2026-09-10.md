# Recovering fast returns without the unsafe joins

Subsequent owner review found the delivery criteria insufficient: lost pickup
momentum, depleted-Force continuation and cautious fallback corners remain
blocking. This document preserves the measured delivery, not current acceptance.
See the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#opening-instruction-for-the-next-ai).

## Decision and implementation

The owner rejected the corner repair's 19.575-second median as a worthwhile
delivery and asked us to take responsibility for a candidate worth reviewing.
The preceding installation remains historical evidence, not a passed speed
milestone. We retained its checked streaming curves, proportional grounded
arrival, rising post-launch join rejection and forward-floor checks.

The broad 50-Force rule was the measured regression: successful early jumps
left 42 Force and about 13 seconds of Speed, but the next launch was refused.
The revised policy removes that blanket threshold. Non-tap launches still
require active Speed with at least 2500 ms remaining, the native activation
reserve (10 at Jump 3), and the existing momentum checks. Checked ordinary-tap
corridors still work at zero Force. No resource wait, grant or teleport is added.

A proposed replacement threshold of 26 was rejected after reviewing actual
successful launches bracketed at 13 and 20 Force. Some force exhaustion ends
in an ordinary safe landing; exhaustion alone is not proof that a jump fails.
The actual landing, following support and subsequent admission matter. This
remains bounded empirical admission, not a universal flight feasibility proof.

The first revision exposed a useful distinction in additional coverage:
blue's 12-Force upper entry failed a roughly 1010-unit same-height flight,
while its otherwise identical 20-Force entry landed. The successful red
13-Force flight descends about 104 units. The final revision therefore requires
20 Force only for non-tap flights whose selected receiver is more than 512 XY
units away and less than 96 units below the actual launch. Short and downhill
flights retain the native activation reserve. This number is demonstrated
containment, not a claimed minimal analytic budget. The failed run is retained.

## Frozen build and delivery targets

Final build: `.codex-research/strafe-build-reviewready-20260910-b`.
The initial revision is retained as `strafe-build-reviewready-20260910-a`.
The source change is in `ai_bot2_route.c`; other corner repair production code
is retained. Source/binary hashes are bound by the fresh build manifest.

Before collection, the active plan recorded these delivery targets: natural
captured-return median at most 14 s, mean horizontal speed at least 475 u/s,
airborne share at least 55%, retaining all failed/censored episodes. Known
corner/stall and zero-Force/Speed-expiry cases must avoid solo falls, followed
by defended normal MP capture with no long idle stall. These targets do not
replace or pass K07.

| Completed natural return metric | Prior fast build | Slower corner installation | Revised build |
|---|---:|---:|---:|
| Median return, seconds | 11.825 | 19.575 | 12.0625 |
| Time-weighted mean horizontal speed, u/s | 510.4 | 372.2 | 538.6 |
| Time airborne | 63.6% | 44.3% | 64.3% |
| Mean sampled stationary time per return, seconds | 0.464 | 0.350 | 0.031 |
| Completed returns | 7 | 5 | 8 |

The middle cohort also retains one ending censored episode. Revised natural
returns all capture: blue 12.275, 11.950, 12.200, 12.075 s; red 24.100,
12.050, 11.875, 9.600 s.
The first depleted red return remains slow; a good median does not erase it.
These are small unmatched cohorts, not a statistical superiority claim.
Metrics are left-sample time-weighted within completed carrier episodes; the
roughly 4 Hz observations cannot establish every jump input or between-sample
minimum speed. Source reports:

- `.codex-research/corners-20260910-b/return-tradeoff-comparison.json`
- `.codex-research/reviewready-20260910-b/natural-return-audit.json`
- `.codex-research/reviewready-20260910-b/delivery-metrics.json`

## Native and focused evidence

Final-b evidence is `.codex-research/reviewready-20260910-b/`: all 14 solo
worlds complete 120 game seconds, with 56 captures, zero deaths, clean
shutdowns, no audited five-second confinement and longest sampled idle at
most 1.0 s. This includes the original ten cases and both teams at 12/20
Force. Blue-12 now refuses the unsafe receiver at level time 2050, moves
512 XY units within 2.275 s of setup and captures within 9.05 s; the reserve
check does not create a waiting state. See `runs.json`, `solo-motion-audit.json`
and each world's provenance, summaries, logs, CSVs and demos.

Final normal-speed, normal MP one-enemy worlds capture twice red and three
times blue. Runner deaths are three red/one blue, all attributed to enemy
weapons in games.log; no runner falling deaths. Longest sampled idle is
1.25/0.75 s, longest continuous walking 4.50/4.75 s, with no audited confinement
or distant stall. See `opposition-audit.json`. These are defended smoke tests,
not demonstrated human competitive strength: `defended-return-audit.json`
retains seven pickups, five captures and two carrier deaths. Completed defended
return median is 24.325 s (13.250–24.925 s), considerably slower than solo.
Further work should be driven by the owner's play review of corner exits and
pressure recovery; do not advertise the solo 12-second median as defended pace.

The initial `.codex-research/reviewready-20260910-a/` repeats the complete ten-world
corner-repair set: natural red/blue; upper/ramp/stall in both directions with
26 Force and declared Speed expiry; upper corners with zero Force/no Speed.
All ten complete 120 game seconds, with 38 total captures, no deaths and
clean shutdowns. Outcome-only acceleration is 5x, at most four isolated
worlds. Natural longest sampled idle is 0.75 s red/1.5 s blue, with no audited
five-second confinement or distant stall. Per-world declarations, binary and
source bindings, logs, CSVs and demos are retained; no failed cases are excluded.

Additional 12/20-Force upper-corner cases and normal-speed defended MP are
recorded under `.codex-research/reviewready-extra-20260910-a/`. Blue-12 falls;
the other three additional solo cases survive. Initial-a defended red/blue
have two/one captures and one/three runner weapon deaths. These are retained
development results, not the final build's evidence.

Focused checks pass at
`.codex-research/fastreturn-check-reviewready-20260910-b/result.json`: non-tap
launches accept native-reserve/13/20/42 Force at exactly 2500 ms active Speed;
9 Force, 2499 ms and inactive Speed refuse while preserving input/PS. Zero-Force
checked taps, rising-sample rejection, ground-floor refusal and prior ownership
checks remain covered. The receiver-specific reserve tests cover exact
512/513-unit distance, 95/96-unit descent and 19/20-Force boundaries, preserving
short/downhill 13-Force flights and zero-Force safe taps. Geometry and strafe
execution are mocked there; the
native worlds are the separate outcome evidence.

The unchanged corner-publication and analog-arrival code retain their passing
source-bound checks from the [corner repair](STRAFE_MOVEMENT_CORNER_REPAIR_2026-09-10.md).
No native cost/timing qualification or broad map-transfer claim is added.

## Handoff

Final build `strafe-build-reviewready-20260910-b` is installed with fast return,
checked corners, forecast guard and pressed-runner staff melee enabled. All
nine installed hashes verified; fresh build sources match collection. Previous
files are preserved in `.codex-archive/20260910T0607586345246Z/manifest.json`.
To restore, close the game, archive the current replacements and move the saved
entries back to their original paths using that manifest. No files were deleted.

The candidate meets the declared bounded delivery targets and is ready for
human in-game review. Start a fresh Kejim CTF game, `exec runner_playtest.cfg`,
then `addrunner`. K07 remains open. Preserve the slower build and every failed
run. Do not substitute another broad resource threshold for this diagnosis.
