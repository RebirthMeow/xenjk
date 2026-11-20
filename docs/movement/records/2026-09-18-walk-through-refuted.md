# The walk-through repair is refuted; the stale mechanism is measured

An attempt to repair the two blocking defects - the landing that is a dead
end, and the planner/executor arrival disagreement - as one root cause. The
repair is **safe and ineffective**, and the diagnosis behind it was drawn
from an atypical sample. What the arms did produce is a measurement of the
real mechanism, which is the useful part of this record.

`bot_moveWalkThrough` is default off, measured, and **not qualified**. Do not
default it.

## The hypothesis

`PathTarget` ends the walk's lookahead on the takeoff cell of a strafe chord
ahead, so the walk aims at the takeoff as a destination. The retained
`onward-bound-20260918-b` log shows, in one frame on `blue_out_L1_stand`:

```
edge=strafe reason=stale  live_speed=28.0  entry_speed=510.9  stale_count=4
edge=onward reason=bound  held=41          speed=35.2
```

A bot at 28 units/sec in front of an edge needing 510.9, with the onward
stage of the plan that landed it there held at the same frame. That reads as
one cause for both defects: a walk that arrives at rest on the takeoff can
never satisfy an entry envelope, and never clears MOVE_ONWARD_DISTANCE
either, so the replan gate waits for ever.

The change carries the target through the takeoff along the chord's own
`entryDir` by the unused lookahead, so the walk accelerates through the gate
rather than stopping on it.

## What the arms say

Build `move-build-walkthrough-20260918-a`, seed
`move-explore-20260916-b/seed.txt` (sha `3e5b1a9dda53`), the standard
configuration `--edges --parity 256 --level 1`, worlds under
`.codex-research/wtc-seed-{off,on}-r{1,2}`.

| arm | rep | passed | total ms | commits | mismatches |
|---|---:|---|---:|---:|---:|
| off | 1 | 12/12 | 230,550 | 74 | 0 |
| off | 2 | 12/12 | 232,200 | 70 | 0 |
| on | 1 | 12/12 | 236,750 | 78 | 0 |
| on | 2 | 12/12 | 234,825 | 72 | 0 |

The control matches the retained references (230,425-233,950 ms, 70-77
commits), so the arm is comparable.

The mechanism counts, which are the point:

| mechanism, one 12-case arm | off | on |
|---|---:|---:|
| strafe edges stale | 45 | 44 |
| onward stage held over 1 s | 138 | 138 |
| stale `live_speed` median | 250.0 | 250.0 |
| stale `entry_speed` median | 400.0 | 413.3 |

45 to 44 and 138 to 138 is not a repair. Within-arm spread is 1,650 and
1,925 ms against a between-arm mean of +4,412 ms; the retained lesson that
one 12-case arm moved 7,100 ms between two arms running the same code means
n=2 cannot separate that from noise. The honest reading is no benefit, and
possibly a cost.

## Why it failed: the diagnosis generalised from one sample

**The typical stale edge is not entered at rest.** Its median `live_speed` is
250.0 - the walk's own wishspeed ceiling - against a median `entry_speed` of
400. The 28.0 sample the hypothesis was built on is real but rare.

A walk cannot reach 400 by any steering change, because 250 is what the
ground wishspeed buys. Only a landing from a preceding chord carries 400. So
those edges are downstream links of a chain: the bot fails or refuses the
first chord, walks to the second chord's takeoff at walking speed, and every
edge after it is stale by definition. Carrying the walk's target through a
takeoff cannot help, because the deficit is not steering - it is that the
bot arrives walking where the plan expected it to arrive flying.

This is chain breakage, already measured in
[where the stand time goes](2026-09-18-where-the-stand-time-goes.md) as 107
broken chains and 18,429 units/sec lost. This record adds the entry-side
number: the chain's downstream edges need about 400 and a walk delivers 250.

The dead-end landing is also untouched and is **frequent**: 138 onward-held
events in a 12-case arm that passed every case. It is not only the rare
174-second deadlock; the stage is held over a second, repeatedly, in runs
that finish.

## Four measurement errors, retained

All four are mine, and all four produced results that looked like findings.

1. **Wrong seed.** `move-explore-20260916-merged` instead of
   `move-explore-20260916-b`, which every 09-17 and 09-18 comparison arm
   uses. Produced 0/12 with `commits=0`.
2. **Off-configuration, twice.** Both 36-case floors and the first seeded
   arms ran with the argparse defaults `--parity 0` and no `--edges`, while
   60 of the 69 retained 36-case floors use `(edges=True, parity=256)`. With
   the parity harness inert the validation that commits chords never runs,
   so `commits=0` and `strafe_planned=0`. This produced two false
   conclusions that were stated before being checked: that the unseeded
   floor does not exercise the fix, and that the working tree had regressed
   the seeded arm to 1/12. Neither is true.
3. **A control staged after the edits.** The step 0 build was staged, then
   sources were edited, so the runner refused it - correctly. A control arm
   has to be collected before the change, not after.

The bench binds build, cases, settings and graph in `result.json`, which is
what made every one of these recoverable. It does **not** record
`--parallel`, so a load-related confound cannot be tested from retained
results; that is a gap in the run binding.

## The floor is stochastic, and one run has been over-read

Across the 60 retained 36-case floors in the standard configuration:

- 6 runs (10 %) have at least one death
- 20 deaths in total, 0.33 per run, but **all 20 fall in those 6 runs** -
  3.3 deaths per affected run
- 17 distinct cases have died; the worst is `blue_out_L1_respawn4` at 3/60

Deaths cluster by run, not by case. Per-case randomness would scatter single
deaths across many runs; clustering points to a per-run factor, which is
consistent with the fixture start being an rcon round trip at wall-clock
time so that no two runs share a root.

PLAN's gate says safety is binary - no green case may turn red - and records
throughout report a single 36-of-36 as establishing it. On this evidence a
single clean floor is about a 90 % outcome on a healthy build, so it is weak
evidence of safety, and a single 35-of-36 is weak evidence of harm. A run
with several deaths is more likely a bad run than a regression and should be
repeated rather than attributed.

## Verification

Seeded control 12/12 matching the retained reference, all four arms 12/12
with zero parity mismatches and no forbidden spend. 94 movement tool tests
pass. The cvar is default off; with it unset the build is the tree's own
behaviour.

## What this leaves

The two blocking defects are **not fixed**. What is now measured rather than
assumed:

- the stale edge's entry deficit is 250 against 400, so the repair has to
  put the bot at a chain's takeoff *flying*, not steer it there faster
- the onward stage is held over a second 138 times in a passing 12-case arm

Both point at the same place: the first chord of a chain. Until a chain's
first hop is reliably flown, every edge behind it is stale by construction,
and no walk-side change reaches that.
