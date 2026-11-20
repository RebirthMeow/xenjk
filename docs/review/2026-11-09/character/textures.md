# Character rendering review: textures, loading and VRAM

Lane 5 of the 2026-11-09 character rendering review. Target: commit 6fe7bb2f (`renderer/install68-models`), read
from `.codex-research/wt-review-6fe7bb2f`. Paths below are relative to that worktree; line numbers are at 6fe7bb2f.

Method: static reading of xenjk's changes over the e4fe7581 import (`vk_image.cpp`, `vk_stream.cpp`, `vk_upload.cpp`,
`vk_model_warm.cpp`, `vk_tangent_cache.cpp`, `vk_normalmap.cpp`, the engine's range reads in `qcommon/files.cpp`, the
picker's warm-up calls, `tools/characters/*`), plus one read-only scan of the installed packs' zip directories and
texture headers (443 packs, 163 with player textures, 7,377 files under `models/players/`). Nothing was built, run or
timed: every millisecond figure is an estimate and says so; every MB figure is computed from header sizes.

Confidence: **confirmed** = the code path was read end to end (and, for pack facts, counted); **plausible** = the code
allows it but a condition was not verified.

## Installed textures (the scan)

| what | count | note |
|---|---|---|
| DDS under `models/players/` | 4,036 | all BC7 (DXGI 98), all power of two, all with stored mips |
| stored, mipmapped, over 256: streamable | 3,324 | 25.4 GB if every one were at full detail |
| deflated DDS over 256: load whole | 144 | 1,136 MB, five packs: `zz_sb_eve_e14` 294 MB, `zz_sb_reze_kb5` 272, `zz_sb_eve_wk` 211, `zz_sb_eve_e16_hr07` 188, `zz_sb_eve_ss` 170 |
| PNG / JPG / TGA maps (icons excluded) | 2,351 | 984 are 1024 or larger (520 PNG, 443 JPG, 21 TGA); none has a DDS beside it |
| the same as RGBA8 with mips after the 2048 clamp | 7,551 MB | 1,888 MB as BC7 |
| folders whose 512+ maps are raw only / raw mixed with DDS | 89 / 25 | of 238 folders; 124 are stored DDS only |
| DDS 8192 / 16384 on a side | 94 / 2 | 85 MB and 341 MB each with mips; 8.7 GB together |
| identical files (zip CRC and size) inside one folder, 512+ | 431 groups, 671 extra copies | 2,857 MB if every copy were sharp |
| full-detail size of one folder's maps | median 75 MB, p90 319 MB, max 1,343 MB | `ff7r_aeb_zayne__full4` |

Largest raw-texture folders as RGBA8: `sb_eve` 528 MB, `ff7r_vincent` 501, `eve2b_np` 485, `sb_tachy` 325,
`ct3x` 320, `hoth3_vader` 277, `cp_songbird_jacket` 235, `mr_psylocke` 219, `cp_alt` 203.

## Findings, most severe first

### TX-1 bug, high, confirmed: character shadow views make every shadow caster want near-full detail

- `codemp/rd-vulkan/vk_stream.cpp:354-381` (`R_StreamWantSurface`), called from `tr_ghoul2.cpp:2857` for every view;
  shadow views at `tr_main.cpp:2311-2325` and `2523-2536`, run at `tr_main.cpp:2643-2658`.
- The wanted size is `density * scale * viewportHeight / (2 tan(fovY/2)) / dist`, with `dist` measured from
  `tr.viewParms.ori.origin`. A character shadow view sets its origin to the caster's own origin
  (`VectorCopy( ent->e.origin, parms->ori.origin )`), `fovY = 90` and `viewportHeight = tile` (`r_shadowMapSize / 4`,
  512 by default). So `dist` is 0, clamped to 16, and the view asks for 512 / 2 / 16 = 16 pixels per world unit.
  The main view at 1080p gives about 960 / distance: the shadow view is the same request as the character standing
  60 units from the camera.
- Result: with `r_charShadows 1` (the default) the up to 8 nearest shadow casters, and 5 more with
  `r_charSelfShadow`, hold their body and hair maps at 1024-4096 at any distance, since the largest want of the last
  0.5-1 s wins. Cost: the difference between the tails and those levels, roughly 50-300 MB per character from the
  folder sizes above, up to the pool.
- Other secondary views (mirrors, portals, probe captures) feed the same function with their own viewport and field
  of view; not traced.
- Fix: return early when `tr.viewParms.charShadow` is set (and for any view that is not the main one), or compute
  the want from `tr.refdef`'s view origin and the main viewport instead of `tr.viewParms`.

### TX-2 perf, high, confirmed: textures that do not stream are never released, and browsing has no cap

- `vk_image.cpp:1308-1379` (chunks only grow: `used` is bumped, nothing is returned), `vk_image.cpp:1586-1608`
  (images are destroyed only in `vk_delete_textures`, from `RE_Shutdown`), `codemp/ui/ui_picker.c:410-417` (the
  warm-up path has no count limit; `PICKER_LIVE_CAP` guards only the path without the warm API).
- 114 of 238 installed folders have raw (PNG/JPG/TGA) maps and five packs have deflated DDS. Each such character
  previewed or met in a map adds its whole texture set until the next map change: 170-530 MB for the larger ones.
  Ten of them in one session is 2-4 GB that the streaming pool neither counts nor can evict.
- Raw maps also cost four times their BC7 size (RGBA8, `vk_image.cpp:778`; `r_ext_compress_textures` is 0).
- Fix, offline first: convert the remaining raw hero maps to stored BC7 DDS with mips and re-store the five deflated
  packs. That turns 7.5 GB + 1.1 GB of permanent loads into tails (about 0.1 MB a map). Runtime fallback: a
  deflated DDS could be inflated once to a cache file and streamed from there.

### TX-3 perf, high, confirmed: the streaming pool defaults to half the card and never shrinks an image

- `vk_stream.cpp:430-443` (`R_StreamPoolBytes`: `r_streamPoolMB 0` = half the largest device-local heap),
  `vk_stream.cpp:841-858` (`R_StreamMakeRoom` only drops images whose wanted level is the tail).
- On the 6 GB target that is 3 GB for character mips, beside the kit pool's fixed 1536 MB, the non-streamed
  textures of TX-2 and the frame's own targets. Nothing reads the device's actual free memory
  (no `VK_EXT_memory_budget` use in rd-vulkan at this commit).
- An image that was sharp once stays at that level while anything draws it, however far away: a hero seen close and
  then standing 2,000 units off keeps its 4096 maps. With the pool full of such images a new close character gets a
  smaller level or waits (`retryMs`, `vk_stream.cpp:895-897`).
- Fix: size the pool from `VK_EXT_memory_budget` (budget minus usage, with headroom), or default to
  min(heap / 4, 1024 MB); let `R_StreamMakeRoom` also rebuild a resident image at its wanted level when that is two
  or more levels below what it holds (one level of hysteresis avoids ping-pong).

### TX-4 perf, medium, confirmed: the local player's model is pinned at full size, all three skins

- `vk_model_warm.cpp:146-151` (the asked skin plus `red` and `blue`), `vk_model_warm.cpp:268-272` (every shader of
  all three is pinned), `vk_stream.cpp:405-408` (pinned = level 0, whatever the screen shows).
- The pinned set is the folder's full-detail size: median 75 MB, p90 319 MB, 1,343 MB for
  `ff7r_aeb_zayne__full4`. `fma_roymustang_valentine` has two 16384 x 16384 maps (`m03_d.dds`, `m03_s.dds`): 683 MB
  for those two alone when that model is the player's. Team-skin maps are pinned in every game type.
- At 1080p in third person the player's body covers a few hundred pixels; 4096 and above is only reached in the
  picker's close-up.
- Fix: pin to max(wanted level, a floor such as 2048 on the longest side) instead of level 0, and pin only the skin
  asked for (the team skins stream by want like any other player's).

### TX-5 bug, medium, confirmed: one failed build or read leaves a texture at its 256 tail for the rest of the map

- `vk_stream.cpp:899-903` (`R_StreamCreateBuild` false -> `failed = true`), `vk_stream.cpp:793-797` and `935-943`
  (a refused or failed range read does the same), `vk_stream.cpp:873` (failed images are never tried again).
- `R_StreamCreateBuild` fails on `vkCreateImage`, `vkAllocateMemory` or the 4,096-set descriptor pool. Under TX-3's
  over-commit a single out-of-memory answer is likely exactly when VRAM is tight, and the character then stays
  blurry until `vid_restart` or a map change. A file system restart without a renderer restart invalidates every
  range handle (`qcommon/files.cpp:3668-3674`, generation check) with the same result for all streamed textures.
- Fix: treat allocation failure like a full pool (`retryMs` with a backoff, and try a smaller level); on a refused
  read reopen the handle once (`R_StreamOpen`) before giving up.

### TX-6 perf, medium, confirmed path, estimated cost: a raw or deflated texture is one unsplittable main-thread step

- `vk_model_warm.cpp:265-275` (a step is one shader with all its textures), `vk_image.cpp:1936-1973`
  (`R_LoadImage` decode on the caller), `vk_image.cpp:815-919` (resample, light scale, box-filter mips),
  `vk_image.cpp:1241-1249` (`vk_flush_staging_buffer( qfalse )` waits on a fence whenever the staging buffer is too
  small for the next texture).
- A 2048 x 2048 PNG is a decode of 16 MB plus a 21 MB mip chain: an estimated 80-150 ms in one step against the
  6 ms `r_modelWarmupBudget`. Raw megapixels per raw folder: median 6, p90 32, max 113, so an estimated 0.2 s,
  1 s and 3 s of frozen frames per model spread over its shader steps. Four installed maps are larger than 2048 and
  are decoded in full, then halved (`vk_image.cpp:764-767`, `MAX_TEXTURE_SIZE` 2048).
- A deflated 4096 BC7 map is a 22 MB inflate and upload in one step (32 installed).
- Fix: the conversion in TX-2 removes it. Otherwise decode on a worker, as the mesh jobs do, and upload from the
  main thread a step later.

### TX-7 perf, medium, plausible: identical textures under different names load once per name

- `vk_image.cpp:1917-1928`: the image cache is keyed by path only.
- 431 groups of byte-identical files (671 extra copies) sit inside single folders, for example
  `ff7r_aeb_rafayel__full4/skin.dds` and `aerithbridalskin.dds`; 2,857 MB if all were sharp, 309 MB for
  `ff7r_aeb_zayne__full4`, 254 MB for `cp_songbird_jacket`. Not verified: that the shaders reference both names of
  each pair (a copy nothing references costs only disk).
- Fix: dedupe in the pack builder (point the shaders at one file), or alias in `R_FindImageFile_Load` on the pk3
  entry's CRC and size, which the zip directory already holds.

### TX-8 quality, medium, confirmed: 96 maps are larger than any target can show

- 94 maps at 8192 and 2 at 16384 (table above); `r_ddsMaxSize` defaults to 0 = the device limit
  (`tr_init.cpp:994`, used at `vk_image.cpp:1801-1805`).
- They stream, so the cost appears when one is pinned or close: 64 MB (8192 over 4096) to 320 MB (16384) each.
- Fix: default `r_ddsMaxSize` to 4096 (2048 in the 6 GB profile). The loader already drops top mips for free. Check
  whether the two 16384 maps are an upscaling accident in the convert.

### TX-9 bug, low, confirmed: the pack tools order packs differently from the engine

- `tools/characters/cook_tangents.py:151`, `tools/characters/roster_check.py:469` sort by `name.lower()`;
  the engine compares upper-cased (`qcommon/files.cpp:2794-2806`) and puts `dl_` packs last
  (`files.cpp:3039-3053`). `_` sorts before letters in the tools and after them in the engine, so `zz_x.pk3`
  against `zzz.pk3` resolves the other way round.
- `cook_tangents.py`'s docstring says loose files win; `installed_glms` never looks at loose files.
- Effect: the cooker can cook a shadowed GLM (the game then misses the cache: 0.4-0.7 s of MikkTSpace on first
  load), and the roster check can validate a `character.cfg` the game does not use.
- Fix: one shared `pack_order()` helper that upper-cases and handles `dl_`; scan loose folders last.

### TX-10 bug, low, plausible: a streamed image's view can be held by something streaming does not update

- `vk_scene.cpp:3398` writes `image->view` into the render scene's every-image set; `vk_stream.cpp:754-767` and
  `698-716` replace `image->view` and destroy the old one two frames later.
- If a scene material ever names a DDS under `models/players/`, the set keeps a destroyed view. No installed case
  was looked for. `vk_normalmap.cpp:133-135` already guards its own use.
- Fix: refuse streaming for an image a scene registers, or rewrite the scene's slot on a swap.

### TX-11 bug, low, confirmed: the DDS header's mip count and kind are trusted

- `vk_image.cpp:1789-1812`: `mips` is used as written. A file claiming more levels than its size has makes
  `vk_create_image` ask for more mip levels than the extent allows (invalid, driver-dependent). `R_DDSFormat`
  (`vk_image.cpp:1714-1737`) does not check the DX10 array size or the cubemap flag, so such a file is read as a 2D
  texture from the wrong bytes.
- No installed file triggers either (all 4,036 have consistent headers).
- Fix: clamp `mips` to `1 + log2( max( w, h ) )`; fall back to the other loaders for arrays and cubemaps.

### TX-12 perf, low, plausible: every sharp streamed image is its own device allocation

- `vk_stream.cpp:607-619`: one `vkAllocateMemory` per build, freed on every level change, on the main thread in
  `vk_begin_frame`.
- With 3,324 streamable maps a busy scene can hold hundreds to a few thousand allocations (the device limit is
  typically 4,096) and pays an allocate and a free per level change (an estimated 0.1-1 ms each).
- Fix: sub-allocate builds from a few large blocks sized by level.

### TX-13 quality, low, confirmed: streaming coverage is decided by path and by one draw path

- `vk_stream.cpp:164-166` streams only `models/players/`; `tr_ghoul2.cpp:2857` is the only source of wanted sizes.
- A hero texture kept in another folder loads whole. A `models/players/` DDS drawn only by something that is not a
  Ghoul2 surface (an MD3 attachment, a 2D draw) is never wanted and stays at its 256 tail.
- Fix: let the pack name its streamed folders, and give non-Ghoul2 users of a streamed image a fixed want.

### TX-14 perf, low, confirmed: the warm-up queue runs every job to the end, oldest first

- `vk_model_warm.cpp:313-318`: no cancellation and no priority. The picker's dwell limits how many jobs start, but
  each one started loads completely (and, for raw packs, permanently: TX-2) before the selected one begins.
  `WARM_READ` (`vk_model_warm.cpp:235-240`) has no timeout of its own.
- Fix: a cancel call for the picker, and newest-first for the local player's request.

### TX-15 perf, low, plausible: layer arrays are always uncompressed

- `vk_image.cpp:2353-2442`: `R_FindImageArray` reads layers with `R_LoadImage` only (never DDS) and forces
  `IMGFLAG_NO_COMPRESSION`. Four 1024 layers are 21 MB per array, two arrays per layered material: 43 MB against
  11 MB as BC7. Not checked: how many installed materials use layers.
- The function repeats `R_NewImage`'s body instead of calling it.

### TX-16 perf, low, confirmed: host memory held for the session

- `vk_image.cpp:98-99` (scratch buffers start at 64 MB and 32 MB and never shrink), `vk_image.cpp:1036` and
  `1246-1249` (the staging buffer grows to the largest upload: 85 MB after one whole-loaded 8192 map),
  `vk_upload.cpp:37` (64 MB ring), `vk_stream.cpp:449-453` (32 MB of slots). About 200-280 MB of system memory.
- Fix: free the scratch buffers after registration ends; cap the staging buffer and split larger uploads.

### TX-17 bug, low, confirmed (imported code): the specular-map conversion

- `vk_image.cpp:2009` sums green twice and never blue; `2008-2010` divides by zero on a black texel;
  `2000` allocates a buffer that is never freed (16 MB for a 2048 map until the next temp-memory clear).
  Reached by the `specularMap` shader keyword (`tr_shader.cpp:1432-1440`).

### TX-18 quality, low, confirmed: generated normal maps (`r_genNormalMaps`, off by default)

- `vk_normalmap.cpp:164-165`: the target is created with no data, so it has one mip level
  (`vk_image.cpp:809-813`) and shimmers when minified; the same albedo in two shaders creates two targets of the
  same name; `vk_normalmap.cpp:305-306` dispatches over `width` rather than `uploadWidth` (four times the work for
  a clamped 4096 source); streamed albedos are skipped, so the result depends on how a pack is zipped.

### TX-19 quality, low, confirmed: cache files are written in place

- `vk_init.cpp:452` (pipeline cache, on every map change) and `vk_tangent_cache.cpp:212-216` use `FS_WriteFile`
  directly. A crash during the write leaves a short file. The `.xtan` reader checks the length, so that one is
  safe; the pipeline cache relies on the driver rejecting it. `cache/tangents/` is never pruned.
- Fix: write to a temporary name and rename.

### TX-20 suggestion, low: format choices

- Every DDS is loaded as UNORM, sRGB variants included (`vk_image.cpp:1722-1727`), matching the other loaders;
  mips and filtering happen in gamma space. Moving albedo to sRGB formats is a shading-lane decision.
- All 4,036 installed DDS are BC7. Single-channel masks as BC4 and opaque low-contrast maps as BC1 would halve
  those files; the loader already accepts both.

## Counts

20 findings: 6 bugs (1 high, 1 medium, 4 low; TX-17 is in imported code), 9 perf (2 high, 3 medium, 4 low),
4 quality (1 medium, 3 low), 1 suggestion. 15 confirmed, 4 plausible (TX-7, 10, 12, 15); TX-20 is a suggestion.

## Needs a high-effort follow-up

1. **Measure TX-1 and TX-3 in game**: `streaminfo 1` in a map with eight bots at distance, `r_charShadows` 1 against
   0, and VRAM on a 6 GB budget (`r_streamPoolMB`). The static reading gives the mechanism, not the MB.
2. **Convert the 114 raw folders and re-store five packs** (TX-2, TX-6): a pack-builder batch with a visual check
   per character, since JPG and PNG sources go through BC7 encoding.
3. **Which duplicate names are referenced** (TX-7): parse the installed `.shader` and `.skin` files against the
   duplicate groups; the scan's JSON is in this session's scratch folder and can be regenerated in a minute.
4. **A texture audit in `tools/characters/`**: stored or deflated, DDS or raw, mips, size cap, duplicates; as a
   gate beside `roster_check.py`. Every pack figure here came from an ad hoc scan.
5. **Pool shrink and budget design** (TX-3, TX-4, TX-12): one change to how the pool is sized, how images step
   down and how builds are allocated.

## Not covered

- Shader-side use of the maps (channel layouts, swizzles, BC5 normal reconstruction, sRGB decode): shading lane.
- `vk_vbo.cpp`, `vk_mesh_jobs.cpp` and `vk_pipeline_async.cpp` beyond their tangent-cache and warm-up calls;
  `vk_upload.cpp` was read (ring allocation, batch reuse and reclaim order look correct) but its callers'
  buffer lifetimes were not.
- The PNG, JPG and TGA loaders are unchanged from the import and were not audited for malformed input.
- `vk_image_process.cpp` (unchanged), stb_dxt / `rygCompress` (off: `r_ext_compress_textures 0`).
- World, UI, lightmap and render-scene textures; the kit pool; cubemaps.
- `ui_picker.c` and `ui_player.c` beyond their warm-up calls; cgame's pending-model swap.
- Nothing was run: no timings, no VRAM readings, no validation-layer output.
