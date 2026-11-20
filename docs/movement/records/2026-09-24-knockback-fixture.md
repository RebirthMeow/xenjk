# Knockback recovery, first measurement, 2026-09-24

Question from the owner: how does the bot handle being hit, a rocket's push?
Nothing had measured it; every rewrite world before this one was empty. The
plan's "Intermediate entries and contention" row names measured perturbation
support as later work.

Owner scoping, given while the campaign ran: a push that sends the bot straight
into a pit is not necessarily a case to solve. A push that puts it off course,
after which it lands and then falls into a pit because it is off course, is.

## The fixture

`bot_moveKick` (`codemp/game/bot2_kick.c`, fixture intervention): declared
kicks applied through the game's own `G_ApplyKnockback`, the call `G_Damage`
makes, so the added velocity, the knockback timer, `g_knockback` and the mass
are native. No damage is dealt, so health, pain and AI state are untouched.
It is armed by `bot_corner_start` and fires on the first frame of the
movement integration, before anything plans from the state. Triggers: `time`,
`walk` (grounded, no sequence in progress), `hop` (airborne in a committed
hop), `climb` (airborne in a committed climb), with a delay after the release
and a time into the phase. `[KICK] ARMED` and `[KICK] APPLIED` lines go to
`server.log`. ARMED is printed on the first frame rather than by the rcon arm,
because rcon prints never reach the log. `movement_bench.py` records a `kicks`
audit per case. `tools/movement_kick_report.py` pairs a kicked campaign with
its control and classifies each death:

- `direct`: the first ground after the kick is the height the bot died at.
- `landed_then_fell`: it first stood somewhere else and fell from there.

Smoke check on ctf4 `red_out_L1_stand`, knockback 100 at 90 degrees and 20 of
pitch: +376 u/s sideways and +256 up, exactly `G_ApplyKnockback`'s 0.8 and 1.5
factors at g_knockback 1000 and mass 200.

## Campaign

Build `strafe-build-df9b250735b9421cb942c446abb93413` (commit `c02210ee` plus
the fixture). Settings as the production floors (`--edges --parity 0
--momentum-trace`, exit speed 500, state preference, reroute). There is one
control per map on the same build. Worlds are under
`.codex-research/kick-20260924-a/`, with each arm's pairing in
`<arm>.report.json`.

The three kicks are all knockback 100, a rocket's direct hit (400 u/s along
the push):

- `hop`: 90 degrees left of travel, 20 up, 100 ms into a committed hop's
  flight.
- `walk`: against travel, 20 up, on the first plain walking frame at least
  2 s after release.
- `climb`: against travel (off the wall), 20 up, 150 ms into a committed
  climb's flight.

| Arm | Fired | Passed | Direct deaths | Deaths after landing | Stalls | Median / max extra time |
| --- | --- | --- | --- | --- | --- | --- |
| ctf4 hop | 36 | 27 | 3 | 0 | 6 | +125 / +5,100 ms |
| ctf4 walk | 36 | 36 | 0 | 0 | 0 | +375 / +1,975 ms |
| ctf4 climb | 35 | 25 | 9 | 1 | 0 | +75 / +3,100 ms |
| Dantooine hop | 36 | 20 | 15 | 1 | 0 | +1,512 / +4,875 ms |
| Dantooine walk | 36 | 33 | 3 | 0 | 0 | +200 / +3,675 ms |
| Dantooine climb | 35 | 35 | 0 | 0 | 0 | +325 / +9,650 ms |
| Kejim hop | 36 | 26 | 10 | 0 | 0 | +625 / +6,950 ms |
| Kejim walk | 35 | 31 | 0 | 4 | 0 | +1,300 / +3,375 ms |

Corrected 2026-09-24 after review: the Kejim walk row read 36 / 32 / +1,238.
`red_ret_L1_respawn5` passed at 12,700 ms and its only kick fired at 12,750,
after the verdict, and the audit counted it as a recovery. `kick_audit` now
takes the evaluation's endpoint and lists such a kick as `late`, unfired; a
re-audit of every retained kick report changes this row alone.

Controls: 36 of 36 on each map. Extra time is paired against the same case's
control, over cases that passed both, one world each, so it is a size, not a
resolved effect. Unfired: one climb case per map committed no climb. It is
listed and never counted as a recovery.

## What failed, by the owner's scope

**Out of scope, 40 direct deaths.** The push carried the bot, airborne
throughout, onto the hazard floor 2.0 to 3.3 s later. Examples: ctf4's void at
z=-2024, Dantooine's chasm at z=-296, Kejim's drop from the z=1640 walkway to
z=32. The ten ctf4 climb deaths were kicked on two climbs: six near
(1150, -1460), including the one in-scope case below, and four near
(520, -830). Both go up walls over the void, so a push off the wall is mostly a
straight drop.

**In scope, 6 deaths after landing, one pattern.** The bot lands off course on
a ledge or near an edge, still carrying momentum. The walk brakes for
`floor_missing`, the searches refuse or find `no_path`, and the momentum takes
it over the edge it has already detected.

- ctf4 climb `blue_out_L1_respawn5`: knocked off the wall, it landed on a ledge
  at z=-312 at 125 u/s. `climb_short` fired and charged the edge. The replan
  routed through the same climb edge, and the walk braked. All 66 climb
  timings were refused, and the bot drifted off the ledge (z=-578 by 800 ms
  later, pit at 2.1 s).
- Dantooine hop `blue_ret_L2_stand`: it landed off the graph at z=280 going
  533 u/s (`offmesh`, then `no_path`, then hold) and slid down into the chasm.
- Kejim walk, 4 of 4 deaths, all `*_respawn5` starts: the push cut 800 u/s to
  424 and the bot landed on the walkway at z=1368 at 631 u/s. The walk braked
  (`floor_missing`) and the straight search refused 12 of 12. It was still at
  470 u/s on the next grounded frame and airborne off the edge about 100 ms
  later.

**In scope, 6 stalls, ctf4 hop only.**

- 5 on a mover: knocked onto a lift or door floor (`floor_nonworld`, z -85 to
  -191), the bot loops `no_path` until the case bound. The planner has no cell
  under a non-world floor.
- 1 onward hold (`red_out_L2_respawn5`): the kicked hop landed where the
  onward stage never covers its 128 units. `bot_moveOnwardBound` is off by
  default, so the plan never releases (`held=41` repeated to the bound). This
  is the hop form of the climb finding repaired in `c02210ee`.

**Recovery cost.** Every walk arm recovered with a median under 1.3 s. The
largest single cost is Dantooine climb `red_out_L3_respawn4` at +9,650 ms. It
was not investigated.

## Open

In the order the scope suggests:

1. Securing footing on an off-course landing: the six deaths after landing.
2. Planning from a mover floor: five stalls.
3. Releasing a hop plan whose landing the onward walk will not cover: one stall.

No repair is made here. This is one world per case per arm, with a single kick
size and direction. It measures that each family exists, not its rate.

Item 1 was repaired the same day: see [knock recovery](2026-09-24-knock-recovery.md).
With recovery on, item 3 did not recur. Item 2 remains.
