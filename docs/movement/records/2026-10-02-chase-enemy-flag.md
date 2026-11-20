# Defenders keep out of the enemy flag (2026-10-02)

Owner rule: a base "should not take the enemy flag except in an emergency"; for a chase, holding the flag correlates
with losing. For now the defenders take it not at all. Build `chaseflag-build-20261002-a` (base c88a6fb7 plus this
change), `.codex-research/chaseflag-20261002-a/` (`run.sh`, `summary.txt`, `chaseflag_metrics.py`,
`chaseflag_stand.py`, `chaseflag_events.py`). Control: `camper-20261002-b/ce-1..8` (build `camper-build-20261002-e`).
Kejim laps worlds, ten game minutes, milestone B1 settings (`--camper`, `bot_chaserGoal 1`, carry log on). 8 worlds an
arm, all distinct, no crashes.

## The fault

In ce, Chase D (red) took the runner team's (blue) flag 8 times in 8 worlds (1.0 a world). Base D took it 0 times. Its
1.4 red-flag touches a world are returns, not pickups. Before every pickup, chase D's CARRY_SAMPLE `goal=` was the blue
stand (2464,5312,1920). Usually that came from `bot_chaserGoal 1`, which makes the runner's stand the goal while the
flag is away. Twice it was an INTERCEPT goal, the last cell of the carrier's route home, which is the stand cell. Chase D
walked in to about (2464,5288). That is 24 u from the flag and inside the pickup box. Then it stood there holding the
flag. It sometimes carried it home: `red_capture` 0.38 a world. The `Chaser_v2` entry in `team_captures` (1.0) counts
these pickups, not captures. While chase D held the blue flag, the runner team could not capture.

## The fix

`bot_defenderFlagKeepOut` (default 1) and `FlagKeepOut` in bot2_defender.c work as follows:

- **Goal replacement.** Every movement goal of base, chase or solo D passes through `DefenderMove`. A goal inside the
  enemy flag's pickup box (`BG_PlayerTouchesItem`: x -50..44, y and z ±36) plus `bot_defenderFlagKeepOutMargin`
  (default 40) is replaced. The replacement is the graph cell outside the box, on the flag's level, nearest the goal and
  a little toward the defender. This covers a flag at home or dropped.
- **Straight walks.** The last-72-u straight walks to an item, a weapon or the own dropped flag do not run when the
  segment crosses the box. The planner takes over instead.
- **Logging.** FLAG_KEEPOUT lines log each episode.
- **Exemption.** The camper is exempt, because its job is to take the flag.

On Kejim the stand goal becomes (2464,5216). That is 96 u in front of the stand, on the carrier's way in.

## Results (per ten-minute world, from play-game.log)

| | ce (control) | ko (keep-out) |
|---|---|---|
| enemy-flag pickups: chase D / base D | 1.00 (0.33) / 0 | **0 / 0** |
| runner-team captures (se) | 10.62 (1.24) | 11.88 (1.09) |
| of which runner / camper | 7.38 / 3.38 | 5.75 / 6.12 |
| carrier kills, all (se) | 15.50 (2.01) | 16.12 (0.93) |
| carrier kills by chase D / base D | 10.25 / 4.62 | 9.88 / 6.00 |
| chase D returns (se) | 9.00 (1.21) | 9.62 (0.73) |
| chase D within 150 u of the stand at a capture | 3 of 85 | 4 of 95 |

FLAG_KEEPOUT fired only for chase D, 373 times in 8 worlds. 353 of those went to (2464,5216).

## Verdict

Enemy-flag pickups fell from 8 to 0, so the default is on (1). Carrier kills, chase D's carrier kills and its returns
are unchanged within noise. Runner-team captures moved +1.25, about one se of the difference. That is not a
factor-sized effect, and some rise is expected because a defender holding the blue flag blocked captures. The runner's
captures fell and the camper's rose (-1.6 / +2.7). This is borderline: chase D was rarely near the stand at a capture in
either arm (3 of 85, 4 of 95), so the stand-off position is not the likely cause. Treat it as noise until a later arm
repeats it. The bench movement floor is unaffected: the runner's movement code is unchanged.
