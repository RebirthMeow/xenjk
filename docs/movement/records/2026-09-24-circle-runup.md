# Circle run-up: bot_moveCircleRunup

Recorded 2026-09-24. Owner question: does the first jump use a circle jump,
maxing ground acceleration before the takeoff? It did not.

## The gap

The run-up (`HopStep`, `bot2_move.c`) asked the controller for
`max(wishspeed, speed)` along the hop heading and tapped once the run-up
distance (0 or 24 units) was covered, so the ground frames settled on the
ground cap. The owner's 7,825 ms level-2 run
([record](2026-09-24-human-level2-runs.md)) held forward, then forward with a
strafe key, and left the stand at 225 ms at **471 u/s**, over Force Speed's
425 cap; the bot left about 125 ms after release at 369 u/s.

Ground physics ([PHYSICS.md](../PHYSICS.md)): friction takes 15 percent a
25 ms frame, then acceleration adds up to `10 * wishspeed * dt` (106 u/s at
Speed) along the wish direction, capped only on the wish-direction component.
With the wish direction 28-37 degrees off travel the cap stops binding; the
equilibrium at a 25 ms step is about 490 u/s.

## The change

`bot_moveCircleRunup` (test, default 0):

- The executor: a hop whose run-up is 64 units or longer asks the controller
  for 150 u/s above the speed it has. The controller already predicts ground
  friction and acceleration for every key and view, so it chooses the
  forward-strafe diagonal with the view off travel. Run-ups of 0 and 24 are
  unchanged.
- The straight search: from at or below ground speed (+25), twelve more
  candidates, the usual hops and aims with run-ups of 96 and 160 units.

The forecast replays the same executor through native Pmove, so every circle
candidate is judged on its measured takeoff.

## Results

Build `circle-build-20260924-a`, production settings. Committed circle
run-ups take off at 446-630 u/s at level 2 and 296-348 u/s at level 1
(`.codex-research/circle-20260924-a/`).

With the production library, the circle moves trips off tapes recorded
without it (entries `unseen` at level-2 classes 100 and 125 and level-1 class
100), so the fair comparison is library off in both arms
(`.codex-research/circle-20260924-b/`, one world a cell, pinned):

| Stand | L2 75 | L2 100 | L2 125 | L2 150 | L1 75 | L1 100 | L1 125 | L1 150 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Circle off | 9,900 | 11,850 | 9,725 | 11,550 | 11,650 | 15,925 | 14,475 | 18,225 |
| **Circle on** | **8,350** | **10,125** | 9,875 | **10,125** | 11,650 | 15,925 | **14,325** | **16,525** |

Level 2 -4,550 ms summed, level 1 -1,850; one cell slower by 150.

Floors with the circle on (`.codex-research/circle-20260924-c/`): Kejim,
ctf4, Dantooine v3 **36/36 each, zero deaths**. Level totals (unpinned, one
pass) against the production floors: Kejim 183.8 / 117.0 / 118.5 s against
192.2 / 111.1 / 118.0; ctf4 253.0 / 176.7 / 174.3 against 250.2 / 175.6 /
175.8; Dantooine 233.3 / 171.9 / 167.8 against 229.4 / 166.0 / 165.0.

## Adopted, 2026-09-24

Owner: default on (unset reads as 1, 0 turns it off); the library re-explore
is deferred (owner). Build `circle-build-20260924-b` with the cvar unset
reproduces the switch-on worlds: level-2 classes 75 and 100, library off,
8,350 and 10,125 ms (`.codex-research/circle-default-20260924-a/`).

**The production library is stale.** Its tapes were recorded without the
circle, and trips leave them where a circle hop is committed early. Circle on,
one world a cell:

| Stand | L2 75 | L2 100 | L2 125 | L2 150 | L1 75 | L1 100 | L1 125 | L1 150 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Library on | 9,900 | 10,350 | 10,100 | 11,500 | 11,650 | 16,100 | 14,325 | 13,950 |
| Library off | 8,350 | 10,125 | 9,875 | 10,125 | 11,650 | 15,925 | 14,325 | 16,525 |

Level 2 is faster at every class with the library off; level 1 is mixed
(class 150 needs its tape). Both gates were passed on the own-flag build with
the library on; this configuration has not been gate-measured.

## Before the re-explore, 2026-09-24

Owner direction: settle anything that would change decisions before the
libraries are explored again.

**Cost (row 5).** One bot, budget 4, production library, circle on
(`.codex-research/circle-cost-20260924-a/`, run alone): p99 **15.7 / 16.9 ms**
(12.9 / 11.8 on the own-flag gate build), 4 frames over 25 ms per 800 in each
world (3 and 2 before); worst 106 / 98 ms, 91 ms of it in `prepare`. Inside
the frame.

**Tuning.** Test switches `bot_moveCirclePush` (default 150) and
`bot_moveCircleScale` (on the 96 / 160 run-ups, default 1); defaults unchanged.
Library off, one world a cell (`.codex-research/circle-tune-20260924-a/`,
build `circle-build-20260924-c`):

| Variant | L2 75 | L2 100 | L2 125 | L2 150 | L1 75 | L1 100 | L1 125 | L1 150 | L2 sum | L1 sum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Default (150, x1) | 8,350 | 10,125 | 9,875 | 10,125 | 11,650 | 15,925 | 14,325 | 16,525 | | |
| Push 100 | 10,325 | 10,425 | 9,875 | - | 11,650 | 15,925 | 14,300 | 14,825 | +2,275 | -1,725 |
| Push 250 | 9,050 | 10,550 | 9,875 | - | 11,650 | 15,925 | 14,900 | 16,750 | +1,125 | +800 |
| Push 400 | 10,275 | 8,625 | 9,950 | 11,850 | 11,650 | 15,925 | 14,450 | 16,750 | +2,225 | +350 |
| Run-ups x0.7 | 10,300 | 10,275 | 10,550 | 11,375 | 11,650 | 15,075 | 14,525 | 17,300 | +4,025 | +125 |
| Run-ups x1.5 | 10,825 | 10,675 | 10,175 | 10,975 | 11,650 | 15,925 | 14,525 | 17,025 | +4,175 | +700 |

(`-`: three class misses before release.) Every variant is slower at level 2;
the defaults stay.

**Dantooine v3, pinned.** Levels 1 and 2, classes 50 and 150, circle off
against on, four worlds at once (same directory). 20 worlds missed their
class before release, none failed, no death. Matched pairs:

| | Pairs | Faster | Slower | Sum |
| --- | --- | --- | --- | --- |
| Level 1 | 19 | 8 | 11 | **-3,975 ms** (stands at class 150 -3,250 each) |
| Level 2 | 11 | 5 | 6 | **+5,550 ms** (`blue_out_L2_stand` class 150 +3,400) |

Mirrored cases give identical times in every pair. The circle is not a clear
gain on Dantooine: level 1 better, level 2 worse on these pairs.

**Dantooine `blue_out_L2_stand` class 150, +3,400 ms, traced.** The
circle's faster first hops bring the bot onto the upper walkway at
(2378, -1458, 216) at about 620 u/s. The only ways down from there are drop
edges 20, 24, 21, 17, 16, 28 and 29 (takeoff z 218, landing z -38, along a
wall at y about -1200 and -1590), onto a narrow floor above a water pit with a
`trigger_hurt` (brush probe: `water_1` and the hurt volume from z -320 to -128
at every point checked). Entered at Force Speed, about 425 u/s, the drops carry
past the floor into the pit and are refused (`unsupported=264`, trigger plus
liquid, 9-12 of 12 candidates); the planner reroutes to the next drop on the
same wall seven times, about 3.5 s, until edge 29 is admitted. The circle-off
trip crossed to the lower level elsewhere at 4.6-5.9 s. The defect is not the
circle: a drop edge is always taken at full speed, with no slow step-off.

Open, in order: (1) slow-approach candidates (done:
[drop-slow record](2026-09-24-drop-slow.md), -2,700 ms on this case) in the drop-edge search (brake
to about 150 u/s before the takeoff; test switch; measure on the Dantooine
pinned pairs, the eight Kejim stand cells and the floors); (2) only if gaps
remain, stop the planner rerouting to sibling drops into the same hazard;
(3) then the libraries re-explored under the circle (scope still to rule:
red stand only, or blue side, respawns, a Dantooine library) and both gates
re-taken.
