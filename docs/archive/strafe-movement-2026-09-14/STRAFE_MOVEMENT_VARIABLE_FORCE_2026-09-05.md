# Movement with changing force resources

Owner-approved priority correction, 2026-09-05. This extends M3a and precedes
the eight-player expansion. Aiming and combat integration remain paused.

Current continuation: the shared hop kernel now has an opt-in production
supported-path adapter, with 30 passing solo resource/expiry trials and 60
native captures. See the [live integration protocol and limits](STRAFE_MOVEMENT_SUPPORTED_LIVE_2026-09-05.md).
The measurements below retain the earlier probe development history.

## Required behavior

The movement controller must travel usefully without Speed, preserve actual
momentum when Speed expires, and choose jumps or alternate paths affordable
with the current force pool. No-Speed and zero-force are separate cases:
Levitation may remain available after Speed expires, or its reserve may also
be depleted. Starting from rest and inheriting fast airborne motion also need
separate checks. Existing Speed routes are geometry proposals, not proof that
their jumps remain feasible under these conditions.

The owner's match example includes Speed plus Absorb outbound, expiry during
the return, ordinary strafing to capture, and a possible tactical selfkill.
Other powers, TE, absorbed attacks and a missing own flag can change the
resources or objective during either leg. Record this context without adding
combat or forcing this sequence into every trial. Selfkill may be a later
tactical decision; it must not substitute for no-Speed traversal or conceal
failed movement. Keep the current no-selfkill movement qualification policy.

## Smallest experiment sequence

1. Verify an ordinary no-power strafe-jump primitive against native Pmove.
   This isolates the acceleration/control foundation from route selection.
2. Exercise repeated short hops with the actual saber/Levitation-3 loadout,
   Speed inactive, and initial force pools 0, 20 and 50. Compare with walking
   over the same supported corridor. Require actual acceleration, repeated
   supported landings, no unintended power activation, and native resource
   accounting. Freeze the corridor/endpoints and time gates before tuning.
3. On that same corridor, let native level-3 Speed expire during ground travel
   and during an airborne segment. Set the remaining duration only in initial
   setup; do not clear active powers or velocity mid-test. Log both sides of
   expiry, commands, speed, force, landings and continued progress.
4. Extend to a useful Kejim route in both directions, including flagstand to
   flagstand. Identify which segments need different takeoffs, lower-cost hops
   or a different path. Do not simply remove the Speed gate and assume a
   Speed-qualified jump remains reachable. Compare matched-resource planned
   references; report waiting separately from travel.
5. Repeat pickup/return/capture with variable resources and natural expiry,
   then expand moving-player/flag-state tests and repeat affordability checks.

These are development stages, not completed qualification gates. Stop and
replan at a specific unsafe maneuver when needed, while supported no-Speed
travel remains the default useful fallback. Do not require a full force pool
merely because the next task is flag travel.

## First measurement

Reused the existing no-power primitive fixture on the current compiled build,
without C changes: `.codex-research/strafe-no-speed-primitive-baseline-20260905/`.
Four native game seconds, 40 Hz, fixed 8 ms floating Pmove, base speed 250,
gravity 800, no active/known force powers, WP_BLASTER and one ordinary jump.
All 160 native samples match one-step prediction; independent feedback and
offline forecast agree on movement. The run records 27 airborne frames and
one landing, with no unsupported samples. Full-state forecast equality is
false for recorded regeneration-debounce/event bookkeeping; movement equality
is the passed gate.

This confirms an available physics/control starting point. It does not prove
repeated hopping, saber/Levitation-3 behavior, zero-force route traversal,
Speed-expiry recovery, or a production no-Speed controller. The current
production route adapter still requires Speed.

## Stages 2–3: repeated hops and native expiry passed

The fresh `strafe-build-hops-pulse-20260905` build adds a pure shared
`Bot2_HopCommand` kernel, currently called only by the corridor probe. It
pulses jump for one grounded command and releases in the air, using the
existing velocity controller. It does not hold Levitation or activate Speed.

Frozen corridor: start (0,0,24.125), goal (2400,0,24.125), broad supported
floor. Arrival requires distance <=32, speed <=80 and grounded. Predeclared
hop gates: at least three landings, peak speed >=300, arrival within 85% of
matched walking time. Native saber/Levitation 3/Speed 3, 40 Hz server, fixed
8 ms floating Pmove. All 30 twelve-second trials pass (14,400 samples).

| Condition, three clock phases each | Hop arrival | Walking arrival | Hop landings |
|---|---|---|---|
| Speed inactive; initial force 0, 20 or 50 | 5.450 s | 9.650–9.675 s | 6 |
| Initial force 20; Speed has 400 ms remaining | 4.575–4.700 s | 9.400–9.425 s | 5 |
| Initial force 20; Speed has 1,400 ms remaining | 4.225–4.375 s | 8.700–8.725 s | 4 |

No-Speed peak speed is 605–612 units/s, with roughly 44% less arrival time
than walking. The first two jumps of each initial-zero-force trial occur
at zero force. Native regeneration resumes after the initialized one-second
debounce; force is not permanently clamped to zero. No trial spends force,
fires, activates another power, dies or violates clock checks. Ground controls
expire grounded; all six hopping expiry cases expire airborne and retain
horizontal momentum (479–735 before, 482–736 units/s after). Ground controls
slow under native physics. No mid-run state reset or forced power clear occurs.

Evidence: `.codex-research/strafe-hop-pulse-audit-20260905/summary.json`.
The audit checks case inventory, capture/source hashes, loaded module path,
current sources against the build manifest, resources and matched walking
gates. Module SHA256:
`43597a7d672e3f0510c1c3903a4259e300736612081d59a24ee0dbe133177ea9`.
Frozen geometry/plan: `.codex-research/strafe-hop-corridor-20260905/`.
Native recordings: `.codex-research/strafe-hop-pulse-matrix-20260905/`.

The failed initial smoke remains in `strafe-hop-smoke-20260905/` and its
analysis directory. Its jump input of 1 was below Pmove's threshold of 10;
it never jumped and failed the travel-time gate. The corrected pulse is 127.
Geometry and acceptance gates were unchanged.

Next is stage 4: supported Kejim traversal in both directions, explicitly
including flagstand-to-flagstand. This corridor result does not qualify gaps,
ledges, corners, ceilings, high jumps or disturbed arrivals. Production Speed
gates and force waits remain until usable resource-aware map traversal is
demonstrated. Live cost and human-like execution also remain unqualified.


## Stages 2-3: repeated hops and native expiry passed

The shared Bot2_HopCommand kernel now passes all 30 native corridor trials
(14,400 samples), using saber, Levitation 3, 40 Hz server and fixed 8 ms Pmove.
It pulses jump for one grounded command and releases in air, without power
activation or sustained Levitation. It is currently called only by the probe.

Frozen corridor: (0,0,24.125) to (2400,0,24.125), broad supported floor.
Arrival requires distance <=32, speed <=80 and grounded. Predeclared hopping
gates: at least three landings, peak >=300 and <=85% matched walking time.

| Condition, three clock phases each | Hop arrival | Walk arrival | Hop landings |
|---|---|---|---|
| No Speed, initial force 0/20/50 | 5.450 s | 9.650-9.675 s | 6 |
| Force 20, Speed remaining 400 ms | 4.575-4.700 s | 9.400-9.425 s | 5 |
| Force 20, Speed remaining 1400 ms | 4.225-4.375 s | 8.700-8.725 s | 4 |

No-Speed peak speed is 605-612 units/s; arrival takes roughly 44% less time
than walking. Each initial-zero-force trial jumps twice at zero force.
Native regeneration resumes after the initialized one-second debounce;
force is not permanently clamped to zero. No trial spends force, fires,
activates another power, dies or violates clock checks. Six walking controls
expire grounded; six hopping runs expire airborne and continue to arrival.
Airborne horizontal speed across expiry is 479-735 before, 482-736 units/s
after. Ground controls slow under native physics. There are no mid-run state
resets or forced power clears.

Audit: `.codex-research/strafe-hop-pulse-audit-20260905/summary.json`.
Checks include exact case inventory, capture/source hashes, loaded module
path, current sources against build manifest, resources and matched controls.
Build: `strafe-build-hops-pulse-20260905`; module SHA256:
`43597a7d672e3f0510c1c3903a4259e300736612081d59a24ee0dbe133177ea9`.
Frozen plan/map: `.codex-research/strafe-hop-corridor-20260905/`.
Recordings: `.codex-research/strafe-hop-pulse-matrix-20260905/`.

The failed initial smoke remains in `strafe-hop-smoke-20260905/` and its
analysis directory. Jump input 1 was below native Pmove's threshold of 10;
it never jumped and failed the time gate. Corrected pulse: 127. Geometry and
gates stayed unchanged.

Next: stage 4, supported Kejim traversal in both flagstand directions with
matched-resource references. This does not qualify gaps, narrow ledges,
turns, ceilings, high jumps or disturbed arrivals. Production Speed gates
and force waits remain until map traversal is demonstrated. Live cost and
human-like execution also remain unqualified for this kernel.


## Stage 4 development: Kejim supported-path baseline

Native surveys between the actual flagstands found 18 path points outbound
from red and 17 from blue, with 243/244 support samples spaced at most 32
units apart. All samples had floor support and at least 100 units of vertical
body clearance. Four short standing sweeps in each direction contacted the
flagstand elevation transitions. The controller walks these segments.

New opt-in native probe modes 4 (walking) and 5 (hopping) follow this path,
starting on the traced own-flagstand floor with zero force and Speed inactive.
The fixture obtains its path from the native navmesh, validates sampled floor
support, and permits hopping only on level segments with standing sweep and
headroom clearance. It slows below 80 units/s within 24 units of each corner
before advancing. The shared hop kernel handles commands; all subsequent
movement, resources and flag pickup use native game code. No production
route adapter or shared bot state was changed for this experiment.

Initial phase-0 results:

| Direction | Walk arrival | Hop arrival | Hop pickup |
|---|---|---|---|
| Red stand to blue stand | 32.875 s | 27.925 s | 27.850 s |
| Blue stand to red stand | 32.750 s | 27.800 s | 27.700 s |

Each hopping run submits 12 single-frame jump pulses. The walking controls
submit none; their 3-4 recorded landings are small terrain drops, so landing
count alone must not be interpreted as jump count. Hop peak speed is 530-535
units/s. All four runs arrive, pick up, stay alive, spend no force and activate
no powers. Native regeneration is allowed after the initial debounce.

Evidence: `.codex-research/strafe-hop-kejim-geometry-20260905/` and
`.codex-research/strafe-hop-kejim-path-20260905/`; audit in
`.codex-research/strafe-hop-kejim-path-audit-v2-20260905/summary.json`.
Fresh build: `.codex-research/strafe-build-hop-kejim-path-20260905/`.

This is a useful map traversal baseline, about 15% faster than walking that
same path. It is not yet a matched-resource planned/TAS comparison or full
stage-4 qualification. Static support samples do not certify the complete
moving body envelope through a hop or changing obstacles. The probe prepares
the path at setup; production caching, cost, replanning and continuous
pickup/return/capture integration remain open. Existing production force waits
remain. Next: reduce unnecessary corner braking under verified clearance,
measure a matched-resource planned reference, then integrate a bounded live
path follower. Preserve the conservative path as a fallback and comparison.


Clock-phase-6 confirmation also passes all four cases. Red walking/hopping:
32.950/28.525 s; blue: 32.775/27.750 s. Together the two phases provide eight
successful native pickups, no deaths and no force expenditure across 19,200
samples. The hop time benefit is 13.4-15.3% against matched walking. Phase
sensitivity remains visible (red hopping changes by 0.600 s); this is not a
full eight-phase or disturbance qualification. Confirmation audit:
`.codex-research/strafe-hop-kejim-path-phase6-audit-20260905/summary.json`.
