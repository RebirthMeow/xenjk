# Depleted-return branch implementation plan

Updated 2026-09-12. Owner-approved review corrections. This is the current
remaining B3/B6, D and E work order, superseding those sections and their
execution order in `~/.claude/plans/fable-see-latest-handoff-curried-summit.md`.
That earlier plan remains historical context for A, C and B1/B2/B4/B5.
The [project plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) retains the end-result
contract; the [scorecard](STRAFE_MOVEMENT_PROGRESS.md) owns measured status.
The September 11 post-campaign review below supersedes earlier completion
claims. Existing implementation and frozen evidence are retained; this review
changes the remaining work order, not the measured sources or installed game.

## Scope and sequence

Keep generated strafe-jump routes, actual-state admission and bounded live
execution. Preserve the fast pickup approach, native resources, supported
fallback, landing/body checks and existing ownership protections. No map-specific
C coordinates, navmesh rebake or general planner rewrite is required.

### Reviewed position after the integrated campaign

Steps 1 and 2 are complete: repeated controls, minimal B3/B6 and BR-A through
production ownership. BR-B also runs end to end. Step 3 remains partial:
later launch families are uncovered. Step 4's collection and regression work
is complete, but its behavioral acceptance and delivery are not. Do not call
all four steps complete or restart the completed collection to resume work.
Work-order items 1 and 2 below are now closed, and item 2's validation exposed
the zero-Force branch hold that item 3 inherits
([tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md)).
See the [step-4 review corrections](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md#post-campaign-review-corrections).

The repeated candidate retains zero rapid join/release cycles versus 29/25/34
in the control, and markedly fewer low-speed descent samples. T2-branch is
**partial, not passed**: entry includes 32/33, descent 50/52, 53/55 and 50/52,
the corridor retains minima of 7.9â€“12.6 u/s, and the frozen rejoin window scores
no transitions. T2, T3, T4 and T5 remain unmet or incomplete. Engine death logs
confirm two normal-speed runner falling deaths and one accelerated runner fall;
T6 remains failed. Those deaths are now attributed: four chasm plunges, two
from opponent knockback and two self-steered under `supported` with the route
inactive, in the
[fall attribution record](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md).
Nothing is installed and K07 remains open.

### Next work, in order

**2026-09-12 corridor update:** the automatic-tap corridor departure and observed
branch-tail refusals from item 3 are resolved on the declared twenty-case set.
Three repetitions have no lower-floor departure or branch abandonment; the
[repair record](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md) binds the fresh
build and checks. The historical item text below preserves what led here.
Item 4's [T5 phase ledger](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md) is now
complete. The [checked seam repair](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md)
now resolves that local stop on its declared case set; overall travel gains
remain mixed. Next is ramp-entry and hop-room continuity. Preparation is not
the exact-12 post-expiry bottleneck. Defended self-steered departures remain open under item 3; this
solo result does not qualify T6 or change its acceptance clause.


1. **Repair measurement contracts and freeze the next comparison.** Preserve
   every old manifest/report. Version corrected audits and any reanalysis in
   new outputs; old raw captures may answer accounting questions without new
   native runs. First reconcile native death cause and victim role, then trace
   the two actual blue normal-speed falls (blue-1x-r1 9:52; blue-1x-r3 3:50)
   for ownership, carrying, contact and preceding commands. A combat-attributed
   killer does not erase `MOD_FALLING`; neither does airborne death establish it.
   Keep the red-1x-r1 5:59 repeater-splash descent as a separate safety case.
   **This item is done.** The death-cause and departure attribution is in the
   [fall attribution record](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md):
   four chasm plunges in the twelve worlds, two from opponent knockback and two
   self-steered under `supported` with the route inactive, on one z 32.1 floor,
   with zero plunges in 180 seeded solo worlds. The contract repairs and the
   versioned reanalysis are in the
   [measurement contracts record](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md):
   bounded recovery, entry-anchored slow entries, T2's contact deadline, T3's
   initial-condition split, T5's exact-12/censoring/post-expiry gates and
   reconciled branch frames, with 459 pure checks passing, every frozen output
   preserved and the corrected outputs under
   `.codex-research/pkgD-contracts-20260911`. What it leaves open needs runtime
   changes and belongs to item 2: one terminal event per branch instance,
   branch instance identity and native event-time state. The next manifest is
   frozen when the repair exists, not before.
2. **Fix tap admission and branch termination with bounded changes.**
   **This item is done.** The repair, its telemetry, its terminal accounting and
   three repetitions are in the
   [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md):
   `takeoff_flight_resources` 6 / 6 / 6 to **1 / 2 / 1** with none of the 30-32
   gradable resource refusals landing on a low jump, entries with no terminal
   event 18 / 13 / 15 to **4 / 7 / 3** (all of them a world's last instance,
   censored by the capture), regressions unchanged at 26/26 and 30/30, churn
   still 0 / 0 / 0, and the corridor window's worst sample 7.9-12.6 u/s to
   **227-231**.
   The envelope was not widened, no geometry is bypassed on a flag, and
   `state->jumpLow` is no longer read by any rule: the authored-tap hint is taken
   from the route (`up==1`), which is what makes the join scan, the lookahead and
   the commitment answer the same question.
   It is **not deliverable as it stands**, and what it handed to item 3 is
   stated there and in the guide's issue 1: on the zero-Force, no-Speed cases the
   runner is no longer ejected from BR-A at index 15 and then flies far enough to
   fall about four hundred units off the corridor at index 34-38, so four cases
   per repetition spend 168-209 frames each under `localnav` climbing back. It is
   survivable -- 0 falls, 0 deaths, 20/20 captured -- and it is the branch
   putting the runner off a ledge on the family it exists for. The original item text follows, for the record.

   Target
   R4/R5/R6b in both directions, where the Speed-clock loss repeats six times
   per repetition. In `FastReturnTakeoffAdmission`, the `speedMs < 2500` branch
   runs whenever `tap` is false, even when the current command has `jumpLow`.
   `FastReturnGroupClass` is telemetry only; changing it cannot fix admission.
   **The caller is identified** in the
   [tap-loss diagnosis](STRAFE_MOVEMENT_TAP_LOSS_DIAGNOSIS_2026-09-11.md): all
   six are the commitment, never the lookahead, all six are on a branch route
   (26 red, 27 blue) at branch index 15 or 16, and every launch on both
   branches is an authored tap chain (`class=0 tap=1` on all six ordinals). The
   refused hops span 277-352 units; the 76-108 figure was the runner's
   remaining distance to the landing. What is still unrecorded is the guide
   offset, the latch and the command's low-jump state at the event, and while
   the runner is on a branch the momentum, admission and release records
   disagree about which route they describe -- fix that telemetry before
   validating the repair or the validation will not be readable. Distinguish an authored
   ordinary tap from a checked substitution for a held jump, and reject stale
   low-jump state. Retain actual-state floor/clearance and landing validity;
   do not widen the 24-unit envelope or bypass geometry on a flag alone.
   Add boundary checks at Speed expiry and around the offset threshold, plus
   negative unsafe/off-guide and held-flight cases before native collection.
   Close every branch instance exactly once on exit, refusal, reset, death,
   objective/owner change or disconnect; censored collection end stays censored.
   Avoid duplicate terminal events on paths that already emit `abandon`.
3. **Validate the focused repair and address confirmed fall mechanisms.**
   **The validation half is done** and is recorded in the
   [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md): a fresh unique
   build (`strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`), the current
   assets byte-identical so the owner change is isolated, three repetitions of
   all twenty cases, and the four required properties met. The later [corridor repair](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md)
   resolves the exposed branch hold and observed rejoin failures. What remains
   under this item is the defended fall mechanisms below. The original item text follows.

   Use
   a fresh unique build, preserve current assets to isolate the owner change,
   and repeat the six tap-loss cases plus BR-A/BR-B entry/exit and boundary
   cases. Require no Speed-clock release of a validated ordinary tap, unchanged
   rejection of unsafe/held launches, zero avoidable immediate churn and correct
   terminal accounting. Investigate/fix concrete fall mechanisms before delivery;
   include the actual blue fall starts and the red combat-descent counterexample
   in focused normal-speed checks. Do not claim a safety cure from non-recurrence
   alone. The confirmed mechanism is now named: the two self-steered departures
   are `supported` hop/walk steps with no floor under the last one, both on the
   outbound fetch leg, at (485, 105) -> (444, 36) and at
   (930..957, 1700..1754). Probe those two sites natively for floor and
   clearance rather than inferring from tapes, and rank the repair against
   three plunges per hour of normal-speed defended play and roughly 1900 passes
   of those neighbourhoods across the twelve worlds. The two knockback
   departures have no post-impulse remedy -- the engine zeroes the command past
   its `fallingToDeath` boundary near z 700 -- so do not spend a fall fix on
   them; the open question they leave is whether a lane hop should survive a
   252 u/s rear impulse, which one timescale-5 sample cannot size. Rerun affected pure checks and 26-body/30-resource regressions after
   owner/kernel changes. No broad new campaign is needed until these fixes hold.
4. **Finish behavior and coverage.** T2 landing-turn braking, T4 remaining
   fallback corners and T5 return times are explicit work, not optional follow-up.
   Rank them using corrected per-family time loss after the tap repair.
   **T5 now has evidence that changes how it should be attacked:** it fails in
   every arm *including the control* â€” exact-12 seeds 18.98-20.60 s against a
   16 s target, zero-Force returns 21.95-23.48 s against 20 s â€” and the newly
   gated post-Speed-expiry mean speed fails four episodes per repetition in
   every arm at 214-294 u/s against 300, the candidate lower than the control.
   It is therefore not a branch-package deficit and no further arm of the
   current candidate will explain it. Decompose a return into phases from the
   retained 40 Hz traces and attribute the seconds per phase before collecting
   anything new; a phase must own the loss before a world is run for it. Keep
   takeoffs 50/57 and other uncovered groups in the coverage table keyed by
   route, takeoff sample and group ordinal. Use native floor/clearance probes
   for unseen candidate geometry; absent grounded recordings are not proof of
   absent floor and do not justify repeated blind seed runs. Retain the branch
   preparation floor gate. There are 30/32 catalog slots; justify any capacity
   change before adding more than one mirrored pair, rather than rewriting the
   planner. BR-A/BR-B assets and the original landing obligation remain distinct.
5. **Qualify the revised candidate and earn delivery.** Declare corrected
   windows, timing anchors, applicable families and failure denominators before
   the collection they score. Compare the repair primarily to the step-4
   candidate, retaining old control and sources-only arms as historical context.
   Three repetitions per configuration remain the minimum repeatability check;
   do not rerun obsolete controls solely because a new source tree exists.
   If missing controls are necessary, collect from a preserved source snapshot
   or isolated checkout with exact bindings, never by disabling source checks.
   Include later-launch cases and normal-speed defended play. Install only
   after the unchanged behavioral gates earn review; campaign execution is not
   acceptance. Preserve failed/censored outcomes and no K07 completion claim.

### Measurement changes required before the next claims

- **Timing:** slow-entry recovery starts at window entry, not exit. Keep
  after-exit recovery as a separate metric. T2's original recovery deadline
  is measured from contact; the step-4 evaluator instead anchors it at the
  minimum. Retain that diagnostic but score the original contact deadline too.
  T3's threshold already met at refusal is an initial condition, not evidence
  of rebuilding after a later collapse: retain the original deadline metric,
  separately report recovery from the following-second minimum and sustained
  speed. Never allow recovery to cross death, respawn, capture or episode end.
- **Denominators:** required cases/repetitions must all be present and valid
  before a report can qualify a candidate. Keep eligible deaths/timeouts in
  the attempted-return denominator; report censoring separately rather than
  `applicable=False`. Exact 12-Force seeds and the broader 1â€“12 group are
  separate. Explicitly score the post-Speed-expiry mean-speed obligation.
- **Events and positions:** add branch instance identity and native event-time
  position, velocity, contact, latch and resources to transition/terminal
  telemetry; add XYZ to the fixture's 40 Hz momentum trace. Bind windows and
  recovery to that instance/life/episode. The historical Hermite reconstruction
  remains useful diagnostic evidence, not a bound proving the 48-unit rejoin
  envelope. Its measured residual can exceed narrow-window margins. Mark
  ambiguous old samples unscorable; publish the corrected sample-44 window
  only for the new manifest, without overwriting the sample-43 result.
- **Causality:** report the sources-only comparison as no difference established
  on its scored aggregates, not proof of equivalence or absence of side effects.
  The branch bundle is associated with the large observed gains; accept-rate
  changes already occur without it and remain a separate diagnostic.
- **Gate aggregation:** each applicable visit/family retains its result. Do not
  turn median improvement into a universal pass or silently set a new tolerated
  failure rate. Reconcile terminal events with all entries before reporting
  branch frame share or completion rate. Treat missing data as incomplete.

## Route coverage is coverage of states

Use the 212 recorded seeds and the
[corridor evidence](STRAFE_MOVEMENT_PACKAGE_D_CORRIDOR_2026-09-11.md).
BR-A's implemented descent and rejoin at sample 44 bypass groups 1â€“3
only for runners admitted at its early entry. The measured corridor does not
visit the original landing platform; this is not a proof that every other
ordinary-jump approach to that platform is impossible.

Declare a coverage table before authoring, with one row per entry family:

| Entry family | Required continuation |
|---|---|
| Pickup overshoot and early run-up; first held flight unaffordable or its run-up unavailable | BR-A entry/connector, descent and checked exit |
| Group 1 successfully flown; group 2 at takeoff 29 subsequently unaffordable | BR-B or a checked connector reachable from that landing state into BR-A |
| Arrival at BR-A's sample-44 exit | Compatible rejoin and admission of the next executable maneuver, or a continuing branch |
| Later refusals at takeoffs 50 and 57 | Checked alternatives; keep open until implemented and measured |
| Each measured momentum-refusal entry family | Feasible run-up or branch from the actual state, not admission to the same unsupported launch |

Cross these families with both teams, Force 0/12/20/26, Speed active/expiring/
inactive, and high versus low momentum with aligned and misaligned headings.
Record which combinations are applicable and why before collection; do not
silently remove unsuccessful combinations. A branch spanning takeoff 29 does
not close the post-group-1 row unless that runner can actually enter it.

## B3/B6: entry, execution and exit contracts

Keep the planned versioned sidecar bound to the catalog hash, atomic refusal
of invalid payloads, generated `STRAFEROUTE1` assets and archive-safe staging.
Validate route/group/index references, branch-role selection, bounded chain
depth and cycles. Check catalog capacity before bundling; the current loader
accepts 32 routes; the current branch catalog uses 30 and the legacy fixture 26. Branches must
not become full outbound guides merely because an entry lies near a stand.

For every transition, publish an empirical admission envelope with numeric
bounds before native qualification. It must include position and height,
horizontal velocity and heading, vertical velocity/contact phase, jump latch,
Force and remaining Speed, collision/floor clearance and the next executable
maneuver. Declare tested interior and boundary states plus negative cases.
XY proximity and projection identify a candidate; they do not authorize it.

- **Entry:** select early enough to retain a feasible moving connector. Admit
  against the actual state and the resources expected at commitment, including
  a pending Speed purchase. Validate static geometry and current bodies. A
  rejection leaves coherent movement ownership and a named safe alternative;
  do not stop or reset the path merely to attempt the same rejected launch.
- **Execution:** preserve native player state, the command clock and jump
  latch. Explicitly map or reset route-local steering, landing/jump target,
  progress watchdog and refusal bookkeeping at a transition. Key branch
  progress and refusals by route as well as group/index so reused indices do
  not alias. Keep airborne ownership until a checked transition or recovery.
- **Tail:** distinguish intermediate branch completion from final objective
  arrival. `Bot2_StrafeCommand` currently steers directly to `state->goal` in
  the final seven samples and applies arrival/jump rules in the final eight.
  A pointer/index switch alone is insufficient. Use explicit intermediate
  terminal semantics or a validated continuation overlap; do not point the
  branch tail at the capture stand through unchecked geometry or brake at an
  artificial branch endpoint. Preserve legacy final-arrival behavior and
  regression coverage if shared kernel inputs must change.
- **Exit:** the implemented rejoin is sample 44. Validate full state compatibility,
  including the takeoff cue at 43, before switching. Do not replay a passed
  launch or skip one still needed. Admit the following maneuver with actual
  resources and momentum; if it fails, continue on a checked alternative.
  Failed rejoin must have defined recovery, not repeated ownership churn.

Pure checks must exercise sidecar refusal, branch selection exclusion, entry
rejection, intermediate tail behavior, jump-latch continuity, airborne exit,
rejoin refusal, route-index bookkeeping and lifecycle reset. Native tests must
then demonstrate actual entry, branch travel, exit and onward execution; a
capture after silently abandoning the branch is not a branch success.

The recorded pure checks pass for BR-A (`strafe_catalog_check`,
`strafe_fast_return_check`, the new `strafe_intermediate_tail_check`), and the
native demonstration is recorded in the [BR-A record](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md). Two rules that record
adds, both learned from falls: a branch is refused outright unless every sample
stands on prepared floor, and an authored tap requires the guide to stay within
12 degrees across the tap's own flight. Nearness to measured play is not
evidence of standable floor when that play was airborne.

## E: evidence and acceptance corrections

Retain original T1â€“T6 results within their original scope. Do not relabel an
old failure or bypassed window as a pass of this plan.

- **T1 resource diagnostic:** retain zero resource-reason releases within
  300 ms of joining. Add **T1-all**: count every join followed by release
  within 300 ms, by reason, route, group and entry family. In declared solo
  branch cases require zero such cycles caused by a resource/momentum/geometry
  incompatibility already present at admission. Report all other cycles too;
  attribute external interruptions from traces rather than filtering them by
  reason name. Defended outcomes retain every cycle and its context.
- **T2 original landing turn:** retain minimum 350 u/s, recovery to 425 within
  0.5 s, and no sample below 300 before the next maneuver for applicable
  12-Force entries that actually fly group 1. BR-A bypass is not a T2 pass;
  retain dedicated landing-state cases for this obligation and BR-B coverage.
- **T2-branch:** publish separate spatial windows for entry, descent (including
  y approximately 4500), blue's additional choke near mirrored y 4300, and
  rejoin. Bind windows to route arclength, height and events so another floor
  or branch cannot satisfy them. For moving entries at least 300 u/s, require
  minimum 200 u/s and no acquisition stop through descent/rejoin; require
  425 u/s within 0.5 s of exit with Speed active, and 300 u/s within 1 s with
  Speed inactive/expired. For slow entries, measure time/distance to 300 and
  425 separately and declare family-specific numeric recovery limits before
  collection; never apply a moving-entry minimum to an initial rest state.
- **T3 refusal continuation:** retain the original targets (no stationary
  sample, no sample below 200 in the following second, 425 within 0.5 s) for
  applicable moving entries. Report any failures by resource state; changed
  route windows do not erase them.
- **T4 corners:** still measure each published accepted curve/pass window,
  retaining the original 250 u/s minimum, at most one zero-input sample and
  no acquisition stop. Report constrained vertices separately; a bypass does
  not prove the fallback corner passed. Attribute blue's additional choke
  from retained traces before choosing its remedy.
- **T5 travel:** retain 12-Force seeded returns at most 16 s both directions;
  zero-Force/no-Speed at most 20 s with mean speed at least 300 u/s; sustained
  post-expiry hop travel mean at least 300 u/s. Report high/low entry momentum
  and resource conditions separately. Pooled station medians of 425â€“750 u/s
  do not establish zero-Force/no-Speed competence.
- **T6 safety/lifecycle:** retain no solo falls and both-team defended capture
  with no falling deaths or five-second confinement. Retain deaths, failed
  entries, branch abandonment and censored attempts in all denominators.
  **An owner decision is outstanding on the falling-death clause, raised and
  not decided.** Of the four chasm plunges two are opponent knockback â€” the
  impulse measured 5-10 degrees from directly away from an opponent 200 units
  off, the velocity turning 96.5 degrees at constant magnitude â€” and the engine
  seals the outcome roughly 1100 units above the floor, zeroing the command past
  `fallingToDeath`. At normal speed the split is one knocked and two
  self-steered, and one of the two normal-speed falling deaths is knocked. No
  movement policy recovers a runner after that impulse, so a movement package
  cannot earn this clause in full as written. The options are to keep it and accept that T6 stays failed for
  reasons outside movement, to restate it over self-steered departures only, or
  to restate it as a rate against measured traffic (35-62 entries per world into
  each of the three departure neighbourhoods). The two self-steered departures
  remain fixable and stay in scope either way. Do not choose unilaterally, and
  do not rescope the target to make a package pass. See the
  [fall attribution record](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md).

Before collection freeze cases, windows, settings, repetitions and the audit
rules in a manifest. The original control and integrated campaign are collected; the audit corrections
above remain pending:
`tools/strafe_branch_manifest.py` writes the manifest,
`tools/strafe_momentum_audit.py` carries the T1-all, route-attribution, family
and branch accounting, and `tools/strafe_branch_report.py` reports per case and
per repetition and refuses a claim inside the control's own spread. The measured
control bands are in the
[control record](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md); a candidate
arm is scored against those, not against a single earlier arm. Use at least three complete repetitions of the same
declared case set per control and candidate configuration. This is a minimum
repeatability check, not a statistical power claim. Preserve matched case IDs
and declared seeds; do not claim deterministic paired combat. Collect controls
before source edits, then repeat the candidate on its frozen build under the
same settings; report any host/session differences and all repetition results.
Do not stop at the first favorable repetition or pool away failing families.

Report per-case and per-repetition all-reason churn, branch selection/entry/
completion/exit, fallback time, lane and branch frame shares with explicit
denominators, speed/zero-input metrics and capture/censor outcomes. Gains
comparable to same-configuration spread remain inconclusive. Additional runs
need a declared unresolved question, not a search for a passing aggregate.

After owner changes, rerun the 26 temporary-body and 30 resource regressions
on the fresh build along with affected pure/Python checks. These fast-return-off
regressions protect existing behavior; branch-on native coverage is a separate
obligation. Freeze sources during build/collection, use hidden isolated worlds
and the AGENTS.md process limits. Final defended checks run at normal speed.
Install only after these behavioral targets earn review; state remaining
uncovered cases explicitly and do not claim K07 qualification.
