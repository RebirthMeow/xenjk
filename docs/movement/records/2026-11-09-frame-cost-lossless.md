# 2026-11-09: frame-cost items of the Bot2 review (BM-11, BM-12, BM-13, BA-12, the standoff loop, BP-2, guide builds)

Branch `claude/bot2-review-next`, commits 82ee8c60 (BM-11, BM-12, BM-13, BA-12) and 829c53e4 (`bot_moveAvoidFull`).
Review items in [docs/review/2026-11-09-bot2.md](../../review/2026-11-09-bot2.md), lane reports
[move.md](../../review/2026-11-09/bot2/move.md), [ai.md](../../review/2026-11-09/bot2/ai.md),
[plan.md](../../review/2026-11-09/bot2/plan.md). No wall-clock run (the owner may be playing): the lossless items are
checked by replay, the standoff fix by work counts in one standoff world.

## Lossless (82ee8c60)

- **BM-11** (`bot2_mover.c`): the mover rest memo is keyed on the real frame and `level.startTime`, not the
  simulated `level.time`. `RestUntil` reads entity fields and no clock, so a rollout standing on a lift no longer
  rescans every entity at each simulated step.
- **BM-12** (`bot2_move.c` `StepOffCell`): a stranded hold's step-off cell (49 grid points, up to two hull traces
  each, asked every held frame and by `Move_Stranded`) is kept per client while the bot stands at exactly the same
  origin on the same graph, up to 500 ms. A body walking through a probe's trace inside that window is seen at the
  next ask after it; strictly that is not lossless, but a stranded hold is rare and the replay check covers it.
- **BM-13** (`ai_bot2_movement.c` `Bot2_NextPush`): `trigger_push` entities come from a list collected once a frame
  (again when the level time, the map or `level.num_entities` changes) instead of a classname scan of every entity
  by every bot, three times a frame. It returns what `G_Find` by classname returns.
- **BA-12** (`ai_bot2_objective.c` `Bot2_NextFlag`): a team's flag entities likewise, used by the runner's dropped
  flag, chase D's dropped own flag and the defenders' `EnemyFlag` (which `KeepOutFlag`, `StraightOk`, the keep-out
  box and `ForbidSync` go through). A dropped flag is spawned mid-frame when a carrier dies, so the list is also
  collected again after any entity spawn or free: `bot2_entitySerial`, incremented in `G_InitGentity` and
  `G_FreeEntity` (two one-line upstream calls). The stand flag was already cached (`Bot2_BaseFlag`, 2026-10-03).

## The CTF standoff loop (829c53e4, `bot_moveAvoidFull`, default 0)

Found on 2026-11-09 (replan-budget record, "Standoff worlds"): with both carriers waiting at home the camper replans
every frame, 2,400 replans a minute in `a-dan-4`. Read from that world's log: the camper (client 3) carries at its
stand with Force 0, its route home goes over 256 u climb edges, every climb search has 66 candidates and all of them
are short (`kind=climb ... admitted=0 short=66 rise=256.0 rollout_ms=12`), and its avoid list is full (`avoid=32`).
A guest's failed edge goes on its own avoid list for the objective (`ChargeFailure`, `AvoidEdge(...,0)`), and an
untimed entry into a full list overwrote the last slot, which is the newest, then replanned at once. So climb edges
488 and 493 took turns: each failure evicted the other, the replan routed over it, and its search failed.

With `bot_moveAvoidFull 1` the oldest untimed entry makes room (the rest keep their order), and a replan forced
through a full list keeps the replan cadence (`MOVE_PATH_RETRY`, 500 ms) instead of running at once. Off, the code
path is today's. It changes what a bot with a full list plans, so it is an arm, not a default.

## Expected effect (written before measuring)

The lossless commit replays the guide campaign's control (Kejim and Dantooine seed 3) and the replan-budget A arm's
standoff world (`a-dan-4`, the same cvars on build -d) exactly. With `bot_moveAvoidFull 1` that standoff world's
camper replans at most twice a second while the list is full: replans in the standoff about -95%, climb rollouts
likewise; what it does instead (stand at its stand with the flag) is what it already did between failures.

## Not done

- **BP-2** (a landing state relaxes 43 strafe edges into 26 distinct keys; 3.3 us an expansion on Kejim against
  0.66 on Dantooine). The review asks to measure where an expansion's time goes before choosing among its three
  fixes. Counts are known (345 near-list entries scanned, 43 admitted, 26 keys); the split of the 3.3 us between
  the scan, the per-edge tests, `StateRelax`'s corridor, body and standing checks, the key's `atan2f`, the hash
  probe, the guide read and the heap needs a sampling profile of a Kejim world, which is a timed run. Held for the
  owner. Fix 1 (keep the cheapest admitted edge per successor key within one expansion) must also apply
  `StateRelax`'s edge-dependent test (`Graph_ForbidBlocks`) before it merges, or a forbidden cheapest edge would
  hide an allowed dearer one.
- **Spreading guidance-table builds over frames** (12-15 million settled cells a world, up to 77,000 in one frame).
  Not built: it changes which searches are guided (a search asked while its table is still building runs
  unguided), so it is an outcome arm, and the shared settle heap (`guideHeap`) allows one build in progress at a
  time. Design: a slot gets a resumable build (the heap and a settle budget a frame, as the route tables'
  `Graph_PathTableStep`), `GuideFor` returns -1 for a slot still building, and preloaded goals stay built at the
  load. Measure by `guide_pops` per frame (max and p99) and team captures.

## Measurement

Build `.codex-research/review-next-build-20261109-d` (829c53e4), evidence `.codex-research/framecost-20261109-a/`
(`runframe.py`, `run.sh`, `chain.log`, `worlds/`).

## Measured (2026-11-09)

- **Lossless commit replays.** On build -d the defaults replay the guide campaign's control on Kejim and Dantooine
  seed 3 (game log 487 and 477 lines, decision lines 148,173 and 151,020, identical), and the A arm's standoff
  world on its old build (`replan-budget-20261109-a/worlds/a-dan-4`, with the same cvars: game log 402 lines,
  decision lines 136,855, identical; `--min-seconds 500`, its game log goes quiet at 520 s). BM-11, BM-12, BM-13 and
  BA-12 change nothing a bot decides in these worlds. Their saving is not measured: it is wall-clock work (entity
  scans, the step-off traces) that the work meter does not count, and it needs the timed run.
- **The standoff world with `bot_moveAvoidFull 1`** (`af-dan-4` against this build's own replay of `a-dan-4`, same
  cvars otherwise; `work-standoff.txt`, `tools/laps_work.py`): replans 8,463 -> 4,149 (-51%), failed climb searches
  4,380 -> 931, forecast steps 15.3 M -> 8.6 M (-44%), forecast traces -60%, engine traces -12%, estimated frames
  over 25 ms 5,824 -> 1,982 (-66%); p99 and the worst frame unchanged (57.5 / 58.0 ms est., 203 ms). The world
  then plays differently: runner hops 470 -> 572, runner speed 295 -> 353 u/s, the same 3 captures. One world;
  the estimate and the outcome side need the arm below.

An arm on ordinary worlds (`af`, `bot_moveAvoidFull=1`, Kejim 35-50 and Dantooine 35-42 against the campaign's
control, build -e) shows how often a full list happens outside a standoff and whether it costs anything:
`.codex-research/avoidfull-20261109-a/` (`identity.txt`, `work.txt`, `compare-*.txt`). Build -e's defaults replay the
control on seed 35 of each map. Of the 24 `af` worlds, 22 replay their control exactly: a full avoid list is rare
outside a standoff. Paired work (16 Kejim, 8 Dantooine): estimated frames over 25 ms +0.0% / -0.5%, replans
-0.1% / -0.5%, engine traces -0.1% / -0.3%, runner hops -0.1% / -0.4%; team captures -0.04 +- 0.36 and
-0.16 +- 0.55 (undecided, as expected when 22 of 24 worlds are copies), runner falls +0.10 +- 0.72 and
-0.33 +- 0.55.

**Recommendation:** `bot_moveAvoidFull` 1 removes the standoff loop (one world: frames over 25 ms -66%) and changes
almost nothing elsewhere. It is a behaviour change for the owner to take or leave; a timed standoff world would
show the milliseconds.

## Default (owner, 2026-11-10)

`bot_moveAvoidFull` is 1 by default since 1fb110d0 (owner); 0 restores the old overwrite of the last slot.
