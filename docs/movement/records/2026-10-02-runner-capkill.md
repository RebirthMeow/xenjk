# Runner self-kill after a capture (2026-10-02)

Owner: "make runner self kill after capping to replenish force". `bot_runnerCapKill 1` (ai_bot2.c, default 0): on a
new native capture (`PERS_CAPTURES`), a movement-only runner with Force under `bot_runnerCapKillForce` (default 100)
calls `G_Kill` (`[RUNNER] CAPKILL`). Plan: [MILESTONE_A.md](../MILESTONE_A.md), step 1b.

Build `capkill-build-20261002-b`, current defender defaults, errands hopped, `bot_defender 2`, 16 laps worlds an arm
(`.codex-research/capkill-20261002-a/`):

| arm | captures (se) | runner pickups | runner deaths | capture → next pickup, median / mean | Force at that pickup |
|---|---|---|---|---|---|
| off | 3.81 (0.48) | 14.4 | 36.6 | 35.0 / 38.8 s | 50 |
| on | 3.50 (0.34) | 13.5 | 40.3 | 15.7 / 29.8 s | 50 |

- It fired on 56 of 56 captures; the runner capped with a median 4 Force.
- The next pickup comes much sooner (median 35 → 16 s); the Force it arrives with is unchanged (spent on Speed on the
  way, either way).
- Captures do not move: of about 14 pickups a world only 3.5 are captures (about three grabs in four end with the
  carrier dead), so the saving applies to 3.5 cycles of a ten-minute world, about 30 s, worth roughly +0.2 captures,
  under what 16 worlds resolve. The extra runner deaths are the self-kills (3.5 a world).
- Default left at 0 for the owner. The term that limits captures is carriers dying on the way home, which is
  milestone A's absorb and protect work.
