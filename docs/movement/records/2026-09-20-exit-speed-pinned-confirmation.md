# The pinned confirmation: 500 holds, the slow start still regresses

Recorded 2026-09-20. **No native source change, no rebuild** - `bot_moveExitSpeed`
is a cvar and every world is the frozen reference build
`.codex-research/successor-aim-build-20260920-a`. One Python tool change, below.
Evidence, protocol, driver and analyser in
`.codex-research/exit-speed-pin-confirm-20260920-a/`; the summary this record
quotes is `summary-confirm-1.json`.

This is the confirmation the [review corrections](2026-09-20-opus-review-fixes.md)
asked for: fresh repeated matched 500-versus-2000 runs at predefined pinned
starts, including the slow start where 500 regresses, with weights and counts
declared before collection.

## Result

Four declared start strata, six matched pairs each, plus a three-pair anchor.
**Every cell produced one distinct completion time over all six worlds.**

| Start class | 500 | 2000 | Pairs | Delta |
| ---: | ---: | ---: | :---: | ---: |
| 150 | **17,575** | 19,050 | 6 | **-1,475** |
| 125 | **15,925** | 16,450 | 5 complete of 6 | **-525** |
| 100 | 19,375 | 19,375 | 6 | 0 |
| 75 (slow) | 21,125 | **20,100** | 6 | **+1,025** |
| anchor, level 3350 | **17,575** | 19,050 | 3 | **-1,475** |

Weighted by the earlier cohort's start frequencies, declared before collection:
**17,394.4 ms at 500 against 18,202.2 ms at 2000, a 807.8 ms improvement.**
Every one of the eight class/arm cells reproduces the retained cohort's value
for that cell to the millisecond, so this number equals its own prior exactly;
that agreement is the confirmation, not a second independent estimate of a
noisy quantity.

**The regression at the slow start is confirmed and it is not rare in kind.**
500 loses 1,025 ms there, six pairs out of six, and 500's worst cell is
21,125 ms against the incumbent's worst of 20,100 ms. The gain is bought at
common starts and paid back at an uncommon one.

Acceptance floors at 500, unpinned: **36/36 pass, zero deaths, movement owner
throughout, zero parity mismatches**, plus both seeded named regressions
(`blue_out_L1_stand` 15,875 ms, `red_ret_L1_stand` 19,625 ms). Parity still
makes **zero comparisons**, so nothing here qualifies forecast/execution
agreement. 15 seconds is not attained: the fastest cell anywhere is 15,925 ms.
Nothing is installed.

## The pin had to change first, and why

The declared strata are start-level **classes**, `corner_level mod 200`, not
the single levels 3275/3300/3325/3350 the earlier records name. That is not a
convenience. The pre-start play log samples every **250 ms** of level time,
not every frame, so a world can only be started on its own ladder `L0 + 250k`,
and `L0` varies world to world - 2275, 2300, 2325, 2350 and 2400 were all
observed today. A pin at one exact level is simply not on offer in a world
whose ladder misses it.

Measured before the grid, on the reference configuration: level 3350 was hit
1/1, while **3325, 3300 and 3275 each missed 4 worlds out of 4**, every miss
overshooting by exactly one ladder row. A first attempt at the grid with exact
pins was abandoned after two cells missed 12 attempts running; its 26 worlds
are retained as `aborted-fixed-level-*` and none of them is a timed result.

Re-reading the retained 100-world sweep by `corner_level mod 200` explains the
earlier "13 distinct start levels": they are **7 classes**, and inside a class
every arm has exactly one completion time - including the three classes that
pool different absolute levels (75 pools 2875/3275/4475, 100 pools
3300/3700/3900, 175 pools 3375/3575/3775). 200 ms is `lcm(25 ms frame,
pmove_msec 8)`.

`tools/movement_bench.py` and `tools/movement_bench_lib.py` therefore gained
`--corner-class MS`, which waits for any visible level of the declared class
inside `--corner-class-min/-max` and verifies the landed level's class exactly
as the exact pin verifies its level. Seven new tests; the movement Python
suite is 134 tests and passes. `--corner-level` is unchanged and still
available; acceptance floors stay unpinned.

**The class claim was then tested prospectively, and held.** The 53 timed
worlds started on 14 distinct absolute levels spread over two seconds of level
time, and every world of a class landed on its class's time:

| Class, arm | Absolute start levels drawn | Completion |
| --- | --- | ---: |
| 125, 500 | 3325 x3, 3925, 4325, 5325 | 15,925 every time |
| 75, 2000 | 3075, 3275, 3875, 4075, 5075 x2 | 20,100 every time |
| 150, 2000 | 3350 x4, 3550, 4350 | 19,050 every time |
| 100, 2000 | 3100 x6 | 19,375 every time |

The anchor cell, pinned to the exact level 3350 rather than to its class,
agrees with the class-150 cell on both arms and with the retained pinned-start
record's 19,050 ms. The exact pin and the class pin measure the same thing.

## Design, as declared

`protocol.md` was written before the grid and amended, before collection, to
record the pin change. Arms alternate inside a pair, the leading arm alternates
by pair parity, classes are visited round-robin, one native world at a time at
`--parallel 1` with a unique port and output path. Twelve attempts per cell;
every attempt world is retained, hit or miss; a cell that exhausts its attempts
is **incomplete** and no easier start is substituted.

141 native worlds were spent for 53 timed results. **One cell is incomplete**:
class 125 at 2000, pair 2, drew twelve consecutive worlds whose ladder never
met the class. That class therefore reports five complete pairs of six; the
missing world is a pin-availability failure, and it is reported rather than
replaced.

Bindings are single-valued across all 53 worlds: one build, one build manifest
(`a26e5663...`), one runner, one graph seed, one case file. The analyser
re-checks each world's arm setting, its landed class, and that the campaign
time equals the world's own `evaluation.time_ms`; no world has a binding error.

## Weighting, and how much it carries

The weights are the retained cohort's class frequencies - 43, 34, 10 and 6 for
classes 150, 125, 100 and 75, covering 93 of its 100 worlds - declared in the
protocol before collection. A pinned grid cannot estimate its own start
distribution, so this borrowed weighting is the weakest link in the headline
number and it should be read as such.

- Weighted by those frequencies: **-807.8 ms**.
- Weighted equally over the four classes: **-243.75 ms**.
- The slow class would have to be about **48 % of starts** to erase the gain
  entirely; it is 6.5 % of the retained cohort.

So the direction survives a wide range of weightings, and the size does not.

## CPU

Logged in-search `rollout_ms` **inside the completed trip**, pooled over the
four grid classes: median **36.85 ms at 500 against 38.70 ms at 2000**, mean
37.39 against 37.69, maximum 101.4 against 89.5, over 570 and 536 samples and
about 23.8 and 23.3 logged searches per world. The setting does not buy its
time with search work. These are fixture searches at timescale 5, not a
production CPU budget.

> **Corrected 2026-09-20.** The first summary of this grid quoted 35.5 against
> 36.8 ms over 695 and 652 samples. That window was bounded below by the
> fixture start and not above, so it included 125 and 116 searches from after
> the trip had completed, which are cheaper (median 30.9 and 31.9 ms) and
> pulled both medians down. The analyser now bounds the window by
> `evaluation.time_ms` and reports the later samples separately;
> `summary-confirm-2.json` is the corrected output. The comparison between the
> arms is unchanged in direction and still small.

## What this does not establish

- **Not 15 seconds.** The best class is 15,925 ms and the weighted mean is
  above 17 seconds.
- **Not production qualification.** Parity makes zero comparisons here, as
  everywhere else in this phase.
- **Not a mechanism.** `lcm(25, 8) = 200` is a plausible reason for the class
  period and it is untested; nothing here looks at why a class maps to a
  trajectory. The class equality is an observation on one build, one map and
  one case.
- **Not a variance estimate.** Within-cell spread is zero, so six repetitions
  test that a deterministic outcome reproduces across fresh worlds and fresh
  absolute start levels. They are not six independent draws of a noisy time.
- **Not a decision about the tail.** The slow-class regression is confirmed,
  priced at 1,025 ms, and still unexplained beyond
  [the refusal at an expensive landing](2026-09-20-exit-speed-finer-sweep.md)
  already on record.

## Status

`bot_moveExitSpeed=500` is confirmed as a **research configuration value**
against 2000 at matched verified starts: faster at two of four declared
strata, tied at one, slower at the slow one, 807.8 ms better under the declared
weighting and 243.75 ms better under equal weighting, at no CPU cost, with
floors and seeded regressions green. It is **not installed** and production
enablement remains separate open work.

## Reproduce

Analysis only, from the retained worlds; writes a fresh output path and
refuses to overwrite:

```powershell
python .codex-research/exit-speed-pin-confirm-20260920-a/analyse.py `
  --out <new unique output.json>
```

One world of the confirmation grid, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/successor-aim-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 35800 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500
```

`--corner-class 75` is the slow stratum; 150, 125 and 100 are the others.
Drop the class arguments for an acceptance floor, which samples starts.
