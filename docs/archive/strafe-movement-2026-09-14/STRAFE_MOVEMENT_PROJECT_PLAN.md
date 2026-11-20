# Strafe movement project plan

Updated 2026-09-14. This document owns the end result, architecture and acceptance
gates. Use the [handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) for next actions
and [scorecard](STRAFE_MOVEMENT_PROGRESS.md) for results. The
[branch plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md) owns package contracts.
K07 remains open. No acceptance threshold is changed by this cleanup.

Owner priority correction, September 14: establish dependable defender-free
movement first. Dynamic body-contact and defended scenarios are deferred to v2;
retain their evidence and safety checks, but do not make solving those scenarios
the next baseline milestone. Solo resource, phase and complete-trip qualification
remain required. This sequencing does not claim the broader gates have passed.

Old delivery invitations and work orders are in the
[plan history](STRAFE_MOVEMENT_PROJECT_PLAN_HISTORY_2026-09-12.md); they are not
the execution queue. Detailed K-package protocols remain in the
[historical guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE_HISTORY_2026-09-12.md).

Latest bounded supplement: [single-hop fallback and predictor repair](STRAFE_MOVEMENT_HOP_FALLBACK_2026-09-14.md).
Routes remain primary. Real evaluator defects are repaired independently; a
fallback may only add a demonstrated safe maneuver, must return control to the
admitted route, and must report refusals and divergence. The supplement is
implemented but default off pending demonstrated solo usefulness and broader
phase qualification; defended qualification is deferred to v2. It does not
close connected-hop or ordinary movement acceptance.

Previous implementation checkpoint: [sequence Speed scheduling](STRAFE_MOVEMENT_SEQUENCE_PLANNING_2026-09-14.md).
Direct command telemetry now separates missing hop permission, insufficient
room and actual speed constraints. The supported owner estimates useful ground
travel across several admitted intervals before spending native Speed, while
retaining a 50-Force reserve and all geometry/execution gates. Whole-trip and
walking outcomes improve in the measured ordinary-resource cases; idle and
low-resource movement remain mixed. Resource-independent connected hop/landing
planning is still missing; no full movement acceptance or installation is claimed.

Foundational implementation checkpoint: [native off-line approach](STRAFE_MOVEMENT_HANDOFF_APPROACH_2026-09-14.md),
extending the [supported handoff contract](STRAFE_MOVEMENT_HANDOFF_CONTRACT_2026-09-14.md).
Preparation owns candidate geometry; execution owns an admitted snapshot and
progress. A replacement must admit the actual position and velocity through
native checks. Temporary frontiers yield before terminal braking, and optional
route entries cannot own movement while waiting for resources. These rules apply
to the controller, independent of map coordinates. A frozen ordinary-route cohort
checks the implementation; broader movement, defended and cost qualification
remain separate. The acceptance contract below is unchanged.

## End result

Deliver a dependable, intermediate-human offense bot. The immediate product
is **movement-only Runner_v2 on Kejim**: launch normal MP, Create Game, add the
runner, defend against it, optionally add Bot2 CHASE/BASE enemies, and inspect
live or completed CSV/demo telemetry. It should move quickly through both flag
legs, face roughly along travel, favor useful ordinary strafe hops, and recover
from interruptions without sustained unexplained confinement.

The general solution must transfer to other known maps through automatic map
preparation, without map-specific C, hand-authored rescue coordinates or a
growing list of corner exceptions. After dependable movement and map transfer,
complete objective decisions, survival, tactical force use and believable
shooting. **Aiming/combat development remains paused until then.**

The owner prioritizes idle time, unnecessary walking and getting stuck.
Captures/deaths establish objective and native lifecycle behavior; they must
not conceal bad movement through resets or tactical selfkill. Walking for a
real turn, landing or objective arrival remains legitimate.

There is no defensible completion percentage, subscription-reset estimate or
fixed research duration. Report completed delivery gates and measured costs.

## Architecture to build toward

1. **Known-map preparation.** Keep Recast/Detour for topology and native
   collision for actual support, clearance and hazards. Coalesce artificial
   polygon seams into traversable intervals. Prepare outbound, return and
   alternate/recovery continuations from real map geometry. Rebuilding the
   navmesh is justified only by measured topology/geometry defects.
2. **Movement-state planning.** Plan racing lines and connected feedback
   maneuvers through position, velocity, heading, contact, jump latch, remaining
   command time and resource state. A nearby landing point is not a compatible
   continuation. Search/optimize substantial trajectories offline; record
   refusals and entry/exit validity, not just nominal successes.
3. **Shared control.** Use the same command policy and native cadence for
   forecast and execution. The current pure flight controller and receiver are
   useful foundations. Keep the frozen cached-route controller as a regression
   reference; change it only for a demonstrated shared control/physics defect.
   Pickup, moving bodies, objective ownership and recovery lifecycle belong in
   live integration, not as special cases in that frozen kernel.
4. **Bounded live work.** Load prepared data, retrieve a small bounded policy
   set, validate the actual state, track it and repair departures using the
   same movement graph. Measure total retrieval, validation, preparation,
   tracking and recovery. A precomputed proposal index alone does not make
   full native rollout selection affordable.
5. **Native lifecycle.** Preserve real motion, force, health, triggers, pickup,
   return, capture, death and respawn. No state replay/reset between legs.
   Static-world discovery guarantees do not cover arbitrary multiplayer world
   mutation. Unavailable or invalid plans must yield a reasoned safe fallback.

Empirical entry regions are not formal safety certificates. Exhaustive global
state coverage, demo mining, training and a new navmesh are not prerequisites
for the next usable route. UnrealJKA and demos are sources of reusable ideas
and counterexamples; do not port their entire stack or restart reconnaissance.

## Acceptance contract

### Existing benchmark: retain M0, M1 and M2

M0 retains the original 12-case native route/feedback regression.

M1 inventories all native standard Kejim spawns: 24 distinct initial/respawn
poses plus two own-flagstand starts. Preserve explicit aliases for duplicate
poses. Each pose needs an actual native enemy-flag pickup and a same-start,
same-physics, same-resource planned reference to that endpoint. Nominal time
must be <=110% of reference; all eight command phases must finish alive and
the slowest must be <=120%. One configuration must select the appropriate
route; per-route cvar changes do not qualify map-wide selection.

M2 freezes feasible offsets ±32 units on each horizontal axis, impulses ±50
and ±150 units/s per horizontal axis at takeoff/mid-flight/approach, one skipped
jump and a temporary body blocker. Require >=95% pickups overall, >=90% in
every family, M0 retention and successful trips <=150% of their clean reference.
Retain infeasible setups explicitly. Use independent evaluation after tuning;
list deaths/timeouts individually. The historical frozen benchmark passed,
but newer integrated movement does not inherit those results.

### Full-route movement quality

Use complete native first round trips, with force 0 and no initial Speed,
120 game seconds per observation. Select one schedule per team from development
before evaluating phases 0/3/6 against matched inactive controls: 12 native
runs, six comparisons. Every candidate trip must be no slower; each team's
median must improve by at least 10%. Travel idle and walking fractions must
not regress in any pair. Preserve all missed entries and failed observations.

The existing speed tool's `passed` field covers time only: also require
`all_idle_not_worse` and `all_walking_not_worse`. Do not reinterpret a speed-only
exit code as the complete movement gate. Isolated savings are proposal weights,
not additive full-trip predictions. This gate does not establish runtime cost,
defended reliability or installation.

No-Speed references cannot be compared to old fixture-injected Speed tapes.
Implement matched-resource references through production dispatch where missing.
Also retain the 30-case solo resource matrix, cached-Speed-route expiry handoff,
travel-facing behavior and ordinary-jump/resource regressions.

### M3 and M3a: runtime cost and continuous defended movement

Owner update: movement correctness and demonstrated usefulness come first.
CPU cost is a separate optimization/provisioning target, not a veto on a good
movement method or a prerequisite to its functional acceptance.
At 40 Hz with eight actual active bots, retain the reference targets of
p95 <=2.5 ms and maximum <=5 ms on this host; report overruns honestly. Include routing, fallback,
validation and recovery; exclude diagnostic file writing and report complete
server-frame time separately. Stress 16 bots and report scaling/activity.
Timing runs execute alone. Dead/idle clients cannot stand in for active work.
Charge preparation even when it executes later inside ClientThink_real, outside
the existing dispatch timer. Include cold load, binding, selection and early
refusal; avoid double-counting already-timed command execution. The25 ms game
tick is not a substitute movement budget. Lost living clients invalidate an
eight-active-client claim; retain and investigate their lifecycle evidence.

Freeze baseline and candidate defended sets: three 600-game-second runs per
team at 1x and three per team at 5x, one runner versus the same CHASE/BASE pair.
That is 12 runs per arm. Preserve starts, resources, opponents, supported seeds,
rates, complete durations and native 25 ms game steps. Use botplayrate for these
matches; changing timescale alone is not equivalent.

Require all cases valid and no unexplained confinement >=5 game seconds inside
a 64-unit region more than 96 units from the goal. Each team/rate cell's travel
idle and walking fractions must not exceed baseline; require no aggregate
regression as well. Compute pooled seconds/denominators, not averages of ratios.
Keep classified flag-state waits separate, retain raw alive-time totals and
review suspicious larger loops in telemetry/demos. Combat/congestion is not
an automatic excuse for confinement.

Metric definitions differ by protocol: the fixed-tick solo report uses XY
speed <50 for idle and grounded 50..300 for walking; the defended auditor uses
grounded 3D speed <20 and 20..300, with time weighting and classified goal-wait
exclusion. Preserve both; do not compare their percentages as the same metric.
The [defended protocol](STRAFE_MOVEMENT_CONTINUITY_CAMPAIGN_2026-09-06.md) and
[fast-trip protocol](STRAFE_MOVEMENT_FAST_TRIP_2026-09-06.md) define the details.

Demonstrate repeated useful objective progress, native pickup/capture and
respawn handoffs, dropped/returned/held flag changes, no-Speed traversal,
mid-trip Speed expiry and bounded recovery. Force is a shared changing match
resource; survival spending and later replenishment cannot be assumed away.
M3a is closed only when the same integrated build also retains the benchmark,
resource/facing/jump checks and normal MP behavior. Record representative cost
separately; exceeding current CPU targets does not block functional acceptance
under the owner’s September 14 direction.

Owner scope clarification, September 13: dropped-flag retrieval is deferred to
later objective/combat work and is not a blocker for the current baseline
movement milestone. Retain existing behavior and evidence. Ordinary route
walking, corners, landing transitions and safe fallback remain in scope;
retrieval requiring passage through `trigger_hurt` is not required. Historical
qualification scores are unchanged by this scheduling decision.

### M4, M5 and project completion

Dantooine has early geometry/transition evidence, not a qualified full-map
runner. M4 repeats M1–M3a with its own native start/reference inventory, the
same preparation/controller and no map-specific C or tuned-down gates.
Additional map/resource coverage follows measured deficiencies.

After movement acceptance, M5 adds objective and survival decisions, force
allocation, then believable aiming/shooting. Declare match and human-behavior
acceptance before evaluation; no numerical combat gate has yet been agreed.
Do not invent a pass threshold or claim full offense intelligence from movement
captures. Tactical selfkill is a later decision and must never hide a stall.

## Execution and evidence discipline

Use [verification](STRAFE_MOVEMENT_VERIFICATION.md) for native procedures and
[AGENTS.md](../AGENTS.md) for boundaries. Keep source/binary/settings/case bindings
and failed evidence. Report implementation, measurement, qualification and
installation separately. Keep chronological results in the scorecard or dated
records instead of appending old next actions here.
