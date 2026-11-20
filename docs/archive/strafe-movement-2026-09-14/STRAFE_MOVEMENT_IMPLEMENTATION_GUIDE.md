# Movement implementation and AI handoff

Updated 2026-09-14. Use this to resume movement work; follow only the references
needed for the task. Historical handoffs are not a startup reading sequence.

## Opening instruction for the next AI

**Work on ordinary baseline movement here on Astra, without subagents.** The
owner has deferred dropped-flag retrieval to later objective/combat work.
Current focus is outbound walking, corner braking and landing continuity.
The owner authorized a coherent handoff redesign. It is implemented: actual-state
native entry, executor-owned snapshots, explicit frontier/destination identity,
frontier yield and admission without waiting for route resources. See the
[handoff contract and fixed cohort](STRAFE_MOVEMENT_HANDOFF_CONTRACT_2026-09-14.md).
Do not restart local prefix-extension experiments. Preparation fallback and
prefix runup remain off. Implementation is not full movement acceptance.
Routine implementation choices do not require an owner decision. Nothing is
installed; broader movement qualification and T5 remain open.

## Next action

**Owner priority correction: defender/body-contact planning is deferred to v2.**
Next establish a current defender-free baseline on both travel legs at normal,
low and zero Force, using the existing decision telemetry to identify remaining
walking, idle and momentum-loss episodes. Prioritize resource-independent
connected hop/landing continuation against those cases, with routes remaining
primary. Verify pickup, corner and jumppad transitions rather than assuming old
complaints still reproduce. Keep legacy-only prediction failures separate from
Runner failures. Do not resume moving-blocker experiments as the next milestone.
Recent fixes remain uninstalled; narrow successful cohorts are not full solo
qualification. Existing safety checks and retained failures remain in force.

### Deferred v2 recovery work

**Dynamic recovery remains unqualified; native command/braking prototype is
implemented and default off.** See the
[three trials, exact failures and next contract](STRAFE_MOVEMENT_DYNAMIC_RECOVERY_2026-09-14.md).
Use `--recovery-native` only for explicit diagnostic work. The preceding
shared ground-descent correction remains active. No installation.

The final prototype remembers failed local legs, aligns primary recovery
commands and checks the actual submitted command plus braking with native
Pmove. All 16 final candidate worlds survive and 14 capture, but red fast
pursuit remains uncaptured and two body stalls return (7.250/7.500 s).
The more aggressive scan/progress trial caused two falls and is retained in
`dynamic-recovery-20260914-a/candidate-a`, not active. Ordinary Runner keeps
its previous activation/scanning behavior and the new option defaults off.

**Deferred v2 implementation:** native-validated candidate ground connections before
selection, with a narrowly defined body-contact model and an admitted onward
route state. Current body/solid refusals can represent unsupported prediction,
not proof that an escape is physically impossible. Do not clear the body bit
or assume a moving body will provide braking support. Separate resource/model
refusals from failed-direction evidence. Latch and verify the selected commands
and receiving state; only then revisit oscillation-aware activation. The exact
narrow-support departure, body cases and pressure worlds are in the record.
Do not substitute another direction/timer tweak. Complete-trip results and
native execution are acceptance criteria; CPU remains separate optimization
or provisioning work. Earlier cached-route and legacy prediction failures stay
open, and dropped-flag/combat work remains deferred.


**Bounded hop supplement implemented and tested, default off.** The owner keeps
routes primary and permits Bot2-style maneuvers only when they show a validated
improvement; real evaluator defects must remain visible and be repaired.
The legacy command-cadence/steering/settings/contact-state defects are corrected.
The opt-in supplement validates a single hop plus native grounded return to the
same publication, compares against ordinary path continuation, and checks live
execution. See the [implementation and evidence](STRAFE_MOVEMENT_HOP_FALLBACK_2026-09-14.md).
Do not switch Runner to pure Bot2. Late-body guards are now exercised; improve
interruption recovery and cover broader phases before ordinary activation. Per owner direction, CPU cost is
measured separately and does not veto a correct, useful movement solution;
optimize later or provision more resources. Keep the
four residual legacy prediction discrepancies and C11 ground fall as separate
open defects; fallback success does not close them. Full connected-hop planning
and movement acceptance remain open.


**Sequence Speed scheduling and direct decision telemetry are implemented.**
See the [sequence planning record](STRAFE_MOVEMENT_SEQUENCE_PLANNING_2026-09-14.md).
The shared owner now considers multiple admitted intervals before buying native
Speed from a full pool, preserving 50 Force and every existing corner/body
contract. Ordinary-spawn scored walking falls 86.250 to 46.825 s across four cases;
all eight primary and four extended candidate worlds capture alive with zero
confinement. Four complete post-purchase trips also improve in the measured
pairs; phase variation limits causal timing claims and idle remains mixed.
The straight-chord prototype produced no admitted shortcut and was removed
from active execution. Its source and failures are retained; do not resume it.

**Legacy comparison completed:** twelve reconstructed residual starts plus
four paired repeats show immediate legacy jumping, but eight of sixteen legacy
worlds have observed fatal falls versus zero Runner deaths. Near-home return
improvements repeat; the near-empty-Force timing win does not. See the
[comparison and exact cases](STRAFE_MOVEMENT_LEGACY_COMPARISON_2026-09-14.md).
This comparison motivated the bounded supplement above. The major native
prediction/live disagreements have now been repaired and receiving-state
admission implemented; retain C09/C12 as failure controls. C01/C02 and C07/C08
supply opportunity/resource cases. Do not merge legacy execution wholesale or
interpret more airtime as success. Full Bot2 combat/power policy was not replayed.

**Broader follow-on: resource-independent connected hop/landing continuation.** Use the
actual decision records to select a repeated low-resource ordinary-route case.
Prepare a maneuver across several intervals, bind actual takeoff and receiving
velocity, and validate it with native rollouts before live ownership. Score the
complete trip and retain body/refusal cases. Do not replace this with another
map-specific exception or lower the existing clearance/arrival gates. The Speed
schedule is a first bounded resource planner, not the full connected-hop solution.
Broader phase, body, defended and cost qualification remain open; nothing is
installed. The [walking inventory](STRAFE_MOVEMENT_WALKING_INVENTORY_2026-09-14.md)
remains the earlier all-episode reference; the new record supplies direct gates.

**Off-line entry milestone completed:** a bounded native-checked walking
approach now connects an unowned slow-ground state to a prepared straight.
The unchanged eight-case cohort captures alive 8/8 with zero audited prolonged
confinement, versus one in preceding C. Seven native approach commits are
retained; six have observed continuation and one is end-censored. See the
[approach record](STRAFE_MOVEMENT_HANDOFF_APPROACH_2026-09-14.md).
Next, advance ordinary-trip idle/walking acceptance using consistent native
start/observation anchors and actual resource/clock phases. Those results remain
mixed; do not call the entire movement project complete or restart the now-done
off-line entry implementation. Broader body, defended and cost gates remain open.

The [owner concerns and review expectations](STRAFE_MOVEMENT_OWNER_CONCERNS_2026-09-13.md)
record the pickup momentum loss, depleted-Force continuations, overly cautious
corners, jumppad exits and the required evidence for a worthwhile playtest.
Use them to evaluate the remaining work against current measured repairs;
do not treat historical complaints as proof that later fixes are absent.

**Owner execution checkpoint:** live game continuation after the takeoff-body
guard is measured, including the final same-frame supported/localnav command,
continued native movement and capture. The normal-speed guarded case captures
alive in 12.225 s; three controls capture in 10.150–10.300 s without false guards.
Four accelerated defended worlds complete 600 s with zero audited confinement.
Retained guard-campaign scoring is complete: T5 remains 0/8 and first-return
mean delta is +0.0433 s against background recovery. See
[current execution](STRAFE_MOVEMENT_LIVE_HANDOFF_2026-09-13.md).
The latest combined partial-prefix, grounded step-join and tap-timing experiment
improves base-goal trips by 3.050 s in both pairs and removes the reproduced
depleted branch regression. Dropped-flag and idle results remain mixed.
Keep experimental switches off outside declared runs;
no installation or broad qualification is claimed. See the
[return acquisition record](STRAFE_MOVEMENT_RETURN_ACQUISITION_2026-09-13.md).

**Latest corner follow-up:** the added base-goal idle is traced and repaired by
native-checked continuation beyond a nearby mesh corner. The radius-only trial
fails and stays off. With `bot_navCornerContinue`, both base-goal pairs improve
34.175→32.150 s and sampled idle 2.000→0.250 s. Controlled body cases refuse the
blocked corridor and retain identical capture time. See the
[corner continuation record](STRAFE_MOVEMENT_CORNER_CONTINUATION_2026-09-13.md).
This is a measured component improvement, not full movement qualification.

**Return counterexample attribution is complete:** same-build controls reproduce
both historical 0.925 s slow trajectories with all experiments off, including
exact equality of the 708 recorded momentum samples. The observed grouping
supports startup movement-clock phase as the cause; no new policy repair is justified by
these two comparisons. See the [same-build record](STRAFE_MOVEMENT_RETURN_COUNTEREXAMPLES_2026-09-13.md).
Preserve prepared tap-chain timing. The historical 60-case score remains mixed;
do not reinterpret this targeted attribution as broad qualification.

1. **Improve ordinary outbound movement and landing continuity.** The owner has
   deferred all dropped-flag retrieval to later objective/combat work, including
   the ramp investigation. Preserve its evidence; it is not a baseline movement
   blocker. A descent issue can re-enter scope if reproduced on ordinary routes
   without a dropped objective. Retrieval through `trigger_hurt` remains out of
   scope. Start from retained standard-route evidence rather than dropped cases.
   The handoff redesign supersedes the following preparation experiments.
   Continue from its fixed-cohort results and actual command owners, preserving
   all failures. Extend the same admission/progress contract where evidence
   requires it, rather than adding map coordinates or route-specific exceptions.
   Two preparation-fallback variants failed full-trip performance; keep
   `bot_supportedPrepareFallback=0`. The broad version worsened normal-spawn
   walking; the narrowed coast-expiry version slowed both matched fixture
   trips by 50 ms and had mixed ordinary-spawn outcomes. All sixteen worlds
   captured alive. The next prefix-publication trial improves one matched
   fixture trip by 3.300 s and leaves the other unchanged, but normal-speed
   walking worsens on both teams. Keep `bot_supportedPrefixRunup=0` too.
   Twenty new diagnostic/comparison worlds complete alive. The unchanged case
   targets a stopped early vertex behind the bot while its published path
   already extends ahead. This motivated the now-implemented actual-state entry
   contract; it is no longer the next unimplemented work item.
   Preserve native fallback checks, resources and lifecycle; judge complete
   ordinary trips as well as the local interval.
2. **Advance full movement and defended qualification after those repairs.** Follow the unchanged
   contract and sections 4/5 of the continuation plan. Keep T6 unchanged; its owner
   decision about knockback does not block self-steered departure fixes. T5
   feasibility is open, not proved impossible by depleted Force.

Use the current sequence-Speed build and the frozen eight-case declaration.
Keep partial spans, step join, tap timing and corner continuation enabled in both
arms, with preparation fallback and prefix runup disabled. Baseline source is
preserved in the study's `frozen-source/`; do not bypass build-source binding
checks to recollect an old binary against the changed checkout. Retain bodyguard and background
builds with their own evidence. Run affected checks
and fresh uniquely named native builds with frozen bindings, as required by the
[verification reference](STRAFE_MOVEMENT_VERIFICATION.md). Preserve all case
pairs, failures and censoring. No installation or threading redesign is in scope.

Also open: valid end-of-world censoring versus missing terminal records in
branch-frame comparisons, T2 landing turns, T4 fallback corners and later launch
coverage. The [branch plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md) owns
package contracts. Do not repeat completed audits/campaigns merely to resume;
targeted reanalysis and requested reviews remain appropriate.

## Current working inputs

- Uncommitted package work remains; inspect relevant diffs and preserve it.
- Current source build: `.codex-research/strafe-build-speed-sequence-20260914-a`.
- Current study: `.codex-research/movement-sequences-20260914-a`; decision audits,
  fixed-cohort and extended-trip comparisons, verified source/settings bindings.
- Previous source build: `.codex-research/strafe-build-handoff-approach-20260914-a`.
- Previous study: `.codex-research/handoff-approach-20260914-a`; `comparison.json`,
  `approach-outcomes.json`, `verification.json` and the retained C source snapshot.
- Previous source build: `.codex-research/strafe-build-handoff-contract-20260914-c`.
- Previous study: `.codex-research/handoff-contract-20260914-a`; frozen baseline,
  three candidate iterations, contract checks and all failures are retained.
- Previous source build: `.codex-research/strafe-build-prefix-runup-20260914-a`.
- Previous trace/comparison evidence: `.codex-research/prefix-publication-20260914-a`;
  `candidate-verification.json` binds that source and twelve comparison
  worlds. `trace-verification.json` binds the preceding trace-only source and
  eight diagnostic worlds; source snapshots are retained.
- Previous source build: `.codex-research/strafe-build-coast-expiry-0fbf7555d0e34c57bfed374045c00444`.
- Latest evidence: `.codex-research/prepare-fallback-0fbf7555d0e34c57bfed374045c00444`;
  `expired/` binds current sources, while top-level trials bind the earlier
  broad version. Both are rejected, default off; all evidence is retained.
- Previous corner build: `.codex-research/strafe-build-nav-continue-b53d672dfcac47499728a024e61c9627`.
- Current evidence: `.codex-research/idle-followup-b53d672dfcac47499728a024e61c9627`.
  Start with `continue-basegoal/comparison.json`, `continue-dropped/comparison.json`,
  `corner-body/comparison.json`, `verification-final.json`, and `integration-lowports/`.
  `basegoal/` is the failed stopping-radius trial. `integration/` is the retained
  interrupted port-collision attempt; it does not replace the clean restart.
- Latest same-build return attribution: `.codex-research/return-counterexamples-8a7d874948664cbe9b94f04a72f30e4b`.
  See `comparison.json`, `attribution.json`, `verification.json`; collection is complete.
- Previous acquisition evidence: `.codex-research/return-acquire-cfe0dcbe32de4e6c9065b28180f5da5a`.
- Experimental switches `bot_supportedGroundSpeed`, `bot_supportedPartialSpans`, `bot_returnStepJoin`,
  `bot_returnTapAtSample`, `bot_navCornerArrival` (rejected) and
  `bot_navCornerContinue` default off. This is not a qualified replacement.
- Previous walking evidence: `.codex-research/live-handoff-54d2cf7dddd34808a50be3c4c66d165f`.
- Guard-only build: `.codex-research/strafe-build-bodyguard-37045bc8f80847618b7f4d860297c923`.
- Current guard evidence: `.codex-research/flight-contact-c9e102ce84ea41baaf1a34be628833a8`.
  See `replay-comparison.json`, `bodyguard-declaration.json` and `guard-verification/`.
- Pre-guard build: `.codex-research/strafe-build-background-final-12c1564c4fdb4f8684cd19f2c946304a`.
- Evidence root: `.codex-research/landing-background-1c665f0490dc44cf971d32c0cb37b4ab`.
- Behavior evidence: `candidate-r{1,2,3}`; final diagnostic correction: `final/`.
- Bindings: `verification-behavior-final.json` and `final/verification.json`.
- Summary: `comparison.json`, `totals-v2.json`, `report.json` and
  `movement-diagnostics.json` under that evidence root.
- Assets remain `.codex-research/pkgD-step3-20260911/assets`.
- Defended discovery: `.codex-research/landing-requery-b64a15afc09445b6b2cbfbb678c67896/defended`.
  It belongs to the first landing trial, with no landing-requery events; do not
  relabel it as final-build qualification. Start with `departure-attribution.json`
  and `walking-attribution.json`, then the exact native logs as needed.
- Earlier trials and all failed checks remain retained. Do not repeat them to resume.
- Installed baseline remains `reviewready-b` / `525f1b50`.

## Read by task

| Task | Reference |
|---|---|
| Inspect latest landing recovery | [Background recovery](STRAFE_MOVEMENT_BACKGROUND_RECOVERY_2026-09-13.md) |
| Inspect latest defended departure and walking | [Defended discovery](STRAFE_MOVEMENT_LANDING_REQUERY_2026-09-13.md#defended-discovery) |
| Inspect prior guard and join repair | [Join approach](STRAFE_MOVEMENT_JOIN_APPROACH_2026-09-13.md) |
| Change an executor release, refusal or admission | [Speed guard](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md), then [degraded release](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md) |
| Change the fast-return join, its retry or its resource policy | [Join race](STRAFE_MOVEMENT_JOIN_RACE_2026-09-13.md) |
| Inspect T5 phase attribution and ranked fixes | [T5 phases](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md) |
| Inspect the preceding spanned-bevel repair | [Corner span](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md) |
| Inspect the preceding ramp-entry repair | [Ramp entry](STRAFE_MOVEMENT_RAMP_ENTRY_2026-09-12.md) |
| Inspect the preceding seam repair | [Seam repair](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md) |
| Inspect the corridor/rejoin repair | [Corridor repair](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md) |
| Change tap admission or branch termination | [Tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md) |
| Locate owners, commands or exact evidence | [Implementation reference](STRAFE_MOVEMENT_CURRENT_REFERENCE.md) |
| Change audits or quote corrected metrics | [Measurement contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md) |
| Fix defended departures | [Fall attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md) |
| Build or collect native evidence | [Verification](STRAFE_MOVEMENT_VERIFICATION.md) |
| Assess completion or architecture | [Project contract](STRAFE_MOVEMENT_PROJECT_PLAN.md) |
| Resolve an old experiment or K-package detail | [Historical handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE_HISTORY_2026-09-12.md) |

Update this short handoff when the next action changes. Keep numbers in the
scorecard and full evidence in dated records; do not append old work orders here.
