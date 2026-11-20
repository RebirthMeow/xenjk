# BR-B: the post-group-1 connector, and what the later refusals cost (2026-09-11)

Step 3 of the owner-approved
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md):
cover the post-group-1 depleted landing state, then takeoffs 50 and 57 and the
measured momentum refusals, and record the remaining uncovered states.

**Nothing is installed.** The installed build remains
`strafe-build-reviewready-20260910-b` (`525f1b50`). Two of the three obligations
in step 3 are met and the third is not: takeoffs 50 and 57 are now *measured*
with dedicated seeds, and they are still *uncovered*, for a reason this record
states rather than works around. K07 remains open.

## What closed and what did not

| Coverage row | Status |
|---|---|
| Pickup overshoot and early run-up (F1) | Closed by BR-A, [recorded 2026-09-11](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md) |
| **Group 1 flown, group 2 at takeoff 29 unaffordable (F2)** | **Closed by BR-B**: entered, run and exited at lane 44 in both directions, at 12 Force and at zero Force with no Speed |
| **Arrival at the 43/44 exit (F3)** | **Closed**: the rejoin now admits against the next *executable* maneuver. The 12-Force runner that completed BR-A and was refused the lane twice in both directions now rejoins it |
| Later refusals at takeoffs 50 and 57 (F4) | **Open, and now measured.** Seeded dedicated cases, ten refusals in 36 worlds, eight of them with a sample below 200 u/s in the following second. No line is authored, because no runner has ever stood on that ground |
| Each measured momentum-refusal family (F5) | Partly: group 1 (BR-A) and group 2 (BR-B) are covered. Groups 3, 4, 5 and 7 are not |

## The connector exists, and it is measured

The [corridor record](STRAFE_MOVEMENT_PACKAGE_D_CORRIDOR_2026-09-11.md) left this
row open because the depleted corridor never comes within 350 units of the
group-1 landing platform. It is no evidence for this line. The episodes that
*flew* group 1 and then stood on that platform are, and there are 24 of them
across the three retained control repetitions.

`.codex-research/pkgD-step3-20260911/connector_stations.py` reads their grounded
tracks. What it finds is a single flat floor at **z = 1808.1** running without a
break from the landing platform to the rejoin, walked at a median 425–430 u/s:

| y | 3450 | 3300 | 3150 | 3000 | 2850 | 2700 | 2600 | 2450 | 2300 | 2150 |
|---|---|---|---|---|---|---|---|---|---|---|
| x | 2666.0 | 2501.8 | 2321.8 | 2165.1 | 2063.5 | 1898.9 | 1659.2 | 1552.3 | 1504.8 | 1457.4 |
| tracks | 8 | 24 | 24 | 24 | 24 | 24 | 24 | 12 | 12 | 10 |

The estimator is **not** the 50-unit band median BR-A's stations used. A band
median takes whatever grounded samples fall inside the band, so the set of
episodes behind each station changes from band to band; on this connector that
alone swung the authored heading by 20 degrees between neighbouring stations and
left the tap rule one usable stretch out of fourteen. Interpolating each
episode's own track where it crosses each y and taking the median across
episodes estimates every station from the same population.

BR-A's table is deliberately not re-estimated — it is the line that was
prepared, flown and rejoined from natively, and re-authoring it would discard
that evidence. Where the two overlap they are **compared** instead, and that
comparison is the best cross-validation in this record: two independent
populations, measured by two different estimators, agree to **0.8 units at
y = 2500**, 1.3 at 2550 and under 5 units from 2450 down to 2250. That is where
BR-B merges, and below it BR-B is BR-A's station list verbatim.

## BR-B as authored

25 samples, red route 28 and blue route 29, the exact half-turn about
(1312, 2080). Segments run 52 to 135 units: the author resamples at a 96-unit
target but keeps every station as a knot, and the stations here are 50 units of
y apart.

- **Entry** at (2666.0, 3450, 1808.1), 32 units from where a runner that flew
  group 1 actually lands. The 640-unit entry arc covers samples **0..8** of 25,
  and the refusal at takeoff 29 stands 27 units from sample 5, 383 units along —
  well inside it. The whole branch is 1748 units.
- **Connector**, 18 samples, the diagonal above.
- **Shared tail**, 6 samples, BR-A's own stations from y = 2500.
- **Rejoin** at lane sample 44, the same position, envelope and gate BR-A
  published. Nothing in the envelope changed.

Every authored sample lies within **30 units** of a grounded measured position,
worst case 30.0, dz within 0.1 — a better margin than BR-A, whose interpolated
samples reach 136 units. Native preparation agrees in both directions:
`prepared route=28 samples=25 usable=1 first_unsupported=-1`.

Catalog capacity: **30 of 32 routes**. Two slots remain, which is one branch pair.

### One tap, and why

BR-A carries six taps; BR-B carries one, at sample 8. The tap rule refuses a tap
whose ballistic flight would leave the guide by more than 12 degrees of heading —
a rule written on 2026-09-11 after a tap into a turn flew over an edge and fell
500 units in both directions. The connector curves continuously, and the author
now reports the number that decided each candidate:

| sample | 5 | 6 | 7 | **8** | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heading change over the tap's flight | 13.8 | 16.8 | 14.2 | **8.3** | 9.8 | 23.8 | 33.5 | 32.4 | 22.2 | 34.2 | 49.9 | 51.8 | 36.9 |

Samples 10 upward are the 70-degree turn the ground route takes around the pit,
correctly refused. Samples 5–7 miss by 2–5 degrees. The bound was not moved to
admit them: it is a safety bound, and moving it would also re-author BR-A and
throw away BR-A's native evidence. What one tap costs is measured below — the
connector's speed is unchanged, because the fallback already ran it at the
ground cap. Taps are not where this branch's value is.

## What was built in the runtime

Two corrections, both found by running the branch rather than by reading it.

**1. A lane's branches are chosen by position, not by table order.**
`FastReturnDeclFor` returned the first declaration whose window contained the
runner's lane index. With one branch per lane that is the same thing; with two
it is not. A runner standing on the group-1 landing platform is inside the
connector's window and far outside the corridor's, and a first-match rule would
have refused it every frame while the branch it could take sat one line further
down the sidecar. `FastReturnDeclsFor` now returns every candidate and
`FastReturnTryBranch` scans them all against one shared 128-unit nearest, so the
branch a runner enters is the one it is actually standing on. A branch that is
not prepared yet, or is unsupported, no longer costs the branch beside it its
entry.

**2. The rejoin admits against the next *executable* maneuver.**
This is the BR-A record's own open item, and BR-B reproduced it on its first
native run. `FastReturnGroupAt` uses the kernel's takeoff window, which reaches
three samples *back*: at the rejoin at lane 44 it names the launch at 43. That
window is right for a lane join that may still owe the launch behind it, and
wrong for a branch rejoin, which never does — the gate has already required
ground contact, a level vertical speed and an unlatched jump, and the rejoin at
44 exists precisely because the gap the launch at 43 crosses has been crossed on
foot. A launch behind the rejoin is now not asked about. Nothing further ahead is
asked about either: three collected arms say a later launch must not veto an
admission, and the lane's own two-sample lookahead judges the next one when the
runner reaches it.

The effect is direct. On the retained BR-A build a 12-Force runner completed the
whole branch and was refused the lane by name (`takeoff_receiver_reserve`, which
wants 20 Force), measured twice in both directions. On the same four cases on
this build:

| Case | BR-A build (retained) | This build |
|---|---|---|
| R2-red, 12 Force, entry at index 7 | abandon `takeoff_receiver_reserve` | **exit admitted at lane 44**, 425.5 u/s |
| R2-blue, 12 Force, entry at index 7 | abandon `takeoff_receiver_reserve` | **exit admitted at lane 44**, 428.8 u/s |
| R4-red, 0 Force, entry at index 9 | release at branch sample 15, `takeoff_flight_resources` | unchanged: same sample, same reason |
| R4-blue, 0 Force | entry, no exit | entry, no exit |

Two captures per world, no deaths, minimum health 90 in all four.

## Native evidence

Twelve seeded cases in both directions, three complete repetitions per arm, 72
isolated worlds. Every case is seeded from a **recorded grounded state** of the
retained control worlds — the world, level time and distance from the lane
sample it stands for are in the case file, and blue is the exact half-turn.
The manifest, its correction and the audit rules were frozen before collection:
`.codex-research/pkgD-step3-20260911/manifest-v2.json`.

- **control**: the BR-A build `strafe-build-bra4-31a4ef26…` and its BR-A-only
  sidecar, collected *before* any source or asset change, because the smoke
  runner refuses a build whose sources no longer match the checkout and a
  control collected afterwards would not be a control.
- **candidate**: build `strafe-build-brb3-680c73cc…` and the four-declaration
  bundle.

Scored by `tools/strafe_branch_report.py`, which refuses a claim inside the
control's own repetition spread:

| Measure | Control (per repetition) | Candidate | Verdict |
|---|---|---|---|
| branch entries | 15, 14, 14 | 24, 23, 20 | **reportable** |
| branch exits admitted | 14, 12, 14 | 21, 23, 19 | **reportable** |
| branch frames | 4579, 4044, 4293 | 7593, 5996, 6204 | **reportable** |
| admissions | 171, 142, 158 | 202, 193, 163 | **reportable** |
| all-reason churn within 300 ms | 2, 2, 2 | 2, 2, 2 | unchanged |
| resource churn | 2, 2, 2 | 2, 2, 2 | unchanged |
| stationary samples | 40, 57, 33 | 52, 34, 32 | inconclusive |
| zero-input samples | 71, 102, 55 | 74, 70, 57 | inconclusive |
| lane frames | 8642, 7171, 7957 | 10386, 10156, 8448 | inconclusive: unstable aggregate |
| supported frames | 7646, 7127, 7516 | 6362, 6568, 6199 | inconclusive: unstable aggregate |
| worlds captured | 12, 12, 12 | 12, 12, 12 | unchanged |

36 worlds per arm, **0 deaths and minimum health 90 in both**. Captures 27/25/26
against 31/28/30 — reported, not claimed: the report tool scores worlds
captured, which is 12 of 12 in every repetition of both arms.

Branch outcomes over every episode of every world, counted from the records
themselves rather than per case row: control 43 entries and 40 admitted exits;
candidate **76 entries and 72 admitted exits**. **No abandonment in either arm**,
and two mid-branch releases in the candidate, both `ground_exit_floor` at BR-B
sample 17 in the shared turn, both in the zero-Force case.

### The windows the branch owns

The F2 cases only, per repetition, in route-5 coordinates with blue half-turned.
The last row is the internal control: it is the part of the return no branch
reaches.

| Window | Measure | Control | Candidate |
|---|---|---|---|
| connector, y 2800..3400 | samples below 300 u/s | 33, 28, 28 | **14, 14, 14** |
| | samples below 200 u/s | 7, 4, 3 | 4, 4, 4 |
| | frames owned by the lane | 52 %, 50 %, 45 % | **76 %, 75 %, 75 %** |
| shared corridor, y 2150..2800 | samples below 200 u/s | 14, 12, 12 | **0, 6, 0** |
| | samples below 300 u/s | 44, 42, 42 | **4, 21, 3** |
| | frames owned by the lane | 50 %, 48 %, 46 % | **99 %, 86 %, 99 %** |
| **below the rejoin, y -400..2150** | samples below 300 u/s | 79, 78, 69 | 68, 69, 72 |
| | samples below 200 u/s | 30, 30, 28 | 25, 26, 29 |
| | frames owned by the lane | 31 %, 33 %, 31 % | 35 %, 34 %, 34 % |

Every row above separates with no overlap between the arms across three
repetitions, with one exception: the connector's own sub-200 count, 7/4/3
against 4/4/4, which is inside the control's spread and is reported as
inconclusive. The window below the rejoin, where nothing was built, does not
separate on any row. The median speed in the connector is 425 in both arms — the
ground cap with Force Speed — exactly as the corridor record predicted and
exactly as BR-A's corridor behaved. **This branch does not make the connector
faster. It stops the runner losing it**: the lane keeps the runner through the
connector and the corridor instead of handing it to the generic executor
halfway, and the samples that fall below 300 u/s halve on the connector
(33/28/28 to 14/14/14) and fall further on the shared corridor (42..44 to
3..21). The slowest single
sample in the connector barely moves (49 -> 53 u/s): this is about how often
the runner is slow, not about how slow it can get.

The refusal itself is unchanged, and that is the point: group 2 is refused 18
times in the F2 worlds of *both* arms, by the same names. What changed is what
follows the refusal.

### Both directions, at both ends of the resource range

| Case | Entry | Result |
|---|---|---|
| F2a red/blue, 12 Force, Speed on, entering at 835 u/s | BR-B sample 7 / 8, 427.9 and 427.7 u/s | exit admitted at lane 44, 429.2 and 425.6 u/s |
| F2b red/blue, **0 Force, no Speed** | BR-B sample 6, **250.7 and 250.0 u/s** | exit admitted at lane 44, 305.7 and 311.3 u/s |
| F2c red, 12 Force, entering at 316 u/s | BR-B sample 7, 431.4 u/s | exit admitted at lane 44, 425.5 u/s |
| F4a/F4b/F4c red/blue, seeded at the rejoin | past BR-B's window; BR-A only | exit admitted |

A fully depleted runner — zero Force, no Speed, at the 250 u/s ground cap —
enters the connector, runs it and rejoins the lane. That is the case the whole
package exists for.

## Takeoffs 50 and 57: measured, and still uncovered

The control manifest of step 1 recorded these as "observed only as refusals
inside these worlds; takeoffs 50 and 57 have no dedicated seed yet". They have
one now: F4a/F4b/F4c seed a recorded grounded state at the rejoin with 0, 18 and
26 Force, and run forward through both launches.

| Group | Refusals in 36 control worlds | Reasons | Following second |
|---|---|---|---|
| 5 (takeoff 50) | **0** | — | — |
| 7 (takeoff 57) | **10** | `takeoff_receiver_reserve` 4 at 18 Force, `takeoff_reserve` 6 at 6–7 Force | median minimum **89.5 u/s**; 8 of 10 drop below 200; one reaches **7.8 u/s**; one stationary sample |

Takeoff 50 was refused 5 times in the 60 worlds of the earlier control arms, from
two distinct cases at 10 and 17 Force, and never in this one. Takeoff 57 is the
worse family by a wide margin, and it is worse than the two families that now
have branches: group 1 refuses 43 times with a median minimum of 403.8 and never
drops below 200; group 2 refuses 18 times with a median minimum of 201.1.

**What happens after the refusal.** Traced in F4a-blue, in route-5 coordinates:
the runner is refused at 1043 u/s while airborne, released to the generic
executor, lands at 441 u/s and is braked to **8 u/s within one 250 ms sample**
at about (585, 452), two samples past the takeoff and 38 units from lane sample
59. It then hops away at 433. The refusal does not cost the flight; the hand-back
costs the momentum, at a vertex whose short navmesh legs refuse a pass speed the
same way the y = 4500 descent choke does.

**Why no branch is authored for it.** A branch is refused outright unless every
sample stands on prepared floor, and a line is not authored unless its samples
sit near ground that play has actually stood on. Between lane sample 57 and the
stand approach the second rule cannot be satisfied at all:

| y band | 850 | 800 | 750 | 700 | 650 | 600 | 550 | 500 | 450 | 400 | 350 | 300 | 250 | 200 | 150 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| grounded samples | 0 | 0 | 0 | 0 | 0 | 1 | 23 | 7 | 14 | 10 | 0 | 0 | 0 | 0 | 16 |

Across 187 carried episodes in both arms, **no runner has ever stood** between
y = 650 and y = 850, or between y = 200 and y = 350, and exactly one sample
stands in the band at y = 600. Every runner is airborne
there, including the zero-Force, no-Speed case: the fallback hops that stretch
whatever its resources. An authored line through those bands would be a line
whose samples sit near positions play reached *while airborne* — which is
precisely the mistake that dropped an authored branch 1776 units into a pit on
2026-09-11, in both directions. The floor may well be there; this evidence
cannot say so, and nearness to an airborne position is not standable floor.

Two things would close it, and neither is this step's:

1. a floor probe that reports the native preparation's own traces over a
   candidate line without executing it — the preparation already traces every
   sample and refuses the branch, so the authority exists; it is the reporting
   that does not;
2. a seeded arm that produces a grounded traverse of those bands, if one can be
   produced at all — three resource levels and both teams did not.

## Checks and regressions

| Check | Result |
|---|---|
| `strafe_catalog_check` | PASS — adds **two branches of one lane in either order**, and that they still cannot name the same branch twice |
| `strafe_fast_return_check` | PASS — adds **position-ordered selection among two branches of one lane** (near/far, either table order, past one window and inside the other, past both, past its own rejoin, and an unsupported second branch not costing the first its entry) and the **next-executable-maneuver rule** at the rejoin |
| `strafe_intermediate_tail_check` | PASS |
| `strafe_route_resume_check`, `strafe_supported_wait_check`, `strafe_corner_track_check`, `strafe_hop_arrival_check`, `strafe_flightplan_check` | PASS |
| Python suites | 409 tests, all passing |
| 26 temporary-body cases (frozen M2, evaluation, accelerated) | **26 / 26 pass, 0 failures** |
| 30 resource cases (accelerated, supported corners 2, staged flight adapter) | **30 / 30 complete, none refused** |

Both regression suites match their last recorded status. `strafe_branch_author.py`
still reproduces BR-A's two assets **byte for byte** after the refactor that
added BR-B, which is what keeps BR-A's native evidence attached to the asset it
was collected on.

Evidence under `.codex-research/pkgD-step3-20260911` (manifest, cases, driver,
station fit, author output, assets, 72 worlds, three audits per arm, report),
`.codex-research/pkgD-step3-checks`, `pkgD-step3-catalog-check3`,
`pkgD-step3-fr-check3` and `.codex-research/pkgD-step3-regression`. Every
`codemp/` hash recorded in those `result.json` files matches the working tree,
so each pass is bound to the sources this record describes.

## What is still open

- **Takeoffs 50 and 57 and every other momentum-refusal family are uncovered**,
  for the reason above. Takeoff 57 is now the worst measured refusal family in
  the return.
- **A zero-Force runner can lose a branch at its own tap.** `FastReturnTap`
  requires the runner within 24 units of its guide; outside that the launch is
  judged as a full flight, which needs 2500 ms of Speed, and a runner with none
  is released by name (`takeoff_flight_resources`). It does not occur in the
  final arm's 76 entries at all, and it occurs in BR-A's own zero-Force case on
  this build and on the retained BR-A build alike — same branch sample 15, same
  reason — so it is neither new here nor rare enough to ignore. It is the
  failure mode that costs a depleted runner its branch.
- **Two `ground_exit_floor` releases at BR-B sample 17**, in the shared turn,
  in the zero-Force case, out of 76 entries. The gate refuses to follow a guide
  over ground it cannot trace ahead of the runner, which is exactly what it is
  for; whether the guide should be where it is there is unexamined.
- **BR-B carries one tap** because the connector curves. Whether a lateral-offset
  bound would serve the tap rule better than a heading bound is unmeasured, and
  changing it re-authors BR-A.
- Catalog capacity is **30 of 32**. One more branch pair fits; the next one after
  that does not.
- The blue-only choke at mirrored y = 4300 is still unexplained and its trace
  still unread.
- **No speed or return-time claim.** Every measure that separated is about
  ownership and about samples below a floor, in windows the branch owns. The
  integrated repeated campaign, the affected regressions at normal speed and the
  defended checks are step 4.
- Nothing is installed, and K07 is not qualified.
