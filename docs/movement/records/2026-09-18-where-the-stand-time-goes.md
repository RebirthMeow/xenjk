# Where the stand time goes: the run-up at a fast landing

The [tap measurement record](2026-09-18-tap-onward-measurement.md) showed that
admitting the continuation does not move the stand times, and asked where the
deficit actually is. This record answers that from the retained worlds alone.
No native run was made for it: `tools/movement_replay.py` over the twelve
control stand runs of `.codex-research/tap-budget-20260918-a`, plus each
world's own `[SPIKE]` and `[MOVE]` telemetry.

## The bot is not standing around; it is flying slowly

Across the twelve runs the mutually exclusive partition is **80.6 % flight**,
11.4 % search walk, 3.7 % approach, 2.2 % prefix walk, 2.0 % tail walk. Flight
distance scales with launch speed, so the stand deficit is a launch-speed
deficit, not idle time.

227 landings, **107 broken chains**, 36,375 ms of ground time inside the
breaks and **18,429 units/sec of speed lost** in them. The tap arms of the
same campaign change none of it: 108, 110 and 101 broken, loss within 5 %.

## The search never runs while the bot is in the air

Of 323 searches in the twelve runs, **zero** fall strictly inside a flight;
140 fall exactly on a flight boundary. A successor therefore cannot be
committed before touchdown, and at a break the bot is on the ground from the
landing until the next plan launches.

That ground interval is not search latency. 73 of the 107 breaks search on the
landing frame itself (median land to first search: **0 ms**). The delay is
afterwards: median commit to launch **100 ms**, four frames.

## What those four frames are: the run-up

Splitting the breaks by the run-up distance of the plan that was committed:

| committed plan | n | commit to launch | speed lost | land speed |
|---|---:|---:|---:|---:|
| `r0=0` (tap at once) | 26 | **0 ms** | 133 | 420 |
| `r0=24` (24 units of run-up) | **81** | **100 ms** | **165** | 415 |

`HopStep` asks for `Wishspeed(ps)` during a run-up stage - about 250 - while
the bot is travelling at 415. The executor spends four frames commanding a
walk that is slower than the motion it already has, then taps. The search
prices it exactly that way, and says so in its own comment:

```c
// The first hop launches at the speed the run-up leaves: the live
// speed when tapped at once, the walk's when a run-up sheds it.
v0=straightRunups[b]>0?Wishspeed(ps):max(Wishspeed(ps),Speed2D(ps));
```

So the run-up candidate is generated at every search, priced at wishspeed,
and committed at 81 of the 107 breaks.

## Why banning the run-up is not the repair

At **66 of those 81** breaks there was no admitted `r0=0` candidate at all.
The immediate tap was refused, and the refusals are dominated by the path
gate: across those searches the immediate candidates came back `off_path` 280
times, `refused` 150, `no_onward` 19, `corridor` 9, `arrives` 4. Removing the
run-up there would leave the bot walking, not chaining.

At the 15 breaks where both were admitted, the run-up candidate **wins 11
times**, with median judged progress 731 against 509. It wins honestly under
the current rule: it launches further along the path and covers more ground
inside the horizon. The rule is what is wrong. Progress over a horizon has no
term for the speed a plan hands to the next one, and 80.6 % of the run is
flight bought with exactly that speed.

## What this says about the work so far

The path gate that `bot_moveReroute` was built to relax is the right
constraint and was measured in the wrong place. Its cost is not mainly at the
jump-6 probe root; it is at the 66 breaks per twelve runs where the immediate
tap is refused `off_path` and the fallback sheds 165 units/sec. Equally, the
tap continuation could not have helped here: it rescues a candidate the walk
refused at its own last landing, which is a different frame from the break
after a completed sequence.

## Next, in order

1. **Give the judge an exit-speed term.** At a commit, rank by the state the
   plan hands forward, not by distance covered inside the horizon. The state
   planner already models a landing state (`Graph_PlanState`), so the
   machinery exists. This is the one change the evidence here actually
   supports, and it is measurable on the same twelve-run control.
2. **Re-measure path-free admission at the break**, not at the probe root:
   the 66 `off_path` refusals are where it would pay, and the earlier
   stale-edge storm came from installing a new route at the commit, which a
   break may not need.
3. The deaths and the older onward deadlock, unchanged from the previous
   record's list.

## Limits

One campaign, one build, level 1, two stands, twelve runs. `speed_lost` and
the break classification are the replay analyzer's event-interval accounting,
not measured ground time; `commit to launch` is a command-boundary
difference. The `[SPIKE]` progress figures are the judge's own numbers for
candidates in the same search, which is what the comparison is about, and are
not a claim about the route either candidate would have finished in.
