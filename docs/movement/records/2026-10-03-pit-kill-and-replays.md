# Pit kill, and exact replays of a laps world (2026-10-03)

## Pit kill

Owner: mirror the legacy Bot2's rule, "as long as it's triggered after for sure being in the pit (trigger_hurt
activation)". `bot_pitKill` (ai_bot2.c `PitKill`, default 1 in play mode, `bot_movementOnly 2`): a runner, camper or
declared defender whose `ps.fallingToDeath` is set - only a fall-to-blackness `trigger_hurt` sets it, and the game
always kills it `FALL_FADE_TIME` (3,000 ms) later - takes that same native kill at once: `G_Damage` 9,999,
`MOD_FALLING`, credited to a recent pusher (`otherKiller`). Kill credit and fall counts are unchanged; the respawn
comes about 3 s sooner. `[MOVE] PIT_KILL` lines (falling_ms 25: one frame after the touch).

Measured (build `pitkill-build-20261003-b`, `.codex-research/pitkill-20261003-a/` and `rep2/`, 4v4 Kejim, 32 worlds an
arm pooled, per world):

| | off | on | difference |
|---|---|---|---|
| falls, both runners | 8.25 | 10.11 | +1.86 +- 0.63 |
| captures, both teams | 8.03 | 7.45 | -0.58 +- 0.53 |
| runner team captures | 6.33 | 6.79 | +0.45 +- 0.67 |
| camper falls | 5.30 | 5.06 | -0.23 +- 0.55 |

The extra runner falls are mostly carrying the flag home and camping; by class own falls 2.12 -> 3.81 and thrown
3.31 -> 4.38 a world. Ruled out: relabelled deaths (weapon deaths flat), exposure (time alive unchanged; the 3 s a pit
fall saves become strafing, worth about +0.2 falls at 0.41 a minute alive), lost edge learning (charged failures
come from takeoff refusals, not falls: 70.5 against 65.3 a world). The mechanism is unexplained.
**Owner (2026-10-03): keep it on.**

## Exact replays

A laps world did not replay: the game's seed was `Com_Milliseconds()` at map load; setup arrived over rcon at
wall-clock game times; and the engine set the Ghoul2 clock (`G2API_SetTime`) once per `SV_Frame`, after a batch of
game frames, so at 25x the poses that missile and saber collision trace against depended on how many frames the
wall clock batched. The first split traced was one repeater-alt hit landing in one run and not the other.

- `g_randomSeed` (g_main.c): a fixed seed when set.
- `botat <level_ms> <command>` (bot2_svcmds.c): a console command at a fixed game time.
- `defense_laps.py --seed N`: g_randomSeed N, and the adds, release and rate change by `botat` at fixed game times.
- sv_main.cpp: `G2API_SetTime` after every game frame inside the frame loop (unchanged at one frame a loop).
- `bot_stateTrace "from to"`: every client's state, its last command and every missile, exact, each frame.

Two 180 s 4v4 worlds on seed 12345 (build `seed-build-20261003-d`) are identical: 120,535 state lines, the same game
log hash. Tools in `.codex-research/seed-20261003-a/` (`pair.sh`, `statediff.py`, `consolediff.py`, `diverge.py`).

The pit-kill pair (seed 12345, `p0`/`p1`) is identical to 31,175 ms, the first pit kill (the blue runner, pulled by
the red chase D). The runner then respawns at a different spawn point (spawn choice draws on the random state) and the
worlds become different games within seconds, so one pair attributes the split, not later falls. A many-pair test of
the pit-killed bot's next life is the next step if the mechanism is wanted.
