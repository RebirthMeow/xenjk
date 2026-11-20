# Supported movement handoff contract

The owner authorized a controller-level repair after repeated preparation and
corner patches failed whole-trip acceptance. The implementation separates the
builder from the executor; acceptance still depends on the unchanged movement
gates. Dropped-flag retrieval remains deferred. Nothing is installed.

## Implemented contract

`bot2_handoff.{c,h}` owns an immutable committed path snapshot, its follower,
endpoint identity and a displacement clock independent of publication. The
builder offers a replacement. Admission projects the actual grounded position
onto a forward interval, checks current/projected lateral displacement, height
and velocity alignment, asks the real path executor to check its speed contract,
then checks a native forward connection. Only successful admission replaces the
snapshot. A refused offer preserves the previous path and follower.

The native connector checks four standing-hull floor samples and step-height
sweeps over up to 32 units ahead, including lava/slime and `trigger_hurt`
rejection. Preparation reserves eight traces within the existing shared 32-trace
budget. This is entry validation, not a new claim of whole-path hazard coverage.

Corner-enabled preparation publishes refined geometry. It cannot temporarily
take movement using raw corner vertices that rounding will later replace.
Non-corner preparation may publish its checked raw intervals. A complete partial
path still has a frontier, not a destination. Flight/connection destination
adapters are eligible only after an admitted destination publication.

When the executor's temporary terminal cap would slow below current or ordinary
ground speed, the owner restores the incoming command and yields to independent
native fallback while retaining preparation and its snapshot. It does not
extrapolate the prepared path beyond checked geometry. Actual lack of movement
expires after three seconds even if publications keep arriving. Objective,
clock, external reset, contract failure and obstruction clear the ledger.

The existing path and follower structure layouts are unchanged, preserving
recorded adapter payload layouts. This does not establish adapter behavior or
cost qualification for the new owner. The legacy non-interval dispatcher remains.

Optional stand-route entry now requires resources before taking ownership for
both outbound and return movement. The route's resource thresholds remain
unchanged; entry no longer parks the runner or toggles Speed to regenerate.

## Fixed comparison

Evidence: `.codex-research/handoff-contract-20260914-a`.
`cohort.json` was frozen before native edits. Baseline's 1105 source inputs are
retained under `frozen-source/`; candidate builds use fresh unique manifests.

Eight cases per arm: red/blue ordinary native spawns at 1x with two decision
seeds, then red/blue original zero-Force/no-initial-Speed return fixtures at 5x,
two repetitions each. Prior partial-span, step-join, chain-preserving tap and
corner-continuation settings are fixed across arms. Preparation fallback and
prefix runup stay off. Ports/homepaths are isolated; at most four outcome worlds
run concurrently. No drops, defenders or mid-run intervention are introduced.

First capture comes from CSV. Ordinary elapsed time begins at first observation,
not actor creation; fixture elapsed begins at the declared intervention. Native
startup phases differ, so differences are observed outcomes, not isolated causal
estimates. The first-return XY trace uses adjacent samples up to 50 ms apart;
uncovered time is explicit. The existing whole-observation audit uses separate
3D speed bins and its declared observation window. All failures are retained.

Initial candidate `strafe-build-handoff-contract-20260914-a` completed eight
worlds alive, as did baseline, with zero audited confinement. It failed movement
acceptance: one ordinary blue trip slowed by 2.550 s and all four zero-resource
fixtures acquired 100 ms of idle. Its records remain in `candidate/` and
`comparison.json`. The trace showed early raw publication braking before refined
geometry was admitted. Frontier identity also had not yet prevented temporary
terminal braking. Those two contract gaps were corrected before candidate B.

Candidate B (`strafe-build-handoff-contract-20260914-b`) completed eight worlds
alive. Zero-resource first returns were 22.150 s with no traced idle, but every
whole-observation world had a distant confinement. A retained red example stops
12.250 s after capture under `route_entry`, waiting for Force to reach 100.
This justified applying actual-resource admission to outbound route entry too.

### Final candidate C: implemented, not accepted

Current build: `.codex-research/strafe-build-handoff-contract-20260914-c`, manifest
`20260914T050143977756Z.sources.json`. Current results:
`comparison-candidate-c.json`; prior `comparison.json` and
`comparison-candidate-b.json` are retained unchanged. Baseline plus three
candidate cohorts total 32 completed worlds; every world captured alive.

| Case | Baseline first observed level / capture level (s) | C first observed level / capture level (s) |
| --- | --- | --- |
| Spawn red r1 | 2.250 / 36.900 | 4.325 / 36.900 |
| Spawn blue r1 | 2.275 / 29.350 | 4.350 / 31.550 |
| Spawn red r2 | 1.775 / 36.275 | 4.350 / 36.750 |
| Spawn blue r2 | 1.750 / 36.350 | 4.325 / 36.925 |

These differing observation starts prevent claiming that shorter
observation-to-capture elapsed time proves faster ordinary trips. Native start
phase is not matched. Report both boundaries rather than advertising an elapsed
gain as an end-to-end improvement.

| Zero-resource case | Baseline fixture-to-capture (s) | C fixture-to-capture (s) | Baseline / C XY walking (s) |
| --- | --- | --- | --- |
| Red r1 | 22.300 | 22.200 | 8.950 / 8.800 |
| Blue r1 | 22.225 | 22.250 | 8.750 / 8.725 |
| Red r2 | 22.300 | 22.300 | 8.950 / 8.675 |
| Blue r2 | 22.225 | 22.250 | 8.750 / 8.725 |

All eight baseline/C fixture first returns have zero traced grounded XY idle;
each has 50 ms uncovered. Fixture start levels also differ (baseline 2250;
C 2200/2225), so small timing differences do not isolate policy causality.

Final whole-observation audit: seven of eight C worlds have no distant
confinement; `zero-blue-r1` has one 5.750 s episode at levels 55525–61275 after
its second capture. Its sampled owners are localnav (22) and recovery (2),
not route-entry resource parking. Supported admission remains unavailable.
Baseline has zero confinements. Ordinary grounded 3D idle/walking percentages
remain mixed: red r1 1.67/39.21 → 1.67/37.75, blue r1 1.25/31.87 → 1.67/32.94,
red r2 1.67/38.71 → 2.50/39.23, blue r2 0.83/34.42 → 2.09/38.78.
The unchanged no-regression gate fails. Nothing is qualified or installed.

### Next contract milestone

Admission currently accepts a player already within 12 units of a forward
interval. Its native connector validates that entry; it does not construct a
new approach from an off-line position. Implement that missing bounded approach
as executor-owned, native-checked geometry, preserving velocity/turn limits and
existing progress across offer replacement. Use the retained post-capture blue
episode as a counterexample and rotated/mirrored entry cases as transferable
checks. Do not relax the lateral gate or add stand/map-specific coordinates.
Fallback may own the command while admission is unavailable, but this evidence
does not establish fallback progress. Reuse the unchanged cohort, score complete
observation windows and preserve observation/start-phase limitations. Defended,
body and cost qualification follow only after this movement gate passes.

## Verification scope

`tools/strafe_handoff_check.py` compiles the real handoff, path and command
kernels. Eight rotations cover forward entry beyond a stale stopped vertex,
reverse/lateral/height/air refusal, real overspeed refusal, immutable snapshots,
transaction rejection and frontier/destination braking distinction.

`tools/strafe_handoff_owner_check.py` extracts the real publication owner with
mock native clearance and foreign adapters. It checks rejected replacement,
destination isolation, progress timeout across repeated publication, incoming
command restoration, body refusal, raw-corner publication exclusion and frontier
yield. `strafe_supported_wait_check.py` selects this harness for the new owner;
explicit historical sources retain the old dispatcher harness. These are
different checks, not a claim that historical dispatcher cases all ran here.

The real path release regression passes. Failed harness compile attempts remain
beside their corrected passing runs. Native build/collection and staged hashes
are separate evidence from these kernel/owner tests.

Final passing checks: `kernel-check-final/`, `owner-check-final-v2/`,
`route-entry-check-v2/` and `path-regression/`. The route check covers both
objectives across insufficient resource levels and exact readiness thresholds.
Final scoring verifies all 1107 current native source hashes, staged binaries
and fixtures; baseline verifies against its frozen snapshot. Case settings match
across arms except declared ports. Python syntax, document links and scoped
whitespace checks pass. No native cost benchmark or defended qualification was
run for this redesign.
