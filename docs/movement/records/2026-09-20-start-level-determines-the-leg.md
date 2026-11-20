# The fixture's start level decides the leg, and it is not controlled

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The association with start level is a useful observation on this build and
> case, not proof that all variation is caused by polling phase. The settings
> exist before fixture release, so release timing cannot simply be assumed
> unaffected by the treatment. The original leg-time tables describe a shorter
> analysis window, not completed trips. The claim that one world per cell removes
> the need for confirmation is withdrawn. Use repeated matched pinned starts,
> including the slow stratum, and keep acceptance floors unpinned.

Recorded 2026-09-20. **No source change, no rebuild.** 100 native worlds on the
frozen reference build `.codex-research/successor-aim-build-20260920-a`,
collected as five `bot_moveExitSpeed` arms alternated round-robin across 20
sequential rounds; evidence and driver in
`.codex-research/exit-speed-sweep-20260920-a/` (`sweep.py`, `analyse.py`,
`sweep.log`, `leg-budget-100.json`). Re-checked against the 16 retained
confirmation worlds of [the exit-speed record](2026-09-20-exit-speed-sweep.md)
in `.codex-research/exit-speed-20260920-b/`.

## The claim

`red_out_L1_stand` is deterministic to within one 25 ms frame once you condition
on the level time at which the corner fixture released the runner. Everything
this project has been calling within-arm spread is that one uncontrolled
number.

| | spread in leg time |
| --- | ---: |
| Within an arm, 20 worlds, unconditioned | 3,525 to 5,250 ms |
| Within a (start level, arm) cell, 15 cells over 87 worlds | **25 ms, worst case** |

Mean within-cell spread is 16.7 ms. No cell anywhere exceeds one frame.

## What the start level is

`movement_bench.py` polls the world until the play log has rows and then sends
`bot_corner_start` over RCON; `corner_level` is whatever `level_time` the
server happened to be at when that command landed. Nothing seeds it, nothing
waits for a phase, and the harness records it only as a bookkeeping offset. It
is recorded before fixture movement. That alone does not establish that an
arm cannot affect release timing; the treatment is configured before release.

**Every level in this record is `corner_level`**, which is the number
`result.json` reports and `--corner-level` takes. Verified equal to the leg
budget's `start_exclusive` in all 100 worlds. The leg budget's first
*touchdown* sits one frame later, at `corner_level + 25`; do not read the two
as the same number.

Across 100 worlds `corner_level` took 13 distinct values, heavily
concentrated:

| `corner_level` | worlds |
| ---: | ---: |
| 3,325 | 34 |
| 3,350 | 43 |
| 3,300 | 8 |
| 3,275 | 4 |
| 3,375 | 3 |
| eight other levels | 1 each |

## It fixes the trajectory

The first touchdown after the stand - its owner, entry speed and frame count -
is a function of the start level in 99 of 100 worlds, and the same mapping
holds on the 16 confirmation worlds collected in an earlier session:

| `corner_level` | first touchdown | leg times seen |
| ---: | --- | --- |
| 3,275 | run-up, 219.2 units/sec, 6 frames | 19,025 to 21,025 |
| 3,300 | run-up, 217.5 units/sec, 5 frames | 18,750 to 19,275 |
| 3,325 | run-up, 219.2 units/sec, 5 frames | 15,750 to 16,275 |
| 3,350 | tap, 250.2 units/sec, 1 frame | 17,500 to 19,286 |

That is 41 worlds from one session and 100 from another agreeing on every
shared level, with one exception, below.

The single exception is instructive rather than damaging. At `corner_level`
3,375, `bot_moveExitSpeed=500` produces a 5-frame run-up at 220.7 and
`bot_moveExitSpeed=2000` produces a 1-frame tap at 250.2. The credit reaches the
very first commit off the stand. **This is why the stratum has to be the start
level and not the first touchdown**: the touchdown is downstream of the setting
under test, and conditioning on it would condition on a post-treatment
variable. Start level avoids conditioning on that touchdown, but its own
relationship to treatment timing still requires care.

## What this costs, and what it buys

Three pilots were defeated by leg time before
[the refined method](2026-09-19-refined-method.md) moved the judgement onto the
leg-budget owner split. The owner split worked because the mechanism moved with
the result. This says why the raw statistic failed: at three runs an arm, an
arm's leg times are a draw over four start levels whose means differ by up to
5,000 ms, so the comparison is dominated by which levels each arm happened to
draw.

The observed within-cell repeatability motivates stratification. It does not
justify a single world per cell or remove independent repeated confirmation.

## What follows

1. **The harness should pin the start level.** Done the same day:
   [the pinned start](2026-09-20-pinned-start-level.md) adds `--corner-level`,
   which holds the corner command for a chosen level, verifies the level the
   world reports and replaces a world that misses. Two strata are reachable and
   each is reproducible to the millisecond. Acceptance floors leave it unset
   and keep sampling starts.
2. **Until it does, report the start level with every stand result**, and
   compare arms within a level. Unstratified means over fewer than roughly
   twenty worlds an arm are not interpretable.
3. Retained results are not invalidated. The stratified sweep reproduces the
   direction and very nearly the size of the effect the paired design found
   (see [the finer sweep](2026-09-20-exit-speed-finer-sweep.md)), which is the
   reassuring outcome rather than the alarming one.

## What is not settled

- Why a given start level maps to a given first flight. The levels are all
  multiples of 25 ms, so a simple frame-phase story does not obviously explain
  why 3,325 and 3,350 differ so much; nothing here looks past the correlation.
- Whether the mapping is stable across builds, maps, cases or levels. Every
  world here is `red_out_L1_stand` on Kejim on one build.
- Whether pinning the start level is even desirable for acceptance runs, as
  opposed to for A/B comparisons. A fixture that always starts on one phase
  measures one phase.

## Reproduce

The historical `sweep.py` and `analyse.py` remain with the retained evidence.
`analyse.py` reports legacy budget windows and groups by downstream touchdown
class; it is not the corrected start-stratified completion reproducer. Use
[movement_sweep_summary.py](../../../tools/movement_sweep_summary.py) with the
command in [review corrections](2026-09-20-opus-review-fixes.md). It reads official
completion results, binds input hashes, declares strata and rejects incomplete
world grids. Write a fresh output path; preserve the original evidence.
