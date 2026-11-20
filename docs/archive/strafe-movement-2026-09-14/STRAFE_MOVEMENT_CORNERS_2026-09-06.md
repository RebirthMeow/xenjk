# Prepared ground connections, September 6

Delivery blocker: the complete generated Kejim routes still brake to rest at
genuine bends. The interval executor reduces walking without a meaningful
round-trip speed improvement. This experiment replaces some stopped bends with
native-validated continuous ground connections, using the existing full-route
observer rather than a new isolated fixture family.

## Frozen first candidate

Opt-in `bot_supportedCorners=1` with `bot_supportedSpans=1`; defaults stay off.
Production preparation proposes a quadratic fillet in the adjacent original
support intervals, with setback at most128 units and40% of either interval.
Both neighboring intervals must be level within one unit, the turn at most90
degrees, setback at least24, and minimum curve radius at least32. The minimum
radius implies a provisional ground speed cap `min(250,sqrt(600*radius))`.
Tight bevel clusters and failed proposals retain the original stopped corner.
There are no map coordinates or manually authored connections in this code.

Each <=16-unit chord receives fresh native checks: actual-body standing
support, four floor probes at a footprint enlarged by16 horizontally, and
a swept enlarged hull. The actual body support check prevents the enlarged
hull from bridging a hole beneath the real player. Require nearly flat WORLD support, unchanged floor
height, no lava/slime or hurt trigger. At most six traces per sample share the
existing global32-trace/frame preparation budget. The whole new plan publishes
only after preparation completes; original partial-prefix fallback is retained.

The shared follower separates local arc-length lookahead from the stopped
arrival target. Prepared turn points are crossings, with a speed envelope and
no jump request inside the turn. Original hop launch/cutoff conditions remain.
The follower checks current and velocity-projected next-step lateral error
within12 units, supported contact, height and entry speed. This projection is
a contract guard, not a native collision forecast. Native motion, pickups,
resources, respawns and actual outcomes remain authoritative.

## Decisions and case set

1. Fresh uniquely named native build; freeze sources during build/collection.
2. Paired old baseline/corner candidate two-team zero-force, no-Speed phase0
   smoke,120 game seconds, production mode6. Optional preparation diagnostics
   are outcome evidence only. Preserve every rejection/failure.
3. Only if complete trips improve without a new persistent failure, run the
   unchanged30-case resource matrix against the same-build baseline. Report
   first-trip time, idle and walking together; airborne time alone is not a win.
4. Native drop/body and defended development follow a useful solo result.
   Final fixed12-case1x/5x qualification, complete live cost and installation
   remain separate gates in the active plan.

Use `strafe_supported_batch.py --supported-corners [--supported-trace]` with
the exact build, then `strafe_supported_report.py`. Compare complete first
captures including preparation delays. The existing solo idle definition is
XY speed<50; walking is grounded XY speed50..<300. Do not substitute defended
travel metrics or exclude slow/refused cases. Accelerated fixed25ms collection
does not establish real-time cost.

Results will be recorded here and summarized in the active scorecard.

## First outcome and representation correction

Build `.codex-research/strafe-build-corners-v1-20260906`, source snapshot
`.codex-research/strafe-corners-v1-source-20260906`, paired smoke
`.codex-research/strafe-corners-smoke-v1-20260906`, audits
`.codex-research/strafe-corners-smoke-v1-audit-20260906`.
Baseline passes; candidate fails the unchanged two-capture gate. Both teams
remain alive without the audited ten-second stalls, but first trips slow from
57.400/57.025 to75.500/75.875 seconds and idle rises to24.11/24.09%.
No corner proposal is accepted. This is a failed result, not lower walking
progress, and the larger resource campaign is not justified for this version.

The retained node/CSV evidence identifies two problems. Most useful bends have
short navmesh bevel edges that the independent-vertex proposal cannot span.
The three eligible ordinary inward fillets per initial route fail native
clearance/support. Separately, the continuous follower advances inside the
old24-unit arrival radius onto an8.385-unit edge while still behind its start.
Clamped projected remaining distance is then below the12-unit stop radius,
producing zero commands for roughly two seconds until existing recovery acts.
Red example: (624.3245,1403.6846), previous (627,1426.75), next (634.5,1430.5).
The next goal is28.68 units away despite projected remaining8.385.

Version2 uses actual distance for the immediate stopped goal and retains the
old executor whenever no continuous connection is accepted. The shape producer
now spans at most four level vertices joined by<64-unit bevels and proposes
two cubic tangent connections: short handles(2/3 setback) for inward rounding,
then extended handles(2 setback + cluster extent) for an outside ground turn.
Setbacks, native clearance/support/hazard checks and12-unit tracking allowance
are unchanged. The control-polygon length bounds sampling to<=16 units, with
a129-point capacity refusal. Sampled analytic curvature limits ground speed;
near-cusps and radius<32 refuse. This is a bounded representation change based
on the failed route geometry, not a new map-specific rescue line. Native logs
now distinguish body-floor, footprint-floor and clearance/hurt refusals.

Repeat the same two-team paired smoke before considering the resource matrix.
Retain all failures. If this replacement still cannot provide useful route
connections, review the geometry/controller representation before sampling
further handle families or relaxing clearance gates.

## Second outcome and support representation review

Build `.codex-research/strafe-build-corners-v2-20260906`, source snapshot
`.codex-research/strafe-corners-v2-source-20260906`, smoke/audits at
`.codex-research/strafe-corners-smoke-v2[-audit]-20260906`.
Both arms pass the existing native/resource/lifecycle smoke audit. Candidate
first trips are57.275/57.350 seconds, idle5.456/5.187%, walking54.038/53.182%.
There are still zero accepted connections. The idle bug no longer occurs, but
this is not a corner capability or speed improvement. Do not expand the matrix.

After these two unsuccessful preparations, review the representation before
further native collection. Independent algebraic review shows the extended
cubic has a zero midpoint derivative for a single bend; the short-cluster
variants also fail the existing radius gate. Remove that option, rather than
sampling more handle values. Uniform parameter steps based on total control
polygon length also exceeded16 units on one segment. Use the rigorous cubic
derivative bound `3*max(control-edge-lengths)` to choose the step count.

BSP brush inspection of red source17/sample5 explains a native footprint
refusal: center approximately(1399.6792,2338.1832,1808.25) has native-body
support on flat brushes52/54. Of the enlarged footprint point probes, one at
(1368.6792,2369.1832) has no floor at1784; the other three have flat floor on
brushes52/54/91. Thus requiring floor beneath each extreme footprint point is
a different, stricter geometric predicate than native standing-body support.

Version3 changes the preparation representation to sampled player
configuration space. Keep the actual-body center floor sweep and perform
actual-body floor sweeps at each of four center offsets(±12,±12). Each must
reach flat WORLD support at the same standing height; enlarged16-unit hull
clearance and hurt/liquid rejection remain. This explicitly replaces the
full-footprint predicate; old full-footprint failures remain failures and are
not relabeled. The sampled offset-body checks are not a continuous-space proof.
Actual native full trips, resources, confinement and performance gates are
unchanged. No connection is accepted solely because its center is supported.

Retain only the ordinary clustered inward cubic. Repeat the same full-route
smoke before expanding collection. A successful prepared curve must actually
execute without contract departures and produce useful complete-trip movement.

## Third outcome and continuation

Build `.codex-research/strafe-build-corners-v3-20260906`, smoke
`.codex-research/strafe-corners-smoke-v3-20260906`, audits
`.codex-research/strafe-corners-smoke-v3-audit-20260906`.
Native preparation accepts four connections and refuses four per direction,
matching the independent BSP explanation. Both teams finish two native trips;
all existing smoke gates pass and neither records an interval-contract release.
First-trip time is57.525/57.700 seconds, idle5.954/6.023%, walking58.583/58.925%.
This establishes executed native corner connections, but not a useful total
speed/idle result. Preparation grows from1.600 to2.625 seconds per trip; movement
excluding preparation improves0.900/0.350 seconds. Ground curves also shift
hop cutoffs upstream and shorten two427.6-unit exit straights to315.4/315.6,
below the unchanged320 cutoff. Do not promote this ground-only candidate.

The accepted gentle bends are8.25/5.98 degrees with minimum radii1408/2141;
at425 speed they need about128/84 units/s² normal acceleration. Other accepted
curves have radii223..459 and remain ground-only. This supports a specific
air-continuation case without assuming all bends can be jumped.

Version4 retains ground mode1 and adds `bot_supportedCorners=2`:

- Execute the already validated, append-only prepared prefix while later
  connections are checked. The current prefix endpoint is a stopped arrival;
  incomplete candidate samples are never published. Preserve follower indices
  when the final plan is published and clear old prefixes on acquisition.
- For radius>=900 only, additionally validate the whole enlarged100-unit tall
  jump envelope and hurt/liquid checks. Any tall failure leaves a ground curve.
  Accepted air curves have speed limit `min(500,sqrt(200*radius))`. Tracking
  still requires12-unit lateral bounds, WORLD or airborne contact, height
  −8..100 above support and the existing speed departure guard. New jump
  requests are suppressed while the speed envelope asks for meaningful braking.
- All traces share the global32/frame budget: six per ground sample, seven
  while validating an air envelope. No per-frame collision forecast or file I/O
  is added to the pure follower. Full runtime cost is still unqualified.

CLI: `--supported-corners` selects1; `--supported-corners 2` selects the new
continuation. Repeat the same baseline/two-team smoke, retaining a same-build
mode1 control if needed for attribution. Only a useful complete-trip result
justifies the unchanged30-case resource campaign and defended development.

## Fourth outcome and Speed command regulation

Build `.codex-research/strafe-build-corners-v4-20260906` passes both native
30-case resource arms. The same-build smoke is faster for both teams:
56.675/56.500 seconds versus57.400/57.025, with idle4.279/4.381% and
walking57.962/57.832%. Eight curves execute per first trip. One gentle curve
per trip is actually airborne for23 frames(0.575s), after one native launch;
it reaches64.027 units above support and stays near250 speed. The opposite
gentle bend stays grounded. This proves curved airborne traversal, not faster
strafe acceleration there. Roughly one second of preparation now overlaps
105–114 units of useful motion; that is the primary elapsed-time gain.

Resource comparison at
`.codex-research/strafe-corners-resource-v4-audit-20260906/comparison.json`:
mean time56.934→56.583s, idle5.244→4.933%, walking59.218→57.244%.
All30 walking comparisons improve, but two blue Speed8000/phase3,6 cases
regress by3.950/4.250s. Phase0 also has the same contract departure despite
finishing sooner than baseline. Keep these failures; do not promote v4.

The three departures occur inside a successfully entered ground curve.
Force Speed remains active; a full forward127 pulse raises speed to
292.204/295.013/292.981, above the unchanged250+40 contract bound. Lateral
error is under1.5 units, height error−0.125 and contact WORLD. Repeated binary
throttle pulses around a lower speed target cause the failure; the guard
correctly releases. Two cases then interrupt replanning with corner recovery.

All four v4 pit/reachable-drop regressions pass. The static body case also
completes with its existing sampled progress audit. Evidence:
`.codex-research/strafe-corners-regression-v4-20260906`.

Version5 adds optional legal analog ground throttle to the local command
selector. `Bot2_MoveControlLimited` uses
`floor(127*preparedWishLimit/nativeWishSpeed)` and predicts that same quantized
input. With native Speed425 and a250 curve limit, magnitude74 requests about
247.6 wish speed. The native PM_CmdScale implementation already supports these
inputs; player state, force accounting and movement physics are unchanged.
The old `Bot2_MoveControl` API passes no limit and retains full127 commands.
Airborne and jump-request commands also retain full throttle. The curve's
physical acceptance/departure bounds remain unchanged.

First rerun all three blue Speed8000 phases, then the unchanged30-case matrix
and body/drop controls on a fresh build. Compare the new baseline arm with
the prior baseline as a regression check for the retained legacy wrapper.
The fixed defended qualification and cost checks remain open.

The fresh v5 blue Speed8000/phase0,3,6 controls all pass native audit with zero
contract releases; first trips53.900/55.100/53.650 seconds. Evidence:
`.codex-research/strafe-corners-speed-v5-20260906` and its `-audit` sibling.
Phase3 still waits0.525s at a retained short bevel, until Speed expires.
Its remaining next-goal distance is approximately24.96, just outside the
24-unit arrival set. The requested speed around51.85 is below half a full
Speed acceleration pulse, so the binary uncapped arrival command selects zero.
This is the same actuator-resolution issue at a stopped arrival rather than
inside a curve. There is no physical obstruction or recovery intervention.

Version6 applies the same quantized native throttle cap to any lower ground
velocity intent in the continuous follower, including arrival/braking:
`min(targetSpeed,preparedLimit or nativeWalkSpeed)`. Far full-speed travel,
airborne/jump commands and the original controller API remain unchanged.
This keeps the existing12-unit intent radius and24-unit arrival set instead of
tuning their margins to individual Speed phases. Recheck all three retained
controls and the full matrix; native acceptance bounds and case inventory stay
unchanged.

## Sixth outcome and recovery connection handoff

Build `.codex-research/strafe-build-corners-v6-20260906` passes all three
retained blue Speed controls, both full 30-case resource arms, and all four
drop controls. The old API's baseline travel is unchanged. Solo mean idle is
5.244→4.219%, walking 59.218→57.592%, trip time 56.934→56.613 seconds.
All 30 idle comparisons improve; one walking percentage and 18 trip-time
comparisons regress. There are zero interval-contract releases. The ordinary
static-body case first crosses 64 units at 3.775 seconds, with no sampled
confinement. These are scoped results, not full movement acceptance.

The four defended development cases fail: red travel idle/walking changes
5.293/33.151→7.159/32.518%; blue 5.511/34.510→4.064/42.732%. Candidate red is
confined for 8.25 seconds at levels 99925..108175 near the enemy stand, while
carrying toward a distant home objective. Three westward recovery attempts
advance only about 4.57, 6.75 and 8.51 units before aborting. The chaser stays
in close contact to the south. Speed remains active and force is 26. The first
two sampled commands wish toward 166.003 and 148.008 degrees while the checked
straight escape points west at 180 degrees. The original recording does not
contain per-frame trace refusal reasons; the exact collision mechanism remains
a hypothesis. A separate trace-enabled defended diagnostic retains additional
horizontal-bound and floor-missing refusals, and is not an exact replay.

Evidence: `.codex-research/strafe-corners-resource-v6-audit-20260906/`,
`.codex-research/strafe-corners-regression-v6-20260906/`,
`.codex-research/strafe-corners-defended-v6-20260906/`, and
`.codex-research/strafe-corners-defended-v6-trace-red-20260906/`.

Next declared correction: optional `bot_recoveryAlign=1` makes the command
honor a straight recovery connection. Turn toward the freshly revalidated
endpoint with zero translation until heading is reachable in the current
360-degree/second yaw step and lateral momentum is at most 5 units/second.
Then request forward-only legal analog ground input for
`min(120,4*distance)` wish speed. Native friction, floor and collision own the
actual motion; the per-frame corridor checks remain authoritative. Preserve
the 16-unit arrival set, 96.125 horizontal validation bound, 2-second step
deadline, 28-candidate scan and 1000ms abort retry. No clearance or outcome
gate is relaxed. The original command path remains the default control.

Extend the existing one-shot corner setup with explicit yaw (default zero),
validated before mutation and echoed from actual native state. On one fresh
build, compare alignment off/on on these predeclared static contact controls:

| Case | Runner XYZ | Body XYZ | Initial yaw |
|---|---|---|---:|
| contact-1 | 2527.752,5586.060,1912.125 | 2531.395,5555.935,1912.072 | -58.997 |
| contact-2 | 2521.574,5588.788,1912.125 | 2522.988,5558.662,1912.072 | -85.990 |
| contact-3 | 2513.067,5588.999,1912.125 | 2509.280,5558.873,1912.072 | -130.979 |
| contact-facing | same as contact-1 | same as contact-1 | 180 |

All are red, carrying, force 26, native Speed with 8000ms remaining, corner
mode 2, 30 native seconds at 5x, with existing trace diagnostics. Compare
first 64-unit displacement, idle, confinement and aborts. Retain setup refusals.
These static bodies do not reproduce a chaser's reactions; use them to test
heading sensitivity, then repeat both-team defended development before any
full qualification. The original defended failure remains retained regardless
of the static controls' outcome.

The alignment experiment is complete on
`.codex-research/strafe-build-recovery-align-v1-20260906`. All eight contact
setups and observations are valid. Both arms escape without sampled confinement
or recovery aborts; first 64-unit displacement is 3.775s in all four controls
and 4.275/4.025/4.025/4.025s with alignment. Static bodies did not reproduce the
moving-chaser failure and the candidate is slower on this measured escape.
Its four defended development cases also complete, with no sampled confinement
in either arm. Red travel idle/walking is 5.418/37.684→7.321/33.088%; blue is
7.168/33.608→4.897/43.798%. One unpaired run per team/arm is descriptive; this
does not establish a fix or satisfy the continuity nonregression gate.

Keep alignment off by default. Its five-unit lateral-velocity threshold is not
a clearance certificate, especially against a body only about 0.125 units away;
the static cases start at zero velocity. Zero-destination scan/abort commands
also coast rather than using the old active braking command, and native slick
or moving/body support can prevent lateral settling before the unchanged
timeout. A future general connection executor must validate the actual short
commanded movement against current clearance, not assume alignment is enough.
Evidence: `.codex-research/strafe-recovery-align-contact-v1-20260906/` and
`.codex-research/strafe-recovery-align-defended-v1-20260906/`.

The unchanged drop/body regressions additionally reject alignment for
promotion. Red-low reaches 64 units at 3.175s, failing its existing 3s deadline;
blue-high/red-high/reachable pass at 1.175/1.175/0.900s. The original static-body
control crosses 64 units at 4.025s without sampled confinement. The retained
audit is `.codex-research/strafe-recovery-align-regression-v1-20260906/`.
Do not weaken the deadline or expand the measured set to hide this failure.

This closes this local candidate comparison, not Kejim movement. The main speed
work proceeds through the ordered running/jumping chain declared in
[the fast-trip protocol](STRAFE_MOVEMENT_FAST_TRIP_2026-09-06.md). Preserve the
existing corner/Speed/body failures as regressions for that executor.
