# Sequence planning: decision evidence and native Speed scheduling

The owner authorized execution of the broad movement direction after the
[walking inventory](STRAFE_MOVEMENT_WALKING_INVENTORY_2026-09-14.md). This first
increment records actual executor decisions and plans a native Speed purchase
across multiple prepared intervals. It improves the measured full-resource
walking cases. It does **not** implement the proposed general connected-hop
planner or finish baseline movement qualification. No installed game changed.

## Implemented behavior

`Bot2_PathCommandDetailed` reports the actual follower index/goal, ground and
target speed, hop permission/room, speed and latch gates, jump request and final
speed constraint. Existing callers retain the same command API through a
wrapper. Recorded path/follower layouts are unchanged. A differential Win32
check compared 40,000 commands and follower states against preserved
pre-telemetry kernels; all matched.

`Bot2_HandoffSpeedGain` examines up to 2,000 units of the admitted continuous
path. Explicit ground intervals contribute estimated time saved at native
Speed's ground target, bounded by their existing speed limits. Hop intervals
contribute distance but no promised saving. The scan stops at a separate pass
or non-crossing endpoint. At least 500 units of useful ground travel and an
estimated 0.75-second saving are required. This is a bounded estimate, not a
native trajectory forecast or a global resource optimizer.

After the supported command and native body check succeed, the outbound owner
may request level-3 Speed from a full 100-point pool. Existing powers, another
generic command, a jump request, and foreign adapter ownership prevent the
purchase. Native accounting spends 50; there is no direct resource edit,
cancellation at interval boundaries, or waiting for regeneration. Actual
pickup Force in the extended candidate trips was 50, versus 100 in controls.
Return controllers retain their resource admission rules.

The planner uses no map name, coordinates, route identity or fixture identity.
It changes neither arc/hop eligibility nor clearance, pass, braking and
actual-state handoff contracts. The old `bot_supportedGroundSpeed` straight-only
experiment remains off. This new schedule runs in the admitted supported owner.

## Direct decision evidence

The diagnostic baseline has eight completed alive worlds, zero audited
prolonged confinement, and 157.300 seconds of scored walking. Its start phases
differ from the earlier inventory's recordings; the earlier 155.500-second
score is not overwritten.

The new analyzer joins walking CSV intervals to same-frame decision records,
requires supported ownership and a continuing result, and excludes released,
yielded, missing or ambiguous decisions. Its left-sample durations reconcile
exactly with the existing walking audit:

| Associated decision | Scored walking seconds |
|---|---:|
| Incoming interval has no hop permission | 69.800 |
| Hop permission, insufficient continuous room | 57.500 |
| Pass contract | 3.500 |
| Jump command at sampled ground contact | 2.250 |
| Runup speed | 0.250 |
| Unmatched or yielded | 13.500 |
| Other movement owners | 10.500 |

These are sampled associations to actual decisions, not continuous causal
replay between CSV observations. `PATHDECISION` precedes final owner refusal;
the record alone is not proof that the command reached Pmove. Non-continuous
commands without detailed fields remain unmatched. The supported ground target
and lack of a useful hop span dominate; a stuck jump latch does not explain
the walking set.

## Fixed eight-case comparison

Cases, resources, seeds, settings, fixture payloads and rates are unchanged:
four normal native spawns at 1x/60 game seconds and four original zero-resource
return fixtures at 5x/55 game seconds. No mid-run intervention was added.

| Case | Baseline walking s | Candidate walking s | Baseline idle s | Candidate idle s |
|---|---:|---:|---:|---:|
| spawn-red-r1 | 22.650 | 11.900 | 1.000 | 1.250 |
| spawn-blue-r1 | 19.875 | 12.750 | 1.000 | 1.000 |
| spawn-red-r2 | 21.325 | 11.100 | 1.500 | 1.250 |
| spawn-blue-r2 | 22.400 | 11.075 | 1.250 | 1.250 |
| zero-red-r1 | 17.575 | 17.075 | 1.250 | 1.250 |
| zero-blue-r1 | 18.100 | 17.150 | 1.250 | 1.500 |
| zero-red-r2 | 17.575 | 17.025 | 1.250 | 1.250 |
| zero-blue-r2 | 17.800 | 17.150 | 1.250 | 1.500 |

Both arms capture alive 8/8 with zero audited confinement. Ordinary-spawn
walking totals 86.250 → 46.825 seconds (45.7% less), with combined idle unchanged
at 4.750 seconds. Across all eight cases walking is 157.300 → 115.225 seconds;
idle is 9.750 → 10.250 seconds. The zero-resource improvement is small and idle
is mixed; this does not resolve depleted movement.

The first Speed purchase in normal spawns occurs after the first capture.
First-trip timing differences therefore cannot be credited to this planner.
The full-Force red flat section has 4.250 seconds of sampled walking in both
diagnostic repetitions. Candidate traversal between the same observed spatial
gates is 2.500 seconds with no sampled walking, at Force 50 with Speed active.
These CSV gate measurements have ordinary 250 ms boundary uncertainty; the
gates appear only in evidence analysis, never in production logic.

## Complete trips after the purchase

Four baseline and four candidate worlds were extended to 90 game seconds.
The baseline runner executes against its preserved native source snapshot;
source checks were not bypassed. Trip 2 is measured from the first capture
event to the second capture event, covering outbound, pickup and return.

| Case | Baseline trip 2 s | Candidate trip 2 s | Baseline walking s | Candidate walking s |
|---|---:|---:|---:|---:|
| red-r1 | 36.175 | 35.900 | 15.675 | 8.400 |
| blue-r1 | 36.300 | 34.850 | 15.125 | 7.500 |
| red-r2 | 39.200 | 34.500 | 15.900 | 7.700 |
| blue-r2 | 39.150 | 36.450 | 15.400 | 8.700 |

All eight extended worlds capture twice alive and have zero audited
confinement. Mean trip-2 time is 37.706 → 35.425 seconds; walking totals
62.100 → 32.300 seconds. Trip-2 idle totals 3.500 → 3.750 seconds. All four
measured trip durations improve, but initial/private movement-clock states are
not replayed exactly. In particular, the 275 ms red-r1 improvement is small
relative to observed phase variation. Report the paired outcomes without
claiming exact causal timing gains or universal superiority.

## Rejected straight-continuation experiment

A separate prototype proposed one long straight chord across several prepared
intervals, with native standing support, tall-body hop clearance, hazard
checks and fresh actual-state admission. Its first version proposed nothing:
it incorrectly excluded straight endpoints carrying the next arc's lookahead
radius. Correcting that predicate produced native geometry refusals, not
commits (27 in the complete repeated eight-case cohort). The prototype was
removed from active execution; no thresholds or clearance gates were weakened
to make it pass.

One intermediate cohort retained four normal-spawn creation-acknowledgement
failures and four completed fixture worlds. A fresh complete eight-world
repetition is retained separately. These setup failures are not movement
passes. Both prototype source snapshots and controlled query tests remain in
the study; they are not the next implementation queue.

## Evidence and verification

Study: `.codex-research/movement-sequences-20260914-a/`.

- `trace/`, `speed-a/`, `comparison-speed-a.json`: primary unchanged cohort.
- `trace-decisions-v2.json`, `speed-a-decisions-v2.json`: exact walking
  reconciliation and per-interval decision references. Earlier outputs retained.
- `flat-ground-check-v2.json`: red full-resource section observations.
- `trace-extended/`, `speed-extended/`, `extended-comparison.json`: whole trips.
- `verification.json`: all twelve final case pairs have equal declarations
  apart from build/runtime bindings, equal movement settings, and verified
  runner/flight payloads. Scorers verify all native source, binary and fixture
  hashes; current candidate has 1,107 native inputs.
- `diagnostic-equivalence/`: 40,000 old/new kernel equivalence cases.
- `speed-kernel-a/`, `speed-owner-b/`: real path/handoff contracts, rotated and
  mirrored horizon checks, stop/pass/limit/air/frontier refusal, and purchase
  exclusion for lower resources, wrong level, return ownership or active power.
- `sequence-a-source/`, `sequence-b-source/`, `sequence-*` cohorts and checks:
  rejected chord implementation and all failures.

Current native build:
`.codex-research/strafe-build-speed-sequence-20260914-a`, manifest
`20260914T081651702425Z.sources.json`.
Diagnostic control:
`.codex-research/strafe-build-sequence-trace-20260914-a`.

The rejected query test was archived from
`tools/strafe_sequence_clearance_check.py` to
`.codex-archive/20260914T082100Z/tools/strafe_sequence_clearance_check.py`;
the parent archive manifest records its original absolute path. Restore by
moving it back to that original path when unused. Its matching prototype
source must also be selected to run that historical test.

## Next action and limits

Keep this measured shared Speed schedule and decision telemetry. The next
capability is a **resource-independent connected hop/landing continuation** for
the remaining short-run and grounded-corner decisions. Start with a repeated
low-resource ordinary-route case, bind actual takeoff and receiving velocity,
and use native rollout checks across multiple intervals. Preserve the complete
trip score, body/refusal cases and all failures; do not reopen coordinate
patches, the failed chord search or straight-only Speed toggling.

Broader phase/body/defended and cost qualification remain open, as do general
map transfer and low-resource maneuver planning. No T5, full movement
acceptance, dedicated-thread benefit, installation or combat claim follows
from this increment. Dropped-flag retrieval remains deferred.
