# T5 return-phase attribution — 2026-09-12

## Result and next implementation

T5's post-expiry loss is primarily supported traversal, not preparation or final
stand braking. The next bounded correction is a **validated near-collinear
continuation**: eliminate the tiny kink that falls between the corner compiler's
and path executor's contracts. Then address the ramp-entry stop contracts and
checked hop continuity. Do not increase turn speed or relax crossing admission
without floor, hull and actual-state validation.

This analysis does not repair T5. Current exact-12 returns still take
18.075–19.150 seconds against 16, and the latest eight declared T5 cases remain
0/8 in each repetition. Even erasing the observed interior below-150 intervals
entirely would remove only 0.300–0.975 seconds; that is not enough to earn T5.

## Ledger and evidence contract

`tools/strafe_return_phases.py` partitions all 606 retained return episodes from
240 worlds: twenty cases, three repetitions each of frozen control, step-4
candidate, tapfix3 and corridor repair. Every observed millisecond reconciles:
8,872,050 ms total, with no trace-gap time in these inputs. Failed and censored
episodes remain in the output. First seeded episodes are summarized separately;
later healthy returns are not pooled into the depleted comparison.

Intervals are half-open, clipped to the existing episode boundaries and held
for at most one 25 ms command. Missing leading, internal or trailing telemetry
would be an explicit unclassified interval. Speed integral is estimated XY
travel distance, not route progress or a simulated achievable trajectory.
Owner, contact and resource state are separate measured dimensions. The
post-expiry interval follows the last active-Speed sample, matching the existing
T5 definition; an earlier inactive gap before reactivation stays separate.

Supported phase priority is preparation, unknown readiness, unlocated position,
objective proximity, published-curve proximity, then other ready traversal.
Positions use the existing mirrored Hermite join; its residuals are retained.
The 256-unit objective neighborhood and 64-unit curve neighborhood are diagnostic
labels, not acceptance changes. **Proximity does not prove maneuver execution**:
the red stopped vertex is near an accepted pass, and part of its time lands in
`published_curve_near`. Event/node inspection supplies the mechanism below.

Current outputs: `.codex-research/t5-phases-20260912/v2/`, with per-interval ledgers,
`manifest-v2.json`, `summary.json` and `corrected-window-summaries.json`.
The top-level first pass is retained: it inferred readiness from absence of
preparation; v2 requires explicit ready state. An initial declaration used the
wrong control directory and stopped before collection; its manifest remains.
No native worlds were collected and no C/C++ source or installed game changed.

## Current exact-12 results, three repetitions per case

| First seeded episode | Capture seconds | Post-expiry seconds | Post-expiry mean u/s | Interior time below 150 u/s |
|---|---|---|---|---|
| R2 red | 19.050–19.150 | 5.775–5.875 | 216.8–217.9 | 0.975 s |
| R2 blue | 18.975 | 5.700 | 236.5 | 0.325 s |
| R7f12 red | 18.650 | 5.375 | 214.6–215.5 | 0.900–0.925 s |
| R7f12 blue | 18.075–18.250 | 4.800–4.975 | 232.7–233.9 | 0.300–0.325 s |

Every post-expiry interval of those twelve episodes is owned by `supported`.
Preparation contributes zero post-expiry milliseconds. Ground contact accounts
for 3.775–4.850 seconds. The final 256-unit neighborhood takes 0.950 seconds per
case, of which only 0.075–0.100 seconds is below 150 u/s. It cannot explain the
interior stop clusters or all the missing travel speed.

Control and both intervening candidates retain the same interior slowdown
pattern. The comparable ledger is present for every case/repetition; this is
not evidence that the branch repair introduced the T5 failure. Zero-Force R4
returns on the current build take 21.500–22.250 seconds; their preparation-owned
intervals last 0.975–2.275 seconds while moving at mean 256.5–400.8 u/s. Preparation
ownership should not be counted as stationary waiting.

The integral of positive speed shortfall below 300 u/s over the current exact-12
post-expiry interval is 1.105–1.634 seconds, mostly before the objective zone.
This is a kinematic diagnostic, not recoverable time: ramps, turns and landing
constraints may legitimately require lower speed. A feasible continuous path
must be measured before claiming that budget can be recovered.

## Specific mechanism and ranked work

1. **Near-collinear stop contract.** On red, node 43 `(462,-107)` changes heading
   by about 0.20 degrees before node 44 `(458.25,-140.75)`. The corner proposal
   refuses its 33.96-unit exit leg because the trim is below 16. The pass proposal
   refuses normalized direction difference below `.01` as degenerate. The path
   executor's much tighter collinearity test does not allow through-travel, so
   the vertex becomes an arrival below 80 u/s immediately before an accepted
   425 u/s pass. These are generic geometry contracts, not a need for red-map
   coordinates in production. Implement a checked near-straight continuation
   using the existing hull/floor sweep path and explicit crossing metadata.
2. **Ramp-entry continuity.** A second red slow cluster occurs around `(255,-310)`;
   blue has a corresponding interior cluster near `(2365,4460)`. Multiple short
   vertices and heading changes produce stopped transitions. Validate whether
   those can be coalesced or given an admissible curved/pass continuation; do
   not remove vertices merely because their XY projection looks straight.
3. **Long ground/ramp travel.** Remaining ramp travel is roughly 237–241 u/s,
   followed by roughly 250 u/s on the upper flat approach. The continuous
   controller falls back to walking when checked hop room is below 320. After
   seam/transition repairs, measure whether preparation unnecessarily fragments
   valid hop room; only then change hop eligibility or layout.

The initial implementation should have pure cases spanning the tiny-kink gap,
short legs, real turns, slope changes and unsafe sweep refusals. Use the current
corridor build's retained cases as baseline before changing sources, then a
fresh unique build and focused both-team exact-12 outcomes. Preserve the
corridor guard and repeat the required body/resource checks for owner changes.
Expand native qualification only for an explicit changed behavior or uncertainty.

## Audit correction and checks

The existing window summary read obsolete `applicable` instead of
`in_denominator` from T5 verdicts. It printed zero cases despite eight detailed
verdicts. It now correctly reports 8 applicable and 0 met in all three corridor
repetitions. Frozen reports remain unchanged; corrected summaries are separate.
No gate or detailed verdict changed.

49 relevant Python tests pass, including new clipping/gap/censoring/reactivation
and T5 denominator checks. Syntax checks pass. Independent read-only review
confirmed the four first-repetition episode sums and mirrored phase attribution,
and identified the curve-proximity interpretation caveat recorded above.
