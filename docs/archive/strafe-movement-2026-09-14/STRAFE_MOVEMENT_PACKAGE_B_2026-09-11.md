# Resource-aware continuation in the fast-return owner (2026-09-11)

Package B of the approved depleted-return plan, parts B1, B2, B4 and B5.
**Nothing is installed.** The installed build remains
`strafe-build-reviewready-20260910-b` (`525f1b50`). B3 and B6 have not started
because both switch to branch lanes that Package D has not authored yet.
K07 remains open.

Scope: this record establishes that the lane no longer joins a launch it
cannot afford, and measures what that cost and did not buy. It does not
establish depleted-Force competence, which still needs Package D.

## What changed in the sources

| Part | Change |
|---|---|
| B1 | `routeGroup_t` and `BuildReturnGroups` in `ai_bot2_route.c`: every launch of a lane classified once from the route's own geometry, with `takeoff`, `landing`, `dist`, `dz`, `gap`, `tapChain` and `forceClass`. The receiver is measured **takeoff to landing**, never from the live origin |
| B2 | `FastReturnTakeoffAdmission` takes a prepared group and explicit resources; `FastReturnContinuation` walks the ladder with no guide, so the admission runs in the join scan and on a bounded two-sample lookahead as well as at the commitment |
| B4 | `fastReturn_t` remembers the last resource refusal (group, reason, Force, Speed state); a repeat at unchanged resources backs the retry off 500 -> 1000 -> 2000 ms. Non-resource refusals forget the ladder |
| B5 | Owner-level momentum tap behind `bot_fastReturnMomentumTap`: on a checked straight, heading within 15 degrees of the guide tangent and no up-cue within four samples, speed above the ground cap rides airborne. At most once per landing; never arms the missed-jump refusal |

`FastReturnProjectedResources` is the load-bearing piece and was not in the
plan. A launch is judged against the resources it will meet, including the
Force Speed the lane is about to buy (grounded, inactive, Force >= 60 ->
Force - 50, Speed 20000 ms; costs from `bg_pmove.c` and the durations from
`WP_ForcePowerStart`). Without it the pre-join admission is nonsense: see the
first arm below.

## Evidence index

| What | Where |
|---|---|
| Control arm (Package C sources, build `131d3e82…`) | `.codex-research/depleted-return-20260910-d` |
| Arm B, pre-join admission without resource projection | `.codex-research/depleted-return-20260911-b` |
| Arm C, projection added | `.codex-research/depleted-return-20260911-c` |
| Arm D, a later launch vetoes the join on any reason | `.codex-research/depleted-return-20260911-d` |
| Arm E, a later launch vetoes only on momentum | `.codex-research/depleted-return-20260911-e` |
| Arm F, the bounded lookahead aimed at the launch ahead | `.codex-research/depleted-return-20260911-f` |
| **Arm G, the shipped configuration, bound to these sources** | `.codex-research/depleted-return-20260911-g` |
| Case driver (adds `--momentum-tap`) | `.codex-research/depleted-return-20260911-b/run_cases.py` |

Every arm ran the same twenty declared cases from
`.codex-research/depleted-return-20260910-a/cases.json` in the same world
configuration as the 2026-09-10 arms, plus `bot_fastReturnMomentumTap 1`.
Every world is retained. Arm G's build manifest matches the working tree on
all 1104 sources, so the result below is bound to the code this record
describes. The control arm was verified the same way **before** any source was
edited, so no control was lost this time.

## Result

Control against arm G, the shipped configuration:

| Measure | Control | Arm G | Established? |
|---|---|---|---|
| Captures | 20 / 20 | 20 / 20 | unchanged |
| Worlds containing a full stop | 0 | 0 | unchanged |
| **Joins** | **119** | **80** | **yes, down a third** |
| **Releases** | **91** | **49** | **yes, down by nearly half** |
| **Churn for a resource reason** | **28** | **0** | **yes, eliminated** |
| Churn for a momentum reason | 6 | 33 | yes, increased |
| Churn, all reasons | 34 | 33 | unchanged |
| Admitted launches | 181 | 179 | no change established |
| Admission accept rate | 0.67 | 0.79 | yes |
| Frames owned by the lane | 8540 | 8048 | no change established |
| Stationary samples | 64 | 65 | no change established |
| Zero-input samples | 208 | 195 | no change established |

**T1 is met as the plan states it** ("zero joins released for a resource
reason within 300 ms"): 28 -> 0, in every one of the five Package B arms.
It is a categorical change, not a margin.

**T1 is met without total churn falling.** 34 -> 33. What changed is the
cause, not the amount: the lane no longer rejoins a launch its resources
already refused, and instead releases on run-up it has not built yet. Those
33 momentum releases are the launches Package D must branch; a refused run-up
has no alternative today.

The honest summary of the rest: the lane takes a third fewer joins and
releases half as often while flying the same launches, so the same work is
done with far less ownership churn. Nothing beyond that is established.

## Run-to-run variance, and what it invalidates

Arms C and G are the **same logic** — arm G only renames a parameter and
deletes an unused lookup — built and run separately. They differ by more than
the gap this package was trying to measure:

| Measure | Arm C | Arm G | Spread |
|---|---|---|---|
| Frames owned by the lane | 9151 | 8048 | 13 % |
| Zero-input samples | 130 | 195 | 50 % |
| Admitted launches | 199 | 179 | 11 % |
| Stationary samples | 71 | 65 | 9 % |

So **a single twenty-case arm cannot resolve a difference of this size in
these aggregates**, and an earlier draft of this record claiming arm C
"improved the lane, 9151 frames against 8540" was not supported. Joins,
releases, churn composition and the accept rate are stable across C and G and
are reported above; the frame and sample aggregates are not, and are reported
as no change established. Counts of rare events (full stops: 0, 0, 1, 2, 0
across the arms) are too small to carry weight either way.

What survives this caveat is what moved by a factor rather than a margin:
resource churn 28 -> 0, joins 119 -> 80, releases 91 -> 49, and the collapses
in arms B, D and E below.

## Three refuted designs, retained

Each cost a full twenty-world arm. They are recorded because the code invites
all three back.

1. **Pre-join admission without resource projection (arm B).** Judging a
   candidate against the Speed state at the join instant refused every
   successful route-5 flight, because every one of them joins with Speed
   inactive at Force 100 and buys Speed on its own lane frames:

   ```
   join       index=2  force=100 speed_remaining=0
   admission  index=2  force=50  speed_remaining=19950  result=admit
   ```

   Lane frames fell 8540 -> 1165 and admitted launches 181 -> 20. The worlds
   still captured 20/20 on the Package C fallback, which is why capture alone
   is not evidence of a working lane.

2. **A later launch vetoes the join (arm D).** Lane frames 3927, admitted
   launches 90, one world gaining a full stop. Resources change over that
   distance and the later launch is judged again when it arrives.

3. **A later launch vetoes the join on momentum (arm E).** Worse: 3851 frames,
   87 launches, two worlds gaining a full stop. A joining runner is slow by
   construction and builds its run-up along the lane — the index-5 join of
   route 5 reaches 494 u/s by index 8. Refusing that join refuses what the
   lane is for.

Arm F aimed only the bounded two-sample lookahead at the launch ahead and
measured 7276 frames and 162 launches. That is *within* the C/G variance band
above, so arm F is **not** distinguishable from the shipped configuration on
this evidence; it was dropped for being more complex, not for being worse.

The rule the code now states: no caller evaluates a launch ahead of the one
the kernel's takeoff window names.

## Pure checks

`strafe_fast_return_check` (extended: prepared receivers invariant to where
the runner stands, the resource projection, pre-join admission, the refusal
backoff ladder, and momentum taps), `strafe_route_resume_check`,
`strafe_catalog_check`, `strafe_supported_wait_check`,
`strafe_flightplan_check`, `strafe_corner_track_check`,
`strafe_hop_arrival_check` all pass; Python suites 384 tests OK.

Two harness defects let arm B through and are now closed: the mocked
`forcePowerNeeded` had Force Speed costing **0** instead of 50, and there was
no test of a join with Speed inactive at all. Both refuted designs above are
pinned by assertions carrying their measured cost in the comment.

## Regression suites

Both obligations from the plan, rerun on the shipped build
`b5d38e71…` whose 1104 source hashes match the working tree:

| Suite | Result | Evidence |
|---|---|---|
| 26 temporary-body cases (frozen M2 suite, evaluation partition, accelerated) | **26 / 26 pass**, 0 failures | `.codex-research/pkgB-regression-20260911/temporary-body` |
| 30 resource cases, fast return off (accelerated, supported corners 2, staged flight adapter) | **30 / 30, `passed: true`**, every case exit 0 | `.codex-research/pkgB-regression-20260911/resources`, report in `resource-report/summary.json` |

Both match the last recorded status for these suites (30/30 and 26/26 on
`…e8f1…`, `.codex-research/strafe-q1-regression-20260909-e`), so Package B
costs nothing on either. The commands are the ones retained in that run's
`plan.json`, against the new build.

## What is still weak

- **B3 and B6 are not started.** Both switch to branch lanes; Package D has
  authored none. The 33 momentum releases and every unbranched gap group from
  the 2026-09-10 table are still unbranched.
- **Total churn did not fall** (34 -> 33). Only its cause changed.
- **The lane's frame share, stationary and zero-input counts are unmeasured**
  at this sample size. Establishing them needs repeats of one configuration,
  not one arm per configuration.
- **T4 is still not measured per published corner window.** That gap was open
  in the 2026-09-10 record and remains open. The body and resource suites
  above are now closed.
- `bot_fastReturnMomentumTap` was enabled in every Package B arm, so B5 is not
  separated from B1/B2/B4. No claim is made for the tap on its own.
