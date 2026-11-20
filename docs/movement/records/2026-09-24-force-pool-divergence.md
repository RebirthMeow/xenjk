# The Force-pool divergence is a start-phase difference

Recorded 2026-09-24. Closes the open item "identical level-1 worlds diverge
on the Force pool (the search reads Force somewhere)" from the
[goal-chain record](2026-09-23-goal-chain-uptime.md).

## The original observation, re-read

`.codex-research/force-pool-20260923-a` (build `landjump-build-3bfdc7bc`):
dash2 level 1 took 31,825 ms at Force 0 and 33,900 ms at Force 100, each
repeated twice. Its `[MOVE]` logs, aligned by the first line's level and with
the `force=` field removed, diverge at the second plan line on the first walk
frame's speed (31.4 against 31.9 u/s). The two arms did not start on the same
server level: 1,750 ms at Force 0, 1,700 ms at Force 100. The round-trip
pairs also started 25-500 ms apart. That arm was unpinned, so the Force value
and the start phase were confounded.

## Pinned re-run

Build `droppush-build-20260924-b` (the current defaults), `--edges`,
`bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1`, dash2 level 1
from `chains/dash2.json` (Force 0) and `chains/dash2_f100.json` (Force 100),
`--corner-class` 150 and 50, four worlds at once. Worlds in
`.codex-research/force-pool-20260924-a/`.

| Class | Force 0 | Force 100 | First `[MOVE]` level | Decision lines |
| --- | --- | --- | --- | --- |
| 150 | 28,625 ms | 28,625 ms | 1,750 / 1,750 | 160 / 160, **identical** |
| 50 | 28,550 ms | 28,550 ms | 1,750 / 1,800 | 156 / 156, diverge on the first walk frame |

At class 150 the Force-0 pool regenerates from 0 to 100 during the run while
the other stays at 100, and every decision line (plans, commits, launches,
candidates, scores) matches once levels are aligned and the `force=` field is
ignored. At class 50 the two worlds' first level differs by 50 ms and the logs
diverge from the first walk frame, yet land on the same time.

## Conclusion

At level 1 the search does not read the Force pool. The 2026-09-23 difference
came from the start phase, which the class pin does not fix to one server
level. Force enters decisions only through the level-2 Speed purchase and the
forecast's `modelForce` path, both gated by the case's spend.
