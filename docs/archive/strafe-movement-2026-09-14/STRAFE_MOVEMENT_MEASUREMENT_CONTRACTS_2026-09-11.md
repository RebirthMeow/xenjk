# The corrected measurement contracts, and what they move (2026-09-11)

The second half of step 1 of the
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order):
repair the audit and window-evaluator contracts the September 11 review found
wrong, version the corrected reanalysis in new outputs, and preserve every old
manifest and report.

**No native world was rerun and no frozen output changed.** The step-4 and
control evaluations keep their 2026-09-11 05:30 bytes; every file produced here
is a `-v2` under `.codex-research/pkgD-contracts-20260911`. No `codemp/` source
was touched, so the working tree still matches
`strafe-build-brb3-680c73cce06841b89886aabe6bd6ec63`
(`verify_hashes.py`: `STALE: none`) and the frozen collection stays bound to its
build. Nothing is installed and K07 is not qualified.

## Six corrections

| Defect the review named | What now happens | Where |
|---|---|---|
| A recovery search ran forward through a death, respawn, capture or possession change, so the next life could supply the recovery this one lost | `life_boundaries` reads those four transitions and the declared setup teleport from the play rows; `recovery` stops before the first boundary past its anchor and every record carries the bound that applied | `tools/strafe_branch_windows.py` |
| A slow entry's recovery was measured from window **exit**, which hands the runner the whole window for free | Scored from window **entry**; the after-exit pair is retained under its own keys, and each verdict names its `anchor` | `evaluate_branch_window` |
| T2's recovery was anchored at the turn's **minimum**, not at the declared **contact** | The contact deadline scores `met`; the minimum-anchored figure stays as a diagnostic, both with their own bounds | `landing_turn` |
| T3 could satisfy "425 within 500 ms" **at the refusal itself**, before anything happened | The declared gate is still scored, and beside it: `already_at_target_at_refusal`, the rebuild from the following second's own minimum, and whether the recovery held | `refusal_continuation` |
| T5 pooled 1–12 Force, marked every noncapture `applicable=False`, and never gated the post-expiry mean | Exact-12 and 1–11 are separate families; a death or timeout is a **censored** attempt that stays in the denominator; the post-Speed-expiry mean is scored | `travel_family`, `evaluate_travel` |
| Branch frames counted every sample to the end of the trace when an entry had no terminal event, so branch frames exceeded lane frames | An entry with no terminal event is closed at the next release or the first sample the lane does not own; only lane-owned samples count; `reconciled` is false while any substitution was needed, and the report refuses to compare an unreconciled frame count | `tools/strafe_momentum_audit.py`, `tools/strafe_branch_report.py` |

A seventh, at the report level: a configuration missing a declared case, short
of its declared repetitions, or carrying a world the audit could not read is now
**incomplete**, and an incomplete configuration blocks every comparison verdict
instead of appearing in a field beside them.

Fifteen new pure checks hold these contracts, each named for the mistake it
prevents. The suite is **459 tests, all passing** (444 before), run from the
repository root as `PYTHONPATH=tools python -m unittest discover -s tools -p
"test_*.py" -t tools`.

## What the corrections move, per repetition, never pooled

### T3: the improvement is real and it is not the met count

The declared gate's counts do not change — control 7 / 5 / 8 of 45 / 41 / 50,
candidate 42 / 41 / 42 of 57 / 59 / 56 — but almost every "met" row was already
above the target when the launch was refused:

| Arm | applicable | met | of those, already ≥ 425 at refusal | met **and** rebuilt from the minimum | median slowest speed in the following second |
|---|---|---|---|---|---|
| control r1/r2/r3 | 45 / 41 / 50 | 7 / 5 / 8 | **7 / 5 / 8** | 3 / 4 / 4 | **117.3–127.3** |
| sources-only r1/r2/r3 | 43 / 43 / 37 | 7 / 4 / 3 | **7 / 4 / 3** | 5 / 3 / 3 | 108.5–120.8 |
| candidate r1/r2/r3 | 57 / 59 / 56 | 42 / 41 / 42 | **39 / 38 / 38** | 37 / 37 / 37 | **361.1–361.4** |

Every control "met" row is an initial condition. The candidate's are too, in 38
of 41 — but the figure that separates the arms is the one the old evaluator
never reported: the **median slowest speed in the second after a refusal is
117–127 u/s in the control and 361 u/s in the candidate**, and it repeats to a
tenth of a unit across three repetitions. That is a factor of three on the same
case set, and it is a stronger statement than "42 of 57 met" because it does not
rest on where the refusal happened to start. Between 14 and 20 control rows per
repetition (17 to 23 in sources-only, 13 to 18 in the candidate) rebuilt from
their own minimum within 500 ms and still failed the gate on a stationary or
sub-200 sample.

### T2: the contact anchor is not what fails it

Recovery to 425 within 500 ms **of contact** is met in every turn of every arm
(13/13, 13/13, 11/11 control; 14/14, 14/14, 13/13 sources-only; 8/8, 12/12, 8/8
candidate), exactly as the minimum-anchored figure was. One sources-only turn
moves from failed to met, in the direction of the runner. So T2 fails on its
**350 u/s minimum alone**: met 1 / 0 / 1 control, 1 / 0 / 1 sources-only,
**0 / 0 / 0** candidate, unchanged.

### T2-branch: the corridor loses three visits per candidate repetition

Anchoring a slow entry at window entry costs the corridor window one visit per
control and sources-only repetition and **three per candidate repetition**; the
candidate's six slow-entry corridor visits go from six met to three:

| Window | control r1/r2/r3 | sources-only r1/r2/r3 | candidate r1/r2/r3 |
|---|---|---|---|
| corridor, frozen | 57/69, 54/65, 57/71 | 53/67, 60/70, 53/62 | 84/93, 92/98, 87/96 |
| corridor, corrected | 56/69, 53/65, 56/71 | 52/67, 59/70, 52/62 | **81/93, 89/98, 84/96** |

`descent` loses one visit in control-r3 (14/57 to 13/57) and nothing else moves:
`entry`, `blue-choke`, `descent` elsewhere and `rejoin` are identical. The
frozen rejoin window still scores nothing in the candidate's r1 and r3 and four
visits in r2, because it still names lane sample 43.

### T5: eight in the denominator, six scored, and a new failure nobody had gated

The old evaluator reported "0 of 8 applicable" per repetition. The eight are
three different obligations:

| Family | per repetition | measured | target | verdict |
|---|---|---|---|---|
| exact-12 seeds | 4 | control 18.98–20.60 s, sources-only 18.98–20.60 s, **candidate 17.90–19.10 s** | 16 s | fails in every arm; the candidate is faster in every seed |
| zero-Force / no-Speed | 2 | 21.95–23.48 s, mean 295.5–314.5 u/s | 20 s, mean 300 | fails on time in every arm |
| Force 1–11 | 2 | 22.2–25.6 s | none declared | reported, **not scored** — the old tool pooled these into the 16-second target |
| post-Speed-expiry mean | 4 of the exact-12 seeds | control 217.0–293.8, sources-only 217.0–293.8, **candidate 214.0–234.7 u/s** | 300 | **fails in every arm, four per repetition** — newly scored, and the candidate is *lower* than the control |

The zero-Force episodes have no post-expiry samples at all, because Speed is
never active in them; that obligation is reported as inapplicable to those two
rather than as passed.

### Branch frames: reconciled, and therefore smaller than the lane

| Measure | frozen | corrected |
|---|---|---|
| candidate r1 branch frames | **20259** — larger than the arm's 16649 lane frames | **11949** |
| entries | not reported per client | **52** |
| closed by an `exit` | 34 | 34 |
| closed at a mid-branch `release` (substituted) | counted to the end of the trace | **6** |
| still open when the trace ended | counted to the end of the trace | **12** |
| samples inside a span the lane did not own | counted as branch frames | **6, excluded** |

Across the candidate's three repetitions **46 entries close with no terminal
event**, so `report-package-v2.json` reports the candidate as
`branch_frames_reconciled: false` and every branch-frame measure comes back
`blocked: branch frames are not reconciled with terminal events and are not a
movement share`. Every other measure still compares, and each one reproduces the
step-4 band exactly: control churn 29/25/34, joins 72/65/82, releases 47/41/55,
lane frames 7667/7006/7909; candidate churn 0/0/0, joins 41/45/41, releases
30/33/30, lane frames 16649/17810/16929, entries 52/51/52, exits 34/38/37. The
audit correction moved the branch accounting and nothing else.

## What these corrections do not fix

- **One terminal event per branch instance** is still missing, and the
  substitution above is a reading of the evidence, not a record. Branch instance
  identity, native event-time position, velocity, contact, latch and resources,
  and XYZ in the 40 Hz trace all need runtime changes; they belong to step 2.
- **The rejoin window still names lane sample 43.** A corrected sample-44 window
  must be declared in the next manifest, before the collection it scores, and
  the sample-43 result stays as published.
- **The Hermite join is still the position source.** Its leave-one-out residual
  is reported per world and can exceed a narrow window's margin; that is a
  diagnostic, not a bound.
- **No verdict here becomes a pass.** T2, T3, T4 and T5 remain unmet, T2-branch
  remains partial, and the corrected T5 adds a failure rather than removing one.

## Evidence

`.codex-research/pkgD-contracts-20260911`: `rescore.sh` (what was re-scored and
with which corrections), nine `*-audit-v2.json`, nine `*-windows-v2.json`, three
`report-*-v2.json`, and `contract_delta.py` with `contract-delta.json`, which
compares each frozen evaluation with its corrected counterpart per repetition
and reports only where they differ.

```bash
bash .codex-research/pkgD-contracts-20260911/rescore.sh
python .codex-research/pkgD-contracts-20260911/contract_delta.py .codex-research/pkgD-step4-20260911 .codex-research/pkgD-contracts-20260911
```
