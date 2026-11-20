# Movement continuation execution plan

Prepared 2026-09-12 for the next AI. The owner requested this handoff after
reviewing progress, generalization and whether CPU threading would solve the
movement problems. This is planned work; no new implementation or result is
claimed. Follow the [handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md),
[scorecard](STRAFE_MOVEMENT_PROGRESS.md) and unchanged
[acceptance contract](STRAFE_MOVEMENT_PROJECT_PLAN.md#acceptance-contract).

## Status after main-task implementation, 2026-09-13

The owner has assigned the [immediate implementer work order](STRAFE_MOVEMENT_IMMEDIATE_IMPLEMENTER_2026-09-13.md):
flight safety reproduction and repair, then supported outbound walking. Execute
that order before the broader qualification sequence below.

The guard, bounded join repair and background landing recovery are implemented
and measured. Defended discovery found an unresolved flight-adapter plunge;
contact attribution and outbound walking now lead the next work.
Use the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#next-action)
and [latest record](STRAFE_MOVEMENT_BACKGROUND_RECOVERY_2026-09-13.md) for results,
current baseline and remaining work. The original sections below are retained
work orders, not instructions to repeat completed cycles. Sections 4 and 5 still
define affected verification and broader qualification; replace their historical
baseline references with the current handoff's build and arm.

T5 and whole-movement qualification remain open. Routine implementation choices
may proceed locally; the user asked to avoid subagents. No installation is in scope.

## Objective and scope

Reduce unnecessary stopping and ground running while preserving useful corner,
landing and objective braking. Implement reusable geometry/controller rules,
not Kejim coordinates, route-ID exceptions or hand-authored rescue points.
Kejim locations are regression cases, not production conditions. Prepared branch
execution is reusable, but current route assets do not establish automatic
preparation or coverage on other maps.

Continue through diagnosis, implementation and affected verification. A local
pass is progress, not completion of full movement quality. Keep installation
separate: the installed baseline remains `reviewready-b` / `525f1b50`.
Do not start combat/aiming expansion or a dedicated-thread redesign in this cycle.

## 1. Establish the current baseline and precise defect

- Inspect the relevant working diff; preserve all existing uncommitted work.
  Locate controller ownership using the Bot2 README before editing code.
- Use seam v3 as the baseline for NEW repairs, not its older corridor control:
  `.codex-research/seam-fix-20260912/v3/candidate-r{1,2,3}` and
  `comparison.json`. Its measured build is
  `.codex-research/strafe-build-seam3-53a5fcfaf4964c438257a4419b667c9c`.
  Verify source bindings before native reuse. Collect any missing controls before
  edits, or preserve an independently bound checkout for later collection.
- Use the [phase attribution](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md) to locate
  ramp-entry intervals near red `(255,-310)` and blue `(2365,4460)`, then inspect
  those intervals in the latest seam-v3 traces. Historical attribution numbers
  describe the corridor build; do not label them current candidate measurements.
- Record controller owner, path nodes/metadata, command, speed, contact state,
  resources and exact reason for each slowdown. Separate preparation waits,
  required braking, controller-imposed stops and genuine obstacles.
- Probe native floor/hull clearance and support over a proposed continuous
  transition. A straight XY projection or mocked trace pass is insufficient.

Deliverable: a dated repair record with the reproducible interval, responsible
code contract, proposed geometric rule and evidence that the maneuver is feasible.

## 2. Repair ramp-entry continuity

Inspect `ai_bot2_supported.c` path preparation, pass/corner validation and arrival
conditions first. Coalesce artificial segments or publish a checked continuation
only where native geometry supports it. Account for elevation, body clearance,
entry velocity and the required exit/rejoin span. Preserve the seam repair's
local metadata boundaries and upstream hop behavior.

Add focused regression cases for the diagnosed failure and nearby counterexamples:
mirroring/rotation or translation, real sharp turns, elevation changes, short
legs, missing support, blocked hulls and hazards as relevant to the changed rule.
Demonstrate that the diagnosed case fails before the correction. Tests must
exercise behavior rather than just reproduce the implementation.

Do not globally raise turn speeds, remove arrival checks or weaken safety gates
to obtain a faster trace. If clearance prevents continuity, retain the refusal
and move to a feasible alternative; document the evidence.

## 3. Repair unnecessary fragmentation of hop room

After the ramp correction, reattribute remaining slow ground travel. Inspect
supported hop preparation and `FastReturnMomentumRoom` in `ai_bot2_route.c` only
where the trace identifies their ownership. Determine whether boundaries split
one genuinely usable interval, or whether slope/clearance/landing constraints
actually require ground travel.

Join usable intervals through checked geometry and compatible movement state.
Preserve speed-dependent landing room before bends, authored jumps and rejoins;
retain resource admission, actual guide alignment and hazard checks. The corridor
repair prevents a real fall and must survive this work. Do not simply lower a
distance threshold until the bot jumps. Validate takeoff, flight and landing
natively, including depleted-Force and Speed-expiry states.

## 4. Verify the candidate and judge whole movement

Follow the [verification reference](STRAFE_MOVEMENT_VERIFICATION.md). Run affected
checks, including `tools/strafe_seam_check.py`,
`tools/strafe_supported_wait_check.py` and `tools/strafe_fast_return_check.py` when
their contracts are touched; inspect each tool's CLI before invocation. Run
relevant existing Python tests if changing audit/runner code.

1. Create a fresh unique native build after source changes. Freeze measured
   sources throughout build and collection; record source, loaded DLL, binary,
   settings, asset and case bindings. Never overwrite retained evidence.
2. Run a bounded native preflight covering both teams and the affected resource
   states. Record all failures. Diagnose a failed preflight before broad collection.
3. For a viable candidate, use the seam record's unchanged twenty-case declaration
   and three repetitions for comparison against seam v3. Reuse its drivers by
   adapting build/output bindings into new files; inspect their arguments first.
   Keep the existing case set and durations. Run at most four isolated worlds.
4. Run affected body/resource regressions: preserve the existing 26-case body and
   30-case resource coverage when supported traversal or route admission changes.
   These subsets do not establish full M2/M3 qualification.
5. Report paired complete-return times, idle/walking measures, stationary and
   zero-input totals, local speed profiles, captures, falls, deaths, branch
   abandonment, churn and resource refusals. Preserve censored endings explicitly.
   Distinguish diagnostics from the contract's idle/walking denominators.

Retain the repaired seam, corridor safety and branch/resource behavior. A local
speed gain with worse whole-return idle/walking is not a movement-quality pass.
Apply the unchanged T5 thresholds and denominators; do not omit failed cases.
If T5 still fails, attribute the remaining loss and continue with the next
supported cause rather than repeating an unchanged campaign.

## 5. Advance qualification after the controller repairs

- Run the contract's matched-resource full-trip comparison; the twenty-case
  repair campaign alone cannot satisfy its time AND idle/walking requirements.
- Address the retained self-steered defended departures using the
  [fall attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md), then execute
  affected defended checks and the declared qualification set when ready.
  Preserve T6's existing falling-death clause. If the owner decision about
  knockback becomes necessary, ask only about that decision; other fixes proceed.
- Profile the complete current movement workload with eight active bots at 40 Hz,
  followed by the declared sixteen-bot stress. Timing runs alone, without diagnostic
  file-writing cost, with complete server-frame time separately reported. Include
  preparation, routing, validation, execution and recovery even outside dispatch.
- Consider asynchronous planning only if measurements connect budget overruns,
  command delays or preparation queues to a real problem. Current evidence shows
  controller-imposed slowing; older forecast-only timing is not current full-cost
  evidence. A thread would require a separate design for snapshots, engine trace
  safety, stale-result rejection and main-thread validation/application.
- Retain T2 landing turns, T4 fallback corners, later launch-family coverage and
  branch terminal/censoring accounting as explicit remaining obligations. Validate
  other geometry/maps and automatic preparation before claiming map transfer.

## Handoff and completion discipline

Update the scorecard only when measured results change, the short handoff when
the next action changes, and keep detailed cases/failures in the dated record.
Report implementation, measured outcome, qualification and installation separately.
Do not invent a completion percentage or treat captures alone as movement health.

The ramp/hop repair cycle is complete when the diagnosed corrections are verified,
their affected regressions are checked and full-travel consequences are recorded.
Then advance the remaining authorized movement work above; do not label K07 or
the full project complete while its acceptance gates remain open. Installation
requires its own task scope. A useful independent subagent assignment is read-only
review of the proposed geometry rule or evidence accounting while the lead owns
implementation; coordinate source freezes and all native process slots.
