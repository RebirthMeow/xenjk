# Perf counters: where the bots' frame time goes (2026-10-04)

A second outside review (Fable) proposed eight exact speed-ups with estimated gains; the first batch of estimated
items ([review fixes](2026-10-04-review-fixes.md)) measured as nothing. Owner: measure first. One build adds counters
for each candidate and the snapshot-skip verify mode; nothing is changed until the numbers say so.
Research directory: `.codex-research/perfctr-20261004-a/` (`perfctr.py`, `stepcost.py`, worlds, scripts).

## The build (`perfctr-build-20261004-a`)

`[PERFCTR]` (bot2_movecost.c), printed beside `[FRAMECOST]` while `bot_moveFrameMeter` is on, cumulative since the
meter's reset; inert otherwise. Counted: guard scope snapshot, End compare and first full copy; the snapshot-skip
verify mode (`bot_forecastScopeSkip 2`: a Begin in the same trace-cache stamp and frame as a clean End compares the
world with the last baseline before copying, and counts whether a skip would have been exact; it skips nothing);
strafe near-list builds, rebuilds and drops; mover rest-memo misses and the misses a scope-serial key would have
answered (and any disagreement); forecast rollouts, simulated and replayed steps, rollouts with a buffer matching in
everything but its scope (`xscope`, steps as an upper bound), candidate ring slots reused inside one scope; the
defender throw-sweep hurt test; cvar reads by name (the two by-name cvar imports routed through counting wrappers);
`G_Find`; `Graph_Reaches` floods. Plus the link-symmetry check Fable asked for (`links=cells/not-opposite/not-any`).

## Identity and overhead

Verify worlds (counters and verify mode on), Dantooine seeds 1, 4, 8, 12, against the theft-goal campaign's `on`
worlds (same sources but the counters): all four identical. The first seeds 1 and 4 (`v-1`, `v-4`) stalled 3.4 s
in setup on the cold build and diverged at line 2; their reruns (`v2-1`, `v2-4`) are identical.

Overhead, against `perf-build-20261004-b` (no counters, identical decisions), one world at a time: mean frame cost
7.87 -> 8.06 ms (+2.4%, seed 1), 6.23 -> 6.60 ms (+5.9%, seed 2). The shares below are of the counter build's total,
so slightly inflated; the cvar share most (each read takes two clock reads).

## Measured (counter build, timing worlds `t-p/w-1`, `t-p/w-2`; verify worlds agree)

| candidate (Fable item) | per world | share of bot time |
|---|---|---|
| guard scope: snapshot / End compare / first copy (2) | ~43,000 scopes, 85 / 140 / 80 us each | 2.0-2.3% / 3.2-3.6% / 1.8-2.1% |
| cvar reads by name (6) | 56 M outside rollouts, 20-21 M inside | 2.0-2.4% + 0.8-0.9% |
| `G_Find` (7) | 2.2 M calls | 0.3% |
| defender throw-sweep hurt test (5) | 1.5 M calls | 0.1% |
| `Graph_Reaches` floods (8) | ~1,100 | 0.1% |
| strafe near-list builds (1) | 2.0 M, 99% rebuilds after a drop | 0.05% |
| mover rest memo (3) | 13,000 misses (Dantooine has no lifts) | ~0% |

**The snapshot skip as proposed is not exact.** In every verify world about a quarter of scopes are eligible (same
trace-cache stamp and frame as a clean End), and 11-12% of those found the world changed since that End (1,181-1,292
a world). The skip could be exact only with the compare it was meant to avoid, and the End compare cannot be skipped;
the most the skip could save is about 1%.

**Near lists:** every rebuild costs about 0.1 us; local invalidation would save at most 0.05%. Links are symmetric
(16,749 cells, 0 asymmetric), so it would be exact, but it is not worth the code.

**Mover memo:** a scope-serial key would have answered 8,000-8,600 misses a world with 0 disagreements: exact, and
worth nothing on Dantooine; on lift maps it is unmeasured.

## Where the time is: simulated forecast steps

From the `[FRAMESLOW]` lines (each bot's microseconds and simulated steps per slow frame): a bot-frame costs about
**590-640 us + 13.5-15.0 us per simulated forecast step** (r = 0.89-0.90). Over the world that is 13.0 M steps x
13.5 us = about 176 of 198 s of bot time on seed 1: simulated steps are roughly nine tenths of all bot time, and they
explain 81-85% of every bot-frame over 25 ms (median 1,960 steps). Every other candidate above is at most 3%.

So the levers are fewer simulated steps or cheaper steps:
- **Replay coverage.** 13.0 M steps simulated, 7.2 M replayed (seed 1). 118,000 rollouts a world (32%) have a buffer
  that matches in everything but its scope (upper bound 7.0 M steps: the buffer's length, not the agreeing prefix),
  and 93,000-145,000 candidate ring slots are overwritten inside their own scope. Cross-scope replay needs the world
  proven equal between the scopes (the 11% above says it is not always); the scope compare that proves it costs
  ~100 us, about seven steps.
- **The step itself.** Pmove, the steering search and the corridor test per step (Fable's breakdown: about half,
  a quarter, an eighth); not counted here.

Neither changes the tail's nature: a 2,000-step search is ~27 ms of steps however it is accounted. Finishing long
searches outside their frame remains the owner's open question.
