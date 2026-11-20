# Strafe movement project plan — retained history

Historical snapshot preserved during the September 12 instruction cleanup.
Contains superseded status and work orders, not current instructions.
Use the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) to resume;
read this snapshot only for a specific old contract, result or counterexample.
Original bytes are also retained in the archive.

Updated 2026-09-11 UTC. This is the active direction and acceptance contract.
The [scorecard](STRAFE_MOVEMENT_PROGRESS.md) owns current measured status.
The [implementation guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) owns the
ordered work packages, exact resume procedure and handoff instructions.
Dated experiment documents are evidence, not competing work orders.

**September 11 post-campaign review:** BR-A and BR-B now execute through the
production owner, with intermediate-tail behavior and checked transitions.
Repeated control, sources-only and candidate evidence is collected. Rapid
join/release churn reaches zero in all three candidate repetitions. This is
measured progress, not completed delivery: step 3 coverage is partial;
T2-branch has failed visits and an unscored rejoin; T2/T3/T4/T5 remain unmet
or incomplete. Native game logs confirm two normal-speed runner falling deaths,
so T6 remains failed. Historical readiness invitations below do not apply.

The [branch plan's remaining sequence](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order)
is current: repair/version audit contracts and death attribution; fix ordinary-
tap admission and branch termination; verify focused transitions and fall
mechanisms; finish landing/corner/travel behavior and later launch coverage;
then qualify a new candidate. Preserve completed collections and their failures.
Read the [review corrections](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md#post-campaign-review-corrections)
for the evidence and scope. No new installation is made and K07 remains open.

**Current owner feedback: return strategy remains incomplete (September 10).**
The installed candidate loses pickup momentum, uses lanes not tailored to
remaining Force, lacks prepared ordinary-jump branches and effective depleted
speed recovery, brakes excessively in fallback corners, and can rejoin an
immediately unaffordable maneuver. The earlier aggregate delivery targets
proved insufficient: the 12.1 s median is about 88% active Force Speed.
Next milestone is resource-aware continuation plus momentum-preserving corner
execution, with separate high-/low-momentum, zero-/low-Force and Speed-expiry
outcomes. Captures/no stalls alone do not establish readiness.
**Momentum-preserving corner execution is now delivered and measured, and the
loss is attributed on native evidence** (see the
[depleted-return record](STRAFE_MOVEMENT_DEPLETED_RETURN_2026-09-10.md));
resource-aware continuation and the depleted-Force branches remain open, and
nothing is installed. Follow the
[current implementation work order](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#opening-instruction-for-the-next-ai).
Earlier review invitations and next actions below are historical. K07 stays open.

**Latest owner direction: take responsibility for a review-worthy candidate.**
Do not request another human playtest of the 19.6-second regression. Retain the
corner/arrival fixes and target the particular unsafe joins instead of broadly
suppressing fast movement. Before delivery, require a natural return median
at most 14 s, mean horizontal carrier speed at least 475 u/s and airborne
share at least 55% in the bounded completed-return cohort, while retaining all
failed/censored outcomes. Recheck both teams at the known corner/stall starts
and zero-Force/Speed-expiry conditions with no solo falling deaths, then normal
MP defended capture and no long idle stall. These are delivery targets, not
replacement K07 gates. Continue internally until the candidate earns review.

**Current delivery meets those review targets:**
[review-ready build](STRAFE_MOVEMENT_REVIEW_READY_2026-09-10.md)
`strafe-build-reviewready-20260910-b` is installed, backup
`20260910T0607586345246Z`. Eight natural captures: median 12.0625 s,
538.6 u/s mean horizontal speed, 64.3% airborne. Fourteen solo cases yield
56 captures/no deaths; both defended teams capture without falls or long stalls.
The first depleted red return remains 24.1 s; defended completed-return median
is 24.325 s with two carrier deaths retained. Human review is now warranted;
K07 remains open. Earlier delivery/next-action statements below are historical.

**September 10 corner repair delivered:** the [human-demo study](STRAFE_MOVEMENT_HUMAN_CORNERS_2026-09-10.md)
led to append-only checked return curves and proportional grounded arrival.
The [repair record](STRAFE_MOVEMENT_CORNER_REPAIR_2026-09-10.md) binds installed
build `strafe-build-corners-20260910-b`, retained failed joins, and their safety
containment. Ten solo worlds yield 35 captures/no deaths; both defended MP
teams capture with no falling death or audited long stall. Natural return
median regresses to 19.575 s versus roughly 12 s; do not hide this tradeoff.
Next: human retest of corners and hesitation. K07 remains open; no navmesh
rebake, general imitation learning or exhaustive qualification restart is needed
before that feedback. Earlier delivery checkpoints below are historical.

**Current owner direction (September 10): fast return is the main behavior.**
The pickup/pad repair did not feel materially different to the owner. Build
one fast return lane per team, select/prepare during approach, join from actual
pickup/recovery state, and judge local maneuver resources instead of requiring
the whole full-Speed guide. Conservative navigation is a connector/recovery,
not the intended trip home. Measure pickup departure, return time, sustained
speed/hopping and defended survival; capture alone cannot pass this delivery.
Keep legacy full-guide gates unchanged. The [fast-return delivery](STRAFE_MOVEMENT_FAST_RETURN_2026-09-10.md)
is installed as build `strafe-build-fastreturn-20260910-d`. Median measured
returns are substantially faster, but severe low-Force first returns remain
weak. Human retest is next; do not call the median a universal return-time gate.

**September 10 human-feedback repair:** pickup return now starts on validated
intervals without waiting for a full-resource cached entry. Pad steering uses
a nearer checked landing and preserves existing cached-route ownership. See the
[repair record](STRAFE_MOVEMENT_PICKUP_PAD_2026-09-10.md). Native pad cases
land alive at normal speed, all 26 body cases pass, and both normal MP defended
smokes capture. The next checkpoint is the owner's retest of these two fixes;
do not restart full qualification before that feedback. Return speed remains
below the fast incoming routes; K07 remains open.

**Latest owner direction (September 9 local / September 10 UTC): prioritize an
in-game playable candidate now.** ComfyUI is closed and native checks may run.
Keep pressed-runner staff melee as the response to a live enemy body, with
movement ownership preserved. Complete one bounded review/fix/build cycle,
focused native regressions, real-opponent and normal MP listen smoke, and an
isolated cost check; then deliver a recoverable, explicitly provisional
playtest installation. Full K07 qualification remains a separate open claim:
do not relabel failed gates, pool away stalls, or require another navigation
research loop before a useful human playtest. Reopen a fix only for a concrete
delivery blocker found by these checks. Map transfer, general aiming and the
remaining exhaustive qualification follow the playtest checkpoint.

**Delivery result:** the [provisional playtest](STRAFE_MOVEMENT_PLAYTEST_2026-09-10.md)
is installed with recoverable backups. Corrected-build resources (30/30),
temporary-body cases (26/26), two defended smokes and both normal MP listen
smokes complete. Strict timing still fails (5.3348 ms eight-bot maximum), and
the sixteen-bot stress loses a client. Human play feedback is the immediate
next checkpoint; full K07 remains open.

**Direction review, September 8:** R1–R4 are implemented and measured. K07
confirms solo first-trip gains of 10.27% red/11.86% blue with idle/walking
better in every pair, the adapter-armed resource matrix, lifecycle regression
and bounded native contention. Integrated qualification remains incomplete:
defended walking/stalls and M2 coverage/outcomes need work. Timing resumed
after the owner closed ComfyUI and still fails the maximum (R8 update below).
[R6a–R6d](STRAFE_MOVEMENT_K07_REVIEW_2026-09-08.md) were executed the same
day; the [R6 execution record](STRAFE_MOVEMENT_R6_EXECUTION_2026-09-08.md)
holds the measured causes and the freeze-2 qualification. Architecture and
gates are unchanged. Two decisions recorded there for the owner: the M2
benchmark now has a v3 suite whose event times were re-derived for the
travel-facing controller (same protocol, both suites and results retained),
and the shared controller change made in R1–R3 is kept as the kernel's
behaviour with its three residual rejoin failures inside the M2 gate.
K04 discovery is not reopened; no gate is weakened. The earlier R1–R5 review
remains historical evidence.

R7 has resumed from the committed handoff on local `master`: declared
repetition seeds and duplicate-recording refusal are implemented, and supported
ownership waits are bounded without changing the shared movement kernel. The
[R7 record](STRAFE_MOVEMENT_R7_EXECUTION_2026-09-08.md) retains new traced
multi-controller stalls and verification. The new integrated collection is
complete: solo/resource/lifecycle regressions retain their gates, but four
defended stalls and a red 1x walking regression keep continuity unqualified.
The three invalid M2 startups have a separately retained valid recollection;
the original audit remains false. Matched-resource planned references remain
unimplemented. Architecture and acceptance gates were unchanged at R7; timing
was still deferred then. The later R8 collection below supersedes that status.

R8 adds contact attribution and preserves bounded local-navigation refusal
history across compatible ownership handoffs. Native traces verify immediate
validated sidestep selection on reentry; neither diagnostic cohort reproduces
a distant stall, so this is not a measured cure for R7 confinement. See the
[R8 record](STRAFE_MOVEMENT_R8_EXECUTION_2026-09-08.md). Defended qualification,
independent resource references and the timing gate remain open.

**Latest timing decision:** the owner closed ComfyUI and requested recollection.
R8's isolated September 9 UTC samples retain a 5.5224 ms eight-bot maximum
failure and a separate invalid sample after a native fall. Timing is now
measured and failed, not deferred. The six-forecast burst requires further
work under the unchanged 2.5 ms p95 / 5 ms maximum gate; see the R8 record.

**R9 recovery work:** prepare alternatives during obstruction waits and permit
bounded, freshly validated continuation after successful recovery arrivals.
The two retained blocked-corner escapes cross 64 units 750 ms earlier; four
former stalled defended cases now have no audited distant stall, with changed
combat timelines. Chained continuation remains mocked coverage, and the native
fall remains unreproduced/unclassified. See the
[R9 record](STRAFE_MOVEMENT_R9_EXECUTION_2026-09-09.md). An unavoidable fall may
be an explained movement outcome, while still invalidating an eight-active-bot
timing sample. Stabilize movement quality before final cost optimization; keep
bounded live work and all existing gates.

**Q1 result (2026-09-09):** a declared pursuing-body protocol executed the bounded
continuation natively (8..10 `corner_continue` events per eight-case collection across six
collections, most with displacement and objective progress while the body stayed
30..66 units away; aborts on re-obstruction; clear handbacks; three-step
exhaustion). Sustained pursuit at running speed varies between runs of the same
case, 0..2 confinements of up to 13.25 s and no capture in the window: a
measured limit of a body that always stands in front, left to Q3's defended
cohort to judge.
The diagnostic trace columns captured a complete fall: a cached-route rejoin at
a 1170 u/s takeoff from about 320 u/s flew into a platform face and the pit; the route follower now declines a rejoin whose first takeoff ahead needs more
momentum than ground running can supply *and* whose recorded flight crosses a
gap, a deep drop or a climb; flat stand-start chains stay admissible from rest,
and no rejoin crosses missing floor between the runner and the rejoin point. The R8 death itself remains
unclassified; the retained traces show a mid-air body contact with client 4 as
the candidate mechanism. See the [Q1 record](STRAFE_MOVEMENT_Q1_EXECUTION_2026-09-09.md).
Regressions: resources 30/30 and temporary-body 26/26 pass on the final revision `…e8f1…` (`.codex-research/strafe-q1-regression-20260909-e`), after four rejoin-gate variants that each failed the body suite were retained and replaced.

The previous plan is preserved verbatim in the
[September 7 archive](../.codex-archive/20260907T013549236Z/docs/STRAFE_MOVEMENT_PROJECT_PLAN.md).
Its chronological decisions remain available; this revision preserves the
acceptance thresholds and replaces the accumulated immediate work orders.

## End result

Deliver a dependable, intermediate-human offense bot. The immediate product
is **movement-only Runner_v2 on Kejim**: launch normal MP, Create Game, add the
runner, defend against it, optionally add Bot2 CHASE/BASE enemies, and inspect
live or completed CSV/demo telemetry. It should move quickly through both flag
legs, face roughly along travel, favor useful ordinary strafe hops, and recover
from interruptions without sustained unexplained confinement.

The general solution must transfer to other known maps through automatic map
preparation, without map-specific C, hand-authored rescue coordinates or a
growing list of corner exceptions. After dependable movement and map transfer,
complete objective decisions, survival, tactical force use and believable
shooting. **Aiming/combat development remains paused until then.**

The owner prioritizes idle time, unnecessary walking and getting stuck.
Captures/deaths establish objective and native lifecycle behavior; they must
not conceal bad movement through resets or tactical selfkill. Walking for a
real turn, landing or objective arrival remains legitimate.

There is no defensible completion percentage, subscription-reset estimate or
fixed research duration. Report completed delivery gates and measured costs.

## Architecture to build toward

1. **Known-map preparation.** Keep Recast/Detour for topology and native
   collision for actual support, clearance and hazards. Coalesce artificial
   polygon seams into traversable intervals. Prepare outbound, return and
   alternate/recovery continuations from real map geometry. Rebuilding the
   navmesh is justified only by measured topology/geometry defects.
2. **Movement-state planning.** Plan racing lines and connected feedback
   maneuvers through position, velocity, heading, contact, jump latch, remaining
   command time and resource state. A nearby landing point is not a compatible
   continuation. Search/optimize substantial trajectories offline; record
   refusals and entry/exit validity, not just nominal successes.
3. **Shared control.** Use the same command policy and native cadence for
   forecast and execution. The current pure flight controller and receiver are
   useful foundations. Keep the frozen cached-route controller as a regression
   reference; change it only for a demonstrated shared control/physics defect.
   Pickup, moving bodies, objective ownership and recovery lifecycle belong in
   live integration, not as special cases in that frozen kernel.
4. **Bounded live work.** Load prepared data, retrieve a small bounded policy
   set, validate the actual state, track it and repair departures using the
   same movement graph. Measure total retrieval, validation, preparation,
   tracking and recovery. A precomputed proposal index alone does not make
   full native rollout selection affordable.
5. **Native lifecycle.** Preserve real motion, force, health, triggers, pickup,
   return, capture, death and respawn. No state replay/reset between legs.
   Static-world discovery guarantees do not cover arbitrary multiplayer world
   mutation. Unavailable or invalid plans must yield a reasoned safe fallback.

Empirical entry regions are not formal safety certificates. Exhaustive global
state coverage, demo mining, training and a new navmesh are not prerequisites
for the next usable route. UnrealJKA and demos are sources of reusable ideas
and counterexamples; do not port their entire stack or restart reconnaissance.

## Remaining delivery sequence

### Current 80/20 triage: competent Kejim play

**First human feedback changes the immediate queue:** fix pickup-to-return
continuity first. The owner sees fast arrivals followed by standing and ground
running; seven of eight recorded pickups report `stand_entry_resource`, usually
with about 26 Force left. Use the [human-play review](STRAFE_MOVEMENT_HUMAN_PLAY_2026-09-10.md)
for the bounded reproduction and ownership correction. A ten-second fixed-XY
vertical bounce is the secondary counterexample. These concrete findings take
precedence over generic survival review and the older qualification queue.

Owner direction after the installed `509454ee` playtest: prioritize visible
competence and pick battles. The working scope is one Kejim runner against a
human and optionally one or two Bot2 opponents at normal speed. This is a
product checkpoint; existing K07 gates remain unchanged and unpassed where
applicable. The Q1–Q5 qualification inventory below is not the immediate queue.

| Priority | Issue | Next action and stopping rule |
|---|---|---|
| Fix first when observed | Crashes, broken spawn/respawn, objective abandonment, repeatable idle traps or avoidable pit entries | Use the installed playtest and its logs. Reproduce one concrete episode, fix its cause, rerun that case plus affected regressions and one ordinary match. A stationary fight or legitimate flag wait needs context; neither is automatically an AI defect or automatically excused. |
| Highest remaining investigation value | Survival and interruption recovery | The corrected matches scored 7/6 captures and the sampled audits count 12/10 deaths. Initial review of both complete server logs attributes every runner kill to enemy weapons, none to MOD_FALLING; raw log and sampled-window totals can differ. Do not treat these deaths as a demonstrated navigation defect. Inspect preceding behavior only for repeated standing exposure or failed escape seen in play; combat losses alone do not justify a combat rewrite. |
| Next if it visibly hurts play | Unnecessary walking, long detours, slow restart after combat | Retained travel walking is about 41–42 percent, but turns/landings can be necessary and these are not matched baseline trials. Find the longest visibly wasted episodes; fix a shared cause only if it is common. Do not chase every walking sample or subsecond route gain. |
| Keep; expand only for a demonstrated failure | Close-range staff melee | Native saber kills and useful trips are observed. Keep current movement-ownership safeguards. Add no general combat planner, weapon search or aiming project to solve a narrow body block. Escalate only if a normal defender repeatedly makes the runner ineffective. |
| Defer unless normal play hitches | Strict forecast maximum | Four eight-bot p95 values are 0.2892–0.3225 ms; two maxima exceed 5 ms, worst 5.3348 ms. Preserve the failed gate. Optimize for observed play hitches or sustained larger workloads, not solely to remove the last 0.3348 ms. Current benchmark excludes objective/melee and cannot prove full-match frame pacing. |
| Defer from this product checkpoint | Sixteen-bot stress, full 12+12/780-case qualification, reference-ratio policy, map transfer and broad aiming | Preserve evidence and open gates. Resume when expanding supported scope, preparing a qualified release, or investigating a specific regression. No repeated exhaustive campaign merely to make the scorecard green. |

Next execution: use the completed weapon-cause triage, review the longest
idle/walking episodes, and prioritize normal-speed human play feedback. If
fresh observation is needed, declare one short red and one short blue match
before collecting, rather than launching the whole qualification chain. Choose
at most one or two recurring defects for a correction cycle. After the targeted
checks pass and ordinary play shows useful trips, recovery and native objective
lifecycle, stop that cycle and play again. A hard crash/unsafe motion issue
remains a blocker regardless of its frequency; tactical defeat is not.

### Immediate execution order after R9

Implementation checkpoint: `9d8dff86` on local `master` (Q3 in progress, not passed; `34049186` was Q2, `f9bdd662` Q1, `aabdb964` R9). The
[Q1–Q5 handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE_HISTORY_2026-09-12.md#next-execution-order-after-r9)
is the current executable order; earlier dated work orders are historical.

| Order | Next work | Completion/decision requirement |
|---|---|---|
| Q1 | Establish recovery execution under moving pressure; instrument/classify the retained fall | Native bounded-continuation and handback evidence, useful travel with defender present, preserved safety and regressions. Fall is corrected if avoidable, explained if demonstrably unavoidable, otherwise explicitly unclassified; no endless identical probes. **Executed 2026-09-09** ([record](STRAFE_MOVEMENT_Q1_EXECUTION_2026-09-09.md)): continuation executed natively under pursuit; a different captured fall exposed and corrected a route rejoin defect; the R8 death stays unclassified with a candidate mechanism. A first momentum-only gate failed 17/26 temporary-body cases and three later variants two more each; all were replaced before the retained regressions. |
| Q2 | Implement independent matched-resource planned references | Strict loader/native validation, independent frozen path selection, all 30 matched pairs through actual pickup/return resources; no borrowed Speed tapes or invented ratio gate. **Executed 2026-09-09** ([record](STRAFE_MOVEMENT_Q2_EXECUTION_2026-09-09.md)): loader, planner, selection on phases 1/4/7 and 30/30 valid pairs on phases 0/3/6; the reference is faster on most legs by 50..900 ms of 24..26 s. The ratio-threshold gap is surfaced as a plan decision, not closed. |
| Q3 | Verify integrated movement quality | Full 12+12 defended comparison plus retained route, disturbance, resource and lifecycle/facing/jump gates. R9's four cases are development evidence only. **In progress, not passed (2026-09-10)** ([record](STRAFE_MOVEMENT_Q3_EXECUTION_2026-09-09.md)): collections `-a` (Q2 build) and `-b` (v2 build) fail the stall gate on the pressed-carrier family only; idle and walking beat the baseline in every cell on `-b`. Owner decision: the carrier fights (`bot_carrierMelee`, staff); the v4 build awaits its bench and the `-c` recollection. If stalls remain after melee, the gate rule itself (per-collection zero versus a classified body-pressure rule or pooled collections) is the owner's decision. |
| Q4 | Optimize the measured forecast burst | Preserve Q3 behavior and current-state validation, then meet the isolated eight-active-bot 2.5 ms p95 / 5 ms max budget; retain all samples and report sixteen-bot stress. |
| Q5 | Freeze and qualify one final revision | Bind every applicable K07 result to the final build; recheck quality affected by cost changes. No mixed-revision pass. |

Bounded cost remains a design constraint during Q1–Q3; final performance
optimization follows useful behavior. A safe exploratory step may temporarily
increase goal distance. Waiting may prepare alternatives, but false handbacks
must preserve ordinary movement. No known unsafe move, resource reset, defender
removal or repeated short loop may stand in for recovery.

### Delivery packages

These packages implement M3a while retaining M0–M3. Package details and
dependencies are in the implementation guide.

| Order | Work package | Completion evidence |
|---|---|---|
| K01 | Qualify native pickup-aware CHAIN2 lifecycle | Frozen six-case focus audited; mixed legs, return wait and missed stages recorded; any untested lifecycle branch remains explicit |
| K02 | Measure all both-leg successor proposals | All 21 generated schedules plus two inactive controls, actual successor states and complete resource/evidence inventory |
| K03 | Freeze selection and evaluate full trips | Exactly 12 selected/control runs, phases 0/3/6, speed and locomotion gates below |
| K04, conditional | Resolve the measured general controller/planning blocker | Reusable change improves complete native trips and retained counterexamples; repeat K02/K03 as warranted |
| K05 (open) | Bake a useful policy graph and implement bounded runtime execution | Artifact/adapter exist; R1–R3 must establish complete cost and runtime time/idle quality; harm-only acceptance is insufficient |
| K06 | Integrate dynamic recovery and changing resources | Production ownership, Speed expiry, pickup/return/respawn/drop/body cases; useful recovery without repeated short escape resets |
| K07 | Qualify the integrated Kejim build | M0/M1/M2 retention, matched-resource traversal, defended continuity, eight-active-bot cost, facing/jump and native lifecycle checks |
| K08 | Deliver normal MP play | Accepted build installed with recoverable backups; Create Game/addrunner/enemies/rates/logging smoke test and user play evidence |
| K09 | Prove map transfer | Same preparation/controller and M1–M3a on Dantooine without map-specific C or weakened gates |
| K10 | Complete offense behavior | Objective/survival/force decisions, then aiming/combat and human-style evaluation under a separately declared protocol |

Do not integrate the expensive 17-candidate research search into normal play
and call K05 done. K03 is offline route quality; K05/K06 are runtime delivery.
Do not wait for a perfect universal motion atlas before delivering qualified
Kejim movement.

After two failed iterations on one representation, review the representation
before another sweep. Each experiment must name the delivery blocker,
hypothesis, fixed cases and decision it will enable. Improve the measured
bottleneck; do not substitute more fixture counts or local corner patches.

## Acceptance contract

### Existing benchmark: retain M0, M1 and M2

M0 retains the original 12-case native route/feedback regression.

M1 inventories all native standard Kejim spawns: 24 distinct initial/respawn
poses plus two own-flagstand starts. Preserve explicit aliases for duplicate
poses. Each pose needs an actual native enemy-flag pickup and a same-start,
same-physics, same-resource planned reference to that endpoint. Nominal time
must be <=110% of reference; all eight command phases must finish alive and
the slowest must be <=120%. One configuration must select the appropriate
route; per-route cvar changes do not qualify map-wide selection.

M2 freezes feasible offsets ±32 units on each horizontal axis, impulses ±50
and ±150 units/s per horizontal axis at takeoff/mid-flight/approach, one skipped
jump and a temporary body blocker. Require >=95% pickups overall, >=90% in
every family, M0 retention and successful trips <=150% of their clean reference.
Retain infeasible setups explicitly. Use independent evaluation after tuning;
list deaths/timeouts individually. The historical frozen benchmark passed,
but newer integrated movement does not inherit those results.

### Full-route movement quality

Use complete native first round trips, with force 0 and no initial Speed,
120 game seconds per observation. Select one schedule per team from development
before evaluating phases 0/3/6 against matched inactive controls: 12 native
runs, six comparisons. Every candidate trip must be no slower; each team's
median must improve by at least 10%. Travel idle and walking fractions must
not regress in any pair. Preserve all missed entries and failed observations.

The existing speed tool's `passed` field covers time only: also require
`all_idle_not_worse` and `all_walking_not_worse`. Do not reinterpret a speed-only
exit code as the complete movement gate. Isolated savings are proposal weights,
not additive full-trip predictions. This gate does not establish runtime cost,
defended reliability or installation.

No-Speed references cannot be compared to old fixture-injected Speed tapes.
Implement matched-resource references through production dispatch where missing.
Also retain the 30-case solo resource matrix, cached-Speed-route expiry handoff,
travel-facing behavior and ordinary-jump/resource regressions.

### M3 and M3a: runtime cost and continuous defended movement

At 40 Hz with eight actual active bots, total movement work must have
p95 <=2.5 ms and maximum <=5 ms on this host. Include routing, fallback,
validation and recovery; exclude diagnostic file writing and report complete
server-frame time separately. Stress 16 bots and report scaling/activity.
Timing runs execute alone. Dead/idle clients cannot stand in for active work.
Charge preparation even when it executes later inside ClientThink_real, outside
the existing dispatch timer. Include cold load, binding, selection and early
refusal; avoid double-counting already-timed command execution. The25 ms game
tick is not a substitute movement budget. Lost living clients invalidate an
eight-active-client claim; retain and investigate their lifecycle evidence.

Freeze baseline and candidate defended sets: three 600-game-second runs per
team at 1x and three per team at 5x, one runner versus the same CHASE/BASE pair.
That is 12 runs per arm. Preserve starts, resources, opponents, supported seeds,
rates, complete durations and native 25 ms game steps. Use botplayrate for these
matches; changing timescale alone is not equivalent.

Require all cases valid and no unexplained confinement >=5 game seconds inside
a 64-unit region more than 96 units from the goal. Each team/rate cell's travel
idle and walking fractions must not exceed baseline; require no aggregate
regression as well. Compute pooled seconds/denominators, not averages of ratios.
Keep classified flag-state waits separate, retain raw alive-time totals and
review suspicious larger loops in telemetry/demos. Combat/congestion is not
an automatic excuse for confinement.

Metric definitions differ by protocol: the fixed-tick solo report uses XY
speed <50 for idle and grounded 50..300 for walking; the defended auditor uses
grounded 3D speed <20 and 20..300, with time weighting and classified goal-wait
exclusion. Preserve both; do not compare their percentages as the same metric.
The [defended protocol](STRAFE_MOVEMENT_CONTINUITY_CAMPAIGN_2026-09-06.md) and
[fast-trip protocol](STRAFE_MOVEMENT_FAST_TRIP_2026-09-06.md) define the details.

Demonstrate repeated useful objective progress, native pickup/capture and
respawn handoffs, dropped/returned/held flag changes, no-Speed traversal,
mid-trip Speed expiry and bounded recovery. Force is a shared changing match
resource; survival spending and later replenishment cannot be assumed away.
M3a is closed only when the same integrated build also retains the benchmark,
resource/facing/jump checks, representative cost and normal MP behavior.

### M4, M5 and project completion

Dantooine has early geometry/transition evidence, not a qualified full-map
runner. M4 repeats M1–M3a with its own native start/reference inventory, the
same preparation/controller and no map-specific C or tuned-down gates.
Additional map/resource coverage follows measured deficiencies.

After movement acceptance, M5 adds objective and survival decisions, force
allocation, then believable aiming/shooting. Declare match and human-behavior
acceptance before evaluation; no numerical combat gate has yet been agreed.
Do not invent a pass threshold or claim full offense intelligence from movement
captures. Tactical selfkill is a later decision and must never hide a stall.

## Execution and evidence discipline

Follow AGENTS.md: preserve unrelated work, archive instead of delete, use fresh
unique native builds after source changes, freeze measured inputs and retain
source/binary/settings bindings. At most four isolated native outcome worlds;
timing alone. Existing accepted snapshots are references, not permission to
clean or reset the checkout.

The scorecard must separate implementation, measured behavior, acceptance and
installed version. Every result records exact build, cases, baseline, audit,
remaining failures and next action. Read history only when resolving a specific
counterexample. A passing intermediate experiment is progress, not automatic
milestone completion.
