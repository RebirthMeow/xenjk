# CHAIN2 native campaign: K01–K03, 2026-09-07

Protocol and evidence for the first native pickup-aware CHAIN2 lifecycle
qualification (K01), the complete successor-schedule measurement (K02) and the
frozen selection plus full-route movement quality gate (K03). The
[active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) owns the acceptance contract and
the [scorecard](STRAFE_MOVEMENT_PROGRESS.md) owns current status.

Everything here is **offline route quality on Kejim**. It establishes no runtime
cost, no defended reliability, no benchmark retention on this candidate and no
installed build. Normal MP still runs the older installed Runner_v2.

## Common bindings

| Input | Path |
|---|---|
| Native build | `.codex-research/strafe-build-window-seed-v2-20260907` |
| Build manifest | `20260907T011517681294Z.sources.json` in that build |
| Proposals | `.codex-research/strafe-window-seed-proposals-v1-20260907/proposals.json` |
| Seed evidence | `.codex-research/strafe-window-seed-v2-20260907/seed-report-v1.json` |

All 1,094 native sources matched the build manifest before K01, before K02 and
at the K02 snapshot. No C, header or CMake input changed during this campaign;
the only code changes were Python auditors and one new selector, recorded below.
Every collection used at most four isolated worlds on distinct ports.

## K01 — native CHAIN2 lifecycle, six-case frozen focus

Ran the retained, hash-validated declaration unchanged
(`.codex-research/strafe-chain2-focus-v1-20260907/campaign`, ports 31300–31305,
phase 0, 120 game seconds, force 0, no initial Speed). All six processes exited 0
and the resource audit passed 6/6 on the first attempt.

**Result: qualified, with one branch explicitly still uncovered.**

| Case | Stages | Evidence |
|---|---|---|
| `red-focus-mixed-p0` | outbound + return, both `LIVE_COMPLETE` | pickup 32475, capture 61250 |
| `blue-focus-mixed-p0` | outbound + return, both `LIVE_COMPLETE` | pickup 33175, capture 61400 |
| `red-focus-return-wait-p0` | return only, `LIVE_COMPLETE` | pickup 33025, capture 61575 |
| `blue-focus-missed-entry-return-p0` | `NO_ROOT` then `LIVE_COMPLETE` | pickup 32975, capture 61150 |
| `red-baseline-p0`, `blue-baseline-p0` | `INACTIVE_CONTROL` | complete trips, 57225 / 57050 ms |

Native log evidence for the three lifecycle questions the guide asked to confirm:

- **Leg ownership follows actual native pickup.** `[CHAIN] LEG from=0 to=1
  index=1 time=32500 psTime=32480 carrying=1 captures=0`, with the return stage
  rooting afterwards at 36875. Generation advances 2→3; no state replay or reset.
- **A return-only schedule waits through outbound travel.** In
  `red-focus-return-wait-p0` the stage armed at setup time 3775 and did not root
  until 37500 — it waited about 29 game seconds of ordinary outbound movement and
  the native pickup at 33050 before becoming eligible.
- **A miss does not poison later stages.** `blue-focus-missed-entry-return-p0`
  recorded `[CHAIN] MISS reason=passed_entry index=0 receiver=22 follower=22
  along=1332.33789 entryAlong=1277.56372 time=13725` and
  `DONE attempted=0 active=0 selected=-1`, then began stage 1 at the same
  millisecond and completed it normally.

Every joined stage reported `oneStepDifferences=0 trajectoryDifferences=0
commandDifferences=0`, so selected and resumed one-step parity is exact. Each
case ended with a single `[CHAIN] DONE ... ended=1`.

`verified_all_stage_chain` is false only for
`blue-focus-missed-entry-return-p0`, which is correct: its first stage
deliberately did not join.

### Retained gap: the native pickup-skip branch is still uncovered

The guide warned that passed-entry expiry might retire the `NO_ROOT` stage before
pickup. That is exactly what happened — the miss occurred at 13725 ms, roughly
19 game seconds before the pickup at ~33000 ms. **The native pickup-skip branch
was therefore not exercised.** Mocked coverage is retained; native coverage
remains explicit. Close this before K07 accepts the integrated scheduler, using a
minimal lifecycle test with a declared intervention. Do not alter seed
coordinates or invent a pose to conceal it.

### Auditor defect found and corrected

Two cases initially failed exactly one check, `native_map_checksum`. Full
diagnosis, proof and lineage are in
`.codex-research/strafe-chain2-focus-v1-20260907/campaign/AUDITOR-CORRECTION.md`.

Summary: the engine emits `sv_mapChecksum` **signed** (`codemp/server/sv_init.cpp:600`
uses `va("%i",checksum)`), and every run recorded `-1301924053`. The auditor
masked only the native operand, so declarations storing the same signed value
failed while masked legacy copies passed. `-1301924053 & 0xffffffff ==
2993043243`: one 32-bit value, two representations, one map.

The defect was in the auditor, not the caller and not a native branch. Retained
`chain-report-v1.json` proves this internally — the same run reports the
sign-safe `continuous_native_map_checksum` true and the sign-unsafe
`native_map_checksum` false. Corrections:

- New `same_map_checksum()` in `tools/strafe_connection_report.py` normalises both
  operands; both former comparison sites now use it. A real map difference still
  fails.
- `tools/strafe_window_seed_chain_report.py` now records `chain_auditor_binding`
  and `connection_auditor_binding`.
- Regression `test_map_checksum_comparison_is_sign_representation_agnostic`.

Failed report `chain-report-v1.json` is retained; `chain-report-v4.json` is
current. **10 of the 21 K02 schedules — including all nine blue schedules — would
have failed this check spuriously.** With no valid blue candidate, K03 would have
been forced to the inactive fallback and reported a failed improvement decision,
sending K04 after a controller blocker that does not exist.

## K02 — all 21 successor schedules plus two inactive controls

`.codex-research/strafe-chain2-development-b754d07ed53f4033902165e3d3c6f2ca`,
ports 31400–31422, phase 0, four workers. All 23 processes exited 0; the resource
audit passed 23/23 and the chain audit reported `evidence_valid: true` on the
first attempt.

- Stage outcomes across the 21 schedules: **123 `LIVE_COMPLETE`, 3 `NO_ROOT`** of
  126 declared stages. 19 of 21 schedules completed all six stages.
- Selected candidate kinds: 120 flight, 2 point, 1 ordinary supported.
- Best complete first trips against the matched inactive control:
  red `red-window-primary` 49500 ms versus 57225 ms (**13.50%**);
  blue `blue-window-primary` 49775 ms versus 57050 ms (**12.75%**).
- Every candidate improved idle and walking fractions against its control.

`source-snapshot/snapshot.json` in that directory records the 1,094 native inputs
(zero drift), the unchanged frozen `bot2_strafe.c`, unchanged installed DLL
hashes, and the helper drift for this package: four changed auditors and two new
selector files.

## K03 — frozen selection and the unchanged 12-case gate

`tools/strafe_window_seed_select.py` is new and implements the guide's contract:
it requires valid full K02 resource and chain evidence bound to the exact 23-case
plan, ranks complete first-trip time per team including the inactive control,
retains every rank, and writes schema `strafe-chain-development-selection-v1`
with the `next_gate` contract copied unchanged. Declared tie rule: ascending
first-trip milliseconds, then ascending case name. Incomplete trips are retained
last and never selected. If a team's inactive control wins it emits
`INACTIVE_CONTROL_FASTEST_RETURN_TO_K04` and selects nothing rather than
fabricating a schedule. `tools/test_strafe_window_seed_select.py` covers refusal
handling, the inactive-fallback path, incomplete trips and selection from
reordered valid inputs.

Selection was frozen before collection:
`selected = {red: red-window-primary, blue: blue-window-primary}`.

Gate collection:
`.codex-research/strafe-chain2-phasegate-6df2e0f20b5748baaa692bc683cdd1a8`,
ports 31500–31511, phases 0/3/6, both teams, 12 cases. All exited 0; resource
audit 12/12; chain audit valid; 34 `LIVE_COMPLETE` and 2 `NO_ROOT` stages.

**Result: the full-route movement quality gate passes for the first time.**

| Team | Baseline median | Candidate median | Improvement | Gate |
|---|---|---|---|---|
| red | 57275 ms | 51125 ms | **10.74%** | >=10% |
| blue | 57050 ms | 49800 ms | **12.71%** | >=10% |

`all_pairs_not_slower`, `all_idle_not_worse` and `all_walking_not_worse` are all
true, so the complete locomotion gate is met, not only the tool's time-only
`passed` field. All six pairs individually:

| Team | Phase | Baseline ms | Candidate ms | Idle ms | Walking ms | Airborne ms | Airborne speed |
|---|---|---|---|---|---|---|---|
| red | 0 | 57225 | 49500 | 2475 → 1800 | 33425 → 23000 | 20975 → 22450 | 344 → 371 |
| red | 3 | 57375 | 51425 | 2325 → 1950 | 33700 → 25375 | 21025 → 22550 | 342 → 364 |
| red | 6 | 57275 | 51125 | 2275 → 1875 | 33650 → 24950 | 21000 → 22450 | 343 → 363 |
| blue | 0 | 57050 | 49775 | 2450 → 1850 | 32575 → 23175 | 21650 → 22575 | 343 → 368 |
| blue | 3 | 57200 | 49800 | 2525 → 1800 | 33325 → 23250 | 21000 → 22450 | 344 → 369 |
| blue | 6 | 56975 | 49825 | 2300 → 1875 | 33275 → 23325 | 21025 → 22450 | 346 → 370 |

The improvement is physically coherent rather than a metric artifact: ground
walking falls by roughly 10 seconds per trip and is converted into slightly more
airborne time at a higher mean airborne speed, over a **shorter** xy distance
(about 14688 → 14360 units). First pickup also improves (about 28600 → 25000 ms),
so the outbound leg improves too, not only the return.

For comparison, the previous prepared-takeoff phase gate reached 5.76% red and
4.47% blue against the same 10% requirement.

## Scope and what this does not establish

- Offline route quality only. No runtime cost, defended continuity, M0/M1/M2
  retention on this candidate, resource matrix, or installation is claimed.
- CHAIN2 remains the discovery oracle. The expensive 17-candidate search must not
  be promoted into normal MP; K05 owns the bounded runtime planner.
- The native pickup-skip lifecycle branch remains uncovered, as recorded above.
- `qualified` is false in the selection artifact and
  `qualified_fast_trip` is false in every chain report; these are development and
  gate evidence, not milestone completion.

## Known related hazard, not corrected

Two map-checksum sign conventions coexist across the tree. Several comparison
sites normalise neither operand and are safe today only because each lineage is
internally consistent — for example `tools/strafe_connection_report.py:967`,
`tools/strafe_window_inventory.py:192`, `tools/strafe_map_probe.py:200`, string
equality in `tools/strafe_disturbance_runner.py:65`, and hardcoded signed
literals in `tools/strafe_cost_report.py:84` and
`tools/strafe_continuous_report.py:37`. `codemp/game/bot2_movebench.c:296-299`
is the one site that accepts either representation and documents the hazard.
These were left alone under "fix only the responsible layer"; they are recorded
so a future crossing of lineages is diagnosed quickly rather than rediscovered.
