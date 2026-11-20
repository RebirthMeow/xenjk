# Red against blue on Kejim: the respawn4 asymmetry

Recorded 2026-09-24. The [level-2 confirmation](2026-09-24-level2-vs-level1.md)
showed Kejim's mirrored cases agree within 150-475 ms at level 2 but not at
level 1, where `red_out_L1_respawn4` takes 14,550 ms and its mirror
`blue_out_L1_respawn4` 17,050 ms, deterministically. Build
`gate-build-20260924-d`, production configuration, one world at a time.

## The map and the cases

- Kejim is point-symmetric about (1308, 2076): the flag stands mirror
  exactly; each blue respawn point is 8 units off the red one's mirror in x
  and y, at the same height.
- `red_out_X` and `blue_ret_X` are the same trip (same start, the blue flag
  stand), as are `red_ret_X` and `blue_out_X`. Over three repetitions each,
  the same trip gives the same time for either team (respawn4 14,550 and
  14,550; 17,050 and 17,050), so team, carrying and goal kind do not matter.
- Stand cases are unpinned in that campaign and dominated by start-class
  spread; `red_out_L1_stand` alone uses the library.

## Where blue loses 2.5 s

Blue mirrored into red's frame, aligned by release (`respawn4-mirror-20260924-a`,
`respawn4-route-20260924-a` with `bot_moveWalkTrace 1`, class 150 pinned):

1. **0-3.5 s identical**: same speeds, same walk.
2. **The routes differ by 8 units.** Along the ledge above the chasm red
   steers at (-128, 1152) and its route's next edge is learned strafe edge
   64164, which validates at 250 u/s: it takes off at 3,925 ms. Blue steers
   at (-104, 1144) and (-136, 1144), 8 units nearer the lip, with no strafe
   edge there.
3. **The walk brakes.** Blue's momentum chord (62 units at 251 u/s, walk
   keep) crosses the lip: `brake why=floor_missing` at 3,700 and 3,775 ms,
   251 → 13 u/s.
4. **A replan cascade.** From 3,875 to 5,400 ms blue installs a new route
   every 75-150 ms, each offering a learned strafe edge two or three cells
   on (83322, 83186, 83252, 83304, 83332, ...), each refused from the live
   state (the hop falls into the chasm's `trigger_hurt`) and avoided; 11405
   finally takes off at 5,525 ms, 1.6 s after red.
5. After that the trips are in different places and states; blue's later
   ground time is a consequence, not a mirror comparison.

**Why 8 units.** The graph's cells lie on a fixed 32-unit world grid, and the
map's centre of symmetry is not on it (2 x 1308 = 81.75 x 32), so a red cell's
mirror falls 8 or 24 units off the nearest blue cell. The planner's clearance
penalty (`Clearance`, 6 cells) keeps routes off the lip equally in cells, but
the walk steers at cell centres, and a centre's distance from the real lip
depends on the grid's phase. The seed graph's learned edges are also
different sets on the two sides (red's route edges 56662.., blue's 391..).

## The lip brake, tested and withdrawn

`bot_moveLipBrake` (test only, removed; patch retained as
`.codex-research/lipbrake-20260924-a/lipbrake.patch`) tried two changes to
the walk's `floor_missing` brake: 1, a drop beyond the 32-unit lip guard slows
the walk to 100 u/s instead of stopping it; 2, the momentum chord follows the
command's turn (8 ms steps of ground friction and acceleration). Build
`lipbrake-build-20260924-a`:

| | 0 (current) | 1 | 2 |
| --- | --- | --- | --- |
| Floor | 36/36 | 36/36 | 36/36 |
| Class-175 round trips (the lip guard's deaths) | pass | pass | pass |
| blue L1 respawn4, pinned 150 | 17,050 | 16,600 | 18,575 |
| red L1 respawn4, pinned 150 | 14,550 | 14,550 | 14,550 |
| blue L2 respawn4, pinned 150 | 10,100 | 10,100 | 11,275 |

Both are safe; neither closes the gap (1 recovers 450 ms, 2 is worse), because
the brake is the symptom of a route along the lip.

## Steering targets pushed off drops

`bot_moveDropPush <units>` (test, default 0; build `droppush-build-20260924-a`):
`PathTarget` moves each walked cell's steering point that far inward from
every refused side of a cell that saw a drop, liquid or hurt hazard, when the
opposite side is linked floor. Strafe chords are untouched.

**Method, faster than before.** A world costs a median 8.8 s of wall time, of
which the trip is 2.7 s at timescale 5 (the bench default). Pinned worlds are
deterministic, and eight cases pinned at class 150 gave the same times and
commits run one at a time and four at a time (`parallel-check-20260924-a`,
132 s against 36 s). So the campaign ran every floor case pinned at classes
50, 100 and 150 instead of repeating unpinned worlds, four at a time: 324
floor worlds and 6 round trips in about 15 minutes (`droppush-20260924-b`).
Start-class misses (a world whose ladder cannot reach the class) left some
cells empty; comparisons use pairs present in both arms.

| | Push 8 | Push 16 |
| --- | --- | --- |
| Deaths, non-pin failures | none | none |
| Class-175 round trips | 2/2 | 2/2 |
| Pairs faster / same / slower | 11 / 75 / 5 | **12 / 75 / 2** |
| Total change | -11.6 s | **-12.1 s** |

Push 16 changes only respawn4 pairs (and +275 ms on one stand case): eleven
faster by 350-2,225 ms, `red_out_L1_respawn4` at class 100 slower by 1,900. At
class 150 blue→red falls from 17,050 to 14,825 and red→blue from 14,550 to
12,850: both faster, the 2 s gap not closed - the two sides' learned edges on
the ledge still differ.

**Adopted (owner, 2026-09-24): `bot_moveDropPush` defaults to 16** (unset
reads as 16, 0 turns it off). Build `droppush-build-20260924-b` with the cvar
unset, all floor cases pinned at class 150: 31 of 31 matched pairs identical
to the push-16 arm, three start-class misses, no death
(`droppush-default-20260924-a`).

## Open

- A route that steers at edge-cell centres puts the bot 8-24 units from a
  drop depending on grid phase. Candidates: pull a steering target inward
  from a drop edge, or refine the grid near lips (the phase-2 open item).
- Blue's learned strafe edges on this ledge are refused from walking speed;
  red's mirror edge is not.
