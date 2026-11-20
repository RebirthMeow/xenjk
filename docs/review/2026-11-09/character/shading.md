# Character materials and shading review (lane SH)

Target: commit 6fe7bb2f, read from `.codex-research/wt-review-6fe7bb2f`. Paths below are under
`codemp/rd-vulkan/` unless they start with `shaders/`, which is `codemp/rd-vulkan/shaders/glsl/`.
Static reading only: nothing was built, launched or measured. "Confirmed" means confirmed by
reading the code path end to end; "plausible" means the code allows it but the visible effect
depends on content, settings or driver behaviour I could not observe.

Counts: 24 findings. 11 bug, 6 perf, 4 quality, 3 suggestion. Severity: 0 high, 9 medium, 15 low.

Things checked and found sound (no finding): std140 layout of every block in `global.h` against
its C mirror (pads line up), specialization-constant ids 0..30 against `FragSpecData` and the
`INIT_SPEC_ENTRY_FRAG_AT` slots, `layer_count` gated by `PBR_HAS_LAYERS` in the pipeline,
the sample-mask module only replacing the `frag_color0` variants, the rig light levels against
the GPU light formula, probe decode in linear mode, self-shadow ray/step packing, quad-uniform
control flow around `QuadMean`.

Generated SPIR-V: `shaders/spirv/shader_data.c` was last changed by the same commits that last
changed `gen_frag.tmpl`, `fma.glsl` and `global.h` (6fe7bb2f, df86ea93), so nothing looks stale
at commit granularity. I did not recompile to compare bytes.

---

## Medium

### SH-1 bug, medium, confirmed: plain PBR stages get dynamic-light specular 1/pi as strong as material stages
- `shaders/common/pbr.glsl:290-292` (`CalcDynamicLightContribution`) against
  `shaders/gen_frag.tmpl:608-609` (`CalcDynamicLightMaterial`) and the grid path at
  `gen_frag.tmpl:1572`, `1779`.
- The grid light is scaled by pi before `(Fd + Fs) * NL`, and the material dynamic-light path
  does the same (`light.color * M_PI * attenuation * MaterialLight`). The plain path
  (`material_mode == 0`) uses `diffuse + CalcSpecular` with no pi and a diffuse that skips the
  1/pi. Diffuse comes out equal in both; specular is pi times weaker on the plain path.
- Failure: on the player screen the fill, rim and top lights are dynamic lights
  (`tr_scene.cpp:697-727`) while the key is the entity's directed light. A plain PBR armour or
  cloth stage gets a key highlight at full strength and rim/top highlights 3.14x dimmer than the
  skin, hair or sheen stages beside it. In play, a saber's glow highlights skin 3.14x more than
  a plain metal stage of the same roughness.
- Fix: make the plain path `light.color * M_PI * attenuation * ( Diffuse_Lambert( diffuse ) + Fs ) * NL`.
  This brightens highlights on world surfaces too (same function), so it is a look change the
  owner should see before it is kept; an alternative is to apply it to Ghoul2 draws only.

### SH-2 bug, medium, confirmed logic / plausible visible effect: a stage runs the occlusion split even when the main pass was not begun with the keeping variant
- `tr_backend.cpp:2019-2028` (`backEnd.charOcclusion`), `vk_renderpass.cpp:1270-1271`,
  `vk_renderpass.cpp:186-197`, `366-396`.
- The main pass keeps its MSAA colour only when it was begun as `main_keep`, chosen from
  `r_charAO`, `r_charContactShadows` or `R_STAGE_COVERS()`. `charOcclusion` is true for any
  `RDF_STAGE` scene regardless (`STAGE_CHAR_AO` 1, `STAGE_CHAR_CONTACT` 8), and
  `RB_CharOcclusion` then ends the pass and reloads it with `LOAD_OP_LOAD`.
- Failure: with `r_charAO 0` and `r_charContactShadows 0` (the code defaults) and neither bloom
  nor dynamic glow forcing a store, the first frame a stage appears (`tr.stageFrame` is still
  old) and every frame of a stage that does not set `r_stageCovers` begin as plain `main`, whose
  MSAA colour store op is `DONT_CARE`. The reload then loads undefined contents: whatever was
  drawn before the figure can be lost. Desktop NVIDIA normally keeps the memory, which would hide
  it on the owner's PC.
- Fix: record which variant `vk_begin_main_render_pass` began and require it in
  `backEnd.charOcclusion`; or begin `main_keep` whenever the frame holds a stage scene.

### SH-3 bug, medium, confirmed: the character fill and rim jump sides when the grid light crosses the camera axis
- `tr_backend.cpp:559-569`.
- `side` is the sign of `cross( toCam, lightDir ).z`. The fill is placed 50 degrees off the camera
  axis on the far side and the rim at 205 degrees on the light's side, both hard-switched by it.
- Failure: as the camera orbits a character, or the character walks through a point where the
  grid's light lines up with the view, the fill (default 0.5 of the grid's whole light) and rim
  (0.6) move across the figure in one frame. Where the grid light is near vertical the cross
  product is near zero and its sign follows interpolation noise, so the pair can flicker from
  frame to frame while moving. The code comment already notes the vertical case is arbitrary.
- Fix: weight by the sine of the angle instead of its sign, for example blend the two mirrored
  placements with `smoothstep` on `cross.z / ( |toCam| |lightDir.xy| )`, and fade to a centred
  fill as the horizontal part of `lightDir` goes to zero.

### SH-4 bug, medium, confirmed: linear-light mode leaves several colour inputs sRGB-encoded
- `shaders/gen_frag.tmpl:1450-1456` decode albedo, grid light and ambient. Not decoded:
  layer albedo (`:1474-1477`, mixed into the already linear albedo), `tintParams` and the entity
  colour behind `tintColor entity` (`:1486`), `layerTint` (`:1477`), `toonRim` (`:1766`),
  sheen and subsurface colours (`:172`, `:188`), dynamic light colours (`:608`), the sun colour
  (`:1842-1849`). `ToonShade` decodes with `pow 2.2` (`:301`) while everything else uses the
  piecewise sRGB curve.
- Failure: a stage is always mode 2 (`tr_backend.cpp:532-533`), so on the player screen a
  layered or tinted material is lit with encoded layer colours in linear space and comes out
  lighter and less saturated than the same character in play with `r_charLinear 0`. A tint of
  0.5 multiplies linear albedo by 0.5 (display 0.73 of the untinted value) instead of the
  authored 0.5. The `r_charLinear` description says dynamic light colours are decoded; the
  shader does not do it (the rig compensates on the CPU, game lights do not).
- Fix: decode once where each is read (layer albedo and `toonShadeTex` already has the pattern),
  or decode the material colours on the CPU in `RB_StageIteratorGeneric` when the entity is in
  linear mode, so the shader stays unchanged.

### SH-5 bug, medium, plausible: FMA shading clips to white before its exposure compression, and takes no shadows
- `shaders/common/fma.glsl:28-29`, `57`, `64`, `86`, `102-109`; `gen_frag.tmpl:1445-1448`.
- `grid = directed + ambient` (each can reach 1, times `mapLightScale`) multiplies the albedo
  first, then the result is clamped to 0..1 several times, and only then `ToonExposure` runs.
  The knee cannot recover what the clamps removed.
- Failure: in a bright spot (grid sum 1.5 to 2) a skin albedo of (0.9, 0.7, 0.6) reaches
  (1.0, 1.0, 0.9) at the first clamp: the face goes near white with a yellow cast, while the
  toon path beside it keeps its palette. In dark spots nothing lifts it (no ambient floor, fill
  or rim on this path). The path also skips character self-shadow, contact shadows, sun, dynamic
  lights and AO, so an FMA character under a saber's glow does not react at all.
- Fix: compress the light before the game math. Compute the grid's luminance, pass it through
  the same knee as `ToonExposure` and scale `grid` to that level (keeping its colour share), so
  the source's clamps see values in the range the game produced. This keeps the mode faithful
  and makes it hold across times of day. Dynamic lights could be added to `grid` the same way.

### SH-6 perf, medium, confirmed: per-pixel dynamic-light work is unbounded by distance or entity
- `shaders/gen_frag.tmpl:571-612`, `shaders/common/pbr.glsl:256-293`, `tr_backend.cpp:485-505`.
- Every lit character pixel loops over every dynamic light in the scene (up to 64); the
  per-entity light mask is `#if 0`. On material stages each light also traces a contact ray
  (12 steps, two texture reads each plus a quad mean) before the range test, unless
  `r_charFast` bit 1 votes it away, and that vote itself evaluates the range, attenuation and
  facing for every light.
- Cost: a saber duel with 8 to 12 lights puts 8 to 12 full BRDF evaluations on every character
  pixel, and up to 12 x 12 depth reads on those in range when contact shadows are on. On the
  3060 laptop this is the term most likely to scale badly.
- Fix: cull on the CPU per entity (sphere against light radius) and upload a 64-bit mask or a
  short index list in the entity block; cap contact rays to the two or three strongest lights
  per entity (the mask path already ranks them).

### SH-7 perf, medium, confirmed: reflection probe work repeats per call
- `shaders/common/pbr.glsl:139-196`; callers at `gen_frag.tmpl:1700`, `1811-1819`.
- `ProbeWeights` (8 distance terms) runs once per `SampleProbes` / `SampleIrradiance` call: three
  times per pixel on a light-grid model, four on an eye stage. A smooth surface reads up to 8
  prefiltered cubes with parallax correction each; `r_cubeMapDiffuse` adds front and back
  irradiance lookups (one or two cubes each).
- Cost: up to about 12 cube reads and 32 weight terms per character pixel for smooth materials.
- Fix: compute the weights once per pixel and derive the cut variants from them; cap the blend
  at the 3 largest weights; for `r_cubeMapDiffuse` evaluate front and back from one probe.

### SH-8 perf, medium, confirmed: some hair still shades once per sample
- `vk_pipelines.cpp:1482-1508`.
- The sample-mask path that restores once-per-pixel lighting only applies to single-texture
  stages with `r_g2SampleShading 2`, alpha to coverage on and the ramp mode. A Ghoul2
  alpha-tested stage that is multitextured, or any when `r_ext_alpha_to_coverage` is 0 or 3,
  keeps `sampleShadingEnable` and runs the whole lit shader 4 times per pixel at MSAA 4,
  including self-shadow rays and contact rays. The stage figure is per sample by design
  (`r_stageSampleShading 1`).
- Fix: extend `USE_SAMPLE_MASK` to the two-texture variants, or fall back to plain alpha to
  coverage (not per-sample shading) where the mask variant does not exist. For the stage,
  consider per-sample only on stages flagged skin or eye.

### SH-9 bug, medium, plausible: contact-shadow rays assume the view fills the render target
- `shaders/gen_frag.tmpl:456-461` (`ScreenUV`), `tr_backend.cpp:443-449`, `1954`.
- The depth extract is always full screen and `ScreenUV` maps clip space straight to 0..1 of
  it. `backEnd.charOcclusion` does not test the viewport.
- Failure: a stage or world view drawn into part of the screen (a menu panel, a reduced
  `cg_viewsize`) projects ray samples to the wrong texels: contact shadows appear displaced or
  vanish. The AO lookup is unaffected (it uses `gl_FragCoord`). The debug print at
  `tr_backend.cpp:456` already logs the viewport next to the screen size.
- Fix: pass viewport scale and offset (viewport / target size) and apply them in `ScreenUV`,
  or disable `charOcclusion` when the viewport is not the full target.

---

## Low

### SH-10 bug, low, confirmed: back faces of two-sided surfaces mirror the normal map's x
- `shaders/gen_frag.tmpl:1035-1037`, `shaders/common/pbr.glsl:18`, `39`.
- Only the normal is reversed; the bitangent is rebuilt from it and flips too, the tangent does
  not. The back face should be the negated front normal (-aT - bB - cN); the code produces
  (+aT - bB - cN).
- Failure: the inside of a dress or coat shows its weave lit as if mirrored left to right.
- Fix: negate `var_Tangent.xyz` together with the normal (or negate `n.x` on back faces).

### SH-11 bug, low, plausible: derivatives are taken after `discard`
- `shaders/gen_frag.tmpl:1335`, `1359`, `1365-1369` then `:535-540`, `:1667`, and every
  implicit-lod `texture()` after the alpha test.
- A discarded fragment stops running, so its quad neighbours' derivatives are undefined.
  `QuadMean` is used precisely on alpha-tested stages (hair contact rays), and specular AA and
  the material maps' mip selection follow the same rule.
- Failure: on a driver that does not treat discard as demotion, hair card edges get noisy
  contact shadows and wrong mips along the alpha edge. NVIDIA demotes in practice.
- Fix: `GL_EXT_demote_to_helper_invocation` (`demote` instead of `discard`), core in Vulkan 1.3.

### SH-12 bug, low, confirmed (not at default settings): entities take dynamic lights twice when `r_dlightMode` is not 2
- `tr_light.cpp:408-421` against the GPU light path (`tr_backend.cpp:485`).
- The classic merge of dynamic lights into `directedLight` and `lightDir` is skipped only for
  `r_dlightMode 2` (the default), not when GPU lights are on.
- Failure: with `r_dlightMode 0` or `1`, characters get each light once in the entity light and
  again per pixel, and on a stage the rig's fill, rim and top bend the key direction that the
  self-shadow uses.
- Fix: skip the merge when `vk.useGPUDLight`.

### SH-13 bug, low, confirmed: FMA face shadow follows the body's yaw, not the head's
- `vk_shade_geometry.cpp:3017-3026`, `shaders/common/fma.glsl:75-81`.
- The face SDF angle is measured against the entity axis.
- Failure: with the head turned 60 degrees (look-around, animations) the face shadow stays where
  it would be for a head facing forward.
- Fix: take the axes from the head bone (the bone matrices are already uploaded; a stage keyword
  could name the bone).

### SH-14 bug, low, confirmed: stage ambient is encoded with gamma 2.2 and decoded with the sRGB curve; the key clamps at 1
- `tr_light.cpp:364`, `369-372`; decode at `gen_frag.tmpl:409`.
- 0.3 encodes to 0.579 and decodes to 0.294: 2 percent low. The key uses the exact curve but
  clamps each channel to 1 first, so a raised `r_stageRigLevel` tints the key.
- Fix: one shared encode helper; pass linear values in spare floats instead of round-tripping.

### SH-15 bug, low, confirmed: skin wrap adds energy
- `shaders/gen_frag.tmpl:170-178`.
- `( NL + w ) / ( 1 + w )` integrates to pi x (1 + w) over the sphere. At the default
  (red weight 1.0 x strength 0.5) skin receives 50 percent more red diffuse light than its
  albedo says, 17 percent more green, 11 percent more blue.
- Effect: skin reads brighter and redder than a Lambert surface with the same albedo, in every
  light. If the look was matched to references with this in, changing it shifts that match.
- Fix: divide by `( 1 + w )^2` (the unused `WrapLambert` in `pbr.glsl:5-8` already does).

### SH-16 bug, low, confirmed: sun PCF rotation covers only 57 degrees
- `shaders/gen_frag.tmpl:633-636` (inherited from rend2).
- `r` is in 0..1 and is used as radians. The character shadow lookups multiply by 2 pi
  (`:870`, `:958`); this one does not.
- Effect: the 3 and 9 tap sun filters show more banding than their tap count should.
- Fix: `r * 6.2831853`.

### SH-17 bug, low, plausible: contact masks follow the nearest Ghoul2 entity of any kind
- `tr_backend.cpp:1711-1722`.
- A dropped weapon, a prop or an NPC nearer the camera than the player takes the three mask
  channels; the player's own grid light then falls back to in-shader rays (unblurred, full
  length). When two candidates swap order the player's contact shadows change character in one
  frame.
- Fix: prefer the entity with the largest screen coverage, or the followed client; add
  hysteresis.

### SH-18 bug, low, plausible: hair map moments can overflow on the RGBA16F fallback
- `vk_sunshadow.cpp:124-128`, `gen_frag.tmpl:1267-1269`, `1020-1021`.
- `tau * d * d` is summed over all strands of a column; with d up to 72 units and several
  dozen strands the sum passes 65504 in half floats and the variance becomes inf - inf.
- Only on devices without blendable RGBA32F. Fix: store d scaled to -1..1.

### SH-19 perf, low, confirmed: fill and rim run the whole material model twice more
- `shaders/gen_frag.tmpl:390-403`, `1787-1795`.
- Each is a full diffuse + GGX (or strands, sheen, cornea) evaluation on every Ghoul2 pixel,
  on by default.
- Fix: fill as diffuse only (it is a soft bounce) and rim as specular plus a narrow wrapped
  diffuse; roughly halves this term.

### SH-20 perf, low, confirmed: repeated texture reads
- `gen_frag.tmpl:1464` reads the normal map's blue for the masks and `pbr.glsl:19` reads the
  same texel again; `mask_texture` is read at `:1462` and again at `pbr.glsl:37`;
  `layer_mask_texture` at `:1471` and `pbr.glsl:26`; the skin soft-albedo test adds two reads.
- Fix: sample once in `main` and pass the values to `CalcNormal`.

### SH-21 perf, low, confirmed: 22 descriptor sets and a 1.3 KB constant block per stage
- `vk_pipelines.cpp:215-231`, `vk_shade_geometry.cpp:3047-3067`, `3125-3158`, `global.h:285-321`.
- Each material map is its own set; four sets are reset on every 3D draw, which widens the
  bind range to sets 10..21 each time. `vkUniformGlobal_t` (about 1344 bytes, mostly probe
  boxes and material constants) is rebuilt and appended per stage.
- 22 sets also exceeds `maxBoundDescriptorSets` on some integrated GPUs (8 on older Intel
  drivers): a laptop that starts the game on its integrated GPU would fail at layout creation.
- Fix: one material set with several bindings; one per-view set for shadow, AO, depth and
  hair map; material constants in a per-stage block written once at shader load.

### SH-22 quality, low, confirmed: FMA and toon keywords fail silently and depend on order
- `tr_shader.cpp:1573-1636` (the `fma*` branch), `1638-1648`.
- `fmaRim`, `fmaSpec`, `fmaFaceAxes`, `fmaPost`, `fmaRearLight`, an unknown `fma...` keyword and
  a `toon` written after `fmaBody` all `return qfalse` without a message, so the whole shader
  falls back to the default with no hint which line was wrong. `toonShade 0 0 0` written before
  `toon` is replaced by the default shade (the zero-length test at `:1641`).
- Fix: a warning naming keyword and shader on each exit; a "shade given" flag instead of the
  length test.

### SH-23 quality, low, confirmed: shipped features ride in fields named as tests, and several values are packed into floats
- `global.h:187-199` (`charTest..charTest4` carry the ambient floor, `r_charFast`, hair map
  settings), `gen_frag.tmpl:317-320`, `:365`, `:582-594`, `:963-965`, `pbr.glsl:203`.
- `ambientLight.w` is mode + 10 x bits and is decoded in three places with different
  expressions; `light.origin.w` is channel + 4 x degrees; `selfShadowParams.x` is rays + 32 x
  steps; `normalScale.z` selects the style pass by range tests.
- Risk: a new consumer decodes one differently (the pbr.glsl copy already duplicates
  `CharTerms`). Fix: named ints in the blocks (std140 has room), one decode helper each.

### SH-24 quality, low, confirmed: about 25 debug views are compiled into the production lit shader
- `shaders/gen_frag.tmpl:835-838`, `862-869`, `951-957`, `1859-1974`.
- They branch on uniforms, so every character pixel evaluates their conditions, and they keep
  the shader near 2000 lines with shared mutable globals (`toonShadow`, `toonMain`,
  `charDbg*`, `selfDbgColor`).
- Fix: a `debug_view` specialization constant (pipelines rebuilt when a debug cvar is set), and
  pass toon state as parameters.

### Suggestions

- **SH-25 suggestion, low:** specular AA (`gen_frag.tmpl:1666-1671`) widens GGX only. The
  strand lobes (exponents 80 and 20), the cornea (roughness 0.05 on the vertex normal) and the
  toon highlight step are not widened and are the terms most likely to shimmer at distance.
  Scale the strand exponents and cornea roughness by the same kernel.
- **SH-26 suggestion, low:** the half-resolution AO and contact masks are read with plain
  bilinear filtering (`gen_frag.tmpl:445-448`). A depth-aware 4-tap upsample against
  `screen_depth_texture` would stop the light and dark fringes at character silhouettes under
  MSAA.
- **SH-27 suggestion, low:** characters receive the sun on top of the grid's light in sun mode 2
  (`gen_frag.tmpl:1829-1850`, kept for rend2 parity) and the shadow value carries a second
  N.L. For looks that hold at every hour it would be worth deciding one rule for characters
  under a moving sun, in the model, since the grid was baked for one sun position.

---

## Needs a high-effort follow-up

1. SH-2 on a GPU: validation layers plus a frame capture of the first stage frame and of a
   non-covering stage, with bloom and dynamic glow off.
2. SH-1 and SH-15 change calibrated looks. Fixing them means re-matching the stage rig and
   skin against the reference renders; the owner should choose.
3. SH-4: a colour-space pass over linear mode with a test character that has layers, tints,
   toon rim and emissive, compared between stage and play.
4. SH-5: FMA characters viewed in the brightest and darkest places of a map and at several
   hours.
5. Hair pixel cost on 3060-class hardware (SH-6, SH-8, self-shadow at up to 8 rays x 12 steps
   on strands, fill and rim): a GPU-time breakdown per term before choosing what to cut.

## Not covered

- `ssao.frag` and `vk_ssao.cpp` (the character AO and contact mask pass itself), beyond their
  interface with the lit shader.
- `R_SetupCharShadows`, `R_RenderCharShadows` and the cascade rendering in `vk_sunshadow.cpp`
  (bias, culling, atlas layout): acne and peter-panning on characters need the game running.
- `vk_lightgrid.cpp`, `light_frag.tmpl` / `light_vert.tmpl` (the `r_dlightMode` pass),
  `tr_shade_calc.cpp`, the outline deform, refraction and the glow pass.
- The fast-light (`USE_FAST_LIGHT`) variants and MDV models.
- Hair chains, cloth, skinning, textures and loading (other lanes); the Watson scene shaders.
- Any runtime behaviour: no build, no validation layers, no captures.
