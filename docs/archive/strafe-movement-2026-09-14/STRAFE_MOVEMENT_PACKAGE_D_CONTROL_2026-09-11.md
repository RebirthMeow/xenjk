# Package D step 1: the frozen case set and the repeated control arms (2026-09-11)

Step 1 of the owner-approved
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md):
freeze the declared cases, windows, settings, repetitions and audit rules, then
collect the repeated control arms **before** any measured source is edited.

**Nothing is installed and nothing is authored.** The installed build remains
`strafe-build-reviewready-20260910-b` (`525f1b50`). No C, header or CMake source
was touched for this record; the only code written is analysis Python, which the
collection reads but the game does not. K07 remains open.

## What was frozen, and why first

`tools/strafe_branch_manifest.py` wrote
`.codex-research/pkgD-control-20260911/manifest.json` before the first world
started. It binds, by hash: the twenty declared cases, the control build and its
1104 source hashes, the staged flight artifact, the three play fixtures, and the
analysis tools themselves. It declares, in advance:

| Frozen | Content |
|---|---|
| Entry families | The plan's five coverage rows, plus a *candidate* family per case |
| Measured family rule | The corridor record's own x split: band 3600 < y < 4300, flown if max x > 2350, blue half-turned about (1312, 2080), episodes split at 2000 ms, no-sample episodes `unclassified` and never dropped |
| Windows | entry, descent (incl. y ≈ 4500), the blue-only choke at mirrored y 4300, corridor, rejoin — each bound to XY *and* height, the branch ones also to an event |
| Speed rules | The plan's moving-entry numbers, plus the slow-entry limits declared here for the first time |
| Retained targets | T1, T1-all, T2, T2-branch, T3, T4, T5, T6 in their original scope |
| Audit rules | All-reason churn at 300 ms, route attribution from the owning join, explicit denominators, no pooling, full retention |

Two declarations are worth stating plainly because they constrain later work:

- **The slow-entry limits.** With Speed active: 300 u/s within 1.5 s and 425
  within 2.5 s of branch entry. With Speed inactive or expired: 300 u/s within
  2.5 s, and **no 425 obligation at all** — the ground cap without Force Speed
  is 250 u/s, so a 425 requirement there would be a requirement on the geometry
  to supply hops, not on the mover to accelerate. Time and distance to each
  threshold are reported separately regardless. A moving-entry minimum is never
  applied to a rest state.
- **A branch frame has a definition.** There is no separate movement owner for a
  branch: a branch is the lane owner executing a different route. So branch
  frames are defined as the lane frames between a `branch … event=entry` record
  and the `exit` or `abandon` that closes it, and an entry never closed is
  counted to the end of the trace and reported as **unclosed**. That is what
  stops an abandoned branch from reading like a finished one.

## What the audit now accounts for

`tools/strafe_momentum_audit.py` gained the accounting the plan's E section
requires, and `tools/strafe_branch_report.py` is new:

| Addition | What it closes |
|---|---|
| `churn_all` | T1 counted only resource-reason cycles. Package B moved those 28 → 0 while total churn went 34 → 33; an audit that reports only the resource prefix would have called that solved. Every join released within 300 ms is now counted, by reason, route, group, index and the resources at both ends |
| Route attribution | A release and an admission carry no route field. The route is taken from the owning join (or the last `armed`) and **labelled attributed**, so a later arm that emits the field replaces the inference instead of confirming it |
| `branch_accounting` | Selection, entry, progress, exit, abandon and their denominators. A build with no branch reports structural zeros against real joins and admissions, so "no branch was taken" is a measurement rather than a missing section. Completion counts exits only |
| `owner_shares` | Frame counts never appear without their denominator |
| `entry_families` | Per-episode family by the frozen rule, with the band evidence (`band_max_x`) attached |
| `strafe_branch_report.py` | Per-case and per-repetition tables, per-family splits, cross-repetition spread, and a verdict function that calls a gap inside the control's own spread **inconclusive** — in the tool, not in the prose |

The verdict rule has two bars. A gap no larger than the control's own
repetition band is inconclusive for any measure. On the aggregates known to
wander between identically built arms — lane frames, supported frames,
zero-input, stationary, admits — the gap must *also* reach half the control
median, so only a factor-sized change is reportable there. That is the Package B
caveat turned into code: arms C and G were the same logic and differed by 13 %
on lane frames and 50 % on zero-input.

25 pure checks in `tools/test_strafe_branch_report.py` pin these rules, each
naming the mistake it prevents — including the height test that misclassified a
flown episode in the corridor record, the two-joins-one-release pairing that
would double-count a cycle, and the `takeoff_` prefix that would have counted a
momentum refusal as a resource one.

The whole Python suite is 409 tests, **all passing** (384 before this work plus
the 25 new checks). Run it from the repository root —
`PYTHONPATH=tools python -m unittest discover -s tools -p "test_*.py" -t tools`
— because several runners hash sources by relative path and report eight
spurious errors when unittest is started from inside `tools/`.

## Catalog capacity, checked before authoring

`BOT2_ROUTE_LIMIT` is 32 and the frozen fixture
`tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt` declares 26 routes,
so **six slots remain**. BR-A and BR-B in both directions are four of them. The
plan's warning applies directly: a branch must not become a full outbound guide
merely because its entry lies near a stand, and capacity is not spare.

## Control arms

Three complete repetitions of the same twenty cases on the same build, in the
same world configuration as the Package B arms (`bot_fastReturnMomentumTap 1`,
supported corners 2, staged flight adapter with its guard, carrier melee, fast
return, timescale 5, 55 game seconds, at most four isolated worlds at a time on
distinct ports and homepaths). Every world is retained.

| Repetition | Evidence |
|---|---|
| r1 | `.codex-research/pkgD-control-20260911/r1` |
| r2 | `.codex-research/pkgD-control-20260911/r2` |
| r3 | `.codex-research/pkgD-control-20260911/r3` |

All three are collected: 20 / 20 worlds per repetition, every case exit 0, 60
worlds retained. The driver is retained beside the evidence as
`run_cases.py`, the audits as `r{1,2,3}-audit.json` and the report as
`control-report.json`.

## Two corrections made before any number was interpreted

Both were found by running the new accounting against real worlds, and both are
recorded here rather than quietly fixed, because each would have produced a
confident wrong claim.

1. **`takeoff_momentum` is not a resource reason.** The first manifest
   classified a resource cycle by the `takeoff_` prefix, inherited from the
   existing `churn()` helper. That prefix matches `takeoff_momentum`, and
   **every** churn cycle in all three control repetitions is
   `takeoff_momentum` — so the first rule would have reported 25..34 resource
   cycles in a configuration where Package B had measured zero. Reasons are now
   classified by name (`resource`, `momentum`, `geometry`, `execution`,
   `external`, and `unclassified` for anything new), the legacy prefix count is
   retained separately as `churn_takeoff_prefix` for comparability, and a pure
   check pins the distinction. `manifest-v2.json` supersedes `manifest.json` and
   states why; nothing else about the collection changed.
2. **A measure was named for a claim instead of its content.** The report had a
   `full_stop` field defined as "a stationary sample and a grounded-slow window
   of at least 1 s", which fired in 19..20 of 20 worlds — it was not the "full
   stop" the earlier arms reported as 0. That criterion was not retained beside
   its evidence and is **not reproduced or compared against here**. The field is
   replaced by three that say what they count:
   `worlds_with_stationary_sample`, `worlds_with_grounded_slow_window` and
   `longest_grounded_slow_ms`.

## Control result, three repetitions

Every number is per repetition. Nothing below is pooled.

| Measure | r1 | r2 | r3 | Relative spread |
|---|---|---|---|---|
| Worlds / captured | 20 / 20 | 20 / 20 | 20 / 20 | 0 |
| **Churn, all reasons (T1-all)** | **29** | **25** | **34** | **36 %** |
| Churn, resource class | 0 | 0 | 0 | — |
| Churn, momentum class | 29 | 25 | 34 | 36 % |
| Churn, geometry / execution / external | 0 | 0 | 0 | — |
| Joins | 72 | 65 | 82 | 26 % |
| Releases | 47 | 41 | 55 | 34 % |
| Admissions / admitted | 214 / 169 | 193 / 152 | 226 / 176 | 17 % / 16 % |
| Accept rate | 0.790 | 0.788 | 0.779 | 1 % |
| Lane frames | 7667 | 7006 | 7909 | 13 % |
| Supported frames | 22671 | 21656 | 24156 | 12 % |
| Trace samples (denominator) | 30393 | 28703 | 32135 | 12 % |
| Stationary samples | 61 | 58 | 65 | 12 % |
| Zero-input samples | 147 | 140 | 129 | 14 % |
| Hops | 454 | 421 | 483 | 15 % |
| Branch selections / entries / exits / abandons | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | structural |

### What these repetitions establish

- **T1 holds under repetition, and not by a margin.** Resource-class churn is
  0, 0, 0 across 60 worlds. Every one of the 88 churn cycles in the three
  repetitions is a momentum refusal. That is the Package B composition claim
  confirmed with repetition rather than on one arm.
- **Joins, releases and churn are themselves unstable at this sample size.**
  Their relative spread across *identically built* repetitions is 26 %, 34 % and
  36 %. Package B reported joins 119 → 80, releases 91 → 49 and churn 34 → 33
  from one arm each. The first two survive this band (119 is outside 65..82, 91
  is outside 41..55, so "down a third" and "down by nearly half" stand); the
  churn total does not move outside it, which matches that record's own "total
  churn did not fall" — but it now also means **no future arm may claim a churn
  improvement inside 25..34**. The earlier record listed joins, releases, churn
  composition and the accept rate as *stable* across arms C and G; on three
  repetitions only the accept rate (1 %) is stable in that sense.
- **The unstable-aggregate list grows.** Lane frames 13 %, supported frames
  12 %, zero-input 14 %, stationary 12 %, admits 16 %, hops 15 %, and the trace
  denominator itself 12 %. The report's verdict function therefore requires a
  factor-sized change before reporting any of these, and the candidate arm will
  be scored against these exact bands.
- **Branch accounting reads zero structurally, as it must.** 0 selections,
  0 entries, 0 exits, 0 abandons, 0 branch frames against 65..82 real joins and
  193..226 real admissions. The runtime cannot execute a branch yet; that is now
  a recorded measurement rather than a missing section, and it is the baseline
  every later entry/completion rate is divided against.

### Family split, and a limit of the case set

Measured by the frozen rule, on the seeded first episode of each world:

| Family | Worlds | Churn (r1 / r2 / r3) | Admits / admissions (r1) |
|---|---|---|---|
| F1-early-entry | 12 | 19 / 16 / 21 | 94 / 120 |
| F2-post-group-1 | 8 | 10 / 9 / 13 | 75 / 94 |
| F3-rejoin-arrival | 0 | — | — |
| F4-late-refusal | 0 seeded | — | — |

The split is stable in composition across all three repetitions, and it matches
the declared candidate families on every case but one: **R6a** (the recorded S1
pickup overshoot) was declared F1 and measures F2 in both directions — its
seeded episode does fly group 1. That is a correction to the declaration, not to
the measurement, and it moves two cases into the post-group-1 row.

Two further limits are visible in the per-case rows and are kept rather than
smoothed:

- **Half the worlds are mixed.** 10, 8 and 10 of 20 worlds per repetition
  contain episodes of both families, because a world runs several returns and
  only the first is seeded. Family accounting keyed on the seed is therefore a
  statement about the seeded return, with the other episodes still counted in
  `family_counts`. Any later per-family claim has to say which it means.
- **Some episodes are `unclassified`**: R6b-red in all three repetitions, plus
  R5-red and R6b-blue in r3. They never entered the classification band. They
  stay in the denominator.

### The stationary samples are attributed, not pooled away

58..65 stationary samples appear per repetition, inside 105 grounded windows
across the 60 worlds. Every one is short: 100..425 ms, median 250 ms, **none
longer than 425 ms**. They cluster at level 43..52 s and 78..92 s, roughly twice
per world. Reading the trace at one of them (R1-red, level 43575) attributes it:

```
level=43575 speed=122.9 ground=1 fwd=-39  owner=supported force=100 speed_ms=0 route=-1
level=43675 speed=5.9   ground=1 fwd=-127 owner=supported force=100 speed_ms=0 route=-1
```

Owner `supported`, no lane (`route=-1`), **Force 100**, a reversed wish: the
supported walker braking with full resources, not a depleted lane stalling. This
is the known short-leg `speed_below_walk_floor` behaviour of that executor, the
same cause the corridor record named at the y = 4500 descent. It is not a T3
failure — T3 governs refusal continuation on the lane — and it is not evidence
about depleted-Force competence either way. It is recorded because the branch
work must not be credited with removing it, nor blamed for leaving it.

## What this record does not establish

- Nothing about a branch. No route is authored, no loader exists, and every
  branch number above is a structural zero.
- Nothing about depleted-Force competence. That still needs Package D's authored
  line and a candidate arm measured against these bands.
- The T2, T2-branch, T3, T4, T5 and T6 windows are declared, not yet measured
  per window on this collection; the report computes the accounting the plan's E
  section requires, and the per-window speed evaluation is the next tool.
- The earlier arms' "full stop" criterion remains unreproduced.

## Next, in the plan's order

1. Implement one complete BR-A end to end with the minimal B3 loader/owner
   machinery and the B6 depleted-pickup choice, emitting the
   `[FASTRETURN] branch` record this audit already reads. Six catalog slots
   remain; BR-A and BR-B in both directions take four.
2. Publish the numeric admission envelope for entry and rejoin before native
   qualification, with tested interior, boundary and negative states.
3. Collect three candidate repetitions on a frozen build under these settings
   and score them against the bands above, then rerun the 26 temporary-body and
   30 resource regressions.
