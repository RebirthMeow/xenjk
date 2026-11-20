# The exit-speed term: implemented, safe, and not sufficient

[Where the stand time goes](2026-09-18-where-the-stand-time-goes.md) found
that the commit judge ranks admitted candidates by the progress they make
inside the horizon and has no term for the speed a plan hands to the next one,
while 80.6 per cent of a stand run is flight bought with exactly that speed.
This record adds that term and measures it.

It is default off (`bot_moveExitSpeed`). It is safe, which the earlier
continuation passes were not. It does not improve the stand times, and the
telemetry says plainly why.

## What the change does

In `codemp/game/bot2_move.c`:

- `ExitCredit()` is the candidate's measured exit speed - the last landing's
  speed, the state the next plan would search from - credited as the distance
  it carries in `bot_moveExitSpeed` milliseconds. A `tap_onward` candidate
  uses its successor's landing speed. A candidate whose last landing was never
  measured earns nothing, so an unmeasured exit can never outrank a measured
  one; an `arrives` candidate earns nothing, because the route is over.
- The ranking compares `score = margin + credit` where it compared `margin`.
- The commit event carries `exit_ms`, `exit_credit`, `score` and `margin`, so
  every commit says what it was chosen on.

**Admission is untouched.** A refused candidate stays refused and
`MOVE_STRAIGHT_GAIN` is still tested against the margin alone, so the term can
only change which admitted candidate wins. That is also its limit; see below.

## Measured

`.codex-research/exit-speed-20260918-a`, build
`move-build-exitspeed-20260918-a`, seeded graph, parity horizon 256, level 1,
18 repetitions per arm per stand.

| arm | stand | median | mean | max |
|---|---|---:|---:|---:|
| control | blue | 19,925 | 19,983 | 20,650 |
| `bot_moveExitSpeed=1000` | blue | 19,750 | 20,501 | 22,925 |
| `bot_moveExitSpeed=2000` | blue | 19,675 | 20,261 | 21,550 |
| control | red | 19,150 | 19,528 | 20,775 |
| `bot_moveExitSpeed=1000` | red | 19,150 | 19,986 | 23,100 |
| `bot_moveExitSpeed=2000` | red | 19,150 | 19,867 | 23,100 |

The medians move by at most 250 ms on blue and not at all on red, while the
means and the maxima get **worse** in every exit arm. A 500 ms arm at six
repetitions is in the same place. There is no improvement here.

Safety is clean, and this is the first continuation-related change in this
series of which that is true: the 36-case floor is **36/36** with the term
unset, at 1000 and at 2000; the named regressions pass at 1000; and there is
no death in 105 stand runs. One control run is `incomplete` with
`corner start refused`, an rcon setup failure of the environment, not of the
bot; it is excluded rather than counted.

## Why it cannot do more on its own

The term ranks admitted candidates. Over six repetitions the committed
run-up mix is:

| arm | immediate (`r0=0`) | run-up (`r0=24`) |
|---|---:|---:|
| control | 87 | 104 |
| exit 1000 | 86 | 113 |
| exit 2000 | 90 | 96 |

It is barely getting a choice, and the previous record already measured why:
at **66 of the 81** broken chains where a run-up was committed, there was no
admitted immediate candidate at all - the immediate tap was refused, mostly
`off_path`. A ranking term cannot promote a candidate that admission never
passed. The term corrects the metric; the binding constraint is upstream of
the metric.

## What this leaves

The metric is now right and the change is safe, so it is a component, not a
result. The pairing it was built for is untested: path-free admission at the
break (`bot_moveReroute` bit 1, which admits the landing the path gate
refuses) **with** the exit term ranking what that admits. Admission alone was
measured slower and produced a stale-edge storm from installing a route at the
commit; ranking alone is measured here and does nothing. Neither has been
measured together, and together is the only configuration in which either
mechanism has something to work with.

Next, in order:

1. The combined arm: `bot_moveReroute=1` with `bot_moveExitSpeed=2000`,
   against this same control, with the floor and the named regressions.
2. If that still does not move the medians, the assumption that a faster exit
   is worth more than a better launch point needs testing directly rather than
   as a ranking weight - the credit horizon is a free parameter and 500, 1000
   and 2000 ms all behave alike, which is itself evidence that the term is not
   what is deciding these runs.
3. The tap deaths and the onward deadlock, unchanged.

## Limits

One build, one campaign, level 1, two stands. Times are quantised and the
arms' spreads overlap heavily; the medians quoted differ by less than one
common sample gap, which is why the means and maxima are quoted beside them
rather than a median difference being claimed as an effect. The commit mix is
from six repetitions per arm, not eighteen.
