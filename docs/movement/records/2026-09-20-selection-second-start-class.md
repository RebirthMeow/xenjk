# At the fast start class the ranking already picks the best, three frames of three

Recorded 2026-09-20. **No native source change, no rebuild** - every world is
the frozen diagnostic build `.codex-research/select-override-build-20260920-a`
and every treatment is a cvar. Protocol declared before the forced arms were
collected, driver, analyser and evidence in
`.codex-research/select-override-class150-20260920-a/`; the summary quoted
here is `summary-arms-1.json`.

[The frame survey](2026-09-20-selection-across-frames.md) measured four frames
at start class 75 and found the ranking taking the worst or near-worst
candidate at all four. This is the other half of that question: a **second
start class**, on a different trajectory. The answer reverses the finding.

Start class 150 pinned, `bot_moveExitSpeed=500` in every arm, three
repetitions per arm, arms alternated. **30 cells of 30, none incomplete, 71
native worlds**, one distinct completion per arm, zero deaths, movement owner
throughout, exactly one forced commit inside each measured trip. The control
is **17,575 ms**, both probe worlds and all three collection worlds.

Three commits of this leg admit few enough candidates to force **every one**,
so each span below is the frame's full range, not a sample.

| Commit | Arm | Candidate | `kind` | `progress` | `score` | Completion | Against control |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |
| 4 | d4c | 11 | straight | 408.6 | **453.9** | **17,575 ms** | 0 - the ranking's own |
| 4 | d4b | 9 | straight | 402.1 | 442.5 | 18,300 ms | +725 ms |
| 4 | d4a | 7 | straight | 400.2 | 437.6 | 18,575 ms | +1,000 ms |
| 5 | d5b | 15 | **state** | 383.2 | **499.5** | **17,575 ms** | 0 - the ranking's own |
| 5 | d5c | 9 | straight | 383.4 | 498.6 | 18,500 ms | +925 ms |
| 5 | d5d | 11 | straight | 383.7 | 496.1 | 20,050 ms | +2,475 ms |
| 5 | d5a | 7 | straight | 381.6 | 496.0 | 20,150 ms | +2,575 ms |
| 10 | d10b | 9 | straight | 412.4 | **246.3** | **17,575 ms** | 0 - the ranking's own |
| 10 | d10a | 7 | straight | 412.4 | 244.4 | 18,650 ms | +1,075 ms |

**At all three frames the ranking already commits the fastest admitted
candidate, and it is the score maximum every time.** The leverage is real -
the frames span 1,000, 2,575 and 1,075 ms - and the current rule captures all
of it. Every other arm is slower than the control; nothing here is left on the
table.

Commit 5 is worth reading closely: the scores 499.5, 498.6, 496.1, 496.0 order
the completions 17,575, 18,500, 20,050, 20,150 **exactly**, including the
2,475 ms cliff between the second and third. Commit 10 separates two
candidates carrying **identical `progress`, 412.4**, by score alone, 246.3
against 244.4, and the higher one is 1,075 ms faster.

## The contrast with the slow class, and what distinguishes them

Same build, same policy, same case, same map; only the start class differs.

| | Class 75 | Class 150 |
| --- | --- | --- |
| Control completion | 21,125 ms | 17,575 ms |
| Frames measured | 4 | 3, all exhaustive |
| Ranking's own choice | worst or near-worst at 4 of 4 | **fastest at 3 of 3** |
| Executed candidate is the score maximum | no, at 3 of 3 checked | **yes, at 3 of 3** |
| Best against control | -1,525 to -2,300 ms | **0 at every frame** |

The `kind` field separates the two classes mechanically. At class 75, the
candidate that executes at commits 4 and 5 is a **planned state candidate that
scores below an admitted straight candidate**, and the straight one is faster:

| Class 75 | Executed | Score | Completion | Best straight alternative | Score | Completion |
| ---: | --- | ---: | ---: | --- | ---: | ---: |
| commit 4 | state, cand 14 | 434.2 | 21,125 ms | straight, cand 10 | **448.8** | **19,400 ms** |
| commit 5 | state, cand 15 | 499.8 | 21,125 ms | straight, cand 11 | **508.9** | 19,975 ms |

At class 150 commit 5 a state candidate also executes - and there it **is**
the score maximum and the fastest of the four. So the preference for a planned
candidate is not harmful in itself; what is measured here is that at class 75
it **overrides a higher-scoring straight candidate and loses**, 1,725 ms at
commit 4 and 1,150 ms at commit 5 against that alternative, while at class 150
it never overrides anything.

At class-75 commit 3 there are no state candidates at all
(`state_candidates=0`), the score maximum executed, and it was still the worst
of the three measured. So the slow class has **two** distinguishable failures,
not one: a preference that overrides a better-scoring candidate at commits 4
and 5, and a score that misorders candidates at commit 3.

## What this changes

A bounded continuation rank would have **nothing to win** at class 150 on
these frames and could only lose there, since the current rule is already
choosing the fastest admitted candidate every time. Any such rank has to be
shown not to regress a class where the incumbent is already optimal - which
the class-75 evidence alone could not have said.

It also names a cheaper thing to test first, because `bot_moveStatePrefer` is
an existing cvar: whether the preference is what costs the slow class. Blanket
removal has been screened before without establishing a better configuration,
but not against this mechanism and not stratified by start class.

## What this does not establish

- Two start classes, one case, one map, one build, three worlds per arm. Two
  classes are not the four the confirmation uses, and classes 100 and 125 are
  unmeasured.
- Three frames of this leg, chosen because their admitted sets are small
  enough to force completely. Frames with larger admitted sets at this class
  are unmeasured, and a small admitted set may itself be easier to rank.
- That the executed candidate is the score maximum at these three frames does
  not mean `StatePreferred` never overrides at this class; it means it did not
  override to a worse candidate at the frames measured.
- The class-75 comparison against "the best straight alternative" uses 3 of 8
  and 4 of 4 admitted candidates at commits 4 and 5, so commit 4's figure is a
  lower bound.
- **The override is a diagnostic, not a policy**, and nothing is installed.
- Parity still makes zero comparisons in these worlds.

## Reproduce

Analysis only, from retained worlds; writes a fresh output and refuses to
overwrite:

```powershell
python .codex-research/select-override-class150-20260920-a/analyse-arms.py --out <new unique output.json>
```

One world of one arm, from the workspace root, on an unused port - here
commit 5, candidate 9:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 46600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_moveSelectNth=5 --set bot_moveSelectCand=9 `
  --corner-class 150 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```
