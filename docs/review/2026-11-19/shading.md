# rd-vulkan shading and frame side (review 2026-11-19, master a322d9dc4)

Scope: xenjk's changes since e4fe7581 in `codemp/rd-vulkan/` (file lines / lines added by us):
tr_backend.cpp (2536 / 846), tr_main.cpp (2735 / 746), tr_shader.cpp (6181 / 635), tr_scene.cpp (881 / 178),
tr_local.h (3461 / 435), tr_xenui.cpp (2032, new), vk_sunshadow.cpp (632 / 378), vk_smoke.cpp (551, new),
vk_video.cpp (364, new), vk_shade_geometry.cpp (3515 / 430), vk_pipelines.cpp (2942 / 358), vk_gputime.cpp (330 / 152);
shaders/glsl: global.h, gen_frag.tmpl (+1302), gen_vert.tmpl, common/{deform,fma,pbr,ssao,utils}.glsl,
depth_extract.tmpl, ssao.frag, ssao_blur.frag, smoke.frag, surface_sprite_frag.tmpl, light_frag.tmpl, xenui_*.{vert,frag};
tools/renderer_spirv.py (362).

How it was read: every C/C++ diff hunk and the three new files in full; global.h in full and checked field by
field against the C structs and the std140 / push-constant rules; the xenui, smoke, ssao and depth-extract
shaders in full. gen_frag.tmpl: interface (specialization constants, sets and bindings, packed-float decoders),
the alpha / coverage / sample-mask / occluder section and the helper functions up to the character shadow lookup
were read; the lighting body after that (char shadow, self shadow, main lighting, about 900 lines) was not
verified line by line. Nothing was built or run; `renderer_spirv.py verify` (do the committed SPIR-V bytes match
the GLSL) is the one check this lane could not do by reading.

Layout checks that came out clean: vkUniformCamera_t, vkUniformEntity_t, vkUniformGlobal_t, vkUniformLightEntry_t
(C packing equals std140); vkExtractSSAO_t is exactly 128 bytes, vkBlurSSAO_t 64, vkExtractDepth_t 32; smoke.frag's
block equals vkSmokePush_t (128 bytes, layout range 128); xenui's geometry push (72), post push (128) and gradient
block (352 bytes, `vec4 stop_positions[4]` over the C `float pos[16]`) match tr_xenui.cpp; fragment specialization
ids 21..31 and the surface-sprite ids 15..20 match gen_frag.tmpl; DEFORM_OUTLINE is 18 on both sides; the
VK_MAT_* binding table (`s_matSlot`) matches global.h.

Files with no findings: tr_local.h (cleanliness only), gen_vert.tmpl, common/deform.glsl, common/pbr.glsl,
common/ssao.glsl, common/utils.glsl, depth_extract.tmpl, ssao.frag, ssao_blur.frag, smoke.frag,
surface_sprite_frag.tmpl, light_frag.tmpl, xenui_color/texture/geom/quad/post shaders.

## Findings

### RD-1 P2 The kept sun cascades' cache survives a map change
- Where: codemp/rd-vulkan/tr_backend.cpp:1572 and :1604 (the key), codemp/rd-vulkan/vk_sunshadow.cpp:392 and :499
  (the only places `cacheValid` is cleared), codemp/rd-vulkan/tr_bsp.cpp:47 and :3437 (`tr.world = &s_worldData`)
- Scenario: the key is `vk_scene_shadow_key() ^ (uint32_t)(uintptr_t)tr.world * ...`. `tr.world` is always the
  address of the one static `s_worldData`, so it says nothing about which map is loaded, and
  `vk_scene_shadow_key()` is constant when no render scene is loaded. The sun shadow image and `cacheValid` live as
  long as the device; a map change (RE_Shutdown / R_Init) does not touch them. Load map A with sun shadows, then
  map B with the same sun direction (two maps without their own `q3gl2_sun` under `r_forceSun`, or two builds of one
  map) and spawn in the same grid cell with the same fov and far split: `RB_FarCascadeReuse` returns true on the
  first frame and the far (and with `r_sunShadowMidCache` the middle) cascade shows map A's static casters until the
  eye leaves the cell.
- Fix: clear `vk.sunShadow.cacheValid[]` when a world loads or the renderer shuts down (RE_Shutdown beside
  `R_SmokeShutdown`, or RE_LoadWorldMap), or replace the pointer in the key with a world-load counter.
- Confidence: confirmed (traced); the coincidence of matrices needs the conditions above
- Status: fixed (ee8d51713): `vk_sun_shadow_cache_reset` from RE_Shutdown

### RD-2 P2 A stage's self-shadow tile is drawn over the world's tile 0 without a clear, so both read merged depth
- Where: codemp/rd-vulkan/tr_main.cpp:2436 (`first = s_numCharShadowViews`, 0 again after the world scene's
  `R_RenderCharShadows`), :2503 (tile 0's rectangle), codemp/rd-vulkan/tr_backend.cpp:1699 (one pass over layer 3,
  cleared once), codemp/rd-vulkan/tr_cmds.cpp:194 (capacity check)
- Scenario: a frame with a world scene that has a self-shadowed character and an RDF_STAGE scene (any stage menu
  while `r_stageCovers` is 0, and the first frame of one that sets it). Both scenes give their nearest figure
  self-shadow tile 0; all tiles are drawn in one render pass with LESS_OR_EQUAL depth and no clear between them, so
  the tile ends up holding the nearer of the two figures' depths, each drawn with a different matrix. The comment
  at tr_main.cpp:2440 says the later draw wins; with a depth test it does not, so the stage figure is shadowed by
  the world character's depth (and the reverse, which the comment accepts). Second form: with 8 character shadow
  tiles and 5 self-shadow tiles in the world scene `charShadowCmds` (13) is full, the stage's tile command is
  dropped at tr_cmds.cpp:194 and the stage figure samples the world's tile outright.
- Fix: before a later scene's tile is drawn into a rectangle already used this frame, clear that rectangle
  (`vkCmdClearAttachments`, depth 1.0, the tile's rect); or reserve a tile for the stage. Count the stage's tile in
  the array size (`+ 1`).
- Confidence: likely (traced by reading; which menus draw a stage over a live world needs a run)
- Status: fixed (ee8d51713): the later tile's rectangle is cleared (depth 1.0) when an earlier tile this frame overlaps it; array + 1. Seeing it in a menu over a live world needs a run

### RD-3 P2 xenui textures RmlUi releases are never freed: every CreateTexture / LoadTexture is a new engine image
- Where: codemp/rd-vulkan/tr_xenui.cpp:294-313 (`R_CreateImage( va( "*xenui%d", ++serial ) ... )`), :351-361
  (release only clears the slot: "the engine's images live until the renderer shuts down")
- Scenario: RmlUi regenerates a font atlas when new glyphs appear and releases the old texture; documents that are
  opened and closed load and release their images. Each of those leaves an image_t, its VkImage and its descriptor
  set alive until RE_Shutdown (the next map change or vid_restart). A long session in menus or with chat text in
  many scripts grows VRAM and the image list without bound between map changes; the image list was already seen to
  outgrow MAX_DRAWIMAGES (vk_image.cpp:1503).
- Fix: on release keep the image in a small free list keyed by size and reuse it with `vk_upload_image_data`; or
  give xenui textures their own VkImage like the saved layers (`own`) and free them through the same deferred path
  (`freeAt`).
- Confidence: confirmed (traced) for the leak; its size in a real session needs a run
- Status: deferred: needs a design choice (reuse engine images by size with an upload, or xenui-owned images freed through `freeAt`) and a run to check reuse against frames in flight and the real growth

### RD-4 P2 renderer_spirv.py's default tree is a research checkout, not this repository's rd-vulkan
- Where: tools/renderer_spirv.py:36 (`DEFAULT_TREE = ROOT / '.codex-research' / 'renderer-bench' / 'src' /
  'jksunny-perf' / 'codemp' / 'rd-vulkan'`), :37 (WORK under `.codex-research`)
- Scenario: `python tools/renderer_spirv.py build` after editing `codemp/rd-vulkan/shaders/glsl/gen_frag.tmpl`
  compiles and rewrites `shader_data.c` in the other checkout (or fails on a clone that has no `.codex-research`),
  and the repository's embedded SPIR-V silently stays stale; `verify` reports on the wrong tree the same way. On a
  published clone the tool does not work without `--tree`.
- Fix: `DEFAULT_TREE = ROOT / 'codemp' / 'rd-vulkan'`; WORK under the system temp directory.
- Confidence: confirmed (traced)
- Status: fixed (e0220bec6)

### RD-5 P2 Every shadow view uploads an entity uniform block for every entity of the scene
- Where: codemp/rd-vulkan/tr_backend.cpp:631-655 (`vk_update_entity_constants`), called through
  `RB_UpdateUniformConstants` at :1669 (cascades), :1710 (each character tile), :1737 (hair map), :2125 (main view)
- Scenario: `r_g2LazyBones` restricts the bone upload to entities with surfaces in the view's draw list, but the
  entity block loop still runs over `refdef->num_entities` for each view: `R_SetupEntityLighting`, the fill/rim
  trigonometry and one `vk_append_uniform` per entity. A frame with 3 cascades, up to 13 character tiles and the
  hair map does that up to 18 times; a tile draws one Ghoul2 instance. With 150 entities that is about 2,700 blocks
  (padded to the uniform alignment) of geometry buffer and CPU per frame for data no draw reads.
- Fix: reuse the `drawn[]` pass (move it up into `RB_UpdateUniformConstants`) and skip entities without surfaces in
  the view, as the bone loop does; `r_cpuTimes`' `uniforms` section measures it.
- Confidence: confirmed by reading; the cost needs a run
- Status: deferred: needs a run. Skipping blocks also skips `R_SetupEntityLighting` for those entities in that view; it has to be shown that nothing outside the draw list (render scene pass, cloth, hair, flares) reads a skipped block, and the saving measured with `r_cpuTimes`

### RD-6 P3 The gradient block is bound with a 512-byte range but only its 352 bytes are reserved
- Where: codemp/rd-vulkan/tr_xenui.cpp:1263 (`bi.range = 512`), :1529 (`XenUI_Stream( sizeof( u ), &uoff )`)
- Scenario: a gradient draw whose block lands within 512 bytes of the end of the 8 MB stream: offset + range passes
  the end of the buffer (invalid dynamic offset; undefined on the device).
- Fix: reserve 512 bytes for the block, or set the range to `sizeof` the block.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713): the stream reserves `XENUI_GRADIENT_RANGE` (512) with a static_assert on the block

### RD-7 P3 A refused PushLayer still gets its PopLayer
- Where: codemp/rd-vulkan/tr_xenui.cpp:426-434 (returns without pushing at the limit), :452-458 (pop), :1720-1735
  (the backend also returns when a target cannot be made)
- Scenario: a document nests more than 15 layers, or a layer target fails to allocate: the push is dropped, RmlUi
  later pops, `xf.layers` and `xb.top` drop a real layer and the rest of the frame composites into the wrong one.
- Fix: count refused pushes (front end and back end) and swallow the matching pops.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713)

### RD-8 P3 SaveLayerAsTexture with a scissor outside the window creates a zero-sized image
- Where: codemp/rd-vulkan/tr_xenui.cpp:465-468 (front end tests the unclamped size), :1750-1755 (the back end's
  clamped rectangle goes to `XenUI_Image`)
- Scenario: scissor fully off screen, width and height positive: the clamped extent is 0 and `vkCreateImage` gets
  extent 0 (invalid usage); the copy region is 0 as well.
- Fix: return early in `XB_SaveTexture` when the clamped extent is 0 (leave the texture without a set).
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713)

### RD-9 P3 A repeating gradient whose first and last stops coincide divides by zero
- Where: codemp/rd-vulkan/shaders/glsl/xenui_gradient.frag:58
- Scenario: `repeating-linear-gradient` with every stop at one position: `mod( t - t0, 0 )` is NaN, the element
  draws as NaN colour (black or garbage by driver).
- Fix: `if ( g.repeating != 0 && g.num_stops > 1 && t1 > t0 )`.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713); xenui_shader_data.c rebuilt

### RD-10 P3 The pipeline worker reads a cvar its contract says it does not
- Where: codemp/rd-vulkan/vk_pipelines.cpp:1549 (`r_g2SampleCoverage->integer`) under the comment at :855
- Scenario: `vk_build_pipeline_ex` runs on the pipeline worker with the settings captured in
  `vkPipelineSettings_t`; this one cvar is read live, so a build queued before the cvar changed compiles with the
  new value while the main thread's builds used the old one (and it is an unsynchronised read).
- Fix: add `g2SampleCoverage` to `vkPipelineSettings_t` / `vk_pipeline_settings()`.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713); the cvar is CVAR_LATCH, so the live read could only differ across a vid_restart

### RD-11 P3 s_descSerial is written under the lock and read without it
- Where: codemp/rd-vulkan/vk_shade_geometry.cpp:992, :1004 (increment under `s_descInfoLock`), :1030 (compare
  without it)
- Scenario: the mutex exists because descriptor writes come from more than one thread; the main thread's
  `s_pushed.serial == s_descSerial` races a worker's increment. Worst case is one stale material push.
- Fix: `std::atomic<uint32_t>`.
- Confidence: confirmed by reading
- Status: fixed (ee8d51713)

### RD-12 P3 tr.stageFrame is declared inside #ifdef VK_CUBEMAP and used outside it
- Where: codemp/rd-vulkan/tr_local.h:1908-1912; users tr_scene.cpp:747-748, tr_local.h:2451
- Scenario: a build without VK_CUBEMAP does not compile.
- Fix: move the field out of the block.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713)

### RD-13 P3 A failed video ring allocation is retried and warned every captured frame
- Where: codemp/rd-vulkan/vk_video.cpp:116-120, reached from :219-225
- Scenario: no host-visible memory type for the ring: `vk_video_alloc` fails, `vv.width` stays 0, and every frame of
  the recording creates a buffer, fails, prints the warning and falls back.
- Fix: remember the failure until the capture size or format changes.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713)

### RD-14 P3 A saved layer released when no command can be queued is never freed
- Where: codemp/rd-vulkan/tr_xenui.cpp:362-365
- Scenario: `XenUI_Command` returns NULL (command buffer full, or `!tr.registered`): `used` goes false, `freeAt`
  stays 0, so `XenUI_Begin` never frees the image and `XenUI_NewTexture` never reuses the slot (`own` is set) until
  R_XenUIShutdown.
- Fix: set `freeAt = xb.frame + NUM_COMMAND_BUFFERS + 1` directly when there is no command.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713)

### RD-15 P3 The sun shadow and hair map render passes are destroyed without vk_gputime_unregister_pass
- Where: codemp/rd-vulkan/vk_sunshadow.cpp:259, :484, :488 (registered at :73, :206)
- Scenario: after a vid_restart a new render pass that reuses one of these handles and is not itself registered is
  reported under "sun shadow" by `r_gpuTimes`; this is the case vk_renderpass.cpp:991 unregisters for.
- Fix: call `vk_gputime_unregister_pass` before each destroy.
- Confidence: confirmed by reading
- Status: fixed (ee8d51713)

### RD-16 P3 r_charSelfShadowDebugBox is parsed with sscanf for every view of every frame
- Where: codemp/rd-vulkan/tr_backend.cpp:417
- Scenario: a debug-only value (read by `r_charSelfShadowDebug 6`) costs a string parse per view (up to 18 a frame).
- Fix: parse when the cvar's `modified` flag is set, or only when the debug view is on.
- Confidence: confirmed (traced)
- Status: fixed (ee8d51713): parsed when its modificationCount changes

### RD-17 P3 The FMA face path looks a bone up by name for every draw of a face stage
- Where: codemp/rd-vulkan/vk_shade_geometry.cpp:3176 (`RB_G2BoneRotation( ..., "cranium", R )`)
- Scenario: per stage, per draw, per view (lit pass, prepass is cut earlier, glow): a string bone search in the hot
  stage iterator, and a bone name of one rig hard-coded in generic code.
- Fix: resolve the bone index once per model (cache on the mdxm data) and let the material name the bone
  (`fmaFaceBone <name>`, default cranium).
- Confidence: confirmed by reading
- Status: not a bug (as a cost): RB_G2BoneRotation (tr_ghoul2.cpp) caches the bone index per skeleton header, so the name search runs again only when the drawn rig changes; a material-named bone (`fmaFaceBone`) is a feature, deferred

### RD-18 P3 The smoke prototype is on by default and floods on the main thread
- Where: codemp/rd-vulkan/vk_smoke.cpp:404 (`r_smoke` default 1; `smoke` and `smoke_clear` always registered),
  :143-346 (`SmokeBuild`: up to 20,000 cells x 26 collision traces inside the console command)
- Scenario: any client can type `smoke`; the fill is a frame hitch of the printed milliseconds. The file's own
  header calls it a look-only prototype.
- Fix: default `r_smoke 0` (or register the commands only with it set) until it is a feature; nothing else needed.
- Confidence: confirmed by reading
- Status: deferred: owner decision, what ships: `r_smoke` default 1 and the always-registered `smoke` / `smoke_clear` commands

## Publish cleanliness

- tools/renderer_spirv.py:7: "extracted to <glslang>", a local absolute path in the docstring; say "set GLSLANG_EXE".
  - Status: fixed (e0220bec6)
- tools/renderer_spirv.py:38-39: Godot and glslang looked up at `ROOT.parents[1]` (the author's drive root); default
  to PATH lookup (`shutil.which`) and the environment variables.
  - Status: fixed (e0220bec6): GLSLANG_EXE / GODOT_EXE, else PATH
- tools/renderer_spirv.py:19: "On perf/vulkan 06e30f0c1, 600 of 607 matched" names a retired branch; drop.
  - Status: fixed (e0220bec6)
- codemp/rd-vulkan/tr_scene.cpp:597: "Character_Audit_Workspace/blender/render_character.py" is a file in a private
  workspace outside the repository; describe the rig without the path.
  - Status: fixed (ee8d51713)
- codemp/rd-vulkan/tr_main.cpp:2421, tr_local.h:2444-2445, tr_backend.cpp:2107: "the owner's play set
  (GameData/Play_Tifa.bat)" refers to a local install file; say "the reference play settings".
  - Status: fixed (ee8d51713): tr_main.cpp and tr_local.h; tr_backend.cpp had no such text
- codemp/rd-vulkan/vk_sunshadow.cpp:352: "owner 2026-10-09:" decision note in code; keep the rule, drop the tag. The
  same tag is in the r_fix* cvar descriptions ("(owner 2026-11-10: on)", tr_init.cpp:1342-1357, another lane's file).
  - Status: comment left for the comment sweep; the r_fix* descriptions are gone with the cvars or rewritten (ee8d51713)
- Review scaffolding: the `r_fix*` A/B cvars (all default 1 except `r_fixFmaShadows`), the `charReviewFix` bit field
  (global.h camera block, tr_backend.cpp:432-438, `ReviewFix()` in gen_frag.tmpl / pbr.glsl / fma.glsl) and the
  "review SH-n / SK-n / TX-n" tags through every file in the lane. For publishing: make the accepted fixes
  unconditional, delete the cvars and the uniform bits, and reword the tags as plain comments (the IDs point at a
  review document readers will not have open).
  - Status: fixed in part (ee8d51713): the fixes the 2026-11-09 record lists as accepted (SH-1, 3, 4, 5 exposure, 9, 10, 13, 14, 15, 16, 17) are unconditional, their 11 cvars and charReviewFix bits 1-32 removed. Kept: `r_fixFmaShadows` (default 0, owner decision) and bits 64+ (SH-18 hair map format, FMA shadows, `r_fmaDebug`); `r_fixSpecBlue` (TX-17) and `r_fixG2Scene*` (SK-1, SK-19) are other lanes'. Review tags in comments are left for the comment sweep
- Test and debug switches left in: `r_charHairMap16` (vk_sunshadow.cpp:129), `r_fmaDebug` (fma.glsl:71-77,
  tr_backend.cpp:438), `r_g2SampleReport` with the "SH8 per-sample stage" print inside RB_StageIteratorGeneric
  (vk_shade_geometry.cpp:3090-3101), `r_charOcclusionDebug >= 5` and `-1` prints (tr_backend.cpp:477-487, :1855),
  `r_portraitRig 2` prints (tr_scene.cpp), `charshadowdump` (tr_main.cpp:2612); `r_charShadowCull` and
  `r_charSelfShadowHairMap` are described in code as "test, latched". Decide which are tools worth keeping.
  - Status: deferred: owner decision, which debug tools ship
- Licence headers: codemp/rd-vulkan/tr_xenui.cpp and vk_video.cpp have none; vk_gputime.cpp and vk_sunshadow.cpp
  have a description block but no GPL-2.0 text (vk_smoke.cpp has the tree's header and is the model).
  - Status: fixed (ee8d51713) for tr_xenui.cpp and vk_video.cpp. Deferred: vk_gputime.cpp and vk_sunshadow.cpp came in with the JKSunny import (e4fe75812, perf/vulkan); whether they take the xenjk header (our perf/vulkan work) or JKSunny's attribution needs the owner
- vk_sunshadow.cpp:3: attribution "port of rend2's sun shadow maps" is present; tr_main.cpp's cascade set-up and
  gen_frag's sunPCF say "rend2" too. vk_smoke.cpp:27 "CS2-style" is a description, no code ported. No other ported
  code found in the lane.
  - Status: provenance listed, unchanged
- Dead locals: `(void)count;` at tr_backend.cpp:774 and vk_shade_geometry.cpp:1165 (with the `count = end - start + 1`
  lines above them); delete both.
  - Status: fixed (ee8d51713)
- tr_backend.cpp:2181-2188 and :2192-2200: the statements wrapped by the r_cpuTimes timers kept their old
  indentation (RB_ShadowFinish, RB_RenderFlares, RB_LightingPass sit left of their block); re-indent.
  - Status: fixed (ee8d51713)
- tr_local.h:1830-1838: `charOcclusion` has no comment and its description ("this view computes them after its depth
  prepass") is attached to `charOccGhoul2`; move it.
  - Status: fixed (ee8d51713)
- shaders/glsl/global.h:422: says the character pass's `reverse_depth` is "light size in radians"; ssao.frag:383 and
  vk_ssao.cpp:200 use it as the ray count (the size is in `lights[i].w`).
  - Status: fixed (ee8d51713)
- tr_shader.cpp:1558-1570: the `toon` keyword's documentation block sits above the `fmaBody` branch; the FMA
  keywords have no block of their own.
  - Status: fixed (ee8d51713): the toon block moved above `toon`, an FMA block added above `fmaBody`
- tr_backend.cpp:2020 and tr_main.cpp (`static int lastPrint` against `refdef.time`): after a map change the debug
  prints stay silent until the new map's clock passes the old value (tr_main's copies handle it; RB_CharOccRect's
  does not).
  - Status: fixed (ee8d51713)

## Liftability

- Character shadows and self-shadows: 630 lines inline in tr_main.cpp (:2071-2696) plus statics. Shape: a
  `tr_charshadow.cpp` exporting `R_SetupCharShadows`, `R_SetupCharSelfShadows`, `R_RenderCharShadows`,
  `R_CharShadowDump_f`; RE_RenderScene keeps its three calls and R_AddDrawSurf / R_AddEntitySurface their
  `R_IS_SHADOW_VIEW` tests.
  - Status: done (e14ab66de): tr_charshadow.cpp; R_RotateForViewer, R_SetupProjectionOrthoZ01 and R_AddEntitySurface lose `static` and R_SortDrawSurfs is declared (tr_local.h)
- Character occlusion (AO, contact masks, rectangle): about 330 lines in tr_backend.cpp (RB_SetupCharOccLights,
  RB_CharOccluders, RB_CharOccRectFind, RB_CharOcclusion) and about 110 lines of character terms inside
  vk_update_camera_constants / vk_update_entity_light_constants. Shape: `vk_charocc.cpp` with
  `RB_CharOccBeginView()` (the test now in RB_DrawSurfs), `RB_CharOcclusion()`, and
  `RB_CharCameraConstants( vkUniformCamera_t *, refdef, viewParms )` /
  `RB_CharEntityConstants( vkUniformEntity_t *, ent, refdef )` called from the two stock functions.
  - Status: done in part (e14ab66de): RB_SetupCharOccLights, RB_CharOccluders, RB_CharOccRectFind, RB_CharOccRect, RB_CharOcclusion and the camera block's terms (RB_CharOccCameraConstants) in vk_charocc.cpp. Not done: the 12-line view test in RB_DrawSurfs stays inline; the entity block's terms are character lighting (r_charLinear, fill, rim), not occlusion, and stay in vk_update_entity_light_constants
- Kept cascade cache: the policy is in tr_backend.cpp (RB_FarCascadeReuse, RB_RenderFarCascade with its statics),
  the images in vk_sunshadow.cpp, the fit in tr_main.cpp, and vk_scene.cpp asks RB_FarCascadeReuse. Shape: move
  both RB_ functions into vk_sunshadow.cpp and pass the draw callback; the cache key (RD-1) then has one owner.
  - Status: done (e14ab66de): RB_FarCascadeReuse and RB_RenderFarCascade in vk_sunshadow.cpp, which calls RB_RenderDrawSurfList (no longer `static`) directly rather than through a callback; RD-1's reset lives there too
- Portrait and stage rig: 140 lines in tr_scene.cpp between RE_BeginScene and RE_RenderScene. Shape: `tr_rig.cpp`
  with `R_AddSceneRig( fd )` as the one call in RE_RenderScene and `R_StageKeyLight` beside it.
  - Status: done (e14ab66de): tr_rig.cpp with R_AddSceneRig( fd, firstEntity, endEntity ) as the one call in RE_RenderScene
- Material keywords: about 380 lines of `else if` inside ParseStage plus a 125-line load block, and 150 lines of
  uniform and descriptor filling inside RB_StageIteratorGeneric. Shape: `tr_shader_material.cpp` with
  `qboolean R_ParseMaterialKeyword( stage, token, text, parseState )` (returns whether it took the token) and
  `R_FinishStageMaterial( stage, parseState, flags )`; `vk_stage_material( pStage, &uniform_global )` for the
  backend half. stageMaterial_t is already one struct, so the move is mechanical.
  - Status: not done: not a pure move. The branches use ParseStage's locals (twelve name buffers, toonShadeGiven, tintSet), the file-static `shader`, three static parse helpers, and `return qfalse` / `continue` inside the loop; extracting them means a parse-state struct and a three-way return, which wants a check that every installed shader parses the same (an offline parse harness or a run)
- Push-descriptor material set: self-contained in vk_shade_geometry.cpp (:981-1117) but it hooks
  `qvkUpdateDescriptorSets` through a macro in vk_local.h:487; a port to another rd-vulkan derivative must take the
  macro, `vk.materialPushSet`, global.h's VK_MAT_* and the layout in vk_pipelines.cpp together. Worth a short
  section in docs/renderer-architecture.md listing those four places.
  - Status: not done: a documentation section (docs/renderer-architecture.md), outside this lane's files
- Profilers: r_cpuTimes lives in vk_gputime.cpp (GPU timing file), r_loadProfile in tr_main.cpp and tr_local.h,
  SUNPROF in tr_main.cpp, with timer blocks inline in RB_DrawSurfs and RB_ExecuteRenderCommands. Shape: one
  `tr_profile.cpp/.h` with the scoped timer types; the stock functions keep one-line scopes.
  - Status: not done: the timer blocks are inline in stock functions across tr_main.cpp, tr_backend.cpp and vk_gputime.cpp with different clocks and sinks; scoped timer types are a rewrite, not a move
- SSAO push constants: the character pass reuses `bias`, `samples`, `reverse_depth`, `depth_falloff` and `power` for
  ray length, thickness, ray count, steps and hair opacity (ssao.frag:286-289, :383). Shape: a second struct of the
  same size for that pass (or a union with named fields) so neither side depends on comments.
  - Status: not done: vk_ssao.cpp is outside this lane, and a second struct is a redesign of the push block on both sides
- xenui is the model: one file, four hooks (RC_XENUI in the command switch, R_XenUISubmit in vk_end_frame,
  R_XenUIShutdown in RE_Shutdown, R_CreateXenUIShader in tr_shader.cpp) and an exported table. The only reach into
  stock internals is building an `image_t` by hand for the output (tr_xenui.cpp:1031-1048).
- vk_smoke.cpp and vk_video.cpp are self-contained (register / shutdown / draw or record hooks only).

## Still open from earlier reviews

From docs/review/2026-11-09-character-rendering.md, still as described on master: SH-6 (every dynamic light
evaluated per pixel, no per-entity mask), SH-7 (probe weights recomputed in SampleProbes and SampleIrradiance),
SH-8 (two-texture alpha-tested character stages have no sample-mask variant; `r_g2SampleCoverage` is the stopgap),
SH-11, SH-19, SH-26, and SH-5's shadow half (`r_fixFmaShadows` default 0, an owner decision).

Note for that document's index: SH-20, SH-21, SH-23 and SH-24 are listed there as designs, and master implements
them (CalcNormal's passed-in texels, the push-descriptor material set, the named camera floats and decoders, the
`debug_views` specialization constant); SH-1, 3, 4, 9, 10, 13, 14, 15, 16, 17 are listed as "default 0" and default
to 1 on master.
