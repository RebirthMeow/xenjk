# Strafe movement progress — retained history

Historical snapshot preserved during the September 12 instruction cleanup.
Contains superseded status and work orders, not current instructions.
Use the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) to resume;
read this snapshot only for a specific old contract, result or counterexample.
Original bytes are also retained in the archive.

Reviewed 2026-09-12 UTC. R1 complete accounting, R2 guard cost reduction,
R3 runtime transfer and R4 native lifecycle/contention were implemented and measured
in the September 7 work; R5 (K07) then froze one integrated revision and measured
it end to end (see the
[K07 protocol](STRAFE_MOVEMENT_K07_QUALIFICATION_2026-09-07.md)). This
scorecard supersedes the preceding preparation session's status claims. See the [active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md),
[execution guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) for the current work
order and the [September 8 K07 review](STRAFE_MOVEMENT_K07_REVIEW_2026-09-08.md)
for historical decisions.
General aiming remains paused. The current provisional normal MP installation
is recorded below; earlier installation statements are historical.

The preceding documents are preserved in the
[review archive](../.codex-archive/20260907T194324685Z/manifest.json).
Historical protocols retain all experiments, including failed evidence.

## Current position

**The branch package is measured end to end and is not installed.**
The [branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md)
owns the remaining work. Steps 1/2 are complete, step 3 is partial, and
step 4 collection is complete while acceptance remains open. The remaining work
order's step 1 is closed and **its step 2 -- the tap repair -- is implemented
and measured** on 2026-09-12: the Speed-clock refusal of a checked tap is gone
(6 / 6 / 6 events to 1 / 2 / 1, and none of the gradable resource refusals lands
on a low jump), every branch instance now carries exactly one terminal record,
the two regressions are unchanged, and the corridor window's worst sample
improves from 7.9-12.6 u/s to 227-231. It is **not deliverable as it stands**: on
the zero-Force, no-Speed cases the runner now flies BR-A far enough to fall ~400
units off the corridor at branch index 34-38, and the four affected cases spend
168-209 frames each under `localnav` climbing back. Record:
[tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md). **Three issues own the
next sessions**: the zero-Speed branch hold and the branch-end rejoin the repair
exposed, T5's return times (failing in every arm including the control, so not a
branch deficit, and needing a mechanism rather than another collection), and an
owner decision on T6's falling-death clause. They are set out in the
[opening instruction](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#opening-instruction-for-the-next-ai). The
[post-campaign review](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md#post-campaign-review-corrections)
corrects gate scope, tap diagnosis and death attribution; frozen outputs remain
unchanged. The death attribution is then completed in the
[fall attribution record](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md),
which measures all four defended-world plunges from the retained tapes: two
opponent knockback, two self-steered `supported` steps with no floor under the
last hop, one z 32.1 floor, and zero plunges in 180 seeded solo worlds. The
[step-4 record](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md) carries the
integrated campaign: three arms of three repetitions on the twenty declared
cases, the per-window T2–T5 evaluation the control record called the next tool,
the regressions, and the twelve-case defended campaign at normal speed.

| Retained target | Result |
|---|---|
| Tap admission (work order step 2) | **met and measured** — `takeoff_flight_resources` 6 / 6 / 6 to **1 / 2 / 1**, and none of the 30-32 gradable resource refusals per repetition lands on a low jump; terminal-less branch entries 18 / 13 / 15 to **4 / 7 / 3**, all of them the world's last instance at capture; regressions 26/26 and 30/30 unchanged; corridor window worst sample 7.9-12.6 to **227-231 u/s**. **Uncovered**: `localnav` 2 / 9 / 2 to **775 / 765 / 735** and stationary 71 / 73 / 64 to **436 / 437 / 419**, entirely in the four zero-Force, no-Speed cases, where the runner falls ~400 units off the corridor at branch index 34-38 and climbs back — survivable (0 falls, 0 deaths, 20/20 captured) ([record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md)) |
| T1 resource-reason churn | **met** — 0 / 0 / 0 in control and candidate |
| T1-all all-reason churn | **met** — 29 / 25 / 34 to **0 / 0 / 0**, outside the control's declared band |
| T2 original landing turn | **failed** on its 350 u/s minimum: 2 of 37 control turns, 0 of 28 candidate. Recovery half met |
| T2-branch windows | **partial / unqualified** — entry 32/33 in r2; descent 50/52, 53/55, 50/52; worst corridor samples 7.9–12.6 u/s; no scored rejoin. Native transition measurement remains required |
| T3 refusal continuation | **improved, not met** — 42 / 41 / 42 of 57 / 59 / 56 against 7 / 5 / 8 of 45 / 41 / 50 |
| T4 accepted curves and passes | **not advanced** — a third as many reached, pass rate 52–57 % to 44–49 % |
| T5 travel time | **failed in every arm including the control** — 0 of 8 per repetition |
| T6 safety and lifecycle | solo half **met** (0 falls, 0 deaths, 180 / 180 captured, and no sample ever below z 1808); defended: both teams capture at normal speed and no normal-speed confinement, but **four chasm plunges, three of them at normal speed**. Across the four, two are opponent knockback and two are self-steered under `supported` with the route inactive; **at normal speed the split is one knocked and two self-steered** ([attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md)) |

The frozen evaluator's figures above are superseded in four places by the
[corrected measurement contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md):
the candidate corridor window meets 81/93, 89/98 and 84/96 rather than 84/92/87;
T3's met counts hold but 38 of 41 are thresholds already met at the refusal,
with the arms separating instead on the median slowest speed in the following
second (117-127 control against 361 candidate); T5's eight denominators are 4
exact-12 seeds, 2 zero-Force and 2 Force-1-11, and its newly gated post-expiry
mean speed **fails four episodes per repetition in every arm**; and branch
frames, once reconciled against what ended each entry, are 11923-12166 against
16649-17810 lane frames and are blocked from comparison.

**No speed or return-time improvement is claimed.** Everything that separated is
about ownership and about samples below a floor. Nothing is installed and K07 is
not qualified. Recovery and travel verdicts above are the frozen evaluator's
results; its timing anchors and failure denominators require the corrections
in the current plan before qualification.

**Packages A and C are done and measured (September 10), B1, B2, B4 and
B5 are done and measured (September 11), the branch plan's step 1 — the frozen
case set and three repeated control arms — is collected, step 2 — one
complete BR-A transition with the minimal B3 loader/owner machinery and the B6
depleted choice — is implemented and flown in both directions, and step 3 is
done in part (September 11): BR-B covers the post-group-1 landing state and the
rejoin row closed with it, while takeoffs 50 and 57 are measured with dedicated
seeds and remain uncovered, and step 4 — the integrated repeated campaign, the
affected regressions and the defended checks at normal speed — is complete and
recorded.**
Nothing is installed; the installed build remains reviewready-b / `525f1b50`,
and K07 remains open. Dated records: the
[tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md), the
[step-4 record](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md), the
[BR-B record](STRAFE_MOVEMENT_PACKAGE_D_BRB_2026-09-11.md), the
[BR-A record](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md), the
[control arms](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md), the
[Package B record](STRAFE_MOVEMENT_PACKAGE_B_2026-09-11.md) and the
[depleted-return record](STRAFE_MOVEMENT_DEPLETED_RETURN_2026-09-10.md), which
carries the per-case table of which maneuvers work and which remain weak. The
next AI starts from the
[opening instruction](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#opening-instruction-for-the-next-ai).

- **Branch step 4 — the integrated campaign separates, and it separates on
  ownership rather than on speed.** Three arms of three repetitions on the twenty
  declared cases, against a manifest frozen before the first world: all-reason
  churn **29 / 25 / 34 to 0 / 0 / 0**, joins 72 / 65 / 82 to 41 / 45 / 41, lane
  frames 7006–7909 to 16649–17810, branch entries 0 to 52 / 51 / 52 with
  34 / 38 / 37 admitted exits, and 20 of 20 worlds captured in every repetition
  of every arm. A **third arm — the same build with no sidecar — is
  indistinguishable from the control** on every aggregate the report tool scores,
  which supports a branch-bundle effect without proving source equivalence; the accept rate is the one measure the source edit does move, by
  about twice its declared band. In the declared windows, measured for the first
  time by `tools/strafe_branch_windows.py`: the descent's lane ownership goes
  **6 % to 89 %**, its median visit minimum **45.7 to 361.4 u/s** and its samples
  below 200 u/s **601–828 to 19–21**; the entry window's median visit minimum
  goes 180–235 to **417**. **What did not improve is stated the same way**: T2's
  350 u/s landing turn is unmet in every arm, T5's return times are unmet in
  every arm (best 12-Force return 17.9 s against 16), T4 is not advanced, and the
  corridor's *worst* single sample gets worse. That last one, the worst T3
  continuation and six lost branches per repetition are **one defect**: a tap
  judged as a full flight whenever `speed_remaining` is under 2500 ms — which
  costs a **26-Force** runner its branch as readily as a depleted one, correcting
  the BR-B record's framing. Defended at normal speed: **both teams capture in
  every repetition**, 143 branch entries and 101 admitted exits across twelve
  worlds, no normal-speed confinement — and **two engine-confirmed normal-speed runner falling deaths**, both blue.
  The previously cited red descent was a repeater-splash death; its motion
  remains a separate safety case. Two telemetry gaps found: a mid-branch release and a death both
  end a branch with no branch record, so branch frames are not reported as a
  movement share. Record:
  [step 4](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md).
- **Branch step 3 — BR-B closes the post-group-1 row; takeoff 57 is measured and
  still uncovered.** The connector from the group-1 landing platform to the
  corridor is one flat floor at z = 1808.1, measured from the 24 grounded tracks
  of the only episodes that have ever stood there, and it merges into BR-A's own
  stations where the two independent estimates agree to **0.8 units**. A runner
  at **zero Force with no Speed, at the 250 u/s ground cap**, enters it, runs it
  and rejoins the lane at sample 44. Twelve seeded cases, both directions, three
  repetitions per arm, 72 worlds, **0 deaths and minimum health 90 in both arms**:
  branch entries 14 → 23 and admitted exits 14 → 21 (median, reportable against
  the control's own spread), all-reason churn unchanged at 2, and in the windows
  the branch owns the samples below 300 u/s fall by half or more with no overlap
  across repetitions — while the part of the return no branch reaches does not
  move at all. **No speed or return-time claim**: the connector's median is 425
  in both arms, the ground cap; what changed is that the lane keeps the runner.
  Two runtime corrections: a lane's branches are now chosen by the runner's
  position rather than by sidecar order, and the rejoin admits against the next
  *executable* maneuver rather than the launch behind it — which also closes
  BR-A's 12-Force abandon, in both directions. Takeoffs 50 and 57 now have
  dedicated seeds: 50 is refused **zero** times in 36 worlds, 57 **ten**, eight
  of them dropping below 200 u/s within a second and one to 7.8, which makes it
  the worst refusal family in the return — and no line is authored for it,
  because across 187 carried episodes **no runner has ever stood** on nine of the
  50-unit y bands such a line would cross. Record:
  [BR-B](STRAFE_MOVEMENT_PACKAGE_D_BRB_2026-09-11.md).
- **Branch step 2 — BR-A runs end to end, and two authored lines fell first.**
  The complete transition works in both directions: entry from the join scan and
  from a lane refusal, the whole descent and corridor, an admitted rejoin at lane
  sample 44, and the lane's own hop chain afterwards from 392 to 948 u/s with no
  release. Four worlds, 2 captures each, **0 deaths and minimum health 90**.
  At the station the corridor record named as the only bottleneck, y = 4500, the
  matched median is **139.9 u/s in the control and 403.7 with the branch**, and
  every branch-owned sample through descent, corridor and rejoin stays above the
  declared 200 u/s minimum; the rest of the corridor is unchanged (427 → 425),
  as that record predicted. **This is four worlds and one repetition: no speed or
  churn claim is made**, and the campaign against the control bands is step 4.
  Getting there cost four native defects, two of them falls: an authored line
  built on the median of *all* measured samples followed the hop arc those
  runners fly and dropped 1776 units into a gap, and a tap placed into the
  70-degree turn that avoids that same gap flew over the edge for 500 more.
  Stations are now grounded-only medians, the runtime refuses any branch with an
  unsupported sample, and taps require a straight guide. The declared rejoin
  moved from lane sample 43 to 44, because a runner that crossed the gap on foot
  no longer needs the launch that crosses it. The 12-Force runner that completed
  the branch and was then refused the lane's next flight was step 3's finding:
  the gate was asking about the launch at 43, and that is now fixed. Record:
  [BR-A](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md).
- **Branch step 1 — the control is repeated, and two measures were corrected.**
  Three complete repetitions of the twenty declared cases on the frozen Package B
  build, 20/20 worlds each, 60 worlds retained, against a manifest frozen before
  the first world started. **Resource-class churn is 0, 0, 0**: T1 holds under
  repetition, and all 88 churn cycles across the three repetitions are momentum
  refusals. But **joins (26 %), releases (34 %) and all-reason churn (36 %) are
  themselves unstable** at this sample size, along with lane frames (13 %),
  supported frames (12 %), zero-input (14 %), stationary (12 %), admits (16 %)
  and the trace denominator (12 %); only the accept rate (0.78–0.79) is stable.
  Package B's joins 119 → 80 and releases 91 → 49 survive these bands; no future
  arm may claim a churn improvement inside 25–34. Branch selection, entry, exit,
  abandonment and branch frames are 0 against 65–82 real joins and 193–226 real
  admissions — a structural zero, now measured rather than missing. Two
  corrections are recorded in the dated record: `takeoff_momentum` was being
  classified as a resource reason by a prefix test, and a field named `full_stop`
  measured something else and is replaced by three that say what they count.
  58–65 stationary samples per repetition are attributed to the supported walker
  braking at Force 100 with no lane, in 105 windows of 100–425 ms; they are not
  lane stalls and not a T3 failure. Record:
  [control arms](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md).
- **Package D groundwork — the corridor is measured and BR-A moves.** The
  measured depleted corridor does not reach the original sample-24..27 rejoin
  via the landing platform. Across 22 red and 23 blue depleted episodes the
  runner never came within 350 units of it. The proposed branch owns the
  descent with a candidate rejoin at 43/44; XY convergence does not establish
  full-state compatibility. It bypasses groups 1–3 for early BR-A entries
  only; later landing-platform entries still require BR-B or a connector.
  Pooled red station medians are roughly 403–750 u/s outside the y = 4500
  choke (median 101 u/s), where short navmesh legs retain the strict stop.
  Blue also has a second choke; zero-Force/no-Speed competence is not
  established by these pooled values. Nothing is authored yet. Record:
  [corridor](STRAFE_MOVEMENT_PACKAGE_D_CORRIDOR_2026-09-11.md).
- **Package B — the lane stops joining launches it cannot afford.** Launches
  are classified once from the route's own geometry with the receiver measured
  takeoff to landing (`routeGroup_t`, `BuildReturnGroups`); the takeoff ladder
  is evaluated before commitment against the resources the launch will meet,
  including the Force Speed the lane is about to buy
  (`FastReturnProjectedResources`); repeated resource refusals back off
  500 -> 1000 -> 2000 ms; and excess ground speed can ride airborne on a
  checked straight (`bot_fastReturnMomentumTap`). Across the same 20 seeded
  worlds, control -> treatment: **joins released for a resource reason within
  300 ms 28 -> 0 (T1 met), joins 119 -> 80, releases 91 -> 49, admission
  accept rate 0.67 -> 0.79**, captures 20/20 and full-stop worlds 0 in both.
  **Total churn did not fall** (34 -> 33): its cause moved from resource
  refusals to momentum refusals, which are what Package D must branch. Frame
  and sample aggregates are reported as *no change established* — two arms of
  identical logic differed by 13 % in lane-owned frames and 50 % in zero-input
  samples, so a single twenty-case arm cannot resolve them. Three refuted
  designs are retained with their measured costs. Both regression suites pass
  on the same build: 26/26 temporary-body and 30/30 resource cases
  (`.codex-research/pkgB-regression-20260911`). Record:
  [Package B](STRAFE_MOVEMENT_PACKAGE_B_2026-09-11.md).

- **Package A — attribution, not a fix.** New telemetry (`[FASTRETURN]
  admission`, an extended `release`, and a 40 Hz `[MOMENTUM]` trace under
  `bot_momentumTrace`), a seeded native start velocity (`bot_cornerVelocity`),
  `tools/strafe_play_state_seeds.py` (212 start states extracted from the
  owner's own four playtest CSVs) and `tools/strafe_momentum_audit.py`. Twenty
  seeded worlds in both directions reproduced all seven owner issues and
  attributed them. Key findings: the receiver reserve refused a gap flight the
  same runner completed 500 ms later at the same 16 Force; the launch class
  depends on where the runner stands rather than on the launch, so churn itself
  lifts the reserve; a refused takeoff costs about 360 u/s within 100 ms; and
  the 250 u/s cap dominates every stretch without Force Speed. Record:
  [attribution](../.codex-research/depleted-return-20260910-a/attribution.md).
- **Package C — corner momentum, implemented and measured.** New ground-turn
  speed law with the lateral bound *selected* by a new pure check
  (`tools/strafe_corner_track_check.py`) rather than assumed, pass-through
  vertices, widened corner admission with named refusals, and a bounded
  acquisition coast. Across the same 20 seeded worlds: stationary carrier
  samples 366 -> 64, worlds containing a full stop 16 -> 0, zero-input samples
  436 -> 208, join-then-resource-release churn 61 -> 34, lane-owned frames
  6076 -> 8540. Two pure checks that had been stale since the fast-return lane
  landed (`strafe_route_resume_check.py`, `strafe_catalog_check.py`) were
  repaired and pass. Record and honest gaps, including the unmeasured T4 and
  the untouched body/resource regressions:
  [corner momentum](../.codex-research/corner-momentum-20260910/record.md).

**Owner review identified unresolved return blockers (September 10).** The
seven issues below remain the work order; A and C address issues 1, 4 and 5 in
part and leave 2, 3, 6 and 7 to Packages B, D and E.
Installed build remains reviewready-b / `525f1b50`.
Observed route 5 drops from 889.9 to 428.9 u/s, then rejoins and immediately
refuses the same low-Force maneuver. Reused lanes lack prepared depleted-Force
branches; generic fallback remains too cautious through corners and lacks an
effective momentum-preservation/rebuilding strategy. The 12.0625 s natural
median depends on approximately 10.56 s active Speed; it does not validate
zero-Force/Speed-expired competence. Previous test results remain valid within
their scope, but their readiness criteria were insufficient. All seven owner
issues and ordered next steps are recorded in the
[current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md#opening-instruction-for-the-next-ai).
The next checkpoint is implementation and resource-separated native evidence,
not another unchanged-candidate retest. K07 remains open.

**Current review candidate installed September 10:**
`strafe-build-reviewready-20260910-b`, backup `20260910T0607586345246Z`, nine
installed hashes verified. The receiver-specific 20-Force reserve replaces the
blanket 50 gate while preserving the corner/arrival and join/floor fixes.
Natural eight returns all capture: median 12.0625 s, mean horizontal speed
538.6 u/s, airborne 64.3%; the first red return still takes 24.1 s. All 14 solo
worlds: 56 captures/no deaths/no sampled idle longer than 1.0 s. Normal-speed
one-enemy MP: five captures/four runner weapon deaths/no falls/no long stalls.
Defended seven pickups include two carrier deaths; completed median 24.325 s.
This meets the bounded review-delivery targets, not K07 or human competition
qualification. See [the final record](STRAFE_MOVEMENT_REVIEW_READY_2026-09-10.md).
Human in-game review is now warranted. Earlier installations below are historical.

**Corner repair installed September 10:** `strafe-build-corners-20260910-b`,
backup `20260910T0541114616343Z`, nine installed hashes verified. Checked
streaming curves preserve follower indices; analog grounded arrivals resolve
the observed 24.99-unit deadzone. Failed fast-lane joins are contained with
rising-sample rejection, forward-floor checks and local sustained-jump reserves.
Ten solo worlds: 35 captures/no deaths. Two normal MP one-enemy worlds:
four captures/two runner weapon deaths/no falls or audited long stalls.
Natural captured-return median is 19.575 s, slower than the prior roughly
12 s fast-return build. See [the repair record](STRAFE_MOVEMENT_CORNER_REPAIR_2026-09-10.md)
for residual braking, retained failures and scope. Human retest is next; K07
stays open. Earlier installation statements below are historical.

**Human corner study, September 10:** four full Kejim recordings decoded
(134,870 snapshots). Selected human corner windows maintain motion where the
bot brakes to 34–56 u/s; two full carrier examples take 15.774 and 11.650 s.
These are observational examples with retained failures, not matched benchmarks
or qualification. Explicit ordinary-corner stopping and disabled streaming
corner rounding identify the next execution target; a longer zero-input
supported stall is separate. See the [study and next steps](STRAFE_MOVEMENT_HUMAN_CORNERS_2026-09-10.md).
Production source and the installed fast-return build are unchanged.

**Fast return delivered September 10:** the owner found the preceding pickup
repair insufficient. `strafe-build-fastreturn-20260910-d` is now installed with
`bot_fastReturn 1`; backup `20260910T0426153079480Z`, nine hashes verified.
Approach-time lane preparation, supported local joins, checked ordinary-tap
corridors and local jump/Speed resource decisions allow fast return sections
without requiring full-trip Speed. Legacy whole-guide gates are unchanged.
Development-c: eight solo cases, 31 captures/zero deaths; natural capture median
11.825 seconds versus prior 24.75. Final-d: normal MP red/blue one-opponent
worlds yield three captures, four weapon deaths, no falling death, captured-return
median 14.325 seconds versus prior 26.35; two pad cases pass. Small unmatched
cohorts, not proof of a survival improvement. Severe low-Force first returns can
still take 20–24 seconds; initial clearance is not universally improved.
Legacy fast-return-off regressions pass 26 body/30 resource cases. See the
[fast-return record](STRAFE_MOVEMENT_FAST_RETURN_2026-09-10.md) for retained
failed builds, scope and next action: human retest. K07 remains open.

**Human-feedback repairs implemented and tested:** current source build is
`strafe-build-pickup-pad-20260910-c`. Pickup return executes validated intervals
without resource waiting; a late preparation failure cannot rewind the moving
return. Pad steering preserves active cached routes and selects a nearer
floor-checked landing. Both normal-speed pad cases land alive; temporary-body
regression passes 26/26 and resources pass 30/30. Installed with recoverable
archive `20260910T0345452433175Z`; nine hashes verify.
Normal MP with one opponent completes both 120-second
windows: red two captures/zero deaths, blue one capture/three deaths, no audited
confinement or distant stall. See the [repair record](STRAFE_MOVEMENT_PICKUP_PAD_2026-09-10.md)
for source bindings, retained failed attempts and installation. Human retest is
next; ordinary return speed and full K07 qualification remain open.

**Original human playtest:** 233 seconds, eight pickups, no captures.
The owner's main complaint is stopping and ground running after fast flag
pickup. Seven pickups report `stand_entry_resource`; pickup/return ownership
and resource waiting motivated this repair. A ten-second fixed-XY vertical bounce
is a separate secondary failure missed by the ordinary 3D confinement metric.
See the [human-play review](STRAFE_MOVEMENT_HUMAN_PLAY_2026-09-10.md).

The owner's latest priority is [80/20 competence](STRAFE_MOVEMENT_PROJECT_PLAN_HISTORY_2026-09-12.md#current-8020-triage-competent-kejim-play):
review observed survival/recovery defects first, then visibly wasted movement;
defer exhaustive qualification and marginal timing optimization unless actual
play or supported workload expansion warrants them. No new measurements were
made for this triage update.

**Playable candidate is the immediate delivery checkpoint (September 10 UTC).**
The owner requested normal in-game play soon and confirmed ComfyUI was closed.
See the [playtest record](STRAFE_MOVEMENT_PLAYTEST_2026-09-10.md). The Q3 v4
bench completed; narrow melee/obstruction ownership fixes are built as
`strafe-build-playtest-20260910-ownership`. Focused checks and 26 temporary-body
cases pass. Corrected red/blue ten-minute matches score 7/6 captures with no
audited distant stall and native saber kills on both teams. Resources pass
30/30; normal MP listen passes on both teams (2/1 captures). The corrected
candidate is installed with archive `20260910T0248073047206Z`; all nine installed
hashes verify. Eight-bot movement maximum remains over budget at 5.3348 ms;
sixteen-bot stress loses a client. Exact cost and restore evidence is in the
delivery record.
This is a provisional human playtest; **K07 remains unqualified**. Full Q3,
M2, independent-reference threshold policy and final timing remain separate
qualification work. Do not rerun completed discovery or start another navigation
repair without a concrete new delivery defect.

### Previous Q3 checkpoint

Q3 (2026-09-09/10) is in progress and committed as `9d8dff86` on local `master`;
see the [Q3 record](STRAFE_MOVEMENT_Q3_EXECUTION_2026-09-09.md). Two complete
12+12 defended collections fail the stall gate with one family (a defender
pressed at 30 units, the runner standing). Ground-navigation repairs (v2/v3)
leave the candidate better than the baseline on idle and walking in every
cell (pooled 6.45 % / 34.33 % versus 7.12 % / 37.65 % on the v2 build) with
three stalls against none. The owner directed pressed-carrier melee
(`bot_carrierMelee`, staff profile, `--carrier-melee` on the candidate arm);
the v4 build is built and unmeasured because ComfyUI was resident. Next: v4
bench, then the Q3 `-c` recollection. Timing remains measured failed (R8);
K07 remains unqualified.

### Previous Q2 checkpoint

Q2 (2026-09-09) is executed and committed as `34049186` on local `master`; see the
[Q2 record](STRAFE_MOVEMENT_Q2_EXECUTION_2026-09-09.md). Independent
matched-resource references exist: a strict `BOT2REFPATH1` loader replaces only
the navmesh answer at supported acquisition, an independent planner proposed
three finite alternatives (frozen navmesh polyline, containment-only string
pull, 512-unit-capped pull) from the retained mode-3 surveys and the Kejim
support inventory, selection on phases 1/4/7 (90 accelerated worlds, zero
reference refusals) froze one polyline per team and leg, and all 30
candidate/reference pairs on phases 0/3/6 pass every existing gate plus the
pair gates (same build and settings, matched pose/yaw/velocity/resources/phase,
Speed deadline, payload loaded, no refusal on either leg). Reference minus
candidate leg time: outbound median −462 ms (−900..+75, reference faster in
22/30), return median −300 ms (−700..+275, faster in 27/30) on 24..26 s legs;
idle no worse in 26/30, walking no worse in 21/30. No ratio threshold is
declared, so this is a reported comparison, not a pass claim. Timing remains
measured failed (R8). K07 remains unqualified; Q3–Q5 follow in order.

### Previous Q1 checkpoint

Q1 (2026-09-09) is executed and committed as `f9bdd662` on local `master`; see the
[Q1 record](STRAFE_MOVEMENT_Q1_EXECUTION_2026-09-09.md). A cheats-only pursuing
body proxy produced native bounded continuation: 8..10 `corner_continue` events per
eight-case collection across six collections, most followed within two seconds
by 51..251 units of displacement and a smaller objective distance while the
body stayed 30..66 units away; aborts on re-obstruction, clear handbacks and three-step exhaustion were
observed; timeout, objective change during an episode and airborne interruption
remain mocked coverage. Under sustained pursuit at running speed
the outcome varies between runs of the same case: 0..2 confinements of up to
13.25 s, 44..56 % travel idle and no capture in the window; the matched 120 u/s
red case once recorded a 9.75 s confinement. The instrumented cost trace captured a
complete fall from a cached-route rejoin at a 1170 u/s takeoff made from about
320 u/s; the route follower now declines such a rejoin when the recorded flight crosses
a gap, a deep drop or a climb that ground momentum cannot make (mocked check
passes; a first momentum-only gate failed 17/26 temporary-body cases and three later variants two more each; all were replaced before the retained regressions). The
R8 `adapter-s4` death remains unclassified; the retained traces show a mid-air
body contact with client 4 as the candidate mechanism. Regressions on the final Q1 revision: resources 30/30 and temporary-body 26/26 pass on the final revision `…e8f1…` (`.codex-research/strafe-q1-regression-20260909-e`), after four rejoin-gate variants that each failed the body suite were retained and replaced. Timing remains measured failed (R8).
K07 remains unqualified; Q2–Q5 follow in order.

### Previous R9 checkpoint

R9 implements preparation during waits, fresh admission checks and bounded
recovery continuation with recent-position memory. The corrected build has four
complete former-stall pressure cases with no distant confinement; all ten
retained corners survive and carrying cases capture. Both blocked-stand cases
cross 64 units 750 ms earlier. Resources 30/30 retain R8 event sequences;
temporary-body regression 26/26 passes. Chained continuation is not observed
natively despite the targeted cases; its coverage is mocked. The earlier fall
remains unexplained after four identical surviving diagnostic trajectories.
The [R9 record](STRAFE_MOVEMENT_R9_EXECUTION_2026-09-09.md) retains the aborted
review-defect collection and all subsequent evidence. K07 remains unqualified.

### Previous R8 checkpoint

R8 continues on local `master`: floor-refusal contact instrumentation and bounded
local-navigation reentry history are implemented. Both four-case native trace
cohorts complete without distant stalls, including the instrumentation-only
control; do not credit absence of stalls as a treatment effect. Native reentry
retains refusal age and selects validated sidesteps immediately. Extracted
production checks cover 2900 compatible gaps and reset boundaries. The
[R8 record](STRAFE_MOVEMENT_R8_EXECUTION_2026-09-08.md) owns new evidence.
All 30 resource cases retain R7 event sequences and pass; all 26 frozen
temporary-body cases pass. These are focused regressions, not a full M2 rerun.
The owner then closed ComfyUI and resumed timing: four candidate eight-bot
samples yield two budget passes, one 5.5224 ms maximum failure and one invalid
sample after a native fall. Sixteen-bot stress retains all clients, p95/max
0.5426/5.2405 ms. Timing is now measured and failed, no longer deferred; the
R8 record preserves all samples and the six-forecast worst-frame attribution.
K07 remains unqualified; the full integrated checkpoint below is still R7.

### Latest complete integrated collection (R7)

R7 is implemented as `1745b4d3` on local `master` and measured on
`strafe-build-r7-waits-a4a18cf49d5e4abe97faf39a578bd1da`. The
[R7 execution record](STRAFE_MOVEMENT_R7_EXECUTION_2026-09-08.md) owns source
bindings, all failures and the next reference-design package. The full chain
finished at 13:39 UTC; no timing arms ran. **K07 remains unqualified.**

| Area | R7 measured status |
|---|---|
| Implementation | Explicit pre-actor seeds, native acknowledgement and duplicate-tape checks; bounded supported preparation/prefix waits at 3 s and final arrival at 250 ms. One native endpoint expiry/release is observed within a declared trace window; three-second expiry remains mocked coverage. Adapter/airborne ownership preserved. |
| Solo and resource regressions | M0 pickup/ownership/time outcomes 12/12 (raw exact-trace false); M1 all 26 starts/208 phase pickups, nominal max 1.098947 and worst phase 1.111421; supported 30/30, lifecycle 18/18, full-route 12/12 paired gates. First-trip gains remain 10.27% red/11.86% blue, 85 verified flight exits and two censored tails. |
| M2 | v2 748/780; re-derived v3 765/780, with the same 12 completed failures as R6 and three invalid startups. Separate four-offset recollection passes every case with matching declarations and native bindings; original campaign/audit remain unchanged and false. |
| Defended comparison | All 24 cases valid and tapes distinct. Candidate idle improves in every cell, but red 1x walking regresses 0.2920 points. Four candidate stalls (24.75 s) and four baseline stalls (33.25 s). Pooled idle 6.8757% vs 7.9369%, walking 33.8856% vs 36.1740%. Baseline has a long native flag stalemate and less travel exposure; retain it. |
| Ablation | All 12 corners-only cases valid; pooled walking 39.7031%, worse than the candidate in every cell; idle 6.6672%, two stalls totaling 12.25 s. |
| Remaining | Defended recovery under body pressure, matched-resource planned-reference infrastructure and final cost qualification. The original isolated supported wait has not reproduced. Timing remains deferred by the recorded owner decision; no installation or map transfer. |

### Previous R6 checkpoint

R6 (September 8) executed the K07 review's packets and qualified one
revision on `.codex-research/strafe-build-r6-freeze-20260908b`
(committed as `4414eddc`: `ai_bot2_localnav.c` continuation policy,
`ai_bot2_route.c` observe-only jump reserve; integrated into local `master`
on September 8). The
[R6 execution record](STRAFE_MOVEMENT_R6_EXECUTION_2026-09-08.md) owns the
evidence; its historical measured status:

| Area | R6 measured status (freeze 2) |
|---|---|
| Solo runtime | 12 valid runs, first-trip medians improve 10.27% red/11.86% blue; every paired gate passes; 85 verified exits, two censored. Identical to K07; phases 1/4/7 are regression. |
| Benchmark retention | M0 12/12 with ownership and ratios (exact-trace false as since 09-05). M1 26/26, nominal max 1.019, worst phase 1.111. |
| M2 | Frozen v2 suite 746/780 (95.64%), every family floor met, overall target met; eight blue_respawn_5 approach impulses remain preempted by the faster controller. Re-derived v3 suite (same protocol, events from the freeze-2 M1 traces) 768/780 (98.46%), every audit check passes. |
| Resources and lifecycle | Adapter-armed supported matrix 30/30; lifecycle 14 interventions, 2 boundary, 2 controls pass. Declared corner matrix: ten poses/resource/body cases, no confinement, every carrying case captures. Matched-resource planned references (Q2, 2026-09-09): 30/30 pairs valid on the Q2 build, reference faster on most legs by 50..900 ms, no ratio threshold declared. |
| Defended continuity | Walking not worse in any cell (K07: three worse); pooled idle 6.68% vs 7.97% baseline, walking 32.28% vs 34.67%. Red 1x travel idle 0.65 points worse (baseline's two identical runs hold 15 s classified goal waits). Two distant stalls vs one baseline: a 5.75 s supported-owner wait and a 5.0 s defender-at-30-units carry. Ablation: corners mode alone walks 37.54%, the adapter offsets it. |
| Native contention | K07's rendezvous evidence stands; not re-collected. |
| Timing | Not collected in R6 by the recorded owner decision; the K07 samples remain pending, not a pass. |
| Delivery | Opt-in research configuration only; installed normal MP baseline unchanged. |

## R1–R5 development history (superseded status wording)

**K01–K03 establish useful offline Kejim route quality. K05 now meets its
own gate on a frozen holdout:** with the explicit command-boundary latency
model, the scoped guard and a runtime-evidence re-baked artifact, twelve
untouched cases (phases 1/4/7) complete 85 of 87 entered windows (two cut off
by the capture end), run the first trip 10.27% faster red and 11.86% faster
blue at the median, with idle and walking better than control in every pair,
no deaths and every capture and pickup the control scored; complete eight-bot
movement measures max 3.889 ms including a six-forecast preparation. K06
dynamic/resource recovery and K07 integrated qualification remain open, and
the qualified configuration is still opt-in (`bot_flightAdapter 1`,
`bot_forecastGuard 1`, the `…-r3-` artifact). **R4 is done (K06):** every
constructed native lifecycle case - 80 unit/s impulses in both flights, a body
on the landing, an operator disable, a Force Speed input, lethal damage while
carrying, and a supported ownership reset - ends in a useful continuation or an
explicit hand-off with no airborne hand-back outside the declared exceptions
and no hidden reset; the 200 unit/s kick is a retained, explained doomed-fall
boundary (red died in the pit 7.6 units below the range). Admission is
deferred-first with tickets; a lost owner is released explicitly; airborne
publication/artifact/gate releases recover; recovery has a reported best
effort. Native same-frame contention was not obtained in nine arms (retained),
so fairness is mocked and instrumented only. See the
[lifecycle protocol](STRAFE_MOVEMENT_LIFECYCLE_NATIVE_2026-09-07.md).
**R1 is done:** the cost fixture now charges every flight preparation to the
same frame and client as dispatch, and a reusable campaign audit reports each
frozen predicate separately. **R2 is done:** an opt-in scoped baseline guard
(`bot_forecastGuard`) compares the whole world exactly after every rollout
instead of hashing it twice, and the local controller's per-step scan is pruned
with bitwise-identical output. With both, complete eight-bot movement measures
p95 0.28 ms and max 3.38 ms (3.30 ms with every ranked candidate and both
prefixes rolled out), inside the 2.5/5 ms budget; the legacy guard on the same
build still peaks at 8.7 ms. The measured windows never reached the baked
four-candidate list, so the theoretical worst case is bounded by the reported
rollout cap rather than measured. R3 then closed K05's runtime speed/idle gate
on the frozen holdout recorded above; R4 closed K06's lifecycle cases; K07
remains open.

The new motion is native-tested through production dispatch in private test
worlds. Normal MP still uses the older installed Runner_v2; nothing has been
promoted. Current native sources match the 1,102-input manifest in
`.codex-research/strafe-build-r4-lifecycle-a5a81bcd5a554bbc89eb423fc4fe950b`
(cost/contention arms); the lifecycle campaign ran on
`strafe-build-r4-lifecycle-65dba03922fd42d6a80304a379cd8d5b`, which differs
only by the cost fixture's two-client world, hold-relative impulse and blue
spawn option. The R3, R2, R1 and review builds are retained and listed in the
evidence map. Earlier K01–K03 evidence belongs to its older build.

| Capability | Verified result | Scope/remaining limit |
|---|---|---|
| Earlier frozen M0–M3 benchmark | All 26 starts; M2 758/780 pickups; eight-bot dispatch p95 0.323 ms/max 0.903 ms | Cached-route benchmark only; current adapter does not inherit it |
| K01 native focus | Six-case lifecycle evidence and corrected checksum audit retained | Native pickup-skip branch still not established |
| K02 offline successors | All 21 schedules + two controls recorded, 123/126 stages complete | Offline development, not runtime transfer |
| K03 fixed offline gate | **Pass:** medians improve 10.74% red/12.71% blue, every time/idle/walking comparison passes | Keep this gate closed on its measured build |
| K05 artifact/control | 9,207-byte payload, 31 windows, 41 located regions; pure loader, per-client construction and production adapter exist | Geometry/ranked candidates still require actual native admission; thin coverage remains |
| Latest runtime solo development | Six adapter runs, two captures each, zero deaths; 79 entries and 53 successful resumed exits | Harm comparison passes; 26 incomplete entries mean original execution gate does not |
| Runtime first-trip quality | Red 57,275→54,600 ms (**4.67%**); blue 57,050→54,000 ms (**5.35%**) | Both miss 10%; idle regresses in five of six pairs, walking improves in all six |
| Eight-bot complete cost (R1, legacy guard) | Adapter arm: complete movement p95 0.727 ms/**max 12.650 ms**; dispatch alone p95 0.713/max 1.007 ms; preparation max 12.360 ms of which forecast rollouts 12.334 ms. Control arm: complete p95 0.742/max 1.523 ms, passes | Eight preparations, each cross-checked against its frame row; only the 5 ms maximum failed. Non-rollout preparation overhead was at most 0.06 ms |
| Eight-bot complete cost (R2, scoped guard + pruned controller) | Adapter arm: complete movement **p95 0.282 ms/max 3.379 ms**, preparation max 3.075 ms, server frame max 4.004 ms, `eight_bot_budget_pass` true. Exhaustive arm (both prefixes, every ranked candidate): max 3.296 ms, passes. Legacy-guard arm on the same build: max 8.718 ms, fails. Control: max 0.673 ms. Rebind on the final tree (`…-v3/`, build `a84f1f87…`): guard max 3.519 ms, exhaustive max 3.571 ms, control max 0.432 ms, all pass | Eight preparations per arm, all charged, one admission per frame, all eight clients alive on all 800 frames. Baseline is 1.94 MB compared per rollout (snapshot 0.13–0.66 ms, compare 0.2–0.7 ms). The observed windows offered at most one candidate with a valid takeoff, so a four-rollout preparation was not reachable; a 256-step rollout costs about 4 ms and `bot_flightRolloutCap` is the declared bound for that case |
| Activity in that cost arm | 800/800 frames with all eight clients alive and dispatched in both fresh arms; 143/800 fully moving | The review's 224 lost-client frames did not recur in two fresh collections; the loss is unexplained, not resolved |
| Preparation scheduling | One admission per frame held in the capture (`prepare_admitted` max 1); prefix selection reduces observed work | Native deferrals remain 0; serialized count is not time budget or fairness proof |
| Campaign outcome audit (R1) | `tools/strafe_flight_native_report.py` on the twelve reviewed cases: 12/12 inputs valid; load, reachability, work-count and harm predicates pass; execution fails (25 refused after entry, 1 recovery refused); 13 entries into thin windows | Reproduces the review's 79/53/26 and 4.67%/5.35% from raw data; accelerated collection carries no timing claim |
| Runtime transfer diagnosis (R3) | On the reviewed captures every refused flight was followed on the same frame by the supported adapter dropping its plan (`interval_contract`), then a 1 s acquire backoff and 0.4–1.1 s to ready; adapter idle exceeded control by 193 frames, of which 280 fell within 3 s after a refusal while its other idle was lower than control's | The forecast root equals the previous frame's post-state; the issued command executes before the first flight command |
| R3 latency model + refusal resume | Rollouts start from this frame's already-issued command (`firstCommand`), policy clocked at this frame; grounded refusals after the first landing try the exit path's receiver resume first | Development cases: 72/78 complete (6 cut off by capture end), zero refusals after entry, idle/walking better in all pairs, 8.0%/9.1% medians |
| R3 oracle at actual roots | `bot_flightExhaustive 2` rolled out every flight id at 96 preparation roots: 537 complete / 91 refused; on windows 0, 1, 21, 25, 26 the listed 1.25× ids refused at phase 5, grounded, 70–97 units lateral off the second landing, while base-speed ids completed at every root | A list deficiency at runtime roots, the review's stated condition for a re-bake |
| R3 re-bake | `tools/strafe_flight_artifact.py --runtime-oracle`: reproduces the measured payload byte-for-byte, re-ranks 13 windows by runtime completion, keeps the anchor rule; only `red:f06:v1` (window 21) has no id completing at every root and is flagged | Development cases: **90/90 complete, 11.70% red / 10.60% blue**, idle/walking better in all pairs |
| **K05 holdout (frozen phases 1/4/7)** | **85/87 complete (2 truncated), 10.27% red / 11.86% blue medians, pairs 7.8–12.2%, idle 3.6–3.8% vs 4.0–4.2%, walking 47–51% vs 58%, 0 deaths, captures/pickups equal, every frozen predicate and the full-route gate pass** | Run once, untouched; a single red pair (p4) is below 10% while its team median passes |
| Eight-bot cost with the re-baked artifact | Complete movement p95 0.314 / **max 3.889 ms**; one preparation ran six forecasts (two prefixes, four candidates) in 3.72 ms; control max 0.402 ms | The maximal two-prefix/four-candidate case R2 could not reach is now measured inside budget |
| **K06 native lifecycle (R4)** | **18/18 inputs valid: 14 intervention cases pass, 2 boundary cases pass on explanation, 2 controls 15/15 complete; no airborne hand-back outside `disabled`/`owner_lost`/death-frame reset/`recover_refused` with geometry; non-lethal cases grounded within 0.8–2.5 s and on a plan within 1.2–6.1 s; damage case reset once on its death frame and replanned 2.4–2.9 s after respawn** | Accelerated solo outcomes; one native best-effort recovery (blue body); publication change mid-flight mocked only; red 200 unit/s kick died in the pit (retained boundary) |
| Contention and fairness (R4) | Serialisation held on every native frame (busiest frame 1 in nine arms incl. sixteen bots); deferred-first tickets mocked (74 scenarios) and instrumented (`waited=`, `queue=`, per-client admissions, `starvation_pass`) | No native same-frame contention obtained: holds, clustered and per-team spawns all changed the timelines (retained); deterministic construction is a K07 stress item |
| Eight-bot cost on the R4 build | Final build: adapter p95 0.369 / max 3.718 ms, control max 0.748 ms, all eight alive on all 800 frames; sixteen bots: p95 0.507 / max 4.612 ms, all alive | First R4 sample max 5.112 ms on the same six-forecast preparation (3.72 ms in R3): per-run variance retained as a failed sample, budget claim rests on the passing samples with that variance stated |
| **K07 frozen revision (R5), passes** | On one fresh build (`strafe-build-k07-freeze-20260907`): full-route runtime gate 12/12 valid, `k05_runtime_pass` (10.27% red / 11.99% blue medians, 87 entered / 85 exits); supported 30-case matrix with the adapter armed passes all 22 gates in every case, faster than the supported-only matrix in 30/30 with lower idle and walking in 30/30; lifecycle regression 14 pass / 2 boundary / 2 control; **first native same-frame contention** via the rendezvous fixture (8 bots: 4 production deferrals, 16 bots: 2; queue 2, waited 50 ms, `starvation_pass`) | Accelerated outcome runs carry no timing; the twelve full-route cases are now a regression, not a fresh holdout; matched-resource references still unbuilt |
| **K07 frozen revision (R5), fails** | Eight-bot complete cost: four samples, max 6.80 (contaminated) / 4.99 / 3.17 / **7.24 ms** (clean); the six-forecast preparation now spans 3.7-6.9 ms across seven measurements with identical work, so the 5 ms maximum is not met robustly (p95 <= 0.34 ms in every sample). Sixteen-bot stress: all alive, p95 0.55 ms, but a **71 ms** frame where the payload's first-use file load inside a live frame stalled 66 ms. Defended continuity 12+12, all valid: candidate idle better in every cell (6.76% vs 8.62% overall), captures 67 vs 52, but walking worse in three cells (+5.6 points red 5x) and four distant stalls (three inherited stand-entry parking, one unexplained 5 s stop) against the baseline's six. Cached-route M2 regression on the same build: 739/780 pickups (frozen reference 758/780), 21 deaths, 12 timeouts, mid-flight impulse family 191/208 | Verdict: K07 not complete. The two cost-maximum samples and the 71 ms load frame are **attributed to ComfyUI host contention (owner's call, 2026-09-08), probably fine, not adapter defects**; the real open items are the stand-entry parking/walking behaviour, the unexplained stop and the M2 shortfall, after which the frozen collection must be repeated on a new freeze, not mixed with this one |

Pre-R3 runtime idle fractions, control→adapter: red p0 4.33→5.68%, p3
4.05→6.01%, p6 3.97→6.45%; blue p0 4.29→8.16%, p3 4.41→5.23%, p6
4.04→3.70%. These use the unchanged solo first-capture metric. They cannot be
described as the offline gate's idle pass. Repeatedly tuning on the same twelve
cases makes them development regressions, not a fresh final holdout.

## September 7 review corrections (historical R1–R5 work order)

1. K05 is incomplete even though a narrower harm predicate passes. Preserve the
   original entered-window completion and work-count predicates separately.
2. Include loading/binding/preparation/selection and execution/recovery in total
   movement cost. Dispatch-only timing omits preparation in ClientThink_real;
   summed forecast timing also omits other preparation overhead. The budget
   remains p95<=2.5 ms/max<=5 ms, not the 25 ms game tick.
3. Missing dispatches in the latest arm coincide exactly with dead-client
   workload loss. The earlier explanation that flight ownership bypasses the
   counter is contradicted by the code and 800-row CSV.
4. Endpoint-only guard batching is not proven safe. Hashing detects arbitrary
   world mutation; it does not restore it. A subsequent rollout could consume
   or undo an earlier mutation. Preserve per-rollout verification and immediate
   refusal while investigating faster exact comparison.
5. Diagnostic guard-cost attribution motivates optimization; estimates of6.8/
   13 ms do not establish performance and would still miss the 5 ms maximum.

## Next executable direction

The [runtime review](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md) specifies
owners, inputs, negative tests and completion criteria:

- **R1 (done):** `bot2_movecost.c` charges the complete preparation boundary,
  buffers adapter prints outside the recorded interval and declares its
  accounting; `tools/strafe_cost_report.py` refuses dispatch-only captures and
  cross-checks every reported preparation; `tools/strafe_flight_native_report.py`
  audits a frozen campaign predicate by predicate. Negative tests cover the
  review's misreported claims. The corrected metric fails the 5 ms maximum.
- **R2 (done):** `Bot2_ForecastGuardBegin/End` in `bot2_forecast.c` take one
  immutable BSS baseline per preparation and compare exactly after every
  rollout, refusing the rollout and every later one on contamination; legacy
  callers keep per-rollout hashing untouched. `bot2_movecontrol.c` prunes the
  fast scan to each key's admissible arc with bitwise-identical commands.
  `bot_flightExhaustive` measures maximal reachable work; `bot_flightRolloutCap`
  bounds it and reports skipped candidates. Harnesses:
  `tools/strafe_forecast_guard_check.py` (17 scenarios),
  `tools/strafe_movecontrol_check.py` (200,000 equivalence cases) and the
  adapter check (52 scenarios).
- **R3 (done):** every rollout now starts from the frame's already-issued
  command with the policy clocked at that frame; the boundary skew is a
  declared cvar (`bot_flightBoundarySkew`, default unchanged at 2, no
  experiment needed because run-up refusals vanished); grounded refusals
  after the first landing try `Bot2_ReceiverResume` before handing back;
  refusals report phase and landing geometry. The oracle diagnostic showed a
  list deficiency at runtime roots and the artifact was re-baked from that
  evidence with the anchor rule intact. Holdout passes the full gate.
- **R4/K06 (done):** `tools/strafe_flight_lifecycle_campaign.py` freezes and
  runs one declared intervention per solo run (`bot2_flightcase.c`, keyed to
  `Bot2_FlightAdapterProbe`); `strafe_flight_lifecycle_report.py` audits every
  declared predicate. Adapter contracts: deferred-first tickets, `owner_lost`,
  airborne handoff to recovery with `cause=`, best-effort recovery and
  `recover_refused` geometry, `Bot2_ClearState` reset. Contention constructions
  (`bot_moveCostHold`, `bot_moveCostSameSpawn[Blue]`, two- and sixteen-client
  worlds) produced no native deferral and are retained as such.
- **R5/K07 (measured, not complete):** one integrated revision frozen on
  `strafe-build-k07-freeze-20260907` and measured end to end. Passing: the
  full-route runtime gate, the 30-case supported matrix with the adapter
  armed, the lifecycle regression, and native same-frame contention through
  the new `bot_flightRendezvous` fixture (which withholds the first admission
  request instead of moving a client). Failing: the 5 ms cost maximum on two
  of four eight-bot samples (forecast rollout time of the same six-forecast
  preparation varies 3.7-6.9 ms), a 71 ms sixteen-bot frame from the payload's
  first-use file load, and the defended continuity comparison (walking worse
  in three cells; distant stalls in both arms, one unexplained). The 780-case
  cached-route M2 regression also fails on this build (739/780 pickups against
  the frozen 758/780, twelve timeouts), so the retained M0–M3 result stays on
  its own build. Protocol:
  [K07 qualification](STRAFE_MOVEMENT_K07_QUALIFICATION_2026-09-07.md).
- **R6 (next):** the timing failures are attributed to ComfyUI host
  contention and are not R6 work (owner's call; the host stays busy, so they
  remain "probably fine" rather than measured passes). R6 is the behaviour:
  decide the cached-route follower's stand-entry parking and the shared
  executor's walking against the continuity rules rather than around them,
  explain or fix the one unexplained stop, and establish whether the M2
  shortfall is dispatch or variance; then a new freeze and the complete K07
  collection again (do not mix revisions). Binding the payload at map load is
  a K08 tidy-up. Then K08–K10: normal MP
  delivery, Dantooine transfer, broader offense and later aiming.

K04's old discovery experiment is not restarted. R3 closed the runtime
transfer blocker on the holdout and R4 the lifecycle cases; K07 is the next
measured item. Do not rerun all historical suites just to resume.

## Evidence map

Paths are repository-relative; use exact paths for ignored research outputs.

| Purpose | Artifact |
|---|---|
| **Tap repair: declaration, three repetitions, scored comparison** | `.codex-research/pkgD-tapfix-20260912/`: `manifest.json` (frozen 2026-09-12T02:46:35Z), `run_cases.py`, `collect.sh`, `collect-r3.sh`, `score.sh`, `regressions.sh`, `tap_losses.py`, `summarize.py`, `verify_hashes.py`, `candidate-r{1,2,3}/` (60 worlds) with `-audit/-losses/-windows/-falls` outputs, the step-4 arm re-read as `step4-candidate-r{1,2,3}-*`, `report-tapfix.json`, `preflight/`, and two retained failures: `candidate-r3-sourcedrift/` (sixteen worlds refused after a mid-collection comment edit) and `tapfix2-candidate-r{1,2,3}/` (three complete repetitions superseded when that comment was corrected). Build `strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`; regressions `.codex-research/pkgD-tapfix-regression/`; checks `.codex-research/pkgD-tap-checks/` including the `falsify-old-rule` build that fails the new assertions |
| **Branch step 3 (BR-B) manifest, arms and report** | `.codex-research/pkgD-step3-20260911/`: `manifest-v2.json` (frozen before collection), `cases.json` (12 seeded from recorded grounded states), `run_cases.py`, `connector_stations.py` + `connector-stations.json` (the station fit), `author-brb/` (assets and `author.json`), `assets/` (30-route catalog + 4-declaration sidecar), `control-r{1,2,3}/` and `candidate-r{1,2,3}/` (72 worlds) with `*-audit.json`, `report.json`, and `bra-recheck/` (BR-A's own four cases on this build). Builds `strafe-build-bra4-31a4ef26…` (control) and `strafe-build-brb3-680c73cc…` (candidate). Checks `.codex-research/pkgD-step3-checks/`, `pkgD-step3-catalog-check3/`, `pkgD-step3-fr-check3/`; regressions `.codex-research/pkgD-step3-regression/` |
| **K07 (R5) freeze and arms** | Build `.codex-research/strafe-build-k07-freeze-20260907` (1,102 sources); cost `strafe-k07-cost-20260907/{adapter-s1..s4,control,sixteen,adapter-rendezvous-8,sixteen-rendezvous-8}/` with `*-audit/summary.json`; lifecycle `strafe-k07-lifecycle-20260907/` + `-report/`; supported `strafe-k07-supported-adapter-20260907/` + `-report/`; full-route `strafe-k07-fullroute-20260907-v2/` + `-report/` (`-v1/` retained, three runner processes failed to start); continuity `strafe-k07-continuity-{candidate,baseline}-20260907/` (`aggregate.json`, `opposition-audit.json`); candidate play smoke `strafe-k07-play-candidate-smoke-20260907/`; M2 `strafe-k07-m2-evaluation-20260907/` + `-audit/` (`-aborted/` retained); mocked checks `strafe-{flight-adapter,forecast-guard,movecontrol,flight}-check-k07-20260907-v1/` |
| **R4 lifecycle campaign and audit** | `.codex-research/strafe-flight-lifecycle-r4-20260907-v3/` (frozen plan, 18 cases) and `…-v3-report/{report,summary}.json`; `…-v2/` with `-v2-report-v1/` (frozen under the literal 3 s reaction gate, two cases failed it) and `…-r4-20260907/` (earlier build, blocker over the gap, first audit's defect) retained; smoke `strafe-flight-lifecycle-smoke-20260907/` |
| R4 cost and contention arms | `.codex-research/strafe-flight-cost-r4-20260907/{baseline,baseline-b7,control-b7,sixteen,contention-hold,pair-baseline*,pair-hold-*}/` with `*-audit/summary.json`; clustered-start smokes `strafe-cost-contention-smoke-20260907/` |
| R4 mocked and pure checks | `.codex-research/strafe-flight-adapter-check-r4-20260907-v5/` (74 scenarios; `-v1-failed`, `-v4-linkfail` retained), `strafe-forecast-guard-check-r4-20260907/`, `strafe-movecontrol-check-r4-20260907/`, `strafe-flight-check-r4-20260907/` |
| R4 builds | `.codex-research/strafe-build-r4-lifecycle-a5a81bcd…` (current: cost arms, per-team spawn), `…-65dba039…` (lifecycle campaigns v2/v3, eight-bot `baseline`), `…-7c6c754a…` (two-client world, pair arms), `…-0b8e6bce…` (campaign v1), `…-89436f6c…` (smokes), `…-fafd3cbb…` and `…-144bf517…` (intermediate, the latter identical in source to `…-7c6c754a…`); each with its build log and source manifest |
| Independent current review | `.codex-research/strafe-direction-review-20260907-v1/review.json` and `review.py`; hashed inputs, raw CSV/log recomputation |
| Latest runtime cases and frozen predicates | `.codex-research/strafe-flight-native-audit-20260907/plan.json`, twelve case directories with CSV, report and server log |
| K05 holdout campaign and audit | `.codex-research/strafe-flight-native-r3-holdout-20260907/` (frozen plan, phases 1/4/7, `--forecast-guard`) and `…-holdout-20260907-report/summary.json` |
| R3 development campaigns | `.codex-research/strafe-flight-native-r3-20260907/` (latency model), `…-r3-oracle-20260907/` (`--flight-exhaustive 2`), `…-r3-rebake-20260907/` (re-baked artifact), each with a `-report/` audit |
| Re-baked runtime artifact | `.codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/{flight.txt,artifact.json}` with `runtime_rerank` bindings to the oracle campaign |
| Eight-bot cost with the re-baked artifact | `.codex-research/strafe-flight-cost-r3-20260907/{adapter-guard,control}/` and `*-audit/` |
| Latest eight-bot evidence (R2 guard comparison) | `.codex-research/strafe-flight-cost-r2-20260907-v2/{adapter-guard,adapter-guard-exhaustive,adapter-legacy,control}/` with `*-audit/summary.json`, build `strafe-build-r2-guard-b72e8685…`; `…-r2-20260907/` (guard only, before the controller pruning) and `…-r2-20260907-v3/` (final-tree rebind) retained |
| R1 eight-bot evidence (legacy guard, complete accounting) | `.codex-research/strafe-flight-cost-r1-20260907-v2/{adapter,control,adapter-audit,control-audit}/`; the earlier `…-r1-20260907/` collection on build `…-d7eb8507…` is retained and differs only by a header comment |
| Guard and controller harnesses | `.codex-research/strafe-forecast-guard-check-20260907/result.json`, `.codex-research/strafe-movecontrol-check-r2-20260907/result.json`, `.codex-research/strafe-flight-adapter-check-r2-20260907-v3/result.json` |
| Campaign outcome audit | `.codex-research/strafe-flight-native-report-20260907-v1/summary.json` over the twelve reviewed cases |
| R3 native build | `.codex-research/strafe-build-r3-oracle-7851ea4040a949fe90b10b3dd3465464/` (1,100 sources); R2 build `strafe-build-r2-guard-a84f1f87…`, R1 build `strafe-build-r1-accounting-65095a07…` and review build `strafe-build-flight-audit-20260907/` retained |
| Review-time eight-bot evidence (dispatch-only) | `.codex-research/strafe-flight-cost-audit-20260907/{adapter,control,adapter-audit,control-audit}/` |
| Runtime artifact (offline bake, superseded for runtime by the re-bake above) | `.codex-research/strafe-flight-artifact-4a724cbaf7da4b9b92f790a672313738/{flight.txt,artifact.json}` |
| R3 mocked adapter check | `.codex-research/strafe-flight-adapter-check-r3-20260907-v3/result.json` (61 scenarios: accounting hooks, scoped guard, issued-command contract, skew override, refusal resume, oracle diagnostic) |
| K03 offline phase gate | `.codex-research/strafe-chain2-phasegate-6df2e0f20b5748baaa692bc683cdd1a8/speed-gate.json` |
| K02 development/selection | `.codex-research/strafe-chain2-development-b754d07ed53f4033902165e3d3c6f2ca/` |
| K01 native focus | `.codex-research/strafe-chain2-focus-v1-20260907/campaign/chain-report-v4.json`; failedv1 retained |
| Expanded88 seed evidence | `.codex-research/strafe-window-seed-v2-20260907/seed-report-v1.json` |
| Diagnostic guard profile | `.codex-research/strafe-flight-diag-20260907/`; separate reverted diagnostic build, not current timing qualification |

The [native experiment log](STRAFE_MOVEMENT_FLIGHT_NATIVE_2026-09-07.md) retains
iteration detail; its old cost/guard conclusions are corrected by this review.
The [artifact design](STRAFE_MOVEMENT_FLIGHT_ARTIFACT_2026-09-07.md) retains
serialization and integration constraints. Their historical next steps do not
override the current review.

Installed baseline remains `strafe-build-stall-fix-lateral-20260906`; see
[play instructions](KEJIM_RUNNER_PLAY.md). Frozen cached-route controller remains
a reference. Full defended Kejim, map transfer and offense completion are not
claimed.
