# Independent offered references and measured onward travel, 2026-09-17

[Final source/binary/tool and campaign bindings](../../../.codex-research/move-independent-20260917-a-final-manifest.json).

This follow-up corrects two implementation problems in the
[heading-offer experiment](2026-09-17-heading-offer.md) and tests the correction
before drawing new performance conclusions. Historical results are retained.

## Implementation

Both observe-only and offered stored-heading candidates now have an
independent walking reference. `HeadingOffer` changes eligibility for
selection, not the control's required horizon or reference buffer. In the
old offer mode, a B candidate requiring 100 frames could lengthen A's
50-frame reference into an unsupported interaction at frame 70, making A
unjudged. A longer B reference can no longer cause this cross-arm failure.

Candidate telemetry records `role=proposal|observe_only`, `offer`, the
actual `walk_ref`, and `walk_ref_frames`. Analyzer version 4 distinguishes
an intentional offered-B commit from an observe-only isolation violation.
Independent references do not promise a faster route: ranking is local
margin over walking, not end-to-end optimality.

No safety threshold, Force policy, entry envelope, reach distance or
onward-distance requirement changed. The offer remains default off.

The onward diagnostics now record horizontal distance travelled, change
in distance to the objective, and observed frames, alongside maximum
displacement. Low displacement with no braking/holding did not establish
turning as the cause. These additional observations distinguish movement
that makes progress from movement that merely covers distance.

## Frozen native binding

Build: `.codex-research/move-build-independent-20260917-a`.
Source/binary manifest: `20260917T220830200184Z.sources.json` in that directory.
All native source files remained frozen during build and collection.
Worlds use isolated localhost ports, unique homepaths, `--edges`,
`--parity 256`, and serial execution. No installation into GameData.

## Collection status

Focused diagnostic campaign
`.codex-research/move-independent-20260917-a-diag` passed all three cases:
red outbound stand, blue outbound stand and red outbound respawn5. All had
zero parity mismatches and no judged-short candidates. Subsequent campaigns
and the final analysis below determine qualification; diagnostic timings
alone are not a treatment effect.

The observe-only small-seed red case also passed with zero parity mismatches.
Its 352 candidates have explicit role/reference evidence, zero isolation
faults and zero censored pairs. The three offered diagnostic cases cover
792 candidates, also with zero reference/role faults or censored pairs.

## Regression coverage

`tools/test_movement_reference.py` extracts and compiles the actual
production `StraightJudge`, mocking the forecast boundaries rather than
reimplementing the judge. Four scenarios cover observation/offer modes and
reference success/failure; the failure scenario gives A 50 frames and B 100
frames with B's reference refusing at 70. A remains admitted. Deliberately
restoring coupled horizon selection fails the same harness. Distinct mock
reference values also verify that the judge reads the correct arm's buffer.
This tests reference isolation, not native physics.

[Retained harness result](../../../.codex-research/movement-reference-test-20260917T221039Z-cd4e68a0/result.json).
No files were deleted; generated sources, binaries and the negative-control
failure are retained. Compiler work completed during outcome-only collection,
before the matched timing campaigns.

## Respawn investigation

Corrected-build red outbound respawn5 completed in 18,600 ms in diagnostic
mode; retained greedy completed in 15,150 ms. They take different routes.
The seeded route's first plan selects an unvalidated pad edge priced at
2,482 ms, goes south toward the central pad, then takes the upper deck.
The greedy reference remains on the lower lane and uses the far ramp.

The diagnostic CSV contains airborne `jumppad` observations around elapsed
6.25–7.75 seconds. These are not ground waiting, even though the hop-only
analyzer groups the interval into residual `search_walk`. Sampled CSV
landmarks are evidence of route shape, not exact segment boundary timing.
Do not attribute the whole 5,800 ms residual bucket to ground friction.

An early planned run-up also loses momentum: at elapsed 1,850 ms its launch
speed is reduced from about 437 to 250 u/s. The corresponding immediate
alternatives are refused by the selected path/ranking. The evidence supports
testing route choice and realized pad transition costs, not assuming the
deficit is exclusively hop timing or pit conservatism.

Next causal experiment after this correction: compare the pad route and
the lower-route alternative from matched initial states, preserving native
continuity and recording real pad entry/exit and pickup. The current pad's
landing is an estimate, and the graph's initial 14,788 ms path cost becomes
18,600 ms of execution. Do not remove the pad globally or insert map-specific
coordinates as a shortcut to passing.

## Onward observations

New diagnostics show both useful and unhelpful motion among `no_onward`
refusals. For example, blue search 1 candidate 7 travels 144.426 units,
reaches only 59.932 units of displacement, and reduces objective distance
by 53.368 units with no braking/holding. Red respawn5 search 1 candidate 0
travels 169.779 units, reaches only 38.556 units of displacement, and moves
28.285 units farther from the objective. Both contain 49 observed onward
movement frames (including the landing command); the configured hold is
still 48 frames. No threshold changed.

Replacing displacement with travelled distance alone would accept both
behaviors. These examples motivate full continuation validation, not a
blanket relaxation or a claim that every refusal rejects a useful turn.


The default floor completed **36/36**, zero parity mismatches and no legacy
ownership. Diagnostic observation and floor were outcome-only checks; one
observe-only diagnostic world overlapped the floor and its elapsed time is
not used as a timing treatment comparison. Matched timing collection starts
only after both outcome worlds and the unit-test compiler have exited.

Final analyzer outputs are retained under
`.codex-research/move-independent-20260917-a-analysis-v4`. Earlier analysis
outputs in this session remain retained. The final output explicitly warns
that the respawn's residual intervals include uninstrumented pad movement.

## Matched seeded outcomes

The corrected build's diagnostic-off seeded campaigns each pass 12/12 with
zero deaths, forbidden spend, parity mismatches or judged-short candidates.
The named regression cases are included in this case set.

| Metric | Control | Offer |
|---|---:|---:|
| Mean duration across 12 cases | 19,202 ms | 18,835 ms |
| Mean deficit against retained greedy | +1,454 ms | +1,088 ms |
| Stand family mean (outbound and return) | 21,125 ms | 19,306 ms |
| Respawn5 family mean | 17,813 ms | 18,819 ms |
| Respawn4 family mean | 18,669 ms | 18,381 ms |
| Cases including legacy ownership | 3 | 2 |

The offer is 367 ms faster on the mean of these particular campaigns, but
this is one campaign per arm, not a demonstrated stable improvement. Both
arms fail the full ownership/performance gate. In the offered arm,
`red_ret_L1_respawn4` and `blue_out_L1_respawn4` include legacy movement;
the control also includes legacy in `red_ret_L1_stand`.

These results neither qualify the offer nor replace the owner's separate
stationary outbound sub-15-second target. The stand-family averages above
include return legs and must not be presented as outbound timing evidence.

## Eight interleaved blue repetitions per arm

All sixteen diagnostics-off runs pass with move-only ownership, zero parity
mismatches and no forbidden spend. Runs are serial; order is control/offer
on odd repetitions and offer/control on even repetitions. Fixtures share
settings but not identical native roots, so these are matched-configuration
trials rather than same-root counterfactuals.

| Arm | Pickup times in ms, repetition order | Mean | Median | Range |
|---|---|---:|---:|---|
| Control | 19925, 19925, 19675, 20650, 20650, 19300, 19300, 19675 | 19,887.5 | 19,800 | 19,300–20,650 |
| Offer | 20575, 20575, 19725, 20525, 20525, 19600, 20575, 19600 | 20,212.5 | 20,525 | 19,600–20,575 |

The offer is slower in five of eight adjacent pairs, with mean +325 ms and
median +725 ms across the two samples. The ranges overlap. This strengthens
the decision not to promote the offer but does not establish a universal
penalty or isolate a particular losing native root. There is no demonstrated
blue benefit and no sub-15-second result. Do not hide these trials behind
the aggregate seeded mean.

The collection commands and order are retained in
`.codex-research/move-independent-20260917-a-commands.json`, with the
orchestrator `.codex-research/move-independent-20260917-a-collect.py` and
individual campaign console logs. No old output directory was reused.

## Controlled pad-route probe

A derived seed changes only the ten unvalidated pad proposal costs to
100,000 ms. This is a deliberately artificial planner penalty, not measured
travel time, a production fix, a physics change, or an instruction to remove
pads. The original seed is unchanged. All changed edge IDs/costs and both
seed hashes are retained in
[the probe manifest](../../../.codex-research/move-independent-20260917-a-pad-price-probe/manifest.json).

Both arms use the same corrected binary, red outbound respawn5 fixture,
offer enabled, replay telemetry bit 1, resources, cadence and safety rules.
Worlds run serially. Three repetitions per arm:

| Seed | Times | Median | Owners |
|---|---|---:|---|
| Original | 18,175 / 18,075 / 18,025 ms | 18,075 ms | jumppad, move |
| Pad-price probe | 15,400 / 15,400 / 15,525 ms | 15,400 ms | move |

All six complete with zero deaths, forbidden spend or parity mismatches.
Every probe run is faster than every original-seed run in this sample;
median improvement is **2,675 ms**. This supports a causal route-selection
problem in this respawn case. It does not establish that pads should be
penalized globally or explain the stationary stand target.

The probe's first route has 127 cells and three planned strafe edges,
modeled at 19,279 ms but executed in 15,400 ms. The original pad route was
modeled at 14,788 ms but executed in 18,600 ms in the earlier diagnostic.
The model orders these alternatives incorrectly in these observations.
The next implementation must measure realizable pad entry/exit transitions
and compare route costs; merely retaining the artificial penalty would
hide the cost-model error.

Probe outputs: `.codex-research/move-independent-20260917-a-pad-route`
(first probe), `-pad-probe-r2`, `-pad-probe-r3`, and `-pad-control-r1` through
`-pad-control-r3`. Further commands are retained in `-pad-commands.json`;
first-probe settings and seed binding are in its case `result.json`.

## Completion and limits

The reference-coupling and telemetry corrections are implemented and tested:
48 analyzer tests, four extracted-production judge scenarios, and a negative
control that fails as intended. Fresh native coverage totals 86 passing
case runs (36 floor, 24 seeded, 16 blue repetitions, four diagnostics, six
pad-route probes), with zero parity mismatches. This is safety evidence,
not a declaration that the seeded planner passes its ownership/speed gate.

The offer remains default off and unqualified. No sub-15-second stationary
outbound run was achieved. The blue samples do not justify enabling it.
The respawn investigation now has a controlled route-choice result rather
than an assumed ground-delay explanation. No installed game was changed.

Follow-up order: measure and correct realized pad-route costs; qualify that
change on all affected cases; then investigate the remaining stand-chain
and onward-continuation behavior without weakening safety. Preserve all
counterexamples and keep hypothetical graph times separate from pickups.
