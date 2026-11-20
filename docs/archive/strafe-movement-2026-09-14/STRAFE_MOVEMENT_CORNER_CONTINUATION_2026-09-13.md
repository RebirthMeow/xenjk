# Intermediate corner continuation

Owner execution, 2026-09-13, Astra without subagents. Continues the
[return acquisition investigation](STRAFE_MOVEMENT_RETURN_ACQUISITION_2026-09-13.md).
No installation; experimental flags remain off by default.

## Idle attribution and implementation

Retained `basegoal-chain` samples show 1.25–1.75 s of zero-input local navigation
near `(1217,1765,1368)`, just before the next mesh corner. Recovery later advances
the runner. The objective remains the enemy base throughout: this is not waiting
for a flag transition. The published sampled-idle total also includes low XY
speed during a jumppad ascent; it is not a pure stationary-ground metric.

The first experiment, `bot_navCornerArrival`, removes the extra stopping radius
at supported intermediate waypoints. It fails: the runner stops closer to the
same corner, trips worsen 34.175→37.375 s in both repetitions and sampled idle
rises 2.000→2.500 s. Keep this switch off. Do not remove the failed evidence.

Inspection of `NavMesh_GetNextWaypointEx` explains why reducing the radius is
insufficient: the LOS hull trace targets a mesh-height point and can hit floor,
returning the first corner again. Bot2's verified ground motion can reach that
corner without the query advancing it. The existing alternative also requires
an entire following path point to be within 96 units, missing longer clear legs.

`bot_navCornerContinue` adds a Bot2-owned alternative. On world ground, within
24 XY units of the current waypoint and more than 64 units from the objective,
it queries the existing path continuation. If the previous path point is within
24 XY units and the next is farther than 96 units, it considers only a 96-unit
prefix toward that next point. The existing native ground corridor checks every
floor sample, full hull sweep, step clearance, body, liquid and hurt trigger.
Cached alternatives are rechecked against current collision each frame. A
failure keeps the existing fallback and passing-body wait. No waypoint skipping
through unverified geometry, Force purchase, coordinate special case or native
flag-timer change is introduced.

## Affected native measurements

Evidence: `.codex-research/idle-followup-b53d672dfcac47499728a024e61c9627`.
`idle-samples.json` retains the preceding-build attribution. `basegoal` is the
failed arrival-radius trial. `continue-basegoal` and `continue-dropped` compare
corner continuation off/on with partial spans, step join and chain-preserving
tap timing enabled in both arms. All setup phases and failed outcomes remain
visible in the declarations, provenance and native logs.

| Case | Continuation off | Continuation on |
| --- | --- | --- |
| Base goal, both repetitions | 34.175 s | 32.150 s |
| Base goal sampled idle, both repetitions | 2.000 s | 0.250 s |
| Base goal sampled walking, both repetitions | 12.250 s | 11.950 s |
| Dropped flag, repetition 1 | 49.250 s | 42.700 s |
| Dropped flag, repetition 2 | 49.250 s | 44.625 s |
| Dropped flag sampled idle | 6.500/6.500 s | 4.500/4.750 s |

All eight worlds complete and capture alive, health floor 90. The corner stop
is gone in both base-goal cases; native continuation records bind the movement
to the checked destination. Dropped-flag walking is mixed: 18.725/18.250 s versus
16.850/19.175 s. Native flag return remains active; neither the earlier slower
pair nor this mixed walking result is excluded. Missing sampled time remains
explicit (25–250 ms), and these totals are not full qualification.

## Verification scope

Current build: `.codex-research/strafe-build-nav-continue-b53d672dfcac47499728a024e61c9627`.
Failed radius-only build: `.codex-research/strafe-build-nav-arrival-b53d672dfcac47499728a024e61c9627`.

Compiled local-navigation checks pass: 2900 reentry durations and lifecycle
boundaries; existing body/wall sidestep cases; four rotated bounded continuations;
legacy disabled behavior; distant-corner and wall rejection; immediate body
refusal when a cached corridor changes. Twelve play-seed/pose tests pass.
These mocks do not award native movement or pressure qualification.

The defended follow-up uses four normal-speed 180 s worlds, two native enemies,
with no pose/resource/body-proxy setup. The integration declaration retains the
unchanged twenty return cases across three repetitions, 26 body compatibility
cases and 30 supported/flight resource cases. The resource runtime enables the
new continuation; it does not run fast return. Historical controls and runtime
scope differences remain explicit. Completed results and bindings are below.

## Defended discovery boundary

Four normal-speed 180 s worlds complete. Red is identical at three captures,
4.1% idle and 32.8% walking; blue changes from three to four captures, idle
2.6→3.0%, walking 30.2→25.7%. No audited confinement occurs. Red has two runner
deaths including one falling death in each arm; blue has three/two deaths and
no falling deaths. These are retained discovery outcomes, not causal estimates.

None logs `corner_continue`, so these games do not establish direct continuation
coverage under enemy pressure. The option also rechecks cached alternatives in
the existing refusal path; divergent combat/setup trajectories are not isolated
by this comparison. Controlled clear/body tests at the reproduced corner are
declared separately. `verification.json` binds current native sources and both
staged binaries in the twelve base-goal, dropped-flag and defended worlds.

## Controlled body refusal and collection setup

`corner-body` compares clear (`r1`) and a world-lifetime stationary body proxy
at `(1231,1810,1368.125)` (`r2`), continuation off/on. The obstruction is in the
previously continued corridor. Both blocked worlds refuse with `body=1`, use
the existing wait/recovery, and capture in 35.525 s with identical sampled idle
and walking totals. No blocked-world `corner_continue` fires. Clear trips improve
34.150→32.550 s and idle 2.000→0.250 s. All four capture alive, health floor 90.
`verification-final.json` binds current native sources and staged binaries in
these sixteen completed affected worlds. Cache interception by a moving body is
covered by compiled checks; the native controlled proxy is stationary.

The first wider `integration` attempt is interrupted after native bind collisions
and wrong-server RCON authentication. Logs show `WSAEADDRINUSE` and automatic
neighbor-port fallback. Windows reports UDP dynamic range 49152–65535. Retain
that directory and its `interruption.json`; do not score empty setups as movement
outcomes. `integration-lowports` restarts the complete declared matrix on unique
ports below that range. The play runner now requires its creation acknowledgement
even without a decision seed, and never retries that non-idempotent command.

## Remaining dropped-objective idle

`remaining-dropped-idle.json` isolates the candidate's later stop near
`(1925,5451,1772)` before the native flag return. This is a different boundary:
the actual ground corridor reports `floor_missing` toward `(1919,5404)`, and
supported preparation also reports missing floor farther down the route. Recovery
makes a short checked step before the objective changes. Do not interpret that
refusal as another waypoint-radius defect or bypass it with unchecked walking.
Inspect an admissible descent/route continuation from that retained state, while
preserving the flag timer and the native floor/clearance contract. The case still
mixes a controller refusal with a timed objective change; retain both facts.

## Completed integration and handoff

`integration-lowports` completes all three twenty-case return repetitions:
60/60 captured alive, health floor 90. The 26/26 body compatibility checks pass;
all 30 supported/flight resource cases complete, with zero deaths and unchanged
source reports. `integration-lowports/verification.json` verifies 1105 native
source hashes, both build binaries, option settings and staged binaries across
all 116 worlds. The sixteen affected worlds have their separate final binding
record above. No native test world remains running from these drivers.

First-return comparison with retained guard-only controls is effectively neutral:
mean delta -0.0017 s, 27 slower time pairs, 16 worse idle fractions and 30 worse
walking fractions. T5 remains 0/8 in each repetition. All time pairs, missing
observations and end censoring remain in the unchanged audits; no full movement
qualification is claimed. Largest retained time counterexamples are R3-blue
repetition 2 and R1-blue repetition 3 (+0.925 s each). Compare options on the same
current build before assigning those historical/phase differences to a policy.

The nearby-corner stall has a measured repair; the radius-only trial remains
rejected. Continue with the return counterexamples and the later missing-floor
descent boundary, not another radius increase or a timer-specific wait. Keep
all experimental switches off outside declared evaluation and follow the
[current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md).
