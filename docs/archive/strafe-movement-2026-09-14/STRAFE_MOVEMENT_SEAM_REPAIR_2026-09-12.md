# Checked seam continuation — 2026-09-12

## Change and scope

The supported compiler now sends resolved shallow turns through the existing
pass-validation path. Its outward-vector cutoff had rejected approximately
0.20-degree seams even though the direction was numerically resolved. The
straight-angle, speed, short-leg and elevation filters remain. Every admitted
proposal still requires four floor probes and five inflated-hull sweeps, plus
hurt-volume rejection, before it becomes a crossing.

A checked pass now receives a local approach point on the already prepared
incoming line. The long upstream interval retains its original hop eligibility;
the final approach retains ground-only pass, speed and lateral/rejoin gates.
The inner approach carries the pass radius to bound lookahead. An additional
ordinary boundary 65 units upstream keeps the normal steering horizon along
the long incoming straight; before that boundary the 64-unit lookahead cannot
reach the inner approach, let alone the bend. It interpolates the
original line's Z, lies ahead of the last emitted curve point, and is omitted
if it would shorten a previous pass's checked rejoin span. Capacity is reserved
before the added boundaries and pass vertex are emitted. No shared field changes meaning and the
path executor and fast-return corridor guard are unchanged.

This is a local traversal correction, not a T5 or defended qualification claim.
Current route assets and all native resource/lifecycle rules remain in use.
Nothing is installed.

## Retained first attempt

The first build lowered only the degeneracy cutoff. All four preflight cases
captured and the red seam minimum rose from roughly 45 to 250 u/s, but red return
times worsened to 20.100 and 19.350 seconds. The newly published pass disabled
hopping over the entire 1,534-unit incoming interval. That revealed a second
metadata-boundary defect. The local approach split above corrects it.

First build: `.codex-research/strafe-build-seam-671d0557286a4802909c4422505e8768`.
Retained four-world preflight: `.codex-research/seam-fix-20260912/preflight`.
Its sources remain at `ai_bot2_supported.v1.c`; the original corridor sources
remain at `ai_bot2_supported.before.c`. These are not scored as the final arm.

## Retained second attempt

One approach point restored upstream hop eligibility but applied the pass radius
to the whole incoming interval. On R2-red, the trajectory matched baseline until
10.775 seconds after pickup, then lost airborne acceleration: at 12.375 seconds,
670 u/s baseline versus 537. The seam recovered about 0.250 seconds of the 0.475
upstream lag, leaving a 0.225-second longer return. Position-crossing times use
interpolated play samples, not native gate timestamps. This motivated the outer
ordinary boundary in the final revision.

Second build: `.codex-research/strafe-build-seam2-36cfc390740c40a2be4ca43e9cb9da3d`.
Its full three repetitions remain in `seam-fix-20260912/v2`: 60/60 captured,
no lower-floor departure, localnav 5/5/6, stationary 54/66/58, T5 still 0/8.
Sources remain at `ai_bot2_supported.v2.c`. No body/resource qualification is
inherited from this superseded build.

## Frozen final comparison

- Final build: `.codex-research/strafe-build-seam3-53a5fcfaf4964c438257a4419b667c9c`.
- Declaration, drivers, worlds and audits: `.codex-research/seam-fix-20260912/v3`.
- Baseline: `.codex-research/corridor-fix-20260912/candidate-r{1,2,3}`.
- Unchanged thirty-route assets: `.codex-research/pkgD-step3-20260911/assets`.
- Same twenty cases, three repetitions, timescale 5, 55-second observation,
  at most four isolated native worlds. Sources frozen during build/collection.
- Revised four-case preflight: all captured, no lower-floor departure. It is
  separate from the three complete repetitions.
- Seam diagnostic: supported-owned first-return samples within 64 XY units of
  canonical `(462,-107)`, with blue mirrored using the existing join. It is a
  fixed local diagnostic, not a new route-wide acceptance threshold.

## Checks

`strafe_seam_check.py` extracts production proposal and validation functions.
It covers shallow mirrored turns, each of the nine floor/hull rejection points,
hurt volumes, short legs, elevation, exact straight and larger/excessive turns.
It fails on the original degeneracy rule, at the shallow-turn proposal assertion.
Mocked traces establish gate execution, not actual native clearance.

`strafe_supported_wait_check.py` checks local run-up publication, preserved hop
metadata/radius, previous-pass rejoin protection and preceding-curve bounds.
`strafe_fast_return_check.py` protects the prior corridor/branch/tap contracts.
Final regression results: 26/26 temporary-body cases passed; 30/30 resource
cases completed without refusal. These subsets retain `milestone_pass=false`
for their broader gates. Pure seam, supported-publication and fast-return checks
pass; syntax and documentation-link checks pass. The final build source hashes
match the checkout (`v3/verification.json`). No measured source changed during
compilation or collection.

## Results on the final build

| Twenty-world totals | Corridor baseline r1/r2/r3 | Seam v3 r1/r2/r3 |
|---|---|---|
| Captured worlds | 20/20/20 | 20/20/20 |
| Deaths / lower-floor departures | 0/0/0 | 0/0/0 |
| Minimum sampled z | 1808.004/1808.004/1808.001 | 1808.004/1808.004/1808.005 |
| Localnav frames | 4/3/5 | 6/4/4 |
| Stationary samples | 58/48/49 | 52/66/65 |
| Zero-input samples | 118/82/102 | 138/119/123 |
| All-reason rapid churn | 0/0/0 | 0/0/0 |
| Branch entries | 46/42/40 | 42/44/50 |
| Completed branch exits | 39/39/37 | 39/37/40 |
| Branch abandonment | 0/0/0 | 0/0/0 |
| Entries open at world end | 7/3/3 | 3/7/10 |
| T5 met / declared denominator | 0/8 each | 0/8 each |

The six red R2/R7f12 first returns cross the seam at minimum 250.0–250.4 u/s,
versus 45.3–49.8 before. Their 0.300–0.325 seconds below 150 u/s in the declared
seam neighborhood disappear in all repetitions. This is the specific defect
resolved. Blue did not have the same stopped seam; one R2-blue repetition has
a local minimum 185.9 versus 251.1 baseline, so no universal turn-speed gain is
claimed for the generic compiler change.

| Exact-12 first return | Baseline seconds r1/r2/r3 | Seam v3 seconds r1/r2/r3 |
|---|---|---|
| R2 red | 19.150/19.100/19.050 | 19.050/19.000/19.000 |
| R2 blue | 18.975/18.975/18.975 | 18.900/18.875/18.900 |
| R7f12 red | 18.650/18.650/18.650 | 18.250/18.250/18.975 |
| R7f12 blue | 18.200/18.250/18.075 | 18.000/18.150/18.225 |

Travel results remain mixed: R7f12-red is faster in two repetitions and slower
in one; blue R7f12's ranges overlap. All remain above 16 seconds. Stationary and
zero-input totals do not establish a global improvement. Do not promote the
local stop removal into a T5, T4 or package pass.

Health floor remains 90. The prior corridor repair and authored-tap admission
hold: no lower-floor departure or branch abandonment, zero low-jump resource
refusals among 32/32/30 gradable releases, and no unmatched resource releases.
Open final branch instances remain censored, not successful transitions.

Outputs reproduce from `v3/compare.py`, `v3/score*.py` and `seam_measure.py`;
per-case evidence is in `comparison.json`, `candidate-r*-seam.json`, windows,
falls and losses. The first diagnostic used the existing corridor tapes;
all three candidate repetitions use the unchanged declared twenty-case set.

## Remaining work

Next inspect and repair the ramp-entry stopped transitions identified by the
[T5 phase ledger](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md), then establish whether
checked hop room is unnecessarily fragmented on the remaining ground approach.
Use retained traces to pick the exact interval; probe native floor/hull geometry
before granting through-travel or hopping. Preserve the local seam boundary,
branch guard, resource gates and explicit censored attempts. Current T5,
T2/T4 and defended T6 obligations remain open. Nothing is installed.

