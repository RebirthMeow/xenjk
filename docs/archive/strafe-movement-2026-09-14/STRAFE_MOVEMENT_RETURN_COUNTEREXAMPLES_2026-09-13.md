# Same-build return counterexamples

September 13, 2026. Owner execution on Astra, no subagents. This follows the
[corner continuation record](STRAFE_MOVEMENT_CORNER_CONTINUATION_2026-09-13.md).
No production code, native resources, acceptance gates or installed files change.

## Question and protocol

The retained combined-policy matrix's R3-blue repetition 2 and R1-blue repetition
3 were each 0.925 s slower than their historical guard controls. Compare those
two original cases on the unchanged nav-continue build before attributing the
difference to a new policy.

Evidence: `.codex-research/return-counterexamples-8a7d874948664cbe9b94f04a72f30e4b`.
Build: `.codex-research/strafe-build-nav-continue-b53d672dfcac47499728a024e61c9627`.
`declaration.json` preserves all commands and the source manifest hash. Three
repetitions of each case use three arms, sequential worlds, ports 36500–36517:

- Control: all four experiments off.
- Prior: partial spans, grounded step join and chain-preserving tap timing on.
- Combined: prior plus corner continuation.

Native fixture poses, velocity, Force, Speed duration, routes, branch assets,
body guard and 55-second game-duration request are unchanged. The middle
repetition reverses arm order. No fixed startup level or private-state replay
is asserted; actual fixture starts are retained. Native sources remain frozen.

## Attribution

All 18 worlds capture alive, health floor 90. Verification passes for 1105
native sources, two build binaries and all 18 staged worlds. Outcomes group by
actual fixture start modulo the 8 ms movement clock:

| Start modulo 8 | Worlds | Fixture-to-capture seconds | Idle / walking milliseconds |
|---|---:|---:|---:|
| 0 | 4 | 16.800 | 0 / 2775 |
| 2 | 1 | 16.825 | 0 / 2550 |
| 3 | 2 | 16.925 | 0 / 2750 |
| 6 | 3 | 16.850 | 0 / 2800 |
| 7 | 8 | 17.725 | 50 / 3975 |

This descriptive grouping retains all arms and repetitions; it is not a new
gate or an adjustment to the original historical scores. Phase 7 includes
control, prior and combined configurations for both cases. Phases 0, 3 and 6
also have exact recorded trace matches across configurations within a case.

Both historical slow candidates capture in 17.725 s from the fixture, versus
16.800 s for their historical controls. The slow trajectory reproduces with
all experiments **off** on the current build. Its complete 708 recorded momentum
samples match the historical candidate after subtracting fixture start time.
This includes command, owner, speed, vertical velocity, ground status, resources
and branch/index fields. It is equality of rounded recorded telemetry, not a
claim about unrecorded private state or between-frame motion.

The slow trajectory also occurs with prior and combined policies. Each slow
case starts at level time modulo 8 equal to 7. The runs use native fixed Pmove
at 8 ms with 25 ms game steps; `g_active.c` rounds command time upward to the
Pmove grid. Different fixture phases therefore need not share the same sequence
of physical step durations. The fixture does not reset `ps.commandTime`.
Matching phases/traces support a startup timing explanation, rather than a
new corner-continuation penalty. This is not an exhaustive controlled phase
sweep and does not establish equivalence for every return case.

The historical slow branch enters at relative 675 ms, exits at 7450 ms and
releases for resources at 8875 ms/index 51. Its historical faster control exits
at 7425 ms and releases at 8850 ms/index 50. Most of the eventual 925 ms gap
accumulates after that small trajectory difference, during fallback. Do not
change resource gates or prepared tap-chain timing to repair an attribution
that does not require those policies.

`comparison.json` retains each new outcome, actual start, fixture-to-capture
time, original episode audit, 40 Hz idle/walking totals and uncovered time.
`attribution.json` retains normalized trace hashes and exact matches to the
historical cases. `verification.json` checks native source, binaries, staged
fixtures, runner and declared experimental settings.

## Next action and limits

Move to the remaining dropped-objective descent refusal documented in
`remaining-dropped-idle.json` under the corner evidence root: local navigation
near `(1925,5451,1772)` reports actual missing floor toward the lower route.
Find an admissible descent or alternative route from actual state. Preserve
native floor checks and the native flag-return timer.

The 60-case historical matrix remains unchanged: 27 slower pairs, 16 worse idle
fractions, 30 worse walking fractions and T5 0/8. These targeted comparisons
resolve attribution of the two largest counterexamples; they do not pass the
full performance contract. Future causal fixture comparisons should record
and compare movement-clock phase, while retaining unmatched runs and all
failures. No installation or broad qualification is claimed.
