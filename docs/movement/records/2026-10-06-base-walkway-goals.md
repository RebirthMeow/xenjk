# Base D's walkway falls: the saber walk, not the goal (2026-10-06)

Handoff item 1. Scripts in `.codex-research/walkway-20261006-a/` (`goals.py`, `runner_ground.py`, `melee_before.py`,
`paired_falls.py`); worlds in `footprint-20261006-a/` and `meleeedge-20261006-a/`. Both arms of each run are 4v4
laps worlds with the `edgeroute-20261005-a` options, 16 paired seeds, four at a time.

## The suspect: chase goals off the floor

The 2026-10-05 record left base D's remaining Kejim falls (about 1.7 a world each) behind short walk-only routes
toward a target past the lip. On the retained `edgeroute-20261005-a/on` worlds (`goals.py`): 60 base D falls (both
teams), 44 during a carry, nearly all chasing (`chasing=1`), and the goal (`CARRY_SAMPLE goal=`) was off base D's
floor in 41 of them. Base D's chase goal is the raw sighting (`VectorCopy(seen->origin, chaseGoal)`), so a runner in
flight over the walkway's inside corner or below the lip put the goal there; chase D had the same tail before
`bot_chaserGoal`. The runner was airborne in 32 of the 44 (`runner_ground.py`), close to a hopping runner's base rate,
so this could not separate cause from coincidence.

**Footprint goal** (`bot_defenderFootprintGoal`, ea79212d, default 0, build `footprint-build-20261006-a`): a chasing
defender retargets only on a grounded sighting, to that graph cell. Kejim, 16 pairs (`footprint-20261006-a`, the
Dantooine half stopped after Kejim; its four stopped worlds' staging moved to `stopped-staging/`):

| per world | off | on | paired |
| --- | --- | --- | --- |
| base D falls (both teams) | 3.56 | 3.50 | -0.06 +- 0.82 |
| chase D falls | 5.44 | 5.44 | +0.00 +- 1.06 |
| team captures | 4.10 | 3.80 | -0.31 +- 0.35 |
| pickups | 29.58 | 28.48 | -1.10 +- 0.67 |

No change. The goals were now floor cells, yet base D still ran over the lip: in `kej-on/kej-8` (477,550) it stood
13 u past its goal pressing full forward north, toward the carrier.

## The mechanism: the saber walk overrides the movement

`CARRY_SAMPLE melee=on`. An unarmed defender within `bot_defenderMeleeRange` (250) of a seen target walks straight
at it with the saber (`Melee`, bot2_defender.c), replacing the movement's command and with it the guest momentum
brake. Its floor test is a full-hull trace 48 u ahead along the heading: the hull keeps finding floor until its
centre is 15 u past the lip, the keys are then released, and from 427 u/s friction alone needs about 63 u. The bot
coasts over (in kej-8 it shed 60% of its speed in 300 ms, sliding off at 132 u/s).

The last grounded melee state before each carry-time base D fall (`melee_before.py`): `on` in 35 of 44
(`edgeroute-20261005-a/on`) and 59 of 81 over both footprint arms.

## The fix: the saber walk takes the guest brake test

`bot_defenderMeleeEdge` (8137d602, default 0, build `meleeedge-build-20261006-a`): `Move_GuestEdgeAhead`
(bot2_move.c) exposes the walk's guest brake test - the corridor and the narrow-hull edge probe over the stopping
distance (`GuestStopDistance`) - along the velocity (2) and along the heading (1, at least 48 u). Melee releases the
keys on 1 and pushes against the velocity on 2; a `MELEE_EDGE` line marks each episode. It applies to every
defender role's saber walk (base, chase, camper); the runner has no such walk.

Kejim, 16 pairs (`meleeedge-20261006-a`):

| per world | off | on | paired |
| --- | --- | --- | --- |
| base D falls (both teams) | 3.38 | 1.31 | **-2.06 +- 0.69** |
| chase D falls | 5.56 | 3.44 | -2.12 +- 0.86 |
| camper falls | 9.31 | 8.69 | -0.62 +- 0.90 |
| base D kills of runners | 6.62 | 8.69 | +2.06 +- 0.80 |
| team captures | 4.10 | 3.64 | -0.46 +- 0.36 |
| runner captures | 3.02 | 2.35 | -0.68 +- 0.33 |
| runners' weapon deaths | 27.52 | 30.12 | +2.60 +- 1.34 |
| pickups | 29.15 | 29.19 | +0.04 +- 0.64 |

Base D carry-time falls with melee on as the last grounded state: 26 off, 1 on. Defenders that no longer run off
the walkway stay to fight: more runner kills, fewer runner captures; pickups unchanged.

Dantooine v3, 16 pairs (same run):

| per world | off | on | paired |
| --- | --- | --- | --- |
| base D falls (both teams) | 1.25 | 0.56 | -0.69 +- 0.25 |
| chase D falls | 3.44 | 2.81 | -0.62 +- 0.69 |
| camper falls | 4.38 | 3.50 | -0.88 +- 0.71 |
| team captures | 4.59 | 4.41 | -0.18 +- 0.27 |
| runner captures | 3.63 | 3.14 | -0.49 +- 0.30 |
| camper captures | 0.95 | 1.26 | +0.31 +- 0.23 |
| defenders' enemy-flag pickups | 1.42 | 0.86 | -0.55 +- 0.31 |
| pickups | 27.63 | 26.53 | -1.10 +- 0.87 |

Frame cost (Kejim, the whole-world `FRAMECOST client=all` line, four worlds at a time): p99 30.2 -> 30.5 ms
(medians 29.6 / 30.1), frames over 25 ms 441 -> 439 a world; unchanged.

**Default on** (a321a3ef, build `meleedefault-build-20261006-a`); a default-settings Kejim world on it
(`meleedefault-20261006-a/kej-1`) logs 37 `MELEE_EDGE` episodes. `bot_defenderFootprintGoal` stays at its off default.

**What is left.** On the Kejim on arm, base D falls 1.31 a world for both teams (21 in 16 worlds): two at the ramp
or walkway corner, the rest spread over the map, ten of them outside a carry; the last grounded melee state is `on`
for one. The walkway item is closed; the rest has no single mechanism.
