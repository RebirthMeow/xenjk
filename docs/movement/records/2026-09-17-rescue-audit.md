# Heading-rescue audit, 2026-09-17

Work order step 1: audit every retained B-only admission and every censored
pair before changing candidate selection. This record is analysis of the
retained analyzer-v2 corpus and the native `[REPLAY]` telemetry it was
computed from. **No native world was started, no source was rebuilt, and no
gameplay setting was changed for this record.**

Read [the correction record](2026-09-17-replay-correction.md) first; its
tables are unchanged by this audit. This record explains *what* the three
B-only admissions and the five censored comparisons actually are, and
rejects the single-cause reading of them.

## Inputs

Analyzer-v2 outputs under
`.codex-research/move-replay-correction-20260917T061951996008Z`, and the
native logs they bind:

| Arm | Native log | Paired candidates |
|---|---|---:|
| large-seed red outbound | `move-replay-20260917T040150120Z-stands/red_out_L1_stand` | 44 |
| large-seed blue outbound | `move-replay-20260917T040150120Z-stands/blue_out_L1_stand` | 52 |
| small clean-seed red outbound | `move-replay-20260917T040150120Z-red-probe/red_out_L1_stand` | 44 |

Pairs are (`edge`, `edge2`, `hops`, `runup`)-matched `MOVE_PLAN_STATE`
candidates; the `MOVE_PLAN_STRAIGHT` candidates in the same searches have
no arm-B twin and are not paired. Search IDs are local to each run.

## Per-arm outcome distribution

Neither arm dominates. A blanket switch to the stored heading is not
supported by these totals; it trades refusals for off-path landings.

| Arm | A outcomes | B outcomes |
|---|---|---|
| large-red | refused 20, off_path 12, admitted 10, corridor 2 | refused 16, off_path 15, admitted 9, corridor 3, unjudged 1 |
| large-blue | refused 25, corridor 15, admitted 8, off_path 2, no_second_landing 1, arrives 1 | refused 32, admitted 8, corridor 8, off_path 2, unjudged 2 |
| small-red | refused 16, admitted 13, off_path 12, corridor 3 | off_path 18, admitted 10, refused 9, corridor 3, unjudged 2, no_path 1, arrives 1 |

Median absolute heading delta is 0.58 deg (large-red), 4.29 deg (large-blue)
and 4.24 deg (small-red); the maxima are 5.77, 10.53 and 9.71 deg.

## The three B-only admissions

### Large-blue search 23, level 20400, edge 93195 to 87500, two hops, run-up 0

Root `pos=439.340,-65.689,1808.125 speed=250.318 view_yaw=-54.9976`.

| | hop0 yaw | hop1 yaw | land0 | land1 | outcome |
|---|---:|---:|---|---|---|
| A cand 12 | -112.8943 | -106.3560 | frame 33, cell 8824, 358.1 u/s | none in 117 frames | refused, `unsupported=256` |
| B cand 16 | -119.7449 | -113.1857 | frame 33, cell 9135, 353.9 u/s | frame 57, cell 4949, 367.0 u/s, **+52.7 z** | admitted, margin 316.0, progress 597.7 |

The two hop-0 headings differ by 6.85 deg and land 22 units apart. From A's
landing the second hop never lands inside the horizon and the rollout is
refused at a trigger (`BOT2_FORECAST_TRIGGER`, bit 8); from B's landing the
second hop climbs 52.7 units onto a ledge and the chain composes. This is a
**genuine second-hop composition rescue by the stored heading.**

### Small-red search 19, level 20400, edge 3999 to 404, two hops, run-up 24

Root `pos=2365.570,4647.228,1857.484 speed=354.080`, `goal_distance=672.020`.

| | hop0 yaw | hop1 yaw | land0 | land1 | outcome |
|---|---:|---:|---|---|---|
| A cand 13 | 83.6526 | 84.8722 | frame 29, cell 1417, 352.2 u/s | frame 62, cell 1356, `2417.578,5194.928` | refused, `unsupported=256` |
| B cand 17 | 83.6973 | 77.0054 | frame 29, cell 1417, 352.2 u/s (identical) | frame 62, cell 871, `2452.830,5191.058` | arrives, margin 155.1, progress 637.9 |

The hop-0 headings differ by 0.045 deg and the first landings are identical
to three decimals. The whole difference is hop 1: 7.87 deg, and a final
position 35 units further along the flag's axis. Both rollouts are refused
at a trigger; only B's projected hull contacts the objective under
`ArrivesAt`, so only B is rescued to `arrives`. Again a **second-hop**
effect, expressed as an arrival rather than as a landing.

### Large-blue search 19, level 18950, edge 78278, one hop, run-up 24

This one is **not** a heading effect and must not be counted as one.

| | hop0 yaw | land0 | frames | done | outcome |
|---|---:|---|---:|---:|---|
| A cand 15 | -91.4145 | frame 37, cell 9496, 372.899 u/s | 86 | -1 | no_second_landing |
| B cand 19 | -91.0956 | frame 37, cell 9496, 373.699 u/s | 86 | 61 | admitted, margin 78.286 |

The headings differ by 0.32 deg, the landing frame is the same, the landing
cell is the same and the landing speeds differ by 0.8 u/s. The same search
also admits A cand 7 (a straight candidate, run-up 24, yaw -90.4215, margin
77.984) and A cand 14 (state, run-up 0, same yaw as cand 15, margin 46.920,
committed), and refuses A cands 6, 8, 9, 10 and 11 with the same label. The
admitted/refused split across those ten candidates is not monotonic in yaw,
in landing cell or in landing speed.

## Two labelling defects this exposes

1. **`no_second_landing` is structurally wrong for one-hop candidates.**
   `SpikeJudge` (`bot2_move.c`) reaches that branch whenever `m->done<0`
   with no terminal decline, and then reports `no_second_landing` because
   `m->land[1]<0`. For a one-hop plan `land[1]` is *always* -1. The actual
   condition is "the executor never advanced past `Onward(plan)`".
2. **`done` latches one stage late.** `SpikeMeasure` sets `done` on the
   stage transition `s==Onward(plan)`, that is, when the executor leaves the
   onward stage, and `MoveStep` only leaves it after the walk has covered
   `MOVE_ONWARD_DISTANCE` (128 units) from the landing mark. Reaching the
   landing is therefore not enough: a candidate must also walk 128 units
   inside the `MOVE_STRAIGHT_HOLD` (48-frame) window, or be refused with a
   label that names a second landing it never planned.

Both A cand 15 and B cand 19 held for the full 48 frames (86 frames from a
frame-37 landing) with no decline, so the post-landing walk ran and simply
did not cover 128 units in A's case. Candidate mechanisms are `WalkStep`'s
momentum brake, which sets `speed=0` without declining, the path-target
`stop` clamp, which lowers the walk speed without declining, or a landing
followed by a turn, which keeps the bot near its own landing mark. **Which
one fired is not recorded**: `ex_braked` is logged on the `kind=root` line
only, never per candidate rollout. This is the diagnostic gap to close
first. It was closed the same day, and the measurement is in
[the arm-B reference record](2026-09-17-arm-b-reference.md): at the
`no_onward` roots measured there the walk neither braked nor held, so the
brake is not the mechanism at those roots. The large-blue search 19 root
itself has not been re-measured and stays open.

## The five censored comparisons

Every censored B candidate had a real `done` frame and real progress. None
was a failure, and none may be counted as a rejected policy.

| Arm | Search | Edge | Hops / run-up | A | B done / progress | Why censored |
|---|---:|---:|---|---|---|---|
| large-red | 13 | 60955 | 1 / 24 | refused (trigger) | 59 / 369.4 | no control admission |
| large-blue | 14 | 85629 | 1 / 24 | admitted, committed, done 60, progress 377.3 | 62 / 391.7 | walk truncated at 60 |
| large-blue | 14 | 85629 to 91633 | 2 / 24 | off_path | 91 / **711.0** | walk truncated at 60 |
| small-red | 10 | 5069 to 4930 | 2 / 24 | refused (trigger) | 91 / **719.6** | no control admission |
| small-red | 21 | 404 | 1 / 0 | arrives, committed, done 49, progress 348.5 | 50 / 349.4 | walk truncated at 49 |

The mechanism is one block in `StraightJudge`:

- `maxDone` is accumulated from **control** candidates only
  (`if(StraightAdmitted(...) && !r->variant)maxDone=max(...)`).
- The walk reference is rolled out only `if(maxDone>0)`, and only to
  `maxDone` frames.
- Any candidate whose `walkFrame = done-1` exceeds `straightWalkResult.count`
  without a stop frame is set to `unjudged`.

So censoring has exactly two forms, and both appear here:

- **No control admission at all** (large-red 13, small-red 10): the walk is
  never rolled out, `maxDone` stays 0, and every B admission at that root is
  unjudged regardless of quality. Both of these B candidates reached `done`
  with 369 and 720 units of progress.
- **Truncation by the control's horizon** (large-blue 14 twice, small-red
  21): B finished 1 to 31 frames later than the furthest control `done`, so
  its own completion frame lies past the reference that exists.

Large-blue search 14 is the most consequential: the committed control
candidate made 377.3 units of progress by frame 60, while the unjudged
two-hop B candidate made **711.0 units by frame 91** from the same root.
That is not a decided comparison, because no walk reference exists at frame
91, which is precisely the missing measurement and not a result.

## Conclusions

1. The stored-heading hypothesis is **partly supported and partly refuted.**
   Two of three B-only admissions (large-blue 23, small-red 19) are real,
   and both are **second-hop** rescues with 6.9 and 7.9 degrees of hop-1
   heading difference. Neither is a hop-0 effect.
2. The third B-only admission (large-blue 19) is **not** a heading rescue.
   It is a 0.32-degree pair whose outcomes are split by the onward-progress
   test and its mislabel, most likely through an unrecorded walk brake.
3. No B-only admission demonstrates a faster executed route. B was
   observe-only throughout; every one of these is a local admission.
4. Arm-level totals refute a blanket switch: B trades refusals for off-path
   landings and is worse overall on small-red.
5. All five censored results are **missing measurements**, produced by
   deriving B's walk reference from the control arm's horizon. Two of them
   involve B candidates with roughly twice the committed control's progress.

## Next executable step

Resolve the censoring before testing any selection change, as the work order
requires, and record the brake at the same time:

1. Give arm B its own walk reference, rolled from the same original
   pre-command root inside the same guard, extended to B's own furthest
   `done`. Leave the control's candidates, walk reference, ranking, commit
   and parity frames identical. Emit judged, censored and refused distinctly,
   and verify the committed candidate and issued command are identical with
   diagnostics off and on, including at roots where B needs the longer
   horizon.
2. Report the onward test honestly: replace the `no_second_landing` label
   for one-hop plans with one that names the onward condition, and record per
   candidate whether the post-landing walk braked, how far it travelled from
   the landing mark, and the stage it ended in.

Only then is step 3 (offering a rescued stored-heading policy through the
existing admission rules) testable, and only on the two genuine second-hop
pairs, not on large-blue search 19.
