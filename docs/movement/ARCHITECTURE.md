# Movement architecture

One runtime movement owner, with independent movement as the base and prepared
plans as an optional advantage. Read [PHYSICS.md](PHYSICS.md) first.

Revised 2026-09-19 after the route-playback work and architecture review. This
document describes the target unless a paragraph explicitly says **Current**.
It does not claim that the target is implemented or qualified. See
[HANDOFF.md](HANDOFF.md) for current repair and qualification status.

```
preparation     BSP + hints + native trials -> graph proposals and optional prepared plans
runtime search  graph + exact live state + resources -> connected movement sequence
plan selection  independent sequence or applicable prepared plan -> one active plan
every frame     active plan + live state -> one command owner
contract miss   preserve feasible flight/landing, then independent search or safe walk
```

Three different objects must not be called a route interchangeably:

- A **graph path** suggests connectivity and estimated travel time. Its approximate
  state transitions do not prove that the bot can execute the whole path.
- An **executable maneuver sequence** contains policies or commands validated
  from a particular native state, including the actual successor states.
- A **prepared plan** stores a longer sequence for reuse, from a human recording
  or offline search. A command tape is one representation; entry conditions,
  timing, physics binding, resource use and expected states are part of its contract.

Independent movement must find useful connected maneuvers without playback or
a successful join. Roughly 15 seconds on the matched stand-to-flag benchmark is
the owner's performance goal, **not an achieved result**. Prepared-plan speed,
independent speed and runtime compute cost are separate acceptance criteria.
Human demonstrations can seed proposals and diagnose the action vocabulary;
independent search work is not blocked on obtaining more demonstrations.

**Current diagnostic input:** a *guided corridor* may be loaded with
`bot_moveCorridor` (`tools/movement_corridor.py` writes one from a human
recording's contract marks). It is a polyline of positions and a radius;
while it is loaded both graph searches expand only cells within that radius,
and nothing else about the bot's movement comes from the recording. It is a
diagnostic lens for the expressiveness question
([the guided corridor](records/2026-09-19-guided-corridor.md)), never a
runtime default: unset, every search is unguided and unchanged.

**Current:** `Graph_PlanState` proposes paths using approximate momentum state;
`StraightSearch` validates short hop sequences from live state. `Explore`
samples synthetic corridor states and harvests individual edges, rather than
growing complete native trajectories. Discovery is currently dispatched through
`Bot2_MoveParityBeforePmove` and its dedicated/cheats/movement-only parity gate.
Production search scheduling must become independent of parity diagnostics.
Recorded playback and its prefix are experimental paths, not yet the unified
runtime plan interface described here.

## 1. Preparation: the maneuver graph

Built once per map from the game's own collision geometry, cached to disk
keyed by map checksum and physics binding (gravity, base speed, pmove step,
Force levels). Never hand-edited. The graph supplies reusable proposals and
connectivity alongside optional prepared plans; it does not replace every
recorded or automatically prepared sequence.

### Where the geometry comes from

The root of the graph is the BSP, queried through `trap->Trace`, because that
is the geometry Pmove collides with. The navmesh is a hint layer, optional,
and never the authority. The reason is in LESSONS: every snapshot consumer
re-traced every navmesh answer, the mesh comes from an external converter
whose cell size, agent radius and climb settings are not recorded here, and a
jump edge needs floor and edge positions accurate to a few units, which a
voxelized, radius-eroded mesh cannot give.

**Node discovery** is a layered flood fill over walkable floor, seeded from
spawn points and flag stands. Cells are keyed by horizontal position and
floor height, so stacked floors in the same column are distinct cells. Each
cell is admitted by the floor, step, ceiling and hull-sweep traces the
snapshot's supported adapter already performs, plus the hurt and liquid
checks. The base grid is coarse (32 units to start) and refined near fill
boundaries and where a coarse cell is refused but its neighbors are not, so
narrow passages are not lost to the grid. Straight runs of admitted cells
become **nodes**: supported standing intervals with two endpoints, floor
height, width margin and hazard flags.

Discovery iterates. Jump rollouts from fill boundaries can land on floor no
seed reached; every rollout landing on unadmitted floor becomes a new seed
and the fill continues from it. A platform reachable only by jumping is
found this way or not at all, and the preparation report lists floor that
was seen by a landing but never reached, so coverage is a measured number.

Jump candidates are not only at boundaries. Momentum hops on continuous
floor are the main source of speed on a straight, so every node long enough
gets interior takeoff candidates along its length, rolled out as hop and
strafe-jump edges that land on the same node further along.

**Navmesh hints**, when a mesh is present for the map:

- a search heuristic and region connectivity, used to prune which boundary
  pairs are worth rolling out and to order the search;
- off-mesh connection pairs for wallrun, drop and basic jump, used to seed
  candidate edges between nodes;
- a coarse reachability answer for staging decisions such as a dropped flag.

A hint only proposes. Maneuver proposals require native validation under the
supported world contract before commitment. Missing or bad hints may reduce
coverage; completeness without a mesh is a target to measure, not an assumption.

**Entity scans** supply the remaining special edges without the mesh:
`trigger_push` for jumppads (already scanned by the pad state), `func_plat`
and vertical `func_door` for lifts (already scanned by the elevator state).
Wallrun candidates come from the near-vertical face sweep that
`Bot2_ScanWallruns` already performs as a diagnostic, promoted to a
preparation step in a later phase.

**Edges** are maneuver templates from one node to another, each validated by
a native rollout of the actual Pmove from a synthetic starting state:

| Template | Inputs | Validation |
|---|---|---|
| walk | node to node along shared floor | corridor traces only |
| corner | arc or pass-through between two intervals | existing corner compiler samples |
| hop | tap at entry speed s, wishspeed w | rollout lands on target node |
| strafe jump | tap, strafe direction, angle fraction f, s, w | rollout, must land on target node within margin |
| held jump | as strafe jump plus hold height class h | rollout, drains Force |
| speed on | 50 Force, opens a 20 s window | none, resource edge |

Each rolled-out edge records an **entry envelope** and an **exit
envelope**: the state dimensions that decide whether the maneuver can begin
and where it leaves the bot. Which dimensions those are is an output of the
phase 1 spike, not a list written in advance; a speed band alone is known to
be too little, and the reviewer's full list (position along and across,
heading, speed range, latch, Speed window) is the candidate set the spike
tests. Each edge also records wishspeed, hold class, Force cost and time. Edges are rolled out per
wishspeed (250 and 425) and across a small set of entry states, because the
same geometry lands differently at each. Failed rollouts are recorded too,
so a later query can say why a gap is not crossable.

A stored edge is a candidate policy, not a proof for every state inside its
envelope. The archive measured this: a sampled selector reached useful exits
in 7 of 16 windows where a full search reached 16 of 16, and once accepted a
clearance violation. So before the executor commits to a jump edge it rolls
that edge out from the actual live state with the forecast engine, through
the landing and far enough past it to reach the next edge's entry envelope.
Envelope membership alone is insufficient: extend the next maneuver from the
actual native terminal state and validate through its critical landing or turn.
Do not replace that state with the stored edge's nominal exit. The continuation
includes position, velocity, command timing, jump latch, resources and policy
state. Validation cost depends on the proposals and continuation required; it
must be measured and budgeted rather than assumed to be one cheap rollout.

These checks establish feasibility in the forecast's supported snapshot and
physics contract. They do not guarantee that a future opponent, moving world
or unsupported interaction will preserve the trajectory. Live monitoring and
recovery remain required.

Chaining is therefore decided at commit time from the real state, and the
graph's job is to say which maneuvers are worth trying and in what order.

**Unknown connections stay unknown.** A gap with no validated edge is not a
walk edge; it is absent, and the planner routes around it or reports no
plan. Walk edges exist only where corridor traces admit walking.

Validated chains may be retained as prepared plans. A graph path remains a
proposal until its connected maneuvers have been validated from the applicable
state; neither storing a chain nor assembling one grants universal entry.

The rollout engine is the phantom Pmove plus the forecast guard from the
snapshot, driven by a policy that uses the same `MoveControl` kernel the live
executor uses. That is the property that keeps preparation honest: the
offline rollout and the online execution issue commands from the same
function.

Offline and online search share proposal, native validation, successor-state
and execution contracts. They differ in search breadth, horizon and budget,
not in what counts as executable. Preparation may run incrementally or ahead
of time; runtime work needs an explicit per-bot and shared server budget with
bounded scheduling. Neither has unlimited compute. Report cost, incomplete
coverage and exhausted budgets. **Current:** Explore batches synthetic states
per frame and rollouts have a step cap; neither is a production CPU deadline.

## 2. Planner

Search is requested when no usable plan remains, the objective changes, a
contract fails, or resources invalidate the continuation. Commit only at a
boundary compatible with the active maneuver. Bounded preparation of successors
during flight is allowed; it must not take command ownership from that flight.

The graph search uses approximate state to propose a corridor and maneuvers.
The connected native search carries each branch's actual successor state,
plus Force, remaining Speed window and the spend policy. Cost is time, with
completion and reliability measured separately. An envelope match proposes an
edge; native continuation and affordable resources establish its admission.
Regen is applied along the way only while no power is active, which the
physics document explains.

The graph supplies guidance toward the objective while runtime native search
has a bounded connected horizon. Offline search may extend farther. Judge the
sequence's elapsed time and useful continuation, not just a hop's distance
advantage over walking. The remaining trip's cost and resource needs are
estimates unless explicitly simulated; expose that distinction. Commit a
validated prefix and preserve its physical continuation while replanning the
tail. A short horizon must not silently treat future resource feasibility as
proved.

**Current:** the state search is `Graph_PlanState`, A* over
the cell and a state key of banded speed, a heading octant kept only on a
landing frame, the landing-frame bit and whether the Speed window is open.
Walk links are priced by the walk's own law from the state's speed
(friction and the run's acceleration per command frame over the link), so
a fast landing is worth its next few cells; a strafe edge is admitted only
from a state inside its entry envelope (a landing-frame edge needs a
landing state within the speed band and heading cone of the launch it was
learned from, within the path reach of its takeoff; ordinary strafe edges also
check launch speed and heading) and leaves its stored measured exit state; every other edge
leaves the walk's state. Strafe edges are learned from completed straight
plans with the state their committed rollout measured, and cross worlds
as a merged seed (`tools/movement_graph_merge.py`, `movement_bench.py
--graph`). When the bot stands at the takeoff of the path's next strafe
edge, the planned chain's next hop joins the straight search as
candidates aimed at its learned landing, chained with the next
landing-frame edge. The base judge compares progress against walking, with
optional route ranking, exit-speed credit and tap continuation. The default
planned-edge preference can override the local winner with an admitted planned
candidate. An infeasible edge may trigger immediate replanning or avoidance,
depending on whether its live entry state was stale or its native validation
failed. These policies are current experimental mechanisms, not the final
connected-search objective. A strafe chord is a flight, not a walk: the
walk's lookahead ends at a chord's takeoff cell, only progress that
reached the takeoff is projected along the chord, and a bot past the
takeoff with no sequence in progress refuses the edge. The cell search
remains the fallback when the state table overflows.

The target resource model treats the levels consistently. With Speed off and
Force zero, only walk, corner, hop and tap edges are admissible. Buying Speed
is an edge the search may take when the time it saves over the remaining
plan exceeds its cost in later held jumps. Held jumps are edges with a Force
price. Running out of Force is just an admission failure that leaves the tap
edge as the cheapest continuation. Nothing hands off.

Objective handling stays in the macro layer: the planner receives a goal
point and returns an executable prefix, or a declared fallback/refusal.

### Prepared-plan selection and entry

The same runtime owner may select a prepared plan when it saves expected
remaining trip time, including approach, entry and recovery risk. Its absence
or rejection leaves independent movement available. Search-generated plans
and human-derived tapes use the same entry and continuation validation contract,
even when their command-generation representation differs.

Initially use only entries with demonstrated support. A standing start is one
useful entry, not a universal restriction on every route. An intermediate
entry is optional and state-specific: validate a connection from the actual
live state and then enough of the prepared continuation to survive the next
critical landing or turn. Nearness to the route line, a speed band or a
matching yaw is insufficient. Refuse a join that is infeasible, unaffordable
to validate, or slower than independent travel; do not repeatedly chase it.
The narrow entry tolerances observed in the recorded-route trials justify
conservative entries, not a general promise of self-correction.

## 3. Executor

One command owner per client holds the active plan, its cursor and validation
context. Policies generate commands through the shared movement kernel; a tape
preserves its validated command timing. The target executor:

1. Issues the active maneuver's command from its live state and continuation.
2. Preserves jump timing and confirms the game's actual launch/landing.
3. Monitors deviation and whether the upcoming maneuver remains feasible.
4. Uses native validation at a compatible commitment boundary before changing
   the active sequence, including any prepared-plan entry.
5. Reports a contract miss with its cause and selects a physically feasible
   continuation under the same owner.

Position-error steering toward a reference is not an assumed repair. Any
correction must preserve the downstream maneuver's feasibility. **Current:**
`HopStep` uses a run-up and fixed hop heading through `MoveControl`, and
`MoveStep` returns to walking afterward; tape playback is a separate prototype.

A miss requests independent replanning. Preserve a still-feasible active
flight while preparing successors. If it cannot continue, choose a supported
reachable landing/recovery policy; do not drop ownership, zero the commands
or demand an impossible stop in midair. The target is to validate that recovery
under the same native contract, then resume independent search at a compatible
boundary. This recovery behavior requires qualification; it is not established
by successful empty-world playback.

There is no per-edge resource ladder at launch time. The plan admitted the
launch with the resources it projected; the live rollout confirms the
geometry from the real state; the contract confirms the state.

The executor never issues Speed or Force on its own. Those are edges.
The plan takes the Speed edge on a grounded frame the walk owns, when the
spend policy permits it, the pool affords it and the path left is worth
the window; the game activates the power after that frame's Pmove, so
the next frame confirms it by the active bit, and the forecast applies
the same activation to a command that carries it. A plan decision the
step function does not make is a declared discontinuity for the parity
harness, as a replan is.

The walk checks two chords on every grounded frame: the command chord to
its target, whose refusal declines the frame, and the momentum chord
along its velocity for the frames a ground turn takes, whose refusal for
a drop, a hazard or a hurt volume brakes the frame. A solid on the
momentum chord refuses nothing; a wall stops the bot itself.

A walk permit is a rollout of the walk from the state it was granted in.
Any plan decision after the grant, a committed sequence or a Speed
purchase, revokes it: the corridor's refusal returns and the gap search
rolls the links out again from the real state.

## 4. Recovery

Independent connected search is the normal continuation when a prepared plan
is unusable. It may choose validated jumps as well as walking; it must retain
its own speed and reliability target. Safe corridor-checked walking is the
last fallback when no useful sequence can be validated within budget, not the
performance target for every interrupted route. The navmesh next-waypoint query
may supply guidance where the graph lacks coverage. If walking makes no
progress for a bounded time, use the existing corner recovery scan and report
the failure. Preserve feasible flight before these ground recovery choices.

Historically, body contact and defended play were excluded from the first
milestones by the 2026-09-14 decision. Empty-world evidence remains scoped to
that setting. Contention and disruption need separate qualification before
claiming ordinary gameplay robustness; a snapshot body trace cannot guarantee
that a blocker will not move into the route later.

## 5. State

Target ownership, one movement state per client:

```
movement_t {
    plan;           // active sequence/tape, cursor, entry and continuation contract
    search;         // independent frontier, exact successors, budget and pending result
    executor;       // projection index, jump latch, launch frame
    recovery;       // walk target, progress clock, scan state
    budget;         // spend policy, the Speed purchase awaiting confirmation, purchases
    lastEvent;      // one enum, one reason, one time
}
```

One lifecycle reset contract covers death, disconnect, map change and objective
change. Separate per-client execution state from explicit search scratch and
immutable map/plan assets. **Current:** implementation still uses static search
and parity scratch; this diagram does not claim that separation is complete.

## 6. Telemetry

Target event schema (current diagnostic families also remain):

`[MOVE] client level event=<plan|edge|miss|walk|recover> edge=<id> reason=<name> speed force speed_ms x y z`

Every event names the edge it belongs to. Campaign tools pair by edge id.
Include plan origin, entry/refusal reason, search budget, validation context
and fallback outcome where relevant. Keep diagnostic parity separate from
production policy decisions.

## What is kept from the snapshot

- `bot2_movecontrol.c`: the one-frame model and key search. Unchanged.
- `bot2_forecast.c`: the rollout engine, reused by preparation and by
  commit-time validation (the phantom Pmove of `ai_bot2_pmove.c` it once
  shared was removed with the classic Bot2 on 2026-10-08). Under
  `modelForce` (phase 1b) it applies the game frame's Force update between
  steps, so Speed expiry, levitation's end and regeneration are rolled out
  and a level-2 flight is validated; forecast-versus-live agreement with
  Speed active and expiring is measured in PLAN. Other active powers,
  triggers, items and moving worlds still refuse.
- The floor, step, ceiling, sweep and corridor traces from
  `ai_bot2_supported.c` and `ai_bot2_localnav.c`, moved into preparation and
  recovery. The corridor's one sample (`Bot2_GroundSample`: floor, step,
  sweep, hurt) is the cell admission of node discovery in `bot2_graph.c`,
  so the graph admits exactly what the walk admits.
- The corner compiler's arc and pass proposals, as the corner edge template.
- The jump-hold and landing arithmetic from `bot2_strafe.c`, as the
  executor's launch rule.
- The navmesh query surface in `g_navmesh.h`, as an optional hint source.
- `Bot2_ScanWallruns` and the pad/lift entity scans, as edge candidate generators.
- The movement benchmark fixture (`bot2_movebench.c`) as the test harness.

## Legacy mechanisms to retire

These are design migration targets, not a deletion instruction or a statement
that the files have been removed. Follow the archive-only filesystem policy.

- The legacy route follower and fast return in `ai_bot2_route.c`, including
  obsolete `STRAFEROUTE1` machinery after useful assets are preserved or migrated.
  Prepared-plan storage and validated recorded tapes remain supported concepts.
- Branch sidecars and their authoring tools.
- The ownership chain in `Bot2_ExecuteMovementImpl` and the seven owners'
  release, retry and handoff logic.
- The supported adapter's publication, prefix and handoff machinery.
- The legacy WALK jump gatekeeper, the four-deep chain scorer and the
  AIRBORNE fixed-angle strafe.
- The flight artifact adapter and hop fallback as separate adapters; their
  rollout policy becomes an edge template.

The jumppad state is kept as the pad owner. The elevator, drop, basic-jump
and wallrun states, which took frames the rewrite declined, were removed with
the classic Bot2 on 2026-10-08 (owner): drops, jumps, pads, climbs and lifts
are native graph edges, and a declined frame stands still.

## Interfaces

Conceptual target interfaces, not a claim that these exact functions exist:

```
qboolean Move_PrepareMap(void);                      // build or load the graph
int      Move_Plan(int client, const vec3_t goal);   // bounded independent search + optional plan selection
qboolean Move_Command(int client, usercmd_t *cmd);   // executor or recovery
void     Move_Reset(int client);
```

The target integration gives this owner a single command path, with explicit
delegation to retained off-mesh states when required. Search and prepared-plan
selection must not compete as independent command writers. Parity diagnostics
observe and test the production command path; enabling diagnostics must not be
what enables the search itself.
