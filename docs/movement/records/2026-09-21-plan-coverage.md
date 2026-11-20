# The library already admits an edge at most waiting frames

Recorded 2026-09-21. Read-only analysis of retained evidence. No native run,
no source change, no rebuild. Tool:
[movement_plan_coverage.py](../../../tools/movement_plan_coverage.py) with
[its unit test](../../../tools/test_movement_plan_coverage.py); reports in
`.codex-research/plan-coverage-20260921-a/`; inputs are the sixteen
successor-aim confirmation worlds
(`.codex-research/successor-aim-20260920-a/confirm-{0,1}-r{1..8}/red_out_L1_stand`),
the 2026-09-16 seeded graph cache
(`.codex-research/move-review-20260916-seeded-b/red_out_L1_stand`) and each
world's own written cache, all bound by hash in the report.

This is the [plan-coverage diagnostic](../../archive/movement-2026-10-05/PLAN.md#the-plan-coverage-diagnostic)
the level-1 exit gate asks for first. It answers one question: at the frames
where the bot waited, would a plan in the library it already has have applied?

## The answer

Over 60 waiting runs in 16 worlds, against 94,708 strafe edges:

| | waiting runs | committed runs (control) |
| --- | ---: | ---: |
| Covered - at least one edge admits the state | **34 of 60 (56.7 %)** | 66 of 107 (61.7 %) |
| Uncovered, exactly one axis over | 26 | 41 |
| Uncovered, no edge near on any axis | **0** | 0 |
| Off the graph | 0 | 0 |

Admitting edges on a covered run: median 20, range 1 to 93; 30 of the 34 have
at least one admitting edge leaving the bot's own cell rather than a
neighbour's. The excluding axis on the 26 uncovered waiting runs is the
heading cone 18 times, the takeoff reach 4 times and the speed band 4 times.

**No waiting state is unvisited.** Every one has strafe edges within twice the
reach envelope, so the third branch of the plan's decision table - "sample
plans from real landings, the states are unvisited" - is refused by the
evidence.

**The second column is not independent.** Each confirmation world's own cache
holds 94,706 to 94,710 strafe edges against the seeded cache's 94,708, and the
two columns produce identical verdicts on all 167 runs. The campaign learned
essentially nothing the seeded library did not already hold, so "the current
campaign's own learned edges" is not a second library and must not be reported
as one.

## The cone is not a near miss

The 18 cone exclusions are not marginal. Their margins beyond the 30-degree
cone are 1.8, 1.8, 1.8 and then 110.2 to 134.0 degrees: fifteen of the
eighteen states are heading roughly away from the entry heading of every edge
near them. Dropping the cone axis entirely lifts waiting coverage to **55 of
60**, which is an upper bound and not a per-frame claim. The reach and speed
margins are smaller - 14.6 to 75.8 units, and 73.9 to 334.4 units per second -
but they account for 8 runs, not the bulk.

So the second branch of the decision table, "uncovered by one axis with small
margins, widen the envelope", is not what these numbers say either. Widening
the cone to admit the 15 large-margin states would admit edges the bot would
have to turn 110 degrees to enter.

## What the heading costs the result

The heading is the one part of the state that is not retained at frame
resolution. It is reconstructed as a finite difference to the next position
record where the next frame has one (51 of 167 states) and otherwise as the
chord from the executed hop's launch record to this landing (116 of 167).
Where both exist they are compared: **median 7.7 degrees apart, worst 18.9**.
That error is large enough to move the three 1.8-degree cone cases and far too
small to move the other fifteen.

The tap bit is the landing-frame bit read from the trace. Coverage depends on
it completely: clearing it leaves **0 of 60** waiting runs covered, 34 of them
excluded by the tap axis alone. Every covered waiting run is covered by tap
edges only.

## The envelope rule itself is validated

The game logs its own `Graph_StrafeAdmits` verdict as `envelope=` on the
`[MOVE] event=edge edge=strafe` records. Recomputing those 150 verdicts from
the same cache, edge index, live speed and tap bit: **130 agree, 20 disagree**.
The cell the game used is not logged, so this tool picks the nearest one to the
position record. Allowing any cell within `Graph_Nearest`'s radius, **18 of the
20 disagreements resolve** and the remaining 2 are cone, that is the
reconstructed heading. The rule as implemented here matches the game; the
residual is the unlogged cell choice and the heading.

## Row 3 of the gate, regenerated

The same sixteen worlds under the corrected LEG_BUDGET2 accounting:

| Grounded-frame owner | Runs | Frames | Share |
| --- | ---: | ---: | ---: |
| `precommit_grounded` | 42 | 747 | 40.7 % |
| `runup_before_launch` | 96 | 468 | 25.5 % |
| `uncommitted_grounded` | 18 | 246 | 13.4 % |
| `single_grounded_frame` | 202 | 202 | 11.0 % |
| `leg_start_stand` | 16 | 160 | 8.7 % |
| `committed_grounded_without_runup` | 11 | 11 | 0.6 % |

Waiting is **993 of 1,834 grounded frames, 54.1 percent**, against the gate's
15 percent. The correction did not move the number: the superseded LEG_BUDGET1
figure was 54.2 percent over the eight aim-on worlds. Candidate outcomes on
those frames: 1,374 of 1,885 are `refused` with the trigger-contact bit, 287
fell, 176 out of corridor.

## What this does and does not establish

Covered means an edge's envelope admits the reconstructed state. It does not
mean that edge's plan would have been committed, validated in the game,
survived the forecast, led toward the objective or been faster. The control
matters here: committed frames are covered at 61.7 percent against waiting's
56.7, so **coverage does not distinguish a frame where the bot waited from one
where it launched**. A high covered count is therefore not by itself a
measured speed opportunity.

The near-miss classification only considers edges whose takeoff is within
twice the reach envelope of the cell, so "no edge near" describes that
neighbourhood rather than the map. One case (`red_out_L1_stand`) in sixteen
worlds of one campaign is the whole sample; other level-1 cases are not
measured. Nothing here was run natively.

## Which branch this picks

The first branch of the decision table - most waiting runs covered, so the
library applies and the live admission or forecast refuses it - is the closest
match, with the qualification above that 56.7 percent is a majority rather
than an overwhelming one and that the control is no different. It sits beside
the refusal census on the same frames, where 1,374 of 1,885 candidate
refusals carry the trigger-contact bit.

That branch's next work is native re-validation of library plans together with
a trigger-aware admission. The owner deprioritized the trigger admission
repair on 2026-09-21 for want of a demonstrated payoff; this diagnostic does
not supply one either, because coverage does not separate waiting frames from
committing frames. What it does remove is the alternative explanation that the
waiting states are unvisited or that the envelope is narrowly missing them.
The decision of whether the next explorer is the trigger-aware admission is
the owner's.
