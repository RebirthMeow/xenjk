# Sun shadows in rd-vulkan: how they work, and the "large casters" bug (2026-10-13, fixed)

Pick-up file for a new session. Port history and design: [renderer-vulkan-sun-port.md](renderer-vulkan-sun-port.md).
Renderer notes and installs: renderer-vulkan-in-xenjk.md.

## Status

- **Fixed and installed (2026-10-13):** on `xenjk_showcase` the sealed studio was lit by the live sun, with edges that
  moved with the camera (owner report: "shadows moving on the walls" of the clean room). The cause was not the casters.
  `get_scissor_rect()` (`vk_init.cpp`) clamped every non-portal scissor to `glConfig.vidWidth/vidHeight`, so on a
  1920×1080 window a 2048 cascade was only written inside 1920×1080. The rest of each layer kept the clear depth and
  read as lit, a camera-relative region. The cascade pass now uses the unclamped viewport rect. This affected every
  map whenever the window is smaller than `r_shadowMapSize` in either axis.
- **Found by** an outside review (Fable). A second review (Astra) flagged the two items below, also fixed:
  - rend2's cascade fit (half the near-far diagonal) leaves each slice's far corners outside the box: 265 vs 414,
    896 vs 1276, 2940 vs 4193 units at the defaults. Cascades 0 and 1 keep it, since a receiver outside falls through
    to the next cascade and the tighter box keeps texels dense. The last cascade now uses the smallest sphere that
    encloses its slice, and its lookup accepts only coordinates inside the layer (rend2 accepted -0.5..1.5 and read
    clamped edge texels).
  - `r_sunShadowDebug 3` read cascade 0 for every pixel; it now reads the first covering cascade (magenta: none).
- **Evidence** (`.codex-research/eve2b-best-20261012/runs/`): before the fix, SC4 (2048, debug 2) shows lit wedges on
  the studio floor, SC5 (`r_shadowMapSize 1024`) the whole studio in shadow, SC6 cascade 0 covering it
  (`SC_scissor_debug_grid.jpg`). After: SC9-SC11 (debug 2, debug 3, lit) all shadowed, the deck still casts
  (`SC_all_studio_grid.jpg`); siege_urban_pbr SU1-SU4 unchanged against the previous install (`SU_all_urban_grid.jpg`).
- The sections below are the investigation as it stood before the cause was found; the caster hypotheses and the
  next-steps list are superseded.

## How it works (code map)

1. **Source.** The sky shader's `q3gl2_sun <r g b> <intensity> <yaw> <elev> <mapLightScale> <shadowScale>` (rend2
   syntax). Baked lightmaps and the light grid already hold the q3map2 sun; the live sun is added on top, and
   mapLightScale scales the baked light down.
   - `r_sunlightMode 2` (default) adds the sun as a primary light times the shadow value.
   - Mode 1 modulates the lightmap instead.
   - Entities get the sun on top of the grid's own sun (a rend2 quirk kept for parity).
2. **Cascade setup**, front end, `tr_main.cpp` `R_SetupSunShadowCascades`.
   - The view out to `r_shadowCascadeZFar` (4096) is split into 3 slices (`r_shadowCascadeZNear`/`ZBias`).
   - Each cascade is an orthographic view down `-sunDir`.
   - Its box is ±radius in all three axes around the slice centre. Radius = half the slice frustum's diagonal (about
     265 for cascade 0 at 1080p, fov 80). The box follows the camera.
   - The light origin is snapped to whole shadow-map texels.
   - PVS comes from the camera (`pvsOrigin = vieworg`).
   - `R_SetupProjectionOrthoZ01` builds the projection: depth runs from `bounds[0][0]` (toward the sun) to
     `bounds[1][0]`. Cull planes are the four sides plus the far plane (`frustum[4]`); there is no near cull plane.
3. **Caster selection.**
   - `R_RenderSunShadowCascades` runs `R_AddWorldSurfaces` and `R_AddEntitySurfaces` for each cascade.
   - `R_AddDrawSurf` keeps opaque, non-sky shaders only (`sort == SS_OPAQUE && !isSky`).
   - `R_CullSurface` (`tr_world.cpp`): faces are culled by the light direction, with the cull type flipped, so a
     front-sided face is kept when it faces away from the sun. Faces with no plane (`q3map_nonplanar`) have normal 0
     and are kept.
   - Models cast only with `r_sunShadowModels`, never first-person or depth-hack models.
4. **Drawing**, backend, `tr_backend.cpp` `RB_RenderSunShadowCascades`.
   - Runs at frame start, before the main pass.
   - Each cascade is drawn into one layer of a 3 × 2048 D16 depth array (`vk_sunshadow.cpp`).
   - The pass is depth-only, with depth clamp (`vk.sunShadow.depthClamp`; the cascades are off if the device lacks
     it), flipped culling and no depth bias.
   - Only each shader's first stage is drawn (`vk_shade_geometry.cpp`: `if ( backEnd.viewParms.sunCascade ...
     ) break;`).
   - Draws go through the normal DrawItem path, with the stage's pipeline created for `RENDER_PASS_SUN_SHADOW`.
5. **Lookup**, `gen_frag.tmpl` `sunShadow()`.
   - The cascade is chosen by view depth.
   - The receiver is offset by `vertexNormal * (1 - N·L)`.
   - Filtering is hardware comparison plus rotated taps.
   - The result is multiplied by N·L into `sunShadowValue`.
6. **Debug and cost cvars.**
   - `r_sunShadowDebug`:
     - 1 = cascade tint
     - 2 = shadow value
     - 3 = cascade-0 stored depth at the pixel, bracketed: red ≥ 0.1, green ≥ 0.4, blue ≥ 0.7, so white = nothing
       stored
     - 4 = cascades cleared only
     - 5..12 = lighting probes
   - `r_sunShadowInfo` 1 prints cascade boxes (`SUNINFO`); 2 adds per-frame cost (`SUNPROF`).

Anything that re-runs per-view front-end work also runs once per cascade. That is how the cloth bug happened: the
cascade camera failed `r_clothMaxDist` and cleared the instance.

## The studio bug: what is proven

The map is built by `tools/showcase_map/build_showcase_map.py`. The studio is a closed box (1280 × 1280 × 640) at
z −1600. Sun: yaw 25, elevation 26-30, so `sunDir` ≈ (0.815, 0.38, 0.44). Every floor point's ray to the sun leaves
through the +x/+y walls (about 800 units up the ray) or the ceiling (about 1,350). The whole floor should be in
shadow.

| Test (map variant, `r_sunShadowDebug 2` or 3) | Result |
|---|---|
| Eve as a caster, deck and studio | casts |
| Stock-textured 192×192 slab over the deck stage (`mk_slab.py`, placed so its shadow lands on the stage) | casts; the pylons cast too |
| Stock 192×192 slab inside the studio, about 190 up, about 430 up the ray (`mk_slab3.py`) | casts (stored depth shows it) |
| Studio walls and ceiling (custom `cyc_grey`/`studio_dark` shaders) | do not cast: floor lit, stored depth white |
| Same, walls in stock `bespin/u_floor04` (`mk_stock.py`) | do not cast |
| Same, texture stage first, lightmap second (`mk_order.py`) | do not cast |
| Same, `cull none` on the wall shader (`mk_cull.py`) | do not cast, so GPU face culling is ruled out |
| `r_vbo 0` | no change, so the world VBO path is ruled out |
| Opaque outer shell instead of sky outer faces (now in the build script) | no change, so sky surfaces are ruled out |
| Large free-standing panels 24 inside the ceiling and +y wall (`mk_liner.py`) | do not cast |
| Depth range extended toward the sun to the world bounds (tried in `tr_main.cpp`, reverted) | no change; depth clamp is on anyway |
| Instrumented: cascade 0's front end keeps the walls and ceiling (correct facing; `DBGFACE`) and they are in its backend draw list (`DBGBE`, SF_FACE, entity 2047) | submitted, yet absent from the depth layer |
| Two small slabs, near (150 up) and far (450 up) (`mk_dist.py`) | inconclusive: the shadows fell outside the shot views, and the far slab was placed outside the room |

**Unseparated hypothesis:** the casters that fail are either large (1,232-1,280 units, against a cascade-0 box of
about ±265) or far up the sun ray (≥ about 800, against ≤ 430 for the one that works). Both are true of everything
that failed so far.

**Not yet checked:**
- the actual contents of a cascade layer (no full-screen view of it exists)
- vertex positions after transform for a large face in the cascade (could clipping drop it?)
- whether the stored-depth bracket view is itself sampling the wrong layer or texel for large receivers
- RenderDoc or a GPU capture (not set up on this machine)

## Next steps (proposed)

1. **Look at the shadow map directly.** Add `r_sunShadowDebug` 13 (or similar): draw cascade layer N full-screen as
   greyscale depth in a 2D pass. One screenshot then shows what each cascade actually holds.
2. **Separate size from distance.** Use a grid of slabs in the studio, inside the camera's view:
   - small near, small far, large near, large far
   - shadows aimed at floor points the `studio_34` view shows
   - fix `mk_dist.py`'s placement (keep x < 640)
3. Depending on the result, check either the cascade box and projection (distance) or primitive clipping / vertex
   transform of large faces (size).
4. Only then: the studio fix (renderer, or map if it is a content limitation). After that, the deck lighting
   (unfinished, below).

## Tools and evidence

- **Look runs:** `.codex-research/eve2b-best-20261012/scripts/run_look_np.py <tag> --showcase [--showcase-pk3
  <pk3>] [--saber-on] [--pbr <pack>] [--set cvar=value ...]`.
  - Runs the installed `GameData/xenjk.x86.exe` hidden and muted, with a private homepath.
  - `cl_renderer` is passed on the command line, which is where xenjk reads it (`tools/renderer_scenes.py`).
  - A side-by-side test renderer goes in GameData as `rd-vulkanmat_x86.dll` with `--set cl_renderer=rd-vulkanmat`
    (owner-approved pattern; move it to `.codex-trash` afterwards).
  - Shots land in `runs/<tag>/home/base/screenshots/eve_{deck,studio}_*.jpg`.
- **Run tags from this investigation** (in that folder's `runs/`):
  - D1/D2: cascade tint, shadow value
  - L1-L4: double shell; depth-range fix
  - L5, L16: stored depth
  - L6: sort counts
  - L7: cull none
  - L8-L12: deck slab
  - L13: face dump
  - L14: stock studio
  - L15: studio slab
  - L17: no VBO
  - L18: backend list
  - L19: liners
  - L20: distance
  - L21: stage order
- **Map variants:** `.codex-research/showcase_map/sun-shadow-tests/mk_*.py` patch the build script into a variant
  script. Edit each one's `dst` path, which points at a deleted session scratchpad, then run the variant with
  `--out .codex-research/showcase_map/build/<name>`. Builds are in `.codex-research/showcase_map/build/`.
- **Renderer build:** `cmake --build .codex-research/eve2b-best-20261012/build --config RelWithDebInfo --target
  rd-vulkan_x86`. Shaders: `python tools/renderer_spirv.py build --tree codemp/rd-vulkan` (Khronos glslang in
  `<glslang>`).

## Showcase map state (`tools/showcase_map/`, untracked, owned by this work since 2026-10-13)

Owner feedback:
- deck: front or back of the model looks good, never both from one spot
- studio: moving shadows on the walls (this bug)

Changes in `build_showcase_map.py`, not yet judged by the owner:
- studio double shell: opaque inner, sky outer, with an 8-unit gap
- `SUN_ELEV` 30 (was 16)
- `SKY_LIGHT` 500 (was 160)
- new `DECK_FILL` 2600: a cool light behind-right of the stage
- `AMBER` 5000 (was 2600)

Effect on the model's back was small (run L22).

2026-10-13, after the shadow fix (runs in `.codex-research/eve2b-best-20261012/runs/`):
- DK1/DK2: `full3` vs `full5` (sun 240, `_ambient` 28) on the fixed renderer; the back is still dark in both.
- LG9/LG5 (`r_sunShadowDebug` 9 and 5): the model gets about as much light on its back as on its front, so the dark
  back is the black outfit without highlights, not missing light. Sheen and rim need bright reflected surroundings
  and a light behind; the light grid gives a model one direction per cell, so baked fills mostly turn into ambient.
- Owner direction: tailor stages that bring out the model, reached by teleporters, rather than fixing the deck.
- Added a Cornell box (`cornell()`, below the studio, the deck's second pad at (0, 460)); `run_look_np.py --cornell`
  shoots it. Its panel is `CORNELL_LIGHT` 12000 with `q3map_lightSubdivide 48`: under `-light -fast`, 16-unit pieces
  lit nothing below about 20000 (CB_L4000/L10000 look like the 900 build). Build `cornell_S48`, run CB_S48.
- Owner: "you pick, make a nice level". Added `ruins()`, the hero stage and new spawn: a pale overgrown temple
  terrace (stock `yavin/` textures) under its own Yavin sky at z -4500, the subject facing away from the sun (rim)
  with the bright sky and stone in front. Its sky repeats the deck's `q3gl2_sun` exactly: the renderer keeps one live
  sun, from the last sky shader parsed. The light grid is `64 64 64` now (it spans every stage). Build `ruins2`, run
  RU2 (`--ruins --cornell`), sheet `showcase_four_stages.jpg`: 2B reads from front, side and back. Look-run
  `setviewpos` teleports push her forward off the 110-unit dais; a real spawn does not. Not installed in the play
  home. The next idea, held back pending this investigation, is a worldspawn
`_ambient` (about 28) and `SUN_LIVE` 320 → 240. Installed play copy: `full1` in
`.codex-research/eve2b-play-20261012/home/base/`.
