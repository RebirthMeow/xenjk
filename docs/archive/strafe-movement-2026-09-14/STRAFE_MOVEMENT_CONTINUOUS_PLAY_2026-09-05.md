# M3a: continuous movement-only play

Later owner clarification (2026-09-05): no-Speed strafing and natural Speed
expiry now precede the eight-player expansion described below. The full-reserve
wait demonstrated here is a development result, not the desired match policy.
See [variable-resource movement](STRAFE_MOVEMENT_VARIABLE_FORCE_2026-09-05.md)
and the active project plan for the revised work order.

Owner-approved plan revision: make useful continuous play the next development
checkpoint after bounded affordability, before transferring to another map.
Aiming and combat remain paused. The six-trial baseline is now collected;
continuous-play qualification remains open.

## Why this is the next experiment

The accepted Kejim controller covers 24 spawn poses and both flagstand starts.
The eight-bot movement/recovery benchmark stays within the cost budget while
all eight move, including after declared impulses. Its fixed outbound goal
does not exercise return/capture decisions. It stops being a representative
continuous workload after pickups.

Current source inspection identifies a concrete handoff gap:

- `ai_bot2_route.c` permits only `MACRO_FETCH_FLAG`, rejects a flag carrier,
  and selects guides by the bot's team. The opposite-direction flagstand
  guide exists, but a returning carrier cannot select it under that contract.
- `ai_bot2_localnav.c` also permits only `MACRO_FETCH_FLAG`. Return movement
  therefore leaves the new two-layer path and uses legacy movement.
- `ai_bot2.c` already chooses fetch/return/survival objectives, but combines
  them with combat and continuous power/weapon provisioning. Its old routing
  harness teleports and replenishes resources. Those behaviors cannot serve
  as evidence for natural continuous movement-only play.

This makes objective handoff, route entry from real arrival momentum, and
native resource availability the first things to measure. More speed tuning
or a larger precomputed route search is not justified by the current results.

## Smallest baseline

First run one offense bot for 120 seconds from native respawn 2, separately
for red and blue. Repeat each direction three times with command phases
0, 3 and 6. Use the accepted catalog, 40 Hz/fixed 8 ms physics and the same
initial health/force/power loadout as M1. Record both the initial setup and
every subsequent native resource change. After setup, allow no pose,
velocity, health or force reset; native pickups, regeneration and respawns
remain active. Do not stop on the first enemy-flag pickup.

Use the production objective decision and movement dispatch. Isolate
movement-only behavior explicitly, with no weapon firing, mine actions,
tactical suicide or discretionary non-movement force powers. Native respawn
input after a death is permitted; fixture respawn-time edits are not.
The fixture observes the bot rather than assigning a new goal after each leg.

Record objective state/target, command and player-state clocks, position,
velocity, force, health, route ownership/release/rejoin, native flag state,
pickups, captures, deaths and respawns. Buffer detailed diagnostics so they
do not pollute cost recording. Keep actual game events authoritative for
capture and flag return; proximity to a stand is not success.

A baseline stall candidate is ten seconds alive without a pickup/capture,
objective transition, or at least 128 units of spatial progress. Record its
raw trajectory and reason: waiting for a missing own flag, insufficient
resources, no supported path, body obstruction or unexplained. Moving in
circles is not automatically useful progress. This diagnostic definition
must be reviewed against the baseline before freezing qualification gates.

## Development sequence after the baseline

1. Make fetch-to-return handoff use the same movement layers. Bind route
   selection to the requested destination and valid entry conditions while
   retaining ownership and collision checks. Existing flagstand guides are
   proposals for return travel; do not assume a pickup leaves the bot at rest
   with a full force pool or at an accepted guide entry.
2. Demonstrate repeated outbound/pickup/return/capture cycles in both solo
   directions. Include natural deaths/respawns and report failures.
3. Add eight moving bots, changing carried/dropped/returned flag states and
   declared interruptions. Freeze the duration, repetition count, intervention
   schedule, valid waiting reasons and numerical progress/recovery gates before
   tuning for qualification. A combat-free two-team flag deadlock must be
   reported as such, not hidden by resetting flags or resources.
4. Retain M1/M2 outcomes and repeat M3 cost in that richer workload. Only then
   transfer this protocol to another map.

The solo baseline duration and phases above are fixed before collection.
M3a qualification thresholds remain pending review of the baseline and the
first handoff candidate. M3a is not complete.

## Baseline collected 2026-09-05

Evidence: `.codex-research/strafe-continuous-baseline-20260905/` and
`.codex-research/strafe-continuous-baseline-audit-20260905/summary.json`.
Build: `strafe-build-continuous-baseline-v2-20260905`. All six trials ran
120 native game seconds at normal pacing: 28,800 recorded command frames.

| Team | Phase | Pickups | Captures | Deaths / native respawns | Capture time (s) |
|---|---:|---:|---:|---:|---:|
| Red | 0 | 1 | 1 | 0 / 0 | 19.250 |
| Red | 3 | 3 | 1 | 2 / 2 | 69.725 |
| Red | 6 | 1 | 1 | 0 / 0 | 27.025 |
| Blue | 0 | 1 | 1 | 0 / 0 | 35.850 |
| Blue | 3 | 1 | 1 | 0 / 0 | 22.625 |
| Blue | 6 | 1 | 1 | 0 / 0 | 25.000 |

Every trial eventually stalls on the next outbound leg near the home-base
exit. All finish alive with 100 force, both flags available and no route
ownership. Thus the final stalls are not explained by missing flags or a
continuing resource shortage. The legacy return path can capture but is
inconsistent, including two deaths in red phase 3. No trial captures twice.

The new opt-in `bot_movementOnly=1` production mode chooses offense goals from
native flag entities and dispatches the existing movement controller. It is
called by `Bot2_Think` and by the observer fixture; the fixture supplies no
per-leg goals. This minimal movement-only offense policy is a new isolated
production path, not a claim that the full legacy tactics engine was tested.
The baseline preserves the accepted fetch-only route and local-navigation
gates; return therefore uses legacy movement. Default full Bot2 is unchanged.

Initial setup uses native respawn 2, health/force 100 and Speed/Levitation 3.
After setup only commands and native client processing move or replenish the
player. The native respawns visibly restore health to 125 and force to 100.
The observer records these changes; it does not normalize them. Stock bot
power bonuses may be known on later lives but are never activated by this
policy. The two respawn frames carry dead-player attack input and native
`commandTime = level.time - 100`; the audit distinguishes these from firing
while alive or ordinary command-clock failures.

The first handoff candidate enables return through the same two layers,
selects guides by destination, and approaches a nearby flagstand entry using
actual position and momentum. It waits for native resources and may deactivate
Speed while parked to allow regeneration. Its measurements are development
evidence; M0–M3 acceptance remains tied to the earlier qualified build until
regressions and cost are repeated.

### First candidate failure and reserve revision

`strafe-continuous-handoff-20260905` tested phase 0 on both teams: four pickups,
zero captures, three native deaths/respawns. The first parked approach worked
in both directions and force regenerated without fixture edits. Departing
with 82 force (32 after activating Speed), however, depleted the available
jump reserve before the final approach. The accepted full stand-to-stand
reference started with 100 force (50 after Speed); reusing the shorter-entry
minimum was unjustified. These failed runs are retained with their CSVs and
`strafe-continuous-handoff-analysis-20260905/summary.json`.

`strafe-build-continuous-reserve-20260905` restores that full departure
reserve for stand entries and checks remaining Speed duration when already
active. It also raises the approach clearance trace by the native 18-unit
step allowance, while retaining sampled floor support, so a walkable step
does not unnecessarily abandon the entry. Six normal-pacing development
trials use the same teams, phases and 120-second duration as the baseline.

### Repeated solo cycles demonstrated

After correcting the runner's one-shot setup and recording setup metadata
directly to disk, all six normal-pacing trials in
`strafe-continuous-reserve-observed-20260905` completed two captures each:
16 pickups, 12 captures, zero deaths and zero ordinary command-clock errors.
All loaded the staged DLL verified by its native module path. Audit:
`strafe-continuous-solo-reviewed-audit-20260905/summary.json`.

| Team | Phase | First capture (s) | Second capture (s) |
|---|---:|---:|---:|
| Red | 0 | 32.825 | 92.700 |
| Red | 3 | 32.825 | 89.175 |
| Red | 6 | 32.150 | 87.450 |
| Blue | 0 | 31.925 | 88.100 |
| Blue | 3 | 33.425 | 91.000 |
| Blue | 6 | 32.875 | 90.400 |

This is a solo development result, not completed M3a qualification. Stand
waits are long (roughly 15–20 seconds) but force regenerates and the bot
departs again. These runs do not measure pressure, human decision quality,
or affordability with eight moving players. The safe full-reserve requirement
is an initial supported departure contract, not a claim that waiting this
long is desirable offense behavior in a match.

Three earlier reserve attempts were invalid fixture attempts, retained in
`strafe-continuous-reserve-20260905`: red phase 0 exited during filesystem
startup; red/blue phase 6 were aborted by a repeated start command after a
delayed/missing Rcon acknowledgment. They are not controller deaths or
successful trials. The observer now arms once and persists setup metadata.

An observability correction follows this six-trial result: the early CSV
`red_flag`/`blue_flag` fields described linked entities, which can remain
linked after pickup. Pickup/capture counters and trajectories remain usable,
but those fields do not prove flag availability. Schema 2 reads the native
CTF flag-status configstring, and the production goal policy checks item
visibility and trigger contents before treating a flag as available. This
must be verified before the multi-player flag-lifecycle campaign.

The final `strafe-build-continuous-native-flags-20260905` build completed
two more 120-second runs, phase 0 on each team: two captures per run, no
deaths, and native flag status consistent with player flag possession.
The commands and recorded movement/resource states match the corresponding
six-trial revision exactly after excluding absolute clocks and the corrected
flag-state fields. Evidence: `strafe-continuous-native-flags-20260905`,
`strafe-continuous-final-audit-20260905`, and
`strafe-continuous-final-comparison-20260905/summary.json`.

The same final build retains all 12 original pickup outcomes with continuous
ownership: nominal 7.025 s, worst 7.650 s. Evidence:
`strafe-m0-continuous-native-flags-20260905/summary.json`. Historical exact
trajectory comparisons remain false as on the accepted controller; the
matrix exit code is not an outcome-regression failure. Full M1/M2 and the
richer M3 cost gate still need to be rerun for final M3a qualification.

Stall review distinguishes resource waits, windows that end during departure,
and unexplained motion. Red phase 6's initial "unexplained" 40–50 s window
ends just as force reaches 100 at 49.975 s; route acquisition follows at
50.025 s. It is a completed resource wait, not a persistent blocked exit.
The diagnostic still only automatically detects confined movement; larger
loops need trajectory review before qualification thresholds are frozen.

Next: collect an eight-moving-player baseline using native carried/dropped/
returned flag changes and declared interruptions, freeze its qualification
protocol, then retain M1/M2 and measure affordability in that workload. A
combat-free flag deadlock is a recorded waiting state, not a reason to grant
resources, reset flags or claim useful travel. Aiming remains paused.
