# General movement planning: return to the research objective

Historical design and experiment context. The current implementation order is
in the [implementation guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md), with
status in the [scorecard](STRAFE_MOVEMENT_PROGRESS.md) and acceptance in the
[active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md). "Next" below refers to this
dated design checkpoint, not an instruction to restart completed preparation.

Owner direction, 2026-09-06: prioritize an elegant, general solution to strafe
jumping through 3D space, including useful recovery and dynamic replanning.
This changes the immediate work order inside M3a; it does not close a gate,
replace the frozen benchmark, or qualify the current installed bot.

## What is already established

The [original handoff](STRAFE_MOVEMENT_RESEARCH_HANDOFF_2026-09-04.md) already
proposed feedback maneuvers with compatible entry/exit movement states and
bounded live rollout. Calling this a newly discovered direction was inaccurate.
The later product work established valuable native control, route tracking,
measurement and integration, but did not finish that general planning program.

The [two-maneuver experiment](STRAFE_MOVEMENT_COMPOSITION_CHECK_2026-09-04.md)
already distinguishes local landing scoring from successful continuation:
the latter completes the nominal and +16 lateral cases where the former fails.
It uses eight prescribed candidates on one development layout. Nominal hull
clearance is only 0.386 units; three starts do not establish a robust entry
region. Its 44–77 ms search includes instrumentation, has no deadline, and
cannot be treated as either an affordable planner or a clean production cost
measurement. Repeating this demonstration is not the next research objective.

UnrealJKA already explored full-state command search, route tracking, atlas
values and joins. The [precedent review](STRAFE_MOVEMENT_RECOVERY_BUDGET_2026-09-04.md)
records both useful routes and rejected disturbed-line continuation. Read
the correction in `<UnrealJKA>/JKA/Docs/LINES_FIRST_PHASE3_CONTINUATION_ANALYSIS.md`:
grounded/stable is not necessarily a useful or surviving state. Proximity to a
successful line does not validate the swept path needed to join it. These are
historical Unreal findings, not a new Unreal benchmark or proof that all
trajectory-library designs fail.

A read-only source follow-up finds `BuildTasRace`/`FGlobalRaceNode` and
`MakeTasKey` in Unreal's `JKABotMove.cpp`: nodes carry player state, while
deduplication uses a quantized subset. `JKABot.cpp` contains same-tape
`bLineTrackContinuation` and `IsSafeToJumpOnLine` gating. The inspected line
telemetry header distinguishes nearest-checkpoint measurement from actually
performing a join. The review did not establish an implemented arbitrary-state
connector library with validated compatible entry/exit regions. Distinguish
those missing contracts from the substantial search/tracking code already
present; old Phase-3 design text alone cannot establish implementation.

The missing demonstration is **automatic construction and affordable reuse of
compatible movement trajectories across geometry and disturbed states**.
Current waypoint/route/recovery fixes do not establish that demonstration.

## Working model and boundaries

Use the current navmesh for topology provisionally. Extract usable corridors
and transitions, then validate body clearance, support, ceilings and hazards
against native geometry. Measure discrepancies before deciding to rebake or
replace the mesh. A path corner is guidance, not an obligatory stop or landing.
The same physical corridor split into extra collinear waypoints should not
change the intended movement problem.

Plan through movement states, including velocity and contact phase, rather
than position alone. Initially retain complete supported simulator and
controller checkpoints: view/jump history, timers, force and Speed state where
modeled, collision setup and relevant world state. An indexing approximation
must not silently become the state used for native validation.

A candidate is an executable feedback policy over a short trajectory, with
its entry conditions, swept occupancy, exit state, duration and resource cost.
Turns and landings are chosen for subsequent movement: both a fast outside
approach and a slower inside approach may deserve retention. Ordinary hops,
ground travel, braking and repositioning share this contract. Add Force Jump
only after its server resource/world updates have a verified prediction
contract; the ordinary-movement parity tests do not cover them automatically.

Execute a short prefix, compare actual state with prediction, and validate or
repair the continuation. A disturbance starts planning from the actual state
using the same candidate semantics. Recovery may widen the search and prefer
survival, but must validate an onward route beyond a merely safe landing.
Emergency braking and waiting remain legitimate bounded outcomes; report
search exhaustion separately from proven infeasibility.

Cached lines, native replay-valid demo seeds and sampled maneuvers are proposal
sources. Their value is amortized discovery and retained alternatives. A cached
future cannot excuse an invalid present transition. Avoid requiring a giant
global atlas before the first useful result, or claiming formal funnels from
a few successful samples. Solver choice remains an experiment, not a commitment
to importing the Unreal stack or to a particular robotics algorithm.

## Next executable research slice

### Known-map preparation is the default architecture

Owner clarification, 2026-09-06: maps are known in advance and substantial
precomputation is acceptable when it reduces live cost. Move expensive static
trajectory discovery and optimization into automatic map preparation. The
69.1 ms guarded search measurement is a reason to change when work runs, not
an obligation to squeeze that whole search into every live decision.

Prepare a reusable map movement graph. Extract support, clearance, corridors
and transitions from map geometry; use native-validated feedback maneuvers as
edges, indexed by entry position, velocity, heading and contact phase. Retain
compatible exit states and several useful alternatives for speed, clearance
and onward travel. Expensive search can discover and improve racing lines,
ordinary-hop sequences and recovery connections before play. Prepare objective
cost-to-go data over compatible transitions where useful. Map coordinates are
generated data, with the same extraction and controller code on every map.

Keep this distinct from the demonstrated exact same-episode cache. A persistent
library must survive fresh processes, serialize no pointers, and bind its
geometry, physics/controller version and declared resource regime. Full native
checkpoints remain validation evidence; compact quantized keys only retrieve
proposals. Sampled entry neighborhoods must be labeled empirical coverage,
not certified funnels or permission to snap actual state onto a stored line.

Live work becomes state-aware retrieval, selection of compatible continuations,
local validation against current obstacles/resources, feedback tracking and a
short bounded connection or repair from the actual state. Precompute recovery
options with the same entry/exit contract. A nearby trajectory is not a valid
join: the complete connector and its useful onward movement still need checking.
Moving players, doors, impulses and changing resources cannot be assumed away
because the static map was baked. Library misses, deadline exhaustion and
legitimate braking/waiting must stay visible in telemetry.

The immediate affordability experiment is therefore **bake, cold-load, connect,
and execute**, with the expensive online search retained as a reference:

1. Build and serialize geometry-derived maneuver alternatives automatically
   on the existing parameterized fixtures. Record preparation time, storage,
   source/settings hashes and the sampled entry/exit coverage.
2. In fresh native processes, retrieve them for held-out entry states and
   disturbances. Compare against the same expensive search and retained-policy
   arms, including failed/missing joins. No successful evaluation trajectory may
   be added to the frozen bake after observing the held-out result.
3. Measure lookup, validation, connector/repair, tracking and amortized updates
   together under a declared live-work bound. Retain useful-exit, walking/idle,
   facing and confinement metrics. Profile remaining live work; do not use cheap
   lookup alone to claim affordable movement.
4. Move automatic game-map extraction and an early second-map bake forward,
   before extensive tuning of a full online search. Expand speed and entry-state
   coverage with offline search, keeping the compiler/controller fixed across
   the transfer probe. Full M4 and the production gates remain unchanged.

Precomputation has no tight owner-imposed time budget, but measure its cost and
storage rather than enumerate every possible game state. Grow a sparse library
of useful policies and connections according to measured coverage gaps. The
existing guarded fixture remains the validator for offline and live components.

The first corner-cell experiment now supplies a concrete stop rule. Although
the shadow/replay set passed 16/16, the new native set has eight refusals and
one accepted clearance violation; full search still passes all sixteen. The
rejected selector is retained as an opt-in fixture. Do not replace continuation
validation with a terminal box solely because all its sampled corners passed.
The fast proposal in the failed cell had only 0.124 units of worst sampled
clearance slack; another candidate had 9.918. Preparation must retain and use
such margins, sample interior/adverse states automatically, and refine regions
with contradictory outcomes before another independent acceptance test.
Native prefix timing also remains dominated by work outside measured Pmove;
separate required validation/controller work from diagnostic overhead rather
than trading away the useful-exit contract to improve a timing number.
See the [full result](STRAFE_MOVEMENT_COVERAGE_NATIVE_2026-09-06.md).

The midpoint/clearance follow-up now supplies a second discrimination: all
32 fresh states and the prior clearance-violation state have useful selected
continuations, but eight known terminal-envelope refusals survive 125-root
preparation. Target adverse execution branches and quantized outcomes rather
than simply adding another regular tensor grid. Preserve the clearance ranking
and failed examples; no per-field empirical range enlargement establishes a
continuous acceptance region.

The measured lookup/compatibility check is now 0.9 microseconds median.
Whole-world diagnostic hash guards account for 78.1–82.4% of instrumented prefix
forecast time; command generation and native physics are separately measured.
Use this attribution to isolate the runtime work while retaining the guarded
implementation as a parity oracle. Do not declare affordability by subtracting
diagnostic bucket medians; measure entry, tracking, obstacle validation and
repair together. [Current experiment](STRAFE_MOVEMENT_REGION_PREPARATION_2026-09-06.md).

The early two-map preparation step now has hash-bound support inventories for
Kejim and Dantooine v3 from the same parser. Native hull/clearance/hazard checks
must next turn those polygons/boundaries into validated transition candidates.
This is preparation transfer, not movement transfer or an M4 pass.
[Inventory and limits](STRAFE_MOVEMENT_MAP_PREPARATION_2026-09-06.md).

Extend the existing composition fixture and native forecast machinery rather
than creating another independent movement simulator. Keep the current ground
controller and the terminal-hop prototype as measured engineering baselines.

1. Parameterize the existing static research geometry and explicit exit
   contracts. Include a continuous floor with alternate waypoint subdivisions,
   the gap/platform/turn layout, and a ramp-to-flat transition. Inventory the
   fixture's actual state/world support and preserve predictor negative controls.
   Define a usable exit by support, heading/velocity and a tested onward segment;
   do not require zero speed at every intermediate boundary.
2. Generate candidate approaches from corridor geometry and actual velocity,
   using the existing command controller where its contract applies. Compare
   the current controller, local scoring and continuation scoring with the same
   proposals and rollout allowance. Keep extra-budget offline search separate:
   it diagnoses whether a useful option was missing, rejected or misranked.
3. Reuse validated work across decisions and persistent map preparations,
   invalidate it against actual state and geometry, and measure the entire
   live planning cost without diagnostic I/O. Follow the bake/load/connection
   experiment above before optimizing a full online candidate search.
   The deployment target remains eight bots, total movement p95 <=2.5 ms and
   maximum <=5 ms per native server frame. Fix the per-decision rollout and
   amortization limits in the protocol before candidate evaluation; the old
   1,280-call instrumented experiment is not a production budget.
4. Lock controller parameters and test unseen combinations of corridor width,
   turn geometry and entry position/velocity/contact phase. Begin with the
   supported static subset; then add declared impulses and body obstruction,
   explicitly validating the required world-model extension. Use the same
   planner after disturbance, with the same total compute accounting.
5. Run an early second-map geometry probe with automatic preprocessing and no
   map-specific movement code or manually supplied successful line. This probes
   transfer before more Kejim polishing; full M4 still requires its original
   map-wide qualification. Previously inspected ctf4/Kothis examples are
   development material, not untouched test-map evidence.

Before native collection, freeze a machine-readable case list, map/build/settings
hashes, geometry split, valid start checks, all controller arms, budgets and
numeric outcome comparisons. This document defines the research sequence; it
is not a frozen evaluation protocol and contains no new pass claim.

## Discriminating results and stop rules

- Extra collinear subdivisions must not induce artificial stops or walking
  stretches. Keep native traces to attribute any difference to representation,
  physics or policy rather than assuming navmesh error.
- Compare complete traversal and usable onward movement, not first contact or
  peak speed. Retain failures/timeouts, minimum observed clearance, continuous
  walking/idle and confinement, recovery latency, force spending, facing and
  compute. Preserve the existing live metric definitions and acceptance gates.
- Evaluate the locked implementation on held-out geometry/initial states.
  A successful Kejim-specific fix cannot satisfy this research gate. Report
  preprocessing time/storage and human route-authoring separately.
- If all sampled candidates fail, use bounded offline diagnostic search and
  native replay to distinguish missing actions, horizon limits and physical
  constraints. A finite search failure is not an impossibility certificate.
- If replay diverges, resolve the prediction contract first. If the simpler
  baseline performs equally well under the frozen comparison, keep it. If only
  extra offline compute succeeds, the missing result is affordable reuse or
  candidate selection, not further proof that lookahead helps.

Only after this slice supplies transferable value should it replace production
movement ownership. Then retain the pit/body/resource checks, complete the
frozen defended comparison and qualify the integrated build under M0–M3a.
Current installed and experimental artifacts remain intact throughout.
