# Review findings

Verified findings from the fork review ([PLAN.md](PLAN.md)), most severe first within each area. Status: open, fixed
(commit), removed (with its code), or declined (owner).

Later reviews, each with its own index and lane reports: [character rendering, 2026-11-09](2026-11-09-character-rendering.md)
and [Bot2, 2026-11-09](2026-11-09-bot2.md).

## Area 10: Python tools (reviewed 2026-10-08)

121 tools (~29,000 lines), in two slices: the paths that delete, move, overwrite or launch, and dead tools with the
analysis tools' correctness. The suite (549 tests, from the repo root) passes before and after.

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| T1 | P1 | `research_shed.py` | Shed `base/demos` from every campaign as regenerable world demos, human sessions included: 47 sessions (`play-vs-runner-*`, `human-route-*`, `kejim-play-*`) recorded demos that exist nowhere under `.codex-research` or the trash now, among them the owner's 2026-09-29 defense session whose record says it keeps "server demos from both POVs". Most likely moved in the 2026-09-30 D: cleanup, when demos were listed as sheddable, and gone when the trash was emptied; no manifest survives to prove it | fixed: a person's session is never shed of its demos (code and KEEP), AGENTS.md says so; the lost demos are not recoverable here |
| T2 | P1 | `laps_compare.py` | A world declared two-sided whose Runner2_v2 never joined (a stalled start) was scored one-sided, its team captures counting flag returns (18-27 against native 3-11): 10 retained worlds, among them `join-`, `prune-` and `pooldefault-20261007-a` checks and the classic removal's void worlds (left out of every comparison this review made) | fixed: the declared roster (`session.json`, else the start config's runners) is checked; such a world is left out and named |
| T3 | P2 | `research_shed.py` | Any campaign name containing "build" was shed as a build tree (every folder and data file); a symlink or junction was walked into; one locked folder aborted the run; the manifest row was written after the move | fixed: `-build-` at a word boundary, links skipped, locked items skipped, row first, restore skips missing items |
| T4 | P2 | `defense_laps.py` | A world that failed before its quit left openjkded running at timelimit 0 on its port, beyond the four-world budget, and the campaign gave the port to the next world | fixed: the runner ends it |
| T5 | P2 | `laps_campaign.py`, `laps_scenarios.py` | Resuming reused a failed world's number and truncated a finished world's log; an arm whose worlds always fail looped without end; `--min` defaulted to 4 where the docstring says 8 | fixed: used numbers skipped, a failed scenario kept, an arm stops after twice `--max` attempts, `--min` 8 |
| T6 | P2 | client launchers | Every `+connect 127.0.0.1` sent a MOTD request to `updatejk3.ravensoft.com` (renderer and version strings) | fixed: `cl_motd 0` in `play_vs_runner`, `human_route_record`, `kejim_defense_play` |
| T7 | P2 | `bot_test_runner.py` main | Ran an internet-mode server (`dedicated 2`, masters, a predictable rcon password) for a classic Bot2 test that now drives stock bots | fixed: retired (its helpers stay) |
| T8 | P2 | `defense_laps.py --vs-legacy` | Its opponents were the classic Bot2; they are now stock bots | fixed: retired |
| T9 | P3 | free-space guard | Only defense_laps and play_vs_runner checked; the bench, explore, route_approve, cost runner, recorder and Kejim play wrote worlds unchecked | fixed |
| T10 | P2 | `laps_compare.py` | An arm took every folder sharing its prefix (`base` took `base-fast-1`) | fixed: `prefix-<n>` only |
| T11 | P2 | `laps_compare.py` statistics | Replayed worlds count as independent (identical hashes stay in n and the error); normal cut-offs (2.5, 1.96) at n of 4-8; the docstring says paired by seed but the error is unpaired Welch (on review5's worlds the two agree, -0.49 +- 0.36 against +- 0.34) | fixed (owner: fix, no more compare time): Student t cut-offs at the same levels with Welch df, a replayed world counted once, the docstring says unpaired; campaign sizes unchanged |
| T12 | P2 | metrics | Runner self-kills (CAPKILL policy and StrandedKill, a failure family; 43-46 an arm on review5 Kejim) are in no metric; `defender_deaths` misses Defender2/Chaser2; `defender_enemy_flag` counts the red defenders' own returns; `laps_scenarios` defines death counts differently | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): `runners_self_kills`; defender deaths of both teams from the game log; enemy-flag touches of the red base and chase D only; `laps_scenarios` counts alike |
| T13 | P3 | `laps_identity.py` | Leaves out `[REROUTE]`, `[TAPE] select`, `[PLANGUIDE]`, `[PATHTABLE]`, `[SHADOW]`, `[FORECAST]`, and strips `hits=`/`traces=` from defender outcome lines too | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): every decision stream compared, defender hits kept |
| T14 | P3 | overwrites | `route_asset_build` replaces an asset and tape, `movement_tape_library` copies over a library, `movement_graph_merge`/`enrich` overwrite `--out` (the live seed if named) without refusing | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): each refuses an existing output without `--force` |
| T15 | P3 | small | `game_seconds` includes setup (605-609 for 600, both arms alike); rcon passwords in two manifests (localhost, per run); unit tests leave a build folder per run (756 in `unit-artifacts`); `install_kejim_runner.ps1` does not check the DLL against its build manifest | fixed but `game_seconds` (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): no password in the cost manifest, the test removes its folders, the installer checks the DLL hash; `game_seconds` kept (changing it biases comparisons with retained summaries ~1.4%) |

Dead and broken tools (import graph over all 121, every rcon command and cvar checked against the engine): dead -
`position_camper`, `laps_support`, `movement_spike_report`, `movement_sweep_summary`, `movement_replay`,
`kejim_defense_play` (+ `run_kejim_defense.bat`, its pinned pre-rewrite build), and the 12 `strafe_*` modules reachable
only from `strafe_fixture_runner`'s retired run path, with their tests; retired with a refusal - `strafe_fixture_runner`
run, `bot_test_runner` main, `defense_laps --vs-legacy` (their helpers have 7, 8 and 4 importers). Removal is listed for
the owner; Removed 2026-10-08 (owner: remove what served removed Bot2, keep what works with the runner and the positional tools): `laps_support`, `kejim_defense_play` with `run_kejim_defense.bat`, the 12 `strafe_*` modules and their tests, `strafe_fixture_runner` cut to its launch helper, `bot_test_runner` cut to its rcon/shutdown/result helpers, `defense_laps --vs-legacy`. Kept, tested on current runner data: `movement_spike_report`, `movement_replay` (a 2026-10-01 bench case), `movement_sweep_summary`, and the positional studies (`position_*`, the defender studies). 484 tests pass and every tool imports.

Checked clean (condensed): `world_retention` (staging removal only of the uniquely named session after a verified copy
and exit), `staged_binary` (exclusive creation, no reparse points), every server launch on `net_ip 127.0.0.1`,
`dedicated 1`, masters blank, `secrets.token_hex` passwords (bot_test_runner aside), private homepaths, the bench's and
route_approve's cleanup, `publish_scrub` (never pushes, fails closed), `install_kejim_runner.ps1` (confined to the repo's
game copy, archives what it replaces); the two-sided capture arithmetic, the play CSV and the game-log regexes.

## Area 9: vendored code (reviewed 2026-10-08)

The fork added five third-party libraries and changed none of OpenJK's: 467 added files under `lib/` and Detour in
`codemp/game/detour`.

| Library | Version (confidence) | License | Local changes |
| --- | --- | --- | --- |
| Recast/Detour (`codemp/game/detour`, 72fc8595) | recastnavigation master 9f4ce64 (v1.6.0-367), byte-identical to the local checkout (high) | zlib | none |
| libogg (`lib/ogg`, e57334d0) | 1.3.6 (high) | BSD-3 | fork CMakeLists, hand-written `config_types.h` |
| libvorbis | 1.3.7 (high) | BSD-3 | fork CMakeLists (encoder dropped, area 1) |
| libtheora | 1.2.0 (high) | BSD-3 + On2 patent non-assert | fork CMakeLists |
| TheoraPlay | 2024+ snapshot, commit unknown (medium) | zlib | fork CMakeLists; the frame cap below |

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| V1 | P2 | `lib/ogg/include/ogg/config_types.h` | The fork's hand-written header was ignored by the vendored `.gitignore`, so `BuildTheora` failed on a fresh Linux clone (CI never builds it) | fixed: tracked |
| V2 | P2 | `lib/theoraplay/theoraplay.c` | Frames up to 99999 a side; buffers sized in int and unsigned (a 40000x30000 stream wrapped to a small allocation the conversion overran); videos come from any pk3 (BuildTheora only, upstream latent) | fixed: 8192 a side, marked |
| V3 | P3 | `g_navmesh.cpp` loader | Trusts the navmesh file (a negative tile size reads out of bounds; Detour does not validate tile contents); only with `bot_navmeshOff 0` | open |
| V4 | P3 | licenses | Detour had no license file; nothing lists the third-party components for a binary distribution; a stale CMake comment | fixed: Detour's `License.txt`, comment; notice list open |
| V5 | P3 | TheoraPlay | `assert(0)` on unsupported pixel formats aborts a debug client; a dead define | open (upstream) |

Versions: ogg 1.3.6, vorbis 1.3.7 and theora 1.2.0 are the latest releases known; the vorbis 1.3.7 CVEs are
encoder-side and the decoder ones were fixed in 1.3.6; Detour has no known CVEs. Removal candidates (no build file
references them; about 5.3 of the 5.8 MB): vorbis encoder sources, codebooks, `vq/`, VS projects and packaging; theora
VS projects, unbuilt asm and encoder sources; ogg's VS project; the Xiph autotools and CI files; Detour's uncalled
`DetourNavMeshBuilder`. Removed 2026-10-08 (owner: trim): 364 files, 4.4 MB, and the uncalled `DetourNavMeshBuilder`; kept the built sources, their headers and each library's notices (99 files, 1.2 MB); `lib/VENDORED.md` records sources and versions. Verified: the four libraries and the MP client build with `BuildTheora` on, and the game and dedicated server build.

## Area 8: fixtures, benches, meters, small kernels (reviewed 2026-10-08)

`bot2_movebench.c`, `bot2_movecost.c`, `bot2_corner.c`, `bot2_kick.c`, `bot2_wallprobe.c`, `bot2_servercost.cpp`,
`bot2_route_record.c`, `bot2_route_play.c`, `bot2_svcmds.c`, `bot2_probe.c`, `bot2_commands.c`, and the three
kernels no earlier area read (`bot2_mover.c`, `bot2_movecontrol.c`, `bot2_finecmd.c`), with their hooks and drivers;
three slices.

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| F1 | P1 | `bot2_movecost.c` start | `Bot2_MoveCostStart` lost its only caller when 411c7a48 removed `bot_flagrun`; `movement_cost_runner.py` still sent `bot_flagrun 0`, so every cost capture waited out its timeout with the bots held | fixed: `bot_movecost_start`, the runner sends it |
| F2 | P2 | `Bot2_UpdateManagedBots`, route play (O16) | Bot2_Think was reinstalled every frame over a fixture's idle think: route play's hold did not hold (the runner walked between `hold` and `arm`), and while a tape played Bot2_Think still ran the kill rules, planned toward the flag on the same client's movement state (a prefix walk re-planned twice a frame, each a reset) and wrote the runner's command into the play log. The bench and cost fixtures never met it (their bots are stock `alora`s, or `addrunner` clears the hold) | fixed: installed only on a bot with no think; route play's idle drains reliable commands and keeps the play log row `route_approve`'s pin reads |
| F3 | P2 | `bot2_mover.c` rest memo | Keyed on level time alone, and rollouts and pre-searches move level time (and the frame number) through simulated frames: an answer a rollout wrote at a simulated time was returned to the real frame at that time, so a lift called meanwhile read as floor | fixed: keyed on a real-frame counter (`Bot2_MoverFrame` in `G_RunFrame`) as well |
| F4 | P2 | `bot2_mover.c` lift calls | A trigger counted as a lift call whatever Touch_Multi's gates: a use- or fire-button call (a bot never presses it), NPC-only or named, one team, head-in-volume, siege, deactivated. None of the 12 maps has one; ctf4's four FACING calls are kept (a rider facing the lift fires it) | fixed; FACING open (the ride does not aim for it) |
| F5 | P3 | `RestUntil` | A pending delayed fire aimed at a team slave's targetname was not seen (CallsPrepare resolves through the master) | fixed |
| F6 | P3 | `bot2_forecast.c` `FallDamage` | Read every event since the step's start without the ring's `MAX_PS_EVENTS` limit; a step raising more than four reread overwritten slots | fixed |
| F7 | P3 | `bot2_kick.c` | A timed kick fired before the body checks, so on a dead or respawning body | fixed |
| F8 | P3 | `bot2_route_play.c` | `load` over a running tape wiped it with no DONE; DONE printed only with `bot_telemetry` 1; a name over ~47 characters lost `.tape`; a human taking the armed bot's slot was driven by the tape; the shutdown hook had no caller (a map change ended a run with no DONE) | fixed: refused, always printed, refused, finished, called from `G_ShutdownGame` |
| F9 | P3 | `bot2_commands.c` `navcheck` | Two temp entities a waypoint, up to 512 waypoints (cheats only) | fixed: capped as `navdraw` |
| F10 | P3 | `bot2_movecontrol.c` | 0/0 in the strafe-skill branch at zero acceleration (no caller passes it) | fixed: guarded |
| F11 | P3 | `bot2_movecost.c` metadata | Sections 0-3 have no writer since the flight adapter's removal (always 0) and the metadata still names it | fixed: retired columns declared, comments current |
| F12 | P3 | `Bot2_FrameMeterReset` | Does not clear the cost sections `bot_moveProfile` prints, so `[PROFILE]` counts from before the release `[FRAMECOST]` counts from | fixed |
| F13 | P3 | `bot2_movebench.c` hold | `bot_movebench_hold` is read once at the first client run; `addrunner`'s 0 cannot clear a world started at 1 | fixed: read once a frame; the hold's end gives the think back |
| F14 | P3 | `botat` | Due commands run at the next command-buffer pass, so a catch-up of several game frames in one engine frame (a stall) lands them later in game time; quoting is lost when words are re-joined | fixed as far as the game can: the level is validated, quoting kept, and each command reports the level it ran at (`AT_RAN late=`), which `defense_laps.py` counts (`setup_late_commands`); running them inside the frame would re-enter the game |
| F15 | P3 | `bot2_finecmd.c` climb entry | Uses the frame-start yaw and position where native checks after the view update, and its extra trace can refuse a rollout on a body Pmove never touches; live and forecast agree (no parity break) | fixed: the command's yaw, the frame's reach, the rage-recovery gate (a superset of native; a sliced frame with no climb is a 125 fps client's) |
| F17 | P3 | `route_approve.py` pin | Its rcon-timed class pin reads a 250 ms play-log ladder whose phase is set by when rows start, so a class off that phase is never offered (3 of 6 plain runs missed, 1 of 4 on 2026-09-21); `movement_bench.py` moved to the game-timed `bot_cornerDefer` for this | fixed: a class pin is the game's (`bot_cornerDefer`) |
| F16 | P3 | small | Play-log row state not cleared on slot reuse; a capture whose client leaves never finishes (cost); `servercost` overflow never reset; `wallprobe` reads its cvar every boundary; `strafe_fixture_runner.py`'s run path sends removed commands (area 10) | fixed: play-log rows per occupant, a capture whose client leaves or a map change ends it, overflow reset, wall probe read once a frame when empty, the dead runner path refuses |

Measured (`.codex-research/review8-20261008-a/`): laps worlds on `review8-build-20261008-a` against the area 7 worlds (which the classic removal replays): Dantooine seeds 1-8 and Kejim 1-16 replay exactly but Kejim 11, which differs from 276 s because F6 counts a step's fall damage once (the control's candidate was refused as lethal fall damage from reread events, the fix's rolls out and falls); each build replays its own Kejim 11; team captures +0.00 on both maps. The first batch of the new build stalled at map load (`fix-void-stall`, kept) and was rerun. The cost fixture runs again (4 bots, 610 moving frames, `cost-4`). Route play (`approve/`, `review8-build-20261008-b`, which moves the held bot's play-log row into the game frame): a prefix run and a pinned plain run play the 2026-09-21 asset as its approvals did (tape end at 15,900-15,925 ms, max drift 31.839), with one objective plan in the prefix walk and the think handed back at release; the area 7 build cannot stage (its sources no longer match), so there is no before world.

The open items F11-F17, then (`.codex-research/review8b-20261008-a/`, `review8-build-20261008-c` against the area 8 worlds): Kejim seeds 1-16 equal on every metric; Dantooine team captures 5.18 -> 5.18 (-0.00 +- 0.63), runner falls 4.07 -> 2.47 (-1.60 +- 0.94, undecided), the climb entry's change (F15); no crash, no late setup command. The cost capture completes (`cost-4`); route approval's three plain worlds all land on class 125 (level 4325) and play alike, and the prefix world as before (`approve/`).

Checked clean (condensed): every CSV and telemetry line against its header and arguments (play log 44 columns,
recorder, cost trace 40, kick, wall probe, corner); console and client command bounds; cheat gating of the human
client commands (`route_record` arms only with `bot_routeRecord` and cheats, `nav*` are CMD_CHEAT); file handles
closed on every path; tape name traversal; every fixture inert when its cvar is unset; Bot2_Pmove slicing against a
125 fps client (timers once per slice, final commandTime and serverTime native, live and forecast slice alike); mover
states, trajectories and delays against `g_mover.c`; the command controller's friction and acceleration constants.

## Area 7: objective, integration, older controllers (reviewed 2026-10-08)

`ai_bot2.c`, `ai_bot2_objective.c`, `ai_bot2_movement.c`, `ai_bot2_wallrun.c`, `ai_bot2_combat.c`,
`ai_bot2_localnav.c`, `ai_bot2_plan.c`, their headers and the fork's Bot2 hunks in `ai_main.c`, `g_active.c`,
`g_main.c`, `g_svcmds.c`, `g_utils.c`, `g_client.c` (5,900 lines after area 3; `ai_bot2_pmove.c` was area 5), read in
three slices. Reach: the off-mesh states in `ai_bot2_movement.c` run only on frames `Move_Command` declines (alive
and not `PM_NORMAL`, a Force grip's `PM_FLOAT` the common case, and until they return to WALK); `ai_bot2_wallrun.c`
only from the `bot_wallrun_check` and `bot_scan_wallruns*` commands; the macro AI of `Bot2_Think` drives `_v2` bots
that are neither the runner nor a defender (`bot_movementOnly` 0, the default).

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| O1 | P2 | `ai_bot2.c` trip-mine plant | `myFlag` dereferenced unchecked: a base-role `_v2` bot holding mines on a map with no flag (FFA, `bot_forcebot2`) crashes the server | fixed, then removed with the classic Bot2 (below) |
| O2 | P2 | `Bot2_PrintTelemetry` | The formatted text went back to `Com_Printf` as a format; combat telemetry (`bot_telemetry` 2) carries the target's name, and `%` survives name cleaning | fixed: printed as `%s` |
| O3 | P2 | `bot_test_routing` | Client number unchecked: out-of-range writes and `G_Kill` of an empty slot | fixed, then removed with the classic Bot2 (below) |
| O4 | P2 | `ai_bot2.c` `MACRO_HUNT_TRIPMINES` | No goal is set for it, so since the rewrite `Move_Plan` planned the zero vector and walked the bot toward the map origin; the stand-and-shoot block sat behind `Move_Command` and never ran | fixed, then removed with the classic Bot2 (below) |
| O5 | P2 | `ai_bot2_movement.c` stuck recovery | The stuck window counts only off-mesh frames: the first one after Move-owned frames (each grip) judged the whole gap as a stuck second, so two short grips ran the escape scan (8 Pmove simulations) and could hold W+jump on a fixed yaw for 3 s | fixed, then removed with the classic Bot2 (below) |
| O6 | P2 | `ai_bot2_wallrun.c`, `ai_bot2_pmove.c` simulations | Game Pmove on scratch states spawned real landing dust and splash entities (the forecast's suppression covered only its rollouts); the wallrun scanner ran thousands in one frame and could exhaust the entity table on dirt or sand | fixed, then removed with the simulations (below) |
| O7 | P3 | `ai_bot2_objective.c` absorb mode 1 | The Speed projection added regeneration while Speed is up (native Force regenerates with no power active) and the Force need was level 3's cost of 10. Absorb is off by default | fixed |
| O8 | P3 | `RunnerMineThreat` | Every enemy mine on the map took one of 16 slots, so past 16 distant unseen mines evicted seen ones ("seen approaching" lost) | fixed: only a seen mine takes a slot |
| O9 | P3 | `ReachableDrop` | A query denied by the one-per-tick budget returned unreachable, flipping the runner's goal to the stand for a frame whenever two drop checks met (two runners) | fixed: an expired check keeps its last answer until its turn |
| O10 | P3 | absorb exposure, protect | `exposed` (absorb mode 1's test) stayed set after the mode changed and blocked protect until death; the protect comments described a miss-distance test the code does not do (it is a 60 degree cone within 600 u) and called mode 1 the default | fixed: cleared off mode 1; comments corrected |
| O11 | P3 | `CheckForTriggerHurt` | A trace for triggers stops at the first: a segment starting inside a non-hurt trigger moved 1 u an iteration and gave up 32 u down, missing a `trigger_hurt` below (carrier melee's floor gate, the legacy simulations) | fixed: an entity-box contact test |
| O12 | P3 | `ai_bot2_wallrun.c` seed | The template playerState path kept the live weapon and Force jump (valid walls failed off-saber), saber lock, thrown saber and vehicle; both paths stood on client 0 (`groundEntityNum` 0). The C4047/C4024 warnings were `vec3_t *` passed for `float *` (same address, no runtime effect) | fixed, then removed with the classic Bot2 (below) |
| O13 | P3 | `ai_bot2.c` role tag | The 2 s rename gate read `abilityTimer`, which only the base heal sets: a name too long for the tag was renamed every frame | fixed, then removed with the classic Bot2 (below) |
| O14 | P3 | `Bot2_GetLeadOrigin` | NULL target dereferenced in its own guard (every caller checks first) | fixed, then removed with the classic Bot2 (below) |
| O15 | P2 | macro AI aim under Move | `Move_Command` writes the view angles and never sees `lockAim`/`aimDir`, so `_v2` macro bots fire along their movement heading; the chase bot's `hasFallback` retarget is dead the same way. Re-aiming means rotating Move's strafe commands (the defenders' raw command does this) | closed: the classic macro AI removed (owner, below) |
| O16 | P2 | `Bot2_UpdateManagedBots` | Reinstalls `Bot2_Think` on any `_v2` bot whose think differs, every frame: route play's hold (`route_approve.py` holds Runner_v2) is undone a frame later and its prefix walk shares the client's movement state with `Bot2_Think`; the bench and cost fixtures' idle thinks too. Changing it changes those fixtures | fixed in area 8 (F2) |
| O17 | P3 | `StrandedKill` | `since[]` is not cleared on disconnect: a bot reusing the slot can be killed at its first stranded check | open (latent) |
| O18 | P3 | legacy wallrun state | Writes the live Force jump to 3 and never restores it; `savedWeapon` survives death. The state needs an off-mesh link under `PM_FLOAT` | removed with the classic Bot2 (below) |
| O19 | P3 | `bot_telemetry` | `CVAR_ARCHIVE`: the 1 the runner tools set is saved into later sessions | open |
| O20 | P3 | `Bot2_GetLeadOrigin` | Alt fire read from the previous command (the first repeater or flechette alt shot is led at primary speed); gravity fixed at 800 | removed with the classic Bot2 (below) |

Measured (`.codex-research/review7-20261008-a/`, O1-O14 together, `review7-build-20261008-a` against the area 6
worlds on `review6-build-20261008-d`, paired by seed): team captures Dantooine 5.24 -> 5.18 (-0.06 +- 0.55, 8 seeds),
Kejim 3.98 -> 3.94 (-0.03 +- 0.40, 16); every other metric within noise; no crash. Dantooine seed 1 replays the
control exactly; seeds 2-3 diverge at the runner's goal (O9's dropped-flag answer).

**Classic Bot2 removed** (owner, 2026-10-08: keep the latest bot in its offense, base and chase applications and
any general one; scrap the classic Bot2). Gone: the `Bot2_Think` macro AI (roles, macro states, item and weapon
hunting, trip mines, combat aim, role-tag renames, the `bot_test_routing` harness), the off-mesh state machine of
`ai_bot2_movement.c` (a declined frame now stands still), `ai_bot2_pmove.c`, `ai_bot2_combat.c`, `ai_bot2_wallrun.c`,
`ai_bot2_plan.c`, the `addbot2enemy`, `bot_wallrun_check` and `bot_scan_wallruns*` commands, the cvars
`bot_forcerole`, `bot_forceweapon` and `bot_jumpPlanMode`, and every `bot2_state_t` field but the runner's objective and
the jump pad's. `Bot2_UpdateManagedBots` installs Bot2 only on a runner or defender; any other bot keeps the stock AI.
No general application of the new bot existed outside mode 2 (every tool that uses it runs `addrunner` with runner and
defender names). Reach before removal: none of it ran in 128 retained laps worlds (CSV `movement_owner` legacy 0 of
476,860 samples; no classic prints). Verified (`.codex-research/classic-removal-20261008-a/`, `classic-removal-build-20261008-a` against the area 7 worlds): Dantooine laps seeds 1-4 replay exactly, game logs and every decision line; the first launch of the new build stalled at map load (`fix-void-stall`, kept) and the rerun was clean. Tools left for area 10: `bot_test_runner.py` (a
mode-0 classic capture test) and `defense_laps.py --vs-legacy` (its `Legacy<n>_v2` opponents are now stock bots).

Checked clean (condensed): flag and team mapping in the carry, camp and drop paths; NULL flags on non-CTF maps;
entity reuse in the drop and mine caches; per-client state reset (`Bot2_ClearState` every dead frame; game statics
reset on map change because the DLL reloads); every `[MAX_CLIENTS]` index; format strings of the three slices;
Force cost tables against `bg_pmove.c`; `Bot2_GameStepMsec` against the engine frame; the custom-think dispatch, the
mode-2 single decision a frame and its `playDispatch` gate; shutdown order; `ai_bot2_plan.c` resets and caps.

Cleanup candidates: `Bot2_GroundSample`'s ascent/drop refusals (unreachable inside the floor trace's window, so the
graph's retries on them too) and the unread `corridorHit`; `isHardStrafe` (strafe direction is only +-1); the
dead `hasFallback` path; `spawnCooldown` (written, never read); `Bot2_ScanWallrunsHeadless` duplicating
`Bot2_ScanWallruns`; the C4133 cast at `ai_bot2.c` `LinkEntity`; stale texts (a 300 u message testing 400, a
"contents" label printing surface flags, the stuck threshold's 50 ms calibration, `pos2` called the top stop,
`state = 4`, `ai_bot2.h`'s "5 runs").

## Area 6: defenders, aim, threat (reviewed 2026-10-08)

`bot2_defender.c` (3,241 lines), `bot2_aim.c`, `bot2_threat.c` and their headers, read in three slices.

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| D1 | P2 | `bot2_threat.c` `Scan` | The missile scan ran once a frame, before the client loop: a missile fired during the frame was unmarked for the rest of it, so every later hide (other bots' boundaries, the frame-end pool of M3) showed it at age 0 | fixed: every hide scans the slots; the setting is read once a frame |
| D2 | P2 | `bot2_defender.c` `RearmPlan` / chase rearm | The chase D's rearm-anywhere fallback (`bot_chaserRearmAny`, default on) is asked in the same frame as the evaluation along the chase route; that one took the 500 ms evaluation slot, so with nothing near the route the fallback never chose an item and the chase D chased unarmed (the 2026-10-02 b3 stall it was added for) | fixed: the fallback evaluates in the frame the bounded evaluation found nothing |
| D3 | P3 | `bot2_defender.c` `Follow` | A throw's outcome was scored on the current target: a carrier or lone candidate replacing it in the window made a hit a miss (no follow-up window) and read the wrong client's landing | fixed: scores the client thrown at; a departed one closes the window |
| D4 | P3 | `Svcmd_AddDefender_f` | A third defender of one role took the second's name, so two clients shared one state and one stood idle | fixed: refused |
| D5 | P3 | `bot_defenderWeapons` parse | "11, 8a" lost the alt flag after a space | fixed |
| D6 | P3 | `bot2_aim.c` arc aim | A steep lob's pitch-up could pass the vertical (100 degrees) | fixed: clamped at 89 |
| D7 | P3 | `Shoot` | `bot_defenderChargeMs` (off by default; documented for the pistol) holds every alt-fire, including ones that never charge | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): the hold only for alt-fires that charge; inert by default |
| D8 | P3 | `RearmBest` armed top-up | With only a teammate-claimed candidate in range, a route query every frame instead of every 500 ms | fixed, measured undecided (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): the claimed-item verdict kept for 500 ms; measured with M10/M13 (build kreplay-c, 2026-10-09): Kejim 1-16 no effect (game logs identical), Dantooine 1-8 undecided (team caps -0.43 +- 0.41, runner falls 0.00 +- 0.67), kept as a correctness fix |
| D9 | P3 | perception history | 64 entries cover the 1,000 ms reaction cap only at 25 ms frames (laps worlds run sv_fps 40) | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): 256 frames; identical at 25 ms frames (Dantooine 1-4 replay) |
| D10 | P3 | `Mate`, `Bot2_DefenderHops`, `Bot2_DefenderRelease` | Slot state not checked against a reused client number; a defender dead at the release keeps the last release's state | fixed (branch defender/flag-avoid-20261009, [record](../movement/records/2026-10-09-flag-avoid.md)): `Mate` and `DefenderHops` check the slot's client (review/kejim-replay-20261009); a defender dead at the release is cleared and released at its post when it respawns (`RELEASE late=1`, `bot_defenderLateRelease`, default 1). Forced case: armed 950 ms after the release (old: never armed, 0 routes in 240 s); ordinary worlds identical |
| D11 | P3 | `bot2_threat.c` slot reuse | In a level's first 2 s a freed missile slot can be reused within a frame and inherit the old age | fixed (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): a slot freed after first sight is a new missile (`freetime`); laps worlds fire nothing in the first 2 s |

Measured (`.codex-research/review6-20261008-a/`, D1-D6 together, `review6-build-20261008-d` against the area 5 worlds
on `review5-build-20261008-a`, paired by seed): team captures Dantooine 5.24 -> 5.24 (-0.00 +- 0.47, 8 seeds), Kejim
3.39 -> 3.98 (+0.59 +- 0.35, 16, undecided; all captures +1.30 +- 0.71); falls and deaths within noise; no crash.
Builds a and b of that name failed or were superseded during a quoting slip (kept, suffixed).

`RearmObserve` learning only while the planner runs is the documented "unseen is unknown" design (not a finding).
Checked clean (condensed): every format string in the three slices; aim projectile speeds, splash radii and the
flechette alt spread against `g_weapon.c`; angle wraps and zero-distance guards; hide/relink pairing at all four call
sites; Force throw velocity and counter rules against `w_force.c`; no `rand()` or wall clock in decisions.

## Area 5: move, forecast, pmove (reviewed 2026-10-08)

`bot2_move.c` (7,919 lines), `bot2_move.h`, `bot2_forecast.c/h`, `ai_bot2_pmove.c`, with the boarding check in
`bg_pmove.c`; read in six slices, each candidate checked against the code and, where it could be, the logs of the
Dantooine laps worlds (`review4-20261008-a/review4-rerun`) or the 12 maps' entity lumps.

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| M1 | P2 | `bg_pmove.c` landing on a vehicle | A simulated move boards for real: a forecast rollout (entity copy as `pm_entSelf`) or the legacy and wallrun simulations (live `g_entities`, scratch playerState) landing on an empty swoop or animal call the live `Board()`; the legacy ones board the live bot, the forecast leaves the live vehicle a pilot pointer into its scratch copy. No prepared map spawns a vehicle | fixed: only a move of an entity's own live playerState boards |
| M2 | P2 | `bot2_forecast.c` rollout root in a saber lock | `PM_SaberLocked` finds the opponent through the entity copy, whose playerState pointer is the opponent's live one, and the lock break writes its velocity, timers and animation | fixed: a root in a lock is refused (it runs nothing) |
| M3 | P2 | `bot2_move.c` `Move_PoolFrameEnd` | The frame-end pool (default on, `bot_moveWalkPoolFrameEnd` 700) runs deferred chunks without the missile reaction-delay hide the boundary applies, so their rollouts see missiles younger than `bot_threatReactMs` and the take judges a different world | fixed: hidden as at the boundary |
| M4 | P2 | `bot2_forecast.c` replay key | Replays are not keyed on the flag pass: steps recorded by an arrival's flag-pass continuation can be replayed into a rollout without it, and replayed steps test no trigger, so a flag refusal is missed | fixed: the flag pass is in the key |
| M5 | P3 | `bot2_move.c` walk-defer rescue | A rescue judges only the deferred winner; with a planned strafe edge at the takeoff it then charges that edge stale or refused though none of its candidates was judged. Rescues: 0-3 a Dantooine world | fixed: not charged on a rescue |
| M6 | P3 | `bot2_move.c` cache validation | `SearchCacheValidate` (both callers) reads `straightRootIndex`, which only the judge sets: on a cached take it holds the last judge's, another bot's in a busy world (off-path refusals, other reroute joins) | fixed: set for the root before validating |
| M7 | P3 | `ai_bot2_pmove.c`, `ai_bot2_wallrun.c` | The legacy and wallrun simulations trace `MASK_PLAYERSOLID`; a bot's own move adds `CONTENTS_MONSTERCLIP` | fixed: the bot's mask |
| M8 | P3 | `bot2_move.c` `PadObserve`, `RerouteJoins` | `Graph_Edge`/`Graph_Cell` NULL not checked (an index a fixture's learned-set reset dropped) | fixed: guarded |
| M9 | P2? | `bot2_move.c` `PlannedEligible` / `ClimbSearch` | A climb edge ahead takes every grounded frame from 400 u to 96 u of its takeoff (the climb search returns before setting its retry), so gap and straight hops do not run on the approach; Dantooine worlds commit 800-1,240 climbs. Changing it changes the approach to every climb | removed (owner, 2026-10-09: "remove what lost"; it lost on Dantooine captures): `bot_moveClimbApproach` and its code taken out on branch defender/flag-avoid-20261009 (default was 0, inert; identity replay in [record](../movement/records/2026-10-09-flag-avoid.md)); measurement kept in the [record](../movement/records/2026-10-09-kejim-replay.md) |
| M10 | P3 | `bot2_move.c` `WalkStep` corridor fallback | A refused corner chord keeps the corner's stop for the cell targets, which can drop the edge braking clamp for that frame | fixed, measured undecided (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): a refused corner keeps the path's stop; measured with M13/D8 (build kreplay-c, 2026-10-09): Kejim 16 worlds no effect (8 of 16 decision streams identical, game logs all identical), Dantooine 8 undecided (team caps -0.43 +- 0.41) |
| M11 | P3 | `bot2_move.c` `PlanBodies` | The defender keep-out lattice (9 bodies) is added only when fewer than 8 bodies are near (16 kept) and not at all with `bot_moveFailCause` 0 | fixed (owner: "using bodies to decide the route is hacky"; branch defender/flag-avoid-20261009, [record](../movement/records/2026-10-09-flag-avoid.md)): the keep-out box is a forbidden volume of the defender's rollouts, plans, reroutes and walk target (`bot_defenderFlagKeepOutRoute` 1, no body slots, no cap, independent of `bot_moveFailCause`); the lattice (mode 2) was removed after the merge (owner, 2026-10-09). Routes entering the box 13-19 -> 0-0.7 a world; laps worlds undecided (Dantooine team caps -0.86 +- 0.49, Kejim +0.31 +- 0.42); work counts inside noise |
| M12 | P3 | `bot2_move.c` `LiftRide` | The rest position is taken as POS1; the graph builds lifts sampled at POS2 too. All lift cells of the 5 lift maps are POS1 | open (latent) |
| M13 | P3 | `bot2_move.c` drop deferral | A drop deferral left when the bot leaves the edge (`deferred` 2) blocks the early pre-search until a walk deferral clears it: timing, not decisions | fixed, measured undecided (branch review/kejim-replay-20261009, [record](../movement/records/2026-10-09-kejim-replay.md)): a drop deferral past its take frame ends; measured with M10/D8 (build kreplay-c, 2026-10-09): Kejim no effect, Dantooine undecided, hops -8.9 +- 5.1, frames over 25 ms -48 +- 38 |
| M14 | P3 | `bot2_move.c` `Move_Reset` | Any client's reset prints the decision-tape census and closes it (a defender respawn in a Kejim laps world) | open (diagnostic) |
| M15 | P3 | `bot2_move.c` tape library | Paths are cut at 64 characters with no error; with the Kejim library directory a tape name has 23 | open (latent) |

Measured (`.codex-research/review5-20261008-a/`, M1-M8 together, `review5-build-20261008-a` against the
`review4-build-20261008-a` control, laps worlds paired by seed, `tools/laps_compare.py`):

| Map | Seeds | team captures | all captures | runners' weapon deaths | runners' falls |
| --- | --- | --- | --- | --- | --- |
| Dantooine v3 | 8 | 4.13 -> 5.24, +1.11 +- 0.37 (effect) | +2.22 +- 0.67 | -3.33 +- 1.40 | +0.99 +- 0.88 |
| Kejim | 16 | 3.88 -> 3.39, -0.49 +- 0.36 (undecided) | -0.86 +- 0.69 | -0.24 +- 1.09 | -0.86 +- 1.07 |

No crash reports; every world distinct. Not attributed to single fixes; M3 (missile hide), M4 (flag-pass replay)
and M6 (root index) are the ones that can change a seeded world without a vehicle, a saber lock or monster clip.

Diagnostic only, open: the diverge line compares the live command with the prediction one frame earlier; any client
failing the search gates disarms the parity harness (humans, non-hopping defenders); `[REROUTE]` prints the last
reroute's route for a first-pass arrival; the shadow rescue reads the arrival continuation's rollout; a kept route
during a split search is marked `bodies`. Off by default, open: `SpikeSearch` commits without `CommitMeasure`;
`ExploreState` keeps the previous cell's path heading after a cell with no path. Latent: `KnockSearch` clears
`pending` before its guard check. Checked and declined: the stuck bail's 16 u reset (bails chain every 400 ms up to 9 in
the worlds, so the reset fires only on a real escape); guest teammate checks' cvar lookups (all cvar lookups are about
24 us a frame). Pre-existing `ai_bot2_wallrun.c` type warnings (`Bot2_SimulateWallrunScenarioEx` argument 11) go to
area 7.

Checked clean (the slices' lists, condensed): every format string in the six slices against its arguments; path,
avoid, candidate, result, horizon, ring and pool bounds; the splice's `GRAPH_MAX_PATH` check; split-search install
checks; the forecast's restore of level time, frame number, Pmove globals, the random stream and animations; its
native parity for generic commands, Force Speed, regeneration and fall damage; determinism of the pool budgets (step
counts, never the clock).

## Area 4: graph and planner (reviewed 2026-10-08)

`bot2_graph.c` and `bot2_graph.h` (about 3,450 lines; there is no `bot2_path.c`, the cell search, state search, route
tables, guidance and split searches all live in `bot2_graph.c`). No live defect found; three latent ones, fixed so
that no prepared-seed world changes (checked against the 13 prepared graphs and the Kejim seed, below).

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| G1 | P3 | `PathSearch` heap (`search.heap`, `search.key`) | Sized by cells, though a cell is pushed once per improvement of its g and the push has no bound check; an overflow would write past the heap into the keys. Measured peak (Python port, 300 random queries): 3,273 on the Kejim seed (95k edges), 696 on Talay, 532 on Manaan, against 32,768 | fixed: sized by the push bound (8 arcs a cell plus edges, plus the root; +3 MB static) |
| G2 | P3 | `Graph_LinkedWithin` | No visited set and a 64-entry frontier: exact to 4 links, partial beyond. Only the pad own-floor test asks for 8 (build only); on 20,000 random nearby pairs a map the old answer missed 725-892 reachable pairs at 8 steps and none at 2 or 4. No pad edge in the 13 prepared graphs or the Kejim seed depends on it | fixed: breadth first, each cell once; answers at 2 and 4 steps (the live callers) unchanged |
| G3 | P3 | `Pads` and `Graph_PadReachable` push target | Discovery took `G_PickTarget` (one `rand()` per `trigger_push` at every graph build, moving a building world's random stream) while the reach check took the first target; a push with two targets would be aimed at one the game may not throw at. No prepared map has a push with two targets (entity lumps of all 12 BSPs) | fixed: both take the first target, without `rand()` |

Checked clean: walk links are symmetric on all 13 prepared graphs and the Kejim seed (the reverse floods of
`Graph_Reaches`, the route tables and the guidance rely on it); learned hop edges do not repeat in the Dantooine laps
worlds (the learner does not merge them, so a repeat would add an edge); the fixture's learned-set reset never passed
its 256 restored edges in 336 resets; the route tables' lift signature (a sum over standing lift cells) has no
colliding lift states on the five lift maps; route-table queries are about 12 a second in a Dantooine laps world;
the batched route-table fold (tight arcs under the costs before the batch, repair, then the cheaper edges) re-derived;
joins are set only around whole reroute searches, so the global join state never meets a split search; the
enclosed-goal test (`Graph_BodiesEncloseGoal`) matches the searches' admission exactly; `StateWalkOf` cannot divide by
zero; the route-table heaps are sized to their push bounds; the state search's heap and table report overflow.

Verification (`.codex-research/review4-20261008-a/`, build `review4-build-20261008-a`): Dantooine laps seeds 1-4
replay the defaults control (`legacy-removal-20261007-a/defaults`, same `bot2_graph.c` before the fixes) exactly, game
logs and every decision line (139,004-145,727 a world). The first arm is kept and void: all four servers stalled
17.7 s at map load on the build's first launch (11 readiness probes against the control's 1), the fixed-time setup
then ran in one burst and play broke; the same build rerun at once started normally. Python ports of the cell search
and `Graph_LinkedWithin` gave the measurements above.

Cleanup candidates: `StateAvoided`'s unused list arguments, threaded through `StateStrafeEdge` and `StateStrafeEdges`;
the redundant `strchr` in `Graph_CorridorLoad`'s name check; the off-default split searches (four 11 MB tables) and
planner guidance (about 34 MB of arcs and tables) hold about 78 MB of the game DLL's 278 MB of static data, kept while
the owner has them undecided. Done with the fixes: the header's description (native edges are the default, climbs,
lifts, the three searches), the split-search comment moved back to its declarations, a statement ahead of
declarations in `Clearance`.

## Area 2: cgame (reviewed 2026-10-07)

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| C1 | P1 | `cg_players.c` enemy brightskins (after the cloak handling in `CG_Player`) | With `cg_enemyColors` set, an extra solid, opaque white-shader pass is drawn for every enemy with no cloak check: a fully cloaked enemy shows as a coloured silhouette, and translucent models turn solid | fixed: the pass copies the model as drawn (cloak, LOD, alpha); `RF_G2MINLOD` is no longer set on it (it never applied to the model itself, see cleanup) |
| C2 | P2 | `cg_players.c` `CG_NewClientInfo` (`cg_enemyModel`) | Enemy is decided from the local team only when that player's configstring is parsed; clients parsed before the local one at load, and everyone after a team change, keep the wrong model until their userinfo changes; spectators see every player as an enemy; brightskins judge teams by `cg.snap->ps.clientNum` instead | fixed: one enemy test against the view (followed player's side), and every model is re-picked when that side changes (`CG_CheckEnemyModels`); load order was already right (own info is parsed first) |
| C3 | P2 | `cg_aimfeedback.c` `CG_ShutdownAimFeedback` / `CG_InitAimFeedback` | Always writes `timescale 1` and `cl_freezeDemo 0`, feature on or off: "timescale can only be set by server" on remote servers at every map change, and a local `timescale` (sv_cheats) reset to 1 | fixed: the lab writes them only while one of its modes is on, and shutdown restores only what it changed |
| C4 | P2 | `g_combat.c` damage (`dmg` server command) | One reliable `dmg` command per damage application to every attacker, whatever its `cg_scorePlums`: stock clients print "Unknown client game command" per hit, and flechette/saber bursts can overflow a lagging client's reliable commands | fixed: sent only to a client whose `cg_scorePlums` (now userinfo) is 2+ |
| C5 | P3 | `cg_localents.c` score plums | Negative plums drawn white without a minus: a suicide's -1 looks like +1; `negative` unused | open |
| C6 | P3 | `cg_players.c` `CG_ParseColorString` | `sscanf` result unchecked (`#zz` gives an uninitialised colour), `#f00` read as `0x000f00`; parsed per enemy per frame | fixed: checked parse, `#rgb`, parsed once a change |
| C7 | P3 | `cg_draw.c` centerprint, disconnect icon | `cg_aimFeedbackLesson` non-zero hides all centerprints in live games, not only demos | open |
| C8 | P3 | `cg_aimfeedback.c` re-init on time going back | Re-init zeroes `af_fcTrackFile` without closing it: leaked handle, truncated timeline | fixed: re-init closes the open file |
| C9 | P3 | `cg_aimfeedback.c` cache readers | `targetNum` / `carrierNum` from `.afr`/`.aft` files used as indices unchecked | open |
| C10 | P3 | `cg_conctrace_visual.c` | `EV_CONC_ALT_IMPACT` not filtered by owner: another player's shot replaces yours | open |
| C11 | P3 policy | `cg_xcvar.h` `cg_soundIndicators` default 1 | Gameplay-visible default (enemy sound markers through walls, even with `cg_footsteps 0`); owner to confirm the default | fixed: default 0, parked (owner) |
| C12 | P3 | `cg_aimfeedback.c` statics | About 6 MB of static arrays in every cgame (`af_fcTrack` 5.5 MB), used or not | open |

Cleanup candidates: the aim-feedback demo lab (~4k lines with one demo's timestamps, fixed screenshot names, a
cgame-issued `quit`, cgame-driven `timescale`) behind a build flag or out of the shipped cgame; the conc-trace lab panel
(off by default, hooked every frame); `RF_G2MINLOD` set after the main model was added (no effect without
`cg_enemyColors`); sound-indicator legacy `#define` aliases; `cg_scorePlums` 2+ undocumented; an encoding-only comment
change in `cg_effects.c`, a stray blank line in `cg_ents.c`.

## Area 1: engine, renderers, client, server (reviewed 2026-10-07)

| # | Sev | Where | Defect | Status |
| --- | --- | --- | --- | --- |
| E1 | P2 | `cl_cin_theora.cpp` end of stream, `cl_cin.cpp` `CIN_StopCinematic` | A Theora video started with `CIN_system` never runs RoQ's shutdown (`cls.state` stays `CA_CINEMATIC`, no `nextmap`, `CL_handle` kept): an intro `.ogv` freezes on its last frame until a key; open skips `Con_Close` and `s_rawend` | fixed: end and stop share a RoQ-style shutdown (leaves CA_CINEMATIC, runs nextmap, clears CL_handle); open closes the console and starts the raw stream |
| E2 | P3 | `cl_cin_theora.cpp` open failure | `THEORAPLAY_startDecode` closes the io on every failure and the caller closes it again: double `Z_Free`; `fileBuffer` leaked | fixed: one file copy per handle with a non-owning reader; a failed start frees the file and never re-closes the reader |
| E3 | P2 | `rd-vanilla/tr_shade.cpp` `ProjectDlightTexture2` and the second dlight path | `SHADER_MAX_VERTEXES` 10000 puts ~610 KB / ~370 KB arrays on the stack per call against Windows' default 1 MB main-thread stack (no `/STACK`); now also reached under `r_vertexLight` | fixed: the arrays are static (single-threaded renderer) |
| E4 | P3 | `cl_cin_theora.cpp` upload | Every bind re-uploads the whole frame (`dirty` always true), once per surface per frame; Draw uploads twice | fixed: uploads only a new frame; Draw lets StretchRaw upload |
| E5 | P3 | `cl_cin.cpp` Theora open | No handle reuse for shaders naming the same video (a decoder thread and two whole-file copies each; over 16 drops), `cl_inGameVideo` ignored | fixed: shaders naming a playing video share its handle; cl_inGameVideo 0 shows the first frame only |
| E6 | P3 | `lib/theoraplay/CMakeLists.txt` | pthreads used without linking `Threads::Threads` (link errors on older glibc) | fixed: Threads::Threads linked off Windows; SDL comment corrected |
| E7 | P3 | `client.h` `CON_TEXTSIZE 0x1000000` | 32 MB console text plus a 32 MB static resize buffer; clear and resize walk 16M cells | fixed: 2M cells (owner) |
| E8 | P3 | `tr_types.h` `skin_t.surfaces[1280]` | Up to ~20 MB more hunk for 1,024 skins, dedicated servers included | open: exact-size allocation during the renderer work (owner) |

Checked clean: shared header changes keep module compatibility (`RF_G2MINLOD` 0x400000 is free; `skin_t` stays inside the
renderers); vanilla/rend2 parity for picmip-world-only, `RF_G2MINLOD`, non-power-of-two `StretchRaw` and vertex-light
dlights; no network-facing server change; ogg 1.3.6, vorbis 1.3.7 and theora 1.2.0 look unmodified (build glue only).
Content above 1,000 vertices a surface loads here but drops stock clients.

Cleanup candidates: the unused outer `whichLod` in vanilla `tr_ghoul2.cpp` (both renderers can compute the LOD bias once);
the duplicated Theora decoder setup and per-loop whole-file copy; the redundant `CIN_THEORA_CloseAll` and
`THEORA_MAX_HANDLES`; the duplicated `CL_PlayCinematic_f` wait loop; `vorbisenc`/`vorbisfile` built but unused; the
per-batch `qglColor4ub` in vanilla `RB_StageIteratorGeneric` (fix the 2D state leak at its source).

## Area 3: lost experiments and legacy code (removed 2026-10-07)

Owner: remove what lost; keep anything undecided. Removed:

- **Lost switches** (each closed in HANDOFF's Closed list or a record): `bot_moveReroutePrune` (with the graph's price-floor
  tables), `bot_moveRerouteJoinValid`, `bot_moveGuestRunupHold`, `bot_forecastBodyMs`, `bot_moveHeadingOffer`,
  `bot_moveChainAdmit`, `bot_moveLandReplan`, `bot_moveWalkThrough`, `bot_moveTapOnward`, `bot_moveTapBudget`,
  `bot_moveStaleAvoid`, `bot_moveSuccessorAim`, `bot_moveSuccessorFan`, `bot_moveHeadingSweep`, `bot_moveFollowThrough`,
  `bot_moveReroute` bits 2 and 4 (bit 1 stays); `bot_baseSupport`, `bot_camperDisplace`, `bot_runnerHandoff` (families),
  `bot_defenderSpentReset`, `bot_defenderFootprintGoal`, `bot_defenderBraceOnBody`.
- **Pre-rewrite runner stack and research benches:** 49 C files, about 10,000 lines (route, supported, flight, hop
  fallback, strafe/path/handoff/receiver/flight kernels, maneuver/transition/connection/map/flagrun/continuous/
  disturbance/flight benches, steering), most of `bot2_movebench.c` and `ai_bot2_localnav.c`; the rewrite is
  unconditional (`bot_moveRewrite` gone). 152 `strafe_*.py` tools, 45 of their tests and `route_ownership_probe.py`.
- **Kept** (undecided or a capability): `bot_runnerAbsorb`, `bot_movePlanSplit`, `bot_movePlanGuide`,
  `bot_moveSearchBudget`, `bot_defenderWalkErrands`, `bot_defenderPatience`, `bot_defenderRetarget`, `ai_bot2_wallrun.c`;
  the legacy 8-state machine, `ai_bot2_pmove.c` and `ai_bot2_plan.c` (the rewrite declines to them on some frames and on
  jump pads; all removed 2026-10-08 with the classic Bot2, area 7); the ground samples in `ai_bot2_localnav.c`; `strafe_fixture_runner`, `strafe_fixture_build`,
  `strafe_map_preparation` and a cut `strafe_cost_runner` (current tools import them).

Verification (`.codex-research/legacy-removal-20261007-a/`): the control (5b94b67c) and a repeat replay Dantooine laps
seeds 1-4 exactly; the switch removals and then the full removal replay the control exactly (game logs and every
decision line, about 140,000 a world), but for `footholds=` dropped from CHASE_END (always 0, no reader). The pinned
bench (Kejim and Dantooine, classes 50 and 150, 84 cases) stays green on every arm; it does not replay run to run, so
it is a safety check only. Python suite 549 tests OK.

Behaviour outside the tools: bots on a plain server (no PRODUCTION settings) now run the rewrite instead of the old
owner chain. Several production values are still not the code defaults (`bot_moveEdges`, `bot_moveExitSpeed`,
`bot_moveReroute` are 0 in code; tools set them): folding PRODUCTION into the code defaults is a follow-up.
