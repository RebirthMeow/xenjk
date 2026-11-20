# 2026-11-10: timed pairs on the new defaults (pool scopes, the guide part, the replan bound)

Owner, 2026-11-10: 30 minutes of an idle machine for the timed runs the 2026-11-09 work left
([replan budget](2026-11-09-replan-budget.md), [guide-part captures](2026-11-09-guide-long-captures.md)).

## Method

Build `.codex-research/review-next-build-20261110-g` (1fb110d0: `claude/bot2-review-next` with the owner's new
defaults of 2026-11-10, `bot_movePoolScopes` 1, guidance on). The milestone-B 4v4 (`laps_campaign.COMMON` +
`--two-sided`), 600 game s at rate 25, frame meter and slow-frame log on (`bot_moveFrameMeterSlow 25000`), one world
at a time, pinned to CPUs 12 and 14 (two P-cores, as on 2026-10-30; the machine was 18% busy with another session's
work at the start, those cores idle). Seeds 1-2, one repetition each, 12 worlds in 29 minutes, all exit 0. Arms:
`ctl` (the defaults), `a` (`bot_movePlanGuideLong 4000`), `ab` (`a` + `bot_movePlanBound 4000`), `p0`
(`bot_movePoolScopes 0`). Evidence `.codex-research/timing-20261110-a/` (`runtiming.py`, `run.sh`, `summary.py`,
`summary.txt`, the worlds and their `.load.csv` samples of the sibling threads 13/15).

## Result (seed 1 / seed 2)

| | ctl | a | ab | p0 |
| --- | --- | --- | --- | --- |
| Kejim p99, ms | 18.25 / 20.25 | 18.5 / 20.75 | 19.0 / 22.25 | 22.25 / 18.75 |
| Kejim frames over 25 ms | 66 / 115 | 86 / 112 | 95 / 185 | 168 / 81 |
| Kejim frames over 50 ms | 5 / 25 | 7 / 18 | 7 / 42 | 14 / 23 |
| Kejim frames over 100 ms | 4 / 7 | 2 / 6 | 2 / 6 | 4 / 4 |
| Kejim worst frame, ms | 134 / 149 | 138 / 131 | 152 / 136 | 215 / 125 |
| Dantooine p99, ms | 28.5 / 29.5 | | | 26.75 / 30.25 |
| Dantooine frames over 25 ms | 394 / 467 | | | 300 / 514 |
| Dantooine worst frame, ms | 114 / 84 | | | 1,117 / 83 |

Outside load on the sibling threads was 0.1-0.7% in most worlds, but 5-11% in `ab-kej-2`, `p0-kej-1`, both
Dantooine `ctl` and `p0-dan-2`; those are the worlds with the worse tails in their pair, so load, not the arm, is the
likeliest cause. `p0-dan-1`'s 1,117 ms frame is one hitch (frames over 100 ms 13, against 0-1 elsewhere).

## Reading

- **One repetition cannot separate these arms.** Every difference above is within the spread between seeds and
  within what sibling load explains; single-world wall-clock tails swung 2.4 times between identical runs before
  (2026-10-06). Nothing here contradicts the work-count results.
- **The guide part** (`a`, `ab`): on today's defaults the Kejim control already has only 4-7 frames over 100 ms
  (24.5-25.5 before guidance, 2026-10-30), and the guide part does not visibly lower them (2 / 6). Its work-count
  gain (worst frame -18-25%) does not show as wall clock at this sample. No case for making it a default from this;
  it stays 0. Captures are settled (no change within 15%), so a later decision needs only more timed repetitions.
- **Pool scopes** (`p0` against the default 1): mixed, within noise on both maps. It stays 1 (lossless; 22-29% fewer
  engine traces).
- **Dantooine** still misses the 25 ms p99 target (28.5-29.5 ms, 394-467 frames over 25 ms; it was 27.5-27.75 ms and
  333-364 on 2026-10-07, before the competitive defaults and later changes).
- Not run: the BP-2 profile (no time left in the window), repetitions.
