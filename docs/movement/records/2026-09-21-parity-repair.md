# The parity harness compares again: 19,413 frames, zero mismatches, and a control that proves the zero

Follow-up: [mixed-refusal guard repair](2026-09-21-parity-review-fix.md).
The results below describe the earlier build. Review found that its trigger
exception omitted the independent `animationChanged` refusal; the follow-up
restores that guard and carries separate validation.

Recorded 2026-09-21, under the owner's direction to restore meaningful
forecast/execution parity comparisons.
[The diagnosis](2026-09-20-parity-zero-comparisons.md) named two defects; this
repairs both, takes the floors, and then deliberately reintroduces one of them
to show the repaired harness reports what it is supposed to report.

Build `.codex-research/parity-fix-build-20260920-a`, control build
`.codex-research/parity-sharedbuf-20260920-a`, evidence and floor runner in
`.codex-research/parity-triggers-20260920-a/`.

## The change

Two edits in `codemp/game/bot2_move.c`, both inside the harness:

1. **The armed prediction gets its own frames.** `RolloutRequest` points every
   rollout at `parity.frames`, so the searches overwrite the prediction within
   the same frame. `ParityBuild` now fills `parity.built`, which nothing else
   touches.
2. **A rollout stopped only by a trigger contact arms over its prefix.** The
   walk this harness forecasts is heading for the objective, whose pickup box
   is a trigger, so refusing the whole build discarded every comparison. The
   contact frame is compared too - `G_TouchTriggers` runs after the Pmove
   boundary the harness compares at - and the horizon ends there. The tally
   carries `truncated=1` so a truncated horizon is never read as a full one.

Anything else unsupported, an unrestored world, a changed world or an empty
rollout still refuses exactly as before.

## What it measures now

One probe world, research configuration, `red_out_L1_stand`, the same start
level (`corner_level 3350`) as the diagnosis worlds:

| | Frozen build | Repaired build |
| --- | ---: | ---: |
| Frames compared | **0** | **743** |
| Forecast refusals | 119 | **0** |
| Horizon tallies | 0 | 4 |
| Mismatches | 0 | 0 |
| Completion | 17,575 ms | 17,575 ms |

Floors on the repaired build, research configuration unchanged:

- **36 of 36 pass**, zero deaths, movement owner only, **19,413 frames
  compared, zero mismatches, zero refusals**, 63 horizon tallies.
- Both seeded named regressions pass, 842 frames compared, zero mismatches.
- Every one of the 63 tallies is `truncated=1`, so the truncation path is what
  makes any comparison possible here, and **41 of 63 carry `planned=1`** - a
  horizon that covered a committed sequence, which is the signal the
  composition gate was written to read.

`compared` is the cumulative count carried by the periodic tally, which fires
only when a horizon is fully consumed on a tally-eligible rollout, so these
are **lower bounds**; two cases report 0 because no tally fired, not because
nothing was compared. Per case the median is 504 frames and the maximum 833.

## The zero is not vacuous

A comparison that cannot fail reports zero just as readily as one that passes.
So the first defect was reintroduced on its own - the parity build writing
into the shared buffer, every other line identical - and the same world was
run again:

| | Repaired | Shared buffer restored |
| --- | ---: | ---: |
| Mismatches | **0** | **935** |
| By reason | - | `after_state` 935 |

All 935 are `after_state`, and only `after_state`. That is exactly the
signature the source reading predicted: the `boundary_time`, `before_state`,
`command` and `executor_state` comparisons all run **before** that frame's
searches and see intact frames, while `after_state` runs after them and reads
the overwritten buffer. The harness detects disagreement, it detects it in the
one comparison that was corrupted, and the repaired zero is therefore a
measurement rather than an absence.

## What this does not establish

- **Nothing is installed.** The harness runs under `bot_moveParity`, which the
  bench sets and the game does not. No installed configuration changes.
- Zero mismatches over 19,413 frames qualifies forecast/execution agreement
  **for the frames that were compared on these 38 cases**, at horizons that
  all ended at a trigger. It does not qualify agreement past a trigger
  contact, and on this map the objective is always one.
- The probe world's **pre-start** commit differs between the two builds (level
  2775 against 2825) at the same start level, and the repaired build adds one
  commit after the trip completed. Every commit of the timed trip is identical
  and so is the completion. The plausible reading is CPU pacing - the repair
  removes about 119 forecast rollouts per world and the pre-start phase is
  wall-clock dependent - but that is unmeasured, and the floors are
  outcome-only so they do not settle it.
- The control build's completion (19,675 ms) is not a treatment effect and no
  comparison of times with it is claimed; it exists to show mismatches appear.
- One map, one case family, one build. Other cases may refuse for reasons this
  fixture never produces.

## Reproduce

```powershell
python tools/strafe_fixture_build.py --out <new build dir>
python tools/movement_bench.py --build <new build dir> `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 55720 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1
```

The floors are `.codex-research/parity-triggers-20260920-a/floors.py`. The
bench refuses a build whose sources differ from the checkout, so the control
build is reproduced by putting `request.frames=parity.frames` back, building,
running, and restoring the line.
