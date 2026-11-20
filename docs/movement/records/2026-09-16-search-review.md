# No-Force fastest-run investigation, 2026-09-16

The owner's target is a stationary start at the home stand, ending at the
enemy flag pickup, with tap jumps and no Force use. The owner reports doing
this in less than 15 seconds. That is the performance target to investigate;
the current roughly 19-second bot result is not evidence of a completed
fastest-time search. No human trace was measured in this review.

## Retained measurements and their limits

The four stand worlds in exploration `b` report final abstract graph costs
of 13.762--14.088 seconds, each with 17 strafe edges:
[red outbound](../../../.codex-research/move-explore-20260916-b/red_out_L1_stand/server.log),
[red return](../../../.codex-research/move-explore-20260916-b/red_ret_L1_stand/server.log),
[blue outbound](../../../.codex-research/move-explore-20260916-b/blue_out_L1_stand/server.log),
[blue return](../../../.codex-research/move-explore-20260916-b/blue_ret_L1_stand/server.log).
These costs are planner estimates, not native executions or proof that a
sub-15-second chain exists. Exploration also began before the requested
fixture in those retained runs, so their initial-state binding is defective.

[Benchmark b](../../../.codex-research/move-explore-20260916-bench-b/result.json)
records stand executions of 19.125--20.750 seconds with only 5--8 state-plan
commits. [Benchmark d](../../../.codex-research/move-explore-20260916-bench-d/result.json)
records 19.525--23.000 seconds for its three completed stand cases; red
outbound is incomplete. Its complete verdict is seven passes, one death and
four runner errors. These different builds/runs are retained diagnostic
evidence, not a controlled comparison of today's working tree. Return legs
also have different objective conditions from the owner's outbound target.

The first question is why the estimated chains fail to become executed
chains. Increasing the number of sampled edges alone does not answer it.

## Identified search and execution limitations

Source references below name functions because the working sources are
being edited during this investigation.

- **Optimistic non-landing entry states.** Previously `Graph_StrafeAdmits`
  checked speed and heading only when an edge required a landing-frame tap.
  Other edges could inherit their measured exit speed and flight-only cost
  from an incompatible entry state. In
  [red outbound benchmark b](../../../.codex-research/move-explore-20260916-bench-b/red_out_L1_stand/server.log),
  level 6375, edge 22975 passes the envelope at live speed 61.8 although its
  recorded entry speed is 250.5; its replay candidates fail or lose to the
  walk. The current [graph source](../../../codemp/game/bot2_graph.c)
  applies speed and heading checks to every strafe edge. This fixes the
  identified admission defect; the fresh validation below separates safety
  from the still-unmet performance gate.
  Position tolerance and approximate state transitions remain relevant.
- **The replay does not reproduce the learned policy.** `StraightSearch` in
  [movement source](../../../codemp/game/bot2_move.c) reconstructs a heading
  toward the stored landing instead of using the stored control heading.
  Incoming momentum affects the landing, so these headings need not agree.
  Compare both controls from identical native entry states before deciding
  how a learned policy should adapt to a displaced takeoff.
- **Local merging is not whole-route dominance.** `Graph_AddStrafeEdge`
  merges equal cell pairs and tap requirements within an entry-speed band,
  retaining the shortest flight without preserving distinct entry/exit
  headings. A locally slower edge may leave a better state for the next hop.
  `StateKey` also groups speed in 32-unit bands and landing headings into
  octants, drops heading for ordinary ground states, and keeps one cheapest
  representative. These are approximations, not proved dominance rules.
  `StateLink` changes the heading while pricing scalar speed, so corner
  turning cost can also be optimistic.
- **The heuristic has no established lower-bound guarantee.** `Heuristic`
  divides horizontal distance by a fixed 450 units/second. Faster learned
  transitions can violate that assumed upper speed. Search closes states
  and stops at the first goal; without a valid bound, this does not establish
  even the cheapest path in the represented graph. A diagnostic Dijkstra
  comparison can separate heuristic effects from candidate coverage.
- **Exploration is a restricted proposal search.** `ExploreCorridor` uses a
  configurable radius (96 units by default) and 64-unit height window around
  the current path. `ExploreState` samples rest plus speeds 220, 280, 340,
  400 and 460 at headings -30, 0 and +30 degrees about that path. It proposes
  one/two hops aimed along the same path, with run-ups of 0 or 24 units and
  three first-hop aim distances. `StraightJudge` requires nearby-path
  landings, at least 16 units of immediate progress advantage over walking,
  and 48 frames of safe walking continuation. Valid pit/corner chains that
  need another jump, temporarily reduce direct progress, or land away from
  the existing corridor can therefore be absent or rejected. Safety must be
  demonstrated by a validated continuation, not removed to admit them.
- **Stopping is not convergence.** Exploration stops when replanning adds
  no unvisited corridor cells or reaches the round limit. Cells already
  visited are not systematically revisited after new edges change useful
  headings and continuations. Neither that stop condition nor a large edge
  count certifies fastest time. Walking permits also suppress the straight
  search while active, an existing source of lost acceleration opportunity.

## Gap death: admission ended before the danger was observed

In the [blue outbound respawn-5 log](../../../.codex-research/move-explore-20260916-bench-d/blue_out_L1_respawn5/server.log),
gap plan 4 is committed at level 11825. It lands at level 14000, z1368.1,
speed370.4, and immediately brakes for `floor_missing`. At level 14450 it is
declared complete at z1287.8, already about 80 units below the landing, with
zero parity mismatches. At level 14700 the bot has no path at z1174.4; legacy
ownership follows, then death.

The rollout ended after the required 128 units of onward progress before
the fall reached the 256-unit threshold. It admitted an unsafe continuation
that forecast and live execution agreed on. The current `HopHoldCommand`
extends gap and replayed-hop observation through the same 48-frame hold used
for straight candidates and rejects exhausted-horizon holds. The source fix
is present. The matched seeded regression now completes without death;
the full results below still do not qualify the experimental seed for the
ownership/performance gate. Zero parity mismatches alone never qualifies a
case.

## Fresh validation and search experiment

Native sources were frozen for build
`.codex-research/move-build-review-20260916-a`; campaign results link its
source/binary manifest and record settings, case and seed hashes. No
installed game files changed. Timing worlds ran serially on localhost.

| Arm | Result | Limits |
|---|---|---|
| [Unseeded floor](../../../.codex-research/move-review-20260916-floor-a/result.json), `--edges --parity 256` | 36/36, no deaths, zero parity mismatches or short walk comparisons, only `move` ownership | Regression floor, not fastest-time qualification |
| [Exploration seed b, level 1](../../../.codex-research/move-review-20260916-seeded-b/result.json), same flags | 12/12, no deaths, zero mismatches or short comparisons; former fatal blue outbound respawn-5 completes in 20.825 s | Two cases include legacy ownership; mean time is 2.085 s slower than the retained greedy Speed-edge arm, so the full state-planner gate is not met |

Matched stationary outbound cases in the seeded arm take **19.150 s red**
and **19.925 s blue**. Both fail the owner's under-15-second target; this is
recorded separately in the [target assessment](../../../.codex-research/move-review-20260916-seeded-b/no-force-target.json).
Unseeded stationary outbound times are 21.450 and 21.275 s. These native
runs do not establish a sub-15-second policy.

Initial review runs `move-review-20260916-regression-a` and `-seeded-a`
accidentally omitted `--edges`, disabling gap discovery. They are retained:
the latter has 9 passes and 3 deaths. They are off-configuration diagnostics,
not the matched qualification arm. No failed evidence was removed.

The [offline comparison](../../../.codex-research/move-review-20260916-search-compare-a/results.json)
extracts the actual current loader, admission and `Graph_PlanState` code,
and changes only the heuristic to zero for its Dijkstra arm. Source, seed,
fixture hashes and both paths are retained. No native gameplay is involved.

| Stationary graph start | A* cost | Zero-heuristic cost | Expanded states, A* / zero |
|---|---:|---:|---:|
| Red stand to blue stand | 13.145 s | 12.790 s | 228 / 22,636 |
| Blue stand to red stand | 13.001 s | 12.745 s | 168 / 22,263 |

Neither search overflowed. The zero-heuristic searches took 115--118 ms
in the isolated harness versus 1--2 ms for A*. Thus the existing search
does leave lower modeled costs undiscovered, but a synchronous Dijkstra
replacement would exceed the frame budget. Both arms retain the same
lossy state merging and optimistic transition model; neither proves a
continuous or executable optimum. The roughly 0.3-second modeled gain is
also much smaller than the model-to-execution gap.

The review also corrected the campaign comparison tool: `time_ms` on a
failed case can mean time to death, so failures are now excluded from
completion-time means/deltas and explicitly labeled in case tables. They
remain in the verdict. All 22 benchmark-helper tests pass, including this
regression. The [isolated graph regression](../../../.codex-research/graph-fixes-20260916-unit/README.md)
passes capacity round trips above 16 MiB, LF/CRLF/EOF parsing and entry
speed/heading checks. A 94,711-edge cache loaded in about 0.3 s in the new
native run, compared with 88 s in the retained pre-fix run; this is an
observed startup improvement, not a controlled microbenchmark.

The [fresh exploration smoke](../../../.codex-research/move-review-20260916-explore-a/result.json)
uses red outbound level 1, one round, 32-unit corridor radius and eight
entry states per batch. Its retained server log orders the fixture command
before `set bot_moveExplore 1`, then `[EXPLORE] begin` (lines 345, 367 and
369). It explores 549 cells, learns 5,416 strafe edges, finishes without
overflow/error, and writes/merges its 5,426-edge cache in a 76-second world.
The abstract final cost is 15.006 s, not a native route time. The
[native replay](../../../.codex-research/move-review-20260916-explore-replay-a/result.json)
of this newly bound seed passes at **19.775 s**, only rewrite ownership,
zero parity mismatches and no short walk comparisons. This verifies the
fixture-to-exploration-to-cache-to-execution workflow; the deliberately
small exploration budget does not establish convergence or the target.

## Next experiments, in order

1. Finish qualifying the experimental seed's ownership and performance:
   safety passes, but its two legacy handoffs and slower mean remain open.
   Keep the unseeded floor green and retain the named failure fixtures.
2. Re-explore with corrected admission and fixture binding, retain each proposed sub-15-second
   chain, and replay it from the exact stationary stand through native
   pickup. Record predicted versus actual segment time, entry/exit state,
   control heading, refusal, braking and replan. Compare stored control
   headings with reconstructed landing headings from identical states.
3. Use the Dijkstra comparison as an offline diagnostic; derive an
   admissible bound for the online search and test preserving distinct
   entry/exit transitions instead of minimum-flight-time merging.
   Measure which approximation actually changes the executable incumbent.
4. Broaden pit and corner proposals beyond the current walking corridor and
   allow a fully validated next jump to establish safe continuation. Keep
   complete native rollout validation, no forbidden spend, and no death.
   Report best executed time, preparation budget and remaining unexplored
   choices separately. Until demonstrated otherwise, the search is
   incomplete and no optimality claim is made.
