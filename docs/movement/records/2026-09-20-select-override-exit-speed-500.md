# Under exit speed 500 the same frame is worth 2,300 ms, and the ranking takes the worst

Recorded 2026-09-20. **No native source change, no rebuild** - every world is
the frozen diagnostic build `.codex-research/select-override-build-20260920-a`
and every treatment is a cvar. Protocol declared before collection, driver,
analyser and evidence in `.codex-research/select-override-500-20260920-a/`;
the summary quoted here is `summary-override-500-1.json`.

This repeats [the forced-candidate diagnostic](2026-09-20-slow-class-selection-failure.md)
with one change: the fixed policy in every arm is `bot_moveExitSpeed=500`, the
current research setting, instead of 2000. The isolated 1,325 ms was measured
under 2000, and neither the size of the frame's spread nor the identity of its
best candidate carried over by argument, because the weight also ranks every
later decision.

## The arms

Start class 75 pinned, `bot_moveExitSpeed=500` in every arm, three repetitions
each, arms alternated between repetitions, one native world at a time.
**12 cells of 12, none incomplete, 34 native worlds.** Every arm produced one
distinct completion time across its three worlds.

| Arm | Committed at the frame | Start levels | Completion | Against control |
| --- | --- | --- | ---: | ---: |
| control, override off | cand 5 (the ranking's own at 500) | 4075, 3075, 3075 | 21,125 ms | - |
| `nth=3 cand=5` | cand 5 | 3075, 3875, 3875 | 21,125 ms | 0 |
| `nth=3 cand=1` | cand 1 | 3275, 4875, 5075 | **18,825 ms** | **-2,300 ms** |
| `nth=3 cand=13` | cand 13 | 3475, 3075, 3875 | 19,950 ms | -1,175 ms |

The control reproduces the reference build's class-75 value at 500 to the
millisecond, so the diagnostic is inert when off under this policy as well,
and forcing the ranking's own winner changes nothing. The frame is the same
one: third sequence commit at `corner_level + 4775`, **18 candidates, 12
admitted**, `progress=712.8 land1_speed=444.9` for the candidate the ranking
takes. Zero deaths, movement owner throughout, exactly one forced commit
inside each measured trip. Parity still makes zero comparisons.

**Under 500 the ranking takes the worst of the three, not the middle.** One
frame's choice spans **2,300 ms** under one fixed policy, and against its own
admitted set the ranking leaves all 2,300 on it - against 2,150 and 1,325 ms
under 2000.

This is also the mechanism of the class-75 regression in
[the pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md). The
1,025 ms that 500 loses to 2000 at this class is mostly this frame: 500 takes
cand 5 at 21,125 and 2000 takes cand 13 at 20,100, and the frame's own choice
between those two candidates is worth 825 ms measured under the 2000 policy
and 1,175 ms under 500. The remainder is the two policies' effect on the
later decisions, at most 200 ms on any one candidate. It is a selection
failure before it is a property of the weight, and **cand 1 is admitted
under both**.

## The same candidate under both policies

Each arm here has a matching arm under 2000. These are two retained
collections, not matched within-world pairs; the arms inside each collection
are the matched ones.

| Committed candidate | At 500 | At 2000 | Difference |
| --- | ---: | ---: | ---: |
| cand 1 | 18,825 ms | 18,775 ms | +50 ms |
| cand 13 | 19,950 ms | 20,100 ms | -150 ms |
| cand 5 | 21,125 ms | 20,925 ms | +200 ms |

Every later decision in the trip is ranked by the weight, and on the same
committed candidate that is worth at most **200 ms** here, against the frame's
2,300. Adding the weight sweep's own cand-1 band, cand 1 completes 18,775 ms
at 2000, 18,825 at 500 and 19,250 at weight 1000: a span of 475 ms that is
**not monotonic in the weight**, so the later ranking is not simply better or
worse at one end of the range.

**cand 1 is the best of the three under both policies**, and the completion
order cand 1 < cand 13 < cand 5 is the same under both.

## Pricing the horizon a bounded rank would need

Read-only over the retained worlds of both collections
(`horizon.py`, output `horizon-2.json`); `horizon-1.json` is the first output
and is retained. For each committed candidate, the straight-line distance
still to run at common elapsed-time endpoints measured from the frame, median
over that candidate's three worlds. Position is the `[MOVE]` telemetry sample
at or before each endpoint - median sample gap 25 ms, the decision step - and
nothing is interpolated. The three worlds of an arm carry identical sample
counts, so an arm is one trajectory, as its single completion time implies.
Each arm is measured to its own final sampled position; those differ by up
to 53.7 units, small against these separations but not zero.

| From the frame | cand 5 | cand 1 | cand 13 | Ranks first |
| ---: | ---: | ---: | ---: | --- |
| +1,000 ms | 4,966.3 | 4,967.5 | 5,015.0 | cand 5 - worst |
| +2,000 | 4,639.4 | 4,651.4 | 4,702.3 | cand 5 - worst |
| +3,000 | 4,198.1 | **4,195.1** | 4,316.3 | cand 1 |
| +4,000 | 4,078.8 | **4,074.7** | 4,086.0 | cand 1 |
| +5,000 | 3,567.5 | 3,847.1 | 3,769.2 | cand 5 - **winner last** |
| +6,000 | 3,151.1 | 3,156.6 | 3,496.8 | cand 5 - worst |
| +7,000 | 2,895.3 | **2,744.1** | 2,831.6 | cand 1 |
| +8,000 | 2,749.3 | **2,280.3** | 2,483.9 | cand 1 |
| +9,000 | 2,336.7 | **1,888.5** | 2,069.6 | cand 1 |
| +10,000 | 1,991.9 | **1,577.3** | 1,967.0 | cand 1 |

The exit-speed-2000 collection gives the same shape, and the two agree on the
conclusion: the ranking is **stable only from frame + 7,000 ms** - 7,000 for
both the winner and the full order at 500, 6,000 and 7,000 at 2000 - and at
**frame + 5,000 ms the true winner ranks last under both policies**. An
earlier correct endpoint is reachable, at +3,000 at 500 and +2,000 at 2000,
but the order flips back afterwards, so a rank cut there is an accident of
where the flight happens to be. Ranking by cumulative progress over the next
decisions is worse: the winner never leads inside six decisions at 500, and
leads only from the fourth at 2000.

The measured leg after this frame runs 14,000 to 16,350 ms across all six
arms, so a stable horizon of 7,000 ms is roughly **half the remaining trip**.

**This prices the horizon; it does not decide the ranking experiment.** It is
an oracle over the realized continuations under the full live policy, so it
bounds from above what a bounded forecast rank could know at the frame. A
bounded rank computing from each candidate's own rollout can only be worse
informed, and this says nothing about what it would cost.

The earlier "about six seconds" reading is refined rather than contradicted:
the separation is not a single crossing after which the winner leads, and the
+5,000 ms inversion is the part a short horizon would actually buy.

## What this does not establish

- One frame, one start class, one case, one map, one build, three worlds per
  arm. The completion order agreeing under two policies is two observations of
  one frame, not a general property.
- **The override is a diagnostic, not a policy.** Nothing chooses cand 1 at
  run time; the arms that reach 18,825 ms were told which candidate to take.
- Nothing is installed and no acceptance floor is claimed here. The diagnostic
  build's floors were taken when it was built - 36/36 unpinned with the
  override off, both seeded named regressions - and no source changed since.
  `bot_moveExitSpeed=500` remains a research configuration value.
- The horizon is priced by an oracle. The CPU cost of a bounded continuation
  rank is unmeasured and needs the implementation.
- Parity still makes zero comparisons in these worlds.

## Reproduce

Analysis only, from retained worlds; both scripts write a fresh output and
refuse to overwrite:

```powershell
python .codex-research/select-override-500-20260920-a/analyse.py --out <new unique output.json>
python .codex-research/select-override-500-20260920-a/horizon.py --out <new unique output.json>
```

One world of the forced arms, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 40600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_moveSelectNth=3 --set bot_moveSelectCand=1 `
  --corner-class 75 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```

The control is the same command with `bot_moveSelectNth=0` and
`bot_moveSelectCand=-1`.
