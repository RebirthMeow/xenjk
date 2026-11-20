# Character shadows and dynamic-light shadows (rd-vulkan)

Owner direction 2026-10-14: no ray tracing; keep the baked lighting. Character shadows come from
**shadow-only lights**. These darken the baked result by the share of light a caster blocks, and never re-light the world.
Tiers: (1) directions from the light grid on every map; (2) real light positions where a map has them; dynamic
lights (sabers, explosions) shadowed within a budget; characters also receive world shadows.

## State before this work

- Sun cascades (`r_sunShadows`, models with `r_sunShadowModels 1`) run only on maps with a `q3gl2_sun` sky:
  `xenjk_showcase` and the PBR bench maps. Every other map has the `cg_shadows 1` blob.
- Dynamic lights are per-pixel (`r_dlightMethod 1`) and cast no shadows; rd-vulkan has no point-light shadow maps.
- Map `light` entities are not runtime lights (baked).

## Light survey (2026-10-14)

Tool: `tools/bsp_light_survey.py` (read-only; BSPs read from the installed GameData pk3s in game load order).
Output: `.codex-research/shadow-survey-20261014/survey.json` (one row per map) and `survey.log`.
Coverage: 145 maps, 140 distinct after identical copies were merged (stock MP 23, stock SP 33, community 82, xenjk 2).

Light grid sampled 40 u above upward-facing vertices (points inside solid skipped), trilerped as
`R_SetupEntityLightingGrid` does. Decode checked two ways. On sun-dominated maps the grid points at the declared sun
(duel9 87% of points within 30°, t1_rail 82%, ctf3 72%). On `xenjk_reflect`, lit only by surface lights, the
visible fixtures explain the grid (68% within 30°).

| Measure (median of maps) | all | stock MP | community |
|---|---|---|---|
| Maps keeping light entities | 11 / 140 | 0 / 23 | 8 / 82 |
| Maps with `info_null` leftovers (stripped spot targets) | 73 | 22 | 19 |
| Maps with a lit sky sun (intensity > 0) | 22 | 8 | 9 |
| Maps with `q3map_surfacelight` fixtures | 102 | 20 | 67 |
| Directed share of grid light at standing points | 0.74 | 0.71 | 0.77 |
| Floor light a character could block (directed × up-facing) | 0.63 | 0.61 | 0.66 |
| Light elevation p10 / p50 | -11° / 45° | -13° / 48° | -3° / 45° |
| Standing points lit from below 30° elevation | 35% | 30% | 34% |
| Direction swing over 64 u of walking, p50 / p90 | 13° / 49° | 15° / 61° | 12° / 46° |
| Brightest visible fixture within 30° of the grid direction | 16% | 13% | 17% |

Stock skies often declare `sun 255 255 255 0 ...`: intensity zero, no sun. "Lit sun" counts intensity > 0 only.

## What it means

1. **Strength: good on every map.** The grid is strongly directional (about 3/4 of the light). A character
   standing in it blocks a large, varying share, so subtracting blocked light gives real contrast. Shadows will
   be deep in hard light and faint in soft light, as wanted.
2. **Direction: usable only after it is stabilised.** Taken raw, a third of standing points would throw long,
   sideways shadows, the lowest tenth would be lit from below the horizontal, and the shadow would swing
   tens of degrees within a step or two. Tier 1 needs:
   - an elevation floor
   - spatial smoothing (a wider sample than one cell)
   - temporal smoothing
   - strength fading when the direction is low or incoherent

   Without these, the shadow looks vague or jittery.
3. **Real light positions: gone on almost every map.** The compiler stripped the light entities: 11 of 140 maps
   keep them, none of the stock MP maps do, and 73 maps still hold the spot targets of stripped lights.
   Surviving fixtures (surface lights) rarely explain the grid (16%), so most light came from the stripped
   entities. Tier 2 therefore cannot be read from BSPs, except for the few maps that keep entities
   (ctf_ns_streets 1365, ctf_dantooine_v3 403, t3_bounty 47, ctf_hagrent 14, siege_urban 7) and the
   fixture-lit ones (ctf_hagrent 80%, ctf_ansion2, ctf_dash2, ctf_telti, ctf_cairn about 50%).
4. **Sun:** on outdoor-heavy maps with a lit sun (duel9, t1_rail, ctf3, hoth maps), the sun direction agrees
   with the grid. The existing cascades could take that sun on those maps, but the risk is doubled sunlight
   on lightmaps (see [renderer-bench.md](renderer-bench.md), sun port).

## Stage 1: grid-derived character shadows (2026-10-14, e0329c59, Install 11)

The owner said "yes" to stage 1 after the survey. It is behind `r_charShadows` (default 1 since 2026-10-14, owner).

**How it works**
- **Casters:** the 8 nearest Ghoul2 models within `r_charShadowDistance` (2048) that can shadow the view.
  - Excluded: first-person, depth-hack and no-shadow models, and third-person bodies (`RF_THIRD_PERSON`, mirrors only).
  - Entities drawing the same Ghoul2 instance share one tile. NPCs come with a second entity at the same origin,
    probably an effect pass; only opaque surfaces reach a shadow view.
- **Light:** five grid samples (the centre and 40 u to each side), each direction weighted by its directed light.
  - The agreeing length is the directed light; the rest of the directed light counts as ambient.
  - Elevation is raised to `r_charShadowMinElevation` (40°).
  - Smoothed over 250 ms per Ghoul2 instance.
  - A light with no directed light (< 1) casts nothing.
- **Tiles:** layer 3 of the sun shadow image is the atlas: 4 × 2 tiles, a quarter of `r_shadowMapSize` each (512 at 2048).
  - The image now always has 4 layers at `r_shadowMapSize` when the device has depth clamp: about 32 MB at 2048.
    Before, it was 1×1 with the sun off.
  - Each tile is an orthographic depth view of its caster alone: radius 72 u, reaching 160 u past the caster.
  - Tiles reuse the cascades' render pass and pipelines, drawn in one pass before the main pass
    (`R_SetupCharShadows` / `R_RenderCharShadows` in tr_main.cpp, `RB_RenderSunShadowCascades` in tr_backend.cpp).
  - `R_IS_SHADOW_VIEW` replaces `sunCascade` at the shared guards: opaque only, first stage, no fog, the Ghoul2
    bone cache, no cloth.
- **Shading** (gen_frag.tmpl `charShadowKeep`), lightmap and vertex-lit stages only:
  - The baked light is multiplied by `1 - occlusion × fade × strength × NL / (ambient/directed + NL)`.
  - 9 rotated PCF taps, radius `r_charShadowSoftness` (2) texels.
  - Fades over the last 40% of the reach.
- **Sun maps:** with `r_charShadows 1` the shadows are off where the scene has sun cascades, which shadow models
  already. `2` keeps both.
- **Debug** (`r_charShadowDebug`, cheat):
  - 1 = the light kept
  - 2 = print each caster's light once a second
  - 3 = tile coverage (red) and occlusion (green)
  - 4 = stored tile depth

**Check** (`.codex-research/renderer-bench/scenes/20261014T0*-npcs_*`, sheets in `.codex-research/charshadow-20261014/`)
- **Setup:** the installed `xenjk.x86.exe` loads the dev DLL through a `cl_renderer` path relative to GameData
  (candidates `xenjk-vk-devdll*`). No exe is copied and nothing is written to GameData.
- **Scenes:** three frozen NPCs (`npcs_<map>` profiles, `d_npcfreeze 1`), hidden window, 4K, 4x MSAA.
- **ctf_kejim:** the NPCs fall from the spawn ledge to the floor at about 1386 1664 1368, where the light is 40°
  (clamped) from −y. Each NPC casts a soft shadow from its feet across the floor, away from the light.
  - No acne and no banding; the shadow follows the floor tiles' shading (`crop_kejim_east_offon.jpg`).
- **mp/ffa1:** the light is about 80° overhead, so the shadow sits under the feet. Debug views 3 and 4 confirmed
  coverage, tile content and lookup there.
- **Cost** (kejim, three casters, one hidden-window run per arm, so indicative only):
  - tile pass 0.013 ms GPU
  - main pass +0.23 / −0.02 / +0.16 ms in the three views
  - frame total +0.16 / +0.05 / +0.03 ms

**Path-traced reference (2026-10-14).** Owner: "mirror the scene in blender ... to compare".
- **Pipeline:**
  - `charshadowdump` (renderer command) writes the casters as drawn into their tiles to `charshadow/dump.obj`:
    Ghoul2 skinned on the CPU with the frame's bone cache, in world space. `charshadow/dump.txt` holds the camera
    and each caster's light (direction, ambient/directed ratio).
  - `tools/bsp_to_obj.py` exports the receiving floor: upward-facing within 700 u, open sky above, as the light
    grid's ambient term assumes.
  - `tools/charshadow_reference.py` runs in Blender 5.1 (Cycles, OptiX, 512 samples, 2 bounces, grey Lambert).
    - It lights each caster as the renderer's model does: a sun of irradiance 1 toward its light, sky radiance
      ratio / π.
    - It renders the floor without and with the caster (invisible to the camera) and takes the ratio: the light kept.
  - Game side: the same frame in `r_charShadowDebug 1`, with bloom, SSAO, dither and the blob off. The debug views
    now divide out the gamma pass's overbright scale (`charShadowInfo`), so the screenshot is linear.
  - Profile `charshadowdump_kejim`.
  - Files: `.codex-research/charshadow-20261014/ref/` (`compare_crops.png`, `cycles/kept_<deg>.npy`,
    `game_kept_960.npy`, `source_run.txt`).
- **Results** (ctf_kejim east view, three stormtroopers, light 40° from −y, ambient/directed 0.37–0.54):
  - **Silhouettes, positions and lengths match** the 2° reference: the projection, direction and dump agree.
  - **Umbra strength matches:** game shadow cores sit at 0.36–0.48 kept, Cycles 2° at 0.32–0.48, analytic
    1 − NL/(k + NL) at 0.37–0.46. `r_charShadowStrength 1` is right for the model.
  - **Contact darkness is missing:** under the soles Cycles goes to 0.00–0.12 (about 600 px at 960×540). There both
    the sun and most of the sky are blocked. Ours never drops below 0.32, because the ambient share is never blocked.
  - **Softness:** ours is uniformly hard, sharper than even the 2° sun. The reference hardens at contact and widens
    with distance from the feet.
    - 10°: the far half of the shadow is a broad soft gradient.
    - 25°: little umbra remains past the knees.
    - Pixels under 0.9 kept: game 11,202; Cycles 11,979 / 15,682 / 20,331 at 2° / 10° / 25°.
  - The real source size is unknown (the map's lights were stripped), so the size is a look choice, not a measurement.

**Contact hardening (2026-10-14).** Owner: "go for it". Contact darkening is dropped: a caster mask rendered in
Cycles put about 90% of the near-black contact pixels under the feet, and the rest is a rim about 1 px wide at 960 px.
- **`r_charShadowLightSize`** (degrees, default 10, 0 = the fixed filter):
  - 8 taps search for blockers over the receiver's widest possible penumbra.
  - Each blocker's depth is found by 3-step bisection with the comparison sampler; layer 3 has no raw-depth view.
  - The filter is 16 Poisson taps at radius (receiver − mean blocker) × tan(size / 2), at least
    `r_charShadowSoftness` texels.
- **Early outs** before any texture work:
  - a per-caster rectangle in tile clip space (`charShadowRect`): the caster's box, ±32 u horizontally and −40..+64 u
    vertically, grown by the widest penumbra
  - 4 ring taps plus the centre before the full search
- **Debug view 5** shows the estimated receiver-to-blocker distance. It reads about 10–20% above the body geometry.
- **Against Cycles at 10°** (`ref/compare_pcss10b.png`, a new dump and reference): vertical cross-sections along all
  three shadows give core kept light within 0.02–0.06 of Cycles (ours lighter) and half-depth widths within 1–2 px.
  - In the debug view a faint hard rim remains where the outer penumbra ends (the sparse search loses the blocker).
    It is not visible in the normal view (`crop_kejim_east_offon_soft10b.jpg`).
- **Cost** (main pass GPU ms, kejim, three casters close, 4K 4x MSAA, one hidden-window run per arm):

  | | east | high |
  |---|---|---|
  | off | 2.33 | 2.41 |
  | fixed filter, 16 taps (`r_charShadowLightSize 0`) | 2.62 | 2.69 |
  | first contact-hardening version | 2.95 | 2.89 |
  | with the early outs | 2.76 | 2.72 |

  The 16-tap filter is most of what remains. A cheaper filter (fewer taps where the radius is small) is the next
  lever if 240 fps needs it.

**Cheaper filter (2026-10-14).** Owner: "do 1 and then try it with eve 2b cloth sim".
- **Filter taps** follow the filter radius in texels: 4 below 3, 8 below 8, otherwise 16. The comparison sampler's
  bilinear 2×2 already smooths small radii.
- **Umbra skip:** when all 8 blocker-search taps are blocked, the filter is skipped. The search disc is at least as
  wide as the filter's.
- **Accuracy:** unchanged against Cycles 10° (`ref/compare_pcss10c.png`). Cross-sections are within 0.01 of the
  16-tap version.
- **Cost** (main pass GPU ms, kejim, three repetitions per arm, medians):

  | | east | high | lightside (no casters) |
  |---|---|---|---|
  | off | 2.375 | 2.604 | 2.211 |
  | on | 2.678 | 2.798 | 2.216 |
  | difference | +0.30 | +0.19 | +0.005 |

  The off arm's high view varied 2.41–2.66 between runs, so single runs cannot resolve these differences.

**Eve 2B with GPU cloth (2026-10-14).** Script `.codex-research/charshadow-20261014/eve_run.py`; runs under `runs/`.
- **Setup:** the installed exe with the dev DLL; eve2b_np as the player in third person on ctf_kejim at 1386 1664
  1368, `r_cloth 1`, `r_clothProfile 1`; static views, then a spin on the spot (`+left`, `cl_yawspeed 720`).
- **The skirt's shadow follows the GPU cloth.** In top-down captures (`eve_top_crop.jpg`) the shadow widens into the
  flared skirt mid-spin and narrows to the legs at rest. Shadow tiles reuse the frame's cloth instance
  (`RB_ClothVkSurface`); the cloth front end is skipped in shadow views.
- **Eve does not self-shadow:** all her stages are `rgbGen lightingDiffuse` (light-vector path). The grey patches on
  her in the debug view are her alpha-blended stages drawn in their own colours.
- **Cost** (1080p uncapped, no MSAA, medians over the last two thirds of each run, one run each; rough): tile pass
  0.022 ms GPU, main pass 0.515 → 0.632 ms.
- On kejim's dark floor the shadow reads subtly in the normal view (`eve_crops.jpg`, `eve_zoom.jpg`). Strength
  follows the grid's ambient/directed ratio, which the Cycles comparison confirmed.

**Airborne casters (2026-10-14, `r_charShadowFloor`, default 1).** Owner: the strafing items "are good to try and
lets see if we like them or not".
- **Goal:** a hopping character's shadow stays on the ground it will land on, holds its direction, and softens
  as the character rises without vanishing.
- **Light at the floor:** a trace (`CONTENTS_SOLID | CONTENTS_TERRAIN`, up to 1024 u) finds the floor under the
  caster. The grid is sampled 24 u above it, where a standing character's origin is, instead of at the body.
- **Reach to the floor:** the depth range grows to twice the floor's distance along the light, from 160 u up to
  1536 u. That keeps the floor at half the range, short of the fade over its last 40%. The 2 u depth bias is now per
  caster (`charShadowScale[n].z`); the penumbra already widens with height (`r_charShadowLightSize`).
- **`r_charShadowFloor 0`:** the stage 1 behaviour, for A/B in play.
- **Check:** run `20261014T030226-best-hidden-npcs_ctf_kejim-charshadow_print` with the new `charshadow_print`
  profile (`r_charShadowDebug 2`, which now prints `floor` and `reach`).
  - Standing NPCs: floor 23–24 u and reach 160, the same as stage 1, and the same view.
  - NPCs falling from the spawn ledge: floor 422–462 u and reach 1226–1271.
  - Owner in play on Kejim (dev DLL via `GameData/Play_Kejim_ShadowTest.bat`): "i like it, it feels like a more
    stable shadow rather than darting all over the place". Kept as the default.

**Known, open**
- Lifts and other movers are not in the floor trace: a character on a lift takes its light from the floor below
  the lift.
- The NPC bodies in these bench scenes draw flat yellow in both arms. This predates the change and was not
  investigated.
  - They take the vertex-light lit path, so they also self-shadow from their own tile.
  - Player models (`lightingDiffuse`) take the light-vector path and are not receivers.
- Receivers within the reach behind a thin floor can also be shadowed. Characters do not receive other characters'
  shadows (stage 3).
- The cgame's blob (`cg_shadows 1`) still draws under the new shadow.
- Committed e0329c59 and installed as Install 11 (2026-10-14; renderer-vulkan-in-xenjk.md). Default on since 2026-10-14 (owner: "charShadows should be default on"); `r_charShadows 0` turns them off.

## Proposed order

1. Tier 1, grid-derived shadow-only character shadows with the stabilisation above. Visible on every map.
   **Stage 1 implemented 2026-10-14 (above).**
2. Dynamic-light shadows within a budget of one or two at a time. Sabers and explosions have real positions,
   so the survey does not limit this.
3. Characters receive world shadows.
4. Tier 2 through **reconstructed** lights, as a separate study:
   - fit point lights where the grid's direction field converges
   - check the fits against the `info_null` spot targets and the lightmaps
   - the result is an optional per-map light list, written to a sidecar file and reviewed
   - maps without a list stay on tier 1

## Next (owner, 2026-10-14: "will pick up in a new session")

- The owner's GameData/base/openjk.cfg now has `r_charShadows 1`, `r_charShadowLightSize 10` and `cg_shadows 0`. The
  previous file is in `.codex-trash/20261014T0217-openjk-cfg-pre-charshadow/`.
- **Sun maps:** `q3gl2_sun` skies in the install are siege_urban_pbr, siege_urban_cyber, ctf_illimiran_pbr,
  ctf_dantooine_pbr_clean and xenjk_showcase. With `r_charShadows 1`, characters there get sun-cascade shadows only,
  so they have none indoors or out of the sun. `r_charShadows 2` gives doubled shadows outdoors.
  - Offered, not started: switch the grid shadow off per character only while it stands in sunlight, using the
    cascades or a trace toward the sun.
- **Sun vs grid quality:** compared from code and settings only.
  - The sun shadows have similar texel size near the camera but coarser far cascades (about 2 u per texel), and
    fixed hard edges.
  - They add self-shadowing and shadows on other characters.
  - Offered, not started: the Cycles comparison on siege_urban_pbr with a 0.5° sun (about an hour with the existing
    tools).
- **Other offers:** saber and explosion shadows (stage 2), the yellow bench NPCs, reconstructed lights (tier 2).
