# Level-2 baseline: where the stand leg loses time

Recorded 2026-09-24, against the [level-2 gate](../../archive/movement-2026-10-05/PLAN.md#level-2-exit-gate-2026-09-24).
Build `droppush-build-20260924-b`, production configuration (research seed,
build-`k` library, `bot_moveExitSpeed=500 bot_moveStatePrefer=1
bot_moveReroute=1`). Worlds: `.codex-research/l2-baseline-20260924-a/`
(pinned stands), `.codex-research/l2-prefloor-20260924-a/` (floors).

## Results

| Class | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| `red_out_L2_stand`, three repetitions | 13,575 (2 of 3; one world missed its class) | 15,600 | 15,200 | 13,575 |
| Level 1 with library, gate build `d` | 13,625 | 14,125 | 13,400 | 14,075 |

Every class is deterministic. Pre-gate floors: Kejim, Dantooine v3 and ctf4
each 12/12 at level 2, zero deaths, owner `move` only.

## The first 3.5 seconds: no hop under Speed

Class 150, aligned at release (the Speed purchase at the stand, 152, -1160):
the leg is 13,525 ms with **5,675 ms not airborne** and the first launch at
3,525 ms. The level-1 control at the same class (`gate-20260924-d/c150-ctl-1`)
launches at 550 ms and spends 2,000 of 16,025 ms not airborne.

- Release frames: every straight candidate is `refused` (12 and 16).
- Strafe edge 50732 is `deferred`, then `avoid` (`why=walked`).
- 3,925 to 6,450 ms: straight candidates end `fell` or `no_progress` (6+6,
  5+1+6 refused, 4+4, then 12 of 12 `fell`) along the ledges down to
  (858, -880, 1604), where the first two-hop sequence is admitted 12 of 12.

The bot runs the upper ledges at 400-550 u/s. Reading: hops aimed for level-1
speeds carry past the ledge turns at Speed's 1.7 times ground speed and are
forecast into the drop. Not yet tested.

## Classes 100 and 125: a pillar and a brake

Both take a two-hop sequence from a landing near (934, 1306) whose second hop
is forecast to land at (1275, 2179) at **138.9 u/s** (`land1_speed=138.9`),
the best of 3 admitted candidates (score 337.4). Class 150 lands 38 units
away, at (896, 1309), and its sequence lands at (1721, 2375) at 970.5 u/s
(score 1,107.8, 2 admitted). The forecast is right; the offer is poor. A
`walk brake` landing near (1833, 3055) at 820 u/s then costs about 0.5 s
more. Classes 100 and 125 lose 1.6-2.0 s against class 150 from here on.

## Two tests the same day

**Route drops read as falls (`bot_moveFallPath`, test only, removed; patch
retained as `.codex-research/fallpath-20260924-a/fallpath.patch`).** The
`[SPIKE]` lines show why the ledges yield no hop: at 5,925 and 6,450 ms all 12
candidates are `fell`, dropping 260-312 units down the route's own descent
and landing at 776-976 u/s with 1,000-1,800 units of progress; at 6,700 ms the
same hops are admitted because the drop is 216-235, under `MOVE_FALL_DROP`
(256). At release every candidate is `refused` with `unsupported=256`: the
root stands in a trigger, as at level 1. The test let a drop past the bound
pass when the next grounded frame is near the root's path window
(`NearPathCell`, 2 steps in height). Build `fallpath-build-20260924-a`:

| Class | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| Control | 13,575 | 15,600 | 15,200 | 13,575 |
| Fall path | 13,225 | 14,075 | 15,325 | 15,200 |

A wash (-125 ms summed, one class +1,625). The first launch moves only from
3,525 to 3,350 ms at class 150: the stretch after release is a hairpin, so
the straight ahead is short whatever the fall bound. Not adopted.

**The level-1 library is selected at level 2.** A library key is the leg's
objective, start cell and phase, with no level, so every level-2 stand
selects the level-1 tape on the `exact` rung; its entry 0 is attempted and
never matches (the tape's route goes north from the stand, level 2's goes
south). With the library off (`.codex-research/l2-nolib-20260924-a/`, build
`fallpath-build-20260924-a` with the test cvar unset) the four classes give
the same 13,575 / 15,600 / 15,200 / 13,575 ms, so it is harmless today; a
level-2 library needs the level in the key first.

## Next

1. Why the release and ledge candidates fall under Speed: hop aim and length
   against carried speed on the upper ledges.
2. The offer at the (934, 1306) landing: why no candidate around the pillar
   is admitted.
3. A level-2 decision library, as at level 1: the explorer gained 1.8-2.7 s
   a class there (487 worlds, about 2 h 20 min). It needs the level in the
   library key and a level option in `tools/movement_decision_explore.py`
   (its base filter is `^red_out_L1_stand$`).
