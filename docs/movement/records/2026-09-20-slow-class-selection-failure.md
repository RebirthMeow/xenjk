# The slow class is decided at one frame, by three candidates 0.1 apart

> **Corrected and confirmed 2026-09-20.** The first version of this record
> attributed the spread using exit-credit weights alone. That comparison
> changes the ranking of *every* later decision, so it could not isolate the
> frame, and two claims drawn from it were wrong: that later decisions carry
> "at most 250 ms", and that a bounded continuation "would not have found"
> the best candidate. A forced-candidate diagnostic since settled the first
> under one fixed policy and withdrew the second. The controlled result is in
> **[Forcing one candidate](#forcing-one-candidate-one-policy-one-frame)**
> below; the weight-sweep sections that precede it stand as the observation
> that led to it.

Recorded 2026-09-20. **No native source change, no rebuild** - `bot_moveExitSpeed`
is a cvar and every world is the frozen reference build
`.codex-research/successor-aim-build-20260920-a`. Evidence, protocol, driver
and analyser in `.codex-research/slow-class-selection-20260920-a/`; the summary
quoted here is `summary-selection-1.json`, pooled with the class-75 worlds of
[the pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md) and of
the 100-world sweep.

This is the specific selection failure the handoff asked for before bounded
continuation ranking: **several continuations are admitted, and the ranking
chooses a worse one.** It is a repeatable example, not a survey.

## The failure in one frame

At start class 75 - the slow stratum, where `bot_moveExitSpeed=500` loses
1,025 ms to 2000 - the arms are identical for their first two commits and
first differ at **relative level 4,775 ms**, a `kind=straight` commit that
offers 18 candidates and admits 12. The rollout list at that frame is
**byte-identical across arms**: the same 18 candidates, the same measured
landings, the same 12 admissions. Only the ranking weight differs.

Three admitted candidates win it, depending on the weight:

| Candidate | First hop | Landing 1 | Landing 2 | Cell | Progress | Margin | **Completion** |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cand 5 | `d0=-0.365 r0=24` | 371.9 | 444.9 | 8548 | 712.8 | 215.2 | 21,125 ms |
| cand 1 | `d0=-0.643 r0=24` | 378.8 | 448.8 | 8184 | 708.9 | 211.3 | **19,250 ms** |
| cand 13 | `d0=-0.643 r0=24`, re-aimed | 378.8 | 452.2 | 7779 | 705.5 | 207.8 | 20,100 ms |

cand 13 is the successor-aim variant of cand 1: the same first hop with a
re-aimed second hop that lands 3.4 units/sec faster and 3.4 units short. It
wins at the incumbent weight and costs **850 ms** against its own parent.

**The 1,025 ms class-75 regression in the exit-speed confirmation is this
frame**: 500 takes cand 5, 2000 takes cand 13.

## The score cannot tell them apart

`score = margin + landSpeed * ms/1000`. At `ms = 1000` the three logged scores
are **660.1, 660.1 and 660.0** - a spread of 0.1 in 660, one part in 6,600 -
and they produce 21,125, 19,250 and 20,100 ms.

That is not a mis-set weight. The three candidates are nearly collinear in the
two terms the score is made of: from cand 5 to cand 1 the margin falls 3.9 and
the exit speed rises 3.9; from cand 1 to cand 13, 3.5 against 3.4. A linear
score can prefer the middle point of three nearly collinear points only inside
a narrow band of weights, and **the best trip is the middle point on both
terms**: neither the most progress (cand 5) nor the most carried speed
(cand 13).

## The step map: 37 worlds, 14 weights

Completion is a function of which candidate wins that frame:

| Winner at 4,775 | `bot_moveExitSpeed` | Worlds | Completion |
| --- | --- | ---: | --- |
| cand 5 | 250, 500, 750, 800, 900, 950 | 15 | 20,925 / 21,125 / 21,175 ms |
| **cand 1** | 990, 1000, 1010, 1030 | 8 | **19,250 ms, every world** |
| cand 13 | 1050, 1250, 1500, 2000 | 14 | 20,100 ms, every world |

Every arm has one winner at that frame and one completion, over five distinct
absolute start levels inside the class. The cand-13 band holds exactly across
a **two-fold** range of the weight, so the weight's effect on all the later
decisions - which it also ranks - changes nothing there. Inside the cand-5
band the later decisions do move the trip, by **250 ms** over 250 to 950.

That looks like one frame carrying the spread, and the controlled experiment
below confirms that it does - but **this evidence cannot show it**, and the
"downstream carries at most 250 ms" reading that the first version of this
record drew from it is withdrawn. A band that holds across a weight range
shows only that the trip is insensitive to the weight *given that winner*; the
forced arms below find later decisions worth **475 ms** on the same candidate.

The best outcome exists only for weights between 950 and 1050, a window at
most 100 wide in a range where 500 and 2000 are the values under discussion.
Nothing in the design put that window there; the 19,250 ms trajectory is
reachable, but not by tuning this weight.

## Forcing one candidate: one policy, one frame

The weight sweep above cannot isolate the frame, so a diagnostic was built
that does. `bot_moveSelectNth` and `bot_moveSelectCand` (default off) commit a
**named admitted candidate at a named sequence commit**, counted from the
movement state's reset, which the corner start performs. The override runs
after `StatePreferred` - the last thing that can change the winner - and
before the route is installed, so the forced candidate keeps its own route and
every later check, and it can only select a candidate the judge already
admitted. Build `.codex-research/select-override-build-20260920-a`, sources
frozen and hash-verified; evidence
`.codex-research/select-override-20260920-a/summary-override-1.json`.

Four arms, three repetitions each, start class 75 pinned, **`bot_moveExitSpeed`
2000 in every arm**, so the policy at every other decision is identical and
only the third commit differs:

| Arm | Committed at the frame | Completion | Against control |
| --- | --- | ---: | ---: |
| control, override off | cand 13 (the ranking's own) | 20,100 ms | - |
| `nth=3 cand=13` | cand 13 | 20,100 ms | 0 |
| `nth=3 cand=1` | cand 1 | **18,775 ms** | **-1,325 ms** |
| `nth=3 cand=5` | cand 5 | 20,925 ms | +825 ms |

Three of three in every arm, over four distinct start levels in the class.
Zero deaths, movement owner throughout, zero parity mismatches. The control
reproduces the reference build's 20,100 ms to the millisecond, so the
diagnostic is inert when off, and forcing the ranking's own winner changes
nothing, so the override itself costs nothing.

**One frame's choice spans 2,150 ms under one fixed policy** - 18,775 to
20,925 - and the ranking takes the middle of the three. Against its own
admitted set, under its own policy, it leaves **1,325 ms** on this frame.

Two things the weight sweep had wrong fall out of this:

- **Later decisions are not negligible.** cand 1 completes 19,250 ms under the
  weight-1000 policy and **18,775 ms** under the weight-2000 policy: 475 ms
  from the later ranking alone, on the same committed candidate. The earlier
  "at most 250 ms" is withdrawn.
- **The best trip at this start is 18,775 ms**, which no weight-only arm
  reached. Tuning the weight could not find it, because the weight that picks
  cand 1 also ranks everything after it.

## How far ahead the winner is visible

Now that the three arms share a policy, the horizon question can be asked
honestly. Matched at start level 3,075, ranking the three by the realized
cumulative progress of the decisions that follow:

| Decisions ahead | cand 5 | cand 1 | cand 13 | Ranks first |
| --- | ---: | ---: | ---: | --- |
| 1 (the frame itself) | 712.8 | 708.9 | 705.5 | cand 5 - worst |
| 2 | 1,219.9 | 1,213.6 | 1,178.4 | cand 5 - worst |
| 3 | 1,582.7 | 1,592.7 | 1,855.3 | cand 13 - middle |
| 4 | 2,390.5 | **2,398.4** | 2,359.2 | cand 1 - by 7.9 |

Distance remaining to the pickup says the same: the winner is level or behind
until about relative 11,000, six seconds after the decision.

| Relative | cand 5 | cand 1 | cand 13 |
| ---: | ---: | ---: | ---: |
| 8,000 | 4,157 | 4,123 | 4,129 |
| 10,000 | 3,521 | 3,491 | 3,354 |
| 12,000 | 2,831 | **2,502** | 2,660 |
| 14,000 | 2,303 | **1,579** | 1,883 |

**This prices the horizon; it does not decide the ranking experiment.** What
is measured here is where the *realized* continuations separate under one
policy, which is not the same as what a bounded ranking would compute from
each candidate's landing at a declared common endpoint. That experiment is
untested and this record does not refuse it. What it does say is that on this
frame a one- or two-decision horizon ranks the worst trip first, so the
horizon is a parameter to measure rather than assume.

Each arm's distance is to its own recorded pickup position; those differ by up
to 29 units, which is small against these separations but not zero.

## Other facts from the leg

The fastest arm is not the most airborne. Over the outbound leg at start
3,275: cand 5 arm 76.3 % airborne at mean 347.8 units/sec, cand 1 arm 79.4 %
at **374.3**, cand 13 arm **80.8 %** at 362.4. Mean speed orders the trips
here; airborne fraction does not.

## What this does not establish

- One start class, one case, one map, one frame. The forced arms are on the
  diagnostic build; the weight sweep is on the frozen reference build, and the
  control ties them together at 20,100 ms.
- **The override is a diagnostic, not a policy.** Nothing chooses cand 1 at
  run time; the arms that reach 18,775 ms were told which candidate to take.
- It fires once per movement-state reset, so the return leg sees it too. Every
  second firing in these worlds is after completion and is counted separately;
  exactly one commit is forced inside each measured trip.
- Only 500 and 2000 have acceptance floors. **No weight near 1000 is a
  proposal**: its advantage here is a knife-edge at one start, and the
  100-world sweep has it losing elsewhere.
- Nothing here changes the confirmed configuration, and nothing is installed.

## Reproduce

Analysis only, from retained worlds; writes a fresh output and refuses to
overwrite:

```powershell
python .codex-research/slow-class-selection-20260920-a/analyse.py `
  --out <new unique output.json>
```

One world at the slow class, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/successor-aim-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 37600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=1000 --set bot_moveSuccessorAim=1 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500
```

The frame is the third sequence commit, at `corner_level + 4775`; its
candidate rollouts are the `[SPIKE]` lines at that level.

The forced arms swap the build for
`.codex-research/select-override-build-20260920-a`, fix
`--set bot_moveExitSpeed=2000`, and add
`--set bot_moveSelectNth=3 --set bot_moveSelectCand=1`. With
`bot_moveSelectNth=0`, the default, the build is the reference build's
behaviour: 36/36 on the unpinned floor, both seeded named regressions pass,
zero deaths, movement owner throughout, and 20,100 ms at this class.
