# Beauclair: extracting Toussaint from The Witcher 3 yourself, and how xenjk renders it

This chapter takes the city of Beauclair (The Witcher 3: Wild Hunt, the Blood and Wine expansion) from your own copy
of the game to a JKA map you can walk around in xenjk: a gameplay BSP built from the game's own PhysX collision, plus
an rd-vulkan render scene with the game's meshes, LODs, materials, terrain, trees, decals, water and daylight. The
second half covers the renderer work that is specific to the Witcher 3: the scene's second vertex stream, the
texture-array blend materials and their `.xw3b` sidecar, the terrain shader, the neutral ambient reflection,
the day sun, coarse shading of far surfaces, and SpeedTree billboards.

**Status.** The renderer side is in `master` and in the installed game: since Install 94 both Beauclair launchers
load the installed `rd-vulkan` and no longer need a kit renderer. Two maps have been built: the castle pilot (`w3_castle`,
Install 85) and the whole city (`w3_city`, Install 91, coarse shading on since 2026-11-19). The converter is in
`master` under `tools/levelport/w3*.py`. Parts of the chain still depend on files that are not in the repo (see
[Limits](#limits-and-open-issues)).

**Converted levels are for local play only.** You extract from your own installed copy, and the packs stay on your
machine. Never commit, upload or publish them, and nothing in this chapter needs you to.

## What it does

The pipeline follows the two-layer design of the general level port ([levelport.md](levelport.md)):

```
your Witcher 3 install (.bundle, texture.cache, collision.cache)
        |  Witcher3-Blender-Tools add-on (headless Blender) materializes files into its uncook folder
        v
 .w2l layers, .w2w world, .w2mesh, .w2mg/.w2mi, .srt, .redcloth, .env, .w2p
        |  w3gltf / w3trees / w3flags / w3terrain / w3foliage / w3autohide      (Blender + add-on)
        v
 work folder: placements.json, materials.json, collision.json, meshes/*.glb, terrain/, autohide.json
        |                                   |
   w3build -> gltf2map -> q3map2       w3scene (+ w3water, vcache)      w3decals, w3env, w3grade, w3fx
        v                                   v                                 v
 zz_<map>.pk3 (gameplay BSP,         zz_<map>_scene.pk3 (.xscn, .xw3b,  zz_<map>_decals/_env/_grade/_fx.pk3
 collision only, light grid)         .xw3t, .xwat, BC1/BC7 DDS)         (.xdcl, .xenv, .xlut, .xfx)
```

- **Reading the game.** The Next-Gen release packs everything (DLC included) into `content/content0/bundles/*.bundle`,
  with textures in `texture.cache` and collision in `collision.cache`. WolvenKit 7.2's CLI cannot unpack those
  bundles, and the pipeline does not use `wcc_lite`'s uncook either. Instead, dingdio's **Witcher3-Blender-Tools** add-on
  (extension `witcher3_tools`) reads the bundles with its own bundle manager, materializes every file it needs into
  its uncook folder under its depot path (`.xbm` textures come out as `.dds`, collision as `.nxs`), and parses the
  CR2W formats. Every Blender-side script imports the add-on's readers from `bl_ext.user_default.witcher3_tools`.
  `w3bundle.py` is a plain-Python bundle reader for the one job where the add-on falls short: the cooked particle
  emitters.
- **One space.** Every work-folder file is in Witcher 3 game space: metres, +Z up. Mesh glbs are mesh-local in those
  axes, which is *not* glTF's +Y up. `w3build` and `w3scene` both convert with the same origin (the build record's,
  `<name>-<tag>.build.json`), so the BSP and the scene line up. Scene units are (game - origin) x 37.5.
- **Gameplay BSP.** The game's PhysX shapes are better collision than anything derived from render meshes. About
  91% of the shapes are boxes (exact brushes); convex hulls become one brush each; spheres and capsules become the hull of 26
  support points; triangle meshes and the terrain become slabs. All of them are detail brushes (a structural build
  stalled q3map2 in portal warnings). Each brush carries a JKA surface material mapped from the game's physical
  material (`stone_solid` to `rock`, `clay_tile` to `tiles`, and so on: `JKA_MATERIAL` in `w3build.py`), written as
  `q3map_material` so footsteps and impacts follow the surface. The BSP draws nothing. It holds collision, a clip
  box, a lid, spawns and the game's lights for the light grid. Where visible floors lack collision,
  `w3build --fill` adds slabs from the render meshes' walkable LOD 0 triangles.
- **Render scene.** `w3scene.py` writes the same `.xscn` the Night City scenes use (version 6): every mesh once with
  all its LODs (the game's switch distances turned into screen sizes), one instance per placement, the lights, and the
  terrain as 64 m chunks plus a render-only far ring. Hide distances are the game's own (`autoHideDistance`,
  sector and component overrides, x 1.5 from the game's `MeshRenderingDistanceScale`, plus the instance radius). The
  game's `entity_proxy` shells draw only past their near distance, and the full meshes inside a proxy's box hide at
  that same distance, fading against the proxy's centre. Triangles are reordered for the vertex cache
  (`vcache.c`, Forsyth). Everything the stock `.xscn` cannot say goes into Witcher 3 sidecars.
- **Materials.** The game's shaders are mapped one by one. `Witcher3_Main`, glass and ivy take diffuse, normal
  (DirectX to OpenGL) and the roughness in the normal's alpha (x `RSpecScale` + `RSpecBase`). `pbr_vert_blend_colorize`
  blends texture-array layers by vertex colour at draw time. `pbr_std_wear_paint` and the `snow.w2mg` rock slope
  layer have their own records. Waterfalls and rivers scroll. Each material records the inputs as the add-on wired
  them (`materials.json` "nodes"), because the full chain (instance, `.w2mi`, `.w2mg` defaults) exists only there.
- **Daylight.** `w3env.py` evaluates the world's environment definition (`sunny_bob_v7.env`) at an hour (14:00 for
  the installed maps). It writes the sun's direction from the game's sun trajectory, the sun and ambient colours, the sky, haze and
  exposure into `maps/<map>.xenv`. `w3grade.py` bakes the game's display transform (exposure key, tone curve,
  gamma, the 64^3 balance map) into a `.xlut`.

## Take this piece

**Depends on:** [levelport.md](levelport.md) (gltf2map, our q3map2, the render scene and its formats) and
[renderer.md](renderer.md) (rd-vulkan as xenjk builds it, GPU-driven draws, sun cascades). The `.xdcl` decal and
`.xfx` particle formats are Night City's ([night-city.md](night-city.md)); the Witcher 3 tools reuse their writers.

### Files

New converter files (copy whole; all under `tools/levelport/`):

| Path | Role |
|---|---|
| `tools/levelport/w3gltf.py` | Blender: layers to a work folder (placements, materials with their wired inputs, lights, fx positions, collision shapes, one glb per `.w2mesh` with every LOD, both UV sets, raw vertex colour, tangents) |
| `tools/levelport/w3terrain.py` | Blender: the world's terrain tiles under a box as vertex grids plus their control/tint buffers (`terrain/tiles.npz`, `tiles.json`) |
| `tools/levelport/w3trees.py` | Blender, then plain Python: SpeedTree `.srt` placements to mesh glbs with every LOD, leaf-card materials, billboard cards and top cards |
| `tools/levelport/w3foliage.py` | Blender: the world's terrain foliage (vineyards, pines, grass, lilies) under a box as `srt` placements; `--merge` appends them |
| `tools/levelport/w3flags.py` | Blender: APEX cloth (flags, banners, laundry) in its bind pose as static meshes in a staging folder |
| `tools/levelport/w3merge.py` | merge a staging folder (flags, water layers) into a work folder, idempotent by `--tag` |
| `tools/levelport/w3city.py` | several work folders (districts) into one |
| `tools/levelport/w3autohide.py` | Blender: the game's hide distances into `<work>/autohide.json` |
| `tools/levelport/w3build.py` | the gameplay glb and build record for `gltf2map.py` |
| `tools/levelport/w3scene.py` | the render scene pack (`.xscn`, `.xw3b`, `.xw3t`, `.xwat`, DDS) |
| `tools/levelport/w3water.py` | sea level, the lakes (levelled from floating lilies) and water meshes; used by `w3scene`, or run alone for a report |
| `tools/levelport/vcache.c`, `vcache.py` | vertex-cache triangle order, compiled on first use (no compiler: order left alone) |
| `tools/levelport/w3decals.py` | Blender `read` stage, plain `build` stage: the layers' packed decals to `.xdcl` |
| `tools/levelport/w3env.py` | Blender: daylight to `maps/<map>.xenv` and `zz_<map>_env.pk3` |
| `tools/levelport/w3grade.py` | Blender: the display transform to `maps/<map>.xlut` and `zz_<map>_grade.pk3` |
| `tools/levelport/w3fx_dump.py` | Blender: `.w2p` particle systems to an emitter JSON |
| `tools/levelport/w3bundle.py` | plain Python: any depot file straight from the `.bundle` files |
| `tools/levelport/w3emitter.py` | plain Python: decodes the cooked emitter modules (life, size, motion, curves, spawn shapes) |
| `tools/levelport/w3fx.py` | the water effects as `maps/<map>.xfx` and `zz_<map>_fx.pk3` |
| `tools/levelport/w3_preview.py` | Blender: rebuild a work folder from its own files and render check views |

Shared converter code it imports: `gltfio.py`, `levelport.py`, `xscn.py` (`SceneImages`, the DDS encoder, the scene
writer; `mip_half_cutout`, the alpha-weighted cutout mips; `cp2gltf.py` re-exports these names), `pk3write.py` (packs
written under a temporary name, shard roll-over), `w3paths.py` (every location the W3 tools read, from an argument or
an environment variable), `w3work.py` (the baselines of a work folder's steps, below), `w3patches.py` (below),
`nier2gltf.py` (`headroom`), `cp77decals.py`, `cp77fx.py`, `gltf2map.py`, `levelcheck.py`.

Renderer (`codemp/rd-vulkan`, all after the import commit e4fe7581; the Witcher 3 commits are listed under
[Further reading](#further-reading)):

| Path | What the Witcher 3 work adds |
|---|---|
| `codemp/rd-vulkan/vk_scene_w3.cpp` | the Witcher 3 side files, moved out of `vk_scene.cpp`: `Scene_LoadW3Blend` (`.xw3b`: the stream, the material records, the slot images; its comment is the spec), `Scene_LoadW3Terrain` (`.xw3t`, spec in the comment above it), `Scene_MarkCardMeshes` (billboard meshes and their casters) |
| `codemp/rd-vulkan/vk_scene.cpp`, `vk_scene_local.h` | the stream at draw-set binding 27, `Scene_LoadEnv` (`.xenv`: `time day`, `sun`, look cvars), the VRS pipeline state and the `r_scene*` cvars; `vk_scene_local.h` declares what `vk_scene_w3.cpp` shares |
| `codemp/rd-vulkan/shaders/glsl/scene/scene.vert` | reads the stream at `gl_VertexIndex` (`vUV1`, `vCol`); billboard cards turned to the eye (-9, -10); `gl_PrimitiveShadingRateEXT` under `#ifdef VRS`; proxy-member fade (instance bit 27) |
| `codemp/rd-vulkan/shaders/glsl/scene/scene.frag` | the Witcher 3 material codes (below), the terrain shader (`w3TerrainLayer`, `w3TerrainBg`), the neutral ambient reflection, the reflective particle mode |
| `codemp/rd-vulkan/shaders/glsl/scene/scene_cull.comp`, `scene_want.comp` | proxy members' hide distance measured from their proxy's centre |
| `codemp/rd-vulkan/shaders/glsl/scene/scene_ssr.comp` | `r_sceneAO` (screen-space occlusion on the ambient), computed beside the reflections |
| `codemp/rd-vulkan/vk_instance.cpp`, `vk_local.h` | `VK_KHR_fragment_shading_rate` detection (`vk.shadingRate`, only with the GPU-driven draws) |

Material detail codes in `scene.frag` (`md.x`, from the material's detail record):

| Code | Source shader | Sidecar record |
|---|---|---|
| -2 | `pbr_vert_blend_colorize`: up to 8 array layers blended by vertex colour, pattern on UV 1 | `.xw3b` layer record |
| -3 | terrain (`.xw3t`): four control taps, overlay and background layers, slope blend, tint | `.xw3t` tile |
| -5 | a material with its own specular colour F0 and no layers | `.xw3b` version 2 |
| -6 | `pbr_std_wear_paint`: paint by vertex blue, wear by 1 - vertex alpha, AO by red | `.xw3b` kind 1 |
| -7 | `snow.w2mg` rock with a grass layer where the world normal faces up | `.xw3b` kind 2 |
| -8 | flowing water: base, normal and ORM scrolled by the game's clock (waterfalls, rivers, dams) | `.xw3b` kind 3 |
| -9 / -10 | SpeedTree billboard card turned about the tree's up axis / the top card | `.xw3b` version 3 |

There are no hooks in stock files beyond what [renderer.md](renderer.md) and [levelport.md](levelport.md) already
describe: the Witcher 3 work lives entirely in the render scene's files.

### Build inputs

- No CMake entries of its own: `vk_scene_w3.cpp` is listed with the rest of the render scene in
  `codemp/rd-vulkan/CMakeLists.txt`, and the shaders are the scene shaders.
- After a scene shader change, regenerate `codemp/rd-vulkan/shaders/spirv/scene_shader_data.c` with
  `python tools/scene_spirv.py` (glslang, found as `renderer_spirv.py` finds it: `GLSLANG_EXE`, else `PATH`). The VRS variant
  `scene_vert_gd_vrs_spv` is `scene.vert` compiled with `-DGPU_DRIVEN -DVRS`.
- Converter: Python 3 with numpy (and Pillow, as for the other levelport tools); a C compiler for `vcache.c`
  (optional).

### Cvars

Full list with defaults: [xenjk-cvars.md](../xenjk-cvars.md). The ones that matter here:

| Cvar | Default | Who sets it | What |
|---|---|---|---|
| `r_sceneSun` | `0.25` | `.xenv` (6 for Beauclair) | the scene sun's strength; 0.25 is Night City's moon |
| `r_sceneAmbient`, `r_sceneSky*`, `r_sceneFog*`, `r_sceneExposure`, `r_sceneGrade*`, `r_sceneAO*`, `r_sceneShadowFar`, `r_sceneFoliage`, `r_sceneCharSun` | various | `.xenv` | look values; the console's value wins once you change it |
| `r_sceneVrs` | `0 0 0` | launcher (`150 150 150`) | metres past which terrain, SpeedTree leaves and everything else shade once per 2 x 2 pixels |
| `r_sceneTerrainPush` | `0` | launcher (`1`) | terrain drawn a little behind its depth so roads laid in its plane win; read at map load |
| `r_sceneLodBias` | `1` | launcher (`2`) | finer LODs farther |
| `r_sceneSpecAA` | `0` | launcher (`0.18`) | geometric specular anti-aliasing |
| `r_sceneSkip`, `r_sceneTerrainFlat`, `r_sceneOnlyMesh` | `0` | benches only (cheat) | draw classes empty, flat terrain, one mesh alone |

### Data formats

| File | Written by | Spec |
|---|---|---|
| `maps/<map>.xscn` | `w3scene.py` (`--version 6`) | [levelport-scene.md](../levelport-scene.md) |
| `maps/<map>.xw3b` | `w3scene.py` | comment above `xw3bHeader_t` in `vk_scene_w3.cpp`: header `XW3B`, version 1-3, material and vertex counts; 12 bytes a vertex (UV 1 as f32 x 2, the colour's raw u8 x 4); a record a material (version 1: 20 floats + 18 64-byte image paths; version 2: 24 floats with F0; version 3: billboard records) |
| `maps/<map>.xw3t` | `w3scene.py` | comment above `xw3tHeader_t` in `vk_scene_w3.cpp`: layers, their images, per tile the (512 + 1)^2 control lattice as exact 16-bit words |
| `maps/<map>.xwat` | `w3scene.py` | Night City's water contract |
| `maps/<map>.xdcl` | `w3decals.py` | `cp77decals.py`, version 2 |
| `maps/<map>.xenv` | `w3env.py` | comment above `Scene_LoadEnv` in `vk_scene.cpp` (text: `xenv 1`, `time day`, `sun r g b intensity yaw elevation`, look cvars, unquoted values) |
| `maps/<map>.xlut` | `w3grade.py` | `XLUT` version 1, 33^3 rgba16f, LogC input (`w3grade.py` docstring) |
| `maps/<map>.xfx` | `w3fx.py` | `cp77fx.py` |

The `.xw3b` is a sidecar rather than a new `.xscn` version, so no existing scene or loader changes. A renderer that
does not know it draws each blend material's first layer. A renderer that reads only versions 1-2 ignores a
version 3 file entirely (billboards included). The stream is read only for a scene that is not tiled or streamed,
because it is indexed by the file's vertex order.

### Porting notes

- **rd-vulkan only.** The render scene, the sidecars and VRS need rd-vulkan with the render scene and its GPU-driven
  draws. rd-vanilla and rend2 can take only the gameplay BSP (collision, spawns, light grid), which draws nothing.
- **64-bit client.** The city scene pack is 3.4 GB and the 32-bit client ran out of zone memory on a third city load.
  Use the 64-bit client (`xenjk.x86_64`) for the city; the castle fits either.
- **VRS** needs `VK_KHR_fragment_shading_rate` with primitive rates and the GPU-driven path. Without them
  `r_sceneVrs` does nothing.
- **Far plane.** `R_SetFarClip` caps zFar at worldspawn `distanceCull` x 1.732 (default 6000 units). The city map sets
  `distanceCull 200000` so the 2 km terrain ring draws.
- **Witcher 3 in another renderer.** The material mapping is in `w3scene.py` (`Materials`). The blend logic to port is
  the -2 block in `scene.frag`, which follows the Witcher 3 Blender tools' wiring of `pbr_vert_blend_colorize`:
  vertex green x 16 picks a place along the array and mixes its two nearest layers, red tints toward `AO_Tint` by
  `AOPower`, and blue applies the pattern on the second UV set where its mask allows.

## Do it yourself

You need:

- **The Witcher 3: Wild Hunt** with **Blood and Wine** installed (the Next-Gen release; its bundles are what the
  add-on reads).
- **Blender 5.1** (the version used; the add-on's manifest asks for at least 4.5), run headless (`blender -b`).
- **Witcher3-Blender-Tools 1.1.0** (dingdio), installed as a Blender *extension* in the default user repository, so
  that it imports as `bl_ext.user_default.witcher3_tools`. In its preferences, set the game path (the folder that holds
  `witcher3.exe`; it auto-detects one) and the uncook path.
- **io_mesh_srt** (SpeedTree import) for trees and foliage; **io_mesh_apx** and NVIDIA's ParamTool (built from PhysX
  3.4 source) only for cloth (`w3flags.py`).
- **The add-on's uncook folder** (its "uncook path" preference): `$W3_UNCOOK`, or `$W3_RESEARCH/uncook` for a
  research folder laid out that way (`--w3 <research>` on `w3trees.py`, `w3flags.py --render`, `w3scene.py` and
  `w3fx.py` overrides both). `w3trees.py` reads `.srt` and their JSON from it, `w3scene.py` and `w3fx.py` the water
  textures. `tools/levelport/w3paths.py` lists every location the W3 tools read.
- **`tools/levelport/w3patches.py`**: runtime patches to the add-on, applied by every Blender script before it
  imports. It unwraps CR2W-wrapped `.srt` files to their raw `SRT 07` blob before the SpeedTree JSON converter sees
  them, and it drops the unsupported `BufferV1` per-instance override that made door layers abort.
- **q3map2**: xenjk's patched build, which `gltf2map.py` finds through `Q3MAP2` ([levelport.md](levelport.md)); there
  is no default location.
- **Environment.** Every location outside the checkout comes from an argument or a variable (`w3paths.py`); there is
  no machine default, and a missing one ends the run naming it. The shared level-port variables (`XENJK_GAMEDATA`,
  `XENJK_RESEARCH`, `Q3MAP2`, `XENJK_X64`) are in [levelport.md](levelport.md).

  | Variable | Used by | Meaning |
  | --- | --- | --- |
  | `W3_GAME` | `w3bundle.py`, `w3emitter.py` (`--game`) | the game folder, the one holding `bin/` and `content/`; `W3_ROOT` is read when it is not set |
  | `W3_UNCOOK` | `w3trees.py`, `w3scene.py`, `w3fx.py`, `w3flags.py` | the add-on's uncook folder (its "uncook path" preference); else `$W3_RESEARCH/uncook` |
  | `W3_RESEARCH` | the same | a research folder whose `uncook/` is that folder; the tools' `--w3 <research>` overrides it |
  | `W3_DOBOZ` | `w3bundle.py` | the add-on's `CR2W/witcher_cache/Bundles/native/Doboz.dll`, for the bundle files compressed with doboz |
  | `Q3MAP2` | `gltf2map.py` | the patched q3map2 |

Run everything from the repo root. Blender processes that share an uncook folder race on the files they
materialize: run one at a time, or dump in parallel with `w3par.py` (below), which gives each its own folder.

### 1. Choose the layers

A layer list is a text file with one `.w2l` path a line, relative to
`dlc/bob/data/levels/bob/locations/city_of_beauclair/` (or a full `dlc/...` depot path). `#` starts a comment.

- **Castle pilot:** the `palace/main/**` layers (29 visual layers: `upper_palace`, `lower_palace`, `tower_cell`,
  `room_with_mirror`, the castle-hill `cliffs`, the two castle roads) plus `palace/main/lights/lights_exterior.w2l` and
  `lights_interior.w2l`. That is about 250 x 235 m, 4,200 placements, 5,184 collision shapes, and one q3map2 run.
- **The city:** about 280 more layers outside the castle, grouped into districts (palace grounds, upper city, lower
  city, harbour, lights, minor locations). Leave out blockout, proxy-only, temp, quest (`q70x`, `qq70x`), `navi`,
  `volumes` and `dimmers` layers; keep the light layers. Add the water layers too: `palace/main/solid/fx/water`,
  `palace/bridge/fx/water`, `palace/gardens/blockout/water` and the world's `fx/rivers/bob_river*` layers. Dump
  them as their own district.

### 2. Dump the layers (Blender)

```
blender -b --python tools/levelport/w3gltf.py -- --layers <layers.txt> --work <work>
blender -b --python tools/levelport/w3_preview.py -- --work <work> [--lod 0] [--colors] [--res 1600]
```

`w3gltf` imports each layer with the add-on (meshes, collision, rigid bodies; cloth off), reads that layer's objects
back and removes them before the next layer (meshes and materials stay for reuse; materials are read once every layer
is in, since the add-on completes some node graphs while importing later layers), then reads every placed `.w2mesh`
again with the add-on's raw reader for every LOD, both UV sets and the raw colour. `--save-blend` saves the last
scene only. Clearing each layer keeps every import at fresh-scene speed: the add-on slows as a scene fills (Blender
helpers scan every object), and the upper city's 124 layers in one filled scene took 356 minutes (2026-11-17).

Several lists at once, each Blender with its own uncook folder, merged into the shared one afterwards (the parts'
JSON paths rewritten to it); a part whose `summary.json` exists is skipped:

```
python tools/levelport/w3par.py --root <workers folder> --uncook <shared uncook> --workers 3 \
    --work-prefix <dir>/work-d- --part upper_west1=<list> --part upper_west2=<list> ...
```

Keep parts to about 20-30 layers. The 2026-11-24 extraction (remaster 5.0): 11 parts on 3 workers in 66 minutes
(the heaviest, 29-31 layers, 42-46 minutes); the whole city, from the first dump to the hide distances, about 3 hours
with half of it on the old path. `w3_preview` renders top, perspective and collision-only views from the dump
alone. Check them against the add-on's own import.

Then the trees, cloth and the hide distances:

```
blender -b --python tools/levelport/w3trees.py -- --work <work>
blender -b --python tools/levelport/w3flags.py -- --out <stage> --layer <cloth layer>
python tools/levelport/w3merge.py --work <work> --stage <stage> --tag <name> --drop box_cloth.w2mesh
```

`w3trees` converts every `srt` placement into a `w2mesh` placement. Re-running starts from its `*.pre-trees.json`
copies, so trees never stack. For billboards it bakes the top cards from LOD 0 in Blender (`--bake-tops [--trees
a,b]`). On a fresh work folder that takes three runs: a write (it reports the trees still without a top image), the
bake from the LOD 0 glbs it wrote, and a second write. Afterwards a plain-Python write (`python tools/levelport/w3trees.py --work <work> --update`) rewrites only the trees
on a work folder that later steps have built on. `w3flags` takes one cloth layer at a time (the castle's
`palace/main/solid/flags/decorations_ns.w2l` is its default). Its `--drop box_cloth.w2mesh` removes the blockout
boxes the game hides.

### 3. One work folder, terrain and foliage

For the city, merge the districts (castle first; a layer given twice keeps the first source's copy), then fetch the
terrain under the city's box, the far ring and the foliage:

```
python tools/levelport/w3city.py --out <city> <castle work> <district work> ...
blender -b --python tools/levelport/w3terrain.py -- --work <city> --box -900 -1620 -130 -950
blender -b --python tools/levelport/w3terrain.py -- --work <city> --box -1740 -2490 740 -10 --sub terrain_far
    (the far ring: render only)
blender -b --python tools/levelport/w3foliage.py -- --work <city> --box <minx miny maxx maxy>
python tools/levelport/w3foliage.py --work <city> --merge
blender -b --python tools/levelport/w3trees.py -- --work <city>
blender -b --python tools/levelport/w3autohide.py -- --work <city>
```

The steps that rewrite the work files (`w3foliage --merge`, `w3trees`, `w3merge`) start each re-run from the copy
they made on their first run (`*.pre-<step>.json`). When an earlier step has run again since that copy was made, the
re-run is refused: starting from it would drop the earlier step's new output. `--rebase` sets the copies aside (under
`<work>/stale-baselines/`) and starts from the current files; `w3trees --update` works on them in place. A new
`w3gltf` or `w3city` export into the folder sets every copy aside.

Boxes are game metres (`minx miny maxx maxy`). For the castle alone, one box (its build box was
`-830 -1360 -520 -1070`) serves `w3terrain` and `w3build`, and it needs no far ring.
`w3terrain` defaults to `--world dlc/bob/data/levels/bob/bob.w2w` and `--multires 8`. `w3foliage --skip <regex>`
leaves out `.srt` paths. Water layers dumped as their own work folder go in with
`w3merge.py --work <city> --stage <water work> --tag water`.

### 4. Gameplay BSP

```
python tools/levelport/w3build.py --work <city> --name city --tag 1 --box -960 -1725 -110 -930 \
    --spawn-radius 150 --terrain-step 4
python tools/levelport/gltf2map.py <city>/city-1.glb w3_city --out <builds>/w3_city-1 \
    --sky yavin --sun "1 0.95 0.85 120 220 45" --skylight 120 --worldspawn _ambient=20 \
    --worldspawn distanceCull=200000 --light fast --vis none
python tools/levelport/levelcheck.py spawns <builds>/w3_city-1/zz_w3_city.pk3
```

`w3build` writes `<work>/<name>-<tag>.glb` and `<name>-<tag>.build.json` (box and origin; `--origin X Y Z` overrides
the box centre). Other options: `--spawn-centre X Y`, `--spawns 16`, `--spawn-sky 6`, `--top Z` (lid; default the
highest collision + 40 m), `--terrain-step` (terrain slabs every Nth ~1 m vertex: 2 for the castle, 4 for the city,
since 2 would be about 700 k slabs), `--terrain-material dirt`, `--light-scale`. `--vis none` is the levelport rule
for an open map. The city compiles as 1.76 M brushes in tiles, which takes 12 minutes. The castle compiles in one run
of about 60 s.

After the scene exists (step 5), find where visible floors lack collision and rebuild with the fill:

```
python tools/levelport/levelcheck.py compare <builds>/w3_city-1/zz_w3_city.pk3 <scene>/zz_w3_city_scene.pk3 <compare> --zmax 1e5
python tools/levelport/w3build.py ... --tag 2 --fill <compare>      (then gltf2map and spawns again)
```

`--zmax 1e5` is needed because this map sits 2,000-10,000 units above its origin, while the default judges only
surfaces up to Night City's collision height.

### 5. Render scene

```
python tools/levelport/w3scene.py --work <city> --build-json <city>/city-1.build.json --map w3_city --tag 1 --version 6
```

The scene is written to `.codex-research/levelport/build/w3_city_scene-<tag>/` in the checkout (or `--out`) as `zz_w3_city_scene.pk3`,
with a `materials.json` beside it. The defaults are the installed choices: `--tex-size 2048` (ORM maps stay at
1024), `--autohide-scale 1.5`, `--bb-fade 30 50` (the pitch band where the top card takes over), `--terrain-step 1`,
`--far-step 8`. `--version` defaults to 5. Pass 6, which the current renderer reads (it adds light shapes). A/B
switches: `--no-layers`, `--no-water`, `--no-vcache`, `--no-autohide`. `--card-match <match.json>` writes each
billboard card's light match to its 3D tree. The tool that measures those matches is local research, not part of the
repo. Without it the cards draw unmatched, a little paler than their trees. The city build takes about 17 minutes.

### 6. Decals, daylight, grade, effects

```
blender -b --python tools/levelport/w3decals.py -- read --layers <layers.txt> --out <decals raw>
python -I tools/levelport/w3decals.py build --raw <decals raw>/decals_raw.json --work <city> \
    --build-json <city>/city-1.build.json --map w3_city [--max-px 1024] [--verify]

blender -b --python tools/levelport/w3env.py -- --out <env> --map w3_city --hour 14 --sun 6 --ambient 0.2 \
    --grade --grade-exposure 24 --foliage 0.7 --shadow-far 12000
blender -b --python tools/levelport/w3grade.py -- --out <grade> --map w3_city --hour 14
```

Those `w3env` values are the installed city's. The calibration is a matter of looking; the tool's defaults (sun 4,
ambient 0.8, grade exposure 8) are an earlier pass. Other options: `--sky` (default: the game's own sky-to-sun ratio),
`--sky-curve 2`, `--clouds 0.5` and `--cloud-scale 3` (the renderer's cumulus as a stand-in for the game's particle
clouds), `--ground haze:0.85` (the land under the horizon), `--fog`, `--exposure`, `--ao 1`, `--ao-radius 3`,
`--world`. `w3decals build` numbers its own output folder under `--builds`, which defaults to
`.codex-research/levelport/build` in the checkout (pass `--builds` if you set `XENJK_RESEARCH`).

Water effects (splashes, mist, foam, rings, fountain jets):

```
blender -b --python tools/levelport/w3fx_dump.py -- --list <w2p list.txt> --out <fx dump.json>
python tools/levelport/w3emitter.py --list <w2p list.txt> -o <decoded.json> [--game <game folder>] [--index <cache.pkl>]
python tools/levelport/w3fx.py --dump <fx dump.json> --placements <sector particles.json> --out <fx> \
    --map w3_city --decoded <decoded.json> [--entity-fx <city>/placements.json] \
    --build-json <city>/city-1.build.json
```

The placements' box and origin are the build record's (`--box` and `--origin` override them; without a record both
are needed). The sector particle list (path, position,
rotation, emission scale of each particle object in the layers' sector data) has no reader in the repo (see
Limits).

### 7. Install and launch

Copy the packs into your game's `base/`: `zz_w3_city.pk3`, `zz_w3_city_scene.pk3`, `zz_w3_city_decals.pk3`,
`zz_w3_city_env.pk3`, `zz_w3_city_grade.pk3` and `zz_w3_city_fx.pk3`. The castle has the same set as `zz_w3_castle*`,
without effects. A launcher is a batch file beside the game folder that starts the 64-bit client and loads the map
on a local listen server:

```
cd /d "%~dp0bin64"
start "" "%~dp0bin64\xenjk.x86_64.exe" +set fs_basepath "<your GameData>" +set cl_renderer rd-vulkan ^
    +set r_sceneTerrainPush 1 +set r_sceneLodBias 2 +set r_sceneSpecAA 0.18 +set r_sceneVrs "150 150 150" ^
    +set net_ip 127.0.0.1 +set sv_pure 0 +devmap w3_city
```

The launcher sets only what is read at map load (terrain push, LOD bias, specular AA) and the coarse shading.
`r_sceneVrs` is archived, so once set it stays in your config; `r_sceneVrs "0 0 0"` turns it off. The light is not on the command
line: `r_sceneSun`, the ambient, sky, haze, AO and shadow reach come from the map's `.xenv`, and the `sun` line there
replaces the map's sun direction and colour. The castle launcher is just `+devmap w3_castle`. Keep the command
line short: past about 32 `+` commands the extra settings drop.

## How xenjk renders it

**The second vertex stream.** The scene vertex stays 28 bytes (position, normal, tangent, one UV). The Witcher 3
architecture shaders need a second UV set and the raw vertex colour. These come in `maps/<map>.xw3b` as 12 bytes a vertex
in the scene's vertex order: UV 1 as two floats, the colour as the game's four bytes. `vk_scene_w3.cpp` reads them and `vk_scene.cpp` uploads them
as a storage buffer at draw-set binding 27, and `scene.vert` reads them at `gl_VertexIndex` into `vUV1` / `vCol`. No
vertex format, pipeline layout or tile manifest changes, so Night City scenes are unaffected. Vertices without the
data get zeros, terrain included (the terrain keeps its tile UV in UV 1).

**Texture-array blends.** A `pbr_vert_blend_colorize` material's record names up to 8 layers' colour and normal
images (roughness in the normal's alpha), a pattern and its mask, plus 20 (later 24) floats. The loader appends
the images to the slot images at the start of the GPU-driven image array and gives the material detail code -2 with
a 9-vec4 block. `scene.frag` then blends at draw time (see the porting note above), so the result is sharp at any
distance with no per-mesh bake. That avoids the soft, blobby ground Watson's bakes gave. Wear-paint (-6),
the rock slope layer (-7) and flowing water (-8) are further record kinds in the same file. Version 2 added each
material's specular colour F0 (taken only from a *wired* `SpecularColor`; unwired inputs hold the group's default 1.0,
which made mirrors).

**Terrain.** `.xw3t` carries each tile's control lattice as exact 16-bit words in the material-detail buffer (not an
image, which the loader might resize or compress). Per fragment, four control taps pick an overlay layer and a
background layer at the tap's UV scale. Both are read in three projections weighted by the facing, blended by
slope against the tap's threshold, and placed under the tile's tint. Holes (control word 0) are cut from both the
collision and the render triangles. A layer that several taps share is read once at their summed weight. The
background UV scales are the ones the game's own DX12 shader uses (read from its shader cache), not the Blender tools'.

**Neutral ambient reflection.** Witcher 3 materials (-2, -5, -6, -7) mirror the ambient through the split-sum BRDF
with their own F0, instead of the flat F0 / 4 other scene materials use. The mirrored light is the ambient
hemisphere along the reflected ray (ground to sky by `R.z`), because the game lights reflections from near-neutral
probes. The first version mirrored the water sky, whose horizon end is the fog colour as linear light. With
Beauclair's cyan haze, every wall and pavement seen at a glancing angle turned bright cyan (commit 49b3f1815).

**Sun, not moon.** The render scene's sun was built for Night City, where `r_sceneSun 0.25` is the moon. A day map's
`.xenv` says `time day` (night-only lights dropped, night emissives scaled down), gives a `sun` line with the game's
light direction (the trajectory's light direction, which sits higher than the drawn sun disc) and colour, and sets
`r_sceneSun 6` against `r_sceneAmbient 0.2` (the game's ratio, about 182 : 4). `r_sceneCharSun 1` lights player
models with the same sun. The grade carries the exposure (`r_sceneGrade 1`, `r_sceneGradeExposure 24`). Sun cascades
blend into the next before their edge, both across the screen and in depth along the light, and fade out over the last part of their
reach. That removed the shadow bands that appeared and changed sharpness as the kept cascades stepped a cell.
`r_sceneShadowFar 12000` lengthens the far cascade so the castle keeps its shadows from the air.

**Coarse shading far away (`r_sceneVrs`).** With `VK_KHR_fragment_shading_rate`, the GPU-driven lit pipelines take
the primitive's rate (`combinerOps` REPLACE then KEEP). `scene.vert` sets `gl_PrimitiveShadingRateEXT` to 2 x 2 when
the provoking vertex is beyond the distance for its class: x terrain, y SpeedTree leaves, z everything else, in
metres. Edges and depth stay per pixel; only the colour inside a block is shared. The cost is a slight softening
of far texture detail.

**Foliage: billboards and mips.** `w3trees` reads each SpeedTree's vertical billboard views (3-12 views, cutout
outline, the `_Billboards.dds` and normal atlases) and starts them at the tree's `m_fBillboardStartDistance`. A card
(-9) turns about the tree's up axis to the eye, shows the stored view nearest the viewing direction, and crossfades
to the next. A horizontal top card (-10) baked from LOD 0 takes over between 30 and 50 degrees of pitch. A mesh
that ends in a card casts its coarsest 3D LOD as the sun caster while the eye sees 3D, because a card turned to the
eye shadowed its own tree in a shape that followed the eye. Per-tree matches scale a card's ambient and sun to its 3D
tree, and the sun wraps round a card from any side. On the converter side, cutout mips weight each 2 x 2 box's colour
by its alpha (`xscn.mip_half_cutout`, shared with CP2077), so the black under clear leaf texels no longer darkens
far foliage. Leaves are flagged 0x800 for `r_sceneFoliage` canopy shading. Wall plants and hedges keep fixed crossed planes.

**Smaller pieces.** Proxy members fade against their proxy's centre (instance bit 27, the centre as a signed byte an
axis in 16-unit steps). Vegetation and small props take no projected decals (instance bit 25). Terrain is flagged
0x1000 for `r_sceneTerrainPush`. Water particles have a reflective normal-mapped mode (flag 32768: the game's splash
textures are normal maps, lit by the sky with Fresnel and the sun's glint). Foam and rings lie in their emitter's
plane, and gravity acts in world space. `r_sceneAO` adds screen-space occlusion on the ambient only.

**Measured at 4K** (RTX 3080, MSAA 4, 64-bit client, the GPU shared with other programs, so launches vary up to 20%;
the Beauclair record's "Pop-in at the billboard switch" section):

| Path (fly-through) | GPU ms, VRS off | fps | GPU ms, `r_sceneVrs 150 150 150` | fps |
|---|---|---|---|---|
| aerial approach (whole city) | 12.6-12.7 | 73 | 10.7-10.9 | 84-85 |
| roofs | 8.6-8.7 | 106-108 | 7.5-7.7 | 120-122 |
| lower street | 6.8 | 131 | 6.7 | 133 |

From the air the frame was geometry-bound (24 M triangles before the game's hide distances, 7.3 M after, 2.8 M with
billboards). Of the shading, the terrain was the largest single cost until shared layers were read once (11-19% off every path).
The CPU is under 1.1 ms a frame in every view. The installed packs are 3.85 GB (the scene 3.43 GB, nearly all of it
BC7/BC1 textures).

## Limits and open issues

- **Not all in the repo.** The district layer lists, the sector particle reader that feeds `w3fx.py --placements`, and the billboard
  card matcher are local research scripts. The add-on, the SpeedTree add-on and ParamTool are third-party.
- **Look gaps.** The sky is the renderer's analytic gradient with a cumulus stand-in (the game's clouds are particle
  systems, not converted). The game's sun-side horizon colours and sun halo are not modelled. Water colour and waves
  are ours. Some kerbs and pavements are near-mirror (the game's roughness, unverified against the game). Grass hides past 60 m, and leaves have no wind.
  Paint and wear roughness are not applied. VarianceColor is read as a tint on cloth only.
- **Collision.** Some rich houses' base floors lack collision, and ivy and flower beds have none, as in the game. Doors
  and rigid bodies are static. There is no Bot2 navmesh.
- **One resident scene.** Beauclair is one `.xscn`, with no geometry or image streaming (those need a tiled scene).
  It costs VRAM and load time more than frame rate.
- **Far LODs.** 3,518 of 4,811 meshes have one LOD and draw it at every distance. A mesh simplifier would cut the
  triangle count, but none is installed.
- **One hour.** The day is fixed at 14:00 (`sunny_bob_v7`).

## Further reading

- [levelport-beauclair.md](../levelport-beauclair.md): the plan, every phase's result, the measurements and the
  dead ends (decision A, terrain, daylight calibration, billboards, timing). Its "Resume here" header predates the
  merge into `master`.
- [levelport-scene.md](../levelport-scene.md): the render scene design and the `.xscn` format.
- renderer-vulkan-in-xenjk.md, Installs 85, 88, 89, 91 and 94: what each Beauclair
  install changed and how it was checked.
- [levelport-collision.md](../levelport-collision.md): the q3map2 patches and tiled compiles the city map relies on.
- [xenjk-cvars.md](../xenjk-cvars.md): every `r_scene*` cvar with its default.
- [levelport.md](levelport.md), [night-city.md](night-city.md): the shared pipeline and the city techniques this
  port reuses.
- Renderer commits, in order: 2c0991bd2 (layer blends and terrain), 302a88c37 (F0 and specular ambient), 9d7481aba
  (wear-paint), 53612024b (slope layer), 49b3f1815 (neutral ambient reflection), 53dc5a0a9 and 15fe13e78 (cascade
  blend in depth, proxy-member fade, `.xw3t` version 2), ab068f095 (no decals on vegetation), 56826e03d (flowing water), 3303a2d07
  (terrain layers read once), 8d2e73ea9 (`r_sceneVrs`), 1362aaa0a / 4ec81ebc0 / d405a544a (billboards, their
  casters, matched cards).
