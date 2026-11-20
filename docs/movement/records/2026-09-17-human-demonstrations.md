# Three human demonstrations

The owner recorded three real client runs in
`.codex-research/human-route-20260917T230456948583Z/server/base/bot2-route`.
All three pass the recorder report's stationary-grounded-start, native-clock,
continuity and actual enemy-flag-possession checks. No teleport is reported.
All pre/post/final Force pools are 100; no active powers or pool decreases
were observed. This describes the samples, not a counterfactual force-zero
replay or proof that every jump input lasted exactly one command.

| Recording | Native pickup | Peak horizontal speed | Distance from fixture start |
|---|---:|---:|---:|
| `1789686350-0-c0` | 12.875 s | 870.9 | 392.9 units |
| `1789686390-1-c0` | 14.800 s | 724.8 | 35.7 units |
| `1789686416-2-c0` | 18.550 s | 621.9 | 9.0 units |

Fixture start is `(152,-1160,1924.125)`. Run 1 starts at
`(-64,-832,1912.125)` but moves backward to y=-1177.6 by 1.725 s;
its first jump occurs at 2.000 s. It is not simply a forward head start.
Runs 2 and 3 start close to the stand fixture. None is an exact matched-root
comparison. Reports and route JSONs are retained alongside each source CSV.

## Measured difference: momentum through landings

Run 1 has six successive landing-to-jump transitions with zero gap between
the previous post-command clock and next pre-command clock. Landing speed
is preserved at the next jump root, successively 428.3, 545.3, 621.9, 681.6,
749.8 and 806.4 units/s. Across its eleven observed inter-jump intervals,
seven have zero command-boundary gap; the sum is 112 ms. Corresponding sums
are 400 ms for run 2 and 2,664 ms for run 3. These are observed command-clock
gaps, not inferred substep airtime or all grounded time during the route.

Run 1 reaches y=3000 at 9.175 s with horizontal speed 849.2; run 2 reaches
that latitude at the same sampled time at 693.1. Run 2 then sweeps wider:
near y=4000, x=2845 versus run 1's x=2099. Thus both momentum retention and
geometry differ. The traces include airborne chords around 709-791 units;
without collision-map analysis these must not be labeled particular pit
crossings or proof that a specific safety test rejected them.

## Bot comparison

Two retained stationary-stand bot runs are included in the comparison:

- Final recorder-build B, unseeded graph: 21.100 s. Initial path 215 cells,
  no strafe edges, modeled 30.622 s. Evidence:
  `.codex-research/human-route-disabled-bench-20260917-b/red_out_L1_stand`.
- Independent-reference seeded diagnostic, heading offer enabled: 19.075 s.
  Initial path 19 cells, 16 strafe edges, modeled 13.145 s. Evidence:
  `.codex-research/move-independent-20260917-a-diag/red_out_L1_stand`.

These configurations differ; they are not an A/B claim about the recorder.
Both have actual stationary fixture starts and only `move` sampled ownership;
neither supplies evidence of pad-related delay in this stand comparison.
Their first CSV rows predate the fixture. Samples at the exact fixture level
also precede intervention and must be excluded: use times strictly greater
than `corner_level` (2900 / 3575 respectively).

The seeded bot's detailed analysis records a landing at 2.400 s with speed
560, followed by next launch confirmation at 3.025 s at speed 254. Other
transitions lose 442 to 253 and 498 to 255. Confirmation gaps are not proven
ground durations. Existing eight-break analysis is retained at
`.codex-research/move-independent-20260917-a-analysis-v4/move-independent-20260917-a-diag/red_out_L1_stand/report.txt`.

The human 14.800 s run beats the seeded reference by 4.275 s, with the
start, Force pool and command-cadence differences still uncontrolled.
Human inputs are approximately 8 ms apart; bot thinking is 25 ms. Both use
fixed Pmove msec 8. Bot sampled trajectory is 250 ms; its observed speed
peaks therefore underestimate short spikes. Do not equate the 40 Hz server
clock with human input cadence or retime the human trajectory.

## Search implication and next diagnostic

The stand deficit has a concrete momentum-preserving human counterexample.
Prioritize diagnosing continuation generation/admission/ranking at these
landings. Current local ranking selects greatest margin over walking
(`StraightEvaluate` in `bot2_move.c`), not minimum remaining pickup time;
its one/two-hop proposal family need not find the demonstrated long chain.
These are mechanisms to test, not proof of the exact rejection responsible.

Build controlled cases from the human landing states and determine whether
the successful continuation is absent from the proposals, fails forecast,
is refused by admission, or loses ranking. Match input cadence and resources
explicitly; the selected recorder fields are not a full serialized engine
snapshot for arbitrary exact replay. Preserve native safety and turn findings
into general search corrections, not hard-coded human route coordinates.
The earlier respawn pad-cost defect remains valid but does not explain this
stand evidence by itself.

Reproducible analysis, source hashes, checkpoint values, landing-gap rows and
plot: `.codex-research/human-comparison-20260917-a/{analyze.py,comparison.json,comparison.png}`.
No native code, installed game, or running session was changed by this analysis.
