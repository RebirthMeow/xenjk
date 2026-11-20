# Flight mode 2: one strafe side a hop, 2026-09-23

Owner report on the class-125 stand video (build `default-lj-build-c172baa7`,
19,550 ms): the flight visibly jitters and the trip is slow. Worlds under
`.codex-research/video-kejim-20260923-a/`; metrics from its `airq.py`
(flight quality) and `loss.py` (speed gained and lost by phase).

## The jitter, measured

Flight mode 0 tracks a velocity exactly on the hop heading. In the air it
changes its key pair **19.9 times a second** (every other frame), holds a back
key on 15 percent of airborne frames and reverses the view 14.6 times a second.
Mode 1 (4 degree band) changes keys 2.4 times a second.

Mode 1's remaining loss is the side change. A held side's view sits
theta - 45 degrees off travel (about 16 at 500 u/s), so a change swings the view
about 32 degrees; at 360 deg/s that is 3.5 frames in which the new key is
within acos(250/v) of travel and adds nothing. With a change every ~11 frames
that is the 75-85 percent air efficiency measured.

## Mode 2

`bot_moveFlightMode 2` (now the default; 0 and 1 unchanged):

- a new flight (first airborne frame after a grounded one, or a launch inside
  the frame) holds the side that turns the travel toward the heading, so
  consecutive hops alternate sides by themselves;
- the side changes in flight only when the travel overshoots the heading by
  `bot_moveFlightBand` degrees (default 12);
- no fallback to the heading-tracking law and no speed gate;
- `bot_moveFlightTurn` (default 360) is the view turn rate in flight;
- a walk frame in the air faster than the wishspeed holds the side toward its
  target instead of back-pedalling to the walk's wishspeed.

`[MOMENTUM]` now ends with `yaw=` (the view yaw), for the smoothness metrics.

## Results

Stand case `red_out_L1_stand`, production settings (seed graph, library,
exit speed 500, state prefer, reroute), one world per class; every class is
deterministic to the millisecond across repetitions.

| Class | Mode 0 | Mode 1 | Mode 2, 360 | Mode 2, 720 |
| --- | --- | --- | --- | --- |
| 75 | 19,775 | 18,550 | **15,525** | 17,475 |
| 100 | 20,050 | 18,925 | 18,650 | 17,750 |
| 125 | 19,550 | 18,675 | 18,050 | 19,900 |
| 150 | 19,600 | 19,550 | 18,875 | 17,775 |
| mean | 19,744 | 18,925 | **17,775** | 18,225 |

| Flight quality | Mode 0 | Mode 1 | Mode 2, 360 |
| --- | --- | --- | --- |
| key changes per airborne second | 19.9-23.3 | 2.2-2.5 | 0.7-0.9 |
| back key, airborne frames | 1-15% | 0% | 0-1% |
| view reversals per airborne second | 14.6 | 2.6-2.9 | 1.3-1.6 |
| air efficiency | 75-84% | 83-86% | 90-93% |

720 deg/s is not faster and turns in 18 degree steps; 360 is kept.

Floor, build `flight2-build-20260923-a`, two pairs (m0 then m2, then m2 then
m0), all 144 worlds pass:

| Level | Pair 1 | Pair 2 |
| --- | --- | --- |
| 1 | -21.2 s, 9/12 faster | -11.5 s, 9/12 |
| 2 | -8.8 s, 9/12 | -4.2 s, 7/12 |
| 3 | -9.7 s, 10/12 | +1.2 s, 6/12 |

Build `flight2-build-20260923-b` (mode 2 by default) reproduces the four class
times exactly. `bot_moveHeadingSweep 1` on it: 16,600 / 18,175 / 18,825 /
16,250 (mean 17,462), two classes better and two worse; not adopted.

## Where the rest of the time goes

Speed is now lost on the ground, not in the air. Class 75 under mode 1 gained
+2,122 u/s in flight over the trip and lost -2,271 on the ground; 4 of 12
landings were touch-and-go. Under mode 2 the typical stop is: the last hop of
a sequence lands, a new sequence commits on that frame with a 24-unit run-up
(`r0=24`, `launch_speed=250`) and launches 4-5 frames later, shedding
250-400 u/s to friction. At the 655 u/s landing of class 125 at level 13,550
every tap-now candidate was refused: the two-hop ones for `trigger_hurt` (the
chasm floor, z 6-706: they fell), the one-hop ones `no_path`. A tap hop's
length is set by the speed and air strafing turns the travel only about 0.55
degrees a frame at that speed, so where the path bends inside a hop the only
admitted continuation sheds the speed first.

## Not established

- The decision tapes of the seed library were explored under mode 0. Under
  mode 2 their first entries still hit, later ones are refused; the 15.9 s gate
  median is not reproduced on this build and needs re-exploration.
- Levels 2 and 3 are only measured on the floor.

## Follow-up the same day: joins, centred paths, safety repairs

Owner direction: switches should stay centred (lean out, lean back), a
collision on an open lane is not a reason to refuse, and more jumps should be
chained. Builds `flight2-build-20260923-c` to `-k`; worlds under
`.codex-research/video-kejim-20260923-a/` (`dash2*`, `f2d` to `f2k`).

Changes, all in the defaults of build `k`:

- **Run-ups keep their speed** (`bot_moveRunupKeep`, default on). A run-up
  asked for the walk's wishspeed and braked with the back key: on dash2 a
  757 u/s landing fell to 485 in the two run-up frames.
- **Hop lines are pinned where the search aimed them** (`bot_moveFlightLine`,
  default on). A mode-2 flight steers for a point half a second ahead on its
  hop's line; hop 0's line runs through the plan origin, hop 1's through the
  plan's new `anchor1` (the straight aim point, the first edge's landing, or
  the measured first landing of a successor fan).
- **Paths keep clear of walls** (`bot_movePlanClearance`, default 6 cells).
  Each cell's link distance to the nearest boundary cell is computed at map
  preparation; both planners charge an arrival with less than 6 cells of
  clearance up to one more walk step. dash2's path ran 100 units from the right
  wall of a 1,024-unit lane and the weave hit it (a tap-now second hop landed at
  45 u/s).
- **The walk keeps its speed, with a longer momentum chord**
  (`bot_moveWalkKeep`, default on). Kept at 850 u/s with the old 96-unit chord,
  the walk launched off a rise on Kejim, hit the far wall of the chasm and fell
  (`blue_out_L2_respawn5`, with the heading fan). With it on, the chord is a
  quarter second of travel up to 320 units.
- **Landings no route leaves are refused** (`bot_moveRouteCheck`, default on):
  `Graph_Reaches` is a reverse search from the goal cell over links and live
  edges, cached per goal and edge count; an admitted landing outside it is
  `no_route`. It did not fire in the measured worlds.
- **An arrival needs 8 units of overlap** (`MOVE_ARRIVE_MARGIN`). Three
  respawn4 worlds at levels 2 and 3 stalled for two minutes on the ledge behind
  the blue flag: a two-hop judged `arrives` by a 1-unit projected contact flew
  over the flag in the live run. With the margin, 12 of 12 reruns pass.
- `bot_moveOffPathLog` prints the nearest path cell to an `off_path` landing
  (diagnostic, off).

Results, production settings:

| | dash2 | Kejim stand, four classes | Floor L1 / L2 / L3 (s) |
| --- | --- | --- | --- |
| mode 0 | 33,750 | 19,775 / 20,050 / 19,550 / 19,600 | 222.2 / 136.4 / 138.9; 213.9 / 138.4 / 133.1 |
| build `k` defaults | **28,625**, top 1,630 u/s, 17/18 landings touch-and-go | 15,450 / 16,825 / 16,900 / 16,050 | 190.7 / 125.0 / 128.6; 194.9 / 126.0 / 129.8 (build `j`, same settings), both 36/36 |

Not adopted: `bot_moveHeadingSweep` (a fall death and a stall on floors, 15.8 s
stand mean), `bot_moveTapOnward` (16.1 s, no floor gain), a 720 deg/s flight
turn.

Why Kejim joins still run up: at 26 of 39 sequence joins over the four stand
worlds every tap-now candidate touched the chasm's `trigger_hurt` (109 of 142
during the hop itself) - the walkway turns inside one hop length. At 4 joins a
tap-now one-hop was admitted but lost to a two-hop run-up, because the judge
continues a one-hop with a walk.
