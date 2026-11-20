# Arm B's own walk reference, and the onward test, 2026-09-17

Work order step 2: resolve the censored B candidates without changing A, and
record why a landed candidate is refused. This record carries the code change,
its native evidence and one negative result that decides the heading question.

Read [the rescue audit](2026-09-17-rescue-audit.md) first; this record closes
the diagnostic gap that audit named, and corrects its brake conjecture.

> Follow-up: see [the independent-reference correction](2026-09-17-independent-reference.md). Offer-mode references and telemetry required another correction; the turn explanation below is a hypothesis, not an established mechanism.

## What changed

`codemp/game/bot2_move.c`:

1. **Arm B gets its own walk reference.** `StraightJudge` rolled one walk, to
   the *control* arm's furthest `done` frame, and judged both arms against it.
   Any observe-only candidate finishing past that frame was reported
   `unjudged`. Arm B now gets a second walk from the same root, in its own
   `replayWalkFrames` / `replayWalkResult` buffers, rolled after the control's
   walk and after every candidate rollout, to arm B's own furthest `done`.
   The control's candidates, its walk, its ranking, its commit and its parity
   frames are untouched, and nothing the control reads is written after it
   reads it. The second walk is rolled only when an observe-only candidate was
   admitted, so default gameplay never pays for it.
2. **`no_onward` replaces the `no_second_landing` mislabel.** `SpikeJudge`
   reported `no_second_landing` whenever `done<0` with no decline, because
   `land[1]<0`; on a one-hop plan `land[1]` is structurally -1. The judge now
   names `no_first_landing`, `no_second_landing` (two-hop plans only) and
   `no_onward` distinctly. Admission is unchanged: `no_onward` was already a
   refusal under the old name.
3. **Per-candidate onward diagnostics.** `spikeMeasure_t` records `endStage`,
   `onwardMax` (the furthest the post-landing walk got from the landing mark),
   `brakeFrames` and `heldFrames`, and `[REPLAY] kind=cand` prints them along
   with `walk_ref=control|replay`.
4. **Explicit launch command boundary.** A `launch` stage event now carries
   `launch_level=<the tap's own command time>`, so an analyzer no longer needs
   `--launch-confirmation-delay-ms` for a new log. The event's own `level`
   still records the confirmation frame.

`tools/movement_replay.py` is **analyzer version 3**: the second hop's heading
delta is its own column, the new candidate fields are carried, censored pairs
are listed in the report, and `arm_isolation` checks that the observe-only arm
cannot reach the control's verdict. Every new field is optional, so a v1/v2 log
still analyses; re-running v3 over the retained corpus reproduces v2's verdicts
exactly (44/2, 52/2, 44/1 pairs/censored) at
`.codex-research/replay-v3-20260917T065058266Z`.

`tools/test_movement_replay.py` is 32 tests, all passing, including the new
hop-1 delta, wrapping, optional fields and the five isolation faults.

## Builds and runs

| Purpose | Path |
|---|---|
| Diagnostics + arm-B reference build | `.codex-research/move-build-replay-20260917T065141062Z` |
| Offer build (adds `launch_level`, `bot_moveHeadingOffer`) | `.codex-research/move-build-offer-20260917T070254469Z` |
| Native runs | `.codex-research/move-replay-20260917T065231423Z-*` |
| Analyzer v3 over the arm-on runs | `.codex-research/replay-v3-armb-20260917T070026774Z` |

Both builds compile `bot2_move.c` with no new warnings. Every run below
passed, completed, reached the objective with **`move` the only owner**, zero
deaths and **zero parity mismatches** over a 256-frame horizon.

## Result 1: the censoring is gone

| Case | Seed | Pairs | Censored before | Censored now |
|---|---|---:|---:|---:|
| `red_out_L1_stand` | small clean | 44 | 2 | **0** |
| `red_out_L1_stand` | large | 42 | 1 | **0** |
| `blue_out_L1_stand` | large | 52 | 2 | **0** |

`arm_isolation` is clean on every arm-on run: over 68 searches and 1,092
judged candidates, no replay candidate was committed, every candidate was
judged by its own arm's reference, no control candidate was judged past the
control arm's own horizon, and nothing was left unjudged.

## Result 2: neither heading dominates

With every pair decided, the comparison is no longer one-sided evidence.
Across the three routes, 138 decided pairs:

| | Both admitted | A only | B only | Neither |
|---|---:|---:|---:|---:|
| Total | 28 | **7** | **8** | 95 |

The eight B-only rescues and the seven A-only losses:

| Route | Search | Edge | Hops / run-up | hop0 delta | hop1 delta | A | B |
|---|---:|---|---|---:|---:|---|---|
| small-red | 10 | 5069→4930 | 2 / 24 | -6.57 | -2.58 | refused | admitted 719.6 |
| small-red | 19 | 3999→404 | 2 / 24 | +0.05 | **-7.87** | refused | arrives 637.9 |
| small-red | 21 | 404 | 1 / 0 | -9.37 | -9.37 | refused | arrives 349.4 |
| large-red | 9 | 51473→67559 | 2 / 24 | -3.11 | -4.90 | off_path | admitted 673.0 |
| large-red | 20 | 43554 | 1 / 24 | +1.85 | +1.85 | refused 410.9 | arrives 458.8 |
| large-blue | 14 | 85629→91633 | 2 / 24 | +10.53 | -4.96 | off_path | admitted 711.0 |
| large-blue | 19 | 78278 | 1 / 24 | +0.32 | -2.25 | **no_onward** | admitted 417.9 |
| large-blue | 23 | 93195→87500 | 2 / 0 | -6.85 | -6.83 | refused | admitted 597.7 |
| small-red | 3 | 95→337 | 2 / 0 | +6.65 | -0.28 | admitted 816.3 | off_path |
| small-red | 3 | 95→337 | 2 / 24 | +6.65 | -0.28 | admitted 847.8 | off_path |
| small-red | 14 | 556→578 | 2 / 24 | +8.97 | -3.04 | admitted 751.0 | off_path |
| large-red | 7 | 36420→38065 | 2 / 0 | -1.71 | -3.45 | admitted 710.8 | off_path |
| large-red | 7 | 36420→38065 | 2 / 24 | -1.71 | -3.45 | admitted 748.7 | off_path |
| large-blue | 3 | 89502→89768 | 2 / 0 | +0.29 | +0.77 | admitted 789.1 | off_path |
| large-blue | 24 | 86982→1649 | 2 / 0 | +8.13 | +1.76 | arrives 593.8 | refused |

Eight against seven, on three routes, with every pair decided. **Replacing the
aimed heading with the stored heading is refuted.** The audit's second-hop
reading survives: the large-blue 23 and small-red 19 rescues are hop-1 effects,
and the B losses are mostly off-path landings. The two constructions are each
better at different roots.

The change the evidence supports is therefore to **offer both** and keep the
one ranking by margin over the walk. That is a local proposal policy, not a
route-time guarantee; the original offered mode also coupled reference horizons.
The treatment below was opt-in and required the linked follow-up correction.

## Result 3: `no_onward` is a real refusal class, and it is not the brake

Every `no_second_landing` outcome in the retained corpus was a one-hop
candidate: 34 in the large-seed red run and 17 in the large-seed blue run, and
none in the small-seed probe. **Not one was a genuine missing second landing.**
The label named a hop that those plans never had.

The new diagnostics measure what actually happened. At small-seed search 2, five
candidates (four control, one replay) ended `no_onward`:

| cand | plan | end_stage | onward_max | brake_frames | held_frames | frames |
|---:|---|---:|---:|---:|---:|---:|
| 7 | straight, 1 hop | 2 | 55.8 | 0 | 0 | 92 |
| 9 | straight, 1 hop | 2 | 56.2 | 0 | 0 | 92 |
| 11 | straight, 1 hop | 2 | 58.3 | 0 | 0 | 92 |
| 15 | state, 1 hop | 2 | 56.4 | 0 | 0 | 92 |
| 19 | state, 1 hop (arm B) | 2 | 57.8 | 0 | 0 | 92 |

Their two-hop siblings from the same root reached `end_stage=5` with
`onward_max` near 312 and were admitted with margins of 449 to 476.

So the post-landing walk **neither braked nor held**. It simply did not put 128
units (`MOVE_ONWARD_DISTANCE`) between the bot and its own landing mark inside
the 48-frame (`MOVE_STRAIGHT_HOLD`) window: about 56 units in 48 frames, while
an admitted one-hop candidate elsewhere covers 128 units in 24. The audit's
momentum-brake conjecture is **withdrawn** for these roots. The large-blue
search 19 root itself has not been re-measured with these fields and stays open.

These measurements establish insufficient displacement, not its cause.
A turn is one hypothesis because `onwardMax` is displacement from the mark;
turns can also exceed the threshold. Distance travelled, objective progress
and trajectory must be measured before attributing these refusals to turning. **No admission rule was
changed here.** Loosening the onward test to obtain a faster time is exactly the
move the work order forbids without its own controlled comparison, and it is
not part of this change.

## What could not be verified, and why

The work order asks for A's selected candidate and issued command to be
identical with diagnostics off and on. **A route-level off/on identity check is
not available on this harness**, because the fixture's start is issued by an
rcon round trip at wall-clock time: two runs of the same case begin at
different level times (3425 against 3275 here) and their roots differ from the
first post-corner commit onward (`x=156.8,y=-1070.1` against
`x=156.9,y=-1070.0`). Two worlds do not share roots, so nothing about
selection can be concluded from comparing them.

Four matched repetitions per arm on the small clean seed, same build, same
case:

| Arm | Pickup times | Modal |
|---|---|---|
| diagnostics off | 20,950 / 19,775 / 19,775 / 19,775 ms | 19,775 |
| diagnostics on | 19,775 / 21,625 / 21,625 / 19,775 ms | 19,775 |

The distributions overlap on the same modal value and the spread (1,175 and
1,850 ms) is within the level-1 route quantisation already recorded in
EVIDENCE. **No off/on difference is detectable at four repetitions**, and that
is a statement about the measurement, not a demonstration of identity.

What *is* verified is within one run and does not depend on reproducibility:
the isolation checks above, over 1,092 candidates; the ordering in
`StraightJudge` (arm-B candidates are constructed and rolled out after every
control candidate, arm B writes only its own result fields and its own walk
buffers, and the control's `maxDone` and ranking read `!r->variant`); and zero
parity mismatches in every arm-on run, which is the harness's own test that the
extra rollouts left live physics untouched.

## The opt-in treatment: `bot_moveHeadingOffer`

Default off; unset or zero is exactly today's behaviour. Nonzero arms the
stored-heading construction and makes it an ordinary proposal: it is judged by
the same admission rules, shares the control's single walk reference, counts
toward `stateAdmitted` so an edge it validates is not avoided, and is ranked
with the control alternatives by margin over the walk. It is **not** installed
into GameData and is **not** a default.

Its matched control/treatment campaign is the next step and its result is not
in this record. Nothing here claims the offer is an improvement.

## Reproduction

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
python -m unittest discover -s tools -p test_movement_replay.py
$out = '.codex-research/replay-v3-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
python tools/movement_replay.py `
  .codex-research/move-replay-20260917T065231423Z-armon/red_out_L1_stand `
  .codex-research/move-replay-20260917T065231423Z-stands-armon/red_out_L1_stand `
  .codex-research/move-replay-20260917T065231423Z-stands-armon/blue_out_L1_stand `
  --launch-confirmation-delay-ms 25 --out $out
```

Those runs were collected by the build that predates `launch_level`, so they
still need the 25 ms binding; `HopStep`'s cadence is unchanged, so the binding
still holds for them. A log from the offer build carries `launch_level` and
needs no such option.

## Next executable step

1. Run the matched `bot_moveHeadingOffer` control/treatment campaign on both
   stand cases with repeated trials, then the named regression cases
   (`blue_out_L1_respawn5`, `red_ret_L1_stand`, `red_ret_L1_respawn4`), then
   the 12-case seeded level-1 arm and the 36-case unseeded floor. Promote
   nothing on fewer matched runs than the recorded spread needs.
2. Re-measure the large-blue search 19 root with the onward fields, which is
   the one audited rescue still without a mechanism.
3. Only then consider the onward test itself, as its own controlled
   experiment, with the falling-endpoint regression kept.
