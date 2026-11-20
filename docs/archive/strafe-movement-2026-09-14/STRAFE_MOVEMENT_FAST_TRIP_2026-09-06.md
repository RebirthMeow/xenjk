# Kejim Generated Fast Trip v1

This is the chronological experiment protocol, including retained failed
iterations. Use the [implementation guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md)
for the current execution order and the [scorecard](STRAFE_MOVEMENT_PROGRESS.md)
for current measured status. Earlier "next" declarations below are historical.

Declared before chain implementation and collection. This is the next concrete
step in delivery checkpoints 2–4 of the active plan. The completed support and
corner experiments establish a generated full route and lower solo idle, but
barely improve its approximately 57-second no-Speed round trip. Further ground
fillet or actuator tuning alone is not the route-speed solution.

The retained zero-force phase-0 comparison explains the bottleneck. Baseline
airborne mean XY speed is 370.9/374.4 units/s (red/blue), versus 343.9/343.5
for corners-v6, despite airborne time increasing from 19.23/19.40s to
20.98/21.65s. Total XY distance remains about 14,700 units per round trip.
Ground idle is only 2.48/2.45s in the candidate; eliminating every idle sample
would still fall short of the declared 10% speed target. Preserve speed through
the flight/landing chain instead of merely increasing hop count or caching
preparation waits. Evidence:
`.codex-research/strafe-corners-resource-v6-audit-20260906/locomotion-speed.json`.

## One prepared ordered movement chain

Use the two existing automatically generated Kejim stand-to-stand routes as
the topology. Preserve their supported running/hopping intervals as fallback.
Propose ordinary-jump connections between successive supports on those routes,
including bends where an airborne chord can cross unsupported space. Do not
restart the old stationary, off-route gap cohort or enumerate a global lattice.

The prepared artifact must retain, for each connection:

- Source route indices and polygon/support identity, with map, geometry, hull,
  physics, source and binary bindings.
- Takeoff and landing geometry, a feedback policy, and its measured supported
  continuation into the next interval.
- Entry and exit position, velocity, yaw, contact, jump latch and resources.
- Complete native validation evidence, refusals, and the supported fallback.

Evaluate successors from the actual native terminal state of the predecessor.
Resetting each bend to rest, retaining only a shared command prefix, or matching
only position does not establish compatibility. Keep at most two useful
continuations per chain frontier, drawn from a declared finite set of geometry
proposals and the existing eight transition-policy variants. Rank by complete
travel time while retaining safety/refusal outcomes; do not optimize captures,
airborne percentage or the number of predictions as substitutes.

This artifact starts as a bounded ordered route, not a full arbitrary-start
map graph. It must be executable by the production shared controller before
its preparation is treated as a delivered capability. Native player/flag
lifecycle remains in the engine. Entry mismatch or moving-body interference
requires bounded actual-state validation/connection or an explicit fallback.

## Implementation order and reuse

1. Extract the retained routes and associate their support intervals with the
   existing `strafe_map_preparation.py` polygons/portals/adjacency. Keep every
   original interval and rejection; geometry proposals are not traversability.
2. Reuse the native support interface and extracted `bot2_forecast.c` rollout,
   with `bot2_movebench.c` restoration/parity evidence, for offline connection
   discovery. Reuse
   `bot2_transition.c` takeoff anticipation, landing-height steering and contact
   recognition, but add a separately declared moving-continuation contract.
   The legacy phase 4 brakes at the onward point and cannot already chain.
   The separate moving API, actual-supported-state caller and native receiver
   are implemented and measured below. Their ordered discovery sequences retain
   useful motion evidence but fail the declared full-trip speed target.
3. Propagate actual terminal states through successive useful connections and
   persist the result. Check the actual production movement settings and weapon;
   old single-transition fixtures have different settings from ordinary play
   and do not qualify the new integrated controller.
   In particular, the legacy single-transition setup rejects nonzero entry velocity; its guarded
   forecast covers at most four seconds and refuses active powers or crossed
   item/trigger regions. Preserve that fixture's contract. The chain needs a
   separate actual-state entry/exit interface and must return event/lifecycle
   ownership to native play rather than silently weakening those guards.
4. Load the artifact into the shared path execution layer. Keep planning work
   offline; runtime performs bounded selection, actual-state connection,
   validation and feedback tracking. Recovery uses the same connection contract.
5. Execute complete native trips with pickup, return and capture. Only a useful
   route result justifies expanding resource, defended and cost qualification.

No manually authored rescue coordinates or production map-name branches are
allowed. Independent rotation/height cases and the later Dantooine transfer
remain checks on generality. Do not start a separate instrumentation project
when the existing native observer, trace and parity machinery answers the gate.

## Fixed development gate

Reuse the existing mode-6 observer with both teams, zero starting force, no
Speed and phases 0/3/6: six cases per arm. Compare the prepared chain against
the unmodified corner-v6 controller mode on the same fresh build. Preserve the
30-case matrix's native pickup/capture, health, resource, clock, provenance and
inventory gates. Observe the full 120 game seconds in every case.

For a useful speed result, require no slower individual first round trip and
at least 10% lower median first-trip time for each team. Report first-trip idle
and walking alongside time and retain any regression. This is a newly declared
development speed target, not a redefinition of the existing defended gate.
Airborne time alone cannot pass it; a missing capture is not a completed trip.

If this route representation fails two iterations, review the representation
and actual failure evidence before another refinement. Do not expand sample
counts merely to find a favorable isolated case.

After this gate: unchanged 30-case resources, retained corner/Speed/drop/body
regressions, the fixed 12-case defended 1x/5x comparison, representative total
eight-active-bot runtime cost, normal MP play, accepted-build installation and
Dantooine transfer. Until those gates pass, Kejim remains unfinished.

## Backbone extraction completed

`tools/strafe_route_backbone.py` extracts the first complete native route snapshot
from each retained corner-v6 zero-force phase-0 recording. It verifies every
snapshot group, native capture events, the accepted audit, historical build and
loaded binary bindings, and the map checksum against the support inventory.
It does not require historical sources to equal the subsequently edited checkout.

Artifact: `.codex-research/strafe-fast-trip-backbone-v1-20260906/backbone.json`.
SHA256: `a0b47d092d6a946c758ee18da49b970d2ebe2fab8217c6ccb4396ed331bc0200`.
The red route has 89 nodes / 88 intervals; blue has 85 / 84. Both retain four
accepted and four rejected corner proposals. Red has 15 intervals with missing
support associations and 24 with ambiguity; blue has 16 and 26. Analytic interval
clipping preserves all missing/ambiguous parameter ranges, including stacked
support layers, without selecting a nearest polygon to conceal the uncertainty.
Six focused in-memory tests and complete interval/provenance checks pass.

This completes implementation step 1 as preparation input, with **zero new motion
proposals**. Logged coordinates are rounded to three decimals; the recording lacks
yaw/full player state and a loaded-navmesh hash. Polygon fans omit detail triangles.
Fresh full-precision native validation and actual-state entry/exit evidence remain
required before any executable bake. The initial generated copy is retained under
`.codex-archive/20260906T205004898Z-ccd7761e9ce14770a71236a578a0063f/`
with its restoration manifest.

## Native boundary review

`BuildForecast` remains a private movebench adapter over its client, clock,
policy, command generator, export and four-second episode. The guarded rollout
mechanism is now extracted behind a caller-owned request: actual `pmove_t` input
template and player/client state, copied controller state, command callback,
explicit first command and bounded horizon. The legacy fixture retains its
existing restrictions and evidence format; supported AI does not call it.
The new moving connection must preserve the current weapon and momentum and
return a supported moving terminal state, not apply fixture setup to a live root.

Obtain this template at the native pre-Pmove boundary already reached through
`Bot2_MoveBenchPmove` in `g_active.c`. AI command generation happens earlier;
it does not possess all finalized native movement inputs. The first command there
has already undergone native 8ms rounding. Forecast later raw 25ms server commands
through the same rounding rule; do not approximate them as uniform 25ms Pmove
steps or compare unrounded and rounded commands as if they were the same input.
Execute the already submitted first command unchanged in every branch; initialize
connection yaw, time and jump latch from that shared step before generating the
next command. Use `Bot2_GameStepMsec()` for the game clock in accelerated play;
the production rounding produces 24/32ms integration, not a fixed 25ms sequence.

The existing guards restore declared Pmove globals, level clocks and temporary
animation projections, then detect remaining world mutations. That is **not
arbitrary world rollback**. A forecast that invokes unsupported GAME side effects
must terminate its private offline episode. It cannot be treated as an ordinary
refusal followed by live movement in a possibly changed world. Native triggers,
pickups, resources and other lifecycle events must run authoritatively between
supported rollout portions. Old parity evidence covers a static blaster/no-active-
power fixture; the new production settings and weapon require their own evidence.

The implementation extracts this mechanism into `bot2_forecast.c/.h`, with
the legacy movebench as adapter. Caller continuation bytes, terminal animation
and frame offset preserve the inputs needed by successors. Completion can return
before another command, separately from refusal. A separate moving transition
API preserves weapon and explicit approach/onward speeds; it requires two supported
observations and 32 units of supported onward progress before returning an exit.
It is not yet an executable full route. The extracted helper retains strict
trigger/item, material, liquid, body and active-power refusals. Validation,
callback dispatch and terminal copying change measured overhead; earlier timing
numbers do not automatically qualify this interface's cost.

## Interface and route preparation qualification

The frozen v2 collection uses fresh build
`.codex-research/strafe-build-moving-interface-v2-20260906` and completes the
declared preservation checks:

| Check | Measured result | Evidence |
|---|---|---|
| Explicit first command and split 80+80 versus 160 steps | All three cases pass: ordinary, fixed+float, and fixed+float with XY velocity (50,-16). Whole PS, effective commands, policies, contacts and restoration agree. Fixed+float records 140 24ms and 20 32ms command steps per case. | `.codex-research/strafe-forecast-root-v2-20260906/audit.json` |
| Legacy transition calibration/reference/controls | All 43 expected outcomes pass. Useful policies are 8/8 flat, quarter-turn, diagonal and raised, and 7/8 lowered: 39/40 total. Lowered candidate 6 remains unsuccessful, matching the historical crossing calibration. | `.codex-research/strafe-moving-interface-transition-v2-20260906/audit.json` |
| Historical movement preservation | All 41 positive transition/reference recordings match exactly across the 11 compared movement, command and relative-time fields. This comparison is narrower than whole-PS equality. | `.codex-research/strafe-moving-interface-transition-v2-20260906/baseline-equivalence.json` |
| Existing native fixture matrix | All 18 expected outcomes pass, including its three negative controls. | `.codex-research/strafe-moving-interface-matrix-v2-20260906/summary.json` |

The split successor starts from the first portion's terminal PS, policy,
animation, frame offset and continuation bytes without moving or resetting the
real client. Its full trajectory comparison runs inside the bound native module;
split trajectories are not independently serialized. These checks establish
preservation of the extracted interface and old fixture behavior. They do not
qualify full-server prediction or the new moving-exit controller's trajectories.

Command-only tests already compare 8,192 old/new legacy states and exercise
weapon preservation, command-free moving exits, late-touchdown rejection,
refusal atomicity and malformed state guards. Evidence:
`.codex-research/strafe-moving-exit-command-v3-20260906/`.
The initial v1 harness compile failure and v2 earlier contract tests remain.
The moving API itself is command-contract-tested only; no new motion qualification
is inferred from the legacy native campaign.

The on-route generator declares 20 frontiers (10 per team: four validated curves
and six stopped turns), with two finite anchor offsets of 32/64 route units.
Approach is 128 units before launch and onward is 96 after landing. Chord bounds
are 32..384 XY units and height delta at most 32. Neighbor-frontier overlap and
ordered support regions remain explicit. Forty proposals yield 36 with unambiguous
anchors and 216 native point/sweep probes. Four proposals have missing anchors;
11 of the 36 cross source polygon seams and cannot use the legacy single-polygon
adapter. All are retained. Evidence:
`.codex-research/strafe-route-connections-v1-20260906/`.
All 216 queries now pass on the fresh interface-v2 build: four points and two
sweeps for each of 36 eligible proposals. The full denominator remains 40,
including all four missing-anchor refusals. Twenty-five accepted proposals emit
legacy-adapter descriptors; the 11 source-polygon seam cases remain collision-
accepted but incompatible with that adapter. The native summary verifies exact
proposal IDs, probe order/roles and declared float32 payload inputs. Evidence:
`.codex-research/strafe-route-connections-native-v2-20260906/route-probe-summary.json`.

These point/support and straight-sweep observations alone do not establish
airborne clearance, supported continuation across seams or moving entry/exit
compatibility. The subsequent caller evaluates actual supported route states;
its motion evidence and refusals are recorded below. The
executable ordered chain and six-case full-trip speed gate remain open. The
installed runner and frozen route kernel are unchanged.

## Actual-route entry qualification — declared before collection

**Named blocker: does the moving-exit policy and native predictor work from
actual production weapon/player state?** The opt-in `bot2_connectionbench.c`
caller is now implemented inside the existing continuous mode-6 observer.
The initially declared build `.codex-research/strafe-build-route-entry-v1-20260906`
failed compilation on an undeclared cheats variable. That failure is retained;
the corrected fresh v2 build below collected the unchanged case set.

Run all 25 collision-prepared adapter descriptors, using each encoded team,
phase 0, zero initial force, no Speed, 120 game seconds and corner mode 2 with
the same production baseline settings. Retain the original 40-proposal denominator:
four geometry refusals and 11 source-polygon seam cases remain outside this
adapter's executable inventory. A missing root or refused forecast is a result;
do not adjust the 48-unit entry radius or remove cases after observing the fleet.

The first eligible root must be within 48 units of approach, supported by WORLD,
with at least 80 units/s of velocity toward launch, before first flag pickup or
capture. Verify that last restriction against the complete observed history,
not merely whether the flag is currently carried. All eight policy candidates
start from the same actual Pmove root and immutable, already-rounded first
command. Proposed speeds are 250 approach, 600 air limit and 400 onward, with
minimum exit speed 80 and at most 160 Pmove calls per candidate. Select the
shortest successful supported moving exit, then execute feedback from the same
first step. No player-state, velocity or force reset is allowed at entry or exit.

Retain all eight forecasts, terminal PS/policy/continuation, and paired one-step
prediction/native player states in the binary recording. Every selected live
step must exactly match its one-step prediction across the whole player state.
Compare later forecast movement and commands separately; force regenerates and
native lifecycle runs normally between actual steps. This is not whole-server
prediction. World mutation or failed restoration terminates the private world.

The live exit must meet the same moving contract: supported onward passage,
two supported observations, at least 32 units of onward progress since touchdown
and the declared minimum forward speed. The ordinary path controller then
observes that same actual PS. This handoff is diagnostic; it does not establish
a compatible baked chain, the six-case full-trip speed gate or product promotion.
One declared fleet answers this blocker before changing the representation.

## Actual-route results and continuation decision

Fresh build `.codex-research/strafe-build-route-entry-v2-20260906` and the retained
`.codex-research/strafe-route-entry-v2-20260906/plan.json` bind all 25 cases.
`connection-report-v1.json` independently verifies 23 successful native moving
exits, one no-useful-policy result (`red:f04:v1`, all eight refuse in phase 3)
and one no-root result (`blue:f02:v1`). All 1,522 selected live steps exactly
match whole-PS one-step predictions and the selected forecast's origin,
velocity, ground entity and submitted commands. Longer-horizon differences in
force, its regeneration deadline and event sequence are retained explicitly.
Nine in-memory auditor tests cover corruption, selection, parity and denominators.

`resource-audit.json` passes every full-observation/native/resource gate in all
25 runs. The two same-build inactive controls pass and preserve all 4,800 CSV
movement/resource/command rows against corner-v6; see
`.codex-research/strafe-route-entry-inactive-v2-audit-20260906/summary.json` and
the inactive campaign's `baseline-equivalence.json`. The complete measured C
source tree is retained under the entry campaign's `source-snapshot/`.

The successful local exits are not a speed pass: 17/23 full trips are slower,
18/23 have higher idle fraction and 5/23 higher walking fraction. Every live exit
causes the ordinary path's clock-gap reset; full readiness takes 50–1,250 ms
(median 800). Nineteen exits exceed 12 units lateral offset from the onward line,
13 exceed 24, and the maximum is 61.021. Current phase-3 constant onward velocity
does not eliminate lateral position error. These comparisons and timings are
retained in `resource-audit.json`, `exit-line-diagnostic.json` and
`continuation-review.json` in the entry campaign.

The next implementation must keep the transition within the supported path's
ownership and clock. Geometrically resolve an unambiguous downstream interval
and parameter range for each published path generation; historical node indices
remain provenance. Add a native-validated tracking tail and receiving envelope,
then atomically resume that interval from actual PS, view, latch and resources.
Ordinary straight-path preparation validates nominal body sweeps: its 24-unit
crossing tolerance is not evidence of cleared lateral space. Reuse a validated
corner tube only where its preparation actually applies. Do not solve this by
index skipping, larger tolerances, or an exit check that cannot steer into its
own contract. Retain legitimate objective/death/recovery invalidation.

Evaluate successor connections from these actual compatible exits, retaining at
most two continuations per frontier and ranking complete travel time. Runtime
should retrieve prepared policies and perform bounded tracking/body checks;
the diagnostic's 446–662 forecast Pmove calls per root and 53–83 guarded live
one-step checks remain offline evidence machinery. There is no new runtime-cost
claim, executable-chain qualification, installation or frozen-kernel change.

## Joined receiver protocol (declared before collection)

The optional `--connection-join` experiment retains all 25 native-adapted cases
and the original 40-proposal denominator, phases/settings/radius/policy set and
160-step horizon above. Association or clearance refusal is a recorded outcome,
not permission to substitute an easier case. The jump and a ground tracking tail
are forecast together. Native support probes at the center and four 12-unit XY
offsets, plus an inflated standing-body sweep, prepare the receiving interval
from onward to its end at no more than 16-unit spacing. This protocol requires
flat support (one-unit height variation, normal Z at least .99), WORLD floor and
no liquid/trigger/item envelope contact. Preparation failure remains explicit.

Binding uses the actual published route, an unambiguous forward interval, and
a publication generation preserved outside route resets. Record the whole path
and its hash. Completion requires WORLD support, current and next-step lateral
error at most 12, at least 80 forward speed, heading within 15 degrees, height
within eight units, an eight-unit finite end margin and the prepared speed cap
when one exists. The supported owner continues its normal authority and clock
checks throughout the jump. A validated resume changes follower progress only.
The ordinary path must produce its command on the same tick, without a route
reset or preparation pause. Record and independently verify that first native
resumed step against a whole-PS one-step forecast, separately from transition
frames. Compare post-handoff progress, full-trip time, idle and walking against
the retained entry-v2 and inactive controls. No local joined success alone
qualifies the full-route speed gate or changes the installed runner.

## Joined result and ordered successor development

The first joined fleet (`.codex-research/strafe-route-join-v1-20260906`) retains
all 25 outcomes: 15 live joins, six association refusals, two flat-support
preparation refusals, one no-useful-policy result and one missing root.
Independent binary audit verifies 1,121 transition steps and 15 additional
resumed native steps, unchanged publication identity and same-tick path command.
All 25 resource/lifecycle audits pass; two inactive controls preserve every
compared CSV movement/resource/command row against entry-v2. All 15 joins lower
first-trip idle fraction; eight lower complete first-trip time and seven remain
slower than the inactive control. Nine have a higher walking fraction. The result
establishes moving rejoin, not the complete-route speed target.

Review found that the first receiver projected its bounds for nominal 25 ms,
while native rounding produces 24/32 ms steps. All 15 observed resumed steps
remain within the 12-unit bound (maximum 9.178), so that collection remains valid
within its measured scope. The next build uses a conservative 32 ms projection;
repeat the same 25-case cohort before broader qualification. The joined-v1
source snapshot retains all 1,090 native files and its runner/auditor helpers.

Next, generate an ordered proposed schedule from the verified joins, retaining
at most two variants per frontier ranked by the isolated complete first trip.
This ranking only proposes schedules. Execute each primary schedule plus each
single alternate-variant schedule in a complete native episode, then rank the
observed complete trips. Each later connection is evaluated from the actual
native state left by preceding motion; never copy an isolated terminal state.
All eight feedback policies remain the finite per-root offline set. Preserve
every declared stage as joined, refused, missed or unreached. A missed entry can
advance only when the actual follower has passed the uniquely associated
receiver; native pickup ends outbound connection discovery. Keep the whole
120-second observation and resource audit. Retain normal supported motion
between connections and after refusal. The sequential native search is an
offline discovery tool, not a runtime-cost claim or a baked policy executor.

The primary and one-variant-flip schedule comparison is development selection.
Freeze the chosen schedule before the independent both-team 0/3/6 gate; include
the supported fallback as a candidate rather than promoting a slower chain.

Ordered iteration 1 executes eight schedules and two same-build controls in
`.codex-research/strafe-route-chain-v1-20260906`. All ten complete native/resource
observations pass. Blue's f07-v0 alternate joins all four connections in one
episode; its first round trip is 56,950 ms versus 57,050 ms inactive. All red
schedules join only two of five: waiting until the missed proposal's receiving
interval ends hides subsequent approach windows. Keep these misses and the
unchanged native source snapshot as failed coverage evidence.

Iteration 2 changes proposal expiration only. Unambiguously associate approach
with the forward published interval. Retire an unexecuted proposal when actual
follower progress passes that interval, or when projection on the same interval
is more than the unchanged 48-unit root radius beyond approach. Fall back to
the stricter receiver-passed rule when entry association is unavailable. This
does not move the player, change follower indices or enlarge entry tolerances.
Repeat the same eight schedules plus controls; select by complete observed time
and retain all native failures before the fixed timing-phase comparison.

## Ordered results, fixed speed gate and representation review

Iteration 2 is retained under `.codex-research/strafe-route-chain-v2-20260906`.
All eight schedules and two controls have independently valid native evidence
and resource/lifecycle observations. Each red schedule now joins four of five
connections at phase 0; the skipped f01 entry overlaps the preceding connection's
exit. The primary red sequence also demonstrates an immediate actual-state root
for f03 after f02's resumed step. Blue's f07-v0 alternate joins all four stages.
The earliest entry expiration is a current forward-traversal rule; future
disturbance/runtime use must additionally preserve an entry while actually
inside its radius and avoid classifying backward projected motion as a forward
miss. Those cases are outside this private solo collection's scope.

Selection by full observed trip, including the inactive fallback, is frozen in
the campaign's `selection.json`: red-flip-f03-v0 and blue-flip-f07-v0. The fresh
measured build is `.codex-research/strafe-build-route-chain-v2-20260906`.
The independent phase gate retains all 12 runs under
`.codex-research/strafe-route-chain-phasegate-v2-20260906`. Its
`chain-report-v1.json`, `resource-audit.json` and `speed-gate.json` verify complete
0/3/6 inventory, module/source bindings, native movement and resources. There are
21 live joins and six missed entries among 27 declared stages, with 1,556 exact
transition one-step/native PS pairs and 21 separate exact resumed steps.
Blue completes four-of-four at phases 0 and 6; red joins 4/4/3 and blue 4/2/4
respectively. Misses remain in evidence, with ordinary supported fallback.

Every selected trip is faster and has lower idle and walking fractions than its
matching control. Median first trips are red 57,275→56,700 ms (1.004%) and
blue 57,050→56,950 ms (0.175%). The 10% target **fails** for both teams. No runtime
bake, defended/resource expansion or installation is justified by this result.
The independent gate auditor is `tools/strafe_connection_speed_gate.py`; it
checks the pre-collection selection's exact six-case condition set, so omitting
an entire timing phase cannot produce a pass.

The required review after two iterations finds a speed representation problem.
At phase 0, red's idle falls 2,475→2,025 ms and walking 33,425→31,625 ms, but its
airborne mean speed falls 343.902→328.233. Blue's idle falls 2,450→2,025 ms and
walking 32,575→32,500 ms, while airborne mean falls 343.457→329.074. Actual XY
distance grows 14,685.8→14,748.5 red and 14,688.5→14,761.2 blue. These are measurements
through the first native capture, not inferred motion from command intent.
Excluding idle, moving time improves only 75 ms for red and worsens 325 ms for
blue. Most observed gain therefore comes from reduced idle, rather than faster
strafe travel.

Current proposals pin landing 32/64 route units beyond each bend. The moving
policy requests `(landing - actual origin) / remaining ballistic time`; 600 is
only a speed cap. A nearby point can therefore request a slow flight despite
the high cap. The approach requests 250 and the ground receiver tail converges
toward native walking speed. The ordinary follower then retains its landing-room
and stopped-arrival rules. The successful handoff machinery cannot make those
fixed short point-hit flights into a fast route. Increasing the number of local
variants or the airborne fraction would repeat this limitation.

The next bounded implementation is a **landing and continuation interval across
two successive supports**, evaluated jointly from the actual moving root to the
second receiver. Derive candidate touchdown ranges from the known support
corridor and ballistic reachable travel, retaining actual hull/ceiling/height
checks. Optimize the touchdown location, carried speed/direction and onward line
together; overlapping approaches become alternative connections in this local
window. Include the unchanged supported run and the present point-hit connection
as controls. Use existing native rollout/restoration, the receiver and the
ordered actual-state executor, keeping at most two useful continuations per
frontier. Declare the finite range/policy set before the next native collection.

Judge the result by complete travel time through the second receiver and then
the same full-route 0/3/6 gate, including all refusals and resource/lifecycle
checks. This changes the connection's motion representation, not the frozen
strafe kernel or navmesh. After it passes: persist validated policies and entry
conditions, implement bounded runtime retrieval/feedback, then retain the 30-case
resources, drop/body/Speed and recovery regressions, 12 defended 1x/5x cases,
eight-bot cost, normal MP and accepted installation, and Dantooine transfer.

## Two-landing window v1 declaration (before collection)

The opt-in `bot_connectionWindow=1` / `--connection-window` mode keeps the
original adapter25 cohort and all40 proposal denominator. It requires joined
ownership. At the untouched moving native root, propose reference speed
`min(600, max(1.7 * basespeed, XYspeed + 100))` and travel `D = .8 * speed`.
The .8 seconds is a proposal heuristic informed by native one-pulse flight;
the minimum-height Force Jump behavior makes a 225/gravity-only flight-time
estimate inappropriate. Native Pmove determines every accepted flight.

Bind the original receiver interval R1 on the actual publication. Window W1
intersects launch projection plus `[.6D, 1.25D]` with `[16, length(R1)-32]`.
W2 begins from W1's midpoint plus the same arclength range. Use R1 again when
that entire range fits before its last96 units; otherwise advance over its
remainder and at most four flat hopping seams of at most64 units to the next
long eligible interval. Intersect there with `[16, length-96]`. Both ranges
require width32 or greater, flat support, hopping permission and zero speed
limit. Refuse empty/incompatible associations. The common final receiver is
64 units beyond W2's maximum, with at least32 units remaining in its interval.
Native preparation checks the first tube beginning at the earlier of W1 and
the legacy receiver's minimum, and the second beginning at W2's minimum.

Evaluate exactly17 policies from the same raw PS and immutable first submitted
command to that common final receiver, with a common256-step/6.4-second cap:

- IDs0..7 choose two ordinary jumps and finite landing ranges. Their bits
  select first launch0/24 units earlier, reference speed1/1.25 (capped600),
  and lateral correction time .75/1 of projected descent. Actual momentum
  determines airborne intent; supported observations and finite landing
  bounds must hold for both flights.
- IDs8..15 run the unchanged point policies0..7 to the first receiver, then
  resume a private copy of the published ordinary follower toward the final
  receiver. ID16 runs that ordinary follower from the initial actual root.

Choose shortest completion with ties preferring16, then8..15, then0..7,
ascending within each kind. Retain all17 outcomes and at most two useful
continuations for later baking. This is an offline search experiment, not
runtime cost evidence. Extra state retains the16-byte wire slot: flight and
point phases0..7 use landing continuation; control phases8/9 use the private
path follower. The policy remains20 bytes. New records declare
`schema=connection-window-v1`, `window=1`, `horizonSteps=256`, `candidates=17`;
legacy records retain their original schema and160-step cap.

Selected live commands must retain native one-step/full-PS equality and the
same publication generation/clock. Final receiver resume and the separate
ordinary resumed step remain mandatory, with no pose, velocity or control
frame reset. Preserve every no-root, binding/preparation refusal and failed
candidate. Compare local complete times by kind, then advance useful results
to actual sequential route execution and the unchanged both-team0/3/6 speed
gate. This declaration does not establish native feasibility or qualification.

Window v1 collection is retained at
`.codex-research/strafe-flight-window-v1-20260906`, built with
`.codex-research/strafe-build-flight-window-v1-20260906`. The ordinary control
wrapper omitted the caller-owned command timestamp/weapon, causing native
`COMMAND_TIME` refusals when it first called PathCommand. This invalidates the
speed comparison, even where a new two-flight candidate completes. Window v2
corrects that integration error and repeats the complete declared cohort with
unchanged geometry, flight policies, horizon and selection rules.

Window v2 completes all25 observations with valid native movement/handoff and
resource/lifecycle audits. Fourteen retain incompatible range associations,
one has no grounded root, and ten execute the best complete policy: four
ordinary controls, five point controls, one new two-flight policy. The new
policy wins at red:f01:v1 in3.200 s versus3.400 s best point and3.975 s ordinary
travel to the same receiver. It is useful local evidence, not a complete route
speed result. Across the80 new-flight forecasts, only three complete;60
refuse late in the first flight with insufficient forward reach, nine miss
the first supported range, and eight leave its finite ground bound before
the second launch. Incoming momentum and receiving direction are poorly
matched at fixed takeoff, especially on the sharper turns.

Evidence: `.codex-research/strafe-flight-window-v2-20260906/`
`connection-report-v2-fixed1.json`, `resource-audit.json`, `source-snapshot/`.
The corrected auditor reports complete evidence for v1 too, while preserving
its control-clock defect; evidence validity is distinct from a fair successful
speed experiment. Same-build inactive controls retain both teams'4800-row
motion/resource/command equivalence after aligning trial elapsed time. All
1644 ordinary candidate16 steps match the corresponding native ordinary run
in command keys, position, velocity, contact and relative native clock; see
`.codex-research/strafe-flight-window-controls-v2-20260906/`
`baseline-equivalence-v2.json`. Its first failed comparison used absolute
world startup times and remains retained.

Window v3 tests one general correction to the first run-up. Before takeoff,
smoothly blend the incoming-line intent toward an intercept in W1 using the
same .8-second proposal. Blend weight is smoothstep of clamped progress from
48 units before approach to launch. Retain the existing source strip, fixed
0/24-unit takeoff alternatives, both ranges, flight/tail guidance, native
guards, 17-candidate controls, common receiver, horizon and original25 cases.
This tests whether a receiving-direction-aware run-up can improve takeoff
momentum without expanding geometry tolerances. Repeat the complete cohort;
if it still fails broadly, review launch-state/range preparation rather than
adding more local correction variants.

The grounded receiving intercept preserves the greater of reference speed and
actual receiving-line forward speed, clipped to W1 over .8 seconds; lateral
intent is `-lateral/.8`. Cap the final blended XY intent at600. A sequential
rotated command test also found carried yaw could cross360 and then fail the
next-call validity guard. V3 canonicalizes successfully emitted private yaw
to the equivalent `AngleNormalize180` representation, including receiver-tail
commands. The packed command and actual PS/view remain untouched. Retain the
failed pure test and its passing sequential regression; this is a declared
correctness fix alongside the run-up experiment, not a changed flight target.

For successor discovery, generate schedules from the audited full window
cohort. Retain at most two variants per frontier and every original outcome.
Use each positive common-receiver saving against ordinary candidate16 as an
offline proposal weight. Its interval spans the actual trial elapsed root
through the ordinary candidate's final boundary. Weighted interval scheduling
chooses a maximum-saving set without overlap on that reference timeline;
ties prefer fewer windows, then proposal IDs. Also force each retained useful
window in turn and recompute a compatible schedule, deduplicating results.
This exposes an early short connection and a later longer connection as
alternatives, rather than letting the early one consume every later entry.
These isolated weights are not additive speed predictions. Evaluate complete
actual-state schedules and matched inactive controls before freezing the
independent0/3/6 gate selection.

## Window v3 route result and next representation boundary

Fresh build `.codex-research/strafe-build-flight-window-v3-20260906`
(`20260907T000558748475Z.sources.json`) retains the full25 cohort under
`.codex-research/strafe-flight-window-v3-20260906`. All evidence and resource
checks pass. Native flight completions grow3→17 of80 across1→5 prepared
windows. Their winning local durations are105/106/109/108/110 frames, versus
the best non-flight136/172/144/145/144 respectively. All five re-jump at the
first landing boundary. The complete adapter outcomes remain10 live selected
completions,14 association refusals and one missing root; five selections are
flight, two point, three ordinary. There are1,382 selected native steps plus10
resumed steps with exact one-step full-PS prediction.

The generated five schedules and two controls are retained under
`.codex-research/strafe-flight-window-chain-v3-20260906`. Every declared stage
executes successfully. The best actual red schedule uses f00-v0, f02-v1 and
f07-v1, while the best blue uses f07-v1. Its first trips are54,375 and56,750 ms,
against57,225 and57,050 ms controls. The predicted maximum sum of isolated
savings is not the best actual red schedule: changed successor states matter.
`selection.json` freezes complete-trip selection before the independent gate.

The12-run phase gate is retained under
`.codex-research/strafe-flight-window-phasegate-v3-20260906`. All native,
resource, clock, publication, inventory and handoff audits pass. Of12 declared
stages,11 complete and one red phase6 entry is missed; ordinary motion remains
the fallback. The selected1,380 steps and11 resumed steps have exact native
one-step PS. All six trips are faster. Median times are red57,275→54,700 ms
(4.496%) and blue57,050→56,500 ms(0.964%). The10% speed target fails. Blue phase0
walks300 ms longer, and phase6 has equal2,300 ms idle in a shorter trip, raising
its idle fraction. Do not qualify or install this build on those results.

The diagnostic at `.codex-research/strafe-flight-analysis-v3-20260906/`
retains its script and all three cohort analyses. After coupled run-up,52 of80
flight candidates lose native WORLD support during phase0, at source progress
82–118 of128 units. They are still within the geometric source lateral/height
guard. Eight W1 contacts overshoot the finite maximum by0.04–7.04 units with
lateral error≤0.79; three W2 contacts miss laterally by71.6–72.7 units. The old
first-flight insufficient-forward-reach pattern is gone. This diagnosis is an
inference from recorded native states and pure guards, not proof of continuous
support between samples.

The next implementation boundary is a native-prepared **takeoff interval and
receiving-state contract**, not a third run-up blend experiment:

1. Derive the supported run-up's actual edge/clearance boundary from native
   collision and its legal body trajectory. Describe a finite takeoff region
   before support ends, with incoming and desired outgoing velocity/heading.
   A single source polygon or fixed plane is insufficient when the run-up
   turns across connected support. Native support remains authoritative.
2. Jointly propose takeoff timing/position and landing-range continuation;
   preserve actual state, one-command jumps and finite landing guards. Use
   the52 support-loss, eight overshoot and three successor-miss records as
   retained regression inputs. Do not accept an unsupported run-up by
   relabeling an unintended fall as a requested jump.
3. Apply the same window generator to suitable supported travel in both route
   directions. Current discovery stops at the first flag pickup and samples
   legacy bend anchors; those choices limit full-trip speed coverage. Explicitly
   declare new leg/root inventory before native collection, retaining every
   earlier refusal and the unchanged full-route/idle/walking comparison.
4. Freeze the finite launch/range candidate set and test complete actual
   successors. Only successful full-route qualification unlocks policy baking,
   bounded runtime retrieval/repair, defended/resource/cost and normal MP work.

## Native takeoff preparation v1 — declared September 7 UTC

Keep the original40 proposals, complete adapter25 cohort, W1/W2 bounds,
17-candidate common-receiver comparison and full-trip gates. Replace the first
flight's fixed takeoff planes with two native-supported state proposals per
grounded speed variant. The unchanged coupled run-up executes with jump intent
disabled before local command selection. Starting at the actual root and its
immutable submitted command, run at most64 native steps until first WORLD loss
or source arclength reaches the finite approach endpoint. There are two prefixes,
because only the preferred-speed bit affects grounded feedback.

For a successful prefix ending unsupported, propose its last two supported
command boundaries before loss. For one ending supported at the finite endpoint,
propose the terminal boundary and preceding boundary. Both must follow the
immutable first command, remain within the existing supported source strip,
have forward motion and a released jump latch. Refused/horizon prefixes and
invalid boundaries remain evidence; they do not acquire a takeoff proposal.
Candidate bit0 chooses the later/earlier state, bit1 chooses speed as before,
and bit2 retains its air correction choice. Each flight still starts from the
original root. It executes feedback, then requests one jump only when actual
source progress reaches the selected boundary and actual position/velocity
match within12 XY units,8 vertical units and40 XY units/s. A missed boundary
refuses rather than falling off and calling that a jump.

Record every native prefix, terminal and proposed state in additional capture
types8/9/10. Independently bind them to the common root, publication, native
settings, complete support sequence and selected flight's launch. Full native
flight and common-receiver validation remain authoritative. The local state
tolerances are admission limits, not proof that every state inside a continuous
region is safe. This is finite actual-state discovery for later policy baking;
it does not establish a real-time preparation cost or disturbance guarantee.

Run the unchanged adapter25 experiment first, retaining the prior52 run-up
losses, eight W1 overshoots and three W2 misses as the comparison. If useful,
generate and measure actual successor schedules, freeze full-trip selection,
and repeat the independent0/3/6 gate including idle/walking. Both-leg and
supported-run inventory expansion is a separate declared follow-on, not an
unrecorded change to this cohort.

## Prepared takeoff result and explicit coverage extension

Build `.codex-research/strafe-build-takeoff-v1-20260907` and the complete25 cases
under `.codex-research/strafe-takeoff-v1-20260907` pass independent prefix,
flight, publication, handoff and resource audits. Both speed prefixes prepare
all ten windows:415 native prefix steps,20 terminals and40 supported boundary
records. Successful flights rise17→54 of80; selected policies are eight flights
and two points. All1,169 actual steps and10 resumed steps have exact one-step PS.
The14,090 point/ordinary control steps equal v3 after absolute clock alignment
(`control-equivalence.json`), binding this to an unchanged control comparison.
Pure regression retains576 historical,216 run-up and576 prepared-takeoff cases
at `.codex-research/strafe-flight-command-v4-20260906/result.json`.

All37 successes among the former52 support-loss candidates complete the window;
the other15 now fail at actual landings/tail. The complete residual count is16
W1 overshoots, nine W2 lateral misses and one tail departure. Exact-capture
diagnosis at `.codex-research/strafe-takeoff-diagnosis-v1-20260907/` finds W1
overshoots0.038–6.010 units: the ballistic intersection occurs3.58–23.90 ms into
a24/32-ms command, and acceptance observes the later command boundary. W2
lateral errors are64.252–91.981; the tail case drifts63.958→65.411. All54
successful first landings re-jump immediately. Retain these as receiving-state
and remaining-command limitations, not justification for looser position gates.

Seven schedules plus two inactive controls pass under
`.codex-research/strafe-takeoff-chain-v1-20260907`. The fastest complete-trip
selection is frozen in `selection.json`. Its independent12-run gate under
`.codex-research/strafe-takeoff-phasegate-v1-20260907` passes evidence/resource
checks, all six time/idle/walking comparisons and14 native flight stages of15
declared. Blue phase3 misses one entry. Median red57,275→53,975 ms and
blue57,050→54,500 ms improve5.762%/4.470%; the10% target still fails. All1,394
actual and14 resumed steps pass full-PS one-step comparison. The directory
retains the full1,092-source native snapshot,11 helpers and installed-DLL hashes.

The separately declared next inventory is
`.codex-research/strafe-window-inventory-v2-20260907/inventory.json`, generated
by `tools/strafe_window_inventory.py`:88 seeds comprising original40,40 actual
return bends and eight supported long runs. Preserve eight geometry refusals,
all old25 point payloads and every native outcome. Return publications are
selected through audited pickup/capture lifecycle, never by reversing another
team's route. Long-run seeds require flat unrestricted hopping intervals of at
least1,300 XY units, with approach32 and launch160 from their interval start.
These geometry choices propose room for a window and do not establish support
or feasibility.

Native seed payloads explicitly identify team, outbound/return leg, approach,
launch guide, first receiver probe/direction and optional real legacy geometry.
Without a point adapter, retain candidate IDs8–15 as explicitly inapplicable
refusals; flight IDs0–7 and ordinary candidate16 keep their current meaning.
All17 captures preserve the actual root's immutable first command. Validate
new roots through ordinary play, bound to the actual ready publication for
their declared leg, with no first-capture reset. After isolated seed evidence,
version chain scheduling to cross actual pickup and use changed successor states;
repeat complete-trip selection and the unchanged independent gate.

The first80 native seed runs are retained at
`.codex-research/strafe-window-seed-v1-20260907/`. Seventy-eight complete their
resource/evidence observation; two fail setup. The new parser incorrectly
required the optional legacy landing/onward pair to be flat, rejecting the
existing red/blue f09-v1 payloads whose height changes about25 units. The seed's
receiver direction is a horizontal association guide; full legacy geometry
must retain its original3D heights. Remove this parsing restriction and keep
the native flat-window/support tests unchanged. Do not reclassify these setup
failures as native window refusals or omit them from the denominator.
`seed-report-v1.json` is explicitly invalid:39 live completions (29 flight,
two point, eight ordinary),34 binding refusals, five missing roots, eight
retained geometry refusals and two evidence failures. The1,094-source snapshot
and failed resource audit are retained before changing the parser.

The next fresh build repeats the complete88 declaration /80 runnable cases.
It also includes separately versioned `BOT2CONNECTIONCHAIN2` support for the
subsequent ordered experiment. CHAIN1 keeps its previous pickup termination.
CHAIN2 declares a nondecreasing leg per seed, observes actual pickup/capture,
retires skipped outbound entries explicitly and waits for the actual return
publication. Lifecycle state belongs to the chain across stage resets; no
stage may close/rearm while native pending or resumed-step evidence is undrained.
Pure lifecycle checks precede focused mixed-leg, return-only and pickup-skip
native checks, followed by all generated schedules and the same0/3/6 gate.
Inactive CHAIN2 code in isolated seed trials is not itself chain qualification.

## September 7 UTC stopping checkpoint

The user requested a stopping point before the next native campaign. The full
repeat at `.codex-research/strafe-window-seed-v2-20260907` now passes all80
native resource/evidence checks and accounts for all88 declared seeds:
39 complete windows (29 flight, two point and eight ordinary selections),36
receiver binding refusals, five missing roots and eight geometry refusals.
Both old parser setup failures become proper native binding refusals; the
failed v1 evidence remains unchanged. The corrected parser preserves real
legacy height geometry and retains native feasibility checks.

Build `.codex-research/strafe-build-window-seed-v2-20260907` is bound by
`20260907T011517681294Z.sources.json`. The collection's `source-snapshot/`
retains all1,094 native files and25 helpers, with verified frozen sources,
unchanged `bot2_strafe.c` and unchanged installed DLLs. See `checkpoint.json`
for the compact evidence and continuation record.

The frozen88 report generates21 schedules, each with six windows across actual
outbound/return legs, at
`.codex-research/strafe-window-seed-proposals-v1-20260907/proposals.json`.
These are unexecuted proposals. CHAIN2 mocked native-function tests pass13
lifecycle scenarios and27 malformed-input/preflight refusals at
`.codex-research/strafe-seed-chain-check-v1b-20260907/result.json`; exact legacy
height regressions pass at
`.codex-research/strafe-windowseed-check-v2-20260907/result.json`.

Resume with declared focused native lifecycle checks using this frozen build.
Include mixed legs, return-only waiting, and missed-stage handling where actual
retained seed behavior permits it; do not fabricate a successful pickup-skip
case or substitute mocked evidence for a native case. Then run all21 generated
schedules and matched inactive controls, select by actual complete-trip time,
freeze the choice, and execute the unchanged12-case phases0/3/6 gate with idle
and walking comparisons. Runtime baking, cost, defended/resource expansion and
normal MP installation remain unqualified. No CHAIN2 native run was started
before this checkpoint.

The next focus is frozen, validated and unstarted at
`.codex-research/strafe-chain2-focus-v1-20260907/campaign/plan.json`:
six phase0 runs on ports31300–31305 using the same v2 build. It declares red/blue
mixed schedules, red return-only waiting from spawn, a retained blue NO_ROOT
outbound entry followed by useful return, and both inactive baselines. All88
source outcomes and unchanged payloads remain bound in `../proposals.json`.
Preparation script:
`.codex-research/strafe-chain2-focus-declaration-v1-20260907/prepare_focus.py`.
This does not establish a natural pickup-skip case: passed-entry expiration may
retire the pending outbound stage first. The scorecard contains the exact run
command. No native launch occurred.
