# Decision explorer, class 75: the tape reproduces, and the judge barely ranks

Recorded 2026-09-22. The stage-2 campaign's class-75 result, checked three ways
the [work order](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer)
asks for before a tape can mean anything: a fresh reproduction of the best
tape, row 3 of the [level-1 gate](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21)
measured on a fast trip for the first time, and a read-only look at whether the
judge's ranking predicted any of it.

> **The tape reproduces exactly.** Three fresh class-75 worlds executing
> `c75-best.tape` complete `red_out_L1_stand` in **15,950 ms**, all eleven
> entries hit, no refused and no absent entry, every aim gap 0.0000, and the
> three worlds agree decision for decision - one of them from a release level
> **600 ms** away from the other two. Three tape-off controls alternated with
> them return the campaign's control cell, **21,125 ms**, three times. The
> class-125 cross-check returns 15,925 ms with and without its tape, 13 and 5
> commits, 11 of 11 hit: nothing drifted.
>
> **Row 3 is not met on the fast trips, and the fast trips are a different
> shape.** Waiting is **30.4 percent** of grounded frames on the class-75 tape
> worlds and **37.9 percent** on the class-125 one, against **71.2 percent** on
> the class-75 control, with a threshold of 15. In frames rather than shares:
> the tape trip waits **21** frames where the control waits **141**, and it is
> grounded for 69 frames where the control is grounded for 198. The threshold
> was declared before a fast trip existed and the owner reads it, not this
> record.
>
> **The judge's ranking does not predict the trip.** Over the winning lineage's
> first three decisions, Spearman's rho between the judge's rank and the
> completed trip is **0.18, 0.04 and 0.33** (n = 21, 11, 17). The judge's own
> rank-1 candidate completed in 21,125, 19,575 and 19,575 ms; the branch the
> explorer eventually won through held rank **7 of 21, 6 of 11 and 5 of 17**.
> At the decision that produced 15,950 ms, the winner scored 646.0 against the
> top-ranked 691.3 - seven percent below - and finished **3,625 ms** sooner.

## What was asked

Three tape worlds and three tape-off controls at class 75, alternated,
sequential, one world per bench call, detached with a progress file; then one
tape world and one control at class 125. Then `movement_leg_budget.py` under
LEG_BUDGET2 over the seven trips, reporting the waiting share and the candidate
outcomes at waiting frames. Then a read-only table per decision of the winning
lineage, with the Spearman correlation and the rank the winner held. No C
source change, no other class's campaign, no rewrite of the work order.

## Binding

Build `.codex-research/decision-tape-build-20260922-f`, manifest
`20260922T061108642831Z.sources.json`, sha256
`13eb3d832786aeed982c9fb65d258666dc3726a24105b077a0bc53d9f87aac35`. All
**1,117 source hashes matched the working tree before and after collection**, so
this is the same build as stage 1 and the stage-2 campaign and no rebuild was
needed. `openjkded.x86.exe` sha256
`ec5fe3f0f16751cc7546843334c7b3e5a757027c23f2f9df3d010685c627861c`,
`jampgamex86.dll` sha256
`52238e80ac58c09508acbe754562e2a971d6d1d191a03a9aa458a669be6b0e2c`. **No C
source was touched by this work.**

Tapes, copied and retained beside the worlds that ran them:

| Tape | sha256 | From |
| --- | --- | --- |
| `c75-best.tape`, 11 entries | `401cdce9acfc3847cd50353b1c8ad1b55dd83c8132044782b3e331fa0a52e066` | `.codex-research/decision-explorer-c75-20260922-a/tapes/`, written from `control-a2-d1c1-d2c3-d3c4` |
| `c125-self.tape`, 11 entries | `f51fa0f15cf40d3fee6f12195102abda25a78ee4893a87e69d9e31aa9412e459` | `.codex-research/decision-tape-verify-20260922-g/tapes/` |

Worlds under `.codex-research/decision-explorer-c75-verify-20260922-a/`
(`run_verify.py`, `progress.jsonl`, `campaign.out.txt`, `budget/`,
`tree-c75.json`), every world retained. Every world runs the decision-tape
record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs),
case `red_out_L1_stand`, the 2026-09-16 seeded graph, `--edges --parity 256`,
`--momentum-trace`, one world per bench call, sequential, launched detached with
a progress file. The campaign it checks is
`.codex-research/decision-explorer-c75-20260922-a/` - beam 3, patience 2, **175
worlds, 166 of them passing and 163 usable to carry the beam, 49 minutes of
wall clock**, stopped after depths 4 and 5 did not improve on depth 3's
15,950 ms.

## 1. The reproduction

Eight worlds, 125 s of wall clock, **no pin miss**, so no retry name was used.
Zero deaths and the movement owner on every frame in all eight.

| World | Tape | Release | Completed | Straight / state | Deaths | Owners | Parity compared / mismatches | Census |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `c75-tape-1` | `c75-best` | 3275 | **15,950 ms** | 13 / 2 | 0 | move | 678 / 0 | 11 hits, 0 refused, 0 absent, deferred 1 |
| `c75-control-1` | unset | 3875 | 21,125 ms | 15 / 7 | 0 | move | **0** / 0 | - |
| `c75-tape-2` | `c75-best` | 3875 | **15,950 ms** | 14 / 2 | 0 | move | 698 / 0 | 11 hits, 0 refused, 0 absent, deferred 1 |
| `c75-control-2` | unset | 3875 | 21,125 ms | 15 / 7 | 0 | move | 905 / 0 | - |
| `c75-tape-3` | `c75-best` | 3875 | **15,950 ms** | 14 / 2 | 0 | move | 718 / 0 | 11 hits, 0 refused, 0 absent, deferred 1 |
| `c75-control-3` | unset | 3875 | 21,125 ms | 15 / 7 | 0 | move | 70 / 0 | - |
| `c125-tape-1` | `c125-self` | 3125 | **15,925 ms** | 13 / 5 | 0 | move | 667 / 0 | 11 hits, 0 refused, 0 absent, deferred 2 |
| `c125-control-1` | unset | 3125 | 15,925 ms | 13 / 5 | 0 | move | 667 / 0 | - |

**Every census entry is a hit, so the tape counts as reproduced.** All eleven
entries of `c75-best.tape` were taken in each of the three worlds, with
`attempts=1` on ten of them and `2` on entry 1, `matches` of 6, 4, 1, 1, 3, 6,
3, 1, 1, 2, 1 and **every aim gap 0.0000**. The last entry is taken with
`why=arrives`. The class-125 census reproduces the self-control's own line to
the field, including the two `rerouted` entries at 3 and 5 and `deferred=2`.

**The three class-75 tape worlds are one trip.** Their eleven decisions agree
on kind, hop count, both aims as printed, both run-ups, both landing cells and
both graph edges, and on each decision's level taken as an offset from that
world's own release. `c75-tape-1` was released at 3275 and the other two at
3875: **600 ms apart in the same class, and identical**, which is a longer span
than stage 1's 400 ms pair. The three controls are likewise one trip, thirteen
decisions each.

Two differences that are not the leg. The whole-world commit counts differ
between `c75-tape-1` (13 straight, 2 state) and the other two (14 and 2): the
leg is 11 decisions in all three and the extra commit is on the return after
the flag, which the world keeps playing and the leg does not include.
`c75-control-1` reports **zero parity comparisons** - `horizons=0`, so the
parity harness never sampled one in that world - while its two identical
siblings report 905 and 70. The mismatch count is zero everywhere, but a zero
comparison count is what gate row 1 explicitly excludes, and the sampling is
evidently not stable across identical worlds. That is an observation about the
parity sampler and not about the trip.

## 2. Row 3 on the fast trips

`tools/movement_leg_budget.py`, LEG_BUDGET2, one report per arm, reports at
`.codex-research/decision-explorer-c75-verify-20260922-a/budget/`. **Waiting is
`precommit_grounded` plus `uncommitted_grounded`**, the two owners the
[plan-coverage record](2026-09-21-plan-coverage.md) and gate row 3 sum, as a
share of that leg's grounded frames. Nothing else is counted as waiting:
`leg_start_stand`, `single_grounded_frame`, `runup_before_launch` and
`committed_grounded_without_runup` are excluded.

| World | Window | Frames | Grounded | Waiting | Share | `precommit` | `uncommitted` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `c75-tape-1` | 15,750 ms | 630 | 69 | 21 | **30.4 %** | 21 (30.4 %) | 0 |
| `c75-tape-2` | 15,750 ms | 630 | 69 | 21 | **30.4 %** | 21 (30.4 %) | 0 |
| `c75-tape-3` | 15,750 ms | 630 | 69 | 21 | **30.4 %** | 21 (30.4 %) | 0 |
| `c75-control-1` | 21,000 ms | 840 | 198 | 141 | **71.2 %** | 107 (54.0 %) | 34 (17.2 %) |
| `c75-control-2` | 21,000 ms | 840 | 198 | 141 | **71.2 %** | 107 (54.0 %) | 34 (17.2 %) |
| `c75-control-3` | 21,000 ms | 840 | 198 | 141 | **71.2 %** | 107 (54.0 %) | 34 (17.2 %) |
| `c125-tape-1` | 15,750 ms | 630 | 66 | 25 | **37.9 %** | 25 (37.9 %) | 0 |

Pooled: the class-75 tape arm is 63 of 207 grounded frames, 30.4 percent, with
an airborne fraction of 0.890; the class-75 control arm is 423 of 594, 71.2
percent, airborne 0.764. The class-125 tape world is airborne 0.895.

The share is the gate's unit and the frame count is what changed. The tape trip
is grounded for **69 frames against the control's 198** and waits **21 against
141** - a factor of 6.7 fewer waiting frames - while the share falls only from
71.2 to 30.4 percent, because the denominator fell too. **Neither figure is
under 15 percent**, and every tape world's waiting is entirely
`precommit_grounded`: on the fast trips there is no run that ends the leg
without a commit at all, which is the whole of the control's 17.2 percent
`uncommitted_grounded`.

**The candidate outcomes at waiting frames**, per world, as the tool records
them (no inferred failed searches):

| World | `refused/trigger_contact_unknown` | `fell` | `no_onward` | `no_path` | `slower_route` |
| --- | ---: | ---: | ---: | ---: | ---: |
| each `c75-tape-*` | 42 | 21 | - | - | - |
| each `c75-control-*` | 210 | 28 | 15 | 6 | - |
| `c125-tape-1` | 50 | 15 | - | 3 | 6 |

The mix is the same one the [leg budget](2026-09-20-leg-budget.md) named: the
trigger contact the forecast cannot model dominates every arm, 67 percent of
the tape arm's judged candidates at waiting frames and 81 percent of the
control's. Not one candidate was `admitted` at a waiting frame in any of the
seven worlds, which is what the owner label means.

## 3. The class-75 tree, read only

`tools/movement_decision_tree.py`, with `tools/test_movement_decision_tree.py`
(22 cases on a hand-built campaign: its progress file, its worlds and their
logs are written by the test). Output
`.codex-research/decision-explorer-c75-verify-20260922-a/tree-c75.json`. No
game code and no native run: every input is a retained world of the stage-2
campaign.

**Where the judge's score comes from, because it decides what this can claim.**
The per-candidate `[SPIKE]` line does not print `score`. It prints the rollout's
`progress`, its `done` frame, its landing cells and speeds and its outcome. The
score is `margin + ExitCredit` and the margin is `progress` minus a walk
reference read at that candidate's **own** `done` frame, which the candidate
line never carries, so the score is not reconstructible from a candidate line.
It is printed exactly on the commit line of the world that committed that
candidate - and in a branch campaign every admitted candidate was committed, by
the forced-index override, in its own world. Every measured field of every row
below is therefore read from **the branch world's own commit line**, never
computed and never taken from the parent's view of that index.

**The lineage** is `control-a2` (21,125 ms) to `-d1c1` (18,950) to `-d2c3`
(18,050) to `-d3c4` (**15,950**).

### Decision 1, level 5325 of `control-a2`, 21 admitted, 21 branched

| cand | kind | hops | r0/r1 | cells | progress | walk | margin | exit speed | score | judge rank | trip | trip rank |
| ---: | --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | straight | 2 | 0/0 | 211/7247 | 788.5 | 565.5 | 223.0 | 484.2 | 465.1 | 13 | 19,725 | 8 |
| **1** | straight | 2 | 24/0 | 886/7710 | 863.9 | 599.1 | 264.8 | 548.3 | 538.9 | **7** | **18,950** | 3.5 |
| 2 | straight | 2 | 0/0 | 211/7247 | 788.6 | 565.5 | 223.1 | 484.3 | 465.2 | 12 | 21,000 | 14 |
| 3 | straight | 2 | 24/0 | 886/7710 | 863.9 | 599.1 | 264.8 | 548.3 | 538.9 | 7 | 18,950 | 3.5 |
| 4 | straight | 2 | 0/0 | 211/6164 | 773.9 | 559.9 | 214.0 | 513.1 | 470.5 | 11 | 19,700 | 7 |
| 5 | straight | 2 | 24/0 | 886/7710 | 863.9 | 599.1 | 264.8 | 548.3 | 538.9 | 7 | 19,575 | 5.5 |
| 6 | straight | 1 | 0/0 | 211 | 375.2 | 328.4 | 46.8 | 365.4 | 229.4 | 19 | 19,775 | 9 |
| 7 | straight | 1 | 24/0 | 886 | 416.6 | 357.5 | 59.0 | 371.1 | 244.6 | 15.5 | **18,125** | 1.5 |
| 8 | straight | 1 | 0/0 | 211 | 375.2 | 328.4 | 46.7 | 365.5 | 229.5 | 18 | 19,900 | 10 |
| 9 | straight | 1 | 24/0 | 886 | 416.6 | 357.5 | 59.0 | 371.1 | 244.6 | 15.5 | **18,125** | 1.5 |
| 10 | straight | 1 | 0/0 | 211 | 374.8 | 328.4 | 46.3 | 352.9 | 222.8 | 20.5 | 22,325 | 20 |
| 11 | straight | 1 | 24/0 | 886 | 416.6 | 357.5 | 59.0 | 371.2 | 244.6 | 15.5 | 21,125 | 17 |
| 12 | state | 2 | 0/0 | 211/6164 | 774.3 | 559.9 | 214.4 | 513.2 | 471.0 | 10 | 21,025 | 15 |
| 13 | state | 2 | 24/0 | 886/7710 | 866.7 | 599.1 | 267.5 | 543.9 | 539.5 | 5 | 20,200 | 12 |
| 14 | state | 1 | 0/0 | 211 | 374.8 | 328.4 | 46.3 | 352.9 | 222.8 | 20.5 | 22,325 | 20 |
| 15 | state | 1 | 24/0 | 886 | 416.6 | 357.5 | 59.0 | 371.2 | 244.6 | 15.5 | 21,125 | 17 |
| 16 | straight | 2 | 24/0 | 886/7710 | 864.6 | 599.1 | 265.5 | 548.1 | 539.6 | 3 | 19,575 | 5.5 |
| 17 | straight | 2 | 24/0 | 886/7710 | 864.6 | 599.1 | 265.5 | 548.2 | 539.6 | 3 | 19,950 | 11 |
| 18 | straight | 2 | 24/0 | 886/7710 | 864.6 | 599.1 | 265.5 | 548.3 | 539.6 | 3 | 20,225 | 13 |
| 19 | state | 2 | 0/0 | 211/6164 | 774.1 | 559.9 | 214.2 | 521.9 | 475.1 | 9 | 22,325 | 20 |
| 20 | state | 2 | 24/0 | 886/7710 | 867.0 | 599.1 | 267.9 | 545.1 | 540.4 | **1** | 21,125 | 17 |

Candidate 20 is the one the ranking itself committed in the control. **Spearman
0.178**, n = 21; **14 distinct completion times** from 21 branches.

### Decision 2, level 6850 of `control-a2-d1c1`, 11 admitted, 11 branched

| cand | kind | hops | cells | progress | margin | exit speed | score | judge rank | trip | trip rank |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | straight | 2 | 9197/6814 | 765.1 | 727.3 | 441.7 | 948.2 | 3 | 18,150 | 3 |
| **3** | straight | 2 | 9197/6814 | 755.7 | 717.9 | 437.5 | 936.6 | **6** | **18,050** | 2 |
| 5 | straight | 2 | 9197/6256 | 764.7 | 726.9 | 443.8 | 948.8 | 2 | 18,575 | 4 |
| 7 | straight | 1 | 9197 | 423.6 | 385.8 | 371.0 | 571.3 | 10 | 20,650 | 10 |
| 9 | straight | 1 | 9197 | 423.8 | 386.0 | 377.5 | 574.7 | 8 | **17,750** | 1 |
| 11 | straight | 1 | 9197 | 427.6 | 389.8 | 369.4 | 574.5 | 9 | 19,075 | 7 |
| 13 | state | 2 | 9197/7320 | 728.1 | 690.3 | 422.9 | 901.8 | 7 | 20,225 | 9 |
| 15 | state | 1 | 9197 | 417.9 | 380.1 | 361.5 | 560.8 | 11 | 18,950 | 5 |
| 17 | straight | 2 | 9197/6814 | 765.7 | 727.9 | 442.3 | 949.1 | **1** | 19,575 | 8 |
| 19 | straight | 2 | 9197/6814 | 756.2 | 718.4 | 438.5 | 937.7 | 5 | 18,975 | 6 |
| 21 | straight | 2 | 9197/6256 | 763.2 | 725.4 | 440.0 | 945.4 | 4 | 21,025 | 11 |

The walk reference is 37.8 for every candidate here, so at this frame the
ranking is `progress + 0.5 x exit speed` outright. Candidate 13 is the one the
parent's ranking committed. **Spearman 0.036**, n = 11; **11 distinct times from
11 branches**, no tie at all.

### Decision 3, level 8625 of `control-a2-d1c1-d2c3`, 18 admitted, 17 measured

| cand | kind | hops | r0 | cells | progress | walk | margin | exit speed | score | judge rank | trip | trip rank |
| ---: | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | straight | 2 | 0 | 5038/10634 | 907.3 | 482.1 | 425.2 | 532.3 | 691.3 | **1** | 19,575 | 10 |
| 1 | straight | 2 | 24 | 2605/8184 | 711.2 | 507.2 | 204.1 | 440.0 | 424.1 | 6 | 19,750 | 12 |
| 3 | straight | 2 | 24 | 2605/8184 | 709.2 | 507.2 | 202.0 | 434.5 | 419.3 | 8 | 20,475 | 13 |
| **4** | straight | 2 | 0 | 5038/10360 | 877.4 | 482.1 | 395.3 | 501.3 | 646.0 | **5** | **15,950** | 1 |
| 5 | straight | 2 | 24 | 2605/8184 | 700.9 | 507.2 | 193.7 | 407.7 | 397.6 | 13 | 18,900 | 7 |
| 6 | straight | 1 | 0 | 5038 | 462.8 | 303.7 | 159.1 | 486.4 | 402.3 | 10 | 17,600 | 2 |
| 7 | straight | 1 | 24 | 2605 | 387.1 | 331.3 | 55.7 | 370.2 | 240.8 | 15 | 18,675 | 5 |
| 8 | straight | 1 | 0 | 5038 | 462.2 | 303.7 | 158.5 | 485.5 | 401.3 | 11 | 17,775 | 3 |
| 9 | straight | 1 | 24 | 2605 | 385.7 | 331.3 | 54.4 | 361.7 | 235.2 | 17 | 20,825 | 16.5 |
| 10 | straight | 1 | 0 | 5038 | 461.1 | 303.7 | 157.4 | 478.6 | 396.7 | 14 | 20,825 | 16.5 |
| 11 | straight | 1 | 24 | 2605 | 385.7 | 331.3 | 54.3 | 361.9 | 235.3 | 16 | 20,500 | 14 |
| 12 | straight | 2 | 0 | 5038/10362 | 901.7 | 482.1 | 419.6 | 533.0 | 686.1 | 2 | 18,925 | 8 |
| 13 | straight | 2 | 24 | 2605/8550 | 711.7 | 507.2 | 204.5 | 439.1 | 424.0 | 7 | 18,800 | 6 |
| 14 | straight | 2 | 0 | 5038/10634 | 902.1 | 482.1 | 420.0 | 531.2 | 685.6 | 3 | 19,725 | 11 |
| 15 | straight | 2 | 24 | 2605/8184 | 706.3 | 507.2 | 199.1 | 431.3 | 414.8 | 9 | 19,125 | 9 |
| 16 | straight | 2 | 0 | 5038/10362 | 890.0 | 482.1 | 407.9 | 525.3 | 670.5 | 4 | 18,075 | 4 |
| 17 | straight | 2 | 24 | 2605/8184 | 701.7 | 507.2 | 194.5 | 408.3 | 398.7 | 12 | 20,525 | 15 |

Candidate 12 is the one the parent's ranking committed. **Candidate 2 is
dropped, not estimated**: the engine printed `select_forced=-1` in its world, so
the named index was not an admitted candidate of that world's own search, that
world committed the ranking's choice instead and its 19,575 ms is not candidate
2's trip. **Spearman 0.335**, n = 17; **16 distinct times from 17 branches**.

### Does the judge's ranking predict the completed trip?

**Barely.** Spearman's rho is 0.178, 0.036 and 0.335, all weakly positive, none
near a usable order. The judge's own first choice completed in 21,125, 19,575
and 19,575 ms while the best branch at that decision completed in 18,125, 17,750
and 15,950. The winner of the whole search held rank **7 of 21, 6 of 11 and 5 of
17**, and at the third decision it scored **646.0 against the top-ranked 691.3**
and finished **3,625 ms** sooner. A ranking that the search has to overrule five
to seven places deep at every decision is the ranking's result, not the
explorer's.

The sign is worth keeping: it is positive at all three decisions, so the score
is not anti-correlated. It separates the coarse classes - the one-hop candidates
score near 230 and the two-hop ones near 540 at decision 1 - and then fails to
order anything inside a class, which is where the 4,000 ms lives.

### Ties, and what they say about the identity rule

| Decision | Branches | Distinct times | Repeated times | Repeats that are one decision |
| ---: | ---: | ---: | ---: | --- |
| 1 | 21 | 14 | 5 | 3 of 5 (cands 7/9, 1/3, 5/16) |
| 2 | 11 | 11 | 0 | - |
| 3 | 17 | 16 | 1 | 0 of 1 |

Three repeats at decision 1 are exact duplicates under the decision tape's
identity rule - same kind, hop count, run-ups and landing cells - and are the
same decision offered twice by the search, as the tape record's tie rule
predicts. Two more repeats (21,125 ms from candidates 11, 15, 20; 22,325 ms from
10, 14, 19) are **three different decisions each** landing on the same
completed time, which is the quantisation the runs are already known to have and
not a duplicate.

**The converse is the finding.** The identity rule groups branches the trip does
not. At decision 3, candidates 0 and 14 are the same decision by the rule -
straight, two hops, run-up 0, cells 5038/10634 - and complete in 19,575 and
19,725 ms; candidates 12 and 16 are the same decision and complete in 18,925 and
18,075; candidates 1, 3, 5, 15, 17 are one decision and complete in five
different times spanning 1,625 ms. At decision 1, six candidates share the
identity `straight 2 hops, 24/0, 886/7710` and produce four times spanning 1,275
ms. So "same landing cells" is the right identity for asking whether a hop is
admissible from a different start, which is all the decision-tape record claimed
for it, and it is **not** an identity for the trip: two candidates the rule calls
one decision are different flights.

### One thing the method does, that a reader should know

A branch runs under a prefix tape rebuilt from its parent's leg, not as a replay
of the parent, and the tape's tie rule may take a different one of several
matching admitted candidates. The branches therefore arrive at the decision in a
state very slightly displaced from the parent's: root speed 219.2 against the
parent's 219.2 at decision 1, 151.7 against 151.1 at decision 2, and **442.3
against 437.5** at decision 3, where the displacement is large enough to shift
the rolled-out landing cells (the parent saw candidate 4 land in 4427/10077, the
branches in 5038/10360). Every branch of a decision agrees with **every other
branch to 0.0**, so the comparison within a table is exact; it is the comparison
with the parent's own `[SPIKE]` line that is not, which is why no field here is
read from it.

## What failed

Nothing in the reproduction. Two things are recorded against the campaign and
the instrument rather than this run:

- **`c75-control-1` reports zero parity comparisons** where its two identical
  siblings report 905 and 70. Zero mismatches everywhere, but a zero comparison
  count is what gate row 1 excludes by name, and the count varies by an order of
  magnitude across worlds that are otherwise the same trip. The parity sampler's
  coverage is not stable and nothing here has explained why.
- **Nine of the campaign's 175 worlds did not complete** (`runner_error`), four
  branches printed `select_forced=-1` - the named index was not admitted in that
  world - and six worlds had their prefix stop being taken. The explorer keeps
  all of them out of the beam and retains them; the count is reported here
  because a 95 percent completion rate is the cost model a later campaign should
  size from.
- **Two lineages reach 15,950 ms**, `...-d2c1-d3c4` and `...-d2c3-d3c4`, and the
  result JSON names the second. The tables above are that lineage's. The first
  has not been tabled and no claim is made that they are the same trip.

## What this does not establish

The tape is a forced-selection diagnostic. **No time here moves a gate row**, by
the work order's own stage 3: an explorer time counts only once the bot selects
the tape itself from the map, leg, start cell and its own phase, with the
harness setting nothing but the case, and the fallback is measured. Neither
holds yet; the bench set the cvar in all four tape worlds.

The reproduction is three worlds at one class on one map for one case, and the
cross-check is one world at one other class. That the class-75 tape reproduces
says the decisions it names are admissible from that start on this build, not
that they are robust to contention, to another map checksum, or to a different
server frame or Pmove step, which invalidate a tape by construction.

Row 3's numbers are measured, not judged. The 15 percent threshold was declared
against trips that spent 54 percent of their grounded frames waiting, before any
trip under 16,000 ms existed; whether a 30 percent share over 69 grounded frames
is the same quantity as a 54 percent share over 1,834 is the owner's reading and
this record does not make it.

The correlation is over three decisions of one lineage of one class. It says the
judge's ranking did not order the branches at those frames; it does not say
which weight is wrong, and nothing here proposes one. The winner's own rank
being 5 to 7 is a statement about a search that ran 175 worlds to find it, not
a bound on what a better ranking would have to be.
