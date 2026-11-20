# Golan refit, pedestal mines, defender view jitter (2026-10-01, evening)

Follows [knock replays](2026-10-01-knock-replays.md). Campaign `.codex-research/refit-20261001-a/` (`run.sh`, `run2.sh`,
`run.log`, `run2.log`); every arm 16 laps worlds, 600 game s at x5, falls3 run17 configuration, `g_fixGolanDamage 1`.
Arms ks0/m0/m2/m3 on `bail-build-20261001-b` (sources identical to HEAD 7311d846); arm fit on `refit-build-20261001-a`
(HEAD plus the refit below). 80 worlds, all exit 0.

## Flechette refit (uncommitted)

Arm ks0 (`bot_defenderKnockSafe 0`) measures single-blast own flechette alt knocks at every range (`kspredict.py`,
updated to the code's table; per-band read inline in the session):

| burst distance | knocks | p50 | p90 | max | old row |
|---|---|---|---|---|---|
| < 80 u | 21 | 275 | 460 | 535 | 1,050 |
| 80-130 u | 42 | 185 | 345 | 690 | 675 |
| 130 u + | 12 | 140 | 195 | 200 | 425 |

`SelfKnock`'s flechette alt rows are now 460 / 345 / 200 (the 90th percentile, as before). Same arm, other weapons:
rocket < 80 u p90 620 against a 500 row (16 knocks), the rocket's knock tail; repeater alt rows fit.

| arm (16 worlds) | runner kills | captures | base D falls | chase D falls |
|---|---|---|---|---|
| m0 (control, old rows) | 34.3 (sd 3.1) | 3.81 (sd 1.74) | 0.50 | 0.88 |
| fit (refit rows) | 37.2 (sd 3.4) | 2.44 (sd 1.12) | 0.38 | 0.75 |
| ks0 (no knock safety) | 36.0 (sd 4.5) | 2.38 (sd 1.27) | 1.00 | 0.31 |

The refit keeps the falls of the control (inside the spread, both under the old 0.5/0.62 figures) and buys the close
shots that KnockSafe off buys: kills +2.9 a world and captures -1.4 a world, each about 2.5 standard errors.

## Pedestal mines (cvar only, not yet default)

`mines_score.py` (same folder). Aim points from the mine study, snapped to graph cells: m2 floor west (80, -1105) and
east (215, -1185), m3 adds the top (160, -1170). Only the placer's origin is logged, not the mine's resting position: the
placer stands 29-41 u from the aimed spot on average.

| arm | mines laid | runner mine kills | of them near the stand | base D own-mine deaths | runner kills | captures |
|---|---|---|---|---|---|---|
| m0 current spots (3) | 28.3 | 3.6 (sd 1.5) | 2.1 | 18 | 34.3 | 3.81 |
| m2 floor W+E (2) | 25.6 | 6.3 (sd 2.2) | 6.3 | 2 | 34.6 | 3.75 |
| m3 floor W+E + top (3) | 26.9 | 6.3 (sd 2.3) | 6.1 | 6 | 36.3 | 3.06 |

Pedestal mines nearly double the runner's mine deaths and nearly end base D's deaths to its own mines (18 to 2). The
top mine adds nothing measurable but its own-mine deaths; captures fall in m3 only by about 1.5 standard errors. Mine
kills are almost all before the pickup (carrying 0-1 an arm): they deny the take, they do not stop carriers.
Recommendation: m2.

## Defender view jitter (diagnosed, not fixed)

`.codex-research/aimjitter-20261001-a/report.md`. Not the 200 u aim switch. The main cause is `MoveControl`
(`bot2_movecontrol.c`) choosing its (key, view) from scratch each frame: with a small intent velocity the +9 and -9 deg
turn-limited views tie and alternate every 25 ms frame; seen with the runner dead, when the aim code does not run.
Secondary at melee range: `SetView`'s 89 deg backpedal clamp toggling. Fix (hysteresis toward the last winner) touches
the shared controller; owner's call.

## Jitter follow-up: what writes the view (2026-10-01, later)

Campaign `.codex-research/viewhold-20261001-a/` (scripts `run*.sh`; `jitter.py` scores flips from each bot's own demo,
`viewlog.py` reads the new `bot_defenderViewLog` lines). A walking flip is an 8+ degree turn reversing the previous one
within 100 ms, with the bot grounded and under 320 u/s for 200 ms either side (strafe-hop weaves excluded).

- **The writers.** With `bot_defenderViewLog 1`: in about 3 of 4 walking flips the movement alone wrote the view
  (no SetView), on the ground, the same writer on both frames. Chase D's largest other group is Shoot's view clamped
  at the 89 degree backpedal edge. Human aim error off (`bot_aimErrorScale 0`, 4 worlds) took base D 72 to 54 walking
  flips a walking minute and left chase D unchanged.
- **Hold last view within 30 degrees** (16+16 worlds): base D 72 to 53, chase D 82 to 101. Dropped.
- **View along the velocity, keys turned** (16+16): all flips down by a third, walking flips 61 to 56 and 80 to 76. The
  frames it must skip are the controller braking with the back keys (turned forwards they would run at full speed, not
  the 0.75 the rollout measured), so the forward/brake alternation stays. Dropped.
- **Clamp hold, kept (owner's option):** `bot_defenderClampHold 1` (default): an aim out of a moving defender's reach
  keeps the movement's view instead of the 89 degree edge, and the reach narrows to 75 degrees until a frame finds
  the aim within reach again. Build `clamp-build-20261001-a`, 16 worlds an arm:

| arm | walking flips/min base D | chase D | clamp-group flips (chase) | kills | captures | falls base / chase |
|---|---|---|---|---|---|---|
| clamp hold off | 69.6 | 91.0 | 5,011 | 36.0 | 2.75 | 0.38 / 0.81 |
| clamp hold on | 61.3 | 72.2 | 3,518 | 34.6 | 3.50 | 0.50 / 0.62 |

Captures trend up (about 1.5 standard errors), kills and falls within the spread. What is left is the movement
controller's own forward/brake alternation; the owner declined the controller-level fix for now (it needs the runner
floor).

## Owner 1v1s and the chase D changes (2026-10-01, evening)

**Sessions.** `play-vs-runner-20261001T162824Z`: the owner spectated the runner against chase D alone
(`tools/play_vs_runner.py --defenders --roles chase`). `play-vs-runner-20261001T163156Z`: the owner played offense
against chase D (`--offense`: the runner is added to set the world up, then kicked; with no runner bot,
`DefenderRunner` takes a human enemy). 6.8 minutes: the owner took the flag 9 times and captured 7; chase D killed
the owner once and died 19 times (13 to the owner, 4 resets, 2 to its own splash). Chase D took the owner's flag 8
times (its stand goal walks onto it); its lethal throws hit 10 of 11 and killed none (the human air-controls out);
it was out of Force for a push 6 times while the carrier was within 200-300 u.

**Owner directions and what landed (uncommitted until this commit; build `chase-build-20261001-d`).**
- Force for catching up, not pre-emptive kills: `bot_defender 2` (only escape-free lethal throws; existing mode).
  Laps: 17 lethal throws against 463, runner falls 31-41 against 48. Not yet the tools' default (`defense_laps.py`
  `DEFENDERS` still sets `bot_defender=1`).
- `bot_defenderResetSpawnMs` (default 2000, was a fixed 5000).
- `bot_chaserRearmDetour` (default 600): in a chase, only an item at most that far off the way.
- `bot_chaserIntercept` (default 1): with `bot_chaserGoal 1`, the first cell of the carrier's route home that chase D
  reaches first (`InterceptGoal`), the stand otherwise.
- Self-kill when spent: `bot_defenderSpentReset`, default **0**. Human study `.codex-research/sk-study-20261001-a/`
  (862 self-kills by 2v2 defenders, the owner's 24 in his defense sessions): they follow a change of the own flag,
  median 1.3 s after it comes home (health 100, 914 u from the stand) and 3.4 s after it is taken (Force 16, the
  carrier about 450 u away); low health is 13%. The rule built from it (returned / refill / health / ammo) made the
  defenders die twice as often and doubled captures in laps worlds; a self-kill costs the bot its weapons and a
  walk back (errands are walked).

| arm (16 laps worlds, `.codex-research/chase-20261001-a`) | captures | runner kills | deaths base / chase D | chase D falls |
|---|---|---|---|---|
| old behaviour (cvars back) | 3.06 | 37.1 | 10.2 / 10.0 | 0.62 |
| changes, spent reset off | 3.81 | 36.9 | 10.5 / 9.9 | 0.81 |
| changes + human-derived spent reset | 6.94 | 30.3 | 22.4 / 24.6 | 1.81 |

**Next (owner, 2026-10-01): a defender tape library.** Spawn, weapon, post is a repeatable trip (spawn at rest with
full Force, fixed items and posts). Errands are walked today (`bot_defenderWalkErrands`, set 2026-09-29 for falls),
and guests may not use the runner's decision tapes (`bot_moveTapeLibrary`). Plan: guest tape support (own index,
parity, fallback to the walk), offline exploration of spawn-to-weapon and weapon-to-post legs, item-taken and
off-library fallbacks, measured as respawn-to-armed-and-posted time and falls against the walking control. Then
re-test self-kills, whose cost is mostly that walk.
