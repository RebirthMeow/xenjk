# Empirical maneuver coverage probe — 2026-09-06

## Question and scope

This experiment asks whether a small empirical coverage table, baked from full native searches on one known static map, can select at most two terminal-compatible candidate prefixes for unseen entry roots. Selection is evaluated in shadow after observing only the first twelve candidate frames. The suffix is scored only after the selection decision. Every world still runs the ordinary full-eight native search, so this probe does not change live movement or measure runtime savings.

The native library-contact build, nominal two-gap geometry, BSP, physics, maneuver kernel, and runner remain unchanged. The campaign introduces only offline evidence collection and coverage analysis.

## Frozen cases

Training contains 27 roots from the Cartesian product:

- `x ∈ {-24, 0, 24}`
- `y ∈ {-16, 0, 16}`
- `vy ∈ {-50, 0, 50}`
- `z=24.125`, `vx=0`, `yaw=0`

All training roots run a four-second, 40 Hz full-eight continuation search. The baker receives all independently audited training directories, including failed outcomes. Each empirical cell is formed from its eight corner observations and records successful candidates ranked by worst useful-exit finish, then mean finish, then candidate ID. It stores at most two prefixes. Runtime shadow selection chooses the first prefix whose terminal contract is compatible with the observed twelve-frame prefix.

Evaluation contains 16 unseen roots from:

- `x ∈ {-18, -6, 6, 18}`
- `y ∈ {-8, 8}`
- `vy ∈ {-25, 25}`
- `z=24.125`, `vx=0`, `yaw=0`

Four refusal controls sit outside one declared coverage bound at a time: `x=-25`, `x=25`, `y=-17`, and `vy=51`, with all unspecified coordinates and velocities zero. These controls test selector refusal only. There is no live shortlist mode in this experiment.

## Stages and isolation

1. `freeze` snapshots the build manifest, every declared source byte, executable and DLL, geometry and BSP, runner, server helper, maneuver evaluator, library baker, coverage helper, campaign tool, protocol, and all 63 training, evaluation, refusal-control, and replay declarations before native collection.
2. `train` runs the 27 full-search training worlds, retaining every output and launcher failure.
3. `bake` independently audits every training raw capture, invokes the frozen coverage helper, and records the coverage JSON and encoded-byte hashes before any unseen evaluation directory exists.
4. `evaluate` verifies the frozen bake and runs the 16 unseen full-search worlds. The four outside controls are evaluated from declared roots without launching native worlds because the selector must refuse before a live shortlist exists.
5. `select` freezes each shadow decision from the first twelve candidate frames, then creates a case-specific singleton library by replacing every original library entry's candidate list with that selected ID. Refused decisions create no replay library.
6. `replay` launches a fresh world at the identical root for every accepted decision. The existing library runtime performs its full 160-step native validation before live movement. Replay cannot feed back into the already frozen shadow decision.
7. `audit` independently rereads all native binaries, verifies canonical saved decoding, staged executable/DLL/BSP, loaded module, source hashes, exact actual roots, 160 steps at 25 ms, and all four movement parity gates. It reruns the frozen coverage selector, requires exact equality with the saved decisions, and audits each replay with the existing library evaluator.

Native outcome worlds use unique ports and homepaths with at most four active worlds. Timing experiments are excluded. Every action refuses to overwrite prior evidence.

## Outcome contract

Training and unseen evaluation evidence must reproduce the full native continuation search and its eight candidates. Useful traversal remains an outcome field rather than an evidence-validity filter.

For each unseen root, the shadow selector must either accept a terminal-compatible prefix based only on the first twelve candidate frames or report a structured refusal. The audit separately reports whether the selected candidate's withheld suffix reaches a useful exit, whether it introduces invalid support, and how its full four-second chosen trajectory compares with the native full-search choice for walking, idle, airborne time, horizontal distance, and finish time. A fresh replay then establishes actual live execution only after the unchanged full 160-step native validation, and binds the native selected ID to the saved shadow choice. All-candidate failures and selector refusals remain visible.

The initial hypothesis is that all 16 unseen roots accept a proposal whose withheld suffix is a useful continuation with no new invalid support. The four outside controls must refuse. A failed hypothesis does not invalidate correctly bound native evidence and is not removed from the campaign.

This probe measures empirical coverage on one nominal map. Movement summaries cover fixed four-second episodes and report idle/walking fractions and longest spans. They cannot test the five-second confinement gate and cannot establish absence of stuck behavior. The Python twelve-frame selector remains a shadow proxy; its count is at most 24 prefix pmoves and is not a CPU percentage. Replay still performs the full native lookahead and does not establish a shortened native path. The probe does not establish pass cost, CPU savings, dynamic-obstacle handling, affordability, map-general coverage, a final connector, or production integration.

## Completed evidence

`.codex-research/strafe-coverage-campaign-20260906` and
`.codex-research/strafe-coverage-campaign-audit-20260906.json` pass this declared
shadow/replay experiment on the unchanged library-contact build. All 27 training
worlds, 16 unseen full-search worlds and 16 subsequent selected-policy replays
pass the movement parity checks: 9,440 live native steps. All 16 shadow choices
and replays satisfy the useful-exit contract; all four outside controls refuse.
Nine finishes match full search and seven are 25 ms slower. Fixed-window walking
rises 0.2734375 percentage points and idle falls 0.1953125 points.

Training retains all 216 candidate outcomes: 202 useful, 14 failures. Candidates
0–5 pass all 27 sampled roots; candidate 6 passes 25 and candidate 7 passes 15.
The eight compiled cells exclude candidate 7 entirely, and retain candidate 6
in five cells. Preparation took 58.1923 seconds with up to four worlds, followed
by 3.0096 seconds for the library and 3.4807 seconds for coverage compilation.
The provenance-rich coverage JSON is 256,854 bytes, SHA-256
`18f5a4688c17526435f2a7e0e22bcdaf36ff6881e9b4aab41c3bbce1b27ca269`.

This pass did **not** establish an acceptance region. The subsequent
[native experiment on new roots](STRAFE_MOVEMENT_COVERAGE_NATIVE_2026-09-06.md)
found both refusals of useful proposals and an accepted clearance violation.
Its negative result supersedes any suggestion that corner envelopes alone can
replace full continuation validation.
