# Removing the preference loses 1,162.9 ms weighted: it is right at the three common classes

Recorded 2026-09-20. **No native source change, no rebuild** - every world is
the frozen diagnostic build `.codex-research/select-override-build-20260920-a`
with the forced-candidate override off, verified from the logs, and
`bot_moveStatePrefer` is an existing cvar. Protocol declared before
collection, driver, analyser and evidence in
`.codex-research/state-prefer-weighted-20260920-a/`; the summary quoted here
is `summary-weighted-2.json`.

[The two-class preference arms](2026-09-20-state-preference-costs-the-slow-class.md)
measured `bot_moveStatePrefer=0` at start classes 75 and 150 only and found
-3,150 ms against +150. This completes the grid at the two classes that
carry most of the declared weight. **The result reverses.**

## The four classes

Classes 100 and 125 collected here, three repetitions per arm, arms
alternated, 16 attempts per cell; **12 cells of 12, none incomplete, 38 native
worlds**, one distinct completion per arm. Classes 75 and 150 are the earlier
collection unchanged.

| Start class | Weight | `StatePrefer=1` | `StatePrefer=0` | Delta | Confirmation's 500 column |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 150 | 43 | **17,575 ms** | 17,725 ms | +150 | 17,575 - agrees |
| 125 | 34 | **15,925 ms** | 18,950 ms | **+3,025** | 15,925 - agrees |
| 100 | 10 | **19,375 ms** | 21,150 ms | **+1,775** | 19,375 - agrees |
| 75 | 6 | 21,125 ms | **17,975 ms** | **-3,150** | 21,125 - agrees |

**All four `StatePrefer=1` cells reproduce
[the pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md)'s 500
column to the millisecond**, so this build and the reference build agree at
every class, and the four-class control is the confirmation's own.

- **Declared weighting (43/34/10/6): 17,394.4 ms on against 18,557.3 ms off,
  a 1,162.9 ms regression.**
- Equal weighting over the four classes: 18,500.0 against 18,950.0,
  a 450.0 ms regression.
- The classes that regress are **93.5 % of starts**. For removal to break even
  they would have to fall to **68.3 %**.

Removing the preference is a clear net loss. It helps only the rarest class.

## What was wrong with the two-class reading

Nothing in the two-class measurement was wrong; the inference from it was.
Classes 75 and 150 are the extremes of the completion range and together carry
**49 of the 93 weighted starts**, but the two classes in between behave like
neither of them. Class 125, the second-heaviest at 34, is where removal costs
the most, 3,025 ms - more than the 3,150 ms it saves at class 75, and at
**5.7 times the weight**.

The earlier record's own caveat named this exactly: "classes 100 and 125 are
unmeasured, and the declared-weight mean is unknown, not assumed positive".
That is now measured, and the answer is negative.

The lesson about unpinned screening still holds, and is now better supported:
this effect ranges from -3,150 to +3,025 ms across four start classes, so an
arm that does not control its class can return almost any verdict. It simply
does not follow that the earlier unpinned screen's negative was wrong - on
this grid, **the earlier screen's negative was right**.

## Search cost

Logged in-search `rollout_ms` inside the completed trip, pooled over the four
classes, the confirmation's method:

| | Median | Mean | Samples |
| --- | ---: | ---: | ---: |
| `StatePrefer=1` | 36.50 ms | 36.50 ms | 285 |
| `StatePrefer=0` | 35.05 ms | 35.97 ms | 300 |

Essentially unchanged, marginally cheaper with the preference off. Cost is not
what decides this; the weighted completion is.

## What this means for the selection work

The preference is **right at three of four classes and wrong at one**, so the
target is a preference that is **conditional**, not one that is removed. That
is the same shape as the original selection finding: at class 75 the executed
planned candidate scores below an admitted straight candidate that is faster,
while at class 150 the executed candidate is the score maximum at every frame
measured ([second start class](2026-09-20-selection-second-start-class.md)).

This **re-elevates bounded continuation ranking**, which can in principle tell
those cases apart at the decision, over any global retuning of the preference.
Its horizon is priced and expensive - stable only from about frame + 7,000 ms,
with the true winner ranking last at + 5,000
([the horizon](2026-09-20-select-override-exit-speed-500.md)) - and that price
now has to be paid against a 1,162.9 ms weighted loss for the cheap
alternative, not against the 3,150 ms gain the two-class reading suggested.

## What this does not establish

- Four start classes of one case, one map, one build, three worlds per arm,
  one distinct completion per cell. Pinned cells, not counterbalanced pairs on
  the unpinned case.
- The weighting is the confirmation's borrowed cohort frequency, not this
  grid's own start distribution. It is the weakest link in the headline
  number, exactly as it is there; the **equal-weight mean regresses too**, so
  the direction does not depend on it.
- It does not identify which commits of the class-125 and class-100 legs the
  preference is winning at. Only class 75 has frame-level evidence.
- Search cost here is logged in-search `rollout_ms`, not a wall-clock runtime
  measurement.
- **Nothing is installed.** `bot_moveStatePrefer=1` is and remains the
  research configuration; this measurement confirms it rather than changing
  it. Parity still makes zero comparisons.

## Reproduce

Analysis only, from retained worlds of both collections; writes a fresh output
and refuses to overwrite:

```powershell
python .codex-research/state-prefer-weighted-20260920-a/analyse.py --out <new unique output.json>
```

One world of one arm, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 52600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=0 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --corner-class 125 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```
