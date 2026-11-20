# The exit-speed credit is set too high: 2000 down to 500

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The pilot and paired timing tables below report LEG_BUDGET1 analysis-window
> durations, including the row originally labeled "Mean completed". They are not
> official completed times; the waiting counts also inherit that tool's original
> classification defect. Preserve these as historical diagnostics. Use official
> `evaluation.time_ms` for performance and the corrected finer-sweep comparison
> for the provisional 500 setting; independent pinned confirmation remains open.

Recorded 2026-09-20. **No source change, no rebuild** - `bot_moveExitSpeed` is
a cvar and every run is on the frozen reference build
`.codex-research/successor-aim-build-20260920-a`. Screening in
`.codex-research/exit-speed-20260920-a`, confirmation in `-b`, floors in
`.codex-research/exit-speed-floor-20260920-a`.

## Why this was tried

`ExitCredit` is `landSpeed * (bot_moveExitSpeed * .001)` and the ranking is
`score = margin + ExitCredit`. At 2000 the credit is **twice the landing
speed**: in a logged commit, `exit_credit=946.7` against `margin=539.3`, so the
credit was about twice the term it supplements and dominated the choice.

[The trigger classification](2026-09-20-trigger-classification.md) had shown
that the candidates refused at the expensive landings are flights into the hurt
volume, and [the search-rate record](2026-09-20-search-rate.md) had shown those
frames are availability rather than latency. Both point at the decision *before*
the landing. The credit rewards exactly the landing speed those refusals then
reject, which made it the cheapest remaining hypothesis to test.

## Screening: the relationship is not monotonic

Three runs an arm, alternating (screening only, excluded from the confirmation
below):

| `bot_moveExitSpeed` | Legs (ms) | Median | Waiting frames | Mean landing speed |
| ---: | --- | ---: | ---: | ---: |
| 0 | 17,275 / 20,025 | 18,650 | 83.0 | 415.9 |
| 500 | 15,750 / 15,775 / 19,250 | 15,775 | 46.0 | 448.0 |
| 2000 | 16,275 / 19,000 / 19,275 | 19,000 | 75.3 | 430.5 |

Removing the credit entirely is worse than keeping it, so this is not "the
credit is bad". It is priced too high.

## Confirmation: eight fresh counterbalanced pairs

| Pair | Control (2000) | Test (500) | Delta |
| --- | ---: | ---: | ---: |
| 1 | 19,025 | 17,500 | -1,525 |
| 2 | 16,950 | 19,275 | +2,325 |
| 3 | 19,025 | 19,125 | +100 |
| 4 | 20,000 | 17,525 | -2,475 |
| 5 | 19,000 | 15,775 | -3,225 |
| 6 | 20,025 | 15,750 | -4,275 |
| 7 | 19,250 | 15,775 | -3,475 |
| 8 | 16,275 | 21,025 | +4,750 |

| | Control (2000) | Test (500) |
| --- | ---: | ---: |
| Mean analysis window (legacy) | 18,693.8 ms | **17,718.8 ms** |
| Median | 19,025 ms | **17,512.5 ms** |
| Worst | 20,025 ms | **21,025 ms** |
| Best | 16,275 ms | 15,750 ms |
| Mean speed | 387.7 units/sec | 408.7 units/sec |
| Waiting frames | 77.9 | 60.9 |
| Run-up frames | 32.2 | 25.9 |
| Rollout median / max | 39.2 / 80.9 ms | 38.4 / 80.9 ms |
| Distinct traces | 8 | 7 |

Five pairs improve, three regress. Mean paired delta **-975 ms**, median
**-2,000 ms**.

**The predicted mechanism moves with it.** Waiting frames fall 22 % and run-up
frames 20 %, which is what the hypothesis said should happen if the credit had
been buying landing speed the geometry then refuses. Mean speed rises 21
units/sec. Search cost is unchanged, so none of this is paid for in CPU.

**The tail is worse.** The worst run is 1,000 ms slower than the control's
worst, and pair 8 regressed by 4,750 ms. That is the honest cost of the change
and it is not explained.

The 15,750-15,775 trajectory appears **five times** across the test arms of the
screening and the confirmation, and **never in any control arm anywhere in this
session** - the best control result recorded is 16,275. That is the strongest
single signal here, and by this project's own standard it is still not a claim
of typical 15.75-second performance.

## Floors

At `bot_moveExitSpeed=500`: **36/36 pass, zero deaths, zero parity mismatches,
movement owner throughout.**

Four blue cases first came back `incomplete` with
`RuntimeError('corner start refused: None')`, `samples=0`, `elapsed_ms=0` - the
fixture never started, so nothing about movement was measured. Free memory was
14.5 GB, so this is not the known memory failure. Re-run serially at the same
setting, all four pass (`blue_out_L2_respawn4` 12,800; `blue_out_L3_stand`
12,775; `blue_out_L3_respawn5` 9,350; `blue_out_L3_respawn4` 12,700). The cause
is RCON setup contention at `--parallel 4`, not the change. Both the failed and
the replacement evidence are retained.

## Status

Comparable in size to the successor-aim result this configuration already
carries (that was mean -897 ms, median -387.5), better on the median, worse on
the tail. Parity still makes **zero comparisons**, so nothing here qualifies
forecast/execution agreement.

This is a configuration value, not code: adopting it means changing the
research configuration's `bot_moveExitSpeed` from 2000 to 500. It is **not
installed** and production enablement remains separate open work.

Not established: the value of 500 itself. 0 is worse and 2000 is worse, so the
optimum is somewhere between and this sweep has three points. A finer sweep
around 250-1000, judged on the owner split, is the obvious follow-up, and the
unexplained tail should be looked at before anything is adopted.
