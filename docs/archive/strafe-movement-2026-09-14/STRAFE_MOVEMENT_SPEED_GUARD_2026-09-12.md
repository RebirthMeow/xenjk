# A crossing speed guard that read the wrong thing — 2026-09-12

**Superseded guard semantics:** the [2026-09-13 takeover repair](STRAFE_MOVEMENT_JOIN_APPROACH_2026-09-13.md)
removes the height/contact exception described below. Speed now remains binding
when those clauses also fail; recovery belongs to the native owner. This record
and its compiled/native evidence describe the historical conditional guard.


Repairs the review finding that the
[degrade-release cycle](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md)'s fourth
correction left open, and corrects the evidence conclusions that cycle and the
[corner-span record](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md) overstated.
Nothing is installed; the installed baseline remains `reviewready-b` / `525f1b50`.

Scope: one gate in the executor's advance loop, the check that covers it, and
documentation corrections. No contract, threshold, envelope, tolerance,
denominator, compiler, route or resource-admission change.

## The defect

`Bot2_PathCommandReported` in `codemp/game/bot2_path.c` walks past a crossing
whose plane is behind the runner and whose contract it missed, and releases the
plan rather than leaving the vertex behind when the miss was speed. It decided
that by reading the refusal's reported clause:

```c
if(clause==clauseSpeed) return Refuse(...);
```

`PassedReported` names **one** condition, the first to fail in a fixed order —
plane, lateral, height, contact, speed. So a crossing that was both wide and
faster than its sweep reported `lateral`, never reached that comparison, and was
left behind with the speed unjudged. Correction 4's rule was never wrong; the
gate that implemented it could not ask the question.

Review compiled the unchanged production executor with the release check plus
one state and got `result=CONTINUE index=2` where a 362 u/s pass vertex tolerates
402 and the runner crossed at 438, ten units wide of a four-unit excursion:
`.codex-research/review-release-e3e0876b81a14a1d9cc2f8f39bdbbc61/{main.c,check.log}`.

## What the repair had to reconcile

Enforcing the speed wherever it was exceeded is not available, because one of the
states it would release is a measured repair. `Overflown`, the seeded descent's
recorded state in the release check, crosses a vertex whose published limit is
300 u/s while carrying 500, 65–75 units *below* it — so it misses height **and**
speed at once. Correction 2 exists to leave exactly that vertex behind. A guard
that asked only "was the ceiling exceeded" would release it again, and the
counterfactual below shows it does.

The rule that satisfies both, and that the executor now states:

> A crossing's swept speed is the ceiling of a turn taken on that vertex's own
> floor. It is judged together with the height window and the contact the
> contract requires, and with nothing else. Lateral offset is no part of it —
> wide of the line is still on the floor and still through the geometry — so a
> wide crossing is held to the speed. A plane crossed in flight, or above or
> below the height window, was not that turn at all, and carries no speed to
> judge; those are the misses correction 2 forgives.

`PassedReported` now reports the ceiling a second time, on its own, as the state
rather than as a name, and the gate asks that. The clause precedence is
unchanged, so every existing refusal is still named as before; the release that
the guard itself decides names `speed`, and reports the measured offset beside
it.

## Checks

`tools/strafe_path_release_check.py` gained section 10, inside the existing
four-rotation mirrored loop, and two scene builders gained the knobs it needs:
`Overflown` a speed and a ground contact, `PassTooFast` a height. Every existing
case keeps the state it recorded. The new section asks the same 500 u/s crossing
of the same 300 u/s vertex four ways and the reviewed pass vertex twice:

| State at a crossing the runner is past | Decision |
|---|---|
| on the floor, on the line, over the ceiling | released, clause `speed` |
| on the floor, 20 units wide, over the ceiling | released, clause `speed` |
| on the floor, 20 units wide, inside the ceiling | left behind, resumed down the interval |
| 70 units below it, over the ceiling | left behind |
| airborne over it, over the ceiling | left behind |
| 300 units below it, over the ceiling | released, clause `height` |
| pass vertex, 10 units wide, 438 u/s through 402 | released, `pass_not_crossed`/`speed` |
| pass vertex, 40 units above it, 438 u/s | left behind |

The releases also assert the vertex index, its own limit, the measured speed and
the measured offset, so a refusal that named speed for the wrong vertex would
fail. The whole check passes at four rotations, mirrored:
`.codex-research/speed-guard-20260912/release-check/`.

`.codex-research/speed-guard-20260912/counterfactual.py` reverts each half of the
rule alone and requires the check to fail, refusing if the text it reverts has
moved:

| Reverted | Assertion that fires |
|---|---|
| the gate asks the reported clause again | the wide, overspeed crossing continues (the reviewed defect) |
| the ceiling is asked without the floor terms | the seeded descent's overflown vertex is released again |

The degrade cycle's own counterfactual still reverts all four of its corrections
and still fails on each: `.codex-research/speed-guard-20260912/degrade-cf/`. Its
`speedmiss` variant was updated to the rewritten gate text, which is the same
correction asked a second way.

Unchanged and re-run on these sources: seam, corner tracking, corner span,
supported wait, fast return. All pass. Every check's recorded source hashes match
both the measured build's manifest and the working checkout
(`.codex-research/speed-guard-20260912/checks.json`), so the checks cover the
binary the arm was collected on and not merely the sources at some earlier
moment. The full Python suite passes: 468 tests.

## Frozen comparison

- Build: `.codex-research/strafe-build-speedguard-20260913`.
- Declaration, drivers, worlds and audits: `.codex-research/speed-guard-20260912/v1`.
- Baseline: `.codex-research/degrade-release-20260912/v2/candidate-r{1,2,3}`,
  unchanged and re-read from its retained worlds by `v1/compare.py`.
- Unchanged thirty-route assets: `.codex-research/pkgD-step3-20260911/assets`.
- Same twenty cases, three repetitions, timescale 5, 55-second observation, at
  most four isolated native worlds. All 1104 measured sources and both binaries
  match the build manifest (`v1/verification.json`); nothing changed during
  compilation or collection.
- A four-case preflight ran first (`preflight/`): all four captured, return 0.
  It is separate from the three complete repetitions.
- Both binaries report the refusing clause, so unlike the previous cycle the
  release columns are comparable on both sides.

## Results

**The guard did not fire in any of the sixty worlds.** Both arms release six
plans per repetition, and all six are the same retained family in both: the red
seeded descent's lateral miss at prefix index 3, `crossing_not_passed`/`lateral`,
in R1-red, R2-red, R3-red, R4-red, R5-red and R4-blue, every repetition. No
release in either arm names `speed`.

That is a decisive negative, not an absence of data. A fired guard necessarily
releases with clause `speed`, and the candidate's gate fires strictly more often
than the baseline's — `clause==speed` implies `tooFast`, because the clause is
only named speed when the floor terms already hold. So no speed release in the
candidate means neither gate fired anywhere in either arm, and the two binaries
decided these sixty worlds identically. Every difference below is run-to-run
variation on this declaration, and the arm measures the guard's cost only where
it does not fire. The compiled check and its counterfactuals are what establish
the decision the guard makes.

| Measure | Baseline (degrade) | Candidate (guard) |
|---|---|---|
| Captures | 20/20, 20/20, 20/20 | 20/20, 20/20, 20/20 |
| Contract releases | 6 / 6 / 6, all `lateral` | 6 / 6 / 6, all `lateral` |
| T5 | 0/8 each repetition | 0/8 each repetition |
| `localnav` frames | 38 / 36 / 31 | 35 / 47 / 46 |
| Stationary samples | 52 / 48 / 43 | 47 / 32 / 40 |
| Zero-input frames | 149 / 158 / 119 | 157 / 144 / 120 |
| Zero-input while stationary | 0 / 0 / 0 | 0 / 0 / 0 |
| Branch entries | 43 / 42 / 42 | 39 / 35 / 40 |
| Entries open at world end | 4 / 4 / 0 | 2 / 0 / 1 |
| Churn | 0 | 0 |
| Minimum z | 1808.005 | 1808.002–1808.005 |
| Health floor | 90 | 90 |
| Deaths, falls, corridor drops | none | none |

Capture time: **+0.041 s on the twenty-case mean**, 8 of 20 cases faster. Every
case moves within ±0.27 s, and the largest single mover is R1-blue (+0.267),
whose candidate r2 repetition reads 17.700 against 16.775 and 16.975 — one
repetition of one case. R5-blue reproduces its known split unchanged in both
arms: one fast repetition each (17.875 baseline, 17.850 candidate) against
21.5–21.7 in the other two, for +0.058 s on the mean, the same figure the
previous cycle reported against its own baseline. Per-case seconds are in
`v1/report.json`; the bevel and ramp neighbourhood minima overlap between arms
at 237.2–320.9 and 237.2–241.5 u/s with no sample below 150 (`v1/summary.json`).

Body and resource regressions match the baseline exactly: the 26-start temporary
body subset passes its thresholds with sources unchanged, and all 30 accelerated
resource cases complete with 60 captures, 0 deaths and no refusals
(`v1/temporary-body`, `v1/resources`).

## Corrections to the retained evidence

Three conclusions were stated more strongly than their instruments support. The
numbers behind them are unchanged and nothing was erased; each record carries a
dated annotation.

**Limited Force is not a movement-time lower bound.** The corner-span record's
travel section read that its attribution "says why T5 cannot be reached on these
cases by movement alone", and the scorecard led with "T5 is not reachable on
these cases by movement alone". What is measured is the resource state: 12 Force,
one 13.275-second Speed window, regeneration to 25–40 against the 50 a level-3
cast costs, and therefore a 250 u/s tail over the final 4.5–5.5 seconds. No
phase-by-phase budget at achievable speeds was computed over the rest of the
return, so infeasibility was an inference. T5 stays **failed**, 0/8 per
repetition, and its feasibility stays **open**.

**The 0.13-second-per-ramp hop ceiling is not established.** Both speeds it was
derived from are means over *grounded* supported frames:
`tools/strafe_hop_attribution.py` skips every frame whose trace row is airborne,
and a frame it labels `hop` is one where the gate would have admitted a hop at
that position, not one where the runner left the floor. The comparison is
therefore between two sets of ground frames, and the speed a hop reaches in
flight — the thing a hop would buy — is exactly what the instrument excludes. The
flatness gate and the missing tall-envelope clearance evidence stand as recorded.

**The corner-span arm's source binding cited the wrong file.** Its frozen
comparison names the `span7` build and the `v2` arm but cited
`v1/verification.json`, which binds the `span4` build of the retained
intermediate arm. Both files exist and both bind 1104 sources, so the wrong
citation read as a valid binding for an arm it does not cover. Corrected to
`v2/verification.json`.

## Status

Implemented, checked and measured. Nothing is installed. K07 is not qualified by
this: a guard correction that never fires on the declaration cannot qualify a
travel target, and T5, T2/T4 and the defended T6 obligations are unchanged and
open.

Open, and named rather than repaired here:

* **The declaration does not exercise the guard.** Sixty worlds produced no
  crossing that was past, overspeed and on its vertex's floor. The state is real
  — the executor accepts published plans whose vertices carry pass and limit
  contracts, and the review built it from those contracts — but a world that
  reaches it has not been found, so the guard's cost and its consequence for the
  owner's re-acquisition are untested in native collection. A case that produces
  it would be worth more than another repetition of this arm.
* The two defects the degrade cycle named are unchanged and unrepaired: the red
  seeded descent's lateral family at index 3, six worlds per repetition, and the
  fast-return join window R5-blue's time depends on. Neither is an executor
  release decision.
* R5-blue's start-phase attribution remains provisional. Both arms here are
  degrade-lineage, so this comparison does not address whether its regression
  against the corner-span arm is independent of the build; matched early/late
  preparation conditions on both the span baseline and a degrade-lineage
  candidate are still the outstanding experiment.
