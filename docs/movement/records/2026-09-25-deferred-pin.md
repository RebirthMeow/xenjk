# Deferred class pins: the game picks the start level

Recorded 2026-09-25. First item of the "make verification cheap" order
([HANDOFF](../HANDOFF.md)): half of every campaign's world-time went to worlds
retired for missing `--corner-class`.

## Why pins missed

Over rcon, `bot_corner_start` lands on the level of the last play-log row the
runner saw. Those rows come every 250 ms, so a world can only start on its own
ladder `L0 + 250k`; 250 is 50 mod 200, so a ladder visits four residues of one
parity of 25 and never the other four. A class is reachable in about half the
worlds. `arrivesafe-20260925-f` retired 43, 51 and 43 worlds against 36 kept on
the three map-classes below, and still lost 3, 8 and 2 cases per 36 after four
misses each.

## Change

`bot_cornerDefer "class min max"` (default empty, fixture only;
`bot2_corner.c`). With it set, `bot_corner_start` validates the window, keeps
its arguments and replies `[CORNER] DEFERRED`; `Bot2_CornerFrame`, called at
the end of `G_RunFrame`, runs the unchanged start at the first frame at or
after `min` whose level is in the class. That is the point an rcon command
reaches: after frame L, before the bots think for L+25. Past `max` it prints
`[CORNER] REFUSED deferred window passed`. Every class is a 25 ms multiple,
so one lies inside any 200 ms window.

`movement_bench.py --corner-class` now defers by default: the cvar goes into
`start.cfg`, the command is sent as soon as the runner is up, and the landed
level is read from the `[CORNER] START` line in `server.log` (the DEFERRED
reply goes to rcon, not the log). `--corner-class-rcon` keeps the old
behaviour; a deferred pin needs `max >= min + 200`. `movement_decision_explore`
pins through the bench and inherits it. `route_approve.py` has its own settings
and loop and still pins over rcon.

## Result

Build `deferpin-build-20260925-b` (sources = `arrivesafe-build-20260925-g` plus
this change), settings as `arrivesafe-20260925-f` without `--linger`, against
that arm's worlds:

| Map-class | Shared pairs | Identical | Passed, rcon / deferred | Worlds, rcon / deferred | World-seconds, rcon / deferred |
| --- | --- | --- | --- | --- | --- |
| ctf4 c50 | 33 | 33 | 33 / 36 | 74 / 36 | 583 / 293 |
| Kejim c150 (seed graph) | 28 | 28 | 28 / 36 | 87 / 38 | 609 / 311 |
| Taloraan c150 | 34 | 34 | 34 / 36 | 79 / 36 | 608 / 257 |

**95 of 95 shared pairs identical**, 108 of 108 passed, world-seconds halved
(1,800 to 861), and each arm ran in 80-100 s wall at four worlds
(`.codex-research/deferpin-20260925-b`, `-c`). Deferred starts landed from
3,050 to 6,050 and matched the rcon arm's worlds that started elsewhere in the
class, which is the class stratum again. The two extra Kejim worlds were ones
whose runner came up after level 7,000 (graph load); the game refused the
window and the bench replaced them. `deferpin-20260925-a` is a failed first
run: the bench looked for DEFERRED in `server.log`.

## Consequences

- An arm is one world per case: a six-map, two-class comparison is about
  20 min an arm instead of 40, and no pair is lost to four misses.
- The start level within the class is now mostly 3,050 / 3,150 rather than
  spread over 3,000-7,000. Arms stay comparable with older rcon-pinned arms of
  the same class (measured above).
- `route_approve.py` stays on the rcon pin: it arms the tape in a separate rcon
  round trip after the start, so deferring the corner alone would not pin
  what it measures.

## Mirror twins

`blue_out_X`/`red_ret_X` and `blue_ret_X`/`red_out_X` run the same route from
the same stand. Over every class-pinned arm retained from 2026-09-24/25:

| Map | Twin pairs | Identical |
| --- | --- | --- |
| Carratos | 419 | 419 |
| ctf4 | 248 | 248 |
| Dantooine v3 | 361 | 361 |
| Metalorn v2 | 417 | 417 |
| Taloraan | 471 | 468 (the 3: L3 respawn 4/5) |
| Kejim | 531 | 491 (the 40: stands at L1-L3, median 3,025 ms apart) |

`movement_bench.py --skip-twins` drops each return leg whose outbound twin is
selected, on the maps in `movement_bench_lib.TWIN_SKIPPABLE`, keeping Kejim's
stands and Taloraan's L3 respawns; an unlisted map runs everything, and a test
fails if a fixture map is missing from the table. 216 cases a class become
118. Smoke (`.codex-research/twins-20260925-a`): ctf4 class 50, 18/18 passed,
18/18 identical to `deferpin-20260925-b`, 41 s. For comparison arms only;
gates and floors keep both twins. The comparison recipe is now in
[PLAN](../PLAN.md#cheap-comparisons-owner-order-2026-09-25).
