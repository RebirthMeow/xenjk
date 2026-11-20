# rd-vulkan resources and loading (review 2026-11-19, master a322d9dc4)

Scope: xenjk's changes since the import (e4fe7581) in rd-vulkan's resource and loading side, read at master with
`git diff e4fe7581 master -- <file>` for what is ours. New files, read whole: `vk_stream.cpp` (2058 lines),
`vk_upload.cpp` (369), `vk_vram.cpp` (354), `vk_model_warm.cpp` (383), `vk_mesh_jobs.cpp` (334),
`vk_pipeline_async.cpp` (244), `vk_tangent_cache.cpp` (361). Changed files, our diff read with its surroundings:
`vk_image.cpp` (3018 lines, 1019 changed), `vk_vbo.cpp` (2089, 652), `tr_init.cpp` (1978, 444), `vk_local.h`
(1776, 276), `tr_bsp.cpp` (3489, 274), `vk_instance.cpp` (1521, 230), `vk_frame.cpp` (1301, 210), `vk_init.cpp`
(885, 135), `vk_cubemap.cpp` (600, 115), `tr_world.cpp` (the flat-world marks and few-surface path), `tr_skin.cpp`
(498, 70), `tr_model.cpp`, `tr_image.cpp`, `tr_curve.cpp`, `tr_cmds.cpp` (RE_BeginFrame's hooks), `vk_cmd.cpp`,
`vk_attachments.cpp`, `vk_shaders.cpp`, `vk_normalmap.cpp`, `vk_bloom.cpp`, `vk_dynamic_glow.cpp`,
`vk_lightgrid.cpp`, `vk_info.cpp`, `tr_allocator.cpp/.h`, and the resource declarations in `tr_local.h`. The engine
side of the range reads (`qcommon/files.cpp` `FS_Range*`) was read only for what the renderer relies on.

Files with no findings: `vk_mesh_jobs.cpp` (apart from RR-9, shared with the pipeline worker), `vk_frame.cpp`,
`vk_instance.cpp`, `vk_cubemap.cpp`, `tr_model.cpp`, `tr_image.cpp`, `tr_curve.cpp`, `tr_skin.cpp`, `tr_cmds.cpp`,
`vk_cmd.cpp`, `vk_attachments.cpp`, `vk_shaders.cpp`, `vk_normalmap.cpp`, `vk_info.cpp`, `tr_allocator.cpp/.h`.

What was checked and holds (so nobody re-derives it):

- **Frames in flight.** A streamed image retired at stream serial S is destroyed at S + 2. Every frame that drew
  it is S - 1 or earlier, and `vk_begin_frame` waited for S - 1's fence at S + 1. A frame `vk_end_frame` drops
  (`vk_resize_geometry_buffer`) goes through `vk_wait_idle`, so the stream's command buffer and staging slots of
  that serial are finished before their reuse. Stream commands are submitted between the staging flush and the
  frame's own submit on one queue, so their barriers order after whole-loaded uploads.
- **Worker threads.** The mesh worker touches only its job and the GLM (`R_TangentKeyOf`, `R_TangentComputeLods`,
  `R_PackMDXMLod`, MikkTSpace: no engine calls, no shared statics). `s_tangents`, the upload ring, the stream state
  and the warm-up list are main-thread only. `FS_RangeCancel` returns only once the worker no longer writes the
  destination, so cancelled slots and `R_StreamVkShutdown`'s unmap are safe. The VRAM ledger is locked and its tag
  is thread-local.
- **Map change and vid_restart.** `RE_Shutdown` stops the mesh worker before models go, `vk_delete_textures` waits
  idle then drops every stream image back to its tail, `vk_release_resources` finishes uploads before the VBOs go
  and stops the pipeline worker before pipeline slots are reused; `vk_shutdown` frees the stream staging buffer,
  blocks, descriptor pool and the upload ring. `R_MeshJobsShutdown` and `vk_pipeline_async_shutdown` also clear the
  defer flags, so an `ERR_DROP` thrown inside a warm-up step does not leave them set past the renderer restart that
  follows.
- **The upload ring.** Wrap, reclaim and slot reuse are consistent (a segment whose batch slot was reused was waited
  for when the slot was taken).

## Findings

### RR-1 P2 On a pure server the tangent cache never hits: every load recomputes and rewrites it
- Where: codemp/rd-vulkan/vk_tangent_cache.cpp:164 (`R_TangentRead`), :320 and :350 (`R_TangentWrite`);
  codemp/qcommon/files.cpp:1531 (loose files refused while `fs_numServerPaks` is set, except .cfg/.fcf/.menu/.game/.dat/demos)
- Scenario: connected to a server with `sv_pure 1` (the stock default is 0, most public servers set 1).
  `cache/tangents/<key>.xtan` is a loose file in the home path, so `ri.FS_ReadFile` does not find it. Every player
  GLM then takes the miss path: MikkTSpace again (0.4-0.7 s for a hero, on the main thread for models built during
  a map load, per map since `R_TangentCacheClear` runs in `RE_Shutdown`) and `FS_WriteFile` of the same file again.
  The work is correct, only repeated; the cache does nothing in the case it was built for.
- Fix: read (and write) cache files through a home-path-only import that skips the pure filter (the engine already
  has `FS_SV_FOpenFileRead`; export a `FS_ReadHomeFile`), and skip the write when the file already exists. RR-2
  wants the same import.
- Confidence: likely (traced in the file system code; not run against a pure server)
- Status: fixed (0c7b0ff91): the engine exports `FS_ReadHomeFile` (appended to `refimport_t`, advertised by `fs_prefetchApi 3`); a tangent read that `FS_ReadFile` misses (a pure server refuses the loose file) tries the home folder. No write skip was added: a write happens only on a miss, and with the home read a miss means no valid file. Not run against a pure server

### RR-2 P2 The pipeline cache file is taken from any pk3 that carries its name
- Where: codemp/rd-vulkan/vk_init.cpp:446 (`ri.FS_ReadFile( "cache/vk_pipelines.bin" )`), :409
- Scenario: `FS_ReadFile` searches pk3s before loose files in a game folder. A pk3 the client downloaded from a
  server (they land in the home path's base folder) that contains `cache/vk_pipelines.bin` shadows the client's own
  file at the next launch. The footer (magic, length, CRC-32) and the header's vendor, device and
  `pipelineCacheUUID` are all reproducible by an author who targets a common driver version, so the blob reaches
  `vkCreatePipelineCache` as initial data. Drivers are not required to be robust against a hostile cache.
  The `.xtan` files have the same path but their contents are fully validated and only shade wrongly, so they are
  not part of this finding.
- Fix: read the pipeline cache with the home-path-only import of RR-1 (never from a pk3).
- Confidence: likely (search order and checks traced; no hostile file built)
- Status: fixed (0c7b0ff91): the pipeline cache is read with `FS_ReadHomeFile` (never a pk3) when the engine has it; an older engine keeps `FS_ReadFile`. Also frees a zero-length read

### RR-3 P2 A DDS larger than the device limit with too few mips to drop is created anyway; huge sizes overflow
- Where: codemp/rd-vulkan/vk_image.cpp:1952 (`if ( skip > mips - 1 ) skip = mips - 1`), the level size loop below it,
  and the same pattern in `R_LoadArrayDDS` (codemp/rd-vulkan/vk_image.cpp:2795-2808)
- Scenario: a DDS whose side exceeds `maxImageDimension2D` (or `r_ddsMaxSize`) but which stores one level: `skip`
  is clamped back to 0 and `vk_create_image` is asked for the full size. Past the device limit `vkCreateImage`
  fails, `VK_CHECK` only logs in debug builds, and the null image is bound, viewed and uploaded to (a crash in
  the driver). With a side of 65536 or more, `vk_block_level_size` overflows `int` (16384 x 16384 x 16 = 2^32), the
  level size comes out 0 and the `dataOffset + levelBytes > len` check passes. Reachable from any pk3, a
  downloaded one included; no installed texture does it.
- Fix: after the clamp, `if ( ( MAX( w, h ) >> skip ) > (int)deviceMax ) DDS_FAIL();` (the other loaders then
  resample), and refuse `w` or `h` above 32768 before any size is computed (both loaders).
- Confidence: confirmed (traced); the driver crash itself needs a run
- Status: fixed (0c7b0ff91): both DDS loaders refuse a side above 32768; the single loader refuses a level still past `maxImageDimension2D` after the drop clamp, the array loader the same against the device limit (the RGBA8 path resamples). The driver crash itself was not reproduced

### RR-4 P3 Converted specular-gloss images under models/players bypass both the stream pool and VRAM admission
- Where: codemp/rd-vulkan/vk_image.cpp:782 (admission skipped when `R_StreamWholeWanted( image->imgName )`), :2206
  (`R_BuildSDRSpecGlossImage` creates with `R_CreateImage`); the same for `R_FindImageFile_Load`'s
  `r_mapGreyScale` branch
- Scenario: a character shader with a `specularMap` (not ORMS): its converted image has a `models/players/` name
  and mipmaps, so `vk_generate_image_upload_data` skips the budget check on the assumption that the stream pool
  admits it, but `R_CreateImage` never attaches it to the pool. It loads whole whatever the budget says.
- Fix: make the skip depend on what the caller does (a flag set by `R_CreateStreamedRawImage` around its call, or
  `image->stream != NULL` after attaching first), not on the name.
- Confidence: confirmed (traced)
- Status: fixed (0c7b0ff91): the admission skip is a flag `R_CreateStreamedRawImage` (and its reload, `R_StreamedRawLevels`) sets around its call, not the name; a converted specular-gloss or greyscale image under models/players is admitted like any other

### RR-5 P3 `R_StreamHold` has no caller; its comment describes a design the scene no longer uses
- Where: codemp/rd-vulkan/vk_stream.cpp:556-569, codemp/rd-vulkan/tr_local.h:3400
- Scenario: review TX-10's hold was replaced by the render scene refreshing its per-slot set when an image's view
  changes (`Scene_RefreshTextures`, vk_scene.cpp:6168). Nothing calls `R_StreamHold`. Its comment also claims
  "no tail drop (the failed state every one of those paths already skips)", which is not true of
  `R_StreamListIdle` (:1253) and `R_StreamIdleCap` (:1477): both drop a `failed` image to its tail. Harmless today
  because nothing holds a view, wrong if the function is ever used again.
- Fix: delete `R_StreamHold` and its declaration.
- Confidence: confirmed (grep, traced)
- Status: fixed (0c7b0ff91): `R_StreamHold` and its declaration removed

### RR-6 P3 `vk_upload_own` is unreachable
- Where: codemp/rd-vulkan/vk_upload.cpp:225-252, :287-292, header comment :31, `uploadOwn_t` and `up.own`
- Scenario: when the ring is full `vk_upload_buffer` calls `vk_upload_finish`, which waits for every batch and
  reclaims every segment, so the retry of `vk_upload_ring_alloc` (a piece is at most a quarter of the ring) always
  succeeds. The own-staging-buffer branch, its reclaim loop and its shutdown loop never run.
- Fix: remove the fallback (or keep one line that returns qfalse so `vk_fill_static_buffer` takes its one-shot path).
- Confidence: confirmed (traced)
- Status: fixed (0c7b0ff91): the own-staging fallback, `uploadOwn_t` and its reclaim and shutdown loops removed; the unreachable branch returns qfalse; `vk_upload_info` no longer prints the own-buffer count

### RR-7 P3 Dead exports in the VRAM ledger; one-time warning counters never reset
- Where: codemp/rd-vulkan/vk_vram.cpp:299 (`vk_vram_kind_bytes`), :305 (`vk_vram_device_bytes`), :271, :277, :292
- Scenario: neither function has a caller. `vram_degraded`, `vram_spilled` and `vram_spilledBuffers` count for the
  whole session, so after the first 16 (or 4) warnings a later map that runs out of VRAM says nothing in the console.
- Fix: delete the two functions; reset the three counters in `vk_vram_hook` or at a map load.
- Confidence: confirmed
- Status: fixed (0c7b0ff91): `vk_vram_kind_bytes` and `vk_vram_device_bytes` removed; the three warning counters reset at each `RE_LoadWorldMap` (`vk_vram_map_start`), and vkmem says they count since the map load

### RR-8 P3 `deviceMax` is cached in a function static across device changes
- Where: codemp/rd-vulkan/vk_image.cpp:1851
- Scenario: `r_device` changed and `vid_restart`: the DDS size limit stays the first device's.
- Fix: read `maxImageDimension2D` from the properties the renderer already keeps (or clear it in `vk_shutdown`).
- Confidence: confirmed (traced); only matters with two GPUs of different limits
- Status: fixed (0c7b0ff91): the device limit is read per DDS load, not cached in a static

### RR-9 P3 Worker threads are `std::thread` members of static structs, and the pipeline worker can call `ri.Error`
- Where: codemp/rd-vulkan/vk_mesh_jobs.cpp:77, codemp/rd-vulkan/vk_pipeline_async.cpp:60;
  codemp/rd-vulkan/vk_pipelines.cpp:812, :1089, :1480 (`ri.Error( ERR_DROP )` inside `vk_build_pipeline_ex`)
- Scenario: (a) a process exit that does not pass through `RE_Shutdown` (a recursive `Com_Error`, `Sys_Error`
  called directly) runs the DLL's static destructors with a joinable thread: `std::terminate` on top of the
  original error. (b) a pipeline definition with an invalid shader type, blend or cull value built on the worker
  raises the engine's error from the wrong thread. Neither happens with valid state.
- Fix: (a) hold the threads through a pointer that is never destroyed, or detach in a guard destructor; (b) make
  `vk_build_pipeline_ex` return `VK_NULL_HANDLE` on those paths when called with settings from the worker (the
  draw's own compile then reports the error on the main thread).
- Confidence: likely (by reading)
- Status: fixed in part (0c7b0ff91): (a) both workers are held by pointer, created with new and deleted after the join. (b) deferred: the `ri.Error` calls are in `vk_pipelines.cpp` (shading lane) and need a worker-aware return path through `vk_build_pipeline_ex`; reachable only with an invalid pipeline definition

### RR-10 P3 `R_StreamBindImage` leaks its range when the bind fails
- Where: codemp/rd-vulkan/vk_stream.cpp:851-853
- Scenario: `qvkBindImageMemory` returns an error after `R_StreamBlockFit` took the range: `b.used` stays raised and
  the range is in neither `freeRanges` nor `st_ranges`, so that block never empties and is never returned.
- Fix: on failure put the range back (record it in `st_ranges` first and call `R_StreamReleaseImage`).
- Confidence: confirmed (traced); needs a failing bind to matter
- Status: fixed (0c7b0ff91): the range is recorded before the bind and released through `R_StreamReleaseImage` when it fails

### RR-11 P3 Leftovers in changed stock functions
- Where and what:
  - codemp/rd-vulkan/vk_bloom.cpp:94, codemp/rd-vulkan/vk_dynamic_glow.cpp:131, codemp/rd-vulkan/vk_lightgrid.cpp:124:
    `offsets`, `offset_count` (and the loop index) are unused since the restore loop became
    `vk_restore_main_descriptor_sets()`.
  - codemp/rd-vulkan/vk_image.cpp:2803-2804: `blockBytes` computed and then `(void)`-ed in `R_LoadArrayDDS`.
  - codemp/rd-vulkan/vk_image.cpp:2816-2830 and :2890-2900: the `image_t` set-up is written out twice beside
    `R_NewImage` (also noted by TX-15).
  - codemp/rd-vulkan/vk_vbo.cpp:1036: `std::vector<vec4_t>` (an array element type) compiles on the toolchains used
    here but is outside what the standard requires of a vector element; a `std::vector<float>` of `4 * numVerts`
    is the portable form.
- Fix: remove the unused locals; one `R_NewImage` call per array path.
- Confidence: confirmed
- Status: fixed (0c7b0ff91): unused `offsets`/`offset_count` removed from bloom, dynamic glow and light grid (no loop index was left); `blockBytes` removed from `R_LoadArrayDDS`; both array paths call `R_NewImage` (then set the array flags and REPEAT as before); the tangent scratch is `std::vector<float>` of 4 per vertex

### RR-12 P3 The DDS range buffer's `VirtualAlloc` path documents an unexplained fault
- Where: codemp/rd-vulkan/vk_image.cpp:1820-1848 (`DDS_AllocRange` / `DDS_FreeRange`)
- Scenario: the comment says a temp-hunk block was found decommitted "by nothing the renderer frees" and moves the
  buffer to its own pages on Windows. The cause is not known, so the same fault can still reach any other temp-hunk
  user; other platforms keep the old path. Not a new bug: a workaround whose reason should be tracked outside the
  source before publishing.
- Fix: keep the code; move the incident note to docs and leave a one-line reason in the comment.
- Confidence: confirmed (as a cleanliness item)
- Status: deferred: comment-only (a dated incident note); the contract leaves dated comments for the separate sweep. The code stays

## Publish cleanliness

No absolute local paths, user names, handles, URLs or credentials in the lane's files. Items:

- codemp/rd-vulkan/tr_init.cpp:1342-1357, :1009, :1393: cvar help strings (shown to players by the console) carry
  process notes: "(owner 2026-11-10: on) Review SH-3: ...", "Review TX-17 ... (default 1, owner 2026-11-10)". Fix:
  keep the sentence that says what the cvar does; drop the owner/date/review prefix.
- codemp/rd-vulkan/tr_init.cpp:967-971: comment quotes the owner and names `scenes.json`, a bench file not in the
  tree. Fix: state the defaults and point to docs/renderer-vulkan-in-xenjk.md only.
- codemp/rd-vulkan/tr_world.cpp:1431: comment cites a local research folder ("look/cp77ref-20261104
  runs/stutter_a-d"). Fix: drop the path, keep the measurement.
- codemp/rd-vulkan/vk_image.cpp:1505-1506 ("the owner's playtest, 2026-11-01"), :2706 (owner quote "ok lossless
  dds"), :1824 ("crash 2, 2026-11-05"); codemp/rd-vulkan/vk_mesh_jobs.cpp:292 and
  codemp/rd-vulkan/vk_pipeline_async.cpp:200 ("2026-11-01 P2: ... cause not found"). Fix: reword as plain reasons.
- "independent review F2 / F9 / F11" (codemp/rd-vulkan/vk_stream.cpp:1240, codemp/rd-vulkan/vk_image.cpp:2835,
  codemp/rd-vulkan/vk_frame.cpp:500, codemp/rd-vulkan/vk_init.cpp:798): those IDs resolve only through
  docs/review/2026-11-09-character-rendering.md. The "review TX-n / SK-n" tags (27 in this lane's files) resolve
  the same way; fine if docs/review ships, otherwise they are dead references.
- codemp/rd-vulkan/tr_init.cpp:1012 `r_arrayDump` and the `LOADARR` / `LOADIMG` prints with content hashes
  (codemp/rd-vulkan/vk_image.cpp:2690-2745, :2843-2870) are pack-tool support inside the loader. Off by default;
  listed so the decision to ship them is deliberate.
- Licence headers: all seven new files carry the GPL-2.0 header matching the tree. No ported third-party code found
  in them (the MikkTSpace glue and the staging path are upstream's).
- Commented-out blocks in the changed regions are upstream's (`vk_resize_geometry_buffer`, `RE_Shutdown`); none added.

Publish cleanliness status (fix lane, 2026-11-19):

- tr_init.cpp cvar help strings: fixed (0c7b0ff91): owner/date/review prefixes and "since <date>" / "Install NN" notes dropped from every help string in the file (the shading and characters cvars registered there included); the sentence that says what each does kept.
- tr_init.cpp:967-971 comment, vk_image.cpp / vk_mesh_jobs.cpp / vk_pipeline_async.cpp dated comments, "independent review Fn" and "review TX-n" tags: left for the comment sweep (contract).
- tr_world.cpp:1431: fixed (0c7b0ff91): the local research folder dropped, the measurement kept.
- `r_arrayDump` and the `LOADARR` / `LOADIMG` prints: deferred, owner decision (ship the pack-tool support in the loader or not). The help string now says "Tool:".
- Licence headers: nothing to do (all new files carry it).

## Liftability

- **DDS, layer arrays and whole-loaded textures live inside `vk_image.cpp`.** About 900 added lines
  (`R_DDSFormat` to `R_StreamedRawLevels`, `R_LoadArrayLayers` to `R_FindImageArray`) sit between stock functions
  and share its statics (`hashTable`, the scratch buffers). Shape: `vk_image_dds.cpp` (DDS and array-from-DDS) and
  `vk_image_array.cpp`, with `R_NewImage` and `generateHashValue` exposed from `vk_image.cpp`; the stock file keeps
  three hooks: the DDS probe in `R_FindImageFile_Load`, the one in `vk_create_phyisical_texture`, and
  `R_CreateStreamedRawImage` in place of `R_CreateImage`.
- **VRAM admission is written three times.** "Drop top levels until it fits `vk_vram_admit()`" appears in
  `vk_generate_image_upload_data` (:780), `R_LoadDDSImageFrom` (:1938) and, as level stepping, in
  `R_StreamWholeLoaded` / `R_StreamArrayLoaded`. Shape: one `vk_vram_fit( w, h, bytesPerLevel0, mips, &skip )` in
  `vk_vram.cpp`. The ledger itself lifts cleanly: `vk_vram.cpp`, the two `VK_ALLOCATE_MEMORY` macros and one
  `vk_vram_hook()` call.
- **Texture streaming's hooks are few and named** (`vk_create_image` usage bit, `vk_begin_frame`, `vk_texture_mode`,
  `vk_delete_textures`, `vk_shutdown`, `RE_BeginFrame`, the Ghoul2 surface add). What ties it down is the engine:
  `FS_Range*` and `FS_Prefetch*` appended to `refimport_t`. See the next entry.
- **Four cvars describe one appended import table.** `GetRefAPI` (codemp/rd-vulkan/tr_init.cpp:1746-1764) copies
  `refimport_t` in four pieces by `offsetof`, each guarded by a cvar the engine sets (`win_vulkan`,
  `cl_videoCaptureHooks`, `fs_prefetchApi`, `fs_prefetchApi >= 2`), and the export side uses a fifth
  (`r_modelWarmupApi`). Every new import adds a cvar and a `memcpy`. Shape: one integer the engine sets
  (`com_refimportLevel`, or the struct's size passed in a cvar) and one table in the renderer of
  `{ level, offsetof(first entry) }`; a derivative then carries one number.
- **Probe box fitting and blend-probe selection are inline in `tr_bsp.cpp`** (:3143-3275, 140 lines) and reached
  from the middle of `RE_LoadWorldMap_Actual`. Shape: move `R_CubemapLeaf`, `R_FitCubemapBox` and
  `R_SetCubemapBlendProbes` beside `vk_create_cubemap_blend_descriptors` in `vk_cubemap.cpp`; the stock function
  keeps one call.
- **`R_LoadPlanes` was replaced, not extended.** `world_t::planes` now holds only the planes the nodes use, indexed
  by rank, and the fog loader reads the file's planes through `R_FilePlane`. Nothing in the tree indexes
  `planes` by file plane number, but a derivative that does (decals, portals) breaks silently. Shape: rename the
  field (`nodePlanes`) so such code fails to compile.
- **Load profiling is hand-timed in `RE_LoadWorldMap` and `RE_LoadWorldMap_Actual`** (`t0` pairs around stock
  calls) while the rest uses `loadProfScope_t`. Shape: scopes everywhere, so the stock functions gain one line each.
- **The flat-world mark and few-surface paths in `tr_world.cpp`** keep per-world state in file statics keyed by
  `tr.world` and reset from `RE_BeginRegistration`. Shape: two fields on `world_t` filled at load, no reset hook.

Liftability status (fix lane, 2026-11-19, untangle commit 40c00b2e9):

- DDS and layer arrays: done, one file `codemp/rd-vulkan/vk_dds.cpp` (DDS, array-from-DDS and the RGBA8 array path, moved as they were). vk_image.cpp exports `R_NewImage`, `vk_block_level_size` and `R_ImageByName`; the whole-loaded raw path (`R_CreateStreamedRawImage`, `R_StreamedRawLevels`) stays in vk_image.cpp beside the admission flag it sets.
- VRAM admission: done for the two image loaders (`vk_vram_fit` in vk_vram.cpp, each caller passing its byte count). Not done for `R_StreamWholeLoaded` / `R_StreamArrayLoaded`: their level stepping interleaves `R_StreamMakeRoom` (it evicts) with the admission test, so one helper would change what is evicted.
- Probe box fitting and blend-probe selection: done, moved to vk_cubemap.cpp; `RE_LoadWorldMap_Actual` keeps its call.
- One import level instead of four cvars: not done: engine and renderer protocol change (engine lane, owner decision on the scheme). RR-1 used `fs_prefetchApi 3` rather than a new cvar.
- `planes` to `nodePlanes`: not done: the rename touches every reader of `world_t::planes` across the renderer (other lanes' files).
- Load-profiling scopes in `RE_LoadWorldMap*`: not done: changes what the profile buckets include (timing semantics), not a pure move.
- Flat-world statics to `world_t` fields: not done: moves state between the loader and tr_world.cpp's marks and drops the registration reset; not mechanical.

## Still open from earlier reviews

- TX-6 (a raw or deflated texture, and a layer array's reload, is one main-thread step: `R_StreamReloadStep` and
  `R_ImageArrayUpload` decode on the main thread).
- TX-13 (streaming coverage is decided by the `models/players/` and `scn/` path prefixes).
- TX-15 in part (the RGBA8 array path is still uncompressed, and both array paths repeat `R_NewImage`; see RR-11).
- TX-16 in part (the renderer's image staging buffer grows to the largest upload and is not capped).
- TX-19 in part (`cache/vk_pipelines.bin` and the `.xtan` files are written in place; the rename waits for an
  engine export).
- TX-21 (non-mipmapped FMA maps load whole and are not admitted against the budget).
