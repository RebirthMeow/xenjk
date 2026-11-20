# The finer exit-speed sweep: 500 is about right, and the tail was a stratum

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> All original timing tables below use the shorter LEG_BUDGET1 analysis window,
> not official completion time. Correct common-stratum weighted completed means
> are 17,084.4 ms at 500 and 18,040.6 ms at 2000 (956.2 ms improvement); sparse
> strata are excluded. The fast common stratum is 15,925 ms and 500's observed
> maximum is 21,125 ms. This supports a provisional setting, not adoption without
> fresh confirmation. Low within-cell spread does not remove the need for repeated
> confirmation. The tail's association with start level is useful; its full causal
> mechanism and a universal speed-cost relationship are not established. Legacy
> entry-speed groupings and waiting counts below also inherit the budget defects.

Recorded 2026-09-20. **No source change, no rebuild** - `bot_moveExitSpeed` is
a cvar and every world is on the frozen reference build
`.codex-research/successor-aim-build-20260920-a`. 100 worlds,
`.codex-research/exit-speed-sweep-20260920-a/`. This answers both open items
left by [the three-point sweep](2026-09-20-exit-speed-sweep.md): the finer
values, and the unexplained tail.

Read [the start-level record](2026-09-20-start-level-determines-the-leg.md)
first. It is the reason this sweep is readable at all, and the reason the
tables below are stratified.

## Design

Five arms - 250, 500, 750, 1000 and the incumbent 2000 - alternated
round-robin across 20 sequential rounds, one native world at a time,
`--parallel 1`, unique port and output path per world. Twenty worlds an arm.
The incumbent is re-measured in this session rather than quoted from the
earlier cohort. Screening and confirmation were not separated here. Review
identified this as an open verification step; low observed within-cell spread
does not replace fresh confirmation.

## Historical analysis-window time by start level and arm

Only the strata with more than one world are shown; means, with cell counts.
Within-cell spread never exceeds 25 ms.

Levels are `corner_level`, the number `result.json` reports and
`--corner-level` takes, not the leg budget's first touchdown one frame later.

| `corner_level` | n | 250 | 500 | 750 | 1000 | 2000 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3,300 | 8 | **18,762** | 19,275 | 19,262 | 19,275 | 19,275 |
| 3,325 | 34 | 16,272 | **15,767** | 15,772 | 15,771 | 16,275 |
| 3,350 | 43 | **17,500** | **17,504** | 19,003 | 19,011 | 19,011 |
| 3,275 | 4 | - | 21,000 | - | **19,025** | 20,025 |
| 3,375 | 3 | - | **19,500** | - | - | 20,475 |

Weighting the three well-populated strata by how often they occurred:

| arm | stratum-weighted leg |
| ---: | ---: |
| 250 | 17,128 ms |
| **500** | **16,976 ms** |
| 750 | 17,735 ms |
| 1000 | 17,740 ms |
| 2000 | 17,942 ms |

**500 beats the incumbent 2000 by 966 ms.** The three-point sweep's paired
estimate was -975 ms. Two designs, two sessions, two analyses, the same
number to within 9 ms; that agreement is the strongest thing in this record.

## The optimum is real but shallow, and it is not the same everywhere

500 is the only value at or near the best in every well-populated stratum, so
it is the value to adopt. It is not dominant:

- At 3,350, the 43-world stratum, **250 and 500 tie at 17,500 and everything
  from 750 up loses 1,500 ms.** The threshold sits between 500 and 750 and is
  sharp: 750, 1000 and 2000 all land on the same trajectory as each other.
- At 3,325, the 34-world stratum, **500, 750 and 1000 tie at about 15,770 and
  both 250 and 2000 lose about 505 ms.** Here the low end is as bad as the
  high end.
- At 3,300, 250 wins by 513 ms and 500 is tied with the rest, on 8 worlds.

So the credit is not a dial with a single optimum. It selects between a small
number of discrete trajectories, and which selection is best depends on the
start. 500 is the best compromise over the observed distribution of starts,
not the best value at every start.

## The tail, explained

The earlier record's pair 8 regressed 4,750 ms and was the reason not to adopt
500. It is not a rare event and there is nothing left unexplained.

That pair's test world started at `corner_level` 3,275. In this sweep, that
level with `bot_moveExitSpeed=500` produces **21,000 ms**, against 20,025 for
2000 and 19,025 for 1000. The earlier world measured 21,025. It is reproduced
to within a frame, in an independent session, and it is what that setting does
at that start.

The pair's 4,750 ms therefore decomposes into two unrelated things. Its
control drew 3,325, the fastest stratum, where any setting is about 16,000 ms;
its test drew 3,275, the slowest, where any setting is about 20,000. Roughly
3,800 ms of the gap is the two worlds being different experiments. The
remaining 975 ms is the genuine cost of 500 at `corner_level` 3,275 - which
is, to the frame, the same 975 ms that 500 *wins* on average elsewhere.

**The mechanism is visible in the touchdowns.** Against the 2000 world at the
same start level, the 500 world is identical for the first eight touchdowns
and diverges at touchdown 9, 7,325 ms into the leg:

| | lands at | then | launches at |
| --- | ---: | --- | ---: |
| 2000 | 477.5 units/sec | 5-frame run-up | 250 |
| 500 | 532.4 units/sec | **27-frame wait**, nothing admissible | 252 |

The lower credit bought a landing 55 units/sec faster, the geometry admitted
no continuation from it, and the runner walked the speed off anyway to reach
the same 250 - taking 675 ms over the control's 125 ms. That is
[the refusal at an expensive landing](2026-09-20-trigger-classification.md)
already on record, priced. The two runs never re-converge and the 500 world
accumulates three more long waits.

## What was checked and refuted

Landing speed does not predict what a landing costs. Pooling all 203
touchdowns of the earlier confirmation worlds by entry speed, the median cost
is **one frame in every band above 350 units/sec**, and the most expensive
band is 470-510, not the fastest:

| entry speed | landings | mean frames on the ground |
| --- | ---: | ---: |
| 350-420 | 117 | 5.4 |
| 420-470 | 56 | 3.2 |
| 470-510 | 48 | **8.6** |
| 510-570 | 37 | 4.6 |
| 570-620 | 20 | 1.8 |
| 620-700 | 20 | 3.1 |

So "the lower credit lands the runner too hot" is wrong as a general
statement, and the 27-frame wait above is a property of that location, not of
532 units/sec. What does hold, and is the mechanism the original hypothesis
predicted, is that above 620 units/sec the lower credit waits 4 frames on
average where the incumbent waits 11.

## Status

`bot_moveExitSpeed=500` is a **provisional research configuration value**.
Confirm against 2000 across predefined pinned starts before adopting it. It is **not
installed** and production enablement remains separate open work.

Floors were established for 500 in the earlier record: 36/36 pass, zero
deaths, zero parity mismatches, movement owner throughout. Nothing here
changes the build, so those floors still bind. No floors were run for 250,
750 or 1000 and none of them is recommended.

Parity still makes zero comparisons, so nothing here qualifies
forecast/execution agreement.

## Reproduce

The historical `sweep.py` and `analyse.py` remain with the retained evidence.
`analyse.py` reports legacy budget windows and groups by downstream touchdown
class; it is not the corrected start-stratified completion reproducer. Use
[movement_sweep_summary.py](../../../tools/movement_sweep_summary.py) with the
command in [review corrections](2026-09-20-opus-review-fixes.md). It reads official
completion results, binds input hashes, declares strata and rejects incomplete
world grids. Write a fresh output path; preserve the original evidence.
