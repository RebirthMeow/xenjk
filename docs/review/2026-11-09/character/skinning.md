# Character rendering review: Ghoul2 skinning and character mesh pipeline

Lane: skinning / mesh. Target: commit `6fe7bb2f` (worktree `.codex-research/wt-review-6fe7bb2f`), read-only.
All `file:line` references are at that commit. xenjk's own changes were found with
`git diff e4fe7581 6fe7bb2f`. Review effort: medium, static reading plus one offline check (the bone remap
table). Nothing was built or run.

"Upstream" below means the code came with the JKSunny rd-vulkan import (or from stock OpenJK); it is listed
when it sits on a character hot path or when xenjk's hero models make it worse.

## Summary

| Category | High | Med | Low | Total |
|---|---|---|---|---|
| bug | 0 | 5 | 9 | 14 |
| perf | 0 | 2 | 3 | 5 |
| quality | 0 | 0 | 3 | 3 |
| suggestion | 0 | 1 | 1 | 2 |
| **Total** | 0 | 8 | 16 | 24 |

No finding is a crash or data-loss risk in normal play; the medium bugs are wrong poses or wrong meshes in
specific situations.

Top 5:

1. **SK-1** The back end reads the Ghoul2 clock when the render commands run (end of frame), not the time the
   scene was built with. With a menu open over a running game, world characters are posed at the UI's clock.
2. **SK-2** A cached GLM of the old 72-bone humanoid format has its bone references remapped again on every
   reload (map change, vid_restart), which scrambles its skinning.
3. **SK-3** Render smoothing (`r_ghoul2animsmooth`, default 0.3, and the ragdoll smoothing factors) never
   blends for most bones, and blends only the bones cloth and hair read.
4. **SK-4** `HackadelicOnClient` stays true after the back end (known, open in `docs/g2-anim-smoothing.md`);
   it is the second half of SK-3 and changes listen-server bone evaluation.
5. **SK-13** The Ghoul2 vertex is 56 bytes; 28 to 32 is reachable. This is the largest VRAM lever in this
   lane for the 6 GB laptop target.

## Findings

### SK-1: back end poses at the clock of command execution, not of the scene
- Category: bug. Severity: med. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:3995` (`RB_TransformBones`), against `:3669` (`R_AddGhoulSurfaces`);
  `codemp/rd-vulkan/G2_API.cpp:176-189`; `codemp/rd-vulkan/tr_cmds.cpp:490`; `codemp/ui/ui_main.c:10092-10093`.
- What is wrong: the front end takes its bone time at scene time (`G2API_GetTime`, with `G2_TIME_EXACT` for a
  stage scene). The back end calls `G2API_GetTime(refdef->time)` again when the render commands execute, in
  `RE_EndFrame`. `G2API_GetTime` ignores its argument unless the exact flag is set and returns the global
  `G2TimeBases`, so the back end uses whatever was written last in the frame. The back end also never applies
  the stage's exact time.
- Failure scenario: a menu is open over a running game. cgame sets the clock to `cl.serverTime` and submits the
  world scene; `UI_Refresh` then sets both clocks to `realtime`; the back end transforms and uploads every
  world character at `realtime`. Animations whose start times are in server time play out of phase or hold
  their last frame, and cloth and hair (simulated in the front end at server time) no longer match the drawn
  body. The player-screen stage is only right because the UI happens to set the shared clock to the same
  `realTime` it passes as `refdef.time`.
- Suggested fix: store the resolved bone time in the scene's `trRefdef_t` when the scene is submitted and use
  that in `RB_TransformBones` (and for the gore time at `:2862`). This also removes the hidden dependency of
  the stage on the UI's clock writes.

### SK-2: old-format humanoid GLMs are remapped again on every cached reload
- Category: bug. Severity: med. Confidence: confirmed by reading (table checked offline: 59 of its entries
  change again when applied twice).
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:4826-4836` (the "hotfix" that no longer returns for a cached GLM when
  Ghoul2 VBOs are on), `:4966-4981` (`OldToNewRemapTable` applied in place), table at `:4331`.
- What is wrong: with `vk.vboGhoul2Active`, a GLM already in the model cache falls through the whole load
  loop so that `R_BuildMDXM` runs. For a model with `numBones == 72` and a `_humanoid` GLA, that loop rewrites
  every surface's bone references through the remap table, in the cached buffer, each time.
- Failure scenario: a JK2-era player model (72-bone humanoid) draws correctly on the first map. After a map
  change or `vid_restart` the cached GLM is processed again, its bone references are remapped a second time,
  and the mesh is skinned to the wrong bones. Stock JKA models (53 bones) and xenjk converts are not affected.
  `R_LoadMDXM_Server` (`tr_model.cpp:451`) never applies the remap, so server and client disagree about such
  a model too.
- Suggested fix: when `bAlreadyFound`, skip straight to `R_BuildMDXM` (the shader registration loop still has
  to run; the remap and the name edits must not). A one-bit "processed" marker in the cache entry is cleaner.

### SK-3: render smoothing is a no-op for most bones and inconsistent for the rest
- Category: bug. Severity: med. Confidence: confirmed by reading for the logic; visible effect plausible.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:2311-2396` (touch bookkeeping), `:287-357` (`SmoothLow`), `:563-580`
  (`EvalRender`), `:3755` and `:4058` (the two client transforms per frame), `:611-614` (`R_ClothEvalBone`).
- What is wrong: `SmoothLow` blends only when a bone's smooth touch equals `mLastTouch`, the previous client
  generation. Each frame has two client generations for a model: the front end (`R_AddGhoulSurfaces`) and the
  back end (`RB_TransformBones`). Bones are fully evaluated with `EvalRender` only in the back end, whose
  `mLastTouch` is the front-end generation of the same frame, so a bone last smoothed in the previous frame's
  back end never matches and is copied unsmoothed. The exception is a bone the front end reads through
  `EvalRender`, which is what cloth colliders and hair hosts do: those bones are smoothed. cgame bolt queries
  count as a third client generation because of SK-4.
- Failure scenario: `r_ghoul2animsmooth 0.3` and the ragdoll factors (0.2 to 0.9) have no effect on the drawn
  skeleton, so ragdolls jitter as if smoothing were off. On a model with cloth or hair, the bones those systems
  read can lag the rest of the skeleton by a fraction of their per-frame motion, and the simulation sees a
  different pose from the one drawn; the size depends on frame rate and on whether cgame queried a bolt that
  frame.
- Suggested fix: decide which behaviour is wanted. Either remove smoothing on this path (never set
  `mSmoothingActive`, drop `mSmoothBones`), or make it work: the back end reuses the front end's generation
  when frame, scene, time and root match (see SK-11) and SK-4 is fixed. Smoothing as written is per frame, so
  restoring it also brings back a frame-rate dependent lag.

### SK-4: `HackadelicOnClient` is left true after the back end
- Category: bug. Severity: med. Confidence: confirmed by reading; already recorded as open in
  `docs/g2-anim-smoothing.md:57` and `:119`.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:3997` (set, never cleared; early `return` at `:4071` too), consumers
  at `:2029`, `:2082`, `:2122-2161`, `:2184`, `:2312`, `:2392`.
- What is wrong: every Ghoul2 evaluation between the end of one frame's back end and the next
  `R_AddGhoulSurfaces` runs as a render traversal. That covers server instances on a listen server and cgame's
  bolt queries.
- Failure scenario: on a listen server, server-side bone evaluation takes the client branches: `newMatrix`
  instead of `matrix` for post-multiplied angle overrides (`:2184-2191`), the ragdoll client branches, the
  smoothing setup, and `mCurrentTouchRender` (which `WasRendered` reports to the ragdoll code). Results differ
  from a dedicated server, and they depend on whether a Ghoul2 model was drawn the frame before.
- Suggested fix: clear the flag at the end of `RB_TransformBones`, on every exit. It changes listen-server
  behaviour against the installed build, so it wants one before/after check of saber and ragdoll behaviour.

### SK-5: a failed bone-palette reservation leaves a stale offset and skips the remaining models
- Category: bug. Severity: med. Confidence: confirmed by reading; how often the buffer fills is not known.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:4068-4071`; `codemp/rd-vulkan/vk_shade_geometry.cpp:1428-1437`.
- What is wrong: `vk_reserve_uniform_range` writes `*offset` before it checks for room. On failure
  `bc->uboOffset` holds a new offset where nothing was written, the cache fields (`uboFrameCount` and so on)
  are not updated, and `RB_TransformBones` returns without handling the entity's remaining models (bolted
  weapons and sabers).
- Failure scenario: the per-frame geometry buffer fills late in a heavy frame. The model that failed, and
  every later model of that entity, draws that frame with whatever bytes sit at the offset: an exploded mesh
  for one frame. The reservation needs the full 12,288-byte descriptor range to fit, so it fails 12 KB before
  the buffer is actually full.
- Suggested fix: reserve into a local, set `bc->uboOffset = ~0U` on failure and `continue`; make the draw skip
  a surface whose bone offset is `~0U` if it does not already.

### SK-6: gore mark vertices are skinned with weights that do not sum to one
- Category: bug. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/G2_misc.cpp:1042-1051`; shader at
  `codemp/rd-vulkan/shaders/glsl/gen_vert.tmpl:157-161` (no renormalisation). The mesh path does it right at
  `codemp/rd-vulkan/vk_vbo.cpp:1100-1119`.
- What is wrong: each gore weight is truncated to a byte independently. The mesh path gives the last influence
  the remainder so the bytes sum to 255; the gore path does not.
- Failure scenario: a mark on a vertex with three or four influences can lose up to 3/255 of its weight. The
  skinned position is scaled towards the model origin by that fraction: about 0.5 units at 45 units from the
  origin, enough for a mark to sink into or float off the skin as the body moves. `cg_ghoul2Marks` defaults
  to 16, so marks are on. Gore tangents are also left zero (allocation is zero-filled).
- Suggested fix: use the same remainder rule as `R_PackMDXMLod`, ideally one shared helper for both.

### SK-7: the gore vertex ring overwrites marks that are still alive
- Category: bug. Severity: low. Confidence: confirmed by reading (upstream design).
- Where: `codemp/rd-vulkan/vk_vbo.cpp:1405-1410`, `:1484-1520`.
- What is wrong: gore vertices and indices live in two 2 MB rings that wrap independently. Nothing invalidates
  the marks whose range is overwritten; they keep their `firstVert` and `firstIndex`.
- Failure scenario: 2 MB holds about 37,000 gore vertices. A mark on a converted hero surface (up to 9,000
  vertices per surface) can take thousands, so a few dozen marks wrap the ring, and older marks that have not
  expired then draw another mark's vertices with their own indices. A single mark larger than the ring would
  write past the mapped buffer; nothing checks for it.
- Suggested fix: on wrap, drop the gore records whose range is being reused (or re-upload live marks); clamp
  or reject a mark larger than the ring. The size macros are unparenthesised expressions and should be
  constants.

### SK-8: nothing rejects a skeleton with more than 256 bones
- Category: bug. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/vk_vbo.cpp:1109`, `:1119` (bone reference stored in a byte);
  `codemp/rd-vulkan/G2_misc.cpp:1050`; `codemp/rd-vulkan/shaders/glsl/global.h:148`; GLA load at
  `codemp/rd-vulkan/tr_ghoul2.cpp:5281`.
- What is wrong: the palette is 256 matrices and vertex bone references are bytes, but the loaders accept any
  bone count. `RB_TransformBones` writes all `numBones` matrices; the vertex data silently wraps modulo 256.
  The hair path does check its own total (`HAIR_MAX_PALETTE`); the base skeleton has no check.
- Failure scenario: a converted model on its own GLA with more than 256 bones loads without a message and
  skins the high bones' vertices to bones 0 upward.
- Suggested fix: warn at GLA or GLM load when `numBones > 256` and refuse the VBO path for that model.

### SK-9: `r_g2Joints` also applies to server instances on a listen server
- Category: bug. Severity: low. Confidence: confirmed by reading that nothing gates it per instance; plausible
  that listen-server instances share the model that carries the joints.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:386-394` (`Joints()`), `:2194-2211`, `:3544-3553`;
  `docs/g2-joints.md:32-35`.
- What is wrong: the doc says the server's own traces keep the GLA's joints. `Joints()` looks only at the cvar
  and the model, so any instance evaluated through this renderer gets the correction. `r_g2Blend` has explicit
  handling to keep never-drawn instances stock (`mEverDrawn`); joints have none.
- Failure scenario: on a listen server, server-side bolt queries and Ghoul2 traces for a model with a
  `.joints` file use the corrected skeleton, so hit positions differ from a dedicated server and change when
  the client toggles `r_g2Joints`. For a solo listen-server setup this arguably matches the drawn mesh better;
  the issue is that it is undocumented and cvar-dependent.
- Suggested fix: decide the rule and write it down. If server instances should stay stock, gate `Joints()` the
  way `mDrawBlend` is gated.

### SK-10: the joint box is computed by reading back from the mapped uniform buffer
- Category: perf. Severity: med. Confidence: plausible (depends on the memory type the driver returns).
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:4090-4107` (reads `dst`), hair equivalent called at `:4105`; buffer
  memory flags at `codemp/rd-vulkan/vk_shade_geometry.cpp:889` and `:950`
  (`HOST_VISIBLE | HOST_COHERENT`, no `HOST_CACHED`).
- What is wrong: the `r_charOccRect` box loop reads every palette matrix back out of `dst`, the mapped
  per-frame geometry buffer. The first matching memory type on NVIDIA is normally uncached, write-combined
  memory, where reads are far slower than ordinary memory.
- Cost estimate: 48 bytes per bone read back, so 3.5 KB for a 72-bone skeleton and up to 12 KB for a full
  palette, per transformed model per frame. On write-combined memory that is likely tens of microseconds per
  hero; with a dozen characters, a few tenths of a millisecond of back-end time. Needs one measurement.
- Suggested fix: compute the box from the CPU-side matrix `b` in the copy loop at `:4075-4078`, and have the
  hair writer return its box from its own CPU palette.

### SK-11: the front end and the back end each start a transform generation for the same pose
- Category: perf. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:3750-3758`, `:4042-4058`, `:2389`.
- What is wrong: `G2_TransformGhoulBones` bumps `mCurrentTouch`, so everything the front end evaluated for a
  model (bolt parents, cloth collider bones, hair hosts, and each one's whole parent chain) is thrown away and
  evaluated again in the back end. Only shadow views reuse the front-end generation.
- Cost estimate: the docs put a full hero skeleton at about 0.03 ms; the repeated part is a fraction of that
  per model per frame, so this is small on its own. Its value is that fixing it also fixes SK-3.
- Suggested fix: in `RB_TransformBones`, when the cache's front-end frame, scene, time and root match, skip
  `G2_TransformGhoulBones` and only evaluate the remaining bones.

### SK-12: per-surface, per-view work in `RenderSurfaces` that is constant per model
- Category: perf. Severity: med. Confidence: confirmed by reading; cost estimated.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:2799-2801`, `:2822-2837` (skin lookup by `strcmp` over every skin
  surface), `:2844` (`R_CubemapForPoint` per surface; loop at `codemp/rd-vulkan/tr_light.cpp:505-529`).
- What is wrong: for every drawn surface, in every view, the code walks the LOD to find the surface, compares
  its name against the skin's surfaces one by one, and searches every cubemap for the nearest to the entity
  origin. All three results are the same for the whole model (per skin and LOD).
- Cost estimate: surfaces x skin entries string compares per view. A 100-surface hero with a 100-line skin is
  about 5,000 compares on average per view; with the main view, three or four sun cascades and the character
  shadow views, 25,000 to 40,000 per hero per frame, plus surfaces x cubemaps distance tests. Roughly 0.1 to
  0.3 ms per hero per frame; stock models (about 30 surfaces, one view) never showed it.
- Suggested fix: resolve the shader per surface once per (model, skin) into a small table kept with the skin
  or the Ghoul2 instance; compute the cubemap index once per entity per scene.

### SK-13: the Ghoul2 vertex is 56 bytes
- Category: suggestion (perf, VRAM). Severity: med. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/vk_vbo.cpp:875-913`, `:995-1030`.
- What is wrong: position, normal and tangent are each four floats (the position and normal `w` are unused
  padding), plus two float texture coordinates and eight bytes of bone data.
- Cost estimate: 5.6 MB per 100,000 vertices per LOD. Position as three floats, normal and tangent as
  10:10:10:2 or 16-bit normalised, and half-float texture coordinates give 28 to 32 bytes, so 2.8 to 3.2 MB.
  The saving repeats in every LOD, in the hair substitute buffers and in the cloth buffers that copy this
  layout, which is what matters for the 6 GB laptop.
- Suggested fix: a packed layout behind the existing offsets table. It touches the vertex input descriptions
  and the hair and cloth copies of the layout, so it crosses into the materials, hair and cloth lanes.

### SK-14: a cvar is looked up by name in every bone transform
- Category: perf. Severity: low. Confidence: confirmed by reading (upstream).
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:2312`.
- What is wrong: `ri.Cvar_VariableIntegerValue( "dedicated" )` is a string hash lookup, run for every model on
  every transform: twice a frame per drawn model, plus once per bolt-query generation.
- Suggested fix: cache the `cvar_t *` once.

### SK-15: the last blend frame can index one frame past the animation
- Category: bug. Severity: low. Confidence: confirmed by reading (upstream; xenjk's blend path inherits it).
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:1766-1770` (clamp allows `blendFrame` in `[numFrames, numFrames+1)`),
  used at `:1865` and in `G2_BlendBoneDraw` at `:1657`.
- What is wrong: the range check resets `blendFrame` only at `numFrames + 1`, so `(int)blendFrame` can equal
  `numFrames`, one past the last frame, and the frame index table is read out of range.
- Failure scenario: a blend that starts from the very end of the GLA reads a garbage pool index for the
  duration of the blend: one wrong pose source, or an out-of-bounds read of the bone pool.
- Suggested fix: clamp to `numFrames - 1` in both paths.

### SK-16: tangent cache fast path trusts a GLM pointer that can be reused
- Category: bug. Severity: low. Confidence: plausible.
- Where: `codemp/rd-vulkan/vk_tangent_cache.cpp:327-330`; cleared only in `RE_Shutdown`
  (`codemp/rd-vulkan/tr_init.cpp:1538`), not in `C_LevelLoadBegin` (`:1654-1661`), which can free GLMs.
- What is wrong: the lookup without hashing matches on pointer and file size. A different GLM allocated at a
  freed GLM's address with the same size and the same per-LOD vertex counts would be given the old tangents.
- Failure scenario: needs a model freed and another of identical size loaded at the same address while the
  renderer keeps running (`devmapmdl`, or a recoloured variant after a pure-server dump). Unlikely; the result
  would be wrong normal-map lighting with no message.
- Suggested fix: call `R_TangentCacheClear` where the model cache frees models, or drop the pointer fast path
  (the hash is about 20 ms and already runs on the worker).

### SK-17: a dropped mesh job can leave a model with no mesh and no way to get one
- Category: bug. Severity: low. Confidence: plausible.
- Where: `codemp/rd-vulkan/vk_mesh_jobs.cpp:191`, `:313-330`; `codemp/rd-vulkan/tr_ghoul2.cpp:2769-2775`,
  `:4154-4155`; `codemp/rd-vulkan/tr_init.cpp:1654`.
- What is wrong: queuing a job sets `vboModels` to NULL; `R_MeshJobsShutdown` deletes pending jobs on the
  assumption that their models go away too. If a `model_t` outlives its job (the `C_LevelLoadBegin` call does
  not by itself reset the model table), later draws find no job, set `vboMesh` to NULL and the surface is
  skipped: an invisible model until the next renderer restart.
- Suggested fix: in `vk_set_ghoul2_vbo_mesh`, when there is neither a mesh nor a job, build it there with the
  one-step path. It is a few lines and makes the assumption unnecessary.

### SK-18: the xenjk animation-set choice is cached on a Ghoul2 pointer
- Category: bug. Severity: low. Confidence: plausible.
- Where: `codemp/cgame/cg_players.c:1094-1113` (`CG_XenAnimCheck`), resets at `:1455`, `:2391`, `:9152`,
  `:11715`.
- What is wrong: the cached choice is kept while `cent->ghoul2` and `localAnimIndex` are unchanged. A freed
  instance replaced by a new one at the same address with the same stock set but a different GLA keeps the old
  answer. The known paths reset it by hand; any other path that swaps the instance does not.
- Failure scenario: an entity slot reused by a humanoid whose GLA lacks the xenjk frames plays frame numbers
  past the end of its GLA, which the renderer clamps to frame 0: a bind-pose flash or a stuck pose.
- Suggested fix: key the cache on the GLA name (or model handle) as well, instead of relying on resets.

### SK-19: each view picks its own LOD
- Category: bug. Severity: low. Confidence: plausible.
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:3767-3773`, `:1036-1140` (`ProjectRadius` with `tr.viewParms`).
- What is wrong: the LOD is computed from the current view's projection, including in sun-cascade and
  character-shadow views. A shadow view can therefore cast from a different LOD than the main view draws,
  while cloth and hair run only for the main view.
- Failure scenario: self-shadow acne or a shadow outline that does not match the body at the distance where
  the main view switches LOD; the shadow of a skirt or hair surface taken from another LOD than its
  simulated substitute.
- Suggested fix: compute the LOD once per entity per scene from the main view and reuse it in shadow views,
  as the bone transform is already reused.

### SK-20: warm-up queue blocks on its oldest job, and has no time limit of its own
- Category: perf. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/vk_model_warm.cpp:313-337`, `:235-240`, `:282-299`.
- What is wrong: `R_WarmStep` works only on the oldest unfinished job. While that job waits (read-ahead, mesh
  worker, pipelines) no other job advances, and `R_MeshJobStepAny` is not called at all, so a server-registered
  model's upload also waits. A job whose read-ahead never reports done waits forever; only cgame's
  `cg_modelWarmTimeout` rescues the other-player case.
- Suggested fix: step the first job that can make progress; give `WARM_READ` a limit after which it proceeds.

### SK-21: duplicated and dead code in the mesh path
- Category: quality. Severity: low. Confidence: confirmed by reading.
- Where and what:
  - Tangent generation exists three times: `vk_tangent_cache.cpp:108-136`, `vk_vbo.cpp:1055-1076`,
    `vk_tangent_cache.cpp:143-151`. The LOD walkers exist twice: `vk_tangent_cache.cpp:89-105` and
    `vk_vbo.cpp:933-956`. Weight unpacking exists twice: `tr_ghoul2.cpp:3960`, `vk_vbo.cpp:512`. The byte
    weight packing exists twice with different rules (SK-6).
  - `CBoneCache::boneMatrices[72]` (`tr_ghoul2.cpp:418`, `:441`) is never read: 3.4 KB per bone cache.
  - `RB_SurfaceGhoul`'s merge-both-ways branch (`tr_ghoul2.cpp:4286-4300`) cannot run, because only the last
    primitive is examined; if it did run it would move an entry without its last, min and max index.
  - `mikkt_mdv_GetNumVertices` casts its user data to the MDXM struct (`vk_mikktspace.cpp:286-290`); it works
    because both structs start with the same field.
  - `R_LoadMDXM_Server` allocates `vboModels` for a model it never builds (`tr_model.cpp:413-416`), leaving a
    non-NULL array of empty LODs.
- Suggested fix: one tangent routine and one weight packer shared by mesh, gore, hair and cloth; delete the
  dead member and branch.

### SK-22: `G2_TransformBone` keeps its working state in function statics
- Category: quality. Severity: low. Confidence: confirmed by reading (upstream).
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:1672-1680`.
- What is wrong: `tbone[6]`, `skel`, `offsets`, `j` and `boneListIndex` are static. It is correct today only
  because the parent and (new) anchor are evaluated before the call and all Ghoul2 work is on the main thread.
  xenjk now has a mesh worker and background stepping in this renderer; any future bone work off the main
  thread, or an evaluation started from inside the function, would corrupt them silently.
- Suggested fix: make them locals. There is no measurable cost.

### SK-23: mesh-job ownership is implicit
- Category: quality. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/vk_mesh_jobs.cpp:169-203`, `:206-246`, `:278-311`.
- What is wrong: the locking is correct as far as I could trace it (every phase change is under the lock; the
  worker touches only a job it marked busy; jobs are erased under the lock in a phase the worker never
  takes). It relies on unwritten rules, though: only the main thread calls the step, finish and queue
  functions, and `mj.running`, `mj.defer` and `mj.levelLoad` are unguarded main-thread state. The worker
  catches only `std::bad_alloc`.
- Suggested fix: assert the main thread in the entry points, and comment which fields are lock-protected.

### SK-24: ship prepared tangents with every pack
- Category: suggestion. Severity: low. Confidence: confirmed by reading.
- Where: `codemp/rd-vulkan/vk_tangent_cache.cpp:212-216`, `:304-318`.
- What is wrong: nothing is broken. A first-ever load computes MikkTSpace (0.4 to 0.7 s, on the worker when
  warmed, on the main thread at a map load) and then writes a file of about 4 MB from the main thread.
- Suggested fix: ship the `.xtan` files in the packs (`cook_tangents.py` already produces them) so no client
  computes or writes them; move the write off the main thread if it stays.

## Count check

- bug (14): SK-1, 2, 3, 4, 5 (med); SK-6, 7, 8, 9, 15, 16, 17, 18, 19 (low).
- perf (5): SK-10, 12 (med); SK-11, 14, 20 (low).
- quality (3): SK-21, 22, 23 (low).
- suggestion (2): SK-13 (med), SK-24 (low).
- Total 24: 0 high, 8 med, 16 low.

## Checked and found sound

- Mesh worker locking and hand-over (`vk_mesh_jobs.cpp`): no race found; the worker reads only the GLM and its
  own job, and the load loop finishes all writes to the vertex, triangle and bone-reference data before the
  job is queued.
- `.xtan` reader bounds checks and key (`vk_tangent_cache.cpp:157-188`, `:233-255`): length, counts and key
  are validated before use.
- `r_g2Blend` blend order and weights against the stock lerp (`tr_ghoul2.cpp:1643-1666` against `:1856-1931`):
  same sources, same direction, same weights.
- `.joints` parsing: cycles through parents and anchors are detected and fall back
  (`tr_g2joints.cpp:103-134`, `:230-243`); bone count is capped at 256 on both sides.
- Gore scratch arrays are now sized per surface (`G2_misc.cpp:788-851`, `:1260-1273`).
- Batch break between plain, cloth, hair and gore buffers in `RB_SurfaceGhoul` (`tr_ghoul2.cpp:4191-4208`).

## Needs a high-effort follow-up

1. **SK-3 and SK-4 together.** The reading says smoothing is dead; confirming what the owner actually sees
   (ragdoll jitter, hair or cloth offset) and choosing between removing and repairing it needs an in-game
   comparison, and the SK-4 fix changes listen-server behaviour.
2. **SK-1 in practice.** Confirm with a menu over a live game that world characters jump pose, then check
   every other reader of the shared clock in the back end.
3. **The 13 s main-thread stall noted at `vk_mesh_jobs.cpp:288`.** I found no deadlock. Candidates I could not
   settle by reading: MikkTSpace's cost on a surface with very many coincident vertices, and the worker being
   starved when the process runs at low priority.
4. **`r_g2Joints` with ragdoll and IK** (`tr_ghoul2.cpp:1940-2190`, `G2_bones.cpp`). The correction assumes
   every path builds the bone from its corrected parent. The ragdoll path takes its position from the parent,
   which fits, but I did not trace the IK and ragdoll solver feedback (`G2_GetBoneMatrixLow`, base-pose
   queries stay uncorrected by design).
5. **Server-loaded models reaching the client.** `R_LoadMDXM_Server` leaves a non-NULL `vboModels` with empty
   LODs. If such a `model_t` can be returned to a client registration through the shared name cache, the draw
   would index a NULL mesh array. I did not trace how the model table and the cache's handle map are reset
   between server spawn and client start.
6. **Surface-only server skins** (`tr_skin.cpp:386-399`): the `%`-keyed skin has the default shader for every
   visible surface. I did not prove that such a handle can never reach a drawn instance, and each one uses
   one of the 1,024 skin slots.
7. **SK-10 and SK-12 costs.** Both are estimates; one `r_cpuTimes` capture with several heroes would rank
   them.

## Not covered

- `G2_bones.cpp` (ragdoll, IK), `G2_bolts.cpp`, `G2_surfaces.cpp`, `codemp/ghoul2/*` and `mdx_format.h`:
  unchanged from upstream; read only where the findings above led.
- `G2_API.cpp` beyond xenjk's nine changed lines; the collision and trace paths in `G2_misc.cpp` beyond the
  gore changes.
- The MD3 parts of `vk_vbo.cpp` and `tr_model.cpp`.
- cgame beyond the diff of `cg_players.c` (the bike bones, `cg_view.c`, the UI picker).
- MikkTSpace itself (`utils/mikktspace`), including its handling of degenerate triangles.
- Run-time behaviour of any kind: nothing was built, launched or timed.

## Boundary notes for other lanes

- Hair and cloth: both read bones through `R_ClothEvalBone` (`EvalRender`) in the front end, which is what
  makes their bones the only smoothed ones (SK-3) and puts them one clock away from the drawn body when a
  menu is open (SK-1).
- Hair and cloth: both copy the 56-byte vertex layout and call `R_MdxmTangents`; a packed layout (SK-13) or a
  shared tangent routine (SK-21) has to change them together.
- Materials: the vertex shader does not renormalise skin weights (`gen_vert.tmpl:157-161`), and unused
  vertices carry zero tangents by design (`vk_tangent_cache.cpp` header); the gore path leaves all tangents
  zero.
- Textures/loading: `R_WarmStep`'s head-of-line blocking (SK-20) also delays shader and texture steps of
  later warm-ups.
