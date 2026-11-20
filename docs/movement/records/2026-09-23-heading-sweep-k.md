# Heading sweep and successor aim on build k, 2026-09-23

Owner order: re-measure `bot_moveHeadingSweep` on build `flight2-build-20260923-k`,
then re-explore the decision tapes. The sweep was set aside in the
[flight mode 2 record](2026-09-23-flight-mode-2.md) for a fall death and a
stall on floors; both failures predate two build-`k` repairs (the 8-unit
arrival margin fixed the `blue_ret_L2_respawn4` flag-ledge stall, the longer
walk chord fixed the `blue_out_L2_respawn5` fall "with the heading fan").

Worlds: `.codex-research/hs-k-20260923-a/` (`campaign.sh`, `followup.sh`,
`followup2.sh`, `progress.txt`). Settings as the flight mode 2 record's
production arm (`arm.sh`): seed graph, seed-gate library, exit speed 500,
state prefer, reroute, harness off.

## Stand run, `red_out_L1_stand`, ms

| Class | Control | Heading sweep | Successor aim |
| --- | --- | --- | --- |
| 75 | 15,450 | 16,300 | 15,450 |
| 100 | 16,825 | 16,500 | **13,425** (twice) |
| 125 | 16,900 | 16,725 | 16,550 (class window 5,500-8,000) |
| 150 | 16,050 | 15,225 | 18,075 (twice) |
| mean | 16,306 | 16,188 | 15,875 |

Controls reproduce build `k`'s record to the millisecond, and every repeat
matched. The sweep's class-150 world and successor aim's class-125 world
missed their class in the default window (fixture misses, `corner level
None`, retained as `*.try*`), then ran.

## Floors, 36 cases each, sweep on / off / off / on

| Level | hs-1 | def-1 | def-2 | hs-2 |
| --- | --- | --- | --- | --- |
| 1 | 198.3 | 191.5 | 193.9 | 188.5 |
| 2 | 124.5 | 129.3 | 131.2 | 128.3 |
| 3 | 128.3 | 126.5 | 127.8 | 125.6 |

All four 36/36, zero deaths. Per-case spread between repeats of one arm is
larger than the difference between arms.

## Findings

- **The sweep is safe on build k and not faster.** Both old failures are
  gone; stand mean -118 ms with 3 of 4 classes faster and one 850 ms slower,
  floors indistinguishable. It stays opt-in.
- **Successor aim was dropped by accident on 2026-09-23.** It is a PLAN
  standard setting and every earlier run that day set it; the flight mode 2
  `arm.sh` did not, so build `k`'s approved numbers are successor aim off.
  On, it is deterministic and mixed: 3.4 s faster at class 100, 2.0 s slower
  at 150. Not adopted on four worlds.
- **The seed library is nearly inert on build k.** Its first entry is
  `absent` (class 75) or `refused why=no_onward` (125, 150) and hits only at
  class 100; entry 0 precedes any flight, so this is not only flight mode 2.
  The Kejim times above are the ranking's own. The tapes are re-explored on
  build `k` with the approved settings (successor aim off, sweep off):
  `.codex-research/explorer-k-chain-20260923-a.sh`.

## Tool change

`tools/movement_decision_explore.py --set CVAR=VALUE` replaces BASE's value
for that cvar (BASE forces successor aim on and the sweep off); regression in
`tools/test_movement_decision_explore.py`.

## The build-k explorer and library

Chain `.codex-research/explorer-k-chain-20260923-a.sh`: the decision explorer
on build `k`, seed graph, `--set bot_moveSuccessorAim=0` (sweep off from
BASE), beam 3, patience 2, one class at a time; 487 worlds, 08:25-10:43.
Every class's control reproduced its production time, so the harness-on
explorer runs the production decisions.

| Class | Control | Explorer best (confirmed) | Worlds |
| --- | --- | --- | --- |
| 75 | 15,450 | **13,625** | 174 |
| 100 | 16,825 | **14,125** | 72 |
| 125 | 16,900 | **13,400** | 140 |
| 150 | 16,050 | **14,075** | 101 |

Library `.codex-research/explorer-k-library-20260923-a/library/`, bound to
`g1-98eb1e97c24a4e21`, index sha256 `b9ea9f32...`; measured by
`.codex-research/explorer-k-library-20260923-a.sh` with the production
settings and `--decision-library`, the bench naming no tape:

- **Stand, two worlds a class:** 13,625 / 14,125 / 13,400 / 14,075 ms,
  both repetitions identical, every world on the `exact` rung with every
  entry hit (9, 10, 8 and 10 entries). Equal-weight median **13,850 ms**,
  tail **14,125**, against the controls' 16,437.5 and 16,900 and the seed
  library's gate figures 15,962.5 and 16,725.
- **Floors with the library on:** 36/36 twice, zero deaths. Level totals
  185.6 / 184.1 (L1), 130.6 / 124.7 (L2), 128.8 / 128.5 (L3) s against the
  control floors' 191.5 / 193.9, 129.3 / 131.2 and 126.5 / 127.8. Levels 2
  and 3 are within the controls' spread. Level 1 is about 8 s lower, more
  than the one taped case (`red_out_L1_stand`, unpinned on a floor) can
  explain; floor cases are not class-pinned, so this is not attributed.

Not established: classes 0, 25, 50 and 175, other cases and legs (no tapes),
row 5 cost with the library loaded, and the tapes' sensitivity to any later
source change (a tape binds to its graph, not its build).
