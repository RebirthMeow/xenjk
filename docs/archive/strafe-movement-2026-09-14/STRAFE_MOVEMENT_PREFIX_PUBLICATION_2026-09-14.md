# Prefix publication and landing entry

September 14, 2026. Ordinary movement only; dropped-flag retrieval remains
deferred. Owner execution on Astra without subagents.

## Decision

Keep `bot_supportedPrefixRunup=0`. Earlier publication helps one matched fixture
trip, but normal-speed ordinary-spawn results do not support enabling it. The
new trace also corrects the previous diagnosis: the reproduced first landing
stop occurs **before** the first executable prefix. A separate trajectory gets
stuck on an early stopped vertex after publication; the end of the published
path is already far ahead. These require distinct reasoning.

## Trace and bounded implementation

`prefix_frame` reports count, follower index/goal, compiler source/sample,
actual position/speed, published endpoint, result and command each preparation
frame. Eight diagnostic worlds use a trace-only build: four ordinary spawns and
four retained ordinary-goal fixtures. The diagnostic directories' `off`/`on`
labels are repetitions, not different treatments; their declared commands have
no new behavior enabled.

The treatment publishes one point on the already native-checked incoming
straight while the corner compiler continues. It stops 161 units short of the
vertex: beyond the maximum 128-unit arc trim and the maximum pass run-up of
96+65 units. It requires a level incoming segment, the existing published end
on that line, forward extension of at least 16 units and remaining capacity.
Only immutable source intervals are considered by the compiler. Repeated calls
do not duplicate or retract the point. It inherits the checked interval's hop
eligibility and preserves a stopped endpoint until a continuation is published.
No unfinished arc sample is admitted. No extra native trace budget, resource
grant, map coordinate or route ID is introduced.

In the improved fixture pair, both arms acquire the landing path at level 13350
from `(1402.283,2332.264,1808.125)`. Control first publishes at 13800, with speed
already zero. The candidate publishes the straight approach at 13700, with
speed 12.547; by 13800 it is moving at 172.903. This shortens the stop but does
not preserve all landing momentum. The later native capture difference cannot
be attributed entirely to the 100 ms publication change.

## Complete native outcomes

All twelve treatment/control worlds capture alive, health floor 90. The eight
diagnostic worlds also complete and capture without a reported death. No
installation or full movement qualification is claimed.

Two matched-start ordinary-goal fixture pairs:

| Pair | Start, both arms | Capture seconds off/on | Sampled idle ms off/on | Sampled walking ms off/on |
|---|---:|---|---|---|
| 1 | 2200 | 35.425 / 32.125 | 500 / 500 | 12650 / 12125 |
| 2 | 2225 | 33.275 / 33.275 | 1500 / 1500 | 11625 / 11625 |

Matching declared fixture state/start is not a full private-state replay.
Preserve uncovered time in `candidate-fixture/comparison.json`.

Ordinary native-spawn discovery uses one runner per world, no opponents, no
pose/resource intervention and 60 game seconds at each rate:

| Rate/team | First capture level seconds off/on | Observed walking % off/on | Observed idle % off/on |
|---|---|---|---|
| 5x red | 36.100 / 36.100 | 30.1 / 35.6 | 1.3 / 1.3 |
| 5x blue | 31.900 / 28.850 | 34.0 / 31.7 | 1.7 / 1.3 |
| 1x red | 36.050 / 36.100 | 36.7 / 37.9 | 1.3 / 1.7 |
| 1x blue | 28.700 / 28.850 | 30.7 / 31.7 | 1.3 / 1.3 |

These are first capture **level times**, not durations from actor creation.
Startup and observation starts differ and are retained in `outcomes.json`.
At 5x blue has one/two captures off/on; red has one each. At 1x blue has two
each and red one each. All eight have zero audited confinement. The locomotion
percentages span the observation, including later travel, and use the existing
3D grounded bins. Fixture totals use the separate XY sampled metric. Neither
discovery rate is phase-matched qualification. Normal-speed walking worsens on
both teams, so the local fixture gain is insufficient for acceptance.

## Remaining entry problem

In fixture pair 2, control and candidate both follow index 1/goal 1 even when
the published endpoint is hundreds of units ahead. The final prepared points
begin at `(1402.704,2334.329)`, then `(1410.477,2343.665)` and
`(1418.250,2353.000)`, at z=1808.25. The first two incoming intervals are kept
as stopped vertices. The bot is already around `(1427.6,2366.4)` when execution
starts and makes small commands back toward the first one. The first vertex's
arc is rejected for `trim`, and its pass proposal says `straight`.

This establishes an entry/straight-classification problem, not absence of a
published continuation. Rounded coordinates look nearly collinear, but their
three-decimal telemetry alone cannot prove a floating-point bug. Next inspect
the exact producer/executor straightness contracts and actual-state acquisition.
Do not unconditionally skip passed stopped corners or relax genuine turn
geometry. A correction needs checked forward entry and complete-trip results
on both teams. The earlier background landing-requery repair addresses a
different refusal path and should not simply be repeated.

## Evidence and checks

Root: `.codex-research/prefix-publication-20260914-a`.

- Diagnostic build: `.codex-research/strafe-build-prefix-trace-20260914-a`;
  `trace/`, `fixture/`, `trace-verification.json`, `supported-trace-source.c`.
- Current treatment build: `.codex-research/strafe-build-prefix-runup-20260914-a`;
  `candidate/`, `candidate-fixture/`, `normal/`, `candidate-verification.json`,
  `outcomes.json`, `supported-runup-source.c`.
- Both builds bind 1105 native sources at collection. Current verification
  covers twelve worlds, binaries, staged fixtures, runner and option settings.
  Each source remains frozen during its build and collection. Source snapshots
  preserve both revisions; all diagnostics and unsuccessful comparisons remain.
- Extracted production checks cover eight rotated run-ups, repeat-call
  immutability, off-line/height-change refusal, and existing supported lifecycle,
  partial-prefix/no-rewind and resource cases. Twelve play-seed tests pass.
  No native collision qualification is inferred from mocked checks.
- At most four isolated hidden native worlds run together, with distinct ports
  36800–36843 below the host's dynamic UDP range. No measured timing budget or
  body/defended qualification is claimed.

Both `bot_supportedPrepareFallback` and `bot_supportedPrefixRunup` remain off.
The prior rejected trials and historical qualification scores remain unchanged.
