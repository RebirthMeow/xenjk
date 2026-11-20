# Recovery substep arrival investigation

M3a-B development experiment, following the installed lateral-recovery build.
Preserve the frozen benchmark controller and use shared production movement
for fixtures and matches. This is not an objective-policy or combat change.

## Evidence and hypothesis

The retained blue defended episode at game time 329525–335775 stays within
64 units at (1569.098,2771.658,1368.125), force 50, Speed active, unladen.
The nearest defender is initially about 360 units away and moves farther away.
Two ground recovery steps occur with long stationary intervals between them.
The other retained carrier episodes have a defender at body-contact distance;
those are separate dynamic-obstruction cases.

Three declared native controls used the installed lateral build, a single
one-shot corner setup, 45 game seconds at 5x, no enemies, and native movement
after setup. The fixture resets initial velocity, heading and active powers;
these controls isolate geometry and are not exact combat-history replays:

| Output under `.codex-research/` | Team | XYZ | Force | Carrying |
|---|---|---|---:|---:|
| `continuity-control-red-20260906` | red | 2557.472,5728.349,1912.125 | 24 | 1 |
| `continuity-control-blue-stand-20260906` | blue | 51.272,-1536.429,1912.125 | 26 | 1 |
| `continuity-control-blue-low-20260906` | blue | 1569.098,2771.658,1368.125 | 50 | 0 |

Both stand controls capture alive. The low control reproduces delayed recovery
without a defender. Its first step targets y=2707.658; the runner settles at
y=2725.617, 17.959 horizontal units away, and remains still from sampled level
4575 through 7825. The completion condition requires distance below 16.

Recovery supplies a waypoint intent with a 12-unit stop radius. At this
residual distance it requests about 23.8 units/second. From rest, the discrete
normal-speed input predictor chooses between zero and about 62.5 units/second
after one tick, preferring zero below a requested 31.25. Thus the intent can
stop outside the recovery completion threshold. A two-second step timeout,
fresh 1.5-second progress delay and a bounded candidate scan then delay the
next step. Speed raises the discrete acceleration, so it needs explicit coverage.

The ordinary match audit starts after rate confirmation and can omit several
seconds after corner setup. Exact-case comparisons must use `corner_level`
as their start and report sampling gaps. Preserve the ordinary audit unchanged
for the defended campaign; use a separate full-window corner audit.

## Bounded candidate and checks

Candidate under review: recovery already ends its substep inside 16 units,
so request a zero-radius waypoint intent during that step instead of adding
a second 12-unit stop margin. Preserve the existing completion threshold,
path/collision/hazard checks, candidate set, query budget, watchdog and native
physics. Do not alter the shared controller's general waypoint semantics.
The predictor's at-rest stop region then falls within 16 units at both normal
and Speed movement settings; native behavior still requires measurement.

Before accepting the candidate:

1. Reproduce the low-corner case with Speed inactive and active. Record each
   step's completion/timeout and first 64-unit displacement from initial setup.
   The recorded first-step residual must no longer cause a timeout; native
   objective selection and collision checks remain in effect.
2. Repeat all four pit/valid-drop controls and the static lateral-body case.
   Preserve native return timing, safe goals, and failed evidence.
3. Run the separately frozen 12-case defended baseline/candidate comparison
   in [the campaign protocol](STRAFE_MOVEMENT_CONTINUITY_CAMPAIGN_2026-09-06.md).
   Do not infer broad reliability from the exact-case fix.

## First candidate results

To preserve baseline collection, candidate development uses the isolated source
copy `.codex-research/continuity-arrival-source-20260906`; original checkout C
sources remain unchanged. Build `strafe-build-recovery-arrival-v2-20260906`
contains the single recovery intent change, arrival/timeout transition logging,
and a declared Speed-expiry option for the one-shot corner fixture. The first
configure attempt (`strafe-build-recovery-arrival-20260906`) lacked the disabled
SP configure directory in the source copy; its failed log is retained. Adding
the unchanged configure inputs and building into a new directory succeeded.

The low-corner control with Speed inactive crosses 64 XY units in 2.900 game
seconds, versus 6.650 on the baseline. Its first step reports arrival with
15.208 units remaining. With 15000 ms initial Speed remaining, it also crosses
64 units in 2.900 seconds and reports arrival with 13.594 units remaining.
Both runs remain alive, show no >=5-second confinement, and have longest
sampled grounded idle 2.25 seconds. The baseline full-window control had a
6.25-second confined episode. This closes the reproduced double-margin fault
in these controls; it does not establish defended continuity.

Evidence under `.codex-research/`: `continuity-controls-fullwindow-audit-20260906-v2.json`,
`continuity-arrival-low-off-audit-20260906.json`, and
`continuity-arrival-low-speed-audit-20260906.json`. The corresponding run
directories retain setup, hashes, CSVs, demos and transition logs. The corrected
corner auditor starts at setup and reports missing sample intervals explicitly.

## Regression and resource evidence

All four pit/reachable-drop regressions pass on the arrival candidate, including
native return timing and valid pickup. First 64-unit displacement is 2.900,
1.175, 0.900 and 0.925 game seconds for red-low, blue-high, red-high and reachable
respectively. The static-body case escapes in 3.775 seconds, with no five-second
confinement. Retained audits: `arrival-regression-drop-audit-20260906.json` and
`arrival-regression-staticbody-corner-audit-20260906.json`.

Eight matched resource checks (baseline/candidate, red/blue, Speed off/8000 ms
expiry) each complete 4800 native frames and two captures without death or
stall. They use initial force 50, supported-hop mode 2, continuous mode 6 and
phase 0 for 120 game seconds. Speed expires at 8025 ms in all four expiry runs.
These verify the declared production fixture and resource handoff; they do not
establish normal MP continuity or reference-speed qualification. Evidence is
under `arrival-resource-{baseline,candidate}-{red,blue}-{off,speed}-20260906`.

The full defended baseline is complete: all 12 cases are mechanically valid.
Its audit retains 15 distant confinement episodes lasting 5.75–9.0 game seconds.
Aggregate travel idle/walking is 9.8840%/34.8168%. See
`strafe-continuity-baseline-20260906/aggregate.json` for all cell denominators and
episodes. Candidate defended collection is complete under
`strafe-continuity-arrival-20260906`, using the same 12 declared case slots.
All candidate cases are mechanically valid and their hashes verify.

| Team/rate | Baseline travel idle % | Candidate idle % | Baseline walking % | Candidate walking % |
|---|---:|---:|---:|---:|
| red 1x | 9.209 | 7.813 | 32.599 | 33.993 |
| blue 1x | 11.084 | 8.096 | 36.127 | 29.224 |
| red 5x | 9.334 | 6.765 | 33.844 | 37.308 |
| blue 5x | 9.895 | 8.729 | 36.653 | 35.521 |
| aggregate | 9.884 | 7.845 | 34.817 | 34.179 |

The candidate reduces observed idle in every cell and retains one distant
episode versus the baseline's 15. It nevertheless fails the declared comparison:
red walking increases at both rates, and red-1x-r1 still has a five-second
carrier confinement at level 163800–168800 near (2513,5588,1930), with force 7,
Speed active and a nearby defender. Logs show two recovery steps, with the
second reaching arrival at 169000; no goal-wait exemption applies. These are
unpaired descriptive results, not statistical proof of equivalence or causality.

## Cost workload failure retained

Two arrival-candidate eight-bot cost runs lose a bot to a native fall and fail
the strict all-clients-dispatched workload gate. The first has movement p95
0.4371 ms and the repeat 0.3619 ms; low timings do not override the missing
live workload. The first death is client 2 at elapsed 19050 ms; the repeat loses
client 1 at 15250 ms. A fresh baseline
repeat keeps all eight alive and passes its numerical budget (p95 0.3496 ms).
These unseeded runs suggest a possible regression, not a deterministic proof.

Retain `arrival-cost8-20260906`, `arrival-cost8-repeat-20260906`,
`arrival-cost8-baseline-repeat-20260906` and their sibling report JSON files.
The cost capture lacks per-client poses/commands, so it cannot reconstruct the
fall, and `bot_telemetry=0` intentionally suppresses recovery transition logs.
A separate opt-in diagnostic fixture is being prepared without changing
the frozen candidate. Installation is held pending diagnosis and evaluation.

## Separate walking-efficiency lead

The full baseline's red-5x-r3 contains 15.325 seconds of continuous grounded
walking at level 282500–297825, with force 100, no active powers, no cached route
and no jump command. Its first approximately seven seconds cover a level floor
from (1594,2721,1368) toward (1779,4316,1368) at roughly ordinary 250-unit speed;
the later section climbs the ramp toward the stand. This is useful movement,
but the level section warrants checking for ordinary-hop opportunities.
It is not evidence that the ramp itself should be hopped. Supported-path segment
classification and ownership need review before changing traversal behavior.

## Declared diagnostic follow-up

Diagnostic source copy `continuity-path-trace-source-20260906`, build
`strafe-build-continuity-path-trace-20260906`, adds opt-in buffered per-client
cost trajectories and supported-path transition logs. Production movement
decisions remain those of arrival-v2. Trace runs explicitly cannot qualify
timing. The preceding cost-only diagnostic build and its sources are retained.

Once a native slot is free, collect one eight-client cost trace with supported
hop mode 2 and the unchanged fixture poses/impulses, then one red one-shot
walking control at (1594.153,2721.343,1368.125), force 100, carrying 0, Speed
inactive, 45 game seconds at 5x with supported-path tracing. The control resets
velocity/heading and has no defender; it isolates geometry, not the exact match
history. Preserve complete captures even if no fall reproduces or the proposed
walking mechanism is disproved. Use new output roots and native 25 ms steps.

The first cost trace completes all 800 frames with eight survivors and no
between-frame position/velocity discontinuity. Three further diagnostic
repetitions are declared to seek a retained fall trajectory; no-fall cases
remain in the evidence and do not erase the two original failures.

The walking control disproves ownership by a completed walking-only supported
edge in its initial flat span: preparation repeatedly rejects a floor sample
within the first raw edge. At level 1750 the sample is 31/87, at raw-interpolated
(1705.182,3693.801,1499.920), while the traversed floor remains at standing
height 1368.125. Since the raw edge is unfinished, existing prefix retention
discards its validated initial span. A bounded experiment will retain only a
still-hop-capable validated unfinished span longer than 320 units, using the
existing partial-path arrival/handoff rules. Floor tests and budgets remain
unchanged; the failing sample is never retained as a destination.

Four no-Speed, force-100, 45-second geometry controls compare arrival-v2 and
`strafe-build-continuity-prefix-20260906` from the red pose above and blue
(988.858,1402.550,1368.125). First enemy-flag pickup improves from 15.200 to
14.150 seconds red and 15.025 to 14.125 seconds blue. Sampled walking time
falls from 32.750 to 28.225 seconds red and 33.700 to 30.400 seconds blue;
idle rises from 1.50 to 1.75 seconds on each. All remain alive at force 100
with no confinement, and both prefix runs log the new retention branch.
These controls do not replace the defended campaign. Full-window audit:
`continuity-walk-corner-audit-20260906.json`; initial sample gaps of 150–175 ms
and terminal gaps of 75–175 ms remain explicit.

Two of the three additional cost traces reproduce a fall; one does not. Both
retained failures involve client 1 near the same route-19 rejoin. The final
upper-platform takeoff is at level 23450 (elapsed 9450 ms), near
(2053.722,2818.194,1808.125), with velocity (-124.285,-460.922,0), force 50 and
Speed active. The route resumes around index 56 and subsequently releases for
a body obstruction; the local layer reports an active landing hint. Transition
logs do not timestamp the release precisely enough to assign that first jump
to the fallback. The airborne guidance later falls short of the upper platform,
hits its side and lands in the pit around level 26650. Native falling death
arrives at level 29250 after a stationary pit-floor interval. The death-row
"last grounded" field in the first audit therefore identifies the pit floor,
not the earlier takeoff; use the full trace for departure analysis.

Traces and audits: `continuity-cost-path-trace-r1-20260906`, `...-r2-20260906`,
`...-r3-20260906` and sibling `...-audit-20260906.json` files. None has a
between-frame pose/velocity discontinuity. The landing continuation's scalar
speed estimate is under review: sideways momentum can overstate progress
toward the landing point. This is a hypothesis pending a native comparison;
the unchanged frozen route kernel is not retroactively qualified by these runs.

## Validated prefix regressions and landing continuation

The prefix candidate passes the four dropped-goal regressions and static-body
case (`continuity-prefix-regression-*`). Long confinement at the red low/high
dropped-goal destinations is classified as native arrival/wait, not distant
confinement. Four corrected resource runs,
`continuity-prefix-resource-{red,blue}-{off,speed}-mode6-20260906`, each complete
4800 frames and two captures without death, stall or force spending. Their
pickup/capture events and movement metrics match the arrival candidate. The
earlier four similarly named runs without `mode6` omitted `--hop-probe 6`;
they and a pre-launch parse failure remain retained but are invalid comparisons.

The isolated landing experiment excludes the prefix change. It replaces only
the local airborne continuation's scalar reach estimate with velocity projected
toward the known landing point, retaining the existing flight-time calculation
and acceleration allowance. The shared frozen `bot2_strafe.c` is unchanged.
Build: `strafe-build-continuity-landing-20260906`. Four diagnostic captures are
`continuity-landing-cost-trace-r{1..4}-20260906`; all retain 800 frames and eight
client trajectories, and cannot qualify timing.

The reproduced client-1 route-19 handoff now lands on the upper platform in r1.
At elapsed 10100 ms the old and new recordings have identical recorded
pre-position, velocity and force: the old norm/base-speed estimate predicts
about 519 units of reach for a 515-unit gap and releases jump; projected
velocity predicts about 441 and retains jump. The new run lands at elapsed
11325 ms near (1590,2243,1808), alive with force 31, and resumes the route.
The old run falls into the pit with force 34. This costs three additional force
points to complete the landing. The recorded fields match from elapsed 9350
through 10075; this is not a complete cloned player-state experiment.
See `continuity-landing-comparison-20260906.json` and the v2 trajectory audits.

Runs r1/r3 have no deaths; r2/r4 expose the same separate client-2 failure.
At elapsed 14325 ms it attempts a sustained route jump from
(2288.420,4426.975,1808.125), with roughly 994 horizontal speed, active Speed
and only five force points. It misses the higher stand, lands in the pit at
16850 and dies at 19425. The native force-cost table lists ten for levitation,
but held-jump physics can continue with any nonzero reserve and drains five
per 300 ms at level three. Ten is an experimental minimum reserve here, not
a proven native held-jump activation boundary or full maneuver cost.

The next isolated resource experiment rejects a newly requested grounded,
sustained route jump below that ten-point reserve, restores the
incoming command and releases to existing ground navigation. Ordinary tap
hints and airborne continuation retain their current behavior. Source:
`continuity-resource-source-20260906`; build:
`strafe-build-continuity-resource-20260906`. Four diagnostic repetitions are
declared before collection. Acceptance must inspect movement after release:
replacing the fall with prolonged idle does not solve the owner's problem.
The reserve threshold alone does not guarantee resources for an entire jump.

All four resource diagnostic runs complete 800 frames with eight survivors and
no recorded pose/velocity discontinuities. The gate fires for client 1 at force
7 in r1/r2/r3 and client 2 at force 5 in r4. After release, those actors travel
924.5, 923.2, 923.2 and 1740.1 units along their XY paths; longest speed-below-20
spans are 2.125, 2.100, 2.100 and 1.175 seconds. None has a five-second
64-unit anchored confinement during the remaining observation window. No
airborne landing hint is announced after these gates. These windows are only
5.7–6.6 seconds long and do not establish long-match reliability.

The combined source `continuity-combined-source-20260906` now composes recovery
arrival, unfinished supported prefix, directional landing continuation and
the conservative reserve gate. Optional buffered cost diagnostics remain opt-in.
The shared frozen route kernel is byte-identical to main. Before defended
evaluation, repeat all five corner/drop controls and four mode-6 resource cases
on its fresh build, followed by two non-trace eight-bot cost runs conducted alone.
Then freeze and execute the unchanged 12-case defended protocol. All cases and
failures remain retained; this is an evaluation sequence, not an acceptance claim.

The combined build passes those nine focused regressions. The two non-trace
cost repetitions also pass their strict numerical checks with eight survivors
and all clients dispatched on every frame. Movement p95 is 1.0025/0.8405 ms;
maximum is 2.4469/1.1973 ms. All eight move in 252/225 of 800 frames, so these
remain bounded workload checks rather than sustained fully-active qualification.
Evidence: `continuity-combined-cost8-r{1,2}-20260906` and sibling report directories.

The five reviewed C files and companion trace/Speed-expiry tools are integrated
in the main working tree. A fresh checkout build,
`strafe-build-continuity-integrated-20260906`, succeeds and has exactly the same
measured source hash map as the combined build. Its binary hashes differ; native
evidence continues to bind specifically to the combined build, and installation
must use the evaluated module if claiming those recordings. No game install has
occurred at this checkpoint. The 12-case campaign is frozen under
`strafe-continuity-combined-20260906` and is running with its original executor,
protocol, module hashes and case declaration.

After continuity evaluation, the retained cached-route regression sequence uses
that same combined build: the 12-case M0 matrix; M1's 26 poses, each with one
planned run and eight runtime phases; then all 780 frozen M2 evaluation cases.
Use `strafe_flag_matrix.py`, `strafe_qualification_batch.py` and
`strafe_disturbance_runner.py`, with the frozen Kejim catalog/inventory/cases
and `strafe-m2-v2-frozen-suite-20260905/suite.json`. Run from the isolated source
root with absolute build/assets/output paths. Outcome collection can use four
worlds, with timing tests alone. The M2 audit receives the same fresh build as
both `--m1-build` and `--build`, plus the fresh M1 report binding; its unchanged
compatibility gate accepts the empty source difference. Keep all failures.

These fixtures exercise cached routes, not the full supported/objective path.
They do not close the production matched-resource reference gap: neither old
Speed-bearing tapes nor a comparison between two runtime controllers establishes
a no-Speed planned reference. The existing eight-phase and same-resource speed
requirements remain in force. Final normal MP and representative continuous cost
also remain required before any M3a completion claim.

## Combined campaign follow-up: preparation ownership

A completed four-case interim audit (three red 1x, one blue 1x) retains four
distant confinement episodes, including carriers near both stands and two
red-side ramp episodes. It is explicitly not the 12-case result:
`strafe-continuity-combined-20260906/INTERIM_4_OF_12_RESULT.json`.
The red carrier pauses 5.75 seconds at level 50725–56475 with force 24 and
Speed active; the nearest defender at its start is over 2100 units away.

`stand_entry_resource` is the follower's last release/status label even while
inactive, so it does not prove current ownership. Supported preparation can
claim movement with destination=current before readiness, making it a plausible
source of the zero commands. Instrumentation must establish the actual intervals
before changing ordering or adding resource waits. A forced refill wait is not
the desired general movement policy.

Diagnostic-only build `strafe-build-continuity-preparation-trace-v2-20260906`
adds opt-in preparation ownership start/end, edge and rejection logs to the
unchanged combined movement. V1 remains retained; V2 corrects a diagnostic flag
that could suppress a new start marker after rejection. Declare two solo controls
before collection: red (2473.877,5354.896,1923.920), force 24, carrying, Speed
remaining 13000 ms; blue (12.562,-1561.981,1912.125), force 22, carrying, Speed
remaining 7500 ms. Each observes 30 game seconds at 5x with supported tracing.
Remaining Speed is estimated from four-Hz activation samples, and setup resets
velocity/heading and has no opponent; these isolate local behavior, not complete
replays of the defended trajectories. Wait for free native slots in the campaign.

Both controls pass provenance and cross 64 XY units at 1.275 seconds with no
confinement. Their initial unready preparation owns motion for 0.800/0.850
seconds, then becomes ready. They do not reproduce the live multi-second pause,
so preparation starvation alone is not established as its cause. Next declare
four diagnostic defended matches, red/blue with two repetitions each, 600 game
seconds at 5x and the same CHASE/BASE pair, with supported tracing enabled.
These retain actual arrival and controller history and cannot qualify timing.

The full combined comparison is now complete. All 12 cases are mechanically
valid and hashes verify, but red-1x and blue-5x walking regress against baseline;
11 distant episodes last 5.0–7.25 seconds. Travel idle is 9.142% versus baseline
9.884%; walking is 34.792% versus 34.817%. The arrival-only candidate had one
episode and 7.845% idle, so combining the isolated fixes has not established
better defended continuity. The matches are unpaired; this does not attribute
the change to any single patch. Keep the build uninstalled while diagnosis
continues. Full cell results and limitations are in
`strafe-continuity-combined-20260906/CANDIDATE_RESULT.md` and `aggregate.json`.

## Ramp fallback reproduction

The four defended preparation diagnostics are complete; see
`continuity-preparation-defended-correlation-20260906.json` and its opposition
audit. Three matches retain distant confinement. Preparation rejection releases
ownership, so an unready flag alone does not prove preparation owns a pause.
The fourth match has no distant episode and a long declared home-flag wait;
absence of a capture is context, not a movement-gate failure by itself.

Two 30-second 5x ramp controls on the unchanged preparation-trace-v2 build start
at rest, force 100, no Speed and no carried flag. Red starts at
(1894.485,4477.510,1373.243), blue at (841.769,-288.431,1368.125).
With clear paths, first 64-unit displacement takes 0.650/0.675 seconds and no
confinement occurs. Supported preparation accepts the walking ramp segments.

The same starts with a static body at the far end of the first segment reproduce
the crawl: red body (1905.750,5450.500,1760.716), blue
(717,-1288.250,1761.458). First 64-unit progress takes 9.675/5.425 seconds;
idle occupies 25.75/25.50 seconds of the declared 30-second window. The body is
roughly 1000 units away, not pressing against the runner. First-edge rejection
discards the walking-only prefix; fallback then repeatedly brakes and recovery
makes short uphill steps. These are static obstruction experiments, not live
opponent replays. Both teams retain multiple distant confinement episodes.

Evidence: `continuity-ramp-control-audit-20260906.json` and
`continuity-ramp-farbody-audit-20260906.json`, with source/binary/setup proofs and
full sampled windows. All worlds are closed. A diagnostic-only owner build,
`strafe-build-continuity-owner-trace-20260906`, adds `movement_owner` to play CSV
using the branch that generated the current command, guarded by the current
game timestamp. It avoids interpreting stale route-release labels as owners.

Two owner-build repetitions confirm the far-body crawl: first 64-unit progress
is 9.900 seconds red and 5.400 seconds blue, with 25.75/25.50 seconds idle.
The confined intervals alternate top-level localnav and recovery branches with
brief supported preparation. Owner telemetry identifies the top-level command
producer; localnav can itself invoke recovery, so it is not an internal
SupportedWalk branch trace. See `continuity-ramp-owner-corner-audit-20260906.json`
and `continuity-ramp-owner-evidence-20260906-v2.json`. V2 restricts aggregate
owner counts to the declared 30-second window (120 rows per team); the original
artifact incorrectly included retained tail rows in those aggregate counts.

The next bounded candidate replaces fallback's fixed-origin floor probes and
horizontal floor-score sweep with progressive ramp validation. Preserve walls,
hazards, bounded work and native movement/resources; compare both clear and
far-body controls before the existing drop/body/resource/cost regressions and
another frozen defended campaign. The frozen shared route kernel stays intact.

### Progressive walk candidate

`strafe-build-continuity-ramp-walk-20260906` builds the isolated owner-trace
source plus a four-sample progressive `SupportedWalk` validator. Each sample
permits an 18-unit rise/drop, sweeps the body between standing points and tries
a bounded raised step if an uphill sweep hits. It retains adaptive lookahead,
rejects liquids and checks endpoint trigger_hurt volumes. It newly requires
world floors and reduces the former 48-unit downward allowance; those are
conservative changes requiring regression checks. It leaves recovery geometry
unchanged and is not installed or integrated into main.

Four exact controls are complete; source/module/setup proofs verify. First
64-unit displacement is 0.650 seconds in both clear and both far-body cases.
Clear paths have no confinement. Far-body cases now reach the vicinity of the
blocker, but red retains 7.25/5.25/7.75-second episodes and blue one 23-second
episode. Far-body idle is 22.50/22.00 seconds; walking rises to 7.25/7.75 seconds
as the bots cover the formerly stalled ramp. This is a resolved initial crawl,
not a full static-obstruction pass. All four remain alive with force 100.
See `continuity-ramp-walk-corner-audit-20260906.json` and
`continuity-ramp-walk-evidence-20260906.json`.

Blue selects forward recovery steps beyond the ramp crest (for example,
(754.554,-1248.175,1753.266) to (754.554,-1312.175,1760.250)), moves only about
four units and abandons the step without arrival. Recovery still validates
straight floor-to-floor chords and total ascent from the original start;
unlike the new walk validator, it cannot step across the convex crest. The next
isolated candidate shares progressive validation with recovery, retaining its
96-unit radius and existing scan/timing policy. It also replaces endpoint-only
hurt probes with a bounded swept-hull hurt neighborhood, preserving benign
triggers. Declare two diagnostic 600-second defended ramp-walk matches at 5x,
one per team with the CHASE/BASE pair and tracing, while that fix is prepared.
These are diagnostic evidence, not the frozen 12-case acceptance comparison.

Both diagnostic defended matches completed. Red retains one five-second distant
episode at (2366.093,4457.987,1808.125), away from the ramp and stand references;
blue retains carrier episodes of 5.25 and 5.0 seconds near its stand. Travel
idle/walking is 7.217/40.826% red and 12.421/32.075% blue. These single unpaired
cases do not establish improvement over the repeated baseline. Evidence and
actual command-owner sequences are in
`continuity-ramp-walk-defended-opposition-audit-20260906.json` and
`continuity-ramp-walk-defended-evidence-20260906.json`. All worlds are closed.

### Shared ground-corridor candidate

`strafe-build-continuity-ground-corridor-20260906` shares progressive floor and
raised-step validation between ordinary walking and recovery. Recovery keeps
its horizontal radius, scan set and timing. A per-segment swept-hull AABB
neighborhood checks only trigger_hurt entities, ignores benign triggers, and
rejects a saturated 64-entity result. This is conservative: an AABB may overlap
a hurt volume outside a diagonal's exact swept hull. Worst-case work per
corridor is 16 collision traces, four entity queries and 252 contact checks;
typical hurt-free neighborhoods require no contact checks. Recovery validation
remains globally once per frame, while ordinary walk validation is per client.
Existing `bot_supportedTrace` enables a reason/sample log for recovery execution
abort. This candidate changes only localnav versus ramp-walk, plus provenance.

All four ramp controls remain alive with force 100. Both far-body starts cross
64 units in 0.675 seconds and now reach native flag pickup. Red has no distant
confinement (7.50 seconds idle, 17.10 walking); blue retains one 7.5-second
episode near the body (11.50 idle, 15.15 walking). Clear paths have no distant
confinement. No recovery_abort is logged: selected crest steps now execute.
Blue still selects several short steps before the longer bypass, so initial
ramp and crest fixes do not establish acceptable overall recovery latency.

Use `continuity-ground-corridor-controls-audit-v2-20260906.json` for exact
windows. The initial audit counted a clear-blue CSV row at the exact setup
timestamp that still contained the pre-RCON pose, incorrectly reporting zero
time progress. Main corner audit now selects strictly later rows, consistent
with the smoke helper, and records the excluded count/reason. The original
audit remains. A synthetic regression confirms exclusion of the ambiguous row
without removing later or end-window samples. Correct first-64 times are
0.500 seconds clear blue and 0.675 for the other three. This is timestamp
ordering ambiguity, not a native teleport after setup.

Five established drop/static-body regressions are next on this frozen build,
then isolated non-trace cost and resource checks. Recovery-candidate continuation
quality is being reviewed separately; no scan or watchdog thresholds have
changed, and no ground candidate is installed or integrated into main.

All four drop regressions pass on the shared-ground build; first 64-unit
progress is 2.925/1.150/0.900/0.925 seconds for red-low/blue-high/red-high/
reachable. The close static-body case crosses 64 units in 3.775 seconds and
has no distant confinement. See the regression corner audit and drop audit v2
under `continuity-ground-corridor-regression-*`. An initial drop-audit invocation
used a malformed prefix and addressed nonexistent folders; it remains retained
as an audit-call error, with no native rerun or excluded native failure.

The first isolated non-trace eight-bot cost check is valid but fails the maximum
budget: p95 1.7212 ms, maximum 5.2233 ms, all eight alive/dispatched, 195 of 800
frames fully moving. The worst frame has 4.3096 ms localnav cost distributed
across several clients. Retain `continuity-ground-corridor-cost8-20260906` and
its report. Declare one fresh combined-build baseline followed by one shared-
ground repeat, each alone under identical settings, to assess whether the
cost difference repeats. A passing repeat will not erase the first failure or
establish full representative-workload qualification.

The declared follow-up timing pair completed: fresh combined baseline p95
1.1068 ms/max 2.2776 ms; ground-corridor repeat p95 0.8633 ms/max 2.6311 ms.
Both are valid, keep all eight alive/dispatched, and have 225/800 fully-moving
frames. The repeat does not reproduce the original 5.2233 ms spike. This is
insufficient to attribute that spike to host scheduling or dismiss it; retain
all three runs. The next recovery-scoring candidate must get its own cost check.

The continuation-scoring v2 experiment was rejected after its first two exact
far-body controls: red confinement rises to 12.75 seconds and blue to 8.0.
It applies a 128 score penalty after one bounded continuation check. The
checks detect body intersections, but short clear connectors and later bends
still lead to ineffective alternatives. Do not tune the penalty merely to
these cases or include it in production. V1 was never built: review found it
could mistake a polygon-height offset for a horizontal connector; V2 fixed
that issue before collection. Both source copies and V2 native failures remain.
See `continuity-recovery-continuation-farbody-audit-20260906.json`.

Two ground-corridor defended diagnostics complete without any confinement.
Travel idle/walking is 8.01/42.82% red and 7.26/40.45% blue. The high walking
fraction remains an efficiency problem. Four current mode-6 resource checks
also pass: both teams, starting force 50, no Speed or expiry at 8000 ms, 4800
native frames, two captures, no deaths or force spending. Events exactly match
the preceding combined controls. See the ground defended audit and
`continuity-ground-corridor-resource-audit-20260906.json`.

The reviewed ground geometry plus preparation/owner telemetry is now integrated
into main. Fresh build `strafe-build-ground-integrated-20260906` verifies every
current measured source and exactly matches the isolated ground build's source
map; loaded binaries differ, so earlier native evidence remains bound to the
isolated DLL. `ground-integration-proof-20260906.json` records this distinction.
The scoring experiment is excluded. No installed game change was made.

## Supported walking follow-up

Walking decomposition in the two ground defended runs attributes 94.4% to the
top-level supported branch, 2.3% to localnav and 1.7% to recovery. Most is
Speed-inactive and force >=50. Observed small sample-to-sample Z change does
not alone classify terrain. Timestamped supported-edge correlation establishes
two distinct cases: an initial seven-unit height change disables hopping for
the remaining roughly 1700-unit flat red edge; blue also walks across several
short hop-eligible edges. See the ground defended walking decomposition and
supported-correlation artifacts. Do not infer all walking has one cause.

Declare a terminal-flat-run experiment that separately validates a safe suffix
of a completed walking edge, without restoring its whole-edge hop flag or
changing partial-rejection behavior. Every suffix sample must pass the full hop
envelope; runtime hopping starts only after entering that span. Keep the shared
hop kernel, >320-unit threshold, ordinary jump pulses and global trace budget.

Native baseline: red start (924.910,-323.279,1375.599), force 100, no Speed,
carrying 0, dropped enemy flag at (2464,5312,1936), 30 seconds at 5x. Setup level
1625 produces the same 56-sample first edge, hop=0 from elevation at sample 1,
ready at 1850 and next index only at 9100 (7.475 seconds after setup). The
dropped flag retains native gravity/return; this is a declared local geometry
control, not a full live replay. A first attempt at drop Z1900.125 was refused
by the fixture's solid-bounds check and is retained; no movement was evaluated
in that refused world. The revised baseline is
`continuity-terminal-hop-baseline-v2-20260906`.

The isolated `strafe-build-continuity-terminal-hop-20260906` changes only
supported-path preparation/runtime eligibility versus the ground build. It
keeps whole-edge hop flags and partial rejection unchanged, and tracks a
separately validated terminal suffix. Later samples recheck the full envelope
even after an earlier edge failure. A raised walking interval is never hop-safe;
the existing five-trace reservation/32-trace global budget still bounds work.
Runtime eligibility requires forward progress past the suffix start and >320
units remaining; the unchanged hop kernel governs ordinary jump pulses and
air steering. Invalid terminal diagnostic records must be ignored by `valid`.

The exact pilot improves next-index arrival from 7.475 to 5.200 seconds after
setup. It validates a 1702.967-unit suffix starting at
(929.538,-260.376,1368.250), enables it at level 2250, and advances to index 2
at 6825. Whole-edge hop remains false. In the full declared 30 seconds, walking
falls from 16.825 to 13.950 seconds and idle from 3.750 to 3.500; neither run
has confinement. See `continuity-terminal-hop-pilot-audit-20260906.json` and
the retained native logs. This is a local efficiency result, not a broad live
qualification pass. Drop/body/resource/cost and defended checks follow before
integration of this separate supported change.

Subsequent checks on that terminal-hop build: all five established drop/body
cases pass (`continuity-terminal-hop-regression-evidence-20260906.json`). The
isolated non-trace eight-bot check passes p95 1.1397 ms/max 2.3166 ms, with all
eight alive and 141/800 frames fully moving; see
`continuity-terminal-hop-cost8-report-20260906/summary.json`. These artifact
paths are under `.codex-research/`. Resource and broad defended checks on this
build remain unrun; the change is neither integrated nor installed.

The owner's subsequent architectural direction supersedes the local tuning
queue above. Preserve this candidate as evidence and an engineering baseline;
the next priority is the [general planning research slice](STRAFE_MOVEMENT_GENERAL_PLANNING_2026-09-06.md).
