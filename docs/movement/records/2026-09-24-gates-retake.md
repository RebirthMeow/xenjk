# Level-1 and level-2 gates re-taken on the own-flag build

Recorded 2026-09-24. Both gates on one build after the [own-flag rule](2026-09-24-own-flag.md)
was adopted and the libraries re-explored under it.

| Item | Value |
| --- | --- |
| Build | `ownflag-build-20260924-c` (commit 7049d7ae) |
| Graph | research seed `g1-98eb1e97c24a4e21` |
| Library | `.codex-research/ownflag-chain-20260924-a/library/mp_ctf_kejim_-1301924053` (level 1 `t1e83`, level 2 `t1e83s1`) |
| Settings | `bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1`, everything else default (own flag on, search budget 4); harness off except the diagnostic arms |
| Worlds | `.codex-research/gate-20260924-e/`: `run.py` (167 worlds; the build-`d` phases plus level-2 phases E, F, G), `cost.py`, `analyse.py` |
| Sources | 1,125 hashes and both binaries matched before the first world and after the last (`verify-before.json`, `verify-after.json`) |

Start-class misses before release were retried by the runner and reached no
bot. One floor world (`blue_ret_L3_respawn5`) failed to create its runner
during a 9.5 s server hitch, an environment failure; re-run alone it passed at
8,700 ms (`floor-on-rerun-blue_ret_L3_respawn5`).

## Level-1 gate

| Row | Result |
| --- | --- |
| 1. Safety | Floor **36/36** (with the re-run), zero deaths, owners `move` and `jumppad` (accepted); seeded regressions at class 125 pass (16,375 and 16,375), respawn4 at class 50 pass (16,125 and 16,125). Harness-on arm: 526-685 comparisons per world, **zero mismatches**; decisions identical to the harness-off worlds at all six cells, every field but the rollout trace count (`traces`, an instrumentation counter the harness changes) |
| 2. Speed | Library **11,650 / 14,750 / 14,475 / 13,950 ms** at classes 75-150, six worlds a class, identical. Median **14,212.5** (bound 16,500), tail **14,750** (bound 18,000). **Met.** Controls 11,650 / 15,925 / 14,475 / 18,225 |
| 3. Mechanism | Waiting share, library against control: 0.0 / 0.0, **67.1 / 48.1**, 57.5 / 57.5, 46.8 / 62.5 percent. **Not met at class 100**: the library trip is 1,175 ms faster but lands and waits twice more (250 ms at (598, 859), 650 ms at (793, 1576)) where the control flies through |
| 4. Coverage | Twelve level-1 cases, three repetitions, library on and off: 72 of 72, no death. The library selects only on `red_out_L1_stand` (`exact`) |
| 5. Cost | One bot, budget 4, library on: p99 **12.9 / 11.8 ms**; 3 and 2 frames over 25 ms per 800. **Met.** Worst frame 70.9 / 383.0 ms, both in the `prepare` stage (K2's 50 ms into the leg, the bot not yet moving): one-off preparation, not a search |

Against the build-`d` gate library (13,625 / 13,975 / 12,300 / 14,075) the
level-1 stand is 1,975 ms faster at class 75 and 775 and 2,175 ms slower at
classes 100 and 125.

## Level-2 gate

| Row | Result |
| --- | --- |
| 0. Pre-gate floors | Kejim, ctf4, Dantooine v3 36/36 each on this build ([record](2026-09-24-own-flag.md)). **Met** |
| 1. Safety | Kejim floor 36/36 as above; level-2 harness-on arm 449-504 comparisons per world, **zero mismatches**, times equal to the harness-off worlds |
| 2. Speed | `red_out_L2_stand` **9,900 / 11,850 / 9,725 / 11,550 ms**, three worlds a class, identical. Every class under 13,000. **Met** |
| 3. Coverage | Twelve level-2 cases, three repetitions: 36 of 36, no death. Medians 8,225 (red out respawn4) to 13,200 (blue out stand); tails to 13,200 |
| 4. Cost | One bot with the Speed purchase (the cost runner's spend): p99 12.9 / 11.8 ms. **Met** |

## Owner ruling, 2026-09-24

Level-1 row 3 at class 100 is met in substance (the library trip is faster
than its control and waits longer at two landings mid-route). **Both gates
are passed on build `ownflag-build-20260924-c`.** Next (owner): the level-1
stand at classes 100 and 125 against the build-`d` library, and the `walked`
avoid about 1.2 s into the level-2 stand.

## Follow-up: the two slower cells, 2026-09-24

**Mechanism, level 2 classes 100 and 125.** The one-hop from the stand is the
same candidate at every class, but the start phase changes its landing: at
classes 75 and 150 it lands at 710-730 u/s past the ledge onto the route's
descent; at classes 100 and 125 at 626 u/s on the upper ledge at (235, -708),
90 units past the takeoff of the next strafe chord (edge 26663, takeoff
(201, -793)). Every candidate from that landing aims back along the path
through the passed takeoff and curls into the chasm's `trigger_hurt`
(`bot_forecastTriggerLog 1`, `.codex-research/l2-walked-20260924-a/`), so all
are refused (correctly); the walked check fires only when the landing hold
ends, 325 ms later, and the replan takes 375 ms more before a hop.

**Level 1 class 125** runs ahead of the build-`d` library trip until 8.7 s,
then lands at (2118, 3613) past a takeoff and is avoided as `walked` at
10,075 ms at 311 u/s; the next landing is 1.5 s later. Level 1 class 100 is
slower through several landings under 420 u/s, not one event.

**Two repairs tested and withdrawn** (`bot_moveLandPast`, test only, removed;
v1 reconstructed as `.codex-research/landpast-20260924-a/landpast-v1.snippet.c`,
its diff not saved before v2 replaced it; v2 as `landpast-20260924-b/landpast-v2.patch`), production library on:

| | L2 75 | L2 100 | L2 125 | L2 150 | L1 75 | L1 100 | L1 125 | L1 150 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Production | 9,900 | 11,850 | 9,725 | 11,550 | 11,650 | 14,750 | 14,475 | 13,950 |
| v1: avoid and replan on any landing past a takeoff | | 10,175 | 9,750 | | | 16,025 | 16,600 | |
| v2: only when the landing search admits nothing | 10,875 | 13,950 | 11,725 | 10,200 | 11,650 | 18,075 | 14,100 | 15,625 |

v1 fires 3-6 times a level-1 trip: at level 1 most landings are past the next
takeoff and chain on from there. v2 fires at least once in seven of eight
cells and is worse in five. Both change decisions early, which also moves the
trips off tapes recorded without them, so a fair test needs the libraries
re-explored under the rule; neither result justifies that cost yet.
