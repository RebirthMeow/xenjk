# Knock replays: fall scenarios that reproduce the knock (2026-10-01)

Owner: "fix the replays so they reproduce the knock". This record follows the scenario fixture of
[defender falls and route](2026-09-30-defender-falls-and-route.md) (`tools/defense_laps.py --scenario`,
`scenarios.json`). Campaign: `.codex-research/knock-20261001-a/` (`runknock.py`, arms `lead400`, `lead200`,
`atknock`). Generator: `.codex-research/falls3-20260930-a/make_knock_scenarios.py`. Builds
`knock-build-20261001-a` (state restore) and `knock-build-20261001-b` (+ declared knock). Uncommitted.

## The first scenario set reproduced nothing

The 2026-09-30 baseline (`scen-20260930-a/base`, build `falls3-build-20260930-t`) died at 62 of 98 worlds when D:
filled. Its 58 finished worlds had **no defender fall at all**. They placed every bot exactly, but restored
positions only, 1 s before the fall: the defenders spawned with the loadout's first weapon, the runner at full
health, and the fight that led to the fall did not happen. Over each world's first 3 s, base D fired in 7 of 40 hold
replays and was knocked in none; chase D fired in 4 of 18 splash replays and was knocked in 3.

The source falls themselves (`make_scenarios.py`'s 49) split two ways: about 13 began with the defender's own splash
knock (its FIRE, then `edge=knock reason=hit` within 300 ms); most of the 33 "hold" falls had no shot and no hit knock
in the 3 s before the fall. The knock replays below cover the first kind only.

## State restore before the shot (build a)

New fixture inputs, all unset by default (no change to any other world):

- `bot_defenderPostState` / `bot_chaserPostState` `"health= force= speed_ms= weapon=W[a] air=0|1 knock=0|1"`
  (`bot2_defender.c` `PostState`, after the loadout): health, Force, Speed's remaining time, the weapon in hand held
  as a fresh choice (refused unless in the loadout), and `air=1` keeps the declared height instead of dropping to the
  floor. Printed as `[DEFENDER] POST_STATE`.
- `bot_cornerHealth` and `bot_cornerAir` (`bot2_corner.c`): the runner's health, and a start in flight (no floor
  required under the pose, ground none).
- `defense_laps.py` passes a scenario role's `state` and the runner's `health`/`air`, and echoes the corner start's
  RCON reply lines (`START`, `REFUSED`, `POST_STATE`, declared knocks), which never reach the server log.

`make_knock_scenarios.py` scans the full-configuration falls3 worlds of builds h..s (164) for defender falls with an
own-splash knock in the 2.5 s before leaving the upper floor: 44 (26 chase D, 18 base D). Each starts LEAD ms before
the shot, from the play samples (the scenario defender holds the shot's weapon).

| Arm | Worlds | Fired | Own knock | Fell |
|---|---|---|---|---|
| `lead400` (400 ms before the shot) | 84 | 81% | 45% (base 39%, chase 50%) | 4 |
| `lead200` | 84 | 90% | 51% (base 44%, chase 56%) | 0 |

Two scenarios are refused every time (a pose inside solid). The two repetitions of a scenario nearly always agree:
laps worlds replay deterministically, so a repetition is not a second sample.

**Why not most: the knock is partly chance even from the exact moment.** Over the source worlds, a close splash shot
knocks its own shooter only 17-44% of the time (base D flechette alt under 120 u 36%, rocket 42%; chase D 31-44%;
`baserate.py` in the campaign folder, run from `.codex-research`). The replays' 51% is already above that.
Not the cause: aim noise (none inside 200 u, `bot2_defender.c` close aim), sample lag between clients (no relation),
shot distance or burst distance (same in knock and miss replays). 14 of 33 fired-without-hit replays took a ground
knock the brace absorbed (`BRACE_KNOCK`), below the movement's hit threshold.

## Start at the knock frame (build b): the fixture to use

`make_knock_scenarios.py at-knock` starts at the knock frame itself: the scenario defender at the `edge=knock
reason=hit` line's exact position with its post-knock velocity, and `knock=1`, which arms the movement's knock
recovery at release (`Move_DeclareKnock`, `bot2_move.c`: the hit check cannot see a velocity set by a reset; it
prints `edge=knock reason=declared`). The knock is a declared intervention, so it is reproduced every time; what is
measured is what the defender does after it, which is what the fall fixes change.

Written to `tools/fixtures/movement/defender/knock_scenarios.json`: 33 scenarios (18 chase D, 15 base D); 11
knock frames were skipped because a bot was dead at that sample (usually the runner, killed by the same shot).

| Arm | Ran | Fell |
|---|---|---|
| `atknock` | 28 | **12 (43%)**: base D 7 of 13, chase D 5 of 15 |

Checked on `base_h-4_469350`: the replay starts at x=196.4 y=-466.8 z=1852.0 with v=(242.7, 169.0, 241.7), as
recorded, then logs the recorded sequence (`recover no_path`, `hold`) and falls. Five worlds are refused every time:
three defender posts overlap another body (positions sampled at different times) and two runner poses sampled as
grounded have no floor within 4 u.

## Use

```
KNOCK_SCEN=<scenario file> python .codex-research/knock-20261001-a/runknock.py <build> <tag> 1 6
```

One repetition per scenario (repetitions replay). Compare arms scenario by scenario on `fell`; a fall fix should turn
falls of the 12 into non-falls without new ones among the 16. About 2 minutes for the set. Runner floor not run:
every new input is unset outside scenarios and `Move_DeclareKnock` has no other caller.

## Judging the fall fixes in the code (2026-10-01, build b)

The open items of the 2026-09-30 handoff (a better knock-safety check, a stop for base D's holds) are not written
yet; what is judged here is each fall fix already on by default, switched off against the all-on control, per
scenario (`cmp.py`). Noise floor: the control run twice differs on 1 of 27 scenarios.

| Fix switched off | Scenario set | Control falls | Arm falls | Per scenario |
|---|---|---|---|---|
| `bot_moveKnockRecover` | knock frame | 12 of 28 | 13 | +1 (noise) |
| `bot_moveGuestBrake` | knock frame | 12 | 12 | identical |
| `bot_moveGuestLip` | knock frame | 12 | 12 | identical |
| `bot_defenderKnockSafe` | 200 ms before the shot | 0 of 42 | 4 | +4, none removed |

After the knock nothing helps because the knock's size decides: 9 of 12 knock-frame replays with a knock of 400 u/s
or more fall, 3 of 15 under 300. The lever is before the shot, and the knock-safety check is the one fix with a
measured effect.

**Its misses: the flechette alt estimate is too low.** `kspredict.py` (campaign folder, run from `.codex-research`)
compares `SelfKnock` with the knock each braced close shot gave, over the falls3 worlds of builds h..t (13,773
braced shots, 5,109 knocked). The estimate is above the actual knock for 86% of knocked shots, but for none of the
114 knocks of 600 u/s or more, and those are the flechette alt's:

| Weapon, burst distance | Knocks | Measured p90 | p99 | `SelfKnock` |
|---|---|---|---|---|
| flechette alt, under 80 u | 131 | 1,050 | 1,160 | 600 |
| flechette alt, 80-130 u | 495 | 675 | 1,073 | 480 |
| flechette alt, 130 u and over | 172 | 425 | 910 | 410 |
| rocket, under 80 / 80-130 / over | 230 / 460 / 191 | 470 / 395 / 354 | | 500 / 385 / 300 |
| repeater alt, under 80 / 80-130 / over | 1,015 / 2,019 / 396 | 300 / 250 / 225 | | 390 / 225 / 210 |

Rocket and repeater rows are close to their 90th percentiles; the flechette alt rows are well under (two balls can
both burst on the shooter). Seven of the 14 knock-set falls of 400 u/s or more were flechette alt knocks.

## The flechette refit (build c)

`SelfKnock`'s flechette alt rows refit to the measured p90: 1,050 / 675 / 425 (was 600 / 480 / 410), build
`knock-build-20261001-c`; control build b. Nothing else differs.

Before-shot replays (42, one each): own knocks 23 to 19, falls 0 to 1 (the control has none to remove, so this set
only shows no harm). Full laps worlds (`.codex-research/fit-20261001-a/`, `run.sh`, `compare.py`): the falls3 run17
configuration, 600 game s, 16 worlds an arm, two of each arm in every wave; no duplicate game logs, no crashes.

| Per arm (161 game minutes each) | Control | Refit |
|---|---|---|
| base D falls | 13 (0.81 a world) | **6 (0.38)** |
| chase D falls | 18 (1.12 a world) | **10 (0.62)**, 5 of them in one world (`fit-16`) |
| runner kills, base D + chase D | 261 + 281 = 542 | 295 + 257 = 552 |
| captures | 55 (3.4 per 10 min) | 50 (3.1) |
| flechette alt shots, base D + chase D | 136 + 236 | 122 + 174 |
| own knocks of 600 u/s or more | 13 | 7 |

Both halves agree (falls 12 then 19 in the control, 6 then 10 in the refit). Falls halve (31 to 16) with kills
unchanged; captures move inside their spread (sd 1.5-1.9 a world). Both arms fall more than the 2026-09-30 build s
figures (0.55-0.7 a world); judge against the paired control only. Runner floor not run: a defender-only change.

## Chase D's five falls in `fit-16` (2026-10-01)

Scripts in `.codex-research/fit-20261001-a/`: `timeline.py` (one defender's events around a departure),
`braceslow.py`, `falltax.py` (the falls3 classifier, pointed at this campaign). Not one defect: five falls, four
mechanisms, and the world is otherwise ordinary (168 chase D hops, 68 chases; its low kills and high captures follow
from the falls).

| Left the floor | Mechanism |
|---|---|
| 111200 | Its flechette shot 425 ms before a committed hop's launch started a brace, which owns the move: the run-up was spent bracing and the hop launched at 361 u/s against 499 planned, then fell short off the walkway. |
| 410725 | A matched launch (613 against 607 u/s), then a mid-air collision with base D (132 u away at launch, closing at 475 u/s): 613 to 225 u/s in 175 ms, landing 123 u off, then over the lip. The "body contacts in the air" of the 2026-09-30 trace. |
| 444250 | Its own rocket at 92 u knocked it 490 u/s: the rocket's tail (p90 395, p99 570), which KnockSafe's p90 lets through by design. |
| 479625 | A 145 u/s knock ended a hop chain at 778 u/s; with no plan left it took a `no_path` decline hold at 750 u/s and slid off. |
| 592500 | `CHASE_END` changed the goal mid-run-up at 484 u/s on the lip (z 1806.9); the new plan was `no_path`, the hold did not stop it. |

Across all 32 worlds (`falltax.py`): the refit took base D's own-splash falls from 4 to 0 and chase D's hop falls
from 8 to 3; chase D's remaining 10 are hop 3, walk_fast 3, splash_own 2, knock_other 1, hold 1. A brace over a hop's
run-up is rare (10 slow braced launches in 32 worlds, 3 then below z 1700).

**The common thread is speed when a plan ends.** Falls 4 and 5 lost their committed sequence at 480-780 u/s near a
lip (a knock miss; a goal change at `CHASE_END`). The decline hold does brake on the ground (`DeclineHeld` ends in the
walk's brake, no wish velocity), but fall 4 alternated hold and walk (a walk restarted at 739 u/s between holds) and
kept about 750 u/s for 350 ms, and fall 5 was already airborne below the floor when the hold began. Candidates, not
built: a guest that loses a sequence above walking speed keeps braking until it is under the walk speed before any
walk restarts; chase D's `CHASE_END` goal change waits for a committed run-up to launch or end. Judge on laps worlds
by the hold and walk_fast classes.

## Speed when a plan ends: both candidates tried, neither kept (2026-10-01)

Build `plan-build-20261001-a`, campaign `.codex-research/plan-20261001-a/` (`run.sh`, `compare.py`, `falltax.py`);
the code is kept as `plan-fixes.patch` there and was reverted from the tree. Two guest-only settings:
`bot_moveGuestShed` (a guest whose committed sequence ends early - a knock, a declined run-up, an objective reset -
brakes on its walk until within 25 u/s of its wishspeed) and `bot_moveGuestRunup` (bot_moveArriveSafe's objective
hold also covers a guest's run-up, for at most 800 ms). Four arms on the one build, 16 laps worlds each, one of each
arm per wave, falls3 run17 configuration:

| Arm | Chase D falls | Base D falls | Total | Runner kills | Captures |
|---|---|---|---|---|---|
| both off (control) | 11 | 6 | 17 | 537 | 53 |
| shed only | 13 | 6 | 19 | 531 | 61 |
| runup only | 10 | 4 | 14 | 492 | 57 |
| both on | **23** | 7 | **30** | 554 | 69 |

The run-up hold alone is inside the noise (and two of its worlds were byte-identical, so it has 14-15 independent
worlds). The shed alone adds nothing. Together they double chase D's falls, mostly hops (10 against 5). The shed was
far broader than intended: 936 of its 974 triggers in the shed arm were objective resets, about 60 a world, because
chase D's goal moves constantly; only 38 were knocks and none were declines. Chase D drops below z 1700 with a shed or
objective hold in the 3 s before: 6 (control), 14 (shed), 12 (runup), 21 (both).

Runner floor on the build: Kejim classes 150 and 50, 24 of 24 each. The comparison floor on the HEAD build was refused
(the working tree differed from it), and is moot with the code reverted.

What remains of `fit-16`'s mechanisms is unfixed: a brace over a hop's run-up, the mid-air teammate collision, the
rocket's knock tail, and losing a plan at speed. A narrower shed (a knock or a decline only, never a goal change)
is untested.

## Runner stuck bail-out (2026-10-01)

In the owner's spectated game (`play-vs-runner-20261001T111858Z`) the runner, carrying, fell from z 1714 at 862 u/s onto
Kejim's chasm ledge and stopped at (-744.997, -335.001, 1380.386) for 135 s: wedged 0.003 u into a corner, so every
trace from its origin starts solid. Each replan found a route home and the walk's first corridor sample refused it
(`floor_startsolid`); the decline hold's only escape (step off to a cell) needs no cell under the bot. 272 replans.

`bot_moveStuckBail` (default on, `bot2_move.c` `DeclineHeld`): a hold on a corridor refusal named `floor_startsolid` or
`floor_allsolid` that has not moved 16 u in 2 s walks straight at the refused target for 400 ms when a hull trace to
it is clear (a trace that starts touching a wall but is not allsolid counts), and hops toward it from the third bail.
Prints `reason=stuck_bail`.

Test (`.codex-research/bail-20261001-a/`, `stuck.json`, build `bail-build-20261001-b`): the wedge pose itself is
refused by the corner start (solid), so the fall is replayed from the sample before landing (`fall_355275`, airborne,
carrying, 31 health). Bail off: lands at (-741.9, -335.1, 1380.4) and stays the whole 40 s. Bail on: two blocked walk
bails, then the hop; out of the wedge 7.5 s after landing. Runner floor (Kejim classes 150 and 50): 24 of 24 each,
every case time identical to the previous floor, no `stuck_bail` event.
