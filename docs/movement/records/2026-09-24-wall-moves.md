# Wall moves at the bot's command rate, 2026-09-24

Owner direction: wall runs are the preferred way between levels, lifts too;
wall moves allowed at every level (they need the Force Jump rank, not Force
points: every drain clamps at 0). ctf4 needs one 384-unit climb for a
flag-to-flag run (lift shafts *12 and *21), 624-640 at the other four lifts.

## The probe

`bot_moveWallProbe` (`codemp/game/bot2_wallprobe.c`, fixture diagnostic): at
one boundary the runner is placed at declared poses and a sweep of scripted
inputs is rolled out through the forecast (native Pmove), one `[WALLPROBE]`
line per rollout, `[WALLPROBEF]` per frame for a chosen rollout; the live state
is restored. Worlds: `.codex-research/wallprobe-20260924-a/` (build
`wallprobe-build-20260924-g`). Runner: Force Jump rank 3, saber, pool 100.

## The run up a wall cannot be sustained at 25 ms commands

At ctf4 x=736, y=2470 (lower floor, facing the *12 shaft's wall 41 units
ahead) every one of 77 input timings ended below 91 units. The traced rollout
(jump frame 2, re-press frame 6): the jump leaves at 301 u/s; the re-press at
35 units of rise sets `BOTH_FORCEWALLRUNFLIP_START` and on the next Pmove
substep `PM_AdjustAngleForWallRunUp` fails and sets
`BOTH_FORCEWALLRUNFLIP_END`, throwing the bot 300 u/s back from the wall.

Cause, from `bg_pmove.c`: the entry (`PM_CheckJump`, ~2556) and every
successful continuation (`PM_AdjustAngleForWallRunUp`, ~1546) set
`ucmd->forwardmove = 0` on `pm->cmd`, and the continuation requires
`ucmd->forwardmove > 0` (~1484). `Pmove` splits one command into 8 ms
`PmoveSingle` substeps that share `pm->cmd` (~11216), so a 25 ms bot command
(three substeps) always reaches a substep with forward zeroed. A client sending
8 ms commands gets a fresh command per substep and can climb. The forecast
reproduces native Pmove here, so this holds for the live bot too.

The side wall run is not affected: `PM_AdjustAngleForWallRun` continues on the
wall alone (forward only picks 250 or 175 u/s), and it rises at most about
224 units (423 u/s at half gravity), which is short of ctf4's 384.

A lower-floor pose at x=768 was refused outright: the player box overlaps the
lift's bottom `trigger_multiple`, and the forecast refuses every trigger
contact.

## 8 ms commands for climbs only (owner, 2026-09-24)

Owner decision: 8 ms commands only where 25 ms fails - wall climbs - and 25 ms
for everything else; revisit later if needed. `codemp/game/bot2_finecmd.c`:
`Bot2_Pmove` re-issues the bot's command every `pmove_msec` while
`legsAnim == BOTH_FORCEWALLRUNFLIP_START`, or when the command enters a climb
(airborne, jump pressed and not held, forward held, Force Jump rank above 1,
legs in `BOTH_JUMP1`/`BOTH_INAIR1`, and the run-up entry's own 32-unit wall
test). Every other command reaches `Pmove` unchanged. The live move
(`bot2_movebench.c`) and the forecast (`bot2_forecast.c`) both call it, so
rollouts slice where the live bot does. Server command times are already
rounded up to `pmove_msec` multiples (`g_active.c:2044`), so 8 ms is the
finest cadence that exists; below it commands merge.

Probe on build `finecmd-build-20260924-a`
(`.codex-research/wallprobe-20260924-a/fine1/`), ctf4 *12 wall, x=736:

| Start (y, distance to wall) | Timings | Climbs started | Ended on the upper floor (+384) |
| --- | --- | --- | --- |
| 2470, 41 | 77 | 54 | 48 |
| 2420, 91 | 77 | 38 | 35 |
| 2350, 161 | 77 | 12 | 11 |

The highest point reached is 462 units. A climb needs the re-press when the
wall is within the entry's 32 units, which is why the timing window narrows
with distance.

Regression on the same build (`.codex-research/finecmd-20260924-a/`): Kejim
production floor 36/36 and Dantooine 36/36; the Kejim library stand run
13,625 / 14,125 / 13,400 / 14,075 ms at classes 75 / 100 / 125 / 150, identical
to build `k` (class 75 missed its class once and passed on the rerun).
Movement that never enters a climb is unchanged.

## Climb edges (build `climb-build-20260924-f`)

- **Discovery** (`bot2_graph.c`, `Climbs`, `bot_moveClimbs`): a cell whose
  axis-neighbour sample struck solid, a near-vertical world face within 48
  units that way, a world floor 48-440 units up and 32 units past the face,
  a graph cell there, and the column against the face open from the floor to
  above the top. Edge `GRAPH_EDGE_CLIMB`, unvalidated, priced rise/300 s plus
  0.8 s. Built graphs only by default (a loaded cache keeps its identity, so
  the Kejim seed and its tapes are untouched); `bot_moveClimbs 2` adds them to
  a loaded cache, 0 disables. ctf4 finds 52 (222 before the column test: 66 of
  66 rollouts at three of them met an overhanging walkway), Dantooine 116.
- **Validation and execution** (`bot2_move.c`, `ClimbSearch`, `ClimbStep`,
  `MOVE_PLAN_CLIMB`): once the walk stands within 96 units of the takeoff, 66
  script timings (jump command 0-20, re-press 2-12 later, facing the face,
  forward held) are rolled out from the live state; the soonest one standing
  on world floor within a step of the edge's landing height and 128 units of
  it is committed. `PlanCompleted` validates the edge only if the bot stands
  on that floor. First live climb on ctf4: 90 frames against the rollout's 92,
  landing z 88.1 against 88.2, 40 ms of rollouts.
- **Two repairs the climbs exposed**, both on ctf4's drops from base to lower
  floor: a planned drop edge's candidates may fall the edge's own declared
  descent plus two steps (they were judged falls at 384 units and the edge was
  disabled), and a drop candidate counts only if it lands at its edge's floor
  height (at Force Speed it flew across to another part of the upper level,
  was admitted, and the edge was then disabled).
- **Lift calls are not refusals** (`bot2_forecast.c`, `MoverCallsPrepare`): a
  `trigger_multiple` whose every target is a `func_door` or `func_plat`
  starts a mover and does nothing to the toucher, so contact no longer refuses
  a rollout. Kejim and Dantooine have no `trigger_multiple`; ctf4 has 14, all
  lift calls.

| Floor (`.codex-research/climb-20260924-c/`) | L1 | L2 | L3 | Cases with a climb |
| --- | --- | --- | --- | --- |
| ctf4, 1 | 11/12, median 18.4 s | 12/12, 13.1 | 12/12, 12.8 | 35 |
| ctf4, 2 | 12/12, 18.4 | 12/12, 13.0 | 12/12, 13.2 | 36 |
| Dantooine | 12/12, 19.8 | 12/12, 14.4 | 12/12, 14.4 | 34 |

Kejim production floor 36/36 and its library stand run 13,625 / 14,125 /
13,400 / 14,075 ms, identical to build `k`. Before this work ctf4 was 0/36
(`.codex-research/maps-full-20260923-b/ctf4-1`).

The one failure, `red_ret_L1_stand` in floor 1, is the blue base's drop next to
lift *12: every candidate touched the lift platform resting at the bottom
(forecast `BODY`, 20) or landed on it (`off_graph`, 4), and after two searches
the edge was disabled. A stationary mover is floor until it is called; teaching
the graph and forecast that is the first piece of lift support.
