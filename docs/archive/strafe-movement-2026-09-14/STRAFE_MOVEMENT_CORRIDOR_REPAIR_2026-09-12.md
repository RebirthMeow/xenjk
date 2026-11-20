# Corridor and rejoin repair — 2026-09-12

## Scope and cause

This owner-only repair tightens automatic momentum-tap admission. It preserves
BR-A/BR-B authoring, authored jump admission, branch rejoin gates and the shared
strafe kernel. It is not installed and does not qualify K07 or T2–T6.

Retained tapfix3 first visits in R4/R6b red/blue launch an opportunistic tap at
BR-A indices 31/32 just before a sharp turn. Incoming velocity persists in air
while the guide turns; the runner misses the upper corridor and reaches
z1368.125, about 440 units below it. The same worlds' second, grounded visits
traverse the bend and rejoin. The cases are seeded depleted; Force has regenerated
by the turn, while Speed remains absent. Release at indices34–38 is a consequence,
not the onset of the tracking error.

R5-blue's two retained tail failures are grounded heading refusals: prior hops
leave a lateral correction opposite the onward lane tangent. The 250ms deadline
from entering the final segment expires during that correction. The new admission
rule is tested against those approaches; the deadline and heading gate remain
unchanged. They are not generally qualified by a successful scoped replay.

## Implementation

`FastReturnMomentumRoom` requires actual guide alignment within the existing
24-unit envelope, incoming heading within 15 degrees, and checked flat edges
whose tangents stay within 12 degrees of the initial direction. It reserves one
second of current-speed travel plus 48 units, subtracting distance already
traveled along the current edge. At least four edges are checked; a sample count
alone no longer constitutes sufficient landing room. An intervening authored
jump, bend, unchecked edge or endpoint with insufficient room refuses the
optional tap. Ordinary authored jumps retain their existing admission path.

The native contract already requires gravity 800, base speed 250 and 25 ms commands.
Force jump's minimum-height assist can raise a tap's launch velocity to about
309 u/s; retained automatic taps take approximately 0.85–0.875s to land. The room
budget covers those measured states. This geometric admission rule is not a
swept-trajectory proof against arbitrary disturbance or all future maps.

## Frozen evidence and verification

- Build: `.codex-research/strafe-build-corridor-4cc4c8199fba4c3ab41ce3814e445d64`.
- Declaration and source snapshots: `.codex-research/corridor-fix-20260912/manifest.json`,
  `ai_bot2_route.before.c`, `strafe_fast_return_check.before.py`, `owner.diff`, `tests.diff`.
- Unchanged inputs: `.codex-research/pkgD-step3-20260911/assets`; twenty cases from
  `.codex-research/depleted-return-20260910-a/cases.json`.
- Baseline: tapfix3 `candidate-r{1,2,3}` under `.codex-research/pkgD-tapfix-20260912`.
- Six-case preflight: R4/R5/R6b red/blue. All captured, no lower-floor departure,
  all completed branches rejoined, and 0–2 localnav frames per world.
- Candidate collection: three full twenty-case repetitions, timescale 5, 55-second
  observation, same flight artifact, owner toggles and audit semantics as baseline.
- Pure owner check passes, including bent checked lines, densely sampled short
  lines, off-guide launches and permitted straight hops. Catalog and resume checks
  pass. First owner-check attempt lacked a fixture-only DotProduct definition;
  its linker failure is retained at `owner-check-v1`, corrected pass at `owner-check-v2`.
- Required 26-body/30-resource regressions run separately after outcome collection,
  keeping at most four isolated native worlds active. Sources remain frozen.

## Results

| Measurement over twenty worlds | Tapfix3 r1 / r2 / r3 | Corridor repair r1 / r2 / r3 |
|---|---|---|
| Captured worlds | 20 / 20 / 20 | 20 / 20 / 20 |
| Deaths | 0 / 0 / 0 | 0 / 0 / 0 |
| Minimum recorded z | 1368.125 / 1368.125 / 1368.125 | 1808.004 / 1808.004 / 1808.001 |
| Localnav frames | 775 / 765 / 735 | 4 / 3 / 5 |
| Stationary samples (speed below 20) | 436 / 437 / 419 | 58 / 48 / 49 |
| Zero forward/right samples | 656 / 627 / 619 | 118 / 82 / 102 |
| All-reason rapid churn | 0 / 0 / 0 | 0 / 0 / 0 |
| Branch entries | 46 / 45 / 38 | 46 / 42 / 40 |
| Admitted exits | 37 / 34 / 30 | 39 / 39 / 37 |
| Explicit branch abandonment | 3 / 2 / 2 | 0 / 0 / 0 |
| Other terminal ends | 2 / 2 / 3 | 0 / 0 / 0 |
| Entries still open at world end | 4 / 7 / 3 | 7 / 3 / 3 |
| Rejoin-refused releases | 3 / 2 / 2 | 0 / 0 / 0 |

The lower-floor departure is absent across all 60 candidate worlds, including
all twelve affected R4/R6b first visits. Completed branches rejoin; entries
still open at the observation endpoint remain censored, not passes. Minimum
health is 90 in both arms. These are accelerated solo outcome runs, not timing
benchmarks or defended qualification.

The completed authored-tap repair is preserved: 31 gradable resource releases
per repetition, zero on low jumps, zero unpaired releases. Flight-resource
refusals are 4/4/4 versus 1/2/1 before; the changed approach states still encounter
resource limits. No claim of eliminating every resource refusal is made.

T5 remains 0/8 in every candidate repetition, read from per-episode verdicts.
The window tool's printed aggregate `applicable=0` is stale relative to its
`in_denominator` verdict field; do not quote that aggregate as zero attempted
T5 cases. All detailed window outputs and failures are retained. Successful
branch transitions do not establish every T2 speed-window gate.

- Body regression: 26/26 passed, sources unchanged, subset thresholds passed.
- Resource regression: 30/30 complete, none refused, all runners returned zero.
- These fast-return-off subsets do not earn their broader milestone gates;
  their summaries correctly keep `milestone_pass=false`.
- Owner, catalog and resume pure checks passed; Python syntax check passed.
- Falsification: new bent-line assertion fails with the old production admission
  function at `falsify-old-admission/check-v2.log`. The first configuration's
  compiler-environment failure is retained in `check.log` and its build directory.
- Current source hashes match the fresh build manifest. Each native world retains
  its source/binary/asset provenance. No measured source changed during collection.

Reproduce the totals with `compare.py`; inspect `comparison.json` for per-case
terminals. Audits, windows, falls and tap losses are saved separately for each
repetition under `.codex-research/corridor-fix-20260912`. The preflight is reported
separately and is not pooled into the three twenty-case repetitions. No files
were deleted, no game install was changed, and pre-edit source copies remain.

## Next work

Build a non-overlapping retained-evidence T5 phase ledger: owner, ground/air,
preparation, curve traversal and stand approach, with distance, time, speed,
stops, resources and an explicit unclassified bucket. Reconcile totals to the
return episode, keeping censoring. Start with R2 and R7f12 in both directions,
then zero-Force families and the corresponding frozen controls.

In retained tapfix3 r1, every post-expiry frame in the four failed exact-12
first episodes belongs to `supported`. The first continuous grounded portion
lasts 3.1–4.0 seconds at mean 198.6–231.2 u/s. Reuse `travel`, `play_track`,
`locate`, `read_supported` and `read_console` in `strafe_branch_windows.py`, and
episode boundaries in `strafe_return_audit.py`. This identifies the next
investigation; it is not a completed causal attribution or a T5 repair.

