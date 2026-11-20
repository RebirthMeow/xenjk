# Competitive defaults against stock, one build (2026-10-09)

Overnight queue item 1 ([overnight-tests.md](../../overnight-tests.md)), and the start of item 2 before the owner
cancelled it. Fresh build `nighttests-build-20261009-a` (HEAD 49f77220), research directory
`.codex-research/nighttests-20261009-a/` (`run.sh` item 1, `item2.sh` item 2, `lib.sh` lock and pre-launch checks,
`carrier.py`, `mods.py`; compare files `compare-*.txt`, `carrier-*.txt`, `mods-*.txt`).

Laps settings: the C and D strings of `.codex-research/be-combat-20261008-a/run.sh` (4v4 two-sided, camper, base and
chase defenders, `--rate 25`, 600 game s). Arm `stock` passes every value of
`tools/fixtures/competitive/stock_force_profile.cfg` with `--set`; arm `default` passes nothing. Dantooine v3 seeds
1-8, Kejim seeds 1-16, at most four worlds at once. No first-launch stall (one `Runner_v2` per world in the first
Dantooine and Kejim batches); every world distinct, none excluded.

## Item 1: what the whole base_enhanced port does to the bots

`tools/laps_compare.py`, stock as baseline, per 600 game s (se); `*` past the t cut-off, `=` inside +-15%:

| metric | Dantooine stock | default | diff | Kejim stock | default | diff |
| --- | --- | --- | --- | --- | --- | --- |
| team captures | 5.18 (0.49) | 5.74 | +0.56 +- 0.55 | 3.70 (0.25) | 3.73 | +0.03 +- 0.35 |
| runner captures | 3.58 | 4.32 | +0.74 +- 0.54 | 2.56 | 2.25 | -0.31 +- 0.34 |
| camper captures | 1.60 | 1.42 | -0.18 +- 0.30 | 1.14 | 1.48 | +0.34 +- 0.20 |
| all captures | 10.85 | 11.59 | +0.74 +- 1.05 | 7.76 | 7.64 | -0.12 +- 0.78 |
| runners' falls (both) | 2.47 | 2.84 | +0.37 +- 0.98 | 7.46 | 8.62 | +1.17 +- 0.99 |
| runners' weapon deaths (both) | 30.33 | 32.07 | +1.73 +- 1.09 = | 30.63 | 33.27 | **+2.65 +- 0.97 \*** |
| defender deaths | 20.22 | 20.72 | +0.50 +- 1.13 | 22.68 | 23.54 | +0.86 +- 1.10 = |

Captures undecided on both maps (no capture effect resolvable at 8/16 worlds). Runner weapon deaths rise on both
maps, past the cut-off on Kejim.

Carrier kills (`carrier.py`: a bot's `carrying` dropping to 0 in the play CSV without a capture, matched to the game
log's kill line within 1.5 s; its capture count matches every world's scoreboard): weapon carrier kills Dantooine
38.10 -> 41.19, Kejim 40.98 -> 42.58; by the base defender Dantooine 5.80 (0.51) -> 8.39 (0.97), Kejim 6.78 (0.64) ->
8.32 (0.29), about +2 se on each map; by the chaser unchanged (25.2 -> 25.9, 26.4 -> 27.4). Carrier falls 4.44 ->
3.08 and 8.81 -> 8.93. Runner deaths by means (`mods.py`): `MOD_ROCKET` 2.10 -> 3.08 (Dantooine) and 2.96 -> 3.76
(Kejim), consistent with improved homing on the defender's alt rockets; Kejim `MOD_FLECHETTE_ALT_SPLASH` 11.59 ->
13.68.

**Owner decision (2026-10-09, relayed by the coordinator): the higher runner weapon deaths under the competitive
defaults are fine and accepted, with no further tests.**

## Item 2: cancelled by the owner, partial worlds kept

Item 2 (each layer of the stock profile alone at stock, the rest at the defaults) was started because item 1 moved
runner weapon deaths, then cancelled by the owner as not needed. Finished: `stock-move` (Dantooine 1-8, Kejim 1-16),
`stock-fight` and `stock-force` (Dantooine 1-8 each). Stopped mid-batch: `stock-fight` Kejim 1-4 (killed at about
05:09; directories kept, incomplete; their C: staging folders `defense-laps-20261009T0906*Z-kej-{1..4}-*` under
`%LOCALAPPDATA%/xenjk-play` were left in place). Not run: `stock-fight` Kejim 5-16, `stock-force` Kejim 1-16.

What the finished arms show, scored against `default` (`compare-layers-dan-vs-default.txt`,
`compare-layers-kej.txt`), for the record only: Dantooine team captures with the movement layer at stock 4.26 against
5.74 (-1.48 +- 0.45, past the cut-off), fight layer -0.74 +- 0.38, Force layer -0.19 +- 0.33; Kejim movement layer at
stock -0.06 +- 0.42 (16 worlds) and runner weapon deaths unchanged (33.27 both). The Dantooine movement-layer signal
is one map at 8 worlds, does not appear on Kejim and is larger than the whole profile's own difference (+0.56); not
pursued.

## Not done here

Queue items 3-5 (Dantooine bench and level gates, Talravin seed rebuild, renderer timedemo bench) were handed to
another agent; the prepared scripts are `item3.sh`, `item4.sh` and `.codex-research/gate-20261009-a/` (the
gate-20261005-e harness rebound to this build), described in
`.codex-research/orchestration-20261009/status.md`.
