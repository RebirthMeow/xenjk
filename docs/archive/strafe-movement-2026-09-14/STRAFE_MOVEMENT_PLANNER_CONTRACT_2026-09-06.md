# First-stage movement planner contract

Status: research contract, 2026-09-06. Initial corridor and geometry-derived
two-gap native subsets are implemented; [fixture evidence and limits](STRAFE_MOVEMENT_PLANNER_FIXTURES_2026-09-06.md)
record their scope. [Actual-state reselection](STRAFE_MOVEMENT_REPLAN_PROBE_2026-09-06.md)
now passes a bounded offline static subset. Affordable online replanning,
cached reuse and production integration remain proposed. This document is not
frozen qualification.

## Existing foundation and boundary

`bot2_movebench.c` already provides the useful simulator boundary. A forecast
copies the complete `playerState_t`, `pmove_t` inputs and movement-policy state,
runs the native `Pmove` path with fixture trace/contents callbacks, advances the
server clock, and restores the world and clock afterward. The harness compares
live and forecast commands, policy state, movement state, contacts and time. It
also retains a deliberately biased forecast as a negative control. The current
route experiment forecasts eight prescribed turn-time candidates for 160
commands each and ranks either the first landing or the second exit.

That route scorer is a development demonstration, not the planner described
below. Its candidate times and map coordinates are hand written, its nominal
successful exit has only 0.386 units of clearance, its three starting offsets
do not define an entry region, and its instrumented 1,280-call search has no
runtime deadline. The planned extension must keep the verified native replay
contract while replacing those assumptions with explicit roots, geometry,
maneuvers and exits.

The first stage supports a fresh dedicated fixture with one moving client and
static BSP collision. The client is in `PM_NORMAL`, uses the ordinary player
hull and fixed blaster setup, and has no vehicle, saber lock, force-hand action,
active Force power, dynamic body contact, mover, missile, item, liquid, or
trigger crossing. Ordinary ground travel, braking, repositioning, jumping and
air steering are in scope. Force Jump, Speed activation or expiry, damage,
moving geometry and other actors require separate world-transition contracts.
An unsupported observation invalidates that rollout; it is not a failed
maneuver. This contract makes no full-world simulator or production-parity
claim.

## Planning root

Every decision starts from one immutable root record captured before candidate
generation:

- the complete `playerState_t`, movement-policy state and `pmove_t` inputs used
  by native movement, including command time, origin, velocity, view/delta
  angles, contact phase, ground entity, `pm_flags`/`pm_time`, animation and jump
  history, gravity, speed and collision bounds;
- the server time/frame and the static-world/build/settings hashes;
- the preceding entity animation prefix consumed by the GAME animation helpers,
  advanced and restored explicitly for each native forecast step;
- an ordered corridor description in world coordinates, its support and swept
  hull validation, and the requested onward direction;
- the supported-state result and its precise rejection reason.

Quantized keys may index cached work, but native validation always begins from
the retained root. A cache hit cannot substitute its representative state.
Forecast generation must finish before the corresponding live prefix begins.

## Maneuver and candidate contract

A maneuver is a deterministic feedback policy, not a prerecorded command
string. It declares an entry predicate, geometry interval, command generator,
maximum duration, swept contacts, terminal reasons, exit signature and resource
cost. The initial maneuver set is ground steer/run, brake/reposition, ordinary
jump with air steering, and landing continuation. All candidates use the same
command generator in forecast and live execution at the fixture's declared
server cadence.

Candidate proposals come from the corridor tangent, turn, width, support
boundary, ceiling and the root position/velocity/contact phase. Map names,
fixture coordinates and a manually selected successful turn time may not enter
proposal or ranking code. Multiple approach/exit states may remain when they
carry materially different velocity, heading, contact phase or clearance.

Each proposal records why it exists and every native step it consumes. Before a
collection, its machine-readable declaration fixes the maximum candidates,
commands per candidate, replans and cache validations. The first implementation
may use the existing eight-candidate, 160-command envelope as an explicitly
offline diagnostic ceiling. It must report that work separately and cannot call
it an online budget. A deployment experiment must freeze a smaller online
allowance and measure it with capture and diagnostic I/O disabled against the
existing eight-bot server-frame target (p95 at most 2.5 ms, maximum at most
5 ms). No result may choose its allowance after observing candidate outcomes.

## Geometry and exits

The fixture extension contains three parameterized static families: continuous
floor with one physical corridor represented by 1, 2, 4 and 8 collinear route
segments; the existing two-gap turn; and a ramp-to-flat transition. Training
and held-out combinations of width, turn angle, platform/gap dimensions, entry
offset, velocity and contact phase are frozen before tuning. At least one
second-map static geometry probe uses the same automatic extraction and
planner, without map-specific movement code or a supplied successful line.

A geometry validator admits a candidate only when the player hull's proposed
swept volume and supports are valid under native trace/contents semantics. It
records minimum clearance and contact transitions rather than treating a path
corner as a required stop.

An exit is useful only if all of these hold:

1. the declared terminal support and hull containment are satisfied, with no
   unsupported contact or dangerous state;
2. velocity, heading and contact phase fall within the next maneuver's declared
   entry predicate;
3. replay of that next maneuver reaches its declared onward boundary while
   remaining supported.

The exit signature retains the complete native and policy checkpoint used by
the continuation. Position, first contact, zero speed, or proximity to a cached
line alone is insufficient. A terminal safe stop remains a valid bounded
result only when explicitly requested or when search exhausts; it is reported
separately from a traversable exit.

## Execution and continuation

The planner selects a candidate, executes a predeclared short command prefix,
then compares the actual complete checkpoint and contacts with the predicted
checkpoint. Exact replay is required in the deterministic fixture, apart from
the two already documented server-bookkeeping fields. A continuing plan is
revalidated from the actual root and current corridor. Any failed entry
predicate, geometry change, unsupported contact or replay mismatch invalidates
it and invokes the same planner from the observed state. Recovery may broaden
its proposals and prefer survival, but it uses the same maneuver and exit
contracts and must prove onward movement.

Prefix length is a frozen fixture parameter. The evaluation includes at least
one replan while grounded, one during ordinary airborne steering and one after
a declared, reproducible position/velocity perturbation. The perturbation is
applied before root capture; prediction is never repaired by resetting the
player to the nominal line.

## Comparisons and fixture gates

First-landing/local ranking and usable-exit/continuation ranking receive one
identical proposal inventory and rollout allowance. The current integrated
controller is a separately measured engineering baseline with its existing
guidance and cost; do not pretend it consumes this proposal inventory. A further arm exercises retained
continuation with prefix replanning. Extra-budget offline search is diagnostic
and labeled separately; it identifies missing proposals, horizon failures or
misranking but cannot make an evaluated arm pass.

Before native collection, freeze the build/settings/map hashes, roots and valid
root tolerances, geometry split, controller parameters, candidate and command
ceilings, prefix schedule, scorers, tie breaks and output schema. Retain every
candidate and failed/unsupported case. The following are first-stage gates:

- **Simulator integrity:** world and clock restore, every selected forecast/live
  command, policy, movement, contact and clock checkpoint matches, and the
  negative-bias control is detected.
- **Proposal parity:** all scorers receive the same geometry-derived candidates
  and allowance; candidates contain no case-specific successful command time or
  coordinate.
- **Usable traversal:** report complete traversal, onward-exit result, survival,
  minimum clearance, walking/idle/confinement, recovery latency, facing and
  resource use. Continuation may pass only through the exit contract above.
- **Representation invariance:** the 1/2/4/8-segment continuous-floor cases
  produce the same normalized physical corridor and proposal inventory, the
  same supported terminal class, and no subdivision-created stop or walking
  interval. Any command/path difference is retained and attributed with native
  traces rather than averaged away.
- **Replanning:** every declared prefix and perturbation starts from its actual
  checkpoint, respects the same limits and either completes a compatible exit
  or reports exhaustion/unsupported state. Reusing a nominal future without a
  valid transition fails this gate.
- **Held-out comparison:** lock proposal and scoring parameters before the
  held-out geometry/state split. The continuation planner must have no fewer
  usable exits than the simpler baseline in each declared family; a regression
  keeps the baseline. Improvements and losses are reported as case counts and
  trajectories, without adding an after-the-fact percentage threshold.

Timing is collected in separate, instrumentation-disabled runs and includes
candidate generation, validation, ranking, cache lookup/invalidation and
replanning. Preprocessing time, storage and human-authored geometry are reported
separately. Fixture success establishes this static supported subset only.
Production dispatch remains gated on production ownership, dynamic-world and
resource contracts, the frozen defended comparison and the active M0--M3a
qualification requirements.
