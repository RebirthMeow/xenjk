# Defender flag keep-out as a forbidden volume (M11), late release (D10), M9 removed (2026-10-09)

Branch `defender/flag-avoid-20261009` (worktree `../xenjk-wt-m11`), not merged. Commits: e13de7cd (M9
removed), 65c21dba (M11 + D10 + fixture), d621b06c, cfd9b265 and 6d1deaed (M11 fixes after each arm). Evidence:
`.codex-research/m11-20261009-a/` (scripts `run.sh`, `forced3.sh`, `run3.sh`, `run4.sh`, `run5.sh`; analysis `flagbox.py`,
`permap.py`, `work.py`, `pickups.py`). Builds: `m11-build-20261009-base` (fc1187ba), `-a` (65c21dba), `-b` (d621b06c),
`-c` (cfd9b265), `-d` (6d1deaed). Native world time about 95 min (65 + 2 + 28 + 28 + 8 worlds). Laps worlds as `nighttests-20261009-a/run.sh` (two-sided, camper, base+chase, 600 game s at x25).

## M9 removed (owner: "remove what lost")

`bot_moveClimbApproach` and its code (`ClimbApproach`, the `PlannedEligible` early return) are taken out; the
measurement stays in [the Kejim replay record](2026-10-09-kejim-replay.md). Its default was 0. Identity (game log and
every decision line, `tools/laps_identity.py`): the base build (fc1187ba, M9 present at 0) against build a with
`bot_defenderFlagKeepOutRoute 2` and `bot_defenderLateRelease 0` (the old behaviour of everything this branch
touches): **Dantooine 1-4 and Kejim 1-4 identical** (`base-*.txt`). This also shows mode 2 is the old lattice exactly
and that the new diagnostics are inert. Seeded worlds replayed across builds here (base, a, b, c), after the Kejim
replay fix.

## M11: the flag keep-out as a forbidden volume

Owner: "Using bodies to decide the route is hacky, find a proper way to have defenders avoid flags."

**Design.** The box is the pickup box in hull-origin space (`BG_PlayerTouchesItem` compares the origin with the
item's: x -50..44, y and z +-36) plus `bot_defenderFlagKeepOutMargin` (40), around the enemy flag a base or chase D
keeps out of (`KeepOutFlag`: a dropped flag, else the stand). Because the box is already in origin space, the hull's
sweep is the origin's segment; a segment *enters* when it meets the box and starts outside it (a step from inside
may leave). `Bot2_SegmentEntersBox` (bot2_forecast.c) is a slab test after a six-compare bounds reject.

- **Rollouts** (`Bot2_ForecastForbidSet`, per client): every step of every rollout of that client (gap, straight,
  drop, climb searches, the walk lookahead, parity), replayed steps included, sets `BOT2_FORECAST_FORBIDDEN` when it
  enters the box, as a refused trigger or lethal contact refuses the rollout. Not a world property: no trigger, no
  replay key (Pmove never reads it).
- **Planner** (`Graph_ForbidSet`, per plan, set and cleared around `Replan` and `RerouteSearch` as the bodies are):
  `Relax` and `StateRelax` refuse a walk link or diagonal whose cell-to-cell segment enters, and an edge whose
  source-to-takeoff segment enters or whose takeoff-to-landing segment enters the box lowered by
  `GRAPH_FORBID_ARC` (96 u: the arc may rise that far over the chord). Route tables are bypassed while set; a split
  search keeps the box it began with.
- **Walk** (`ForbidTarget`): the walk cuts corners toward a target up to 96 u along the path; a target whose chord
  enters the box becomes the next corner, then the next cell, then the reached cell.
- **Shared state**: nothing is written to cells or edges, so other bots' plans and the learned costs never see it;
  a defender is a guest, which charges no failure to the graph anyway (`ChargeFailure`).
- **Cost per expansion**: one flag test when no box is set; with one, the bounds reject and at most a slab test.
- **Camper**: exempt, as before (`KeepOutFlag` returns none for the camper).
- **Flag moves**: `ForbidSync` (at `Move_Command`, the Pmove boundary and the pool's frame end) reads the box each
  time; a box that appears or moves by 16 u or more replaces the old one, ends a split search, and replans the
  route only if the new box cuts it (cfd9b265).
- **The goal**: the volume constrains the route, never the goal. `FlagKeepOut` moves every `DefenderMove` goal out
  of the box, so a movement objective inside it is a defender layer's deliberate exception (Melee at the empty stand)
  or a stale objective after the box moved; the volume is off while the objective is inside (d621b06c).
- **Cvar**: `bot_defenderFlagKeepOutRoute` 1 (default) the volume; 2 the old nine-body lattice, kept as this
  record's comparison arm (removing it is a one-line change once the volume is accepted); 0 neither. The lattice's
  limits (16 bodies, `bot_moveFailCause`) do not apply to the volume.
- **Diagnostics** (both modes, `[FLAGBOX]`, not a decision stream): `enter` when a base or chase D's origin enters
  the box (`flag=` home, dropped, or away with the box on the empty stand; `goal_in=` its last goal inside);
  `plan_cross` when an installed route enters the box; `plan_none` when a mode-1 plan found no route.

### Iterations (all against the same control; builds a, b and c replay the control exactly: `ctlb-*.txt`, `ctlc-*.txt`)

1. **Build a** (arm `arm`): a chase D stood 2.5 s at the edge of Dantooine's stand replanning a route the volume could
   not give: its movement objective was inside the box (a stale objective: `Move_Plan` keeps one within 64 u). A
   dropped flag still moving replanned every frame. Kejim planner expansions +17.5 +- 10%.
2. **Build b** (`armb`): the objective exemption and the 16 u hysteresis. Kejim expansions still +14.1 +- 10%: every
   flag drop, pickup and return moved the box and replanned both D's of a team.
3. **Build c** (`armc`): replan only a route the new box cuts.

### Results

Control `ctl` (mode 2, the lattice) against `armc` (build c, mode 1), 8 Dantooine + 16 Kejim, 600 game s each.
Per world, means (`permap.txt`):

| per world | Dan ctl | Dan armc | Kej ctl | Kej armc |
| --- | --- | --- | --- | --- |
| installed routes entering the box (`plan_cross`) | 13.25 | **0.00** | 18.62 | **0.69** |
| base/chase D origin entering the box, all | 5.25 | 4.50 | 10.50 | 6.12 |
| ... with the flag home | 0.50 | 0.12 | 0.56 | 0.12 |
| ... with the flag dropped | 1.25 | 2.12 | 5.31 | 2.81 |
| ... with the flag away (empty stand) | 3.50 | 2.25 | 4.62 | 3.19 |
| mode-1 plans with no route to a real goal (`plan_none`, to >= 0) | - | 5.00 | - | 1.12 |

The lattice's routes crossed the box 13-19 times a world (nine 40 u bodies on a 48 u lattice leave gaps, and edges
fly over them). The remaining 0.69 a Kejim world (11 lines, all `state=1` strafe routes) were routes from a source
cell inside the box: the planner exempted the whole edge, not just its first segment; fixed in 6d1deaed (below).
Entries that remain are mostly not routes: a flag dropped onto or beside the defender that killed its carrier (the box
appears around it), the deliberate Melee entries at an empty stand, and pushes.

`tools/laps_compare.py` (arm minus control, +- 95% t half-width; `*` past the cut-off):

| per 600 s | Dan ctl | Dan diff | Kej ctl | Kej diff |
| --- | --- | --- | --- | --- |
| team captures (primary) | 5.31 | -0.86 +- 0.49 | 3.76 | +0.31 +- 0.42 |
| runner captures | 3.95 | -1.17 +- 0.57 | 2.28 | +0.62 +- 0.39 |
| all captures | 10.73 | -1.48 +- 0.98 | 7.70 | +0.44 +- 0.86 |
| runners' falls | 3.09 | +0.25 +- 1.19 | 8.62 | -0.92 +- 1.08 |
| runners' weapon deaths | 31.83 | +3.21 +- 1.70 | 33.26 | -1.46 +- 1.10 |
| defender deaths | 20.85 | +0.19 +- 0.90 | 23.22 | -0.01 +- 0.92 |
| defender enemy-flag pickups | 0.49 | +0.37 +- 0.43 | 1.11 | -0.49 +- 0.25 |
| carrier kills by base + chase D (`carrier.py`) | 32.9 | -0.7 | 35.7 | -0.3 |

Verdict (laps_compare): **undecided** on both maps. The signs disagree between maps (Dantooine captures down and
runners' weapon deaths up, Kejim the reverse), and the earlier arms on Dantooine (team captures -0.25 and -0.31)
did not show the -0.86. No case either way from 24 worlds; not grown (owner: keep compares cheap).

**Frame cost** (work counts from `[FRAMEWORK]`, paired by seed, `work.py`; wall clock is not used, look-only screen
runs overlapped some batches): build c against the control, Dantooine est. frames over 25 ms -4.3% (-83 +- 182),
planner expansions a frame +1.0 +- 5.1%; Kejim over 25 ms +2.9% (+16 +- 24), expansions +5.5 +- 7.8%: inside noise.
Build a was +17.5% Kejim expansions and build b +14.1% (both past their intervals) before the replan fix.

**Build d** (6d1deaed, the per-segment exemption; an edge whose takeoff lies under the box within the arc allowance is
refused): a check of 4 Kejim + 4 Dantooine worlds, `armd/` (seeds 1-4): **no installed route enters the box** (0 in 8
worlds), no plan failures beyond build c's (to a real goal 1-2 a world), work counts against the control's same seeds
inside noise (Kejim expansions +7.9 +- 17.6%, Dantooine -2.0 +- 4.2%). Four worlds a map say nothing about the game
metrics (team captures Dantooine -0.62 +- 0.46, Kejim -0.80 +- 0.44 against the full control, both undecided). The
branch head is build d; the full paired arm is build c's.

## D10: a defender dead at the release

Every fixture releases once a world, and `Bot2_DefenderRelease` skipped a dead defender, so it was never armed for
the whole world (or kept an earlier release's state). Now (`bot_defenderLateRelease`, default 1; 0 the old skip) the
release clears a dead defender's slot (nothing of an earlier release leaks), marks it pending (`RELEASE_PENDING`),
and `DefenderCommand` releases it at its post when it respawns (`ReleaseOne`, the same path, with
`RELEASE ... late=1 released=<the world's release level>`).

- **Forced case** (`forced3/`, Dantooine seed 1, 240 game s, build a): `botkill Defender_v2` 100 ms before
  `defenderpost` (new fixture: the `botkill` svcmd and `defense_laps.py --kill-before-release`). `RELEASE_PENDING` at
  8500, the other five released, then `GUARD` and `RELEASE late=1 released=8500` at 9450 after its respawn; it
  planned 215 routes in the world. The same world with `bot_defenderLateRelease 0` and `Chaser2_v2` killed: no
  release line for it and **0 routes planned in 240 s** (the silent failure).
- **Identity**: ordinary worlds (all six alive at the release) with `bot_defenderLateRelease` 1 against 0, same build
  a, route mode 2: Dantooine 1-4 and Kejim 1-4 identical (`ident/` against `ctl/`).

## Left

- Owner: merge of `defender/flag-avoid-20261009`; whether to remove the lattice (mode 2) now.
- The control arm's dropped-flag pickups are mostly a flag dropped onto or beside the defender that killed its
  carrier (`pickups.py`): a route constraint cannot prevent those.
