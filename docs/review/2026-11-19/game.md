# Game module (review 2026-11-19, master a322d9dc4)

Scope: `git diff 407a56fb master -- codemp/game` without `detour/` and `*bot2*` (70 files, 12,838 added lines), and
`git diff bc503e8f master -- 'codemp/game/*bot2*'` (12 files, +266 -91). Read only; nothing built or run.

- New files, read whole: `be_force.c/h` (1,461), `be_combat.c/h` (392), `be_pmove.c/h` (143), `be_collections.c/h`
  (214), `be_table.c/h` (491), `bg_bike.c/h` (1,178), `bg_bike_ct3x.h` (174), `bg_bikemove.c` (717), `BikeNPC.c` (114),
  `g_tick.c/h` (310), `g_saberlab.c/h` (648), `g_saberbench.c/h` (358), `g_hitcoverage.c/h` (332), `g_levelcheck.c`
  (182), `g_navmesh.cpp/h` (737), `g_crash_handler.c/h` (122).
- `be_stats.c` (3,892): state, identity, blocks, frame, hooks, output, commands, init and shutdown read whole; the
  table cell and help code (about lines 2093-2730 and 3252-3440) skimmed for bounds only.
- Stock files: every hunk of the 45 changed files read.
- Bot2 delta: every hunk read.

Files with no findings: `be_collections.c/h`, `be_table.c/h`, `be_pmove.c/h`, `be_combat.c/h`, `be_stats.c/h`
(see RG-20), `bg_bike.c`, `BikeNPC.c` (but headers, below), `g_tick.h`, `bg_local.h`, `bg_misc.c`, `bg_saber.c`,
`bg_vehicleLoad.c`, `bg_vehicles.h`, `g_ICARUScb.c`, `g_client.c`, `g_items.c` (but RG-13), `g_misc.c`, `g_mover.c`,
`g_public.h`, `g_saga.c`, `g_target.c`, `g_team.c`, `g_trigger.c`, `g_turret.c`, `g_turret_G2.c`, `g_vehicles.c`,
`g_weapon.c`, `g_xcvar.h`, `NPC.c`, `NPC_spawn.c`, `surfaceflags.h`, `w_force.c`, `w_saber.c`, `ai_bot2.h`,
`ai_bot2_localnav.c`, `bot2_graph.c`, `bot2_movecontrol.c/h`, `bot2_movecost.c/h`, `bot2_move.h`.

Client and server agreement in the shared `bg_*` code was checked for the movement styles (the style, the 1.28
speed in `basespeed`, the jump timer and `forceJumpZStart` all travel in the playerState), the base_enhanced
switches (configstring 25 tokens cover all eleven flags), and bikes (state in full-float vehicle fields, substeps on
an absolute 4 ms grid, the sleep path is server only). No disagreement found; RG-9 is the one exposure.

No P0 or P1.

## Findings

### RG-1 P2 `g_navmesh.cpp` includes `<windows.h>` on every platform
- Where: codemp/game/g_navmesh.cpp:25-28
- Scenario: any non-Windows build of the game module stops at this include. Nothing in the file uses a Windows
  API; the `#undef INFINITE` above it exists only to make the include compile.
- Fix: delete lines 25-28.
- Confidence: confirmed by reading (no Windows symbol is used); the build failure itself needs a non-Windows build.
- Status: fixed (d39cb0ecf): the include and the INFINITE guard removed.

### RG-2 P2 Every map load appends to `navmesh_crash.log` in the working directory
- Where: codemp/game/g_navmesh.cpp:59-74 (`NavMesh_Log`), :100, :110; also :120-121, :73
- Scenario: `NavMesh_InitForMap` runs at every `G_InitGame`. With the default `bot_navmeshOff` it logs two lines
  ("--- NavMesh Init Start ---", "INIT: disabled by bot_navmeshOff.") through `fopen("navmesh_crash.log","a")`,
  outside the engine's file system, on every server and listen host, forever. Two smaller defects in the same
  path: a failed `TrueMalloc` returns without closing the file (:120-121), and the text goes to `trap->Print` as the
  format string (:73).
- Fix: print through `trap->Print("%s", text)` only; drop the `fopen`; print nothing when the mesh is disabled;
  close `f` before the early return.
- Confidence: confirmed (traced)
- Status: fixed (d39cb0ecf): NavMesh_Log prints through `trap->Print("%s", ...)` only (no fopen); nothing is printed while the mesh is disabled; the file is closed when TrueMalloc fails.

### RG-3 P2 The crash handler reports any module's access violation as a game DLL crash, once, then goes quiet
- Where: codemp/game/g_crash_handler.c:39-84, :86-90; installed unconditionally at codemp/game/g_main.c:197
- Scenario: a vectored handler sees first-chance exceptions from the whole process, handled ones included (a
  driver or another DLL probing memory inside its own `__try`). The first one writes `jampgame_crash.dmp` and
  "FATAL GAME DLL CRASH DETECTED" to the working directory and sets `crashed`, so a later real game crash writes
  nothing. `crashed` also survives `map_restart`. It loads `dbghelp.dll` inside the handler, on stack overflow too.
- Fix: put the install behind a cvar that defaults off (or a build option); test that the fault address is inside
  this module (`GetModuleHandleEx` on `ExceptionAddress`) before writing; do not latch on a skipped exception.
- Confidence: confirmed (traced) for the scope and the latch; how often a foreign first-chance fault happens needs
  a run.
- Status: fixed (d39cb0ecf): the handler reports only faults whose address is inside the game module and does not latch on others; dbghelp is loaded at install, not in the handler. Install stays unconditional; deferred to the owner whether shipping builds keep it on (cvar or build option, default off).

### RG-4 P2 Every damage application spawns a score-plum entity, for bots and NPCs too, with no switch
- Where: codemp/game/g_combat.c:5405-5411 (`ScorePlum` at :437-447)
- Scenario: any hit whose attacker has a client makes a temp entity (alive about 300 ms), whatever the attacker's
  `cg_scorePlums`. A bot attacker gets one it never sees; an NPC attacker gets one addressed to a client number
  above 31 that is sent to nobody. Lightning (20 hits a second per target), saber passes and flechette shards
  multiply it. A stock client (its `cg_scorePlums` defaults to 1) draws every hit as a floating number, unsigned
  (C5). Review C4 gated the `dmg` command on userinfo; the plum beside it was left ungated.
- Fix: send the plum only when the attacker is a human client whose userinfo `cg_scorePlums` is not 0 (keep it in
  `pers`, as `damageLog` is), and add a server cvar for the feature.
- Confidence: confirmed (traced)
- Status: deferred: owner decision (plums on every hit for stock clients, a server cvar and a userinfo gate are gameplay presentation; no sign it was meant to be gated). Done in d39cb0ecf: no plum for an NPC attacker, whose plum reached no snapshot. Bot attackers keep theirs: a spectator following the bot sees it (SVF_SINGLECLIENT matches the followed ps.clientNum).

### RG-5 P2 `saberbench setup` pins its target slot for the rest of the map, with no release
- Where: codemp/game/g_saberbench.c:27-51 (`G_SaberBenchFrame`, called from g_main.c:3081), :202
- Scenario: after one `saberbench setup`, every frame zeroes that slot's command, puts it back on the stored origin,
  forces the blaster and holsters the saber. Nothing clears `saberBenchTargetNum`: not a subcommand, not the target
  leaving, not `map_restart` (the DLL stays loaded). A human who later takes the slot cannot move.
- Fix: add `saberbench clear`; clear on `G_InitGame` and when the slot disconnects or is no longer the bot it was
  set on.
- Confidence: confirmed (traced)
- Status: fixed (d39cb0ecf): `saberbench clear`; G_SaberBenchShutdown from G_ShutdownGame; the frame releases the slot when the client it was set on leaves (pers.connectTime).

### RG-6 P2 A saber lab run never unbinds its fighters, and a map change does not end a run
- Where: codemp/game/g_saberlab.c:257-265 (`LabFinish`), :612-613 (bind), :100-104; g_active.c:3679;
  g_main.c:474-493 (no lab call in `G_ShutdownGame`)
- Scenario: `LabFinish` (done, stopped or a fighter lost) leaves `lab.bound[]` set and the no-op `customThink` on
  the two bots. `G_RunClient` returns early for a bound slot, so those bots never run `ClientThink_real` again; a bot
  re-added into the slot (its botstate is cleared, `bound` is not) stands frozen until the DLL unloads. A run cut by
  `map_restart` or a map change is not finished: the CSV handle stays open and its buffered rows may be lost.
- Fix: in `LabFinish` clear `bound[]` and each fighter's `customThink`; call a `SaberLab_Shutdown` (LabFinish
  "map_shutdown") from `G_ShutdownGame`.
- Confidence: confirmed (traced)
- Status: fixed (d39cb0ecf): LabFinish clears `bound[]` and the fighters' LabBotThink; SaberLab_Shutdown (LabFinish "map_shutdown") from G_ShutdownGame.

### RG-7 P2 Lab commands pass formatted text, a player name included, to `trap->Print` as the format
- Where: codemp/game/g_hitcoverage.c:231-251 (netname), :298, :302; g_saberbench.c:59, :239, :264, :303, :309, :343
- Scenario: `trap->Print( va( "...%s...", netname ) )`. `ClientCleanName` does not remove `%`, so a player named
  `%s%s%n` makes `hitcoverage <that client> ...` run `Com_Printf` with a format the player wrote: garbage reads
  or a crash. `saberbench model` and `mark` do the same with the admin's own argument.
- Fix: `trap->Print( "fmt", args )` directly at all nine sites (no `va`).
- Confidence: confirmed (traced); the crash needs a run.
- Status: fixed (d39cb0ecf): all nine sites print with the format directly.

### RG-8 P2 BM-8's frame scaling is applied on top of the tickrate scaling added afterwards
- Where: codemp/game/bot2_move.c:7648, :7741, :7744, :7751 (`MoveFrames`, :652) over :7530-7534, :7558-7562,
  :7679-7683 (`WalkDeferFrames`, `DropDeferFrames`, `DeferGroundFrames` now return `Bot2_StepsFor(25*K)`)
- Scenario: with `bot_moveFixBM8 1` at a step other than 25 ms, K is converted to steps twice. At sv_fps 125 (8 ms)
  the walking deferral's 150 ms becomes `StepsFor(150)` = 19, then `MoveFrames(19)` = 59 steps = 472 ms; at sv_fps
  20 it becomes 100 ms. Identical at sv_fps 40, where every Bot2 pairing has run, which is the one rate BM-8 does
  not address. The flag defaults to 0 and its arm has not been run.
- Fix: drop the four `MoveFrames` calls on values that came from the three `*Frames()` functions (keep the ones on
  literal frame counts at :1220, :1280, :4649).
- Confidence: confirmed (traced)
- Status: fixed (d39cb0ecf): the four MoveFrames calls on values already in steps are dropped (DropDefer, WalkDefer K, BX-4 extra, DeferGround N); the literal-frame ones stay. Default (flag 0) unchanged; the flag's arm is still unrun.

### RG-9 P3 The client takes its movement style from `stats[9]` on any server
- Where: codemp/game/bg_public.h:598-599; bg_pmove.c:68-75 (`PM_MoveStyle`); bg_saber.c:69-73, :3032
- Scenario: `STAT_MOVEMENTSTYLE` and `STAT_JUMPTIME` use the first two free stat slots. Other mods that grew the
  enum put different data there (japro and TaystJK keep a dash timer and a jump speed directly after
  `STAT_MAX_HEALTH`, as far as I know). A xenjk client on such a server predicts CPM physics whenever that value is
  1 or 2, and predicts free Force while it is 2. The base_enhanced switches avoid this by a configstring token.
- Fix: announce the feature in configstring 25 (a token beside the `be_pmove` ones) and have `PM_MoveStyle` and
  the two raw reads return stock when the cgame has not seen it.
- Confidence: likely (the slot use is confirmed; the other mods' layout is from memory)
- Status: deferred: protocol design (a configstring token beside the be_pmove ones and cgame reads in the cgame lane) and it needs a run against another mod's server to confirm the slot clash.

### RG-10 P3 Two tick helpers have no caller
- Where: codemp/game/g_tick.c:182-184 (`G_TickPerFrame`), :186-193 (`G_TickTake`); g_tick.h:114-117
- Scenario: declared, defined, never called (the per-frame amounts were all gated to the 50 ms grid instead).
- Fix: remove both.
- Confidence: confirmed (grep)
- Status: fixed (d39cb0ecf): both removed.

### RG-11 P3 Three comments in `be_force.c` contradict the code
- Where: codemp/game/be_force.c:43-49, :380-381, :396-399
- Scenario: the TODO says the grip-rework victim slow is not ported, and the force-timer note says rage recovery's
  `<=` in `bg_pmove.c`/`bg_saber.c` is not ported. Both are ported (`BE_PM_GRIP_SLOW`, `BE_PM_RAGE_END`,
  be_pmove.h:41-42, bg_pmove.c:8582, :2577, bg_saber.c:2393, :2487). Lines 380-381 describe `BE_GripUseCooldown`,
  which sits 55 lines lower under another section header.
- Fix: delete the TODO and the "Not ported" sentence; move the cooldown comment to :436.
- Confidence: confirmed
- Status: fixed (d39cb0ecf): the TODO and the "Not ported" sentence now state the BE_PM_GRIP_SLOW / BE_PM_RAGE_END announcements; the Grip section header and the cooldown comment moved to BE_GripUseCooldown.

### RG-12 P3 `addbot` lost its team assignment block and kept the variable that depended on it
- Where: codemp/game/g_bot.c:828, :960-985 (18 stock lines removed before `ClientConnect`)
- Scenario: `preTeam` is now always `TEAM_FREE`. `G_InitSessionData` picks the bot's team from userinfo either
  way, so teams come out right, but in every team game the "team changed" branch now runs for every bot (a second
  `G_ReadSessionData` and `ClientUserinfoChanged`). The removal has no comment and the code reads as a leftover.
- Fix: restore the stock block (it is harmless and keeps the file stock), or delete `preTeam` and the branch that
  compares against it and say why.
- Confidence: confirmed (traced); no wrong team found.
- Status: deferred: either fix changes the bot connect path in team games (restoring the stock block calls PickTeam before ClientConnect again, and siege puts the bot on spectator first; deleting the branch drops a G_ReadSessionData/ClientUserinfoChanged that every team-game bot now gets). Needs a team-game run (laps) to show the teams are unchanged.

### RG-13 P3 A bounce in the frame after another bounce gets a late hit time
- Where: codemp/game/g_missile.c:159, :205; g_items.c:3182, :3211
- Scenario: `hitTime` assumes the traced segment started at `level.previousTime`. With the tick change the arc
  restarts at the previous bounce's `hitTime`, which is earlier, so on consecutive-frame bounces (a detonator in a
  corner) the second hit time is late by up to the unspent part of the previous frame. Smaller than the stock loss
  the change removes, but not zero.
- Fix: when `ent->s.pos.trTime > level.previousTime` is false and the last bounce was this entity's, interpolate
  from `max(trTime, previousTime)`; simplest: `from = max(ent->s.pos.trTime, level.previousTime)`.
- Confidence: likely (read, not measured)
- Status: deferred: needs a run, and the proposed `max(trTime, previousTime)` is not the fix: after a bounce trTime (the last hitTime) is below previousTime, so max returns previousTime, the current value. The segment start is trTime only on the frame after a bounce, which needs a per-entity "bounced last frame" mark (or a trace from trBase).

### RG-14 P3 `g_navmesh.cpp`: shifts past the int width, and the query set up three times
- Where: codemp/game/g_navmesh.cpp:298-300, :591; :214-266, :276-378, :380-418
- Scenario: `1 << a` for `a` up to 63 and `1 << area` for an area up to 63 are undefined; on x86 they wrap, so an
  avoid mask also penalises area N+32. The three query functions repeat the same filter, nearest-poly, path and
  straight-path code with their own 512-entry statics.
- Fix: loop to 32 (the mask is an int) and test `area < 32`; one static helper returning the straight path.
- Confidence: confirmed. Only reachable with `bot_navmeshOff 0`.
- Status: fixed (d39cb0ecf): the avoid loop runs to 32 and the draw filter skips areas of 32 and over. Not done: the shared query helper (a refactor of a path that only runs with bot_navmeshOff 0; needs a run with a mesh).

### RG-15 P3 The forecast's step capacity tripled every frame buffer
- Where: codemp/game/bot2_forecast.h:22 (256 to 800); bot2_move.c:3595, :3606 (12 ring slots), :5156, :5163,
  :5748, :5757; bot2_wallprobe.c:39; bot2_move.c:4103 (`guestPredict`)
- Scenario: 18 buffers of `bot2_forecastFrame_t` (two playerStates, a pmove and 256 bytes of policy: about 3.5 KB
  by my count, not compiled) grow from about 17 MB to about 55 MB of static data in every game module, bots or not.
  Only sv_fps 125 uses the capacity; the target servers run 30-40.
- Fix: size for the supported rates (for example 6400 ms at 16 ms = 400), or allocate the ring on the first Bot2
  use.
- Confidence: likely (sizes estimated)
- Status: deferred: Bot2 memory sizing; sv_fps 125 uses the 800-step capacity, so shrinking it is a supported-rates decision, and lazy allocation touches 18 buffers across Bot2 files (needs laps evidence that the forecasts are unchanged).

### RG-16 P3 `Bot2_GameStepMsec` reads two cvars by name on every call and is now on per-rollout paths
- Where: codemp/game/ai_bot2.c:96-100; callers `Bot2_StepsFor`, `Bot2_WorkPerFrame` (:105-116), `MoveSteps`
  (bot2_move.c:182), `MoveFrames`, `SearchBudget`, `WalkPoolLeft`, `PlanSplitBudget`; also be_force.c:408
- Scenario: each call is an `sv_fps` integer lookup and a `timescale` string lookup with `atof`. It was called a
  few times a frame; the ms horizons call it per rollout request and per budget query.
- Fix: latch the value once a frame (`level.time` keyed static), as `StepCvars` does for its cvars.
- Confidence: confirmed (read); cost not measured.
- Status: deferred: cost not measured (perf changes need a measurement first), and a level.time-keyed latch is stale on the first frame after a map change that also changes sv_fps (addrunner forces sv_fps 40) unless keyed on more than level.time.

### RG-17 P3 `g_bikeDebug` prints uninitialised wheel state when a command runs no substep
- Where: codemp/game/bg_bikemove.c:615, :633, :693-699
- Scenario: `bikeState_t bs` is a stack variable; `BikeUnpack` does not set `rpm`, `contact[]`, `load[]` or
  `comp[]`, which only a substep writes. A command that crosses no 4 ms grid point (1-3 ms) prints them. Debug
  print only; the packed state is not affected.
- Fix: `memset( &bs, 0, sizeof( bs ) )` before `BikeUnpack`.
- Confidence: confirmed (traced)
- Status: fixed (d39cb0ecf): bs is zeroed before BikeUnpack.

### RG-18 P3 `levelcheck_trace` does not check its allocation
- Where: codemp/game/g_levelcheck.c:34-37
- Scenario: `malloc( len + 1 )` is used unchecked; the file is read whole. A lab command, console only.
- Fix: return with a message when `text` is NULL.
- Confidence: confirmed
- Status: fixed (d39cb0ecf): returns with a message (and closes the input) when malloc fails.

### RG-19 P3 A diagnostic print and Bot2 timing run in every game frame
- Where: codemp/game/g_main.c:2972-2973, :3128-3131, :3519-3527
- Scenario: three `trap->Milliseconds` calls a frame and a `[FRAME] slow` console line on any frame over 200 ms
  (a map's first frames, a debugger pause), on servers that run no Bot2 bot.
- Fix: behind `bot_telemetry` or a developer cvar.
- Confidence: confirmed
- Status: fixed (d39cb0ecf): the `[FRAME] slow` line prints only when a bot is connected or `developer` is set (records use it on bot servers). The three Milliseconds calls stay (a few ns a frame).

### RG-20 P3 The stats frame walks linked lists per teammate pair
- Where: codemp/game/be_stats.c:1944-2008 (`BE_StatsAddPlayerTick`), :439-501 (`StatsForEnt`)
- Scenario: every frame of a CTF match, each player visits each teammate and searches that teammate's positioning
  list for his own entry: about 7,000 node visits a frame at 16 a side, 400 at 4 a side. Small today; it is the
  only per-frame cost of `g_beStats 1` that grows with the square of the team.
- Fix: keep the entry pointer per (client, teammate client) in a 32x32 cache cleared when either struct changes.
- Confidence: confirmed (read); not measured.
- Status: deferred: not measured; the report calls it small today (perf changes need a measurement first).

## Publish cleanliness

- codemp/game/g_crash_handler.c:1, g_crash_handler.h:1, g_hitcoverage.c:1, g_hitcoverage.h:1, g_saberbench.c:1,
  g_saberbench.h:1, g_saberlab.h:1, g_levelcheck.c:1, g_navmesh.cpp:1, g_navmesh.h:1, bg_bike_ct3x.h:1: no GPL-2.0
  header. Add the xenjk header `g_tick.c` carries.
  - Status: fixed (d39cb0ecf) for all but bg_bike_ct3x.h: its values are extracted from CP2077 data, so its licence line waits for the owner's provenance decision (next item).
- codemp/game/bg_bike.h:3, bg_bike.c:3, bg_bikemove.c:3, BikeNPC.c:3: new xenjk files under "Copyright (C) 2013 -
  2015, OpenJK contributors / part of the OpenJK source code". Use the xenjk header.
  - Status: fixed (d39cb0ecf) for bg_bike.h and bg_bike.c. deferred: bg_bikemove.c (the pmove adapter) and BikeNPC.c (G_CreateBikeNPC follows G_CreateSpeederNPC, Raven/Activision code) keep the OpenJK header until the owner says whether they count as derived from stock files, which would also need the Raven and Activision lines.
- codemp/game/bg_bike_ct3x.h:1-2 (and bg_bike.h:27-33): values generated from CP2077's TweakDB
  (`tweakdb_ep1.bin`), named as such. Owner decision before publishing whether extracted tuning data ships.
  - Status: deferred: owner decision (whether TweakDB-derived tuning data ships, and under what notice).
- codemp/game/bg_pmove.c:55-56, bg_slidemove.c:902, bg_saber.c:2260, g_active.c:2379: TaystJK/japro attribution is
  present with the commit (2ab07a79). be_*.c/h carry the base_enhanced authors and URL. Nothing to add.
  - Status: not a bug: attribution present; nothing changed.
- codemp/game/g_combat.c:4588-4591: a leftover "BOT TELEMETRY: REMOVED (DECOUPLED)" banner and a whitespace-only
  line; :28 `#include "ai_main.h"` is no longer used. Remove all three.
  - Status: fixed (d39cb0ecf).
- codemp/game/g_cmds.c:3412-3418: the `ClientCommand` header comment was replaced by an indented copy of
  "Cmd_SetViewpos_f" that heads nothing. Restore the stock comment.
  - Status: fixed (d39cb0ecf).
- codemp/game/g_main.c:35: `g_local.h` included a second time. Remove.
  - Status: fixed (d39cb0ecf).
- codemp/game/g_missile.c:27, :390: blank-line-only changes to a stock file. Revert.
  - Status: fixed (d39cb0ecf).
- codemp/game/g_svcmds.c:514-531: eighteen commands prepended to the sorted stock table, unaligned and unsorted
  (lookup is linear, so it works). Sort them in.
  - Status: fixed (d39cb0ecf): the whole table sorted and aligned (lookup is linear, so order does not change behaviour).
- codemp/game/bg_pmove.c:2829, :2833: `PMF_JUMP_HELD` cleared and set again four lines later. Drop the clear.
  - Status: fixed (d39cb0ecf).
- codemp/game/be_force.c:1134-1146: `userInt1`/`userInt2` are written for "clients that draw the result"; no xenjk
  client reads them. Keep for base_enhanced clients or remove; say which.
  - Status: not a bug: kept. They are upstream base_enhanced's drain-hit fields on the wire; a base_enhanced-aware client can draw them, and removing them changes what such a client receives.
- codemp/game/g_xcvar.h:103: `g_fixGolanDamage` is `CVAR_ARCHIVE` under a block comment (:104-108, "none is archived" at :107) that says none
  of the base_enhanced switches is archived. Make it `CVAR_NONE` or move it out of that statement's reach.
  - Status: not a bug: the "none is archived" comment starts after g_fixGolanDamage (it heads the Force-port block below); g_fixGolanDamage sits with the archived g_fix* switches above it. Changing the flag would change what persists in configs.
- codemp/game/CMakeLists.txt:36-78: `g_tick.h` and `g_saberlab.h` are not listed; the Bot2 files sit unsorted at
  the top of the list with `g_levelcheck.c` among them.
  - Status: fixed (d39cb0ecf): g_tick.h, g_saberlab.h and the unlisted bot2_corner.h, bot2_probe.h, bot2_threat.h added; Bot2 files sorted; g_levelcheck.c and g_saberlab.c among the g_ files.
- codemp/game/ai_main.c:7540, :7587: a function-local `extern` and a "COMPLETELY BYPASS LEGACY AI" comment.
  Declare in `ai_bot2.h`; plain wording.
  - Status: fixed (d39cb0ecf): ai_main.c already includes ai_bot2.h, which declares it; the local extern is gone and the comment reworded.
- Comments across the new files quote the owner and cite internal planning ("owner 2026-11-09: ...", "wishlist
  item 1", "kit 11s"): BikeNPC.c:21-22, bg_bike.c:21, :727, bg_bike.h:21, :35, :58, bg_bikemove.c:21, :161, :229,
  bg_public.h:602, g_levelcheck.c:3, g_local.h:1559, g_xcvar.h:105, :122, be_combat.h:26, g_tick.h:23. Not private,
  but they read as an internal log; reword to the reason alone.
  - Status: deferred: comments that cite owner dates or planning are left for the separate comment sweep (fix contract).
- docs/gameplay/TICK_INDEPENDENCE.md:33-34: points at the branch `claude/vigilant-sanderson-aa0512`; the record is
  on master and the branch is archived.
  - Status: fixed (d39cb0ecf): links the record on master.
- No absolute local path, user name, handle, URL (other than the base_enhanced and GNU ones) or credential in any
  added line of this lane.
  - Status: not a bug: rechecked after the fixes; none added.

## Liftability

- **Labs and probes ship in the game module.** `g_saberlab.c`, `g_saberbench.c`, `g_hitcoverage.c`,
  `g_levelcheck.c` and their eight console commands are always compiled and registered; the saber lab and bench
  each hook `G_RunFrame`, the lab also `G_RunClient`. Shape: one CMake option (`BuildGameLabs`) and one header with
  `G_Labs_PreFrame`, `G_Labs_Frame`, `G_Labs_RunClient`, `G_Labs_ConsoleCommand`, `G_Labs_Shutdown`; stock files
  call those five and nothing else. That also gives RG-5 and RG-6 their shutdown call.
  - Status: done (b5d3f7b7f): g_labs.c/h with the five hooks; the eight commands moved out of the svcmd table; CMake option BuildGameLabs (default ON; OFF builds clean with the lab files left out).
- **The base_enhanced port depends on Bot2 and on g_tick.** `be_force.h:49` declares `Bot2_GameStepMsec`
  (`ai_bot2.c`) for `BE_FixDuration`; `be_force.c:691-725, :863-873` call `G_TickStrict*`. Shape: move the nominal
  frame length into `g_tick.c` (`G_TickNominalFrameMsec`), have Bot2 call that, and state in `be_force.h` that the
  port needs `g_tick.c` (or wrap the three timers in two `BE_` inline helpers that fall back to the stock
  expressions when g_tick is absent).
  - Status: done (b5d3f7b7f): G_TickNominalFrameMsec in g_tick.c, called by Bot2_GameStepMsec and BE_FixDuration; be_force.h and docs/ports/base_enhanced.json state the g_tick need.
- **Grip exists twice.** `BE_ForceGrip` and `BE_DoGripAction` (be_force.c:468-844) are whole copies of the stock
  functions with the changes inline, chosen by `BE_GripReplacesStock`; the tick hooks had to be added to both.
  Shape: keep it (upstream did the same), but note in `docs/ports/base_enhanced.json` that any stock grip change
  must be mirrored.
  - Status: done (b5d3f7b7f): the mirror rule is in docs/ports/base_enhanced.json (ours.mirror_rule); code kept as upstream has it.
- **Bot2 inside stock functions.** `G_Find` is wrapped for a Bot2 perf counter (g_utils.c:245-255);
  `PmoveSingle` and `Pmove` carry ten `PM_PROFILE_MARK` calls into `bot2_forecast.c` (bg_pmove.c:10348-10356 and
  the marks); `ClientThink_real` calls `Bot2_MoveBenchPmove` in place of `Pmove` (g_active.c:3078); `G_RunClient`
  has three Bot2 intercepts (g_active.c:3680-3682, after the lab's at :3679); `G_InitGentity`/`G_FreeEntity` bump `bot2_entitySerial`;
  `G_TempEntity` returns a scratch entity during a forecast; `g_main.c` includes eight Bot2 headers and calls
  twelve Bot2 functions by name. Shape: one `bot2_hooks.h` with `Bot2_GameInit`, `Bot2_GameShutdown`,
  `Bot2_FrameBegin`, `Bot2_FrameEnd`, `Bot2_RunClient`, `Bot2_Pmove`, `Bot2_EntityChanged`; the perf wrapper and
  the pmove marks behind a `BOT2_PROFILE` define that is off in shipping builds.
  - Status: not done: moving the Bot2 hooks in stock files (G_Find, Pmove marks, ClientThink_real, G_RunClient, G_TempEntity, g_main.c) has no purely mechanical path; each hook order matters to Bot2 forecasts and needs laps evidence.
- **Bikes as "speeder or bike" at fifteen sites.** `type == VH_SPEEDER || type == VH_BIKE` is repeated in
  g_active.c:904, g_combat.c:4450, g_missile.c:754, g_weapon.c:1369, :3715, :4171, :4393, w_force.c:1441, :3524,
  be_force.c:541, bg_slidemove.c:190, :214, bg_pmove.c:4571, g_vehicles.c:198. Shape: `BG_VehicleRidesLikeSpeeder(
  const Vehicle_t * )` in `bg_vehicles.h`; a derivative then adds a type in one place.
  - Status: done (b5d3f7b7f) for the game lane: BG_VehicleRidesLikeSpeeder in bg_vehicles.h at all fourteen sites. The cgame sites (cg_event.c:1178, cg_main.c:126, cg_players.c:4831) are the cgame lane's.
- **Movement styles.** Fourteen inline sites in `bg_pmove.c`, two in `bg_saber.c`, one in `bg_slidemove.c`, two in
  the game. The physics constants and `PM_CPMAirControl` are already grouped; the CPM jump (bg_pmove.c:2810-2834)
  and air branch (:3355-3369) could move with them into `bg_movestyle.c` behind `PM_StyleJump()` and
  `PM_StyleAirMove()`. Needs RG-9's token to be safe to lift.
  - Status: not done: the report makes it depend on RG-9's token (deferred).
- **Damage numbers.** The plum and `dmg` command are an inline block in `G_Damage` (g_combat.c:5405-5421, the command at :5416). Shape:
  `G_DamageFeedback( targ, attacker, take, mod )` in its own file with the cvar from RG-4.
  - Status: done (b5d3f7b7f): G_DamageFeedback in g_damagefeedback.c; the RG-4 cvar is deferred with RG-4.
- **Tick independence** lifts cleanly: `g_tick.c/h`, four entity fields, two client fields, four level fields and
  marked one-line hooks. One coupling to note for a derivative: `G_TickSaberClockBegin/End` need the engine's
  `G2API_SetTime` import and the `sv_g2ClockImport` cvar (g_public.h:1099-1101), which a stock engine does not
  provide; the code checks and degrades.
  - Status: nothing to do (already lifts cleanly).

## Still open from earlier reviews

In this lane's files, checked against master:

- V3 (FINDINGS.md): `g_navmesh.cpp:154-170` still trusts tile sizes from the file; only with `bot_navmeshOff 0`.
- C5 (FINDINGS.md): negative plums are still drawn unsigned; RG-4 makes every hit one.
- From the 2026-11-09 Bot2 review, untouched by the delta read here and still as its index says: BP-2, BP-6,
  BP-13, BP-14, BP-15 (nodes, merge tool), BP-16 (`Graph_BodiesEncloseGoal`), BP-17, BM-4, BM-6, BM-14, BM-18,
  BM-19, BX-2, BX-8, BX-9, BA-7, BA-13, BA-14, BA-16, BA-17, BA-18, BA-20, BA-22 (rest), BA-23. BM-8, BM-9 and
  BM-10 are built behind flags that default to 0 with no arm run; RG-8 is a defect in BM-8's flag. BM-3
  (`bot_moveChargeFlown`), BX-1 (`bot_moveArriveReroute`), BP-1 and BP-5 remain default 0 as recorded.
