# Character loading and the character picker (design, 2026-10-31)

Owner: "if im tifa and trying to change to aerith, the game hangs for multiple seconds, like 10+", then: background and
phased loading "sound band aid ish ... how is it supposed to be solved?", then "write the design", with a new model
interface: "see preview of the model, maybe scroll through options up and down, maybe variants left and right ...
something modern vs jka old way". Not NVMe (owner).

Status: M0 and most of M1 built (section 5). The task-by-task plan, the interface spec and the order are in
[character-loading-plan.md](character-loading-plan.md). Measurements and the switch timer are in
`.codex-research/model-switch-20261031/` (`NOTES.md`, `switch_time.py`).

## 1. What happens today (measured)

A model change runs synchronously on the main thread, inside one frame:

1. `model` cvar -> userinfo -> server `ClientUserinfoChanged` -> `SetupGameGhoul2Model` (on a listen server the game
   VM loads through the client renderer's caches).
2. cgame `CG_NewClientInfo` -> `CG_RegisterClientModelname` (cg_players.c): `R_RegisterSkin` (shaders, every image
   read whole with `FS_ReadFile` and uploaded), `G2API_InitGhoul2Model` (`R_LoadMDXM`), `CG_LoadCISounds`.
3. First draw: cloth and hair build their runtime state.

Worst frame around a switch (installed exe and renderer, hidden client):

| switch | worst frame |
|---|---|
| Tifa -> Aerith, files in the OS cache, showcase 720p | 1275 ms |
| same, Watson slice at 4K | 1532 ms (VRAM peak 8.4 of 12 GB) |
| same, `r_cloth 0 r_hair 0` | 584 ms |
| Tifa -> Aerith Gentiana (450 MB), cold | 3928 ms (D: read 130-180 MB/s for ~3 s) |
| back to a model already loaded | ~10 ms |

Cold reads cost ~1 s per 140 MB from the D: HDD (Aerith 212 MB, Gentiana 450, Tux 773, Tifa Witchy 978), more when
other work uses the disk. The 10+ s was not reproduced; a cold big variant under disk contention reaches it.

Work redone on every load, though it depends only on the files:

- **Mesh** (`R_BuildMDXM`, vk_vbo.cpp): repacks all four LODs into GPU vertex/index buffers, renormalises weights,
  generates MikkTSpace tangents for ~264k vertices.
- **Hair** (`tr_hair.cpp`, `vk_hair.cpp`): parses the 2.9 MB `model.chains`, then builds a "substitute" vertex buffer per
  LOD: a second full copy of the mesh buffer with the hair vertices re-pointed at hair bones (VRAM paid twice).
- **Cloth** (`skirt_sim.c`, `vk_cloth.cpp`): edges and bends by sorting, tethers by multi-source Dijkstra, masses, then
  vertex-triangle tables, three graph colourings (edges, bends, triangles) and a ~4 MB GPU state image.
- **Textures** are already prepared (BC7 with stored mips, `R_LoadDDSImage`); they cost the read and the upload.
- Upload paths wait on the GPU: one-shot command buffers end in `vkQueueWaitIdle` (vk_cmd.cpp), the staging buffer
  waits on its fence when full.

### 1.1 Stage split (milestone 0)

Update 2026-11-19: "not installed" below is the state when written. The stage timers and M1 (and M2a, M3, M4c) are installed:
`character-loading-plan.md` records M0-M2a as Install 58 and the renderer, UI and picker as Install 61; Install 94
(2026-11-19) later replaced the engine, renderer and modules with master's.


Renderer stage timers (`r_loadProfile`, `loadprofile`; worktree branch `renderer/load-profile-stages`, not installed)
on the Tifa -> Aerith switch, Release build. The stages sum to the frozen frame within 1%, so nothing else in the
frame matters:

| stage | ms | what it is |
|---|---|---|
| GLM read | 89 | `model.glm` through the model cache (OS-cached) |
| mesh build | 617 | `R_BuildMDXM`, all four LODs, of which **MikkTSpace tangents 469** (144 surfaces) |
| skin: images | 1079 * | 24 BC7 images read and uploaded |
| skin: shaders, lookups | ~350 | `.skin` and shader parse, file lookups (the missing-map probes) |
| skin: new pipelines | 0 (Costa: **952** for 8) | pipelines for material permutations not seen this session |
| cloth | 158 | asset 25, solver instance 11, first step 122 (GPU setup 114) |
| hair | 566 | asset 35, **substitute buffers 531** (four LODs) |
| **frame** | **2876** | |

\* This run shared the GPU with another session's job at 100% load, which inflates every stage that waits on the GPU
(images, the hair and cloth uploads); images were 193 ms on a quiet GPU in the earlier run. Evidence and the other
runs: `.codex-research/model-switch-20261031/NOTES.md` "Milestone 0".

The renderer's `VkPipelineCache` starts empty each launch and is never saved (vk_init.cpp), so a model whose materials
need new pipeline permutations compiles them in the switch frame.

## 2. How games solve it

Shipping engines treat high-quality assets with four pieces that work together; none of them is a patch on top of a
blocking load.

1. **Cooked data.** Everything derivable is computed by the build (or once, into a derived-data cache: Unreal's DDC,
   keyed by a hash of the inputs and the builder's version). At run time a load is a read and a copy.
2. **Asynchronous I/O and upload.** Reads are requests to an I/O thread with priorities and cancellation; GPU uploads go
   through a transfer queue under a per-frame budget, completion is a fence that is polled, never waited on.
3. **Texture streaming.** Every known asset keeps a small resident mip tail; the large mips are requested by on-screen
   size and evicted under a fixed VRAM budget (Unreal's streaming pool). A new character appears at once at reduced
   texture detail and sharpens; the visible failure mode is pop-in, never a freeze.
4. **Design that hides the rest.** Matches know their cosmetics at the loading screen; character select screens show
   a portrait or a low-detail model until the full one is in; menus prefetch what the cursor rests on.

## 3. Loading design

### 3.1 Prepared models (derived-data cache)

- One blob per model: `<homepath>/cache/models/<folder>-<key>.xmc`.
- **Key**: the pk3 central directory's CRC32 and size of every input file (GLM, `.cloth`, `.chains`, `.joints`, the
  GLA), the renderer's blob layout version, and the settings that change the result (cloth bend model, tether count,
  hair/cloth enable). The zip directory already holds the CRCs, so computing the key reads nothing.
- **Contents**: per LOD the GPU vertex and index buffers exactly as `R_BuildMDXM` writes them, tangents included; the
  hair remap as a small per-vertex stream (bone slot overrides) instead of a second vertex buffer; the cloth topology,
  tethers, colour orders and the GPU state image; the bind tables both sidecars build today.
- **Load**: read, check the header, copy into staging, upload. A miss (first load, edited asset, new renderer layout)
  builds as today and writes the blob.
- The pack builder runs the same builder so hero packs ship pre-filled (`model.xmc` beside `model.glm`, still checked by
  key, so a stale one is rebuilt instead of trusted).
- Stays at run time: per-instance simulation state (particle positions, history), bone caches.
- Pipelines: save the `VkPipelineCache` to the homepath at shutdown and load it at start (built: `r_pipelineCache`),
  and create a requested model's material pipelines on the loader's schedule instead of at first draw (milestone 2:
  NVIDIA's driver cache already serves repeats, so only a first-ever compile still lands in the switch frame).
- Expected gain from the stage split (1.1): MikkTSpace and the hair substitute buffers (~1 s together) become a copy;
  the cloth tables and the mesh repack shrink to their uploads.

### 3.2 Asynchronous loader

- **Requests**: `R_RequestModel( name, priority )` returns a handle at once; states `queued -> reading -> uploading ->
  ready (low mips) -> sharp`; cancelled when no longer wanted (the picker moved on).
- **I/O thread**: Q3's file system is not thread-safe, so the main thread resolves each file to (pk3 path, offset,
  size, stored or deflated) from the pack directory and the worker reads that byte range with its own handle into a
  malloc'd buffer. Our packs store entries uncompressed, so this is a plain read; deflated entries fall back to the
  main-thread `FS_ReadFile`. No Hunk/Zone allocation on the worker.
- **Integrate step**: once a frame, under a time budget (`r_streamBudgetMs`, default 2 ms) and a byte budget per frame:
  create images and buffers, record copies on the transfer queue (or the graphics queue with a fence), poll fences
  from earlier frames. No `vkQueueWaitIdle` on this path.
- **Server side**: the game module needs the skeleton and mesh for saber and hit traces. On a listen server it shares
  the renderer caches; with prepared blobs that load is a short synchronous read of the GLM data only (no images).
  The dedicated server is unchanged.

### 3.3 Texture mip streaming

- DDS stores mips largest first, so the tail (every level at or below `r_streamTailSize`, default 256) is one
  contiguous byte range at the end of each file: read and upload it first for every requested model (a 4096^2 BC7 map's
  tail at 256 is ~85 KB of its 22 MB).
- The image keeps its full mip count; unloaded levels are clamped with the view's `baseMipLevel` until they arrive,
  then the view is swapped after the upload fence (descriptor update between frames).
- **Wanted level** per image from the entities that use it: projected height of the model on screen and the
  material's texel density, recomputed a few times a second, not per frame.
- **Budget**: `r_streamPoolMB`, default from device VRAM minus the world's use; eviction drops top levels of the
  least recently wanted images first. This also lifts today's limit (22 BC7 heroes filled 12 GB when the icons were
  shot): heroes far away or off screen sit at their tails.

### 3.4 Swapping in cgame

- `clientInfo_t` gains a pending model: the player keeps drawing the old one until the new one reaches `ready`, then
  swaps (optionally a short dissolve). Other players use the same path, replacing Q3's `deferred` client info.
- Sounds (`CG_LoadCISounds`) load through the same queue; until then the previous set plays.
- The local player's switch is usually already `ready` when Apply is pressed, because the picker requested it
  (section 4.4).

## 4. Character picker

### 4.1 What other games do

- **Overwatch 2 hero gallery, Marvel Rivals**: roster list beside a large live model on a studio stage; skins in a
  column, the model changes as the cursor moves; drag to rotate.
- **Fortnite locker**: the model stands centre stage; styles (variants) are swatches under the item, applied live.
- **Street Fighter 6, Tekken 8**: roster grid, big live model, colours and costumes on left/right after the pick;
  the select screen keeps the roster at low detail so moving the cursor is instant.
- **Smash Bros**: alternate colours on left/right (or a button) at the portrait, previewed immediately.
- **Genshin Impact, Apex Legends**: a character list, a large model with an idle animation, an outfits tab, an
  inspect mode that zooms to the face.

Common to all: the model is the screen, browsing never waits, variants are one key away from the character, and
applying is one confirm.

### 4.2 Layout

```
+-------------------+----------------------------------------------+----------------+
| [search........]  |                                              |  AERITH        |
| All FF7R CP2077   |                                              |  Gainsborough  |
| Stellar  JKA  *   |              live 3D model                   |  FF7 Rebirth   |
|                   |        (studio light, floor shadow,          |                |
|   Tifa            |         idle animation, drag to turn,        |  Variant       |
| > Aerith     10   |         wheel to zoom, F = face view)        |  Tux  (4/10)   |
|   Gentiana        |                                              |                |
|   Eve         5   |                                              |  Team colour   |
|   Judy        2   |      <   Original . Costa . Tux . Loveless >  |  o def o red o blue |
|   ...             |                                              |  [Apply]       |
+-------------------+----------------------------------------------+----------------+
```

- **Left: roster.** One row per character (icon, name, variant count); up/down or the wheel moves the cursor; tabs
  by source (All, FF7R, CP2077, Stellar Blade, JKA, Other) and favourites (star); type to filter.
- **Centre: stage.** The selected variant, live: idle animation, drag to turn (released, it eases back to a three-
  quarter view), wheel to zoom between full body and face, `F` toggles a face close-up, a small animation cycle (idle,
  stance with the saber lit, run).
- **Under the stage: variants.** Left/right steps through the character's outfits; dots or small thumbnails with the
  variant name; wraps at the ends.
- **Right: details and apply.** Name, source game, variant name, team colours (default, red, blue skins as chips; in
  team games the forced team colour is shown and locked), Apply. Enter applies, Esc goes back without a change.
- Keys: up/down character, left/right variant, Enter apply, Esc back, `F` face, mouse everywhere. Gamepad maps the
  same way later.
- In-game it opens over the running game (the world keeps running behind a dimmed backdrop); the stage itself is a
  separate scene, so the map's lighting never decides how the preview looks.

### 4.3 Stage rendering

- The menu already draws Ghoul2 models in a world-less scene (`Item_Model_Paint`, `RDF_NOWORLDMODEL`), but rd-vulkan
  lights such scenes with a flat ambient + directed 150 and skips the character fill and rim terms, so PBR heroes look
  flat there.
- Add a stage mode for world-less scenes: the existing `r_portraitRig` key/fill/rim/top layout (built for the icon
  shoots against the Cycles renders) driven by the scene instead of cvars, a small built-in studio environment for
  reflections, a floor disc with a contact shadow, and the cloth and hair simulation stepping on the menu's clock.
- The roster and the loading placeholder use the existing 64 px icons. Full-body portraits were dropped
  (owner, 2026-11-01; [plan](character-loading-plan.md) 5.3).

### 4.4 Browsing and loading

- The stage requests the selected variant at high priority, and its neighbours (next and previous character, next and
  previous variant) at low priority; moving on cancels requests that have not started reading.
- Until the model is `ready` the stage shows the icon with a progress ring; the model then fades in at its mip
  tail and sharpens. With prepared blobs on a warm cache, `ready` takes a frame or two; cold from the HDD it is the
  read of the mesh and tails (tens of MB), not the whole pack.
- Leaving the picker drops everything it requested except the applied model, which is then already loaded: Apply
  sends the userinfo and the swap of section 3.4 is immediate.

### 4.5 The character list

- Today the grid lists `models/players/<folder>/` directories with an icon per skin (`UI_BuildQ3Model_List`); the short
  names pack (`zz_xen_model_names_v8.pk3`, 53 folders) makes our converts appear. Variants are separate folders
  (`aerith`, `aerith_tux`, `cp_judy`, `cp_judy_2yl`, `eve_kunoichi` ...); team colours are skins.
- Add a small manifest per folder, `models/players/<folder>/character.cfg`: character id (`aerith`), display name,
  variant name (`Tux`), source tag, sort order, stage framing (height, face bone), idle animation. The install
  pipeline (`short_names.py`) writes it beside the short-name copy.
- Folders without a manifest (stock JKA models, older converts) group by folder name: text before the first `_`, or
  before the second for prefixed sources (`cp_judy_2yl` -> `cp_judy`); stock models become one-variant characters
  whose skins are their variants.
- Built once when the UI starts, like today's list; no model loads to build it.

### 4.6 Where it lives

- A new menu file (`ui/jamp/character.menu`) in a xenjk UI pack, opened from the player menu's model area; the old
  grid stays reachable.
- The roster, variant strip and stage are ownerdraws and a feeder in `ui_main.c`; the stage paint extends
  `Item_Model_Paint`. Everything stays in the UI module plus the renderer's stage mode, so it ports to other OpenJK
  derivatives as menu + UI code.

## 5. Milestones

Update 2026-11-19: the "not installed" notes below are dated. The stage timers and M1 are installed (Installs 58 and 61,
see `character-loading-plan.md`; Install 94 carries master's current code).


0. **Stage timers** for the load path, printed under `r_loadProfile`. Built and measured 2026-10-31 (section 1.1);
   the timers live on `renderer/load-profile-stages`, not installed. Sounds load in cgame and are not timed, but the
   renderer stages already sum to the frozen frame.
1. **Prepared blobs** (3.1). Done when a warm Tifa -> Aerith switch drops from ~1.3 s to the texture upload, and a
   blob built from the pack equals a fresh build byte for byte.
   - 2026-10-31, first part built (branch `renderer/load-profile-stages`, not installed): prepared tangents
     (`vk_tangent_cache.cpp`, `r_tangentCache`) shared by the mesh build and the hair buffers, which had run
     MikkTSpace a second time; and the pipeline cache kept between launches (`r_pipelineCache`).
   - Measured: warm Tifa -> Aerith 1126 -> 452 ms (textures 197 of it); 24 of 24 cached models equal a fresh
     computation byte for byte (`r_tangentCache 2`). NVIDIA's driver cache already covered repeat pipeline compiles
     (25 -> 2 ms), so the saved cache pays off only without one.
   - Finished the same day: the cloth's ~60 ms was a third MikkTSpace pass; it reads the cache too (6 ms). With it,
     369 ms on a quiet GPU (textures 176, shader creation ~60, model and sidecar reads ~90). The unnamed PBR map
     probes cost 4 ms. Evidence: `.codex-research/model-switch-20261031/NOTES.md` "Milestone 1".
2. **Asynchronous loader and pending swap** (3.2, 3.4). Done when no switch, cold or warm, produces a frame over
   50 ms in `switch_time.py`, and the old model stays drawn until the new one is ready.
3. **Mip streaming** (3.3). Done when a new hero is visible within 100 ms of the request, sharp within 1 s warm, and
   VRAM stays inside the pool with 20+ heroes in a map.
4. **Picker** (4). Can start beside 1-3 with icons and today's loader; browsing becomes smooth once 2 lands.
   Done when every installed character and variant is reachable with up/down and left/right, and Apply never waits.

## 6. Open questions for the owner

- Stage look: neutral studio (grey gradient, rim-lit) or a themed backdrop per source?
- Should variants of other characters' outfits (`aerith_sylus`, `tifa_tux`) list under the character (Aerith) or the
  outfit set?
- Model changes mid-life: apply at once (today) or at the next respawn, as most competitive games do?

## 7. Review round 3 fixes (2026-11-06)

Fable and Codex Astra reviewed the loading merge (b63c9dbb) read-only on 2026-11-06
(`<levelport root>\levelport\look\review-20261106\REPORT.md`, area B). Fixed on `claude/r3-gameplay-fixes-212fe2`
(not installed):

- **Picker stack overflow** (both, `ui_picker.c`): `Picker_Animate` built the `animation.cfg` path with `strcpy` into
  `gla[MAX_QPATH]`, past the buffer for a GLA folder of 49+ characters. Now `Com_sprintf`, and no idle when the path
  does not fit. The engine's three `G2API_GetGLAName` copies (`cl_uiapi.cpp`, `cl_cgameapi.cpp`, `sv_gameapi.cpp`)
  copy at most `MAX_QPATH` (every module passes such a buffer; the name comes from the file).
- **UI animation-set pool** (Fable): every own-skeleton outfit the picker rested on took one of the 16
  `bgAllAnims` slots and pool memory; the 17th wrote past the array in a release build (`UI_AnimsetAlloc` only
  asserted). The picker now parses own-skeleton sets into one set of its own (`UI_ParseAnimationFileInto`, not
  registered), and `UI_AnimsetAlloc` / `UI_ParseAnimationFile` refuse a 17th set instead of overrunning. Not run in a
  client: the 16+ outfit browse of the review's A/B is still to do.
- **Stored pk3 reads** (Fable P1, `files.cpp`): `FS_ReadFile` reads a stored (method 0) entry with one `fread` on the
  pack (`FS_ReadStored`, the range reader's unbuffered handle) instead of minizip's 16 KB stream copy and CRC, whose
  result `FS_FCloseFile` never checked. `fs_storedFast 0` is the old path. Offline A/B of the two paths on installed
  hero GLMs, OS-cached, best of 7, identical bytes: 34.5 MB (zayne) 53.2 -> 6.8 ms, 43.2 MB (rafayel)
  66.9 -> 9.0 ms. The in-game A/B (`r_loadProfile 1`, warm Tifa -> Aerith, `fs_storedFast` 0 then 1) is not run;
  the 89 ms "GLM read" in section 1.1 should drop by about the same share.
