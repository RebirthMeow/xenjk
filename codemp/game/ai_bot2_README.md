# Bot2: CTF bot AI

Bot2 is a CTF bot AI built on OpenJK's bot framework in `codemp/game/`: the
movement-rewrite runner (offense, `Runner_v2`) and the fixture defenders (base,
chase and the camper). It takes over the think function of a bot whose name
contains `_v2` (or any bot, with `bot_forcebot2`) when that bot is a runner or
a defender (`Bot2_UpdateManagedBots`); every other bot keeps the stock AI, so
both kinds can play on one server. The classic Bot2 macro AI (roles, item and
weapon hunting, combat aim, its off-mesh movement states and wallrun tools)
was removed on 2026-10-08 (owner) and survives in Git history.

This file maps the source for contributors. Behaviour and status live in the
design documents:

- [docs/movement/](../../docs/movement/README.md): the movement rewrite, the
  current work. Start with its README, then
  [ARCHITECTURE.md](../../docs/movement/ARCHITECTURE.md) and
  [PLAN.md](../../docs/movement/PLAN.md).
- [Pre-rewrite archive](../../docs/archive/strafe-movement-2026-09-14/README.md):
  the earlier strafe-movement stack and its experiments. Their code was
  removed on 2026-10-07 and survives in Git history. Read it for history only.
- [Playing against the runner](../../docs/KEJIM_RUNNER_PLAY.md).
- [AGENTS.md](../../AGENTS.md): working rules for this repository.

## Movement

**Movement rewrite.** `bot2_move.c` plans over a movement graph that
`bot2_graph.c` builds from map geometry at map load. Drop, jump, pad, climb
and lift edges are found from the world by default, so no navmesh is
required. Every hop is checked by native Pmove rollouts (`bot2_forecast.c`)
from the live state before it is committed.

Every Bot2 bot that reaches movement dispatch (`Bot2_ExecuteMovement`) goes
through `Move_Plan` and `Move_Command`, after the jump-pad state; a frame
`Move_Command` declines (alive but not `PM_NORMAL`, a Force grip's `PM_FLOAT`
the common case) stands still. The
rewrite's hop searches run only for movement-only clients (and searching
guests) on a dedicated server (`MoveSearchGates` in `bot2_move.c`; no
`sv_cheats` since 2026-10-07); elsewhere it walks its planned route. The
production settings are the code defaults since 2026-10-07 (`bot_moveEdges`,
`bot_moveExitSpeed` 500, `bot_moveReroute` 1, `bot_carrierMelee`,
`bot_navmeshOff`).

The pre-rewrite runner stack (a recorded route follower, supported-path
traversal, local navigation and a prepared-flight adapter), the
`bot_moveRewrite` switch and the archived research benches were removed on
2026-10-07; the classic off-mesh states that took declined frames, on
2026-10-08.

`Runner_v2`, added by `addrunner`, is the movement-only offense bot.
`tools/play_vs_runner.py` runs it with the settings the movement gates
measure (its `PRODUCTION` table).

## File map

`codemp/game/CMakeLists.txt` lists every file below. Headers not listed there
(`bot2_corner.h`, `bot2_probe.h`, `bot2_threat.h`) are included by listed
sources.

### Core AI and integration

| File | Owns |
|---|---|
| `ai_bot2.h` | Public surface used by the rest of the game: `bot2_state_t` (the runner's objective and the jump-pad state), lifecycle and the movement entry point. |
| `ai_bot2_internal.h` | Private helpers shared between Bot2 files: the ground corridor and the floor probes. |
| `ai_bot2.c` | Storage for `bot2_states[]`, managed-bot selection (`Bot2_UpdateManagedBots`), the play-frame dispatch (`Bot2_RunPlayFrame`), `Bot2_Think` (one command a game frame for a runner or a defender) and the runner's kill rules (`PitKill`, `RunnerCapKill`, `StrandedKill`). |
| `ai_bot2_objective.c` | Movement-only offense policy for the runner: flag targets, dropped-flag reachability, protect/absorb and pressed-carrier melee (`bot_carrierMelee`). |
| `ai_bot2_movement.c` | Movement dispatch (`Bot2_ExecuteMovement`): a declared kick, the jumppad state, then the rewrite (`Move_Plan` / `Move_Command`); a declined frame stands still. Also the floor and pit probes carrier melee uses. |
| `ai_bot2_localnav.c` | The ground corridor: `Bot2_GroundSample`, the one corridor sample the rewrite's walk and graph admission share, the four-sample `Bot2_GroundCorridor` and its per-boundary memo; `[GROUNDTRACE]` lines with `bot_supportedTrace`. |

### Movement rewrite

| File | Owns |
|---|---|
| `bot2_move.c` / `.h` | The runner's movement: one `movement_t` per client with one reset (`Move_Reset`), `Move_Plan`, the executor `Move_Command`, hop and drop searches, the speed purchase, failure attribution (`bot_moveFailCause`), the `[MOVE]` event lines and the forecast-versus-live parity harness (`bot_moveParity`). |
| `bot2_graph.c` / `.h` | The movement graph: flood fill of walkable 32-unit cells from the BSP, nodes, native drop/jump/pad/climb/lift edges (navmesh hints only with `bot_moveNativeEdges 0`), learned hop and strafe edges, and A* by time. Built by `Move_PrepareMap` at map load and cached as `bot2-graph/<map>_<checksum>.txt`. |
| `bot2_forecast.c` / `.h` | Native Pmove rollouts from a copied live state under a static-world guard, with the frame's Force update modelled. Used to validate hops and by the parity harness. |
| `bot2_movecontrol.c` / `.h` | Pure one-frame command controller: intent plus current momentum to a legal usercmd. Shared by the rewrite and the forecast. |
| `bot2_mover.c` / `.h` | Binary movers at rest treated as floor, lift call triggers and boarding delays, used by the graph's lift edges and the ground checks. |
| `bot2_finecmd.c` / `.h` | `Bot2_Pmove`: repeats a bot's command every `pmove_msec` only while it climbs a wall, so live play and the forecast slice Pmove the same way. |
| `bot2_threat.c` / `.h` | Missile reaction delay (`bot_threatReactMs`, default 850): a missile younger than the delay is hidden from the bot's traces while it decides. |

### Fixtures and diagnostics in current use

All are opt-in and intended for localhost test worlds.

| File | Owns |
|---|---|
| `bot2_movebench.c` / `.h` | The Pmove-boundary wrapper (`Bot2_MoveBenchPmove`) where the rewrite's boundary searches and parity run, and the fixtures' pre-start hold (`bot_movebench_hold`). |
| `bot2_movecost.c` / `.h` | Multi-client movement cost fixture: per-frame, per-client timing of movement dispatch and preparation. `codemp/server/bot2_servercost.cpp` adds whole-server-frame timing. Also the frame meter (`bot_moveFrameMeter`): per-bot movement time per frame in ordinary bench worlds, as `[FRAMECOST]` lines. |
| `bot2_corner.c` / `.h` | `bot_corner_start`: one-shot exact pose, velocity and resource setup for a benchmark case, including game-timed starts (`bot_cornerDefer`) and an optional body blocker. Dedicated server and cheats only. |
| `bot2_defender.c` / `.h` | `Defender_v2`, a scripted enemy that holds a post and uses native push/pull when a throw would be lethal; optionally chases the carrier, walking or (`bot_defenderStrafe`) with the runner's own searches as a guest (`MoveGuest` in `bot2_move.c`: no graph writes, no harness). See its [fixture notes](../../tools/fixtures/movement/defender/README.md). |
| `bot2_kick.c` / `.h` | Declared knockback without damage (`bot_moveKick`) to measure recovery. |
| `bot2_wallprobe.c` / `.h` | Rolls out scripted wall-move inputs from a declared pose (`bot_moveWallProbe`); never commands the live bot. |
| `bot2_probe.c` / `.h` | Play-test probes: missile trace counts, Force command arrival (`bot_probeGenCmd`) and `probe_rocket`. |
| `bot2_route_record.c` / `.h` | Records a human's (or a declared bot's) commands and native state to CSV for comparison with the search. Never issues commands. |
| `bot2_route_play.c` / `.h` | Replays a recorded command tape on one bot (`bot_route_play`). A research tool, not part of the runner. |

### Commands

| File | Owns |
|---|---|
| `bot2_commands.c` / `.h` | Cheat-gated client commands for inspecting the navmesh (`navinfo`, `navtest`, `navcheck`, `navdraw`, `navdrawoffmesh`). |
| `bot2_svcmds.c` / `.h` | Server commands `addrunner`, `botplayrate`, `botleveltime` and `botat`, plus the play CSV log under `bot2-play/`. The header also declares `move_graph` and the defender commands. |

### Removed 2026-10-07

The pre-rewrite runner stack (`ai_bot2_route.c`, `ai_bot2_supported.c`,
`ai_bot2_hop_fallback.c`, `ai_bot2_flight.c`, `bot2_strafe`, `bot2_path`,
`bot2_handoff`, `bot2_hop`, `bot2_receiver`, `bot2_flight`, `bot2_flightplan`,
`bot2_flightlib`, `bot2_windowseed`, `bot2_refpath`), the archived research
fixtures (`bot2_maneuver*`, `bot2_transition`, `bot2_connectionbench`,
`bot2_mapbench`, `bot2_flagrun`, `bot2_continuous`, `bot2_disturbance`,
`bot2_flightcase`, `bot2_flightbody`, the `bot_movebench` probe),
`ai_bot2_steering.c` and the local-navigation layers of `ai_bot2_localnav.c`
are in Git history before that date; the
[archive](../../docs/archive/strafe-movement-2026-09-14/README.md) says what
each was for.

### Removed 2026-10-08

The classic Bot2 (owner): the `Bot2_Think` macro AI (roles, macro states, item
and weapon hunting, trip mines, combat aim, role-tag renames, the
`bot_test_routing` harness), the off-mesh state machine of
`ai_bot2_movement.c`, `ai_bot2_pmove.c`, `ai_bot2_combat.c`,
`ai_bot2_wallrun.c`, `ai_bot2_plan.c`, and the `addbot2enemy`,
`bot_wallrun_check` and `bot_scan_wallruns*` commands
([FINDINGS](../../docs/review/FINDINGS.md) area 7).

## Call flow

Selected calls, not a full call graph. Search for callers before changing an
API.

```
ai_main.c   BotAILoadMap    --> Move_PrepareMap (bot2_graph.c)
g_main.c    G_RunFrame      --> Bot2_RunPlayFrame --> Bot2_Think   (ai_bot2.c, bot_movementOnly 2)
                            --> Bot2_CornerFrame  (bot2_corner.c)
ai_bot2.c   Bot2_Think      --> ai_bot2_objective.c (runner) | bot2_defender.c (defenders)
                            --> Bot2_ExecuteMovement (ai_bot2_movement.c)
movement.c  jumppad state first, then Move_Plan / Move_Command (bot2_move.c)
bot2_move.c --> bot2_graph.c, bot2_forecast.c, bot2_movecontrol.c, bot2_mover.c,
                bot2_threat.c, Bot2_GroundSample (ai_bot2_localnav.c)
g_active.c  ClientThink     --> Bot2_MoveBenchPmove (bot2_movebench.c)
                            --> Bot2_MoveParityBeforePmove / AfterPmove (bot2_move.c)
                            --> Bot2_Pmove (bot2_finecmd.c)
bot2_forecast.c             --> Bot2_Pmove, bot2_mover.c
bot2_defender.c (chaser)    --> Bot2_ExecuteMovement
```

## Shared state

`bot2_state_t` is declared in `ai_bot2.h`; the array `bot2_states[MAX_CLIENTS]`
is defined in `ai_bot2.c`. It holds the runner's objective (role, macro state,
goal entity and origin, which the play log records) and the jump-pad state
(`state`, `stateTimer`, the locked landing `tele_predPos`).

Several files also keep private per-client state, chiefly `movement_t` in
`bot2_move.c`. Pure kernels (`bot2_movecontrol.c`) take explicit inputs and
caller-owned state.

The compiler cannot catch a changed meaning. Before changing how any
`bot2_states[]` field is read or written, search all Bot2 sources for it and
review every use. `state`, `stateTimer` and `tele_predPos` are read by
`bot2_move.c` as well as written by the pad.

## Public and private API

- `ai_bot2.h` is the only Bot2 header the rest of the game needs for the AI
  itself: `Bot2_ClearState`, `Bot2_Think`, `Bot2_UpdateManagedBots`,
  `Bot2_ExecuteMovement` and `Bot2_PrintTelemetry`.
- `ai_bot2_internal.h` is the private contract between Bot2 files. Treat a
  change there as an interface change.
- `bot2_*` kernels and fixtures each expose a small header of their own.
  Stock files include those headers only where they call the fixture.

## Working on this code

- Read [AGENTS.md](../../AGENTS.md) first. AI policy and integration belong
  in `ai_bot2*`; shared kernels and fixtures in `bot2_*`.
- For movement changes, read the rewrite's
  [ARCHITECTURE.md](../../docs/movement/ARCHITECTURE.md) and
  [LESSONS.md](../../docs/movement/LESSONS.md). Rules there, such as one
  movement state with one reset and no rule without a benchmark case, apply
  to `bot2_move.c` and `bot2_graph.c`.
- `bot2_move.c` runs the same step function live and in forecast rollouts.
  A change to the executor changes both; the parity harness
  (`bot_moveParity`) reports any divergence.
- The graph cache is keyed by map checksum and physics. Changing discovery
  means rebuilding caches and prepared seeds (`move_graph rebuild`,
  `tools/movement_graph_prepare.py`).
- The movement benchmark is `tools/movement_bench.py` with cases under
  `tools/fixtures/movement/`. Run test worlds on localhost only.

## Hooks into stock OpenJK

Keep upstream edits small so Bot2 stays portable. This list covers Bot2
calls; the fork has other changes, so check the diff as well.

| Stock file | Bot2 integration |
|---|---|
| `ai_main.c` / `ai_main.h` | `customThink` hook, `Bot2_ClearState` on bot setup, map-load preparation (`Move_PrepareMap`), `Bot2_UpdateManagedBots`, `bot_telemetry` registration. |
| `g_main.c` | Route recorder lifecycle, `Bot2_RunPlayFrame` before client movement, `Bot2_CornerFrame`, graph cache flush and play-log close at shutdown. |
| `g_active.c` | `Bot2_MoveBenchPmove` around every Pmove, probe hooks, recorder after-effects, and fixture takeover of `G_RunClient` (route play, the cost fixture, the movebench hold). |
| `g_client.c` | Recorder stop on disconnect. |
| `g_cmds.c` | `nav*` client commands and `route_record`. |
| `g_svcmds.c` | Bot2 server command entries; `route_record` and `bot_route_play` console hooks; `bot_trigger_shadow`. |
| `g_utils.c` | Suppresses temp entities during forecast rollouts. |
| `bg_pmove.c` | Forecast profiler mark. |
| `codemp/server/sv_main.cpp` | Server-frame cost timing (`bot2_servercost`). |

The navmesh (`g_navmesh.cpp`, `g_navmesh.h`, `detour/`) is also fork code, used
by the jump pad's offmesh landing, the planner's optional navmesh hints and the
`nav*` commands. `bot_navmeshOff 1`, read at map
load, runs as if the map had none.

## Build

Every Bot2 source is listed in `codemp/game/CMakeLists.txt`; add new ones
there. The build and native verification procedure is in the
[verification reference](../../docs/archive/strafe-movement-2026-09-14/STRAFE_MOVEMENT_VERIFICATION.md),
which is archived but still current for builds. Documentation-only changes
need no native build.

## Console commands

Server commands (rcon or server console). Their table is `svcmds[]` in
`g_svcmds.c`.

| Command | Effect |
|---|---|
| `addrunner [red\|blue]` | Adds `Runner_v2` in a CTF game, sets `bot_movementOnly 2` and the play settings, and opens a play log. Requires `bot_enable 1`. |
| `adddefender red\|blue`, `defenderpost` | Adds the scripted defender, then places it at its post (`bot_defenderPost`). |
| `botplayrate 1\|2.5\|5` | Sets `timescale` and `sv_fps` together, keeping 25 ms game steps. |
| `move_graph [rebuild\|path x y z x y z]` | Reports, rebuilds or searches the movement graph. |
| `bot_corner_start ...` | Benchmark case setup (see `bot2_corner.c`). |
| `route_record <client> arm\|stop\|status` | Human command recorder; also a client command. |
| `bot_route_play load\|arm\|hold\|where\|status\|stop` | Command tape replay. |
| `probe_rocket`, `bot_trigger_shadow` | Play-test and forecast diagnostics. |

Client commands `navinfo`, `navtest`, `navcheck`, `navdraw [radius]` and
`navdrawoffmesh [radius] [type ...]` need `sv_cheats` and draw navmesh data.

## Cvars

Most Bot2 settings are not registered in one place: each is read where it is
used with `trap->Cvar_VariableIntegerValue` or `Cvar_VariableStringBuffer`.
To find the full set, search the sources for `"bot_`. The gates' production
settings are the `PRODUCTION` table in `tools/play_vs_runner.py`.

| Cvar | Default | Effect |
|---|---|---|
| `bot_forcebot2` | 0 | Nonzero makes a runner or defender of any name Bot2, not only `*_v2`. |
| `bot_movementOnly` | 0 | 2 (set by `addrunner`): `Runner_v2`/`Runner2_v2` are runners and the defender names defenders; 1 makes every `_v2` bot a runner (older tools; the cost fixture's bots are stock bots it drives itself); 0 leaves every bot on the stock AI. |
| `bot_telemetry` | 0 | 1 prints movement telemetry lines. |
| `bot_moveEdges` | 0 | 1 enables the rewrite's gap and planned-edge hop searches (on in production). |
| `bot_movePlanner` | on | 0 falls back to navmesh waypoint steering. |
| `bot_moveNativeEdges` | on | 0 takes pad, drop and jump edges from the navmesh instead of the world. |
| `bot_navmeshOff` | 0 | 1 ignores the map's navmesh (read at map load). |
| `bot_moveFailCause` | 1 | Charges a failed edge to its cause: bodies and missiles only for the failing bot, briefly; movers for 5 s; the world as before, but a validated edge returns after 30 s. 0 restores the old rule. |
| `bot_moveParity` | 0 | Parity harness horizon in frames; a diagnostic. |
| `bot_moveStrafeSkill` | 90 | Percent of ideal air-strafe the runner uses. |
| `bot_carrierMelee` | 0 | 1 lets a stalled flag-carrying runner swing its saber at an opponent within `bot_carrierMeleeRange` (default 48, at most 192). |
| `bot_threatReactMs` | 850 | Delay before a new missile is visible to the bot's traces; 0 is instant. |
| `bot_pmoveFixed` | 0 | 1 steps Bot2 bots with fixed Pmove (set by `addrunner`). |

## Conventions for new files

- Put AI policy and integration in `ai_bot2_*.c`; include `ai_bot2.h`, and
  `ai_bot2_internal.h` only for private cross-file helpers.
- Put shared kernels and fixtures in `bot2_*.c` with their own small header
  and explicit inputs.
- Keep file-local helpers `static` and add every new file to
  `codemp/game/CMakeLists.txt`.
- Keep calls from stock files to one line where possible.
