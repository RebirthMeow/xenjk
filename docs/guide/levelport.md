# Porting levels: the pipeline and the render scene

This chapter covers the parts of xenjk's level port that do not depend on the source game. It has two halves. The
first is the **pipeline**: any level that can be written as a glTF scene with a small node convention compiles to an
ordinary Jedi Academy BSP through `tools/levelport/gltf2map.py` and our patched q3map2, with staged builds, hidden
screenshots and offline checks around it. The second is the **render scene**: a level too dense for a BSP is split
in two. A light gameplay BSP keeps collision, spawns and entities, and a `maps/<map>.xscn` render layer is drawn by
rd-vulkan with instancing, GPU culling and LOD, the source's own lights, haze, reflections and a colour grade. Night
City ([night-city.md](night-city.md)) and Beauclair ([beauclair.md](beauclair.md)) build on both halves.

**Status.** The pipeline tools are in master. They are Python and need no engine or renderer changes. Plain BSP
ports (Quake Live maps, NieR:Automata tiles) load in any JKA renderer. The render scene runtime
(`codemp/rd-vulkan/vk_scene*.cpp` and `shaders/glsl/scene/`) is in master and in the installed game, and it is on
whenever a map ships a scene (`r_scene 1`). Converted levels are never committed or published. You extract from a
copy of the game that you own, and the builds stay on your machine.

## What it does

### The pipeline

```
Quake 3 / QL BSP --bsp2gltf.py--+
NieR:A world tile --nier2gltf.py-+                     .map (brushes, patches, entities)
CP2077 / W3 readers -------------+--> level.glb --gltf2map.py--> + render chunks (.glb misc_models) + shaders
Blender / your own exporter -----+                     |          --q3map2 -bsp -meta / -vis / -light--> .bsp
                                                       |          --colbsp slim--> zz_<map>.pk3
                                                       +--(dense levels) collision + entities only, and a
                                                          separate scene writer --> zz_<map>_scene.pk3 (.xscn)
```

**Interchange: glTF with an `lp` role per node.** glTF space is metres with +Y up. Quake space is units with +Z up,
at **37.5 units a metre** (`tools/levelport/levelport.py`: `q3 = (gx, -gz, gy) * 37.5`). Each node carries its role
in its extras under `"lp"` (in Blender, an object custom property, exported with *Include > Custom Properties*):

| `lp` | becomes |
|---|---|
| `render` (default for a mesh) | triangles cut into `misc_model` chunks (`.glb`) on a `--chunk` grid (1024 u); lightmapped, or vertex-lit with `--vertexlit`; the material name is the shader; node matrices place instances |
| `collision` | one convex brush (convex hull if the faces are not convex). `"shape": "mesh"`: thin detail slabs `"thickness"` deep (default 8 u), one per convex group of coplanar neighbouring triangles (`--merge-verts`, 12 corners). `"contents"` takes Quake names (`solid`, `playerclip`, `water`, ...); `"visible": true` draws the sides with the face materials, UVs fitted (movers) |
| `patch` | a `patchDef2` from `"cp"`, `"width"`, `"height"`, `"shader"` |
| `entity` | extras are the entity keys, the node position its origin; collision and patch children become its brushes (doors, triggers) |
| `ignore` | the node and its children are skipped |

Material extras add decals (`lp_blend`), emissive surfaces with q3map2 surface lights (`lp_emissive`), water
(`lp_water`), scrolling (`lp_scroll`), two-layer terrain blends in vertex alpha (`lp_layers`) and surfaceparms. A
material named after a shader that already exists (one from `--source-base` or the installed base) keeps that shader.
An embedded image becomes `textures/levelport/<map>/<material>` with a generated shader. The full table is in
[tools/levelport/README.md](../../tools/levelport/README.md).

**gltf2map.py** writes `maps/<map>.map` (brush primitives through `mapio.py`), the render chunks, the shader
script and the textures. It then runs q3map2 `-bsp -meta`, `-vis -saveprt` and `-light -fast -filter
-patchshadows`, slims the planes (`colbsp.py slim`) and packs `zz_<map>.pk3` with a `scripts/<map>.arena`. `--sky
NAME` seals the level in a sky box whose shader carries `q3map_sunExt`, `q3map_skylight` and rd-vulkan's
`q3gl2_sun` (the sun the shadow cascades use). `--sky-haze` generates a gradient sky in its place. `--preset
quakelive` remaps Quake entity classes and weapons (`presets.py`). `--scale` resizes the level, because the JKA
player is taller than Quake's.

Rules that cost us time. They are enforced in the code, and you need them if you write your own glTF:

- q3map2 prefixes `textures/` to brush and patch shaders. Names cap at 63 characters, and the renderer refuses image
  paths of 64 or more, so generated names stay at 59 (`Compiler.short`).
- netradiant-custom's q3map2 reads misc_models through assimp, Y-up. Chunks are `.glb` because glTF `COLOR_0`
  alpha survives `-meta` and `-light` into the draw vertices (OBJ has no alpha, and PLY loses the material name).
- Chunks are anchored at the first spawn point. A `misc_model` origin in the void leaks the map.
- Model triangles bypass `-meta`. A dense lightmapped mesh stays one draw surface per few triangles: the NieR camp
  made 118,876 of them and crashed the renderer. `--vertexlit` merges them into about 1,200 surfaces.
- The JKA renderers refuse non-power-of-two images, so packing resamples them up.
- Quake 3 lighting is shown 4x brighter at load (`r_mapOverBrightBits 2`). `lighttransfer.py --scale 4` bakes that
  shift in.

### Our q3map2

`tools/levelport/q3map2/` holds patches to netradiant-custom's q3map2 (upstream Garux/netradiant-custom, tested at
be3b369). Its [README](../../tools/levelport/q3map2/README.md) has the measurements and the build line (msys2
mingw64). Collision maps made from meshes have hundreds of thousands of detail brushes, which stock q3map2 was never
built for:

| Patch | Why |
|---|---|
| `plane_hash.patch` | `FindFloatPlane` hashed planes on `(int)abs(dist) & 8191` alone. Keyed on distance bin and normal, the `.map` load drops from 141 s to 12 s |
| `cullsides_sweep.patch` | `CullSides` tested every pair of brushes. A sweep along x tests only pairs whose bounds overlap. Identical output; with the hash, Kabuki's `-bsp` went from 1,250 s to 23 s |
| `large_maps.patch` | 64-bit `.map` reads (Windows `ftell` is 32-bit: a `.map` past 2 GB read as empty) and only the planes a side or node uses are written (half of q3map2's planes are never read) |
| `exact_planes.patch` | `-exactplanes`: a plane is reused only when it is exactly the snapped plane, face distances are computed in double and bevels come from the brush's own windings, so **a brush's planes depend on the brush alone**. `-pointgrid N` snaps points so a whole-unit move is exact. A chunk compiled alone then matches the whole compile bit for bit; this is what makes collision chunks possible |
| `world_coords.patch` | world bound ±65,536 -> ±262,144, for a coordinate test only (chunks compile about their own origin instead) |
| `brush_cook.patch`, `brush_bin.patch` | `-brushcook` / `-brushbin`: brushes in and out as binary records, with no `.map` text, tree or BSP (`colcook.py` is the other end; city chunks) |

gltf2map finds q3map2 through `$Q3MAP2`, read when a compile starts (there is no default location, and `--help`,
an import or `--no-compile` need none). `--exact-planes` (and `--cook`) need the build with `exact_planes.patch`: set
`$Q3MAP2_CHUNKS` to it, or `$Q3MAP2` to that build when it is the only one you have. The game definitions are built in (`-game ja`).

### Staged builds, looks and checks

- **stage.py** runs one q3map2 stage on a `--no-compile` build, or packs it. Long single commands hid problems,
  such as an 800 m map or a full vis that would have taken an hour, so dense ports are built one stage at a time and
  each result is checked before the next.
- **relight.py** copies a build's stage and reruns only `-light` with other options, for lighting comparisons
  without recompiling geometry.
- **look.py** takes hidden, muted screenshots with the installed client in a private homepath (nothing is installed).
  It shoots as a spectator from spread spawn points, or from `--viewpos "x y z yaw [pitch]"`. `--extra-pk3` stages
  a render scene, `--cvar` sets cvars before the load, `--set` runs console lines, and `--png` saves lossless shots
  for reading debug views per pixel. **look_stats.py** gives brightness percentiles and contrast of shots, for
  comparing against reference images.
- **levelcheck.py** finds what a player would trip over before a playtest does:
  - `spawns`: every `info_player_*` box against the player-solid brushes, plus a floor within 64 u below it.
  - `aerial` + `holes`: the 64-bit client photographs the level straight down with the sky cleared to magenta, and
    the magenta pixels inside the level's radius are holes in the ground or roofs.
  - `compare`: per 16 u column, the player-solid intervals against the render scene's walkable surfaces, giving
    floors with no collision under them and collision with nothing drawn on it.
  - `collide`, `drop`: those results clustered, and a player box dropped onto each fall-through surface.
    `drop --engine N` checks N of them with the engine's own trace (game command `levelcheck_trace`,
    `codemp/game/g_levelcheck.c`).

  The rule we work by is to check offline first and play last.
- **probe.py** answers a `/screenpos` playtest report (`codemp/cgame/cg_screenpos.c`) offline: what the crosshair
  ray, the floor below and the player box hit in the collision parts or in the compiled chunk BSPs. It reads
  Night City's tile parts, so outside a tiled area use it as a model of the idea rather than as-is.
- **retain.py** keeps the build folder bounded. It moves superseded builds to `.codex-trash/` (never deletes), keeps
  the newest per area, anything in use and anything listed in `.codex-research/KEEP.txt`, and runs dry unless you pass
  `--apply`. **seed_work.py** starts a CP2077 work folder from other areas' caches by hard link. Both are tied to the
  `levelport/` layout under the research root (`.codex-research`, or `XENJK_RESEARCH`).
- **Light tools.** `lighttransfer.py` replaces q3map2's lightmap texels and light grid with a source Quake 3 / QL
  BSP's baked lighting by world position (`gltf2map --light-from` runs it). `relight.py` is above. `light_time.py`
  is CP2077-specific: it finds the lamps the game switches on by the clock ([night-city.md](night-city.md)).
- `blender_preview.py` renders a top and a perspective view of a `.glb` in headless Blender.
  `colbsp.py stats` reports what a BSP's collision costs.

### The front ends, as simple examples

- **bsp2gltf.py** (Quake 3 IBSP 46, Quake Live 47): render surfaces, every gameplay brush as a collision node with
  its side shaders, patches and entities. Mover brushes are marked visible. A brush map compiles back almost 1:1,
  because the brushes already are the collision. Dueling Keeps took 2 minutes, vis included.
- **nier2gltf.py** (NieR:Automata `wd1/gXXXYY` tiles, read from the CPKs with the PlatinumGames readers of a
  `2b-smallsword` checkout, `--nier-tools` or `NIER_TOOLS`): LOD0 meshes, placed props from `Layout.lay`, COL2 collision as
  `shape: mesh`, spawns sampled on the ground. It maps material variables to `lp_layers`, cutouts, decals and
  `lp_water`. This is the worked example of a mesh game with no brushes:
  [levelport-nier.md](../levelport-nier.md).

### Two layers for dense levels

A BSP breaks down at modern density. Kabuki has 61,710 placements of 2,718 meshes within 135 m. Flattened, objects
of 5 m and over alone made 1.1 M triangles and a 176 MB BSP. The BSP fixes one LOD per placement and its vis sees
everything, and q3map2 cannot light that many surfaces live. So a dense map has two layers:

1. **Gameplay BSP** (`maps/<map>.bsp`): collision, entities, spawns, the playable boundary, the sky shell and a light
   grid. It carries no render geometry, so it compiles in minutes (`--light fast --vis none`; it is one open cell).
   The server, bots and every other renderer load only this.
2. **Render scene** (`maps/<map>.xscn` in the pk3): unique meshes with their whole LOD chains, placements, baked
   materials and the source's lights, in the same frame as the BSP (game units, Z up). Only rd-vulkan reads it.
   Other renderers show the BSP's sky and entities alone.

### The .xscn format (version 6; 1-5 still load)

Little-endian. Sections start on 16-byte boundaries, and offsets count from the start of the file. The header is
`XSCN`, a u32 version, then a count and an offset for each section: 8 of each in v1, 9 in v2-5 (80 bytes), 10 in
v6 (88 bytes). The record layouts are `xscn*_t` in `vk_scene_local.h` (the header and its reader,
`xscnHeader_t` and `Scene_ReadHeader`, are in `vk_scene.cpp`). The Python writer is `xscn.xscn_blob` in
`tools/levelport/xscn.py` (with `scene_light_section`, `scene_box_section`, `scene_shape_section`); `cp2gltf.py`
re-exports its names, and `cp2gltf.py` and `w3scene.py` both write through it.

| Section | Bytes | Fields |
|---|---|---|
| vertices | 28 | position f32x3 (mesh local); normal s8x4; tangent s8x4 (w = bitangent sign, OpenGL green-up); uv f32x2 |
| indices | 4 | u32, relative to the part's vertex offset |
| meshes | 32 | bounding sphere centre and radius (mesh local); first LOD, LOD count |
| LODs | 16 | first part, part count, `screen`: drawn while the sphere's projected diameter is at least this share of the view height |
| parts | 16 | first index, index count, vertex offset (i32), material |
| materials | 240 | albedo, normal, ORM image paths (64 B each); kind (0 opaque, 1 cutout, 2 blend, 3 emissive); flags; emissive rgb + intensity; tint rgba; two window values |
| instances | 64 | 3x4 row-major mesh-to-world matrix; mesh; near distance (v5, a far proxy's start); signal slot; far hide distance (v4) |
| lights | 64 | origin, radius; linear colour, power; spot direction, cos outer; cos inner, type bits, boxed, haze share (v3) |
| light boxes (v2) | 64 | scene space -> the light's interior unit cube (3x4); v4 adds hide distance, shadow fade, fade range, flags |
| light shapes (v6) | 16 | half the tube axis, source radius (zero: a point light) |

Material flags the renderer reads: 1 two-sided, 4 masked glow, 0x40 mesh decal (DBuffer), 0x80 water, 0x100
night-only emissive, 0x2000 stand-in (not drawn). The rest are game-specific: 2 window interiors, 8 traffic-signal
lamp, 0x200 screen and 0x400 ad glow are CP2077's; 0x800 SpeedTree leaf and 0x1000 terrain are Witcher 3's. Light
type bits: 0 spot, 1 lights only the haze, 2 casts shadows, 19-23 CP2077's channel, time and template classes.

Images sit in the same pk3 under `scn/<map>/`, one DDS per distinct image. BC1 is used for opaque colour and ORM,
BC7 for alpha, normals and detail, with full mips and a DX10 header (`xscn.dds_bytes`, encoded with the `etcpak`
Python package). Block compression is required: on Kabuki, 8,780 RGBA8 uploads ran the device out of memory.

A scene too big for one file (the renderer reads a file whole into an `int` length) is **tiled**. A text manifest
`maps/<map>.xman` lists geometry shards, one material table and per-tile instance/light files, and the loader joins
them in memory. If both exist, the `.xman` wins over a single `.xscn`. Tiles, streaming (`r_sceneStream`) and
residency are city tools: [night-city.md](night-city.md).

### Sidecars

All are optional files beside the scene, named after the map. The loader skips any that are missing. Each reader's
opening comment is the format's spec; the file to read is in the middle column.

| File | Read in `codemp/rd-vulkan/` | What | Written by / owned by |
|---|---|---|---|
| `.xlut` | `vk_scene_post.cpp` (`Scene_LoadGrade`) | colour grade: `XLUT` header, n³ rgba16f display-light texels, input ARRI LogC v3 | both ports (`cp2gltf --grade`, `w3grade.py`) |
| `.xenv` | `vk_scene.cpp` (`Scene_LoadEnv`) | text: `xenv 1`, `scene <map>` (share another map's scene), `time day\|night`, `sun r g b I yaw elev`, look cvars for this map | `build_watson.py`, `w3env.py` |
| `.xdet` | `vk_scene.cpp` | per-material tiled detail layer (multilayer materials) | `cp77mat.py` (night-city) |
| `.xwat` | `vk_scene_files.cpp` (`Scene_LoadWater`) | water materials' settings, an ocean record (v2+), a sea mask (v3) | `cp2gltf.py`, `w3scene.py` |
| `.xdcl` | `vk_scene_files.cpp` (`Scene_LoadPDecals`, `Scene_DrawPDecals`) | projected box decals | `cp77decals.py`, `w3decals.py` |
| `.xfx` | `vk_scene_files.cpp` (`Scene_LoadFx`, `Scene_DrawFx`) | particle-like effects (steam, smoke) | `cp77fx.py`, `w3fx.py` |
| `.xfog` | `vk_scene_cp77.cpp` (`Scene_LoadFogVolumes`) | local haze volumes: 3x4 box rows, density, mode, soft edge, kind (box or steam column) | `cp2gltf.py` (night-city) |
| `.xrot`, `.xscr`, `.signals` | `vk_scene_cp77.cpp` (`Scene_LoadRotations`, `Scene_LoadScreens`, `Scene_LoadSignals`) | rotating meshes (with their compute pass), screen shader values, traffic-signal cycles; the signals file may also name the crossing sounds (`sound <role> <path>`, below) | CP2077: [night-city.md](night-city.md) |
| `.xint` | `vk_scene.cpp` | interior streaming boxes | CP2077: [night-city.md](night-city.md) |
| `.xw3t`, `.xw3b` | `vk_scene_w3.cpp` (`Scene_LoadW3Terrain`, `Scene_LoadW3Blend`) | terrain tiles, and the vertex-blend / billboard material records | Witcher 3: [beauclair.md](beauclair.md) |

`.signals` files carry optional `sound <role> <path>` lines (roles `walk_base`, `walk_layer`, `dontwalk_base`,
`dontwalk_layer`). The renderer skips them; the cgame (`cg_signals.c`) registers and plays the named files, so a
level names its own sounds and the code holds no sound folder. `cp2gltf.py scene --signal-sounds <folder>` writes
the lines for `<folder>/<role>.wav`; a file without them plays nothing. Format:
[levelport-scene.md](../levelport-scene.md), "Traffic signals".

`make_xlod.py` writes `maps/<map>.xlod` (per-LOD thresholds from CP2077's own LOD distances), but **no renderer
reads it yet**. The code has no reader.

### At run time

```
RE_LoadWorldMap (tr_bsp.cpp) -> R_SceneLoad: .xenv, .xman|.xscn, sidecars; buffers; lights binned; light shadows
per frame:
  R_GenerateDrawSurfs (tr_main.cpp) -> R_SceneAddView   eye view registers (also each sun cascade view)
  vk_begin_frame (vk_frame.cpp)     -> R_SceneVkBeginFrame: scene_cull.comp / scene_occ / scene_compact /
                                       scene_cluster for every registered view, before any render pass
                                    -> sun cascades drawn (vk_scene_draw SCENE_PASS_SHADOW from tr_backend.cpp)
                                    -> vk_scene_haze (vk_scene_post.cpp): scene_fog.comp lights the froxel volume
  RB_DrawSurfs (tr_backend.cpp)     -> SCENE_PASS_DEPTH (prepass), decals, SCENE_PASS_OPAQUE before the BSP's
                                       surfaces, SCENE_PASS_BLEND after; SCENE_PASS_GLOW in the dynamic glow
  after the MSAA resolve            -> vk_scene_capture (SSR history, scene_ssr.comp), vk_scene_grade (.xlut); both in vk_scene_post.cpp
```

- **Cull and LOD.** For every view, a compute pass tests each instance's bounding sphere against the frustum, drops
  it under `r_sceneCullPixels` (1.5 px), picks the LOD by projected size (`screen`, times `r_sceneLodBias`), and
  appends it to that LOD's parts' indirect commands. `r_sceneLodFade` (0.25) crossfades LODs with dither instead of
  popping. `r_sceneOcclusion` (1) runs two-phase occlusion: last frame's depth first, then this frame's prepass for
  what that hid, so nothing visible waits a frame. Measured savings were 0.5-5 ms on dense streets.
- **GPU-driven draws** (`r_sceneGpuDriven` 1; needs a Vulkan 1.2 device). Every scene image sits in one descriptor
  array and every material in a buffer. `scene_compact.comp` packs each view's non-empty commands into lists (lit,
  depth, depth cutout, glow, blend, water), and each list is one `vkCmdDrawIndexedIndirectCount`. The lit pass tests
  `LESS_OR_EQUAL` against the depth prepass without writing depth (`r_depthPrepass` is required). With
  `r_sceneGpuDriven 0` it draws one indirect call per material.
- **Lights.** Lights are binned once into a world grid (up to 128 cells an axis, the 48 strongest per cell). The eye
  view also sorts them each frame into 40x24x32 clusters (`r_sceneClusters`, 126 a cluster, `scene_cluster.comp`).
  Shading is physically based diffuse and specular with inverse-square falloff windowed to the radius, plus a
  hemisphere ambient term and emissive. Version-6 tube lights widen highlights. The game's own dynamic lights (saber
  glow, shots, effects; up to 32) light the scene through the same BRDF (`r_sceneDlights` 150,
  `r_sceneDlightReach` 2.5). Characters take the scene's light in place of the BSP's grid (`r_sceneCharLight` 1,
  through `R_SceneLightPoint` in `tr_light.cpp`).
- **Shadows.** The map's `q3gl2_sun` drives rd-vulkan's three sun cascades. Scene casters draw into them, so
  characters and scene shadow each other (`r_sceneShadows`, `r_sceneShadowLod`, `r_sceneShadowCascades`; the far
  cascade's static casters are cached, `r_sunShadowFarCache`). Lights with type bit 2 get **static shadow maps
  rendered at load** into a depth atlas: one face for a spot up to 120 degrees, else six cube faces, sized by reach
  (`r_sceneLightShadows`, `r_sceneLightShadowRes` 128). Kabuki: 308 lights, 1,533 faces, about 1 s at load and
  0.1 ms a frame.
- **Haze.** Exponential fog thins with height and mixes toward the sky's horizon colour in linear light
  (`r_sceneFog*`). Light in the haze uses a froxel volume of 240x135x64 slices out to `r_sceneFogFar` (600 m), lit by
  each light's haze share and by the sun through its cascades, so light falls in shafts. `.xfog` volumes add to the
  density. The pass costs about 0.15 ms at 1080p on Kabuki and is the largest cost on a city.
- **Reflections.** Screen-space reflections on wet, up-facing surfaces. They are traced at half resolution from a
  history captured after the resolve, settled over frames with reprojection, and run on the async compute queue
  where there is one (`r_sceneReflect` 3, `r_sceneAsync`). A rain mask (an orthographic depth view from above, made
  at load) keeps covered floors dry (`r_sceneWet`, `r_sceneRain`). They need MSAA and the depth extract.
- **Grade and HDR.** With a `.xlut`, the resolved frame goes back through the scene's tone curve, through LogC and
  the LUT, and out, before bloom and the HUD (`r_sceneGrade`, `r_sceneGradeExposure`). The BSP's sky, characters
  and effects are graded too. A graded scene switches the colour target to float at load (`r_sceneHdr`), so neon
  reaches the grade unclipped.
- **Glow.** Emissive materials draw again in the dynamic glow's extract pass, above `r_sceneGlowThreshold`.
- **VRS.** `r_sceneVrs "terrain leaves other"` (metres, 0 = never) shades far surfaces once per 2x2 pixels through
  `VK_KHR_fragment_shading_rate`'s per-primitive rate (a second vertex module, `scene_vert_gd_vrs`). Edges and depth
  stay per pixel. It is off by default; Beauclair's launcher turns it on.
- **Debug.** `r_sceneDebug` selects a second lit pipeline with debug views: 1 albedo, 2 normals, 3 lights per cell,
  4 sun shadow, 5-7 the ambient / source-light / sun shares, 8 material index, 9 wet, 10 reflections, 13-14 world
  position, 15 instance index, 16 material index (24 bits), 18-20 UV, sampled texel and image index (the full list
  is in [xenjk-cvars.md](../xenjk-cvars.md)). `levelcheck.py aerial` uses 1 and 8. The commands `sceneinfo`,
  `sceneimage <n>` and `scenecullcount` print what is loaded and drawn.

## Take this piece

**Depends on:** [renderer.md](renderer.md) for the render scene. It builds on xenjk's rd-vulkan: sun cascades and
`q3gl2_sun`, the depth prepass, the depth extract (SSAO), the MSAA resolve and post 1x pass, dynamic glow, the float
colour target, and the DDS/BC7 loader. The pipeline alone needs none of it.

### Files: the pipeline (copy whole; all Python)

| Path | Role |
|---|---|
| `tools/levelport/README.md` | node convention, material extras, tool list |
| `tools/levelport/paths.py` | every location the tools use (`REPO`, `RESEARCH`, `GAMEDATA`) and the variables for tools outside the repo, with no machine defaults (see *Environment* below) |
| `tools/levelport/levelport.py`, `gltfio.py`, `mapio.py`, `q3bsp.py`, `presets.py` | units and axes; a numpy-only glTF reader/writer; `.map` writer; Q3/QL/JKA BSP reader; entity presets |
| `tools/levelport/gltf2map.py`, `slabwriter.py` | the compiler; mesh-collision slabs |
| `tools/levelport/colbsp.py`, `colmerge.py` | plane slimming and stats; split a huge `.map` into q3map2 tiles and merge them under one tree |
| `tools/levelport/colcook.py`, `colpack.py`, `colexpand.py`, `colzbsp.py` | brush records to and from q3map2 `-brushbin`, slab packages, zstd-compressed BSPs (city chunks) |
| `tools/levelport/q3map2/*.patch`, `README.md` | the q3map2 patches and build line |
| `tools/levelport/stage.py`, `relight.py`, `lighttransfer.py` | staged compiles, relighting, Quake light transfer |
| `tools/levelport/look.py` (+ `tools/renderer_bench.py`), `look_stats.py`, `blender_preview.py` | screenshots and their statistics |
| `tools/levelport/levelcheck.py`, `codemp/game/g_levelcheck.c` | offline checks; the game command that traces in the engine |
| `tools/levelport/probe.py`, `codemp/cgame/cg_screenpos.c` | playtest reports and their offline answer |
| `tools/levelport/retain.py`, `seed_work.py`, `jobguard.py`, `memgate.py` | disk retention, cache seeding, child-process cleanup, memory-gated starts |
| `tools/levelport/pk3write.py` | pack writes under a temporary name then renamed, and the scene shard roll-over (`zz_<map>_scene2.pk3`, ...) |
| `tools/levelport/bsp2gltf.py`, `nier2gltf.py` | the simple front ends |

### Files: the render scene

| Path | Role |
|---|---|
| `codemp/rd-vulkan/vk_scene.cpp` | the core: loader (`R_SceneLoad`), the `.xenv`, `.xdet` and `.xint` readers, cull, draws, lights, shadows, streaming |
| `codemp/rd-vulkan/vk_scene_local.h` | the render scene's private header: the record layouts (`xscn*_t`), constants, `sceneState_t` (the one state struct, `sceneState`) and the declarations the side files share |
| `codemp/rd-vulkan/vk_scene_post.cpp` | the passes after the draws: haze (`vk_scene_haze`), reflection and AO capture (`vk_scene_capture`), the exposure meter, the `.xlut` grade (`vk_scene_grade`) |
| `codemp/rd-vulkan/vk_scene_files.cpp` | side files both ports write: `.xdcl` decals, `.xwat` water, `.xfx` particles, load and draw |
| `codemp/rd-vulkan/vk_scene_cp77.cpp` | CP2077's side files: `.xfog`, `.signals`, `.xrot` (and its compute pass), `.xscr` |
| `codemp/rd-vulkan/vk_scene_w3.cpp` | Witcher 3's side files: `.xw3t` terrain, `.xw3b` blends and billboard cards |
| `codemp/rd-vulkan/shaders/glsl/scene/*` | 25 GLSL sources (`scene.vert/.frag`, `scene_cull`, `scene_occ`, `scene_compact`, `scene_cluster`, `scene_fog`, `scene_ssr`, `scene_capture`, `scene_grade`, ...) |
| `codemp/rd-vulkan/shaders/spirv/scene_shader_data.c` | generated SPIR-V arrays (do not edit) |
| `tools/scene_spirv.py` | regenerates the above |
| `tools/levelport/xscn.py` (`xscn_blob`, `SceneImages`, `dds_bytes`, `vertex_frame`, `scene_*_section`) | the game-independent `.xscn` writer: records, sections, container, images; `cp2gltf.py` and `w3scene.py` write through it |
| `tools/levelport/make_xlod.py`, `vcache.py` / `vcache.c` | LOD thresholds (no reader yet); vertex-cache reordering |

**Hooks in stock or shared rd-vulkan files** (the declarations are in `vk_local.h` under "render scenes"):

| File | Function | Hook |
|---|---|---|
| `tr_init.cpp` | `R_Register`, `RE_Shutdown` | `R_SceneRegister()` cvars and commands; `R_SceneShutdown()` |
| `tr_bsp.cpp` | `RE_LoadWorldMap` | `R_SceneLoad( name )` after the BSP, before the cubemap bake |
| `tr_main.cpp` | `R_GenerateDrawSurfs`, the sun cascade loop | `viewParms->sceneView = R_SceneAddView( viewParms )`; widens the view's bounds for the far plane; `vk_scene_shadow_far()` stretches the cascade split |
| `tr_local.h` | `viewParms_t` | `int sceneView` |
| `vk_frame.cpp` | `vk_begin_frame` | `R_SceneVkBeginFrame()` before the first render pass; `vk_scene_haze()` after the cascades |
| `tr_backend.cpp` | `RB_DrawSurfs`, the shadow and resolve passes | `vk_scene_draw( SCENE_PASS_* )`, `vk_scene_occlusion()`/`vk_scene_decals()` after the prepass, `vk_scene_capture()`, `vk_scene_grade()` |
| `vk_sunshadow.cpp` | the far-cascade cache | `vk_scene_shadow_key()` in the cache key, `vk_scene_view_skipped()`, `vk_scene_draw( SCENE_PASS_SHADOW )` for the scene's casters |
| `tr_light.cpp` | `R_SetupEntityLighting` | `R_SceneLightPoint()` for Ghoul2 models |
| `tr_scene.cpp` | `RE_BeginScene` | `R_SceneSunDir()`, `R_SceneCharSun()` |
| `vk_attachments.cpp`, `vk_renderpass.cpp`, `vk_image.cpp`, `vk_instance.cpp` | attachment and pass setup, sampler changes, device features | `vk_scene_color_storage()`, `vk_scene_splits_main()`, `vk_scene_samplers_changed()`, the shading-rate feature for VRS |

Neither the engine nor the cgame needs changes for a plain scene. Geometry streaming (`r_sceneStream`) also needs
xenjk's `FS_Range*` additions to the renderer import (`codemp/rd-common/tr_public.h`) and `fs_prefetchApi` 2 or more.
Without them the scene loads whole.

**Build inputs.** `vk_scene.cpp`, `vk_scene_local.h`, the four `vk_scene_*.cpp` side files and
`shaders/spirv/scene_shader_data.c` are listed in `codemp/rd-vulkan/CMakeLists.txt`; a derivative that does not want
Witcher 3 or CP2077 can leave `vk_scene_w3.cpp` or `vk_scene_cp77.cpp` out only by stubbing their loaders
(declared in `vk_scene_local.h`). After editing a scene shader, run `python tools/scene_spirv.py` (it uses
Khronos glslang found the way `tools/renderer_spirv.py` finds it, or `GLSLANG_EXE`). The pipeline needs Python 3 with
numpy, scipy, Pillow and psutil, plus etcpak for scene images. q3map2 comes from netradiant-custom with the patches.

**Cvars / commands** (all in [xenjk-cvars.md](../xenjk-cvars.md), about 200 `r_scene*`): `r_scene` (1, read at map
load), `r_sceneGpuDriven` (1), `r_sceneOcclusion` (1), `r_sceneLodBias` (1), `r_sceneLodFade` (0.25),
`r_sceneCullPixels` (1.5), `r_sceneShadows` (1), `r_sceneLightShadows` (1), `r_sceneClusters` (2), `r_sceneFog`
and `r_sceneFogLight` (1.6), `r_sceneReflect` (3), `r_sceneGrade` (1), `r_sceneHdr` (1), `r_sceneDlights` (150),
`r_sceneCharLight` (1), `r_sceneVrs` ("0 0 0"), `r_sceneEnv` (an `.xenv` override for A/Bs), `r_sceneDebug`.
Commands: `sceneinfo`, `sceneimage`, `scenecullcount`. A map's `.xenv` can set look cvars for that map only; a
value typed at the console wins.

**Data formats.** `.xscn`/`.xman`, the sidecars above. The record layouts are in `vk_scene_local.h` (the sidecars' in
the comments of the files that read them) and the history is
in [levelport-scene.md](../levelport-scene.md). Patched q3map2 options: `-exactplanes`, `-pointgrid`, `-xplanes`,
`-brushcook`, `-brushbin`.

**Porting notes.**

- **rd-vanilla / rend2 / any OpenGL renderer.** Take the pipeline. A BSP with render chunks loads in any JKA
  renderer, and the `.arena` and pk3 are stock. A two-layer map is invisible there apart from its sky and entities,
  so either ship a coarse render layer in the BSP or keep dense levels to rd-vulkan.
- **JKSunny's rd-vulkan without xenjk's changes.** `vk_scene*.cpp` assume the infrastructure listed under
  *Depends on*. Bring the sun cascades, the depth prepass and extract, and the post 1x pass first
  ([renderer.md](renderer.md)). The scene needs `drawIndirectFirstInstance`. The GPU-driven path needs Vulkan 1.2
  (draw parameters, descriptor indexing). VRS needs `VK_KHR_fragment_shading_rate`. The renderer checks each of these
  and falls back or skips.
- **32 vs 64-bit.** A Kabuki-sized scene (about 65k instances, 5 M triangles, about 0.7 GB of images) runs in the
  32-bit client. City-scale tiled scenes need the 64-bit client. On NVIDIA, a 64-bit process commits write-combined
  memory equal to its device-local allocations, which shows as about 3 GB more commit charge without being resident
  ([levelport-render-district.md](../levelport-render-district.md) section 3).
- **Upstream q3map2 works** for small brush maps (Quake ports). The patches matter from a few thousand mesh-collision
  brushes up. `exact_planes.patch` matters only for chunked collision.

## Do it yourself

You need your own copy of JKA and of the source game. Expose the JKA install to the tools as
`game_directory/Jedi Academy/GameData` in the checkout (a link; it is ignored by Git), or set `XENJK_GAMEDATA` to
it. gltf2map reads its base shaders, and look.py and levelcheck.py run its client. You also need Python 3 with the
packages above, a q3map2 built from netradiant-custom with `tools/levelport/q3map2`'s patches (point `Q3MAP2` at
it), and Blender if you edit the glTF by hand. Builds go to `<research>/levelport/build/<map>-<stamp>` unless you
pass `--out`; `<research>` is `.codex-research` in the checkout, or `XENJK_RESEARCH`.

#### Environment

Every location outside the checkout comes from a variable (`tools/levelport/paths.py`). A tool reads its variables
when it needs them, so `--help`, an import or `--no-compile` need none, and a missing one ends the run naming it.

| Variable | Used for | Default |
| --- | --- | --- |
| `XENJK_GAMEDATA` | your JKA `GameData` (gltf2map, look.py, levelcheck.py, colexpand.py) | `game_directory/Jedi Academy/GameData` in the checkout |
| `XENJK_RESEARCH` | work, caches and output root | `.codex-research` in the checkout |
| `Q3MAP2` | the patched q3map2 (every compile, `colpack.py`) | none |
| `Q3MAP2_CHUNKS` | the q3map2 with `exact_planes`, `brush_cook`, `brush_bin`; used for `--exact-planes` / `--cook` when `Q3MAP2` is unset | none |
| `XENJK_LIBZSTD` | libzstd for `colpack.py`; else the one beside `Q3MAP2`, else the system's | none |
| `XENJK_X64` | the x64 client build folder `levelcheck.py aerial` and `drop --engine` run | none |
| `XENJK_X64_GAME` | optional game module built with `g_levelcheck.c`, for `drop --engine` | none |
| `GAME_LOCK_DIR` | levelcheck's shared game lock folder (`GAME_LOCK_SLOTS`, `GAME_LOCK_EXCLUSIVE` tune it) | `<research>/anime-batch-20261030` |
| `NIER_DATA`, `NIER_TOOLS` | `nier2gltf.py`: the folder holding `data003.cpk`, and the 2b-smallsword readers | none (`--game`, `--nier-tools`) |
| `BLENDER` | Blender, for `cp2gltf.py decimate` | none |
| `CP77_GAME`, `WOLVENKIT_CLI`, `WOLVENKIT_DOTNET`, `PWSH` | the CP2077 front end: [night-city.md](night-city.md) | none (`PWSH`: `pwsh` on `PATH`) |
| `W3_GAME`, `W3_RESEARCH`, `W3_UNCOOK`, `W3_DOBOZ` | the Witcher 3 front end: [beauclair.md](beauclair.md) | none |

### A. A Quake 3 / Quake Live map

```
python tools/levelport/bsp2gltf.py <your baseq3>/maps/<map>.bsp <work>/<map>.glb --base <your extracted baseq3>
python tools/levelport/gltf2map.py <work>/<map>.glb ql_<map> --preset quakelive \
    --source-base <your extracted baseq3> --light-from <your baseq3>/maps/<map>.bsp
```

`--light-from` keeps the source's baked lighting (`lighttransfer.py --scale 4`). Add `--scale 1.2` if the level feels
small for JKA's player; the scale carries over to the light transfer.

### B. A mesh level from any game (the NieR route)

1. **Export a glTF** in the node convention: render meshes with materials and embedded images, collision as
   `lp: collision, shape: mesh` nodes (or boxes), water as a `contents: water` collision node, `info_player_*`
   entities. Use your own reader, or Blender with custom properties. `gltfio.py` writes the subset gltf2map reads.
   Check the bounds first: a NieR tile is about 300 m across with its margin.
2. **Write the build without compiling:**
   ```
   python tools/levelport/gltf2map.py <work>/<map>.glb <map> --out <build> --sky yavin \
       --sun "1 0.95 0.85 380 35 50" --skylight 800 --worldspawn _ambient=35 --vertexlit --gamma 1.6 --no-compile
   ```
3. **Compile stage by stage**, reading `<build>/q3map2.log` after each: the `Size:` line and no leak after `-bsp`, a
   sane portal count after `-vis` (thousands, not tens of thousands):
   ```
   python tools/levelport/stage.py <build> <map> -bsp -meta
   python tools/levelport/stage.py <build> <map> -vis -saveprt
   python tools/levelport/stage.py <build> <map> -light -fast -filter -patchshadows -bounce 1 -samples 2 -gamma 1.6
   python tools/levelport/stage.py <build> <map> --pack "Long Name" --type ffa
   ```
   The light stage takes minutes on a dense mesh map, so run it in the background.
4. **Check before playing:**
   ```
   python tools/levelport/levelcheck.py spawns <build>/zz_<map>.pk3
   python tools/levelport/look.py <build>/zz_<map>.pk3 <map> --views 8 --viewpos "x y z yaw pitch"
   ```
   Pick camera positions from data (a top-down plot of collision, water and spawns), not by guessing.
5. **Play:** copy `zz_<map>.pk3` into `GameData/base` and run `devmap <map>`. The `zz_` prefix makes it sort after
   stock packs.

### C. A dense level: gameplay BSP plus a render scene

1. **Gameplay glTF.** Collision nodes, entities and spawns, no render nodes (or mark them `ignore`). Add a playerclip
   ring at the edge of the playable area and give every player-walkable surface collision.
2. **Gameplay BSP:**
   ```
   python tools/levelport/gltf2map.py <work>/<map>-col.glb <map> --out <build> --sky <sky> \
       --sun "r g b intensity yaw elevation" --skylight 150 --worldspawn _ambient=20 --light fast --vis none
   ```
   `--sun` becomes the scene's sun or moon through `q3gl2_sun`. The light stage only fills the light grid.
   gltf2map tiles collision past 500,000 brushes on its own (`--tile-brushes`) and merges the tiles with
   `colmerge.py`.
3. **Scene pack.** Write `maps/<map>.xscn` with `xscn.xscn_blob(V, I, meshes, lods, parts, mrecs, inst, lrecs)`
   (`tools/levelport/xscn.py`; `cp2gltf` re-exports the same names):
   - `V`: an array of dtype `xscn.SCENE_VERT`, with tangents from `xscn.vertex_frame`.
   - `meshes`: tuples of (centre, radius, first LOD, LOD count), centre and radius in mesh-local game units.
   - `lods`: tuples of (first part, count, screen). `cp2gltf.SCENE_LOD_SCREEN` gives defaults (0.08, 0.03, ...).
   - `parts`: tuples of (first index, count, vertex offset, material).
   - `mrecs`: tuples of (image paths, kind, flags, emissive, tint, window).
   - `inst`: tuples of (3x4 matrix in the BSP's frame, mesh, signal slot, optionally the hide and near distances).
   - `lrecs`: light records, in the order `scene_light_section` packs them.

   Encode the images with `xscn.SceneImages`, which names each DDS by content under `scn/<map>/`, encodes once
   into a cache and packs it. Put the scene and the images in `zz_<map>_scene.pk3`, stored or deflated;
   `pk3write.writing()` writes it under a temporary name and renames it, and `pk3write.ShardWriter` rolls over to
   `zz_<map>_scene2.pk3` when a stored shard would pass its limit. Add `.xlut`,
   `.xfog` or `.xenv` if you have them. `cp2gltf.py scene` and `w3scene.py` are two complete writers to copy from.
4. **Look and check:**
   ```
   python tools/levelport/look.py <build>/zz_<map>.pk3 <map> --extra-pk3 <scene build>/zz_<map>_scene.pk3
   python tools/levelport/levelcheck.py compare <build>/zz_<map>.pk3 <scene build>/zz_<map>_scene.pk3 <out>
   python tools/levelport/levelcheck.py collide <out>/cats.npz <out>/collide
   python tools/levelport/levelcheck.py drop <build>/zz_<map>.pk3 <out> <out>/drop
   python tools/levelport/levelcheck.py aerial <build>/zz_<map>.pk3 <scene build>/zz_<map>_scene.pk3 <out>/aerial \
       --map <map> --radius <game units>
   ```
   `aerial` and `drop --engine` run a 64-bit client. Set `XENJK_X64` to your 64-bit build's folder (and
   `XENJK_X64_GAME` to a game module built with `g_levelcheck.c` for `--engine`; it is one of the game labs, compiled
   unless you build with `-DBuildGameLabs=OFF`). The captures run with
   `s_mute 1`. Then copy both packs into
   `GameData/base`.
5. **If it is slow,** look at `r_gpuTimes 1` first. On a dense map the cost is geometry in the prepass, the lit pass
   and the cascades, then the haze and the light clusters. Culling itself costs about 0.15 ms. Try
   `r_sceneShadowCascades 2`, `r_sceneVrs`, and coarser `r_sceneLodBias`.

### Collision for a dense level

The general part of [levelport-collision.md](../levelport-collision.md) comes down to three rules:

- **Brushes from collision nodes.** Structure comes from meshes as `shape: mesh` slabs, always `detail` so they
  never split vis. Small props collide as their bounding box, except stairs, steps and ramps, which keep their finest
  LOD so they are walked rather than climbed. A gameplay BSP that is all detail needs a structural shell for
  q3map2's tree (`--sky`, or the temporary shell `--col-chunk` adds).
- **Size.** q3map2 holds about 7.6 KB a brush while compiling. Past `--tile-brushes` (500k), gltf2map splits the
  `.map` (`colmerge.py split`), compiles each tile and merges them under a tree of our own: leaves of about 48 brushes
  or 256 u, with traces testing about 20 times fewer brushes than under q3map2's 1,024 u block tree. `colbsp.py slim`
  then keeps one plane per orientation used: Kabuki went from 312 to 243 collision bytes a brush, exact. The result
  is an ordinary RBSP that any engine loads, within the format's 2 GB lump offsets.
- **Chunks only for a city.** The engine's collision chunks (`cm_chunk.cpp` `CM_LoadChunk`, `CM_TraceChunks` in
  `cm_trace.cpp`, `sv_chunk.cpp` for the server's hold, the `maps/<map>.chunks` manifest of `name ox oy oz minx miny minz maxx maxy maxz` lines, the
  `cm_chunk*` cvars) place separately compiled BSPs at whole-unit origins and stream them by player position. A
  level that fits one BSP does not need them, and with no `.chunks` file the engine behaves as stock. See
  [night-city.md](night-city.md).

## Limits and open issues

- No glTF-to-`.xscn` stage. The writer itself is game-independent (`xscn.py`), but only the CP2077 and Witcher 3
  converters drive it. A plain "glTF render nodes to `.xscn`" stage would make section C one command.
- Python still has three readers of the `.xscn` header and dtypes besides `xscn.py` (`levelcheck.py`,
  `scene_relight`, `scene_vcache_blob`), and the scene instance record is declared again in four patch tools. The
  C++ side shares one header but not one side-file reader: `.xfog`, `.xwat`, `.xrot` and `.xscr` each check their
  own header. The sub-features of `sceneState_t` are not split into their own structs, light-shadow rendering exists
  in two diverged copies, and the `vk_scene.h` public interface is still the prototypes in `vk_local.h`
  ([scene review](../review/2026-11-19/scene.md), Liftability).
- Two-layer maps draw only in rd-vulkan. Other clients see an empty level with its sky.
- `.xlod` has no reader, so every converted mesh uses the build's fixed LOD thresholds and some facades switch LOD
  at the wrong distance. `r_sceneLodFade` hides the pop but not the distance.
- Scene lighting is direct only: there are no GI or light probes for scene surfaces, and the ambient is a hemisphere
  term. q3map2's light grid lights nothing in the scene.
- NieR maps remain unfinished as levels (the Flooded City has falls into the sea and missing standing areas).
  NieR's precomputed GI cannot be carried over.
- `nier2gltf.py` needs a checkout of the `2b-smallsword` readers, which is not in this repo; give it with
  `--nier-tools` or `NIER_TOOLS`.
- q3map2 has no default location: set `Q3MAP2` (and `Q3MAP2_CHUNKS` for `--exact-planes` and `--cook`).
- Several tools (`retain.py`, `seed_work.py`, `probe.py`, `make_xlod.py`) assume the `levelport/` layout under
  the research root or CP2077 tiles.

## Further reading

- [tools/levelport/README.md](../../tools/levelport/README.md): every tool and flag, the material extras, and the
  "things that bit" list.
- [tools/levelport/q3map2/README.md](../../tools/levelport/q3map2/README.md): each patch, its measurement and the
  build line.
- [levelport-scene.md](../levelport-scene.md): the render scene's design, format history and per-feature
  measurements.
- [levelport-render-district.md](../levelport-render-district.md): where a dense frame goes, occlusion culling,
  tiled loading, the haze's cost, 64-bit memory.
- [levelport-collision.md](../levelport-collision.md): tiled compiles, plane slimming, exact planes, the engine's
  chunks (sections 1-4, 9 and 14 are the general ones).
- [levelport-nier.md](../levelport-nier.md): a complete mesh-game port, stage by stage, with what to check at each.
- [renderer-map-load.md](../renderer-map-load.md): where map load time goes (`r_loadProfile`).
- [levelport-kabuki.md](../levelport-kabuki.md): the first two-layer map, look by look.
- [night-city.md](night-city.md), [beauclair.md](beauclair.md): the two ports built on this chapter.
