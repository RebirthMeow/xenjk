# Refined movement method, 2026-09-19

Owner direction after the full logic review. This is a design and work-order
update, not an implementation or new qualification result. The
[repair evidence](2026-09-19-review-repairs.md) is the latest measured baseline.
The current design lives in [ARCHITECTURE.md](../ARCHITECTURE.md), acceptance
in [PLAN.md](../PLAN.md), and the next executable work in
[HANDOFF.md](../HANDOFF.md). This decision supersedes requests to wait for
respawn demonstrations, universal start-only entry claims, and the old rule
that complete maneuver chains must never be stored.

## Intended behavior

Independent movement is the base capability. The bot should find reasonably
fast movement from its actual state even when no prepared route applies.
The owner's target is roughly 15 seconds for the stationary no-Force stand
case that previously took about 20-21 seconds. This is a target to measure,
not a claim of feasibility or a deadline for every arbitrary start.

Prepared plans are optional candidates: they may come from human recordings
or offline search. Select one only when its entry, remaining travel and
resource use make it worthwhile relative to independent progress. A failed
route must not strand the bot or require a successful rejoin before it moves.

One runtime movement owner receives the objective, considers candidates,
validates the immediate continuation, executes the selected plan and handles
misses. Continue a viable committed maneuver rather than replacing it at every
frame. When a flight is interrupted, manage the actual flight toward a
reachable landing; standing still or applying a ground recovery command is
not a general airborne recovery policy. Rejoining is a candidate connection,
not another independent controller.

## Three objects currently called a route

| Object | Meaning | What it does not establish |
| --- | --- | --- |
| Graph path | Approximate connectivity, cost and arrival-state proposals | An executable continuous native trajectory |
| Executable maneuver sequence | Commands/policy rolled from an actual root, with expected successor states | Success outside the rollout's supported world and state binding |
| Prepared plan / recorded tape | A retained sequence and its timing, state, resource and world binding | Feasible entry from an arbitrary live state or resilience to interference |

The current graph has momentum state, but quantizes it and substitutes stored
edge exits. The independent search validates short candidates; its usual
one/two-hop vocabulary and walking continuation constrain what it can find.
The current offline explorer samples synthetic corridor states and harvests
individual edges. It is not a full-route optimizer carrying exact native
successor states through the whole trip.

Human playback is a manually armed research fixture with one global tape,
placement and a separate frame hook. It proves useful execution facts; it is
not the final automatic route-selection owner. Current hop discovery also
requires `ParityHorizon`'s dedicated/cheats/movement-only/rewrite/parity gates.
Neither path is qualified ordinary multiplayer movement merely because a
benchmark passes.

## Shared method, different search budgets

Keep the authoritative Pmove forecast and shared movement step. Separate
proposal generation, validation, result storage, selection and execution with
explicit inputs/results rather than another set of policy flags sharing
mutable scratch. A successor expansion must receive the actual simulated
terminal state and required continuation context, including timing, jump
latch, velocity, animation and resource state. Never replace that state with
a graph edge's nominal exit to make a chain connect.

The graph guides connectivity and estimated remaining travel. Short-horizon
progress and an exit-speed bonus alone are not a complete trip objective:
continuation feasibility matters, and speed pointing into a dead end is not
valuable. Preserve useful distinct successor states where merging them would
erase a feasible continuation. Specific action sets, search widths and
heuristics must be selected by the diagnostic below, not declared qualified
by this document.

Offline search can explore broadly and retain complete validated plans.
Online search uses a smaller explicit CPU budget from the live state. They
share maneuver semantics and validation contracts; they need not use an
identical search algorithm or serialize every plan as a raw human tape.
Production scheduling must support multiple bots, leave a usable active command
when work is deferred, and invalidate results when their root/goal/world
binding no longer applies. Searching successors during flight is a proposal
to test, not existing behavior or permission to trust stale predictions.

The short-rollout throughput measurements justify an experiment. They do not
establish unlimited offline compute, full-route throughput or live frame cost.
The forecast has a finite horizon and a restricted static-world model; carry
terminal context across bounded rollouts and qualify that continuation before
calling it a complete-trip simulator. Dynamic contacts and pickups require
native checks outside what the forecast actually models.

## Entry, departure and recovery

Begin with demonstrated standing entries. A potential intermediate entry must
be reachable in the required state, not merely close to the route's line.
Validate both the connecting maneuver and the ensuing route segment through
the relevant landing/turn. Measure tolerance through perturbations; do not
invent a universal distance or speed band for narrow ledges.

Selection includes entry cost plus remaining travel. Abandon an uneconomic or
unreachable entry and keep making independent progress. Never teleport, clear
velocity or replenish resources to satisfy an entry outside declared fixture
setup. A native rollout is conditional validation, not a guarantee against
future opponent motion. On a miss, retain physical state and recover through
the same movement owner. No indefinite onward stage or repeated join attempts
may block progress.

The existing walking-entry failures do not prove that all moving joins are
impossible. Conversely, a suffix command index alone does not solve physical
entry. Intermediate joins, follower correction and contention are later
qualification work; none blocks improving independent movement now.

## Ordered acceptance

1. Bind a repaired control with explicit start, goal, resources, command cadence,
   `pmove_fixed`/`pmove_msec`/`pmove_float`, map and seed. Reuse qualified
   evidence when its binding matches; rebuild only when sources/settings need
   it. Audit whether the search actually ran, not just whether the case passed.
2. Run a guided **expressiveness diagnostic**: give the search a human route's
   coarse corridor but have it generate its own legal movement from the
   matched start. No copied command tape, resets between maneuvers or injected
   velocity. Compare connected native execution with the human reference.
   Failure directs work at the movement vocabulary/continuation evaluation;
   success directs work at unguided discovery. Neither outcome alone qualifies
   independent movement.
3. Run independent connected search with playback and human-corridor guidance
   disabled. Generic geometry and automatically learned maneuver proposals
   may remain, with provenance reported. Measure complete stationary no-Force
   stand runs toward the approximately 15-second target, repeated completion,
   median/tail times, deaths, stalls, spend, parity and ownership. Use displaced
   starts and interrupted trajectories to check that competence transfers.
4. Establish runtime cost and lifecycle separately: decouple search enablement
   from parity diagnostics without removing native validation, enforce a
   measured multi-bot CPU budget, and verify that ordinary intended gameplay
   invokes the search. Qualification needs both movement quality and runtime
   behavior; an offline discovery result proves neither by itself.
5. Add prepared-plan selection under the shared lifecycle, initially at known
   starts. Test entry cost, no applicable plan, objective/resource changes and
   departure back to independent movement. Add intermediate entries only when
   their connection and continuation pass measured perturbation cases.

Keep the 36-case floor, seeded level-1 arm and named failures as regression
gates. Count setup failures separately and rerun missing cases before calling
an arm complete. Do not hide deaths/timeouts inside a fast median. A best run
under 15 seconds is reported as a best run, not reliable target attainment;
the earlier strict sub-15 challenge is distinct from the owner's approximate
independent-movement goal. Match physics before comparing against tapes.

New demonstrations are optional additional evidence. More route recordings,
follower tuning, broader entry tolerances and another admission/scoring cvar
are not the default next step. The chain-entry and onward-held defects remain
named regressions. Past reroute/tap causal claims require repaired controls.
