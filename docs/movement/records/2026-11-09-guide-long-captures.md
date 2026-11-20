# 2026-11-09: capture campaign on the guide part (`bot_movePlanGuideLong 4000` against the defaults)

Next action 2 of the [2026-11-09 Bot2 review](../../review/2026-11-09-bot2.md). The guide part ("A" of
[2026-11-09-replan-budget.md](2026-11-09-replan-budget.md): guide a state search still running at 4,000 expansions)
cut Kejim's worst-frame work 18-25% but read team captures -0.28 +- 0.25 (Kejim, 32 pairs) and -0.28 +- 0.36
(Dantooine, 16): undecided, both down. This campaign closes the capture question. It does not close the timed
question (frames over 50 and 100 ms on pinned cores), which is the owner's.

## Method

`laps_campaign.py`'s rule (laps_compare's verdict on team captures: an effect past 2.5 se, or the 95% t interval
inside +-15% of the control's mean), applied to seeded paired worlds and pooled with the replan-budget worlds as
its first rounds (the record named them so): Kejim seeds 3-34, Dantooine 3-18 there (control `ctl`/`pure`, arm `a`),
then rounds of 8 new seeds from 35 until decided or 96 Kejim / 64 Dantooine pairs. Fresh seeds, because seeded
worlds replay across builds that differ only in off-by-default code. The milestone-B 4v4 (`laps_campaign.COMMON` +
`--two-sided`), 600 game s at rate 25, frame meter on, four worlds at once.

Build `.codex-research/review-next-build-20261109-a` (33aee709: `renderer/pbr-materials` with both 2026-11-09 Bot2
branches merged; `bot_movePoolScopes` default 1, `bot_moveChargeFlown` and `bot_moveArriveReroute` 0). Its
defaults replay the replan-budget build -d's default worlds on Kejim and Dantooine seed 3 (game log 488 and 477
lines, decision lines 148,744 and 151,020, all identical; `identity.txt`, with the branch's `laps_identity.py`,
which reads past the guide preload's wall-clock `ms=`). C: had 218 GB and D: 334 GB free at the start.

Evidence `.codex-research/guidelong-20261109-a/` (`runcamp.py`, `run.sh`, `runcamp.log`, `chain.log`,
`compare-kej.txt`, `compare-dan.txt`, `identity.txt`, `worlds/`): 130 worlds (2 identity, 96 Kejim, 32 Dantooine;
pairs), none failed.

## Result

| map | pairs | team captures (ctl -> a) | verdict |
| --- | --- | --- | --- |
| Kejim | 80 (32 earlier + seeds 35-82) | 3.85 -> 3.73, -0.12 +- 0.17 | no change within 15% |
| Dantooine v3 | 32 (16 earlier + seeds 35-50) | 4.43 -> 4.48, +0.04 +- 0.27 | no change within 15% |

The earlier "both down" was noise: Kejim's interim reads went -0.25 +- 0.22 (40 pairs), -0.20, -0.23, -0.22, -0.24
(72), then -0.12 +- 0.17 at 80. Nothing else moves past its cut-off on either map. Kejim: runner captures
-0.17 +- 0.14, runner weapon deaths +0.66 +- 0.40, runner falls -0.19 +- 0.31, all captures -0.25 +- 0.34.
Dantooine: runners' weapon deaths +1.02 +- 0.85, defender deaths +1.05 +- 0.58, runner falls +0.22 +- 0.28.

## What follows

The guide part costs no captures within 15% on either map. What is left before it can be a default is the timed
pair (the owner's: pinned two cores, Kejim, frames over 50 and 100 ms), since its work-count gain was in the worst
frames and not in p99. `bot_movePlanBound` goes with it if it is taken.
