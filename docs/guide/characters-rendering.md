# How xenjk draws and animates characters

This chapter covers what xenjk adds on top of stock Ghoul2 to draw a player model. Bones blend in rotation space,
and a converted model can bend about its own joints. Hair, body springs and garment cloth are simulated at run time.
Characters get their own shadows, self-shadows, contact shadows and AO, plus a fill and rim light taken from the
light grid. Character textures stream into one VRAM budget, and a model change loads over several frames
instead of freezing the game. The current state is the
[2026-11-09 character rendering review](../review/2026-11-09-character-rendering.md) with all of its fixes. Every
review cvar defaults to the fix, and all of it is on `master`.

**Status.** The installed game runs a fresh build of `master` (Install 94), so the code defaults below are what
it plays with. On by default: rotation-space blending, own joints, hair, springs, GPU cloth, grid character
shadows, fill, rim and ambient floor, per-sample hair coverage, texture streaming with the VRAM budget, model
warm-up and the `_humanoid_xen` animation set. Built but off by default: linear-light shading, character AO,
contact shadows and self-shadow. The installed launcher `Play_Tifa.bat` turns those on, and the player screen's
stage always uses them. The portrait rig and the skin-softening switches are test tools. Most of this
needs `rd-vulkan`. Only rotation-space blending and CPU cloth also build in rd-vanilla.

## What it does

### Where the pieces sit in a frame

```
cgame CG_Player ─► RE_AddRefEntityToScene ─► RE_RenderScene (tr_scene.cpp)
                                              ├─ R_SetupCharShadows / R_SetupCharSelfShadows   (tr_charshadow.cpp)
                                              ├─ R_RenderView ─► R_AddGhoulSurfaces (tr_ghoul2.cpp)
                                              │     G2_TransformGhoulBones: r_g2Blend slerp, r_g2Joints shift
                                              │     R_ClothAddGhoul / R_HairAddGhoul: step sims (first view of the frame)
                                              │     R_StreamWantSurface: on-screen texel density per image
                                              └─ R_RenderCharShadows: depth tiles in the sun shadow atlas
back end  RB_DrawSurfs (tr_backend.cpp)
            depth prepass ─► RB_CharOcclusion (AO + contact masks, vk_ssao.cpp) ─► lit pass
            RB_TransformBones: skeleton + hair palette (256 matrices) ─► RB_SurfaceGhoul:
            cloth / hair substitute vertex streams, GPU skinned like the body
vk_begin_frame: R_ClothVkBeginFrame (cloth compute), R_StreamVkBeginFrame (texture slices)
RE_BeginFrame: R_WarmStep (model warm-up steps, r_modelWarmupBudget ms)
```

### Ghoul2 rotation-space blending (`r_g2Blend`, `r_g2BlendCurve`)

Stock Ghoul2 lerps the 12 floats of each bone's 3x4 matrix, both between keyframes and between animations. A
rotation shrinks and skews halfway through such a lerp, so limbs squash and candy-wrap. xenjk decodes each
compressed-quaternion bone into a pose made of a unit quaternion and the displacement of the bone's joint, the
pivot being the bone's base-pose translation. Poses blend by slerp of the rotation and lerp of the displacement,
and the result is rebuilt into a matrix. The joint turns about its pivot and the matrix stays orthonormal.
`r_g2BlendCurve 2` also eases the animation-to-animation weight: smoothstep into looping animations, ease-out
otherwise. Blend lengths do not change.

This applies to draws only. A bone cache's transform generation is marked as a draw generation when the
renderer's own traversal (`R_AddGhoulSurfaces`, `RB_TransformBones`) asks for it. Bolts, collision and every
server-side query keep the stock lerp, so gameplay and saber timing are untouched.

### Own joints (`r_g2Joints`)

A GLM has no skeleton of its own. Every vertex is skinned with `animMatrix * inverse(basePoseMat)` from the GLA, so
a converted character with its own proportions bends about Kyle's joints, and each bend shears the limb. A model can
ship `<model>.joints` beside its GLM, giving each bone's own joint and the bone that carries it. `R_LoadMDXM`
reads it (`R_LoadG2Joints`). After the stock transform, `G2_TransformBone` keeps each bone's rotation and moves its
translation so that it turns about its own joint, the joint riding its anchor: `S'(j) = S'(anchor)(j) + [S(k) -
S(anchor)(k)]`. Bone bolts (`G2_GetBoltMatrixLow`) sit on the corrected joint, so the saber hilt stays in the hand.
All of this is client side. Mods' cgames bend models through the renderer, so it works on any server, while the
server's own traces keep Kyle's joints. Models without the file draw exactly as stock.

### Review fixes to the Ghoul2 path

- `r_g2SmoothFix 1`: the back end reuses the front end's bone transform for the same frame, scene, time and root.
  That gives one smoothing pass per frame, as in rd-vanilla, and `r_ghoul2animsmooth` and the ragdoll factors
  work again. It also clears the render-traversal flag after the back end (SK-3/SK-4).
- `r_fixG2SceneTime 1`: the back end poses at the time the scene was submitted (`refdef.g2Time`), not at the clock
  of a menu drawn over the game (SK-1).
- `r_fixG2SceneLod 1`: LOD is measured from the scene's camera, so shadow views draw the LOD the main view draws
  (SK-19).
- `r_g2LazyBones 1`: skeletons are transformed and uploaded only for entities a view draws.
- `G2_TIME_EXACT` (advertised by `r_g2TimeExactApi`) poses a model at the caller's own time. The player screen's
  stage draws at its refdef time (`RDF_STAGE`).

### Vertex layout and bone palette

rd-vulkan (JKSunny) already skins Ghoul2 on the GPU from a static per-LOD VBO built by `R_BuildMDXM`. xenjk packs
that vertex into 44 bytes instead of 64 (`G2_VERT_*` in `tr_local.h`, review SK-13): normal and tangent as
`R16G16B16A16_SNORM`. The bone palette (`vkUniformBones_t`, `shaders/glsl/global.h`) holds 256 matrices: the
skeleton's bones, then the hair bones. Vertex bone references are bytes, so 256 is the ceiling. Tangents come from
MikkTSpace once per model and are kept in `cache/tangents` (`r_tangentCache`, `vk_tangent_cache.cpp`), shared by the
mesh, hair and cloth builds. `tools/characters/cook_tangents.py` cooks them into a pack ahead of play.

### Hair chains and body springs (`r_hair*`)

A model gets hair dynamics when it ships `models/<m>/model.chains` (HCH1 v2, [hair/FORMAT.md](../hair/FORMAT.md)).
The solver (`rd-common/hairchains/hair_sim.c`, C99) is position-based dynamics on particles at bone heads. It has
cones, lateral links, capsule and sphere colliders, containment shapes and a mirror step. Spring bones
(`hair_spring.c`: one damped spring per bone, chasing the bone's animated point, stepped at the asset's 1/120 s) handle
body jiggle. Both are root hair bones in the same
sidecar.

- **Stepping** (`rd-common/tr_hair.cpp`, `R_HairAddGhoul`): each Ghoul2 instance owns one solver, keyed by its
  `CBoneCache`, and steps at the asset's dt (1/30 s) from scene time. Inputs are lerped between the previous pose
  and this frame's. The game profile (`r_hairFeel*`) lets the hair feel the entity's run, turns and jumps at bounded
  rates. The flow terms (`r_hairFlow*`) damp the cone springs and add air drag, so locks swing and settle instead of
  ringing.
- **One decision per frame** (`rd-common/tr_sim.h`, `tr_sim.cpp`, `sim_frame.h`, shared with cloth; hair registers
  the shared cvars itself, so hair builds without `tr_cloth.cpp`): the first view of a frame that
  draws the character decides and steps, shadow views included. A character the main view culled still casts a
  simulated shadow. Steps owed after a long frame are carried over about `r_simCarryFrames` frames, at most
  `r_hairMaxSteps` per frame. The back end draws only a palette built for its own frame.
- **Resets** happen on first sight, when time runs backwards, after 500 ms unseen, on a teleport or on NaN.
  `r_hairPreroll 12` runs solver steps on the reset pose before the first draw, so hair does not visibly fall into
  place (HR-6). `r_hairFade 0.2` blends the drawn offsets to the animation over the last 20 % of `r_hairMaxDist`
  (HR-15).
  `r_hairWorldScale 1` makes a `modelScale`d entity feel gravity in its own units (HR-18).
- **Drawing** (`rd-vulkan/vk_hair.cpp`): `RB_TransformBones` appends the hair bones' matrices to the palette
  (`RB_HairVkPaletteWrite`). With `r_hairSharedVbo 1` (HR-10) the listed vertices are patched into the model's own
  vertex buffer to point at those palette slots; `0` keeps a substitute copy of each listed LOD. Every pass draws
  the simulated hair (lit pass, depth prepass, sun and character shadows, the hair opacity map), and nothing per
  vertex is uploaded per frame.
- **Body physics groups:** `r_hairSprings`, `r_hairSpringLimit`, and the `r_hairSpringChest` / `Hips` / `Other`
  scales are one set of controls. They cover both spring bones and FF7R-style body chains.

Deep reference: [hair/README.md](../hair/README.md), and [hair/PRESETS.md](../hair/PRESETS.md) for sidecars
built from presets.

### Garment cloth (`r_cloth*`)

A model gets cloth when it ships `models/<m>/model.cloth` (SKA1, [cloth/FORMAT.md](../cloth/FORMAT.md)). The solver
(`rd-common/cloth/skirt_sim.c`) runs in centimetres in the frame of a reference bone (the pelvis). It has tethers,
bending (dihedral or spring), body capsules and contacts against the collider mesh. `rd-common/tr_cloth.cpp`
(`R_ClothAddGhoul`) poses the asset's bones and runs fixed steps of 1/`r_clothHz` (60 Hz) under the same
per-frame decision as hair. It keeps each step relative to the pelvis and draws with `r_clothDrawLead 2`: the newest
step carried to the frame time, with each correction blended over the next step.

- **GPU solver** (`r_clothGpu 1`, `rd-vulkan/vk_cloth.cpp`): the particle part runs in the compute shaders
  `shaders/glsl/cloth_step.comp` and `cloth_draw.comp` (layout in `cloth_layout.h`), one workgroup per instance.
  The CPU runs only the frame part (`SkirtSim_StepFrame`) and queues step records. `vk_begin_frame` records the
  dispatches before the first render pass (`R_ClothVkBeginFrame`), and `cloth_draw.comp` writes the render
  vertices straight into a per-instance buffer in the Ghoul2 vertex layout.
- **CPU solver** (`r_clothGpu 0`, or for knobs the kernels lack): the draw buffer is written once per frame into the
  dynamic geometry buffer.
- **Drawing:** `RB_SurfaceGhoul` asks `RB_ClothVkSurface` for a substitute VBO/IBO. Pinned vertices skin from the
  GLM like the body, and simulated vertices hang from the reference bone with a (254, 0, 0, 1) weight on it twice.
  That pattern tells the lit shader the surface is cloth, so cloth skips self-shadow and the hair-strand paths.
  It is also kept out of the hair occluder and hair-map passes (`RB_SubmitDrawSurfs`).
- **Review defaults:** `r_clothSettle 6` relaxes a garment on the reset pose before its first draw (CL-15).
  `r_clothFeltSpeed 1` lets the solver feel body motion up to 2000 u/s, so a skirt lifts in falls (CL-2).
  `r_clothGpuPinSkin 1` places GPU-pinned particles with this frame's pose, as the CPU path does (CL-13).
- **rd-vanilla** builds the same glue with the CPU solver, and `RB_ClothSurface` (`rd-vanilla/tr_cloth_rb.cpp`)
  copies the cloth over the CPU-skinned positions in `tess`.

### Character shadows from the light grid (`r_charShadows`)

These shadows come from shadow-only lights that darken baked light and never re-light the world.
`R_SetupCharShadows` (`tr_charshadow.cpp`) takes the 8 nearest Ghoul2 casters within `r_charShadowDistance`. It finds
each caster's light from five light-grid samples, raises the elevation to `r_charShadowMinElevation` (40°) and
smooths it over 250 ms per instance. With `r_charShadowFloor 1` the light is taken at the floor under the caster.
Each caster gets an orthographic depth tile in layer 3 of the sun shadow image, a 4 x 2 atlas
(`vk_sunshadow.cpp` owns the image; `tr_charshadow.cpp` owns the casters, lights and tiles). `R_RenderCharShadows`
draws the tiles with the cascades' pipelines, and `R_IS_SHADOW_VIEW` guards the shadow-view paths. In the lit shader, `charShadowKeep` (`gen_frag.tmpl`) multiplies lightmap and
vertex-lit stages by the light they keep, with PCF and a penumbra from `r_charShadowLightSize`. `r_charShadows 1`
skips maps with sun cascades, which already shadow models; `2` keeps both. On by default since Install 11.
Design and measurements: [renderer-character-shadows.md](../renderer-character-shadows.md).

### Character self-shadow and the hair opacity map (`r_charSelfShadow*`)

`R_SetupCharSelfShadows` gives the nearest characters (`MAX_CHAR_SELF_SHADOWS`, within
`r_charSelfShadowDistance`) a tile in the lower half of the same atlas layer. The tile is a depth view of the
character alone along its own grid light, and alpha-tested stages cast through their alpha. `charSelfShadow` in
the lit shader traces `r_charSelfShadowRays` rays over the light's disc with `r_charSelfShadowSteps` distances
each, and takes the blocked share off the grid's directed light (hair, nose, brows, arms). With
`r_charSelfShadowHairMap 1` (latched) the nearest character's hair casts into an opacity map instead: its alpha is
summed as optical depth with two depth moments (`charHairTransmittance`), giving a continuous transmittance and no
strand speckle. `r_charSelfShadowScreen 1` folds the self-shadow into the grid light's contact mask, where it is
blurred. Off by default (`r_charSelfShadow 0`). It needs `r_shadowMapSize`.

### Character occlusion pass: AO and contact shadows (`r_charAO*`, `r_charContactShadows*`, `r_charOcc*`)

Stock SSAO is multiplied over the finished image, which darkens a character's direct light and makes stacked hair
cards darken each other. xenjk keeps `r_ssao_character 0` (no world SSAO on Ghoul2 pixels) and computes the
character's occlusion before it is lit:

1. The main pass begins as the `main_keep` render pass variant, and the depth prepass draws the opaque world and
   models. Model pixels are marked in the stencil (`r_charOccMask`).
2. `RB_CharOcclusion` (`vk_charocc.cpp`, called from `RB_DrawSurfs`) ends the pass, extracts linear depth (`vk_depth_extract_ex`) and, with
   `r_charOccHair 1`, draws the alpha-tested hair and lashes into a second channel through the occluder pass
   (`vk_begin_char_occluder_render_pass`).
3. `vk_ssao.cpp` computes GTAO (`r_charAORadius`, `Slices`, `Steps`) and up to three lights' contact-shadow masks
   (`r_charContactRays`, `Steps`, `Softness`), all at half size and only inside the screen rectangle of the models
   that read them (`r_charOccRect`; the box comes from posed joints, hair joints included, plus
   `r_charOccRectMargin`). A depth-aware blur (`r_charContactBlur`) follows.
4. The main pass resumes with everything loaded (`vk_begin_main_reload_render_pass`). In the lit pass, `ScreenAO`
   applies AO to ambient light and reflections only, and `ContactShadow` / `ContactShadowRay` darken the grid light
   and dynamic lights. A short full-resolution ray (`r_charContactFine`) keeps the darker of ray and mask.

Both are off by default (`r_charAO 0`, `r_charContactShadows 0`) and need `r_ssao` and `r_depthPrepass`.
`r_charOcclusionDebug` shows each term. `r_charFast 3` skips work that cannot change the image.

### Fill, rim and ambient floor (`r_charFill`, `r_charRim`, `r_charAmbientFloor`)

Light-grid lit Ghoul2 models get three terms derived from the grid at the model. `vk_update_entity_light_constants`
(`tr_backend.cpp`) sets them and `CharGridExtra` (`gen_frag.tmpl`) applies them:
- a fill light from 50° off the camera axis, opposite the grid light, occluded like ambient (`r_charFill 0.5`);
- an unshadowed rim from behind, 205° from the camera on the grid light's side (`r_charRim 0.6`);
- ambient at least 0.45 of the grid's whole light (`r_charAmbientFloor 0.45`).

The owner chose these defaults on 2026-10-27. Fill and rim turn smoothly as the camera crosses the grid light (SH-3;
its `r_fixRigSide` switch was removed, the fix is always on).

### Character look switches (`r_char*`)

- `r_charLinear` (0): 1 lights characters in linear space (albedo and light colours decoded from sRGB, the result
  encoded back), and 2 adds the Khronos PBR Neutral tonemap used by the Cycles references. Hooks are in `tr_light.cpp`,
  `tr_scene.cpp`, `common/pbr.glsl` and `common/fma.glsl`. Layer and tint colours are decoded too (SH-4; always on, the
  `r_fixLinearInputs` switch is gone).
- `r_charSkinSoftNormals` / `r_charSkinSoftAlbedo` (0): a cheap stand-in for subsurface blur on skin stages, a
  per-channel normal blend and a blurrier albedo mip for red. These are test switches in play. The stage uses
  `r_stageSkinSoft*` (1 / 2.5).
- `r_g2SampleShading 2` (latched): under MSAA, alpha-tested character stages run a per-sample alpha test as the
  sample mask, lit once per pixel, so sub-pixel hair strands get coverage instead of gaps. `r_g2SampleCoverage` is
  the alpha-to-coverage alternative (SH-8).
- `r_charTerms` (cheat) drops lighting terms to compare against reference renders.
- The shading lane's accepted fixes (skin wrap, plain-stage dlight specular, back-face normals, the FMA stage fixes,
  linear inputs, rig side) are unconditional now; their `r_fix*` switches were removed. What is left are
  `r_fixFmaShadows` (off: FMA stages take the grid light's shadow, test) and `r_fixG2SceneTime` /
  `r_fixG2SceneLod` (on, above).

Material keywords that select character shading modes (`toon`/`toonShade` with `r_toon 1`, the FMA game ramp
`fmaBody`/`fmaFace`) are parsed in `tr_shader.cpp`. They belong to the material system; see [renderer.md](renderer.md).

### Portrait rig and the stage (`r_portraitRig*`, `r_stageRig*`)

`r_portraitRig 1` (`tr_rig.cpp`, `R_AddSceneRig`, called by `RE_RenderScene`) places the Cycles studio rig's key,
fill, rim and top lights as dynamic lights around a point `r_portraitRigDist` ahead of the camera. Characters then
take a flat `r_portraitRigWorld` ambient instead of the light grid (`R_SetupEntityLighting`, `tr_light.cpp`, which
takes a stage's key light from `R_StageKeyLight`). It is a test tool for comparing against
Cycles renders. A menu scene with `RDF_NOWORLDMODEL | RDF_STAGE` (`rd-common/tr_types.h`) always gets the rig with
the `r_stageRig*` levels. It also gets the owner's play set of AO, contact shadows and self-shadow at full rays
(`STAGE_CHAR_*` in `tr_local.h`, `STAGE_SELF_SHADOW_*` in `tr_charshadow.cpp`), whatever the play cvars say, plus
per-sample shading of the figure (`r_stageSampleShading`). This is how the player screen and picker draw their
character; the screen itself is in wishlist/player-screen.md.

### Character texture pool and VRAM (`r_stream*`, `r_vram*`)

- **Ledger** (`vk_vram.cpp`, which also holds `vk_vram_fit`, the mip-drop search the DDS and RGBA8 loaders share): every `vkAllocateMemory` / `vkFreeMemory` passes through a hook (`vk_vram_hook`,
  installed in `vk_instance.cpp`) that counts allocations by kind (`vkmem` lists them). The budget is the
  device-local heap's `VK_EXT_memory_budget` figure, capped by `r_vramBudgetMB`, which lets you run a big card as
  a 6 GB one. Images that do not stream are admitted against what the budget leaves and drop top mips or go to
  system memory instead of failing.
- **Streaming** (`vk_stream.cpp`): a mipmapped character DDS under `models/players/` loads only its mip tail
  (`r_streamTailSize` 256) in `R_LoadDDSImage` (`vk_dds.cpp`). Each frame `R_StreamWantSurface` (from the Ghoul2
  surface adds) records the texel density the screen asks for. Sharper levels are read by range from the pk3 on
  the engine's prefetch worker (`FS_RangeReadAsync`) and copied in slices (`r_streamSliceMB`,
  `r_streamUploadMB`) by `R_StreamVkBeginFrame`, then swapped in. Shadow views ask for nothing (TX-1).
- **One pool:** the pool is the budget, less the rest of the renderer, less `r_vramReserveMB` (1024). When it is
  over, `R_StreamRelieve` gives memory back in the order of least visible loss: idle images to their tails,
  compaction of 64 MB blocks, surplus levels, then a level from every unpinned image. Textures that cannot be read
  by range (deflated DDS, PNG, TGA) are held in the pool too (`r_streamWhole`), and go back to their tail after
  `r_streamWholeIdle` seconds undrawn. `r_ddsMaxSize 4096` caps loaded size (TX-8).
- **Pins:** the model the local player warms up stays at full detail. `r_streamPinSize 0` and
  `r_streamPinTeamSkins 0` keep the pin floor and team-skin pins off (owner, 2026-11-10).

Design and numbers: [renderer-character-vram-20261109.md](../renderer-character-vram-20261109.md).

### Character loading without a freeze (`cl_modelWarm`, `cg_modelWarm`, `R_WarmModel`)

Stock loads a new player model in one frame: skin, every image, GLM build, then the cloth and hair state on first
draw. A switch between two heroes froze for 1.3 s warm and about 4 s cold. xenjk loads it in steps:

- **Renderer** (`vk_model_warm.cpp`): `R_WarmModel("model/skin")` returns a handle and steps from `RE_BeginFrame`
  for at most `r_modelWarmupBudget` (6) ms per frame. The stages are: read ahead the model's folder and its
  shaders' folders into the OS cache (`FS_PrefetchDir` / `FS_PrefetchDirEx`), register the GLM while
  `vk_mesh_jobs.cpp` packs its LODs on a worker, load and bind the cloth and hair assets, register the shaders one
  at a time, ask `vk_pipeline_async.cpp` for their pipelines, upload LODs and hair buffers a step at a time, and
  register the skins. `R_WarmModelEx(..., WARMMODEL_OTHER_PLAYER)` serves other players: it pins nothing and
  shares jobs. `warmmodel <model/skin>` starts one by hand, and `loadprofile` prints stage times
  (`r_loadProfile`). The renderer advertises all this with `r_modelWarmupApi 2`, and `r_pipelineCache 1` keeps
  the pipeline cache between launches.
- **Engine** (`client/cl_main.cpp`, `CL_ModelWarming` in `CL_CheckUserinfo`): with `cl_modelWarm 1` a changed `model`
  is held out of the userinfo until the renderer's handle is ready, or for at most `cl_modelWarmTimeout` (10 s).
  Server and cgame registrations then find everything loaded. `cl_cgameapi.cpp` and `cl_uiapi.cpp` append
  `R_WarmModel` / `R_WarmState` to the cgame and UI import tables' `ext` and set the ROM cvars `cl_cgameWarmApi`
  / `cl_uiWarmApi`. The read-ahead and range reads are in `qcommon/files.cpp` (`fs_prefetchApi` 2 or more).
- **cgame** (`cg_modelwarm.c`, `CG_ModelWarm` / `CG_ModelWarmCheck`, hooked from `CG_NewClientInfo` and `CG_DrawActiveFrame`): when another player's configstring names a
  model that player does not have, cgame keeps drawing their old model (or `DEFAULT_MODEL`), warms the new one,
  and runs `CG_NewClientInfo` again once it is ready, failed or older than `cg_modelWarmTimeout`.
- **UI**: the picker (`ui_picker.c`, roster in `ui_roster.c`) and the player screen (`ui_player.c`) warm the
  previewed model the same way.

Plan and measurements: [character-loading-plan.md](../character-loading-plan.md),
[character-loading-and-picker.md](../character-loading-and-picker.md).

### The xen animation set (`cg_xenAnims`)

`cg_xenanims.c` (`XEN_ANIM_DIR`, `CG_XenAnimSetForModel`; `CG_AnimSet` and `CG_AnimSetForModel` are the entry points
`cg_players.c` calls) loads
`models/players/_humanoid_xen/animation.cfg` and `animevents.cfg`: the stock rows plus replacement rows whose frames
are appended after a GLA's stock frames. A humanoid model whose GLA holds those frames (or that `glas.txt` lists)
plays the set while `cg_xenAnims 1`. The set drives display only (frames, events, ragdoll and corpse poses).
Prediction, timers and the server keep the stock set and stock timing. Making a GLA that holds the set is in
[characters-converting.md](characters-converting.md).

## Take this piece

**Depends on:** [renderer.md](renderer.md) (rd-vulkan as xenjk builds it, SPIR-V generation, the sun shadow image and
SSAO these features reuse). The models and sidecars come from [characters-converting.md](characters-converting.md).

### Files

New files, copy whole:

| Path | Role |
|---|---|
| `codemp/rd-common/tr_g2blend.{h,cpp}` | rotation-space bone pose, blend, curves (rd-vulkan and rd-vanilla) |
| `codemp/rd-common/tr_g2joints.{h,cpp}` | `.joints` parser and per-bone correction |
| `codemp/rd-common/hairchains/hair_asset.{h,c}`, `hair_sim.{h,c}`, `hair_spring.{h,c}` | hair and spring solvers, `.chains` reader (C99) |
| `codemp/rd-common/tr_hair.{h,cpp}`, `tr_hair_local.h` | hair front-end glue; needs `tr_sim` and `tr_g2ext.h`, not `tr_cloth` |
| `codemp/rd-common/cloth/cloth_asset.{h,c}`, `skirt_sim.{h,c}` | cloth solver and `.cloth` reader (C99) |
| `codemp/rd-common/tr_cloth.{h,cpp}`, `tr_cloth_local.h` | cloth front-end glue |
| `codemp/rd-common/tr_sim.{h,cpp}`, `sim_frame.h` | what hair and cloth share: the `r_sim*` cvars (`R_SimRegister`, called by both `R_HairRegister` and `R_ClothRegister`), the `r_simCheck` counters, the one-decision-per-frame rule and step plan |
| `codemp/rd-common/tr_g2ext.h` | what a renderer's `tr_ghoul2.cpp` gives hair and cloth on its bone caches: `R_ClothEvalBone`, `R_BoneCacheModel` (rd-vulkan) |
| `codemp/rd-vulkan/vk_hair.{h,cpp}` | hair palette, patched or substitute vertex buffers |
| `codemp/rd-vulkan/vk_cloth.{h,cpp}` | GPU cloth dispatch and draw streams |
| `codemp/rd-vulkan/shaders/glsl/cloth_step.comp`, `cloth_draw.comp`, `cloth_layout.h` | cloth compute shaders |
| `codemp/rd-vulkan/shaders/spirv/cloth_shader_data.c` | generated cloth SPIR-V (`tools/cloth_spirv.py`) |
| `codemp/rd-vanilla/tr_cloth_rb.cpp` | rd-vanilla cloth back end |
| `codemp/rd-vulkan/vk_stream.cpp`, `vk_vram.cpp` | texture streaming, VRAM ledger and budget |
| `codemp/rd-vulkan/tr_charshadow.cpp` | character shadows and self-shadows: casters, grid lights, atlas tiles, `charshadowdump`, `R_RenderCharShadows` |
| `codemp/rd-vulkan/vk_charocc.cpp` | the character occlusion pass: `RB_SetupCharOccLights`, `RB_CharOcclusion`, `RB_CharOccCameraConstants` |
| `codemp/rd-vulkan/tr_rig.cpp` | the portrait and stage light rig (`R_AddSceneRig`, `R_StageKeyLight`) |
| `codemp/rd-vulkan/vk_dds.cpp` | DDS loading with the stream's tail attach (`R_LoadDDSImage`), layer arrays |
| `codemp/rd-vulkan/vk_model_warm.cpp` | stepped model warm-up |
| `codemp/rd-vulkan/vk_mesh_jobs.cpp`, `vk_pipeline_async.cpp`, `vk_tangent_cache.cpp` | GLM pack worker, pipeline worker, tangent cache |

Hooks in stock (JKSunny or OpenJK) files:

| File | Function | Hook |
|---|---|---|
| `codemp/rd-vulkan/tr_ghoul2.cpp` | `G2_TransformBone`, `G2_BlendBoneDraw` (new), `G2_TransformGhoulBones` | slerp blends and eased curve in draw generations; `G2J_Correct` per bone |
| | `R_LoadMDXM`, `R_LoadG2Joints` (new) | read `<model>.joints` |
| | `R_AddGhoulSurfaces` | `R_ClothAddGhoul` / `R_HairAddGhoul` after the bones; `R_StreamWantSurface`; scene LOD and smoothing reuse |
| | `RB_TransformBones` | hair palette after the skeleton, lazy bones, occlusion-rectangle joint box |
| | `RB_SurfaceGhoul` | cloth substitute stream, hair buffer |
| | `RemoveBoneCache` | `R_ClothForgetBoneCache` / `R_HairForgetBoneCache` |
| | `G2_GetBoltMatrixLow` | bone bolts on the model's own joint |
| `codemp/rd-vulkan/tr_main.cpp` | `R_AddDrawSurf`, `R_AddEntitySurface` | the shadow-view tests (`R_IS_SHADOW_VIEW`); the character shadow code itself is in `tr_charshadow.cpp` |
| `codemp/rd-vulkan/tr_scene.cpp` | `RE_BeginScene`, `RE_RenderScene` | `g2Time`; calls `R_SetupCharShadows`, `R_SetupCharSelfShadows`, `R_RenderCharShadows` and `R_AddSceneRig` |
| `codemp/rd-vulkan/tr_light.cpp` | `R_SetupEntityLighting` | rig ambient on characters; linear light |
| `codemp/rd-vulkan/tr_backend.cpp` | `RB_DrawSurfs`, `vk_update_entity_light_constants`, `vk_update_camera_constants` | calls into `vk_charocc.cpp` (the occlusion split of the main pass); fill, rim, floor, self-shadow uniforms |
| `codemp/rd-vulkan/vk_ssao.cpp`, `vk_renderpass.cpp`, `vk_depth.cpp` | | character GTAO and contact masks, `main_keep` / reload and occluder passes, depth extract |
| `codemp/rd-vulkan/vk_sunshadow.cpp` | | the shadow image's atlas layer for character and self-shadow tiles, hair opacity map |
| `codemp/rd-vulkan/shaders/glsl/gen_frag.tmpl`, `gen_vert.tmpl`, `global.h`, `common/ssao.glsl` | | `charShadowKeep`, `charSelfShadow`, `charHairTransmittance`, `ContactShadow`, `ScreenAO`, `CharGridExtra`; 256-bone palette |
| `codemp/rd-vulkan/vk_image.cpp` | calls `R_LoadDDSImage` (`vk_dds.cpp`) | tail load and stream attach happen there |
| `codemp/rd-vulkan/vk_frame.cpp` | `vk_begin_frame` | `R_ClothVkBeginFrame`, `R_StreamVkBeginFrame` |
| `codemp/rd-vulkan/vk_instance.cpp` | | `vk_vram_hook` |
| `codemp/rd-vulkan/tr_init.cpp` | `R_Register`, `RE_Shutdown`, `GetRefAPI` | cvars, `R_ClothRegister` / `R_HairRegister` / `R_StreamRegister`, `re.WarmModel*` exports |
| `codemp/rd-vanilla/tr_ghoul2.cpp`, `tr_init.cpp` | | `r_g2Blend` and CPU cloth, as above |
| `codemp/rd-common/tr_public.h`, `tr_types.h` | `refexport_t` | `WarmModel`, `WarmState`, `WarmModelEx`; `RDF_STAGE`, `WARMMODEL_*` |
| `codemp/client/cl_main.cpp` | `CL_CheckUserinfo`, `CL_ModelWarming` (new) | userinfo hold |
| `codemp/client/cl_cgameapi.cpp`, `cl_uiapi.cpp` | | `ext.R_WarmModel` / `R_WarmState`, `cl_cgameWarmApi` / `cl_uiWarmApi` |
| `codemp/qcommon/files.cpp`, `qcommon.h` | | `FS_PrefetchDir(Ex)`, `FS_Range*` |
| `codemp/cgame/cg_players.c`, `cg_public.h`, `cg_xcvar.h`, `cg_local.h` | `CG_NewClientInfo` path | calls `CG_ModelWarm` (`cg_modelwarm.c`) and `CG_AnimSet` (`cg_xenanims.c`); the files are new, these are the hooks |

### Build inputs

- `codemp/rd-vulkan/CMakeLists.txt` lists every new renderer file, the `rd-common` sources and
  `shaders/spirv/cloth_shader_data.c`. `codemp/rd-vanilla/CMakeLists.txt` lists `tr_g2blend`, the cloth sources and
  `tr_cloth_rb.cpp`, but no hair or joints.
- After editing `gen_frag.tmpl`, `gen_vert.tmpl`, `global.h` or `common/*.glsl`, run
  `python tools/renderer_spirv.py build` (Khronos glslang, `GLSLANG_EXE`; Godot 4 as fallback, `GODOT_EXE`), then
  `verify`.
- After editing the cloth shaders, run `python tools/cloth_spirv.py` (Godot's glslang, `GODOT_EXE`).
  `python tools/cloth_spirv.py --check` exits 1 when the GLSL changed since the arrays were built. It is not wired
  into CMake.
- Hair and cloth solvers must not be built with fast-math or FP contraction (`/fp:precise`, `-ffp-contract=off`)
  if you want them to match the offline harnesses.

### Environment

| Variable | Read by | Meaning |
| --- | --- | --- |
| `GLSLANG_EXE`, `GODOT_EXE` | `renderer_spirv.py`, `cloth_spirv.py` | shader compilers; else `PATH` ([renderer.md](renderer.md)) |
| `ProgramFiles(x86)` | `tools/characters/cook_tangents.py` | Windows: where `vswhere` finds MSVC for the tangent cooker |
| `XENJK_MAIN`, `GPU_WINDOW` | `tools/char_vram_check.py` | the checkout holding `.codex-research` (default this repository), and the folder with the local client harness `gw_common.py` |

The hair and cloth harnesses build and run from the repository root and read no variables. The game checks (`simcheck_run.py`,
`char_vram_check.py`) run hidden, muted (`s_volume 0`, `s_mute 1`) clients against the game at
`game_directory/Jedi Academy/GameData`.

### Cvars and commands

All of them, with flags and descriptions: [xenjk-cvars.md](../xenjk-cvars.md), sections *Ghoul2 animation*, *Hair*,
*Cloth*, *Character shadows*, *Character self-shadow*, *Character AO*, *Character contact shadows*, *Character
occlusion pass*, *Character look*, *Portrait rig*, *Engine and client*, and the `r_stream*`, `r_vram*`, `r_sim*`,
`r_stage*`, `r_fix*` rows in *Renderer, other*. The ones that matter:

| cvar | default | |
|---|---|---|
| `r_g2Blend` / `r_g2BlendCurve` | 1 / 2 | rotation-space blending; eased blend curve |
| `r_g2Joints` | 1 | own joints for models with `.joints` |
| `r_hair` / `r_hairSprings` | 1 / 1 | hair chains; body springs |
| `r_hairMaxDist` / `r_hairMaxInstances` | 1500 / 8 | hair simulation range and count |
| `r_cloth` / `r_clothGpu` | 1 / 1 | cloth; compute solver |
| `r_clothMaxDist` / `r_clothMaxInstances` | 1500 / 8 | cloth range and count |
| `r_simPerFrame` / `r_simSteps` | 1 / 1 | hair and cloth: one decision per frame; carry owed steps |
| `r_charShadows` | 1 | grid character shadows where there are no sun cascades |
| `r_charSelfShadow` | 0 | self-shadow strength (Play_Tifa: 1) |
| `r_charAO` / `r_charContactShadows` | 0 / 0 | occlusion pass (Play_Tifa: 1 / 8) |
| `r_charFill` / `r_charRim` / `r_charAmbientFloor` | 0.5 / 0.6 / 0.45 | grid-derived extra light |
| `r_charLinear` | 0 | linear-light characters (Play_Tifa: 2) |
| `r_g2SampleShading` | 2 | per-sample alpha test for hair under MSAA |
| `r_streamTextures` / `r_vramReserveMB` / `r_vramBudgetMB` | 1 / 1024 / 0 | texture streaming; pool reserve; budget cap |
| `cl_modelWarm` / `cg_modelWarm` / `r_modelWarmupBudget` | 1 / 1 / 6 | local and other players' warm-up; ms per frame |
| `cg_xenAnims` | 1 | the `_humanoid_xen` animation set |

Diagnostics: `r_hairStats`, `r_clothStats`, `r_simCheck` (SIMCHECK counters), `r_hairDump` / `r_clothDump`
(per-step dumps for the offline harnesses), `r_g2BlendStats`, `r_g2LodStats`, `r_charShadowDebug`,
`r_charSelfShadowDebug`, `r_charOcclusionDebug`, `r_charOccRect 2`, `r_g2BindPose`. Commands: `charshadowdump`
(casters as drawn to an OBJ), `streaminfo`, `vkmem`, `warmmodel`, `loadprofile`.

### Data formats

| File | Spec | Read by |
|---|---|---|
| `models/<m>/model.chains` (HCH1 v2) | [hair/FORMAT.md](../hair/FORMAT.md) | `hair_asset.c` |
| `models/<m>/model.cloth` (SKA1) | [cloth/FORMAT.md](../cloth/FORMAT.md) | `cloth_asset.c` |
| `models/<m>/model.joints` (text, `xenjk_joints 1`) | [g2-joints.md](../g2-joints.md) "The file" | `tr_g2joints.cpp` |
| `models/players/_humanoid_xen/animation.cfg`, `animevents.cfg`, `glas.txt` | stock animation.cfg rows; the comment above `XEN_ANIM_DIR` in `cg_xenanims.c` | cgame |
| `cache/tangents` (prepared tangents) | `vk_tangent_cache.cpp` | renderer |
| `*.hcd` / `*.cld` dumps, `*.hcr` / `*.skp` reels | [hair/README.md](../hair/README.md), [cloth/FORMAT.md](../cloth/FORMAT.md) | offline harnesses |

Every one of these files is made by a conversion from a game you own. None ship with the repo.

### Porting notes

- **Blending** has no renderer-specific dependency. It ports to rd-vanilla as-is (it is already there) and to rend2
  by the same `G2_BlendBoneDraw` hook in its `G2_TransformBone`. Keep it out of bolt and collision traversals, or
  gameplay changes.
- **Joints** touches only `tr_ghoul2.cpp` and the GLM loader. It is not built in rd-vanilla. The correction writes
  `mFinalBones`, which rd-vanilla's CPU skinning also reads, so it should port the same way, but this is untested.
- **Hair** relies on GPU skinning with a palette larger than the skeleton (256 entries, byte bone indices). A CPU-
  skinned renderer would have to skin the listed vertices against the hair matrices itself; rd-vanilla leaves hair
  rigid.
- **Cloth** works on the CPU anywhere (`RB_ClothSurface` pattern). The compute path needs rd-vulkan's frame
  structure (`vk_begin_frame` before the first render pass) and the packed 44-byte vertex (`CL_RV_WORDS 11`).
  On a tree with the 64-byte JKSunny vertex, port SK-13 first or revert `cloth_draw.comp`'s writer.
- **Shadows, self-shadow, AO, contact shadows, fill and rim** all live in rd-vulkan's generated fragment shader and
  its render pass layout (`main_keep`, reload, occluder pass, the 4-layer sun shadow image). They do not port to
  GL renderers without a rewrite. They need the light grid (`R_LightGridSample`) and, for the occlusion pass, the
  depth prepass.
- **Streaming and warm-up** need the engine side: `FS_PrefetchDir(Ex)` and `FS_Range*` in `qcommon/files.cpp`,
  exposed to renderers through `refimport_t`, and the client userinfo hold. Modules check `cl_cgameWarmApi` /
  `cl_uiWarmApi` and `r_modelWarmupApi` first, so a xenjk cgame runs on a stock engine and the reverse.
- **32 vs 64 bit:** everything builds for both. Many converted heroes press on the 32-bit address space, which is
  one reason the installed launchers default to the 64-bit client.
- **Time:** the simulations step from scene time (`refdef.time`), not frame count. Replays, `timescale` and paused
  menus behave because of that. Keep it if you hook them into another renderer.

## Do it yourself

This chapter is runtime code, not a pipeline. To bring one piece over and check it offline before any game run:

1. Copy the piece's new files (table above) into the same paths of your tree, and add them to your renderer's
   `CMakeLists.txt`.
2. Apply the hooks for that piece only: `rg -n "xenjk|r_g2Blend|R_HairAddGhoul|R_ClothAddGhoul|RB_ClothVk|RB_HairVk"
   codemp/rd-vulkan/tr_ghoul2.cpp` lists them. `git log e4fe7581.. -- codemp/rd-vulkan/<file>` gives each one's
   commit and reason.
3. If you touched shaders, regenerate SPIR-V: `python tools/renderer_spirv.py build`, then `verify`. For cloth,
   run `python tools/cloth_spirv.py`, then `--check`.
4. Check the solvers against the offline harnesses, built from the repo root:
   - hair: `gcc -std=c99 -O2 -ffp-contract=off -o hc_replay.exe tools/hair/hc_replay.c
     codemp/rd-common/hairchains/hair_sim.c -lm`. Record an in-game step dump with `r_hairDump <file>` and turn it into a
     reel with `python tools/hair/hcd_to_hcr.py <dump.hcd> <model.chains> <out.hcr>`, which prints the matching
     `hc_replay` arguments. `tools/hair/hc_frames.c` (build line in its header) checks the per-frame step plan with
     long frames.
   - cloth: `cmake -S tools/cloth -B <build dir> -A x64`, then `cmake --build <build dir> --config Release`, which
     builds `skirt_replay_f` / `_d`. Then run `skirt_replay --asset <model.cloth> --mats <dump.cld> [--frames ...]` on
     an `r_clothDump` file.
5. In game, load a model with the sidecar, then set `r_hairStats 5` / `r_clothStats 5` and `r_simCheck 5`. Toggle the
   piece's cvar to 0 for the stock behaviour, which is the A/B.

## Limits and open issues

- rd-vanilla has blending and CPU cloth only: no hair, joints or character lighting. rend2 has none of it.
- `charshadowdump` skins on the CPU and shows hair rigid.
- Cloth draws only on LOD 0 surfaces; lower LODs draw the skinned garment.
- The occlusion pass (AO, contact) and self-shadow are off by default. They are tuned against Cycles references
  through `Play_Tifa.bat` and the stage, and their cost in crowded scenes is not measured beyond the review's fast
  pass, which had about 0.45 ms of launch-to-launch noise.
- Review items left open by design ([review](../review/2026-11-09-character-rendering.md), "Next actions"):
  TX-6 (raw or deflated textures decode on the main thread, a hitch is left), TX-14 (picker cancel), TX-16
  (staging-buffer cap), HR-11, HR-12 (an automated fixture test), CL-6 (GPU readback for cloth checks), CL-14,
  SK-12, and SK-13's depth-pass cost (0.1-0.3 ms suspected, wants a longer A/B). SH-11 needs an AMD or Intel GPU
  to check.
- A team or saber change during another player's warm-up loads the old model's other team skin at once. The GLM read
  on a listen server is still synchronous in the server frame.
- `tools/char_vram_check.py` drives the installed game through a client harness kept in a local research folder
  (`GPU_WINDOW`), so it does not run from a clean checkout. `tools/hair/simcheck_run.py` uses
  `tools/renderer_scenes.py`'s launcher and runs from the checkout.

## Further reading

- [review/2026-11-09-character-rendering.md](../review/2026-11-09-character-rendering.md): the 115 findings, each
  fix, its cvar and what was measured. This is the current state of everything above.
- Lane reports [skinning](../review/2026-11-09/character/skinning.md),
  [shading](../review/2026-11-09/character/shading.md), [hair](../review/2026-11-09/character/hair.md),
  [cloth](../review/2026-11-09/character/cloth.md), [textures](../review/2026-11-09/character/textures.md): code
  locations and failure scenarios, at the reviewed commit.
- [g2-anim-smoothing.md](../g2-anim-smoothing.md): the blending math, the curves and the side-by-side check.
- [g2-joints.md](../g2-joints.md): why own joints, the correction, the file and the 0.3 mm parity check.
- [hair/README.md](../hair/README.md), [hair/FORMAT.md](../hair/FORMAT.md), [hair/PRESETS.md](../hair/PRESETS.md):
  the solver, its cvars, the sidecar and preset-built chains.
- [cloth/FORMAT.md](../cloth/FORMAT.md) and
  [the 2026-11-09 views and dropped steps record](../cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md):
  the cloth asset, and why hair and cloth decide once per frame.
- [renderer-character-shadows.md](../renderer-character-shadows.md): the light-grid survey and the shadow design.
- [renderer-character-vram-20261109.md](../renderer-character-vram-20261109.md): the budget, pool and step-down
  design with measurements.
- [character-loading-plan.md](../character-loading-plan.md) and
  [character-loading-and-picker.md](../character-loading-and-picker.md): the load-path measurements, warm-up
  milestones and picker.
- wishlist/player-screen.md: the player screen and its stage lighting.
- stellar-blade-characters.md: spring bones and cloth on a full port.
- renderer-vulkan-in-xenjk.md: the install log; Installs 11 (character shadows),
  19-28 (look, hair, occlusion pass), 37 (fill and rim), 58-61 (loading), 62 (xen animation set).
