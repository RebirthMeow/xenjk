# Review fixes for 3700dbdd: identity checker, lossless batch, theft goal (2026-10-04)

An outside review of 3700dbdd (three issues, four performance items); owner: do all three of the proposed steps.
Research directories: `.codex-research/perf-20261004-a/` (identity and timing), `.codex-research/theft-20261004-a/`
(theft goal arms, `theftgoal.py`, `compare.txt`).

## Identity checker (`tools/laps_identity.py`)

It trimmed both runs to their common end and never required a run to be complete: two empty logs, or a run that
stopped early, passed. Now each run must reach `--min-seconds` (default 590 game s) in both the game log and the
decision lines and hold `--min-lines` (default 10,000) decision lines; otherwise it prints `TRUNCATED` and exits 2
("not compared"), never 0. InitGame's `\version\` and `\gamename\` (commit and build date) are masked: they made
every cross-commit pair differ at line 1. The earlier identity results (200,000-820,000 decision lines a pair,
full-length runs) are unaffected. Checked: empty logs exit 2, a missing world exits 2, the negative control
(seeds 1 against 4) exits 1, a wall-capped world below (552 s) is refused.

## Lossless batch (build `perf-build-20261004-a`)

- **State-table slot** (bot2_graph.c `StateEntry`): `(key*K) & mask` kept the product's low bits, which depend only
  on the key's low 18 bits (the cell and two speed-band bits), so every heading, tap, Speed and taken state of a
  cell probed from one run of slots. The slot is now the product's top 18 bits. Entries are numbered by creation, so
  the search is unchanged.
- **Reverse edge index** (`Graph_Reaches`): built once per topology (cell count, edge count, `reachTopology`) instead
  of on every goal-cache miss.
- **Trace-key hash** (bot2_forecast.c `TraceKeyHash`): the key's words are copied out with `memcpy`; reading its float
  fields through an `unsigned int` pointer broke strict aliasing on GCC/Clang. Same hash values.
- **Dead enemies** (ai_bot2_objective.c `RunnerUpdateThreats`): with `bot_runnerKillFeed` on, a dead enemy takes no
  sight trace; the branch is the same.

The forecast's per-rollout cvar reads (review item 3) were left: a handful of lookups a rollout against about 11 traces
a step.

**Identity:** Dantooine seeds 1, 4, 8, 12 against the retained perf build c/b2 worlds: game logs and decision lines
identical (201,462 / 209,784 / 202,968 / 202,759 decision lines). Both timing pairs below are identical as well.

**Frame cost** (one world at a time, 25x, alternating with perf build c, `bot_moveFrameMeter 1`):

| Dantooine seed | mean per frame, c -> new | p99 | frames over 25 ms |
|---|---|---|---|
| 2 | 8.31 -> 8.19 ms | 51.5 -> 51.5 ms | 1,693 -> 1,625 |
| 1 | 6.98 -> 6.96 ms | 51.5 -> 51.8 ms | 1,556 -> 1,548 |

No measurable gain. A first round on a loaded machine (frame costs about 50% above these) gave seed 3 -18% and lost
the new build's seed 2 to the wall-clock cap after a ~100 s machine stall (`t-d/w-2`, kept); it is not used. The
probe clustering was real but not where the long searches spend their time.

## Theft goal (`bot_defenderTheftGoal`, default 1; build `perf-build-20261004-b`)

An unseen theft (`bot_defenderTheftKnown`) set `lastSeen` and started the chase but left `chaseGoal` alone. Chase D
replaces it with its intercept in the same frame; base and solo D walked to whatever goal an earlier chase left (or
the origin) until the next sighting. With the cvar on, an unseen theft also sets the goal to the stand.

16 paired Dantooine seeds an arm (`theft-20261004-a/run.sh`). The off arm replays the pre-fix build's worlds exactly
(seeds 1, 4, 8, 12 identical to `perf-20261004-a/d-*`).

- **Mechanism** (`theftgoal.py`): base D's goal at the CHASE line after an unseen theft was a median 914 u from its
  stand off (303 of 387 over 300 u), 0 u on (0 of 393).
- **Outcome** (`compare.txt`): team captures 8.32 -> 8.32 (+-0.79), both teams' captures 10.41 -> 9.61 (-0.80 +-
  0.68), runners' weapon deaths +1.3 +- 1.0, falls unchanged; defender enemy-flag pickups 0.31 -> 0.92 a world
  (+0.62 +- 0.26, about 2.4 standard errors). Nothing else moved beyond its spread.

The fix does what it says and costs nothing measurable at 16 worlds; the one borderline change is more defender
enemy-flag pickups (carry-home makes those useful runs home, see the HANDOFF open items). **Done (owner, 2026-10-04):**
default on.
