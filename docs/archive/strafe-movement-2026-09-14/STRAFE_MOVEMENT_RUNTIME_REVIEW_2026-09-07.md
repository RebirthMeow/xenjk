# Runtime direction review and next implementation order

Review: 2026-09-07 UTC. User requested review and planning; another AI will
implement. No native run, source change, rebake or installation was performed.
This work order supersedes the guard-amortisation instructions in the earlier
native protocol and implementation handoff. The active plan's gates are unchanged.

## Evidence reviewed and conclusions

Independent read-only recomputation:
[review.json](../.codex-research/strafe-direction-review-20260907-v1/review.json),
with [reproducer](../.codex-research/strafe-direction-review-20260907-v1/review.py).
It hashes its inputs and recomputes CSV metrics and raw log counts. This is a
focused fact-check, not a complete independent qualification audit. All 1,100
native sources match `strafe-build-flight-audit-20260907` at review time.

| Claim | Review result | Planning consequence |
|---|---|---|
| K03 offline route quality passed | Confirmed: red 10.74%, blue 12.71%; all time/idle/walking comparisons pass on that older build | Keep the discovery result; do not restart K01–K03 or rebake by default |
| Production harm comparison passed | Confirmed on latest twelve runs: six adapter cases each capture twice, no deaths, no lost pickups/captures and all first trips faster | Useful runtime progress, not full K05/K07 acceptance |
| Runtime preserves offline quality | False: red median 54,600 vs57,275 ms =4.67%; blue 54,000 vs57,050 ms =5.35% | Runtime 10% target remains unmet |
| Runtime locomotion is nonregressing | Walking improves in all six; idle worsens in five | Idle is a blocker, not an optional later polish |
| Executions all finish | 79 entries, 53 successful resumed exits | Original frozen execution gate is not met; retain 26 non-completions and reasons |
| Eight-bot cost passes | Latest audit says false; dispatch-only timing omits preparation | Correct total accounting before any new cost qualification claim |
| Missing dispatches are flight-owned frames | All224 missing-dispatch rows have alive<8 and dispatches==alive | They are lost living-client workload, not a counter bypass |
| One preparation per frame closes cost | Observed forecast-only preparation median10.909 ms, max11.642 ms; no observed deferral | One-at-a-time is not a time bound or native contention evidence |
| Endpoint-only world guard batching is safe | Not established; current hashing detects mutation rather than rolling it back | Preserve a post-check and immediate refusal for every rollout |

First-trip idle fractions, using the unchanged solo XY<50 definition through
first capture inclusive:

| Team/phase | Control | Adapter |
|---|---:|---:|
| red/0 |4.3250%|5.6777%|
| red/3 |4.0523%|6.0092%|
| red/6 |3.9721%|6.4501%|
| blue/0 |4.2945%|8.1633%|
| blue/3 |4.4143%|5.2315%|
| blue/6 |4.0369%|3.7019%|

These are development comparisons: the same twelve cases were reused while
fixing skew/release behavior. They are no longer an untouched holdout. K07 needs
a separately declared evaluation after tuning. Do not reinterpret the weaker
harm predicate as the original execution, work-count or full-route quality gate.

## Why the cost direction changes

`Bot2_MoveCostClient` times `Bot2_ExecuteMovement`, increments the living client's
dispatch count, then calls `ClientThink_real`. The latter reaches
`Bot2_FlightAdapterBeforePmove` through `bot2_movebench.c`; preparation therefore
runs outside that timer. Flight command execution through `ai_bot2_supported.c`
is inside movement dispatch. The current `spent.us` sums forecast internals,
not complete library/load/bind/build/admission/selection/publish overhead.

Latest cost arm: dispatch p95 0.8813 ms/max0.9721 ms; server-frame max12.6342 ms;
only139/800 frames have all eight bots alive and moving. All224 frames with fewer
than eight dispatches also have fewer than eight living clients. Investigate
the loss of clients before calling this an eight-active-bot qualification.
Do not relabel a failed composite flag as passing on dispatch time alone.

Profiling identifies repeated full-world integrity checks as a promising cost
target, but its 62–76% attribution is from a separate diagnostic build. The
suggested 6.8/13 ms post-optimization numbers are estimates, not measurements,
and both exceed the existing **5 ms maximum total movement** budget. A25 ms
server tick is not a replacement threshold.

The world guard does not restore arbitrary changed entities, other clients or
nonclock `level` fields. Named clocks/globals/animations are restored separately.
If the first rollout mutates the world, a second could consume that mutation or
undo it before a preparation-wide final hash. A before-first/after-last check
would not preserve the existing immediate refusal contract. Do not implement
that shortcut or justify it by calling the current guard debug-only.

## R1 — complete accounting and honest acceptance (first implementation)

Owners: `bot2_movecost.c/.h`, preparation call boundary in `bot2_movebench.c`,
`ai_bot2_flight.c`, cost runner/report and focused tests. Keep control policy,
artifact candidate order, movement physics and existing thresholds fixed.

1. Add complete preparation/admission/load/selection cost to the same game-frame
   and per-client movement totals as command execution. Include early refusal,
   first-use load and recovery; do not double-count flight execution already
   inside `Bot2_ExecuteMovement`. Separate forecast subparts for diagnosis.
2. Buffer diagnostics outside measured collection. Preserve frame/clock keys,
   native module/source bindings and activity counters. Account honestly for
   measurement overhead: `measureTime` also reads clocks around Pmove steps.
3. Build a narrow reusable adapter outcome audit, proposed
   `tools/strafe_flight_native_report.py`, using the frozen plan, full CSV and
   lifecycle logs. Independently report load/reachability, entered/exited/
   refused/recovered stages, work counts, harm, first-trip10% and idle/walking.
   Preserve original frozen predicates; do not silently modify the campaign's
   `GATE` or conflate all predicates into harm. Check dependent hashes, sources,
   settings, clocks and complete inventory. Failed input remains a failed case.
4. Test one alive flight-owned command increments once, dead clients remain
   visible, preparation appears in that frame exactly once, refusal/load are
   charged, malformed/missing telemetry fails, and no timing from accelerated
   collection can qualify cost. Use negative examples that would have accepted
   this handoff's incorrect claims.

**Done:** complete cost and activity are measurable; the retained reports are
described accurately. A corrected metric may fail—that is expected evidence.
Use one fresh build and focused native timing/outcome checks after code changes;
do not reopen the historical CHAIN2 campaign for a reporting fix.

## R2 — reduce guard cost without weakening isolation

Dependency: R1 accounting. Owners: `bot2_forecast.c/.h`, adapter caller and
targeted native guard tests. Leave legacy callers' public behavior and capture
layout intact; preserve all guarded byte coverage and named restoration rules.

Prototype an opt-in immutable baseline plus exact byte comparison as an
alternative to slow bytewise FNV, with a **comparison after every rollout** and
immediate refusal before any later callback. Keep the baseline separate from
mutable `forecastEntities`; cover the same entity/client/level data as today.
Account for snapshot creation, comparison, memory and cold costs. Do not put
megabytes on the x86 stack. A faster hash is another candidate only with an
explicit integrity/collision/portability argument.

Reusing one immutable baseline may be considered only within a synchronous
fixed-root, no-interleaving scope that still checks each run. Do not carry live
`pmove_t`, borrowed forecast pointers or a live-world guard scope across frames.
Scope exit must work for all early failures; no READY state is published before
verification. This is a hypothesis to measure, not a promised5 ms solution.

Required tests: entity/client/nonclock-level mutation in first/middle/last run;
first run mutates and second would undo it (second must never execute); nested/
busy refusal; reused scope; every early return; root/animation/clock restoration;
legacy result equivalence; no candidate selection after contamination. Mutation
detection is not rollback: preserve the existing failure policy without claiming
a damaged live world was restored.

**Done:** integrity/legacy regressions pass and complete cost improves on a fresh
build. Then measure the maximal reachable two-prefix/four-candidate case, not
only the observed two-run cases. If total cost still misses2.5 ms p95/5 ms max,
declare a bounded-work/refusal policy or another justified optimization. A
rollout cap must expose skipped candidates and its quality cost. Do not remove
coverage from the artifact or adopt25 ms as the gate to obtain a pass.
An improved-guard experiment can finish while the cost gate still fails. R2's
cost closure requires measured complete work within the existing limits; until
then retain an explicit open cost item and implement/verify the chosen bounded
work alternative before calling K05 affordable.

## R3 — transfer route quality to actual runtime entries

K04's old discovery blocker is not reopened. This is a measured K05 runtime
transfer blocker: reduced improvement, five idle regressions and26 incomplete
entries. Keep R1 metrics active and use R2's accepted cost envelope. Initial
diagnosis can use existing captures while R1/R2 are implemented.

1. Attribute idle and lost time to entry waiting, preparation latency, run-up
   refusal, missed window, ground release and follower resume. Include later
   successor effects; a whole trip is not the sum of isolated window savings.
2. Compare the actual preparation root, already-issued immutable command and
   next owned command boundary to the validated forecast. The bench already
   models a common first command. Production preparation occurs after this
   frame's command exists; model that latency explicitly rather than assuming
   a universal `FLIGHT_BOUNDARY_SKEW=2` is a transfer contract.
3. Implement the smallest general actual-state/command-boundary correction in
   the adapter/shared preparation; change flight feedback only if captures show
   a kernel defect. Keep finite support and velocity gates, native timing and
   force behavior. No wider acceptance, per-map offsets or blend sweep.
4. Compare the ranked artifact with the full oracle on selected **actual runtime
   roots** offline to distinguish a missing candidate from bad execution timing.
   Re-bake only if that evidence demonstrates a representation/list deficiency.

**Done:** the production adapter itself reaches the plan's full first-trip speed
and idle/walking gates, with every failed/refused stage reported. Freeze new
evaluation cases before final tuning assessment. If two attempts on the same
representation fail, review it before a third; do not restart unrelated gaps.

## R4 — native contention, recovery and changing resources (K06)

The preparation scheduler has mocked contention but zero observed native
deferrals. Declare a synchronized native multi-client case that genuinely
requests preparation concurrently, with distinct clients and normal physics.
Record every admitted/deferred/refused request, per-client progress, starvation,
same-frame cost and the state used after waiting. Fairness and latency must be
validated; one global slot alone does not prevent repeated low-index wins.
Do not satisfy activity by silently respawning/resetting dead workload clients.

Exercise post-launch airborne refusal, missed landing, impulse/body block,
publication/ownership change, force/Speed expiry, disable and respawn. The
release floor has no native recovery example yet. It also does not cover every
release: Gate/SamePlan/disable paths precede the ordinary flight refusal branch.
Verify a useful airborne continuation or explicit safe owner handoff on each
relevant path; do not assume the comment 'never handed back airborne' proves it.
Ground fallback, waits and refusals remain legitimate when explained and bounded.

**Done:** current-state rejoin/recovery, contention and resource cases pass with
complete cost and no hidden lifecycle reset. Preserve the old pit/drop/body
regressions. This is not yet final defended qualification.

## R5 — freeze the integrated candidate, then finish the existing plan

Run K07 on one frozen integrated revision: M0/M1/M2 retention, matched-resource
references, complete runtime speed/idle/walking,30-case solo resources,12
baseline +12 candidate defended runs at1x/5x, representative eight-active-bot
complete cost and16-bot stress. Retain failures and full frame time; suspicious
control runs may get declared supplemental repeats, not automatic host-blame
or replacement of the original. Outcome runs max four worlds; timing alone.

K08 delivers normal MP/Create Game/addrunner/CHASE+BASE/rates/telemetry after
qualification. K09 transfers the same preparation/controller to Dantooine;
K10 adds offense/survival/force and later aiming under separately declared gates.
No installed settings are promoted by this review. The general architecture,
frozen cached-route reference and paused aiming remain unchanged.

## How the next AI should start

Read the scorecard, this review and relevant source call sites. Implement R1
first; use the recorded review artifact instead of launching another broad
fact-check or all historical tests. Inspect the working diff and current native
hashes; changes to C require a fresh unique build. Retain measured sources until
audit/snapshot completion. Reuse the current artifact and declared adapter
campaign helper; choose fresh output directories and explicitly freeze new
settings/cases. Treat R2 guard design and R3 latency correction as reasoning
tasks with focused negative tests, not permission to waive a contract.
