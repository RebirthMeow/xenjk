# The renderer: rd-vulkan in xenjk

This chapter covers xenjk's main renderer, `rd-vulkan`. It is JKSunny's Vulkan PBR renderer from the EternalJK fork,
plus the work xenjk did on it before and after the import. You get the build (32- and 64-bit), the shader
generation, the renderer API that xenjk added to `tr_public.h` and the engine side that serves it, and the
upgrades that apply to every map: sun and cascaded shadows, parallax-corrected reflection probes, faster SSAO, dynamic
glow, 4x MSAA with alpha to coverage, PBR material sidecars and material keywords, DDS/BC7 textures, video
capture, and the bench tools used to measure all of this.

**Status:** `rd-vulkan` is the installed game's default renderer, built from `master` for both clients
(`rd-vulkan_x86.dll` beside `xenjk.x86.exe`, `bin64/rd-vulkan_x86_64.dll` beside `bin64/xenjk.x86_64.exe`; Install 94,
2026-11-19). Every feature here is in `master` and on by default, with three exceptions: bloom (off by default),
the `r_smoke` volumetric smoke (a look-only prototype that draws nothing until the `smoke` command is used) and the
debug views. rd-vanilla is still built and shipped as the fallback.

## What it does

### Where the code came from

```
JKSunny rd-vulkan (EternalJK fork, pbr line)
  |  our perf/vulkan commits in a JKSunny checkout (d4225a303..d4090e931, outside this repo):
  |  latency fix, one MSAA resolve, half-res SSAO, rend2's sun and cascades, parallax, depth prepass
  v
e4fe7581  codemp/rd-vulkan imported unchanged (perf/vulkan d4090e931)
ffbabfee  wired into xenjk's build and client; Vulkan window entries in refimport_t; xenjk.x86.exe
2f785a34  the bench preset became the code defaults
  ...     ~230 later commits in codemp/rd-vulkan (git log e4fe75812.. -- codemp/rd-vulkan)
```

- **Before the import.** The bench work in [renderer-bench.md](../renderer-bench.md) was done in a JKSunny checkout
  on a branch called `perf/vulkan`. It covers the frame-latency fence (`r_maxFrameLatency`), `r_presentWait`,
  `r_swapchainImages`, a single MSAA resolve with the post-processing at 1x, half-resolution SSAO with a bilateral
  blur, an 8-bit glow chain, rend2's `q3gl2_sun` with three sun cascades, the parallax port, the depth prepass and
  lightmap styles drawn once per surface. Those commits came into xenjk as one squash (e4fe7581), so `git log` here
  starts at the import. [xenjk-cvars.md](../xenjk-cvars.md) lists their cvars in their own section.
- **The import.** `codemp/rd-vulkan` was copied unchanged. The next commit (ffbabfee) built it against xenjk's
  own `rd-common` (fonts, image loaders), `qcommon` and shared headers. The cgame/ui ABI is therefore xenjk's by
  construction. Where JKSunny's headers differed (`RF_FULLBRIGHT` against xenjk's `RF_G2MINLOD` on bit 0x400000,
  the skin surface count, `SHADER_MAX_VERTEXES`), the renderer's uses were already `#ifdef`'d or sized from the
  shared headers. renderer-vulkan-in-xenjk.md, "API/ABI", has the table.
- **After the import.** About 230 commits. `git diff e4fe75812 --stat -- codemp/rd-vulkan` sizes them. Most of the
  line count belongs to the render scene (`vk_scene*.cpp`, `shaders/glsl/scene/`) and to the character work, which
  have their own chapters (see "What lives where" below).

### One frame

```
RE_RenderScene (front end)
  R_SetupSunShadowCascades / R_RenderSunShadowCascades   three ortho views along -sunDir (tr_main.cpp)
  R_RenderView                                            main view, draw-surface list
backend (tr_backend.cpp), per frame
  RB_RenderSunShadowCascades   D16 2D-array cascades, drawn before the main pass (vk_sunshadow.cpp)
  depth prepass                opaque world, depth only (r_depthPrepass)
  main MSAA pass               lit stages: lightmap/grid/vertex light + sun + probes + dlights (gen_frag.tmpl)
  refraction extract           (only when refractive surfaces are drawn)
  vk_one_resolve_pass          the single MSAA resolve, then:
    depth extract -> SSAO at half res + blur (vk_ssao.cpp)
    glow extract -> blur chain (vk_dynamic_glow.cpp)
    post 1x pass: SSAO blend, glow blend, smoke (vk_smoke.cpp), render-scene grade
  bloom (vk_bloom.cpp, off by default), HUD and 2D at 1x, gamma, present
```

Before the perf/vulkan work, each post effect reloaded and resolved the MSAA image again.
Collapsing that into one resolve (`vk_one_resolve_pass`) and running SSAO at half resolution took the bench's GPU
frame from 3.9 to 1.3 ms at 4K ([renderer-bench.md](../renderer-bench.md), "JKSunny parity steps 1-4").

### Sun and cascaded shadows

- **Source.** The sky shader's `q3gl2_sun <r g b> <intensity> <yaw> <elevation> <mapLightScale> <shadowScale>`
  (rend2's keyword) is parsed in `tr_shader.cpp`. `sun`, `q3map_sun` and `q3map_sunExt` give a direction too.
  `r_sunShadows 1` reads the map light and shadow scales. `r_forceSun` (cheat) lights maps whose sky has no sun.
- **Term.** `r_sunlightMode` 1 modulates the lightmap, 2 (default) adds the sun as a primary light. Both follow
  rend2's formulas, quirks included, so that lit content looks as it does in rend2. Mode and colour travel in the
  camera UBO (`vkUniformCamera_t`, `shaders/glsl/global.h`), which costs one uniform branch and no new shader
  variants.
- **Cascades.** Three orthographic, sphere-bounded cascades with texel snapping and depth clamp; the device must
  have `depthClamp`, otherwise there are no shadows. Casters are opaque world surfaces, brush models and
  third-person models (`r_sunShadowModels`). Faces are culled from the light direction.
  Sampling uses rend2's `sunShadow()`/`PCF()` with a comparison sampler. `r_shadowFilter 2` is 9 rotated taps, with the
  rotation covering a full turn (always on; its `r_fixSunPcf` switch is gone).
- **xenjk additions.**
  - The cascade pass is no longer scissored to the window (5abd5c28); the clamp had left lit borders.
  - The far and middle cascades are cached around the eye on a grid: static casters are redrawn only when the eye
    changes cell, and moving models are drawn over the copy each frame (`r_sunShadowFarCache`,
    `r_sunShadowMidCache`, `r_sunShadowCacheSkip`; a16c446bd). The copy, store and restore live in
    `vk_sunshadow.cpp` (`vk_sun_shadow_cache_copy`, `vk_sun_shadow_kept`); the render scene only asks whether a
    layer is kept (`r_sceneKeptCull`).
  - Cascades blend in depth along the light (53dc5a0a9).
- **Defaults:** `r_sunlightMode 2`, `r_sunShadows 1`, `r_shadowMapSize 2048`, `r_shadowFilter 2`,
  `r_shadowCascadeZFar 4096`, `r_sunStylePasses 0` (the sun once per surface, not once per lightmap style).
- **Debug:** `r_sunShadowDebug 1..12` (cascade tint, shadow value, lighting probes), `r_sunShadowInfo`.

Design and the stage-by-stage port: [renderer-vulkan-sun-port.md](../renderer-vulkan-sun-port.md). The scissor and
last-cascade fixes: [renderer-vulkan-sun-shadow-investigation.md](../renderer-vulkan-sun-shadow-investigation.md).

### Reflection probes (`misc_cubemap`)

- **Placement.** At map load (`tr_bsp.cpp`), `cubemaps/<map>/env.json` is tried first. Then `misc_cubemap` entities are read,
  with their `radius`. A map without them falls back to the first spawn-point class it has
  (`info_player_deathmatch`, `_start`, `_duel`, `_intermission`). Probes render once, in `R_RenderAllCubemaps`
  (`tr_bsp.cpp`), and are prefiltered in `vk_cubemap.cpp`, with mip j holding perceptual roughness j/6.
- **Parallax correction** (`r_cubeMapParallax 1`, cef0129a). Each probe gets a box fitted at load
  (`R_FitCubemapBox`, `vk_cubemap.cpp`): along each axis direction, a 5 x 5 fan of rays steps through the BSP until it reaches a leaf
  without a cluster, and the median distance sets that face. Reflections are box-projected (Lagarde 2012). Mode 2
  uses the probe's sphere and 0 the old infinite distance.
- **Blending** (`r_cubeMapBlend 1`). Each draw binds its probe and the probe's 7 nearest neighbours. Each pixel
  weights them by inverse distance to the 4th power. `r_cubeMapRoughCut 1` drops the parallax and most neighbours
  on rough surfaces, where they cannot be seen.
- **Ambient and occlusion** (85ed982d). `r_cubeMapDiffuse` lets the probes' irradiance shape the ambient light of
  models lit by the grid. `r_specularOcclusion` (Lagarde) occludes reflections by material AO. `r_specularAA`
  (Tokuyoshi-Kaplanyan) widens roughness where the normal changes quickly across a pixel.
- **Limits.** At most 63 probes are used, because the sort key holds probe index + 1 in 6 bits. Only xenjk's packs
  place probes, so stock maps get spawn-point probes.
- **Test map.** `tools/reflection_map/build_reflection_map.py` builds `xenjk_reflect`, a polished hall with one probe
  per colour zone. It needs q3map2 through the showcase builder (`tools/showcase_map/build_showcase_map.py`).

### Materials: PBR sidecars, keywords, roughness

- **Sidecars by name (JKSunny's).** For a stage's albedo `X`, `tr_shader.cpp` looks for a physical map `X_rmo`,
  `_rmos`, `_moxr`, `_mosr`, `_orm` or `_orms`, and for a normal map `X_n` or `X_nh` (normal + height for
  parallax). The suffix table is `textureMapTypes` in `vk_local.h`. Explicit keywords take precedence: `normalMap`,
  `normalHeightMap`, `specMap`/`specularMap`, `rmoMap`/`rmosMap`, `ormMap`/`ormsMap`.
- **`.mtr`.** rd-vulkan reads `shaders/X.mtr` only in place of an existing `shaders/X.shader`; a standalone `.mtr` is
  never read. Maps found by name can hide this.
- **xenjk's stage keywords.** All are parsed in `ParseStage` (`tr_shader.cpp`) into `stageMaterial_t`
  (`tr_local.h`), so stage collapsing moves them together. Each feature is a specialization constant of
  `gen_frag.tmpl`, so a stage without the keyword runs the original expressions. `r_pbrMaterials` (latched,
  default 1023) is a bit mask that can turn each one off.

| keyword | what | applies to |
| --- | --- | --- |
| `detailNormalMap <img> <tile> <strength>` | tiling detail normal over the stage normal | any stage |
| `detailMap <img> <tile> <strength>` | tiling albedo (rgb) and roughness (a) detail | any stage |
| `emissiveMap <img> [r g b] [intensity]` | emission after lighting; with `glow`, the glow pass sees only the emissive | any stage |
| `tintMask <img>`, `tintColor <0..2> <r g b \| entity>` | per-channel albedo tint, optionally the entity colour | any stage |
| `materialMask <img>` | R subsurface, G sheen, B hair shift, A detail weight | any stage |
| `layer <albedoRough> <normalMetal\|-> <tile> [...]`, `layerMask <img>` | up to 4 tiling material layers, uploaded as 2D-array images (`R_FindImageArray`, `vk_dds.cpp`) | any stage (CP2077 multilayer) |
| `roughnessEncoding <perceptual\|alpha\|metalpow>` | how the stage stores roughness; decoded once | any stage |
| `subsurface`, `sheen`, `hairAnisotropy`, `eye`, `toon*`, `fma*`, `deformVertexes outline` | skin, cloth, hair, eye, cel shading | characters: [characters-rendering.md](characters-rendering.md) |

- **Roughness is perceptual everywhere.** Since 2026-10-31, every shading term takes perceptual r and uses GGX
  alpha = r^2. Before that, direct light read the stored value as alpha while probes read it as r, a split rend2
  also has. `roughnessEncoding alpha` reproduces the old direct light. Full table:
  renderer-vulkan-in-xenjk.md, "Perceptual roughness".
- **Descriptor layout.** Under review SH-21 (f1098abf), every material and screen map moved into one
  push-descriptor set (`VK_KHR_push_descriptor`). The lit shaders now bind 7 sets (`VK_DESC_PHYSICAL_COUNT`,
  `global.h`). A device without push descriptors, or with fewer than 7 bound sets, runs the fast-light path
  instead.
- **Parallax.** `r_parallaxMapping 1` (default) applies rend2's parallax to `normalHeightMap` stages. `r_parallaxFast`
  skips it where the offset would be under a pixel.
- **Fixes that change how stock content looks.**
  - Vertex-lit diffuse + `$lightmap` stages are lit by the vertex colour, as rend2 does (fa4fc05b).
  - Patch poles get a tangent (6a111412, which fixed black wedges on Dantooine).
  - The `specularMap` conversion counts blue, not green twice (`r_fixSpecBlue`, latched; 0 restores the old
    conversion).
  - Two-sided back faces take the negated normal map. This and the other accepted `r_fix*` look fixes (sun PCF
    rotation, plain-stage dlight specular, skin wrap, linear tint inputs, rig side) are now unconditional; their
    switches were removed after the owner accepted them. Besides `r_fixSpecBlue`, only `r_fixFmaShadows` (off by
    default) and `r_fixG2SceneTime`/`r_fixG2SceneLod` ([characters-rendering.md](characters-rendering.md)) are
    still switches.

Content-side keyword reference and the history of each: renderer-vulkan-in-xenjk.md,
sections "Stage material features" and "Material masks, combined modes, detail/emissive/tint maps, eyes, layers".
Why these exist (what converts used to bake): converter-feature-gap.md.

### Textures: DDS/BC7 and the VRAM budget

- **DDS** (35638750; now `vk_dds.cpp`, `R_LoadDDSImage`, which `vk_image.cpp` calls before its other loaders). Every texture name, whatever its extension, is tried as
  `<name>.dds` first.
  - Formats: BC1, BC3, BC4, BC5 and BC7, with a legacy FourCC or a DX10 header. sRGB variants are read as UNORM,
    like every other texture.
  - The file is uploaded as stored, with its own mips: no decode, resample or light scale.
  - Size: not held to `MAX_TEXTURE_SIZE`, only to the device limit or `r_ddsMaxSize` (default 4096). Over that,
    top mips are dropped. Picmip drops mips the same way, and a texture over the VRAM budget drops mips through
    `vk_vram_fit` (`vk_vram.cpp`; the DDS and RGBA8 paths each pass their own byte-count function).
  - Fallback: cubemap and volume DDS files, and anything broken, go to the normal loaders.
  - Range reads: a stored (uncompressed-in-pk3) DDS is read by range through the engine's `FS_Range*` imports
    (`r_ddsRangeRead`). A DDS in the home folder (a cache the renderer wrote) is read through
    `ri.FS_ReadHomeFile`, which needs `fs_prefetchApi` 3.
  - Measured on the hero packs: BC7 at source size used 2-3x less VRAM than the old 2048 RGBA8 path.
- **Writing DDS files.** The repo's writers are `tools/characters/pack_dds_convert.py` (BC7/BC1 beside a pack's
  images, `models/players/` only), `pack_bc1.py` and `pack_layer_dds.py`. They need the Python packages `etcpak`,
  `texture2ddecoder`, `numpy` and `Pillow`. Mips must be made with the engine's 2x2 box filter, and sides must be
  powers of two. No repo tool converts world textures in bulk.
- **VRAM** (`vk_vram.cpp`: ledger, budget, `vk_vram_hook` and `vk_vram_fit`, the one search for how many top mips a
  chain must drop to fit). Every `vkAllocateMemory` goes through one ledger with one budget, read from
  `VK_EXT_memory_budget` when the device has it. `r_vramBudgetMB` emulates a smaller card and `r_vramFloorMB` keeps
  a reserve. `vkmem` prints the ledger. Character texture streaming (`vk_stream.cpp`, `r_stream*`) draws on this
  budget and is covered in [characters-rendering.md](characters-rendering.md).

### Anti-aliasing, AO, glow, bloom, dynamic lights

- **MSAA.** `r_ext_multisample 4` is the default and the project standard: alpha-to-coverage hair breaks up at 2x.
  The cvar name is shared with the SDL window code, but rd-vulkan registers it before it creates its window, so its
  default holds only in Vulkan starts.
- **Alpha to coverage.** `r_ext_alpha_to_coverage 1` (default; `vk_pipelines.cpp`) applies with MSAA to
  alpha-tested stages such as foliage, fences and hair. Mode 1 is a one-pixel coverage ramp on Ghoul2 models and
  the old coverage elsewhere, mode 2 the ramp everywhere, mode 3 the old coverage everywhere, and 0 turns it off.
- **SSAO.** `r_ssao 1` (default). Before the import: half resolution (`r_ssao_halfRes 1`) and a separable,
  depth-aware blur (`r_ssao_blurRadius 2`). Since: `r_ssaoWorld` (0 drops the world pass) and
  `r_ssaoReuseExtract`, which reuses the character pass's depth extract.
  SSAO is multiplied over the lit image, so it also darkens sun and specular light; that is a known limit.
  `r_ssao_character` and the character AO pass belong to [characters-rendering.md](characters-rendering.md).
- **Dynamic glow.** `r_DynamicGlow 1` (default; the name is shared with rd-vanilla, where it defaults to 0).
  - `r_DynamicGlowScale` sets the size of the first blur level, and `r_DynamicGlowAllStages` keeps the glow
    stages that vanilla skips.
  - Fix c38d75292: a viewport helper bypassed the backend cache, so glow was drawn at half size after half-res SSAO.
  - An `emissiveMap` with `glow` blooms through the emission alone.
- **Bloom.** `r_bloom 0` (default) and `r_hdr 0` with `r_dither 1` were chosen on look and cost.
  - `r_hdr 1` is 16-bit UNORM, for precision only.
  - `r_hdr 2` is 16-bit float, so a render scene's colour grade receives highlights unclipped
    ([levelport.md](levelport.md)).
- **Dynamic lights.** These are JKSunny's GPU per-pixel lights (`r_dlightMethod 1`, `r_dlightMode`), with the full
  material BRDF and no shadows; each pixel loops over every light. xenjk keeps them under `r_vertexLight 1` (as in
  xenjk's rd-vanilla). Other changes:
  - Linear lights honour `r_dlightSaturation` (672c7b3c).
  - Plain PBR stages get full dlight specular (it was pi times weaker; the `r_fixPlainDlightSpec` switch is gone).
  - Perceptual roughness applies to dlights too.
  - `r_dlightScale` and `r_dlightIntensity` came with the import.
  Clustered lights belong to the render scene, not to this path.

### Smoke prototype

`vk_smoke.cpp` and `shaders/glsl/smoke.frag` (4ca99d9f) hold a look-only, CS2-style smoke. The `smoke [seconds [x y
z]]` console command drops a smoke where the view points. The volume floods outward over 16-unit cells
(`r_smokeCells` budget) with collision traces, is stored as a small RGBA8 atlas, and is ray-marched in the post 1x
pass against the depth extract. That pass exists only with MSAA, and the depth extract only with `r_ssao 1`.
`smoke_clear` removes all smoke. It has no gameplay, network or bot effect.
Not measured at 4K. Cvars: `r_smoke` (1: draws once spawned), `r_smokeDensity`, `r_smokeLife`, `r_smokeGrow`,
`r_smokeFade`, `r_smokeLight`.

### Map load and pipelines

- **Load profiling.** `r_loadProfile 1` and the `loadprofile` command time the load phases: world, light
  directions, probe setup, VBOs, probe bake, images and pipelines. Values above 1 add per-shader and layer-array
  times. On the bench maps the cold load was mostly disk. The probe bake is under 1.5 s and pipelines under 1 s;
  the warm residual is image decode ([renderer-map-load.md](../renderer-map-load.md)).
- **Pipeline cache.** `r_pipelineCache 1` keeps `cache/vk_pipelines.bin` in the home folder between launches
  (c51ba3c4). A first-time pipeline costs 100-200 ms; from the cache it costs 0.5 ms.
- **Robustness.** Overflow descriptor pools open once the first one is full (04db99fa), and maps with tens of
  thousands of visible surfaces no longer crash (6da89d83).
- **Elsewhere.** Off-thread pipelines, mesh jobs, tangent caches and model warm-up serve player-model loads; they
  are in [characters-rendering.md](characters-rendering.md).

### Video capture

`r_videoAsync 1` (`vk_video.cpp`) copies each frame into a host-visible buffer ring and reads it one frame later
instead of idling the GPU. The engine's `client/cl_video.cpp` pipes the frames to ffmpeg
(`cl_videoCodec h264|hevc|av1|ffv1|prores|avi`, `cl_videoGPU`, `cl_videoQuality`, `cl_videoFFmpeg`). With
`r_renderScale`, the recording is made at the render size rather than the window size. Engine and renderer
negotiate through the `CL_SetVideoCaptureHooks` import (below). Details:
renderer-vulkan-in-xenjk.md, "Video recording".

### Renderer selection and fallback (engine)

`codemp/client/cl_renderer_select.cpp`:
- **Choice.** A client binary named `xenjk.*` starts the renderer named in `xenjk_renderer` (archived, default
  `rd-vulkan`), not the one the configs' `cl_renderer` names. `+set cl_renderer` on the command line still wins.
  An `openjk.*` binary keeps stock behaviour.
- **Probe.** `CL_VK_Probe` loads the Vulkan loader and looks for an instance with surface extensions and a device
  with a graphics queue and `VK_KHR_swapchain`. If any is missing, the client starts rd-vanilla.
- **Crash fallback.** A start that dies inside Vulkan initialisation leaves `xenjk-renderer-start.txt` in the home
  folder. The next launch then uses rd-vanilla and sets `xenjk_renderer` to it.

## Take this piece

**Depends on:** an OpenJK tree with SDL2 windows (any OpenJK derivative). Nothing else in the guide.
[levelport.md](levelport.md), [characters-rendering.md](characters-rendering.md), [night-city.md](night-city.md) and
[beauclair.md](beauclair.md) build on this chapter.

### What lives where

- **Here:** the renderer core, the build, shaders, the API, sun, probes, materials, DDS, VRAM, MSAA/A2C, SSAO,
  glow, bloom, dlights, smoke, video and the bench tools.
- [characters-rendering.md](characters-rendering.md):
  - cvars: `r_char*`, `r_hair*`, `r_cloth*`, `r_g2*`, `r_stage*`, `r_portrait*`, `r_stream*`, `r_sim*`
  - code: `vk_hair.cpp`, `vk_cloth.cpp`, `shaders/glsl/cloth_*`, `vk_stream.cpp`, `vk_model_warm.cpp`,
    `vk_mesh_jobs.cpp`, `vk_pipeline_async.cpp`, `vk_tangent_cache.cpp`, `vk_upload.cpp`,
    `rd-common/tr_g2blend.*`, `tr_g2joints.*`, `tr_hair*`, `tr_cloth*`, `hairchains/`, `cloth/`
  - topics: character shadows, occlusion, AO and contact shadows; the skin, hair, sheen, eye, toon and FMA
    material modes; the texture pool and player loading
- [levelport.md](levelport.md):
  - code: the render scene `.xscn` (`vk_scene*.cpp`, `vk_scene_local.h`, `shaders/glsl/scene/`, `scene_shader_data.c`)
  - cvars: `r_scene*`, `r_sceneVrs` (VRS is scene-only), `r_sceneGpuDriven`
  - topics: GPU-driven draws, clustered lights, the colour grade, projected decals `.xdcl`, particles `.xfx`,
    haze and rain
- [night-city.md](night-city.md) and [beauclair.md](beauclair.md): the game-specific shading (CP2077 screens,
  moon and exposure; W3 terrain, SpeedTree canopy, water).
- xenui (RmlUi menus): `rd-vulkan/tr_xenui.cpp`, `rd-common/xenui_render.h`, `shaders/glsl/xenui_*`,
  `xenui_shader_data.c`; see [xenui-agents.md](../xenui-agents.md).

### Files: the renderer (copy whole)

| path | role |
| --- | --- |
| `codemp/rd-vulkan/` | the renderer; one DLL. Everything under it, including the vendored Vulkan headers (`vulkan/`) and `utils/` (stb_image, stb_dxt, mikktspace) |
| `codemp/rd-vulkan/CMakeLists.txt` | target `rd-vulkan_${Architecture}`, compiled with `USE_OPENJK`; lists every source, and the `rd-common` files it compiles in |
| `codemp/rd-vulkan/tr_init.cpp` | `R_Register` (every cvar and its default), `GetRefAPI` (the import handshake below) |
| `codemp/rd-vulkan/tr_shader.cpp` | `q3gl2_sun`, sidecar lookup, every stage keyword |
| `codemp/rd-vulkan/tr_main.cpp`, `tr_backend.cpp` | sun cascade views, the frame order (`vk_one_resolve_pass`, `RB_RenderSunShadowCascades`) |
| `codemp/rd-vulkan/vk_sunshadow.cpp` | cascade images, passes, the far and middle cascade cache; the character-shadow atlas layer |
| `codemp/rd-vulkan/vk_cubemap.cpp`, `tr_bsp.cpp` | `vk_cubemap.cpp`: probe prefilter, blend sets and box fitting (`R_FitCubemapBox`); `tr_bsp.cpp`: probe entities, `env.json`, the bake (`R_RenderAllCubemaps`) |
| `codemp/rd-vulkan/vk_dds.cpp` | DDS loader (`R_LoadDDSImage`), layer arrays (`R_FindImageArray`) |
| `codemp/rd-vulkan/vk_image.cpp`, `tr_image.cpp` | image upload, mip generation, the loaders `vk_dds.cpp` is called from |
| `codemp/rd-vulkan/vk_vram.cpp` | VRAM ledger, budget and `vk_vram_fit` |
| `codemp/rd-vulkan/vk_ssao.cpp`, `vk_dynamic_glow.cpp`, `vk_bloom.cpp` | post effects |
| `codemp/rd-vulkan/vk_smoke.cpp` | smoke prototype |
| `codemp/rd-vulkan/vk_video.cpp` | deferred frame readback |
| `codemp/rd-vulkan/vk_pipelines.cpp`, `vk_shaders.cpp`, `vk_shade_geometry.cpp` | pipeline defs and specialization constants, shader modules, descriptor binding |
| `codemp/rd-vulkan/shaders/glsl/` | GLSL sources; `gen_vert.tmpl`/`gen_frag.tmpl` are the generic stage shaders, compiled into many variants with `-D` defines; `global.h` is shared by C and GLSL |
| `codemp/rd-vulkan/shaders/spirv/shader_data.c`, `shader_binding.c` | **generated** SPIR-V arrays and their binding table (do not edit) |
| `codemp/rd-vulkan/shaders/spirv/scene_shader_data.c`, `cloth_shader_data.c`, `xenui_shader_data.c` | **generated**, one per tool below |

### Files: hooks outside the renderer

| path | hook |
| --- | --- |
| `codemp/rd-common/tr_public.h` | appended `refimport_t` entries (Vulkan window, video hooks, pk3 prefetch, range reads and `FS_ReadHomeFile`) and `refexport_t` entries (`WarmModel`, `WarmState`, `WarmModelEx`); `REF_API_VERSION` stays 9 |
| `codemp/rd-common/tr_types.h` | `RF_G2MINLOD`, `RDF_STAGE`, `G2_TIME_EXACT`, `WARMMODEL_OTHER_PLAYER`, skin 1280 surfaces |
| `codemp/qcommon/qfiles.h` | `SHADER_MAX_VERTEXES` 10000 (stock 1000) |
| `codemp/client/cl_main.cpp` | `CL_InitRef`: fills the appended `ri.*` entries and sets the ROM cvars `win_vulkan`, `cl_videoCaptureHooks`, `fs_prefetchApi` (3) |
| `codemp/client/cl_renderer_select.cpp` | new file: `xenjk_renderer`, `CL_VK_Probe`, crash fallback |
| `shared/sdl/sdl_window.cpp`, `shared/sys/sys_public.h` | `GRAPHICS_API_VULKAN` window (`SDL_WINDOW_VULKAN`); `WIN_VK_IsMinimized`, `WIN_VK_GetInstanceProcAddress`, `WIN_VK_createSurfaceImpl` (`SDL_Vulkan_CreateSurface`), `WIN_VK_destroyWindow` (= `WIN_Shutdown`) |
| `shared/sys/sys_main.cpp`, `codemp/qcommon/common.cpp` | `Sys_BinaryName`, `Com_CommandLineSets` (for the branded-binary rules) |
| `codemp/qcommon/files.cpp` | `FS_PrefetchDir`, `FS_PrefetchDone`, `FS_PrefetchDirEx`, `FS_Range*` (stored pk3 entries read by range), `FS_ReadHomeFile` (a file under the home folder only, never a pk3) |
| `codemp/client/cl_avi.cpp`, `cl_video.cpp` | `CL_SetVideoCaptureHooks`; the ffmpeg sink |
| `codemp/client/cl_cgameapi.cpp`, `cl_uiapi.cpp` | pass `WarmModelEx`/`WarmState` to cgame/ui as `ext.R_WarmModel`/`R_WarmState` when `r_modelWarmupApi >= 2` |
| `codemp/rd-vanilla/tr_init.cpp`, `codemp/rd-dedicated/tr_init.cpp` | copy `refimport_t` only up to `VK_IsMinimized`, so they ignore the appended entries |
| `CMakeLists.txt`, `codemp/CMakeLists.txt` | `BuildMPRdVulkan` (default ON), `MPVulkanRenderer`, the `xenjk.<arch>` copy of the client |

### The API handshake

`tr_public.h` gained entries only at the ends of both tables, and `REF_API_VERSION` stays 9. An older renderer
therefore runs on the new engine, and the new renderer checks what the engine has before it reads past the stock
table:

```
engine (CL_InitRef)                         renderer (GetRefAPI, tr_init.cpp)
  Cvar win_vulkan 1 (ROM)          ------>    no win_vulkan or a NULL VK_* entry: print and return NULL
  ri.VK_IsMinimized ... VK_destroyWindow      (the engine then falls back to another renderer)
  Cvar cl_videoCaptureHooks 1      ------>    ri.CL_SetVideoCaptureHooks( &videoHooks )
  Cvar fs_prefetchApi 3            ------>    FS_PrefetchDir/Done (>=1), FS_PrefetchDirEx + FS_Range* (>=2), FS_ReadHomeFile (>=3)
                                   <------    Cvar r_modelWarmupApi 2 (ROM): refexport WarmModel/WarmState/WarmModelEx
                                   <------    Cvar r_xenUIRenderApi 2 (ROM) + exported XenUI_GetRenderAPI
```

The Vulkan handles are opaque (`void *instance`, `void **surface`), so `tr_public.h` needs no Vulkan header.
`GetRefAPI` is the only export at link level. `XenUI_GetRenderAPI` (`tr_xenui.cpp`) is a second, optional export
that the UI module looks up by name.

### Build inputs

- **CMake.** `-DBuildMPRdVulkan=ON` (the default). The target is `rd-vulkan_x86` (Win32) or `rd-vulkan_x86_64`
  (x64); `${Architecture}` comes from the top-level `CMakeLists.txt`. No Vulkan SDK is needed: the headers are in
  `codemp/rd-vulkan/vulkan/`, the SPIR-V is committed as C arrays, and the runtime loader (`vulkan-1.dll`) comes
  with the GPU driver.
- **Libraries.** The same as rd-vanilla: libjpeg, libpng, zlib and minizip from `lib/`. The 64-bit client needs
  the x64 `SDL2.dll`, which is in `lib/SDL2/bin/x64`.
- **Layout.** The installed game keeps the 32-bit client and renderer in the game folder and the 64-bit client,
  renderer and SDL2 in `bin64/`. The `Play_*.bat` launchers start `bin64\xenjk.x86_64.exe` with `+set fs_basepath`
  pointing at the game folder, so both clients share `base/` and its game modules (`cgamex86.dll` /
  `cgamex86_64.dll` and the others). `bin64/` is an install layout; CMake's `install` does not create it.
- **Shaders.** Rebuild them only when you edit GLSL. Each tool rewrites one C file:

| tool | sources | writes | compiler |
| --- | --- | --- | --- |
| `tools/renderer_spirv.py build --tree codemp/rd-vulkan` | `shaders/glsl/*` and the `*.tmpl` variants (skips `cloth_*`, `xenui_*`, `scene/`) | `shader_data.c`, `shader_binding.c` | Khronos glslang (`GLSLANG_EXE`), or `--compiler godot` (`GODOT_EXE`; no geometry shaders) |
| `tools/renderer_spirv.py verify --tree codemp/rd-vulkan` | same | nothing; exit 1 on a difference | same |
| `tools/scene_spirv.py [--tree ...]` | `shaders/glsl/scene/*` | `scene_shader_data.c` | glslang |
| `tools/cloth_spirv.py [--tree ...]`, `--check`, `--stamp` | `cloth_layout.h`, `cloth_step.comp`, `cloth_draw.comp` | `cloth_shader_data.c` (with a source hash) | Godot 4's embedded glslang (`GODOT_EXE`) |
| `python -I tools/xenui_spirv.py [--tree ...]` | `shaders/glsl/xenui_*` | `xenui_shader_data.c` | glslang |

  `renderer_spirv.py` builds the same task list, names and order as JKSunny's `shaders/tools/compile_threaded.cpp`
  (the sources of JKSunny's `bin2hex`/`bindshader` tools are in `shaders/tools/`, and `shaders/compile.bat` is the
  Vulkan SDK route). `build` keeps the committed bytes of every shader that compiles equal except for the header
  words, so only edited shaders change. Seven shaders differ by glslang version (lightgrid, SSAO, surface sprites);
  keep their committed bytes unless you edited them.

### Cvars and commands

Every added cvar, with its default and description, is in [xenjk-cvars.md](../xenjk-cvars.md). These are the ones
that matter for this chapter:

| cvar | default | what |
| --- | --- | --- |
| `xenjk_renderer` | `rd-vulkan` | the branded client's renderer (engine) |
| `r_sunlightMode`, `r_sunShadows` | `2`, `1` | sun as a primary light, `q3gl2_sun` scales on |
| `r_shadowMapSize`, `r_shadowFilter`, `r_shadowCascadeZFar` | `2048`, `2`, `4096` | cascade size, 9 rotated taps, reach |
| `r_sunShadowFarCache`, `r_sunShadowMidCache` | `1`, `1` | cached outer cascades |
| `r_cubeMapping`, `r_cubeMapParallax`, `r_cubeMapBlend`, `r_cubeMapRoughCut`, `r_cubeMapDiffuse` | all `1` | probes, box projection, 8-probe blend, rough cut, irradiance ambient |
| `r_specularAA`, `r_specularOcclusion` | `1`, `1` | |
| `r_pbrMaterials` | `1023` | material keyword bits (an archived `15` keeps the newer ones off) |
| `r_parallaxMapping`, `r_depthPrepass` | `1`, `1` | |
| `r_ext_multisample`, `r_ext_alpha_to_coverage` | `4`, `1` | |
| `r_ssao`, `r_ssao_halfRes`, `r_ssaoWorld` | `1`, `1`, `1` | |
| `r_DynamicGlow`, `r_bloom`, `r_hdr`, `r_dither` | `1`, `0`, `0`, `1` | |
| `r_ddsTextures`, `r_ddsMaxSize` | `1`, `4096` | |
| `r_vramBudgetMB`, `r_pipelineCache` | `0` (driver), `1` | |
| `r_maxFrameLatency`, `r_presentWait`, `r_swapchainImages` | `1`, `0`, `0` | latency |
| `r_gpuTimes`, `r_cpuTimes`, `r_loadProfile` | `0` | measurement |

Commands: `loadprofile`, `vkmem`, `vkinfo`, `gfxinfo`, `imagelist`, `smoke`, `smoke_clear`.

### Data formats

- **Shaders.** `.shader` plus the stage keywords above; `.mtr` only where it replaces a `.shader` of the same name.
  The keyword reference is renderer-vulkan-in-xenjk.md.
- **Textures.** `<name>.dds`: BC1/3/4/5/7, stored mips, power-of-two sides. The PBR sidecars `<albedo>_n`, `_nh`,
  `_orm`, `_orms`, `_rmo`, `_rmos`, `_moxr` and `_mosr` take any image format.
- **Map entities.** `misc_cubemap` with `radius`, or `cubemaps/<map>/env.json` (JKSunny's format, `R_LoadEnvironmentJson`), which is tried first.
- **Caches** in the home folder, rebuilt when missing: `cache/vk_pipelines.bin`, and `cache/tangents/*.xtan`
  (described in [characters-rendering.md](characters-rendering.md)).

### Porting notes

- **Plain OpenJK master.** `refexport_t` matches. Port the appended `tr_public.h` entries, then the engine table in
  `CL_InitRef` and the ROM cvars, then `sdl_window.cpp`'s Vulkan window, then `cl_renderer_select.cpp` (optional:
  without it, `cl_renderer rd-vulkan` selects the renderer). The prefetch, range-read, home-read and video entries are
  optional, because the renderer checks for each one. Without `FS_Range*`, DDS files are read whole; without
  `FS_ReadHomeFile`, the renderer's home-folder caches are not read back.
- **rd-common.** rd-vulkan's CMake compiles xenjk's `rd-common` hair, cloth, G2 blend and G2 joint sources into the
  DLL, and `tr_ghoul2.cpp`, `tr_main.cpp` and `tr_backend.cpp` call them. Copy them too; stubbing them out means
  editing those call sites.
- **EternalJK or JKSunny's own fork.** It already has an rd-vulkan. Do not copy the tree. Take xenjk's commits as
  diffs: `git log e4fe75812.. -- codemp/rd-vulkan`, filtered by topic (sun, cubemap, DDS, a2c and so on). The header
  differences in "Where the code came from" apply in reverse: EternalJK's `RF_FULLBRIGHT` and skin and vertex sizes.
- **rd-vanilla or rend2 trees.** No part of this chapter applies to GL renderers, except that the `q3gl2_sun` and
  `.mtr` content is rend2's own format, so maps lit for rend2 look the same in both.
- **32 vs 64 bit.** Both build from one commit. x64 gives two more warnings, from `stb_dxt.h`'s `MIN`/`MAX`, which
  `vk_image.cpp` guards. A 64-bit client needs 64-bit cgame/ui/game modules and an x64 SDL2.
- **Device requirements.**
  - Vulkan 1.0, a graphics queue and `VK_KHR_swapchain` (probed before load).
  - `depthClamp` for sun shadows.
  - `VK_KHR_push_descriptor` and 7 bound sets for the lit PBR path; otherwise the fast-light fallback.
  - Optional: `VK_KHR_present_wait` and `VK_EXT_memory_budget`.
  - The render scene needs more; see [levelport.md](levelport.md).
- **Tools assume xenjk's cgame.** The scene bench times views with xenjk's cgame `benchview` command, which it
  stages for every engine it tests.

## Do it yourself

### Build rd-vulkan

```
# 32-bit (from an empty build folder)
cmake -G "Visual Studio 17 2022" -A Win32 -DBuildMPRdVulkan=ON <xenjk checkout>
cmake --build . --config Release --target rd-vulkan_x86 openjk.x86
# 64-bit
cmake -G "Visual Studio 17 2022" -A x64 -DBuildMPRdVulkan=ON <xenjk checkout>
cmake --build . --config Release --target rd-vulkan_x86_64 openjk.x86_64
```

Copy `rd-vulkan_<arch>.dll` next to the client exe built from the same tree (`xenjk.<arch>.exe` is written beside
`openjk.<arch>.exe`). To try a development build against an installed client, start the client with
`+set cl_renderer <path to the DLL without _<arch>.dll>`; the bench candidates do it this way.

### Change a shader

1. Get Khronos glslang (a release zip from the KhronosGroup/glslang GitHub page) and put `glslang` or
   `glslangValidator` on `PATH`, or set `GLSLANG_EXE` to `bin/glslang.exe`.
2. Edit the GLSL under `codemp/rd-vulkan/shaders/glsl/`.
3. `python tools/renderer_spirv.py build --tree codemp/rd-vulkan`. `--tree` defaults to this checkout's
   `codemp/rd-vulkan`; pass it when you work on another tree.
4. `git diff --stat codemp/rd-vulkan/shaders/spirv/`. Arrays that changed only in the SPIR-V header word, and the
   seven glslang-version shaders, should be restored to their committed bytes, so that the diff shows only your
   shader.
5. Then run the tool for the family you touched: `scene_spirv.py` for `scene/`, `cloth_spirv.py` (with
   `GODOT_EXE`) for `cloth_*`, `xenui_spirv.py` for `xenui_*`. `cloth_spirv.py --check` fails when the cloth arrays
   are stale.
6. Rebuild the DLL. The C side and `global.h` must agree on descriptor slots and specialization constant ids
   (`vk_pipelines.cpp`).

### Bring it into another OpenJK tree

1. Copy `codemp/rd-vulkan/` and the `rd-common` files its `CMakeLists.txt` lists, and add
   `add_subdirectory("${MPDir}/rd-vulkan")` behind a `BuildMPRdVulkan` option. Add `MPVulkanRenderer` to the binary
   names in the top-level `CMakeLists.txt`.
2. Merge the `tr_public.h` and `tr_types.h` additions (the hooks table above), keeping every new entry at the end of
   its struct. Make the tree's other renderers copy `refimport_t` up to `VK_IsMinimized`, or rebuild them against
   the new header.
3. Engine: the `WIN_VK_*` functions and the `GRAPHICS_API_VULKAN` window in `sdl_window.cpp`, and the `ri.VK_*`
   assignments with `Cvar_Get("win_vulkan", "1", CVAR_ROM, ...)` in `CL_InitRef`. This is the minimum.
4. Optional: `cl_renderer_select.cpp` (default renderer and fallback), the `FS_Prefetch*`/`FS_Range*`/`FS_ReadHomeFile`
   reads in `files.cpp`, and the video hooks.
5. Build, then start with `+set cl_renderer rd-vulkan`. The console prints the device, `MSAA ... using 4x` and
   `sun shadow cascades: on (3 x 2048)` on a map whose sky has a sun.

### Environment

The variables this chapter's tools read; each is optional.

| variable | read by | meaning |
| --- | --- | --- |
| `GLSLANG_EXE` | `renderer_spirv.py` (and so `scene_spirv.py`, `xenui_spirv.py`) | glslang binary; else `glslang` / `glslangValidator` on `PATH` |
| `GODOT_EXE` | `renderer_spirv.py --compiler godot`, `cloth_spirv.py` | Godot 4 binary; else `godot` on `PATH`; required for the cloth shaders |
| `PRESENTMON` | `renderer_bench.py` | PresentMon 2.x CLI, tried before the usual install folders |
| `SCENE_CONTENT_SOURCE` | `renderer_scenes.py` | folder of scene packs the scene bench stages, over `scenes.json` |
| `JKSUNNY_REPO` | `cvar_list.py` | a JKSunny checkout, to tell import-time cvars from later ones |

### Measure a change

The bench tools are offline and private: each run gets its own bin and home folder, and runs with
`net_enabled 0`, muted (`s_volume 0`, `s_mute 1`). They read your installed game, linked into the checkout at
`game_directory/Jedi Academy/GameData`, as `fs_basepath`, and write under `.codex-research/renderer-bench/`.
Candidates (tree, exe, renderer, forced cvars) are in `tools/fixtures/renderer_bench/candidates.json`; maps, views
and setting profiles are in `scenes.json`. Timing runs one client at a time. Protocol and caveats:
[renderer-bench.md](../renderer-bench.md).

```
python tools/renderer_bench.py list
python tools/renderer_bench.py build [candidate ...]
python tools/renderer_bench.py run --demo kejim-runner --candidates a,b [--scenarios default,max] [--reps 3]
python tools/renderer_scenes.py run --maps siege_urban_pbr --candidates a,b --profile best,gputimers [--reps 1]
python tools/renderer_gpu_passes.py <scene run dir> --candidate <name>     # per-pass GPU ms (r_gpuTimes 1)
python tools/renderer_parity.py --map siege_urban_pbr --out <dir> --ref <run>:<cand> --test <run>:<cand>
python tools/renderer_latency.py run --candidates a,b --map siege_urban_pbr --profile best --reps 3
```

- `renderer_bench.py` times a demo with `timedemo 1`. It uses `com_speeds` and PresentMon when PresentMon can run
  (that needs an elevated shell or the Performance Log Users group; `presentmon-check` tests it).
- `renderer_scenes.py` times fixed views at 4K with the cgame's `benchview` and takes screenshots. It takes over
  the main monitor. The `loadprof` profile adds `r_loadProfile`.
- `renderer_parity.py` writes side-by-side pairs, a crop of where two runs differ most, and luma statistics.
- `renderer_latency.py` measures mouse-to-desktop latency with `latprobe.exe` (SendInput plus DXGI Desktop
  Duplication). Build the probe first with `tools/latency/build_latprobe.cmd` (MSVC x64); it lands in
  `.codex-research/latency/bin/`. Do not touch the mouse during a run.
- Inside the game: `r_gpuTimes 1` prints GPU milliseconds per pass, `r_cpuTimes 1` the CPU split, and
  `loadprofile` the load phases. GPU timings on a shared GPU swing about 20% between launches; interleave the
  baseline between variants.

## Limits and open issues

- SSAO multiplies the lit image, so it darkens sun and specular light too; no AO on ambient light only.
- Dynamic lights have no shadows and are not culled per tile. Each pixel loops over every light on the base path.
- There is no tone mapping and no temporal AA, by choice: 8-bit output with dither, and bloom off.
- Only xenjk's packs place reflection probes. Stock maps get spawn-point probes, which barely show on materials
  without PBR maps. There are at most 63 probes.
- The cold map load is dominated by disk reads. Image decode on worker threads and a compressed-texture cache are
  proposed in [renderer-map-load.md](../renderer-map-load.md), not built.
- The smoke prototype has no gameplay side (line of sight, clearing, fairness on GL renderers), and its cost at 4K
  is not measured.
- There is no bulk DDS cooker for world textures; the repo's writers cover character packs.
- `renderer_spirv.py` finds glslang through `GLSLANG_EXE` or `PATH`, and Godot through `GODOT_EXE` or `PATH`. It
  reports a missing tool rather than guessing a path.
- The Godot fallback cannot compile geometry shaders (`filtercube.geom` keeps its committed bytes on that path), and
  cloth shaders compile only through Godot.

## Further reading

- renderer-vulkan-in-xenjk.md: the port, API/ABI table, defaults, and every install
  since (newest first); the material keyword reference lives in its 2026-10-13/14 sections.
- [renderer-bench.md](../renderer-bench.md): the bench protocol and the perf/vulkan work before the import (latency,
  one resolve, SSAO, sun parity, styles), with numbers.
- [renderer-vulkan-sun-port.md](../renderer-vulkan-sun-port.md): how rend2's sun and cascades were mapped onto
  rd-vulkan, stage by stage.
- [renderer-vulkan-sun-shadow-investigation.md](../renderer-vulkan-sun-shadow-investigation.md): the cascade scissor
  and last-cascade fixes.
- [renderer-map-load.md](../renderer-map-load.md): where map load time goes, and the options.
- [renderer-character-shadows.md](../renderer-character-shadows.md): the off-sun character shadows built on this
  renderer's lights.
- converter-feature-gap.md: which material features converts needed at runtime, and why
  the keywords exist.
- [xenjk-cvars.md](../xenjk-cvars.md): every cvar since the import, generated from source.
- [THEORA.md](../THEORA.md): the Theora video path (`RE_StretchRaw` with NPOT frames is ported to rd-vulkan).
- [libraries.md](../libraries.md): how the client loads the renderer and game libraries (`GetRefAPI`, module APIs).
