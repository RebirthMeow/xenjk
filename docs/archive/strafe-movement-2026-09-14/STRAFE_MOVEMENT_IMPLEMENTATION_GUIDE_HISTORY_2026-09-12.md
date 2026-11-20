# Movement implementation and AI handoff — retained history

Historical snapshot preserved during the September 12 instruction cleanup.
Contains superseded status and work orders, not current instructions.
Use the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) to resume;
read this snapshot only for a specific old contract, result or counterexample.
Original bytes are also retained in the archive.

Reviewed 2026-09-12 UTC. Start here when resuming implementation. The
[active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) defines the end result and gates;
the [scorecard](STRAFE_MOVEMENT_PROGRESS.md) owns current results. Update this
guide when a work package changes, not after every diagnostic run.

## Opening instruction for the next AI

### Start here: three issues to resolve, in this order (September 12 handoff)

The measuring is done and **the tap repair is done**. Steps 1 and 2 of the
branch plan are implemented and flown, the repeated campaign and regressions are
complete, step 1 of the remaining work order is closed (the falling deaths are
attributed and the audit and window contracts are repaired), and **step 2 -- the
tap repair -- is implemented, validated on three repetitions and recorded** in
the [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md). **Do not open
this session by auditing anything, and do not re-derive the tap repair.**

What the repair settled: the Speed-clock refusal of a checked tap is gone
(`takeoff_flight_resources` 6 / 6 / 6 to **1 / 2 / 1**, and of the 30-32
resource refusals the record can now grade, **none** lands on a low jump — the
four that remain are all the same honest lane launch, route 5/4 group 4, 675-680
units of level flight at `speed_remaining=0`), every branch instance carries
exactly one terminal record, `[FASTRETURN] admission` names the route it judged
and every input it judged from, and the corridor window's worst sample goes from
7.9-12.6 u/s to **227-231**. The two regressions are unchanged at 26/26 and
30/30.

What it uncovered, and what now owns the next sessions:

1. **The fall off the corridor at branch index 34-38.** The repair stops ejecting
   the runner from BR-A at branch index 15, and it then flies the branch far
   enough to fall off it: on the zero-Force, no-Speed cases the runner leaves the
   floor around index 34-38 and drops about **four hundred units to z 1368**
   (R4-red 440, R4-blue 372, R6b-red 488, R6b-blue 406). The lane keeps steering
   through half a second of that descent and releases when the 256-unit envelope
   trips; `localnav` takes the landing and the recovery costs three to five
   seconds. It is survivable -- 0 falls, 0 deaths, 20/20 captured, health floor
   90 -- but it is the branch putting the runner off a ledge on the very family
   it exists for, and the tap loss was hiding it. **Start here.** It is branch or
   executor work, not admission work.
2. **T5, the return times.** The headline goal, failing in **every arm
   including the control** — so it is not a branch-package deficit, and no
   further arm of the current candidate will explain it. It needs a mechanism,
   not another collection.
3. **T6's falling-death clause.** One of the three normal-speed plunges — and
   one of the two normal-speed falling deaths — is opponent knockback, with no
   movement remedy after the impulse. Whether the clause is achievable as
   written is an owner decision, raised below and not decided here. The other
   two are movement's to fix and stay in scope regardless.

Two smaller things the repair leaves on the table, both recorded: the
**branch-end rejoin** now refuses 2-3 times per repetition where the tap loss
used to hide it (BR-A's open rejoin row, now natively measurable), and
`strafe_branch_report.py` still **blocks every branch-frame comparison** for
entries that are merely censored by the world ending mid-branch — the runtime can
tell a censored end from an unreconciled one, the tool cannot, and changing that
is a declared measurement-contract change.

Measured position, unchanged by this handoff: T1/T1-all **met and repeatable**
(29/25/34 rapid churn cycles to 0/0/0, outside the control's own band). T3
improved on a figure the old evaluator never reported — the median slowest speed
in the second after a refusal is **117-127 u/s in the control and 361 u/s in the
candidate**, repeating to a tenth of a unit across three repetitions. T2-branch
partial; T2, T3, T4 and T5 unmet; T6 failed. **Nothing is installed**: the
installed build remains `strafe-build-reviewready-20260910-b` (`525f1b50`) and
K07 is open and not claimed.

Before quoting any step-4 number, read the
[review corrections](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md#post-campaign-review-corrections)
and the
[corrected contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md):
the corridor, T3, T5 and branch-frame figures in the older tables are superseded
there. The retained worlds and the completed campaign stay as they are; do not
repeat them to resume.

**The working tree carries the whole branch package and the tap repair,
uncommitted.** The last commit is `272f3bc1`, from before step 1. `git status
--short` shows **20 modified tracked files** (eleven under `codemp/game/`
including the Bot2 README, six `tools/` checks, three `docs/`) and **29
untracked** entries, of which twelve are the dated `docs/` records and the branch
plan and twelve are new `tools/` modules and their pure checks; the rest are
`.claude/`, `.codex-archive/`, `.codex-research/`, `navmesh_crash.log` and
`newmoduserdata`.

Of that, the **tap repair** is one production file — `codemp/game/ai_bot2_route.c`
— plus `tools/strafe_fast_return_check.py`, `tools/strafe_catalog_check.py`,
`tools/strafe_momentum_audit.py`, `tools/test_strafe_branch_report.py` and
`docs/STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md`. Everything else is steps 1-4.

**None of this is stray work.** Do not reset, stash or "clean up" any of it, and
do not install anything. Every evidence directory under `.codex-research/` stays
untracked by convention; the sources, tools and docs are what a commit would
take.

**The build that matches this tree** is
`.codex-research/strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`;
`ai_bot2_route.c` hashes
`8a03f087fcb804b915a6d548f5cdf5e99ed80426cc3ef1ae76c6d889002ea0d7` and the three
source-bound pure checks under `.codex-research/pkgD-tap-checks/` are bound to
that hash. The step-4 build `strafe-build-brb3-680c73cce06841b89886aabe6bd6ec63`
no longer matches the checkout; its worlds are retained evidence.

`strafe_play_smoke.py` refuses a build whose sources moved, and it means it:
rewrapping **one comment line** while a collection was running cost sixteen of
repetition 3's twenty worlds on 2026-09-12 (retained at
`.codex-research/pkgD-tapfix-20260912/candidate-r3-sourcedrift/`). Do not touch a
source file, comments included, while any `openjkded` is alive.

Verify before doing anything native:

```bash
python .codex-research/pkgD-tapfix-20260912/verify_hashes.py
```

It prints each current pure check with the codemp hashes it was built from and
ends with `STALE: none` while the tree is unchanged. The older
`.codex-research/pkgD-step4-20260911/verify_hashes.py` still runs and now reports
`ai_bot2_route.c` stale for its three route-bound checks, which is correct and
expected: those results belong to the pre-repair source.

**The work order of record** is the repository-owned
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md). It
supersedes B3/B6, D, E and the execution order in the earlier approved
`~/.claude/plans/fable-see-latest-handoff-curried-summit.md`; completed-package
history remains valid within its recorded scope.

**Read in this order, and do not re-derive any of it or rerun its worlds:**

0. [Tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md) — the most recent
   work and the state the tree is in: what the admission ladder now decides
   from, the telemetry and terminal accounting it added, the three repetitions
   that scored it, the fall it uncovered, and three recorded process errors.
   Read this first; everything below it predates the repair.
1. [Step 4](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md) — the integrated
   campaign, what each acceptance target actually measures, the three-arm
   attribution, and the open list every later step inherits. This is the base
   the repair was measured against.
2. [BR-A](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md) — what the branch
   machinery is, the published envelope, and the four native defects it cost.
   Its first two open items were closed by step 3; the rest stands.
3. [BR-B](STRAFE_MOVEMENT_PACKAGE_D_BRB_2026-09-11.md) — the post-group-1
   connector, the two runtime corrections it needed, the arms that measured it,
   and why takeoffs 50 and 57 are measured but deliberately unbranched.
4. [Control arms](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md) — the frozen
   case set, the measured bands, and two corrected measures.
5. [Corridor](STRAFE_MOVEMENT_PACKAGE_D_CORRIDOR_2026-09-11.md) — the shape
   evidence BR-A was authored from, with two caveats now known: its convergence
   figures at lane samples 42/44 are **airborne** convergence, and its rejoin
   bullet offers samples 43 *and* 44 with both positions, which is how the
   step-1 manifest came to freeze a window on the one step 2 then abandoned.
6. [Fall attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md) — what
   actually took the runner off the floor in all four defended-world plunges,
   the engine paths that make kill credit non-causal, and the two sites a
   supported-traveler repair has to change.
7. [Measurement contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md)
   — the seven corrected contracts, what each one moves in the published
   numbers, and the reconciled branch accounting. Read it before quoting any
   step-4 window or frame figure.
8. [Package B](STRAFE_MOVEMENT_PACKAGE_B_2026-09-11.md) and
   [depleted returns](STRAFE_MOVEMENT_DEPLETED_RETURN_2026-09-10.md) — the lane
   owner's resource rules and the native attribution of the seven owner issues.

#### What is implemented, and where

| Piece | Where |
|---|---|
| Branch sidecar, hash, validation, lane exclusion | `ai_bot2_route.c`: `CatalogHash`, `LoadBranches`, `branchTable` |
| Branch execution, entry, rejoin, recovery | `ai_bot2_route.c`: `FastReturnGuide`, `RouteIndex`, `Decl`, `EntryLimit`, `DeclsFor` (every candidate, not the first), `BranchRecord`, `JoinSample`, `JoinGeometry`, `BranchUsable`, `BranchEnter`, `TryBranch` (one shared nearest across candidates), `RejoinRefusal` (the next *executable* maneuver), `Rejoin` |
| Tap admission: the two questions kept apart | `ai_bot2_route.c`: `FastReturnTapChain` and `FastReturnAuthoredTap` (prepared -- what the launch costs), `FastReturnOnGuide` (this frame -- whether a low tap may be substituted), `FastReturnLatchFresh` (telemetry only; no rule reads `state->jumpLow`). `FastReturnTakeoffAdmission` takes `tapChain` and `aligned` separately and gates all three flight reserves on `tapChain || authored` |
| One terminal record per branch instance | `ai_bot2_route.c`: `FastReturnBranchClose` (`end` on release/reset/death/objective/owner change/disconnect, `abandon` on the two rejoin-refusal paths, never twice), `fastReturn_t.branchInstance` / `branchClosed`, and `Bot2_StrafeRouteReset` split from `StrafeRouteResetState` so the loading prefix keeps no telemetry dependency |
| Published envelope | `BOT2_BRANCH_*` and `BOT2_TAP_GUIDE_OFFSET` in `ai_bot2_route.c`. The pure checks extract these from the source; never restate them in a test |
| Intermediate terminal semantics | `bot2_strafe.{c,h}`: one `intermediate` flag gating the four final-approach rules |
| Telemetry the audit already reads | `[FASTRETURN] branch … instance=… event=select\|entry\|progress\|exit\|abandon\|end`, `[FASTRETURN] admission …` (the route it judged, plus `tap_chain`, `aligned`, `guide_offset`, `latch`, `latch_fresh`, `authored`, `low_jump`, `proj_force`, `proj_speed_ms`), `[FASTRETURN] group …`, `[FASTRETURN] prepared …`, `[FASTRETURN] branch_sidecar …`, `[MOMENTUM] … route= branch= lane=` |
| Authoring | `tools/strafe_branch_author.py --branch bra|brb` (grounded-only stations, refusal gate, tap straightness, a per-candidate tap report), `tools/strafe_branch_sidecar.py` (bundle, hash, validate). The BR-B station fit is `.codex-research/pkgD-step3-20260911/connector_stations.py` |
| Collection and accounting | `tools/strafe_branch_manifest.py` (manifest, windows, window rules, staged assets), `tools/strafe_momentum_audit.py`, `tools/strafe_branch_report.py`, `tools/test_strafe_branch_report.py` |
| Per-window T2/T2-branch/T3/T4/T5 evaluation | `tools/strafe_branch_windows.py` with `tools/test_strafe_branch_windows.py`. It joins the 4 Hz positions to the 40 Hz trace by cubic Hermite and reports that join's own leave-one-out residual; it refuses to score a window nobody entered, or a branch window whose event did not occur |
| Branch checks | `tools/strafe_catalog_check.py` (sidecar), `tools/strafe_fast_return_check.py` (owner), `tools/strafe_intermediate_tail_check.py` (kernel) |

#### Materials on disk

| What | Path |
|---|---|
| Build matching the working tree | `.codex-research/strafe-build-tapfix2-14294c8f4dd44cd09670e393de98bc41`. The step-4 build `strafe-build-brb3-680c73cc…` and the BR-A build `strafe-build-bra4-31a4ef26…` no longer match the checkout; their worlds are retained evidence only |
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
| Regressions on the current build | `.codex-research/pkgD-step4-regression` (26/26 and 30/30); `pkgD-step3-regression` and `pkgD-regression-20260911` retained |
| Pure checks bound to these sources | `.codex-research/pkgD-tap-checks`, with `pkgD-tapfix-20260912/verify_hashes.py`. `pkgD-step4-checks` is retained and now reports `ai_bot2_route.c` stale, correctly |

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

Blue is `--team blue --corner 152 -744 1914.5 … --corner-yaw 90 --corner-velocity 0 800 0`.
That exact red command has been run three times as a recipe check and works as
written: before the tap-repair collection
(`.codex-research/pkgD-tapfix-20260912/preflight` — sidecar loaded with four
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
terminal record** — `end` covers the mid-branch release, the reset, the death, the
objective and owner change and the disconnect. Pair by `instance=`, not by nesting.
The only entries left open are worlds whose capture ended mid-branch, which is
censoring and not a missing record. On the **pre-repair arms** (step 4 and
earlier) no such record exists, so
`.codex-research/pkgD-step4-20260911/branch_outcomes.py` is still the way to see
what ended every entry there.

For the declared windows, run `tools/strafe_branch_windows.py --manifest
<manifest> <world>…`; it needs a manifest carrying `window_rules`, so a
pre-step-4 manifest is refused by name rather than silently misread.

#### Next action, issue 1: the fall off the corridor at branch index 34-38

The tap repair is **done** — implemented, validated on three repetitions and
recorded in the [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md),
which supersedes the instruction that used to stand here. Read that record and
the [tap-loss diagnosis](STRAFE_MOVEMENT_TAP_LOSS_DIAGNOSIS_2026-09-11.md)
before touching `FastReturnTakeoffAdmission`, and do not re-derive either.

What the repair uncovered is the next item, and it is not admission work.

With the tap no longer refused, the runner flies BR-A past branch index 15 for
the first time, and on the **zero-Force, no-Speed** cases it falls off the
corridor at index 34-38. The 40 Hz trace shows the descent starting well before
the release — R4-red is airborne at `vz=-159` and the lane steers it through
eighteen more frames of accelerating fall before the 256-unit envelope trips at
`vz=-517`. `envelope` and `rejoin_refused` are the same event caught by different
gates: the exit gate when the runner had reached the branch's exit zone, the lane
envelope when it had not. It lands ~400 units lower at z 1368, and the owner then
alternates `localnav` 40 / `supported` 3-6 for several seconds.

Measured, in every one of three repetitions, in exactly four cases, with the
`localnav` cost repeating per case to within a few frames:

| Case | branch index at release | release | vz | z before → after | `localnav` frames |
|---|---|---|---|---|---|
| R4-red | 34 | `envelope` | −517.7 | 1808 → 1368 (**440**) | 168 / 168 / 168 |
| R4-blue | 36 | `rejoin_refused` | −677.7 | 1740 → 1368 (**372**) | 202 / 202 / 202 |
| R6b-red | 34 | `envelope` | −517.7 | 1856 → 1368 (**488**) | 192 / 192 / 192 |
| R6b-blue | 38 / 37 / 34 | `rejoin_refused` ×2, `envelope` | −677.7 | 1774 → 1368 (**406**) | 209 / 200 / 173 |
| R5-blue | 43 | `rejoin_refused` | — | 1808 → 1808 (**0**) | 0 / 0 / 0 |

Height and `localnav` are from repetition 1; the index and reason columns hold in
all three except R6b-blue, whose release walks 38 / 37 / 34. R5-red never ends a
branch that way at all, and R5-blue does so in two of three repetitions.

Arm totals: `localnav` 2 / 9 / 2 to **775 / 765 / 735**, stationary 71 / 73 / 64
to **436 / 437 / 419**, zero-input 114 / 128 / 111 to **656 / 627 / 619**,
longest grounded slow window 2325 ms to 3350 / 3350 / 5475 ms. R5, which holds
Speed for its first three seconds, is the control inside the arm: it is refused
at branch index 43 instead of 34-38, never descends, and the supported executor
takes over for **321 consecutive samples with zero stationary ones**.

This is BR-A failing on the family it exists for, and it is survivable: 0 falls,
0 deaths, 20/20 captured and a worst health floor of 90 in every repetition, so
z 1368 is a lower floor and not the chasm. The whole cost is the climb back.

**Answer this before choosing a fix:** does the authored line leave the floor at
index 34-38, or is the runner already off the line by then? `tapSafe` checks the
authored line's floor, not the trajectory a slower runner actually flies, so it
catches neither, and the two have different repairs. Three shapes, none chosen
here:

1. **Author the branch for the speeds it is flown at.** If the line itself runs
   off an edge that is only safe at the 425 u/s the recordings were made at, that
   is `tools/strafe_branch_author.py` work and it re-opens the station fit.
2. **Close on the guide instead of releasing.** If the runner has drifted off a
   line that is fine, the 256-unit envelope release is what strands it mid-fall;
   holding and steering back is a different response from handing over.
3. **Refuse the entry.** `FastReturnBranchUsable` could decline a branch the
   runner's projected resources cannot fly, which keeps it on the supported
   fallback it used to reach anyway — the smallest change, and the one that gives
   up the branch rather than fixing it.

**The cheapest first step is free and needs no world.** Each retained case has a
4 Hz play CSV with `x, y, z` and a 40 Hz `[MOMENTUM]` trace with `index`. Take
the frames just before the runner leaves the floor — R4-red is already airborne
at level 8450 — and compare its position against branch route 26/27's own sample
positions at that index. If it is on the line, the line leaves the floor; if it
is not, the runner did. That single reading picks between repairs 1 and 2 below,
and only then is a native floor probe worth running.

Evidence, no new collection needed:
`.codex-research/pkgD-tapfix-20260912/candidate-r{1,2,3}/R{4,6b}-{red,blue}/server.log`
for the `[MOMENTUM]` owner timelines and the releases, the `bot2-play/*.csv`
beside each for height, and `report-tapfix.json` for the scored aggregates. The
branch's authored samples are in
`.codex-research/pkgD-step3-20260911/assets/catalog.txt`, routes 26 (red) and
27 (blue).

Also open from the repair, both smaller: the **branch-end rejoin** now refuses
2-3 times per repetition (BR-A's rejoin row, measurable natively for the first
time), and `strafe_branch_report.py` blocks branch-frame comparisons for entries
that are only **censored** by the world ending mid-branch — the runtime
distinguishes those now and the tool does not.

#### Next action, issue 2: find T5's mechanism before collecting anything

T5 fails in **every arm including the control**, so the branch package is not
what is costing the time. Per repetition, from the corrected contracts: the four
exact-12 seeds return in 18.98-20.60 s (control and sources-only) and
17.90-19.10 s (candidate) against a 16 s target; the two zero-Force/no-Speed
returns take 21.95-23.48 s against 20 s; and the newly gated post-Speed-expiry
mean speed fails **four episodes per repetition in every arm**, at 214-294 u/s
against 300 — the candidate lower than the control.

That last row is the lead. Start by **decomposing a return into phases from the
retained 40 Hz traces** — run-up, each flight, each landing, corner rounding,
stand entry — and attribute the seconds against the 16 s target per phase, for
one exact-12 seed in each arm. Reanalysis over the retained worlds costs about a
minute, so this is a reading exercise, not a campaign. Only once a phase owns
the loss should anything native be collected, and then against a declared
question rather than a search for a passing aggregate.

#### One decision the owner owns: T6's falling-death clause

Raised, not decided. Of the twelve defended worlds' four chasm plunges, **two
are opponent knockback** — one of them at normal speed, with the velocity
turning 96.5 degrees at constant magnitude and the impulse 5-10 degrees from
directly away from an opponent 200 units off; the other the timescale-5 carrying
case. The engine seals both outcomes about 1100 units above the floor, zeroing
the command past its `fallingToDeath` boundary, so no movement policy can
recover a runner after that impulse.

At normal speed the split is **one knocked and two self-steered**, and of the
two normal-speed falling deaths one is knocked (blue-1x-r1) and one is
self-steered (blue-1x-r3). So most of the exposure is movement's to fix — but
not all of it, and the part that is not cannot be fixed by any movement change.
The clause "no falling deaths in defended play" therefore may not be achievable
as written. The options are to keep it and accept that T6 can fail for reasons
outside movement, to restate it over self-steered departures only, or to restate
it as a rate against traffic (the three departure neighbourhoods are entered
35-62 times per world). **Do not pick one unilaterally, and do not quietly
rescope the target to make a package pass.**

#### After those: the rest of the plan's queue

[Next work, in order](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order)
remains the authoritative work order for everything else — the confirmed fall
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
416.5..417.2 — those repeat to a tenth of a unit and will expose a regression
that the churn aggregates would not. The **sources-only** arm (same build, no
sidecar) has no established difference on the scored aggregates and is the right
comparison for any change to shared kernels: churn 21..28, joins 60..74,
releases 38..46, lane frames 6936..8048, accept rate 0.803..0.805.

Two cautions on those bands. **Branch frames are not among them.** On the step-4
arms an entry ended by a release or a death emitted no branch event at all. The
audit does not run such a span to the end of the trace — it closes it at the next
release or the first sample the lane does not own, counts only lane-owned
samples, and reports `reconciled: false` — so the step-4 candidate's frames read
11923..12166 against 16649..17810 lane frames instead of exceeding them, and
`strafe_branch_report.py` blocks every branch-frame comparison while 46 of its
155 entries close with no terminal event. **The runtime now emits that record**
(`event=end`, with `instance=`), so on a post-repair arm the only unclosed
entries are worlds whose capture ended mid-branch; the tool still blocks the
comparison for those, and separating a censored end from an unreconciled one is
an open measurement-contract change. The corrected corridor band is
**81/93, 89/98, 84/96** met visits, not the frozen 84/92/87; see the
[measurement contracts record](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md).
And the **accept rate is no longer purely a control property** — it moved
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

#### Eleven procedural rules learned the hard way

- **Collect a control arm before editing the sources it belongs to.**
  `strafe_play_smoke.py` refuses a build whose sources have changed. Six
  declared corner cases were lost that way on September 10.
- **A comment is a source edit.** The hash does not care what the bytes mean.
  Rewrapping one over-long comment line in `ai_bot2_route.c` while a collection
  was running cost sixteen of repetition 3's twenty worlds on September 12
  (retained at `.codex-research/pkgD-tapfix-20260912/candidate-r3-sourcedrift/`).
  Keep every source file untouched until `Get-Process openjkded*` is empty.
- **Re-read the arm you are comparing against with today's tools.** The step-4
  audits were written before the reconciliation and terminal-event fields
  existed, so quoting them beside a new audit compares two different
  measurements. Re-read the retained worlds into the new record instead — it
  costs about a minute and the frozen outputs stay frozen.
- **Tune against measured refusal reasons, not guesses.** Every Package B and
  BR-A correction came from reading `[FASTRETURN]` lines in a retained world.
- **A pre-commitment check must model what the runner will have, not what it
  has.** Judging a join against the Speed state at that instant refused every
  successful route-5 flight. Three further arms were spent discovering that no
  caller may evaluate a launch ahead of the one the kernel's takeoff window
  names.
- **Author route lines from grounded samples only.** A median over *all*
  measured samples follows the hop arc those runners fly: the first BR-A line
  sat within 15 units of real positions — while airborne — and fell 1776 units
  into a gap, in both directions. A tap placed into the turn that avoids that
  same gap cost another 500-unit fall, because a tap is ballistic. Nearness to
  measured play is not evidence of standable floor.
- **A station estimator must keep the same population at every station.** A
  median over whatever grounded samples fall inside a 50-unit band changes the
  set of episodes behind each station, and on the post-group-1 connector that
  alone swung the authored heading 20 degrees between neighbours and left one
  usable tap stretch out of fourteen. Interpolating each episode's own track
  where it crosses each station and taking the median across episodes fixed it.
  Where the two estimators overlap they agree to 0.8 units, which is also how
  the merge point was chosen.
- **The kernel's takeoff window reaches three samples *behind* the index.** That
  is right for a lane join that may still owe the launch behind it, and wrong
  for anything asking "what must this runner fly next". A branch rejoin asked it
  and got the launch the branch exists to walk around, so a 12-Force runner
  completed the whole branch and was then refused the lane — twice, in both
  directions, on two different branches, and it read exactly like a resource
  problem.
- **A frozen window outlives the contract it was written for.** Step 1 froze the
  rejoin window at lane sample 43 from the corridor record's convergence; step 2
  then moved the declared rejoin to 44, 111.8 units away, and step 4's frozen
  window could not contain a single one of the rejoins it was meant to measure.
  When a contract moves, re-declare every frozen window that names it, in the
  next manifest and before the collection that scores it — and when the freeze
  has already failed, score it as frozen and measure the obligation a second way
  against some *other* declaration that predates the collection. The runtime's
  own published envelope did that job here. Never move a window after seeing
  what moving it would do to the numbers.
- **An absent event is not a satisfied one.** Both branchless arms walk straight
  through the coordinates a branch uses, and an evaluator that scored those
  crossings would have reported a perfect entry and rejoin for a build that
  cannot execute a branch at all. A window bound to an event is scored only when
  the event occurred; a window nobody entered is reported `visited: false`; an
  accepted curve with no supported-owned sample is a bypass, not a pass.
- **Run the Python suite from the repository root:**
  `PYTHONPATH=tools python -m unittest discover -s tools -p "test_*.py" -t tools`
  (409 tests, all passing). Started inside `tools/`, three unrelated modules
  report eight failures that are pure working-directory artifacts.

### Owner work order of record (September 10)

The owner reviewed the candidate and identified the following blocking issues.
Packages A and C address 1, 4 and 5 in part and leave 2, 3, 6 and 7 open. This
work order supersedes earlier invitations to retest and earlier next-action text
below. **Read the items below as the owner's September 10 findings, not as
current status**; each carries its own progress note, and the scorecard owns
measured status. The installed build remains `strafe-build-reviewready-20260910-b`
(commit `525f1b50`); no production change was made while recording this handoff.

1. Incoming momentum is squandered: observed 889.9 u/s becomes 428.9 u/s,
   essentially the 425 u/s Force Speed ground-running pace.
2. Return lanes were reused, not tailored to depleted Force, actual pickup
   velocity/location and nearby opposition.
3. There are no prepared low-/zero-Force branches off the main lane. The
   checked ordinary-tap substitution follows the same geometry; generic
   supported recovery is not an alternate fast lane.
   *Progress (2026-09-11): one prepared branch now exists and runs end to end in
   both directions — an authored tap line with its own geometry, classified
   `class=0 tap=1`, entered on a refused lane launch and rejoining at lane
   sample 44. It covers the early-entry family only; the post-group-1 landing
   state, takeoffs 50 and 57 and the other momentum-refusal families remain
   exactly as described here. No speed claim. See
   [BR-A](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md).*
4. There is no effective zero-Force continuation preserving existing momentum,
   nor a deliberate ordinary-hop recovery for zero Force plus lost speed.
5. Supported fallback brakes/hesitates excessively around corners, undermining
   both preservation and rebuilding of speed and leaving the carrier exposed.
6. Joining and immediate maneuver feasibility are disconnected: a join can
   immediately release for the same unaffordable jump.
   *Progress (2026-09-11): closed as a diagnostic — resource-reason churn is
   0/0/0 across three control repetitions of 60 worlds, because a launch is now
   judged before the join against the resources it will meet. What remains is
   the momentum half: 25..34 cycles per repetition, every one of them a
   `takeoff_momentum` refusal, which is what a branch has to answer.*
7. Readiness was overstated by aggregate captures/no-stall results and a
   12.1-second median that depends heavily on active Force Speed. Resource-
   depleted cornering and sustained pace must be evaluated explicitly.

#### Evidence to start from; do not repeat completed inventory

Owner screenshot transcription (client 1, route 5): pickup/armed/join at
89125, index 0, speed 889.9, Force 26, Speed remaining 13300 ms; release at
92875/index 27 for `takeoff_receiver_reserve`; rejoin at 93375/index 29,
speed 428.9, Force 12, Speed remaining 9050 ms; immediate same-reason release.
This proves a wasted rejoin and endpoint speed loss, not the exact intervening
braking cause or an endless loop. Trace steering/contact to attribute that loss.

`tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt`, zero-based route 5,
has 70 samples and 10 separate positive-up groups: 2 (tap), 10-14 (held),
29, 36, 43, 50, 52, 57, 64, 66. These are command cues, not ten proven physical
jumps; 52 and 66 are already airborne in the reference. Its first flight has
about 922 u/s horizontal reference velocity, followed by a zero-velocity
sample at 21 and roughly 425-500 u/s through the ground turn. The follower
uses max(425, reference horizontal speed), so zero reference does not itself
command a stop; the low-speed reference turn is nevertheless a suspect.

`.codex-research/reviewready-20260910-b/natural-force-speed-audit.json` binds
the same eight return episodes: the two defining the 12.0625 s median have
10.55/10.575 s of Speed and 1.5 s without. The 24.1 s red episode has Speed
for 11.25 s. Telemetry is approximately 4 Hz; active-power time is not a
measurement of ordinary-hop competence or of every native jump.
See the [prior delivery evidence](STRAFE_MOVEMENT_REVIEW_READY_2026-09-10.md)
and [human corner study](STRAFE_MOVEMENT_HUMAN_CORNERS_2026-09-10.md).

#### Ordered implementation steps

Kept verbatim as the owner stated them. Status against the
[record](STRAFE_MOVEMENT_DEPLETED_RETURN_2026-09-10.md): **step 1 done**
(Package A, findings 1-8), **step 3 done** (Package C, measured before/after),
**step 4 holds** (no reserve was lowered, no Force granted, no recharge waited
on); steps 2 and 5 remain.

1. Read the Bot2 README and relevant consumers before changing code. Inspect
   `ai_bot2_route.c` (FastReturn selection, tap checks, join/release),
   `bot2_strafe.c` (reference speed, turn steering, jump/landing selection),
   `ai_bot2_supported.c` and `bot2_hop.c` (fallback turns and hopping).
   Reproduce the route-5 first landing/turn and next jump with actual low-Force
   entry velocity. Record entry/minimum/exit speed, commands, contacts, owner,
   Force and Speed lifetime. Determine where momentum is lost, not merely
   where the resource guard finally reports refusal.
2. Implement a bounded Kejim continuation choice before an unaffordable
   maneuver. Supply checked low-/zero-Force ordinary-hop alternatives, including
   a moving connector from the main lane. Choose using actual velocity,
   heading, resources, remaining Speed and receiver clearance. Start with the
   reported route-5 bottleneck, then cover the opposite direction; avoid a
   general planner rewrite. Check the next executable maneuver before committing
   a join so the same unsupported choice cannot immediately churn ownership.
3. Fix ordinary corner execution in both lane connectors and supported fallback:
   preserve feasible momentum, use clearance-aware turn approach, and brake only
   as needed. Provide ordinary strafe-hop acceleration when depleted and slow.
   Do not assume more navmesh resolution alone solves controller braking.
4. Keep landing/floor/body checks, native Force ownership, the corner/arrival
   fixes and pressed-runner staff melee. Do not fix this by globally lowering
   jump reserves, granting Force, waiting for recharge or simply forcing unsafe
   jumps. Distinguish airborne continuation hints from new physical launches.
5. Freeze a candidate and run focused native evidence below. Use the existing
   fresh-build/hidden-runner procedures from AGENTS.md. Keep failed cases and
   source/binary bindings. Update the scorecard with results, then install with
   the archive-safe helper only after the behavioral evidence earns review.

#### Acceptance evidence for this package

Declare the bounded cases and numeric speed/turn targets before collection;
these are new behavioral checks, not a retrospective pass of the old median.
Cover both teams and high-momentum versus slow entries at zero and low Force
(including 12/20/26), with Speed active, expiring and inactive. Include the
reported route-5 landing/turn and the existing upper/ramp/stall locations.
Separate these conditions in the results rather than pooling them into a
Speed-dominated median. Measure:

- Corner entry/minimum/exit horizontal speed and ground contact/braking duration.
- Time/distance to rebuild speed after a slow zero-Force entry, plus sustained
  ordinary-hop travel after Speed expires.
- Feasible branch selection before the rejected jump, actual landing outcome,
  and absence of repeated join/immediate-refusal cycles.
- Pickup-to-capture times, deaths and censored attempts; active-Speed time;
  carrier speed and airborne share separately by resource condition.
- Normal-speed defended performance after focused solo checks. Preserve the
  no-unforced-fall expectation; capture and no long stall alone are insufficient.

Do not invite another owner playtest merely because a guard/test passes. The
next handoff must explain which momentum/resource cases now work and which
remain weak. K07 remains open; do not restart the historical qualification
chain or demo decoding. General aiming remains paused.

### Historical delivery context

**Current delivery: `strafe-build-reviewready-20260910-b`, installed.**
Backup `20260910T0607586345246Z`; nine installed hashes verified. Read the
[review-ready record](STRAFE_MOVEMENT_REVIEW_READY_2026-09-10.md) first.
The owner asked us to take responsibility for a worthwhile review candidate.
The blanket 50-Force regression is fixed with a receiver-specific 20 reserve;
retain short/downhill native-reserve admission and checked zero-Force taps.
Do not restore a broad 26/50 gate. Checked curves, analog arrival, rising-join
rejection, floor checks and pressed-runner melee remain enabled.
Fourteen solo worlds yield 56 captures/no deaths; natural median 12.0625 s,
538.6 u/s mean horizontal speed and 64.3% airborne. Both defended teams capture
(five total), with four weapon deaths and no falls or long stalls. Defended
completed median is still 24.325 s; the first depleted solo red return is 24.1 s.
Next: use the owner's in-game review to identify the largest remaining corner
or pressure-recovery problem. Do not restart demo decoding or completed test
inventory. K07 remains open. Older next-action instructions below are historical.

**Latest delivery: corner repair, build `strafe-build-corners-20260910-b`.**
Installed with fast return enabled, backup `20260910T0541114616343Z`.
Read [the repair record](STRAFE_MOVEMENT_CORNER_REPAIR_2026-09-10.md) before
continuing. Curved streaming publication and grounded analog arrival are
implemented. A failed native iteration exposed unsafe fast-lane joins; the
delivered build contains those failures with local admission/floor checks.
The price is slower natural returns (19.575 s median versus roughly 12 s).
Do not loosen the reserve merely to recover that median: validate the local
flight first. Ten solo worlds and two defended normal MP smokes are complete;
human retest is the next checkpoint. Do not rerun the inventory, failed build
or old qualification chain. K07 remains open; older next-action text below is
historical where it asks to implement this now-completed repair.

**Latest checkpoint: human corner study completed September 10.** The owner
reported continued corner hesitation and authorized studying human demos.
Read [the study](STRAFE_MOVEMENT_HUMAN_CORNERS_2026-09-10.md), including its
ordered implementation steps. Four recordings were decoded; do not repeat the
inventory or decode to rebuild context. Next repair should validate moving
corner exits for streaming returns and separately address the supported
zero-input stall. Preserve collision/floor/landing checks and moving preparation
progress; do not simply remove the shared arrival-speed threshold. Use the
two recorded bot corner locations and stall location for focused native checks,
then defended smoke and human retest. No movement code changed in the study;
the installed build remains fastreturn-d. K07 remains open.

**Latest delivery: fast return, build `strafe-build-fastreturn-20260910-d`.**
Installed with `bot_fastReturn 1`, backup `20260910T0426153079480Z`.
Read the [fast-return record](STRAFE_MOVEMENT_FAST_RETURN_2026-09-10.md).
Lane preparation/joins and checked ordinary taps produce faster measured
returns; legacy full-guide resource gates are unchanged. The first severely
depleted return remains weak. Next: inspect the owner's retest, especially
low-Force pickup clearance and recovery-to-lane handoff. Use
`tools/strafe_return_audit.py`; retain deaths/censoring and separate first
low-Force returns from subsequent full-resource ones. Do not repeat failed
development builds or the historical qualification chain to rebuild context.
K07 stays open. Earlier checkpoint instructions below are historical.

**Latest checkpoint: pickup/pad repair, build `strafe-build-pickup-pad-20260910-c`.**
Read the [repair record](STRAFE_MOVEMENT_PICKUP_PAD_2026-09-10.md) before the
historical work below. The two human-observed defects have focused native
coverage and normal MP defended smoke. Deliver/review the owner's retest next;
do not repeat the first-build failures or launch the old qualification chain.
If new human evidence appears, inspect pickup-to-return zero-input time and
pad landing/ownership first. Ordinary returns remain slower than the incoming
cached routes. K07 is still open.

**Latest execution priority: 80/20 competence triage.** Follow the
[current triage](STRAFE_MOVEMENT_PROJECT_PLAN_HISTORY_2026-09-12.md#current-8020-triage-competent-kejim-play)
before the historical Q1–Q5 queue. Review existing corrected-match deaths and
the longest wasted-motion episodes; use normal-speed human feedback to choose
at most one or two recurring controllable defects. Keep the working melee
policy. Do not launch full qualification, general combat development or timing
optimization without the trigger specified in that triage. Passing a focused
playtest remains distinct from K07 qualification.

The first human playtest motivated **pickup-to-return
continuity**, using the [recorded complaint and evidence](STRAFE_MOVEMENT_HUMAN_PLAY_2026-09-10.md).
Inspect `StandEntry` resource waiting and supported preparation/ownership before
new collection. Do not interpret zero five-second confinement counts as a
successful handoff. The fixed-XY vertical bounce is second priority.

**Current priority override (September 10 UTC): playable candidate first.**
The owner requests a speedrun to normal in-game play and confirms ComfyUI is
closed. The Q3 v4 bench completed; do not launch its old 70-minute gate
chain automatically. The reviewed melee/jump/flight and obstruction handback
defects are corrected in `strafe-build-playtest-20260910-ownership`. Both focused
checks pass; 26 body cases and red/blue defended smokes retain useful movement.
Use the [playtest record](STRAFE_MOVEMENT_PLAYTEST_2026-09-10.md) for final
resource, normal MP listen, isolated cost and installation evidence. The
archive-safe installation is complete (archive `20260910T0248073047206Z`).
Next: human playtest via `exec runner_playtest.cfg`, then `addrunner`. Do not
reinstall or repeat this bench to reconstruct context. Fix concrete play
feedback, then continue the remaining qualification packages.
K07 stays open until all its gates pass; this delivery checkpoint does not
weaken or retroactively pass them. The older Q3 first-action sequence below is
superseded where it delays this explicit playable checkpoint.

**Q3 checkpoint (2026-09-10): `9d8dff86` on local `master`** (the repository's
primary branch, not `main`). Implementation and evidence are committed there;
this handoff commit only updates documents. **Q3 is not passed.** Read the
[Q3 record](STRAFE_MOVEMENT_Q3_EXECUTION_2026-09-09.md) first. Two complete
12+12 defended collections fail the stall requirement with one family: a live
defender pressed at about 30 units while the runner stands. Ground-navigation
repairs (body-aware bounded sidestep, obstruction-aware preparation release,
execution-obstruction release) narrowed the standstills: on the v2 build the
candidate beats the baseline on idle and walking in every cell (pooled
6.45 % / 34.33 % versus 7.12 % / 37.65 %) but records three stalls against
none (`strafe-q3-continuity-{candidate,baseline}-20260910-b`). **The owner's
direction (2026-09-10) is that the carrier fights:** `bot_carrierMelee`
(v4) makes a standing movement-only runner with a live enemy within 48 units
face it, hold forward and swing the saber; the runner profile now carries a
staff (`saber1 dual_1`); `--carrier-melee` is a declared candidate play
argument, the baseline stays plain. The v4 build
`strafe-build-q3-melee4-2e5f8a1b3c6d4e7f9a0b1c2d3e4f5a6b` (1104 inputs,
matches `9d8dff86`) is built and unmeasured: ComfyUI was resident (8 GB, under
2 GB free) and fixture setup times out under that pressure, so no world has
run on v3 or v4.

**First actions for the next session (Q3 continued):**

1. `git status --short`; expect only untracked `.claude/`, `.codex-*`,
   `navmesh_crash.log`, `newmoduserdata`. Do not reset or install anything.
2. Check memory before any world: no ComfyUI `python_embeded\python.exe`
   resident and several GB free. Never kill the owner's ComfyUI; report and
   wait. Then check `tasklist | grep openjkded` is zero.
3. Run the v4 bench detached (Git Bash path, see memory):
   `.codex-research/strafe-q3-repair-v4-bench.sh` (shadow eight, traced red 5x
   with melee on, Q1 regressions; about eight minutes; stage log
   `strafe-q3-repair-v4-20260910.chain.log`). Compare the shadow summary with
   `strafe-q1-analysis-20260909/shadow-summary-final5.json` and
   `strafe-q3-shadow-20260910-v2/shadow-summary.json` (the proxy is not a
   client, so this stays a pure movement bench); require 26/26 body and
   30/30 resources; read the traced worlds' `[MELEE] swing` prints and their
   opposition audit.
4. If the bench holds, run `.codex-research/strafe-q3-repair-v4-chain.sh`
   (Q2 batches with the pair-report gate, then Q3 `-c`: both arms frozen on
   ports 43500..43531, about seventy minutes). Do not edit anything in the
   build's source manifest while it runs. Judge `-c` with
   `strafe-q3-review-20260909/compare_continuity.py` after pointing it at the
   `-c` directories, and attribute any stall with the R7 `attribute_stalls.py`.
5. If `-c` has no candidate stall and no cell is worse, Q3 closes on `-c`
   and Q4 starts on the v4 build. If stalls remain, classify each from its
   telemetry and melee prints and put the gate decision to the owner with
   that evidence: per-collection zero stalls (current), a classified
   body-pressure rule, or pooled collections. Do not iterate a fifth
   navigation repair without a new traced mechanism.
6. Open plan decisions unchanged: no candidate/reference ratio threshold;
   timing measured failed (R8), repaired in Q4 after Q3 fixes behavior.
7. Process rules learned this session: never patch native sources while any
   campaign or batch is launching worlds (the `-a` baseline lost its third
   wave that way); every accelerated bench is minutes, only defended
   campaigns are hour-scale; retain invalid attempts and rerun with new
   output names.

**Q2 checkpoint (historical): `34049186` on local `master`.** Matched-resource
references: a strict `BOT2REFPATH1` loader, the planner, selection on phases
1/4/7 and 30/30 valid candidate/reference pairs on phases 0/3/6 with the
reference 50..900 ms faster on most 24..26 s legs and no ratio threshold
declared ([Q2 record](STRAFE_MOVEMENT_Q2_EXECUTION_2026-09-09.md)). The Q2
batches are the resource regression for any native change; they were rerun
and passed on the Q3 v2 build (`strafe-q3-resource-regression-20260910-v2`)
and must be rerun on v4 (the v4 gate chain does this first).

**Q1 checkpoint (historical): `f9bdd662` on local `master`** (the repository's
primary branch, not `main`). Implementation and evidence are committed there;
the Q1 handoff commit only updated documents. Read the
[Q1 record](STRAFE_MOVEMENT_Q1_EXECUTION_2026-09-09.md) first: bounded recovery
continuation now has native execution evidence under a pursuing body (8..10
`corner_continue` events per eight-case collection, most with displacement and
objective progress while the body stayed 30..66 units away); the route follower declines rejoins whose first
takeoff ahead needs strafe momentum the runner lacks and whose recorded flight
crosses a gap, drop or climb, after an instrumented world captured a low-speed
rejoin at route 19 step 84 flying into a platform face and the pit; the cost trace carries
`pre_falling,post_falling,owner`; the R8 death remains unclassified with a
documented candidate mechanism (mid-air body contact). Q1 verification status,
regressions and the sustained-pursuit confinement are in that record. Continue with Q2; do not reopen Q1 probes without a new hypothesis.

**First actions for the next session (Q2):**

1. `git status --short`; expect only untracked `.claude/`, `.codex-*`,
   `navmesh_crash.log`, `newmoduserdata`. Do not reset or install anything.
2. Read the Q1 record's *Status and next decision*, then the Q2 package below
   and the R7 record's reference design. Declare the loader format, selection
   rules and the 30 candidate/reference pairs in a plan before any native world.
3. The final Q1 build
   `.codex-research/strafe-build-q1-final-e8f1a2b3c4d54e6f7a8b9c0d1e2f3a49`
   matches `f9bdd662`; after any native change, build anew and freeze sources.
4. Carry two Q1 observations into Q3, not Q2: sustained pursuit by a body that
   always stands in front produces 6..13 s confinements in some runs, and ground
   fallback once walked 800 units to a dead end and stood 17 s when the route was
   unavailable (`strafe-q1-regression-20260909-c`, `blue_respawn_4`). Neither is
   a reason to rerun Q1 protocols.
5. Q1's launchers (`.codex-research/strafe-q1-shadow-final5-launch.py`,
   `strafe-q1-regression-launch-e.py`, the `plan-5.json` trace command) are the
   repeatable regression set for any later route or recovery change; run the
   26 temporary-body cases before believing any new follower refusal.

**R9 checkpoint (historical): `aabdb964`.** Read the
[R9 record](STRAFE_MOVEMENT_R9_EXECUTION_2026-09-09.md). Recovery scans now prepare
during the grace period without taking movement ownership; every false return
preserves the caller's destination. Admission revalidates current geometry.
Successful steps can continue along a fresh validated corridor, at most three
steps/six seconds, with recent-position rejection. Two blocked-corner cases
improve sampled escape by 750 ms; four former stalled matches have no distant
stall in their declared windows. No native `corner_continue` was observed.
Do not infer sustained-pressure qualification from mocked branch coverage or
repeat arbitrary static placements to obtain it. The native fall remains
unreproduced; unavoidable outcome classification and active timing workload
validity are separate. Full integrated gates, references and cost work remain.

## Next execution order after R9

This is the current work order. Earlier dated checkpoint instructions below
are historical context, not instructions to rerun their completed experiments.
Complete these packages through their decision gates; a small fixture pass is
progress, not permission to declare K07 qualified. Keep quality and execution
first, then optimize cost, then qualify the final integrated revision.

### Resume safely and bind evidence

1. Inspect `git status --short` and the relevant diff; preserve unrelated work.
   Read the plan, scorecard and Bot2 README before editing. Do not reset the
   checkout to a historical build or install anything into GameData.
2. The last measured build is the Q2 revision
   `.codex-research/strafe-build-q2-refpath-38e5638bd38c48ac9b04e210a79f5973`
   (1104 source inputs; hashes in its `*.sources.json` and the Q2 record). The
   Q1 final build `strafe-build-q1-final-e8f1a2b3c4d54e6f7a8b9c0d1e2f3a49`, the
   earlier Q1 builds and the R9 build
   `strafe-build-r9-recovery-691d9d112f0440e7bc89313574862f62` are historical. Reuse a build only while its hashes match the checkout. After
   native source changes, create a unique build and freeze sources during collection:

   ```powershell
   $handoffBuild = '.codex-research/strafe-build-' + [guid]::NewGuid().ToString('N')
   python tools/strafe_fixture_build.py --out $handoffBuild
   ```

3. Keep the current artifact unless evidence requires rebaking:
   `.codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt`.
   Candidate uses supported hop 2, corners 2, flight adapter and scoped guard.
   Runner flags differ by tool; inspect `--help` before freezing commands.
4. Run at most four isolated outcome worlds total across all tasks. Timing
   runs alone, without helper compilation, file scans or other agent work.
   Use existing hidden localhost runners. All R9 and Q1 worlds have exited; no
   old chain or background task needs resuming. Archive-only filesystem rules apply.

### Q1 — establish recovery execution and diagnose the fall

**Q1 status (executed 2026-09-09; see the [Q1 record](STRAFE_MOVEMENT_Q1_EXECUTION_2026-09-09.md)):**
the pursuing-body fixture (`bot_cornerShadowSpeed`, `--blocker-shadow-speed`)
produced native `corner_continue` execution, aborts on re-obstruction, clear
handbacks and step exhaustion; timeout, objective change during an episode and
airborne interruption remain mocked/R9 coverage. Sustained pursuit at running
speed varies between runs of the same case (0..2 confinements of up to 13.25 s,
no capture in the window): retained as a measured limit, not a Q3 stall
classification; a ground-fallback dead end seen while the route was unavailable
is noted for Q3. The fall work found and
corrected a route rejoin defect (rest rejoin at a 1170 u/s gap takeoff) and left
the R8 death unclassified with a documented candidate mechanism. A first momentum-only gate broke 17/26 temporary-body cases and three later variants two more each (all retained as failed collections; one exposed a ground-fallback dead end when the route is unavailable, noted for Q3); the catalog's own takeoffs are mostly 850..1173 u/s and complete from rest over flat floor, so the gate had to be geometry-aware, with floor samples rather than step indices deciding whether a rejoin crosses missing floor. Regression status: resources 30/30 and temporary-body 26/26 pass on the final revision `…e8f1…` (`.codex-research/strafe-q1-regression-20260909-e`), after four rejoin-gate variants that each failed the body suite were retained and replaced. The numbered instructions below are retained as
executed provenance; do not rerun them without a new hypothesis.

**Pressure implementation ownership:** `codemp/game/ai_bot2_localnav.c`,
`ai_bot2_movement.c` dispatch contracts, and
`tools/strafe_recovery_episode_check.py`. Keep subsystem policy in Bot2 files.

1. Use R9's input-bound evidence first:
   `.codex-research/strafe-r9-outcomes-fixed-20260909/pressure-audit.json`,
   `corner-progress.json`, and
   `.codex-research/strafe-r9-analysis-20260909/defended-continuation-review.json`.
   The four former-stall cases have no distant confinement; none executes
   `corner_continue`. The ten static probes also do not cover that branch.
   Do not repeat identical probes or expand an arbitrary static-placement grid.
2. Prepare a finite diagnostic protocol for a defender continuing to obstruct
   the runner at an arrived recovery step. First use available native pressure
   traces; if they cannot isolate that condition, add a dedicated/cheats-only
   fixture using ordinary native actor commands and collision. Declare initial
   setup, defender behavior, durations and case set before collecting. A fixture
   must not force recovery state, teleport each step, restore resources or remove
   the defender to manufacture escape. Extend telemetry only where needed to
   distinguish route clearance, body refusal, arrival, timeout and owner handoff.
3. Require actual `corner_continue` execution, subsequent displacement and
   useful objective travel while the defender remains present. Exercise clear
   route handback, a new obstruction, episode exhaustion, objective change and
   airborne/lifecycle interruption. Verify bounds, current-state validation,
   command/destination preservation on false handbacks, and recent-position
   rejection. A physically valid lateral/retreat step may temporarily worsen
   goal distance; it must not become repeated loops or knowingly unsafe motion.
4. If this representation fails twice, review why before another sweep. Prefer
   a bounded prepared continuation over restarting discovery or adding rescue
   coordinates. Do not extend the six-second/three-step limits simply to hide a
   stall. Native branch coverage alone is not the full defended gate in Q3.

**Fall diagnostic ownership:** `codemp/game/bot2_movecost.c`,
`tools/strafe_cost_runner.py`, `tools/strafe_cost_trace_audit.py` and their tests.
This work can be independently reviewed while pressure work progresses, but
coordinate edits and all source freezes/native world slots.

5. Keep the original death:
   `.codex-research/strafe-r8-cost-20260908/adapter-s4/`, client 1, level 28400,
   elapsed 14400, `MOD_FALLING`. The four R9 diagnostic trajectories in
   `.codex-research/strafe-r9-fall-traces-20260909/` are physically identical and
   survive; repeating them is not useful evidence. The fixture's declared +150 Y
   impulse at elapsed 2525 is relevant; there is no demonstrated rocket attack.
6. Add bounded diagnostic-only pre/post ownership and `fallingToDeath` state to
   the trace if needed, updating the CSV header, buffer-size checks and parser
   tests together. Keep old recordings readable. Correlate the last safe state,
   native impulse, issued command, takeoff, loss of support and pit-entry timer.
   These traces invalidate timing claims. A changed diagnostic case must have a
   specific hypothesis; do not randomly rerun startup phases until a death occurs.
7. If reproduced, test whether a feasible alternative existed before commitment.
   Correct a demonstrated controller/ownership defect; retain a demonstrated
   unavoidable displacement as an explained boundary. If still unreproduced,
   report it as unclassified and keep capture instrumentation ready rather than
   inventing a cause. Either way, a dead client cannot count as active timing work.

**Q1 deliverable:** code plus source-bound native execution/negative cases,
explicit fall classification or remaining uncertainty, and retained focused
regressions. Use unique outputs for `strafe_recovery_episode_check.py` and
`strafe_localnav_reentry_check.py`; run resource/body and affected route/lifecycle
checks after behavior changes. Do not label an unexecuted branch native-qualified.

### Q2 — implement independent matched-resource references

**Q2 status (executed 2026-09-09; see the [Q2 record](STRAFE_MOVEMENT_Q2_EXECUTION_2026-09-09.md)):**
items 1–4 below are implemented and measured. Loader `bot2_refpath.{c,h}` with
`bot_supportedReference` (mocked check `strafe-refpath-check-20260909-b`, 43
refusals); planner/selector `tools/strafe_reference_path.py` (proposals `-d`,
selection `strafe-q2-selection-20260909-a`, all alternatives qualified, chosen
`pull512`/`pull`/`pull`/`navmesh` for red 0/1, blue 0/1); runner, batch and
auditor extended; 30 pairs on the Q2 build pass every gate with 60/60 valid leg
measurements (`strafe-q2-evaluation-20260909-a/pair-report-b`). The policy gap
in item 4 is surfaced, not closed: no ratio threshold exists, and the auditor
reports `reference_ratio_policy: undeclared`. Declaring one is a plan decision.
The numbered items are retained as executed provenance.

The reference design in the R7 record was unimplemented before Q2; its old timing
deferral is superseded by R8. Existing replay/CHAIN2 and supported-only controls
are not substitutes for independently planned, resource-matched references.

1. Add a bounded raw-path loader at supported acquisition (`NavMesh_GetPath`
   boundary in `ai_bot2_supported.c`), with a Bot2-owned parser/module and CMake
   entry as needed. Bind map/physics, team/leg, initial condition identity,
   endpoints and input hashes. Strictly reject malformed/stale payloads.
2. Loaded XYZ knots must pass ordinary native preparation, support, sweep,
   headroom/corner checks and shared execution. Never load trusted clearance
   flags. Preserve production objective dispatch and native resources across
   pickup/return; bind initial resource identity once, not anew on return.
   An enabled reference refusal invalidates that reference measurement even
   if fallback later captures. Record reference ownership and every refusal.
3. Build an independent finite path proposal/selection tool using retained
   mode-3 geometry, map-preparation support inventory and mapbench joins.
   Backbone/connection tools supply proposals, not a finished optimizer.
   Declare shortcut/bevel alternatives and selection rules before development.
   Select on phases 1/4/7 for both teams and five resource conditions; freeze
   both legs before evaluation on phases 0/3/6. Candidate evaluation tapes must
   not choose the reference. Do not inject Speed or replay player state.
4. Extend the resource runner/auditor for all 30 candidate/reference pairs,
   checking actual initial pose, yaw, velocity, Force, Speed deadline, phase
   and physics. Retain 120-second observation, native pickup/first capture,
   travel idle/walking, lifecycle and fallback evidence. No separate numeric
   reference ratio is currently declared: surface that policy gap before making
   a ratio pass claim, rather than borrowing M1's threshold or inventing one.

### Q3 — establish quality on one integrated candidate

Status 2026-09-10: two collections failed the stall gate on the pressed-carrier
family; the owner directed pressed-carrier melee (`bot_carrierMelee`, staff
profile) as the candidate's response, declared with `--carrier-melee`; see the
Q3 record and the opening instruction. The text below is the package as
declared.

Freeze the candidate/configuration/case declarations before collecting.
Use `strafe_continuity_campaign.py freeze`, `run`, `audit` for **all 12 candidate
and 12 baseline** cases at original rates/seeds; candidate play arguments are
`--supported-corners 2 --flight-artifact <payload> --flight-adapter --forecast-guard`.
The baseline declaration uses its ordinary empty play arguments. Preserve
confirmed seeds and distinct tapes. Do not splice the four R9 development cases
into a new qualification or average percentages across runs.

Require no unexplained distant confinement of at least five seconds within 64 units, more
than 96 units from the goal; travel idle/walking must not regress in any team/rate
cell or pooled aggregate. Any explained confinement requires evidence-backed
classification; combat or congestion alone is insufficient justification.
Review larger moving loops and flag-state waits.
Enemy self-kill is not proof of escape. On failure, diagnose the named episode
and repair Q1; retain every original result rather than dropping a repetition.

Also retain M0, 26-start/eight-phase M1, full frozen M2, full-route quality,
30 resource pairs and lifecycle/facing/jump checks using the K07 tools table
below. The R7 chain is a command inventory, not a script to blindly rerun.
M2 needs valid declared starts/interventions: separate successful recollections
did not repair R7's original false audit. If event timing needs re-derivation,
follow the existing protocol, freeze from clean references before evaluation
and retain both suites. Distinguish M0's retained pickup/ownership/time outcomes
from its legacy raw exact-trace false result; never report that raw flag as true.

### Q4 — fit the successful behavior within the timing budget

Timing is **measured failed, not deferred**. The owner closed ComfyUI and
explicitly resumed collection. Evidence:
`.codex-research/strafe-r8-cost-20260908/qualification.json`; adapter-s3's worst
frame is six forecasts/226 steps, 5.3558 ms forecast, 5.5224 ms total movement.
No cold file load occurs on that frame. Adapter-s4 is invalid after the fall.

Start with `ai_bot2_flight.c` preparation and `bot2_forecast.c` accounting/guard
breakdown. Eliminate demonstrated duplicate work or improve bounded selection
without discarding validation. If work moves across frames, preserve current-root,
issued-command, publication, resource and world bindings and explicit latency
contracts; a stale forecast cannot authorize movement. A smaller rollout cap
is a behavior change requiring quality evaluation, not a free timing fix.

After changes, repeat the declared isolated four eight-bot samples, control and
sixteen-bot stress from the R8 cost `plan.json`, using new paths/ports and the
new build. Tracing/rendezvous/holds stay off for cost claims. Require all eight
clients alive and dispatched, complete movement accounting, p95 <=2.5 ms and
max <=5 ms; report server-frame cost and stress/activity separately. Retain
failures and invalid samples. Passing selected favorable samples is insufficient.

### Q5 — final freeze and qualification handoff

Freeze one final source revision, build, artifact, configuration and declared
cases. Run the applicable K07 table below on that revision, including quality
checks affected by timing changes. Do not assemble a pass from different native
revisions or call previously used phases a fresh holdout. Produce one evidence
manifest binding sources/binaries/settings/inputs/audits, list every failed or
censored case, and update the scorecard with each gate's result. Stop repeating
checks when required validation passes and no new concern warrants it.

K07 closes only when all applicable gates pass. Installation/Create Game smoke
is K08 and needs its separate recoverable deployment scope; map transfer and
aiming remain later packages. Do not install or push as an incidental test step.

### Historical R8/R7 checkpoint notes

The following checkpoint prose is retained for provenance. Its superseded
next-action language does not override Q1–Q5 above.

**R8 continuation:** read the
[R8 record](STRAFE_MOVEMENT_R8_EXECUTION_2026-09-08.md) before repeating R7 work.
Ground-floor refusals now report native hit identity and trace fields; opponent
contacts are observed, but the four diagnostic cases do not reproduce distant
confinement. Local navigation retains refusal/exclusion/side history across
compatible grounded handoffs up to 3 s and 32 units, refreshing all geometry
and commands. Native dispatch proves immediate sidestep selection after two
such gaps. The separate recovery arrival delay remains unchanged. Full defended
qualification is still open; the record distinguishes focused R8 checks from
the last complete integrated R7 collection below.

Timing has since resumed at the owner's request with ComfyUI closed. The R8
record's six sequential cost samples include a 5.5224 ms eight-bot failure
(six-forecast preparation) and an invalid sample after a native fall. Do not
continue the earlier timing deferral or assume host contention closes the gate.
Preserve actual-root validation when reducing/scheduling preparation work;
no source performance change was made during this recollection.

**Historical checkpoint (R7):** the handoff `4414eddc` and implementation
`1745b4d3` are on local `master`. The
[R7 execution record](STRAFE_MOVEMENT_R7_EXECUTION_2026-09-08.md) owns the
completed qualification and targeted traces. The original isolated supported
wait did not reproduce. Explicit
pre-actor seeds and duplicate-tape checks are implemented and tested. Grounded
supported preparation/prefix waits now expire at 3 s; final arrival permits
250 ms for native objective publication before fallback. Adapter-owned and
airborne commands retain ownership.

The new frozen build is
`.codex-research/strafe-build-r7-waits-a4a18cf49d5e4abe97faf39a578bd1da`.
Pure checks and direct ownership/seed harnesses pass; ten admissible corner
cases finish without death and every carrying case captures. The two old blue
solid-pose refusals are retained alongside the handoff's replacement poses.
The 12-case M2 reproduction matches R6 exactly (nine passes, two known deaths,
one preempted/invalid impulse). The full chain completed at 13:39 UTC:
`.codex-research/strafe-r7-chain-20260908/chain.sh`. All 36 defended cases are
valid with confirmed seeds and distinct tapes. Candidate idle improves in every
cell; red 1x walking regresses 0.2920 points. Candidate and baseline each retain
four distant stalls. The scorecard owns the complete regression and ablation
results. Three invalid M2 startups have a valid separate four-offset recollection;
the original full audit remains unchanged and false. No timing arms ran.

The four follow-up traces in
`.codex-research/strafe-r7-pressure-trace-20260908/` are also complete. They prove
one native 250 ms endpoint expiry within the declared window and retain an
eight-second blue pressure stall with `floor_startsolid`/preparation refusals.
The opposing bot's native self-kill removes the pressure before useful travel
resumes; do not call that autonomous escape. Hit entity/contents are absent
from the trace, so discriminate body versus world refusal before selecting a
controller correction. Keep floor safety, airborne ownership and native resources.
The R7 record also specifies the missing independent planned-reference loader,
planner and paired resource collection. Do not repeat completed chains or seed
experiments to reconstruct context. K07, resource references and final timing
qualification remain open; normal MP is unchanged.

### Historical R6 handoff (superseded next-session notes, 2026-09-08)

Facts to build against, all measured on `strafe-build-r6-freeze-20260908b`:

- The committed tree at this commit is the freeze-2 source; `strafe_fixture_build.py`
  into a fresh directory reproduces it (the build hashes 1,102 sources).
  Pure checks (`strafe_flight_adapter_check`, `strafe_forecast_guard_check`,
  `strafe_movecontrol_check`, `strafe_flight_check`) and the Python suites
  pass; run the suites from the repo root with `PYTHONPATH=tools python -m
  unittest tools.test_<name>`, not from `tools/`.
- Localnav's continuation policy is trace-gated: `--supported-trace` (or
  `bot_supportedTrace 1`) prints `walk_refused`, `walk_stuck`,
  `waypoint_stale`, `waypoint_none`; the route follower prints
  `jump_reserve_low` once per low-Force jump and no longer releases.
- The corner fixture refuses a pose at the exact telemetry origin as solid;
  place it one unit above the floor (z+1). A static body proxy 30 units away
  embeds the runner; 36 units reproduces a pressed body.
- The frozen M2 v2 suite's blue_respawn_5 approach impulses fire at 9,800 ms;
  this controller reaches the flag at 9,775 ms, so those eight cases are
  invalid on any faster build. The v3 suite
  (`.codex-research/strafe-m2-v3-frozen-suite-20260908b`) was frozen from
  the freeze-2 M1 traces with `strafe_disturbance_suite.py --references
  <m1-qualification>/*/summary.json`; its audit uses the freeze-2 M0/M1
  outputs and the same build as `--m1-build`.
- The continuity campaign's red 1x r1 and r3 produced byte-identical runs in
  the candidate and baseline freeze-2 arms (the corners-only arm is distinct) (`strafe-r6c-walking-attribution-20260908-v1/reps.py`
  shows csv hashes). Check the per-case ports/seeds in
  `tools/strafe_continuity_campaign.py` before the next collection.

First actions, in order:

1. Traced defended blue 1x collection for the supported-owner wait (blue
   1x r3, 217,450 ms in `strafe-r6-continuity-candidate-20260908b`): three
   runs with the candidate play arguments plus `--supported-trace`, e.g.
   `python tools/strafe_play_smoke.py --build <freeze> --out <fresh> --team
   blue --enemies 2 --timescale 1 --game-seconds 600 --port <p>
   --supported-corners 2 --flight-artifact <payload> --flight-adapter
   --forecast-guard --supported-trace`, then `strafe_opposition_audit.py`
   on the directories and read `[SUPPORTEDTRACE]` around any distant
   episode. Name the wait's reason, then give supported the same explicit
   reason/expiry/fallback contract localnav has.
2. Fix the duplicated red 1x repetition before any re-collection.
3. After any C change: fresh build, the pure checks, the corner matrix
   (`strafe-r6b-corner-matrix-20260908-v1/run_matrix.sh <build> <out>` plus
   the two blue cases at `-16 -1485 1913`), the M2 subset reproduction, then
   the chain with a new stamp. No timing arms while ComfyUI runs.

### September 7 handoff, retained context

> Continue from the September 7 R4 lifecycle campaign. K01–K03 passed
> offline; R1–R3 of the
> [runtime work order](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md) met K05's
> gate on a frozen holdout; R4 (K06) is implemented and measured natively:
> every constructed lifecycle case (impulse, body, disable, Speed, death,
> ownership reset) has a useful continuation or an explicit hand-off, the
> doomed-fall boundary is retained and explained, and the one-slot schedule
> is deferred-first. See the
> [lifecycle protocol](STRAFE_MOVEMENT_LIFECYCLE_NATIVE_2026-09-07.md).
> **R5 (K07) is measured and not complete** - see the
> [K07 protocol](STRAFE_MOVEMENT_K07_QUALIFICATION_2026-09-07.md). On one
> frozen build the full-route runtime gate, the 30-case supported matrix with
> the adapter armed and the lifecycle regression pass, and native same-frame
> contention was finally constructed (`bot_flightRendezvous`). Two timing
> results fail on paper - the 5 ms cost maximum on two of four samples and a
> 71 ms sixteen-bot first-use load frame - and are **attributed to ComfyUI
> host contention (owner's call, 2026-09-08): probably fine, not adapter
> defects, not R6 work**; the host stays busy, so do not try to recollect
> them. What really fails is behaviour: the defended continuity comparison
> (walking worse in three cells; stand-entry parking stalls in both arms, one
> unexplained stop) and the cached-route M2 regression (739/780 against
> 758/780). **Execute R6 next**: decide the stand-entry parking and walking
> against the continuity rules, explain or fix the unexplained stop, establish
> whether the M2 shortfall is dispatch or variance, then freeze a new build
> and repeat the complete K07 collection on it; never mix arms across freezes. Run adapter collections
> with `--forecast-guard` and the `…-r3-` artifact named in the scorecard.
> Do not implement endpoint-only guard batching, treat 25 ms as the movement
> budget, excuse dead-client workload loss as hidden flight dispatch, or
> promote the opt-in configuration. Preserve all failures, the frozen
> cached-route reference and the installed baseline. Keep aiming paused.

### First actions

The current native source reference is the K07 freeze
`.codex-research/strafe-build-k07-freeze-20260907` (1,102 sources; every K07
arm is bound to it). It differs from the R4 contention build
(`strafe-build-r4-lifecycle-a5a81bcd…`) only by the rendezvous fixture in
`ai_bot2_flight.c`. The R4, R3 (`strafe-build-r3-oracle-7851ea40…`), R2 and R1
builds and the review's `strafe-build-flight-audit-20260907` are retained. The reviewed captures are
already complete. Do not relaunch K01–K06, rebake, or run every historical
test to reconstruct context.

Read the September 8 review's R6 packets, then the K07 section below, then the K06
section's R4 record for the contracts the integrated revision must keep. The
adapter's lifecycle paths are `Handoff`, `BeginRecover`, `Recover`,
`Bot2_FlightAdapterBeforePmove` (owner-lost and admission) and
`ClaimPreparation` in `ai_bot2_flight.c`. The raw review and reproducible
calculation are retained at `.codex-research/strafe-direction-review-20260907-v1/`.
Focused tests and fresh builds follow actual code changes. Old native
directions below are reproduction history where marked complete.

Read [the Bot2 README](../codemp/game/ai_bot2_README.md) before C changes. Read
dated protocols only for the package being executed. Do not load the entire
historical scorecard, rescan UnrealJKA, rerun successful evidence, or launch a
new research branch just to reconstruct context.

This handoff is local: builds, captures and snapshots under `.codex-research/`
are retained here and excluded from broad searches. They may not exist in a
fresh clone. If moving machines, copy and hash-bind the required artifacts or
reconstruct them from the declared protocols; missing local evidence is not a
pass. No commit, clean checkout or installed research DLL should be assumed.

## Map of the implementation

| Area | Existing files and role |
|---|---|
| Production ownership | `ai_bot2.c`, `ai_bot2_objective.c`, `ai_bot2_movement.c`, `ai_bot2_supported.c`, `ai_bot2_localnav.c`, `ai_bot2_route.c` under `codemp/game/` |
| Shared control | `bot2_movecontrol.c/.h`, `bot2_hop.c/.h`, `bot2_path.c/.h`; cached-route reference `bot2_strafe.c/.h` |
| New pure motion | `bot2_flight.c/.h`: coupled run-up, prepared takeoff, two flights and landing ranges; `bot2_receiver.c/.h`: moving acceptance and same-path resume |
| Input format | `bot2_windowseed.c/.h`: team/leg/guides plus optional complete real legacy transition; CMake already includes it |
| Research caller | `bot2_connectionbench.c/.h`: actual-root native discovery, prefixes, 17-candidate comparison, one-step parity, CHAIN1/CHAIN2 lifecycle |
| Native oracle | `bot2_forecast.c/.h`, `bot2_movebench.c/.h`: guarded native Pmove and captured state; static-world restrictions remain |
| Preparation/proposals | `tools/strafe_window_inventory.py`, `strafe_window_seed_runner.py`, `strafe_window_seed_chain.py` |
| Collection | `tools/strafe_continuous_runner.py`, `strafe_window_seed_chain_campaign.py`, `strafe_flight_native_campaign.py`, `strafe_cost_runner.py` |
| Evidence | `tools/strafe_connection_report.py`, `strafe_window_seed_report.py`, `strafe_window_seed_chain_report.py`, `strafe_supported_report.py`, `strafe_connection_speed_gate.py` |
| Earlier runtime/recovery research | `bot2_maneuver_library.c/.h`, `ai_bot2_plan.c`, `tools/strafe_runtime_forecast.py`; inspect measured scope before reuse |
| Play delivery | `tools/install_kejim_runner.ps1`, `tools/kejim_defense_play.py`, `bot2_svcmds.c`, [play guide](KEJIM_RUNNER_PLAY.md) |

Paths in the first rows are relative to `codemp/game/`; helper paths are relative
to `tools/`. The loader, plan construction and flight adapter now exist; new
reporter names in the runtime review are proposals. Prefer Bot2-owned files
and small upstream hooks. Before altering any
`bot2_state_t` meaning, inspect every consumer; supported/route controllers also
have separate per-client state.

### Contracts that must survive

- Seeds propose geometry; they do not certify native support. The original 25
  adapters retain their full legacy polygons and heights. Other seeds must not
  acquire fabricated point polygons. Candidate IDs 0–7 are flight, 8–15 point,
  16 ordinary. Inapplicable points retain explicit refusal evidence after the
  common immutable first command.
- Native ground-only prefixes prepare two supported takeoff states per speed
  variant, outside pure control callbacks. Actual position/velocity/contact and
  released jump latch must match; unavailable preparation refuses atomically.
  The first common command, 17 comparisons and common receiver stay bound.
- Receiver acceptance includes finite support, momentum, heading and the next
  native command bound. Native 24/32 ms Pmove durations differ from the 25 ms
  game tick; use the existing cadence, never a guessed fixed air step.
- Whole-PS one-step prediction is the oracle. Native force regeneration,
  triggers and animation between commands are not a reason to demand an
  identical multi-second full-state tape or to reset real state.
- CHAIN2 owns leg lifecycle across stage resets. It waits for pickup/return
  publication, records missed/retired stages, drains pending prediction and
  resumed-step records before advancing, and preserves actual owner state.
  CHAIN1 retains its earlier pickup-stop behavior. A mocked branch is not a
  native tested branch.
- All declared cases survive into reports, including NO_ROOT, binding refusal,
  setup/evidence failures and geometry refusal. Valid refusal evidence does
  not mean useful movement. Do not loosen gates to make the inventory green.

## K01 — qualify native CHAIN2 lifecycle

**Status: COMPLETE, 2026-09-07,** with one retained native gap. All six focus
cases are valid; leg ownership follows actual native pickup, a return-only
schedule waited about 29 game seconds through outbound travel before rooting, a
miss did not poison the later stage, and every joined stage reported exact
one-step parity. Evidence, native log excerpts and the auditor correction this
package uncovered are in
[the CHAIN2 campaign protocol](STRAFE_MOVEMENT_CHAIN2_CAMPAIGN_2026-09-07.md).

**Retained gap:** the chosen `NO_ROOT` case was retired by passed-entry expiry
about 19 game seconds before its pickup, so the native pickup-skip branch was
never exercised. Close it before K07 with a minimal lifecycle test and a declared
intervention; do not alter seed coordinates or invent a pose.

The procedure below is retained as the reusable recipe for a repeat or a new
focus. Native lifecycle defects require careful control-flow reasoning, not a
parameter sweep.

Run from repository root in PowerShell. First inspect the working diff and
confirm no other native collection/timing run owns the source freeze or ports.
Use these exact retained inputs:

```powershell
$movementBuild = '.codex-research/strafe-build-window-seed-v2-20260907'
$movementFocus = '.codex-research/strafe-chain2-focus-v1-20260907/campaign'
$movementProposals = '.codex-research/strafe-window-seed-proposals-v1-20260907/proposals.json'
```

Before launching, validate both the frozen campaign and native source hashes:

```powershell
@'
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,'tools')
from strafe_window_seed_chain_campaign import validate_frozen
p=validate_frozen('.codex-research/strafe-chain2-focus-v1-20260907/campaign')
m=json.loads(Path(p['build_manifest']['path']).read_text())
bad=[name for name,h in m['sources'].items()
     if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=h]
assert not bad,bad
assert len(p['cases'])==6
print('Frozen six-case focus and native sources match; nothing launched.')
'@ | python -B -
```

The plan binds six phase-0 runs, ports 31300–31305, at most four worlds:
red/blue mixed legs, red return-only waiting, blue retained NO_ROOT followed by
return, and red/blue inactive controls. Proposals and declaration script are
under `.codex-research/strafe-chain2-focus-v1-20260907/` and
`.codex-research/strafe-chain2-focus-declaration-v1-20260907/prepare_focus.py`.

```powershell
python -B tools/strafe_window_seed_chain_campaign.py --action run --out $movementFocus
```

Keep C, headers, CMake, runner and frozen helpers unchanged through collection
and audit. `run` refuses existing output: inspect actual state instead of
implicitly restarting. After all six processes finish, create the resource
audit using the following **chain-campaign** adapter. The existing resource
CLI expects `cases.json`; CHAIN2 uses `plan.json`, so do not invoke that CLI
unchanged or invent a cases file in the frozen declaration.

```powershell
$movementCampaign = $movementFocus
@'
import json,sys
from pathlib import Path
sys.path.insert(0,'tools')
from strafe_supported_report import analyze
root=Path(sys.argv[1]);plan=json.loads((root/'plan.json').read_text())
names=[c['name'] for c in plan['cases']]
assert names and len(names)==len(set(names))
trials={}
for name in names:
    try: trials[name]=analyze(root/name)
    except (OSError,ValueError,KeyError,IndexError) as e:
        trials[name]=dict(gates=dict(complete_observation=False),error=repr(e))
passed=all(bool(t.get('gates')) and all(t['gates'].values()) for t in trials.values())
with (root/'resource-audit.json').open('x') as f:
    json.dump(dict(passed=passed,trials=trials),f,indent=2)
print(json.dumps(dict(passed=passed,cases=len(trials))))
'@ | python -B - $movementCampaign
python -B tools/strafe_window_seed_chain_report.py --campaign $movementCampaign --out "$movementCampaign/chain-report-v1.json"
```

**Done when:** all six observations/resource audits and declared stage evidence
are valid; actual leg ownership, pickup/return order, no reset, and exact
selected/resumed one-step parity are demonstrated. Read outcomes as well as
top-level validity. Confirm return-only waits during outbound travel, and a
miss does not poison later stages. Compare inactive traces against the retained
same-condition controls; preserve CHAIN1 regression evidence.

The chosen NO_ROOT case might be retired by passed-entry expiry before pickup.
That does not establish the native pickup-skip branch. If no natural case
exercises it, retain the gap and declare a minimal lifecycle-focused test with
an explicit intervention before claiming it covered. Do not invent a pose or
alter the existing seed coordinates to conceal the missing coverage.
The six-case focus can pass without covering this branch; that is a scoped
focus pass, not complete lifecycle qualification. Close retained native branch
gaps before K07 accepts the integrated scheduler.

**On failure:** retain captures and audit output; separate a caller defect,
missing native branch and an auditor/schema defect. Fix only the responsible
layer. A C change requires a new unique build and fresh declaration; do not
overwrite the old focus. Audit/source-snapshot the old collection first.
Corrected auditors write new reports, never overwrite failed evidence. The
first native CHAIN2 audit is itself unproven integration, despite pure tests.

Useful focused tests after relevant edits:

```powershell
python -B -m unittest discover -s tools -p 'test_strafe_window*.py'
python -B -m unittest discover -s tools -p 'test_strafe_connection_report.py'
```

For native/parser edits, the pure harnesses `strafe_seed_chain_check.py`,
`strafe_windowseed_check.py` and `strafe_flight_check.py` accept a fresh `--out`.
Use the relevant harness and fresh native build; existing passes need not be
repeated for documentation changes.

## K02 — measure complete successor schedules

**Status: COMPLETE, 2026-09-07.** 23/23 cases valid on the first audit; 123 of
126 stages `LIVE_COMPLETE`; 19 of 21 schedules completed all six stages. Best
complete first trips: red `red-window-primary` 49500 ms versus a 57225 ms control
(13.50%), blue `blue-window-primary` 49775 ms versus 57050 ms (12.75%). Sources
and helpers are snapshotted in the collection directory. See
[the campaign protocol](STRAFE_MOVEMENT_CHAIN2_CAMPAIGN_2026-09-07.md) and the
scorecard for exact paths. The recipe below is retained for a repeat.

**Dependency:** K01 native lifecycle integrity. Do not begin by changing the
flight policy. Use all 21 generated schedules from the valid 88-seed report;
each has six stages. Freeze all 23 phase-0 runs, including two inactive controls:

```powershell
$movementDevelopment = '.codex-research/strafe-chain2-development-' + [guid]::NewGuid().ToString('N')
python -B tools/strafe_window_seed_chain_campaign.py --action freeze --proposals $movementProposals --build $movementBuild --out $movementDevelopment --first-port 31400 --workers 4
python -B tools/strafe_window_seed_chain_campaign.py --action run --out $movementDevelopment
```

Check ports are available and update the first port before freeze if needed.
Use K01's resource adapter with `$movementCampaign = $movementDevelopment`,
then the CHAIN2 campaign auditor. Keep every schedule, failed stage and control.
The generator's weighted interval scheduling is only a proposal ranking; later
roots use actual earlier motion, not the isolated root PS. Verify useful return
stages actually run. Diagnose missed entries and downstream changes explicitly.

**Done when:** all 23 cases are accounted for and valid, with complete first-trip
times, idle/walking, stage outcomes and exact movement/handoff evidence.
Snapshot the measured native sources and helpers. The current v2 source snapshot
already preserves 1,094 native inputs; record any new helper versions too.

## K03 — freeze selection and evaluate the unchanged gate

**Status: COMPLETE and PASSED, 2026-09-07.** This is the first time the plan's
full-route movement quality gate has been met.

| Team | Baseline median | Candidate median | Improvement | Gate |
|---|---|---|---|---|
| red | 57275 ms | 51125 ms | 10.74% | >=10% |
| blue | 57050 ms | 49800 ms | 12.71% | >=10% |

`all_pairs_not_slower`, `all_idle_not_worse` and `all_walking_not_worse` are all
true, so the complete locomotion gate is met — not only the tool's time-only
`passed` field. Selected schedules were frozen before collection:
red `red-window-primary`, blue `blue-window-primary`. Per-pair figures are in
[the campaign protocol](STRAFE_MOVEMENT_CHAIN2_CAMPAIGN_2026-09-07.md).

`tools/strafe_window_seed_select.py` now exists and implements the contract
below, with `tools/test_strafe_window_seed_select.py` covering refusal handling,
the inactive-fallback path, incomplete trips and reordered valid inputs. The
remainder of this section is the contract it satisfies, retained for reuse.

Reuse existing binding helpers and the schema example
`.codex-research/strafe-takeoff-chain-v1-20260907/selection.json`.

The helper must:

1. Require valid full K02 resource/chain evidence and its exact 23-case plan.
2. Rank **complete first-trip time** per team, including inactive baseline;
   retain all ranks and use a declared deterministic tie rule.
3. Write schema `strafe-chain-development-selection-v1`, `selected`, `ranked`,
   and hash bindings named `resource_audit`, `development_plan`, `proposals`,
   `chain_audit`. Copy the unchanged `next_gate` contract from the example.
4. Reject incomplete/modified evidence and duplicate/missing controls. If
   inactive fallback wins a team, report the failed improvement decision and
   return to K04; do not fabricate a schedule to fill the 12-case gate.

Test failure handling and selection from differently ordered valid inputs.
Do not select again after seeing phase-gate results. Freeze the output before
collection. With both selected schedule names in variables:

```powershell
$movementPhaseGate = '.codex-research/strafe-chain2-phasegate-' + [guid]::NewGuid().ToString('N')
# Set $movementSelectionPath to the new frozen selection.json, then:
$movementSelection = Get-Content -LiteralPath $movementSelectionPath -Raw | ConvertFrom-Json
$movementRed = $movementSelection.selected.red
$movementBlue = $movementSelection.selected.blue
python -B tools/strafe_window_seed_chain_campaign.py --action freeze --proposals $movementProposals --build $movementBuild --out $movementPhaseGate --schedule $movementRed --schedule $movementBlue --phases 0 3 6 --first-port 31500 --workers 4
python -B tools/strafe_window_seed_chain_campaign.py --action run --out $movementPhaseGate
```

Run K01's resource adapter and campaign audit for this exact 12-case directory,
then:

```powershell
python -B tools/strafe_connection_speed_gate.py --campaign $movementPhaseGate --selection $movementSelectionPath --out "$movementPhaseGate/speed-gate.json"
```

**Done when:** evidence is valid, all pairs are no slower, both median speed
improvements reach 10%, and both `all_idle_not_worse` and
`all_walking_not_worse` are true. The tool's `passed`/exit status alone covers
time, not all locomotion gates. Preserve all misses and full 120-second runs.

Tooling caveat: the speed tool currently reads canonical `chain-report-v1.json`
and `resource-audit.json`. If a corrected re-audit needs a new filename, add
explicit report-path inputs with unchanged binding/gate checks instead of
overwriting failed reports. Do not silently substitute selected evidence.

## K04 — conditional general controller/planning correction

**Status: NOT TRIGGERED, 2026-09-07.** K03 passed every gate, so there is no
measured K02/K03 blocker to resolve. The retained receiving-state hypothesis
below was suspected to be the dominant full-trip bottleneck; K03 passed without
touching it, so that suspicion is not confirmed. Do not open this package
speculatively. Reopen it only if K05 or K06 measurement shows a controller or
planning defect actually blocking delivery, and name that measurement first.

**Dependency:** a measured K02/K03 blocker, not merely unfinished research.
This package requires problem solving; do not promise another AI it is a fixed
mechanical recipe. Diagnose first-trip lost time and missed joins from existing
captures before adding instrumentation or changing control.

The retained hypothesis is receiving-state viability through the remainder of
a native command. First-land overshoots are 0.038–6.010 units; later contacts
retain lateral momentum that can leave support. See the scorecard's diagnosis.
Investigate position **and velocity** at the next observable command boundary,
including time after touchdown, the next ordinary jump and receiver braking.
Keep legal native cadence and normal jump pulses. Do not widen acceptance,
grant resources, change physics or select a later falling takeoff to earn a pass.

Likely owners: `bot2_flight.c/.h`, native preparation in `bot2_connectionbench.c`,
and `bot2_receiver.c` only if its general contract is deficient. Preserve old
pure modes, rotated/sloped/control-seam tests and exact legacy point inputs.
Add only counterexample-driven tests, declare the next native case set, then
use a fresh build. Do not silently drop the earlier 25/40 or expanded 88 cases.

If the dominant full-trip issue is instead missing compatible windows or
successor association, improve generated coverage/graph composition using
actual exit state. Do not tune air feedback to repair a schedule-selection bug.
After two failed iterations on the same representation, write the evidence and
review/replace the representation before another sweep. Re-run K02/K03 for a
changed candidate, with an independent evaluation after tuning as required.

## K05 — prepared runtime execution (gate met on a frozen holdout)

The baked31-window artifact, pure loader, per-client window construction and
opt-in production adapter exist. Reuse them. The current source is bound to
`strafe-build-flight-audit-20260907`; both artifact/adapter controls remain off
by default and the installed game is unchanged.

Keep the [artifact implementation constraints](STRAFE_MOVEMENT_FLIGHT_ARTIFACT_2026-09-07.md)
and [native experiment history](STRAFE_MOVEMENT_FLIGHT_NATIVE_2026-09-07.md) as
references. Previous detailed K05 handoff text is preserved in the
[review archive](../.codex-archive/20260907T194324685Z/docs/STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md).
Its guard batching and cost qualification claims are superseded.

Current evidence: 79 entered windows, 53 resumed exits, six adapter runs with
two captures each and no deaths. Runtime first-trip improvements are 4.67% red/
5.35% blue, with idle worse in five pairs; this is not the 10%/idle gate.
Preparation timing is outside the old dispatch timer, and measured forecast
work alone exceeds the 5 ms maximum. Missing dispatches coincide with lost
living clients. K05 is therefore still open.

R1–R3 from the [runtime review](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md)
are complete; R4 supplies the native K06 cases next.

**R3 is complete.** Contracts now in place:

- Every preparation rollout starts from this frame's already-issued command
  (`request->firstCommand=&move->cmd`) with the policy clocked at this frame,
  so rollout step i is actual frame level.time+i·step and the policy first
  speaks on step 1 exactly as it will live. A boundary whose command does not
  carry the fixed-step time is refused visibly (`issued_command_refused`).
  `bot_flightBoundarySkew` (default 2) is a declared override, not a sweep.
- A grounded refusal after the first landing tries `Bot2_ReceiverResume` on
  the receiving interval before handing the follower back; success is
  `reason=refused_resumed`. Every refusal prints a `[FLIGHT] refusal` line
  with phase, ground, speed, run-up and landing coordinates; every refused
  rollout prints its terminal phase and landing geometry.
- `bot_flightExhaustive 2` rolls out every unlisted flight id at each actual
  preparation root as `oracle_complete`/`oracle_refused` evidence, never
  selecting from it. `tools/strafe_flight_artifact.py --runtime-oracle <that
  campaign>` reproduces the measured payload byte-for-byte, then re-ranks
  windows met at three or more roots by runtime completion (full-anchor ids
  first by measured frames, truncated at the first anchor; a window with no
  full anchor keeps its best ids and is flagged `runtime_anchor=false`).
  Bindings and the per-window matrix are recorded under `runtime_rerank`.
- The audit classifies an entry still open within 3 s of the capture end as
  `truncated_by_capture_end`, reported separately from failures.

Measured: the development cases went from 53/79 complete, 4.67%/5.35% to
72/78 and 8.0%/9.1% with the latency model alone, then 90/90 and 11.70%/10.60%
with the re-baked artifact; the frozen holdout (phases 1/4/7, run once) gives
85/87 complete (2 truncated), 10.27%/11.86%, idle and walking better in every
pair, no deaths, every frozen predicate and the full-route gate passing. Cost
with the re-baked artifact: complete movement p95 0.314 / max 3.889 ms, with a
six-forecast preparation at 3.72 ms.

Remaining limits: the qualified configuration is opt-in (`bot_flightAdapter 1`,
`bot_forecastGuard 1`, the `…-r3-` artifact) and nothing is installed; window
`red:f06:v1` has no runtime anchor; one holdout pair (red p4) is 7.8% while
its team median passes; K06 recovery/resource cases and K07 integrated
qualification are still open.

**R2 is complete.** Contracts now in place:

- `Bot2_ForecastGuardBegin(root,&guard)` / `Bot2_ForecastGuardEnd(&guard)` in
  `bot2_forecast.h`: one immutable BSS baseline of every entity, every present
  client and the level struct, compared byte-for-byte after each rollout. A
  difference refuses that rollout (`worldChanged`) and every later rollout in
  the scope before any callback; End reports `contaminated`. Scopes are
  synchronous, single-root and non-reentrant: Begin refuses while a rollout
  runs or a scope is open, a foreign root is a bad argument that executes
  nothing, and every early return leaves the scope closeable. Detection is
  not rollback. Outside a scope the legacy hash path is untouched.
- The adapter opens one scope per admitted preparation when `bot_forecastGuard`
  is nonzero, closes it on every path, publishes no READY state before a
  clean close, and reports `guard=… guard_runs=… snapshot_us=… compare_us=…
  guard_bytes=…` on its `prepare_cost` line. The snapshot is added to the
  reported `us` and to the cost fixture's `forecast_us`.
- `bot2_movecontrol.c` computes each key's admissible angle arc once per step
  and evaluates exactly the same candidates in the same order; the harness
  proves bitwise-identical commands, yaw and work against the unpruned scan.
  All controllers benefit, which is why dispatch p95 also fell.
- `bot_flightExhaustive` (diagnostic) rolls out both prefixes and every ranked
  candidate without changing selection; `bot_flightRolloutCap` (bounded work)
  stops after N candidate rollouts and reports `skipped=`. Runners:
  `--forecast-guard`, `--flight-exhaustive`, `--rollout-cap N`.

Measured on the current sources (eight bots, timing alone, same build): scoped
guard complete movement p95 0.282 ms / max 3.379 ms and exhaustive max 3.296
ms, both `eight_bot_budget_pass`; legacy guard max 8.718 ms, fails; control
max 0.673 ms. Every measured window offered one candidate with a valid takeoff,
so the four-rollout worst case remains a bound (about 4 ms per 256-step
rollout) enforced by the cap, not a measurement. A larger sample and the
sixteen-bot stress belong to K07.

**R1 is complete.** Contracts now in place:

- `bot2_movecost.c` writes `accounting=complete_movement_v2`; `movement_us` is
  `dispatch_us+prepare_us`, with `forecast_us` the oracle-measured subpart and
  `prepare_calls/prepare_admitted/prepare_deferred/library_loads/
  prepare_refused_free` per frame. `Bot2_MoveBenchPmove` times the whole
  `Bot2_FlightAdapterBeforePmove` call and charges it through
  `Bot2_MoveCostPreparation`; the adapter reports rollout time through
  `Bot2_MoveCostAdd`, activity through `Bot2_MoveCostCount`, and prints through
  `Bot2_MoveCostPrint`, which buffers while a capture is active. Flight command
  execution stays inside dispatch and is not counted twice.
- `tools/strafe_cost_report.py` lists `cost_qualification_blockers`; a capture
  without the declaration is `dispatch_only` and cannot pass, a preparation the
  adapter reported but its frame row did not charge fails
  `preparation_charged`, and more than one admission on a frame fails
  `schedule_held`. `tools/test_strafe_cost_report.py` holds the negatives.
- `tools/strafe_flight_native_report.py --campaign <frozen dir> --out <fresh dir>`
  audits a campaign after `strafe_flight_native_campaign.py` ran it: plan hash,
  gate and inventory unchanged, artifact/build/catalog bindings intact, every
  case's capture hash, module, settings, clocks and recomputed counts, then the
  frozen predicates one by one plus the full-route 10%/idle/walking gate,
  `incomplete_entries` with reasons, thin-window entries, and preparation costs
  flagged as scale only. Exit 2 means at least one failed input case.
  `tools/test_strafe_flight_native_report.py` holds the negatives.

Measured with complete accounting on the current build (adapter arm, eight
bots, timing alone): complete movement p95 0.727 ms and maximum 12.650 ms;
eight preparations of 8.8–12.4 ms each, all rollout time within 0.06 ms; the
control arm's maximum is 1.523 ms and passes. The single remaining blocker is
the 5 ms maximum. Both fresh arms kept all eight clients alive on every frame,
so the review's lost-client frames are not reproduced and remain unexplained.
The old expensive 17-candidate search remains an offline oracle; no need to
reintroduce it into normal MP or replace the navmesh without evidence.

Existing collection commands are in
`tools/strafe_flight_native_campaign.py` (freeze/run, twelve matched cases),
`tools/strafe_continuous_runner.py` and `tools/strafe_cost_runner.py`
(`--flight-artifact`, `--flight-adapter`). Inspect current options and freeze
fresh destinations against the new build; the reviewed campaigns already ran.
Outcome acceleration is not timing evidence. A new aggregate outcome auditor
is specified in R1; do not treat the per-case harm result as every frozen gate.

**Done when:** useful cold-loaded runtime execution meets complete cost and
the unchanged full-trip time/idle/walking gates, with all refusals and resource/
ownership limits reported. Met on the 2026-09-07 holdout as recorded above;
subsequent integrated K07 qualification still applies.

## K06 — general recovery and resources in the same planner

Use actual contact, velocity, publication/objective generation, resource state
and progress to keep, abort or rejoin a maneuver. Rejoin or select another
continuation from the same prepared graph; do not reset the bot onto a path.
Persist a still-valid escape long enough to make progress instead of abandoning
successive short clear steps. Invalidate on map/publication changes, respawn or
lost ownership; never let stale state command a different client life.

Retain native pit-drop, lateral-body, skipped-jump and impulse counterexamples.
Exercise moving blockers, missed landings, held/dropped/returned flags, natural
respawn, no Speed and expiry during run-up/flight/return. Force availability,
remaining Speed duration and actual momentum are separate inputs. No force or
health grants after declared setup; no selfkill workaround.

The static discovery guard is not multiplayer rollback. Dynamic acceptance
must use current native collision/body state and bounded validation, report
staleness/refusal, and yield to legitimate objective/combat ownership. Keep
ground running/braking/waiting as explicit fallback actions with progress and
reason telemetry. Reuse existing play logging; add fields only for a question
the current data cannot answer.

**Done when:** production lifecycle/resource cases have useful continuations,
retained regressions pass, and recovery consumes the same bounded movement
budget. Defended development is diagnosis, not K07 qualification.

**R4 is complete (2026-09-07).** Protocol:
[lifecycle, contention and recovery](STRAFE_MOVEMENT_LIFECYCLE_NATIVE_2026-09-07.md).
Contracts now in place in `ai_bot2_flight.c` (mocked, 74 scenarios):

- Deferred-first admission: a deferred client holds a ticket for the next
  slot in deferral order; a lower-numbered newcomer that Pmove visits first is
  deferred behind it; tickets expire after two frames. `prepare_deferred`
  prints `queue= head= waited=`, `prepare_cost` prints `waited=`, and the
  parser/cost report give admissions and deferrals per client, the longest
  wait, the longest run of consecutive deferrals and `starvation_pass`.
- `owner_lost`: a plan that stops offering frames releases the client
  explicitly at the boundary, with phase, ground and health.
- Airborne publication, artifact and force-gate releases enter bounded
  recovery with `cause=` recorded; recovery ignores the flight gates and ends
  when grounded, dead or under abnormal physics; operator `disabled` and
  pre-launch releases still release at once.
- Best-effort recovery toward the nearest range point while still above the
  range (`recover_best_effort`, kernel result 2); `recover_refused` prints
  along/lateral/height so a doomed fall is attributed.
- `Bot2_ClearState` resets the adapter; `Bot2_FlightAdapterProbe` serves
  research fixtures.

Measured natively (campaign `…-lifecycle-r4-20260907-v3`, 18 cases, all
inputs valid): 14 intervention cases pass, the two 200 unit/s boundary cases
pass on explanation (red died in the pit 7.6 units below the range, blue did
not), both controls complete 15/15 with no deaths. No airborne hand-back
outside the declared exceptions; every non-lethal case grounded within 0.8-2.5
s and moving on a plan within 1.2-6.1 s; the damage case reset exactly once,
on its death frame, and re-acquired a plan 2.4-2.9 s after respawn. The blue
body case is the one native best-effort recovery. Publication change
mid-flight was not constructible natively and remains mocked.

Contention: no native same-frame contention was obtained in nine arms
(eight-, two- and sixteen-client worlds, holds, clustered and per-team spawns;
all retained and explained in the protocol). Serialisation held on every
frame; deferred-first fairness is mocked and fully instrumented; the eight-bot
budget passes on the final build (p95 0.369 / max 3.718 ms) with one earlier
sample at 5.112 ms retained as variance of the same six-forecast preparation.
A deterministic contention fixture (pausing the world, not a client) is a K07
stress item.

Retained limits: outcome evidence from accelerated solo worlds and one
eight-bot/two-bot cost world each; not K07 qualification; the clustered
same-spawn start (`bot_moveCostSameSpawn`) rejects supported preparation and
is retained as a refused construction.

### R4 start notes (handoff from the R1–R3 session, 2026-09-07; executed above)

Facts the R4 cases must be built against, all measured this session:

- No native preparation deferral has ever been observed: every eight-bot cost
  arm reports `prepare_deferred` 0 and `prepare_admitted` at most 1 per frame,
  and no R3 outcome collection printed `prepare_deferred`. The one-slot
  schedule (`FLIGHT_PREPARATIONS_PER_FRAME`) is therefore serialisation
  evidence only; R4 has to construct a case where two or more distinct clients
  genuinely present admitting roots on the same frame, and record admitted/
  deferred/refused per client, per-client progress, and the state each client
  prepared from after waiting. The cost fixture's declared impulses
  (`bot2_movecost.c`, `elapsed>=2500+client*25`) are a starting point for
  synchronised disturbance, not yet a contention case.
- No refusal after entry occurred in any R3 collection, so three recovery
  paths have no native example: `FLIGHT_RECOVER` (airborne refusal; one
  `recover_refused` in the reviewed captures, none since), `refused_resumed`
  (grounded refusal after the first landing re-seated by `Bot2_ReceiverResume`;
  mocked only), and the plain grounded hand-back. Gate, publication, disable,
  artifact and path-content releases precede the refusal branch and have no
  airborne coverage either; the review's R4 asks for a useful airborne
  continuation or an explicit safe owner handoff on each.
- Every refusal now prints `[FLIGHT] refusal … phase= ground= speed= vz=
  upmove= jumpheld= run_along= run_lateral= landing0= landing1=`, every refused
  rollout prints its terminal phase and landing geometry, and the audit reports
  `refusal_phases`, `recovered_entries`, `truncated_entries` and the release
  outcomes per case. Add fields only for a question this data cannot answer.
- The review's 224 lost-client frames never recurred: five fresh eight-bot
  arms kept all eight clients alive on all 800 frames. The loss is unexplained,
  not resolved; report it if it reappears rather than filtering it.
- The scoped guard refuses a rollout whose world differs from the scope
  baseline and contaminates the scope; a moving blocker or another client
  changing state during a preparation is exactly what it detects. R4's
  moving-blocker cases must therefore expect `forecast_world_refused` and
  bounded ground fallback, not a rollout through a changed world.

Commands, exactly as used this session (PowerShell or bash from the root):

```bash
python tools/strafe_flight_native_campaign.py --action freeze --build .codex-research/strafe-build-r3-oracle-7851ea4040a949fe90b10b3dd3465464 --artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh-dir> --first-port <port> --phases 1 4 7 --runner-args=--forecast-guard
```

```bash
python tools/strafe_flight_native_campaign.py --action run --out .codex-research/<fresh-dir>
```

```bash
python tools/strafe_flight_native_report.py --campaign .codex-research/<fresh-dir> --out .codex-research/<fresh-dir>-report
```

```bash
python tools/strafe_cost_runner.py --build .codex-research/strafe-build-r3-oracle-7851ea4040a949fe90b10b3dd3465464 --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --bots 8 --supported-hop 2 --supported-corners 2 --flight-artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh-dir> --port <port> --flight-adapter --forecast-guard
```

`--runner-args` takes one string; a single dashed flag needs the `=` form
(`--runner-args=--forecast-guard`), a multi-token string may be quoted
(`--runner-args "--forecast-guard --flight-exhaustive 2"`). Use
`--flight-exhaustive 2` only for oracle evidence; it multiplies rollouts and
its collections are not cost evidence. Phases 0/3/6 were used for development
and 1/4/7 for the K05 holdout; declare new phases or a new case shape for R4
rather than reusing the holdout for tuning. After any C change: fresh build
with `tools/strafe_fixture_build.py`, then the mocked checks
(`strafe_flight_adapter_check.py`, `strafe_forecast_guard_check.py`,
`strafe_movecontrol_check.py`) before native collection.

## K07 — qualify one integrated Kejim revision

Freeze the candidate, configuration, native builds and case sets before final
evaluation. If a later fix changes measured code, invalidate affected claims
and repeat the necessary evaluations; do not mix revisions into one pass.

| Work | Existing starting points | Required result |
|---|---|---|
| Integrated full-route quality | K03's frozen comparison and metrics; add a production-runtime arm to the continuous fixture/audit where missing | Same 12-run time/idle/walking gates on the actual runtime adapter, with expensive CHAIN2 discovery disabled |
| Cached baseline regressions | `STRAFE_MOVEMENT_M1_M3_CAMPAIGN_2026-09-05.md`, existing M0/M1/M2 runners/audits | Plan's 26-start/eight-phase speed and independent disturbance thresholds retained |
| Integrated resource traversal | `tools/strafe_supported_batch.py`, `strafe_supported_report.py`, `STRAFE_MOVEMENT_SUPPORTED_LIVE_2026-09-05.md` | Full 30-case matrix, no-Speed and natural expiry, production handoffs and matched-resource references |
| Continuity | `tools/strafe_continuity_campaign.py`, `strafe_opposition_audit.py`, `STRAFE_MOVEMENT_CONTINUITY_CAMPAIGN_2026-09-06.md` | All 12 baseline +12 candidate defended runs; every team/rate cell and aggregate satisfy the plan |
| Cost | `tools/strafe_cost_runner.py`, `strafe_cost_report.py`, `strafe_cost_trace_audit.py` | Eight actual active movement/recovery clients meet 2.5 ms p95/5 ms max; 16-bot stress and full server frame reported |
| Player-visible motion | `STRAFE_MOVEMENT_PLAY_FEEDBACK_2026-09-06.md`, existing CSV/demos | Roughly travel-facing, ordinary jump behavior and retained corner/drop/body cases, no hidden Force Jump/resource workaround |

Inspect each helper's current options before declaring its new candidate arm.
Some old fixtures inject Speed or bypass production movement ownership. Keep
them as regressions, and implement the missing same-start/resource production
reference mode rather than borrowing their times for no-Speed movement. Exact
new reference/case values must be frozen before collection, not invented here.

Schedule outcome worlds at most four at once; timing alone. Report raw alive
locomotion, classified waiting, travel fractions, longest idle/walking spans
and distant confinement. Preserve the distinct solo/defended metric definitions
in the active plan. Four-Hz play sampling cannot certify absence of moving loops.

**Done when:** the same integrated revision passes all applicable plan gates,
with a compact evidence manifest and explicit remaining limits. A cost sanity
check or two defended development runs do not complete this package.

### R5 start notes (handoff from the R4 session, 2026-09-07)

Facts the K07 freeze must be built against, all measured this session:

- The candidate revision is the committed tree at `2ff38c6a` with the
  opt-in configuration `bot_flightAdapter 1`, `bot_forecastGuard 1`,
  `bot_flightArtifact` = the `…-r3-` payload, on top of `bot_supportedHop 2`,
  `bot_supportedCorners 2`. Its native build is
  `.codex-research/strafe-build-r4-lifecycle-a5a81bcd5a554bbc89eb423fc4fe950b`
  (1,102 sources); the lifecycle evidence is on `…-65dba039…`, which differs
  only in `bot2_movecost.c` fixture options. Freeze one fresh build from the
  committed tree for K07 and bind every arm to it; do not mix the two.
- Every lifecycle path now has a native example except publication change
  mid-flight (mocked only) and native same-frame contention (never observed:
  nine arms, all retained in the lifecycle protocol). K07's sixteen-bot
  stress needs a contention fixture that pauses the whole world for one
  frame when two clients hold admitting roots, not a per-client hold:
  clients couple through flag state, and a hold that is not a multiple of
  200 ms shifts the 8 ms Pmove phase against the 25 ms frame. The
  telemetry to read it already exists (`prepare_deferred … queue= head=
  waited=`, `prepare_cost … waited=`, the cost report's `contention` block
  with `starvation_pass`).
- Eight-bot complete cost varies per run on identical work: the same
  six-forecast preparation measured 3.72 ms (R3), 5.11 ms (`baseline/`) and
  3.72 ms (`baseline-b7/`). K07's cost claim must come from several
  declared samples with the spread reported, timing alone, not from one
  passing arm. Sixteen bots measured p95 0.507 / max 4.612 ms with every
  client alive on all 800 frames.
- The retained pit/drop/body regressions are the M1–M3 runners in the K07
  table plus the lifecycle campaign's boundary case; the lifecycle campaign
  (`tools/strafe_flight_lifecycle_campaign.py`, phase 2) is itself a
  regression to rerun on the frozen K07 build, expecting 14 pass, 2 boundary
  pass, 2 control pass.
- Interventions and clustered starts are research fixtures
  (`bot2_flightcase.c`, `bot_moveCostHold`, `bot_moveCostSameSpawn[Blue]`);
  none of them belongs in a K07 qualification arm except the lifecycle
  regression above.

Commands, exactly as used this session (bash from the root; the campaign
helper takes `--phase` and `--first-port`, the cost runner `--bots 2|8|16`,
`--hold`, `--same-spawn`, `--same-spawn-blue`, `--supported-trace`):

```bash
python tools/strafe_flight_lifecycle_campaign.py --action freeze --build .codex-research/<build> --artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh> --first-port <port> --phase 2
python tools/strafe_flight_lifecycle_campaign.py --action run --out .codex-research/<fresh>
python tools/strafe_flight_lifecycle_report.py --campaign .codex-research/<fresh> --out .codex-research/<fresh>-report
python tools/strafe_cost_runner.py --build .codex-research/<build> --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --bots 8 --supported-hop 2 --supported-corners 2 --flight-artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh> --port <port> --flight-adapter --forecast-guard
python tools/strafe_cost_report.py --run .codex-research/<fresh> --out .codex-research/<fresh>-audit
```

After any C change: `tools/strafe_fixture_build.py` into a fresh directory,
then `strafe_flight_adapter_check.py`, `strafe_forecast_guard_check.py`,
`strafe_movecontrol_check.py`, `strafe_flight_check.py` and the Python suites
named in the lifecycle protocol, before any native collection. Patch scripts
belong in files, not shell heredocs: a heredoc collapses backslashes and
three of this session's patches silently failed that way.

### R5 result (2026-09-07, executed as above)

Executed in full on one freeze; the
[K07 protocol](STRAFE_MOVEMENT_K07_QUALIFICATION_2026-09-07.md) is the record.
What the next session needs from it: the world-pause idea was replaced by the
schedule-side rendezvous fixture (`bot_flightRendezvous`, `--rendezvous` on
the cost runner), which produced native contention without touching any
client; the supported batch, play smoke and continuity campaign helpers now
carry the adapter arm (`--flight-artifact --flight-adapter --forecast-guard`,
the campaign's `--play-args`/`--first-port`); and the three failures above are
R6's reasoning items. Hour-long campaign chains must be launched detached
(PowerShell `Start-Process` of a script file) - the tool's background
commands are capped at ten minutes and a stopped chain's Python runner keeps
its four worlds alive, which is how two campaigns overlapped for a minute
this session (all overlapping runs passed their validity predicates and are
retained). A ComfyUI process was resident on the host throughout; cost
samples should be collected with nothing else running, and the host state
recorded either way.

## K08 — deliver the playable bot

After qualification, use the existing installation helper with explicit accepted
build paths. Review its operations against archive-only policy; retain installed
binaries/configs with hashes and a restore manifest before replacement. Build
client/UI targets separately if those changed; the research helper disables
some targets. Do not install the dedicated research fixture as the MP feature.

Smoke test normal `openjk.x86.exe` → Create Game → CTF Kejim → join a team →
`addrunner`, both explicit teams, and `addbot2enemy 2`. Verify CHASE/BASE roles,
`botplayrate 1` and `5` native cadence, human defense, native respawn/pickup/return,
CSV flush/live inspection, demos and orderly shutdown. Update `KEJIM_RUNNER_PLAY.md`
with the actual installed revision, defaults, known limits and restore steps.
Human evaluation is additional evidence; do not claim it happened without play.

**Done when:** qualified movement is available through the user's ordinary MP
workflow with inspectable telemetry and a concise before/after report. The
September 7 research snapshots must remain available as historical references.

## K09 — transfer the solution

Use Dantooine as already chosen. Inventory native spawns/objectives; run the same
map preparation, policy generation, runtime and M1–M3a gates with new native
references. Existing Dantooine geometry and three useful transition proposals
are early evidence only. Measure preparation time, artifact size, accepted and
unsupported coverage, runtime cost, complete trips and recovery.

Fix reusable geometry/physics/controller defects and rerun affected Kejim
regressions. A map-specific C branch, manual rescue line, copied reverse route,
or altered acceptance threshold is not transfer. If topology is actually wrong,
retain the defect and correct the general extraction/navmesh pipeline.

## K10 — finish broader offense behavior

Once movement is dependable, add minimum objective decisions for absent home
flag/carrier/dropped flags, survival and resource allocation. Then resume paused
aiming/combat using existing aiming work selectively. Declare scenario and
match evaluation before tuning: useful objective decisions, survival, force
costs, understandable behavior and intermediate-human shooting. Numerical
combat/style acceptance has not yet been agreed; establish it before claiming
the complete offense goal. Preserve movement qualification throughout.

## Working efficiently and handing off again

Routine implementers can execute frozen campaigns, parser/format changes,
serialization, CLI/report compatibility and explicitly specified regressions.
Receiving-state viability, reduced-validation safety, dynamic ownership and a
new representation need a reasoning/review checkpoint. Do not hide those
unknowns inside a long unconditional implementation checklist.

Use bounded independent review only when it saves work; avoid multiple agents
re-reading history or running uncoordinated native worlds. Work until the
authorized package is handled, but respect a user's stop/checkpoint request.
No periodic automation or background continuation is implied by this document.

At a stopping point: finish/audit the declared collection where feasible;
record any incomplete cases/processes, freeze status and output paths; preserve
native/helper snapshots and failed evidence; update the scorecard's current
result, next K-number, exact command and installed state. Do not turn a new
scorecard into another chronological experiment log. Keep detail in the dated
protocol. This documentation handoff launched no native runs and changed no
controller, helper or installed binary.

Campaign record for September 7: K01, K02 and K03 ran to completion — 6, 23 and
12 native cases, all valid, at most four worlds per collection on distinct ports.
All 1,094 native source hashes matched before each collection and at the K02
snapshot; installed DLL hashes and frozen `bot2_strafe.c` are unchanged; nothing
was installed and no setting promoted. Code changes were four Python auditors
(the map-checksum sign correction and its bindings) plus two new selector files;
the K02 snapshot records them as `changed_helpers` and `new_helpers`. Focused
suites pass: 43 window, 38 connection-report, 27 chain tests. Details are in
[the campaign protocol](STRAFE_MOVEMENT_CHAIN2_CAMPAIGN_2026-09-07.md).

Previous plan/scorecard copies are listed in
[`manifest.json`](../.codex-archive/20260907T013549236Z/manifest.json); restore a
document by copying its archived file back to the recorded original path after
preserving the newer version. No original evidence was moved or deleted.

Session record for September 7, R1–R3: C changes in `bot2_movecost.c/.h`
(complete accounting, buffered prints), `bot2_movebench.c` (preparation
charged at the Pmove boundary), `bot2_forecast.c/.h` (scoped baseline guard),
`bot2_movecontrol.c` (pruned scan, bitwise identical), `ai_bot2_flight.c/.h`
(guard scope, issued-command latency model, refusal attribution and resume,
`bot_flightExhaustive`, `bot_flightRolloutCap`, `bot_flightBoundarySkew`).
Python: `strafe_cost_report.py` rewritten, new `strafe_flight_native_report.py`,
`strafe_forecast_guard_check.py`, `test_strafe_cost_report.py`,
`test_strafe_flight_native_report.py`; `strafe_flight_artifact.py`
`--runtime-oracle`; runner flags `--forecast-guard`, `--flight-exhaustive`,
`--rollout-cap`; campaign helper `--runner-args`; `strafe_flight_log.py` guard
and work fields; the adapter and movecontrol mocked checks extended. Six fresh
builds, nine eight-bot cost arms, four twelve-case campaigns; every build's
1,100 sources matched its tree at collection time. Nothing was installed or
promoted. After the handoff review the sources, helpers and documents were
committed on this branch; local evidence under `.codex-research/` and
`.codex-archive/` stays uncommitted. Evidence paths are in the scorecard's
evidence map.

Session record for September 7, R4 (committed as `2ff38c6a`): C changes in `ai_bot2_flight.c/.h`
(deferred-first tickets, owner-lost release, airborne handoff to recovery,
best-effort recovery telemetry, probe), `bot2_flight.c/.h` (best-effort
recovery result 2), `ai_bot2.c` (`Bot2_ClearState` resets the adapter), new
`bot2_flightcase.c/.h` (declared interventions, hooked from `bot2_continuous.c`),
`bot2_movecost.c` (`bot_moveCostHold`, `bot_moveCostSameSpawn`, two-client
world, hold-relative impulse), `codemp/game/CMakeLists.txt`. Python: new
`strafe_flight_lifecycle_campaign.py`, `strafe_flight_lifecycle_report.py`,
`test_strafe_flight_lifecycle_report.py`; runner flags `--flight-case*`,
`--hold`, `--same-spawn`, `--supported-trace` (cost), `--bots 2`;
`strafe_flight_log.py` ticket/fairness fields; `strafe_cost_report.py`
contention summary; the adapter mocked check extended to 74 scenarios. Five
fresh builds, three lifecycle campaigns (the first two retained with their
auditor defects), smokes and cost/contention arms; nothing installed or
promoted.

Session record for September 7, R5 (K07): one C change, the rendezvous fixture in
`ai_bot2_flight.c` (`bot_flightRendezvous`, tri-state claim, `prepare_withheld` /
`rendezvous` / `rendezvous_lost` prints). Python: `strafe_flight_log.py` parses the
fixture lines; `strafe_cost_report.py` reports `contention.rendezvous`;
`strafe_cost_runner.py --rendezvous`; `strafe_supported_batch.py`,
`strafe_play_smoke.py` and `strafe_continuity_campaign.py` gained the adapter arm;
the adapter mocked check has 82 scenarios; one continuous-runner test updated.
One fresh build, eight cost arms, one lifecycle campaign, one 30-case supported
matrix, one twelve-case full-route campaign (plus a retained partial), two
twelve-run continuity arms, one candidate play smoke and the 780-case M2
evaluation; nothing installed or promoted.
