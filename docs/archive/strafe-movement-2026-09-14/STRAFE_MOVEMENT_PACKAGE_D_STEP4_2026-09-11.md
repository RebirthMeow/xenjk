# Step 4: the integrated campaign, and what it did and did not earn (2026-09-11)

Step 4 of the owner-approved
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md):
freeze the integrated candidate, run the declared repeated campaign against the
step-1 control bands, rerun the affected regressions and the defended checks at
normal speed, and update the scorecard.

**Nothing is installed.** The installed build remains
`strafe-build-reviewready-20260910-b` (`525f1b50`). **K07 is not qualified and
is not claimed.**

## Post-campaign review corrections

Reviewed against the runtime, window/report evaluators and retained native
`games.log` records after this collection. All 1104 build-source hashes still
match the checkout. No native worlds were rerun and no frozen report changed.
The [current branch plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order)
owns the remaining work. The detailed tables below retain the original scored
values; their interpretation is corrected here.

- **Collection completed; acceptance did not.** Step 3 is partial and step 4
  did not earn delivery. T1/T1-all's repeated zero churn is retained.
- **T2-branch is partial, not met.** The original table itself records entry
  32/33 in r2 and descent 50/52, 53/55 and 50/52. The corridor has 7.9–12.6
  u/s worst samples. The frozen rejoin has no scored visits. Lower low-speed
  counts and better medians are useful progress, not a universal gate pass.
- **The engine logs correct T6's death count and attribution.** There are two
  runner `MOD_FALLING` deaths at normal speed: blue-1x-r3 at 3:50
  (`base/games.log:60`) and blue-1x-r1 at 9:52 (`base/games.log:126`). There
  is a third runner fall in blue-5x-r1 at 9:58 (`base/games.log:142`). Eight
  engine falling deaths across all twelve worlds comprise three runners and
  five opponents. T6 remains failed. The red-1x-r1 5:59 runner death previously
  called falling is `MOD_REPEATER_ALT_SPLASH` (`base/games.log:90`); its 6:09
  `MOD_FALLING` victim is Bot2Enemy2_v2 (`base/games.log:93`). Keep the red
  descent as a separate motion-safety episode. Airborne state plus a preceding
  drop is not a means-of-death classifier. The actual blue falls still need
  ownership/contact/carrying attribution; enemy kill credit does not prove
  their movement unavoidable.
- **That attribution is now measured**, from these same retained worlds with no
  native rerun: see the
  [fall attribution record](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md).
  There are **four chasm plunges** in the twelve worlds, three at normal speed,
  all ending on one surface at z 32.1. The red-1x-r1 5:59 splash death happened
  **inside** a plunge of the same family, so it is a separate means-of-death
  case but not a separate movement case. Of the four departures, **two are
  opponent knockback** (blue-1x-r1: velocity turns 96.5 degrees at constant
  magnitude, impulse 5.5-10.5 degrees from away-from-opponent at 200 units;
  blue-5x-r1 at timescale 5, carrying, `PMF_TIME_KNOCKBACK` set, +252 u/s from
  directly behind) and **two are self-steered** under the `supported` executor
  on the outbound fetch leg with the route inactive (blue-1x-r3, nearest
  opponent 514 units away; red-1x-r1, whose hit lands 1.75 s and one ground
  contact before the departure). No branch or lane geometry is on either
  self-steered line. Nothing after the departure is a movement decision: the
  engine's `fallingToDeath` boundary sits near z 700 and `bg_pmove.c:11185`
  zeroes the command past it, so the 2.4-2.5 s of stillness on the chasm floor
  is the 3000 ms `FALL_FADE_TIME`, not a missed recovery. In 180 seeded solo
  worlds no runner sample is ever below z 1808.
- **The tap-loss code path needs a precise repair.** `FastReturnGroupClass`
  supplies telemetry, not admission. `FastReturnTakeoffAdmission` checks
  `speedMs < 2500` when `tap` is false even if `state->jumpLow` is true. The
  24-unit test and `tapChain` determine `FastReturnTap`. Distinguish authored
  taps from held-jump substitutions, establish which caller refused each
  event, and validate actual-state clearance before changing the Speed gate.
- **The window evaluator's contracts are now repaired**, and what that moves
  is measured in the
  [measurement contracts record](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md):
  the corridor loses three met visits per candidate repetition, T2 still fails
  on its 350 u/s minimum alone, T3's met counts are unchanged but 38 of 41 are
  initial conditions while the median slowest speed after a refusal separates
  the arms 117-127 against 361, T5's eight denominators split 4 exact-12 / 2
  zero-Force / 2 Force-1-11 and its newly gated post-expiry mean fails four per
  repetition in every arm. The frozen tables below are unchanged; the corrected
  outputs are `-v2` files under `.codex-research/pkgD-contracts-20260911`.
- **The window evaluator did not implement every acceptance contract.**
  Slow-entry recovery starts at exit; T2 recovery starts at the minimum rather
  than the original contact deadline; T3 can satisfy its threshold at refusal
  before the subsequent collapse. Recovery searches are not bounded to one
  carrier life/episode. T5 marks noncaptures inapplicable, gates Force 1–12
  rather than isolating exact-12 seeds, and does not gate post-expiry mean
  speed. Keep its reported counts as results of the frozen evaluator; do not
  use its recovery halves as qualification of the intended behavior.
- **Measurement completeness matters.** Missing cases must block qualification,
  not merely appear in a report field. Correct branch teardown accounting and
  use native event-time state before claiming transition envelopes or branch
  frame shares. Hermite residuals do not certify a 48-unit boundary. Retain
  old rejoin reconstruction as diagnostic evidence and declare a new sample-44
  window prospectively.
- **Sources-only similarity does not establish equivalence.** Interpret
  within-band comparisons as no difference established, and retain the source-
  only accept-rate change. The categorical attribution wording later in this
  record is superseded by this qualification.

| Retained target | Result |
|---|---|
| **T1** resource-reason churn | **met** — 0 / 0 / 0 in the control and in the candidate |
| **T1-all** all-reason churn | **met** — 29 / 25 / 34 to **0 / 0 / 0**, outside the control's own declared band |
| **T2** original landing turn | **failed** on its 350 u/s minimum: 2 of 37 control turns, **0 of 28** candidate turns. Its recovery half is met in every turn |
| **T2-branch** windows | **partial / unqualified** — substantial descent improvement, but failed visits remain and the frozen rejoin scores nothing. Reconstructed exit positions are diagnostic only; see review corrections |
| **T3** refusal continuation | **improved, not met** — 42 / 41 / 42 of 57 / 59 / 56 against the control's 7 / 5 / 8 of 45 / 41 / 50 |
| **T4** accepted curves and passes | **not advanced** — a third as many curves reached, pass rate 52–57 % to 44–49 % |
| **T5** travel time | **failed in every arm**, including the control: 0 of 8 applicable returns per repetition |
| **T6** safety and lifecycle | solo half **met** — 0 falls, 0 deaths, 180 of 180 worlds captured. Defended half **met on two clauses of three**: both teams capture at normal speed (62 captures in six 1x worlds) and no normal-speed cell records a five-second confinement, but **two runner falling deaths occur at normal speed in the engine logs** (review correction) |

Every one of those rows has its denominators below.

## What was frozen, and when

`.codex-research/pkgD-step4-20260911/manifest.json`, written at
**09:18:41Z, before the first world of this step started** (the first finished
at 09:21:42Z). It binds by hash: the twenty declared step-1 cases, the build and
its 1104 source hashes, the 30-route catalog and the four-declaration sidecar,
the staged flight artifact, the three play fixtures, and the five analysis tools
— including `tools/strafe_branch_windows.py`, which did not exist when this step
began and is described below.

No source in the build's manifest differed from the checkout at freeze time, at
collection time or now. The integrated candidate needed no rebuild: the step-3
build `strafe-build-brb3-680c73cc…` still matches the working tree exactly, so
it *is* the integrated candidate.

> **Superseded 2026-09-12.** "Now" means when this was written. The tap repair
> changed `ai_bot2_route.c`, so `strafe-build-brb3-680c73cc…` no longer matches
> the checkout and `strafe_play_smoke.py` will refuse it. The build that matches
> the tree is `strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`; see the
> [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md). Everything else
> in this record stands: its worlds and figures are retained evidence of the arm
> that build produced.

### Three arms, because two cannot attribute anything

| Arm | Build | Assets | Repetitions |
|---|---|---|---|
| **control** | `strafe-build-b5d38e71`, the sources before any branch work | 26-route fixture catalog, no sidecar | 3, retained from step 1, **not re-run** |
| **sources-only** | the integrated candidate | 26-route fixture catalog, **no sidecar** | 3, collected here |
| **candidate** | the integrated candidate | 30-route catalog + 4 declarations | 3, collected here |

The control cannot be re-collected: `strafe_play_smoke.py` refuses a build whose
sources have changed, and a control collected after the edit would not be a
control. The step-1 arms were collected before that edit for exactly this
reason, and every fixture, case file and flight artifact they ran with is
byte-identical to what these arms ran with — checked, not assumed.

The **sources-only** arm was declared in the manifest before collection and
exists to answer one question: is a difference the branch assets, or the 1120
added and 117 removed lines of C that carry them? Its answer comes first below, because every
other number on this page depends on it.

120 worlds collected here, 60 retained from step 1, all kept. One further world
is retained beside them and belongs to no arm: `preflight/R2-red`, a single
candidate world run before the six repetitions to confirm the driver, build and
assets worked together. It is named here rather than deleted, and no number in
this record comes from it.

## What the source edit changes on its own, and what it does not

`tools/strafe_branch_report.py` scores control against sources-only on every
aggregate it computes. **Not one of them is reportable**: every gap falls inside
the control's own three-repetition spread.

| Measure | control | sources-only |
|---|---|---|
| all-reason churn | 29 / 25 / 34 | 26 / 28 / 21 |
| joins | 72 / 65 / 82 | 71 / 74 / 60 |
| releases | 47 / 41 / 55 | 46 / 43 / 38 |
| lane frames | 7667 / 7006 / 7909 | 7997 / 8048 / 6936 |
| worlds captured | 20 / 20 / 20 | 20 / 20 / 20 |
| branch entries / exits | 0 / 0 | 0 / 0 (structural) |

One measure does move, and it is reported rather than left to the tool's
silence. `strafe_branch_report.py` does not compute an accept rate, so
`compare()` never scored it; the step-1 record declared it the *only* stable
measure of this case set, at about 1 % spread. It reads:

| Arm | accept rate (admitted / admissions) |
|---|---|
| control | 0.790 / 0.788 / 0.779 |
| **sources-only** | **0.804 / 0.805 / 0.803** |
| candidate | 0.818 / 0.828 / 0.823 |

The source edit moves it by about 0.015, roughly twice its declared band, and
the branches move it 0.015 again. The branch work touched shared paths —
`ai_bot2_supported.c` by 215 lines, the corner and path kernels, and one
`intermediate` flag in the strafe kernel — so a small shift there is neither
surprising nor free of consequence. It is the one thing on this page that the
sources-only arm does not exonerate.

Everything else that follows belongs to the four declared branches, not to the
machinery that can execute them. A build that carries the loader, the owner
accessor, the intermediate-tail flag and the rejoin gate, but stages no sidecar,
is otherwise the control arm to within its own repetition noise.

## What the campaign reports

Scored by `tools/strafe_branch_report.py` against the step-1 bands, per
repetition, never pooled. A gap inside the control's own spread is called
inconclusive by the tool.

| Measure | control | candidate | Verdict |
|---|---|---|---|
| **all-reason churn (T1-all)** | 29 / 25 / 34 | **0 / 0 / 0** | **reportable** |
| resource-class churn (T1) | 0 / 0 / 0 | 0 / 0 / 0 | unchanged, both hold |
| momentum-class churn | 29 / 25 / 34 | **0 / 0 / 0** | **reportable** |
| joins | 72 / 65 / 82 | 41 / 45 / 41 | **reportable** |
| releases | 47 / 41 / 55 | 30 / 33 / 30 | **reportable** |
| admissions / admitted | 214 / 169, 193 / 152, 226 / 176 | 314 / 257, 343 / 284, 317 / 261 | **reportable** |
| lane frames | 7667 / 7006 / 7909 | 16649 / 17810 / 16929 | **reportable** |
| branch entries / exits | 0 / 0 | 52 / 34, 51 / 38, 52 / 37 | structural |
| supported frames | 22671 / 21656 / 24156 | 13412 / 14091 / 13401 | inconclusive: unstable aggregate |
| zero-input samples | 147 / 140 / 129 | 114 / 128 / 111 | inconclusive: unstable aggregate |
| stationary samples | 61 / 58 / 65 | 71 / 73 / 64 | inconclusive: unstable aggregate |
| hops | 454 / 421 / 483 | 469 / 501 / 473 | inconclusive |
| worlds captured | 20 / 20 / 20 | 20 / 20 / 20 | unchanged |

**Every declared case takes a branch.** All twenty enter one two or three times
in every repetition, in both directions and at Force 0, 6, 12, 20 and 26, with
Speed active, expiring and inactive. The result above is not carried by a subset
of the case set: the six cases that lose a branch mid-way (R4, R5, R6b in both
directions) are the ones whose admitted exits read 0–1 against 2–3 entries, and
they are named below.

**T1-all is the one that matters and the one that was hardest to earn.** The
step-1 record put it in writing that *no arm may claim a churn improvement
inside 25..34*, because three identically built control repetitions spread 36 %.
This arm reports **zero, three times**, which is not inside that band. Every one
of the control's 88 cycles across three repetitions was a `takeoff_momentum`
refusal, and a momentum refusal now takes a declared branch instead of handing
movement back. That is the whole mechanism: the refusals did not stop happening
(see T3 below), the churn did.

Supported frames fall by about 40 %, which the tool still calls inconclusive
because it does not reach half the control median. It is reported, not claimed.

## The declared windows, measured for the first time

The step-1 record closed with the T2–T5 windows *declared and not yet measured
per window*, and named the per-window evaluation as the next tool. It is
`tools/strafe_branch_windows.py`, written before the freeze and hashed into it,
with 35 pure checks in `tools/test_strafe_branch_windows.py`.

It has one hard problem: the declared windows are spatial and the 40 Hz momentum
trace carries no position, while the 4 Hz play CSV carries position but cannot
show a one-frame minimum. Each trace sample is therefore placed by cubic Hermite
interpolation between the bracketing play rows using their recorded velocities —
exact for constant acceleration, and so exact for ballistic flight. The cost is
measured rather than assumed: the same interpolation is re-run leave-one-out at
every interior play row, and on this collection the residual is a **median of
about 5 units, p90 about 20, worst about 46**. Rows separated by a teleport (the
declared setup, a respawn) are detected by their own velocities and excluded —
one such row would otherwise report a 4896-unit error and sweep a window it
never crossed.

Two rules in the evaluator decide more than any number it computes:

- **A window nobody entered is not a window that passed.** An empty window
  reports `visited: false`; an accepted curve with no supported-owned sample
  reports as a bypass.
- **A window bound to a branch event is not scored when the event is absent.**
  Both branchless arms walk through the coordinates the branch uses. Scoring
  those crossings would have reported a perfect entry and rejoin for builds that
  cannot execute a branch at all.

Independent corroboration that the join is sound: the corridor record declared
the y = 4500 bottleneck's minimum as **45.7 u/s**, measured from the play rows
alone. This evaluator, reading the 40 Hz trace through the Hermite join, finds
45.7 as the single most common value of a descent visit's minimum — 13 of the 54
control visits in r1 bottom out at exactly that, and it is the median of all 54.
Two different inputs and two different code paths land on the same characteristic
floor.

### T2-branch, per repetition, with denominators

Lane frame share is of the samples inside the window. Nothing is pooled.

| Window | Measure | control | sources-only | candidate |
|---|---|---|---|---|
| **entry** | lane frame share | 23 / 23 / 22 % | 25 / 22 / 23 % | **87 / 88 / 87 %** |
| | median visit minimum | 189.6 / 234.5 / 179.6 | 215.3 / 168.2 / 201.2 | **417.0 / 416.5 / 417.2** |
| | samples below 300 | 171 / 149 / 208 | 148 / 208 / 135 | **0 / 3 / 1** |
| | samples below 200 | 53 / 40 / 84 | 39 / 90 / 35 | **0 / 0 / 0** |
| | scored visits met | 0 of 0 | 0 of 0 | **34 / 32 / 34** of 34 / 33 / 34 |
| **descent** (y 4400–4700) | lane frame share | 6 / 6 / 5 % | 6 / 6 / 7 % | **89 / 89 / 89 %** |
| | median visit minimum | 45.7 / 45.7 / 45.7 | 45.7 / 45.7 / 46.0 | **361.4 / 361.4 / 361.4** |
| | samples below 300 | 1022 / 931 / 1195 | 951 / 995 / 840 | **133 / 127 / 139** |
| | samples below 200 | 675 / 601 / 828 | 615 / 653 / 543 | **20 / 21 / 19** |
| | visits met | 15 / 15 / 14 of 54 / 50 / 57 | 16 / 16 / 15 of 52 / 54 / 46 | **50 / 53 / 50** of 52 / 55 / 52 |
| **blue choke** (y 4300) | lane frame share | 0 % | 0 % | **100 %** |
| | samples below 300 | 7 / 7 / 8 of 66 / 56 / 69 | 7 / 6 / 8 | **0 / 0 / 0** of 162 / 158 / 162 |
| **corridor** (y 2200–4300) | lane frame share | 23 / 23 / 23 % | 24 / 24 / 23 % | **86 / 86 / 86 %** |
| | median visit minimum | 283.2 / 283.2 / 283.1 | 250.2 / 284.7 / 286.3 | 337.0 / 382.6 / 362.1 |
| | samples below 200 | 307 / 267 / 318 of 7324 / 6807 / 8156 | 337 / 244 / 208 | **102 / 101 / 107** of 9100 / 9250 / 9271 |
| | **worst visit minimum** | 40.1 / 40.4 / 40.4 | 38.5 / 38.5 / 40.2 | **12.6 / 11.3 / 7.9** |

Every row above separates without overlap across three repetitions, **except the
last**, which separates the wrong way and is the subject of its own section.

The descent is the declared bottleneck of this return — the corridor record
named it as the only one — and it is where the branch does its work. The runner
also spends *fewer* samples there (1560 against 2131), because it crosses
faster rather than lingering.

### The rejoin window is mis-declared, and the rejoin is measured anyway

The frozen `rejoin` window is centred on **(1575.7, 2234.1)** with a 48-unit
radius, and is labelled `samples: [43, 44]`. That position is lane route 5
sample **43** alone. The corridor record had offered both by name — "near lane
sample 43/44, (1575.7, 2234.1, 1819.2) / (1497.6, 2154.1, 1834.8)" — and the
step-1 manifest took the first of the pair. Step 2 then settled the contract on
**44**, because 43 is the launch the branch exists to walk around on foot, and
the window was never re-declared. Lane sample 44 is **111.8 units away** — 63.8
units outside the frozen window's edge. No branch exit can fall inside it, and
none does.

A window that names two candidates has not declared one. That is the whole
defect, and it was introduced before any of this step's numbers existed.

The window is **not moved**. Choosing a window after seeing what it would do to
the numbers is the exact failure a pre-collection freeze exists to prevent, so
it is scored as frozen: `rejoin` reports **0 scored visits in every arm**, and in
the candidate the runner barely passes through sample 43's neighbourhood at all
(0 / 4 / 0 visits, against 24 / 23 / 27 in the control), because it arrives on
the lane at 44 instead.

The same obligation is then measured a second way, against a declaration that
also predates this collection: the `BOT2_BRANCH_*` envelope published in step 2
and compiled into the build that ran these worlds.
`.codex-research/pkgD-step4-20260911/transition_envelope.py` places every branch
event with the same Hermite join and measures it against lane sample 44.

| Measure | candidate, per repetition | sources-only |
|---|---|---|
| branch entries | 52 / 51 / 52 | 0 / 0 / 0 |
| branch exits admitted | 34 / 38 / 37 | 0 / 0 / 0 |
| exits the join could place | 34 of 34, 37 of 38, 37 of 37 | — |
| placed exits within the published 48-unit bound | 30 of 34, **37 of 37**, 34 of 37 | — |
| exit distance to lane sample 44 | 38.2 – 48.6, 36.4 – 47.8, 38.2 – 48.6 units | — |
| exit speed | 425.1 – 429.3 u/s | — |

Every exit the join could place lies between **36.4 and 48.6 units** of lane
sample 44, and r2's are all inside the bound. The seven readings of 48.1–48.6
across r1 and r3 are 0.1–0.6 units over a 48-unit bound and inside the join's
own residual: the gate admits at its bound, as a gate checked every frame on an
approach does. One further exit in r2 — R7f20-red at
level 93200 — reads 117.4 units and is **not** a violation: the world's play
record ends at 93025, 175 ms earlier, so the sample lies outside the span the
join can place and falls back to that last row, which 425 u/s carries about 74
units. It is listed with `position_interpolated: false` and excluded from the
range above rather than dropped. Taking the nearest 4 Hz row for *every* exit
instead of interpolating put five of r1's 34 up to 86 units out, which is the
125 ms sampling offset at 425 u/s and the reason the interpolation is there.

A corrected rejoin window belongs in the next manifest, declared from lane
sample 44 and stated as corrected after this collection. It is **not** scored
here.

### T2: the original landing turn is unmet in every arm, and precisely so

The `landing_turn` window was declared for this step, before collection, from
the **37 group-1 touchdowns retained in the step-1 control worlds** — they lie
within x 2666.5–2699.6, y 3386.8–3478.9, z 1808.0–1808.1, every one within 55
units of the declared centre. The window is opened by the touchdown itself and
closed by the next maneuver, so the 750–900 u/s approach that precedes the
landing cannot lend the turn its speed.

| Arm | Turns measured | minimum ≥ 350 | recovery to 425 within 0.5 s | no sample below 300 |
|---|---|---|---|---|
| control | 13 / 13 / 11 | **1 / 0 / 1** | **13 / 13 / 11** | 5 / 5 / 5 |
| sources-only | 14 / 14 / 13 | 1 / 0 / 1 | 13 / 14 / 13 | 7 / 5 / 4 |
| candidate | 8 / 12 / 8 | **0 / 0 / 0** | **8 / 12 / 8** | 4 / 7 / 4 |

**T2's recovery half is met in every measured turn of the control and the
candidate (one sources-only turn misses it). T2's 350 u/s minimum is met twice
in 37 control turns and never in the candidate's 28.** The
landing turn's minimum is 275–351 u/s with a median near 297, from a touchdown
at 750–900. The branch does not address this and was never going to: BR-A
bypasses the platform, and BR-B *enters* from it, after the turn is over. The
plan's own words apply — a bypass is not a pass — and this record does not
claim one.

### T3: the refusals do not stop; what follows them changes

| Arm, per repetition | refusals | drop below 200 u/s in the following second | median following-second minimum | worst | recovery to 425 within 0.5 s |
|---|---|---|---|---|---|
| control | 45 / 41 / 50 | **38 / 36 / 42** | **121.2 / 127.3 / 117.3 u/s** | 9.9 / 9.6 / 7.8 | every one |
| sources-only | 43 / 43 / 37 | 36 / 39 / 34 | 120.8 / 120.1 / 108.5 u/s | 9.6 / 3.7 / 0.0 | every one |
| candidate | 57 / 59 / 56 | **15 / 18 / 14** | **361.1 / 361.4 / 361.4 u/s** | 3.1 / 2.6 / 3.1 | every one |

Per repetition, T3 is met 7 / 5 / 8 of 45 / 41 / 50 in the control and
**42 / 41 / 42 of 57 / 59 / 56** in the candidate. The candidate is refused
*more* often — it attempts more launches, 314–343 admissions against 193–226 —
and the composition of refusals is the same by name. The recovery half — 425 u/s
within half a second — is met by every refusal of every arm. The worst single
continuation is not fixed and is slightly worse: 2.6–3.1 u/s in the candidate
against 7.8–9.9 in the control, and that worst case is the open item below.

### T4: fewer corners are reached, and the ones reached are no better

| Arm | published curves | bypassed | measured | met |
|---|---|---|---|---|
| control | 394 / 364 / 397 | 235 / 212 / 231 | 159 / 152 / 166 | 83 / 86 / 88 |
| sources-only | 384 / 385 / 330 | 226 / 230 / 186 | 158 / 155 / 144 | 83 / 83 / 77 |
| candidate | 188 / 190 / 186 | 137 / 135 / 135 | 51 / 55 / 51 | 25 / 24 / 24 |

The supported executor publishes half as many accepted curves and executes a
third as many, because the lane owns the runner through ground it used to hand
over. Among the curves it *did* execute, the pass rate is **52–57 % in the
control and 44–49 % in the candidate** — slightly worse, on a denominator a
third the size. **T4 is not advanced by this work**, and the bypass count is
reported rather than netted out: a corner the fast lane flew past is not a
corner the fallback executor proved it could hold. No constrained vertex (a
published limit below the 425 u/s cap) occurs in any arm of this fixture.

### T5: no return-time claim, in any arm

Every arm reports **8 applicable seeded returns per repetition and 0 met**.

| Case | Force | control | candidate | Target |
|---|---|---|---|---|
| R7f12-blue | 12 | 18.975 / 19.25 / 18.975 s | **17.9 / 18.4 / 17.9 s** | ≤ 16 s |
| R7f12-red | 12 | 20.6 / 19.225 / 20.6 s | 18.725 / 19.05 / 18.7 s | ≤ 16 s |
| R2-blue / R2-red | 12 | 19.05 – 19.425 s | 18.825 – 19.1 s | ≤ 16 s |
| R6b-blue / R6b-red | 6 | 22.225 / 25.6 s | 22.7 / 25.6 s | reported, not 12-Force |
| R4-blue / R4-red | 0, no Speed | 21.95 – 23.475 s, mean 295.5 – 314.5 u/s | 22.475 – 23.4 s, mean 298.0 – 308.1 u/s | ≤ 20 s and mean ≥ 300 |

R7f12-blue is the only case whose candidate range clears its control range
without overlap, by about 1.1 s. Nothing here earns a speed claim, and none is
made: the target is 16 seconds and the best measured return is 17.9. The
zero-Force cases remain 2.5–3.4 seconds over their 20-second limit with a mean
hovering at the 300 u/s requirement.

The evaluator's `low-force` family is Force 1–12, which is broader than the
target's wording ("12-Force seeded returns"); the 6-Force R6b cases are listed
separately above rather than counted against a 12-Force limit.

## The worst-case corridor sample got worse, and it is the known open item

The corridor's worst visit minimum falls from about 40 u/s to **7.9–12.6**. Six
visits per repetition sit below 100 u/s, and they are the same six every time:
R4-red, R4-blue, R5-red, R5-blue, R6b-red, R6b-blue — the low- and zero-Force
cases. Each is a 4.5–6.3 second window in which the lane owns 33–36 frames and
the supported walker owns 151–217.

`branch_outcomes.py` names the cause exactly, and it is stable to the single
event across three repetitions:

| Branch entry outcome | r1 | r2 | r3 |
|---|---|---|---|
| exit admitted | 34 | 38 | 37 |
| **released mid-branch, `takeoff_flight_resources`** | **6** | **6** | **6** |
| still open when the observation window ended | 12 | 7 | 9 |
| `abandon` | **0** | **0** | **0** |
| entries, total | 52 | 51 | 52 |

This is the open item the BR-B record already carried — a runner losing its
branch at its own tap, because outside 24 units of its guide the launch is
judged as a full flight needing 2500 ms of Speed. What this campaign adds is its
cost and a **correction to its name**. Every one of the eighteen losses across
three repetitions is `takeoff_flight_resources` at branch sample 15 (once 16),
and the six cases are the same six every time — R4, R5 and R6b, both teams:

| Case | Force at the loss | Speed remaining | Speed at the loss | Refused hop |
|---|---|---|---|---|
| R5-red / R5-blue | **26** | **650–675 ms** | 438–452 u/s | 76–78 units |
| R6b-red / R6b-blue | **30** | 0 | 301–308 u/s | 97–108 units |
| R4-red / R4-blue | 9–10 | 0 | 301–308 u/s | 98–107 units |

**It is not a zero-Force failure.** A runner with 26 Force and 650 ms of Speed
left loses the branch, and so does one with 30 Force regenerated and none. The
common factor is `speed_remaining` below the 2500 ms a *full flight* needs,
applied to a hop whose recorded landing is **76 to 108 units away**. The BR-B
record called this "the failure mode that costs a depleted runner its branch";
it costs a well-resourced runner its branch too, whenever the Speed clock is
short. That is a larger and more tractable target than the one that was carried.

Its cost is measured here: **six branches per repetition**, and the corridor's
worst case, and the worst T3 continuation, are all the same six events. It is
the single highest-value repair left in this package.

### An accounting gap found by the same table

`abandon` is emitted only when a *completed* branch is refused its rejoin. A
branch lost mid-way closes with an ordinary `release` line carrying the branch
route and **no branch event at all**, so `strafe_momentum_audit.py` counts that
entry's frames to the end of the trace. That is why the candidate's branch
frames (20259) exceed its lane frames (16649) in a build where every branch
frame is by definition a lane frame.

**Branch frames are therefore not reported as a movement share anywhere in this
record**, and `branch_frames` in `report-package.json` should not be read as one.
The entry/exit/release/open table above replaces it. The fix is a telemetry
line at the release path; it is not made here, because making it means editing
the sources this collection is bound to.

## T6, the solo half

`solo_falls.py` over all 180 worlds of all three arms, nothing filtered:

| Arm | worlds | captured | deaths | falls | worst health floor | largest airborne descent |
|---|---|---|---|---|---|---|
| control | 60 | 60 | **0** | **0** | 90 | 79.4 units |
| sources-only | 60 | 60 | **0** | **0** | 90 | 99.6 units |
| candidate | 60 | 60 | **0** | **0** | 90 | 99.6 units |

No solo falls, no deaths, and every world captured in every arm.

## T6, the defended half, at normal speed

The declared twelve-case continuity campaign on the frozen build with the branch
bundle staged: both teams, three repetitions per team and rate, 600 game seconds
and two forced opponents per world, four worlds at a time.
`.codex-research/pkgD-step4-defended/campaign.json` was frozen before the first
world and snapshots all 1104 sources; the six **1x cells are the normal-speed
defended checks** the plan requires and the 5x cells are retained beside them.

`strafe_continuity_campaign.py audit` reports the campaign **valid**: twelve of
twelve runs complete their 600 game seconds, every rate confirmed, every role
verified, two enemies observed in each, no decision-seed error, no case
declaration mismatch and no duplicate runner tape.

| Cell | captures | deaths | confinement episodes | travel idle | travel walking |
|---|---|---|---|---|---|
| **red 1x** | 9 / 10 / 12 | 14 / 12 / 7 | **0** | 2.92 % | 34.31 % |
| **blue 1x** | 10 / 11 / 10 | 10 / 10 / 9 | **0** | 2.62 % | 32.38 % |
| red 5x | 10 / 7 / 8 | 9 / 5 / 7 | 3 | 2.62 % | 32.79 % |
| blue 5x | 12 / 12 / 12 | 8 / 8 / 7 | 2 | 2.88 % | 30.75 % |

**Both teams capture at normal speed**, in every repetition, 62 captures across
the six 1x worlds. No cell records a distant stall.

### The three T6 clauses, separately

**No five-second confinement:** met in every normal-speed cell — zero episodes,
longest grounded idle 1.50–2.75 s. The five episodes that do occur are all at
timescale 5, and all five are the same thing: the runner standing **at its own
capture point** (goal distance 26–30 units) holding the enemy flag while
`home_flag_status` is *carried* — the state in which a capture is impossible by
the rules of the game. The audit's own criterion classifies all five as
`arrival_or_wait`. The longest is 194.5 s in red-5x-r2, and the runner moves
2.8 units in it. This is reported as measured; it is a game state, not a mover
that could not move, and no normal-speed cell contains one.

**Original sampled-state attribution (superseded by the engine-log correction above):** 106 deaths across the
twelve defended worlds — two opponents per world, so most are theirs — and one
of them is a fall: red-1x-r1 at level 359100, a 778.6-unit descent ending at
vz −1214.5 u/s. The runner was **not carrying the flag**, was outbound, and was
owned by the `supported` executor, not by a lane or a branch; combat had already
taken it from 70 to 17 health nine samples earlier. It is a falling death all
the same, and T6's clause is failed by it.

One correction belongs here rather than in silence. `solo_falls.py` first called
a death a fall whenever the runner had lost more than 300 units of height in the
preceding two seconds, and on that rule the defended worlds reported **two**
falls. The second, blue-5x-r1, was a runner **standing still on a floor** at
z 1368 with vz 0, killed by an opponent after descending earlier in the window.
A fall now requires the runner to be airborne when it dies; the height lost is
still reported for every death, and the count under the old rule is reported as
`deaths_after_a_descent`. The solo arms are unaffected: 0 deaths under either
rule across all 180 worlds.

**Both-team defended capture:** met, 62 captures in six normal-speed worlds.

### The branch in defended play

| Measure | 12 defended worlds |
|---|---|
| branch entries | **143** |
| admitted exits | **101** (70.6 %) |
| released `gate` / `ground_exit_floor` / `body` | 21 / 4 / 4 |
| released `takeoff_flight_resources` | **1** |
| ended by a death, with no branch record | 9 |
| still open when the 600 s window ended | 3 |
| entry reasons | `join_scan` 58, `takeoff_momentum` 39, **`takeoff_receiver_reserve` 38**, `takeoff_reserve` 7, `takeoff_flight_resources` 1 |

Two things in that table are not visible in the seeded worlds. The branch is
entered 38 times on a **`takeoff_receiver_reserve`** refusal — a resource refusal
the seeded case set never produces at an entry — so in real play the branch
answers the resource family as well as the momentum one. And the tap loss that
dominates the seeded arm occurs **once in 143 entries** here: what interrupts a
branch in defended play is an opponent, through `gate` and `body`, not the Speed
clock.

### A second telemetry gap, found the same way

Nine of the 143 entries are closed by nothing at all: no `exit`, no `abandon`, no
`release`, and no further fast-return record until the next entry. Reading the
play rows names every one of them. In red-1x-r3 the runner entered a branch at
level 27175 and **175 ms later was killed** — health 42 to −12, flag dropped —
and the branch owner was torn down without emitting a line.

That is not a movement defect; being killed by two opponents is what a defended
world is for. It is the same accounting gap as the mid-branch release, in its
second form: **the branch record cannot distinguish a death from a silent
abandonment**, and the plan's rule that a capture after silently abandoning a
branch is not a branch success needs it to. Both forms want one telemetry line
at the teardown path, and neither is fixed here, because fixing it means editing
the sources this collection is bound to.

## Checks and regressions, on the sources this collection is bound to

| Check | Result |
|---|---|
| `strafe_catalog_check` | PASS |
| `strafe_fast_return_check` | PASS |
| `strafe_intermediate_tail_check` | PASS |
| `strafe_route_resume_check` | PASS |
| `strafe_supported_wait_check` | PASS |
| `strafe_corner_track_check` | PASS |
| `strafe_hop_arrival_check` | PASS |
| `strafe_flightplan_check` | PASS — 58752 window constructions agree, byte-identical outputs including partially written refusals |
| Python suite | **444 tests, all passing** (409 before this step, plus the 35 new window checks) |
| 26 temporary-body cases (frozen M2, evaluation, accelerated) | **26 / 26 pass, 0 failures, 0 infeasible** |
| 30 resource cases (accelerated, supported corners 2, staged flight adapter) | **30 / 30 complete, none refused, 0 deaths, 60 captures** |

Both regression suites match their last recorded status exactly, and every
`codemp/` hash recorded in the check results matches the working tree
(`verify_hashes.py`: `STALE: none`). Run the Python suite from the repository
root — started inside `tools/`, three unrelated modules report eight failures
that are pure working-directory artifacts.

Evidence under `.codex-research/pkgD-step4-20260911` (manifest, driver, the six
collected repetitions, audits, three reports, six window evaluations, the
transition and outcome analyses, falls), `.codex-research/pkgD-step4-checks` and
`.codex-research/pkgD-step4-regression`.
The defended campaign, its frozen declaration, source snapshot, twelve worlds,
opposition audit and aggregate are under
`.codex-research/pkgD-step4-defended`.

## What is still open

Carried forward with their numbers, not as silence.

- **Takeoff 57 and takeoff 50 remain uncovered**, exactly as the BR-B record left
  them, and no case here seeds them. Groups 3 and 4 are likewise uncovered.
- **The tap loss is a Speed-clock failure, not a zero-Force one.** Six branches
  per seeded repetition, at 26 and 30 Force as well as at 9. It is now
  diagnosed: see the
  [tap-loss diagnosis](STRAFE_MOVEMENT_TAP_LOSS_DIAGNOSIS_2026-09-11.md). All
  six are the commitment refusing an authored tap chain on BR-A because the
  runner was more than 24 units from its steering guide; the hops are 277-352
  units long and the 76-108 figure quoted here is the runner's remaining
  distance to the landing, not the hop. It is the corridor's worst case, the worst T3
  continuation and the largest remaining loss in the package. In defended play it
  occurs once in 143 entries, so its cost is concentrated in the depleted seeded
  cases rather than spread across ordinary play.
- **Two telemetry gaps at the branch teardown path**: a mid-branch `release` and
  a death both end a branch with no branch record, so the audit counts those
  entries' frames to the end of the trace and cannot tell them apart. Branch
  frames are therefore not a movement share and are not reported as one here.
- **The frozen rejoin window names lane sample 43, not 44.** A corrected window
  belongs in the next manifest, declared before the collection it scores.
- **T2's 350 u/s landing-turn minimum is unmet in every arm**, and no branch
  addresses it: BR-A bypasses the platform and BR-B enters from it.
- **T4 is not advanced**; a third as many published corners are reached and the
  pass rate among them is slightly lower.
- **T5 is unmet in every arm including the control.** The best 12-Force return is
  17.9 s against a 16 s target; zero-Force returns are 22.5–23.4 s against 20.
- **Two engine-confirmed normal-speed runner falling deaths**, blue-1x-r1 and
  blue-1x-r3, need causal movement attribution. The previously cited red event
  is a separate combat death during a descent; see the review correction.
- **The accept rate moves with the source edit alone**, by about twice the band
  the step-1 record declared for it.
- Catalog capacity is **30 of 32**: one more branch pair fits, the next does not.
- The blue-only choke at mirrored y = 4300 is still unexplained; this collection
  measures the window at 100 % lane ownership and zero samples below 300 u/s in
  the candidate, which removes its cost without explaining its cause.
- **Nothing is installed, and K07 is not qualified.**
