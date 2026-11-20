# State planner: per-cell strafe-edge lists

Recorded 2026-09-22. This is the first exact candidate from the
[search-speed handoff](../HANDOFF.md). All worlds use the seed and the library.
They are under `.codex-research/planstate-20260922-a/`, built from
`.codex-research/row5-build-20260922-o`. The comparison arm is build `n`
(`.codex-research/search-speed-20260922-e/`). The sources matched build `o`
before and after collection (`sources-before.json`, `sources-after.json`).

## Change

`PlanStateSearch` (`codemp/game/bot2_graph.c`) used to walk nine edge chains
on every expansion: the cell's own and its eight neighbours'. For each edge it
tested the kind and the takeoff's reach, then the state. Now each cell holds a
list of the strafe edges its expansion can admit. The list is built once and
kept in the chain walk's order: the cell, then for each direction the
neighbour and the next cell around, each chain in its own order. The search
relaxes the same edges in the same order, so the heap sees the same pushes.

What stays live on every expansion: disabled, tap, speed band, cone and
avoid.

`strafeNearGeneration` drops the lists. It is bumped by an edge add, a merge
that moves a takeoff, a learned-edge reset, a graph reset and a map load.
A cost update, a result update, or a merge that keeps the takeoff leaves the
lists alone, because the search reads those fields live. The arena holds
2^20 indices. A cell whose list does not fit walks the chains as before.

## Results

**Identity:** all six reference cells are decision-identical to build `n`.

| Cell | Time (ms) | Commits |
| --- | ---: | ---: |
| `red_out_L1_stand`, class 75 | 15,950 | 11 |
| `red_out_L1_stand`, class 100 | 16,725 | 11 |
| `red_out_L1_stand`, class 125 | 15,925 | 11 |
| `red_out_L1_stand`, class 150 | 15,975 | 11 |
| `red_ret_L1_stand` | 19,075 | 13 |
| `blue_out_L1_stand` | 20,400 | 13 |

The only field that differs is `traces` on `red_ret`, 32,469 against 29,940.
That is the known pair that follows pre-release history (search-speed
record). Class 75 needed four attempts to reach its pinned level; those misses
were wall-clock starts, not failures.

**Floor:** 36/36.

**Cost, one bot.** `MOD_FALLING` worlds are excluded: `cost-b4-1` and `-2`
hit the known spawn-fall pose.

| Arm | Worlds | us per expansion | ms per PlanState call | PlanState total (ms) | p99 (ms) | Frames over 25 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| n, budget 0 | b0-1, b0-2 | 1.75 | 3.19-3.21 | 156-157 | 22.3-22.8 | 8, 8 |
| o, budget 0 | b0-1, b0-2 | 1.17-1.40 | 2.13-2.55 | 94-125 | 25.7-31.0 | 9, 10 |
| n, budget 4 | b4-1, b4-2 | 1.74-1.77 | 3.12-3.18 | 166-168 | 11.4-11.7 | 2, 2 |
| o, budget 4 | b4-3, b4-4 | 1.43-1.44 | 2.51-2.53 | 121 | 11.0-12.8 | 3, 2 |

`o/cost-b0-2` makes the same decisions as both `n` budget-0 worlds: 49 calls,
89,592 expansions and 529 rollouts. It is the clean pair:

- PlanState: 157 ms falls to 125 ms (-20%).
- us per expansion: 1.75 falls to 1.40.
- Rollouts in unchanged code slowed about 4%: 11.67 to 12.14 us a step.
- p99: 22.8 rises to 31.0 ms.

The p99 is the eighth-worst frame of 800. Rollouts dominate the heavy frames
(58-71%), so the p99 moves with machine speed and not with this change.

**Reading:** the planner is 20-33% cheaper for each expansion, with every
decision unchanged. The frame tail does not measurably move. In the heavy
frames the planner's share falls from 30-43% to 20-35%. The rest of an
expansion is the heap, the hash table, the walk's law over eight links and the
heuristic; it is now the larger part.
