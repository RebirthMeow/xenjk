# The planned-candidate preference costs the slow class 3,150 ms and buys the fast class 150

> **Superseded 2026-09-20 by the full grid.**
> [Removing the preference loses 1,162.9 ms weighted](2026-09-20-preference-removal-refuted.md).
> The two classes measured here are the extremes of the completion range. At
> the two classes in between, which carry 44 of the 93 weighted starts,
> `bot_moveStatePrefer=0` **regresses**: +3,025 ms at class 125 and +1,775 ms
> at class 100, against the -3,150 ms here at class 75. Declared-weight mean
> 17,394.4 ms on against 18,557.3 off; equal-weight 18,500 against 18,950.
> The measurements below stand; the suggestion that the preference is what
> costs the slow class **generalizes to nothing** - it is right at three
> classes of four. `bot_moveStatePrefer=1` remains the configuration.

Recorded 2026-09-20. **No native source change, no rebuild** - every world is
the frozen diagnostic build `.codex-research/select-override-build-20260920-a`
with the forced-candidate override off, so its behaviour is the reference
build's, and `bot_moveStatePrefer` is an existing cvar. Evidence, driver and
floor runner in `.codex-research/state-prefer-classes-20260920-a/`.

This tests the mechanism named by
[the second start class](2026-09-20-selection-second-start-class.md): at start
class 75 the candidate that executes at commits 4 and 5 is a planned **state**
candidate scoring below an admitted straight candidate that is faster, while
at class 150 the executed candidate is the score maximum at all three frames
measured. `bot_moveStatePrefer=0` removes that preference.

## The arms

Both start classes pinned, `bot_moveExitSpeed=500` and every other research
cvar unchanged, the forced-candidate override off in every arm, three
repetitions per arm, arms alternated. **12 cells of 12, none incomplete, 20
native worlds**, one distinct completion per arm.

| Start class | `bot_moveStatePrefer=1` | `bot_moveStatePrefer=0` | Difference |
| --- | ---: | ---: | ---: |
| 75, the slow class | 21,125 ms | **17,975 ms** | **-3,150 ms** |
| 150, the fast class | 17,575 ms | 17,725 ms | +150 ms |

The 17,975 ms at class 75 is **faster than any single forced candidate reached
at any frame of that leg**: the best frame-forced arm was 18,825 ms, and this
is 850 ms better again, because the preference is overriding at more than one
commit and removing it fixes all of them at once.

It also nearly closes the gap between the two classes. The slow class has been
the whole of the exit-speed regression and the whole of the selection failure;
at 17,975 against 17,725 it is no longer the outlier.

## Acceptance

Both floors pass on this build with `bot_moveStatePrefer=0`, unpinned:

- **36-case floor: 36 of 36 pass**, zero deaths, movement owner only,
  zero parity mismatches out of zero comparisons, 5,490 refusals.
- **Both seeded named regressions pass**: `red_ret_L1_stand` and
  `blue_out_L1_stand`, zero deaths, movement owner only.

## Why this was missable before

[The Opus handoff](../../archive/movement-2026-10-05/OPUS_HANDOFF.md) records that blanket preference removal
"has already been screened without establishing a better replacement". No
dated record in `records/` carries that screening's numbers, and it predates
`--corner-class`, which landed on
[2026-09-20](2026-09-20-pinned-start-level.md). An unpinned arm samples start
classes it does not control, and this effect is **-3,150 ms at one class and
+150 at another**, so an unpinned screen's verdict would be decided by which
classes its worlds happened to draw. That is a sufficient explanation for a
missed effect of this size; it is not proof of what the earlier screen did.

## What this does not establish

- **Two start classes of four.** Classes 100 and 125 are unmeasured, and the
  declared-weight comparison that
  [the pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md) uses
  cannot be computed from two cells. The weighted effect is **unknown**, not
  assumed positive, and class 150 already regresses.
- One case, one map, one build, three worlds per arm, one distinct completion
  per arm. Route times are quantised and these are not counterbalanced pairs.
- **Search cost is unmeasured here.** Removing a preference can change how
  many candidates are judged; that was not recorded in these arms.
- This is a policy arm: it changes every decision in the trip, not one frame.
  It does not isolate which commits recover the 3,150 ms, and the frame
  evidence only accounts for part of it.
- **Nothing is installed.** `bot_moveStatePrefer=1` remains the research
  configuration. Runtime cost, parity qualification and the 15-second target
  are untouched, and parity still makes zero comparisons.

## What to do next

1. The same two arms at classes 100 and 125, then the declared-weight mean
   over all four. That is the comparison that decides whether this is a
   configuration proposal at all, and it can be collected the same way.
2. Search cost inside the completed trip, pooled, for both arms.
3. If it survives both, fresh counterbalanced pairs on the full case rather
   than pinned cells, then the regression floors again.

Note that this does not retire the selection work. Class-75 commit 3 has no
state candidates at all and still misorders its three candidates, so removing
the preference cannot be the whole story, and
[the horizon a bounded rank would need](2026-09-20-select-override-exit-speed-500.md)
is priced independently of it.

## Reproduce

One world of one arm, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 48600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=0 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```

The floors, unpinned, from the same build. The retained outputs are
`floor-prefer-off/` and `seeded-prefer-off/`; move or rename them first,
because the runner writes to those fixed paths and the bench refuses an
output directory that already holds a campaign:

```powershell
python .codex-research/state-prefer-classes-20260920-a/floors.py
```
