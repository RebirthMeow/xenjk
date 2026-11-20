# Movement benchmark: Kejim

The fixed case list behind the benchmark gate in
[docs/movement/PLAN.md](../../../../docs/movement/PLAN.md). `cases.json` is
`MOVEBENCH1`: a map, its BSP hash, and one entry per case. Cases are added
from observed failures, one per failure, and never edited to pass.

## The declared set

Two teams, both legs, three levels, three starts each: 36 cases.

- **Leg.** `outbound` starts on the team's own side without the flag and
  passes on pickup (`carrying` becomes 1). `return` starts on the enemy side
  carrying and passes on capture (`captures` reaches 1). The return leg uses
  the enemy stand and the enemy team's two respawn poses, so both legs cover
  the same ground in both directions.
- **Start.** The flag stand (standing height 1924.125 over the flag origin)
  and two respawn poses whose grounded height was recorded in the strafe
  catalog guides (`respawn_4` and `respawn_5`). The first respawn pair
  (`respawn_0`) is not used: its recorded height is inside solid and the
  corner fixture refuses the pose. Every start is at rest; velocity and
  Speed window are case fields for later seeded cases.
- **Level.** The spend policy names the edge templates the level permits,
  independent of the pool. Force is matched per level so the snapshot
  baseline runs under the same resources: level 1 starts at 0, level 2 at
  50 (one Speed purchase), level 3 at 100.

| Level | Force | Templates permitted | Bound |
|---|---|---|---|
| 1 | 0 | walk, corner, hop, tap | 180 s |
| 2 | 50 | + speed | 120 s |
| 3 | 100 | + hold | 120 s |

Red is death, a stall past the bound, or a forbidden spend: Speed active at
level 1, or Force below the start pool (less the one Speed purchase once Speed
has been seen active) below level 3. The spend checks read the four-hertz
play log, so they are a measured floor, not a proof of every frame.

## Running

```
python tools/movement_bench.py --build <build> --out <dir> [--level N] [--filter REGEX] [--parity 16] [--no-rewrite]
```

One dedicated localhost world per case; `--parallel` up to four. Each
world receives the case's spend policy as `bot_moveSpend` (bit 1 Speed,
bit 2 held jumps), so a level-1 case starting with Force still buys
nothing. `--no-rewrite`
runs the snapshot behaviour for baselines. Results and every world's files are
retained under `--out`; `result.json` binds the build manifest, this case file
and the runner. `tools/test_movement_bench.py` covers the pure evaluation.

Each world prepares its graph at map load and learns strafe edges from its
own straight plans, saved under `<out>/<case>/base/bot2-graph/`. Edges
cross worlds only as a seed: `--graph <cache>` copies one cache into every
world before it starts, and `tools/movement_graph_merge.py <campaign>
--out <seed> [--level N]` unions the caches of a campaign's worlds into
one. `--no-state` (`bot_moveState 0`) searches the path over cells alone
and validates no planned strafe edge. Each case's `state` record counts
the paths by search, the strafe edges planned, the planned hops committed
and refused, and the edges learned.
