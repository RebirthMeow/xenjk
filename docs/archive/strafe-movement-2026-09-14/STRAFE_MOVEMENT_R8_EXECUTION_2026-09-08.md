# R8: contact attribution and local-navigation reentry

Continues the [R7 handoff](STRAFE_MOVEMENT_R7_EXECUTION_2026-09-08.md) from
local `master` at `a595b828`. Acceptance gates, native floor safety and resource
lifecycle are unchanged. The owner subsequently resumed timing, recorded below.
No game installation.

## Hypothesis and fixed diagnostic cases

R7 retained sustained pressure stalls with floor refusals but no hit identity.
Instrument the existing trace result without adding world queries or changing
collision masks. Report hit entity, contents, start/all-solid, fraction and
normal for local ground-floor refusals and supported preparation-floor refusals.
The engine merges trace flags across colliders (`sv_world.cpp`); reported hit
identity is contact evidence, not sole-collider or confinement causation.

The diagnostic cases retain R7's declared candidate red/blue 5x seeds
2026090806/2026090811, same-seed blue baseline (600 game seconds each), and
baseline red 1x seed 2026090802 (120 seconds). Sources remain frozen during
each build/collection. Four isolated native worlds maximum, separate ports and
homepaths. Explicit seeds do not promise identical combat timelines.

Instrumentation-only build:
`.codex-research/strafe-build-r8-contact-6eb2000dd18e457881d8065c87a096ff`.
Evidence: `.codex-research/strafe-r8-contact-trace-20260908/` with commands in
`plan.json`, native provenance, complete recordings, opposition audit and
`contact-report.json`. All four durations complete, all four have zero audited
distant stalls. Opponent entities 1/2 are reported in floor refusals, alongside
world, missing-floor and other entities. This does not reproduce or cure R7's
stall. `tools/strafe_ground_contact_report.py` binds logs/audit and retains
individual contact records inside any audited stall; full-log counts include
shutdown tails and are not outcome-window counts.

## Narrow production correction

`ai_bot2_localnav.c` previously cleared all private navigation state after
more than 100 ms away from that owner. Production recovery and supported
preparation can exceed this gap while the runner remains at the obstruction,
restarting the 750 ms passing-body wait. R7 traces already showed renewed
`choice=wait` following bounded ownership handoffs.

`NavigationReentry` retains only refusal start time, absolute heading-exclusion
deadline/direction and side preference when the gap is positive and at most
3000 ms, the runner is grounded, and both actual displacement and objective
change are at most 32 units. Cached geometry, commands, progress tracking and
yaw are refreshed. Objective changes now explicitly clear this history even
without a long gap. Clock reversal, longer gaps, airborne reentry, displacement
and lifecycle resets discard it. A successful direct corridor still clears the
refusal normally. No clearance result is inherited across a handoff.

The separate recovery controller's arrival delay is unchanged; this correction
does not claim to solve repeated short recovery steps under a following body.

## Verification

Fresh behavior build:
`.codex-research/strafe-build-r8-reentry-fc7aae654cd445078371541bd883c675`.
Manifest SHA256:
`afc09192683c671842669d08c9173c88ed71ead2629353168c60415c29eda8f5`;
game DLL SHA256:
`ff93b4bdc47c4a6ca467aef078a0efeee0e029a54f19287d56fa450086faf835`.
Native reentry observations use the same four declared diagnostic cases under
`.codex-research/strafe-r8-reentry-trace-20260908/`.
All four durations complete with clean shutdown and zero audited distant
stalls. The instrumentation-only cohort also has zero; do not interpret the
cohorts as proof of reduced stall incidence. All eight diagnostic worlds exited.

Extracted-production Win32 checks pass at
`.codex-research/localnav-reentry-check-78b54b671f5a406096089ed3153652f0`:
2900 gap durations (101..3000 ms), entire-state comparison, absolute exclusion
expiry, exact distance boundaries, clock/airborne/lifecycle and client isolation.
The helper is `tools/strafe_localnav_reentry_check.py`; it binds source and
extractor hashes. This checks policy state, not native physics or full dispatch.
The existing 16 seed/campaign tests and opposition-auditor synthetic checks pass.

Native blue baseline records a 425 ms ownership gap at level 58850 with
775 ms retained refusal age and zero displacement, immediately choosing a
validated sidestep. At level 424525 an 825 ms gap similarly retains 825 ms
history with 0.386 units displacement and selects a sidestep. These observations
prove actual dispatch no longer restarts that wait; they do not prove complete
escape from live pressure or close defended continuity.

Resource and temporary-body regressions are declared in
`.codex-research/strafe-r8-regression-launch.py`: all 30 adapter-armed resource
cases and all 26 frozen M2 temporary-body cases, retaining every outcome.
Completed evidence is in `.codex-research/strafe-r8-regression-20260908/`.
All 30 resource cases pass the existing audit, with exactly the same native
pickup/capture event sequences as R7. All 26 temporary-body cases are feasible,
valid and pass; 24 preserve R7 outcome/time/frame values exactly. Blue respawn 0
changes 12575 to 12600 ms; red respawn 1 changes 12550 to 12475 ms. These small
differences do not establish a causal gain. The subset runner correctly leaves
`milestone_pass=false`: full M2 plus M0/M1 retention are not requalified here.

Final checks retain all 1102 native source hashes with zero mismatches; 68 local
documentation links resolve and both new Python tools parse. No native worlds
remain. No timing collection, source cleanup or game installation occurred.

## Remaining work

The reentry defect is corrected and directly observed in native dispatch.
Sustained moving-body escape remains unqualified: R7's four candidate stalls
are not replaced by the non-reproducing probes. The next pressure correction
must establish useful continuation through recovery arrival, whose separate
1.5-second restart remains, with native validation and a bounded total episode.
Do not remove bodies from sweeps or credit enemy self-kill as escape. Then
collect the complete declared defended cohort before assigning gate credit.
Independent planned resource references remain the implementation package in
the R7 record. K07 and normal MP delivery stay open.

## Timing recollection after ComfyUI closed

Owner explicitly resumed timing qualification on September 8 local time.
Collection ran September 9 UTC, 02:42:15..02:46:04, on unchanged R8 sources
at `fcdf0ffd` using the frozen behavior build above. No Python/ComfyUI,
native test server or build processes were observed before or after collection.
CIM process detail was unavailable; this is not proof of an otherwise idle OS.
No other agent work, scans, compilation or outcome worlds ran during samples.

The declaration in `.codex-research/strafe-r8-cost-20260908/plan.json` fixes
four eight-bot candidate samples, one adapter-disabled control and one
sixteen-bot stress sample. Every world runs sequentially at native 40 Hz for
800 recorded movement frames. Corners 2, the R3 flight artifact and scoped
guard are unchanged; tracing, rendezvous, artificial holds and rollout caps
are off. Reports/hashing run only after each native process exits. Existing
cost-runner/report unit tests and the report self-test pass before collection.

| Sample | Movement p95 / max (ms) | Server frame p95 / max (ms) | Result |
|---|---|---|---|
| adapter-s1 | 0.2862 / 4.5674 | 0.7911 / 5.1732 | Eight-active-bot budget pass; at most three forecasts observed |
| adapter-s2 | 0.3405 / 5.0000 | 1.0024 / 5.3646 | Pass exactly at the maximum; six forecasts observed |
| adapter-s3 | 0.3550 / 5.5224 | 0.9838 / 5.8268 | Maximum fails; all eight alive/dispatched |
| adapter-s4 | 0.2860 / 4.8344 | 0.7829 / 5.2842 | Invalid eight-active workload: 225 dead-client frames, 224 missing-dispatch frames |
| control | 0.2919 / 0.3758 | 1.1541 / 1.8028 | Eight-active control pass; no forecasts |
| sixteen | 0.5426 / 5.2405 | 1.6946 / 6.3791 | All sixteen alive/dispatched; stress evidence, not an eight-bot pass |

The failing adapter-s3 frame at level 27700 charges 0.1156 ms dispatch and
5.4068 ms preparation, of which 5.3558 ms is forecast work. Native telemetry
identifies client 4, window 21, `entry_refused`, six forecasts/226 steps,
guard intact. Cold load is zero on that frame. This is the previously observed
six-forecast burst; with ComfyUI closed it still exceeds the five-millisecond
maximum. Do not retain the prior unmeasured assumption that host contention
alone resolves this gate. The exact source of remaining runtime variance is
not established by these samples.

Adapter-s4 client 1 dies by native `MOD_FALLING`; no fixture reset repairs the
lost workload. Its worst-cost frame already has seven living/dispatched clients.
It is retained as invalid, not counted as a third candidate pass. The falling
trajectory/controller cause is not established by this timing-only capture.

All six captures verify complete accounting, clocks, settings, staged module
and engine bindings, and every original source/manifest hash. `qualification.json`
binds the declaration, results and individual audits; 1102 source hashes still
match. All native worlds exited. Original K07 failures and all new samples are
retained; no replacement samples were selected to turn the result into a pass.

**Timing qualification is now measured and failed, no longer deferred.**
Next work must reduce or safely schedule the complete six-forecast preparation
within budget while preserving actual-root validation and movement quality,
and establish a valid active workload for the retained fall. Repeat the declared
isolated timing samples after relevant changes. K07's other gates remain open.
