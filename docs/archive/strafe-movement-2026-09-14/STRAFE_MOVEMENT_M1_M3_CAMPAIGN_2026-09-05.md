# M1â€“M3 execution campaign

The owner authorized completing M1â€“M3. The acceptance thresholds in
`STRAFE_MOVEMENT_PROJECT_PLAN.md` remain unchanged. Work is movement only.

## M1

Keep the frozen 24 native spawn poses and two flagstand starts. Screen candidate
guidance in native XenJK before including it in the shared catalog. Old Unreal
tapes are suggestions, with original hashes and adaptations recorded; they are
not native reference times. Preserve failed imports and failed runs.

Qualify every start against a frozen successful native planned command tape,
using the same starting pose, heading, physics, resources and actual pickup
endpoint. Run all eight phases under identical catalog bytes. Re-run the
original twelve cases after controller changes.

Fixture resource clarification during pacing validation: explicitly set health
to 100 at solo setup, as well as force 100. Previously native spawn overheal
decayed during the launcher preamble, so initial health varied with wall-time
pacing. This makes future comparisons stricter and reproducible; preceding
traces retain their actual health values. All live collision remains native.

## M2: suite declaration before recovery tuning

Apply the following construction to every M1-qualified start. Freeze the exact
resulting cases and reference hashes before running disturbance experiments.
Do not select event times from disturbed trajectories.

- Four offsets: (+32,0), (-32,0), (0,+32), (0,-32), applied once at setup.
  Native stationary bounding-box and floor traces must validate the resulting
  pose. Report infeasible starts and their trace results separately.
- Twenty-four impulses: each of (+50,0), (-50,0), (0,+50), (0,-50),
  (+150,0), (-150,0), (0,+150), (0,-150), at three clean-reference times.
  Use the qualified nominal runtime trace for event timing; use the planned
  reference only as the speed denominator. Takeoff is the first grounded
  positive jump command. Mid-flight is the
  midpoint of the longest airborne interval. Approach is the first frame
  inside 512 horizontal units of the enemy flag after halfway through the run.
  Round each event time up to a 25 ms command boundary; preserve coincident
  events as distinct named cases.
- One skipped jump: measure the first grounded launch's positive-up episode
  duration in the clean reference. Suppress jump for that frozen duration from
  the disturbed run's first actual grounded positive jump command. Then permit
  fresh attempts. This avoids suppressing retries forever when the controller
  correctly notices that the first launch never happened. Suppression changes
  submitted input only; it does not edit airborne state.
- One blocker: a declared stationary player-sized solid body, centered 64 units
  ahead of the start along its clean initial travel direction, for 500 ms from
  the first movement command. Validate placement against native world collision
  and record whether the moving bot actually contacts it or its native body
  sweep detects it while the fixture body is present. Detection followed by
  avoidance is a valid response; a case with neither contact nor detection
  cannot earn blocker-recovery credit. Solo actor exclusion keeps the fixture
  body as the only additional body in these trials.

Development uses phase 0 and the above event times. Evaluation uses phase 4,
with impulses delayed by 25 ms and blocker duration 600 ms; the same offsets and
skipped-launch definition remain. Freeze both lists before development tuning,
and run evaluation after tuning. Development results cannot substitute for
evaluation. Evaluate the plan's aggregate and per-family gates separately;
  record deaths, timeouts, infeasible placements and absent blocker detection.

## M3

Use actual native bot entities with independently owned movement state, normal
collision and native command integration. Stage eight bots at varied Kejim
starts, then stress sixteen. Include route acquisition, ground guidance and
disturbance recovery in the measured window. Count dispatches per frame and
report deaths or stationary bots so a cheap inactive workload cannot pass.

Measure all movement dispatches together per server frame, separately from
native game-frame execution and diagnostic file writing. Retain raw frame
samples, settings, source/binary hashes and host information. The eight-bot
gate remains movement p95 <=2.5 ms and maximum <=5 ms at 40 Hz. Report full
server-frame timing and the scope of its instrumentation explicitly.

## Status

Execution in progress; none of M1â€“M3 is complete.

Working revisions after the eighteen-entry qualification:

- Eight real-time/accelerated pairs matched every recorded movement field,
  including health, commands, force, position, velocity and pickup time
  (`strafe-pacing-equivalence-v2-20260905`). The outcome-test option uses
  `timedemo=1`, `fixedtime=25`, `timescale=1`: one ordinary 25 ms native engine
  frame per iteration, with faster wall pacing. Native metadata verifies these
  controls. Cost tests explicitly retain real-time pacing.
- Full four-start development recorded 116/120 pickups, but mid-flight
  impulses were 28/32 (87.5%), below the family threshold. A touchdown
  detector correction fixed the original four failures; a fresh mid-flight
  subset then recorded 30/32, with two different timeouts. The separate
  evaluation partition remains untouched.
- New lower-spawn entries walk to the native base jump pads, preserving force.
  All twelve respawns and both flagstands now have nominal pickups on candidate
  revisions. This does not add final qualified coverage: full reference/phase
  requalification on the final controller/catalog is still required.
- Final-approach experiments retained a failed broad-braking revision (too
  slow). The current proposal walks the supported goal surface and sheds
  excess ground momentum there to avoid being launched above the flag by a
  ramp. Corner projection now advances past a completed segment when the
  adjoining segment shares the closest endpoint; native revalidation is pending.

The qualification results below belong to their named, frozen builds. They
must not be read as a completed gate for later working controller revisions.

Latest combined qualification (`strafe-m1-core-six-qualification-20260905`)
passed all six entries with the profiled build, one six-guide catalog and
unchanged source hashes:

| Start | Native planned | Nominal runtime | Worst phase |
|---|---:|---:|---:|
| red respawn 2 | 6.600 s | 6.750 s | 6.900 s |
| red respawn 3 | 6.925 s | 6.950 s | 7.075 s |
| blue respawn 2 | 6.925 s | 6.975 s | 7.050 s |
| blue respawn 3 | 6.600 s | 6.775 s | 6.825 s |
| red flagstand | 9.425 s | 9.425 s | 9.600 s |
| blue flagstand | 9.525 s | 9.525 s | 10.450 s |

Blue flagstand uses its own native recorded/replayed command reference; the
earlier mirrored reference died and remains rejected. All twelve initial
spawns subsequently picked up in nominal screening (8.950â€“9.125 s) under one
expanded catalog. Full eighteen-entry qualification subsequently passed under
identical expanded catalog bytes (`strafe-m1-eighteen-qualification-20260905`),
with unchanged source hashes. This qualifies sixteen of twenty-four spawn
poses and both flagstands. The center jump-pad proposals then picked up from
red respawn 5 in 11.600 s and blue respawn 5 in 11.500 s in nominal screening;
those still require phase/reference qualification. Six other respawn entries
remain failed.

The frozen development suite's skipped-jump and temporary-body cases passed
on all four previously qualified respawn entries (8/8, 7.125â€“7.300 s;
`strafe-m2-skip-body-screen-20260905`). All interventions were verified and
sources remained unchanged. This is a subset result, not M2 completion.

Cost attribution with 26 entries found 12.3853 ms of the 12.477 ms first-frame
peak was immutable catalog loading. `BotAILoadMap` now preloads that asset;
live route selection, bounded floor preparation, steering and recovery remain
inside movement timing. Mid-match catalog changes still incur reload cost.

The isolated precache eight-bot run measured movement p95 0.4344 ms, maximum
0.7218 ms; full server p95 0.8478 ms, maximum 1.4542 ms
(`strafe-m3-eight-bot-precache-analysis-20260905`).
The completed sixteen-bot stress run measured movement p95 0.9198 ms,
maximum 1.9633 ms; full server p95 2.1052 ms, maximum 3.1003 ms
(`strafe-m3-sixteen-bot-native-setup-analysis-20260905`). Both preserve native
40 Hz commands and verified staged modules. These are provisional cost results:
unfinished routes still cause deaths and stopped bots, so M3 is not complete.

The sixteen-client fixture required reliable-command acknowledgement while
paused, complete multi-packet Rcon status collection, and native ClientSpawn
initialization with participants unlinked until their distinct test poses were
set. This prevents preamble spawn occupancy from corrupting starting health.
All earlier incomplete launch attempts remain retained. Native collision is
enabled for all participants before the settling and measurement windows.

Earlier isolated eight-bot evidence (`strafe-m3-eight-bot-profiled-analysis-20260905`):
movement p95 0.3196 ms, maximum 6.0672 ms; full server p95 0.7695 ms,
maximum 6.417 ms. Native settings, staged DLL and command clocks verified.
The first client's cold dispatch dominates the peak. Deaths and stopped bots
still prevent treating this as the required representative workload. Sixteen
bots remain unmeasured.

The following results preserve earlier campaign revisions and failures.

The landing/run-up revision qualified four spawn entries under identical
four-route catalog bytes:

| Start | Native planned | Nominal runtime | Worst phase |
|---|---:|---:|---:|
| red respawn 2 | 6.600 s | 6.800 s | 6.875 s |
| red respawn 3 | 6.925 s | 6.925 s | 7.050 s |
| blue respawn 2 | 6.925 s | 6.950 s | 7.025 s |
| blue respawn 3 | 6.600 s | 6.800 s | 6.800 s |

All twelve original red cases still picked up under that controller: nominal
6.650 s and worst phase 6.800 s. Its commands changed, so the historical exact
trace comparison reports differences; do not describe it as an exact match.

The first red flagstand proposal picked up in 11.250 s but failed phases 1 and
6 after running short of force. Later takeoffs and omitting a main-route hop
did not solve it; failures are retained. A low-hop approach subsequently picked
up in 9.425 s in phase 1. That candidate still needs full qualification.

The first complete eight-bot baseline recorded 800 movement frames and 831
full server-frame samples. Movement p95 was 0.4348 ms, maximum 7.0891 ms; full
SV_Frame p95 was 0.9382 ms, maximum 7.6651 ms. All eight clients dispatched and
survived, but an average of only 0.935 bots moved faster than 50 units/s.
This fails the maximum cost gate and is not a representative successful
offense workload. The peak was the first movement frame.

The working revision adds bounded lazy floor preparation, nearby grounded
route resumption after interruption, and a low-hop guide hint. The declared
500 ms blocker case improved from a 20 s timeout to a 7.250 s native pickup.
The body was detected by the native route body sweep; physical contact was
zero. Broader recovery, clean regression requalification and fresh cost tests
remain required before accepting this working revision.

Key retained artifacts under `.codex-research/`:

- `strafe-m1-red2-requalification-20260905`, `strafe-m1-red3-qualification-20260905`,
  `strafe-m1-blue2-qualification-20260905`, `strafe-m1-blue3-requalification-20260905`
- `strafe-m1-landings-red-regression-20260905`
- `strafe-m1-redstand-qualification-20260905` (failed)
- `strafe-m1-redstand-hop-entry-run-20260905` (single phase, unqualified)
- `strafe-m2-body-fixture-baseline-20260905`, `strafe-m2-body-resume-20260905`
- `strafe-m3-eight-bot-baseline-analysis-20260905`

The first disturbance suite used planned-command event times. Before recovery
tuning, version 2 froze event times from each clean runtime trace instead;
planned command traces remain the speed denominator. Both versions are retained.

### Full coverage and complete input replay (06:55 UTC)

`strafe-m1-full-corners-qualification-20260905` completed all 208 runtime
trials alive with continuous route ownership. Twenty-three of 26 starts passed
the complete M1 gate. Red respawns 0/1 had invalid planned references; blue
respawn 2 nominal was 7.825 s against its retained 6.925 s planned reference.
No gate or denominator was relaxed.

The old FLAGTAPE2 CSV omitted generic commands and replay synthesized Force
Speed at frame 25, whereas the actual route controller activated it at frame
75 after preparation. FLAGTAPE3 now records/replays generic commands and
buttons explicitly, with FLAGTAPE1/2 compatibility. Frozen full usercmd
captures supplied the missing fields for red 0/1; extraction provenance and
axis checks are in `strafe-m1-explicit-command-references-20260905/proof.json`.
Native replays picked up in 12.000/11.875 s. Red 0 movement matches the source
exactly; red 1 has a maximum 0.030-unit position difference from the initial
ramp settling and the same pickup time. These remain generated development
references, not optimal TAS claims.

The first approach change (direct goal only in the last three segments)
cleared the retaining wall but overshot the stand, failing the red 3 and blue
2 nominal speed gates. It is retained in `strafe-m1-native-input-screen-20260905`.
The next candidate turns in the last five segments; results pending.

The five-segment approach candidate passed all nominal speeds but failed four
phase gates (red respawn 2 and red initial 0/3/4); arrival pedestal contacts
caused an extra bounce and a stuck return behind the stand. The six-segment
follow-up restored the three tested red phase gates but blue respawn 2 was
8.000 s nominal. Neither candidate was accepted. The seven-segment controller
from the 208/208 alive run is restored, retaining the complete input recorder.
`strafe-m1-blue-corridor-candidates-20260905` declares three late-corridor
clearance changes for blue respawn 2 (16/32/48 units), preserving all other
catalog members and its 6.925 s planned-reference denominator.

### Selected corridor candidate (07:04 UTC)

All three declared blue respawn 2 corridor candidates passed planned + eight
phase qualification: 16 units nominal/worst 7.200/7.200 s; 32 units
6.925/7.050 s; 48 units 6.950/7.075 s. Selected 32 for speed and clearance
margin. The reference stays 6.925 s. Full 26-start qualification is running
with this single catalog and `strafe-build-m123-corridor-20260905`.

The original 12-case M0 outcome regression completed 12/12 alive pickups with
continuous ownership using the unchanged red catalog member. Nominal 7.025 s;
worst 7.650 s (the +50 X impulse), below 120% of 6.600 s. Historical command
traces are not identical after the deliberate controller fixes; the old
`strafe_runtime_matrix` exact-trace gate remains false and is not presented
as an exact replay pass. Evidence: `strafe-m0-corridor-regression-20260905`.

### M1 complete; M2 frozen full-map development started

`strafe-m1-full-corridor-qualification-20260905` passed all 26 starts with
unchanged source/asset fingerprints. Audit and per-start table are in
`strafe-m1-map-qualified-20260905`. Native module SHA-256:
`7170b8c4b58ca678518c3162fa8553edd28fc56d311c8a54715c9b4126e456d8`.
All 26 planned references and 208 runtime phases pick up alive. Maximum
nominal ratio 1.045455 and maximum phase ratio 1.119777 meet the original gates.

Full M2 cases are frozen in `strafe-m2-full-map-frozen-suite-20260905/suite.json`,
using each accepted clean runtime trace for event timing and its planned
reference for the speed denominator. Thirty development and thirty separate
evaluation cases per start: 780 + 780. Development output is
`strafe-m2-full-map-development-20260905`; evaluation remains unrun.

### M2 full development and rejoin correction (07:27 UTC)

The full 780-case development baseline completed at 764/780. Blockers were
22/26 (84.6%), the only family below its gate. Three localhost startup UDP
resets and one lost offset setup reply also invalidated collection; all four
were rerun successfully after collector fixes. No invalid capture is credited
as a native success. Abandoned fixture processes were gracefully quit.

The private `rejoinRunup` state now gates a resumed jump on actual forward
speed and direction alignment. Ground commands rebuild the run-up; no direct
player-state correction, added world query or trajectory search. Fresh
followers leave this flag zero. The hypothesis and diagnostic velocity are
recorded in `strafe-m2-rejoin-design-20260905.md`.

`strafe-m2-rejoin-development-check-20260905` reran all 26 blockers, every
failed/invalid development case and every case that released its route: 38
trials, 30 passing. All blockers now pass. Combining these with the unchanged
non-rejoin baseline paths gives 772/780 development outcomes within budget,
with every family above 90%. This is explicitly a mixed-revision development
campaign, not a claimed full run on the new binary. Its branch-coverage rationale
and eight remaining failures are in `strafe-m2-development-campaign-20260905`.
The original frozen 780-case evaluation is still untouched and must run in full
on the final revision; M1 requalification is running first.

### Plan revision and resumed validation

Owner approved keeping M3 affordability as a bounded measurement and adding
M3a continuous movement-only play before M4 map transfer. The active plan
now records the minimum objective lifecycle to bring forward from M5; aiming
remains paused.

The first independent evaluation attempt (`strafe-m2-full-map-evaluation-20260905`)
was interrupted after the first eight worlds timed out in filesystem startup.
The final audit found that 16 later blue-flagstand cases completed successful
pickups before the dispatcher and its owned children were stopped. These valid
individual results and the eight invalid startup captures remain preserved;
the incomplete set cannot qualify M2. No movement tuning was based on these
evaluation results. A later single-server probe reproduced
the accepted red spawn 2 nominal in 6.700 s without changes to movement code.

`strafe-m3-eight-rejoin-v2-20260905` completed 800 game frames, with all eight
bots alive and dispatched throughout. Movement p95 0.3621 ms, max 0.8037 ms.
Full SV_Frame recording had only 632 samples and a 4.715-second stall outside
measured movement, so the timing capture is not accepted. The cost fixture now
buffers CSV rows and flushes them after recording ends to test/remove per-frame
diagnostic I/O as a contributor. This modifies the cost fixture only, not the
production movement controller. A fresh build and isolated rerun are required.

### Bounded affordability result

The fresh buffered diagnostic build differs from the M1-qualified build only
in `bot2_movecost.c`; all production movement sources are identical.
`strafe-m3-eight-buffered-20260905` records 800 valid game frames, all eight
clients dispatched and alive throughout. Movement p95 0.2684 ms, max 0.5636 ms;
full SV_Frame p95 2.0199 ms, max 3.1966 ms. There are 170 frames with all eight
moving, including 72 during the 2.5–5 second impulse/recovery interval. All
eight declared impulses were delivered. No route-loading work occurred in
movement dispatch. Both CSV files are flushed outside the recording interval.

`strafe-m3-sixteen-buffered-20260905` stress records 800 valid game frames.
Movement p95 0.5328 ms, max 0.8827 ms; full SV_Frame p95 3.4054 ms, max 4.7054 ms.
All sixteen move together for 158 frames, including 72 in the recovery interval.
The maximum movement frame has all sixteen moving. One bot later dies by
falling, reducing the late workload to fifteen; no sixteen-bot reliability
claim is made. Neither run is a continuous objective-play qualification.

The bounded M3 measurement passes; the hash/workload audit is
`strafe-m3-buffered-audit-20260905/summary.json`. Retain the prior 4.715-second
stall as an invalid timing capture. Its absence after buffering supports
removing diagnostic I/O as a confound but does not prove that I/O caused it.
M3 affordability remains an ongoing measure during the new M3a workload.

The accepted 26-route catalog, spatial guides, native planned references,
inventory, scorecard and byte provenance are now portable in
`tools/fixtures/strafe_flag/kejim_m1_20260905/`. Relative case paths resolve
against the cases file. Package bytes and all 52 guide/reference paths were
verified; no installed game assets were changed.

### Independent M2 evaluation: one family fails

`strafe-m2-full-evaluation-buffered-20260905` completes all 780 frozen cases
on one unchanged controller: 763 pickups (97.8205%), 17 deaths, no timeouts,
invalid captures or feasibility exclusions. Every successful trip meets its
150% reference budget. Families: blocker 26/26, approach impulse 208/208,
mid-flight impulse 207/208, takeoff impulse 196/208, offset 104/104, skipped
jump 22/26. The skipped-jump family is 84.615%, below 90%; **M2 fails**.
The complete audit in `strafe-m2-full-evaluation-audit-20260905` verifies
frozen cases/references, build/source provenance and retained M0/M1.

Skipped-jump deaths are blue respawns 0, 4 and 5, and red respawn 4. The two
upper-bridge starts walk off without a launch; blue 5 also leaves support
before suppression ends. Blue 0 makes a delayed jump, lands off the intended
corridor, then walks off the next platform. These are valid recovery failures.

The original evaluation is now promoted to development. Before controller
changes, `strafe-m2-v2-frozen-suite-20260905` freezes a new 780-case holdout:
phase 2, impulse events 50 ms after original development events, 700 ms body
blockers, unchanged offset magnitudes and skipped-jump durations. This is
an engineering holdout, not a claim of independent real-match samples.

The missed-launch candidate detects a requested grounded jump that did not
become an observed native launch and releases to the existing supported-ground
fallback/rejoin. It adds no player-state edit or world query. First screen all
26 skipped jumps from each opened set (52 trials), before M1/M0 regression,
new holdout evaluation and renewed M3 cost. No candidate result is yet credited.

### Missed-launch grace revision

Immediate release passes 46/52 opened skipped-jump trials but disrupts several
25 ms misses on initial platforms. The retained failed candidate is
`strafe-m2-missed-launch-development-20260905`. Requiring two continuously
unaccepted command frames (50 ms) lets one lost input retry normally.
`strafe-m2-missed-launch-grace-development-20260905` passes 51/52, including
all four skipped-jump deaths from evaluation v1. Red respawn 4 in original
development phase 0 times out; all successful trips meet the reference budget.

The additional 27-case opened non-skip regression screen preserves its eight
previously repaired/valid cases and nineteen known failures; no new outcome
regression. This is a targeted screen, not a full new development campaign.

`strafe-m1-missed-launch-grace-qualification-20260905` completes all 26 planned
and 208 runtime pickups. All clean starts retain continuous ownership and
M1 speed gates; maximum nominal ratio 1.045455 and worst-phase ratio 1.119778.
The qualified game module SHA is
`6589e82da88b9baaa234833d673490f1c4dec882886ff2742d0921a87d2697b8`.
Its audit is `strafe-m1-missed-launch-grace-qualified-20260905/summary.json`.
The portable catalog/guides/references are unchanged. M0 again gives 12/12
pickups with continuous ownership, nominal 7025 ms and worst 7650 ms; no
historical exact-trace claim. Cost is being remeasured before the v2 holdout.

### M1–M3 qualification complete for the declared Kejim benchmark

The fresh v2 holdout (`strafe-m2-v2-evaluation-20260905`) completes 758/780
pickups (97.1795%), 21 deaths and one timeout. All 780 captures are valid,
with no feasibility exclusions or changed sources/references. Every successful
trip meets the 150% reference limit. Families:

| Family | Pickups / cases | Rate |
|---|---:|---:|
| Start offset | 101/104 | 97.12% |
| Takeoff impulse | 196/208 | 94.23% |
| Mid-flight impulse | 203/208 | 97.60% |
| Approach impulse | 208/208 | 100% |
| Skipped jump | 26/26 | 100% |
| Body blocker | 24/26 | 92.31% |

The complete audit is `strafe-m2-v2-audit-20260905/summary.json`; every gate
passes, including M0/M1 retention and exact frozen-case coverage. Its 22
failures remain explicitly listed. V1 and v2 use different held-out conditions;
do not interpret their aggregate percentages as a paired improvement measure.
The failed v1 evaluation and both development candidates remain preserved.

M3 remeasurement on the same qualified module:

| Workload | Movement p95 / max | Full SV_Frame p95 / max |
|---|---|---|
| 8 bots | 0.3228 / 0.9031 ms | 1.9818 / 3.4369 ms |
| 16-bot stress | 0.5226 / 0.9946 ms | 3.3606 / 5.0162 ms |

All eight bots survive and dispatch for all 800 game frames. There are 171
frames with all eight moving, including 76 during the impulse/recovery
interval; fully-active movement p95/max is 0.3069/0.4354 ms. Sixteen-bot stress
has 157 fully-active frames (71 in recovery), then one falling death; fully-
active movement p95/max is 0.4746/0.7535 ms. All declared impulses are recorded.
Both CSV files are buffered outside recording. The late workload stops making
useful objective progress; that limitation is why M3a follows this checkpoint.
The M3 audit is `strafe-m3-missed-launch-grace-audit-20260905/summary.json`.

The aggregate completion record is `strafe-m123-qualified-20260905/summary.json`.
M0–M3 are complete for this defined movement-only benchmark. M3a has a recorded
baseline protocol but has not been implemented or measured. Aiming, broader
survival/force policy, intermediate-human style and another map are not claimed.
No installed game binary, asset or server configuration was changed.
