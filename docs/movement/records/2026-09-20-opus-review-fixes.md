# Opus follow-up review corrections

Recorded 2026-09-20. This correction supersedes conflicting metric, causal and
next-action claims in the earlier same-day leg-budget, search-rate, route-pricing,
trigger-classification, exit-speed and start-level records. Their original
observations and retained artifacts remain available; none was deleted.
No native experiment or installed-game change is part of this correction.

## Completed trips and analysis windows

The 100-world finer sweep used a shorter safe analysis window as if it were
completed-trip time. Re-read official completion results with
[movement_sweep_summary.py](../../../tools/movement_sweep_summary.py). The three
common `corner_level` strata 3300, 3325 and 3350 have pooled counts 8, 34 and 43.
These weights cover 85 of 100 worlds; the other 15 remain reported separately.
They were selected after collection, so this is an exploratory comparison.

| Exit-speed setting | Weighted official completed time |
| ---: | ---: |
| 250 | 17,242.6 ms |
| 500 | 17,084.4 ms |
| 750 | 17,830.6 ms |
| 1000 | 17,830.6 ms |
| 2000 | 18,040.6 ms |

The 500-versus-2000 improvement is **956.2 ms**, not the previously quoted
966 ms analysis-window difference. At common starts 3300/3325/3350, 500 means
19,375 / 15,925 / 17,575 ms and 2000 means 19,375 / 16,450 / 19,050 ms.
The observed maximum for 500 is 21,125 ms. These facts support a provisional
research setting; neither 15-second attainment nor production qualification
follows. Weighted means are not medians or whole-campaign means.

Across all 20 worlds per arm, the unconditioned means are 17,930 ms for 500
and 18,731.25 ms for 2000; maxima are 21,125 and 20,625 ms respectively.
The average improves while the observed tail worsens. Start distributions
differ, so these means also do not replace the planned matched confirmation.

Corrected retained summary:
`.codex-research/opus-review-fixes-20260920-a/official-sweep.json`.

```powershell
python tools/movement_sweep_summary.py `
  .codex-research/exit-speed-sweep-20260920-a `
  --arms 250 500 750 1000 2000 --rounds 20 `
  --strata 3300 3325 3350 --out <new unique output.json>
```

The script verifies the complete grid, passing evaluation, consistent case and
exit-speed arm, and records input hashes. It does not independently certify all
physics or source/binary bindings. It does not rerun native worlds. The old
local `analyse.py` remains historical; its touchdown-class groups are downstream
of treatment and are unsuitable as causal strata.

## Budget corrections

LEG_BUDGET2 reports `analysis_window_ms` separately from official `completion_ms`;
`leg_ms` remains a compatibility alias for the analysis window. It validates
both endpoints as well as internal cadence, so a truncated trace cannot silently
become a shorter valid budget. A speed integral alone does not prove a fixed
route or a physically achievable mean speed.

The earlier 473-frame, 54.2% waiting category included 59 terminal frames with
no recorded candidate search. Neutral `precommit_grounded` and
`uncommitted_grounded` categories avoid attributing an absent commit to failed
planning. A single grounded sample alone does not establish a tap command.
Fresh eight-world LEG_BUDGET2 evidence reports 341 precommit, 132 uncommitted,
213 run-up, 102 single-grounded, 80 start and 4 committed-non-run-up frames
(total 872). All eight pass complete coverage. Evidence:
`.codex-research/opus-review-fixes-20260920-a/leg-budget-aim-on-v2.json`.
Do not treat the old percentages as causal attribution.

Entry speed now uses the first grounded sample, not the preceding airborne
sample. The hypothetical speed-preservation calculation never reduces an
observed speed and is explicitly hypothetical. It is neither a lower bound nor
an achievable forecast: feasibility and subsequent flight dynamics require
native rollouts. The prior claimed lower bound is withdrawn. The old
2,895-units/sec single-frame friction-loss attribution becomes zero when entry
is measured at the actual grounded sample; it was not a measured friction cost.

## What remains useful, and what happens next

Start level strongly predicts the retained results on this build and case.
Pinning and verifying it improves comparison design. This does not prove that
all variation is polling phase, that treatment cannot affect fixture timing, or
that one world per cell replaces confirmation. The rejected pricing and search
pilots remain useful negative results for their tested variants, not universal
refutations. Observed hurt-volume contacts justify retaining the safety gate,
not declaring every possible speed-preserving continuation impossible.

Next: fresh repeated matched 500/2000 confirmation on predefined pinned starts,
including 3275 where 500 regresses and the common 3300/3325/3350 starts. Declare
counts and weights first, retain setup misses, report unreachable strata as
incomplete, and keep floors unpinned. Use official completed times, tails,
failures and CPU. After that, reassess bounded continuation ranking against a
specific observed selection failure. Keep 15 seconds, runtime qualification and
parity qualification open; zero parity comparisons is not agreement evidence.

## Verification and cleanup

Sixty focused tests pass: 15 leg-budget, 8 sweep-summary, 5 momentum-audit and
32 benchmark tests. The budget regressions cover missing endpoints, neutral
grounded attribution and a speed floor that cannot lower observed samples.
The summary checks complete grids, failed/inconsistent results, explicit
strata and refusal to overwrite an existing output. Documentation links and
patch whitespace were checked. No native rebuild was needed for these Python
and documentation changes. Historical evidence is retained; cleanup replaces
misleading active claims and labels rather than removing evidence.
