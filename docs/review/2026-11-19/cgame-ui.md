# cgame and UI (review 2026-11-19, master a322d9dc4)

Scope: `git diff 407a56fb master -- codemp/cgame codemp/ui` without the vendored libraries: 50 files, 15,941 added
lines, plus `tools/ui/*.py` and the generated `tools/ui/xen_player.menu`.

- New cgame files, read whole: `cg_aimfeedback.c` (4171) / `.h`, `cg_be.c` (254) / `.h`, `cg_bench.c` (167),
  `cg_bike.c` (540), `cg_conctrace_visual.c` (338) / `.h`, `cg_pitdeath.c` (392), `cg_screenpos.c` (265),
  `cg_signals.c` (219), `cg_soundindicators.c` (535) / `.h`.
- New UI files, read whole: `ui_picker.c` (1338), `ui_player.c` (873) / `.h`, `ui_player_look.c` (2924),
  `ui_roster.c` (519) / `.h`, `ui_xenui_host.c` (121), `xenui.cpp` (1378), `xenui.h`, `xenui_stub.c`,
  `xenui_profile/tracy/Tracy.hpp`.
- Stock files, the diff read: cgame `CMakeLists.txt`, `cg_consolecmds.c`, `cg_draw.c`, `cg_effects.c`, `cg_ents.c`,
  `cg_event.c`, `cg_local.h`, `cg_localents.c`, `cg_main.c`, `cg_players.c`, `cg_predict.c`, `cg_public.h`,
  `cg_servercmds.c`, `cg_spawn.c`, `cg_view.c`, `cg_weapons.c`, `cg_xcvar.h`; UI `CMakeLists.txt`, `ui_atoms.c`,
  `ui_local.h`, `ui_main.c`, `ui_public.h`, `ui_saber.c`, `ui_shared.c`, `ui_shared.h`, `ui_xcvar.h`.
- Tools: `tools/ui/rcss_lint.py` (161), `bake_xen_player_art.py` (306), `make_xen_player_menu.py` (308).
- RML / RCSS: none in the tree (`git ls-files` has no `.rml` or `.rcss` outside `lib/rmlui`); see Publish cleanliness.

The five files unchanged since the 2026-10-07 review (`cg_aimfeedback`, `cg_be`, `cg_bench`, `cg_conctrace_visual`,
`cg_soundindicators`) were read by a helper agent; its findings are RU-5, RU-6 and RU-19 to RU-24 and were not
re-traced by me. Everything else is my own reading. Nothing was built or run.

Reviewer's note: while checking `tools/ui/make_xen_player_menu.py` I ran it with `--help`; it has no argument parser,
so it rewrote the tracked `tools/ui/xen_player.menu` (one line changed, RU-7). I restored the file with
`git checkout -- tools/ui/xen_player.menu`; `git status` shows it clean.

Files with no findings: `cg_be.h`, `cg_aimfeedback.h`, `cg_conctrace_visual.h`, `cg_consolecmds.c`, `cg_event.c`,
`cg_main.c`, `cg_predict.c`, `cg_public.h`, `cg_localents.c`, `cg_weapons.c`, `ui_roster.h`, `ui_player.h`,
`ui_public.h`, `ui_saber.c`, `ui_shared.c`, `ui_shared.h`, `ui_xcvar.h`, `xenui.h`, `xenui_stub.c`, `Tracy.hpp`,
both `CMakeLists.txt` (licence headers aside, see Publish cleanliness).

Counts: P0 0, P1 2, P2 6, P3 21.

## Findings

### RU-1 P1 The picker stage uses an uninitialised animation-set index for a model with its own skeleton
- Where: codemp/ui/ui_picker.c:308 (`int animIndex, anim;`), 325-335 (the own-skeleton branch never sets it), 336
  (`picker.animIndex = animIndex;`), 466-469 (`Picker_Anim` reads `bgAllAnims[picker.animIndex].anims[anim]`)
- Scenario: an outfit whose GLA is not under `players/_humanoid/` (any path the `strstr` at 319 misses, a
  `_humanoid_xen` folder included) goes on the stage. `picker.animIndex` gets whatever the stack or register held.
  Every later `Picker_Anim` call (each stage frame through `Picker_UpdateAnim`, 474; `UI_PickerStageGesture`, 1272;
  `Picker_FitReach`, 580) indexes `bgAllAnims` with it: a negative value gives "no animation" (poses and gestures
  silently do nothing), a positive one reads outside the 16-entry array and dereferences what it finds. Separately,
  the private set parsed for that skeleton (`pickerAnims`, 301) is only used for the first idle at 342; poses and
  gestures never consult it.
- Fix: set `animIndex = -1` in the own-skeleton branch and give `Picker_Anim` the table in use (a
  `const animation_t *picker.anims` set to `bgAllAnims[i].anims` or `pickerAnims`), dropping the index.
- Confidence: confirmed (traced) for the uninitialised read; whether it crashes or only drops the poses depends on
  the value and needs a run. Which installed models take this branch was not checked.
- Status: fixed (5ac2fdc9c): the stage keeps the animation table in use (picker.anims), so the own-skeleton branch has no index and poses / gestures use its private set; not run

### RU-2 P1 The player screen can reach only the first 64 characters of a source filter
- Where: codemp/ui/ui_player_look.c:62 (`LK_MAX_ROWS 64`), 646-651 (`LK_AddRow` returns NULL when full), 751-760
  (`LK_ModelCharacter`: `continue` on NULL, `shown` still counts), 1832-1840 and 2021-2035 (selection by row index)
- Scenario: the roster had 71 characters on 2026-11-01 (docs/character-loading-plan.md:37) and has grown since.
  With the filter on ALL, the head says "N characters" but Down, the wheel and clicks stop at row 64; characters
  after it cannot be chosen on this screen. If the current `model` is one of them, `lkm.cur` is -1 and the first
  Up / Down or a filter change jumps the selection to the first character (1894-1896, 2023).
- Same cap on hilts: 851-855 breaks at 64 rows while `MAX_SABER_HILTS` is 256 (ui_local.h:42).
- Fix: build only the visible window (the list already draws `cfg->win` rows around the selection, 1478-1486):
  keep a filtered index array of roster indices (512 ints) and fill `lkm.rows` for `top..top+win`; move by index in
  that array.
- Confidence: confirmed (traced); the installed roster size was taken from the plan document, not counted.
- Status: fixed (5ac2fdc9c): LK_MAX_ROWS is the larger of ROSTER_MAX_CHARACTERS (512) and MAX_SABER_HILTS (256), so a list holds every row its source can have; the list still draws only its window

### RU-3 P2 misc_model_ghoul2 instances are never freed
- Where: codemp/cgame/cg_spawn.c:283 (`G2API_InitGhoul2Model( &g->ghoul2, ... )`), codemp/cgame/cg_main.c:2656
  (`CG_DestroyAllGhoul2` frees centities, weapons and items only); `cgs` is zeroed at the next `CG_Init`
- Scenario: a map with misc_model_ghoul2 decor (the Kejim giants): every map load, map change and `vid_restart`
  leaves up to 16 Ghoul2 instances allocated in the engine's Ghoul2 info array with no pointer left to them. The
  array has a fixed number of slots, so a long session of restarts on such a map runs towards "out of ghoul2 info
  slots"; a debug build reports the leak at exit.
- Fix: in `CG_DestroyAllGhoul2`, loop `cgs.miscG2Models[0..numMiscG2Models)` and `G2API_CleanGhoul2Models` each
  (guarded by `G2_HaveWeGhoul2Models`), then zero the count.
- Confidence: confirmed (traced) that nothing frees them; the slot exhaustion is likely, not run.
- Status: fixed (5ac2fdc9c): CG_DestroyAllGhoul2 frees them (now CG_MiscG2_Shutdown in cg_miscg2.c, e5666e69f); not run

### RU-4 P2 Pit death: any death while the hand rises is shown as a catch, and the corpse is hidden
- Where: codemp/cgame/cg_pitdeath.c:250-251 and 261-262 (`doomed = dead || falling`), 317-323 (RISING -> HELD on
  `valid && doomed`), 356-361 (`killed`, `hideBody`)
- Scenario: a player falling over a dressed pit (within 620 u above the trigger, falling faster than 150 u/s) is
  shot dead in the air, or dies there for any other reason. The state goes to HELD, `CG_PitDeath_HeldOrigin` draws
  the body in the palm at the trigger's top (it teleports there), the next frame `dead` ends the hold as `killed`,
  the crush flash plays and `hideBody` hides the corpse until the respawn. The local player's camera also jumps to
  the hand view for a frame.
- Fix: catch only on the fall itself: `fallingToDeath` for the local player, `EF_RAG && !EF_DEAD` for others (or
  require the position to be within a body height of `c->target`); a plain death while RISING goes to PIT_DONE with
  `killed` false.
- Confidence: confirmed (traced); not run.
- Status: fixed (5ac2fdc9c): only the trigger's own signal (fallingToDeath for the local player, EF_RAG, which g_trigger.c sets with it, for others) turns RISING into HELD; a plain death lets the hand sink unfed; not run

### RU-5 P2 `benchcam` detaches the rendered camera on any server, with no cheat or demo gate
- Where: codemp/cgame/cg_bench.c:135-166 (`CG_Bench_Camera`, `CG_Bench_Cam_f`), codemp/cgame/cg_consolecmds.c:298,
  hook codemp/cgame/cg_view.c:2679
- Scenario: on a live server a player types `benchcam <x> <y> <z> <yaw> <pitch>`; `cg.refdef.vieworg` is replaced
  every frame, so the view sits at any point (round a corner, over a wall) and draws every entity in the snapshot.
- Fix: refuse unless `cg.demoPlayback`, cheats or a local server; or compile both bench commands under a build flag.
- Confidence: confirmed by reading (helper agent); not run.
- Status: fixed (5ac2fdc9c): benchcam refuses unless cg.demoPlayback or sv_cheats (CS_SYSTEMINFO); the bench and levelcheck load with devmap

### RU-6 P2 Candidate lesson mode reads `af_fcTrack[-1]` when the track file is missing
- Where: codemp/cgame/cg_aimfeedback.c:3071, 3083 (`AF_UpdateCandidateDirector`) -> 2917
  (`AF_CalculateLessonMetrics`)
- Scenario: `cg_aimFeedback 1; cg_aimFeedbackLesson 2` on a demo without `aimfeedback_fc_track_v2.aft`:
  `AF_FCTrackIndexAtTime` returns -1, `AF_StartLessonStage( 2 )` bails, then 3083 calls
  `AF_CalculateLessonMetrics( -1 )` and reads one entry before the array: garbage metrics on screen.
- Fix: return from `AF_CalculateLessonMetrics` when `trackIndex < 0 || trackIndex >= af_fcTrackCount`.
- Confidence: confirmed (traced, helper agent).
- Status: fixed (5ac2fdc9c)

### RU-7 P2 The committed xen_player.menu is not what its generator writes
- Where: tools/ui/make_xen_player_menu.py:85 (`'0 0 265 480'`), tools/ui/xen_player.menu:73 (`rect 0 0 330 480`)
- Scenario: the file header says "edit that, not this", but the stage item's rect was changed in the output only
  (or the generator after it). Running the generator moves the stage's home from x 165 to x 132.5
  (`Picker_PaintStage` centres the figure on the item, ui_picker.c:841) and nobody is told.
- Fix: decide which value is right, put it in the generator, regenerate and commit; add the generator to a check
  (`python make_xen_player_menu.py` then `git diff --exit-code tools/ui/xen_player.menu`). Give the script a
  `--check` / `--out` argument so running it is not a silent overwrite.
- Confidence: confirmed (the generator was run and its output diffed, then restored).
- Status: fixed (5ac2fdc9c): the tracked menu is current (5ab692f78 widened the stage to 330 after the generator was written); the generator now writes 330, takes --out (required) or --check, and tools/test_xen_player_menu.py checks the tracked menu against it

### RU-8 P2 The first bike drawn registers 108 sounds in that frame
- Where: codemp/cgame/cg_bike.c:81-102 (`CG_BikeRegisterSounds`: 96 engine steps + 12), 114-115 (called from
  `CG_BikeSound` on the first draw)
- Scenario: a bike comes into view for the first time in a map (someone spawns one, or it enters the PVS): 108
  `S_RegisterSound` calls with their file reads run inside `CG_Player`, a visible hitch mid-game. Without the CT-3X
  pack it is 108 failed lookups.
- Fix: register at load when the map or the vehicle list has a VH_BIKE (from `CG_G2AnimEntModelLoad`'s VH_BIKE case
  at level load, or `CG_Init` when `BG_BikeParams` exists), not on first draw.
- Confidence: confirmed by reading; cost not measured.
- Status: fixed (5ac2fdc9c): CG_CacheG2AnimInfo registers them at load for a VH_BIKE in the model configstrings; first use still registers for a bike that was not precached; cost not measured

### RU-9 P3 `r_stageCovers` stays 1 when the UI restarts with the player screen open
- Where: codemp/ui/ui_picker.c:148, 1251-1256 (`UI_PickerStageCovers` writes the cvar only on a change of its own
  static), codemp/ui/ui_player_look.c:1706; `UI_Shutdown` (ui_main.c:1096) does not run the menu's onClose
- Scenario: `vid_restart` (or any UI module reload) while the player screen is open: the cvar stays "1", the new
  module's static is false, so `UI_PickerStageCovers( qfalse )` is a no-op. Opening `ui_characters` in game then
  draws its stage with the world under the dimmed overlay left out by the renderer (`R_STAGE_COVERS`,
  rd-vulkan/tr_local.h:2451) until the player screen has been opened and closed once.
- Fix: `trap->Cvar_Set( "r_stageCovers", "0" )` in `UI_Init` and `UI_Shutdown`.
- Confidence: confirmed by reading; not run.
- Status: fixed (5ac2fdc9c): UI_PickerStageCoversReset in UI_Init and UI_Shutdown (only when the cvar is set, so other renderers get no new cvar); not run

### RU-10 P3 Indices from the xenui document's events are used unchecked
- Where: codemp/ui/ui_player_look.c:2796-2815 (`LK_XenEvent` builds a hit from the document's strings) ->
  2166-2196 (`LK_ClickHit`: `lkm.rows[t->idx]`, `lkm.fields[t->idx]`, `lkBladeNames[t->sub]`)
- Scenario: a `player.rml` whose `data-event-click="hit('chip', 0, 9)"` or `hit('field', 40)` passes an index the C
  model does not have (an edited or mismatched pack, or a click on a row of a frame older than the model): reads
  past `lkm.fields[6]` / `lkBladeNames[6]`, and for a Force row passes a wild power number into `LK_Raise`.
- Fix: in `LK_XenEvent`, drop the event unless `idx` is inside the count for its kind and `sub` inside the chip /
  option count.
- Confidence: confirmed by reading; needs a malformed document to happen.
- Status: fixed (5ac2fdc9c): LK_XenEvent drops an event whose index (or chip) the model in hand does not have

### RU-11 P3 Every console command the UI sees copies eight 1 KB arguments before the `ui_xen` test
- Where: codemp/ui/ui_xenui_host.c:107-121, called first in `UI_ConsoleCommand` (codemp/ui/ui_atoms.c:130)
- Scenario: any command that falls through to the UI (every unknown command, every `ui_*` command) fills an 8 KB
  stack array with `Cmd_Argv` copies, then compares the first six characters.
- Fix: read argv 0 into a small buffer, return unless it starts with `ui_xen`, then copy the rest.
- Confidence: confirmed.
- Status: fixed (5ac2fdc9c)

### RU-12 P3 The bike's front-arm trace runs with `cg_bikeFrontArm 0`
- Where: codemp/cgame/cg_bike.c:243-262 (the box trace and the maths, then `if ( !cg_bikeFrontArm.integer ) return;`)
- Scenario: the cvar off still costs one `CG_Trace` per bike per frame.
- Fix: test the cvar at the top of `CG_BikeFrontArm`.
- Confidence: confirmed.
- Status: fixed (5ac2fdc9c)

### RU-13 P3 misc_model_ghoul2: animation, tracking and bone writes run before the cull
- Where: codemp/cgame/cg_draw.c:8432-8504 (clip advance and the `cervical` / `cranium` `G2API_SetBoneAngles` at
  8496-8498 come before `CG_CullPointAndRadius` at 8502)
- Scenario: a tracked giant behind the player still scans 32 clients and sets two bone overrides each frame.
- Fix: cull first; keep only the clip clock (`clipEnd`) running for culled models.
- Confidence: confirmed; small.
- Status: fixed (5ac2fdc9c), partly: a culled model skips the two bone writes; the clip clock and the look's smoothing (with its player scan) keep running, because skipping them would change what is seen when the giant comes into view

### RU-14 P3 xenui cannot find the renderer on non-Windows builds
- Where: codemp/ui/xenui.cpp:501 (`dlopen( host->rendererModule, RTLD_LAZY | RTLD_NOLOAD )` with a bare file name)
- Scenario: the engine loads the renderer by path; `RTLD_NOLOAD` with only `rd-vulkan_x86_64.so` does not match a
  library loaded by another path string on glibc, so `FindRenderAPI` returns null and xenui is off on Linux / macOS
  whatever the renderer exports.
- Fix: pass the interface through the UI import table (see Liftability), or have the host hand over a function
  pointer the engine resolved.
- Confidence: likely; not built on Linux.
- Status: deferred: needs a non-Windows build to confirm, and the fix is a slot in the engine's UI import table (cl_ui.cpp, engine lane) as the Liftability section proposes

### RU-15 P3 `XenHost_FS_ReadFile` does not check `malloc`
- Where: codemp/ui/ui_xenui_host.c:29-31
- Scenario: a failed allocation (a huge or corrupt file length) writes through NULL.
- Fix: return -1 when `malloc` fails (close the file first).
- Confidence: confirmed; edge case.
- Status: fixed (5ac2fdc9c)

### RU-16 P3 A `glas.txt` of 2,048 bytes or more is ignored, which lists every GLA
- Where: codemp/cgame/cg_players.c:1006-1010 (read only when `len < sizeof( xenAnimGLAs )`), `CG_XenAnimGLAListed`
  returns qtrue for an empty list
- Scenario: the list grows past the buffer: it is dropped without a message, every padded GLA then counts as holding
  the xenjk frames and plays the set's rows from stand-pose padding (frozen poses).
- Fix: print a warning and treat an oversized list as "none listed" (return qfalse), or size the buffer from `len`.
- Confidence: confirmed by reading.
- Status: fixed (5ac2fdc9c): an oversized glas.txt prints a warning and lists no GLA (every GLA keeps the stock set)

### RU-17 P3 The stage's tag says LIVE MODEL while the model is not on the stage yet
- Where: codemp/ui/ui_player_look.c:1274 (`state == 0 ? "LIVE MODEL" : ... : "LIVE MODEL"`), 2744 (the xenui copy)
- Scenario: `UI_PickerStageState` returns 1 during the 500 ms dwell (the icon is shown): the tag reads LIVE MODEL.
- Fix: a tag for state 1 ("PREVIEW" or none); the `state == 0` arm at 1274 is redundant either way.
- Confidence: confirmed.
- Status: fixed (5ac2fdc9c): state 1 (the dwell) reads LOADING; one LK_StageTag for both looks

### RU-18 P3 Crossing-signal clock is a float of seconds
- Where: codemp/cgame/cg_signals.c:192 (`now = cg.time * 0.001f + ...`), 198 (`fmodf`)
- Scenario: `cg.time` is the server's level time; on a server up for days the float holds 30-60 ms steps, so the
  schedule's "events in (a, b]" windows jitter against the renderer's lamps. Not audible on a fresh map.
- Fix: reduce in integers first (`( cg.time + shiftMs + offsetMs ) % cycleMs`), then convert.
- Confidence: likely (arithmetic); not run.
- Status: fixed (5ac2fdc9c): the clock is double seconds in cg_signals.c and, so lamps and sounds keep one clock, in rd-vulkan vk_scene.cpp Scene_SignalStates (cross-lane, two lines); not run

### RU-19 P3 Own force sounds raise a sound indicator on yourself
- Where: codemp/cgame/cg_soundindicators.c:527-529 (EV_PREDEFSOUND), the self filter at 68
- Scenario: `cg_soundIndicators 1`, use Protect / Absorb: the event is a temp entity, `es->number` is never your
  client number, so an arc appears from your own feet.
- Fix: for temp-entity events skip origins within ~32 u of the predicted origin, or drop EV_PREDEFSOUND.
- Confidence: confirmed (traced, helper agent); the feature is off by default.
- Status: fixed (5ac2fdc9c): an EV_PREDEFSOUND within 32 u of the predicted origin is skipped

### RU-20 P3 Missile-impact indicators filter on the entity hit, not the shooter
- Where: codemp/cgame/cg_soundindicators.c:486-490, filter at 68
- Scenario: a rocket that hits you is suppressed as "your own sound"; your own rockets exploding elsewhere draw
  indicators.
- Fix: pass `ENTITYNUM_NONE` as the source for impacts, or the missile's owner where the event carries it.
- Confidence: confirmed by reading (helper agent).
- Status: fixed (5ac2fdc9c), partly: impacts are keyed on the missile (es->number), so a rocket that hits you shows; your own rockets' impacts still show, because ordinary missiles do not network their owner (s.owner is set only for turret and vehicle shots)

### RU-21 P3 The held drain line hides lightning for up to 200 ms
- Where: codemp/cgame/cg_be.c:116-130 (`CG_BE_DrainShown`), codemp/cgame/cg_players.c:9908, 9968
- Scenario: on a base_enhanced rework server a player fires the one-frame drain then lightning within 200 ms: the
  drain branch wins the `if / else if` and the lightning is not drawn for those frames.
- Fix: in the hold path also require `!cent->currentState.activeForcePass`.
- Confidence: likely (helper agent).
- Status: fixed (5ac2fdc9c): the hold path requires activeForcePass 0; not run

### RU-22 P3 The aim lab starts and stops whatever `cg_aimFeedback` and demo playback say
- Where: codemp/cgame/cg_aimfeedback.c:2365-2397 (`CG_InitAimFeedback`), 2413-2417 (the shutdown `quit`)
- Scenario: `cg_aimFeedbackEnvelope 1` (or Review 1/3/4) left set, then a live server: every cgame load creates or
  truncates `aimfeedback_fc_track_v2.aft` / the `.afr` caches and writes `timescale`; with `cg_aimFeedback 0` in a
  demo the shutdown still issues `quit`.
- Fix: start the lab at the first `CG_AimFeedback_Update` past its demo gate; gate the `quit` on `af_drovePlayback`.
- Confidence: confirmed by reading (helper agent).
- Status: fixed (5ac2fdc9c): the lab's modes start at the first CG_AimFeedback_Update past its demo gate (AF_StartLab), and the shutdown quit needs the lab to have run; the two-pass launcher needs a run

### RU-23 P3 Aim-lab shot analysis runs in one frame
- Where: codemp/cgame/cg_aimfeedback.c:2248-2269 (`AF_FinalizeShot`), 1638-1683, 1731-1776, entity scan 3280-3288
- Scenario: each analysed shot runs two 1,369-cell trajectory searches plus a 9 x 9 heat map (Golan: 81 x 40
  Monte-Carlo) with collision traces in a single frame: a hitch per shot in demo viewing.
- Fix: spread rows over frames, or accept as lab-only behind a build flag (see Liftability).
- Confidence: confirmed by reading (helper agent); not measured.
- Status: deferred: lab-only (demo playback with cg_aimFeedback); spreading the search over frames is a design change, a build option for the lab is an owner decision on what ships

### RU-24 P3 Dead or redundant code
- codemp/cgame/cg_aimfeedback.c:905 unused `shot` parameter; 177-185, 2241-2242 `afValidationState_t.complete`,
  `.previousTime`, `.previousPosition` never read; 55 `AF_LESSON_DEFAULT_TIME` duplicates the cvar default; 31
  `trap->Print( va( ... ) )` calls pass formatted text as the format string.
- codemp/cgame/cg_bench.c:74 `va()` built twice; 33-36 hand-declared `QueryPerformanceCounter` prototypes.
- codemp/cgame/cg_soundindicators.c:251-252 empty banner; 367 comment names a dead check that is not there; 8-10
  header says no assets are registered while 49-52 registers two; `.h`:86-91 alias macros (pick one set of names).
- codemp/cgame/cg_conctrace_visual.c:34 and cg_bike.c:35 both declare `CG_WorldCoordToScreenCoordFloat` locally
  (move one prototype to cg_local.h); cg_conctrace_visual.c:218 self-assignment.
- codemp/ui/ui_player_look.c:1295 `hover ? lkPaper : lkPaper`; 1742 and 2400 declare `lkXen` twice (a tentative
  definition, then the initialised one): keep one, above its first use.
- tools/ui/rcss_lint.py:31, 84-85 `NOT_YET` is empty and its branch never fires.
- codemp/cgame/cg_servercmds.c:1605 `CG_DamagePlum_f` prints a console line and draws no plum (name).
- Confidence: confirmed.
- Status: fixed (5ac2fdc9c): unused parameter and fields removed, the 26 trap->Print( va() ) calls pass the format directly, one CG_WorldCoordToScreenCoordFloat prototype in cg_local.h (cg_aimfeedback.c had none), the sound-indicator aliases resolved to the CG_SoundIndicators_* names, banner, dead-check comment and asset note fixed, the conc-trace frame copy skipped when it is the same slot, the lkPaper ternary and the second lkXen declaration gone, rcss_lint's empty NOT_YET gone, CG_DamagePlum_f renamed CG_DamagePrint_f, benchview's va() built once. Not changed: the hand-declared QueryPerformanceCounter prototypes (they keep windows.h out of the cgame) and AF_LESSON_DEFAULT_TIME (the fallback when the cvar is 0; the demo timestamp itself is a Publish item)

### RU-25 P3 Style slips in stock functions
- codemp/cgame/cg_players.c:2067-2068 a trailing-whitespace line and `qboolean isEnemy` declared mid-block in a C
  file whose other locals are at the top.
- codemp/cgame/cg_view.c:2483-2485 three calls placed between the function's declarations (the
  `VEH_CONTROL_SCHEME_4` locals follow them); 2679 `inwater = CG_CalcViewValues(); CG_Bench_Camera();` two
  statements on one line.
- codemp/cgame/cg_ents.c:3281 a stray blank line and codemp/cgame/cg_effects.c:383 an encoding-only comment change
  are the whole diff of those files (already listed as cleanup in the 2026-10-07 review): revert both to stock.
- Confidence: confirmed.
- Status: fixed (5ac2fdc9c): isEnemy declared with the locals, the cg_view.c calls after the declarations and on their own lines, the cg_ents.c / cg_effects.c no-op hunks back to stock

### RU-26 P3 Static memory for small features
- codemp/ui/ui_roster.c:152-157, 402-404 and `uiRoster` (ui_main.c:522): about 1.5 MB of static tables in the UI
  module (folders 512 x ~900 B, variants 1024 x ~400 B twice, skins). Fine for a client; sort in place (an index
  permutation applied with one temporary) to drop `sortedVariants`.
- codemp/cgame/cg_bike.c:37-38, 75-79, 336-340: per-entity arrays for all 1,024 entities (about 150 KB) for a
  handful of bikes; a small table keyed by entity number would do.
- Confidence: confirmed; cleanliness only.
- Status: not a bug: memory only (about 1.5 MB in the UI module, 150 KB in cgame), left as is

### RU-27 P3 `cg_screenpos`: a long description loses its line end
- Where: codemp/cgame/cg_screenpos.c:87 (`text[1024]`), 193 (`"issue: %s\n"` with a description up to 1,023 chars)
- Scenario: a description near the console's limit is cut without its newline; the "when:" line joins it.
- Fix: write the description with `FS_Write` directly, then the newline.
- Confidence: confirmed; cosmetic.
- Status: fixed (5ac2fdc9c)

### RU-28 P3 Test commands ship in the UI module
- Where: codemp/ui/ui_atoms.c:104-111 (`ui_menuitem`, `ui_roster`), codemp/ui/xenui.cpp:1159-1203 (`ui_xenDump`,
  `ui_xenShot`, `ui_xenSend`, `ui_xenKey`, `ui_xenStats`, `ui_xenProfile`, `ui_xenDebug`)
- Scenario: none harmful (they act on the local UI only); `ui_menuitem` presses any item of any menu from a script.
  For a published build decide whether they stay (they are the documented agent hooks, docs/xenui-agents.md) and say
  so in the guide, or put them under a build option.
- Confidence: confirmed.
- Status: deferred: owner decision, whether the agent hooks (docs/xenui-agents.md) ship in a published build or go under a build option

### RU-29 P3 Player-facing text is hard-coded English
- Where: codemp/ui/ui_player.c:208-211, 312-334, 382-408; codemp/ui/ui_player_look.c throughout the section models
  (758-781, 840-880, 893-895, 1049-1139); codemp/ui/ui_picker.c:899-901, 939-960
- Scenario: the stock screens these replace read `@MENUS_*` string-ed references; with `se_language` set to another
  language the player screen stays English.
- Fix: a `XP_Str( "KEY", "English fallback" )` through `trap->SE_GetStringTextString`, keys in a `xenjk.str`.
- Confidence: confirmed.
- Status: deferred: design (string keys through SE_GetStringTextString and a xenjk .str asset to ship)

## Publish cleanliness

- codemp/ui/ui_player_look.c:22-23: names a private path
  (`.codex-research/wishlist-20261101/drafts/player-screen/mockup_fable.html`). Replace with a path in the tree, or
  drop it.
- codemp/ui/ui_player_look.c:22, 2441; ui_player.c:24; ui_player.h:22; tools/ui/bake_xen_player_art.py:1, 3:
  "Fable's look", "mockup_fable.html": an internal working name for the design draft. Call it "the look" / "the
  mockup".
- The xenui documents are not in the repository: `ui/xen/player.rml`, `ui/xen/player.rcss`,
  `ui/xen/player_paint.rml` (ui_player_look.c:2393-2397, 2899, 2923), the fonts in `fonts/xenui`, and the mockup
  HTML that `bake_xen_player_art.py --mockup` needs for the backdrops and paint layers. `tools/ui/rcss_lint.py`
  lints files a clone does not have. Add the sources (for example `assets/ui/xen/`), or state in
  docs/wishlist/ui-system.md that they ship only in a pack. `ui/jamp/xen_character.menu` is tracked under
  tools/characters, `xen_player.menu` under tools/ui: one place for both.
- codemp/cgame/cg_aimfeedback.c:309-314: a comment names a player handle and match ("<player> Kejim 4v4",
  2025-10-29) with that demo's timestamps; 2555, 2848, 3157: screenshot names `xen_kejim_*`; 2946-2947 a hard-coded
  flag position for one map; cg_xcvar.h:44 default `1217444` is one demo's timestamp. Remove the handle, make the
  rest data.
- Owner and review process notes in comments: cg_bike.c:311, cg_players.c:796, 8980, 11626, cg_signals.c:21,
  cg_view.c:252, cg_xcvar.h:83, cg_aimfeedback.c:316-320, ui_picker.c:643, ui_player_look.c:1691 ("owner
  2026-11-..", "review 2026-10-07", a quoted remark). Keep the reason, drop the attribution and date.
- cg_soundindicators.c:3, `.h`:3, cg_draw.c:8362, cg_local.h:1830: "Fortnite-style" (a third party's trademark as a
  description). "Directional sound indicators".
- ui_picker.c:259-260, 1295; ui_player_look.c:715: franchise names as source tabs in code ("Final Fantasy VII
  Remake / Rebirth", "Cyberpunk 2077", "Stellar Blade", "Marvel Rivals"). Move the tag and display name into
  `character.cfg` (`source ff7r "Final Fantasy VII Remake"`), so the code carries no game list.
- cg_bike.c:21-24, 46, 32 and cg_signals.c:26-31, 54-57: comments say the sounds and rig come from Cyberpunk 2077's
  own files (`sfx_container.bnk`, the CT-3X rig), with hard-coded `sound/cp_kabuki/signals/` and
  `sound/vehicles/ct3x/` paths. Accurate, but a published tree should say the assets are not included and are the
  user's own extraction.
- Licence headers. Missing a GPL block: cg_aimfeedback.c / .h, cg_conctrace_visual.c / .h, cg_soundindicators.c /
  .h, cg_pitdeath.c, ui_xenui_host.c, xenui.cpp, xenui.h, xenui_stub.c, xenui_profile/tracy/Tracy.hpp, the three
  tools/ui scripts. cg_bike.c:1-19 carries "Copyright (C) 2013 - 2015, OpenJK contributors ... part of the OpenJK
  source code" on a file written in 2026: use the xenjk block the other new files have. cg_soundindicators.h:27
  says "GPLv2-or-later"; the tree is GPL version 2 only.
- Attribution. cg_be.* cites base_enhanced and docs/ports/base_enhanced.json (present). cg_view.c:252-258 and
  cg_xcvar.h:148 credit EternalJK / TaystJK for `cg_cameraFPS`; docs/ports has no entry for either: say in the
  comment whether code or only the idea was taken (the damping maths here reads as original). `cg_enemyModel` /
  `cg_enemyColors` (cg_players.c:752-871, 2066-2090, 11624-11635) have no origin note; if they follow another
  mod's implementation, credit it. xenui.cpp:244-245 says the filter matrices are RmlUi's GL3 backend's (MIT):
  add the RmlUi copyright line beside them.
- tools/ui/bake_xen_player_art.py:38: default `C:\Program Files\Google\Chrome\Application\chrome.exe` (a stock
  install path, not private; use `shutil.which` first). Line 39: the bake fetches the two faces from Google Fonts
  at run time; say so in the docstring and name the SIL OFL licence files to ship with the atlases.
- Debug output: all gated (`cg_debugAnim`, `cg_debugEvents`, `cg_bikeSeatDebug`, `developer`, the aim-lab modes).
  cg_bike.c:274, 399, 459, 482-498 use `Com_Printf` where the file's neighbours use `trap->Print` (works; one
  style). cg_signals.c:118 prints one line per map load when a `.signals` file exists (fine).
- No absolute private paths, user names, URLs with credentials or keys in any file of the lane.
- Commented-out blocks, stale TODOs: none added by the fork in this lane.

Status of the publish items (5ac2fdc9c unless noted):
- Private mockup path and the "Fable" design name: reworded in ui_player_look.c, ui_player.c, ui_player.h and
  bake_xen_player_art.py (the mockup is "the mockup", its path is not named). Fixed.
- xenui documents: found outside the tree, in the main checkout's research folder only:
  `.codex-research/wishlist-20261101/drafts/xenui/data*/ui/xen/player.rml`, `player.rcss`, `player_paint.rml`,
  `player_paint.rcss` (data, data-c, data-e, data-f variants), copies under that folder's `game/runs/*/home_xp/`, and
  the mockup `.codex-research/wishlist-20261101/drafts/player-screen/mockup_fable.html`. Not copied in. Deferred,
  owner: which variant is the shipping one, and whether the sources go into the tree (for example `assets/ui/xen/`) or
  the docs say they ship only in a pack.
- The aim lab's player handle: removed from the comment. The screenshot names, the hard-coded flag position and the
  cg_aimFeedbackLessonTime default (one demo's timestamp): deferred, owner: the lab's demo-specific defaults are its
  current behaviour; making them data changes it.
- Owner and review notes in comments: left for the separate comment sweep (no user-visible string in the lane
  carries one).
- "Fortnite-style": now "directional sound indicators" (cg_soundindicators.*, cg_draw.c, cg_local.h). Fixed.
- Franchise names as source tabs (ui_picker.c, ui_player_look.c): deferred, owner: moving the tag and display name
  into character.cfg changes the packs' data format.
- Asset provenance comments (cg_bike.c, cg_signals.c: CP2077's own files, hard-coded sound paths): listed, not
  changed (provenance wording).
- Licence headers: the xenjk GPL-2.0 block added to cg_aimfeedback.c / .h, cg_conctrace_visual.c / .h,
  cg_soundindicators.c / .h (its "GPLv2-or-later" line gone), cg_pitdeath.c, ui_xenui_host.c, xenui.cpp, xenui.h,
  xenui_stub.c, xenui_profile/tracy/Tracy.hpp (xenjk's own stand-in for Tracy, not Tracy's code) and, as comments, the
  three tools/ui scripts; cg_bike.c's OpenJK 2013-2015 block replaced by the xenjk one (the file was added on
  2026-11-05). Fixed. The new files from e5666e69f carry it too.
- Attribution: listed, not changed (provenance wording): cg_view.c cg_cameraFPS credits EternalJK / TaystJK with no
  docs/ports entry; cg_enemyModel / cg_enemyColors have no origin note; xenui.cpp's filter matrices from RmlUi's GL3
  backend (MIT) have no RmlUi copyright line beside them. Owner: say whether code or the idea was taken.
- bake_xen_player_art.py: Chrome from PATH first (chrome, google-chrome, chromium), then the stock Windows path; the
  docstring says the faces come from Google Fonts at run time under the SIL OFL 1.1 and their OFL.txt ship with the
  atlases. Fixed.
- cg_bike.c Com_Printf calls: now trap->Print like its neighbours. Fixed.

## Liftability

- Player model warm-up (`cg_modelWarm`): 75 lines inline in cg_players.c (1863-1934) plus three touch points in
  `CG_NewClientInfo` (1985-1987, 2281-2282, 2292-2295) and one in cg_view.c:2502. Move the struct and the three
  functions to `cg_modelwarm.c` with `CG_ModelWarm_Begin( clientNum, ci, &newInfo )`, `CG_ModelWarm_Forget(
  clientNum )`, `CG_ModelWarm_Frame()`; the stock function keeps three one-line calls. The import-table slots
  (`cg_public.h:719-723`, `ui_public.h:378-381`) guarded by `cl_cgameWarmApi` / `cl_uiWarmApi` are already the
  right shape for another engine.
- xenjk animation set (`cg_xenAnims`): 195 lines inline in cg_players.c (955-1138), six fields on `centity_t`
  (cg_local.h:465-470), one on `lerpFrame_t`, and 14 call-site edits (`localAnimIndex` -> `CG_AnimSet( cent )`) in
  cg_players.c and cg_servercmds.c, plus resets of `xenAnimG2` on five ghoul2 paths. Move the block to
  `cg_xenanims.c` exporting `CG_AnimSet`, `CG_EventSet`, `CG_AnimSetForModel`; keep the per-entity state in a
  static array indexed by entity number there instead of new `centity_t` fields; the SK-18 GLA-name key already
  makes the hand resets (`xenAnimG2 = NULL` at cg_players.c:1467, 2403, 9204, 11775, cg_servercmds.c:1000-1002,
  1046-1048) unnecessary, so they can go and the stock files keep only the `CG_AnimSet( cent )` substitutions.
- Enemy model and brightskins: inline in cg_players.c (752-871 helpers, 2066-2090 in `CG_NewClientInfo`,
  8920-8921, 11279-11282, 11624-11635 in `CG_Player`). Move helpers to `cg_enemy.c`; hooks become
  `CG_Enemy_PickModel( clientNum, &newInfo, v )` and `CG_Enemy_AddBrightskin( cent, ci, &legs )`.
- Bike client: the code is in cg_bike.c, but `VH_BIKE` is tested inline in five stock places (cg_event.c:1178,
  cg_main.c:126, cg_players.c:4831, 7548-7550, 8980, 9173-9176, 9708-9715). The three `type == VH_SPEEDER ||
  type == VH_BIKE` tests can be one `BG_VehicleLeansLikeSpeeder( pVeh )` in bg; the two `CG_Player` blocks one
  `CG_BikeFrame( cent, &legs )` hook each. The CT-3X specifics (bone names, tyre radii, 108 sound paths, 48 engine
  steps, cg_bike.c:32-33, 40, 60-67, 87-100) belong in the vehicle's data (`.veh` keys or a `bike.cfg`), so a second
  bike or another mod's bike needs no code change.
- misc_model_ghoul2 and the pit death: the entity is spawned in cg_spawn.c (251-342), drawn by a 108-line function
  placed in cg_draw.c (8432-8538), typed in cg_local.h (1540-1563), stored in `cgs`, and read by cg_pitdeath.c
  (`PitNearestGiant`, 173-196, uses `scale >= 8` to find "giants"). Put spawn, draw and cleanup in one
  `cg_miscg2.c` with `CG_MiscG2_Spawn`, `CG_MiscG2_Draw`, `CG_MiscG2_Shutdown`, `CG_MiscG2_Nearest( at, minScale )`;
  the pit death then asks that file instead of reading `cgs`. The pit death's hooks in stock code are already one
  line each (cg_draw.c:8142-8145, 8599; cg_players.c:8969-8976; cg_view.c:2759; cg_spawn.c:389, 550).
- Crossing signals (cg_signals.c): self-contained, one hook (cg_view.c:2485). The sound paths are one map's
  (`sound/cp_kabuki/signals/`, 54-57): read the four names from the `.signals` header or a key so another level
  port brings its own.
- Bench, aim lab, conc-trace lab, screenpos: each is one file with one-line hooks, but they are developer tools in
  every shipped cgame (the aim lab alone is 4,171 lines and about 6 MB of statics, C12). One CMake option per tool
  (`XENJK_AIMLAB`, `XENJK_CONCTRACE`, `XENJK_BENCH`) with empty inline stubs in the headers; replace the two inline
  cvar tests in stock code (cg_draw.c:4259-4262, 4508-4510) with `CG_AimFeedback_HidesHud()`. Move the bench and
  screenpos prototypes out of the tail of cg_local.h (2137-2143) into their own headers.
- Sound indicators and base_enhanced client (cg_be): already self-contained (helper agent): cg_be lifts as
  `cg_be.*` + `be_pmove.*` + two cvars with ten marked one-line hooks; sound indicators need only the alias macros
  resolved.
- Camera FPS damping: `CG_DampCameraFPS` (cg_view.c:260-285) plus two `else if` arms and four bookkeeping lines in
  the stock damp functions. Small and marked; liftable as is. The two globals at 249 could be `static`.
- Player screen and picker: clean seams. Stock touch points are two `displayContextDef_t.ext` callbacks
  (ui_shared.h:496-497, used at ui_shared.c:4235, 6617), four hooks in ui_main.c (`UI_LoadMenus` 1510-1511,
  `UI_OwnerDraw` 3050-3053, `UI_RunMenuScript` 6224-6225, `UI_Refresh` 10142) and the ESC arm of `UI_KeyEvent`
  (10300-10301). Two things reach across: `ui_picker.c` and `ui_player*.c` declare a dozen ui_main.c / ui_saber.c
  functions with local `extern`s (ui_picker.c:54-70, ui_player.c:55-63, ui_player_look.c:50-56) instead of a
  header; and the player screen's stage API lives inside the picker (`UI_PickerStage*`, 16 functions,
  ui_local.h:399-417). Add the externs to ui_local.h, and split the stage (model load, camera, fit, sabers:
  ui_picker.c:270-751, 818-907, 1227-1292) into `ui_stage.c` that both the picker menu and the player screen call.
- xenui: the C interface (xenui.h) and the host table are a good seam; the stub keeps non-RmlUi builds working.
  The tangle is how it reaches the renderer: `FindRenderAPI` looks the renderer DLL up by file name and
  `GetProcAddress`es an export (xenui.cpp:490-515, ui_xenui_host.c:97-99) "because the installed engine's UI import
  table has no slot for it". For other derivatives add `const void *(*R_GetXenUIRenderAPI)( int version )` to
  `uiImport_t.ext` behind a `cl_uiXenApi` cvar, exactly as the warm-up slots are done, and keep the module lookup
  only as the fallback (it also fixes RU-14). The statistics and profile code (xenui.cpp:42-76, 375-488, about 170
  lines, plus `XenUI_FrameMark`) can sit in `xenui_stats.cpp` under the existing `XenUIProfile` option.
- `ui_roster.c`: pure and already liftable (it is compiled offline by tools/characters/roster_driver.c).

Status of the liftability proposals:
- Model warm-up: done (e5666e69f), cg_modelwarm.c; CG_NewClientInfo keeps CG_ModelWarm and CG_ModelWarm_Forget calls,
  cg_view.c its CG_ModelWarmCheck call.
- xenjk animation set: moved (e5666e69f), cg_xenanims.c with CG_AnimSet, CG_EventSet, CG_AnimSetForModel,
  CG_GLAFrameCount. Not done: the per-entity state stays on centity_t and the hand resets of xenAnimG2 stay; moving the
  state to a static array and dropping the resets changes when the set is worked out again, which needs a run.
- Enemy model and brightskins: done (e5666e69f), cg_enemy.c; hooks CG_Enemy_PickModel and CG_Enemy_AddBrightskin (the
  brightskin copy stays in CG_Player, where the model is drawn).
- Bike client: not done. The two CG_Player blocks are already one call each; the three VH_SPEEDER || VH_BIKE tests
  need a bg helper (game lane), and data-driving the CT-3X specifics is a design change.
- misc_model_ghoul2: done (e5666e69f), cg_miscg2.c with SP_misc_model_ghoul2, CG_DrawMiscG2Models, CG_MiscG2_Shutdown,
  CG_MiscG2_Nearest; the pit death asks CG_MiscG2_Nearest. The instances stay in cgs (zeroed with it at CG_Init).
- Crossing signals' sound names from data: not done (a data format change for the level port).
- Developer tools: the prototypes moved to cg_bench.h and cg_screenpos.h, the two inline aim-lab tests in cg_draw.c
  are CG_AimFeedback_HidesDisconnect / HidesCenterPrint (e5666e69f). Not done: the CMake options with stubs (what a build
  ships is an owner decision).
- Sound indicators: the aliases resolved (5ac2fdc9c). cg_be: nothing to do.
- Camera FPS damping: the two globals static (e5666e69f).
- Player screen and picker: the local externs moved to ui_local.h (e5666e69f). Not done: ui_stage.c; the stage and the
  picker menu share the picker's state struct (variant, team, load state, dwell, fit), so a split needs that struct
  in a shared header or an accessor API, more than a move.
- xenui: not done. The R_GetXenUIRenderAPI import slot is an engine change (cl_ui.cpp, engine lane); the statistics
  code shares xenui.cpp's anonymous-namespace frame state, so xenui_stats.cpp needs that state exposed first.
- ui_roster.c: nothing to do.

## Still open from earlier reviews

- C5 (cg_localents.c score plums: a negative plum is white with no minus). Still as described.
- C7 (cg_draw.c:4508: `cg_aimFeedbackLesson` hides centerprints in live games).
- C9 (cg_aimfeedback.c:2215, 2809, 2853, 2897, 3215: `targetNum` / `carrierNum` from cache files unchecked).
- C10 (cg_conctrace_visual.c:155: `EV_CONC_ALT_IMPACT` not filtered by owner).
- C12 (cg_aimfeedback.c:244, 272: about 6 MB of statics in every cgame).
- The 2026-10-07 cleanup candidates: one demo's timestamps and screenshot names and the cgame-issued `quit`
  (cg_aimfeedback.c:2416, 3045, 3104), the sound-indicator alias defines, the conc-trace panel hooked every frame,
  the `cg_effects.c` / `cg_ents.c` no-op diffs.
- Verified fixed by the helper agent: C3, C8, C11. C1, C2, C6 (cg_players.c) read as fixed in the diff
  (`CG_IsEnemyClient`, `CG_CheckEnemyModels`, `CG_EnemyColor` with a checked parse). SK-18 (the animation-set cache
  keyed on the GLA name) is in master (cg_players.c:1094-1107).
