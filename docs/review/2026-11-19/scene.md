# Render scene (review 2026-11-19, master a322d9dc4)

Scope: `codemp/rd-vulkan/vk_scene.cpp` (12,846 lines, all read; there is no other `vk_scene*` source or header: the
prototypes live in `vk_local.h:1627-1632, 1688-1706`), and the 25 shaders under
`codemp/rd-vulkan/shaders/glsl/scene/` (5,553 lines, all read), with the call sites in `tr_backend.cpp`, `tr_main.cpp`,
`tr_scene.cpp`, `tr_light.cpp`, `tr_bsp.cpp`, `tr_init.cpp`, `vk_frame.cpp`, `vk_renderpass.cpp`, `vk_attachments.cpp`,
`vk_image.cpp`, `vk_instance.cpp`. Read only: nothing built or run, so "confirmed (traced)" means traced in code.

Files with no findings (code or comments): `scene_grade.frag`, `scene_grade.vert`, `scene_sky.vert` (see RS-38 for the
pipeline it is paired into), `scene_scatter.comp`, `scene_groupcull.comp`, `scene_viewcull.comp`, `scene_lightfade.comp`,
`scene_distant.comp`, `scene_meter.comp`, `scene_rotate.comp`, `scene_water.comp`. Comment-only items (session notes,
duplicated helpers): `scene_capture.comp`, `scene_occ.comp`, `scene_sort.comp`, `scene_ssr.comp`, `scene_fog.comp`,
`scene_want.comp`, `scene_cull.comp`, `scene_compact.comp`.

Host/shader layouts checked and matching: `sceneFrame_t` against the `Frame` block of scene.frag (whole) and its
prefixes in scene.vert, scene_sky.vert, scene_fx.vert, scene_pdecal.frag and scene_fog.comp (through `surface2`);
`sceneSsrParams_t` against scene_ssr.comp; `scenePush_t` (128 B), `sceneCullPush_t` (128 B, also scene_groupcull), and
the push ranges of pdecal (96), scatter (16), occ (64), compact (48), cluster (80), viewcull (80), lightfade (48),
distant (16), water (32), rotate (32), meter (16), grade (16), want (48); every set layout table
(`cullTypes[16]`, `scatterTypes[5]` with its 8-view array, `sortTypes[5]`, `occTypes[2]`, `drawTypes[30]` and its stage
flags, `ssrTypes[7]`, `captureTypes[4]`, `fogTypes[16]`, cluster 8, compact 7, pdecal 5) against the shaders' bindings;
the record layouts (`Material`, `Light`, `Instance`, `Mesh`, `Lod`, `LightBox` 20 floats, `ShadowFace`, pdecal
records); the Occ block's word offsets (scene_cluster.comp's `pad[90]` lands on `counts[6]`, `[7]`); buffer capacities
against what the shaders write (cull pool `64 + cap * 16`, visible `cap * 4`, occlusion list, touched and sort lists
`numCmds`, receivers 112 B, view-light and view-cull counts); the host clamps on the shaders' divisors and loop bounds
(SSR steps 4..64, rays 1..2, AO samples 1..32); the variant defines in `tools/scene_spirv.py`.

Counts: P0 0, P1 1, P2 17, P3 39.

## Findings

### RS-1 P1 The per-material light-shadow path culls without the pool reset or the scatter, so the rain view drawn at load is wrong
- Where: codemp/rd-vulkan/vk_scene.cpp:4043-4087 (`Scene_RenderLightShadows`); compare `Scene_LightShadowBatch` 3678 and 3730, which call `Scene_CullPoolReset` / `Scene_CullPoolFinish`. Shader side: scene_cull.comp:124-160 (`Emit`), scene_scatter.comp:36-56. Template `firstInstance = 0` at 9826; pool header zeroed at 2644-2645.
- Scenario: the cull only takes pairs in the slot's pool and bumps `instanceCount`; `firstInstance` and `visible[]` are written by scene_scatter.comp alone. This path copies the template, dispatches the cull and draws, with no reset and no scatter. If the listed path has not run on slot 0, the capacity `hdr[12]` is 0, every `Emit` is left out and nothing is drawn (the rain layer stays at depth 1: every up-facing surface wet, interiors included; with `r_sceneLightShadowLists 0` every light face is empty too). If it has (the default), the draws read `visible[0 .. instanceCount)`, which holds the last listed batch's instances. The rain view is never listed (3360), so it always takes this path at 10108, 8629 and 12497; a map whose rain view does not follow the eye never gets a correct redraw (a following one is repaired by `Scene_RainFrame`, which goes through the batch).
- Fix: in the `if ( n )` block call `Scene_CullPoolReset( cb, 0 )` before the culls, set `push.counts[3] |= v << 8`, and `Scene_CullPoolFinish( cb, 0, ( 1u << n ) - 1u )` after them; better, one push-building helper shared with `Scene_LightShadowBatch`.
- Confidence: confirmed (traced); the visible symptom needs a run
- Status: fixed (d5fc921be)

### RS-2 P2 The mesh-decal pipeline gives its attachments different write masks without `independentBlend`
- Where: vk_scene.cpp:1859-1861 (`colorWriteMask = i < 3 || projected ? RGBA : 0`), 1871 (`attachmentCount = 5`); vk_instance.cpp:829-885 (the feature is never enabled; no other reference in codemp)
- Scenario: any scene with mesh decals. With the feature off every `pAttachments` entry must be identical (VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605); a driver may apply attachment 0's state to all five, and scene.frag's DECAL variant declares no outputs 3 and 4, so the surface and emission targets would take undefined values the lit pass reads as roughness, metalness, puddle and emission.
- Fix: in the DECAL variant declare outputs 3 and 4 written `vec4( 0.0 )` (alpha 0 leaves the destination under the existing blend) and use the full mask on all five; or enable `independentBlend` where supported. Correct the comment at 1808-1809.
- Confidence: confirmed (traced); whether a driver misbehaves needs a run
- Status: fixed (d5fc921be): the DECAL variant writes outputs 3 and 4 as zero at zero alpha; one full write mask on all five

### RS-3 P2 A teleport frame updates descriptor sets already bound in the frame's recording command buffer
- Where: vk_scene.cpp:10790-10791 (rain and faces recorded), 10809 (`Scene_StreamFrame`, which may settle), 8053 and 8108 (the rain batches bind `sc.compactSet[f][0]`), 8629/8501 -> 3855-3858 and 3814-3836 (`Scene_LightShadowsListed` -> `Scene_LightShadowSets`)
- Scenario: the eye jumps more than 2000 units with a following rain view. `Scene_RainFrame` records its batch with `sc.compactSet[f][0]` bound; then the settle's `Scene_LightShadowSets( qtrue )` rewrites binding 1 of that set (and binding 9 of `sc.cullSet[f][v]`) and restores them. Updating a set bound in a recording command buffer invalidates it. It also memsets `sc.receiversPtr[f]` for both slots mid-frame.
- Fix: run the settle decision (or all of `Scene_StreamFrame`) before `Scene_RainFrame` / `Scene_StreamFacesFrame`, or give the rain path its own packing set as the faces have `ss.faceCompact`.
- Confidence: likely (bind and update traced; the effect needs a validation run)
- Status: deferred: needs a validation-layer run; moving Scene_StreamFrame ahead of the rain and face recording also changes when streamed arrivals reach the rain view

### RS-4 P2 A half-failed block read leaves its other request writing into staging memory that is reused or unmapped
- Where: vk_scene.cpp:8696-8700, 8733-8738 (ring pop), 7322-7330 (`Scene_StreamFree`), 8515-8527 (`Scene_StreamSettle`)
- Scenario: a block's index read reports -1 while its vertex read is still pending on the prefetch worker (or the reverse). The entry is marked `done` and the pending request is not cancelled; it pops from the ring at once, so `Scene_StreamRingAlloc` can hand the same bytes to a new read while the worker still writes there. `Scene_StreamFree` cancels only `!r->done` reads, so on a map change the worker can write into unmapped staging memory.
- Fix: in the failure branch call `ri.FS_RangeCancel` on whichever of `reqI` / `reqV` still has state 0 before setting `done`.
- Confidence: confirmed (traced); needs one read to fail while its pair is pending
- Status: fixed (d5fc921be)

### RS-5 P2 A block that cannot fit the staging ring stalls every block queued behind it
- Where: vk_scene.cpp:8280-8281 (`break` in `Scene_StreamIssue`), 8550-8551 and 8579-8580 (`Scene_StreamSettle`), 8329-8333 (the widening in `Scene_StreamArrived`), 7535 (staging sized once)
- Scenario: a block whose indices reach past its planned vertices is re-queued with `vCount = hi + 1`. If its new size exceeds `ss.stagingSize` (also any block over the 1 GB cap), `Scene_StreamRingAlloc` fails forever; Issue breaks at it every frame, so no farther block is read, and a settle drops the rest of the queue without counting it failed.
- Fix: when `Scene_BlockBytes( b ) > ss.stagingSize`, mark the block `SB_BAD` with a warning and `continue`.
- Confidence: confirmed (traced); needs a shard whose parts are not written in vertex order
- Status: fixed (d5fc921be): marked bad with a warning in Scene_StreamIssue and Scene_StreamSettle

### RS-6 P2 F0-only .xw3b materials read the vertex stream after it has been replaced by a 16-byte dummy
- Where: vk_scene.cpp:5229-5237 (the `-5` record bumps `numF0`, not `sc.numW3Blend`), 5342-5345; scene.vert:188-192 (`else if ( detailKind <= -2.0 )` reads `w3Stream[gl_VertexIndex * 3 ...]`)
- Scenario: a valid .xw3b whose materials are all layerless with a specular colour (only `-5` records) and no .xw3t. `sc.w3Stream` is the 16-byte buffer, yet every vertex of those materials indexes it by `gl_VertexIndex * 3`: an out-of-bounds storage read (undefined without robustBufferAccess).
- Fix: narrow the shader test to the kinds that use `vUV1` / `vCol`, or keep the stream when `numF0` is non-zero.
- Confidence: confirmed (traced); whether a shipped map has such a file was not checked
- Status: fixed (d5fc921be): the stream is kept when any -5 record exists

### RS-7 P2 scene_cluster.comp assumes full subgroups and at most four of them; the pipeline requires neither
- Where: shaders/glsl/scene/scene_cluster.comp:22 (`SUB_MAX = 4`), 51-52, 158-182 (`tileLights[gl_SubgroupID]`, `pos = base + gl_SubgroupInvocationID`, `base += gl_SubgroupSize`); vk_scene.cpp:6658 (only `vk.subgroupSize` 16..64 is checked), 6727-6734 (stage flags 0)
- Scenario: a device whose compute subgroups can be smaller than the reported `subgroupSize` or partly filled (Intel: reported 32, actual 8/16/32 unless the stage sets `REQUIRE_FULL_SUBGROUPS`). With 8 lanes there are 8 subgroups in the 64 threads: `tileLights[sg]` / `tileKept[sg]` are written out of bounds for `sg >= 4`, and each run steps by the reported size while only the live lanes test, so lights are left out of clusters (surfaces and haze lose lights by tile).
- Fix: create the pipeline with `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT` (Vulkan 1.3 / `VK_EXT_subgroup_size_control`, `computeFullSubgroups`) and return qfalse without it (the light grid's cells are the existing fallback); or size the shared lists for 64 / the smallest subgroup and step by the ballot's live lanes.
- Confidence: likely (traced; needs such a device to show)
- Status: deferred: REQUIRE_FULL_SUBGROUPS needs computeFullSubgroups / VK_EXT_subgroup_size_control enabled at device creation (vk_instance.cpp, another lane) or a shader rework, and a device with varying subgroups to verify

### RS-8 P2 Without MSAA the auto-exposure meter only runs when a reflection/AO capture is wanted
- Where: vk_scene.cpp:11326 (`Scene_Meter()` called inside `vk_scene_capture`); tr_backend.cpp:1514-1534 (`if ( !ao && !capture ) return;` ... `if ( capture ) vk_scene_capture( qtrue );`); the MSAA path calls it unconditionally (tr_backend.cpp:1467)
- Scenario: MSAA off, a dry scene with no water and `r_sceneAO 0` (the default): `vk_scene_capture_wanted()` is false, the meter never runs, `sceneMeter.have` stays false and `r_sceneAutoExposure 1` (default) draws at the fixed `r_sceneExposure`. The same map with MSAA meters. The comment at 11276-11279 says the non-MSAA path is covered.
- Fix: export the meter as its own hook (`vk_scene_meter()`) and call it in `vk_postfx_pass` whether or not a capture is wanted.
- Confidence: confirmed (traced)
- Status: deferred: owner decision and a run: metering without MSAA ends the main pass early on frames that do not capture, and turns auto exposure on where it is off today (a look change for non-MSAA play)

### RS-9 P2 Scene visibility cvars are unprotected: on a scene map they remove the visual occluders
- Where: vk_scene.cpp:12632 (`r_scene`), 12636-12637 (`r_sceneLodBias`, `r_sceneCullPixels`), 12641 (`r_sceneHideScale`), 12828 (`r_sceneGpuDriven`), 12829 (`r_sceneDebug`, `CVAR_TEMP` without `CVAR_CHEAT`)
- Scenario: on a level-port map the BSP is the collision layer and the scene draws the level. A client on a pure server sets `r_scene 0` (or `r_sceneCullPixels 100000`, `r_sceneHideScale 0.01`, a `r_sceneDebug` view) and sees players through everything the scene alone draws. `r_sceneProxyDebug`, `r_sceneOnlyMesh`, `r_sceneSkip`, `r_sceneTerrainFlat` are already `CVAR_CHEAT`.
- Fix: `CVAR_CHEAT` on `r_sceneDebug`; clamp `r_sceneCullPixels` / `r_sceneHideScale` / `r_sceneLodBias` at read; whether `r_scene 0` is allowed on such a map is an owner decision.
- Confidence: likely (depends on what a converted BSP still draws)
- Status: fixed in part (d5fc921be): r_sceneDebug is CVAR_CHEAT like the other scene debug views; deferred: owner decision on clamping r_sceneCullPixels / r_sceneHideScale / r_sceneLodBias and on allowing r_scene 0 on a level-port map

### RS-10 P2 `r_sceneFogTemporal` reuses haze history across frames that drew no scene view
- Where: vk_scene.cpp:10393 (early return before `sc.hazeRan = qfalse` at 10494), 10481-10487, 11228-11230
- Scenario: `r_sceneFogTemporal 1` (default 0). After one or more frames with no scene view (a full-screen menu, `RDF_NOWORLDMODEL` frames) the next scene frame takes `hazePrevEye.w = 1` and blends 85% history from "the other slot's" scatter image, which is arbitrarily stale and reprojected with the older camera.
- Fix: clear `sc.hazeRan` in the early-return path, or stamp the history with the frame number.
- Confidence: likely (traced; the visible effect needs a run)
- Status: fixed (d5fc921be)

### RS-11 P2 `Scene_LoadW3Terrain` reads the layer table and image paths without a length check
- Where: vk_scene.cpp:5029 (only `len >= 16`), 5041-5044, 5065-5073
- Scenario: a truncated .xw3t with a valid header and `layers` 32: the loop copies up to 1024 bytes of layer values and 4096 bytes of paths past the file buffer and hands them to `R_FindImageFile`. The tiles are bounds-checked (5075, 5085); the layer block is not.
- Fix: after computing `tile` (5044), treat `tile > end` as an invalid file before the layer loop.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): the layer table is part of the header check

### RS-12 P2 `materialDetail` block indices travel as floats and are not guarded past 2^24
- Where: vk_scene.cpp:5091-5101 and every `d[1] = (float)( params.size() / 4u )` (5185, 5201, 5223, 5262, 5322, 5498); scene.vert:164-165, scene.frag:1394, 1431, 1482, 1521, 1559-1561, 1596 (`uint( md.y )`)
- Scenario: a .xw3t whose lattices total more than 16,777,216 vec4s (about 64 tiles at m = 1025; m up to 4097 is accepted at 5085). Later bases round to even, so tiles read control words, layer values and image indices from a neighbouring vec4 (wrong layers at best, an out-of-range `sceneTex[]` index at worst) with no warning.
- Fix: refuse with a warning once `params.size() / 4` reaches `1 << 24`, or carry the indices as bits (`floatBitsToUint`).
- Confidence: likely (arithmetic certain; whether a current map reaches it was not measured)
- Status: fixed (d5fc921be): the terrain stops before the detail passes 2^24 vec4s, with a warning; a warning if the whole buffer still does

### RS-13 P2 Unguarded normalisation of the vertex normal and tangent gives NaN shading
- Where: scene.frag:1697 (`normalize( vNrm )`), 1734 (`normalize( vTan.xyz - Ng * dot( Ng, vTan.xyz ) )`), 492, 1228 (DECAL); scene.vert:106-108
- Scenario: a vertex whose stored tangent is zero or parallel to its normal (a mesh without UVs, a degenerate UV triangle) on a material with a normal map, detail or W3 blend: `T` is NaN, so the colour is NaN, which enters the frame, the temporal SSR/AO history (it persists) and, in the DECAL variant, the DBuffer normal.
- Fix: `t0 = vTan.xyz - Ng * dot( Ng, vTan.xyz ); T = dot( t0, t0 ) > 1e-8 ? normalize( t0 ) : any perpendicular of Ng`; the same guard for `Ng`.
- Confidence: likely (no guarantee found in tools/levelport that such vertices are never written)
- Status: fixed (d5fc921be): safeNormalize / tangentOf in scene.frag (the same values when not degenerate)

### RS-14 P2 `Scene_RotatePass` retries every frame after a partial failure and leaks its objects
- Where: vk_scene.cpp:4691-4745, caller 4766
- Scenario: any step after `sc.rotLayout` fails; `sc.rotPipe` stays null, so `Scene_RotateFrame` calls it again each frame, overwriting `rotLayout`, `rotPipeLayout` and `rotPool` with new objects. Only the last set is freed.
- Fix: a tri-state "tried" flag as `ss.faceSets` (7717), or `Scene_RotateFree()` on the failure path and clear `sceneRots`.
- Confidence: confirmed (traced); needs a device failure
- Status: fixed (d5fc921be)

### RS-15 P2 `scenelshash` reads back an atlas created without `TRANSFER_SRC`
- Where: vk_scene.cpp:3562-3563 (usage set only when `r_sceneLightShadowHash` is on at load), 3912-3951, callers 12479, 12495, 12499
- Scenario: load with the default `r_sceneLightShadowHash 0`, run `scenelshash`: `vkCmdCopyImageToBuffer` on an image without the usage bit (invalid usage, driver-defined result, possibly garbage hashes).
- Fix: always add `TRANSFER_SRC`, or refuse the command when the cvar was off at load.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): the atlas records whether it was made readable; the hash and scenelshash refuse otherwise

### RS-16 P2 Side-file record counts are bounds-checked in `size_t`, which wraps on a 32-bit build
- Where: vk_scene.cpp:4561 (.xwat `count * 104u`), 4632 (.xrot), 4820 (.xscr), 5142 and 5375 (.xw3b), 5432 (.xdet `pad * 64u + count * 288u`)
- Scenario: the 32-bit renderer and a crafted count (0x10000000, or .xdet `pad` 0x04000000): the product wraps, the length check passes, and the loop reads past the buffer (the .xdet case also indexes far past `tableImages`). The 64-bit build is safe; .xfx (4891), .xlut (5558) and the .xscn header already use `uint64_t`.
- Fix: compute these in `uint64_t`; one shared side-file header check (see Liftability) closes all six.
- Confidence: confirmed for 32-bit (traced)
- Status: fixed (d5fc921be): also the tiled scene's .xwat ocean record offset and the .xfog / .xdet version 1 products

### RS-17 P2 Per-scene statics outlive the scene (stale `image_t *`, counts, look values)
- Where: vk_scene.cpp:5890-6126 (`R_SceneFree`). Not reset: `scenePd` CPU vectors, `count`, grid (1365-1396; cleared only in `Scene_LoadPDecals`, which runs only behind `if ( ok )` at 10081-10082), `sceneFx` (4863, 4880), `sceneCmdKeep`, `sceneCmdMat`, `sceneSkipApplied`, `sceneLodLead`, `sceneVisibleCapacity`, `sceneRain`, `sceneFileVertices`, `sceneSurfaceTubes`, `sceneOcean*`, `sceneEnvVars` / `sceneEnvCount` / `sceneEnvDay` (reset only at 9193-9194; `R_SceneLoad` returns at 9335-9339 before `Scene_LoadEnv`)
- Scenario: a scene map followed by a map without a scene, `r_scene 0`, or a failed load. The vectors keep tens of MB and pointers to images the map change freed; `Env()` and `Scene_Clock()` still answer with the previous map's look values and day flag. The image pointers are not dereferenced today (every user is gated on `sc.loaded`, `scenePd.pipe`, `sc.fxPipe`), so the live effect is the stale environment plus retained memory.
- Fix: clear all of them in `R_SceneFree` beside the other vectors (6106-6119), with a `Scene_FreePDecals()` for the CPU side.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): Scene_FreeCpuState in R_SceneFree

### RS-18 P2 Per-frame string parse and descriptor rewrite in `Scene_DrawPDecals`
- Where: vk_scene.cpp:1727 (`sscanf( r_scenePDecalNormal->string, ... )`), 1697-1717 (`vkUpdateDescriptorSets` for the depth view), 1641/1757 (`R_SunProfNow` pair with `r_scenePDecalDebug 0`)
- Scenario: every eye view with decals re-parses a three-int cvar and rewrites a descriptor whose view changes only at `vid_restart`.
- Fix: parse on `->modified`; write binding 4 once per slot; time only under the debug cvar.
- Confidence: confirmed
- Status: fixed (d5fc921be): parsed on modificationCount; the depth binding written when its view changes; timed only under r_scenePDecalDebug

### RS-19 P3 `scenePd.written[f]` caches the frame UBO binding with no link to the buffer's lifetime
- Where: vk_scene.cpp:1698-1718, 2807, 5915, 1565
- Scenario: correct today only because the buffers die in the same `R_SceneFree` that resets `written[]`; any path that recreates `sc.frame[f]` alone leaves set 0 on destroyed buffers.
- Fix: write bindings 0-3 where the buffers are created and drop `written[]`.
- Confidence: confirmed as latent
- Status: not a bug: latent only; written[] is reset in the same free that destroys the buffers (left for the pass split)

### RS-20 P3 `Scene_RenderLightShadows` rewrites slot 0's receivers before anything waits for the queue
- Where: vk_scene.cpp:4029-4030; mid-game callers 8629, 12497
- Scenario: at a settle in a frame recording slot 1, slot 0's previous frame may still be reading `receivers[0]`; the memset changes its cascade and hide culling mid-flight (a one-frame cull glitch).
- Fix: move the memset after the first idle wait, or use a dedicated receivers buffer as `ss.faceReceivers` (7726).
- Confidence: likely
- Status: not a bug: both mid-game callers wait for the queue first (Scene_StreamSettle's qvkQueueWaitIdle, scenelshash's vk_wait_idle) and the load caller runs before any frame

### RS-21 P3 Non-finite values in side files are not rejected in two places
- Where: vk_scene.cpp:4643-4644 (.xrot axis: a NaN length passes `n < 1e-6f`), 4845 (`templates[(uint32_t)Com_Clamp( 0, 5, f[0] )]`: a NaN converts with undefined behaviour and can index outside `templates[6]`)
- Scenario: malformed converter output.
- Fix: `if ( !( n >= 1e-6f ) ) continue;`; reject a non-finite `f[0]`.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-22 P3 A spot light with a zero `dir` gets a degenerate shadow face
- Where: vk_scene.cpp:3175-3182
- Scenario: the face matrix and planes are zero; the face takes atlas area, lists every instance in reach and renders nothing useful.
- Fix: skip the light (or treat it as a cube) when `VectorNormalize2` returns 0.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): the spot gets no face

### RS-23 P3 `Scene_StreamDrawFaces` clears dirty faces without drawing them
- Where: vk_scene.cpp:8126-8153, called at 8740-8741; 3841-3843
- Scenario: the fallback runs only when `ss.faceSets < 0`, under the same conditions that make `Scene_LightShadowsListed` return qfalse at once; the dirty flags are already cleared and counted as drawn.
- Fix: delete the fallback and leave the faces dirty for the next settle, or clear the flags only on qtrue.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): the flags are cleared only when Scene_LightShadowsListed drew them (it can also succeed after a face-set allocation failure, so the fallback stays)

### RS-24 P3 A settle leaves the other slot's pre-jump want pending
- Where: vk_scene.cpp:8669-8671, 8674-8676
- Scenario: only `ss.wantFrame[f]` is zeroed; the other slot's want, recorded before the jump, is taken next frame and rebuilds every `refs` from the old eye (the just-settled blocks become evictable, old-position reads are queued).
- Fix: zero every `ss.wantFrame[]` in `Scene_StreamSettle`.
- Confidence: confirmed (traced); the visible effect needs tight pools
- Status: fixed (d5fc921be)

### RS-25 P3 `R_SceneLoad`'s validation failures return without `R_SceneFree`
- Where: vk_scene.cpp:9382-9391, 9635-9638, 9646-9649, 9654-9656 (compare 9847-9848, 9953-9954, 10099)
- Scenario: a tiled, streamed scene that fails the header, range or device-limit checks: `Scene_ReadTiled` has set `ss.on` and left every shard's range handle open; `vk_set_scene_range( qtrue )` (9360) stays in force; `sceneInteriorBox`, `sc.numInteriors`, `sceneMoonImage` stay set until the next load.
- Fix: `R_SceneFree(); vk_set_scene_range( qfalse );` on those four paths (one failure exit).
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-26 P3 A map with a .xenv but no scene still has its `sun` line applied
- Where: vk_scene.cpp:9246-9249 (writes `tr.sunLight` / `tr.sunDirection`), called before the scene file is known to exist
- Scenario: `maps/x.xenv` present, `maps/x.xscn` missing or refused: the stock map's sun is overridden.
- Fix: apply the `sun` line once the scene has loaded, or document it as intended.
- Confidence: confirmed (traced)
- Status: deferred: design choice: whether a map's .xenv sun is meant to apply to a map whose scene is missing

### RS-27 P3 The light dedup comparator is not a strict weak order when an origin is NaN
- Where: vk_scene.cpp:9437-9444
- Scenario: a corrupt .xscn with a NaN `origin[0]` in two otherwise equal lights and `r_sceneLightDedup > 0`: `std::sort` has undefined behaviour.
- Fix: compare the origin's bits with memcmp like the rest of the key, or drop non-finite lights before sorting.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): a NaN x sorts after every number

### RS-28 P3 Tiled scenes with light boxes in only some tiles are refused with a misleading message
- Where: vk_scene.cpp:8996 (shapes padded per light), 9039-9045 (boxes not), 215-216, 9383
- Scenario: one tile has lights with `boxes 0`, another has boxes: the joined counts differ, `Scene_ReadHeader` rejects the buffer and the load prints "is not a version 1..6 render scene".
- Fix: pad a box-less tile's lights with zero boxes, or refuse in `Scene_ReadTiled` naming the tile.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be): refused in Scene_ReadTiled, naming the tile

### RS-29 P3 Finished reads are checked on the main thread with no per-frame budget
- Where: vk_scene.cpp:8687-8707 -> 8306-8326
- Scenario: every read that finished this frame (up to 1024) gets a full index max-scan and per-triangle cross product in `Scene_StreamArrived`; `r_sceneStreamUploadMB` limits only the copies.
- Fix: stop checking once the checked bytes pass the upload budget, or scan on the worker.
- Confidence: likely (cost not measured)
- Status: deferred: performance; needs a measurement first

### RS-30 P3 Rain tile bookkeeping runs every frame when nothing changed
- Where: vk_scene.cpp:7961-8009
- Scenario: two vectors built and sorted and the held-rectangle search run before the early return at 8008.
- Fix: return first when the window has not moved, no tile is stale and `sceneRain.dirty` is false.
- Confidence: confirmed (traced)
- Status: deferred: performance, not measured; the held-rectangle search also depends on the eye's tile within the window, so an early return needs that stored too

### RS-31 P3 `vk_scene_capture` reads an uninitialised `fog` when `r_sceneFog` does not parse
- Where: vk_scene.cpp:11441-11442 (contrast 10441)
- Scenario: `r_sceneFog ""`: stack garbage through `powf` into `sp->sky` (the reflection's miss colour), possibly NaN in the settled history.
- Fix: `vec3_t fog = { 0, 0, 0 };`
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-32 P3 GPU-driven `vk_scene_draw` pushes uninitialised push-constant fields
- Where: vk_scene.cpp:11984, 12012-12019, 11825-11826
- Scenario: `emissive`, `info[1..3]` and (without VRS) `tint.xyz` go to the GPU as stack bytes; no GPU_DRIVEN shader reads them today.
- Fix: `Com_Memset( &push, 0, sizeof( push ) )`.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-33 P3 `scenecullcount` prints dead and stale diagnostics
- Where: vk_scene.cpp:975-977, 10896-10900, 12559-12560, 12581-12583, 2691 (`Scene_CreateViews( uint32_t visibleCapacity )`, parameter unused), 9843, 12836
- Scenario: `sceneCullDbg[][].skipped/groups/bound` are only ever written `0 / 0xffffffff / numInstances`; `sceneVisibleCapacity` is the retired per-view list's size, yet the command reports "% of the capacity" against it.
- Fix: drop the three fields, `sceneVisibleCapacity` and the parameter; report against `sc.visCap[f]`.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-34 P3 `r_sceneFxDebug`'s first report is inflated
- Where: vk_scene.cpp:11946-11951
- Scenario: `drawn` / `slots` accumulate always, `frames` only while the cvar is on.
- Fix: accumulate all three under the cvar.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-35 P3 Debug-print cvars are archived
- Where: vk_scene.cpp `R_SceneRegister`: `r_sceneFxDebug`, `r_sceneExposureDebug` are `CVAR_ARCHIVE_ND` (the other stats cvars are `CVAR_TEMP`)
- Scenario: set once, they print every 60 frames in every later session.
- Fix: `CVAR_TEMP`.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-36 P3 Frame-slot parity hard-codes `NUM_COMMAND_BUFFERS == 2`
- Where: vk_scene.cpp:11228 (`scatter = f == 1 ? 2 : 0`), 2826 (`fogView[i == 0 && f == 1 ? 2 : i]`), and the "two frames ago" assumptions at 11296, 11351, 12147, 12342
- Scenario: raising `NUM_COMMAND_BUFFERS` makes slots 0 and 2 write the same scatter image while the "other" is read.
- Fix: `static_assert( NUM_COMMAND_BUFFERS == 2 )` beside `fogImage[3]`.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-37 P3 The frame block is built in mapped memory and read back from it
- Where: vk_scene.cpp:10403-10404 (memset of about 6.5 KB on `sc.framePtr[f]`), reads at 10422, 10442, 10458, 10481, 10521, 10596, 10609-10612, 10623-10624; redundant re-zeroing at 10469-10470, 10538-10540, 10547-10549, 10566, 10580, 10589-10591, 10621
- Scenario: per frame, reads from a host-visible (possibly write-combined) mapping and seven redundant clears.
- Fix: fill a stack `sceneFrame_t` and memcpy once (the later patches at 12224 and 12380 stay writes); delete the redundant clears.
- Confidence: likely (memory type not checked)
- Status: deferred: performance (memory type not checked); building the block on the stack touches every write of the frame block; needs a timing run

### RS-38 P3 Sky and particle pipelines pair the full scene.frag with vertex stages that do not feed it, with statically used sets unbound
- Where: scene_sky.vert:26-33 (outputs 0-7) against scene.frag:244-247 (inputs 9, 10, 11); vk_scene.cpp:2325, 2327-2328, 11790-11799, 11909-11938 (sets 0 and 1 only)
- Scenario: every GPU-driven frame with the sky, haze or particles: unmatched fragment inputs (VUID-RuntimeSpirv-OpEntryPoint-08743) and statically used sets 2-5 never bound (VUID-vkCmdDraw-None-08600). Modes 6, 10, 11 never read them, so desktop drivers draw correctly, but it is invalid usage.
- Fix: dedicated `-DSKY` / `-DFX` fragment variants, or write the three varyings in scene_sky.vert and bind white at sets 2-5.
- Confidence: confirmed (traced); the layer's output needs a run
- Status: deferred: needs dedicated sky / particle fragment variants (or matching varyings and bound sets) and a validation-layer run; desktop drivers draw it correctly today

### RS-39 P3 scene_fx.vert's AlphaByDistance relies on `smoothstep` with equal edges
- Where: scene_fx.vert:299-301; tools/levelport/cp77fx.py:360-363 sets the flag whenever the modifier exists
- Scenario: an emitter with one pair at (0, 0): `smoothstep( 0, 0, d )` is undefined; with the usual implementation the far factor is 0 for every d > 0 and the emitter draws nothing.
- Fix: apply each factor only when its outer edge exceeds its inner one.
- Confidence: likely (needs a data check)
- Status: deferred: needs a data check of cp77fx output; guarding equal edges makes such emitters draw where they draw nothing today

### RS-40 P3 A tangent frame built from world +X is NaN on a surface facing exactly along X
- Where: scene_pdecal.frag:229; scene.frag:1637
- Scenario: a tiled-normal projected decal or a W3 terrain fragment whose normal is exactly (+-1, 0, 0): `normalize( 0 )`.
- Fix: pick the reference axis by `abs( Z.x ) < 0.9 ? X : Y`.
- Confidence: confirmed by reading; rare in data
- Status: fixed (d5fc921be): the reference axis falls back to +y only when the normal lies along x (the same frame otherwise)

### RS-41 P3 `r_sceneHideDither 2` keeps at most 8 samples when MSAA has more
- Where: scene.frag:1186; vk_scene.cpp:10411-10413 (gated on `vkSamples > 1` only)
- Scenario: 16x MSAA: a fading instance covers samples 0-7 only and pops to half coverage as its fade starts.
- Fix: gate mode 2 on `vkSamples <= 8` as `sc.cutoutCov` is (10416-10417).
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-42 P3 The "nodebug" lit variant still carries the emissive debug views
- Where: scene.frag:1335-1350 (views 8, 15, 16, 20 sit outside `#ifndef NO_DEBUG_VIEWS`; 1329-1334 inside)
- Scenario: the default lit modules evaluate these branches for every emissive fragment.
- Fix: move the `#endif` at 1334 below 1350.
- Confidence: confirmed (traced)
- Status: fixed (d5fc921be)

### RS-43 P3 Misleading indentation in the sun term
- Where: scene.frag:1856-1858 (`col += sunPart;` indented under `if ( sun > 0.0 )` but outside it)
- Scenario: harmless now (`sunPart` is 0 otherwise); a later edit to its initial value changes the result.
- Fix: brace or outdent.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-44 P3 `R_SceneFree`'s early return skips the CPU-side cleanup
- Where: vk_scene.cpp:5893-5894
- Scenario: with `vk.device` NULL it returns before `Scene_StreamFree()` (FS range handles, `ss`) and the vector clears. Not reachable in the normal order (tr_init.cpp:1586).
- Fix: run the non-Vulkan cleanup before the device check.
- Confidence: confirmed as latent
- Status: fixed (d5fc921be)

### RS-45 P3 The grade's full-frame copy image is allocated when the in-place compute path will be used
- Where: vk_scene.cpp:5572, 5667; the choice at 11615
- Scenario: `vk.colorStorage` and the latched `r_sceneGradeCompute` are fixed for the device's life; once `gradeCompReady` is set the copy (about 66 MB of RGBA16F at 4K) is never read.
- Fix: create the compute path first; make `gradeCopy`, the fragment pipeline and its set only without it.
- Confidence: likely
- Status: deferred: memory only; reordering the grade's creation needs a run of both grade paths

### RS-46 P3 `Scene_MarkCardMeshes` clears mesh word 6 only when a billboard section exists
- Where: vk_scene.cpp:5392-5393; called only with `vk.gpuDriven` (9974-9975); scene_cull.comp:252 reads it as `M.lods.z`
- Scenario: a file with a non-zero mesh pad and no version 3 .xw3b switches the card-caster rule on.
- Fix: zero `meshes[i].pad[0]` for every mesh at load, then mark.
- Confidence: confirmed (needs such a file)
- Status: fixed (d5fc921be): zeroed for every mesh before the marking

### RS-47 P3 Linear image de-duplication at load
- Where: vk_scene.cpp:6264-6270, and `slotIndex` at 5057, 5164, 5438
- Scenario: O(materials x images) pointer compares on a large merged scene, at load.
- Fix: an `unordered_map<image_t *, uint32_t>` (and one shared `Scene_SlotIndex`).
- Confidence: confirmed; cost not measured
- Status: fixed (d5fc921be): an unordered_map for the GPU-driven image list (the slot-image lambdas over the much smaller slot list are unchanged)

### RS-48 P3 Dead code and leftovers
- Where and fix, each confirmed:
  - vk_scene.cpp:2178-2179, 2251-2252: `if ( 0 ) return qfalse;`, `if ( 0 ) ok = qfalse;`: delete.
  - 4480-4482: the `numFogVolumes > 4096` warning cannot fire (4444 caps the count): delete.
  - 4362: the `xsig` header's `count` and `used` are parsed and never used: check or drop.
  - 8443: `(void)f;` though `f` is used at 8429.
  - 8992-8994: `joined[k]` assigned twice.
  - 8896: inner `at` shadows the manifest cursor (8809).
  - 5911: `sceneRots.clear()` inside the per-slot loop.
  - 6251-6252, 6278-6279: `qvkGetPhysicalDeviceProperties` twice, the inner `props` shadowing the outer.
  - 6205-6218: `Scene_WriteTextures` repeats `Scene_TextureInfo`'s body.
  - 6908: a 1.5 MB zero vector built only to initialise `wavesRows`.
  - 5681, 12814: `r_sceneGradeCompute` registered twice, once with an empty description.
  - 5545, 10110-10115, 10134: unused return values of `Scene_LoadGrade`, `Scene_CreateClusters`, `Scene_CreateLightFade`, `Scene_CreateDistant`, `Scene_CreateViewCull`, `Scene_CreateAsync`.
  - 6134: `Q_strncpyz( name, path, MIN( sizeof name, 64 ) )` cuts a 64-character path to 63 while callers size for 64.
  - scene.frag:647: `{	// (was sunShadow's loop body)` leftover scope.
  - scene_compact.comp:147-155 and 198-206: two inline copies of `ScanSums` (48-59).
  - scene_cull.comp:168-173 and scene_want.comp:33-38: `HideCentre` duplicated with its comment.
- Status: fixed (d5fc921be): the if ( 0 ) lines, the 4096 warning, the xsig %n, (void)f, the double joined[k], the shadowing at (renamed and 64-bit), sceneRots.clear in the loop, the second properties query, Scene_WriteTextures' copy, the empty r_sceneGradeCompute help. Not a bug: the 63-character cut (MAX_QPATH caps every game path at 63). Kept: wavesRows' zero fill (whether the first pass reads rows before writing them was not checked), the unused return values, scene.frag's scope; deferred: the ScanSums / HideCentre shader copies (a shared include, see Liftability)

### RS-49 P3 Forward declarations inside the `extern "C"` SPIR-V block, and repeated ones
- Where: vk_scene.cpp:154-156 (`Scene_SortVisible`, `Scene_ImageBarrier` get C linkage by accident); `Scene_DeviceCommands` declared at 2623, 3619, 7879 (defined 10243); `Scene_CullPoolCheck` at 2689 (defined 10334)
- Fix: move below line 161; keep one declaration each, or move the short definitions up.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-50 P3 Write-only state in `sc`
- Where: vk_scene.cpp:421 `numWindows` (only 9697), 481 `numInteriorInst` (only 9613), 615 `gradeSize` (only 5665); 641 `lsTexels` (set 3299-3302; reader not confirmed)
- Fix: remove, or print in `sceneinfo`.
- Confidence: confirmed for the first three
- Status: fixed (d5fc921be): removed, lsTexels too

### RS-51 P3 `Scene_WriteSet` guards its 16-entry arrays with `assert` only
- Where: vk_scene.cpp:2363-2366; callers at 2969 and 7757 already pass 16
- Scenario: a 17th binding overruns `bi` / `wr` on the stack in a release build (`Scene_SetLayout` got a runtime check after exactly this).
- Fix: the same runtime check, or size the arrays to 32.
- Confidence: confirmed (latent)
- Status: fixed (d5fc921be)

### RS-52 P3 `Scene_CreateBuffer` leaves `b->mem` untouched when `vkCreateBuffer` fails
- Where: vk_scene.cpp:1005-1008 (a stack `sceneBuffer_t` such as `staging` at 1042 would later free garbage; no caller does today)
- Fix: set `b->mem = VK_NULL_HANDLE; b->size = 0;` on that path.
- Confidence: confirmed (latent)
- Status: fixed (d5fc921be)

### RS-53 P3 Optional pipelines' failures are silent
- Where: vk_scene.cpp:2325 (`pipeDepthFade`), 2329-2332 (`fxPipe`), 2342 (`SCENE_PIPE_WATER`)
- Scenario: a failed create turns the feature off with no log line.
- Fix: one `PRINT_WARNING` per failed optional pipeline.
- Confidence: confirmed
- Status: fixed (d5fc921be): the depth fade and the particles (the water's already warned at load)

### RS-54 P3 The projected-decal grid clamp can fold far cells into the last one
- Where: vk_scene.cpp:1512-1517 against 1661 (the cell's bound sphere assumes its nominal position)
- Scenario: decal centres spanning more than 4096 x 1200 units on an axis: folded records are culled by the wrong sphere. Beyond any current map.
- Fix: warn or grow the cell size when the extent exceeds the clamp.
- Confidence: confirmed by reading
- Status: fixed (d5fc921be): a warning

### RS-55 P3 Stale log values when the decal file yields no records
- Where: vk_scene.cpp:1542-1544 prints `gridW/gridH`, set only inside `if ( scenePd.count )` (1509)
- Fix: zero them at 1394.
- Confidence: confirmed
- Status: fixed (d5fc921be)

### RS-56 P3 The teleport settle line prints at `PRINT_ALL`
- Where: vk_scene.cpp:8636 (every teleport, unlike SCENESTREAM, which is cvar-gated)
- Fix: `PRINT_DEVELOPER`, or gate on `r_sceneStreamStats`.
- Confidence: confirmed
- Status: fixed (d5fc921be): PRINT_DEVELOPER unless r_sceneStreamStats

### RS-57 P3 FS range handles: one import unchecked, closed handles not zeroed
- Where: vk_scene.cpp:9366-9367 (`ri.FS_RangeClose` is used at 7333, 8970, 9001, 9090 but is the one FS_Range import not null-checked in `wantStream`); 8969-8972 with 9001/9005/9086 (on a failed open the earlier handles are closed but not zeroed, so `closeAll()` closes them again)
- Fix: add the check; zero each handle when closed.
- Confidence: confirmed (harmless today)
- Status: fixed (d5fc921be)

## Publish cleanliness

No absolute local paths, user names, URLs, credentials, TODO/FIXME or commented-out code blocks in the lane. The file's
licence header (vk_scene.cpp:1-19) is GPL-2.0 and matches the tree. The scene shaders carry no licence header (the
stock shaders beside them were not compared).

Agent, owner and session references (reword to the technical reason, drop names, quotes and dates):
- scene_cull.comp:166-167, scene_want.comp:31-32, scene.vert:83: "review 2026-11-17, Fable and Astra"; scene.frag:331: "the Fable review".
- vk_scene.cpp:12678: cvar help text "the owner's pick, 2026-11-04" (user-visible).
- scene.frag:940, 1141 ("nani"), 1178, 1675; scene.vert:68, 206-207, 214-215, 232; scene_fx.vert:123; scene_pdecal.frag:158 ("omega z fighting"); scene_ssr.comp:135, 165, 284; scene_capture.comp:6; scene_occ.comp:45: quoted owner chat and owner dates.
- vk_scene.cpp:177 ("renderer session 2026-11-03"), 9166 and scene_sort.comp:13, scene_occ.comp:47, scene_want.comp:89 ("review of perf-3, 2026-11-11"), vk_scene.cpp:9412 ("the class A/Bs (2026-11-04)"), 11992 ("2026-11-16"), 6656-6657 and scene_fog.comp:326 ("night-2's ..."), scene.frag:1625, 1807-1808 ("Night City session, 2026-11-17"), scene_fog.comp:101-102.
- vk_scene.cpp:10511, 12130 ("review F10"), 12232 and vk_local.h:1699 ("review SH-25"): internal review IDs with no public referent.
- vk_scene.cpp:1357: cites `look/decals-20261106/CONTRACT.md` (not tracked); 5270: cites `.codex-research/w3-billboards-20261118/CONTRACT.md` (local research path). Move the contracts into `docs/` or drop the references.
- vk_scene.cpp:1093, 8468-8469: history in a comment ("passed 24 for a while", "Before, every face (8,191 in Watson) ..."); keep the fact, move the story to docs.

Stale or misplaced comments:
- vk_scene.cpp:175: the "dynamic lights" text belongs to `SCENE_MAX_DLIGHTS` (172).
- 1357-1360: .xdcl header says version 1 / 80 B materials; the loader takes 1 and 2 (112 B).
- 1808-1809: "three targets ... no independent blend"; the code has five targets and two masks (RS-2).
- 2438-2442: "two rgba16f 3D images"; the loop makes three.
- 2700-2705, 2709: the pool-count comment and a doubled trailing comment no longer match the sets.
- 3247: reads backwards. 4337-4338: orphaned `.xlut` comment above the xfog header (its function is at 5545).
- 4598: "not a version 1 water file"; versions 1-3 are accepted.
- 6146-6148: two stacked headers above `Scene_TextureInfo`. 7296: doubled blank line.
- 9166: "the look values, 50"; the list at 9189 has 56. 9347-9350: the float-target comment sits above the wrong code.
- Function headers detached from their functions: 11026-11031 (three blocks above `vk_scene_view_skipped`), 11050-11057 (`R_SceneLightPoint`'s, above `R_SceneSunDir`), 11259-11263 and 11276-11279 (`vk_scene_capture`'s), 11832-11833 (`vk_scene_draw`'s, above `Scene_DrawFx`), 12230-12231 (`vk_scene_decals`'), 12464-12465 (`sceneimage`'s).
- `sceneFrame_t` member comments behind the shader: 339, 343, 353-354, 356-360, 374, 382.
- scene.frag:15-17 (`kMode` list lacks 10, 11), 116 (says 16-bit pairs; indices are 32-bit), 136-137 (`ShadowFace.texel.z` undocumented), 587-588, 641-643, 1000-1001 (comments broken or merged), 779 (stray blank line inside a statement), 2082-2084 (two comments for one line).
- scene.vert:164-165 (`bh` reads the index before `bb` names it); scene_pdecal.vert:13 (push `z`/`w` comment stale against vk_scene.cpp:1729-1730); scene_pdecal.frag:8-73 and scene_fx.vert:7-46 (stale copies of the Frame block's comments); scene_sort.comp:22 (`Lod.pad` is `resident` in the other shaders).
- 12538, 3988: `scenelshash` writes `lshash_gate.txt` and `lshash.txt` into the game folder; the command help (12835) names only the second.
- The literal `37.5` (units per metre) appears about 30 times in vk_scene.cpp and as a constant in four shaders; name it once (`SCENE_UNITS_PER_METRE`).

Status of this section (lane fix d5fc921be):
- Fixed: the user-visible cvar help strings with owner notes or dates (r_sceneCutoutCoverage, r_sceneLightShadowDepth,
  r_sceneSettleFaces, r_sceneDetailSlots, r_sceneWaterBreakup, r_sceneTerrainPush, r_sceneFxAccelDrag, r_sceneVrs) now
  describe the setting alone; the two comments naming `look/decals-20261106/CONTRACT.md` and
  `.codex-research/w3-billboards-20261118/CONTRACT.md` now name the writer in tools/levelport; the `SCENE_MAX_DLIGHTS`
  comment, the .xdcl header comment (versions 1 and 2), the decal pipeline's blend comment, the two stacked headers above
  `Scene_TextureInfo` and the scenelshash help (both files) are corrected. `docs/xenjk-cvars.md` is generated from the
  help text (`tools/cvar_list.py`) and was not regenerated here.
- Left for the comment sweep (owner decision): the comments citing owner dates, review IDs, sessions and agent names,
  in vk_scene.cpp and the shaders, and the other stale comments listed above.
- Licence headers on the scene shaders: not added. No shader under `codemp/rd-vulkan/shaders/glsl/` carries one, the
  stock ones included; whether xenjk's own shaders get the GPL-2.0 header is an owner question for the whole tree.

## Liftability

- **One file, one state struct.** A 330-line anonymous `static struct sc` (418-747) holds every sub-feature, with about 220 cvar pointers (749-968) interleaved with forward declarations and unrelated statics. Shape: `vk_scene_local.h` (format structs 189-237, `sceneBuffer_t`, `sceneFrame_t`, push structs, per-feature sub-structs `sc.decal`, `sc.occ`, `sc.fog`, `sc.ssr`, `sc.grade`, `sc.ls`, `sc.gd`, `sc.async`) and one unit per feature with Create/Free/Frame entry points: `vk_scene_pdecal.cpp` (1356-1764, already nearly self-contained), `vk_scene_lightshadow.cpp` (3079-4190), `vk_scene_files.cpp` (side-file loaders 4342-4985), `vk_scene_w3.cpp` (5000-5411, game-specific), `vk_scene_grade.cpp` (5545-5888), `vk_scene_stream.cpp` (7087-8784), `vk_scene_env.cpp` (9153-9315, no Vulkan).
  - Status: done in part (1cb556881, 6cc76e4de): `vk_scene_local.h` (the types, the constants, the state struct as `sceneState_t`, the shared declarations), `vk_scene_w3.cpp`, `vk_scene_cp77.cpp` (.xfog, .signals, .xrot, .xscr), `vk_scene_files.cpp` (.xdcl, .xwat, .xfx: both level ports write them; .xlut and .xenv too, so the grade went to post and the environment stayed) and `vk_scene_post.cpp` (haze, capture, meter, grade); vk_scene.cpp 12,846 -> 9,658 lines. Not done: the per-feature sub-structs of `sc` (a rewrite of every `sc.` use) and the light-shadow, stream and environment units (the next cuts, by the same method)
- **Public interface.** The hooks in stock code are single calls (good), but the 24 prototypes and the `SCENE_PASS_*` enum are split across `vk_local.h` 1627-1632 and 1688-1706 among unrelated ones. Shape: a `vk_scene.h` included by the callers.
  - Status: not done: the prototypes live in vk_local.h, shared by the whole renderer (another lane's file); a `vk_scene.h` is one move once that lane agrees
- **Light-shadow face rendering has two implementations** (4043-4150 and 3661-3810) that have diverged (RS-1; the rain view's LOD and minimum radius differ at 3694-3703 against 4071-4078). Shape: one `Scene_FaceCullPush( view, v, &push )` and one `Scene_FaceCull( cb, f, views, n, sets )` (reset, culls, finish) used by both.
  - Status: not done: unifying them is a rewrite of both paths (RS-1 fixed the missing pool reset and scatter; the rain view's LOD and minimum radius still differ, which needs a run to judge)
- **Rain borrows the stream's resources.** `sceneRain` (3404), `sc.rainFace` / `sc.rainMargin`, and `ss.lsFbs` / `ss.faceCull` / `Scene_StreamFaceSets` are used by `Scene_RainFrame` even when nothing streams (7881-8122). Shape: the per-layer framebuffers and face sets belong to the light-shadow code; rain and stream both call it. A "LOD became resident" callback replaces the stream's direct `sceneRain.dirty` write and face marking (7661-7672).
  - Status: not done: an ownership change in the frame's recording order (RS-3's area), not a move; needs a validation run
- **Frame split.** `Scene_SplitFrame` (6593-6637), `R_SceneFree` 6086-6090 and `vk_scene_take_wait` swap `vk.tess[f].command_buffer` and write `vk.cmd->acquire_waited`, `curr_index_buffer`, `last_pipeline`, `scissor_rect`. Shape: `vk_split_frame( signal, wait )` in `vk_frame.cpp` owns the second command buffer and the state reset.
  - Status: not done: `vk_split_frame` belongs in vk_frame.cpp (another lane's file)
- **Two-way coupling with the sun-shadow cache.** `R_SceneVkBeginFrame` (10862-10870) walks `backEndData->sunCascadeCmds` and calls `RB_FarCascadeReuse` / `vk_sun_shadow_kept`, while tr_backend.cpp:1572/1604/1622 call `vk_scene_shadow_key` / `vk_scene_view_skipped`. Shape: the back end passes the reuse decision in (`vk_scene_skip_views( mask )` before the frame begins).
  - Status: not done: the shape changes tr_backend.cpp (another lane's file)
- **`RB_DrawSurfs` carries the pass sequencing inline** (tr_backend.cpp:2155-2175: depth, occlusion-or-decals, opaque, blend, with `vk.cmd->last_pipeline` resets). Shape: `vk_scene_after_prepass()` (returns whether it split), one call per phase. `backEnd.hasGlowSurfaces` (vk_scene.cpp:11976) is better returned from `vk_scene_draw` than set as a side effect. The meter gets its own hook (RS-8).
  - Status: not done: tr_backend.cpp (another lane's file); RS-8's meter hook is deferred with it
- **`R_SceneLoad` is one 800-line function** (9327-10135) that mutates the file buffer through const casts (9398, 9566, 9602, 9853, 9904). Shape: `Scene_FilterLights`, `Scene_Validate`, `Scene_LoadInteriors`, `Scene_BuildMaterials`, `Scene_WaterBand`, `Scene_BuildCommands`, `Scene_BuildBounds`, one failure exit that calls `R_SceneFree` (RS-25). `Scene_ReadTiled` (8797-9151) splits into `Scene_ReadManifest` and `Scene_AppendOcean`.
  - Status: not done: splitting it passes its many locals between new functions (a rewrite, not a move); RS-25's early failures share one exit now (`failLoad`)
- **Side files.** .xfog, .xwat, .xrot, .xscr share the header `{ ident, version, count, pad }` and repeat the name-building and length check. Shape: `Scene_ReadSideFile( scnName, ext, ident, minVer, maxVer, recordSize, &buf, &count )` with the 64-bit check once (closes RS-16). The `params` vector passed through detail, w3 blend, water, screens and fx (5417-5541) is an implicit ordering contract with offsets stored as floats; a small append helper returning the offset makes it explicit (and is where RS-12's guard goes).
  - Status: not done: one reader for the four headers rewrites six loaders; RS-16 closed with 64-bit checks in each. The params append helper is not done either (RS-12's guard sits in the terrain, the one producer that can reach it)
- **Host/shader constants and blocks are duplicated by hand.** The `Frame` block exists in six shaders plus `sceneFrame_t` and the copies' comments have drifted; `Light`, `ShadowFace`, `Instance`, `Mesh`, `Lod`, `CLUSTER_*`, `DIST_STRIDE`, `FOG_*`, `SCENE_CULL_SUB`, `VIEWS`, `LISTS`, `UNITS_PER_METRE`, and scene_cluster.comp's `pad[90]` offset into the Occ block are redeclared per shader. Shape: `scene_shared.glsl` / `scene_frame.glsl` included by all (the stock shaders already use `GL_GOOGLE_include_directive`; `tools/scene_spirv.py` needs the include path), usable from C++ for the constants, with a `static_assert` on `sizeof( sceneFrame_t )`.
  - Status: not done: a shared include across the 25 shaders and an include path in tools/scene_spirv.py, then a byte check of every variant; a pass of its own
- **scene.frag is twelve shaders behind one `kMode`** (sky 6, particles 10/11, DECAL, water 9, glow 5, depth 4/7, lit), with magic mode integers in `Scene_GraphicsPipeline` (1176; only 9 is named). Shape: `scene_sky.frag`, `scene_fx.frag`, `scene_decal.frag`, `scene_lit.frag` over shared includes (closes RS-38), and a mode enum mirrored in the shader. Game-specific materials (Witcher 3 detail kinds -2..-10, CP2077 windows, screens, holograms, signals) move to one include per source game with named kinds shared with scene.vert and the host loaders.
  - Status: not done: scene.frag keeps its game-specific modes and kinds (Witcher 3 detail kinds, CP2077 windows, screens, holograms, signals); splitting it is the shader-side pass above
- **GPU-driven draws overload `pc.tint` and `pc.emissive`** (VRS distances, the `r_sceneTerrainFlat` switch, the particles' accel-drag and mask mode). Shape: their own named push blocks, or `Frame` members.
  - Status: not done: a push-layout change in host and shaders
- **`Scene_DecalPipeline` tells its callers apart by `vs == sc.pdecalVs`** (1831; the member exists only while the pipeline is made). Shape: an explicit `projected` parameter.
  - Status: not done (small; the function is now shared through the header for vk_scene_files.cpp, so an explicit parameter is the natural next edit)
- **Compute-pass boilerplate** (set layout, pipeline layout, per-slot sets, pipeline) is repeated for grade, meter, compact, clusters, viewcull, lightfade, waves, distant, want and rotate. Shape: `Scene_ComputePass( spv, types, n, pushBytes, &layout, &pipeLayout, &pipe )`, which also makes the partial-failure paths uniform (RS-14). `Scene_CreateViews` (2691-3075) writes about 30 bindings inline though `Scene_WriteBinding` exists.
  - Status: not done: a helper that rewrites ten creation paths
- **Generic helpers** (`Scene_CreateBuffer`, `Scene_StaticBuffer`, `Scene_Shader`, `Scene_SetLayout`, `Scene_WriteSet`, `Scene_WriteBinding`, `Scene_HalfImage`, `Scene_PrintStats`) depend only on `vk.*` and could be a small `vk_util` unit.
  - Status: done in part (1cb556881): they are shared through vk_scene_local.h; not moved to a unit of their own
- **Engine-side dependencies for a lift.** Streaming needs the `ri.FS_Range*` imports and `fs_prefetchApi >= 2` (9366; the fallback without them is clean), whose int offset/length signature caps a shard at 2 GB; the look table at 9189 is a single 1,400-character line hand-synced with the cvar list (a registration flag would replace it).
  - Status: noted; nothing to do in this lane

## Still open from earlier reviews

None in this lane. `docs/review/2026-11-09-character-rendering.md` F3 (drop `R_StreamHold` in vk_scene.cpp) is done: no
reference remains. SH-25 (the scene's pass splits) is recorded fixed there and the keep check is present at
vk_scene.cpp:12130. The fork review (`docs/review/FINDINGS.md`, 2026-10-07/08) has no finding on the render scene.
