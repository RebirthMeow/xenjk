# Current movement implementation reference

September 12 optional supporting material. Start with the
[short handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md); read only the section
needed for the active investigation. The [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md)
binds the current build and regressions. Build procedures live in
[verification](STRAFE_MOVEMENT_VERIFICATION.md).

#### What is implemented, and where

| Piece | Where |
|---|---|
| Branch sidecar, hash, validation, lane exclusion | `ai_bot2_route.c`: `CatalogHash`, `LoadBranches`, `branchTable` |
| Branch execution, entry, rejoin, recovery | `ai_bot2_route.c`: `FastReturnGuide`, `RouteIndex`, `Decl`, `EntryLimit`, `DeclsFor` (every candidate, not the first), `BranchRecord`, `JoinSample`, `JoinGeometry`, `BranchUsable`, `BranchEnter`, `TryBranch` (one shared nearest across candidates), `RejoinRefusal` (the next *executable* maneuver), `Rejoin` |
| Tap admission: the two questions kept apart | `ai_bot2_route.c`: `FastReturnTapChain` and `FastReturnAuthoredTap` (prepared -- what the launch costs), `FastReturnOnGuide` (this frame -- whether a low tap may be substituted), `FastReturnLatchFresh` (telemetry only; no rule reads `state->jumpLow`). `FastReturnTakeoffAdmission` takes `tapChain` and `aligned` separately and gates all three flight reserves on `tapChain || authored` |
| One terminal record per branch instance | `ai_bot2_route.c`: `FastReturnBranchClose` (`end` on release/reset/death/objective/owner change/disconnect, `abandon` on the two rejoin-refusal paths, never twice), `fastReturn_t.branchInstance` / `branchClosed`, and `Bot2_StrafeRouteReset` split from `StrafeRouteResetState` so the loading prefix keeps no telemetry dependency |
| Published envelope | `BOT2_BRANCH_*` and `BOT2_TAP_GUIDE_OFFSET` in `ai_bot2_route.c`. The pure checks extract these from the source; never restate them in a test |
| Intermediate terminal semantics | `bot2_strafe.{c,h}`: one `intermediate` flag gating the four final-approach rules |
| Telemetry the audit already reads | `[FASTRETURN] branch â€¦ instance=â€¦ event=select\|entry\|progress\|exit\|abandon\|end`, `[FASTRETURN] admission â€¦` (the route it judged, plus `tap_chain`, `aligned`, `guide_offset`, `latch`, `latch_fresh`, `authored`, `low_jump`, `proj_force`, `proj_speed_ms`), `[FASTRETURN] group â€¦`, `[FASTRETURN] prepared â€¦`, `[FASTRETURN] branch_sidecar â€¦`, `[MOMENTUM] â€¦ route= branch= lane=` |
| Authoring | `tools/strafe_branch_author.py --branch bra|brb` (grounded-only stations, refusal gate, tap straightness, a per-candidate tap report), `tools/strafe_branch_sidecar.py` (bundle, hash, validate). The BR-B station fit is `.codex-research/pkgD-step3-20260911/connector_stations.py` |
| Collection and accounting | `tools/strafe_branch_manifest.py` (manifest, windows, window rules, staged assets), `tools/strafe_momentum_audit.py`, `tools/strafe_branch_report.py`, `tools/test_strafe_branch_report.py` |
| Per-window T2/T2-branch/T3/T4/T5 evaluation | `tools/strafe_branch_windows.py` with `tools/test_strafe_branch_windows.py`. It joins the 4 Hz positions to the 40 Hz trace by cubic Hermite and reports that join's own leave-one-out residual; it refuses to score a window nobody entered, or a branch window whose event did not occur |
| Branch checks | `tools/strafe_catalog_check.py` (sidecar), `tools/strafe_fast_return_check.py` (owner), `tools/strafe_intermediate_tail_check.py` (kernel) |
| Executor release, refusal and admission | `bot2_path.c`: `CrossingContract` and `PassedReported` (one crossing contract, with the condition it missed named), `OnInterval` (the resume admission), `Room` and `Reachable` (a vertex limit against the braking room left to reach it), `Refuse` (report only). The airborne deferral is in the head envelope check and the stale-vertex resume in the advance loop; a speed miss is deliberately excluded from it. Owner policy stays in `ai_bot2_supported.c`: `Reject`, the airborne prefix rule, and the release line that prints `clause`, `contract_lateral`, `contract_limit` and `contract_room`. Check: `tools/strafe_path_release_check.py` |

#### Materials on disk

| What | Path |
|---|---|
| Earlier tapfix3 build (retained evidence only) | `.codex-research/strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`. The step-4 build `strafe-build-brb3-680c73ccâ€¦` and the BR-A build `strafe-build-bra4-31a4ef26â€¦` and tapfix3 no longer match the checkout; their worlds are retained evidence only |
| **Tap repair: declaration, three repetitions, scored comparison** | `.codex-research/pkgD-tapfix-20260912` (`manifest.json`, `candidate-r{1,2,3}/`, `report-tapfix.json`, the step-4 arm re-read as `step4-candidate-r*`, `verify_hashes.py`, and the retained failed `candidate-r3-sourcedrift/`); regressions `.codex-research/pkgD-tapfix-regression`; checks `.codex-research/pkgD-tap-checks` |
| Current assets (catalog 30/32 routes, four declarations) | `.codex-research/pkgD-step3-20260911/assets` |
| BR-A-only assets (catalog 28/32), retained | `.codex-research/pkgD-bra-20260911/assets4` |
| BR-A authoring report | `.codex-research/pkgD-bra-20260911/author6/author.json` |
| BR-A native worlds | `.codex-research/pkgD-bra-native5-20260911` |
| Frozen control declaration and its three arms | `.codex-research/pkgD-control-20260911` (`manifest-v2.json`, `r1..r3`, `control-report.json`) |
| Flight artifact every world stages | `.codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt` |
| Step-3 manifest, cases, station fit, arms, report | `.codex-research/pkgD-step3-20260911` |
| **Step-4 manifest, three arms, reports, window evaluations, transition and outcome analyses** | `.codex-research/pkgD-step4-20260911` |
| **Step-4 defended campaign** (declaration, source snapshot, twelve worlds, aggregate) | `.codex-research/pkgD-step4-defended` |
| **Fall attribution reanalysis** (per-death windows, plunge scan over 192 worlds, impulse alignment, edge traffic) | `.codex-research/pkgD-fallattrib-20260911` |
| **Corrected-contract reanalysis** (nine audits, nine window evaluations, three reports, the frozen-versus-corrected delta) | `.codex-research/pkgD-contracts-20260911` |
| Regressions on the current build | `.codex-research/pkgD-tapfix-regression` (26/26 and 30/30); `pkgD-step3-regression` and `pkgD-regression-20260911` retained |
| Historical tapfix3 pure checks | `.codex-research/pkgD-tap-checks`, with `pkgD-tapfix-20260912/verify_hashes.py`. `pkgD-step4-checks` is retained and now reports `ai_bot2_route.c` stale, correctly |

#### Running and reading a branch world

```
python tools/strafe_play_smoke.py --build <build> --out <dir> --port <p> --team red \
  --seconds 150 --timescale 5 --game-seconds 55 --supported-corners 2 --supported-trace \
  --flight-artifact <artifact> --flight-adapter --forecast-guard --carrier-melee \
  --fast-return --momentum-trace --momentum-tap \
  --catalog .codex-research/pkgD-step3-20260911/assets/catalog.txt \
  --branches .codex-research/pkgD-step3-20260911/assets/branches.txt \
  --corner 2472 4904 1914.5 12 1 --corner-yaw -90 --corner-speed-expiry 13300 \
  --corner-velocity 0 -800 0
```

Blue is `--team blue --corner 152 -744 1914.5 â€¦ --corner-yaw 90 --corner-velocity 0 800 0`.
That exact red command has been run three times as a recipe check and works as
written: before the tap-repair collection
(`.codex-research/pkgD-tapfix-20260912/preflight` â€” sidecar loaded with four
declarations, two branches entered and exited at lane 44, a third censored by the
capture, world captured), before the step-4 collection
(`.codex-research/pkgD-step4-20260911/preflight`), and when the step-3 handoff was
written (`.codex-research/pkgD-handoff-verify-20260911`).

One caveat on the first of those: it ran on
`strafe-build-tapfix-707c807912164e4094ff121ce124400b`, the first tap-repair
build, which differs from the scored one by the branch instance id being scoped
to an open branch. The recipe is what it proves; its telemetry is a build behind,
so read the scored arms for numbers.

Then `rg "FASTRETURN] branch" server.log` for the transition and
`tools/strafe_momentum_audit.py` + `tools/strafe_branch_report.py` for the
accounting. A `select` record is rate-limited to 250 ms and carries `instance=-1`
because no branch is running; `entry`, `progress`, `exit`, `abandon` and `end` all
carry the instance they belong to, and **every instance now has exactly one
terminal record** â€” `end` covers the mid-branch release, the reset, the death, the
objective and owner change and the disconnect. Pair by `instance=`, not by nesting.
The only entries left open are worlds whose capture ended mid-branch, which is
censoring and not a missing record. On the **pre-repair arms** (step 4 and
earlier) no such record exists, so
`.codex-research/pkgD-step4-20260911/branch_outcomes.py` is still the way to see
what ended every entry there.

For the declared windows, run `tools/strafe_branch_windows.py --manifest
<manifest> <world>â€¦`; it needs a manifest carrying `window_rules`, so a
pre-step-4 manifest is refused by name rather than silently misread.

#### Next action, issue 1: the fall off the corridor at branch index 34-38

**Resolved on the declared case set.** The failure began with automatic taps
before the bend at indices 31/32. The runner's airborne momentum missed the
supported line. Distance/turn-based tap room now prevents that departure and
the observed tail heading refusals. See the
[corridor repair](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md), including
three complete repetitions, preserved baselines and regression scope.

The corridor repair was measured on
`.codex-research/strafe-build-corridor-4cc4c8199fba4c3ab41ce3814e445d64`;
checks and native recordings are under `.codex-research/corridor-fix-20260912`.
The earlier issue analysis is preserved in that directory as
`STRAFE_MOVEMENT_CURRENT_REFERENCE.md.before` and in the tap repair record.
Do not repeat that diagnosis to resume. T5 attribution and the local seam fix
are complete; the handoff names the current seam3 build and next ramp work.

#### Next action, issue 2: find T5's mechanism before collecting anything

**Attribution complete:** [T5 phase ledger](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md).
The retained inputs below remain useful; do not repeat the ledger to resume.
The [seam repair](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md) is now measured.
Next is ramp-entry and hop-room continuity. No preparation time occurs in the latest exact-12 post-expiry intervals.


T5 fails in **every arm including the control**, so the branch package is not
what is costing the time. Per repetition, from the corrected contracts: the four
exact-12 seeds return in 18.98-20.60 s (control and sources-only) and
17.90-19.10 s (candidate) against a 16 s target; the two zero-Force/no-Speed
returns take 21.95-23.48 s against 20 s; and the newly gated post-Speed-expiry
mean speed fails **four episodes per repetition in every arm**, at 214-294 u/s
against 300 â€” the candidate lower than the control.

That last row is the lead. Start by **decomposing a return into phases from the
retained 40 Hz traces** â€” run-up, each flight, each landing, corner rounding,
stand entry â€” and attribute the seconds against the 16 s target per phase, for
one exact-12 seed in each arm. Reanalysis over the retained worlds costs about a
minute, so this is a reading exercise, not a campaign. Only once a phase owns
the loss should anything native be collected, and then against a declared
question rather than a search for a passing aggregate.

#### One decision the owner owns: T6's falling-death clause

Raised, not decided. Of the twelve defended worlds' four chasm plunges, **two
are opponent knockback** â€” one of them at normal speed, with the velocity
turning 96.5 degrees at constant magnitude and the impulse 5-10 degrees from
directly away from an opponent 200 units off; the other the timescale-5 carrying
case. The engine seals both outcomes about 1100 units above the floor, zeroing
the command past its `fallingToDeath` boundary, so no movement policy can
recover a runner after that impulse.

At normal speed the split is **one knocked and two self-steered**, and of the
two normal-speed falling deaths one is knocked (blue-1x-r1) and one is
self-steered (blue-1x-r3). So most of the exposure is movement's to fix â€” but
not all of it, and the part that is not cannot be fixed by any movement change.
The clause "no falling deaths in defended play" therefore may not be achievable
as written. The options are to keep it and accept that T6 can fail for reasons
outside movement, to restate it over self-steered departures only, or to restate
it as a rate against traffic (the three departure neighbourhoods are entered
35-62 times per world). **Do not pick one unilaterally, and do not quietly
rescope the target to make a package pass.**

#### After those: the rest of the plan's queue

[Next work, in order](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order)
remains the authoritative work order for everything else â€” the confirmed fall
mechanisms at the two named sites, T2's landing turn, T4's fallback corners,
later launch coverage at takeoffs 50 and 57, and the revised repeated candidate
under predeclared contracts. Acceptance remains open and no package is
installed.

#### The bands any later arm is scored against

From the three control repetitions, per repetition, never pooled: resource churn
0/0/0, all-reason churn 25..34, joins 65..82, releases 41..55, admissions
193..226, admits 152..176, accept rate 0.78..0.79, lane frames 7006..7909,
supported frames 21656..24156, trace samples 28703..32135, stationary 58..65,
zero-input 129..147, hops 421..483, and every branch count 0.

`churn_all`, joins and releases are **not** stable at this sample size (26..36 %
spread); lane frames, supported frames, admits, zero-input, stationary, hops and
the trace denominator vary 12..16 %. Only the accept rate proved stable. A churn
claim inside 25..34 is not available to any arm. `strafe_branch_report.py`
enforces this in code: a gap inside the control's own spread is reported
inconclusive, and an unstable aggregate needs a factor-sized change.

Step 4 added a **second set of bands, on the same case set and settings**, which
a later arm should be scored against as well because they are far tighter than
the aggregates above. Per repetition, from the three candidate arms: branch
entries 51..52, admitted exits 34..38, branches lost mid-way **6, 6, 6**, all-
reason churn 0/0/0, joins 41..45, releases 30..33, admissions 314..343, admits
257..284, lane frames 16649..17810, accept rate 0.818..0.828. In the declared
windows the candidate's descent visits met 50..53 of 52..55 with a median visit
minimum of 361.4 in all three, and the entry window 32..34 of 33..34 with
416.5..417.2 â€” those repeat to a tenth of a unit and will expose a regression
that the churn aggregates would not. The **sources-only** arm (same build, no
sidecar) has no established difference on the scored aggregates and is the right
comparison for any change to shared kernels: churn 21..28, joins 60..74,
releases 38..46, lane frames 6936..8048, accept rate 0.803..0.805.

Two cautions on those bands. **Branch frames are not among them.** On the step-4
arms an entry ended by a release or a death emitted no branch event at all. The
audit does not run such a span to the end of the trace â€” it closes it at the next
release or the first sample the lane does not own, counts only lane-owned
samples, and reports `reconciled: false` â€” so the step-4 candidate's frames read
11923..12166 against 16649..17810 lane frames instead of exceeding them, and
`strafe_branch_report.py` blocks every branch-frame comparison while 46 of its
155 entries close with no terminal event. **The runtime now emits that record**
(`event=end`, with `instance=`), so on a post-repair arm the only unclosed
entries are worlds whose capture ended mid-branch; the tool still blocks the
comparison for those, and separating a censored end from an unreconciled one is
an open measurement-contract change. The corrected corridor band is
**81/93, 89/98, 84/96** met visits, not the frozen 84/92/87; see the
[measurement contracts record](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md).
And the **accept rate is no longer purely a control property** â€” it moved
0.779..0.790 to 0.803..0.805 on the source edit alone, about twice its own
band, so compare a new arm against the arm it actually differs from.

**A third set of bands, from the tap repair (2026-09-12), on the same twenty
cases and the same settings.** This is the arm a further owner change should be
compared against, because it is the one the tree currently builds. Per
repetition: branch entries 38..46, exits 30..37, abandons 2..3, `end` records
2..3, entries with no terminal event 3..7 (all of them a world's last instance,
censored by the capture), joins 37..45, releases 37..39, admissions 292..347,
admits 242..287, accept rate 0.8223..0.8288, lane frames 15017..17993, hops
412..471, supported frames 10693..11464, trace samples 26923..30650, worlds
captured 20/20, falls 0 and deaths 0. Resource
releases: `takeoff_flight_resources` 1..2, `takeoff_reserve` 8,
`takeoff_receiver_reserve` 21..23, and **zero** of the 30..32 gradable resource
releases on a launch the record marks `low_jump=1`.

Two of those bands are **regressions carried deliberately**, not noise, and any
later arm must move them the right way: `localnav` frames 735..775 (step-4
candidate 2..9) and stationary samples 419..437 (step-4 candidate 64..73), both
concentrated entirely in R4-red, R4-blue, R6b-red and R6b-blue, where they repeat
per case to within a few frames (168/168/168, 202/202/202, 192/192/192,
209/200/173). In the declared windows the repair's corridor is 73/74..89/89 met
with a **worst visit minimum of 227..231 u/s** (step-4 candidate 7.9..12.6), and
its entry window is 18/20..26/28 met with a worst visit minimum of 136..141
(step-4 candidate 299..302).

