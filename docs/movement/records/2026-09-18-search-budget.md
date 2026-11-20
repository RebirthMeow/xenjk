# The search budget: what the rollout kernel actually costs

Step 0 of the route-compiler line. Before designing an offline search that
looks for sub-15-second routes, measure what a rollout costs, so the design
is sized against a number rather than a guess.

The question was specific: is the existing offline explorer bound by the
server frame loop's batch allowance, in which case there is large headroom
for free, or is it bound by CPU, in which case ~600 rollouts/sec is the
ceiling and a compiler has to be designed around it.

**Answer: it is CPU-bound, the batch is not a lever, and the ceiling is far
higher than the explorer's own rate suggests.** One core simulates about
62,000 command frames a second, which at the fixture's 25 ms frame is about
**1,550x real time**, or **103 complete 15-second route rollouts per second
per core**. Compute is not the constraint on the compiler.

## Instrumentation

Counters only; no control flow changed.

`RunCandidate` already read `parity.result.timing.totalUs` and the rollout's
frame count and threw both away after adding the microseconds to a caller's
`float`. Two accumulators were added to the parity struct (`rolloutFrames`,
`rolloutUs`) and the explorer now reports `rollout_us`, `sim_frames` and
`traces` on its progress and done lines.

The totals are taken from the `double` counter, not from the existing
`float *us` out-param: a float mantissa is 24 bits and these runs accumulate
about 1.4e8 microseconds, so a float accumulator would silently stop adding
small values partway through. This is the same defect class as the map
checksum parsed through a float in the route-asset work.

`Microseconds()` in the forecast is QueryPerformanceCounter-backed on
Windows, so the resolution is real.

## Measured

Build `move-build-step0-20260918-a`, case `red_out_L1_stand`, `--rounds 3`,
worlds under `.codex-research/step0-batch{4,32}-20260918`.

| arm | wall s | rollout CPU s | CPU % | rollouts/s wall | sim frames/s CPU | x real time |
|---|---:|---:|---:|---:|---:|---:|
| `--batch 4` | 196.0 | 141.9 | 72.4 | 641 | 61,923 | 1,548 |
| `--batch 32` | 180.7 | 127.2 | 70.4 | 640 | 63,569 | 1,589 |

An eightfold batch increase moves the rollout rate by one part in six
hundred. The explorer is not waiting on the frame loop. Roughly 71 % of wall
clock is already inside rollouts, and the remaining 29 % is the per-state
replan, the walk step, edge insertion and server overhead.

The two arms did different total work - exploration is adaptive and the
second run widened its corridor twice rather than three times - so the
comparable quantity is the rate, not the totals.

Derived, from the batch-4 arm:

- 16.15 us per simulated command frame
- 11.35 traces per simulated frame
- 885 rollouts per CPU second; 6.4 rollouts per search

Sixteen retained runs from 2026-09-16 agree with the wall-clock figure at
568-713 rollouts/sec, so this is not a property of one build.

## What it means for a route compiler

At 25 ms a command frame, a 15-second route is 600 frames:

- **103 full route rollouts per second per core**
- the tooling already runs four worlds in parallel: **~410/sec**
- an eight-hour overnight run at four workers: **~1.2e7 route rollouts**

For scale, a beam search of width 1000 over 20 decision points is 20,000
rollouts for one (start, objective) pair - about three minutes on one core.
Large-neighbourhood improvement over a human tape is cheaper still.

The conclusion for the plan: **the offline search is not compute-limited,
and no part of the compiler design needs to be traded away to afford it.**
What limits it is the algorithm - what the search expands, what it keeps,
and what it is allowed to admit - which is where the design effort belongs.

## The one lever with upside, if it is ever needed

11.35 collision traces per simulated frame against 16.15 us per frame makes
the trace count the first suspect for where the time goes. This record does
not separate trace time from the rest of the frame - the per-trace figure
above is arithmetic from the same two totals, not an independent
measurement - so that split is an open question, not a finding. If the
compiler ever does need more throughput, measuring it is the place to start.

## Limits

- One case, one map, one build. The rate is stable across sixteen older runs
  but has only been checked on Kejim.
- The rollout cadence is the server's 25 ms frame. Human recordings are
  8 ms tapes; a compiler searching at 25 ms produces routes at the bot's own
  command cadence, which is what the executor uses, but it is not the
  human tape's resolution.
- Rollouts are measured inside the explorer's path, which rolls out one- and
  two-hop candidates over a bounded horizon. A compiler rolling a full
  15-second route touches more world and may trace at a different rate; the
  frames-per-second figure is the portable one, the per-route figure assumes
  the same cost per frame.
- These counters are inert unless the explorer runs. The default floor was
  not re-run for this change; the edits add two accumulators and three
  telemetry fields and touch no control flow. 94 movement tool tests pass.
