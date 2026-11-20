# 2026-10-21: tick-independent gameplay, first implementation

Design and decisions: [TICK_INDEPENDENCE.md](../TICK_INDEPENDENCE.md).

- **Branch:** `gameplay/tick-independence`, cut from `master` a265c31c, uncommitted.
- **Build:** `.codex-research/tick-dev-build` (incremental dev build). Its source manifest
  (20261021T123123252521Z.sources.json) matches the source the smoke ran on, with no file changed after the build.

## Logic check

`tools/tick_rules_sim.py` mirrors the `g_tick.c` rules over integer frames for 60 s. With an area on:

| Rule | 20 Hz | 30 | 40 | 60 | 90 | 125 | 250 |
|---|---|---|---|---|---|---|---|
| Strict 100 ms, stock | 150 | 132 | 125 | 112 | 110 | 104 | 104 |
| Strict 100 ms, code value | 100 | 100 | 100 | 100 | 100 | 100 | 100 |
| Strict 100 ms, today (exceptions) | 132 | 132 | 132 | 132 | 132 | 132 | 132 |
| Strict 300 / 600 ms, today | 330 / 627 | same | same | same | same | same | same |
| Think 100 ms, stock / code value | 100 / 100 | 132 / 100 | 100 / 100 | 112 / 100 | 110 / 100 | 104 / 100 | 100 / 100 |
| Sentry fire (200 in a 100 ms think), stock / today | 300 / 264 | 264 / 264 | 300 / 264 | 224 / 264 | 220 / 264 | 208 / 264 | 300 / 264 |
| Saber pass interval, mean (min-max) | 50 | 33 | 33 (25-50) | 33 (32-48) | 33 | 33 (32-40) | 33 (32-36) |

At 30 Hz every on-value equals stock for the exceptions, and the saber pass runs every frame.

## Native smoke

The smoke ran private localhost dedicated servers with stock bots, 90 s per world, four worlds at a time, on ports
29170-29183. The script and evidence are in `.codex-research/tick-smoke-20261021T123143Z/` on the main checkout's D:
drive: `results.json`, and per world `console.log`, `games.log`, `smoke.cfg` and the build manifest. The staging
copies stay under `%LOCALAPPDATA%/xenjk-play/tick-smoke-*` on C:.

| World | 20 Hz | 30 | 40 | 125 | 250 |
|---|---|---|---|---|---|
| Duel mp/duel1, 2 bots, g_tick* on: saber kills | 2 | 1 | 0 | 2 | 1 |
| Duel, g_tick* off | 1 | 0 | 2 | 3 | 2 |
| FFA mp/ffa3, 6 bots, on: kills (saber) | | 4 (3) | | 5 (5) | |
| FFA, off | | 2 (2) | | 5 (5) | |

All 14 worlds started, ran 90 s, answered rcon and shut down cleanly (exit 0), with no error lines in the console.
Kill counts show the saber game still runs at every rate. They are far too few to compare damage or outcomes.

An earlier pass (`tick-smoke-20261021T122431Z`) ran on the build before the gencmd-repeat edit. It gave the same
picture and is kept as superseded evidence.

## Not measured yet

- Whether saber results match 30 Hz at other rates: damage per swing, lock, parry and bounce rates, and lock
  mashing. That needs per-swing logging that survives FINAL_BUILD, plus a fixed duel fixture (`bot_forgimmick`,
  `g_debugSaberLocks`).
- Per-area rates in a live game: station heal per second, Force upkeep per second, turret fire. These are checked
  by the logic simulation only.
- The effect on Bot2 fixtures at sv_fps 40: movement gates and laps worlds.
