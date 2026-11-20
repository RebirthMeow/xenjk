# Independent crossing speed and bounded join approach — 2026-09-13

Implemented and measured in the main task, without subagents. Nothing is installed;
installed baseline remains `reviewready-b` / `525f1b50`. This cycle resolves the
remaining speed-guard review concern and implements a resource-aware join approach.
It does not qualify K07, T5, full movement quality, defended play or runtime cost.

## Changes and contracts

`bot2_path.c` now tests a crossing's speed independently of lateral, height AND
contact failures. A missed height/contact window is not evidence that an overspeed
state can execute a safe continuation. Such a crossing is refused to the native
owner; existing within-speed stale-vertex recovery remains available. No speed
ceiling, arrival tolerance or resource threshold was raised. The previous rule
explicitly waived speed outside height/contact; this record supersedes that choice.

`ai_bot2_route.c` gives a nearly affordable return join one bounded approach per
return. It starts only on world ground, travelling toward a checked sample within
96 units, at ordinary travel speed, with Speed inactive and a receiver-reserve
refusal that at most four additional Force would resolve. That copied-state check
only permits approaching; the actual join still requires actual resources.

The approach keeps travelling in its entry direction at 40% of ordinary ground
speed, rather than aiming for a stopped arrival at the sample. Current geometry
to the sample AND the braking direction are checked each frame. It expires after
one second, aborts on invalid state, geometry or clock, and cannot restart on the
same return. The normal scan still uses its 128-unit radius and original launch
admission. Speed is cast by the existing lane owner after actual admission; no
cast is injected into the pure supported executor and no player resources change
outside native gameplay. The policy uses geometry and state, not map coordinates
or route-ID special cases.

## Retained development failure

The first approach aimed at the sample with diminishing target speed. Both R5
preflights joined, but each gained eleven below-50 u/s frames (275 ms) during the
approach. This is retained at `.codex-research/takeover-a3fa9a86cffa4f16b928d6bb3af25c63/preflight`, with source snapshots under
`approach-v1/` and its build in `v1-build-path.txt`. It was not promoted into the
scored candidate. The second preflight removed that stopping behavior.

## Frozen evidence and checks

- Evidence root: `.codex-research/takeover-a3fa9a86cffa4f16b928d6bb3af25c63`.
- Final build: `.codex-research/strafe-build-takeover-c1047a5fb3e3413b99db77e61fa820e7`.
- Baseline: `.codex-research/speed-guard-20260912/v1/candidate-r{1,2,3}`.
- Same twenty cases, three repetitions, assets, 55-second observation setting,
  timescale 5 and owner-level momentum tap. At most four isolated worlds; sources
  frozen throughout compilation and collection. No timing claim from these runs.
- `verification.json`: all 1104 source hashes and both binary hashes match.
- `strafe_path_release_check`, seam, corner tracking/span and supported-wait checks
  pass. `join-check5` covers bounded approach, rotated geometry, no resource grant,
  actual admission, timeout/no-repeat, insufficient Force, body refusal, airborne
  state, wrong refusal and travelling away; existing fast-return checks pass.
- `guard-counterfactual` compiles the prior source against the strengthened check
  and fails on the combined height/speed case. The reviewed wide/speed case and
  airborne/speed cases also remain in the current check.
- `temporary-body/summary.json`: 26/26; `resources/batch.json`: 30/30 complete,
  no contract release. These subsets retain their broader `milestone_pass=false`.
- `collect.py`, `score.py`, `compare.py`, `report.py`, `regressions.py` reproduce
  the recorded outputs in a NEW output directory. `totals_v2.py` corrects copied
  arm labels; the original `totals.json` is retained but its labels are obsolete.

## Results

All 60 worlds captured; no deaths, falls or lower-floor departures; health floor
90; rapid churn zero. All six scored approaches admitted at 70 Force after 800 ms,
with no aborted approach. Their minimum sampled speed was 67.9 u/s and none had
below-50 samples during the approach.

| First return | Baseline r1/r2/r3 (seconds) | Candidate r1/r2/r3 | Mean change |
|---|---|---|---|
| R5 red | 21.675 / 21.600 / 21.650 | 18.000 / 18.025 / 18.000 | -3.633 s |
| R5 blue | 17.850 / 21.700 / 21.725 | 18.000 / 18.050 / 18.000 | -2.408 s |

The previously fast blue repetition is 0.150 s slower. Across all twenty case
means the delta is -0.332 s, with thirteen lower means. Keep all cases; this is
not proof that the changes caused the small differences in unaffected cases.

Whole first-return diagnostics use the same 40 Hz definitions on both arms:
idle below 50 u/s; walking grounded 50..300 u/s. All 120 windows have complete
trace coverage. Of sixty paired observations, nineteen have worse time, six
worse idle fraction and nineteen worse walking fraction. These are diagnostics,
not the separately declared full-round-trip qualification. T5 remains 0/8 each
repetition and feasibility remains unresolved.

R5 walking time falls from approximately 8.2–8.45 s in previously missed joins to
3.275–3.4 s. Each candidate R5 first return contains one idle frame outside the
approach; five of its paired baselines had none. Do not turn the no-idle approach
result into a no-idle whole-return claim.

| Whole-world totals | Baseline r1/r2/r3 | Candidate r1/r2/r3 |
|---|---|---|
| Localnav frames | 35 / 47 / 46 | 35 / 82 / 49 |
| Stationary samples (below 20) | 47 / 32 / 40 | 60 / 83 / 44 |
| Zero-input samples | 157 / 144 / 120 | 208 / 240 / 204 |
| Contract releases | 6 / 6 / 6 | 6 / 7 / 6 |
| Branch entries open at end | 2 / 0 / 1 | 7 / 2 / 1 |

These totals cover later laps too, whose routes/ownership change after the earlier
join. They do not establish an overall idle improvement; retain open instances
as censored. The additional r2 R5-blue release occurs at level 49400: 435.847 u/s
against 390.957+40 at a crossing. The owner re-acquires at level 50400 and later
changes objective at 58950, with no death/fall. This exercises native speed
refusal/re-acquisition, but not the new combined height/contact rejection; that
specific decision is established by compiled cases, not a native coverage claim.

## Next work and corrected interpretation

The resource/radius mechanism is useful, but the older 1.2-unit / 5 ms margin
came from 250 ms interpolated positions with several units of uncertainty. It is
not a measured tolerance. A mechanism conditioned on position at affordability
does not rule out build or startup effects on that position. The current result
is a bounded repair measured on the declared cases, not proof of independence
from all start states or builds.

Next prioritize the retained lateral-miss descent family and actual-state
re-acquisition, then whole-trip idle/walking and defended qualification. Inspect
the new later-lap refusal before claiming broad recovery improvement. Keep the
launch reserves, T5 thresholds and T6 clause unchanged. A broader plan-owned
Force policy and automatic alternative-route preparation remain separate work;
no owner algorithm choice is required for ordinary bounded implementation.
