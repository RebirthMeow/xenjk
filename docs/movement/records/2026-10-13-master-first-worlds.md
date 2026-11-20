# 2026-10-13: first laps worlds on master

Build `.codex-research/master-build-20261013-a` (`tools/strafe_fixture_build.py`, master at fa4fc05b: the movement
defaults merged on 2026-10-09 in 2763e8ce, plus the renderer, cloth and client work since, which the dedicated server
does not load). Campaign `.codex-research/master-check-20261013-a/` (`run.sh`, `paired.txt`, `compare.txt`): 4v4
laps with the `kreplay-20261009-a` settings, Dantooine v3 seeds 1-8 and Kejim 1-8, 600 game s, unpinned, four at once.
Control: `kreplay-20261009-a/fix` (kreplay-build-20261009-c, the last review-fix arm before 2763e8ce). Worlds from
different builds do not replay each other, so this compares distributions, paired by seed.

Starts: no fresh-build stall (one ready echo on Dantooine, two on Kejim, both runners named correctly everywhere).

Work meter and hops (`kreplay-20261009-a/paired.py`, master - control, paired se):

| | Dantooine (8) | Kejim (8) |
| --- | --- | --- |
| est. frames over 25 ms | 1,913 -> 1,830 (-84 +- 75) | 563 -> 573 (+11 +- 32) |
| est. p99 ms | 58.4 -> 57.2 (-1.2 +- 1.3) | 32.1 -> 32.5 (+0.4 +- 0.8) |
| hops | +13 +- 16 | -0 +- 14 |
| chained hops | +12 +- 9 | -13 +- 15 |
| climb commits / failed climb searches | -4.9 +- 2.7 / -70 +- 81 | 0 / 0 |

Outcomes (`tools/laps_compare.py`, control 24 worlds, master 16): team captures 4.29 -> 3.88 (-0.41 +- 0.41,
undecided); runner falls, weapon deaths, camper and defender deaths all within noise (several inside +-15%).

Reading: the merged defaults (M11 forbidden volume, D10 late release, level-2 take-before-the-lip guard, lattice
removal) and the renderer/cloth line change neither the bots' frame cost nor their outcomes measurably at this size.
Owner (2026-10-13): short re-runs are not worth more; anything finer waits for overnight arms. The level-1 gate rows
2 and 4 re-run is prepared, not run (`.codex-research/gate-rows-20261013-a/`, `NOT_RUN.txt`).
