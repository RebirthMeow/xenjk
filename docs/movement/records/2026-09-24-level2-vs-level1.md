# Level 2 against level 1: the slower-level-2 item is closed

Recorded 2026-09-24. The open item "level 2 is slower than level 1 on 10 of
12 floor cases" ([goal-chain record](2026-09-23-goal-chain-uptime.md)) is
re-measured on the build that passed the [level-1 gate](2026-09-24-level1-gate.md).

| Item | Value |
| --- | --- |
| Build | `gate-build-20260924-d` (commit 69d0243d) |
| Configuration | production: seed graph, build-`k` library, harness off, `bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1` |
| Worlds | `.codex-research/l2-confirm-20260924-a/` (72), `.codex-research/dash2-l2-20260924-a/` (4) |
| Sources | 1,125 hashes and both binaries matched before the first world and after the last |

## dash2, the base case

A `dash2_L2_run` case was added to `tools/fixtures/movement/chains/dash2.json`:
the same lane at level 2, Force 50 and the `speed` spend, as Kejim's level-2
cases declare. Two worlds per level, no graph:

| | Level 1 | Level 2 |
| --- | --- | --- |
| Time | 28,975 / 29,250 ms | **20,825 / 19,600 ms** |
| Speed active | 0% | 90% |
| Top speed | 1,622 u/s | 2,218 / 2,410 u/s |
| Reached x 30,000 at | 30,575 ms level | 22,325 / 21,575 ms level |

## Kejim, three repetitions per case

Levels alternated within each repetition, unpinned starts. All 72 complete, no
death.

| Case | Level 1 median / tail | Level 2 median / tail | Median change |
| --- | --- | --- | ---: |
| red_out_stand | 14,075 / 14,075 | 13,575 / 15,200 | -500 |
| red_out_respawn5 | 15,100 / 15,150 | 9,325 / 9,325 | -5,775 |
| red_out_respawn4 | 14,550 / 15,950 | 9,950 / 9,950 | -4,600 |
| red_ret_stand | 17,425 / 17,425 | 12,025 / 13,200 | -5,400 |
| red_ret_respawn5 | 14,125 / 14,275 | 8,850 / 8,850 | -5,275 |
| red_ret_respawn4 | 17,050 / 17,050 | 10,100 / 11,100 | -6,950 |
| blue_out_stand | 16,000 / 16,325 | 13,200 / 13,575 | -2,800 |
| blue_out_respawn5 | 14,275 / 14,275 | 8,850 / 8,975 | -5,425 |
| blue_out_respawn4 | 17,050 / 17,050 | 10,100 / 10,100 | -6,950 |
| blue_ret_stand | 18,225 / 18,225 | 11,550 / 11,550 | -6,675 |
| blue_ret_respawn5 | 14,900 / 16,075 | 8,700 / 8,700 | -6,200 |
| blue_ret_respawn4 | 14,550 / 14,550 | 9,950 / 9,950 | -4,600 |

**Level 2's median is faster on all twelve cases**, and on eleven its slowest
run beats level 1's fastest. The exception is `red_out_stand`, the one case
where level 1 runs the prepared library route: level 2 is 500 ms faster at the
median and its tail, 15,200, is 1,125 ms above level 1's.

The 2026-09-23 figure predates flight mode 2 and the build-`k` defaults (kept
run-up and walk speed, hop lines pinned to aim points); which of them turned
level 2 round is not isolated here.

## What this does not establish

- Level 2 as a gate: no level-2 gate is declared. `red_out_stand` has no
  level-2 library, and its tail is the widest.
- Level 3: set aside by the owner (2026-09-24) until level 2 is good.
- The Force-pool divergence between identical level-1 worlds, still open.
