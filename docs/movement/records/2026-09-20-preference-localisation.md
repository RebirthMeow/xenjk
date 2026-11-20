# The preference is decided at one commit per leg, and the classes differ there by root speed

Recorded 2026-09-20. **No native run, no source change, no rebuild** - this
reads the worlds already retained by the two preference collections,
`.codex-research/state-prefer-weighted-20260920-a` (classes 100 and 125) and
`.codex-research/state-prefer-classes-20260920-a` (classes 75 and 150), both
on the frozen diagnostic build with the forced-candidate override off.
Analyser and evidence in `.codex-research/prefer-localisation-20260920-a/`;
the summary quoted here is `summary-localisation-2.json`.

This is the localization the handoff asked for before a conditional rule:
**where does `bot_moveStatePrefer` actually change a decision, in the classes
it helps as well as in the one it hurts.**
[Removal is refuted](2026-09-20-preference-removal-refuted.md) - the
preference is right at three classes of four - so the question is what tells
the fourth apart at the decision itself.

## Reading the judge out of the retained logs

The preference replaces the judge's winner when that winner is a straight
candidate and some state candidate was admitted, and neither the replacement
nor the judge's own winner is logged. Both are recoverable: every candidate's
rollout is in the retained `[SPIKE]` lines, and the judge's rule is

```text
score = margin + ExitCredit,  margin = progress - walk(done - 1),
ExitCredit = last landing speed * bot_moveExitSpeed / 1000
```

Only the committed candidate's `walk` is logged, and `walk` is read at each
candidate's own finishing frame. Where the walk reference has stopped
(`walk_stopped=1`), or where every admitted candidate finished on the same
frame, that one value is every candidate's and the reconstruction is **exact**.
Elsewhere it is an approximation, reported as such and never quoted as a
finding.

Over 24 worlds, three per arm, eight arms, **378 commits**:

| | Commits | Reconstruction agrees with the log | Explained as the preference | Unexplained |
| --- | ---: | ---: | ---: | ---: |
| Exact | 141 | 120 | **21** | **0** |
| Approximate | 237 | 175 | 31 candidate firings | 31 |

The candidate set the judge ranked matches the set the commit line reports at
**378 of 378** commits. Every one of the 21 exact firings is in a
`bot_moveStatePrefer=1` arm; the preference-off arms produce **none**, which
is what they must produce. The approximate branch reports 3 firings in those
same off arms, so roughly a tenth of its 31 are its own error - it
localizes, it does not establish.

Two further checks fall out. At the decisive commit below, the
preference-**off** arm commits exactly the candidate this reconstruction
names as the judge's winner, in all nine of those worlds. And at classes 75,
100 and 125 the two arms are identical for the first commit of the timed trip
and first differ at the second, over every world of both arms.

## The decisive commit is the same site in three classes

Of the 21 exact firings, the earliest inside the timed trip is also the
largest at each of the three classes that have one. At classes 75, 100 and
125 it is the **same decision**: commit 2, about 2,950 ms into the leg, on
graph edge 36420 into 38065, with a chord of 296 to 307 units.

| Start class | Preference on, against off | Root speed | Committed state plan | Judge's straight winner | Score gap |
| ---: | ---: | ---: | --- | --- | ---: |
| 75 | **-3,150 ms** | **99.1** | progress 764.8, exit 443.0 | progress 781.2, exit 454.3 | 22.1 |
| 100 | +1,775 ms | 250.0 | progress 748.7, exit 438.6 | progress 765.5, exit 466.3 | 30.8 |
| 125 | +3,025 ms | 250.1 | progress 739.2, exit 442.8 | progress 755.7, exit 439.6 | 14.9 |
| 150 | +150 ms | 242.1 | progress 839.8, exit 521.7 | progress 825.3, exit 585.1 | 17.2 |

Class 150 is a different site - its first firing is commit 1 at relative 450
on edge 50732, and that firing is **approximate**, not exact. The other three
classes commit a state plan on edge 50732 at that same first commit in both
arms: there the judge chooses it unaided and the preference has nothing to do.

Per class, inside the timed trip, the exact firings are: class 75 one (commit
2), class 100 two (commits 2 and 11), class 125 two (commits 2 and 11), class
150 none - its three in-trip firings are all approximate. The later exact
firings are small: gaps of 0.2 and 2.3 against 14.9 to 30.8 at commit 2.

## What separates the classes there, and what does not

The one feature that orders these four cases is the **speed the bot arrives at
the decision with**. Where the preference helps, the root is at or near the
250 units/sec ground walk speed - 250.0, 250.1, 242.1. Where it loses 3,150 ms,
the root is at **99.1**, well below it, and that is the only class of the four
where it does.

These do **not** separate them:

- **The score gap.** 22.1 at the losing class sits between 14.9 and 30.8 at
  two winning ones.
- **Domination.** At class 75 the committed state plan is worse than the
  judge's winner on both terms the score is made of, progress and carried exit
  speed. At class 100, where the preference wins 1,775 ms, it is worse on both
  terms as well. Only at class 125 does it carry more speed than the winner.
- **The heading offset.** The state plan is aimed 3.3 to 3.5 degrees off the
  reference at all three classes.
- **How much state there is.** 3 admitted state candidates of 12 at class 75,
  6 of 24 at 100, 6 of 15 at 125.

So a conditional rule cannot be read off the candidate pair alone, and the
root state is where to look first. One sample per class is a hypothesis, not a
rule.

## What this does not establish

- **No attribution.** The preference fires two to four times per trip, and
  this locates the firings without pricing any of them. It does not show that
  the class-75 loss is the commit-2 firing, or that the class-100 and 125
  gains are theirs. That needs the forced-candidate override, which can hold
  one decision while leaving the rest to the ranking.
- One map, one case, one build, four classes of one decisive commit each,
  three worlds an arm. Classes 100 and 125 agree with each other; they are not
  independent evidence of a rule.
- The class-150 firing and every firing after commit 2 at classes 75 and 125
  rest on the approximate branch, which is wrong about a tenth of the time.
- Root speed at the decision is a **correlate over four cases**. Nothing here
  measures a rule that uses it, its cost, or its effect on the floors.
- Nothing is installed and nothing changed. `bot_moveStatePrefer=1` remains
  the research configuration; parity still makes zero comparisons.

## Next

The ablation the attribution needs, and which also supplies the positive and
negative pairs a conditional rule would be fitted to, uses only existing
cvars: at each class, hold commit 2 with `bot_moveSelectNth=2` and
`bot_moveSelectCand` set to the other side of that one decision - the judge's
straight winner under `bot_moveStatePrefer=1` (candidate 19 at class 75, 16
at class 100, 3 at class 125), and the state plan under
`bot_moveStatePrefer=0` (candidate 13, 13 and 22) - against both unforced
arms. The override runs after the preference and can only select a candidate
the judge already admitted, so each arm differs from its control at one
frame.

## Reproduce

Analysis only, from the retained worlds of both collections; writes a fresh
output and refuses to overwrite:

```powershell
python .codex-research/prefer-localisation-20260920-a/analyse.py --out <new unique output.json>
```
