# Maneuver forecast reuse probe — 2026-09-06

## Question and scope

This experiment asks whether a previously selected maneuver forecast can be reused at later decision anchors without hiding divergence from the actual native player state. It is an opt-in fixture probe on one generated nominal map. It does not change production movement, the maneuver kernel, native physics, or the four-second horizon.

The protocol freezes twelve positive cases before observing outcomes. Every case starts at `(0, 0, 24.125)` with zero velocity and runs 160 native 25 ms steps. Decisions occur at an anchor and again 100 ms and 200 ms later. The optional Y impulse is applied only at the anchor.

| Start | Anchor | Anchor impulse Y |
|---|---:|---:|
| `ground` | 250 ms | 0 |
| `air` | 600 ms | 0 |
| `air-plus` | 600 ms | +100 |
| `air-minus` | 600 ms | -100 |

Each start runs under three predeclared arms:

- `retain` keeps the incumbent on a cache miss while building a diagnostic forecast from the actual state. It performs no candidate search on a miss.
- `search` evaluates all eight candidates at every decision from the actual native root.
- `reuse` reuses a valid cached suffix. An undisturbed case is expected to produce three hits and no extra search. A perturbed anchor is expected to miss, search from the actual root, and permit later hits if the replacement remains valid.

These arm definitions are fixture behavior to audit, not desired production policy. A failed arm and its raw evidence remain in the result set.

## Frozen inputs

The freeze records and snapshots:

- every source byte declared by the fresh build manifest;
- dedicated-server and game-DLL bytes;
- the runner, baseline maneuver evaluator, reuse evaluator, server helper, and this matrix tool;
- the nominal geometry JSON and BSP, including the encoded native configuration hash;
- all twelve case commands, ports, roots, velocities, anchors, impulses, and cache modes.

Execution is a separate action. It uses at most four isolated worlds and unique output directories. A launcher exception is retained as a failed case; it does not discard other results. Timing measurement is excluded and must run separately.

## Audit gates

The audit independently decodes the raw binary and requires canonical equality with `decoded.json`. It verifies staged executable, DLL, BSP, loaded-module path, build/source declarations, native geometry hash, actual initial root, and 160 commands at 25 ms.

Every positive case must pass all four existing movement parity gates and report no unsupported records. The reuse evaluator is rerun against the raw capture and its normalized result must equal the runner report. It must verify all three decision anchors, actual-state roots, entity-animation restoration, candidate forecast parity, and cache declarations.

Candidate-search calls and selected-forecast calls are recorded separately. The fixed upper bound is 5,120 candidate calls: the initial `8 × 160` samples plus at most three eight-candidate suffix searches. The bound is a fixture accounting gate, not a runtime-cost claim.

Expected cache behavior is checked per arm and start:

- `search`: eight candidates at every decision.
- `retain`: zero candidate-search work on a miss; its diagnostic selected forecast begins at the actual state.
- `reuse`, undisturbed: three cache hits and zero extra candidate-search work.
- `reuse`, perturbed: a miss and replacement search at the anchor, followed by later hits when the replacement validates.

Useful traversal outcome remains separate from evidence validity. Comparisons pair only cases with the same declared start and impulse. The audit does not require a reused trajectory to equal a forced-search trajectory and makes no live-world, performance, or production claim.

## Measured evidence

The evidence chain retains the failed audits and native outcomes that led to the corrected probe:

- The first audit of `.codex-research/strafe-maneuver-reuse-matrix-20260906` failed all cases because the matrix auditor compared the decoded integer cache mode with the case's string name. The corrected auditor preserved that failed artifact and exposed a real result: both undisturbed `reuse` cases missed at the first decision with reason bit 16, the saber/entity-context mismatch.
- The v2 content-mask guard removed that mismatch incompletely. Its stale input rejected otherwise valid reuse, so the v2 source, build, capture, and rejection remain failed evidence.
- The v3 guard derives its allowed queries directly from the actual Pmove root. The fingerprint includes entities intersecting the movement trace, solid/body, water and trigger masks; a pure saber hit volume is outside those ordinary-movement dependencies. Full contents and ownership remain hashed for every included entity. An unexpected trace-mask bit fails closed. `.codex-research/strafe-maneuver-reuse-matrix-v3-20260906` passed all twelve cases and all 1,920 native movement steps. The independent audit passed native decoding, source and binary provenance, actual roots, three decisions per case, candidate and selected-forecast parity, entity-animation restoration, cache accounting, and all four movement parity gates.

The v3 useful-exit outcomes were:

| Arm | Useful exits | Result detail |
|---|---:|---|
| `search` | 4/4 | All four declared starts reached the useful passage. |
| `reuse` | 4/4 | Undisturbed starts used cached work; ±100 starts searched after the anchor miss and then reused two suffixes. |
| `retain` | 2/4 | Ground and air completed. Both ±100 cases retained the incumbent after the miss and failed to reach the useful passage. |

The hash-bound metrics are in `.codex-research/strafe-maneuver-reuse-metrics-v3-20260906.json`. Against the matching forced-search case:

- Ground `retain` and `reuse` each saved 3,528 of 4,808 candidate calls and 441 of 601 selected-forecast calls, about 73.4% in both counters.
- Undisturbed air `retain` and `reuse` each saved 3,192 of 4,472 candidate calls and 399 of 559 selected-forecast calls, about 71.4%.
- Perturbed `reuse` saved 2,096 of 4,472 candidate calls and 262 of 559 selected-forecast calls, about 46.9%, while preserving 4/4 useful exits overall.
- Across the four `reuse` cases, candidate calls fell from 18,224 to 7,312 and selected-forecast calls from 2,278 to 914, a 59.9% reduction in each recorded work counter.

Fixed-window walking was unchanged between `reuse` and forced search in every paired case. This is not a speed improvement or identical-trajectory claim: undisturbed air exits at 2.825 seconds under reuse versus 2.800 under forced search. The two failed perturbed `retain` cases had 1.25 percentage points more walking over four seconds; their full failed episodes remain in the metrics artifact.

These counts measure deterministic forecast work, not elapsed CPU time. The separate [guarded search timing experiment](STRAFE_MOVEMENT_SEARCH_COST_2026-09-06.md) measures a 69.1 ms median without candidate export and leaves affordability open. The validated reuse key covers the exact native context fields and content mask exercised by this static nominal map. Dynamic-world reuse, approximate matching, trace failure beyond the fail-closed behavior, other geometry, and production integration remain unqualified.
