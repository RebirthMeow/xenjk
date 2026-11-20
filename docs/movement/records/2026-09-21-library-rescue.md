# Arm A rejected: the library commits, and the trip gets slower

Recorded 2026-09-21. Implementation and screen of the
[library-rescue arm](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-21-the-library-rescue-arm),
the work order the [plan-coverage diagnostic](2026-09-21-plan-coverage.md)
picked. Build `.codex-research/library-rescue-build-20260921-b`; screen in
`.codex-research/library-rescue-screen-20260921-a/`.

> **Result: not accepted.** Row 2 worsens by 1,988.7 ms at declared weight
> and row 3 falls 2.0 points, far short of the gate. The switch stays
> default off. Arm B's precondition is not met and it does not run.

## What was added

Two functions and one default-off switch. No admission threshold moved.

`Graph_AdmittedStrafe` (`bot2_graph.c`) returns the strafe edges a state
admits from a cell, cheapest first, excluding avoided and disabled ones. Its
owner set is the planner's own - the cell and the neighbours a diagonal step
passes through - and the test it applies is the unchanged
`Graph_StrafeAdmits`, so what it offers is what the state search could have
planned from that cell.

`StraightSearch` (`bot2_move.c`) gains one pass, guarded by
`bot_moveLibraryRescue`, which runs only when the judge admitted nothing and
the bot is on the ground. The search's own early exits are untouched, so
ordinary walking, a straight too short, a near goal and an invalid path
never reach it. Each admitted edge becomes one candidate: a single hop from
where the bot stands, aimed at the edge's learned landing by the same
`AimAt` the planned chain uses, with no run-up, because the envelope prices
an edge from its measured launch and a run-up walks the state that admitted
it away before the tap. The batch is capped at `MOVE_RESCUE_MAX` 8 - a
covered waiting frame has a median of 20 admitting edges - and judged by a
second `StraightJudge` call over its own slots, so every gate is the judge's
own: pit, trigger contact, corridor, fall, off-path, off-graph, progress,
the walk margin, the reroute revalidation and `AuditOffPath`. A rescue
winner then takes the same reroute install, the same commit and the same
parity arming as any other candidate.

Telemetry is printed after the search returns and never inside it: one
`[MOVE] ... reason=rescue` line per rescue search with the edges offered,
how many the judge admitted, whether one was committed, and the batch's own
outcome census. A committed sequence carries `rescue=1`. The census is
tallied from the results rather than from the judge's counts, because a
rollout can end on a decline the candidate table does not name - `corridor`
is the common one - and a census built from the table alone reported whole
batches as unexplained. That was caught in the first smoke world and fixed
before the screen.

## The smoke world

One world, class 150, rescue on
(`.codex-research/rescue-smoke-20260921-b/on-r1`): **pass at 19,575 ms**,
zero deaths, movement owner throughout, 833 parity comparisons with zero
mismatches and zero refusals. The rescue fired 11 times and offered 5 to 8
edges each time; two of those searches committed a library edge. The
refusal census of the other nine is already informative: `corridor` leads
it, then `refused`, `fell`, `slower_route` and `no_onward`. One world is not
a measurement of anything but liveness.

## Screen

Case `red_out_L1_stand`, the four declared start classes by
`--corner-class`, six matched pairs per class, arms alternated in sequence
and never in parallel batches, the leading arm swapped every pair. The
control is the same build with the switch off. Settings:
`bot_moveSuccessorAim=1 bot_moveReroute=1 bot_moveExitSpeed=500
bot_moveStatePrefer=1 bot_moveSuccessorFan=0 bot_forecastItems=0
bot_forecastTriggerShadow=0`, playback and heading sweep off, the
2026-09-16 seeded graph, `--edges --parity 256`.

Driver: `.codex-research/library-rescue-screen-20260921-a/run_screen.py`,
one world per bench call with its own output directory, progress appended to
`progress.jsonl`.

## Prediction, stated before collection

If learned edges transfer across the envelope's width, row 3 falls because a
library flight is committed where the walk resumed. If rescue candidates are
refused at the search's own rate for the same reasons, learned edges do not
transfer from near-envelope states, and the next explorer is whole chains
validated as chains.

Acceptance is row 3 falling with row 2 no worse at declared weight. A row-2
gain with row 3 unchanged is a selection artifact and is not taken. A
passing screen then takes the 36-case floor, both seeded regressions, parity
with a non-zero comparison count, and in-trip rollout cost, before anything
is called a mechanism win.

## What the screen measured

48 worlds, **44 complete** and 4 given up because the start never landed on
the pinned class inside the window (`off` c125 r2, `off` c150 r5, `on` c100
r2 and r4 - a pin miss, not a crash: clean navmesh init, rcon quit, no
deaths). Zero deaths everywhere, the movement owner on every completed
world, **30,406 parity comparisons with zero mismatches and zero refusals**,
24 distinct start levels. As in every campaign on this fixture, each cell
returned **one distinct completion time**, so the six repetitions measure
reproducibility, not spread.

### Row 2, completion time

| Start class | off (control) | on (rescue) | change |
| --- | ---: | ---: | ---: |
| 75 (weight 4) | 21,125 ms | 18,400 ms | **-2,725** |
| 100 (weight 12) | 19,375 ms | 19,300 ms | -75 |
| 125 (weight 30) | 15,925 ms | 19,350 ms | **+3,425** |
| 150 (weight 47) | 17,575 ms | 19,575 ms | **+2,000** |
| Declared weight | **17,427.7 ms** | **19,416.4 ms** | **+1,988.7** |
| Equal weight | 18,500 ms | 19,156.2 ms | +656.2 |

### Row 3, waiting share of grounded frames

| Start class | off | on | change |
| --- | ---: | ---: | ---: |
| 75 | 71.2 % | 61.7 % | -9.5 |
| 100 | 59.5 % | 53.4 % | -6.1 |
| 125 | 37.9 % | 55.9 % | **+18.0** |
| 150 | 50.5 % | 60.3 % | **+9.8** |
| Pooled | 60.3 % | 58.3 % | -2.0 |

**The two rows move together, class by class.** Where the rescue helps it
helps both; where it hurts it hurts both. That is the attribution the gate
was designed to produce, and it says this is a mechanism change rather than
a selection artifact - a mechanism change in the wrong direction at the two
classes carrying 77 of the 93 declared weight.

### The rescue's own census

217 rescue searches over the 24 on-arm worlds, **1,571 edges offered, 113
admitted (7.2 percent) and 45 committed**. Nearly every world commits
exactly two. The refusals:

| Outcome | Count |
| --- | ---: |
| `corridor` | 726 |
| `refused` | 365 |
| `slower_route` | 160 |
| `fell` | 131 |
| `no_onward` | 76 |
| admitted (`rerouted` 59, `admitted` 54) | 113 |

## What this establishes, and what it refuses

The prediction offered two branches and the measurement fits neither
cleanly, which is the result worth recording. Learned edges **do** transfer
across the envelope's width: 113 of 1,571 offers survive every gate and 45
are committed, at frames where the search had admitted nothing. What they do
not do is pay. Two committed library flights per world cost 3,425 ms at
class 125 and 2,000 at class 150, and they leave the bot waiting *more*
afterwards, not less. Waiting at those frames was cheaper than the flight
the library offered.

So the coverage result's first branch is now measured and closed: the
library applies, the live pipeline can be made to admit it, and admitting it
is worse. That is not a statement about the trigger - the leading refusal is
`corridor` at 726 of 1,458, not a trigger contact - and it is therefore
**arm B's precondition failing**: objective-flag contact does not lead the
rescue census, so the item-branch repair does not run.

The limits: one case (`red_out_L1_stand`), one distinct outcome per cell so
six repetitions cannot bound within-cell variance, four missing worlds, the
cheapest-eight cap never varied, and the run-up fixed at zero. A different
ranking or a wider cap is unmeasured. Nothing was installed and the switch
stays default off.

## What follows

The order's failure branch names it: **whole chains validated as chains**
rather than single hops offered one at a time. The census supports that
reading - an admitted single hop is most often refused later for `corridor`
or `slower_route`, which is what a flight with no validated continuation
looks like - and so does the coverage record's fifteen landings facing more
than 110 degrees away from every nearby edge's entry heading. That is a
proposal for the owner, not a decision: no explorer is built before the
owner picks one.
