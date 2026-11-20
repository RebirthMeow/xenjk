# One commit is worth 1,900 to 5,300 ms, and its right answer differs by class

Recorded 2026-09-20. **No native source change and no rebuild** - every world
is the frozen diagnostic build `.codex-research/select-override-build-20260920-a`,
and `bot_moveStatePrefer`, `bot_moveSelectNth` and `bot_moveSelectCand` are
existing cvars. Protocol declared before collection, driver, analyser and
evidence in `.codex-research/prefer-ablation-20260920-a/`; the summary quoted
here is `summary-ablation-1.json`.

[The localisation](2026-09-20-preference-localisation.md) found where the
preference acts: at start classes 75, 100 and 125 the preference-on and
preference-off trips are identical for the first commit of the timed trip and
first differ at the **second**, about 2,950 ms in, on graph edge 36420. It
could not say whether the class deltas are that firing or the two to four
later ones. This prices it, from both sides, one frame at a time.

## The four arms

Three classes, four arms each, three repetitions an arm, arms alternated,
**36 cells of 36, none incomplete, 93 native worlds**, one distinct completion
per cell, zero deaths, movement owner only. **All six controls reproduce the
preference collections to the millisecond**, so this collection and those
agree before anything is forced. In all 36 accepted worlds the override forced
exactly the declared plan at the declared frame and nowhere else.

| Start class | Preference on | Preference off | On, that commit forced to the judge's straight winner | Off, that commit forced to the state plan |
| ---: | ---: | ---: | ---: | ---: |
| 75 | 21,125 | **17,975** | 19,225 | 19,400 |
| 100 | 19,375 | 21,150 | 19,225 | **18,100** |
| 125 | **15,925** | 18,950 | 21,225 | 17,125 |

Root speed at that commit is 99.1 units/sec at class 75 and 250.0 / 250.1 at
classes 100 and 125, identical in both arms of each class.

## What the two sides agree on, and where they contradict

Read the two forced arms as the same question asked twice: **is the state plan
the right commit at that frame?**

- **Class 75: no, from both sides.** Holding the straight winner under the
  preference saves **1,900 ms** (21,125 to 19,225); forcing the state plan
  into the preference-off trip costs **1,425 ms** (17,975 to 19,400).
- **Class 125: yes, from both sides.** Holding the straight winner under the
  preference costs **5,300 ms** (15,925 to 21,225) - more than the whole
  3,025 ms the preference is worth at that class; forcing the state plan into
  the preference-off trip saves **1,825 ms** (18,950 to 17,125).
- **Class 100: the two sides contradict each other.** Holding the straight
  winner under the preference is 150 ms *faster* (19,375 to 19,225), so the
  firing is not what earns the class its +1,775 ms; but forcing the state plan
  into the preference-off trip saves **3,050 ms** (21,150 to 18,100), the
  fastest class-100 trip in this grid, faster than either pure policy.

So the frame carries a large share of each class delta, and **the spans are
not additive and not policy-independent**: the same committed plan is worth
-150 ms under one policy and +3,050 under the other at one class. That is the
frame evidence's own lesson again - each forced commit changes everything
after it - now with the preference as the policy rather than the exit weight.

## What it does to the conditional rule

Two clean labels and one contradiction:

| Class | Root speed at the decision | Is the state plan right there? |
| ---: | ---: | --- |
| 75 | 99.1 | no, both sides |
| 100 | 250.0 | contradictory |
| 125 | 250.1 | yes, both sides |

The root-speed reading survives on the two clean labels - the preference is
wrong where the bot arrives below the ground walk speed and right where it
arrives at it - and class 100 neither confirms nor refutes it, because the
frame's value there depends on the rest of the policy rather than on the
frame.

**What a rule that only declined the preference at that class-75 decision
could buy is small.** If it reproduced the forced arm exactly and changed
nothing else, the four-class table would read 17,575 / 15,925 / 19,375 /
19,225, which is **17,271.8 ms** on the declared weights against the
preference's own **17,394.4** - a **122.6 ms** gain, or 475 ms equally
weighted.

**Best of what was tested is not a ceiling, and its size is mostly the
weighting.** Picking the best of the four policies measured here at each
class - 17,575, 15,925, 18,100 and 17,975 - gives **17,054.0 ms**, which is
340.3 ms better than the preference alone on the declared weights and
**1,106.25 ms** better equally weighted (17,393.75 against 18,500). Those are
the same combination priced two ways and they differ by more than a factor of
three, so that number is mostly the borrowed start distribution. It is also
only the best of the policies that were run: the other admitted candidates at
this commit, the later firings, other decisions and their interactions are
untested, and nothing here bounds what they are worth.

**And the rule would not be a no-op elsewhere.** Inside the timed trips the
only firing below 250 units/sec is this class-75 commit, but the retained
worlds also carry firings at 62.5, 107.3, 107.4 and 211.2 units/sec **before
the timed start**, at classes 75, 100 and 150, and class 150's own first
in-trip firing is at 242.1. Any threshold rule would change those too, and
their effect is unmeasured here.

## What this does not establish

- Three start classes of one case, one map, one build, three worlds an arm,
  one commit per class. Class 150 is not in this grid at all.
- It prices one frame under two fixed policies. It does not measure a
  conditional rule, its cost, or its behaviour at a class it was not fitted
  to, and a rule that changes that decision changes every decision after it -
  the forced arms are what one such change did, not what a rule would do.
- The class-100 contradiction is a property of this frame under these two
  policies; nothing here says which side a rule should believe.
- The weighting is the confirmation's borrowed cohort frequency, the weakest
  link here exactly as it is there.
- **Nothing is installed.** `bot_moveStatePrefer=1` remains the research
  configuration, the override defaults off, and parity still makes zero
  comparisons.

## Reproduce

Analysis only, from the retained worlds; writes a fresh output and refuses to
overwrite:

```powershell
python .codex-research/prefer-ablation-20260920-a/analyse.py --out <new unique output.json>
```

One world of one arm, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 54600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_moveSelectNth=2 --set bot_moveSelectCand=19 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```
