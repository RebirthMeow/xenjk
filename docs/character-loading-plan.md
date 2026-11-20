# Character loading and picker: implementation plan (2026-10-31)

Owner: "make the full plan first, with the interface etc". The why and the measurements are in
[character-loading-and-picker.md](character-loading-and-picker.md) (design); this file is the how and the order. Evidence
for every number here: `.codex-research/model-switch-20261031/NOTES.md`.

## 0. Where it stands

| milestone | state |
|---|---|
| M0 stage timers | done: `LOADPROF` stage slots, `stage_split.py`; the stages sum to the frozen frame within 1% |
| M1 prepared data | done: tangents shared by mesh, hair and cloth from one cache, pipeline cache kept between launches; warm Tifa -> Aerith 1126 -> 369 ms (textures 176 of it), every cached and consumer tangent byte-identical |
| M2 async loader | M2a installed (Install 58): pk3 read-ahead on a worker, stepped warm-up, userinfo held until loaded; a 936 MB cold pack reads in the background with no frame over 9 ms, but a step is one shader (4K hero 75-95 ms, an 8K skin 478 ms), so the 50 ms target moves to M3's sliced uploads; M2b (other players) built (3.5), not measured in game |
| M3 mip streaming | built and measured on the dev renderer (section 4), not installed: textures no longer cost a switch frame (Witchy's 38 in 5 ms, was 478 ms for one shader); worst frames seen-before 51-127 ms are all model, cloth and hair builds; warm full detail 1.3-1.5 s, from the HDD at disk speed; 13 heroes stayed inside a 600 MB pool; screenshots match unstreamed (detail ratio 1.00) |
| M3 mesh steps | built on the dev renderer (section 4.1), not measured in game (owner: no GPU runs for now): the model loaded once per switch instead of four times (a `va()` cache-key bug), buffer uploads without GPU waits, the GLM packed on a worker thread, hair buffers built in warm-up steps, a table CRC for hair assets |
| M4 character picker | M4a data built (5.4): the roster grouping in the UI module (`ui_roster.c`, console `ui_roster`), 104 manifests in `zz_xen_characters_v1.pk3` (not installed), `tools/characters/roster_check.py` passes on the installed folders (353 grid entries -> 71 characters, 131 variants, each entry once); no portraits (dropped 2026-11-01, see 5.3). M4c screen built (5.6.1), not seen in game; M4b, M4d, M4e not started |

All code so far is on `renderer/load-profile-stages` (worktree `.claude/worktrees/load-profile`, from 4d212187), merged
into `renderer/pbr-materials` on 2026-11-01 (b63c9dbb; the merged engine, renderer, UI, cgame and game build clean in
`.codex-research/loading-merge-20261101/`, not run in game). New loading work merges again after each install, so an
install from either branch keeps it.
M0-M2a are installed (Install 58); the engine (Install 59). **Install 61** (2026-11-01, from 4e237143): the renderer
(M3 streaming, mesh steps, M2b), the UI with the picker (M4c), the cgame, `zz_xen_characters_v2.pk3` and the cooked
`zz_xen_tangents_v1.pk3`; install smoke I61 passed 9 of 9 on installed files only (`docs/renderer-vulkan-in-xenjk.md`).

**Owner:** the wishlist session from the morning of 2026-11-01; back in its own session ("what branches do we have")
from the afternoon of 2026-11-01 (owner: the wishlist has too many items to do any one of them right).

**2026-11-01 (wishlist session):** the engine at fdba4f81 is installed as **Install 59**
(`xenjk.x86.exe` only; previous exe in `.codex-trash/20261101T0436-gamedata-pre-install59/`). The first in-game checks
used the installed exe with the dev renderer and dev UI/cgame from a private home, one game-lock slot each, labelled
functional (the PC was not quiet). Harness and records: `.codex-research/wishlist-20261101/gpu-window/`.
- **Switch check S1, pass.** Tifa -> Aerith -> Tifa -> Aerith all went through the warm hold (751, 176, 16 ms). After
  the first load no frame was over 50 ms (worst 10-15 ms).
- **Picker check P4/P5, pass, 9 of 9.**
  - `cl_uiWarmApi` / `cl_cgameWarmApi` were 1.
  - The roster equalled the listing (71 characters, 131 outfits).
  - Down x3 / Right x2 / Enter applied `imperial/officer` through the hold (about 100 ms).
  - Six models were browsed with no 4-model cap.
  - Screenshots came from the engine's `screenshot_png`.
- **Apply still does a second warm-up.** The preview warms 1 skin / 9 shaders; the player form needs 3 skins / 18
  shaders (the team skins), about 100 ms over 11 frames. Next: warm the resting outfit in player form.
- **One hang, P2.** After Apply the main thread stopped answering for 13 s and ignored WM_CLOSE. It was not
  reproduced in P3-P5, and the cause is not found. The suspects are the untimed main-thread waits on workers,
  `vk_pipeline_async.cpp:198` and `vk_mesh_jobs.cpp:288` (from `tr_ghoul2.cpp:2770`). Read again on the
  afternoon of 2026-11-01: neither has a deadlock; each waits only as long as its worker's compile or pack takes. Both
  now wait in 2 s slices and print a warning naming the pipeline (def, pass) or the model and phase each time; built
  (`build.hangwait.log`, ffb74889). Run on it the same afternoon (PC busy with other sessions' clients, one lock slot
  each, functional only): **P6 pass 9 of 9** (hold 107 ms) and **P7 pass 9 of 9** with P2's PrintWindow captures
  (`--printwindow-probe`: five captures, 0.06 s each, the last after Apply; hold 99 ms). No wait warning in either log;
  the main thread's mesh builds were 53-112 ms. Not reproduced in four runs since P2 (P4-P7). If it comes back
  without one of the warnings, it is elsewhere on the main thread (a cold HDD read is the next suspect).

Left for the 50 ms goal after M3: the model step (GLM VBO build, 40-95 ms seen), the cloth and hair asset steps (30-125
ms) and the first draw's cloth/hair instance setup; and first-ever loads (tangents 430-680 ms, new pipelines up to
1 s) until prepared data is cooked ahead of play. These are mesh data, the M1/M2 prepared-data line, not textures.
Section 4.1 is the work on them (built, not yet measured).

Goal, end to end: changing character never freezes a frame (no frame over 50 ms in `switch_time.py`, cold or warm),
the new character is on screen within 100 ms at reduced texture detail and sharp within 1 s warm (3 s cold from the
D: HDD), and the picker shows any installed character and outfit live with up/down and left/right.

## 1. Workstreams and order

```
M1 finish ──► M2 async loader ──► M3 mip streaming ──► M4d live browsing ──► M5 install
                    │                                        ▲
M4a data ──► M4b stage ──► M4c picker screen (icons) ────────┘
```

- M1 and M2 come first: everything later loads through the M2 queue.
- M4a-M4c (data, stage rendering, the screen with icons and today's loader) do not depend on M2/M3 and can run
  beside them in their own branch; they touch `ui/`, the menu pack and the renderer's stage mode only.
- **Live 3D browsing (M4d) needs M3.** Nothing frees a model or image mid-map (JKA purges only at a map load), so a
  picker that loads every character it shows at full detail fills VRAM: 22 BC7 heroes filled 12 GB when the menu icons
  were shot. With M3 the browsed characters sit at their mip tails (tens of KB a texture) and only the selected one
  streams up.
- Branches: `renderer/load-profile-stages` (M0-M1), then one branch per milestone off it. The Watson session edits
  `qcommon/cm_*.cpp` and `common.cpp` in the main checkout: M2 must not touch those files (its engine part is
  `files.cpp`, `cl_ui.cpp`, `cl_cgame.cpp` and new files).

## 2. M1 finish: prepared data

| task | files | outcome |
|---|---|---|
| 1.1 Cloth GPU setup (~60 ms) | `vk_cloth.cpp` | done differently: the time was a third MikkTSpace pass (`Cloth_VkAsset`, LOD 0's cloth surfaces), not the tables (colouring is under 1 ms at 3.6k particles). It now reads LOD 0 from the tangent cache: `cloth_gpu` 60 -> 6 ms, all 6 surfaces equal to their own computation |
| 1.2 Shader/skin parse cost | `tr_shader.cpp` | measured: new `shaders` and `mapprobes` slots; the unnamed-map probes cost 4 ms, so no lookup cache; ~60 ms of shader creation besides images stays (parse, FinishShader) |
| 1.3 Commit M0+M1, regenerate `docs/xenjk-cvars.md` with `tools/cvar_list.py` | | committed on the branch |
| 1.4 Stable tangent key, cooked tangents (2026-11-01) | `vk_tangent_cache.cpp`, `tools/characters/cook_tangents.py` | the version 1 key hashed the loaded GLM, whose header and surface hierarchy hold this session's GLA and shader handles (`R_LoadMDXM` writes them before the build), so a cached file was found only when the map and load order repeated; version 2 hashes each LOD's counts, triangles, vertices and texture coordinates. The cooker builds the files outside the game from the renderer's own source (extracted, MSVC x86 with its flags): all 177 installed player GLMs in 78 s (140 files, 195 MB; short-name copies share one), and every one the game had written itself (30) byte-identical. Not installed: needs the version 2 renderer and the owner's go-ahead (pack `zz_xen_tangents_v1.pk3`, `.codex-research/character-picker-20261101/`) |

## 3. M2 asynchronous loader

Revised 2026-10-31 before any M2 code, from two facts found while starting it:

- On a listen server the game module's model change (`SetupGameGhoul2Model`) does the whole client load in the
  server frame: `RE_RegisterServerSkin` hands over to `RE_RegisterSkin` once the client runs, so the skin's shaders
  and images load there, before cgame hears of the change. Keeping the old model drawn in cgame alone would not
  remove the freeze.
- The client is a 32-bit (large-address-aware) exe: holding a 773 MB pack's bytes in memory is not an option.

So M2a, the local player, is:

1. **Cache warming, not buffers.** `FS_PrefetchDir( prefix )` resolves every pk3 entry under the prefix on the main
   thread (the search order of `FS_FOpenFileRead`); a worker thread reads those byte ranges with its own handles into
   a scratch buffer and drops them, so the OS file cache holds them. `FS_PrefetchDone( id )`; `FS_Restart` drains it.
2. **Warm-up in steps.** `re->WarmModel( "name/skin" )` and a per-frame `re->WarmStep( budgetMs )`: read the model's
   `.skin` files (the userinfo's skin, red, blue), take their shader names, prefetch the model's folder and every
   folder those shaders' images live in, then `RE_RegisterModel` the GLM, then `R_FindShader` the skins' shaders one
   at a time under the budget, then `RE_RegisterSkin` each skin. The largest step is one shader's textures (four 22 MB
   maps, ~35 ms warm).
3. **Hold the userinfo.** `CL_CheckUserinfo` keeps the userinfo back while the `model` value changed and its warm-up is
   not ready (cap `cl_modelWarmTimeout`, default 10 s), then sends it. The server's registration and cgame's then find
   the model, skins, shaders and images loaded. Covers the console, the menu and any other path.

M2b, other players (bots, remote players joining or switching): cgame keeps drawing their current model and asks for
the warm-up through its `ext` table, as 3.3 below. The rest of this section is the original M2 design; 3.1's
in-memory buffers are replaced by item 1.

### 3.1 Engine: resolve and read

- `FS_ResolveForRead( qpath, &where )` (files.cpp, main thread): the pk3 path, the entry's local-header offset
  (`fileInPack_t::pos`), sizes and compression; loose files resolve to their OS path. The search order is exactly
  `FS_FOpenFileRead`'s, so the bytes are the ones a synchronous load would read.
- `sys_ioworker` (new, `qcommon/io_worker.cpp`): one thread, its own file handles, a priority queue of byte-range
  reads into malloc'd buffers; stored entries are a plain read, deflated ones inflate on the worker (zlib is
  thread-safe per stream). Requests: `IO_Request( where, priority ) -> id`, `IO_Poll( id )`, `IO_Cancel( id )`,
  `IO_Release( id )`. No Hunk/Zone use, no `Com_Printf` from the worker (results carry their errors).
- Exposed to the renderer through `refimport_t` (`ri.IO_*`), to cgame and UI only indirectly (they ask the renderer).

### 3.2 Renderer: model requests

- `R_RequestModel( name, priority ) -> handle` and `R_ModelState( handle )`: `queued -> reading -> building ->
  ready (tail mips) -> sharp`; exported through `refexport_t` and on to cgame/UI through their `ext` import tables
  (`ui_public.h`, `cg_public.h`; `UI_API_VERSION`/`CGAME_API_VERSION` unchanged, `ext` grows at its end).
- A request reads every file the model needs: GLM, GLA if new, `.skin`, its shaders' texture files (tails first in
  M3; whole files in M2), `.cloth`, `.chains`, `.joints`, prepared records.
- **Integrate step** once a frame, before the scene: parse and build whatever finished reading, under
  `r_streamBudgetMs` (default 2) and a per-frame upload byte budget; uploads go through a transfer command buffer with a
  fence polled in later frames (no `vkQueueWaitIdle` on this path, unlike `vk_end_command_buffer` and
  `Hair_VkUpload` today).
- **Pipelines** for the model's materials are created in the integrate step, before the model is `ready`, so a first-
  ever compile never lands in a draw.
- The synchronous `RE_RegisterModel` / `RE_RegisterSkin` stay for everything else and become "request + wait" for a
  model that is already queued, so nothing loads twice.

### 3.3 cgame and server

- `clientInfo_t` gains `pendingModel`/`pendingSkin` and a request handle. `CG_NewClientInfo` for a model not `ready`
  requests it and keeps drawing the current model (or `DEFAULT_MODEL` for a client never seen) until it is; then
  `CG_RegisterClientModelname` runs on loaded data. Replaces Q3's `deferred` client info.
- Sounds (`CG_LoadCISounds`) request through the same queue (a trap in the cgame `ext` table); the previous set plays
  until they are in.
- Server (listen server, `SetupGameGhoul2Model`): needs the skeleton and mesh for saber/hit traces, not images. It
  waits on the GLM part of the request only (the GLM read plus the prepared mesh: tens of ms warm); dedicated servers
  are unchanged.

### 3.4 Done when

- `switch_time.py`: no frame over 50 ms for Tifa -> Aerith, -> Gentiana, -> Tux, cold (a pack not read since boot) and
  warm, on showcase and on the Watson slice.
- The old model stays drawn until the new one is ready (screenshot sequence in the harness), and nothing loads twice
  (`LOADPROF models` count 1 per model).

### 3.5 M2b as built (2026-11-01, not measured in game)

Other players (bots, remote players) load a new model the way the local player's does, in warm-up steps, without the
userinfo hold (the server sends their change at once). The deferred engine read (3.1) and request queue (3.2) were
not needed: the M2a warm-up already is that queue.

- **cgame** (`cg_players.c` `CG_ModelWarm`): when another player's configstring names a model and skin that player
  does not have, cgame asks the renderer for a warm-up (`trap->ext.R_WarmModel( "model/skin", WARMMODEL_OTHER_PLAYER
  )`) and registers the model they had (`DEFAULT_MODEL` for a player not seen before, team skin validated) instead;
  `CG_ModelWarmCheck`, every frame, runs `CG_NewClientInfo` again once the warm-up is ready, failed or older than
  `cg_modelWarmTimeout` (10 s), and the real model registers from loaded data, without Q3's `cg_deferPlayers`
  deferral. Off for the local player, during a level load, and with `cg_modelWarm 0`. The rest of the client info
  (name, team, sabers) applies at once.
- **Engine** (`cl_cgameapi.cpp`): `R_WarmModel` and `R_WarmState` appended to the cgame import table's `ext`, NULL
  when the renderer has no `WarmModelEx` (`r_modelWarmupApi 2`); the ROM cvar `cl_cgameWarmApi 1` tells a cgame the
  table is that long, so a new cgame on an old engine reads nothing past its end.
- **Renderer**: `R_WarmModelEx` (the local player's `WarmModel` is `WarmModelEx( name, 0 )`): another player's job pins
  no textures (only the local player's model is held at full detail) and warms only the skin cgame asked for (the
  team skin it will use); another player's request for a model and skin such a job has shares it, and one whose GLM
  is built and whose skin is registered is ready at once. The local player's requests never share (each pins
  afresh). A player GLM registered while a map runs builds on the mesh worker like a warm-up's; "runs" means a world
  scene was rendered since the last registration or world load (`R_MeshJobsLevelLoad`: `RE_BeginRegistration`,
  `RE_LoadWorldMap`, `C_LevelLoadBegin` set it, `RE_RenderScene` with a world clears it), so map loads, demos and
  `vid_restart` build at once as before. `R_WarmStep` steps such a job when no warm-up runs (one the server alone
  registered).
- **Listen server**: the game module registers the new model and skin in the server frame, before cgame hears of it.
  `RE_RegisterServerSkin` used to hand that to the client's `RE_RegisterSkin` (shaders and images, the freeze). For a
  skin the client has not registered it now builds a surfaces-only skin under its own key (`%` + the name): the
  surface names, `*off` where the file says so and the default shader elsewhere, all `G2_SetSurfaceOnOffFromSkin`
  reads. The GLM registration there now queues its mesh build (above).
- Review (independent, 2026-11-01) fixed before commit: the first version tied "a map runs" to
  `C_LevelLoadBegin`/`End`, which `CL_ShutdownAll` undid in a listen server's map load and which other load paths
  never call (every map load would have queued its player models and finished them in the first frame); a
  repeated local request reused a finished job and lost its texture pin; a deferred client's "old model" was one
  never loaded (now `DEFAULT_MODEL`); the surfaces-only skin's `*off` test was case-sensitive.
- Known gaps: a team or saber change during the wait loads the old model's other team skin at once; a listen
  server's bots get a `%` skin beside the client's (two `MAX_SKINS` slots per skin); a job only the server uses is
  stepped only between warm-ups (a draw finishes it at once anyway).
- Still synchronous: the GLM file read and parse in the server frame (tens of ms for a hero GLM, unmeasured), a
  never-seen player's `DEFAULT_MODEL` the first time, and cgame's sound registration once the model is warm (from the
  OS cache: the warm-up read the voice folder ahead).

## 4. M3 mip streaming

As built (2026-10-31, `vk_stream.cpp`; revised from the first draft, which kept every image at full size and moved
its view's `baseMipLevel`: that saves no VRAM, since a VkImage's memory is bound whole at creation):

- **Engine** (`files.cpp`, `fs_prefetchApi 2`): `FS_RangeOpen` resolves a file the way `FS_FOpenFileRead` does and
  keeps a stored pk3 entry's pack path and data offset; `FS_RangeRead` reads part of it on the calling thread,
  `FS_RangeReadAsync` on the prefetch worker, which serves range requests ahead of the next 4 MB block of a folder
  read-ahead. `FS_PrefetchDirEx` reads only the first 4 KB and last `R_StreamTailBytes()` of the entries with an
  extension. Loose and deflated files get no handle and load as before.
- **Tails first.** A character DDS (`models/players/`, mipmapped, larger than `r_streamTailSize`, default 256) reads
  its header and its levels at or below 256 by range and becomes a small image of those levels: about 85 KB of a
  4096^2 BC7 map's 22 MB. The tail image is permanent, the fallback every larger level is built from. The warm-up
  reads ahead only the heads and tails of a streamed folder's DDS files, so a shader step costs its tails.
- **Builds.** A sharper level is a new VkImage of the levels from it down, in its own allocation. Its first frame
  copies the levels the bound image has on the GPU; the missing levels arrive in slices of at most `r_streamSliceMB`
  (4: block rows of a level), read by the worker straight into one of eight slots of a mapped staging buffer, and
  copied by the frame's own command buffer before the first render pass, at most `r_streamUploadMB` (16) a frame.
  The finished image is transitioned and swapped in for that frame's draws through its own descriptor set (a set a
  frame in flight may have bound is never written); the replaced image is destroyed two frames later.
- **Wanted level** from on-screen size, per Ghoul2 surface added to a view: pixels per world unit at the entity's
  distance (viewport height, fov) times the model scale times the surface's world units per UV unit (each triangle's
  largest stretch, held to 16:1 as anisotropic filtering samples a thin hair card; the area-weighted 90th percentile
  of its triangles, computed once per surface). The level is the smallest with that many texels,
  `r_streamBias` levels sharper; the largest of the last 0.5-1 s counts. The warmed model (the client's own, through
  `R_WarmModel`) is pinned at full detail.
- **Pool** `r_streamPoolMB` (0: half the device-local heap) counts the images above their tails. A build that does
  not fit first sends images nothing drew lately back to their tails, least recently drawn first, else builds a
  smaller level. Non-streamed images (world, UI, lightmaps, the render scene) are outside it.
- `streaminfo [all]` lists the state; `LOADPROF stream` is the per-frame cost; developer prints report each build
  and when the pinned model is sharp.
- Done when: a new hero is visible within 100 ms of its request, sharp within 1 s warm / 3 s cold; VRAM stays within
  the pool with 30 heroes requested in one map; no visible mip pop on the player's own model in third person (it is
  always wanted at full detail).

### 4.1 Mesh steps (after M3; built 2026-10-31, not yet measured in game)

The model, cloth and hair steps left after M3 (worst seen-before frames 51-127 ms) came from four things, and each has
a change:

- **The GLM was loaded four times per switch.** The warm-up passed `va()` strings to `RE_RegisterModel`, and the
  model cache inserts its key after the load, by which time the loader's own `va()` calls (4 rotating buffers) had
  overwritten it. Every later lookup missed: the cloth step, the hair step and cgame/server each built the mesh
  again, with its own GPU buffers. That was M2a's warm-up (twice per switch, so Install 58 has it too) and M3's cloth
  and hair steps (four times). `R_LoadMDXM`'s own `.gla` registration did the same. `RE_RegisterModel` and
  `RE_RegisterServerModel` now copy the name first.
- **Every buffer upload waited for the GPU.** `R_CreateVBO`/`R_CreateIBO` and the cloth and hair uploads called
  `vkQueueWaitIdle` per buffer, with a frame in flight (eight to ten waits per hero, `mdxmupload` 28-89 ms).
  `vk_upload.cpp` copies into a 64 MB persistently mapped ring and records the copies into fenced batches, flushed ahead
  of every later submission (the frame's, one-shot command buffers, `vk_wait_idle`).
- **The mesh build ran in one frame.** The warm-up's GLM is now packed off the main thread (`vk_mesh_jobs.cpp`, cvar
  `r_meshWorker`): the worker hashes the GLM for the tangent cache and packs every LOD (`R_PackMDXMLod`), computing
  tangents there on a first-ever load (430-680 ms of MikkTSpace moved off the main thread). The main thread reads cached
  tangents and uploads a LOD per step. The packing is the old one's, byte for byte (checked offline on three heroes'
  GLMs). A draw of a model still being built finishes it at once, as before.
- **Hair.** The `.chains` CRC is table driven (Zayne's 10 MB asset: 72 -> 3 ms, same result), and the hair substitute
  buffers the first draw used to build (14-30 ms) are built in warm-up steps, a LOD a step, for the model's own GLA.

Warm-up order: read ahead, GLM (job queued), cloth asset, hair asset, shaders (no pipelines), the draws' pipelines
queued, mesh upload (a LOD a step), hair buffers (a LOD a step), wait for the pipelines, skins. `r_loadProfile N` (N > 1) prints `LOADHITCH` lines: each frame longer than N ms with the
load stages that ran in it.

- **Pipelines** (`vk_pipeline_async.cpp`): a pipeline the driver never compiled costs 100-200 ms where it is first
  created (Zayne's first load: one shader step 531 ms, 6 compiles 1058 ms); from the saved cache it is ~0.5 ms. The
  warm-up's shader steps compile none (a Ghoul2 VBO draw never binds a shader's own defs as they are), and a
  pipeline step asks a worker thread for the ones a Ghoul2 draw derives from them (`vk_request_ghoul2_pipelines`:
  every stage in the main pass, glow stages in the glow pass, the first stage in the depth prepass, the sun shadow
  views and the occluder pass, each only when that pass is on). Not predicted: fog, mirrors, UI model views (the
  HUD, the picker), $screenmap and the hair opacity map; those still compile where first bound. The model is ready once they
  are installed (installed a frame at a time on the main thread). A draw that binds one still queued compiles it itself;
  one on the worker it waits for. A prediction miss compiles at the draw as before and prints
  `pipeline def#N pass P: compiled where it was first bound` (developer 1) for the next measurement.

Still to measure: every step and frame in `switch_time.py`, and which pipelines the prediction misses. Expected left:
Zayne's deflated 10 MB `.chains` is inflated on the main thread (about 20 ms with zlib, measured offline), so its hair
step should land near 30 ms.

## 5. M4 character picker (the interface)

### 5.1 What it is

A full-screen character screen that replaces the 64 px icon grid as the way to choose a model, in game (over the
running game) and from the main menu. Characters down the left, the selected one live on a stage in the middle,
outfits on left/right under it, details and Apply on the right.

```
+--------------------+---------------------------------------------+------------------+
| [ search......  ]  |                                             |  AERITH          |
| All FF7R CP2077    |                                             |  FF7 Rebirth     |
| Stellar JKA Other *|              live model, idle               |                  |
|--------------------|        studio light, floor shadow           |  Outfit          |
|   Tifa          7  |                                             |  Tux    4 / 10   |
| > Aerith       10  |         drag: turn   wheel: zoom            |                  |
|   Gentiana         |         F: face      A: animation           |  Team colour     |
|   Eve           7  |                                             |  (o) (o) (o)     |
|   Judy          2  |                                             |                  |
|   Panam         2  |    <   Original . Costa . Tux . Loveless >  |  [ Apply ]       |
|   ...              |                                             |  [ Back  ]       |
+--------------------+---------------------------------------------+------------------+
```

### 5.2 Controls

| input | action |
|---|---|
| Up / Down, mouse wheel over the list, W / S | previous / next character (wraps) |
| Left / Right, A / D, the arrows under the stage | previous / next outfit of this character (wraps) |
| Click a row / a dot | select that character / outfit |
| Drag on the stage | turn the model; released, it eases back to three-quarter view over 0.4 s |
| Wheel on the stage | zoom between full body and upper body |
| F | face close-up and back |
| Space | next animation: idle, saber stance (blade lit), run in place, taunt |
| 1 / 2 / 3 | team colour default / red / blue (where the game type allows a choice) |
| Typing | filters the list by name (Backspace edits, Esc clears the filter first) |
| Tab / Shift-Tab | next / previous source tab |
| Ctrl+F or the star | favourite / unfavourite (kept in `ui_favoriteCharacters`) |
| Enter | Apply |
| Esc | Back without a change |

Mouse and keyboard only; a gamepad maps onto the same actions later.

### 5.3 States

| state | what the stage shows |
|---|---|
| browsing, model not requested yet | the variant's icon (the existing 64 px one) |
| requested, reading/building | the icon with a progress ring |
| ready (tail mips) | the live model fades in over 150 ms |
| sharp | no indicator |
| failed (missing file, bad model) | the icon, a one-line reason, Apply disabled for that variant |
| applied | "Applied" under the button; in game the player switches with no pause |

No portraits (owner, 2026-11-01): with the warm-up and streaming the live model is the point, and the gap before it
(warm full detail 1.3-1.5 s, tails sooner) is short enough for the icon and the ring. Full-body stills would be ~131
GPU shots tied to M4b's lighting. Bring them back only if the gap looks bad in game.

### 5.4 Data

- **`models/players/<folder>/character.cfg`**, one per selectable folder:

  ```
  character	aerith
  name		"Aerith"
  variant		"Tux"
  source		ff7r
  order		40
  framing		64 18		// stage height (units) and face-view height from the feet
  idle		BOTH_STAND1
  teams		default red blue
  ```

  Written by the install pipeline beside each short-name copy
  (`.codex-research/model-icons-20261029/scripts/short_names.py`); shipped in the names pack.
- **Fallback** for folders without one (stock JKA, older converts): the character is the folder name up to the first
  `_` (the second for prefixed sources, `cp_judy_2yl` -> `cp_judy`); a stock model with several skins becomes one
  character whose skins are its outfits.
- **Icons**: the roster and the stage's placeholder use the existing 64 px icons (`icon_<skin>`); no portraits (5.3).
- Built once at UI start like `UI_BuildQ3Model_List` today, reading only the small files.
- As built (2026-11-01, M4a): `codemp/ui/ui_roster.c` groups the grid's own entries (`uiInfo.q3HeadNames`, so the
  roster lists exactly what the grid lists) and reads each folder's `character.cfg`; its header documents the keys.
  Beyond the draft above: `variantorder` (a folder's place among its character's variants), `skin <name> "<variant>"`
  (other skins of a folder), `hide`, and (2026-11-19) the source tabs' `sourcelabel`, `sourcename` and
  `sourceorder`: the tabs come from the tags the installed folders declare, with only `jka` and `other` built in.
  Team colours come from the folder's listed `red`/`blue` (and `red_X`/`blue_X`)
  skins, so there is no `teams` key. The fallback groups a folder under the longest listed folder its name starts
  with up to an underscore (only when that folder exists: `hoth3_han` stays its own character). The grouping is pure
  C, so `tools/characters/roster_check.py` compiles the same file with gcc and checks it against the installed packs
  (`--manifests DIR` for files not installed yet); `ui_roster` in the console prints the same format.
- Manifests: `tools/characters/manifests.py` holds the table (our 53 named converts, Jan HD and the stock JKA
  folders) and writes `zz_xen_characters_v1.pk3` (cfg files only, 29 KB; built under
  `.codex-research/character-picker-20261101/`, not installed). Crossover outfits (`aerith_sylus`, `tifa_sylus`)
  list under the character (owner decision 2 still open; one table column to change). Variant names the records do
  not explain keep the folder code: Judy `2YL`, Alt `U2`, Evelyn `BD`, Eve `Outfit 14` / `Outfit 16`.

### 5.5 Stage rendering

- A stage mode for world-less scenes (`RDF_STAGE` beside `RDF_NOWORLDMODEL`): the `r_portraitRig` key/fill/rim/top
  layout driven by the scene (subject distance from the framing), a small built-in studio environment for
  reflections, a floor disc with a contact shadow, and the character fill/rim terms on (they are off in world-less
  scenes today, which is why menu models look flat).
- Cloth and hair step on the menu's clock; the model stands on a fixed origin.
- Aspect: the menu is a 640 x 480 canvas stretched to the window; the picker lays out in real pixels (the panels
  pinned to the edges, the stage keeping its aspect) so 16:9 and 4K are not stretched.

### 5.6 Loading while browsing

- The stage requests the selected variant at high priority, the neighbours (next/previous character, next/previous
  outfit) at low priority; moving on cancels requests not yet reading.
- Before M2/M3 (M4c): the picker shows icons only and loads the live model when the cursor rests 0.5 s, with a
  cap on distinct live loads per map (default 4) so VRAM cannot fill; Apply then loads as today.
- With M2/M3 (M4d): live models for every variant the cursor reaches, tails only, the selected one sharp; leaving the
  picker drops its requests except the applied model.

### 5.6.1 M4c as built (2026-11-01, built, not seen in game: no GPU runs yet)

- `codemp/ui/ui_picker.c`, the screen `tools/characters/xen_character.menu` (menu `xen_character`, shipped as
  `ui/jamp/xen_character.menu` in `zz_xen_characters_v2.pk3` with the manifests; the UI parses it beside every menu
  set when a pack has it). Opened by the console command `ui_characters` (owner decision 5, a default key, still
  open: bind one). Layout as 5.1 on the 640 x 480 canvas: roster, stage, outfit strip, details, Face / Apply / Back.
- Ownerdraws 400-403 (roster, stage, outfits, details). The keys of 5.2 go through a menu-level hook
  (`displayContextDef_t::ext.menuHandleKey`, NULL in cgame): Up/Down/W/S/wheel, Left/Right/A/D, 1/2/3, F, Enter,
  clicks on rows, the strip's halves and the colour chips; Esc is the menu's `onESC`. Not yet: drag to turn, wheel
  zoom, animation cycle, search, tabs, favourites (M4e). Owner 2026-11-01: the roster shows that it goes on: its
  last 3 rows before an edge with more characters past it fade (1, 0.7, 0.4, 0.15 towards the edge), and the
  selection stays 3 rows clear of the edges while it can.
- Stage: the selected outfit's model once the cursor rests 0.5 s; until then its icon (no portraits, 5.3). With the
  engine's appended UI imports (`R_WarmModel`/`R_WarmState`, ROM `cl_uiWarmApi`) the renderer warms it first
  (`WARMMODEL_OTHER_PLAYER`), so browsing is the M2 path, not "today's loader"; without them at most 4 distinct
  models per UI session register live (5.6's cap). Framing from `character.cfg` (`framing`, default 70 units),
  fov 30, three-quarter view; F frames the face.
- Apply sets `model` as the grid does (with the chosen team skin) and returns to the game; the engine then holds the
  userinfo until the model is loaded (`cl_modelWarm`). Owner decision 3 (apply at respawn) not taken: at once.
- Checked offline: the menu's keywords and symbols against the parser's tables in `ui_shared.c` (a scratch check that
  passes the stock player menu apart from cvar-list values). The cc.py scripted check of 5.7 waits for GPU runs.
- Review (independent, 2026-11-01) fixed before commit: from the main menu a non-fullscreen menu is closed on the next
  frame (the client reopens the main menu), so `ui_characters` makes it full screen and opaque when disconnected and
  a 60% overlay in game; Esc on a non-fullscreen menu closed every menu and left the key catcher set (now the
  picker's, through `UI_KeyEvent`); a failed model load leaked its Ghoul2 instance; Enter applied even with Back
  focused; a team colour the new outfit lacks is dropped; no idle animation is parsed without an `animation.cfg`
  (the parser takes pool memory before opening the file). Left: a pure server that excludes the pack hides the menu.

### 5.7 Sub-milestones

| step | content | done when |
|---|---|---|
| M4a data | `character.cfg` parser and grouping; manifests for the 53 named folders | the roster lists every installed character once with its outfits; a listing check script compares it with the folders |
| M4b stage | `RDF_STAGE`, rig, environment, floor shadow, sim on the menu clock | stills of 5 heroes in the stage, owner-reviewed; no PBR flatness |
| M4c screen | `ui/jamp/character.menu`, roster/variant/stage ownerdraws and feeder in `ui_main.c`, controls 5.2, states 5.3, dwell loading 5.6 | scripted check (cc.py harness): open, Down x3, Right x2, Enter applies the expected folder; screenshots per state |
| M4d live browsing | M2 requests, M3 tails, prefetch/cancel | browsing 30 outfits: no frame over 50 ms, VRAM inside the pool |
| M4e polish | animation cycle, favourites, search, source tabs, the old grid reachable from a "Classic" button | owner review |

## 6. Testing

- `switch_time.py` (frames, `LOADPROF` split, `PRESEED` caches) for every loader change; cold runs need a pack not
  read since boot (pick one, record it).
- Timing runs take the game lock's exclusive mode and wait for no levelcheck/q3map2 and a quiet GPU; when the GPU is
  shared, say so with the numbers (the M0/M1 runs show how much a busy GPU inflates upload stages).
- Byte-for-byte checks for every prepared record (`r_preparedCache 2`).
- Picker: scripted key sequences through the existing client harness, screenshots by `PrintWindow`, hidden window.

## 7. Install

One numbered install per milestone that changes what the owner plays: M1 (renderer DLL), M2 (exe, renderer, cgame,
game, UI DLLs: the `ext` tables change), M3 (renderer), M4 (UI DLL, menu pack, names pack v9 with manifests). Each with the owner's go-ahead, the old binaries moved to `.codex-trash/`, and a short install smoke.

## 8. Owner decisions

Answered 2026-11-01 (owner, in the wishlist session, which owned this plan that morning):

1. **Stage look:** it should fit xenjk's Persona-quality menu design (the "own mix": Persona-style motion and angles in
   xenjk's palette, see `docs/wishlist/menus-spec.md` on `wishlist/drafts`) and show the actual model under studio
   lighting. The background does not need to be live: a pre-recorded or pre-rendered backdrop (still or short loop)
   is fine, and the menu must stay light. So: the model is live in the `RDF_STAGE` studio rig; everything behind it
   is a cheap recorded layer. The main-menu backdrop is the same decision.
2. **Crossover outfits:** under the character (`tifa_tux` is a Tifa outfit, left/right).
3. **Apply mid-life:** at once, as built.
4. **Caches:** `GameData/base/cache/` (the default; no objection).
5. **Opening the picker:** the same way the model picker is reached today: the picker replaces the stock model grid
   where the player menu opens it, rather than a new default key. `ui_characters` stays as the console command; the
   old grid stays reachable from a "Classic" button (M4e).

## 9. Risks

| risk | answer |
|---|---|
| The worker reads a pk3 the main thread later closes (a pure-server change, `fs_restart`) | `FS_Restart` cancels and drains the queue first |
| A prepared record outlives a code change | the record version is in the key; bump it with any builder change; `r_preparedCache 2` in every test run |
| VRAM fills while browsing before M3 | the dwell rule and per-map cap (5.6) |
| The menu stage costs frame time in game | stage renders at the panel's size, sim at 30 Hz, one model |
| Engine `ext` changes break other OpenJK derivatives | `ext` only grows at its end, and an older engine passes a shorter table (`cl_uiapi.cpp`, `cl_cgameapi.cpp`), so a module reads an appended entry only after the engine's ROM cvar says the table is that long (`cl_cgameWarmApi`, `cl_uiWarmApi`), then checks it for NULL (no renderer support) and falls back to today's path. To be folded into the shared versioned handshake (import version, struct size, capability bits; `docs/wishlist/REVIEW-2026-11-01.md`) when the branches meet |
