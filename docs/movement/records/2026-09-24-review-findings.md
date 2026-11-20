# Review findings on bd15d286..e6533c31

Recorded 2026-09-24. A review of the day's commits found no defect in the
slow-drop change ([record](2026-09-24-drop-slow.md), notes under its Open list)
and six P2 findings in earlier work, each confirmed from source and repaired.

| # | Finding | Repair |
| --- | --- | --- |
| 1 | A pending knock recovery was skipped on a frame the executor did not own (the walk declined `NO_PATH` or an off-mesh yield): the boundary returned before `KnockSearch` | `MoveParityBeforePmove` runs a pending search on such a frame; a commit issues the frame's command, and the next frame's recovery owns it |
| 2 | The hit never ended a lift ride: the `LiftEnd(...,"knocked")` test was unreachable (an active recovery had returned or cleared), so a ride could resume its stale command after recovery | The ride ends when the hit is accepted, and no ride boards while a recovery is pending. A hit charges no edge: `LiftEnd` success -1 ends the ride uncharged, as a knocked sequence charges none |
| 3 | A cache from before lifts loads under version 4 with every cell's `mover` zero, and the ride migration ran only when mover cells existed, so such a graph never gained lift routes | On a map with lift calls, a cache with no mover cell is discarded and rebuilt (`[GRAPH] cache_pre_lift`). The version is not bumped: it feeds the identity, which Kejim's libraries and seeds bind to. Kejim and Dantooine v3 have no lift calls (BSP entity lumps), so their caches are untouched |
| 4 | Forecast call deadlines are recorded against a lift team's master, but the floor test compared the raw ground entity, so a multipart lift's slave stayed floor after a call | `ForecastStandable` compares the master (`Bot2_MoverMaster`, exported from `bot2_mover.c`) |
| 5 | `kick_audit` counted a kick applied after the evaluation's endpoint as a recovery | It takes the endpoint; a kick at or after it is listed as `late` and counted unfired. The endpoint frame is late too (follow-up review): the verdict's sample reports the previous Pmove and the kick is applied before it, so a kick on that frame came after the completion. Regression: a pass and a kick both at 12,700 ms |
| 6 | `death_class` read ground and positions past the death, so a respawn could make a death with no landing `landed_then_fell` | Both searches are bounded to the evaluated life, one 25 ms frame of margin |

Findings 5 and 6 have regressions in `tools/test_movement_kick.py`, both shown
to fail against the previous readers (the equal-frame case against the first
repair too); 63 tests in `test_movement_kick` and
`test_movement_bench` pass.

## Retained evidence re-audited

Every kick report under `kick-20260924-a` and `knock-recover-20260924-a`/`-b`,
re-audited with the repaired readers: one row changes, the fixture's Kejim
walk, 36 fired / 32 passed / +1,238 ms median to **35 / 31 / +1,300**
(`red_ret_L1_respawn5`, passed at 12,700 ms, kicked at 12,750). The
[fixture record](2026-09-24-knockback-fixture.md) is corrected. No death class
changes.

## Native checks

Build `reviewfix-build-20260924-a`, production settings, recovery at its
default (on), `.codex-research/reviewfix-20260924-a/` (`reviewfix-20260924-a.sh`):

| Arm | Result |
| --- | --- |
| ctf4 from the pre-lift cache (`lift-control-20260924-a`, 0 mover cells) | 36/36; every world printed `cache_pre_lift calls=14` and rebuilt to 192 mover cells, 112 lift edges; 3 worlds rode a lift |
| ctf4, Kejim, Dantooine floors | 36/36 each; Kejim identity `g1-9fc7bd2f05e12a2d`, the same as the drop-slow build before these repairs, no Kejim rebuild |
| ctf4 hop kicked | 36 fired, 36 passed, no death, no stall (33/36, 3 stalls in `knock-recover-b`) |
| Dantooine hop kicked | 36 fired, 36 passed (30/36, 6 direct in `knock-recover-b`) |
| Kejim walk kicked | 33 fired, 32 passed, 1 direct death (36/36 in `knock-recover-b`) |

The Kejim walk death, `blue_ret_L2_respawn5`: the kick struck at a
`floor_missing` lip with the bot braking from 916 u/s, and every knock rollout
touched the pit's `trigger_hurt` until it died; all its searches ran on owned
frames. It is a push straight into the pit, out of scope (owner), and the
earlier arm's kick fired elsewhere in an unpinned world. The kicked arms are
single unpinned passes; they show no regression, not an improvement.

**Not exercised natively:** no knock search in these arms fell on an unowned
frame (finding 1), and no kick struck during a ride (finding 2); both rest on
source tracing.

The Kejim floor identity differs between `circle-20260924-c`
(`g1-98eb1e97c24a4e21`) and both builds from this session
(`g1-9fc7bd2f05e12a2d`). It is not a code change: the circle floor loaded a
cached graph (`cached=1`), these built one (`cached=0`), with the same cells,
nodes and links; the difference is the edges that cache carried.
