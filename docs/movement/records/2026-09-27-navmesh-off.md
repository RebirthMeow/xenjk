# Navmesh off: what movement loses without it (2026-09-27)

Owner question: can the navmesh be ruled out early, and did the gates use it?

## The gates used it

Every gate world loads `maps/<map>.navmesh` from the installed GameData
(`navmesh_crash.log`: `SUCCESS: Loaded Tiled Mesh`). The mesh reaches movement
three ways:

- **Graph edges from off-mesh connections** (`Hints` in `bot2_graph.c`). It is
  the only source of pad, drop and jump edges; nothing else creates
  `GRAPH_EDGE_PAD`. Bound hints per map: Kejim 10 pads; Dantooine v3 6 pads,
  36 drops, 12 jumps; ctf4 16 drops, 16 jumps; Metalorn v2 29 pads, 189 drops,
  26 jumps.
- The waypoint steering fallback off the planned path (`NavMesh_GetNextWaypointEx`).
- The objective path in `ai_bot2_objective.c` (`NavMesh_GetPath`).

Taloraan and Carratos have no navmesh and passed 216/216 on the 2026-09-25
floors ([record](2026-09-25-new-maps.md)).

## Method

`bot_navmeshOff 1` (new, read at map load in `NavMesh_InitForMap`, unset = the
old behaviour) skips loading the mesh. Build `navoff-build-20260927-a`. Four
maps that have both fixtures and a mesh, class 150, `--skip-twins`, parity
256, `--edges`, no `--graph` (each world builds its graph fresh, so no cached
mesh edges), mesh and no-mesh arms side by side, two worlds each.
`.codex-research/navoff-20260927-a/`. The no-mesh worlds show
`pad=0 drop=0 jump=0 hints_bound=0`.

## Result

| Map | Mesh | No mesh | Passing pairs: same / faster / slower, sum |
| --- | --- | --- | --- |
| Kejim | 24/24 | 24/24 | 24 / 0 / 0, 0 ms |
| ctf4 | 18/18 | 18/18 | 10 / 0 / 8, +22.1 s (stands +2.6 to +4.9 s) |
| Dantooine v3 | 18/18 | **12/18** | 10 / 2 / 0, -3.0 s |
| Metalorn v2 | 18/18 | **12/18** | 6 / 0 / 6, +132.4 s (respawn4 +17 to +29 s) |

- **The six failures on each of Dantooine and Metalorn are the six stand
  cases**, all `stall` with zero commits: the stand region is an island of
  3,844 cells (Dantooine) with `no_path` to the goal. The way off it is a drop
  or jump that only the mesh supplied.
- Metalorn respawn4 finds a walking detour 2-3x as long; ctf4's stands lose
  a shortcut; Kejim does not use its pads in these cases.

## Wall climbs are not from the mesh

Climb edges are the graph's own: Kejim 14, Dantooine v3 116, ctf4 66,
Metalorn v2 324 in both arms. The mesh's wallrun links (`OFFMESH_AREA_WALLRUN`)
were never imported and are avoided by the mesh steering. Every ctf4 case
climbs once (rise 384) and rides no lift. Owner: the sideways wall run is
never needed; the climb is, and beats waiting on ctf4's lift.

## Conclusion

The navmesh cannot be removed today, but what it supplies is narrow: pad,
drop and jump connectivity. Ditching it needs the graph to find those itself:
pads from `trigger_push` entities and their targets, drops from graph ledges
onto floor cells below, basic jumps by rollout. The acceptance is this
comparison with the mesh off matching the mesh arm on these four maps, then
the eight new maps built without a mesh.

## Native pads, drops and jumps (same day)

`bot_moveNativeEdges 1` (test, off by default; `bot2_graph.c` `Pads`,
`DropsAndJumps`) makes the three edge kinds from the world itself. Drops
leave a ledge cell for the floor below the next column (up to 400 down);
jumps cross a refused direction to the nearest cell 64-192 out within a
jump's rise and fall with a clear line; pads trace `AimAtTarget`'s arc with
the hull under 16 air-strafe headings and keep a landing two cells inside its
floor that the steered arc also reaches. The jumppad state flies a native pad
by the same arrival law (`Graph_PadSteer`: accelerate toward the velocity
that lands on the target, coast once held); a pad flown by holding forward
overshot at Force Speed, and one aimed at a ledge edge fell into Dantooine's
pit (build `b`, six deaths). Same four maps, class 150, `--skip-twins`,
parity 256, mesh off. Mesh arm re-run on every build: identical to the
retained one, 78/78 pairs.

| Map | Native edges (no pads) vs mesh | With pads (build `i`) vs mesh |
| --- | --- | --- |
| Kejim | 24/24, -13.9 s (6 faster, 0 slower) | same, pads unused |
| ctf4 | 18/18, -16.0 s (6 / 2) | same, no pads on the map |
| Dantooine v3 | 18/18, -5.0 s (6 / 2) | 18/18, **-14.4 s** (16 / 2) |
| Metalorn v2 | 18/18, +2.6 s (4 / 8) | 18/18, **+13.5 s** (2 / 16) |

No falling death in any native arm after build `b`. Counts: drops 0 / 278 /
228 / 746, jumps 64 / 104 / 164 / 166 (Kejim, Dantooine, ctf4, Metalorn),
against the mesh's bound 0 / 36 / 16 / 189 and 0 / 12 / 16 / 26.

**Pads are priced right and still chosen wrong.** A Metalorn stand pad
landed on target at 553 u/s after 2.7 s against 2.4 s priced; the loss is
the route it replaced. Without a learned library the planner prices walking
at the run or Force Speed wishspeed, while the runner strafes at 550-700
u/s, so any accurately priced shortcut is over-valued. A flat pad margin
cannot fix both maps (`.codex-research/native-padsweep-20260927-i`):
margins 250-750 ms keep Dantooine's -14.4 s and cost Metalorn +13.5 s;
1,250 ms and up lose Dantooine's gain and still cost Metalorn +4.5 to +7.0 s.
`bot_movePadMarginMs` (test, 250) stays for that measurement.

**Where this leaves the navmesh:** without it and with native drops and
jumps, every case on the four maps completes and three of four maps are
faster (-32.2 s net). The open items are the walk's price against strafe
speed (which decides pads) and Metalorn's +2.6 s from drops and jumps.
Evidence: `.codex-research/native-20260927-{a,c,f,h,i}`,
`native-padmargin-20260927-g`, `native-padsweep-20260927-i`.

## Walk pricing: hop law refuted, the price is double the time (same day)

**Hop pricing refuted.** A test build priced a state-planner link that goes
on in the state's heading, with two cells of clearance, by the ideal air
strafe gain (v^2 grows by a(2w-a) a frame) at 50, 75 and 100 percent, up to
900 u/s, taking the faster of hop and walk (build `hop-build-20260927-a`,
`.codex-research/hopprice-20260927-a`, mesh off, native edges and pads).
78/78 in every arm, and worse everywhere but Metalorn: Kejim from -13.9 s to
+16.9 to +22.6 s against the mesh (18 of 24 slower), ctf4 from -16.0 to
-1.1 to -7.8, Dantooine from -14.4 to -7.7 to -9.0, Metalorn from +13.5 to
+5.2 to +11.0 with pads still taken in 18 of 18. Kejim's level-1 stand route
went from 200 cells to 341 across open floor, priced 24.3 s, run in 24.6 s:
the runner averages about 300 u/s over that leg, so open floor is not where
speed keeps building. The change was reverted.

**The walk's price is double its time.** The planner's first route on
Kejim's level-1 stand is priced 42.1 s and run in 21.1 s; Metalorn's mesh
routes 34.8-36.1 s priced against 14.2-18.6 s run. The clearance penalty
(`bot_movePlanClearance`, default 6: up to one extra walk step for each cell
near a wall) is part of it and falls on corridors, never on a pad's landing,
which is two cells inside its floor. With it off
(`.codex-research/padclear-20260927-a`, one world a case): Dantooine without
pads -17.7 s against the mesh (from -5.0), with pads -14.8; Metalorn without
pads +2.5 (unchanged), with pads +5.5 (from +13.5). Pads still do not pay on
Metalorn. Two worlds were incomplete for the harness (a deferred pin window
missed, a runner quit), not for movement.

**What follows.** Pads stay off the default. The planner's prices are the
next question, and they matter beyond pads: a CTF brain will ask routes how
long they take. Calibrate the walk's price against measured traversal times
from retained worlds, and keep the clearance preference separate from time,
before pads, native-edge defaults and the mesh are decided.

## Native edges by default, pads at the mesh's price (same day, owner)

**Re-scoped (owner).** Native drops and jumps already met this record's
acceptance: every case completes on the four maps and the net is faster.
The pad loss was a mismatch the native pad introduced, an accurate price
beside walks priced at double, so it is no reason to rebuild the cost model.
Pads are wanted and priced as the mesh priced them; link-price calibration
waits for milestone A, when a CTF brain first asks routes for times.

**Changes** (build `native-build-20260927-k`):

- `bot_moveNativeEdges` defaults on (unset = 1). With it on, the graph
  imports no off-mesh connections even when a mesh is loaded, so its edges do
  not depend on one; 0 restores the mesh's hints.
- Pads, native or hinted, cost `CandidateCostMs`: the horizontal length at
  250 u/s plus 2,000 ms, the formula `Hints` always used.
  `bot_movePadMarginMs` is removed.
- Loaded graph caches are unchanged (they keep whatever edges they were
  built with), so the Kejim seed and its tape library still bind.
- Dropped-flag reachability (`ai_bot2_objective.c`) asks the movement graph
  (the planner's own root and objective cells) instead of the mesh; without
  a mesh every dropped flag used to read unreachable. It falls back to the
  mesh only when no graph is prepared. Not exercised natively: no fixture
  drops a flag.
- An unplanned pad (`PadThrownLanding`, `bot2_move.c`). With the mesh loaded,
  a pad touched by a walk that did not plan it was flown to the mesh's
  landing; without it the jumppad state aimed at the objective. Now it flies
  to the landing of a native pad edge leaving that trigger (one that still
  reaches the objective, then the nearest to it), held for that flight,
  with an `edge=pad reason=thrown` event.
- `bot_navmeshOff` still defaults to loading a mesh, because older Bot2
  states read one; `tools/play_vs_runner.py` sets it to 1 in the runner's
  production settings.

**Result.** Class 150, `--skip-twins`, parity 256, `--linger 4`, mesh not
loaded (`.codex-research/native-default-20260927-{j,k}`):

| Map | Build `j` | Build `k` | `k` against the mesh arm |
| --- | --- | --- | --- |
| Kejim | 24/24, 0 died | 24/24, 0 died | -13.9 s (6 faster, 18 same) |
| ctf4 | 18/18, 0 died | 18/18, 0 died | -16.0 s (6 / 2 slower) |
| Dantooine v3 | 18/18, **2 died after the objective** | 18/18, 0 died | -4.1 s (6 / 2) |
| Metalorn v2 | 18/18, 0 died | 18/18, 0 died | +2.6 s (4 / 8) |

- **Pads at this price are not planned in any case**, as with the mesh:
  Dantooine and Metalorn on build `j` are identical, 36 of 36, to the
  no-pads arm (`native-20260927-c`), and the mesh arm enters a pad in 3
  Dantooine worlds and no Metalorn world. The accurately priced pads'
  -14.4 s on Dantooine is given up with the Metalorn loss.
- **Build `j`'s two deaths** (Dantooine blue_out L1 respawn4, L2 respawn5):
  after the pickup the route home walks across a pad's trigger, the pad
  throws the carrier and the jumppad state aims at the flag and falls about
  520 units into the pit. The same build with the mesh loaded
  (`meshload` arm) flies the same touches to the mesh's landings and loses
  no one. On build `k` those touches fly to native landings (`src=native
  pad edge`); 0 deaths.
- **Identity:** build `j` with `bot_moveNativeEdges 0` reproduces the retained
  mesh arm exactly, 78 of 78, so the shared price function changed nothing
  on the mesh path. Build `k` equals `j` except Dantooine blue_out L2 and L3
  respawn5 (+425 ms each), whose routes cross a pad before the pickup and
  now fly it to its native landing.
- **Mesh loaded or not** (build `j`, native defaults): 69 of 78 identical;
  the rest differ by at most 1.5 s where only `WalkStep`'s off-path
  steering still reads a mesh.

**Open.** A route that walks across a pad trigger is thrown whether or not it
planned the pad; the landing is now safe, but the planner does not yet
avoid triggers it does not mean to take. `WalkStep`'s off-path steering
still reads a mesh when one is loaded and declines `no_path` without one.

### Pad edge matched to the trigger entered (review fix, same day)

Review: the path's next pad edge was taken for any jumppad flight, so an
unplanned pad crossed near a planned one flew to the planned pad's landing,
and `PadObserve` could time the wrong edge. Now one choice per flight
(`PadFlightEdge`, `bot2_move.c`): the path's next pad edge only if its
takeoff touches the trigger entered, otherwise the thrown search; timing
only for a matched planned edge. Build `padmatch-build-20260927-a` against
build `m` (sources identical but for this change), Dantooine v3, class 100,
mesh not loaded, `--linger 4`, `--skip-twins`
(`.codex-research/padmatch-20260927-a` against `mapprep-20260927-a`):
seeded and unseeded 18/18 each, **36 of 36 identical** to the control, the
same six `thrown` flights (edge 37). No case plans a pad, so the fixture
does not exercise the mismatch; the change is inert on it.
