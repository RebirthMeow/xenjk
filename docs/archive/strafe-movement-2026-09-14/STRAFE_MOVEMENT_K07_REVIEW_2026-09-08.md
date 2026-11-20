# K07 review and R6 execution order

**Executed September 8, 2026:** see the
[R6 execution record](STRAFE_MOVEMENT_R6_EXECUTION_2026-09-08.md) for the
measured causes, corrections and freeze-2 qualification. Two corrections
to this review's premises: the M2 "24 new" failures were 16 new plus 8
invalid, and `StandEntry` was not holding ownership in the carrying stalls;
both stall classes were localnav emitting nothing when its straight ground
corridor was refused.

Reviewed September 8, 2026. This supersedes the September 7 runtime review's
immediate R1–R5 work order. It reviews the committed K07 work and retained
evidence; it does not implement controller changes, run new native worlds or
promote the installed bot. The architecture and acceptance thresholds in the
[active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) remain intact.

## What is established

- R1–R4 delivered complete preparation accounting, an exact per-rollout
  integrity guard, forecast/execution command-boundary alignment, a rebaked
  runtime artifact, explicit lifecycle hand-offs and deferred-first admission.
  Keep these. Endpoint-only guard batching remains unsuitable.
- K07's 12 solo cases pass the first-trip speed/idle/walking comparison:
  ratio-of-team-medians improvement is **10.27% red, 11.86% blue**. These are
  now regression phases (1/4/7), not a fresh holdout. There are **85 verified
  resumed exits and two observation-end censored entries**, not 87 verified
  exits. The report excludes entries less than 3 seconds from recording end
  from its incomplete count; that does not prove their later completion.
- The adapter-armed supported matrix passes 30 cases. Lifecycle reports 14
  passing interventions, two controls and two declared boundary cases; the
  stronger kick still includes the retained red pit death.
- The schedule-side rendezvous fixture produced real production deferrals:
  four at eight bots and two at sixteen, with measured queue length two and
  50 ms wait. This closes the earlier absence of native contention evidence
  for that bounded scenario. It is not proof of fairness for every workload.
- K07 remains incomplete. No new navmesh, planner rewrite or frozen-kernel
  change is justified by these results. The immediate problems are live
  resource admission/continuation and disturbance qualification.

## Findings that change the next work

**1. Separate M2 movement failures, missed interventions and provenance.**
The audited result is 739/780 qualified pickups (94.74%), against 758/780.
The raw runs have 747 flag-grab endings, 21 deaths and 12 timeouts. Eight of
those flag grabs are invalid disturbance trials: all are
`blue_respawn_5__impulse_approach_*`, reaching the flag at 9,775 ms before
the frozen 9,800 ms impulse; no perturbation was applied. They cannot be
counted as successful disturbed cases, discarded from the denominator or
described as failed pickups. Compared by case name, the failed-case sets
are **24 new, 17 shared, five recovered**, not 13 new and two shared.

Four audit predicates fail: `overall_target`, `all_cases_valid`,
`regression_build_compatible`, and `m0_retained`. All family floors pass,
including mid-flight 191/208 (91.83%), although that is below the reference's
203/208. There are 56 changed source-manifest entries. The latter two
predicate failures are provenance/hash checks, not freshly measured M0
behavior failures. An unchanged `bot2_strafe.c/.h` does not establish an
unchanged caller. Neither dispatch regression nor outcome variance has yet
been established as the cause.

**2. Resource parking is a general admission problem.**
Three candidate and six baseline distant stalls carry
`stand_entry_resource`: the runner is carrying, far from its current home
goal, with Speed active and low Force. This is not a legitimate flag wait.
`StandEntry` in `ai_bot2_route.c` retains movement ownership while waiting
for a cached route's resource requirements. It already contains a Speed
deactivation attempt; inspect why the attempt/conditions fail before adding
another toggle. Dispatch calls this before some supported/local continuations.
The general contract should be: an inadmissible preferred route must not
indefinitely suppress a safe resource-feasible continuation. Do not lower
the cached route's resource contract or add a Kejim coordinate escape.

**3. Walking attribution needs an ablation, and one stop is unexplained.**
Travel idle improves in all four defended cells. Walking worsens in three;
the main change is red 5x, **35.323% versus 29.686% (+5.637 percentage
points)**. The other regressions are small but still fail the frozen gate.
The candidate changes both `--supported-corners 2` and the flight adapter;
the baseline has no play arguments. This is a valid whole-configuration
comparison, not an isolated flight-adapter effect. Do not assume the shared
flight executor caused the walking delta.

The candidate has four distant stalls versus six baseline, but red 1x is
two versus one. Aggregate improvement does not satisfy every cell. The
candidate's red 5x r2 interval **432,800–437,800 ms**, near
`(802,-1275,1760)`, is a five-second non-carrying stop with release `none`
and recovery/localnav ownership. Its cause remains unknown. Explaining the
other stalls does not make this one acceptable.

**4. Timing is deferred, not qualified or causally resolved.**
Two of four eight-bot samples exceeded the 5 ms maximum; all p95 values
passed. Sixteen-bot stress retained a 71 ms first-use frame. The September 8
owner decision recorded in the K07 protocol attributes these to ComfyUI and
defers further timing work while it remains running. Respect that decision:
do not stop ComfyUI or spend R6 recollecting timing. Host contention is a
plausible explanation, not a controlled causal finding. Preserve the failed
measurements and label final timing acceptance pending or explicitly waived;
never turn them into a measured pass or import older-build timing as proof.

## R6: ordered, bounded implementation packets

Execute the first packet before choosing fixes. Each packet must produce an
evidence-linked result and a clear next decision, rather than launching an
entire qualification chain after every edit.

### R6a — classify and reproduce the M2 difference

1. Read the K07 and frozen-reference audit summaries. Emit a paired table of
   all 780 names with validity, intervention actually applied, raw completion,
   qualified result and source bindings. Preserve the existing frozen suite.
2. Select one new timeout, one new death/skipped-jump failure, one recovered
   case and one invalid approach case. Run only these on the retained K07
   and reference binaries using the disturbance runner's `--starts` and
   `--cases` options (start name and case suffix are separate arguments),
   distinct output directories and ports. Bind each to its original build;
   compare actual setup state, server/command phase and intervention timing.
   A reference binary on newer checked-out sources may fail current-source
   validation: use its retained source snapshot or an isolated matching
   checkout, never bypass the binding check. Keep repeated outcomes.
3. Trace the first state/command divergence before labeling the cause. If a
   cause is repeatable, make the smallest shared caller/controller correction
   supported by it. If not, characterize bounded repeatability before tuning.
4. For the preempted 9,800 ms impulse, an earlier or event-relative diagnostic
   is a separately versioned suite with paired references. It does not replace
   the frozen 780 cases. Any qualification-fixture revision must explicitly
   justify its coverage and retain both old and new results; no silent gate
   waiver or exclusion. Fresh M0 behavior evidence is needed for the final
   build; do not infer failure or success from the old hash check alone.

### R6b — resource-aware ownership and useful continuation

1. At a retained stand-entry stall, trace preferred-route resource admission,
   Speed duration/Force, deactivate cooldown, the issued generic command and
   the eventual native result. Record feasible supported/local alternatives.
2. Implement a general admission/yield policy if the trace confirms an
   inadmissible route is monopolizing ownership. Keep geometry, landing and
   resource validation; prevent immediate reacquisition loops. A wait must
   have an explicit reason, progress/expiry and fallback decision.
3. Test both teams, outbound/return, low/no Force, Speed active/expired,
   carrying state and a blocked alternative. Require useful progress when
   a feasible continuation exists, no repeated zero-command wait and no
   new resource misuse. Preserve the frozen strafe kernel unless evidence
   demonstrates a defect in its shared control semantics.

### R6c — isolate defended walking and the unexplained stop

1. Inspect the red 5x r2 stop's preceding ownership transitions and commands.
   Reproduce its state with a declared fixture only if needed; do not hide
   it behind a resets-based success counter or change stall classification.
2. On one build and matched red 5x development conditions, compare existing
   baseline, corners=2/flight-off, and corners=2/flight-on. Add other arms
   only if the result needs them. Bind resource/guard settings and all
   non-treatment settings. Attribute walking to owners and transition
   reasons, not merely aggregate speed histograms.
3. Fix the demonstrated admission/continuation mechanism. If R6b fixes both
   symptoms, reuse its evidence; do not create a second rescue system.
   Check blue and 1x regressions before a complete defended collection.

### R6d — qualify the resulting revision, then deliver

After focused checks pass, create one fresh unique native build and bind all
new qualification arms to it. Reuse the
[K07 runners](STRAFE_MOVEMENT_K07_QUALIFICATION_2026-09-07.md#reproduction),
not old measurements attached to a new DLL. Cover M0/M1/M2 retention,
supported resources, lifecycle and contention, and all four defended cells.
Build the missing matched-resource planned reference. Keep phases 1/4/7 as
regression; declare fresh phases before collection if claiming a holdout.
Observe entered flights through exit/release after the first-trip endpoint;
report any censored tails separately from verified completion. Preserve the
first-trip metric and observation limits instead of extending until success.

Do not include deferred timing recollection in an automatic R6 campaign
chain. A changed build still needs final cost qualification when host
conditions permit, or an explicit acceptance decision with that limit visible.
K07 cannot claim all frozen gates passed while it is pending. Then follow
K08 playable installation/telemetry and K09 second-map preparation in the
implementation guide. Normal MP remains unchanged until that delivery step.

## Review evidence and preservation

The reproducible read-only calculation and input hashes are under
`.codex-research/strafe-k07-direction-review-20260908-v1/` (`review.py`,
`review.json`). All 1,102 current native source hashes match the K07 build
manifest. This is a source comparison, not a new loaded-module attestation.
Source inspection covered forecast integrity, production
movement/stand-entry ownership, campaign settings and report censoring;
this is a focused review, not an exhaustive code audit or new live test.
The K07 protocol names the raw collections. Prior active documents were
copied to `.codex-archive/20260908T051507234Z/` with a manifest before these
review edits; no evidence or originals were removed.
