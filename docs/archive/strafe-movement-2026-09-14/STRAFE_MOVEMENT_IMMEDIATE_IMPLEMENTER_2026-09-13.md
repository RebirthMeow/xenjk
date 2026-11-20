# Immediate implementer work order

Authorized 2026-09-13: execute flight safety first, then supported outbound
walking. This is implementation work, not another plan-only review. The user
delegated these items to the implementer after the main-task recovery cycle.
Keep the important work on Astra and do not spawn subagents. Follow the
[current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) for current build,
evidence and source boundaries; older task messages and dated work orders do
not supersede this direction.

## 1. Reproduce and repair the defended flight failure

Start from `.codex-research/landing-requery-b64a15afc09445b6b2cbfbb678c67896/defended`:
`departure-attribution.json`, `blue-departure-rows.json`, and
`blue-1x-r1/server.log`, its CSV and demo. Read the
[defended record](STRAFE_MOVEMENT_LANDING_REQUERY_2026-09-13.md#defended-discovery)
before drawing a causal conclusion.

Flight window 11/candidate 2 prepared at level 525425, entered at 525450,
jumped at 525700, lost upward velocity at 525775, refused at 525875 and failed
recovery at 525975. A subsequent lateral refusal handed off to localnav; the
runner reached the bottom and died by MOD_FALLING. The CSV's `supported` label
includes flight-adapter ownership. An overhead opponent is a plausible contact,
not a confirmed cause. Speed expiry coincided with entry but is not proved causal.

1. Trace the actual controller, forecast and native contact boundaries. Likely
   entry points are `ai_bot2_flight.c`, `bot2_flight.c`, `bot2_forecast.c` and
   `ai_bot2_supported.c`; inspect the relevant diff and consumers first.
2. Build a bounded native reproduction preserving the cached path and pre-hop
   player/controller state. Record contact entity, hulls, positions, velocity,
   commands, timing, resources, opponent motion and forecast/live divergence.
   If the tape lacks required state, collect that state with scoped diagnostics;
   do not pretend an approximate reset restores it. Instrumentation must not
   alter native gameplay or be included in representative runtime-cost claims.
3. Identify the first meaningful divergence and repair the general admission,
   execution or recovery defect it establishes. Do not suppress a Kejim window,
   add rescue coordinates, assume knockback, or add a speculative expiry guard.
4. Retain a failing control and verify the corrected case plus relevant nearby
   counterexamples: clear passage, moving-body interference, geometry/contact
   changes and resource transitions. Show useful continuation or safe refusal,
   not simply disabled movement. Non-recurrence in a short match is insufficient.

Three existing fresh-path solo approximations captured without falling. They
omit cached state and combat and are not replays. Their declarations and limits
are in `.codex-research/landing-background-1c665f0490dc44cf971d32c0cb37b4ab/departure-scope.txt`.
Do not repeat them unchanged as proof of a fix.

## 2. Diagnose and reduce supported outbound walking

Use `walking-attribution.json` under the defended evidence root above. The two
discovery worlds spent about one-third of alive travel walking. Supported
outbound movement with Speed inactive accounts for 153.525 s red and 142.45 s
blue; a 9.25 s red walking interval retained Force 100 throughout.

1. Trace representative intervals through movement ownership, Speed policy,
   hop admission, preparation, terrain and landing/continuation constraints.
   Separate legitimate ground travel from avoidable braking or unused resources.
2. Select a demonstrated cause, declare affected comparisons before tuning,
   then implement a reusable controller/policy correction. Preserve resource
   ownership, supported floor/clearance, turn limits and the flight safety repair.
   Do not force Speed everywhere or lower hop thresholds solely to improve counts.
3. Verify both teams and depleted/full Force with Speed inactive, active and
   expiring. Measure complete trips, idle/walking fractions, meaningful hop use,
   safety and recovery; retain every regression and censored outcome. A local
   speed increase alone does not complete this item.

Proceed to this item after the flight repair's affected verification. If faithful
flight reproduction needs additional evidence, continue useful walking diagnosis
while preserving the safety item as unresolved; do not silently replace it.

## Verification, delivery and scope

Use the [verification reference](STRAFE_MOVEMENT_VERIFICATION.md) and unchanged
[acceptance contract](STRAFE_MOVEMENT_PROJECT_PLAN.md#acceptance-contract).
Freeze sources through fresh unique builds and native collection, retain failed
evidence and verify source/binary/settings/case bindings. At most four isolated
outcome worlds with distinct ports/homepaths; timing runs alone. Use hidden
localhost helpers and preserve native lifecycle apart from declared fixture setup.

Run affected compiled checks and native preflight before broader measurement.
Where supported traversal or route admission changes, retain the twenty-case,
three-repetition comparison and affected 26-body/30-resource coverage described
in the continuation plan, using current compatible baselines. Add affected
normal-speed defended checks for the flight repair. Existing discovery and older
subset results do not qualify a changed integrated build.

Deliver each demonstrated correction with its causal evidence, before/after
results, counterexamples, exact frozen build, limitations and remaining failures.
Update the scorecard when results change and the handoff when the next action
changes. Continue through implementation and affected verification; do not stop
after adding diagnostics or writing a plan. If evidence cannot establish the
cause, state precisely what remains missing rather than marking the item fixed.

Preserve unrelated working changes. No installation, bulk commit of the dirty
workspace, combat development, map-porting campaign or threading redesign is
part of this work order. T5/T6/K07 gates remain unchanged and open until their
actual requirements pass. The broader completion roadmap follows these repairs.
