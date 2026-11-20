# Interior preparation and validation-cost attribution — 2026-09-06

## Hypothesis and scope

The previous native coverage experiment failed eight entries by refusal and
accepted one trajectory with inadequate second-platform clearance. This probe
tests whether automatic interior sampling and ranking by physical clearance
improve empirical entry regions while preserving the same native prefix gate.
It separately profiles guarded validation. It does not qualify production
execution, a certified continuous funnel, dynamic obstacles, or M3 affordability.

The frozen maneuver/controller sources, geometry and four-second continuation
score remain the reference. The normal installed game is unchanged.

## Preparation declared before collection

Insert the midpoint between every adjacent coordinate in the original 3x3x3
grid. The resulting tensor grid has 125 roots:
`x={-24,-12,0,12,24}`, `y={-16,-8,0,8,16}`,
`vy={-50,-25,0,25,50}`. All other initial conditions match the previous probe:
z=24.125, vx=vz=0, yaw=0, pending native floor contact, 25 ms game steps.
Each root runs the full eight-candidate native search and 160 live steps.
The grid automatically supplies edge, face and volume interior samples;
the original 27 corners are retained. All values are exactly representable by
the fixture's integer initial-velocity cvars.

Audit every raw candidate and preserve both useful and failed labels. Store
training in v1 library shards of at most 64 roots, sharing identical source,
binary, geometry, map and physics bindings. These shards are preparation
inputs; they do not change the production library's native capacity.

Each original region receives all 27 measured roots within its closed bounds.
A proposal must have a useful full continuation and a supported twelve-step
prefix at every local sample. Its measured clearance is the minimum horizontal
distance to the second platform edge, minus the mandatory 15-unit inset, over
all world-supported phase-6/7 samples. Require finite nonnegative clearance;
rank by greatest worst clearance, then shortest worst finish, mean finish, ID.
Keep up to two proposals. Record first useful first-platform clearance
separately; takeoff naturally leaves that platform's inset.

Compute terminal envelopes from all 27 measured prefixes, without independent
padding or manual changes to the previous [-33,-32] velocity range. Regions
without an eligible proposal remain refusal-only. No adaptive split or new
policy is introduced in this probe; retain contradictory labels so a later
refinement has explicit evidence. A sampled minimum is not a continuous bound.

## Evaluation fixed before training outcomes

Freeze 32 unseen roots: x={-15,-3,3,15}, y={-10,10},
vy={-43,-11,11,43}. They are disjoint from training and prior evaluation.
Also rerun the previous 16 native roots (x={-21,-9,9,21}, y={-12,12},
vy={-37,37}) as a separately labeled regression group, never as new holdout.
Every root has full-eight reference and native region-selection arms.
Freeze the baked JSON and native payload before either evaluation group runs.

Acceptance hypothesis: all 32 unseen and 16 regression roots produce useful
continuations with native/Python decision agreement, all four native parity
gates, unchanged clearance contract, and at most two twelve-step candidate
prefixes. Report false acceptance and refusal despite useful reference
separately. Retain failures and every case in the denominator.

The later 160-step selected forecast is diagnostic only; it cannot choose or
veto the proposal. It remains additional fixture work outside the prefix timer.
Report per-root and per-group finish, walking, idle, travel and longest spans.
Travel-performance comparisons remain observations, not a relaxed speed gate.
Four-second episodes cannot qualify the five-second confinement gate.

Keep all seven native controls from the prior probe: x=-25, x=25, y=-17,
vy=51; wrong map/candidate identity; terminal X envelope shifted by +1000.
The first six refuse with no rollout/live steps. The last refuses within
24 prefix calls and before live movement. Verify raw roots and staged bytes.

## Timing and lifecycle

After outcome worlds exit, run five serialized native timing worlds at root
(0,0,24.125), velocity zero. The unchanged quiet repeat uses outer clocks only,
retaining restoration and trajectory/private-state digests. Compare its exact
prefix and decision against the recorded primary run.

Complementary instrumented forecast records separate world/level hash guards,
world clone/setup, per-step context hashing, command generation, native Pmove,
global restoration and remaining bookkeeping. Account for all measured time
with an explicit residual; do not infer a stripped production cost by summing
only favorable buckets. Per-category clock overhead and host scheduling remain
limitations. Prefix acceptance and digest work outside BuildForecast are
reported separately when instrumented. Full selected diagnostic cost is
reported separately from entry selection.

Use a fresh unique native build with source/binary hashes, snapshot all helper
and protocol declarations, isolate homepaths and ports, run at most four outcome
worlds, and run timing alone. Freeze native sources throughout compilation and
collection. Preserve raw captures, failed audits and all prior builds.

## Execution

Run `tools/strafe_coverage_native_campaign.py freeze` with
`--preparation interior-clearance`, exact build/assets/geometry/BSP paths and a
fresh output directory; then `train`, `bake`, `evaluate`, `control`, `timing`,
and `audit` against that directory. Each stage verifies its frozen inputs.
Current milestone status belongs in the active plan and progress scorecard.

## Result: better selected clearance, region hypothesis still rejected

Fresh native build: `.codex-research/strafe-build-region-profile-20260906`,
log `20260906T121348282520Z.log`. Frozen campaign:
`.codex-research/strafe-region-preparation-v2-20260906`; audit:
`.codex-research/strafe-region-preparation-audit-20260906.json`.
The earlier `.codex-research/strafe-region-preparation-20260906` retains a
launcher-format failure: velocity arguments such as `-50.0` were rejected by
the integer CLI parser before any native simulation. The correction serializes
the same exact integral velocities as integers; no state or case was removed.

| Group | Full reference | Prepared selection |
|---|---:|---:|
| New unseen states | 32/32 useful | 32/32 useful |
| Previous native evaluation, now regression | 16/16 useful | 8/16 useful; eight refusals |
| Explicit refusal controls | — | 7/7 expected refusals |
| Serialized timing repetitions | — | 5/5 exact quiet prefix/decision matches |

All 40 executed selection cases satisfy the existing clearance and useful-exit
contract, including the previous accepted clearance failure. The eight refused
regression states still have initial vy=-37: their terminal vy=-31 lies outside
the automatically measured [-33,-32] envelope. Uniform midpoint enrichment did
not observe that interior physics/policy outcome. The full 48-state hypothesis
therefore fails; the new unseen successes cannot erase the retained failures.

Independent raw analysis in `.codex-research/strafe-region-analysis-20260906.json`
finds at least 8.865570 units of actual second-platform clearance slack across
all 40 executed selections. The old failing root x=-9,y=-12,vy=37 now chooses
candidate 5 and has 10.386475 units of slack. All sixteen refused candidate
prefixes match their full-reference prefixes after clock normalization; all
sixteen corresponding full continuations are useful. The only rejected terminal
feature in each is vy=-31 versus [-33,-32]. Training retains 936 useful and
64 failed candidate outcomes out of 1,000, with no failures discarded.

The native payload remains 2,002 bytes despite expanding preparation from 27 to
125 roots. The full JSON is 956,533 bytes and retains failed labels and raw
provenance. Native training worlds took 281.980 seconds with at most four
concurrent worlds. Library-shard baking took 35.255 seconds and region baking
18.433 seconds, excluding the separate campaign audit work. Each region has
two proposals with worst sampled clearance roughly 9.4–10.3 units beyond the
mandatory inset. No controller parameters or native acceptance bounds changed.

On the 32 fully comparable unseen cases, finishes are 0–100 ms slower than full
search. Fixed-four-second walking rises 1.09375 percentage points and idle falls
1.171875 points. These are travel-time/clearance tradeoffs, not a walking-speed
improvement. The eight executed regression cases are 25–125 ms slower; their
walking/idle statistics cannot substitute for an aggregate over all sixteen
because refused cases have no live movement. Four-second recordings do not
qualify distant confinement.

The campaign has 34,880 matched live native steps. The separate
`.codex-research/strafe-region-v6-regression-20260906` retains the +100 Y airborne
impulse/reuse result: 160 matched steps, useful finish at 2.975 s, one actual-state
replacement and two reuse hits. Total on this build: 35,040 matched live steps.

## Measured cost attribution

All five serialized cases choose one twelve-step proposal and verify the quiet
repeat. Quiet outer selection is median **3.7038 ms**, range 3.5673–5.6210 ms.
This remains a guarded research path. The complementary instrumented prefix
forecast is median 4.2590 ms; its categories are:

| Instrumented category | Median microseconds |
|---|---:|
| Initial world/level verification hash | 1,621.4 |
| World clone/setup | 514.1 |
| Per-step context hash | 7.0 |
| Twelve feedback command generations | 313.4 |
| Twelve native Pmove calls | 28.6 |
| Global restoration | 0.1 |
| Final world/level verification hash | 1,615.8 |
| Residual bookkeeping and clock overhead | 12.1 |

The two whole-world hash guards consume **78.1–82.4% of each instrumented prefix
forecast**. Root lookup/compatibility is median 0.9 microseconds; envelope
evaluation 0.4 and trajectory/private-state digests 33.3 microseconds, measured
outside BuildForecast. These independent category medians are not additive.
They must not be subtracted from a quiet median to invent a production cost.

The selected 160-step diagnostic is additional: median 8.8736 ms total,
4.8563 ms command generation and 0.2555 ms physics. It is excluded from quiet
prefix selection. Profiling retains all guards, and makes no eight-active-bot
dispatch or actual runtime tracking claim. Cheap lookup is now measured;
complete useful live validation/tracking/repair remains unqualified.

## Next decision

Keep clearance-aware preparation and the full actual-state validation reference.
Do not enlarge one terminal velocity field to pass the known refusals. The next
sampler must search for adverse execution branches and quantized outcomes,
retain counterexamples, and refine or refuse the associated state regions;
uniform tensor corners and midpoints demonstrably miss them. Bind useful
continuation margin to those branches before another fresh evaluation.

The profiling result supports separating the static runtime predictor/controller
from research-wide verification machinery, with the guarded implementation kept
as its parity oracle. Measure the actual bounded runtime entry, tracking and
repair path rather than treating these diagnostic hash passes as path lookup.
The [two-map support inventory](STRAFE_MOVEMENT_MAP_PREPARATION_2026-09-06.md)
now supplies real-map inputs for the next native geometry-validation interface.
