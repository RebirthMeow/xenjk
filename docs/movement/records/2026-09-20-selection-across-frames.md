# Every frame tested leaves seconds, and the score does not order the trips

Recorded 2026-09-20. **No native source change, no rebuild** - every world is
the frozen diagnostic build `.codex-research/select-override-build-20260920-a`
and every treatment is a cvar. Protocol declared before collection, driver,
analyser and evidence in `.codex-research/select-override-frames-20260920-a/`;
the summary quoted here is `summary-frames-1.json`.

[The exit-speed-500 repeat](2026-09-20-select-override-exit-speed-500.md)
measured one frame. This asks the same question at three other sequence
commits of the same leg, so a scoring rule is not chosen from one example.
Start class 75 pinned, `bot_moveExitSpeed=500` in every arm, three
repetitions per arm, arms alternated. **33 cells of 33, none incomplete, 94
native worlds**, one distinct completion per arm, zero deaths, movement owner
throughout, exactly one forced commit inside each measured trip.

## Three more frames

The control commits at relative 2,975 (commit 2), 4,775 (commit 3, already
measured), 6,525 (commit 4) and 8,025 (commit 5). Commit 5 admits exactly
four candidates, so **all four are forced and its span is the frame's, not a
sample**; commits 2 and 4 admit 12 and 8, of which three each were forced
across the progress range, so their spans are **lower bounds**.

| Commit | Arm | Candidate | `progress` | `score` | Completion | Against control |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| - | control | the ranking's own | - | - | 21,125 ms | - |
| 2 | c2lo | 15 | 417.8 | 570.3 | **19,025 ms** | **-2,100 ms** |
| 2 | c2hi | 3 | 781.9 | 969.9 | 20,275 ms | -850 ms |
| 2 | c2mid | 1 | 750.1 | 929.0 | 21,300 ms | +175 ms |
| 4 | c4hi | 10 | 521.1 | 448.8 | **19,400 ms** | **-1,725 ms** |
| 4 | c4lo | 7 | 415.5 | 243.2 | 20,400 ms | -725 ms |
| 4 | c4mid | 14 | 507.1 | 434.2 | 21,125 ms | 0 |
| 5 | c5a | 7 | 357.8 | 488.6 | **19,600 ms** | **-1,525 ms** |
| 5 | c5d | 11 | 365.9 | 508.9 | 19,975 ms | -1,150 ms |
| 5 | c5b | 9 | 357.8 | 490.6 | 20,200 ms | -925 ms |
| 5 | c5c | 15 | 362.8 | 499.8 | 21,125 ms | 0 |

`c4mid` and `c5c` reproduce the control to the millisecond because they **are**
the ranking's own choices at those commits: the control's own commits carry
`progress=507.1 score=434.2` at commit 4 and `progress=362.8 score=499.8` at
commit 5. That is the fourth and fifth independent demonstration that the
override is inert when it names the candidate the ranking already takes.

**Nothing about the measured frame was special.** Every frame tested carries
between 1,525 and 2,300 ms, and at every one of them the ranking's own choice
is the worst or equal-worst of the candidates measured:

| Commit | Admitted | Forced | Span measured | Ranking's own rank | Best against control |
| ---: | ---: | ---: | ---: | --- | ---: |
| 2 | 12 | 3 | >= 2,275 ms | worse than 2 of 3 | -2,100 ms |
| 3 | 12 | 3 | >= 2,300 ms | worst of 3 | -2,300 ms |
| 4 | 8 | 3 | >= 1,725 ms | worst of 3 | -1,725 ms |
| 5 | 4 | **4, all** | **1,525 ms** | **worst of 4** | -1,525 ms |

Commit 5 is the load-bearing row: its admitted set is small enough to measure
exhaustively, and the executed choice is the slowest of the four.

**These spans are not additive.** A forced commit changes every decision after
it, so each arm is its own trip and each span is what that one frame is worth
while the ranking plays everything else. Nothing here says the leg contains
1,525 + 1,725 + 2,275 + 2,300 ms of recoverable time.

## The score does not order the trips, and it is not what executes

At commit 5 the whole admitted set is known, so the two can be checked
exactly. Ordered by `score`:

| `score` | Arm | Completion |
| ---: | --- | ---: |
| 488.6 | c5a | **19,600 ms** - the best trip has the **lowest** score |
| 490.6 | c5b | 20,200 ms |
| 499.8 | c5c | 21,125 ms - **what executed** |
| 508.9 | c5d | 19,975 ms - the highest score |

Score is not merely a weak predictor here; over these four it is mildly
**anti**-correlated with the outcome. Neither is progress: the best trip has
the **lowest** admitted progress at commit 2 (417.8 of 417.8 to 781.9) and the
**highest** at commit 4 (521.1), and at commit 5 `c5a` and `c5b` carry
**identical progress, 357.8, identical carried speed, 0.0, and complete
600 ms apart**. They are the same point in the two coordinates the score is
built from. Their margins do differ, by 1.2 in 304 - and that difference
orders them the wrong way, `c5b` higher and 600 ms slower.

And **the score maximum is not what executed** at any of the three frames. At
commit 5 the executed candidate scores 499.8 while `c5d` scores 508.9; at
commit 4 it scores 434.2 while `c4hi` scores 448.8; at commit 2 it scores
948.8 while `c2hi` scores 969.9. This is the final decision the handoff warns
about - `StatePreferred` can replace the winner - and it is confirmed here
rather than assumed. Whatever selects the executed candidate, it is not
"highest score", and the highest score would not have been better at commit 5
either.

## What this means for a scoring rule

The hypothesis that a better *weighting* of the existing terms would fix this
does not survive commit 5. The four candidates' scores span 20.3 points in
490, their progress spans 8.1 units in 358, two of them are identical on
progress, and their completions span 1,525 ms. There is no weighting of
progress and carried speed that orders these four correctly, because two of
them are the same point in those coordinates.

That leaves the continuation itself, and
[its horizon is already priced](2026-09-20-select-override-exit-speed-500.md):
on the frame measured there, a ranking of realized continuations is stable
only from about 7,000 ms out, and at 5,000 ms the true winner ranks last.

## What this does not establish

- **One start class**, one case, one map, one build, three worlds per arm.
  Other start classes are unmeasured and are the remaining half of this
  question.
- Commits 2 and 4 sample 3 of 12 and 3 of 8 admitted candidates, so their
  spans are lower bounds and their "worst of 3" is worst of the sample. Only
  commit 5 is exhaustive.
- The anti-correlation is four points at one frame. It refutes "a better
  weighting of these terms suffices" at that frame; it is not a measured
  negative correlation over a population of frames.
- **The override is a diagnostic, not a policy.** Nothing chooses these
  candidates at run time, and nothing is installed.
- Parity still makes zero comparisons in these worlds.

## Reproduce

Analysis only, from retained worlds; writes a fresh output and refuses to
overwrite:

```powershell
python .codex-research/select-override-frames-20260920-a/analyse.py --out <new unique output.json>
```

One world of one arm, from the workspace root, on an unused port - here
commit 5, candidate 7:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 42600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_moveSelectNth=5 --set bot_moveSelectCand=7 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```
