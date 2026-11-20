# Plan: rend2's sun in JKSunny's Vulkan PBR renderer (2026-10-08)

Owner order: after the JKSunny speed fixes (one MSAA resolve with post-processing at 1x, SSAO cost, bloom and HDR
settings). Measurement background: [renderer-bench.md](renderer-bench.md).

- **Reference:** rend2, `.codex-research/renderer-bench/src/somaz-prof/shared/rd-rend2` (branch `perf/rend2`),
  written R below.
- **Target:** JKSunny, `.codex-research/renderer-bench/src/jksunny-perf/codemp/rd-vulkan` (branch `perf/vulkan`),
  written V below.
- Line numbers are from the 2026-10-08 trees.

## What rend2 does

- **Sun definition:**
  - `q3gl2_sun r g b intensity yaw elevation mapLightScale ambientScale`, in the sky material (R tr_shader.cpp
    2631-2688).
  - JKSunny rejects the whole material on that keyword (V tr_shader.cpp 2968); the bench rewrites it to
    `q3map_sunExt`.
- **Scene values** (R tr_scene.cpp 355-390):
  - `colorScale = mapLightScale` scales every world stage colour and the sky.
  - Mode 2: `sunCol = sunLight * 2^(r_mapOverBrightBits - overbrightBits - 8)`, `sunAmbCol = sunCol * shadowScale`.
  - Mode 1: `sunCol = 1`, `sunAmbCol = shadowScale`.
- **Cascades** (R tr_main.cpp 2683-3066):
  - 3 splits.
  - Orthographic, sphere-bounded boxes along `-sunDir`.
  - Depth clamp.
  - Per-cascade texel snapping (our fix).
  - Casters are opaque, non-post-render surfaces, with culling flipped and no depth bias. Third-person models cast.
- **Shading** (R lightall.glsl):
  - `sunShadow()`/`PCF()` at 524-620: hardware compare with LINEAR filtering; `r_shadowFilter 2` = rotated 9 taps.
  - The sun term at 1159-1170 and 1250-1265: mode 1 modulates the lightmap, mode 2 adds a primary light.
  - Normal bias `vertexNormal * (1 - NPL)`.
- **Cost measured at 4K with 4x MSAA:** cascade rendering 0.08 ms; sun shading plus PCF about 0.8 ms.

## How JKSunny can render the cascades

- `R_RenderView` has no orthographic or depth-only view, and `R_GenerateDrawSurfs` overwrites the projection
  (V tr_main.cpp 1598-1672).
- Cubemap capture renders extra views only at load, by ending the main pass; that is too expensive per frame.
- The usable pattern is the screenmap pre-pass. `vk_begin_frame` scans the command list before the main pass
  begins (V vk_frame.cpp 573-578, `vk_find_screenmap_drawsurfs`). The 3 depth passes are recorded there, outside the
  main pass, and `RB_DrawSurfs` skips the cascade commands.

## Progress

- **2026-10-09:** (a) and (b) on `perf/vulkan` cf5aec729 (results in [renderer-bench.md](renderer-bench.md)).
  - Differences from the plan: the mode is read from the camera UBO (`sunDir.w`), not a specialization constant;
    it is one uniform branch with no new shader variants. rend2's `r_depthPrepass` condition is taken as true.
  - Left over from (b): `colorScale` is computed but not yet applied to stage colours and the sky.
  - The sun term costs 0.01 ms GPU at 4K.
- **2026-10-09:** the map light scale (e2a371813), then (c) and (d) (13ed3f18f).
  - Cascades: built before the main view (matrices in the refdef), their draw lists after it, drawn by the backend at
    frame start before the main pass. Casters: opaque world surfaces, brush models and third-person models
    (`r_sunShadowModels`); camera PVS; no point-based face culling (the pipeline flips culling, as rend2's
    orthographic cull does). One set of cascades per frame.
  - Pipelines: `RENDER_PASS_SUN_SHADOW` variants, vertex-only unless the stage alpha-tests, first stage only.
  - Sampling: 11th descriptor set, rend2's `sunShadow()`/`PCF()` with the matrices the cascades were drawn with
    (Vulkan clip space). `r_shadowFilter`, `r_shadowMapSize`, `r_shadowCascadeZNear/ZFar/ZBias` as rend2.
  - Cost at 4K with 2048 maps: cascade pass 0.09 ms, lit pass +0.11 ms.
  - Not yet: (e) the sun on models (their lighting paths skip the sun block), (f) defaults, (g) parity pairs on all
    maps; Ghoul2 bones are recomputed per cascade view (the per-frame cache of (e)).
- **2026-10-09 (R2):** (e) per-frame Ghoul2 bone cache (c659f743f); lightmapped misc_models with an `rgbGen vertex`
  diffuse now collapse with their lightmap stage as in rend2, which fixed most of the model brightness gap
  (8b679a8e5); `colorScale` on unlit stages and the sky (8b679a8e5). Left: the police car's remaining gap, (f), (g).
  Details and numbers in [renderer-bench.md](renderer-bench.md).
- **2026-10-09 (R4):** the owner's "urban shadows softer and fainter" was not in the shadow inputs (cascades,
  bias, sampler, depth range, sun colour and overbright are identical; the shadow-value views match). rend2 turns each
  lightmap style stage into a lit copy of the stage, so a surface with N styles gets the sun N+1 times; urban's street
  carries 3 styles. `r_sunStylePasses` (default 1) copies that (b24362a11). Also: cascades cull faces by the light
  direction (rend2's orthographic cull), `r_depthPrepass` on by default (4b2a01cd2). Remaining for (g): the police car
  (rend2 draws it without its lightmap), the hover cart's shadow, Dantooine's black wedges. Details in
  [renderer-bench.md](renderer-bench.md).

## Stages

Each stage builds, is behind a cvar that defaults off, and is checked before the next.

**(a) Parse and data (0.5 day, ~120 lines)**
- V tr_shader.cpp 2788: accept `q3gl2_sun` with both extra values.
- Add `tr.mapLightScale`, `tr.sunShadowScale` and `tr.sunShadows`, reset at map load.
- Add rend2's cvars with rend2's names and LATCH flags.
- Fill `refdef` sun values in `RE_BeginScene`.
- After this the bench's material rewrite is no longer needed.

**(b) Sun term without shadows (1 day, ~150 lines)**
- Extend the camera UBO (`vkUniformCamera_t`, global.h): sun direction (w = mode), colour, ambient
  (w = colorScale), `shadowMvp[3]`, shadow parameters. This is a size change only, no new layout.
- gen_frag.tmpl: port the mode 1/2 block, chosen by a specialization constant (cvars are latched, so no variant
  explosion).
- Apply `colorScale` to stage colours and the sky.
- Check: screenshot pairs against rend2 with `r_sunShadows 0` on both.

**(c) Shadow maps and cascade rendering (4.5 days, ~650 lines)**
- Image: D16 2D array, 3 layers; per-layer framebuffer views plus an array view for sampling.
- A depth-only render pass and three framebuffers. `VK_Pipeline_t.handle[RENDER_PASS_COUNT]` grows by one.
- Pipelines: no colour attachment, depth clamp (enable `features.depthClamp`), inverted cull, an alpha-test variant.
- Front end:
  - `R_RenderSunShadowViews`, ported from R tr_main.cpp 2916-3066.
  - An orthographic projection with Vulkan z in [0,1].
  - `viewParms.sunCascade` guards far clip, `R_MarkLeaves`, mirrors and non-opaque surfaces.
  - Called once per frame from `RE_RenderScene` for world scenes.
- Backend: `vk_render_sun_cascades()` in the screenmap pre-pass.
- Risks: projection convention (Y flip, z range), so add a debug tint cvar; overflow of the draw-surface pool.

**(d) Sampling and PCF (1.5 days, ~200 lines)**
- Comparison sampler (LESS_OR_EQUAL, LINEAR, clamp).
- `sampler2DArrayShadow`; port `PCF()`/`sunShadow()` verbatim.
- Descriptor: an 11th set (simple; gate on `maxBoundDescriptorSets >= 11`) or merged into the BRDF LUT set
  (works on 8-set devices, more code).

**(e) Models lit and casting (1.5 days, ~120 lines)**
- The light-vector path gets the sun and shadow block.
- Third-person MD3/Ghoul2 models cast; first-person and depth-hack models don't.
- Cache Ghoul2 bones per frame, otherwise the bone cost is about 4x.

**(f) Cvars and defaults (0.5 day):** rend2 names and semantics, plus `r_sunShadowDebug`. Defaults stay off until
(g) passes.

**(g) Parity (1 day)**
- Scene bench screenshot pairs against rend2: sun term only, then with shadows, then with the cascade tint.
- GPU timers (`r_gpuTimes`) targets: shadow pass ≤ 0.15 ms, lit-pass increase ≤ 1 ms.
- Latency probe unchanged.

**Total:** about 10.5 working days, about 1,300 lines plus a shader rebuild.

## Owner answers (2026-10-09)

The recommendations were accepted:
1. Copy rend2's quirks for parity.
2. Cubemap capture gets the sun term without shadows.
3. Use an 11th descriptor set now; merge into the BRDF set later only if 8-set GPUs matter.
4. Cache Ghoul2 bones per frame within stage (e), so the ~4x cost is temporary.
5. Require depth clamp; without it there are no shadows.
6. Sky-portal and portal views get sun shading, as in rend2.

## Owner questions (as asked)

1. Copy rend2's quirks for parity? These are the specular term using `dot(E, H)`, the sun added on top of the light
   grid for entities, and `mapLightScale` applied to all world stages.
2. Cubemap capture: sun without shadows, or no sun?
3. An 11th descriptor set (simpler) or merging into the BRDF set (works on more GPUs)?
4. Is a temporary ~4x Ghoul2 bone cost acceptable until the per-frame cache lands?
5. Require depth clamp, with no shadows as the fallback?
6. Sun shading in sky-portal and portal views? rend2: yes, lit outside the cascades.
