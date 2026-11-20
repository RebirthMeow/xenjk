# The tap continuation, measured correctly, and its budget

The [reroute admission record](2026-09-18-reroute-admission.md) left the tap
continuation (`bot_moveTapOnward`) as the mechanism the human route needs: a
landing the walk cannot leave should be continued the way a chain continues,
with a tap on the landing frame. This record reports two defects in that
pass's own measurement, the corrected arm, and one hypothesis the corrected
telemetry raised and then refuted.

Nothing here is installed. `bot_moveTapOnward` and the new
`bot_moveTapBudget` are default off, the default floor is 36/36 with both
unset, and no stationary run under 18.475 s exists in either campaign.

## Two defects in the measurement

In `codemp/game/bot2_move.c`:

- `TapMeasure` scanned for the successor's tap from frame 0. The candidate's
  own hops also carry an `upmove` from a grounded frame, so on every
  candidate the first match was the candidate's own launch, not the
  continuation's. The scan now starts at `measure.land[count-1]`, the last
  landing, which is what the function's comment already described.
- The `[TAPONWARD]` line reported `walk_outcome` by re-evaluating
  `TapRescuable(r)` after `r->outcome` had been overwritten with the tap
  pass's own verdict, so it always printed `n/a` and the walk's refusal was
  lost. The refusal is now captured before the rollout and printed.

The first defect made every admission decision read the wrong frames. Before
the fix, on the jump-6 root, every admitted line read `launch=0` or
`launch=6` with `land_speed` near 261 - the candidate's own landing. After
the fix the same root reads `launch=33` to `launch=70` with `land_speed`
between 475.9 and 499.6. The pass had been admitting landings on the
evidence of a hop it had already measured.

## The corrected arm

`.codex-research/tap-onward-20260918-b`, build
`move-build-tap-20260918-g`, seeded graph
`move-explore-20260916-b/seed.txt`, parity horizon 256, level 1, six to
seven repetitions per arm per stand. Stand medians in ms:

| arm | red_out_L1_stand | blue_out_L1_stand | safety |
|---|---:|---:|---|
| control | 19,150 (19,050-20,700) | 20,650 (19,925-21,275) | floor 36/36 |
| `bot_moveTapOnward=1` | 19,750 (19,000-21,675) | 21,600 (20,575-22,350) | - |
| tap + `bot_moveOnwardBound=1` | 19,650 (19,050-21,675) | 22,375 (18,475-23,225) | floor 35/36, death `blue_out_L1_respawn4` |
| tap + reroute + bound | 25,650 (20,950-25,650) | 22,463 (21,575-23,350) | two deaths on `blue_out_L1_stand` |

No arm improves on the control median at either stand. The named
regressions (`blue_out_L1_respawn5`, `red_ret_L1_stand`,
`red_ret_L1_respawn4`) pass under tap + bound. The human demonstrations
recorded in [three human demonstrations](2026-09-17-human-demonstrations.md)
complete in 12.875, 14.800 and 18.550 s; the fastest run in either campaign
is a single 18,475 ms blue sample inside the control's own spread.

The jump-6 probe moved with the fix, from 14.550 to 12.075 s at force 0 and
from 11.425 to 12.300 s at force 100. These are single runs on a quantised
clock and are a direction, not a result.

## What the corrected telemetry showed

On the jump-6 root, 29 tap passes: 10 admitted, 19 refused `no_landing`, and
every admitted line landed on the rollout's **last** frame - `launch=33
land=72 frames=73`, `launch=37 land=69 frames=70`, `launch=66 land=98
frames=99`. That is not a physical refusal. The rollout was ending while the
successor was in the air, so the pass could only admit a successor whose
whole flight fitted inside the budget the walk had been given, and a human
chain's hops are longer than that.

## The budget, and why it is not the answer

`bot_moveTapBudget` grants the successor that many extra frames once it is
airborne. The run-up and the tap keep the 48-frame hold; the walking
reference is still rolled to the same frame as the longest extended
candidate. It deliberately gives the flight more frames than the walk it is
compared against had, which is why it is a diagnostic and not a default.

The first implementation extended only the tap pass's own check and did
nothing: the hold that actually ends the rollout is `straightStopTime`, which
`StraightCommand` starts at the first onward frame. Frames such as
`launch=33 frames=68` with a 96-frame allowance proved it. The budget is now
added to that hold too, at the frame the successor leaves the ground.

`.codex-research/tap-budget-20260918-a`, build
`move-build-tapbudget-20260918-b`, same fixture settings, six repetitions:

| arm | red_out_L1_stand | blue_out_L1_stand | deaths |
|---|---:|---:|---|
| control | 20,150 (19,150-20,700) | 20,650 (19,675-20,650) | 0 |
| tap, budget 0 | 19,900 (19,150-21,675) | 20,850 (20,075-22,350) | 1 |
| tap, budget 48 | 19,525 (19,050-21,675) | 21,150 (20,575-22,350) | 0 |
| tap, budget 96 | 19,900 (19,900-21,675) | 20,850 (18,475-22,350) | 1 |

The hypothesis is refuted. The budget changes the distribution of tap
outcomes - at force 0 the admissions rise and a new `fell_or_refused` class
appears, which is the continuation flying on and falling - and it does not
move either stand median outside the control's spread. The floor with
`bot_moveTapBudget=48` is **34/36**, with deaths at `blue_out_L1_respawn4`
and at `red_ret_L1_stand`, the safety case that caught the earlier reroute
fall. Three deaths appear across the two campaigns and every one of them is
in an arm with the tap pass on.

## What this means

Admitting the continuation is not what separates the bot from the human
runs. With the measurement honest, the pass admits real continuations at
476-500 units/sec and the stand times do not move; the cost it does produce
is deaths in default-floor cases. The deficit is therefore not located at
the admission test alone, and the next work should not be a further
relaxation of it.

Open, in order:

1. The deaths. Three in arms with the tap pass on, one of them on
   `red_ret_L1_stand`. A landing the search admits and then cannot survive is
   a defect in the pass, not a tuning constant, and it must be understood
   before any further tap work.
2. The `no_landing` majority at budget 0 is now known to be the walk's hold;
   at budget 96 a substantial share remains, which means those successors
   genuinely do not land. Where they go is unmeasured.
3. The two older defects named in [HANDOFF.md](../HANDOFF.md) - the landing a
   walk cannot leave, and the planner/executor arrival disagreement - remain
   the blocking items. Neither campaign here addresses them.

## Verification

Both campaigns ran on one build each, with no source edit between arms.
Parity mismatches are zero in every arm. `floor-default` is 36/36 on both
builds with the diagnostics unset, so nothing moved while they were off.
Stand repetitions are separate worlds with non-overlapping run sets; per
[LESSONS.md](../LESSONS.md), single samples are not read as differences, and
no seeded-arm aggregate is quoted here.
