# Dynamic recovery trials and native command validation - 2026-09-14

## Decision

Dynamic recovery is **not solved or ready for ordinary activation**. Three
implementations were tested. The final prototype adds native command/braking
validation and stays off by default (`bot_recoveryNative 0`). Enable it only
with the retained fixture runner's `--recovery-native` option for further work.
The preceding ground-descent evaluator correction remains active.

Final candidate C survives all 16 outcome worlds; 14 capture. Its eight body
cases all capture, but C01-air and C07-air regain 7.250/7.500-second confinement
episodes, versus none in the preceding descent cohort. Red fast pursuit has no
capture in either 90-second candidate world. These functional failures, not
CPU cost, keep the prototype off. Earlier failed trials and setup failures
are retained, not omitted from the record.

## What the trials established

**A: failure memory, a complete 28-candidate scan in one frame, and a larger
progress window.** Small displacement no longer starved recovery and failed
directions were avoided, but this was not useful movement acceptance. Of eight
30-second pursuit worlds, six survive and four capture. Two red fast-pursuer
worlds fall; others move around without capturing despite zero confinement.
This trial is removed from active code and preserved in `candidate-a/`.

The exact failure is a command/execution mismatch with the geometric admission
contract, not a cached-route fall. In C01-300-runner, recovery selects the
northward segment from (654.842, -310.037, 1808.125) at level 31625. Commands
include simultaneous forward/right input at 31700. The runner drifts to
x=655.158 and becomes airborne at 31750, then falls. The narrow support admitted
by the corridor does not protect against the actual controller's sideways
motion. `rejected-edge-departure.log` retains the full decisions around it.
Do not broaden activation using geometric admission alone.

**B: failure memory alone, preserving previous activation and scanning.** All
eight pursuit worlds survive, four capture. The slower red Runner captures at
11.050 seconds instead of the preceding 20.625-second repeats or 32.100-second
first run. However both red fast-pursuer cases lose their previous observed
captures in the short window. Failure memory can stop repeated choices; it
does not establish a viable, useful alternative. B is preserved in `candidate-b/`.

**C: opt-in memory plus native command/braking validation.** This is the current
source. The aggressive A progress/scanning changes are not present. Ordinary
Runner retains the previous behavior when the new option is off.

## Current prototype implementation

`ai_bot2_localnav.c` owns the policy and private state. Four recent failed
local directions are retained for six seconds, within 96 units of their
origin and a 30-degree angular cone. This covers longer/shorter copies of a
failed leg. Timeout, live corridor refusal and activation refusal can record a
failure; lifecycle/objective reset clears it. Every admitted alternative still
requires the native corridor and topology checks. The expiry is temporary
search history, not a permanent map prohibition or a learned body model.

With the option on, the primary recovery owner aligns heading and lateral
motion before translating, with a bounded requested speed up to 250 units/s.
The secondary navigation path retains its existing controller. Both mark the
actual issued recovery command for checking at the native Pmove boundary.

`Bot2_MoveLayersBeforePmove` uses the existing forecast adapter to evaluate
that exact submitted command, then zero-input braking until grounded horizontal
speed is below 5 units/s. Maximum horizon is 24 native commands. Every retained
forecast position must still have bounded native floor support. A supported
small descent is legal; simply predicting a landing far below is insufficient.
No position, velocity, resources or command time are overwritten.

A refused rollout neutralizes the current movement/action request, records
failure and returns to replanning after the existing retry delay. A world or
restoration failure disables this prototype for the client until lifecycle
reset. `bot2_movebench.c` invokes the check immediately before real Pmove;
`ai_bot2_internal.h` declares it. The runner binds the option in provenance.

This is a **static-world command shield**, not a dynamic-body trajectory
planner. Braking is evaluated against current body positions. The existing
forecast adapter still rejects body contact and active powers. The guard does
not prove that a moving body will remain a braking obstacle, or generate a new
move when all proposed commands are refused. It also does not compare live
post-state against a latched complete recovery trajectory. No complete safety
or execution-fidelity claim is made for this prototype.

## Final C outcomes

Times are sampled seconds after the acknowledged physical-start intervention.
All candidate runs request 90 game seconds and retain the native shutdown tail.
Phase variation is visible; these are physical-start repetitions, not exact
replay of all private state or an isolated speed attribution.

| Pursuit case | Runner first capture | Hop supplement first capture |
| --- | ---: | ---: |
| C01, speed 120 | 8.975 | 11.350 |
| C01, speed 300 | none observed | none observed |
| C07, speed 120 | 16.800 | 16.550 |
| C07, speed 300 | 18.650 | 76.275 |

All eight survive. Six have at least one confinement episode. The native
recovery guard records 54 solid/body refusals (mask 3) and two active-power
refusals (mask 64). No world/restoration failure is reported. Body/solid refusal
is an unsupported forecast condition here, not proof that every physically
valid escape is unsafe. In particular, red fast pursuit repeatedly fails on
its first predicted frame and does not capture.

| Body case | First capture | Second capture | Confinement |
| --- | ---: | ---: | ---: |
| C01-none | 5.425 | 38.400 | 0 |
| C01-pre_takeoff | 8.425 | 48.725 | 0 |
| C01-takeoff36 | 6.075 | 46.550 | 0 |
| C01-air | 11.225 | 59.625 | 1 (7.250 s) |
| C07-none | 10.875 | 44.125 | 0 |
| C07-pre_takeoff | 15.775 | 55.600 | 0 |
| C07-takeoff36 | 11.425 | 51.600 | 0 |
| C07-air | 14.500 | 61.200 | 1 (7.500 s) |

All eight capture alive. Six expected hop-body guards still fire; no unexpected
hop execution/world mismatch occurs. The recovery guard adds eight body/solid
refusals. Those new refusals and failed-leg exclusions do not remove the two
air-body stalls. None of these outcomes establishes a broad walking win.

## Same-build default-off controls

Four additional 90-second worlds use build C with the prototype disabled.
First captures are 32.100/20.550 seconds for C01 pursuit 120/300, and
17.925/62.800 seconds for C07 pursuit 120/300. C07-120-runner subsequently
falls; the other three survive. `control-fall.log` retains that separate
baseline failure. The clear red fast-pursuer capture with the prototype off
versus no candidate capture through 90 seconds is a material regression.
Default-off safety is not a claim that the previous controller has no defects.

There are 36 outcome worlds in this work: 33 survive, with two deaths in
rejected A and one in the default-off control. The eight failed setup worlds
are retained separately. `verification.json` verifies all 36 copied binaries,
build/provenance bindings and all 1,109 native inputs for each source variant.
No failure is reclassified as a pass based on process exit status.

## Verification and evidence

Root: `.codex-research/dynamic-recovery-20260914-a/`.
Native builds: `.codex-research/strafe-build-dynamic-recovery-20260914-{a,b,c}/`.
Final C source manifest: `20260914T202824770710Z.sources.json`.

- `before-source/`, `candidate-a/`, `candidate-b/`: retained source variants.
- `pursuit-a/`, `pursuit-b/`, `pursuit-c/`: all declared pursuit outcomes.
- `native-c-retry/`: final eight body outcome worlds.
- `native-c/`: eight failed setup worlds, not movement passes. Runner
  initialization took about three seconds, exceeding the two-second RCON reply
  wait. The runner had actually been created, but the required acknowledgement
  was not received. No creation command was retried in those worlds.
- `strafe_play_smoke.py` now waits up to 15 seconds for that acknowledgement;
  the acknowledgement and decision-seed checks remain required. Fresh worlds
  succeeded with that change. Runner hashes distinguish the two cohorts.
- `results.json`, `recovery-events.json`, `rejected-edge-departure.log`: complete
  outcomes, exact confinement windows, failed legs and native refusal details.
- Existing recovery, sidestep and descent checks passed during development.
  The recovery check also covers failure locality, rotation, expiry, client
  isolation and selecting a different safe leg after live obstruction.
- `tools/strafe_recovery_native_check.py`: extracted production-C integration
  checks actual command/raw-time binding, inert disabled mode, body/support/
  incomplete-horizon/world refusals, neutralization without PS mutation and
  braking-policy behavior. Physics itself is covered by native worlds.
- Both existing runner seed suites pass (12 Python tests).

Native sources remained frozen during each build and collection. The harness
acknowledgement wait changed between cohorts, not during a world. No game
installation, ordinary activation or CPU timing qualification occurred.

## Next implementation: validate candidate connections before selection

Do not resume A's aggressive activation or promote C based on survival alone.
Reuse the existing forecast and handoff contracts to prepare complete ground
connections from actual state, rather than select a geometric leg and discover
its command refusal during execution.

1. Define a narrow, tested contact-capable forecast contract. Distinguish
   actual native collision response, unsupported moving-body evolution and
   genuine unsafe motion. Do not simply clear `BOT2_FORECAST_BODY`, ignore
   bodies, or assume a moving body will remain available to stop the runner.
   Resource/model refusals must not be treated as evidence against a direction.
2. Evaluate several bounded command sequences, including their turns,
   acceleration and braking, and an admitted onward route interval. Require a
   useful receiving state; an arbitrary sideways endpoint is insufficient.
   Keep the existing objective/route publication as owner of the destination.
3. Latch the admitted command sequence and before/after state expectations;
   validate actual state and the next native command each frame. A new body
   refusal should lead to another evaluated connection, not another unvalidated
   steering choice. Preserve refusal evidence and all native lifecycle rules.
4. Only after execution safety is established, restore progress observation
   that distinguishes oscillation from departure. Judge complete trips and
   captures as well as confinement: A demonstrates that a moving failure can
   pass the confinement audit.
5. Repeat the retained body cohort, both pursuit speeds with Runner and hop
   supplement, the exact narrow-support failure and complete-trip controls.
   Require the previous ground-descent successes to remain intact. Measure
   CPU separately; optimize or provision resources after functional correctness.

Dropped flags and combat remain deferred. Previous cached-route falls, legacy
prediction discrepancies and C11 ground fall remain independent open issues.
See the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) and
[preceding descent record](STRAFE_MOVEMENT_GROUND_DESCENT_2026-09-14.md).
