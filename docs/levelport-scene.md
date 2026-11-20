# Render scenes: dense levels beside the BSP (2026-10-27)

The owner asked what it would take to show a level of Cyberpunk 2077 density (Kabuki market) at high quality and
speed, then chose this design: "go for the new way, not trying to shove this into the old". This file is the
design and its state. The converter is `tools/levelport/cp2gltf.py`; the runtime is `codemp/rd-vulkan/vk_scene.cpp`.

Converted third-party levels stay local, like every levelport output: they are never committed or published. Kabuki's state, next steps and references: [levelport-kabuki.md](levelport-kabuki.md).

## Why the BSP alone fails here (measured on Kabuki, 2026-10-27)

- **Instancing.** The area within 135 m holds 61,710 placements of 2,718 unique meshes. Those meshes carry
  4.8 M triangles across all of their LODs (3.2 M at LOD 0). A BSP flattens every placement into world surfaces:
  keeping objects of 5 m and over already made 1.1 M triangles, a 176 MB BSP and a 338 MB pk3.
- **LOD.** One LOD per placement is fixed at build time, chosen by object size rather than distance.
- **Culling.** vis split the open city into 864 clusters, and every one sees every other.
- **Lighting.** Vertex light for 1.1 M triangles and 1,158 point lights took 8 minutes. Emissive surfaces as q3map2
  area lights (50k) never finished. The source game lights these streets live.
- **Materials.** Multilayered materials (up to about 20 tiled layers through masks) are flattened to one 512 px
  bake each (the render scene now tiles the main layer at draw time: "Detail" below).

The first in-game look (`cp_kabuki-2`, 1.1 M triangles) ran at the 120 fps cap. It was recognisable, but holed
where 3 m ground tiles fell under the size cut, flat, and soft.

## Design: two layers per map

1. **Gameplay layer: the BSP.** Collision (boxes for props, clustered meshes for structure), entities, spawns, the
   playable boundary and a light grid for characters. It is what the server, Bot2 and every other renderer load.
   It carries no render geometry, so its compile is minutes: no lightmaps, no vis work on dense meshes.
2. **Render layer: a scene file** `maps/<map>.xscn` in the pk3, read only by rd-vulkan. If it is present, the
   renderer draws it in the world view. Other renderers ignore it and show the BSP alone (sky and entities; a coarse
   fallback render layer can be added later if other clients matter).

The scene keeps the source's instancing and its full LOD chains. That removes Blender decimation and the q3map2
light stage from the pipeline.

## Scene file, versions 1 to 6

Little endian. Version 2 adds the light boxes section. Its header is still 80 bytes: 9 counts and 9 offsets, where
version 1 has 8 of each. Version 3 keeps version 2's layout and fills the light record's last field with the light's
share in the haze; rd-vulkan reads all three, giving every light a full share before version 3. The sections start on 16-byte boundaries, and every offset is from the start of the file. Space is
Quake units, Z up: a CP point maps to `(p - origin) * 37.5`, the same transform the BSP's glTF went through.

Version 4 (2026-11-01) keeps the layout and fills pad words with CP2077's hide distances: the instance record's last
word is its far hide (forceAutoHideDistance, f32, game units, 0 = none), and the light box record's last 16 B are the
light's hide distance, shadow fade distance and fade range (f32, game units) and flags (u32, bit 0
allowDistantLight). Version 5 makes the instance record's second word, flags (always 0 before), its near distance
(f32, game units, 0 = none): the instance is drawn only between near and far, measured from the eye to its bounding
sphere's centre, both scaled by r_sceneHideScale. CP2077's proxies (the far city) are the instances with near > 0.
The slot word stays the traffic-signal slot: no proxy bit.

Version 6 (update 2026-11-19; `SCENE_VERSION` in `vk_scene.cpp`, writer `scene_shape_section` in
`tools/levelport/cp2gltf.py`) adds a 10th section, the lights' shapes, and the header grows to 88 bytes (10 counts and
10 offsets). One 16 B record per light (the count is the light count or 0): the tube's half axis (f32x3, scene units; the
tube runs origin - axis to origin + axis) and the source's radius (f32); all zero is a point light. The light record's
radius is then its reach from the centre plus the axis length. Versions 1 to 5 still load, without shapes.

| Section | Record | Fields |
|---|---|---|
| header | 80 B (88 B from version 6) | `XSCN`, version; counts and offsets of vertices, indices, meshes, LODs, parts, materials, instances, lights, light boxes (v2), light shapes (v6) |
| vertices | 28 B | position f32x3 (mesh local); normal s8x4; tangent s8x4 (w = bitangent sign); uv f32x2 |
| indices | 4 B | u32, relative to the part's vertex offset |
| meshes | 32 B | bounding sphere centre f32x3 and radius (mesh local); first LOD, LOD count |
| LODs | 16 B | first part, part count; `screen`: the LOD draws while the sphere's projected diameter is at least this fraction of the view height |
| parts | 16 B | first index, index count, vertex offset, material |
| materials | 240 B | albedo, normal, ORM image paths (64 B each, empty = none); kind (0 opaque, 1 cutout, 2 blend, 3 emissive); flags (1 two-sided, 2 a window, 4 a masked glow: a lit material glowing in its emissive colour where its albedo's alpha says; further flags the reader in `vk_scene.cpp` uses, update 2026-11-19: 8 a traffic signal's lamp, an emissive kind lit by its signal's state, bits 0x10 and 0x20 picking the lamp; 0x40 a mesh decal, a blend kind projected into the decal buffer within its depth threshold (`r_sceneDecals`); 0x80 water, a blend kind drawn as water over the lit bed (`r_sceneWater`, records in `maps/<map>.xwat`); 0x100 night-only emissive (lamp bulbs, lit rooms), scaled by `r_sceneDayLamps` in a day scene; 0x200 a screen (ads, UI, televisions), its emissive scaled by `r_sceneScreens`, drawn with `r_sceneScreenLook`; 0x400 an ad's glow, an opaque or cutout material glowing in its albedo's colours where its normal image's alpha says; 0x800 a SpeedTree leaf, shaded as a canopy with `r_sceneFoliage`; 0x1000 terrain, an opaque material pushed back along the eye's ray with `r_sceneTerrainPush`; 0x2000 a stand-in the scene does not draw, such as CP2077's distant water patches); kinds 4 to 6 (decal, water, none) are made by the loader from flags 0x40, 0x80 and 0x2000, they are not written; emissive rgb + intensity; tint rgba (a window: room width and height in metres, dark share, colour spread); a window's depth in metres and its atlas's rooms (zero otherwise; older files have zero padding here) |
| instances | 64 B | 3x4 row-major mesh-to-world matrix (scale included); mesh; near distance (f32, version 5; the flags word before); traffic-signal slot; far hide distance (f32, version 4) |
| lights | 64 B | origin, radius; colour (linear), intensity; spot direction, cos outer; cos inner, type (bit 0 a spot, bit 1 lights only the haze, bit 2 casts shadows), has a box, share in the haze (v3: CP's `scaleVolFog` / 100) |
| light boxes (v2) | 64 B | per light, 3x4 rows from scene space to its interior's unit cube (all zero when it has none) |

A map may also carry a colour grade, `maps/<map>.xlut` beside the scene: a 16-byte header (`XLUT`, version 1, size
n, input mapping 1 = ARRI LogC v3 at EI 800), then n³ rgba16f texels (red fastest, then green, then blue) of display
light, the source's tone curve included. The converter writes the game's own (`--grade`, `--regrade`).

And the haze's local volumes, `maps/<map>.xfog`: a 16-byte header (`XFOG`, version 1, count, pad), then 64 B per
volume: the 3x4 rows from scene space to the volume's [-1, 1] box, then its density per metre, mode (0 a multiple of
the haze's density there, 1 absolute), soft edge share and kind (0 a box, 1 a steam column: densest at the vent and
in the middle, widening, thinning and breaking up as it rises). The converter writes the source's fog volumes and
steam effects (`scene_fogs`).

And the multilayered materials' tiled detail, `maps/<map>.xdet`: a 16-byte header (`XDET`, version 1, count = the
scene's materials, pad), then 80 B per material in the scene's order: its detail image (64 B path, empty for none),
then tiles per UV unit, normal strength, roughness range and colour strength (f32 each). The converter writes it with
`--detail-size` (512; 0 bakes every layer as before).

The images sit in the same pk3 under `scn/<map>/`, each distinct image once, as BC1 (colour, ORM) or BC7 (alpha,
normals, detail) DDS.

### Traffic signals, `maps/<map>.signals`

Text, written by `cp2gltf.py` (`SceneSignals`). The renderer lights the signal lamps from it (`vk_scene_cp77.cpp`
`Scene_LoadSignals`); the cgame plays the crossings' sounds from it (`cg_signals.c`). Both use the same clock.

- **Header.** `xsig 1 <count>`.
- **Sound lines** (optional, 2026-11-19): `sound <role> <path>`, one per role, after the header. The roles are
  `walk_base`, `walk_layer`, `dontwalk_base` and `dontwalk_layer`. Walk plays its base every 3.0 s and its layer
  every 3.5 s from 1.5 s in, 10 times. Don't walk plays its base every 3.5 s and its layer the same way as walk's.
  `<path>` is a game path with no spaces, such as `sound/<map>/signals/walk_base.wav`. A role without a line is
  silent; a file without sound lines plays nothing. The converter writes them with
  `--signal-sounds <folder>` (`<folder>/<role>.wav`). The renderer skips these lines, so files with and without them
  light the same, and older renderers skip them too (a line that is not a slot is ignored).
- **Slot lines.** One per slot: `<slot> <crossing|car> <cycle s> <offset s> <x y z> <n>`, then `<G|Y|R> <seconds>`
  n times. The position is in game units. Only `crossing` slots make sound.

## Tiled scenes, version 1 (2026-10-31; layout agreed with the renderer session)

For an area too large for one `.xscn` (Watson: 1.6 M instances; the renderer reads a scene whole with an `int`
length, and one pk3 past 4 GB needs zip64), the scene is split by the city's tile grid (docs/levelport-watson.md
section 14) into files that are each an ordinary `.xscn` of version 3 with only some sections filled (the others
count 0). The renderer loads them all into the same buffers, adding base offsets; no residency yet.

- `maps/<map>.xman`, text, one record a line:
  - `xman 1`
  - `tile_size <u>` and `grid_origin <x> <y>`: the city grid in scene units (a tile (i, j) covers
    `grid_origin + (i, j) * tile_size` to the next corner)
  - `lib <path> <meshes> <lods> <parts> <vertices> <indices>`, in order: the geometry shards
  - `mat <path> <materials>`: the one material table (and `maps/<map>.xdet` follows its order, as now)
  - `tile <id> <path> <instances> <lights> <boxes> <x0> <y0> <z0> <x1> <y1> <z1>`: each tile's file, its counts and
    the scene-space box of what it draws (its instances' spheres; with height, for rejecting a tile against the view:
    megabuildings, elevated roads). `<id>` is `<i>_<j>` on the grid, or a name for what has no square (`water`: the
    area's water surfaces)
- A geometry shard `maps/<map>/g_<id>.xscn`: vertices, indices, meshes, LODs, parts. A shard holds the meshes
  that tile is the first to place, tiles taken in the manifest's order, so a tile's shards are its own and
  earlier tiles' (the way to stream later). Every index is global, into the shards' arrays laid end to end in
  manifest order: a mesh's first LOD, a LOD's first part, a part's first index and vertex offset (identical geometry
  is stored once for the whole scene, so a later shard's part may point into an earlier shard's vertices), and a
  part's material, into the one material table. The shards end to end are byte for byte the sections a single
  `.xscn` of the same tiles would hold.
- The material table `maps/<map>/materials.xscn`: the materials section only.
- A tile `maps/<map>/t_<id>.xscn`: instances, lights, light boxes. An instance's mesh index is global: shards'
  meshes numbered in manifest order. A light belongs to the tile its origin is in.
- Unchanged and once per map: `.xfog`, `.xlut`, `.signals` (instance and light slots index it), `.xdet`.
- Images under `scn/<map>/` as now, split over `zz_<map>_scene_img<k>.pk3` packs of at most about 1.5 GB each; the
  scene files in `zz_<map>_scene.pk3`.

Every file stays under about 1 GB (the renderer reads one whole into an `int` length; two near 2 GB would be held at
once). After the bases are added a part's vertex offset is an `int32` and its first index a `uint32`: the whole scene
under 2^31 vertices and 2^32 indices (Watson about 28 M triangles). Renderer limits outside the format (its review,
2026-10-31): light clusters off above 65,535 lights (Watson about 40 k), the static light grid 64 cells an axis (update 2026-11-19: `SCENE_GRID_MAX` is 128 now) and 48
lights a cell, 256 signals; per-view visible buffers to be sized by one LOD's parts (its loader change).

Loading: append every shard's vertices, indices, meshes, LODs and parts in manifest order (no bases to add), then
the material table, then every tile's instances and lights. An area small enough
for one `.xscn` keeps writing one.


- **Load.** `RE_LoadWorldMap` loads `maps/<map>.xscn` if it is present: one device-local vertex and index buffer,
  storage buffers for instances, meshes, LODs and parts, and one draw record per (mesh, LOD, part).
- **Views.** The front end registers each world view that wants the scene (`viewParms.sceneView`). At frame
  start, before any render pass, a compute pass culls every registered view's instances. It tests the bounding
  sphere against the frustum, picks the LOD by projected size, and appends the instance to each of that LOD's
  parts' draw records: an indexed indirect command with an atomic instance count.
- **Draw.** The draw records are sorted by material. A view draws them with one `vkCmdDrawIndexedIndirect` per
  material, with that material's textures bound. Opaque and cutout go before the BSP's surfaces (in the depth
  prepass too); blend after them.
- **GPU-driven draws** (`r_sceneGpuDriven`, 1; a Vulkan 1.2 device). Every scene image sits in one descriptor array
  and every material in a buffer. After the cull, `scene_compact.comp` packs each view's commands that have
  instances into five lists (lit, depth, depth cutout, glow, blend), keeping material order, and each list is one
  `vkCmdDrawIndexedIndirectCount`. The lit list holds the opaque, cutout and emissive kinds in one pipeline, in
  material order (exactly coplanar duplicates, a window's glass on its facade, resolve as before). It tests depth
  before the shader without writing it: the prepass holds the nearest depth, so LESS_OR_EQUAL passes exactly where
  EQUAL would and keeps the coarse depth cull; a cutout still discards its clear texels. The prepass and the lit pass
  run the same vertex module without `invariant` (with it, those coplanar duplicates flipped winners). Material
  constants are read where they are used (copied into variables they held 13 registers through the light loop), and
  the lit shader has no debug views (`r_sceneDebug` selects a second pipeline with them): 64 registers. The depth
  lists (prepass, cascades) walk the commands biggest mesh first. Needs the depth prepass (`r_depthPrepass`).
- **Lighting.** The source's lights are binned once at load into a world-space grid of cells (static lights). The
  eye view instead sorts them into clusters each frame (`r_sceneClusters`, default 2, since the update of 2026-11-19: 1 uses them except where a cell kept only its strongest lights, 2 uses them there too; `scene_cluster.comp`): 40 x 24 tiles of
  its frustum by 32 depth slices from 1 m to `r_sceneFogFar`, with sphere, cone and interior-box tests. The scene's
  spot directions are not unit length, so the cone tested is the one scene.frag actually draws, cos = cosOuter /
  |direction|. A cluster holds at most 126 lights; where it is full, or where the grid cell kept only its 48
  strongest, the fragment uses the cell (so the result is the cell's exactly). The haze's volume uses a second set of
  clusters holding the lights it scatters. Each
  fragment loops its cluster's (or cell's) lights: physically based diffuse and specular with inverse-square falloff windowed to
  the light radius. A hemisphere ambient term and emissive are added. Probes and GI come later.
- **Sun (or moon).** The map's `q3gl2_sun` drives rd-vulkan's three shadow cascades. The cascade views register as
  scene views too. They cull with the main view's LOD choice (coarser by `r_sceneShadowLod`) and drop casters under
  a few texels. The casters draw two-sided into the cascade layers before the main render pass, beside the BSP's and
  the characters' casters. scene.frag then reads the layers back with a normal offset and 2x2 bilinear comparisons.
  Characters are shadowed by the scene, and the scene by characters. The two near cascades leave out casters outside
  one of the eye's sides whose shadows travel away from it, with a margin of six cascade texels (the world shaders' filter reaches about 4.4)
  (`r_sceneShadowReceivers`, 1).
- **The far cascade, cached** (`r_sunShadowFarCache`, 1). The third cascade surrounds the eye on a grid (a sphere
  reaching the view's farthest corner in any direction, centred on a cell a sixteenth of that, Kabuki about 12 m),
  so it stays put while the view turns and moves within a cell; its texels are about 30% larger than the fitted
  sphere's. Its static casters (the world's surfaces and the scene) are drawn only when it moves (or the sun, the map
  or the scene's shadow settings change) and kept in an image of their own; every other frame the layer is copied
  back from it and only the moving models (entities) are drawn over it (`RB_RenderFarCascade`). Kabuki lap, 1080p:
  the shadow pass 2.64 to 1.36 ms, the frame 7.37 to 6.12 ms (122 to 139 fps; 4K 75 to 80.5).
- **Views.** Each draw pushes its own view's eye (`scenePush_t.eye`): a second world view (a portal, a mirror)
  shades from its own origin. Its w marks the view the haze's light volume and the reflections were made for; other
  views read neither.
- **Haze.** Exponential in distance, thinning with height, mixed toward the sky's horizon colour in linear light
  (the colour decoded through the tone curve), before the curve. A surface dims by the haze over the whole path; the
  haze colour fills in only for the path past `r_sceneFogStart` (the source's distant fog starts at 150 m). Light sources (emissive materials) take 60% of it, so windows, screens and neon cut through. Lit materials can
  carry their own glow: translucent holographic screens in their own colours, and masked glows (lanterns, lightbox
  faces; material flag 4) in their emissive colour where the albedo's alpha says.
- **Light in the haze.** After the cull, `scene_fog.comp` lights a volume over the eye view's frustum: 240x135
  froxels across the view, 64 slices spaced exponentially from 1 m to `r_sceneFogFar`. Each froxel takes its
  light-grid cell's lights times their share in the haze (CP's `scaleVolFog`), through a Henyey-Greenstein phase
  toward the eye. A light inside a froxel is averaged over it rather than sampled at its peak, so fixtures don't show
  as blocks. The map's sun (Kabuki's moon) scatters too, through its shadow cascades (`r_sceneFogMoon`, 1): the volume
  is lit after the cascades are drawn (`vk_scene_haze`, from `vk_begin_frame`), so the moon's light in the haze falls
  in shafts and stops under decks and buildings. A second pass integrates each column toward the eye. scene.frag adds the result at each surface's
  depth, with the haze, in linear light, and tone maps once. A full-view triangle at the
  far plane adds the volume's far end over the sky (`scene_sky.vert`, mode 6), at the start of the blend pass. CP's
  fog-only lights (no diffuse or specular) light only the volume. The scattered light takes the haze's albedo
  (`r_sceneFogAlbedo`, Kabuki: the pollution weather's warm (1, 0.925, 0.794)). About 0.15 ms per frame at 1080p.
- **Light shadows.** Lights flagged as casting shadows (type bit 2: CP's `enableLocalShadows`) get static shadow
  maps, rendered once at load (`Scene_RenderLightShadows`, about 1 s for Kabuki):
  - **Faces.** A spot up to 120 degrees gets one face (its cone plus 4 degrees); every other light six cube faces
    (+x, -x, +y, -y, +z, -z, each a little over 90 degrees). The near plane is a quarter metre out, so a lamp's own
    fixture casts nothing; the far plane is the light's reach.
  - **The atlas.** Each face is a square tile sized by the light's reach (`r_sceneLightShadowRes` texels across for
    15 m, powers of two from 32 to twice that), packed largest first in Z order into the layers of one depth atlas
    (a 2D array of 2048² layers, D32F where the device filters it, else D16).
  - **Drawing.** Each face is culled by `scene_cull.comp` like a view (its four sides and the light's reach; LOD by
    `r_sceneShadowLod`; casters under a texel and a half dropped) and drawn depth only, up to eight faces a
    submission, with the opaque and cutout materials. Emissive materials (a lamp's glowing lens would shadow its own
    light) and glass cast nothing.
  - **Reading.** `scene.frag` and `scene_fog.comp` pick the face (a cube's by the direction from the light), offset
    the point off the surface and toward the light by about a texel, and take one bilinear comparison. In the haze
    this makes the light fall in shafts. Kabuki: 308 lights, 1,533 faces in 9 layers (144 MB); about 0.1 ms per
    frame at 1080p (four comparisons: about 0.4 ms, no visible difference at play distances).
- **Haze volumes.** With a `.xfog`, each froxel adds the local volumes' density (`r_sceneFogVolumes` scales it): the
  source's fog volumes and steam columns. They scatter the lights like the haze does, and the haze's own colour as
  light (scene.frag already mixes that in for the haze itself), so steam shows away from lamps too. Surfaces behind
  them are not dimmed: the volume only adds light, as before.
  - **Candidates.** Each 8x8-froxel tile of one slice first marks, one bit per volume, the volumes whose box reaches
    the box around its four corner centres (grown by far more than their rounding); its froxels test only those, in
    volume order, so the sum is unchanged (up to 256 volumes; more, and every froxel tests them all). Kabuki, 52
    volumes, 4K: the haze pass 0.39 -> 0.14 ms, pixel-identical on fixed demo frames.
- **Windows.** `window_parallax_interior` materials draw rooms behind the glass ("fake interiors"):
  - **Rooms.** The facade is cut into rooms of the source's size in the mesh's own space (z up). In world space the
    grid cut across window frames; the mesh's UVs repeat per window.
  - **Per room.** A hash of the cell, the facade's direction and the instance picks one of the RoomAtlas's
    pre-rendered rooms, darkens the room at the source's `AmountTurnOffAtNight`, and gives lit ones a brightness
    (0.6–1.4) and a colour toward warm by `LightsTempVariationAtNight`.
  - **Depth.** The view ray is traced into the room's box and its hit mapped onto the picture, a one-point
    perspective whose back wall fills 45% of it.
  - **Brightness.** Lit rooms glow in `TintColorAtNight` at 2^(EmissiveEV − 5) times `r_sceneWindows`; dark rooms
    show 2% of their picture.
  - **Cost.** Not measurable.
- **Wet ground.** Up-facing surfaces (geometric normal over 0.55 up) turn wet (`r_sceneWet`): albedo times 0.45
  and roughness down to 0.2 at most (a water film; 0.12 everywhere read as glossy plastic). Puddles, from gradient noise over the world plane (`r_sceneWetPuddles`, the share
  covered), darken further and become near mirrors with the normal map flattened. Every light's highlight widens
  with its size (`r_sceneLightSize`, Karis's sphere-light lobe), so highlights on glossy surfaces read as lamps, not
  pinpoints.
- **Rain mask.** At map load one more view goes into the light shadow atlas, a layer of its own: the scene from
  above, orthographic over the area its lights cover plus 20 m (Kabuki: 306 m across, 15 cm a texel), every mesh at
  its finest LOD, light sources included (a lit canopy covers too), glass not. A surface is wet only where that view
  sees nothing over it within 16 units (`rainReach`, a 3x3 tent of comparisons), so floors under roofs, canopies,
  decks and indoors stay dry (`r_sceneRain`, 1; 0: every up-facing surface). About 16 MB and 0.2 s at load.
- **Reflections.** Screen-space, traced at half resolution and settled over frames:
  - **Capture.** After the main view's MSAA resolve (`vk_scene_capture`, called from `vk_one_resolve_pass`),
    `scene_capture.comp` packs the resolved world (no HUD) and the depth extract into a half-size rgba16f history:
    display colour plus linear view depth.
  - **Trace.** `scene_ssr.comp` then works on each history texel:
    - It rebuilds the surface from depth: the position, and the normal from the neighbour nearer in depth on each
      axis. Surfaces facing up are wet.
    - It marches the reflected ray through the same history: 32 steps growing to `r_sceneReflectFar`. A step behind
      the history's depth by less than a thickness is a hit, refined by bisection; it marches on past thicker
      occluders.
    - The ray's start and a glossy spread (roughness 0.2, scene.frag's wet cap; puddles 0.05) change every
      frame.
  - **Under decks.** A reflected ray rises, so it cannot meet the top of a deck: a hit on a surface facing up (its
    normal from the history's depths) means the ray passed under it, and it mirrors the dark underside instead.
  - **Settle.** The result blends into last frame's at 15%, reprojected with that frame's camera, kept only where
    the same surface was (depth within 8%), each side weighted down by its brightness (Karis 2014), so a bright thin
    thing moving through the mirror (a lit saber) no longer sparkles in half-size blocks. A surface with no history
    yet takes two rays. Two images alternate. Each texel stores the mirrored light (linear, before Fresnel) and its
    view depth.
  - **Shading.** The next frame's wet fragments reproject into it, read a tent 1.5 texels wide over the texels
    showing the same surface, and apply their split-sum Fresnel (Karis's analytic fit) times `r_sceneReflect`.
  - **Misses.** A miss mirrors the haze colour: half when the ray left through the top, a quarter otherwise.
  - **Glossy hits.** After the capture the history's colour gets four more levels (blitted, each a 2x2 mean). A hit
    reads the level as wide as its glossy cone where it lands (the cone's angle, 4 x roughness squared, times the
    ray's length on screen in history texels), so a thin bright thing such as a saber blade mirrors as a streak, not
    as beads where single steps landed on it; puddles (roughness 0.05) stay sharp.
  - **Async** (`r_sceneAsync`, 1; a device with a compute-only queue family). The trace runs on that queue beside
    the frame's post passes. The frame's commands are split after the capture: the first part is submitted and
    signals a semaphore the trace waits for; the trace signals another that the next frame's first submit waits for
    at the fragment stage, where the wet ground reads the reflections. The images and the params buffer are shared
    by both queue families. A trace that no frame waited for (a frame without the scene in between) is taken by the
    next `vk_end_frame`.
  - **Requirements.** The depth extract (SSAO) and an offscreen frame. With MSAA the capture follows the resolve
    (`vk_one_resolve_pass`); without it, the end of the single-sample pass (`vk_postfx_pass`).
  - **Cost.** About 0.25 ms per frame at 1080p, two runs each, back to back (the single-frame per-fragment trace it
    replaced was 0.4–0.8 ms and noisy).
- **Detail.** With a `.xdet`, a multilayered material's main layer is drawn at the source's own tiling over its
  bake. The converter picks the layer whose tiling shows most (its visible share, under none of the layers above it,
  times how much its texture varies: fine paint barely changes a pixel, asphalt does), bakes that layer's mean
  colour and roughness and a flat normal, and puts where the layer shows in the ORM's red (no occlusion is baked for
  these). Its detail image holds the template's tiling texture as zero-mean detail: rg the normal's slope, b the
  brightness over its mean / 2, a the roughness over its mean + 0.5. scene.frag (set 5, binding 13's parameters)
  scales the albedo and roughness and bends the normal by it, times the layer's share (`r_sceneDetail`, 1; 0 off).
- **Glow.** Emissive materials draw again in the dynamic glow's extract pass (`r_DynamicGlow`), against the main
  pass's depth. Only light past `r_sceneGlowThreshold` glows, so neon blooms but dim lit windows don't haze whole
  facades. The glow is compressed on its brightest channel, so a halo keeps its light's hue.
- **Colour grade.** With a `.xlut`, `vk_scene_grade` (from `vk_one_resolve_pass`, after the post 1x pass's SSAO, glow
  and smoke blends, before bloom and the HUD) ends the pass, grades the resolved frame in place with a compute pass
  (`scene_grade.frag` compiled with `GRADE_COMPUTE`; `r_sceneGradeCompute`, 1, read at video start: the colour image
  is then a storage image too, on the float target with storage support) and begins the pass again. Otherwise it
  copies the frame aside and grades it with a full-frame draw (`scene_grade.vert`/`.frag`). Each pixel goes back through scene.frag's tone curve to the light
  that curve was given, times `r_sceneGradeExposure`, then through LogC and the LUT, and out sRGB-encoded (the LUT's
  black stays black). Everything in the main view is graded: the BSP's sky, characters and effects too, as in the
  source game. Debug views are not. It needs MSAA (the post 1x pass). About 0.02 ms at 1080p.
- **The frame's encoding.** scene.frag writes the tone curve with gamma 2.2 up to 0.999 and past it an e-fold of
  light per 0.1 (`toDisplay`; `fromDisplay` and the grade's `curveInput` invert it). An 8-bit target clips that
  headroom where the curve alone would; a float target (`r_hdr 2`, about 0.1 ms) keeps it, so neon cores reach the
  grade unclipped. Models and effects draw in 0..1 as before. A scene with a grade asks for the float target at load
  (`r_sceneHdr`, `vk_set_scene_range`): with `r_hdr 0` the swapchain and attachments are rebuilt before the scene's
  pipelines (pending uploads flushed first), and a map without a grade switches back.

## Milestones

1. **Instanced scene, live lights** (done). Converter `scene` stage and `build --no-render`; rd-vulkan load,
   GPU cull and LOD, indirect draws, grid-binned lights, baked materials as now.
2. **Sun cascades, glow, decal bias** (done). Hi-Z occlusion was measured, not built (see State). Static shadow
   maps for the source's shadow-casting lights followed (done).
3. Runtime layered materials (CP multilayer in the shader), window interiors, glass.
4. Light probes or GI, fog (light in the haze, local volumes and steam done), wet-street reflections (screen-space
   done; a rain mask open), temporal AA, a linear-light colour grade.
5. CP's own collision (PhysX caches), texture streaming, neighbouring areas.

## State (2026-10-27)

Milestone 1 runs: the dev rd-vulkan (`.codex-research/renderer-bench/builds/xenjk-vk-dev`) draws all of Kabuki:
61,702 instances of 3,264 meshes, 4.8 M triangles across their LODs, 3,773 materials and 1,528 lights (scene-18:
61,771 instances of 3,474 meshes, 5.0 M triangles, 3,919 materials and 2,023 lights; loading 2.7 s with the light
shadows' 0.9 s). Loading took 1.6 s. The GPU frame is 2.6-3.3 ms at 1600x900, of which the scene is 2.0-3.0 ms (depth prepass plus shading); this
was a hidden look run, not a 4K timing. Builds and measurements are in the Kabuki notes
(`.codex-research/levelport/cp77/kabuki/NOTES.md`).

Facts that cost time:

- **Cyberpunk lights face their local +Y.** Aimed down −Z, the ring's floods lit the plaza instead of the towers,
  and the frame went flat (Kabuki notes, "Contrast").

- **Textures must be block compressed and shared.** One JPEG per material slot (8,780) uploaded as RGBA8 ran the
  device out of memory. Distinct images as BC1 (colour, ORM) or BC7 (alpha, normals) DDS are 2,410 files and about
  0.7 GB.
- **Bended roads are embedded in the sectors.** `worldBendedMeshNode` and the road intersections reference meshes
  under `_external\road_meshes` with `"Flags": "Embedded"`: they live in the sector's `EmbeddedFiles`, not in any
  archive. `fetch --embedded` writes them back to .mesh (WolvenKit `convert deserialize`) and exports them with a
  material depot. A bended node's mesh is a straight template along +Y; its `deformationData` frames are spaced
  evenly over the template's length (`bend()`).
- **The gameplay BSP has no portals** (one open cell): compile it with `--vis none`.

Milestone 2 (same day) adds the moon's shadow cascades and neon glow to the scene. These are GPU times for one
view, from hidden look runs:

| Resolution | Frame median (p95) | Scene shading | Scene shadows | Scene depth prepass |
|---|---|---|---|---|
| 1600x900 | 5.1 ms | 1.7 ms | 2.1 ms | 0.8 ms |
| 3840x2160 | 7.6 ms (7.9) | 2.7 ms | 2.2 ms | 1.6 ms |

- **Shadows cost fill, not triangles.** The cascades are 4096² (the owner's `r_shadowMapSize`).
  - With every caster culled, the shadow pass costs 0.44 ms (empty indirect draws in three cascades).
  - Coarser caster LODs (`r_sceneShadowLod` 0.25) or a larger caster cut save only 0.1–0.3 ms.
  - By cascade: the first costs 0.25 ms, the second 0.66 ms, and the third (the whole city) 1.24 ms.
    `r_sceneShadowCascades 2` drops the third, but moon shadows then end about 33 m out.
  - One-sided casters saved 0.6–0.75 ms but leaked light under the overpasses, so casters stay two-sided.
- **Hi-Z occlusion is not worth building yet.** At 4K, forcing the coarsest LOD everywhere (far fewer triangles) cut
  the depth prepass from 1.6 to 1.2 ms and shading from 2.5 to 2.0 ms. Occlusion culling only removes triangles,
  so it could save at most about 0.9 ms, and less in practice. The rest is pixel work.

Later the same day (see [levelport-kabuki.md](levelport-kabuki.md)):
- light in the haze (froxel volume, format version 3);
- wet ground with screen-space reflections settled over frames;
- rooms behind the windows;
- the source game's colour grade (`.xlut`).

Then the reference A pass (see [levelport-kabuki.md](levelport-kabuki.md), "Reference A pass"):
- wide-spread lights as cones, the haze's albedo, its local volumes (`.xfog`);
- masked glows (material flag 4), stand-in ads per placement and ad lights in their ads' colours;
- wet roughness 0.2 at most.

And the lamps' shadows (static shadow maps at load, type bit 2).

Runtime cvars:

- `r_scene`, `r_sceneLodBias`, `r_sceneCullPixels`
- lighting: `r_sceneLightScale`, `r_sceneExposure`, `r_sceneEmissive`, `r_sceneAmbient`, `r_sceneSun`, `r_sceneTonemap`
- colour grade: `r_sceneGrade` (1; 0 off, between them a blend), `r_sceneGradeExposure` (1.5, calibrated on Kabuki
  shot A's clean camera); `r_sceneHdr` (1: a graded scene turns `r_hdr 0`'s target into a float one at map load;
  0 keeps `r_hdr`'s), or `r_hdr 2` (latched) for every map
- haze: `r_sceneFog` (sRGB colour before the grade, matched to the map's sky), `r_sceneFogDensity`, `r_sceneFogHeight`, `r_sceneFogMax`;
  `r_sceneFogStart` (150 m, the source's; 0: the colour fills in from the eye); light in it: `r_sceneFogLight` (1.6, calibrated on
  Kabuki shot A; 0 off), `r_sceneFogMoon` (1, the moon's light in it), `r_sceneFogPhase` (0.5), `r_sceneFogFar` (600 m),
  `r_sceneFogAlbedo` (its tint), `r_sceneFogVolumes` (1; the `.xfog` volumes, 0 off)
- windows: `r_sceneWindows` (1)
- detail: `r_sceneDetail` (1; 0 off, up to 2 stronger)
- wet ground: `r_sceneWet` (1), `r_sceneWetPuddles` (0.3), `r_sceneLightSize` (0.3 m), `r_sceneRain` (1, the rain mask);
  reflections: `r_sceneReflect`
  (3, matched by eye), `r_sceneReflectSteps` (32), `r_sceneReflectFar` (150 m)
- glow: `r_sceneGlow`, `r_sceneGlowThreshold`
- the game's dynamic lights (a saber's glow, shots, effects): `r_sceneDlights` (150, their power; 0 off),
  `r_sceneDlightReach` (2.5, times their own radius: the game sizes them for lightmapped maps). The eye view's
  positive lights (up to 32) are lit like the source's lights (same falloff and BRDF)
- shadows: `r_sceneShadows`, `r_sceneShadowLod`, `r_sceneShadowCull`, `r_sceneShadowCascades`, `r_sunShadowFarCache` (the
  far cascade's static casters reused); the lights':
  `r_sceneLightShadows` (1; built at map load, and read while 1), `r_sceneLightShadowRes` (128, at map load)
- performance: `r_sceneGpuDriven` (1), `r_sceneClusters` (2), `r_sceneShadowReceivers` (1), `r_sceneAsync` (1),
  `r_sceneGradeCompute` (1, at video start); bench: `r_sceneInvocations` (0; 1
  prints the lit pass's shader invocations), `r_sceneShaderStats` (0; 1
  prints the driver's register counts of the scene's pipelines at map load, set before the renderer starts)
- `r_sceneDebug`: 1 albedo (raw, no exposure), 2 normals, 3 lights per cell (red, /128; green those reaching and facing), 4 sun shadow; lighting shares 5 ambient, 6 the source's lights, 7 sun; 8 material index (4 bits per channel, emissives too; exact with `look.py --png` and MSAA off, rounding to the
  nearest step); 9 wet (red) and puddles (yellow); 10 reflections alone

`sceneinfo` prints the loaded scene.
