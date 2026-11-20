# Beauclair levelport (plan 2026-11-10; work since 2026-11-15)

## Resume here (2026-11-17, evening)

- **Update 2026-11-19:** the notes below are the 2026-11-17 state. The W3 renderer work is no longer unmerged: commits
  d405a544a and 8d2e73ea9 are ancestors of master (checked with `git merge-base --is-ancestor`), and Install 94
  (2026-11-19, `docs/renderer-vulkan-in-xenjk.md`) replaced the installed renderer with master's, so
  `Play_Beauclair.bat` and `Play_Beauclair_Castle.bat` load the installed renderer and the per-kit renderers
  (`w3_city_kit-*`, `w3_castle_kit-*`) are no longer used. Install 89 is not the current install; read the newest
  "## Install NN" in that file.

- **Installed now (Install 89, 2026-11-17, owner "Yes, install it"):** the city as map w3_city-3, scene-14 (review fixes,
  water, waterfalls), effects fx-7 (the game's water particles); before it Install 88 had scene-11, decals-1, env-city-4 (canopy shading), grade-1, with kit `w3_city_kit-3`'s renderer (cccb3178d) and the terrain
  push, LOD bias and specular AA on the bat's command line; record in `docs/renderer-vulkan-in-xenjk.md` "Install 89".
  The castle is still Install 85's. Open: the game's water colour, the near-mirror kerbs (needs a game
  reference), kept-cascade caster flips, the `g_013` door, clouds, grass past 60 m.
- **State:** the whole city is built as one map (owner 2026-11-16 "build it fully", day fixed at 14:00): section
  "The city (2026-11-17)". **Installed as Install 85** (owner "Yes, install both"): `GameData/Play_Beauclair.bat`
  (city) and `Play_Beauclair_Castle.bat`, the 10 packs copied into `GameData/base/`, each bat loading its kit's
  renderer (`.codex-research/levelport/build/w3_city_kit-1`, `w3_castle_kit-1`); entry in
  `docs/renderer-vulkan-in-xenjk.md`. Smoke: both load and draw from GameData.
- **Next (by size):** collision under the rich houses' base floors (the fill misses them; most other fall-through is
  ivy and flower beds, as in the game); the far valley past the city's 16 terrain tiles (stand-in ground colour);
  the game's particle clouds (cumulus stand-in); billboards and wind for the trees; paint and wear roughness;
  VarianceColor's meaning outside cloth; the sun-side sky colours. Then a playtest.
- **Code:** converter on `renderer/pbr-materials` (main checkout, `tools/levelport/w3*.py`); renderer on
  `renderer/w3-vertblend` (worktree `.claude/worktrees/w3-vertblend`, 49e0e421d: cascade blend and fade,
  `r_sceneShadowFar`). Not merged; Install 85 predates map-3, scene-4, env-city-3 and this renderer.
- **Latest video:** ground level, section "Ground-level fly-through and the shadow pops".
- **Rebuild chain:** `scripts/dump_districts.sh` (w3gltf + w3trees per district, sequential: parallel Blender imports
  race on the shared uncook files), `scripts/city_stage1.sh` (cloth per layer with w3flags + w3merge, w3city merge,
  w3terrain), `scripts/city_map.sh TAG K [FILL_COMPARE]` (w3build, gltf2map, spawns), `scripts/city_scene.sh TAG 2048`,
  `w3decals.py build`, `w3env.py` / `w3grade.py --map w3_city`, `levelcheck compare --zmax 1e5` / `drop`,
  `scripts/make_city_kit.sh N`. All under `<levelport root>\levelport\w3\beauclair\`.
- **Traps:** Bash heredocs turn `\0` and `\x..` into bytes: write Python with backslashes to a file. A worktree's
  `tools/renderer_spirv.py` looks for glslang in the wrong folder (set `GLSLANG_EXE`). Long chains: Start-Process
  Git Bash with a script file; stopping a background task leaves the script's own children running (memory).

## The plan (2026-11-10)

Plan only, nothing built. It ports Beauclair (Witcher 3, Blood and Wine) the way Night City was ported: a gameplay BSP
plus an rd-vulkan render scene ([levelport-scene.md](levelport-scene.md)). The source is the extraction handed off in
`<w3 extraction>\output\HANDOFF_city.md`. Like every levelport output it stays local and is never published.

## What carries over from Night City, and what does not

| Night City piece | Beauclair |
|---|---|
| Two layers: gameplay BSP (`gltf2map`, `--vis none`) + `.xscn` render scene | Same. Reuse as is |
| `cp2gltf.py` stages (`sectors`, `scan`, `fetch`, `build`, `scene`) over WolvenKit JSON | **New front end** `w3gltf.py`. The Witcher 3 data (`.w2l` layers, `.w2mesh`, `.w2mg`, `collision.cache`) is read by the dingdio add-on's own Python readers inside headless Blender, because WolvenKit 7.2 cannot read Next-Gen bundles. It writes the same work-folder shape `scene` and `build` read: `placements.json` (meshes, lights), one `.glb` per mesh, and material records |
| `cp77mat.py` (multilayer, mlsetups, `.xdet` detail slots) | **New** `w3mat.py`: `WitcherTexArray`, `pbr_vert_blend_colorize`, `pbr_std_wear_paint`, `Witcher3_Main`, glass, ivy (`pbr_hair_moving`). The parameters are already on every material (`witcher3_mat_params`) |
| City tile grid, chunked collision, residency, the ±65,536 u limit | **Not needed.** The city is about 730 x 630 m (27 k x 24 k units) with about 164 k placements. That sits between Kabuki (62 k) and the Watson stress slice (267 k, installed and played as one map). One map, one hub BSP. Tiled `.xscn` files only if a file passes about 1 GB |
| Collision: boxes for props, slabs for structure, tile compiles + `colmerge` | Better source: the game's own PhysX shapes. About 91% are boxes (exact brushes), the rest convex hulls (one brush) or tri-meshes (slabs). Tile compiles + `colmerge` if it passes about 500 k brushes |
| Lights (CP's, cones, hide distances, haze share) | Entity light components (268 in upper_south alone), game intensities to calibrate. Day sun + sky from the environment definition, not night lamps |
| `.xlut` grade, `.xfog` haze from CP's environment | From the `.env` environment definitions (the add-on has `import_environment.py` / `environment_catalog.py`); not extracted yet |
| Terrain (`meshRef` meshes) | **New**: heightfield tiles (257² at 1 m) + a `bob_array` texture-array splat. Render as scene meshes with a terrain material; collision from a decimated heightfield |
| Foliage (`cp77_foliage.py`) | SpeedTree 7 point clouds (208 `fi__*` sets) + city trees. Camera-facing leaf cards need a decision (below) |
| Water (`patch_water`, `lp_water`) | The harbour and the river need the global water plane, which was not extracted |
| `levelcheck.py`, `look.py`, `look_stats.py`, `retain.py`, kits, `Play_*.bat` | Reuse as is |

## The decision that matters most: vertex colour and the second UV

The scene vertex is 28 bytes: position, normal, tangent and one UV. The Witcher's architecture shaders depend on a
**second UV set and two vertex-colour attributes** (texture-array blend weights, tinting, wear). Watson showed what
baking blends into one 512 px image per mesh does: waves, blobs and blur on the ground (fixed only by the 8 detail
slots). The options:

**Owner, 2026-11-10: A.**

- **A. Scene v8 vertex stream (chosen):** an optional second stream per mesh (uv1 f16x2 + two colours u8x4,
  12 bytes) and a material kind that blends texture-array slices by vertex colour and mask at draw time. Nearest to
  the source, sharp at any distance, no per-mesh bakes. It needs a renderer change (the renderer session's work) and a
  format bump.
- **B. Bake into the existing `.xdet` slots:** no renderer change, but a bake per mesh that leaves the per-vertex blends
  coarse, which is the mistake Watson made.

## Phases

Each phase ends in something checked offline before anything goes into the game, and one district is finished
before the rest are batched (owner rules: offline first, perfect one case first).

0. **Measure (half a day).** Census from the delivered `.blend` files: placements, unique meshes, triangles at LOD0
   and with full LOD chains, materials by shader, images by format, lights. Run the collision census over every layer
   (`test_collision.py` path, collision only, about 1-2 h). Check drive space: the extraction is about 13 GB of
   `.blend` plus 13 GB of `uncook\` on D:, and the level port lives on C: (2026-11-10: C: 148 GB free, D: 193 GB).
1. **Front end, pilot: the ducal palace's main castle building** (owner, 2026-11-10: "the actual castle main
   piece"). It is part of `beauclair_palace.blend` (68 layers, 15.4 k objects, palace and gardens). Phase 0 picks out
   the layers that make up the castle itself; the gardens and the rest of the palace grounds come after. `w3gltf.py` inside headless Blender: layers → `placements.json`; meshes with **every LOD** (the
   handoff kept LOD0 only, `keep_lod_meshes=False`), both UV sets and both colours; collision shapes with their
   physical material names; lights. Skip FBX entirely.
2. **Gameplay BSP for the pilot.** Boxes → brushes from their 8 corners, convex → one brush, tri-mesh → `shape: mesh`
   slabs. The physical material names map to JKA surface flags for footsteps and impacts (`stone_solid`, `wood_solid`,
   `dirt_hard`, ...). Spawns, boundary, `levelcheck` (spawns, aerial, holes, drop).
3. **Render scene for the pilot.** `w3mat.py`, decision A or B, the `.xscn`, images as BC1/BC7 DDS (the handoff notes
   some `.dds` formats Blender cannot read; convert them with texconv). First look runs against the owner's own
   in-game reference shots of the same spots (`look_stats.py`).
4. **Day light and atmosphere.** Sun cascades (already in the renderer), sky, environment fog, the grade, light
   intensity calibration. This is the Witcher counterpart of Night City's night work, and the look will turn on it.
5. **Terrain, trees, water.** The 16 terrain tiles, foliage instancing, harbour water.
6. **The remaining districts** (the rest of the palace grounds, upper south, upper west, upper east/north, lower city, harbour, doors/trees, misc) through
   the same recipe, then a merge into one map, its level checks and a playtest kit.
7. **Later:** doors and rigid bodies as entities, particle effects (candles and fire are placeholder empties now), the
   navmesh for Bot2 (none extracted; regenerate from the collision), interiors.

## Phase 0 results: the castle (2026-11-15)

The castle is `palace/main/**` under `city_of_beauclair/`: 29 layers in the visual import (`upper_palace`,
`lower_palace`, `tower_cell`, `room_with_mirror`, the castle-hill `cliffs`, the two castle roads) plus the two light
layers the handoff's filter dropped (`main/lights/lights_exterior.w2l`, `lights_interior.w2l`). The bridge, gardens,
dungeon and the rest of the grounds are outside it. Work folder: `.codex-research/levelport/w3/beauclair/`
(`castle_layers.txt`, `palace_census.json`, `scripts/castle_collision.py`, `collision-1/`).

**Size.** About 250 x 235 m and 190 m tall (game X −795…−546, Y −1327…−1093, Z 55…244), so about 9.3 k x 8.8 k
Quake units: a small map.

| | Castle | For scale: Kabuki |
|---|---|---|
| Mesh placements | 4,184 | 61,710 |
| Distinct meshes | 397 | 2,718 |
| Triangles, LOD0, unique / placed | 1.16 M / 4.06 M | 3.2 M unique |
| Materials / images | 245 / 362 | |
| Lights (visual layers only) | 73 | 1,900 |

Materials by shader (placements): `WitcherTexArray` 2,291, `Witcher3_Main` 2,028, `pbr_vert_blend_colorize` 1,978,
`pbr_std_wear_paint` 205, ivy 22, glass and transparent 7. 2,287 placements have the second UV set and 2,171 both
colour attributes: more than half the castle needs the vertex stream (decision A). 68 placements have leaf cards.

**Collision** (`collision-1`, the add-on's collision-only import of the 31 layers, 155 s): 5,184 shapes, no rigid
bodies.

| Shape | Count | Brushes |
|---|---|---|
| Box | 3,816 | one each, exact |
| Convex hull (`_col`), at most 115 vertices | 977 | one each |
| Triangle mesh (`_tri`) | 208 (63,369 triangles, at most 921 a shape) | slabs |
| Capsule / sphere | 125 / 58 | a few-sided brush each |

Surface types: `stone_solid` 4,739, `glass` 154, `clay_tile` 126, `flesh` 73, `wood_solid` 65, then a few each of
dirt, carpet, debris, metal and rubber. That is well under 100 k brushes, so one q3map2 run, no tiles or `colmerge`.

**Gaps found.**
- The ground the castle hill stands on is terrain (heightfield), not layer collision: the castle needs the terrain
  tiles under it for both layers.
- The decal layers (`main/solid/decals.w2l`, `upper_palace/decals.w2l`) brought in no objects: the add-on does not
  import decals. They need a reader of their own.
- The two light layers' lights are not counted yet.
- LOD chains are not measured (the visual import kept LOD0 only).

## Phase 1: the castle front end (2026-11-15)

`tools/levelport/w3gltf.py` (runs in headless Blender with the add-on) imports the layers with the add-on, reads the
placements, materials, lights, effects and collision back out, and reads every placed `.w2mesh` again with the
add-on's raw reader for what the Blender import drops. One space throughout: game space, metres, +Z up; mesh files
are mesh-local in the same axes (not glTF's +Y up). The docstring lists the files. `gltfio.Writer.mesh` gained
optional TANGENT, TEXCOORD_1 and primitive extras for it.

`tools/levelport/w3_preview.py` rebuilds a work folder in Blender from its own files alone and renders it (top, two
perspectives, the collision alone), so the dump is checked apart from the add-on's scene.

**The castle, `work-2`** (31 layers, all imported; 10 min, nearly all of it the add-on's import):

| | |
|---|---|
| Placements | 4,214: 4,092 `.w2mesh`, 122 SpeedTree `.srt` (not dumped yet) |
| Meshes | 328, none failed; LOD chains of 1 (192), 2 (101), 3 (34), 4 (1), each LOD with its distance (e.g. 0 / 15 / 35 m) |
| Triangles | 657 k unique at LOD0 (phase 0's 1.16 M counted shared meshes once per layer), 959 k across all LODs, 4.08 M placed at LOD0 |
| Vertex data | both UV sets and the game's raw colour bytes; LOD0 vertex counts equal the Blender import's for every mesh |
| Materials | 243, each with its `.w2mi`/`.w2mg` base, every parameter and its image files |
| Lights | 193 (145 point, 48 spot), with the game's radius, angles, flicker and shadow mode |
| Effects | 742 particle placeholders and preview meshes (embers, smoke, flames), positions only |
| Collision | 5,184 shapes, as in phase 0, with their world vertices and faces |
| Skipped | 9 armatures (the waving flags) |
| Size | 94 MB |

The preview matches the add-on's scene: the castle, its spires and the cliff rocks are in place, the collision sits on
them, and the bounds agree with phase 0 (the two light layers reach further, to Y −1442, with lanterns along the
road). `work-1` was the same run before the effect meshes were sorted out (488 counted as skipped); it is superseded.

**Not in the dump yet:** the SpeedTree trees (122), the decals, the terrain under the castle, the waving flags.

## Phase 2: the castle's gameplay BSP (2026-11-15)

Three new pieces and one converter change:

- `tools/levelport/w3terrain.py` (Blender + add-on): the terrain tiles under a box, no foliage, as vertex grids in game
  space (`terrain/tiles.npz`, `tiles.json` with each tile's buffers and layer metadata). The add-on builds a tile
  mesh about its own centre in X and Y (heights are absolute); the script places it by the world's tile bounds. The
  castle needs tiles 12-13 x 10-11 (1 m grid, heights 70-186 m); a render with the castle shows it sitting on its hill.
- `tools/levelport/w3build.py` (plain Python): the gameplay glb from `collision.json`, the terrain and the lights.
  Boxes and convex hulls become one detail brush each; spheres and capsules the hull of their 26 support points;
  PhysX triangle meshes slabs; the terrain slabs every 2 m. Each carries its JKA surface material, mapped from the
  game's physical material (`stone_solid` rock, `clay_tile` tiles, `wood_solid` solidwood, `glass` glass, terrain
  dirt, ...). A playerclip wall around the box and a lid 40 m over the highest collision; the 190 lights for the light
  grid; spawns on open floor near the courtyard.
- `w3_preview.py` draws the terrain too.
- `gltf2map.py`: a collision node's `jka_material` extra becomes the twin shader's `q3map_material` line, so
  footsteps and impacts follow the surface. (q3map2's JA mode reads `q3map_material`; plain `material`, which the
  renderer also accepts, left the bits 0 in `w3_castle-2`.)

**Build `w3_castle-3`** (`.codex-research/levelport/build/w3_castle-3/zz_w3_castle.pk3`, 17 MB; glb
`work-2/castle-2.glb`, box game X −830…−520, Y −1360…−1070, origin at its centre): 108,082 brushes (4,981 shape and
boundary brushes, 103,101 slabs), all of them in the BSP; compiles in 60 s (`-bsp` 9.5 s, `-light fast` 4.5 s).
Brushes by surface: rock 55,436, dirt 50,821, gravel 1,389, glass 154, tiles 126, flesh 73, wood 65, carpet 8,
metal 2, rubber 1.

Checks: `levelcheck spawns` 0 of 16 stuck, all 16 over a floor; no leak (850 leafs filled); the client loads it
(`look.py`, sky only: the BSP draws nothing).

Failed attempts, kept: `w3_castle-1` with the shapes structural: q3map2 `-bsp` ran 10 min in "node without a volume"
portal warnings and was stopped (the shapes are detail since); `w3_castle-2` had the material bits at 0.

**Not checked yet:** the collision against what is drawn (`levelcheck compare`, `holes`, `drop`) needs the render
scene, phase 3. Water, doors and the flags' collision are not in.

## Phase 3: the castle's render scene, first pass (2026-11-15/16, in progress)

- `w3gltf.py` now records every material's shader inputs as the add-on wired them (`materials.json` "nodes": each
  group node's inputs resolved to images with channel, colour space and UV set, values, texture-array slices). The
  first dump kept only the instance's own overrides, and 57 materials had none: the full chain (instance, `.w2mi`,
  `.w2mg` defaults) exists only in the add-on's node wiring. Castle dump `work-3`.
- `tools/levelport/w3scene.py`: the scene pack from a work folder and `w3build`'s record (same origin as the BSP).
  Meshes with all their LODs (W3's switch distances as screen sizes), an instance per placement, the 190 lights (the
  game's brightness, radius, cone), the terrain as 64 m meshes with a flat stand-in material. Materials in a first,
  one-layer pass: Diffuse / Normal / roughness in the normal's alpha (x RSpecScale + RSpecBase) for the main, glass and
  ivy shaders; BaseDiffuse / BaseNormal for wear-paint; slice 0 of the arrays for the vertex blends. Normal maps
  DirectX to OpenGL. Writes scene version 5 (the installed renderer reads 1-5; 6 only adds tube lights).
- `w3terrain.py` fix: the add-on places a tile (its centre, and Z at the world's lowest elevation, −15 m) after
  creating it, and `matrix_world` holds that only after a view-layer update. `terrain-1` sat 15 m high and buried 79%
  of the castle's street and wall collision; now 98% of the cliff shapes straddle the ground, as they should.

**Builds:** BSP `w3_castle-4` (spawns 0 of 16 stuck) with scene `w3_castle_scene-3` (253 MB: 4,076 castle instances,
49 terrain meshes, 371 meshes, 1.36 M triangles over all LODs, 182 materials, 261 images, every image decoded, 190
lights). Looks: `.codex-research/levelport/w3/beauclair/look-scene3/sheet.jpg` (`r_sceneSun 10`).

**What the first look shows:** the castle, its spires, cliffs and courtyards draw in place on the BSP, sun shadows
work. Not right yet: the light (the scene sun defaults to Night City's 0.25 moon; at 10 sunlit plaster burns white and
shadows are black: no sky light), the one-layer materials (the courtyard floor near black, walls flat), the terrain's
flat stand-in colour, two of the eight spawns view from inside meshes, the 122 SpeedTree trees, decals and the harbour
water are not in. The collision compare against the scene (`levelcheck compare`, `holes`, `drop`) has not been run.

**Next in phase 3:** the renderer's scene v8 (second UV set and vertex colour, the texture-array blend material), the
terrain's splat material, then the collision compare. Daylight calibration is phase 4.

## Phase 3b: layer blends in the renderer (2026-11-16)

The owner's choice A, built as a sidecar rather than a new `.xscn` version, so no existing scene, loader or tile
manifest changes: `maps/<map>.xw3b` beside the scene.

- **Format** (`vk_scene.cpp`, `Scene_LoadW3Blend`, whose comment is the spec): header `XW3B`, version 1, material
  count, vertex count; 12 bytes a vertex in the scene's vertex order (second UV set f32 x 2, the colour's raw bytes);
  1232 bytes a material (20 floats, 18 image paths: 8 layers' colour, 8 their normals with the roughness in alpha,
  the pattern and its mask).
- **Renderer** (branch `renderer/w3-vertblend` from `baseline/1` f6ca2a12, worktree
  `.claude/worktrees/w3-vertblend`, uncommitted): the layers' images join the 8-slot detail images at the start of
  the GPU-driven image array and the material's detail record says −2 with a 9-vec4 block; the stream is draw-set
  binding 27, read by `scene.vert` at `gl_VertexIndex` (only for a scene that does not stream). `scene.frag` blends
  as the Witcher 3 Blender tools wire the game's `pbr_vert_blend_colorize`: green x 16 a place along the array (its
  two nearest layers mixed by the fraction), the tint on the layers from `ColoredTextureBottom` to `Top`, red toward
  `AO_Tint` by `AOPower`, blue the pattern (second UV set) where its mask says; roughness the normals' alpha x
  `RSpecScale` + `RSpecBase`. The per-material path draws the first layer as before. The existing `md.x < 0` test
  (8-slot detail) is now `== −1`. Build: `.codex-research/w3-vertblend/build.sh` (Win32 Release, renderer only,
  0 warnings); `GLSLANG_EXE=<glslang>\bin\glslang.exe python tools/scene_spirv.py` from the worktree.
- **Converter**: `w3scene.py` writes the stream for every vertex (zeros where a mesh has none, terrain) and a record
  per `pbr_vert_blend_colorize` material; `--version 6` for this renderer, `--no-layers` to leave the file out.

**Result** (`w3_castle_scene-4`, 294 MB, on `w3_castle-4`): the renderer reads 25 layer-blend materials (2 to 6
layers each), 43 slot images, none missing. Side by side with the first-layer scene-3 under a test daylight
(`look-day-scene3` / `look-day-scene4`): the palace arch's ornament goes from slice 0's rusty marble to the white
braided stone, the vault from slice 0's bricks to the blended plaster and ribs; everything else unchanged.

**Seen, not yet fixed:** close textures are soft (the 1024 px cap; the game's detail maps are not used yet); the
terrain stand-in shows through the upper courtyard and the tower cell (64% of its interior's collision lies below the
heightfield: the game cuts terrain holes there); the light is a test setting (`r_sceneSun 8`, `r_sceneAmbient
"1.2 1.4 1.9"`, `r_sceneSky 1`), not the game's.

## Phase 3c: terrain shader, holes, proxies (2026-11-16)

- **Terrain shader** (renderer `renderer/w3-vertblend`, `maps/<map>.xw3t`; spec in `vk_scene.cpp`
  `Scene_LoadW3Terrain`): the game's terrain as the Witcher 3 Blender tools emulate it (`terrain_detail_nodes.py`).
  Each tile's control lattice ((512 + 1)² 16-bit words, row +y, column +x, neighbours' edges stitched; read straight
  from the `.w2ter` texture buffers, which match the height buffer's layout exactly) goes into the material-detail
  buffer as exact numbers, not an image (the image loader may resize or compress). Per fragment, four control taps:
  the overlay layer (word bits 0-4) on world XY at 0.333, the background layer (bits 5-9) at the tap's UV scale
  (bits 13-15) in three projections weighed by the facing, blended by the slope against the taps' threshold (bits
  10-12) and the layers' sharpness, the background normal flattened by its base dampening, then the tile's tint as
  an overlay. 31 layers (`bob_array`), colour and normal (roughness in the normal's alpha). One departure from the
  Blender tools: the background's colour is triplanar too (they keep it from above; cliffs streaked).
- **Holes** (control word 0): `w3build` and `w3scene` both leave out the terrain triangles whose centre the game's
  shader would not draw (bilinear share of solid taps under 0.5): 166 collision and 1,236 render triangles, mostly
  under the tower cell. Shared helpers `w3build.control_lattice / solid_share / terrain_tiles`.
- **Proxies** (the texture study, an Opus agent's report: `w3 .../texstudy/`): the game's 38 `entity_proxy` shells
  (63 placements, one LOD starting at 70-140 m, textured at 2-33 px/m) were drawn at every distance over the real
  walls. They now carry their first LOD's distance as the instance's near distance and draw only past it.
- **Opaque albedos**: the Blender tools mark every main-shader material with its Diffuse alpha linked as a hashed
  blend; 126 whose albedo alpha is opaque throughout are opaque again (were cutouts with BC7 coverage mips).

**Builds:** BSP `w3_castle-5` (holes cut; spawns 0 of 16 stuck), scene `w3_castle_scene-6` (`--version 6`).
`look-day-scene6`: textured cobbled courtyards and mossy cliffs, the palace walls' brick and inlaid floors, copper
roofs. Open: a cobbled terrain mound in front of the east walls (likely ground under the bridge or gate layers, which
are not in the castle's layer set); daylight is still the test setting.

**From the texture study, not done:** `--tex-size 2048` (148 of 247 images are larger than 1024; about 680 MB
against 280 MB with ORM kept at 1024 and opaque albedos BC1); the moss/grass top layer of the 10 `snow.w2mg` rock
materials (25% of the placed area; a slope blend); detail normals (8 materials, 1.3% of the area: low value).

## Texture cap 2048 (owner, 2026-11-16: "Yes, 2048")

`w3scene.py --tex-size` now defaults to 2048 for colour and normals (terrain layers and blend slices included); ORM
images stay at 1024 (`ORM_MAX`). Scene `w3_castle_scene-7`: 870 MB pack against scene-6's 308 MB (the study's
~680 MB estimate left out the terrain layers and blend slices at 2048); build 10 min. Side by side at 1600x900
(`compare_tex_1024_vs_2048.jpg` in the work folder): cliff rock and cobbles slightly crisper, the inlaid floor about
the same at that distance; the gain is up close and at higher resolutions. Not timed, not measured in VRAM.

## Phase 4: daylight, first pass (2026-11-16)

`tools/levelport/w3env.py` (headless Blender + the add-on's CR2W environment readers) reads Toussaint's world
environment (`bob.w2w`: its default definition `sunny/sunny_bob_v7.env`, 415 curves, and the sun's trajectory),
evaluates it at `--hour` (14:00) and writes `maps/w3_castle.xenv` in its own pack `zz_w3_castle_env.pk3` (staged
beside the map and scene packs) plus `env.json`. Mapping (the scales are the calibration):

| Game | Renderer (`.xenv`) | 14:00 |
|---|---|---|
| the trajectory's light direction (the game lights from higher than it draws its sun) | `sun r g b 100 yaw elev` | yaw 240°, elevation 65.4° |
| `m_globalLight.sunColor` hue | the sun's colour; strength `r_sceneSun` 4 | 1.00 0.88 0.78 |
| `envProbeBaseLightingAmbient.colorAmbient` hue | `r_sceneAmbient` x 0.8 | 0.80 0.76 0.70 |
| `m_sky.skyColor` / `skyColorHorizon` | `r_sceneSky 1`, zenith / horizon x 0.6 (linear) | |
| `m_globalFog.fogColorFront` / `Middle` averaged, `fogDensity` | `r_sceneFog`, `r_sceneFogDensity` | 98 181 196, 0.00059 |
| `m_toneMapping.exposureScale` | `r_sceneExposure` | 1.04 |
| (a dry day) | `r_sceneWet 0` | |

Found on the way: `Scene_LoadEnv` keeps a value's characters as they are, so quoted vectors (`"0.8 0.76 0.70"`) read
as 0 (the first try's black shadows); and the renderer's haze defaults to Night City's brown at 0.005/m (half of what
is 140 m away hidden), which turned the first try sepia. The calibration (`look-envcal-a`..`e`, sun 3-8, ambient
0.4-1.2) chose sun 4, ambient 0.8, sky 0.6 (`env-2`, tour `look-env2-tour`): warm white plaster with its texture,
green copper roofs, shadows filled, short shadows from the high light.

**Open:** the sky is the renderer's analytic gradient (saturated teal, no clouds, no sun disc as the game draws it);
the game's tone curve (its AgX / custom curve and colour balance) is not mapped (the renderer's filmic curve); no
reference screenshots from the game to judge against; one hour only.

## Phase 4b: references and root causes (2026-11-16)

Owner: "everything very flat/washed out ... go online to find refs". References (viewed in the browser, not stored):
GameGPU's Remaster / Next-Gen / original comparison shoots the palace's inner courtyard at 09:35, clear (the round
stair, the fountain: a view we have); the Witcher wiki's `Tw3_beauclair_palace_3` shows the palace facade by day.
Both: warm cream stone, terracotta-red roofs, deep warm shadows, strong occlusion, saturated sky. An Opus agent
audited the material pipeline (`w3 .../audit/`): textures, normal maps (DirectX, BC7 arrays with roughness in alpha),
tangent frames and roughness (normal alpha x RSpecScale + RSpecBase) are right; the flat look is mostly the
lighting model.

Found and fixed:
- **Green roofs:** `pbr_std_wear_paint` materials draw their base only; the roofs' base is green copper tinted red
  (BaseDiffuseTint 0.88 0.007 0.007 x 0.6). `w3scene.py` bakes the tint into the albedo (8 materials); the paint,
  wear and vertex-AO layers (the Blender tools' chain, socket map in this session's notes) are not done. Wear-paint
  roughness now from the base normal's alpha (the tools leave it unwired).
- **No grade:** `tools/levelport/w3grade.py` bakes the game's display transform (exposure from the shaped key, the
  environment's Hable-style curve and postScale, gamma, the 64^3 balance map `sunny_bob_v0.xbm` at 0.63) into
  `maps/w3_castle.xlut` (33^3, LogC in); `w3env.py --grade` sets the renderer's own exposures to 1 and hands the light
  to the grade (`--grade-exposure`, the units' bridge: 24).
- **Ambient 10x too strong** against the sun (the game: sunColor scale 182, ambient 4.03): ambient 0.2 against sun 6
  (`env-i2`).
- **Specular:** every surface had F0 0.04 and no specular ambient. Renderer: Witcher 3 materials (detail codes -2,
  -5) take the sky through the split-sum BRDF and their own F0; `.xw3b` version 2 carries F0 (24 floats a record).
  Converter: F0 from a *wired* `SpecularColor` on the main shaders only (unwired inputs hold the group's default 1.0:
  mirrors; the layer-blend marble's 0.43 0.28 0.03 made gold mirrors). Terrain left out (its grass mirrored the sky).

Builds: scene `w3_castle_scene-11` (`--version 6`), env `env-j3` + grade `grade-1` packs; look `look-sky-j2`.

**Ambient occlusion (2026-11-16):** renderer `r_sceneAO` (worktree branch `renderer/w3-vertblend`, 6ff302289):
computed in `scene_ssr.comp` beside the reflections on last frame's half-size depth (hemisphere samples about the
rebuilt normal, range-checked, settled over frames), applied by `scene.frag` to the ambient light only. `w3env.py`
writes `r_sceneAO 1`, `r_sceneAORadius 3` (`--ao`, `--ao-radius`); 3 m read better than 1.5 m (`look-ao1`,
`look-ao3`). Its effect is modest while the ambient is 0.2 of the sun: the seams, pilaster feet and arches darken.
(`r_ssao 0` still crashes with a scene loaded.)

**Wear-paint (2026-11-16):** renderer 9d7481aba (worktree branch), `w3scene.py` record kind 1 (`.xw3b` float 23;
layers 0, so older renderers read F0 only). The Blender tools' `pbr_std_wear_paint` "W3 Wear" chain: paint by vertex
blue past the masks' green (pattern slice 0 x PaintTint), wear by 1 - vertex alpha (the *untinted* base x
WearBrightness, desaturated, x WearTint), vertex red x UseAO. Scene `w3_castle_scene-11`, look `look-scene11-wp`:
courtyard roofs weathered terracotta; `tower_roof_b` spires blue-grey slate (vertex blue 1: full paint with the cyan
tint, as the data says). Paint/wear roughness inputs not applied.

**Sky (2026-11-16):** the game's sky colours were right (14:00: zenith 35 160 207, horizon 103 208 241: turquoise,
as the wiki's palace shot); it drew pale at the hand-set scale 0.6. `w3env.py` now scales it by the game's own
sky-to-sun ratio (--sun x m_sky.globalSkyBrightness / sunColor's scale: 0.25), curve 2 (`r_sceneSkyCurve`, renderer
commit on the worktree branch). Stand-ins, flagged: clouds are the renderer's cumulus (`--clouds 0.5 --cloud-scale 3`;
the game's are particle systems, weather table WT_Clear = `bob_mid_clouds_small_1.w2p`, not converted) and the land
under the horizon is a flat hazed colour (`--ground 120 150 150`, `r_sceneSkyGround`) until the valley's terrain is
converted. Env pack `env-j3`; looks `look-sky-j1`, `look-sky-j2`. The game's sun-side horizon colours
(sunColorHorizon, sunBackHorizonColor) and sun halo (sunColorSky x sunColorSkyBrightness, sunAreaSkySize) are not
modelled: their formula is not known.

**Open, by size:** the valley's terrain (replaces the ground stand-in under the horizon); the game's particle clouds
(replace the cumulus stand-in); `tower_roof_b` spires blue-grey where the wiki shot shows some cone roofs red (which
meshes, unchecked); paint/wear roughness; two distant tower shafts drawing cyan (not identified); VarianceColor on the
main shader (meaning unverified); the sun-side horizon colours and sun halo (formula unknown); `r_ssao 0` crashes with
a scene loaded.

## Castle finished (2026-11-16/17)

Built on top of phase 4 (sky, AO, wear-paint above):

- **Trees** (`w3trees.py`, agent): the 122 SpeedTree placements (21 kinds incl. one grass) as work-folder meshes with
  every LOD and cutout leaf materials; the glbs equal the add-on's import vertex for vertex. Fixed cards (decision).
  Not carried: billboards, wind, hue variation, SpeedTree specular.
- **Flags** (`w3flags.py`, agent; merged with `w3merge.py --drop box_cloth.w2mesh`): APEX cloth in its bind pose, 8
  racing pennants and 5 wedding garlands; opaque, double-sided; the garland's red cloth is its white cloth tinted by
  VarianceColor (read as a tint about sRGB 128, applied to cloth only: 30 other castle materials carry VarianceColor
  greys and browns whose meaning is not a tint).
- **Decals** (`w3decals.py`, agent): W3 packs them in each layer's sector data (no components); 572 castle decals,
  62 diffuse images, `.xdcl` version 2, +Z out of the surface, flag bits 0/1 = V/U flips (measured), hide distance
  from the sector radius (inferred). `w3_castle_decals-1`. Left/right of unmirrored images unmeasurable on the castle
  (no text decals): `FLIP_U_ALL` if a city sign reads mirrored.
- **Rock grass** (`snow.w2mg`, 9 materials): renderer slope layer (-7), grass where the world normal's up component
  passes ThresholdMin..Max (a reading of the names; the add-on leaves them unused).
- **Cyan shafts:** four `blockout_base\capsule_stone.w2mesh` placeholders (material id view); blockout meshes are
  left out of the scene and the fill. The tall tower windows' glass instances now draw as blended glass.
- **Collision:** `levelcheck compare` capped surfaces at 600 u (Night City's collision height) and judged nothing on
  the castle (2,000-10,000 u): `--zmax`. Map-5 vs scene-11 inside the box: 7.5% of visible floors without collision,
  664 m2 a player falls through (balcony, column and wall tops, sloped roofs: places Geralt never reaches). `w3build
  --fill <compare>` adds slabs from the render meshes' walkable LOD 0 triangles there: map-7, 62 k triangles, 178 m2
  left in small patches (largest 9 m2). Spawns 16/16 clear.
- **Builds:** map `w3_castle-7` (`w3build --tag 6 --fill checks/compare-5-11b`), scene `w3_castle_scene-14`, decals
  `w3_castle_decals-1`, env `env-j3`, grade `grade-1`; look `look-castle14`. Checks in `checks/`.

## The city (2026-11-17)

- **Layers:** 282 outside the castle (`scripts/city_layers.py`: the handoff's exclusions, light layers kept), in
  seven groups. Dumps: palace grounds 3,526 placements, upper city 41,453 (124 layers in one Blender scene: its west
  exteriors took 73 min, the add-on slowing as the scene fills), lower city 16,391, harbour 16,074, lights 211 (264
  lights), minor 13. Trees 1,243 more; cloth on 21 more layers (banners, awnings, laundry; the embassy's tall flag
  needed `w3flags` to skip APEX collision spheres).
- **One work folder** (`w3city.py`, castle first): 82,034 placements, 2,898 meshes (hard links), 2,009 materials,
  1,370 lights, 68,260 collision shapes; terrain: 16 tiles.
- **Map** `w3_city-2`: box game X -960..-110, Y -1725..-930 (origin -535 -1327.5), terrain collision every 4 m (2 m
  would be ~700 k slabs), the fill from compare `city-compare-1-1`: 1.78 M triangles, 1.76 M brushes, about 20 M
  planes; gltf2map compiled it in tiles and merged them (12 min). Spawns 16/16. Fall-through inside the box 7,156 m2
  (map-1 without the fill: 11,319), mostly ivy, creepers and flower beds round the rich houses (the game has no
  collision there either) and some house base floors. New JKA surfaces for the city's physics materials.
- **Scene** `w3_city_scene-1`: 79,939 instances of 4,097 meshes, 8.8 M triangles at LOD 0, 1,920 materials, 1,457
  images at the 2048 cap, 2.98 GB (the engine's pack limit is 4 GB). **Decals** `w3_city_decals-1`: 5,564, 185
  images. Env `env-city-1` and grade `grade-city-1` (the castle's day for map `w3_city`).
- **Look** `look-city2` (map-2 loads): the street views read as Beauclair (banners, flowers, decals); from above the
  walled upper city and its roofs, the castle beside it, the lower city on its slope.

## Quality pass and fly-through (2026-11-17)

Owner on the first fly-through: "a very poor showing compared to night city"; the Night City session diagnosed it (its
frames and scripts in its scratchpad `bc\`); owner: "fix all of it, then redo the video". Video:
`.codex-research/beauclair-video-20261117/beauclair_flythrough_v2.mp4` (82 s, 1080p30; 720p copy beside it), shots
`runs/v9`, method `vidcap_bc64.py` (64-bit, demo record then `video` with benchcam), `paths.py`, `edit.py`.

- **Proxies drew over their buildings** (the main cause of the mottled roofs, dot-screen facades and the sweeping
  "fade-in"): only the proxy had a near distance. `w3scene.py` gives the full meshes inside an entity proxy's box (same
  layer, not vegetation) the proxy's near distance as their hide distance: 25,274 instances, 660 proxies.
- **Renderer fixes missing from the branch:** baseline/1 6e47de09 merged into renderer/w3-vertblend (2a5ff1fa3): the
  eye-relative eye view, cutout alpha-to-coverage, settle faces.
- **Roads fighting the terrain** in aerials: terrain tiles flagged 0x1000; `r_sceneTerrainPush 1` on the command line
  (read at load; with `r_sceneLodBias 2`, `r_sceneSpecAA 0.18`).
- **Cyan wet shade:** W3 materials mirrored `waterSky`, whose horizon end is the fog colour as linear light (several
  times the 0.2 ambient); now the ambient hemisphere along the reflected ray (49b3f1815). Roughness is as the game has
  it (normal alpha x RSpecScale + RSpecBase: pavement ~0.08, marble ~0).
- **Trees as sticks:** 1,528 vineyard poles with their vines missing. The world's terrain foliage (vineyards, pines,
  willows, ferns, grass) was never imported: `w3foliage.py` (Blender) reads it as SpeedTree placements, `w3trees.py`
  converts them: 142,832 more (195 kinds; grass and flowers hidden past 60 m).
- **Cliffs streaked:** the terrain overlay layer sampled from three directions, as the background already was.
- **The void:** a far terrain ring 2 km out (`work-city/terrain_far`, 400 tiles, every 8th vertex, control maps
  decimated; render only), ground under the horizon in the haze's colour (`r_sceneSkyGround` negative red,
  `w3env --ground haze:0.85`), and the far plane: `R_SetFarClip` caps zFar at worldspawn distanceCull x 1.732 (default
  6000), so map `w3_city-3` sets `distanceCull 200000`.
- **64-bit capture:** the 32-bit client ran out of zone on a third city load; the capture now runs Watson's x86_64 exe
  and game modules with the branch's x64 renderer (`.codex-research/w3-vertblend/build64.sh`).
- **Camera:** the lower-city path flew through a new pine; raised.

Builds: map `w3_city-3`, scene `w3_city_scene-3` (223,155 instances, 3.41 GB), decals `w3_city_decals-1`, env
`env-city-2`, grade `grade-city-1`. Not installed: Install 85 has the earlier map-2 / scene-1 and the 32-bit kit.

## Ground-level fly-through and the shadow pops (2026-11-17)

Owner on v2: "still a lot of pop in and flicker/z-fighting. can you try a fly through at like player/ground level?"
Video: `.codex-research/beauclair-video-20261117/beauclair_ground_v3.mp4` (101 s; `_720.mp4` 27 MB), routes
`g_upper`, `g_lower`, `g_castleyard` (`ground_route.py`: eye height, open-sky routing, wall and prop clearance), run
`runs/v13`, cut with `edit.py v13 --order g_upper,g_lower,g_castleyard`.

- **Measuring it:** frame-to-frame and three-frame tests (`micro.py`, `micro2.py`, `artifacts.py`) count detailed
  texture and shadow edges moving as flicker (a 0.5 u/frame dolly read 32-37% "flickering"). `flowres.py` /
  `flowpeaks.py` warp the previous frame onto the next by optical flow (OpenCV DIS, both ways consistent) and count
  what the motion does not explain: the micro-dollies are stable (0.1%), and real pops show as isolated peak frames.
  Shadow softening (`r_sceneShadowSoft`), shadow LOD, LOD bias, hide scale and receiver culling did not move them.
- **The pops were the sun cascades.** The kept cascades (`r_sunShadowFarCache`, `r_sunShadowMidCache`, centred on the
  eye's grid cell) moved a cell at a time, and the shader switched cascades hard and cut shadows off in a line at
  `r_shadowCascadeZFar` (4096 u, ~110 m): a band of shadow appeared or changed sharpness at each cell step (the castle
  courtyard going dark in one frame). Renderer 49e0e421d: cascades blend into the next before their edge (a kept
  middle cascade's band measured from the eye), shadows fade over 80-97% of the reach, and `r_sceneShadowFar` (a look
  value) lengthens the far cascade alone. Castle shot, unexplained share p99 / max: 0.236 / 0.364% before; 0.084 /
  0.145% with the caches off; 0.061 / 0.076% with the fix and the caches on; 0.057 / 0.071% with the reach 12000.
- **Env `env-city-3`** = env-city-2 + `r_sceneShadowFar 12000` (patched; `w3env.py --shadow-far 12000`): the castle
  keeps its shadows from the air. **Scene `w3_city_scene-4`:** no projected decals on vegetation and small props
  (instance slot bit 25; ab068f095).
- Left as is: sub-pixel shimmer of fine detail at speed (leaded window lattices, distant leaves) under MSAA 4.

## Review and fixes (2026-11-17)

Owner: have Fable 5.2 high and Astra 6.2 high review the footage, code and assets. Four reports and the merged list:
`.codex-research/beauclair-review-20261117/` (`REVIEW_MERGED.md`; the owner ran Astra). Owner: "Yes, start fixes".

- **Renderer** (`renderer/w3-vertblend`): 53dc5a0a9 the cascade blend also in depth along the light (both reviews);
  15fe13e78 proxy members fade against their proxy's centre (instance slot bit 27 + the centre as a signed byte an axis
  in 16-unit steps; `scene_cull.comp`, `scene.vert`, `scene_want.comp`), `.xw3t` version 2 with the terrain layers'
  specular level (the Blender tools' formula; it was 0.04 everywhere), haze shafts blend cascades, slope-layer gradients
  before the branch.
- **Converter** 663d7719f: proxy anchors (17,515 members, none on their own centre); a proxy hides only members wholly
  inside its box, never ground pieces (25,274 -> 17,515 hidden); helper materials skipped (shadowmesh, debug, gridmat,
  fake sky); 4,376 exact twin placements dropped; SpeedTree leaves flagged 0x800 (62); every near terrain chunk drawn
  and normals from the whole tile; stale staged copies replaced; non-finite tangents dropped.
- **The "lakes" were a hole.** The cyan rivers and lakes seen from the air were the missing terrain strip (62.5 m x
  1 km, near row 12) showing the haze colour under the horizon; drawn now, the valley is a dry bed until water is
  converted. The river meshes (`m_river_vertex_offset`) stay in (skipping them changed nothing visible).
- **Measured:** castle shot p99 / max 0.062 / 0.072% (unchanged); ground routes p90 0.90 / 1.54 / 1.21% (v13 0.97 /
  1.59 / 1.32): the walking-speed residual is dominated by near foliage, not the proxy band. The anchor fix is
  established in the data, not yet shown at a pose that crosses a swap.
- **Look options for the owner** (`.codex-research/beauclair-video-20261117/look_options.jpg`, stills `runs/s_look2`):
  canopy shading (`r_sceneFoliage 0.7`), haze height 200 or 300 m, ambient 0.42 0.43 0.45 (from 0.2 0.19 0.18). Not in
  the env until chosen.
- **Owner's pick: B** ("i like the dark shadows of the 3rd row trees in A and B"; haze is not needed to cover pop-in,
  which has its own fixes): canopy shading only, ambient and haze as they were. Env `env-city-4` = env-city-3 +
  `r_sceneFoliage 0.7` (patched; `w3env.py --foliage 0.7`); checked in game (`runs/s_env4`: 20 look values, 62 leaf
  materials).
- Builds: scene `w3_city_scene-6` (218,856 instances), env `env-city-4`.

## Water (2026-11-17)

Owner: "Start on water". The export has no water shapes: the global water sits at sea level (the world's per-tile
`minWaterHeight` values are depth bounds at or under 0), the lakes above it show only by what floats on them, and a
few meshes are water (fountain basins, the stream under the palace bridge, cave pools). `tools/levelport/w3water.py`
(fd91d4cba; `python tools/levelport/w3water.py <work>` reports the bodies) finds the surfaces and `w3scene.py` draws
them in the renderer's water mode (Night City's contract: one blend material flagged 0x80, ripple normal, tint
24 52 50, `maps/<map>.xwat`):

- **Sea level:** every terrain cell under 0, near and far tiles (1.88 km2): the harbour and the river below the dam.
- **The upper river and lakes** (80.2-80.8 m): the water lilies give the level; the terrain floods from them, no
  deeper than 12 m (stops at the waterfall dam), held back by placed meshes crossing the level, its core 1.2 m deep
  plus 3 m to the bank. A flood that would drown land plants (> 100) is land: the formal lily pool keeps only its
  basin. Lilies in a fountain that has its own water mesh are left to it. Rules found on the map
  (`crops/water_map.png`, `water_lakes.png`, `pool_map.png`); stills `runs/s_water5`, sheet `water_v1.jpg`.
- Scene `w3_city_scene-11`. Not done: waterfalls (the dam's mesh is still a plain opaque material), the game's own
  water colour and waves (tint and ripples are ours), puddles (off on a dry day).

## Waterfalls (2026-11-17)

Owner: "i love waterfalls..please do it. ask fable 5.2 medium and astra 6.2 medium for help", then "Start now" on the
effects. Fable's data report: `.codex-research/waterfalls-20261117/REPORT_FABLE_DATA.md` (the falls sit in layers the
city export did not use and in sector data a string search cannot see; the cooked materials' values). Astra's lane
(the rendering) was briefed (`codex_prompt_astra.md`); its report had not arrived when this was built.

- **Layers:** `palace/main/solid/fx/water`, `palace/bridge/fx/water`, `palace/gardens/blockout/water` and the world's
  `fx/rivers/bob_river2,3,4,5,7,12` dumped as a water district (`city_layers_water.txt`, `work-d-water`) and merged
  (`w3merge --tag water`): 25 waterfall strips, 7 river meshes, the pool planes, river 5's dam.
- **Renderer** 56826e03d: a flowing material (`.xw3b` record kind 3, `scene.frag` -8): base, normal and ORM scrolled and
  tiled by the game's clock, opacity scaled. **Converter** 85f3dda68: the waterfall strips (white sheet, the strip
  texture's alpha and normal, the game's speed and tiling), the dams, and the rivers (the game's shallow colour, 70%
  opaque, its wave normal along the stream: as still water they read as dry beds).
- **The bridge curtain** is a particle system drawing strip meshes: placed as five strip sheets at its emitters, at the
  emitter's cooked size (40, 30, 24.5; `w3merge --tag curtain`).
- **Spray, mist, foam, rings, fountain jets** (2418ce017, `tools/levelport/w3fx.py`, `w3fx_dump.py`): the renderer's
  particle path (`maps/w3_city.xfx`, Night City's format), pack `<levelport root>\levelport\build\w3_city_fx-3`
  (288 placements, 27 systems: the world's sector particles, which the export drops, and the fountains' entity
  effects). The add-on cannot read the cooked emitter modules, so lifetime, size, motion and alpha are hand-set per
  kind of effect; textures, blend, birth rate and caps are the game's.
- **Upgrade** (owner: "is the existing effect engine good enough", then "upgrade it"): `tools/levelport/w3emitter.py`
  decodes the game's cooked emitter modules (Fable subagent, from WolvenKit 7's layout; every cooked emitter in the
  game, 8,800 in 3,797 .w2p, read to its end), so the water effects take the game's own life, size, motion, curves and
  spawn shapes (35 of 35). Renderer cccb3178d: a reflective normal-mapped particle mode (the game's splash textures are
  normal maps: sky reflection with Fresnel, the sun's glint), foam and rings lying in their emitter's plane, gravity in
  world space. The fountains' jet trails are drawn as dense droplet streams along the same arc (no ribbon drawer).
  Converter 2827bf085, effects `w3_city_fx-7`, sheet `waterfalls_v3.jpg`. (REDkit, which would give the uncooked
  sources, was not found on this machine; the decoder made it unnecessary.)
- Scene `w3_city_scene-14`, effects `w3_city_fx-7` (stills `runs/s_fx4..11`). Installed as Install 89.
  Open: the curtain reads a little dense; the other systems in the city (chimney smoke, dust motes, pollen) are not
  converted; the game's particle look (reflective splash normals) is approximated. Open from the review: water (rivers, lake,
  fountains), the near-mirror kerb/pavement materials (needs a game reference), kept cascades' casters flipping on a
  cell step, the door at `g_013`, clouds, grass past 60 m.

## Owner decisions, 2026-11-16: build it fully

- **Time of day: fixed** (owner): the current day, `sunny_bob_v7` at 14:00 (`env-j3`, `grade-1`).
- **Scope** (owner: "build it fully"): read as the whole city, phases 5 and 6. Order: finish the castle first (trees,
  decals, rock moss, flags, the valley ground, level checks, a playable kit), then the districts through the same
  recipe and one merged map with its checks and kit. The area stays the city (the handoff's terrain); the valley past
  it, for the horizon only, is render-only low-resolution terrain if the tiles are within the world's reach.
- **Leaf cards** (no owner answer; chosen to keep moving, revisit any time): fixed cards as imported, no renderer
  change.

## Open questions for the owner

- None blocking.

## Water fly-through (2026-11-17, Install 89)

Owner: "can you record fly through after" (the install). `.codex-research/beauclair-video-20261117/beauclair_water_v4.mp4`
(89 s, 1080p30; `_720.mp4` 24 MB): approach and castle aerials, then paths.py's water shots (`w_curtain`,
`w_cascade`, `w_river3`, `w_lions`, `w_fountain`, `w_swan`), run `runs/v15` (v16 re-shot the cascade, which began
behind a wall, and the fountain, which began in a tree), scene-14 + effects fx-7 on the x64 kit renderer at cccb3178d.

## Frame rate and size (2026-11-18, Install 89)

Owner: "what fps do we get in this map? And how large would it be to distribute?" `.codex-research/install89/fps89.py`
(results `fps89_results.json`, logs `<levelport root>/fps89/<size>/home/base/qconsole.log`): the installed client as
`Play_Beauclair.bat` runs it (x86 exe, kit-3 renderer, the bat's settings), RTX 3080, borderless window, uncapped
(`com_maxfps 1000`, `r_swapInterval 0`), nine standing views from the fly-through's shot starts, 595 frames each after
a settle (`r_cpuTimes` / `r_gpuTimes`). GPU-bound in every view (the CPU waits on the GPU for most of the frame).

| View | 4K fps (1% low) | 1080p fps (1% low) |
| --- | --- | --- |
| aerial approach (whole city) | 36 (34) | 58 (49) |
| aerial castle | 56 (51) | 95 (86) |
| roofs | 59 (49) | 104 (91) |
| upper street | 100 (88) | 236 (205) |
| lower street | 121 (107) | 296 (243) |
| castle yard | 115 (103) | 295 (260) |
| bridge curtain | 79 (72) | 228 (194) |
| courtyard fountain | 122 (109) | 316 (265) |
| swan lake | 77 (71) | 169 (151) |

Size: Install 89's six packs are 3.85 GB on disk (scene 3.43 GB, of it 3.21 GB stored BC7/BC1 textures; map 270 MB;
decals 139 MB; effects 7 MB), plus the 22 MB kit renderer. A 60-texture sample compresses to 65% with LZMA, so a
download would be about 2.6 GB. Converted levels stay local (owner rule); the figure answers the size question only.

**Night City's frame-rate work on this map (2026-11-18).** Beauclair's renderer (`renderer/w3-vertblend`) contains every
committed Watson performance change (`renderer/perf-2`, `renderer/vram-budget` 657bd66f, `baseline/1` 9b7943c8: the
count-then-scan cull, GPU cull groups, occlusion culling, LOD crossfade, event-driven texture streaming). The CPU
side is under 1 ms a frame; the GPU's main scene pass is the cost (aerial 21.6 of 27.6 ms, upper street 5.2 of 9.3
ms). Not used here: geometry and scene-image streaming, which need a tiled scene (`.xman`, Watson's chunked build);
Beauclair is one `.xscn`, all resident (a VRAM and load-time matter more than fps). A/B at 4K (`fps89.py --tag`,
four views, one run each, ±5% run to run):

| View | installed (LOD bias 2) | LOD bias 1 | occlusion off |
| --- | --- | --- | --- |
| aerial approach | 35 | 36 | 33 |
| roofs | 55 | 53 | 56 |
| upper street | 94 | 96 | 75 |
| swan lake | 72 | 80 | 74 |

Occlusion culling is working (+25% on the street); the LOD bias costs nothing measurable. Next lever, unmeasured:
what inside the scene pass costs (shading of the terrain and layer-blend materials, foliage overdraw, far triangles).

### Scene-pass profile (2026-11-18)

Owner: "ok profile it". `.codex-research/install89/prof89.py p1` (one 4K launch as the bat runs it, 15 variants set live
over 5 views, per-pass GPU ms plus `r_sceneInvocations` / `r_sceneOcclusionStats` counts; table `prof89_p1.out.txt`,
json `prof89_p1.json`) and `tri_census.py` (the cull shader's picks offline from `w3_city.xscn`: frustum, cull pixels,
hide and proxy distances, LOD by size; `tri_census_p1.txt`). Run-to-run noise: `base` against `base_again` up to 0.9 ms.

- **The far views are geometry-bound.** Aerial approach: the lit pass draws 21.3 M triangles and 31.6 M vertex
  shader runs for only 8.6 M fragments (about one a pixel: the lit pass tests EQUAL against the prepass), and the
  depth prepass and the cascades draw the same geometry again. Lighting off (`r_sceneDebug 1`) saves 0.4 ms there,
  1.4 on the roofs. The offline census agrees (24.1 M before occlusion; 17.4 M opaque, 3.8 M leaves, 1.8 M terrain, 1.1 M
  cutouts) over 59,931 drawn instances. The top meshes are props and building pieces of 0.3-9 k triangles placed
  hundreds of times each.
- **Why so many:** 167,896 of the 219,015 instances (3,518 of 4,811 meshes) have one LOD, so they draw their full
  mesh at any distance; a coarser LOD bias (0.5) changes nothing. 138,011 instances hide at 2,250 units (60 m), the
  rest never hide; W3's own proxies replace 17,515. The terrain has no LODs (256 near chunks at full resolution, the
  384-tile far ring at every eighth vertex).
- **Vertex reuse:** sampled parts have 0.98 unique vertices a triangle but 1.43 cache misses a triangle in their
  index order (FIFO 32), matching the measured 1.48 vertex runs a triangle: the source order, never optimised.
- **Ground views:** occlusion leaves 1.9 M triangles on the upper street (9.3 M before it). Smaller items there:
  sun shadows about 1 ms, haze 0.3-1.3 ms, `r_sceneHideScale 0.5` 1 ms.
- **Bridge curtain:** the particles cost 2.9 ms at 4K (`r_sceneFx 0`: 81 to 105 fps), the waterfall curtain's
  overdraw; `r_sceneDebug 1` saves 3.4 ms there (the particles' lighting).
- **Not costs:** layer detail, decals, water shading, foliage shading, cutout coverage: each within the noise.

Fixes, by expected gain (none measured yet): far LODs generated for the one-LOD meshes and a last coarse LOD for
the rest (converter; needs a mesh simplifier, e.g. meshoptimizer or fast-simplification, not installed); index
order optimised for the vertex cache (converter, no look change; about a quarter of the vertex runs); terrain chunk
LODs (converter, with skirts); a thinner curtain (fewer, larger particles); `r_sceneCullPixels 4` (aerial -3.2 ms;
small far props pop out, a look A/B).

### Vertex-cache order (2026-11-18, scene-15)

Owner: "Yes, both" (far LODs and the vertex order); the simplifier download declined, so the far LODs wait. Converter:
`tools/levelport/vcache.c` / `vcache.py` (Forsyth's linear-speed vertex cache order, built on first use with the
installed Visual Studio into `.codex-research/levelport/bin/`; without a compiler the order is left alone), called
from `w3scene.py`'s `geometry()` for every part but blended ones (`--no-vcache` for an A/B). On the city's meshes:
1.43 cache misses a triangle to 1.03 (0.98 unique vertices a triangle), 2.7 s for all 11.8 M triangles.

`w3_city_scene-15` (17 min; every count as scene-14, `vcache_parts 11106` of 11,337). A/B against the installed
scene-14, alternated, two pairs, 4K (`fps89.py --tag s14a/s15a/s14b/s15b --pk3`): vertex shader runs down 9% from the
air (31.6 to 28.8 M) and 30% on the street (3.7 to 2.6 M); GPU time down 1.4-2.8% in every view (aerial 27.5 to 26.7
ms, swan lake 12.0 to 11.7; the pairs agree within 0.1 ms), about 1-2 fps. Screenshots differ between the builds no
more than between two runs of one build. So vertex shading is not the limit: the triangle count is (21.3 M from the
air), which only far LODs reduce. Not installed.

### Frame-time allocation (2026-11-18)

From the scene-14 timing run (`<levelport root>/fps89/3840x2160-s14a`, 4K, standing views; `r_gpuTimes` per pass,
`r_cpuTimes`). CPU: game, client and renderer under 1.1 ms a frame in every view; the rest is the CPU waiting for
the GPU (aerial 26.9 of 27.9 ms). GPU, ms:

| Pass | aerial | roofs | upper street | bridge curtain | swan lake |
| --- | --- | --- | --- | --- | --- |
| scene (lit opaque and cutout) | 21.54 | 11.62 | 5.28 | 4.66 | 7.79 |
| scene depth (prepass) | 3.02 | 1.84 | 1.43 | 0.77 | 1.32 |
| scene blend (particles, glass, water) | 0.76 | 0.62 | 0.17 | 4.42 | 0.99 |
| sun shadows (kept cascades: redrawn on change) | 0.00 | 0.00 | 0.39 | 0.06 | 0.03 |
| everything else (cull, sort, clusters, haze, decals, SSAO, post, grade) | 2.12 | 1.97 | 1.95 | 2.11 | 1.93 |
| total | 27.44 | 16.05 | 9.22 | 12.02 | 12.06 |

Not allocated yet: inside the lit scene pass (by material class only as triangle estimates, `tri_census.py`); and
moving views, where the kept sun cascades redraw as the eye moves (standing views hide that cost).

### Moving, by material class (2026-11-18)

Owner: "Yes with the main scene drawing broken out". Renderer `renderer/w3-vertblend` 3c671b96f adds `r_sceneSkip`
(bench, cheat): material classes drawn empty in every view (1 terrain, 2 SpeedTree leaves, 4 other cutouts, 8 other
opaque, 16 the rest: blends, glass, water, emissives, decals). `.codex-research/install89/motion89.py m1` (64-bit
kit client, the x64 renderer at 3c671b96f, GameData's Install 89 packs, 4K, uncapped): the seven fly-through paths
flown by benchcam, their 30 fps frames interpolated 4x, timed throughout, once with everything and once without each
class (`motion89_m1.out.txt`, `motion89_m1.json`; logs `<levelport root>/motion89/m1`). A class's saving is what
it costs, less what hiding it reveals behind it (buildings hide terrain: on the ground, leaving them out costs time).

| Path | fps (1% low) | GPU ms | terrain | leaves | other opaque | blends etc | other cutouts | sun and light shadows |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| approach (aerial) | 36 (26) | 26.7 | 9.1 | 3.0 | 3.9 | 0.0 | 0.1 | 0.03 |
| roofs | 69 (53) | 13.8 | 4.5 | 1.4 | 0.3 | 0.0 | 0.0 | 0.03 |
| upper street | 99 (78) | 9.2 | 1.1 | 0.8 | (-1.4) | 0.0 | 0.0 | 0.36 |
| lower street | 106 (83) | 8.6 | 2.1 | 0.5 | (0.0) | 0.1 | 0.0 | 0.22 |
| castle yard | 101 (64) | 9.1 | 2.3 | 0.7 | (-1.6) | 0.1 | 0.0 | 0.25 |
| bridge curtain | 92 (77) | 10.3 | (-0.4) | 0.5 | (-1.9) | 1.4 | 0.0 | 0.06 |
| swan lake | 71 (60) | 12.9 | 4.5 | 1.0 | 0.3 | 0.9 | 0.0 | 0.04 |

- **The terrain is the largest single cost**: a third of the frame from the air and over the roofs and lake (4.5-9.1
  ms), 1-2 ms on the streets. It is its lit shading, not its triangles (its depth prepass share 0.1-0.4 ms): the
  terrain shader (`scene.frag`, material -3) blends four control taps, each with the overlay and background layers
  from up to three projections, colour and normal: 16 texture reads a pixel on level ground, up to 48 on slopes. This
  corrects the scene-pass profile's reading that props' triangles led: props cost 3.9 ms from the air, the terrain 9.1.
- **Leaves** 0.5-3 ms; **the curtain's blends** 1.4 ms (plus its particles, 2.9 ms in the standing profile);
  **sun shadows while moving** at most 0.36 ms (the kept cascades redraw little); other cutouts nothing.
- Moving matches standing within a few percent (upper street 99 fps moving, 94-101 standing).

Fix for the terrain, unmeasured: fewer reads for far fragments (one control tap past some distance, the side
projections only where the slope needs them), or a baked colour and normal per tile for far terrain (one read).

### The game's terrain shader; shared layers read once (2026-11-18)

Owner: "Can we check how witcher handles this", "Check the shader", "Yes do those". The next-gen game's DX12 shader
cache (`content/content0/shaderdx12_0.cache`, read only) holds 44,828 zlib-packed DXIL blobs and an index of materials
with five blob hashes a context; `bob_terrain_material.w2mg` has 20 contexts, disassembled with the Windows SDK's dxc
(`.codex-research/w3-shaders-20261118/`: `walk.py`, `dump_mat.py`, README with the details). Its main pixel shader
(context 0x2105, tessellated):

- the same four control taps, field layout, slope thresholds and 0.576 projection cutoff as ours; the overlay from
  above only (4 taps x colour and normal: 8 reads), the background in three projections, each behind `weight > 0`
  (8-24 reads): 16 reads on level ground, 32 at most (ours: up to 48, the overlay being triplanar too, 2026-11-17);
- every tap read even when taps share a layer; no distance-dependent branch; nothing baked for far ground;
- **its background UV scales are 0.4, 0.25, 0.15, 0.075, 0.05, 0.025, 0.00154**, not the Blender tools' 0.333,
  0.166, 0.05, 0.025, 0.0125, 0.0075, 0.00375 (Beauclair's ground uses index 0 63%, 1 13%, 2 10%, 6 8%).

A cheaper variant (contexts 0x42115/0x46115, no hull or domain shader, a pre-tessellated grid) decides the slope
blend before sampling and reads one layer in one projection: 8 reads. What the game draws with it (far terrain,
reflection probes, another pass) is not established from the shaders.

Renderer `renderer/w3-vertblend` 3303a2d07: a layer (the background's with its scale) that several taps share is
read once at their summed weight (Beauclair: 1.31 distinct overlays and 1.42 distinct backgrounds a cell); the
background scales are the game's. Shader built with `tools/scene_spirv.py`, x64 DLL with `build64.sh`; DLLs kept in
`.codex-research/w3-vertblend/{before,mergeonly,after}-terrain-20261118/`.

- **Look** (`stills64.py terr_before/terr_before2/terr_mergeonly/terr_after`, 2560x1440, six views; `terr_diff.py`):
  merge-only against before differs only where two runs of the before build differ (water, foliage): under 0.02% of
  pixels off by more than 8 outside that noise. The scales make the stones of paved ground and the rock of cliffs
  smaller (the game's table repeats the textures 1.2-3x more often): 4-15% of pixels change in ground views.
- **Time** (`motion89.py t2_<build>_<a|b> --variants 0`, 4K, seven paths, three builds alternated, two rounds
  agreeing within 0.2 ms):

| Path | GPU ms before | merge only | after | fps before -> after |
| --- | --- | --- | --- | --- |
| approach (aerial) | 26.7 | 23.7 | 23.7 | 36 -> 40 |
| roofs | 13.7 | 11.8 | 11.8 | 69 -> 80 |
| upper street | 9.1 | 7.7 | 7.8 | 100 -> 116 |
| lower street | 8.5 | 7.0 | 7.0 | 107 -> 128 |
| castle yard | 9.0 | 7.4 | 7.4 | 102 -> 122 |
| bridge curtain | 10.2 | 8.8 | 8.8 | 93 -> 106 |
| swan lake | 12.8 | 10.4 | 10.4 | 71 -> 86 |

GPU time down 11-19% on every path, all of it in the scene pass (aerial 20.9 to 17.8 ms); the scale change costs
nothing measurable. Not installed: the installed x86 renderer (kit-3) has neither change.

### Toward 80 fps from the air (2026-11-18)

Owner: aerial to 80 fps "with limited visual impact", 4K, no render scale; "check source for w3 lod". Start: scene-15 +
the terrain merge, aerial 40 fps (23.7 ms GPU). Measured on the 64-bit kit client (`motion89.py`, now with `--set` named
variants and `--packs`), 4K MSAA 4. **Method:** other programs share the GPU (NVIDIA Broadcast, LM Studio, a video
app), so a launch's level moves by up to 20% (the aerial base read 17.7 ms in one launch, 21.2 in another, the clocks
steady at 2040 MHz); compare only within a launch against interleaved bases, or alternate launches.

- **Where the aerial frame went:** at a quarter of the pixels (1080p, diagnostic only) the scene pass kept 11.7 of its
  17.6 ms: the frame was geometry-bound (22 M triangles, each drawn by the prepass and the lit pass).
- **The game's hide distances** (commit 26f9875ce, scene-16; `w3autohide.py`): each mesh's `autoHideDistance` (absent:
  20 m, the class default), a sector block's `forceAutoHide` or a component's `forceAutoHideDistance` overriding it, x
  1.5 (`MeshRenderingDistanceScale` in every preset of the game's graphics.xml), plus the instance's radius; proxies
  and trees untouched. Aerial 24.1 to 7.3 M triangles; 40 to 53 fps (23.7 to 17.7 ms), roofs 80 to 90, the streets
  unchanged. Seen: clutter (market tables, barrels, garden walls, far balustrades) leaves at distance; the buildings
  stay (their proxy shells).
- **Coarse shading, `r_sceneVrs`** (renderer 8d2e73ea9; off by default): far surfaces shade once per 2 x 2 pixels,
  edges and depth per pixel. 150 m for everything: aerial -2.8 ms of 20.6 (terrain alone -1.7), roofs -1.3, streets
  nothing. Look: a slight softening of far texture detail (1:1 crops), leaves a shade darker.
- **Tried and dropped:** a cheap far-terrain path (the game's own 8-read variant, one layer and one projection past
  150 m) saved 0.3 ms and hardened grass/rock edges; a merge without local-memory arrays (the shader kept 96
  registers) timed the same (kept: same picture). Terrain still costs ~9 ms from the air, about half in its shading
  (`r_sceneTerrainFlat`, bench: -4.5 ms) of which its texture reads are little; not resolved without a GPU profiler.
- **SpeedTree billboards** (commit 83d4ac2b8, scene-17; `w3trees.py --update`): the SRTs' vertical billboards (3-12
  views, cutout outline, `<species>_Billboards.dds` and normal atlases, 132 of 198 city trees) as a last LOD from
  the game's `m_fBillboardStartDistance`; crossed two-sided planes; no top-down billboard exists in the data. Aerial
  7.3 to 2.8 M triangles.

| Aerial, alternated launches | GPU ms | fps |
| --- | --- | --- |
| scene-16 | 17.5-18.2 | 52-54 |
| scene-16, r_sceneVrs 150 150 150 | 15.9-16.3 | 57-59 |
| scene-17 | 12.8-13.2 | 70-72 |
| scene-17, r_sceneVrs 150 150 150 | 10.6-10.8 | 84-86 |

Roofs: scene-17 + VRS 7.6 ms (from 10.5 for scene-16). Lower street unchanged (6.7-6.9 ms in all four).

**Billboards' look:** level views (the swan lake) hold; from the air the trees within billboard range turn into pale
green stars and cones (planes seen from above, no top view) and the crossed planes cast star-shaped shadows. Not
installed; scene-16/17 and the renderer are in the build folders only.

### Game-style billboards (2026-11-18, scene-18)

Owner: "do the game-style billboards with the top-down card". Contract and converter checks:
`.codex-research/w3-billboards-20261118/CONTRACT.md`.
- **What's drawn:** 100 trees (renderer 1362aaa0a, converter a60f05d47) get one card that turns to the viewer about the
  tree's up axis. The card shows the stored view nearest the viewing direction and crossfades to the next. As the view
  looks down, a horizontal top card baked in Blender from LOD 0 takes over, from 30 to 50 degrees of pitch. Wall
  plants and hedges (30) keep the fixed planes. xw3b version 3 carries the records; renderers that read only versions
  1-2 ignore the whole file.
- **Stills:** the stars and the star shadows are gone. Tree colour is off by 10-20% by species (oaks brighter and
  greyer, pines darker; level views within 5%).
- **Motion check** (frame-locked 4K fly-throughs, `vidcap_bc64.py` runs bbmotion / crossed / trees3d / cardcast;
  `flowpeaks.py`, the worst frame's share of pixels the motion does not explain):

  | Approach / roofs / castle, max % | p99 % |
  | --- | --- |
  | 3D trees (scene-16): 0.06 / 0.10 / 0.08 | 0.03 / 0.04 / 0.07 |
  | crossed planes (scene-17): 0.06 / 0.41 / 0.09 | 0.03 / 0.05 / 0.08 |
  | cards (scene-18): 0.38 / 0.52 / 0.44 | 0.11 / 0.11 / 0.28 |
  | cards + caster fix (renderer 4ec81ebc0): 0.09 / 0.16 / 0.12 | 0.05 / 0.07 / 0.09 |

  The cards' pops were their sun shadows. A card faces the eye in every pass, so as a caster it darkened its own 3D
  tree in a shape that followed the eye, and the shading changed in one frame where the casters' coarser LOD
  (r_sceneShadowLod 0.25) reached the card. Streaming off changed nothing; r_sceneShadowLod 1 removed it. The fix:
  a mesh ending in a card casts its coarsest 3D LOD while the eye's LOD is 3D. The remaining worst frames are LOD
  switches that the 3D trees show too.
- **Still open:** the small shrubs' cards read paler than their 3D models from the air (the colour gap above).
  Timing waits for a GPU with nothing else on it. The caster fix draws more shadow geometry between 0.25x and 1x of
  the billboard size. Not installed.

### Pop-in at the billboard switch: mips and matched cards (2026-11-18, scene-20)

Owner, on the matched-shadow clip: "thats some mad pop in"; then "Mips + match cards". Trees switching from card to
3D went from a pale card to a much darker tree within a few frames.
- **Builder mips** (`cp2gltf.mip_half_cutout`, every cutout of every level, W3 and CP2077): a cutout's mips now weight
  each 2x2 box's colour by its alpha. SpeedTree leaf atlases keep black under their clear texels (0.03-0.05), and the
  plain box mixed that black into the far mips, so distant 3D foliage read darker than its true colour. The cache key
  changed (`coverage-aw`), so every cutout image is re-encoded once on its next build. Seen from the air, the
  forests are markedly lighter green: the textures' own colour.
- **Cards matched to their 3D trees** (`.codex-research/w3-billboards-20261118/match/cardmatch.py`; renderer d405a544a):
  - Method: per carded tree, at its switch distance, the tree drawn alone (`r_sceneOnlyMesh`), as 3D and as a
    card, sun on and off, 2 headings x 2 pitches. Fog, clouds, grade, glow and auto exposure off; the exponential
    tone curve, inverted.
  - The card's ambient light is scaled by the 3D tree's ratio (median 0.55: the canopy darkens its inside) and its
    sun by the sun ratio (median 0.88).
  - A card now takes the sun wrapped round it from any side. As a sheet it took none with the sun behind the tree, and
    the sun ratios spread 20x between the headings.
  - 85 of 100 trees matched; the rest are under 40 pixels at their switch. The matches are written into the
    xw3b records' pads (`cardmatch.py patch --full`, or `w3scene.py --card-match match.json` on a rebuild).
- **Fly-throughs** (frame-locked 4K, `popin.py`: residual over 1/3 s windows, p90, approach / roofs / castle):

  | Scene | p90 % |
  | --- | --- |
  | 3D trees (scene-16) | 0.037 / 0.057 / 0.166 |
  | cards + caster fix (scene-18) | 0.099 / 0.120 / 0.193 |
  | mips + matched cards (scene-20) | 0.057 / 0.070 / 0.176 |

- **Builds:** scene-19 (mips), scene-20 (scene-19 with the matches); renderer x64 and x86 in
  `.codex-research/w3-vertblend/cardfix3-20261118/`. Installed as Install 91 (kit-4, `docs/renderer-vulkan-in-xenjk.md`).
- **4K timing** (2026-11-19, owner: "gpu free"; `install89/t16_run.sh`, `motion89_t16_*`): the 64-bit kit client with
  the installed renderer (cardfix3), 4K MSAA 4, four launches alternating scene-17 and scene-20, each with
  `r_sceneVrs` off, `150 150 150`, off again. GPU ms (fps), the two launches:

  | Path | scene-17 off | scene-17 VRS | scene-20 off | scene-20 VRS |
  | --- | --- | --- | --- | --- |
  | approach (aerial) | 13.25, 12.95 (70-71) | 10.63, 10.68 (85-86) | 12.62, 12.73 (73) | 10.67, 10.90 (84-85) |
  | roofs | 8.97, 8.57 (103-108) | 7.65, 7.84 (117-119) | 8.71, 8.56 (106-108) | 7.51, 7.68 (120-122) |
  | lower street | 7.06, 6.87 (127-130) | 6.64, 6.97 (128-134) | 6.84, 6.82 (131) | 6.71, 6.69 (133) |

  The cards, their caster fix and the matches cost nothing measurable against scene-17's planes (within the launches'
  spread; the repeated-off variants drift by up to 0.6 ms). From the air Install 91 runs at 73 fps as installed and
  84-85 fps with `r_sceneVrs 150 150 150`. The 1% lows swing 40-61 between launches on either scene and are not
  compared.
- **Coarse shading on** (2026-11-19, owner: "Yes, add it"): Install 91's `Play_Beauclair.bat` now sets
  `r_sceneVrs "150 150 150"` (`install91/vrs91.py`; record in `docs/renderer-vulkan-in-xenjk.md`, Install 91).
- **Freestyle kit** (2026-11-19, owner: CPM "we had it in night city ... with unlimited force"): GameData's game
  modules (2026-11-05) predate `g_moveStyle` (87e37a5f), so `<levelport root>/levelport/build/w3_city_freestyle-1/`
  (`install91/freestyle91.py`, not installed) runs Beauclair as the Night City kits do: cp_watson_baseline-11zd's
  64-bit client and modules, Install 91's renderer as its 64-bit build (cardfix3-20261118, d405a544a, a
  descendant of the castle kit's 53612024b), GameData's packs, its own home (a copy of GameData's openjk.cfg).
  `Play_Beauclair_Freestyle.bat` (the city bat's settings) and `Play_Beauclair_Castle_Freestyle.bat` set
  `g_moveStyle 2`. This client, renderer and packs had already loaded the city for the motion89 timing; neither bat was
  launched before handover (the owner was in game). The castle has not been seen on d405a544a.

### Cleanup after Install 91 (2026-11-19)

Owner: "now that castle is done please clean up is working files". Moved, not deleted, to `<levelport root>/.codex-trash/`
(each stamp has a MANIFEST.tsv; restore with `tools/research_shed.py --research <levelport root> --trash
<levelport root>/.codex-trash --restore <stamp>`, the retain stamp with `--research <levelport root>/levelport`):
- `20261119T024921-levelport-retain` (`tools/levelport/retain.py --apply`): the superseded W3 builds' packs (city
  scenes 2-19, castle scenes 7-13, maps city-1/2 and castle-5/6, fx 1-6).
- `20261119T025229-beauclair-runs` (`.codex-research/beauclair-cleanup-20261119/shed_runs.py`): the run homes of the
  fly-throughs, timing, profiling and card matching (`<levelport root>/{beauclair-video-20261117,motion89,cardmatch,fps89,prof89}`):
  their pack links, exe/dll copies and pipeline caches, and the videos and stills that have byte-identical copies in
  the D: research folders.
- `20261119T025909-beauclair-w3-looks` (`shed_w3.py`): city kits 1-3 (superseded by kit-4; no bat names them), the
  pack links and binaries of the finished look runs and audit in `levelport/w3/beauclair`, the autohide scene
  extracts, the mip test pack.

Emptying the three returns 84.5 GB on C:; 15.1 GB of the moved names are links to packs that kept builds hold. Kept:
the installed kits (city kit-4, castle kit-1), each area's newest build (city scene-20, map city-3, decals-1, fx-7;
castle-7, castle scene-14, castle decals-1), the work folders (`work-city`, `work-3`, env and grade folders: rebuild
and patch inputs), every log, screenshot, result and the remaining videos.

On D: (owner: "Yes, move them"): the draft fly-through renders, the .mp4s of `beauclair-video-20261117/runs/v1`-`v15`
and `runs/all` and the first final cut (`beauclair_flythrough.mp4`, `_720`), 73 videos, 2.66 GB, went to
`.codex-trash/20261119T031921-beauclair-drafts` (`beauclair-cleanup-20261119/shed_drafts.py`; restore with
`tools/research_shed.py --restore`). Their logs and settings stay; the v2-v4 finals, v16, the 4K pop-in clips and the
stills are kept.
