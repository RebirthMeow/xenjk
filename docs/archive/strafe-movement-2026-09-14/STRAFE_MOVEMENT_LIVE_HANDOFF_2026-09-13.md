# Live body handoff and supported walking

Owner execution, 2026-09-13. Flight continuation measured; walking candidates
are experimental and fail complete-trip improvement. No installation.

The owner took over the two immediate priorities on Astra without subagents.
Evidence root: `.codex-research/live-handoff-54d2cf7dddd34808a50be3c4c66d165f`.
Fixture build: `strafe-build-livebody-54d2cf7dddd34808a50be3c4c66d165f`.

## Retained guard campaign scoring

Completed the missing branch-window and first-return movement scoring in
`flight-contact-c9e102ce84ea41baaf1a34be628833a8/guard-verification` using
`review_score.py` and `review_movement_diagnostics.py`. Relative to the retained
background-recovery arm, all 60 pairs remain present: mean first-return time
delta +0.0433 s, 28 time regressions, 5 idle-fraction regressions, 27 walking-
fraction regressions. T5 remains 0/8 in each repetition. These return fixtures
and diagnostics do not establish the separate complete-round-trip gate.

## Live encounter method

`bot2_flightbody.c` provides an opt-in dedicated cheats-only fixture. At the
first grounded jump command for the selected flight window, it inserts one
player-sized body with declared relative position and velocity. The runner's
state, resources, clock and ordinary dispatch are untouched. The proxy advances
under gravity and hull collision in normal game ticks; it has no combat AI or
impact damage and is never teleported away or removed. Before/after native Pmove
records cover the following 15 seconds. Game collection continues afterward.

The initial four solo 600-second runs never reached window 11: they are retained
as uncovered attempts, not successful encounter tests. The subsequent four
two-enemy worlds use the same declared body, moving-away, overhead-clear and
no-body cases. Accelerated outcome runs keep native 25 ms game frames.

The first moving-body encounter occurred at level 113875, window 11/candidate 2.
The guard refused on world ground; the supported follower then refused its
stale lateral crossing and localnav issued `(0,127,0)` on that same frame.
The runner remained alive (health at least 90 in the subsequent 15 seconds),
with captures increasing from 2 to 3. This establishes useful continuation for
this live encounter, not universal safety or the full defended qualification.

## Walking decision pending

Native level-three Speed costs 50 Force and lasts 20 seconds. Supported movement
already regulates active-Speed motion through its ordinary braking and corridor
contracts, but never purchases Speed itself. Active powers exclude flight-window
preparation. Any purchase policy must therefore be measured through the whole
trip, including later flight opportunities and return resources; a faster local
straight alone is insufficient evidence.

## Completed live continuation

The four normal-speed corner worlds use a one-time native pose/velocity/resource
setup before normal dispatch, plus the declared body where applicable. All four
capture alive with health floor 90. Capture times from setup are 12.225 s for the
moving body, 10.150 s clear and 10.300 s for each moving-away/overhead control.
Only the moving body triggers the guard. Its same-frame final command is
`(0,127,0)` and it later completes window 13 before capturing. The controls
retain the window-11 jump. These are live continuations, not exact trajectory
pairs; initial phase/pose differences remain in the evidence.

The four accelerated two-enemy worlds complete 600 native seconds each, with
zero audited confinement. Only one injected encounter reaches the selected
window; the other two injection attempts are uncovered. The no-proxy world has
a natural guard event against enemy 1 at level 98375. The runner stays on the
platform but dies 600 ms later from `MOD_BRYAR_PISTOL_ALT`; this is explicitly
retained as a combat-ended continuation, not a successful capture or falling
death. World captures are 8 clear, 12 moving-body, 12 moving-away and 13 overhead;
these divergent defended worlds are not controlled estimates of guard benefit.
See `normal-corner/encounter-summary.json` and `defended/audit.json`.

## Walking experiments and rejected promotions

All switches default off. No candidate below is accepted as the walking fix.

1. `walking-baseline` / `walking-candidate`: two full-force ordinary-spawn cases
   per team, 120 native seconds each. The first straight-Speed candidate never
   purchases Speed in these worlds. All capture alive; timing differences are
   not evidence of policy benefit. See `walking-comparison.json`.
2. The retained 182225 red interval is a **partial waypoint prefix**, after a
   later floor sample failed. Whole-edge hop eligibility is false because of
   the initial slope, although later per-sample intervals allow hopping.
   `walking-prefix` reconstructs native pose, velocity, Force and dropped-flag
   objective. All four initial runs fail the rate acknowledgement before useful
   collection; retained logs show activation. `walking-prefix-retry` retains all
   four successful runs after the runner gained bounded idempotent RCON retries.
   Unbounded native Speed reduces the first edge from 9.00–9.05 s to 5.65–5.75 s,
   but complete trips worsen from 40.500/41.075 s to 43.800/43.800 s.
3. `walking-bounded`: native deactivation before leaving the purchased straight
   reduces the edge from 9.150 s to 6.000/6.150 s. Both complete trips still worsen
   from 41.075 s to 43.375 s. Speed is not the selected repair.
4. `walking-partial-spans`: preserve only the existing validated prefix through
   its unchanged endpoint, with original per-interval hop decisions. Never
   include unfinished samples or rewind an executing prefix. No Force purchase
   or relaxed hop/turn/clearance threshold. The edge improves from 9.150 s to
   6.750/6.650 s, but dropped-flag full trips worsen from 41.100 s to
   48.175/45.375 s. The dropped flag's native return timing remains active.
5. `walking-partial-basegoal`: same native initial condition, enemy flag at its
   base. Outbound pickup improves from 27.375 s to 24.900 s; complete trips worsen
   from 37.050 s to 38.375 s. Sampled walking falls from 19.200 s to
   12.150/12.000 s. All four capture alive, health floor 90. Walking/idle measures
   use CSV samples up to 250 ms, with missing time explicit; not qualification.

Each directory contains its declaration, commands, source/binary provenance,
logs and outcomes; the successful targeted arms have `comparison.json`.
Builds share suffix `54d2cf7dddd34808a50be3c4c66d165f` and prefixes
`strafe-build-groundspeed-`, `strafe-build-groundspeed-prefix-`,
`strafe-build-groundspeed-bounded-`, and `strafe-build-partial-spans-`.

## Next causal boundary

Superseded by the [return acquisition investigation](STRAFE_MOVEMENT_RETURN_ACQUISITION_2026-09-13.md):
fallback does take the first hop, without Speed, before fast return joins. The
new combined experiment improves the affected complete-trip pairs; broad
qualification and mixed defended behavior remain open. The following is the
retained observation that led to that investigation.

The base-goal control reaches pickup with Force 100, inactive Speed and about
110 u/s, then joins route 5 at index 2. It completes the first short hop and
admits the next gap at 689.2 u/s. The faster outbound candidate reaches pickup
with nearly the same resource/velocity state, joins at index 5, and refuses the
next takeoff at index 8 (`takeoff_momentum`, 492.3 u/s). It then takes branch 26.
The return leg grows from 9.675 s to 13.475 s, erasing the outbound gain.

Inspect pickup-to-join acquisition and preservation of that first short hop;
do not lower the momentum gate or exclude the slower pairs. Reproduce the
different joins from retained native states before adjusting policy. This is a
measured downstream blocker, not proof that the walking change alone is ready.
The broader 60/26/30 campaign is not rerun for a candidate already failing its
affected full-trip preflight. Existing campaigns do not qualify these experiments.

## Normal-speed defended check and final verification

`normal-defended` contains four completed 180-second native worlds, two enemies
per world, red/blue with partial spans off/on. No pose, proxy or resource setup.
Captures are 4/3 red/blue in both arms. Idle rises from 2.2/2.7% to 3.0/2.8%;
walking changes from 32.7/32.9% to 31.8/36.5%. No confinement is detected.
Partial spans activate 3 times red and 7 blue. These results also fail promotion.

Both blue worlds retain one `MOD_FALLING` death; neither has a takeoff-body guard
event. Off-blue loses health 96→65 during a supported jump, switches to localnav
and falls to the void floor before the later kill. On-blue loses health 60→7
with a knockback flag and falls from the platform under localnav. These are
combat-associated departures, not evidence of the reproduced flight-adapter
underside collision. General disturbance/knockback qualification remains open;
enemy kill credit alone is not treated as proof of a complete causal mechanism.

Final `verification.json` confirms all 1105 native source hashes, both build
binaries, and both staged binaries in each normal-speed world. The 82 mocked
flight-adapter cases, supported lifecycle/partial-endpoint/no-rewind/resource
checks, rotated path-release checks and 12 play-seed/pose runner tests pass.
No native world remains running from these drivers. No installation performed.
