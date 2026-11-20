# Bounded route hop fallback and legacy predictor repair

2026-09-14. The owner explicitly keeps routes primary, asks for Bot2 as an
additional maneuver tool only, and requires real evaluation defects to be fixed
rather than hidden by a fallback. No installed-game changes.

## Predictor defects corrected

The legacy simulator was recomputing steering every `pmove_msec` (8 ms in the
fixture) instead of every actual Bot2 command (25 ms). It also smoothed unlocked
aim while live execution snapped; did not copy `pmove_float`/`g_stepSlideFix`;
and returned a first-contact origin combined with later post-slide velocity,
flags and command time. Live takeoff itself requested jump without selecting
the same strafe command as the forecast's first step.

`Bot2_JumpCommand` now supplies the same unlocked takeoff/air command to both
paths. Prediction uses the server command cadence with native fixed-step time
rounding, the actual float/slide settings, and returns a coherent contact PS
after checking the subsequent slide. Locked combat steering is not qualified
by this movement-only cohort. The old simulator still has a narrower model
than actual-boundary forecasting; its animation/resource/trigger limitations
are not declared solved.

In twelve reconstructed legacy continuations, observed deaths fell from 6 to 1.
Of 167 logged post-setup landings, 163 have zero XY error at printed 0.1-unit
precision; the previous cohort had zero of 122. Unequal retained durations
and changed trajectories mean these counts are diagnostic, not a paired
reliability estimate. Four remaining mismatches reach 159.2 XY units and stay
open. C11 still falls after a Walking -> Falling/Airborne transition, following
five matching predicted landings; fixing the jump predictor does not validate
legacy ground-route decisions. Runner controls remain alive 12/12.

## Bounded integration

`ai_bot2_hop_fallback.c` adds a default-off `bot_hopFallback` option, exposed by
`tools/strafe_play_smoke.py --hop-fallback`. The ordinary route remains the owner
and is never replaced by legacy walking, off-mesh drops, escape or chain logic.

Eligibility requires an admitted destination path, a successful ordinary path
command and body check, no higher-priority flight ownership, at least 750 ms of
consecutive grounded slow movement, at least 400 path units remaining, no active
power, jump, attack or generic command, and the existing movement-only/25 ms
contract. It does not intercept path refusal, frontier waiting or arrival.
Brief legitimate idle therefore need not trigger it.

At an actual Pmove boundary, the current command is immutable and executes
first in every forecast. At most one preparation is attempted per server frame,
with a per-client 1 s retry interval. Three proposals use the shared strafe
command: left, right and a direction chosen toward a point 400 units along the
published path. The latter is a general receiving-directed proposal, not a map
coordinate or relaxed landing gate. The first two alone admitted no useful
fallback in the initial four-case pilot; their refusals are retained.

Each candidate must complete one native jump, satisfy the existing 12-unit
landing-entry envelope/direction/height checks at an onward interval, and execute
200 ms of grounded ordinary path following without a new jump or refusal. A
separate same-duration native forecast of ordinary route following must succeed,
and the candidate must reduce remaining published-path distance by more than
48 units. A refused baseline is reported, not treated as a reason to take over.
Three candidate forecasts and up to three baselines are bounded to 100 commands
each. This is a work bound, not proof that the CPU budget passes.

The admitted commands retain their predicted physical boundaries and publication
generation/hash. Every live frame checks time, position, velocity, support and
active powers; a fresh one-step native guard checks the actual command against
current collision/trigger state. The guard preserves native command time when
returning to ordinary input. The terminal state must match before route follower
ownership resumes. Failures log `execution_mismatch`, `native_guard`, publication
change or world refusal; a mismatch disables the supplement for that owner until
lifecycle reset. Native physics, resources, health and objective events remain
live. No historical state is restored between commands.

## Evidence and interpretation

Local root: `.codex-research/bounded-hop-20260914-a/`.

- `before-source`, `before-fallback`, `scope.json`: preserved starting sources.
- `native-a`, `cases.json`, `collect.py`: repaired-predictor twelve paired cases.
- `native-c`: initial fixed-side fallback pilot; no admissions, all eight alive.
- `native-e`: receiving-directed four paired cases, current implementation.
- `extended-e`: the same four pairs requesting 60 game seconds each.
- `landing-comparison.json`, `analysis.json`, `fallback-results.json`: retained
  landing error, command/capture/death anchors, native fallback events and
  sampled locomotion. `analyze.py` and `analyze_fallback.py` reproduce them.
- `policy-portable-check`: compiled sequencing/refusal checks from the real
  policy. Portable entry point: `tools/strafe_hop_fallback_check.py --out <new>`.

The current build is `.codex-research/strafe-build-bounded-hop-20260914-e`.
Build A measured the predictor fixes; C measured the fixed-side supplement.
Builds B and D compiled intermediate code but collected no native outcomes.
All were unique builds; measured native sources were frozen during collection.

In the short current pilot, C01, C07 and C09 have observed return capture gains
of 0.275, 0.325 and 0.250 s respectively. C12 makes no fallback request and
retains its ordinary controller. Four prepared maneuvers all rejoin, with zero
execution mismatches/native-guard/world refusals. All eight worlds remain alive.
These are reconstructed physical starts, not exact historical controller/RNG
replays; the earlier comparison's fixture/reset and capture-sampling caveats
continue to apply. Local forecast gain is not equivalent to a full-trip gain.

## Extended outcomes and final checks

All eight 60-second-requested worlds capture and remain alive; no audited
prolonged distant confinement is observed. The four fallback worlds prepare
and rejoin eight maneuvers, with no execution/native/world mismatch or
publication-loss event. Together with the short pilot, 12/12 admissions rejoin.
Observations can extend beyond the requested window; the C12 second capture
is at 64.425/64.700 seconds from setup and belongs to retained full-trip evidence,
not the first-60-second motion score.

| Case | Initial capture, control / fallback (s) | Complete next trip, control / fallback (s) |
|---|---:|---:|
| C01 | 5.650 / 5.400 | 42.675 / 34.100 |
| C07 | 10.650 / 10.400 | 34.725 / 34.175 |
| C09 | 13.275 / 13.025 | 28.175 / 28.050 |
| C12 | 29.775 / 30.325 | 34.650 / 34.375 |

The four post-capture trips average 35.056/32.675 s, dominated by C01's
phase/route variation; do not attribute its entire 8.575 s difference to a local
hop. C12's initial trip is 0.550 s slower. Fixed first-60-second CSV walking totals
are 66.050/68.550 s and idle 5.500/5.750 s (control/fallback). Thus faster measured
complete trips do not establish a broad walking or idle reduction. Keep the
mixed evidence, separate anchors and lifecycle context when judging usefulness.

All 1,109 current native inputs match build E. Eight final-build pairs have
matching staged binaries and fixture hashes; declared pose/velocity/yaw match,
and paired runtime settings differ only in isolated port and `bot_hopFallback`.
The portable compiled policy checks and twelve existing decision/state-seed
Python tests pass. The actual dynamic-body refusal branch and CPU qualification
remain open, rather than inferred from unchallenged successful runs.

## Remaining work and activation boundary

Keep the supplement opt-in. Before ordinary activation, qualify native moving
body/late-obstruction guard behavior and wider phase/start/resource coverage.
Owner update: measure complete per-frame CPU cost separately, including rejected
searches and native guards, but do not let current CPU targets veto a correct,
useful method. Optimization and resource provisioning follow the functional solve. Investigate the four retained legacy prediction discrepancies
and C11 ground fall separately; do not route around them silently or resume
pure legacy movement. Candidate refusals at a legitimate receiver remain valid.

The new capability is one bounded hop with a route return. It is not full
connected multi-hop planning, a general idle cure, combat readiness, map-transfer
qualification or installation. Existing movement acceptance gates remain open.
