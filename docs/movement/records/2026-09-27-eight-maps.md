# Eight new maps without a navmesh (2026-09-27)

Roadmap step 1 ([PLAN](../PLAN.md#roadmap-owner-2026-09-27)): the frozen list's
eight new maps - talay, illimiran_v2, telti, nelvaan, talravin, manaan,
kharzet_v3, kothis_v5 - on the level-1 and level-2 case shape from automatic
preparation, with and without the map's navmesh, and with survival after the
objective. Build `native-build-20260927-k`
([record](2026-09-27-navmesh-off.md#native-edges-by-default-pads-at-the-meshs-price-same-day-owner)):
native pads, drops and jumps by default, pads at the mesh's price.

## Fixtures

`tools/movement_cases_from_bsp.py`, Kejim's declared set (36 cases a map),
`tools/fixtures/movement/<map>/cases.json`. Heights from one engine world per
map (`--graph-world`); the offline brush trace agrees with the engine on every
start of all eight maps. Illimiran v2 refuses one seed, a deathmatch spawn no
case uses. Seven maps ship a navmesh; illimiran_v2 has none (only the older
`ctf_illimiran`). The twin skip does not apply (these maps are not measured
symmetric), so every arm runs all 36 cases. (Measured 2026-09-29: all eight
symmetric, now in the table; [twin audit](2026-09-29-twin-audit.md).)

| Map | Cells | Native pads | Drops | Jumps | Climbs |
| --- | --- | --- | --- | --- | --- |
| talay | 20,313 | 52 | 376 | 100 | 276 |
| illimiran_v2 | 10,964 | 20 | 164 | 38 | 146 |
| telti | 11,529 | 22 | 634 | 92 | 450 |
| nelvaan | 12,768 | 28 | 298 | 38 | 132 |
| talravin | 11,485 | 20 | 463 | 82 | 100 |
| manaan | 18,213 | 54 | 468 | 264 | 128 |
| kharzet_v3 | 13,776 | 20 | 728 | 68 | 330 |
| kothis_v5 | 12,398 | 6 | 206 | 144 | 116 |

## Result

`--edges --parity 256 --linger 4`, pinned classes 150 and 50 with the mesh not
loaded (`bot_navmeshOff 1`, the runner's production), class 150 with it loaded
(`.codex-research/maps8-20260927-a/`, `maps8-20260927-a.sh`).

**Every case completes on every map in every arm: 828 of 828 trips, zero
deaths on the trip, zero deaths after the objective, owner `move`** (plus the
jumppad state where a route crosses a pad).

| Map | L1 median / max, class 150 | L2 | L1, class 50 | L2 | Mesh loaded, mean per case |
| --- | --- | --- | --- | --- | --- |
| talay | 15.9 / 26.7 s | 12.0 / 22.8 | 15.2 / 26.3 | 11.9 / 22.4 | -0.6 s |
| illimiran_v2 | 16.3 / 20.9 | 11.8 / 13.1 | 15.9 / 19.9 | 11.8 / 13.6 | (no mesh) |
| telti | 19.2 / 22.6 | 13.8 / 16.7 | 19.1 / 23.3 | 13.5 / 15.7 | -0.1 |
| nelvaan | 12.0 / 17.1 | 9.4 / 11.8 | 12.5 / 15.3 | 8.8 / 11.4 | identical |
| talravin | 20.4 / 37.9 | **31.9** / 41.9 | 20.6 / 38.1 | 19.1 / 33.1 | **-7.6** |
| manaan | 14.3 / 17.9 | 9.1 / 12.4 | 14.4 / 18.5 | 9.5 / 12.7 | +0.0 |
| kharzet_v3 | 14.3 / 18.7 | 10.3 / 13.0 | 14.2 / 19.0 | 10.0 / 13.2 | identical |
| kothis_v5 | 16.6 / 24.0 | 10.8 / 18.7 | 16.3 / 22.8 | 10.9 / 18.9 | -0.7 |

Parity is 0 everywhere except Talravin (below). Six maps are clean. Two are not
fast enough to call done.

## Talravin and Talay: native drops the drop search cannot fly

**Failed drop edges a case:** Talravin 72-85, Talay 31, every other map 0-1.
The graph disables a drop after two failures, but native discovery makes one
drop per ledge cell and side, so a long ledge offers dozens of parallel drops
into the same lower floor. Each failure costs a replan and the next plan
takes a neighbour: 94 edge failures in one Talravin world, about 13 s of
standing at one ledge (level-1 red stand, class 150). The landings are real
floor (large connected regions, often stairs or ramps 100-390 units down);
the fall height does not separate failed from flown drops.

The drop search's reasons differ: on Talay the rollouts mostly fall past the
landing (`fell`); on Talravin 38% of plans find no run-up line at all, and the
rest are refused for `no_onward`, `miss`, forecast refusals and `off_graph`.

**The mesh's drops are much better here**
(`.codex-research/maps8-meshedges-20260927-a/`, `bot_moveNativeEdges 0`, class
150): Talravin 9 failed drops a case and 9.3 s a case faster than native
without a mesh (1.7 s faster than native with one loaded); Talay 3 and 2.1 s.
Both 36/36, zero deaths.

**The mesh-loaded native arm** is faster on Talravin (-7.6 s a case) for an
indirect reason: before the pinned start, a walk with no planned path
declines `no_path` without a mesh (`WalkStep`) and holds, so the bot learns
different strafe edges and later searches diverge; the same world at class
150 first differs at level 5,050, a straight-hop search that one arm makes
and the other does not. The drop failures are there in both arms (3,070 against
442 in 36 worlds).

**Candidate repair, not attempted:** native drops are admitted by one
straight-down trace and never flown; native pads are admitted only when an
arc flown with the hull lands on them. A drop checked the same way at
discovery (walk off at run and slow-drop speed, steered at the landing, must
come down on its floor) would remove the drops the executor cannot land. It
does not address Talravin's `no_onward` and no-line refusals, so it may not
close the gap; a comparison against the mesh-edge arm above decides it.

## Other findings

- **Talravin parity:** 26-36 `before_state` mismatches an arm (9 with the
  mesh's edges), all after the trip ended, during the linger, all on
  `fd.forcePower`: at 10 Force the forecast predicts a 5-point spend the live
  game does not make. Only Talravin's long level-2 trips drain Force that far.
  A narrow forecast defect at low Force, open.
- **Talravin level 2 slower than level 1** at class 150 (31.9 against 20.4 s):
  the level-2 trips meet the failing drops more; with the mesh's edges level
  2 is 12.7 s.
- Every Talravin, most Kothis and Nelvaan cases cross a pad; all 258 pad
  entries without a mesh flew to native landings (`src=native pad edge`),
  none fell.

## Drops checked by a flown arc: no effect (same day, reverted)

Tried (owner): a native drop kept only when an arc flown with the hull from
the open column past the ledge, at the run speed or 150 u/s and air-strafing
at the landing, comes down on the landing's floor, as pads are checked
(`bot_moveDropArc`, build `native-build-20260927-l`,
`.codex-research/droparc-20260927-a/`, mesh not loaded, class 150).

| Map | Drops refused | Failed drops a case | Against build `k` |
| --- | --- | --- | --- |
| Talravin | 76 of 463 | 80 (was 72-85) | -0.6 s a case (10 faster, 24 identical) |
| Talay | 20 of 376 | 31 (was 31) | identical, 36 of 36 |

The drops that fail do fly as plain arcs. The executor's drop search refuses
them for its own geometry: a run-up line must reach the crossing from where
the bot stands and there must be floor under the flight at the landing's
height (Talravin's no-line plans), and the landing must continue onward
(`no_onward`). The chain was stopped after these two arms, the check reverted;
`bot2_graph.c` is byte-identical to build `k`'s source again.

**What a prepared drop set would hold** (reanalysis of the 72 no-mesh worlds
a map above, no new runs): Talravin, of 463 native drops, 19 were ever flown
(12 of them also failed from some approach), 135 failed and were never flown,
309 were never tried; Talay 11 flown (8 also failed), 24 failed and never
flown, 341 never tried. The cache saves `validated` but not failures, and
`movement_graph_merge.py` does not carry outcomes.

## A prepared drop set from measured outcomes (same day, owner)

`tools/movement_graph_prepare.py` (new, with `tools/test_movement_graph_prepare.py`)
writes a map's graph seed from earlier worlds: the built graph (cells, nodes,
pads, drops, jumps, climbs) with no learned edges, `validated` reset, and
without the drops that failed somewhere and were flown nowhere. Built edges
are the leading block of every cache and identical across one map's worlds
apart from `validated` (checked on all 72 worlds of each map; their costs
never change in play), so the seed differs from an unseeded run only by the
drops it leaves out. Input: the class-150 and class-50 no-mesh worlds above;
judged out of sample at class 100, build `k`, mesh not loaded, `--linger 4`
(`.codex-research/dropseed-20260927-a/`, `dropseed-20260927-a.sh`).

| Map | Drops left out | Failed drops a case | Pruned against the control seed |
| --- | --- | --- | --- |
| Talravin | 135 of 463 | 81 to 9 | **-6.7 s a case**, 24 faster, 0 slower |
| Talay | 24 of 376 | 29 to 1 | **-3.2 s a case**, 12 faster, 0 slower |

The control seed (the same built graph, nothing left out) is identical to the
unseeded arm on all 72 cases, so loading a seed changes nothing by itself.
Every arm 36/36 with zero deaths. Talravin level-2 median 24.5 to 14.4 s,
Talay's slowest level-1 trip 25.9 to 17.6 s. Talravin parity: 62 to 9
`fd.forcePower` mismatches, 6 of them now inside a trip (at 42-54 Force, on
the z 56 floor), so that defect is not confined to low Force or to the
linger.

## Why Talravin's drops fail: a floor material the forecast refuses

Talravin has 8,325 forecast refusals flagged `BOT2_FORECAST_MATERIAL` in 36
worlds; every other new map has none. `ForecastTrace` (`bot2_forecast.c`)
refuses any rollout whose traces touch mud, sand, dirt, snow or gravel,
because GAME Pmove's `PM_CrashLandEffect` spawns a landing effect there. That
effect fires only on a landing faster than 300 u/s with no water, and changes
nothing about the motion; the refusal covers every contact, walking
included. Talravin's lower floor (z about -360) is such a material (5,016 of
the refusals at z -359, 1,046 at -343), so drop rollouts and onward walks
there are refused. Added with the forecast (f40ffdc3, 2026-09-07), before
rollouts stopped spawning temp entities at all (`G_TempEntity` returns a
scratch entity during a rollout, `Bot2_ForecastSuppressTempEntity`, 7e1df32f,
2026-09-24), so the refusal was already redundant as a side-effect guard.

## Materials and water no longer refused (same day, owner)

Owner: the landing effect is visual and must not steer route choice; water
can hold pits, but those are refused by their `trigger_hurt`, not by the
water. `ForecastTrace` no longer refuses the five materials, and
`ForecastContents` refuses only slime and lava (`BOT2_FORECAST_LIQUID`);
`BOT2_FORECAST_MATERIAL` is no longer set. Build `native-build-20260927-m`,
mesh not loaded, `--linger 4` (`.codex-research/material-20260927-a/`,
`material-20260927-a.sh`).

| Talravin, class 100 | Stand family | Respawn-4 family | Failed drops a case | Parity |
| --- | --- | --- | --- | --- |
| build `k`, no seed | 33.8 s | 27.0 s | 81 | 62 |
| build `m`, no seed | 32.4 | 17.9 | 67 | 0 |
| build `m`, pruned seed | **19.5** | **13.9** | **4** | 0 |

- Talravin, no seed: -3.9 s a case against build `k` (32 of 36 faster),
  material refusals 8,325 to 0, and **every `fd.forcePower` mismatch gone**:
  that defect came from the refused rollouts. The failed drops barely move
  (81 to 67), so the material was not the main cause of them.
- Talravin with the pruned seed (made from build `k`'s worlds): -5.6 s a case
  against build `m` without it, -2.8 s against the seed on build `k` (36 of 36
  faster); about -9.5 s a case against where Talravin started. Its stand
  family is now under the mesh-edge arm's (22.2 s, class 150).
- Talay (no such floor): identical to build `k` with and without its seed, 36
  of 36 each; the seed's -3.2 s stands.
- The other ten maps (six new at class 150, the first four at class 150 with
  `--skip-twins`): identical to build `k`, every case, parity 0.
- **Water is not exercised.** No retained world on any of the twelve maps
  ever raised the liquid refusal, so these fixtures never route through
  water; the water half of the change is untested by them.

## Map preparation: prepared seeds for every map (same day, owner)

Owner: prepared seeds for drops, pads, wall climbs and anything else the
graph builds from geometry are part of map preparation. The tool now prunes
every candidate type the graph builds from geometry - pads, drops, jumps,
climbs - that failed somewhere and succeeded nowhere; lift rides are kept
(a ride's failure is the lift's timing, not the edge). An outcome counts only
when its type matches the edge at that index. Seeds for all twelve maps,
from the retained no-seed worlds of `maps8-20260927-a`,
`material-20260927-a` and `native-default-20260927-{j,k}` (72 to 145 worlds
a map, class 100 excluded): `.codex-research/mapprep-20260927-a/seeds/<map>/`.

| Map | Left out | Seeded against unseeded, class 100 |
| --- | --- | --- |
| Talravin | 126 drops, 2 jumps | **-5.7 s a case**, 24 faster, 0 slower |
| Talay | 19 drops | **-2.3 s**, 12 faster, 0 slower |
| ctf4 | 21 drops, 1 jump | identical, 18 of 18 |
| Kothis v5 | 9 drops, 6 climbs | identical, 36 of 36 |
| Manaan | 1 jump | identical |
| Telti, Kharzet v3, Nelvaan, Illimiran v2, Kejim, Dantooine v3, Metalorn v2 | nothing | identical |

Build `m`, mesh not loaded, `--linger 4`, parity 256, twins skipped on the
first four maps (`.codex-research/mapprep-20260927-a/`,
`mapprep-20260927-a.sh`): all 22 arms pass every case, zero deaths on the
trip or after the objective, parity 0. Talay's seed leaves out 19 drops
where the drops-only seed left 24 (the added worlds flew five of them) and
gains 2.3 s instead of 3.2. Pads never appear in the outcomes: at the mesh's
price they are not planned, and a thrown pad is not scored. The Talravin
seeded arm retried 19 worlds whose pinned start came after its window
(`[CORNER] REFUSED deferred window passed`); a harness miss, each retried
and passed.

`tools/play_vs_runner.py` stages a map's prepared seed from that directory
(`PREPARED`) for every map but Kejim, which keeps its own seed and the tape
library bound to it.
