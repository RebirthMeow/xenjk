# R7 defended continuity continuation

The R6 handoff `4414eddc` was fast-forwarded into local `master` on September 8.
No remote push or installation was performed. Tracked source content was clean;
untracked research, archives and unrelated local files were preserved.

## Traced R6 collection

Three blue 1x 600-game-second runs, candidate corners/flight/guard settings plus
supported tracing, on the unchanged R6 freeze-2 build. Evidence:
`.codex-research/strafe-r7-supported-trace-898886919d374c2db50fc15fd0456625/`
(`launch.json`, per-run provenance/CSV/server log/summary,
`opposition-audit.json`, and `inspect_trace.py`). All three completed, roles and
rate confirmed, with clean native shutdown. These are diagnostic observations,
not an independent twelve-case continuity qualification or timing evidence.

| Run | Distant confinement | Trace attribution |
|---|---|---|
| blue 1x r1 | 5.0 s, level 392375..397375, near (3019,3797,1368) | Localnav 11 samples, supported 2, recovery 8. No live enemy within 853 units. Repeated floor-check refusal on the same future path segment, interleaved with fallback. |
| blue 1x r2 | 5.25 s, level 316025..321275, carrying near (145,-1232,1916) | Supported 12 samples, recovery 5, localnav 5. Live defender approaches 30 units; commanded movement becomes blocked, floor validation refuses, then preparation is reacquired. |
| blue 1x r3 | none | Complete retained observation; absence here does not close the historical failure. |

The original isolated supported wait at level 217450 was not reproduced.
Source review found two unbounded ownership paths: preparation can brake at
the current/temporary endpoint without expiry; the prepared follower's
`ARRIVED` result is ignored. Neither is yet proven to be the original episode's
cause. The new stalls span controller handoffs, so a single-owner timer alone
cannot be claimed to fix defended continuity. Trace and reproduce before
selecting the controller correction; preserve airborne adapter ownership.

The first attempted collection was refused before native launch because Git
checkout changed line endings in 83 build-listed files. Every mismatch was
verified as newline-only against the retained source snapshot and restored to
the exact frozen bytes. Failed launcher logs remain in
`.codex-research/strafe-r7-supported-trace-4af806da73074ad8a56229a6974d5453/`.
No provenance check was bypassed.

## Repetition correction

The campaign now declares distinct pre-actor seeds shared across arms, requires
native acknowledgement and provenance agreement, verifies helper hashes before
launch, and rejects duplicate complete runner recordings within each cell.
The seed is applied once before actor creation in a private dedicated world;
ordinary play retains its existing initialization. This is controlled stream
diversity, not a claim of deterministic paired matches or map-load seeding.

Verification:

- Python seed/campaign suites: 16 checks pass; retained test artifacts include
  `.codex-research/continuity-seed-tests-2q0qv9hn/`.
- Extracted native seed parser/admission: 54 mocked cases pass in
  `.codex-research/play-seed-check-c4731995b2f74b6a87c028cf0eff228e/`.
- Three blue 5x 600-second observations on
  `strafe-build-r7-seeds-0b14256759d54a4ebdab049111d440f6` acknowledged seeds
  2026090810..2026090812 at native level 1075 and produced distinct tapes.
  All complete, roles/rate confirmed, clean shutdown, no distant confinement.
  Evidence: `.codex-research/strafe-r7-seed-native-223e5b5ae02a432a90297acaa0b93475/`
  (`seed-validation.json`, `opposition-audit.json`). This is a seed verification
  cohort, not full continuity qualification. Its unseeded lower-corner probe
  also had no confinement; the clean pose did not reproduce the live stall.
- A repeated seeded `addrunner` request during a live native solo run was
  refused before another seed or actor was queued:
  `.codex-research/strafe-r7-seed-admission-97c66b5ef9864291a526da2dc7f956cb/reseed-rejection.json`.
  An earlier attempt after the completed three-run world had closed received
  no response; that failed check is retained in the seed cohort directory.

## Supported ownership wait contract

The supported controller now names and bounds preparation and temporary-prefix
arrival waits at three seconds, matching the existing follower stall interval.
A complete endpoint allows 250 ms (ten native ticks) for objective publication,
then yields to normal dispatch if still arriving. More than 16 units of motion
restarts the wait; compiler progress alone does not. Airborne commands and
commands owned by either flight/connection adapter do not expire through this
guard. New timeout handoffs restore the incoming command and use the existing
rejection/backoff. No resource, objective or native lifecycle reset was added.

Fresh frozen build:
`.codex-research/strafe-build-r7-waits-a4a18cf49d5e4abe97faf39a578bd1da/`.
Game-module SHA-256:
`ed71f19f182669894a23f89a48f0eee568e0cacd941c594d411b8fb57e95c592`.
Source-manifest SHA-256:
`3300f21e8a8295abf17899ec225b219be90041e746052bda4f530983d156a121`
(`20260908T111635312128Z.sources.json`). The build precedes the implementation
commit, so its embedded Git version still names `4414eddc`; exact source and
loaded-module hashes bind the R7 measurement, not that older version string.
Focused verification:

- Full extracted supported dispatch/wait harness passes:
  `.codex-research/supported-wait-check-b269b81bf4c4432191065d3ccbb57dcb/`.
  This is mocked ownership evidence, not a reproduced native three-second wait.
- Existing adapter 82, guard 17, movecontrol 200000 equivalence, and flight
  checks pass in `.codex-research/strafe-r7-wait-checks-7c2de66a99d44a0ca4c9fd5a7712f25c/`.
- Corner matrix: ten admissible cases complete alive, every carrying case
  captures, no distant confinement. The two old blue solid-pose refusals are
  retained together with the previously declared replacement poses:
  `.codex-research/strafe-r7-wait-corners-0a97679680d54994adce2889874f6249/`
  (`admitted-audit.json`, `setup-refusals.json`).
- The twelve-case M2 reproduction matches R6 in setup, outcome, elapsed time,
  frames and validity: nine passes, two known skipped-jump deaths at 5625 ms,
  one blue5 approach impulse preempted by pickup at 9775 ms. Evidence:
  `.codex-research/strafe-r7-wait-m2-subset-20260908/summary.json`.

Full qualification runs through
`.codex-research/strafe-r7-chain-20260908/chain.sh`, with `20260908r7` outputs
and the fresh wait build above. Every runner retains source/binary verification;
no timing arms run. Read `chain.log` for current stage completion. M0 retains
12/12 pickup, ownership and time outcomes; exact-trace remains 0/12 and its raw
aggregate `passed` field remains false, as in the retained R6 reference.
Integrated qualification fails the defended gate. K07, matched-resource planned references, normal
MP delivery and map transfer remain open. The wait contract is not yet a proven
fix for the new multi-controller stalls or the original isolated wait.

## Integrated collection

The implementation is committed as `1745b4d3` on local `master`. Results below
belong to the fresh R7 wait build, not the earlier seed-only build. Output names
retain the chain's `strafe-r6-` prefix with the distinct `20260908r7` suffix.

| Completed arm | Measured result |
|---|---|
| M0 | 12/12 pickup, ownership and time outcomes retained; nominal 6700 ms, maximum 6750 ms. Raw exact-trace aggregate remains false (0/12), as before. |
| M1 | 26 starts and all 208 phase pickups pass. Nominal maximum ratio 1.098947; worst phase 1.111421. One catalog and frozen sources retained. |
| Supported resources | All 30 declared cases pass the supported/resource report. |
| Lifecycle | 14 intervention passes, two boundary passes, two control passes; no inconclusive cases. |
| Full route | All 12 regression cases valid and paired gates pass; first-trip median improves 10.270979% red and 11.859956% blue. Of 87 entered flights, 85 verify exit/release and two are censored by capture end. Phases 1/4/7 remain regression, not a fresh holdout. |
| M2 legacy v2 | 748/780 passes, all family and overall outcome floors met. Eight approach impulses remain preempted. Legacy audit also reports old-build compatibility/retention failures because it deliberately uses September 5 reference inputs. |
| M2 re-derived v3 | 765/780 passes (98.0769%): 11 deaths, one timeout and three invalid offset setups. Build compatibility, M0/M1 retention, family and overall outcome floors pass; `all_cases_valid` is false, so the audit does not pass. |
| Defended candidate/baseline | All 24 cases valid, seeds confirmed and tapes distinct within each cell. Candidate has four distant stalls (24.75 s); baseline also has four (33.25 s), including three in its final blue 1x run. Idle improves in every cell; walking regresses in red 1x by 0.2920 percentage points. Pooled idle 6.8757% vs 7.9369%, walking 33.8856% vs 36.1740%. Continuity fails. |
| Corners-only ablation | All 12 cases valid, seeds match the other arms and recordings are distinct. Pooled idle 6.6672%, walking 39.7031%; walking exceeds the candidate in every cell. Two stalls total 12.25 s. |

Evidence: `.codex-research/strafe-r6-m0-regression-20260908r7/summary.json`,
`strafe-r6-m1-qualified-20260908r7/summary.json`,
`strafe-r6-supported-adapter-20260908r7-report/summary.json`,
`strafe-r6-lifecycle-20260908r7-report/summary.json`,
`strafe-r6-fullroute-20260908r7-report/summary.json`, and
`strafe-r6-m2-v2-evaluation-20260908r7-audit/summary.json` /
`strafe-r6-m2-v3-evaluation-20260908r7-audit/summary.json` (all under
`.codex-research/`). Shell exit zero does not supersede a false audit predicate.

The v2 improvement comprises two settling-sensitive route admissions at
blue_respawn_0/y-1 and red_respawn_0/y+1. The other 32 nonpasses match R6 in
outcome, elapsed time, frames and validity. M1's narrower nominal margin also
coincides with changed initial settling at the respawn_0 starts; its slower
phase was already present in R6. These observations do not establish an effect
of the supported-wait change.

The candidate finished at 12:37 UTC, baseline at 13:08 and corners-only at
13:39. All 36 cases and audits are complete. The retained three-arm comparison
is `.codex-research/strafe-r7-review-20260908/continuity-comparison.json`, with
input and reporting-script hashes. Its `passed` field concerns candidate versus
baseline; ablation validity is explicit and separate. No source changed during
the chain. Do not repeat these completed runs merely to reconstruct context.

The three v3 startup failures were recollected together with their successful
fourth offset sibling, using the unchanged build and frozen declarations,
one world at a time. All four finish valid pickups with native pose/module
verification: x-1 11950 ms, x+1 11975 ms, y-1 12800 ms, y+1 12875 ms.
Evidence: `.codex-research/strafe-r7-m2-blue1-offset-recollection-20260908/`
and `strafe-r7-review-20260908/m2-recollection-review.json`. The original
780-case collection and false audit remain unchanged; this is linked separate
recollection, not replacement of failed observations or a rewritten full audit.

Completed candidate red 1x observations already retain two distant stalls:
r2 at 241925..247675 ms (5.75 s), near (2553,5707,1912), and r3 at
236225..242975 ms (6.75 s), near (2526,5587,1912) after landing. Both carry
the flag toward an unchanged home objective over 7100 units away. A defender
presses from about 30.13 units south and follows lateral recovery. Supported
owns roughly 1.25 seconds, then recovery/localnav take over; corner preparation
finishes at 243150/237650 respectively. The second runner subsequently dies
to the defender's alternate pistol at sampled 243025. These remain failures;
body pressure is not a gate exemption. Untraced logs cannot identify every
clearance refusal or prove whether a wait expired. Evidence:
`.codex-research/strafe-r7-review-20260908/candidate-red1-audit.json` and the
complete candidate case directories.

The completed red 5x r3 also stalls at 551125..557625 (6.5 s), near
(2550,5582,1912), under southward body pressure while supported hands off to
recovery/localnav. Blue 5x r2 stalls at 191525..197275 (5.75 s), near
(53,-1450,1912), with a defender pressing from the north. That blue episode
contains no supported-owned sample: route entry hands off to localnav/recovery.
Both are carrying toward the unchanged home goal and later die to the defender.
The original remaining Speed deadlines are unavailable; any static probe's
initial Speed duration must be declared as a probe condition, not called an
exact reproduction. The reusable retained `attribute_stalls.py` in the R7
review directory reports sampled ownership/resources and prior live-enemy
proximity with input hashes; proximity alone is not causal proof.

Baseline red 1x r2 has a corroborated native flag stalemate, not an unexplained
distant stall: enemy1 takes the red flag at 54050; from 66050..602800 the runner
holds blue at its red stand and enemy1 holds red at its blue stand. Both flags
remain carried, with no later drop/return/capture. The runner's stationary
tail lasts 536.75 s and leaves only 62.85 s travel exposure. Existing movement-only
objective policy waits at home while carrying unless its home flag is dropped;
it does not chase a live opposing carrier. Retain the classified waiting and
the limited exposure; neither establishes successful baseline travel. The
complete red 1x cell pools 1198.05 travel seconds: baseline idle 8.2634%, walking
31.7370%; candidate idle 7.3206%, walking 32.0290%. The walking cell regresses
under the unchanged gate. Evidence: `baseline-red1-audit.json` in the R7 review
directory and the full baseline red 1x r2 native recording.

Baseline blue 5x r2 also has a 10.5 s distant stall (245025..255525), carrying
near (40,-1499,1912) under the same north-side pressure family. Supported
hands off to localnav/recovery; the runner later dies. Its seed matches the
candidate blue 5x r2, but timing, entry pose, prior captures and ownership
differ. The candidate's shorter episode is not a controlled causal gain.

## Targeted native follow-up

Four declared traces completed with roles/rates/durations confirmed and clean
shutdown in `.codex-research/strafe-r7-pressure-trace-20260908/`: candidate
red/blue 5x seeds 2026090806/2026090811 for 600 game seconds, the same-seed
blue baseline control, and a 120-second baseline red 1x seed 2026090802
goal-wait probe. `plan.json` records commands and scope. Outcome audits clip
to the declared windows; retained shutdown tails do not extend the gate.

Native endpoint expiry is directly observed in candidate red: wait starts at
604900, expires at 605150 after exactly 250 ms, and releases on the same
frame. The sampled owner changes supported at 605025 to localnav at 605275,
grounded near (153,-1141,1924), carrying while both flags are held. The first
expiry is inside the declared cutoff 605325; 26 further retained expiries are
outside it. This proves native endpoint release and dispatch, not travel
success during legitimate unavailable-home-flag waiting. See red-pressure
`server.log` lines 9282 and 9288–9289. No three-second preparation/prefix expiry
was observed. The short goal-wait probe did not reproduce its former stalemate.

Only the candidate blue trace contains an audited distant stall: 202350..210350
(8 s), carrying near (71,-1565,1912). Its sampled ownership is localnav 20,
recovery nine, supported four. Supported repeatedly rejects the first edge's
floor sample; localnav reports `floor_startsolid`, then `choice=none` and a
heading exclusion. A westward recovery reaches its local arrival envelope at
206800 but does not establish sustained progress. The pressing enemy remains
about 30 units away until its native tactical self-kill between sampled
208350 and 208600; the nearest live enemy then exceeds 2000 units. A new
recovery step completes at 209450 and supported preparation succeeds at
210825. Do not credit removal of the opposing body as autonomous escape.
The runner itself was not reset or killed by the fixture.

Evidence: `opposition-audit.json` in the trace root and
`.codex-research/strafe-r7-review-20260908/pressure-trace-attribution.json`.
The other three probes have no audited distant confinement; seeds did not
reproduce identical combat timelines. A floor sweep can include bodies, and
the current refusal trace omits the hit entity/contents. Next discriminate
dynamic-body versus world support refusal and retain a bounded useful
continuation under moving pressure; do not weaken floor safety or infer a
geometry defect from `floor_startsolid` alone. Keep the original isolated supported
wait and all failed recordings as separate evidence.

## Next implementation package: matched-resource planned references

Read-only investigation confirms that existing fixture switches do not close
this gap. The old flagrun/replay paths inject Speed or bypass production
ownership; CHAIN2 intercepts before production and refuses active Speed.
The supported-only control is not an independently planned reference.

The smallest portable implementation is a bounded frozen raw-path loader at
the supported acquisition boundary, currently `NavMesh_GetPath` in
`ai_bot2_supported.c`. Loaded XYZ knots must still pass the unchanged native
preparation, floor/sweep/headroom/corner validation and shared path execution.
Never deserialize trusted clearance flags or restore recorded player state.
Keep normal objective dispatch, resource regeneration, natural Speed expiry
and both legs' native lifecycle. An enabled reference refusal must invalidate
its reference measurement even if ordinary fallback later captures.

An independent planner is still required. Existing mode-3 geometry observations,
the hash-bound known-map support inventory and mapbench join probes can supply
geometric inputs; backbone/connection utilities supply proposals, not an
already-qualified resource-aware full-route optimizer. Declare the finite
shortcut/bevel alternatives and selection rule before development collection.
Select on separate phases 1/4/7 for both teams and all five resource conditions;
freeze selected outbound/return paths before the resource evaluation phases
0/3/6. Candidate evaluation recordings must not choose the reference.

The payload/manifest must bind map/physics, initial condition identity,
endpoints, selected raw knots, input hashes, alternatives/refusals and selection
evidence. Bind the initial resource identity once; the return leg must consume
its actual inherited resources, not the outbound resource value. Run all 30
candidate/reference pairs with matching actual initial pose, yaw, velocity,
resources, Speed deadline, phase and physics. Record native first pickup and
first return capture while retaining the existing 120-second observation and
resource gates. Report ownership/fallback, time, idle and walking in both arms.

This is an implementation design, not implemented infrastructure or measured
reference quality. The current plan does not declare a separate numeric ratio
threshold for these 30 comparisons; do not silently borrow the M1 ratio or
invent a tolerance after seeing results. Final cost qualification also remains
deferred by the recorded host-contention decision.
