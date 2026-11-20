# Guarded maneuver search cost — 2026-09-06

This is a separate serialized timing diagnostic after the
[exact reuse probe](STRAFE_MOVEMENT_REUSE_PROBE_2026-09-06.md). It measures the
existing native research search with candidate file export suppressed. It does
not qualify a production planner or modify installed gameplay.

## Declared measurement

The frozen protocol is
`.codex-research/strafe-maneuver-search-cost-20260906/protocol.json`.
Five fresh processes run one at a time, using the same nominal generated
geometry, resting root `(0, 0, 24.125)`, 40 Hz and continuation scorer. Each first
exports the usual eight candidates, then repeats the same search from the same
untouched root without export. Thus the timed process is warm; this is not a
cold-start benchmark.

The timed span includes eight cloned native forecasts (1,280 Pmove steps),
scoring and ranking, state copies, full world/level restoration checks and
trajectory digest comparisons. It excludes file writes, logging, per-Pmove
clock calls, the selected-main forecast, live execution and process/map setup.
Reference search timing is not the reported compute measurement.

The quiet repeat must match every reference candidate's exact serialized
trajectory digest, score and validity. A separate private digest compares
animation, movement settings, context and contact outputs. The independent
Python auditor binds reference digests directly to raw exported bytes and
recomputes candidate scores through the existing evaluator. It also checks
the selected forecast and all four native movement parity gates, staged
DLL/executable/BSP bytes and source declarations. All five repetitions remain.

## Result

Build: `.codex-research/strafe-build-maneuver-search-cost-20260906`.
Passed audit:
`.codex-research/strafe-maneuver-search-cost-audit-v2-20260906.json`.

| Guarded search, five repetitions | Milliseconds |
|---|---:|
| Median | 69.0934 |
| Minimum | 66.5280 |
| Maximum | 77.5601 |

All forty quiet candidate trajectories match their exported references; each
run performs 1,280 separately accounted timing steps in addition to the normal
1,280 reference steps. All five select candidate 7, exit at 2.825 seconds,
and match actual movement (800 live steps total).

On the same final build, the full twelve-case reuse regression also passes
(1,920 steps) with every outcome and work count unchanged. Its audit and
comparison are `strafe-maneuver-reuse-cost-regression-audit-20260906.json` and
`strafe-maneuver-reuse-cost-regression-comparison-20260906.json` under
`.codex-research`. The prior v5 single-replan +100 airborne case passes another
160 steps at `strafe-maneuver-search-cost-v5-regression-20260906`. Thus the
final build has 2,880 matched live steps across v4, v5 and v6 captures.

The first audit falsely rejected all five roots because raw decoder vectors
were tuples and declared vectors were lists. That failed artifact remains at
`strafe-maneuver-search-cost-audit-20260906.json`. The corrected audit canonicalizes
decoded JSON before comparison and records both old and current auditor hashes.
Native recordings, timing values and acceptance conditions were unchanged.

This cost is unaffordable for per-frame live search. Removing export alone is
insufficient. These measurements do not isolate the fraction spent on full
world hashes, trajectory digests, copying or native physics. Next: profile those
components and design a bounded search implementation, using this guarded
fixture as the equivalence validator. Exact reuse remains useful but does not
close affordability, higher-speed movement or dynamic-world qualification.

## Reproduction and retention

`tools/strafe_maneuver_search_cost.py` provides `freeze`, `execute` and `audit`.
Freeze requires a fresh build, assets, geometry JSON and BSP. Execution uses
five declared ports and no parallel executor. Keep other native workloads and
builds idle during collection. Auditing requires the frozen evaluator bytes
to match the checkout; snapshots preserve those bytes for later restoration.
Only the auditor itself may evolve, with both hashes recorded. Output and audit
paths must be new. No installed binaries or native resources are substituted.
