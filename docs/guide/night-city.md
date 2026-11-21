# Night City: converting Cyberpunk 2077 yourself, and what xenjk changed to run it

This chapter covers two things. First, how to turn your own Cyberpunk 2077 install into a playable xenjk map with the
repo's tools, from a single block (Kabuki, one map) up to a chunked city district (Watson). Second, what xenjk added to
the engine, server, renderer and cgame so a city of that size loads, collides, streams and looks like the source. A
reader who only wants the engine side (collision chunks, the 128 k world, the 64-bit client) can skip to
[What xenjk changed](#what-xenjk-changed-to-run-it).

**Status (2026-11-20).**

- In the installed game (Install 94):
  - Kabuki, as `Play_Kabuki.bat`.
  - Every engine, server, renderer and cgame change described here, in both the 32-bit and the 64-bit clients.
- Not installed: the Watson / Night City district (map `cp_watson`, area `night_city_b`). It plays from dev "kits",
  folders under the local build tree that carry the 64-bit binaries, the packs and a settings file.
- Built from committed tools, run locally:
  - the converters;
  - the build driver (`build_watson.py`);
  - the collision builder (`cp77_collision.py`).
- The process reads **CP2077 2.5 or later** through WolvenKit 10. Watson was built from the installed 2.5 on
  2026-11-20 (tag 15) and compared with the earlier 2.31 build: the differences are 2.5's own content.
- Every input is made by a repository tool or kept in the repository, except your own crossing sounds and the
  Realistic Map mod's tiles the sea mask is traced from (both named below).
- **Not yet replicated elsewhere.** Every build so far ran on the authors' machine, with its toolchain already in place;
  nobody has yet followed this chapter from a fresh clone and a fresh install. Expect to fill gaps, and compare your
  counts with [What a correct build looks like](#what-a-correct-build-looks-like). The last 2.5 fix (advertisement
  lights, `cp77_sector25.py` version 4) is in the code and its tests but not yet in a built kit.

**Converted levels are for local play only.** The pipeline assumes you own the game and extract from your own install.
The output (meshes, textures, sounds, ads, the maps themselves) is the game's content and must never be published,
shared or committed. Nothing in this repo contains any of it.

## What it does

### Two layers per map

A CP2077 street is far too dense for a Quake 3 BSP. Kabuki's radius alone holds 51 M triangles at LOD 0 and about
2,000 lights. So every converted map has two layers. The general mechanism, which Beauclair uses too, is covered in
[levelport.md](levelport.md):

1. **A gameplay layer.** One or more q3map2-compiled BSPs that hold collision, spawns, the boundary, and the light
   grid that characters are lit by. A single map (Kabuki) is one BSP. A city (Watson) is a small *hub* BSP plus one
   *collision chunk* BSP per 256 m tile, which the engine streams in and out.
2. **A render layer.** A render scene (`maps/<map>.xscn`, or a tiled scene with a `maps/<map>.xman` manifest). Only
   rd-vulkan draws it, instanced, with GPU culling and LOD and the source's own lights.

CP2077-specific data rides in sidecar files beside the scene: the grade, haze, interiors, rotations, decals, effects,
ads, signals, water and environment.

### The pipeline at a glance

```
your CP2077 install
  | WolvenKit CLI (unbundle, serialise to JSON, uncook meshes to glb + textures to png)
  v
cp2gltf sectors / blocks / scan / fetch (+ --ents --lods --embedded --embedded-ml --proxies)
  |   work folder: raw/, json/, placements.json, meshes/, meshes_lod/, ents/, apps/ ...
  |
  +-- single map (Kabuki) ----------------------------------------------------------+
  |     cp2gltf build --no-render  -> <area>-<tag>.glb -> gltf2map + q3map2 -> zz_<map>.pk3
  |     cp2gltf scene              -> zz_<map>_scene.pk3 (xscn + images + sidecars)
  |     levelcheck spawns / aerial / compare / collide / drop                       |
  |                                                                                 |
  +-- chunked city (Watson): build_watson.py <tag> --data inputs.json -------------+
        tiles      cp2gltf tiles: placements on the city's 256 m grid
        indexes    cp77_ads index, cp77_rotate index, cp77_foliage
        parts      cp77_tiles --parts-only: per tile a collision part, then a scene part
        clutter    colclutter (road litter loses collision)
        collision  cp77_collision: chunk glbs -> chunk BSPs (XBSZ) -> chunk packs + manifest + hub
        spawns     the spawn list into the compiled hub
        scene      cp2gltf scene --merge --tiled (8 detail slots, ocean plane)
        interiors  tag_interiors (.xint)
        decals     cp77decals (.xdcl), fx: cp77fx (.xfx), env: the .xenv packs
        kit        a folder: bin64 + home/base packs + openjk.cfg + launchers + KIT.json
        checks     scene parity, chunk CRCs, tag_verdict judge
```

### Spaces and grids

- CP2077 space is metres, +Z up. The interchange glTF is metres, +Y up, written as `(x, z, -y)`. In game units,
  37.5 u = 1 m.
- Night City streams from `base\worlds\03_night_city\_compiled\default\exterior_X_Y_Z_L.streamingsector`. A level-L
  exterior cell is 64 * 2^L m; interior sectors are on half that grid. `sectors` unbundles levels 0-3 over the area's
  box (or its `sector_boxes`).
- An **area** is an entry in `AREAS` in `tools/levelport/cp2gltf.py`. It has a centre and a half-size in CP metres,
  a radius, and optionally:
  - a cell list (`areas/<area>_cells.json`, 128 m cells and a floor height);
  - its own scan box and sector boxes;
  - another area's work folder (`work`), so `night_city` and `night_city_b` reuse Watson's fetched meshes and bakes;
  - named spawns.

  The committed areas are `kabuki`, `watson_slice`, `watson`, `night_city` and `night_city_b`.
- **The city tile grid.** `cp2gltf tiles` splits placements into 256 m squares whose corner sits at CP2077's world
  origin, so tile `i_j` is the same square in any area's build. A placement belongs to the tile its placed bounds'
  centre lies in, so a building across an edge is built once. 256 m is 9,600 u, which is also the collision
  residency's default load distance.
- A collision chunk is compiled about its own tile corner (whole metres, so the shift is exact). Watson's map frame
  puts CP `(-1280, 2240, 12)` at the origin. That is the `--origin` default of the decal and fx converters.

### What converts, and how

- **Placements** (`scan`). Every mesh node type that places render meshes is read: static, instanced, destructible,
  rotating, bent roads, terrain, advertisement, decoration. The placement rules follow the WolvenKit Blender add-on's
  importer (nodeData Position/Orientation/Scale; instanced transforms absolute; destructible transforms local).
  Content is limited to the default quest and state variants (`blocks` reads `all.streamingblock`;
  `cp2gltf.shown_records` filters every reader), so quest-only content stays out.
- **Entities and devices** (`fetch --ents`). Placed `.ent` templates and their `.app` appearances, including
  `worldDeviceNode`, give doors, lamps, traffic heads, poles, terminals and vending machines. Their mesh components go
  to `entity_meshes.json`, and their lights to the light table.
- **Meshes** (`fetch`, `fetch --lods`, `--embedded`, `--proxies`):
  - glb plus `.Material.json` plus PNG textures per mesh.
  - Every LOD as geometry.
  - The meshes the sectors embed, such as bent roads and terrain. Skipping them leaves the streets as holes.
  - The far proxies.

  WolvenKit CLI drops LODs 2-4 unless its `appsettings.json` sets `"MeshExportArgs": {"LodFilter": false}`.
  `fetch --lods` patches and restores that itself.
- **Materials** (`cp77mat.py`):
  - Multilayered materials (`.mlsetup` + `.mlmask`) are composited per (setup, mask) at `--texsize`. The most visible
    layers are left to the renderer as tiled detail (`.xdet`, 8 slots by default, `--detail-slots`).
  - The `metal_base` family becomes BaseColor x BaseColorScale, alpha-tested where the alpha is bimodal.
  - Signs, neon and screens become unlit glows at 2^(EmissiveEV - 5).
  - `window_parallax_interior` becomes a window with rooms.
  - Glass and mesh decals are alpha-blended.
  - Terrain is flagged.
  - CP normal maps are DirectX, so green is flipped.
- **Lights** (`scan` + `scene`):
  - Lights face local +Y. Spots and area lights become cones with CP2077's own inner and outer angles, including
    spreads past 180 degrees.
  - `scaleVolFog` becomes the light's share in the haze. Haze-only lights are kept.
  - `enableLocalShadows` sets type bit 2, which gives the light a static shadow map.
  - `LU_Nit` tubes are converted as luminance x area, without a pi factor.
  - Lights confined to light channels keep their channel volume's box and only light inside it.
  - Day-only and night-only bits come from `light_time.py`.
  - Ad lights take the mean colour of their own billboard.
- **Collision** (`build --no-render`, per tile with `cp77_tiles.BUILD_ARGS`). This is not CP2077's PhysX geometry.
  It is the render meshes' opaque LODs, chosen to keep LOD 0's walkable surfaces:
  - props under `--col-box` collide as boxes;
  - stairs, steps and ramps (by file name) use their fine mesh;
  - grass, litter, awnings and roofs follow name rules;
  - there is no height cap, so collision reaches wherever a player can go.

  The tiled build adds the door rule: a door the collision opens has its leaves left out of the scene part. That is
  why the collision parts are built before the scene parts. It also adds the road-clutter rule (`colclutter.py`):
  bags, piles, cardboard, tyres and pallets lose collision.
- **Foliage** (`cp77_foliage.py`). `worldFoliageNode` populations from the sectors' embedded `.cfoliage`: 1.54 M
  instances in `night_city_b`. Render only, each drawn out to its `streamingDistance`.
- **Rotating meshes** (`cp77_rotate.py`). Fans, propellers and the ad rings go to `maps/<map>.xrot`. Each turns about
  its mesh's origin, not the record's `Pivot`.
- **Ads** (`cp77_ads.py`). Every `worldAdvertisementNode` and ad device's TweakDB group, override and format go to
  `ad_index.json`. The scene build picks each screen's ad as a rendered image of its ink layout.
- **Decals** (`cp77decals.py`). `worldStaticDecalNode` (grime, puddles, graffiti, road markings): each `.mi` chain is
  resolved through 13 templates into a box record plus albedo / normal / roughness / emissive / tiled images, giving
  `maps/<map>.xdcl` and an image pack. Watson tag 11 has 347,669 decals.
- **Effects** (`cp77fx.py`). Placed `.particle` and `.effect` systems (steam, smoke, dust) become emitter templates and
  placements in `maps/<map>.xfx` (format in the module docstring). Watson tag 11 has 18,594 placements.
- **Haze, grade, signals.**
  - `worldStaticFogVolumeNode` boxes and `e_steam_*` columns go to `maps/<map>.xfog`.
  - The game's LUT (`base\weather\24h_basic\luts\cp2077_gen_lut_nge_v017.xbm` by default, `--grade`) goes to
    `maps/<map>.xlut`.
  - Traffic and crossing signal programs go to `maps/<map>.signals`, shared by the renderer's lamps and the cgame's
    sounds.
- **Interiors** (`tag_interiors.py`). CP2077's interior streaming boxes go to `.xint`. An interior is drawn only from
  inside its box, as the game streams it.
- **Water.** Water patches and an ocean plane with a sea mask go to `.xwat`. `patch_water.py` fills the bay into a
  built scene.

## Take this piece

**Depends on:**

- [levelport.md](levelport.md): gltf2map, the node convention, the render scene format and its renderer.
- [renderer.md](renderer.md): rd-vulkan in xenjk.
- The collision chunk engine work stands alone. It needs no render scene and no CP2077.

### Files

New files (copy whole):

| Path | Role |
|---|---|
| `tools/levelport/cp2gltf.py` | CP2077 world to glTF: `sectors`, `blocks`, `scan`, `fetch`, `decimate`, `tiles`, `build`, `chunks`, `scene`, `scene-hide`; `AREAS` |
| `tools/levelport/cp77mat.py` | CP2077 materials, multilayer compositing, detail slots |
| `tools/levelport/cp77_tiles.py` | an area through tiles: tiling, per-tile parts (keyed), merge, checks |
| `tools/levelport/cp77_collision.py` | a kit's collision: chunk glbs, chunk BSPs, chunk packs, manifest, hub |
| `tools/levelport/build_watson.py` | the whole kit, stage by stage, resumable, with a preflight |
| `tools/levelport/cp77_area.sh` | every stage of a single-map port, in order, then the level checks |
| `tools/levelport/cp77_foliage.py`, `cp77_rotate.py`, `cp77_ads.py`, `cp77decals.py`, `cp77fx.py` | the side readers above |
| `tools/levelport/cp77_meshlod.py`, `make_xlod.py` | CP2077's own per-mesh LOD distances to `maps/<map>.xlod` (no renderer reader yet) |
| `tools/levelport/wk_fetch.py`, `wk_read_lenient.ps1` | resources by depot path through WolvenKit; a lenient reader for files WolvenKit half-parses |
| `tools/levelport/tag_interiors.py`, `patch_water.py`, `light_time.py`, `tag_probesky.py` | post-steps on a built scene |
| `tools/levelport/colclutter.py`, `colcook.py`, `colzbsp.py`, `colpack.py`, `colmerge.py`, `colbsp.py`, `slabwriter.py` | collision cooking, XBSZ compression, the slab package, BSP reading |
| `tools/levelport/patch_chunks.py`, `patch_tiles.py`, `patch_kit.py`, `patch_materials.py` | fixes into a dev kit without a rebuild |
| `tools/levelport/kit_derive.py`, `kit_verify.py` | kit B = kit A + these files (hard links, `KIT.json`); check a kit against its record |
| `tools/levelport/levelcheck.py`, `engine_trace_ded.py`, `probe.py`, `scene_pick.py`, `look.py`, `look_stats.py` | offline and in-engine checks, screenshots |
| `tools/levelport/memgate.py`, `stagekey.py`, `jobguard.py`, `scene_parity.py`, `tag_verdict.py` | the build's memory gate, stage keys, kill-on-close, parity and verdict |
| `tools/levelport/areas/*.json` | Watson's and Night City's cells, the hub's pocket and spawns (hand-made, no game data) |
| `tools/levelport/q3map2/*.patch` | patches to netradiant-custom's q3map2 (see its `README.md`) |
| `codemp/qcommon/cm_chunk.cpp`, `cm_chunk.h` | the whole collision-chunk system: manifest, worker thread, zip reader, validation, slots, residency, grid, commands and counters (the calls it shares with `cm_load.cpp` are in `cm_chunk.h`) |
| `codemp/server/sv_chunk.cpp` | the server's side: `SV_ChunkFrame` (residency around the clients), `SV_ChunkHeld`, `SV_ChunkHoldEnabled` |
| `codemp/qcommon/cm_zbsp.cpp`, `cm_zbsp.h` | XBSZ (zstd per lump) chunk decoding |
| `lib/zstd/` | zstd 1.5.7's decoder, vendored (`lib/VENDORED.md`) |
| `codemp/cgame/cg_signals.c` | crossing-signal sounds from `maps/<map>.signals` |
| `codemp/rd-vulkan/vk_stream.cpp`, `shaders/glsl/scene/*` | scene streaming and the scene shaders (general: [levelport.md](levelport.md)) |
| `codemp/rd-vulkan/vk_scene_cp77.cpp` | the CP2077 side-file readers: `.xfog`, `.signals`, `.xrot` (and its compute pass), `.xscr` |
| `codemp/game/bg_bike.c`, `bg_bikemove.c`, `bg_bike.h`, `BikeNPC.c`, `codemp/cgame/cg_bike.c`, `tools/bike/` | the CT-3X bike (below) |

Hooks in stock files:

| File | Function / place | What the hook does |
|---|---|---|
| `codemp/qcommon/cm_local.h` | `cmChunk_t`, `MAX_CM_CHUNKS` (1024) | chunk clip maps beside the main map |
| `codemp/qcommon/cm_load.cpp` | `CM_LoadMap` calls `CM_LoadMapChunks`, `CM_ClearMap` calls `CM_ClearChunks`; `CM_Alloc`, `CM_ParseChunk` | the stock lump loaders, now also parsing a chunk's lumps into its own blocks (`CM_Alloc` records them, so a chunk can be freed alone). The rest of the chunk system is not in this file |
| `codemp/qcommon/cm_public.h` | `CM_LoadChunk`, `CM_ChunkResidency`, `CM_ChunkReadyAt` | the engine's entry points into `cm_chunk.cpp` |
| `codemp/qcommon/cm_trace.cpp` | `CM_Trace` split into `CM_TraceInMap`; `CM_TraceChunks` from `CM_BoxTrace` (model 0) | world traces also run over every chunk the swept box touches, in the chunk's frame; hits stay `ENTITYNUM_WORLD`; an unloaded chunk is a solid box |
| `codemp/qcommon/cm_test.cpp` | `CM_PointContents` | world point contents OR the chunks' (unloaded: solid) |
| `codemp/qcommon/common.cpp` | `Com_Init` | registers the commands `cm_loadchunk`, `cm_chunkmanifest`, `cm_drive`, `cm_drive_move`, `cm_chunkreport`, `cm_chunkpreload`, `cm_chunkpending` (their handlers are in `cm_chunk.cpp`; the `cm_chunk*` cvars register there on first use) |
| `codemp/qcommon/q_shared.h` | `MAX_WORLD_COORD` | +-64 k to +-128 k (131,072) |
| `codemp/qcommon/cm_polylib.h` | `MAX_MAP_BOUNDS` | now `MAX_WORLD_COORD`: patch facets past 65,535 were silently dropped |
| `codemp/server/sv_main.cpp`, `sv_ccmds.cpp` | `SV_Frame`, `map_restart` | one call each to `SV_ChunkFrame` (`sv_chunk.cpp`): residency around the active clients before the game frame; held clients flagged `SVF_CHUNKHOLD` and `PM_FREEZE` |
| `codemp/server/sv_client.cpp` | `SV_ClientThink` | tests `SV_ChunkHeld` (`sv_chunk.cpp`): a (re)spawning client waits for the chunks within `cm_chunkSpawnNear`, then for the chunk it stands in |
| `codemp/client/cl_main.cpp` | `CL_Frame` | a remote client (no local server) drives residency around its own player, so prediction sees the server's city |
| `codemp/client/cl_cgame.cpp` | `CL_GetSnapshot` | freezes prediction while the chunk under the player, or where it will be in a ping, is not in yet |
| `codemp/game/g_public.h`, `g_active.c` | `SVF_CHUNKHOLD` | the game skips a held client's think; bots honour it |
| `codemp/cgame/cg_draw.c` | crosshair trace | literal 131072 becomes `WORLD_SIZE` |
| `codemp/cgame/cg_view.c` | `CG_DrawActiveFrame` | calls `CG_Signals_Frame()` |
| `codemp/rd-vulkan/vk_scene.cpp`, `vk_scene_local.h` | the render scene core; `SCENE_GRID_MAX` 64 to 128 (in `vk_scene_local.h`) | the `.xdet`, `.xint` and `.xenv` readers; the CP2077 side files are in `vk_scene_cp77.cpp` (above) |

### Build inputs

- `codemp/CMakeLists.txt` lists `qcommon/cm_chunk.cpp`, `cm_chunk.h` and `server/sv_chunk.cpp` for the engine and the
  dedicated server, builds `lib/zstd`'s decoder into both and compiles
  `cm_zbsp.cpp` with `XENJK_ZSTD_LINKED`. Without it, `cm_zbsp.cpp` loads `libzstd.dll` / `libzstd.so.1` at run
  time, and a binary that has neither reads plain chunks only.
- `codemp/cgame/CMakeLists.txt` lists `cg_signals.c`, `cg_bike.c`, `bg_bike.c` and `bg_bikemove.c`;
  `codemp/game/CMakeLists.txt` lists the bike's game files.
- After any scene shader change, regenerate the scene SPIR-V with `python tools/scene_spirv.py`
  (`shaders/spirv/scene_shader_data.c`).
- Build the **64-bit** client for anything past Kabuki. See the porting notes.

### Cvars and commands

All of them, with descriptions: [../xenjk-cvars.md](../xenjk-cvars.md). The ones that matter:

| Cvar | Default | What |
|---|---|---|
| `cm_chunkManifest` | 1 | read `maps/<map>.chunks` at map load |
| `cm_chunkLoadDist` / `cm_chunkEvictDist` | 9600 / 19200 | load within, free beyond (x and y, to the chunk's bounds) |
| `cm_chunkMegs` | -1 | resident chunk cap: 45% of RAM (at least 512 MB); the Watson kits set 2048 |
| `cm_chunkSpawnNear` | 2000 | a spawning client is held until these chunks are in |
| `cm_chunkAsync` | 1 | load on a worker thread |
| `cm_chunkUnloadedSolid` | 1 | an unloaded chunk traces as its solid bounds (cheat) |
| `cm_chunkIdleEvictSec` | 30 | free everything after this long with no driver |
| `r_sceneStream` / `r_sceneStreamMB` / `r_streamScene` | 1 / 1536 / 1 | tiled scene geometry and images stream from their packs |
| `r_sceneInteriors` | 1 | interiors drawn only from inside their boxes |
| `r_sceneGrade` | 1 | the `.xlut` colour grade |
| `r_sceneFogVolumes`, `r_sceneWet`, `r_scenePDecals`, `r_sceneFx`, `r_sceneRotate`, `r_sceneSignals` | 1 | haze volumes, wet ground, projected decals, particles, rotations, signal cycles |
| `r_sceneDlights` | 150 | the game's dynamic lights (sabers, shots) on scene surfaces |
| `r_sceneEyeRelative` | 1 | eye-relative projection, so depth holds 130,000 u from the origin |
| `r_sceneLodFade` | 0.25 | dithered LOD crossfade, as CP2077's |
| `r_sceneEnv` | "" | load another `.xenv` (dawn, day, night) for an A/B |
| `r_sceneDebug` | 0 | 5 ambient, 6 lights, 8 material index (with the scene's `materials.json`), 9 wet |

Commands:

- `cm_chunkreport`: residency counters.
- `cm_chunkpending`: the chunks loading now.
- `cm_drive x y z`: a virtual residency driver.
- `/screenpos <words>`: writes a report that `probe.py` and `scene_pick.py` read offline.

### Data formats

| File | Spec |
|---|---|
| `maps/<map>.chunks` | text, one chunk a line: `<bsp> <ox> <oy> <oz> <minx> <miny> <minz> <maxx> <maxy> <maxz>`, whole-unit origin, placed bounds (`CM_ReadChunkManifest`) |
| `maps/<map>_chunks.cfg` | `cm_loadchunk` lines, a manual fallback |
| `maps/chunk_<tile>.bsp` | an RBSP, or XBSZ (RBSP header + one zstd frame per lump, some lumps byte-shuffled): `colzbsp.py`, [../levelport-collision.md](../levelport-collision.md) section 38 |
| `zz_<map>_colchunks*.pk3` | chunk packs, at most 3.5 GB each (the engine reads no zip64); members stored |
| `.xscn`, `.xman`, `.xdet`, `.xwat` | the render scene: [../levelport-scene.md](../levelport-scene.md) |
| `.xlut`, `.xfog`, `.signals` | grade, haze volumes, signals (with optional `sound <role> <path>` lines): [../levelport-scene.md](../levelport-scene.md), `SceneSignals` in `cp2gltf.py`; readers in `vk_scene_post.cpp`, `vk_scene_cp77.cpp` |
| `.xint`, `.xrot`, `.xscr`, `.xenv` | interiors, rotations, screen flipbooks, environment: the readers' comments in `vk_scene.cpp` (`.xint`, `.xenv`) and `vk_scene_cp77.cpp` (`.xrot`, `.xscr`); `tag_interiors.py`, `cp77_rotate.py` |
| `.xdcl` | decals: `cp77decals.py` docstring (the contract file it cites is local research) |
| `.xfx` | effects: `cp77fx.py` docstring |

### Porting notes

- **The collision chunks are engine-only** (`qcommon`, `server`, `client`). They work with any renderer and no render
  scene. A map without `maps/<map>.chunks` behaves exactly as stock. The chunk system is its own file pair
  (`cm_chunk.cpp`/`.h`, plus `server/sv_chunk.cpp`); the stock files carry only the hooks in the table above. Two
  things come with it: `cm_trace.cpp`'s `CM_Trace` split (`CM_TraceInMap`), and the compact collision records (the
  index-based `cNode_t`/`cbrush_t`/`cbrushside_t`/`cmPlane_t` in `cm_local.h`, which `cm_load.cpp`'s parser fills),
  a separate change that ships in the same files. The client's `CL_Frame` and `CL_GetSnapshot` blocks are still
  edits in stock client files. Remote clients need the same packs as the
  server; there is no checksum of the manifest in the gamestate (see Limits).
- **The 128 k world** is four edits (`q_shared.h`, `cm_polylib.h`, `cg_draw.c`, and `SCENE_GRID_MAX` in `vk_scene_local.h`). `msg.cpp` already
  sends origins as full floats, and pmove's snapping is exact at this size. Single-player `code/` was not touched.
  Other trees that bound positions (rend2's culling, mods' own constants) need their own audit.
- **64-bit.** The Watson kits run the x64 client. On NVIDIA, a 64-bit process commits about its VRAM again as
  write-combined private memory that is never touched, so judge RAM by the working set
  ([../levelport-render-district.md](../levelport-render-district.md) section 3). The engine and the 32-bit client
  build from the same tree.
- **The render side is rd-vulkan only.** rd-vanilla and rend2 draw the gameplay BSP: collision and the light grid,
  with no scene. A Kabuki BSP built with the BSP-only path (`decimate` + `build`) does draw in other renderers, at a
  1.5 M triangle budget.
- **q3map2.** The hub needs `world_coords.patch`, because the joined hub reaches past +-65,536 u. Chunks must be
  compiled **without** it (`exact_planes` + `brush_cook` + `brush_bin`): `world_coords` changes the float bevels.

## Do it yourself

### What you need

- **Cyberpunk 2077**, installed, a copy you own (Steam or GOG layout: `archive/pc/content` and `archive/pc/ep1`).
  Set `CP77_GAME` to the install root. Do not run the game while WolvenKit reads its archives (`build_watson.py`'s
  preflight checks this).
- **WolvenKit CLI** and the .NET runtime it is built for:
  - `WOLVENKIT_CLI` is the path to `WolvenKit.CLI.exe`;
  - `WOLVENKIT_DOTNET` is a folder holding that runtime (it becomes `DOTNET_ROOT`; ours is .NET 10);
  - `PWSH` is PowerShell 7, for `wk_read_lenient.ps1`.

  Use **WolvenKit 10.0.0-beta.1** with **CP2077 2.5** (the pins are in `tools/levelport/toolchain.json`; the
  driver's preflight checks every tool there, stops on a missing one and reports another build). 2.5 changed the
  sector format (WolvenKit 9 cannot read it), and `cp77_sector25.py` rewrites 2.5's sectors, as the sectors stage
  serialises them, in the layout the readers take. A later patch or WolvenKit build may need reader fixes: compare its
  build with the last one (Limits below).
- **q3map2**, netradiant-custom at be3b369 with the patches in `tools/levelport/q3map2/` (build steps in its
  `README.md`). You need two builds:
  - the hub build: all patches, including `world_coords`;
  - the chunk build: `plane_hash`, `cullsides_sweep`, `large_maps`, `exact_planes`, `brush_cook`, `brush_bin`.

  gltf2map takes `$Q3MAP2`; `cp77_collision.py` takes `--q3map2` and `--q3map2-chunks`.
- **Blender 5.1**, only for `cp2gltf decimate` (the BSP-only path): `BLENDER`.
- **Python 3.11** with numpy, scipy, Pillow, psutil and etcpak. `XENJK_LIBZSTD` is a libzstd shared library, used to
  write XBSZ chunks.
- **xenjk built** (x86 for Kabuki, x64 for a city). The tools look for the game at `game_directory/Jedi
  Academy/GameData` in the repo, or in `XENJK_GAMEDATA`. `levelcheck.py` uses `XENJK_X64` (an x64 Release folder)
  and `XENJK_X64_GAME` (a jampgame build with `g_levelcheck`).
- **Disk, memory and time** (measured on the authors' machine, 32 GB of RAM).
  - Disk: plan on at least 150 GB free for one fresh Watson build, on one drive. From 2.5, the serialised sectors
    alone are 18 GB and the material bakes 39 GB; a collision build is about 21 GiB; the first two 2.5 builds (tags 13
    and 14) and a Kabuki pilot in one research root came to about 247 GB.
  - Stages refuse to start without their own size plus 15 GB free (`cp77_collision.py` stops under 25 GB); the
    driver's preflight sums the run per drive first.
  - Memory: the parts and compiles are memory-gated (`memgate.py`). The collision stage's chunk export peaks above
    43 GB of committed memory and runs alone (it reserves 45 GB), so with 32 GB of RAM Windows must be able to grow the
    page file by well over 16 GB, on a drive with that much free.
  - Time: about 5 hours for a first build from a fresh extraction (tag 13); about 3 hours when the extraction and
    bakes are kept (tag 12). Windows only.
- Work and build folders go under `levelport/` in the research root (`cp77/<area>/`, `build/`, `look/`): `.codex-research`
  in the checkout, or `XENJK_RESEARCH`. They are local and never committed. The examples below write
  `.codex-research/levelport/...`; read that as `$XENJK_RESEARCH/levelport/...` if you set it.

#### Environment

This chapter's tools read these variables ([levelport.md](levelport.md) lists the ones every level port shares). A
tool reads a variable when it needs it, and a missing one ends the run naming it.

| Variable | Used by | Meaning |
| --- | --- | --- |
| `CP77_GAME` | `cp2gltf.py`, `build_watson.py` | the Cyberpunk 2077 install root |
| `WOLVENKIT_CLI` | `cp2gltf.py`, `wk_fetch.py` | `WolvenKit.CLI.exe` |
| `WOLVENKIT_DOTNET` | same | the .NET runtime folder (becomes `DOTNET_ROOT`) |
| `PWSH` | `wk_read_lenient.ps1` callers | PowerShell 7; else `pwsh` on `PATH` |
| `BLENDER` | `cp2gltf.py decimate` | `blender.exe` |
| `Q3MAP2`, `Q3MAP2_CHUNKS` | `gltf2map.py`; `cp77_collision.py` takes `--q3map2`, `--q3map2-chunks` | the hub's and the chunk build's q3map2 |
| `XENJK_LIBZSTD` | `colpack.py` | libzstd, to write XBSZ chunks |
| `XENJK_X64`, `XENJK_X64_GAME` | `levelcheck.py`, `cp77_area.sh` checks | the x64 client folder; a game module with `g_levelcheck` |
| `XENJK_GAMEDATA`, `XENJK_RESEARCH` | all | the JKA `GameData`; the work root |
| `CP77_ADS_DIR` | `cp77_ads.py` | the ad compositor folder (`ads.json`, `rendered/`) |
| `CP2GLTF_RERUN`, `CP2GLTF_ARGS` | `cp2gltf.py`, `build_watson.py`; `cp77_area.sh` | `1` reruns stages whose key is unchanged; extra `cp2gltf` arguments |

Run every tool from the repository root. The Python tools are meant to run as `python -I`.

### A. One block as one map (Kabuki)

1. **Pick or add an area.** Add an entry to `AREAS` in `cp2gltf.py`: `'myblock': {'centre': (x, y), 'half': 192.0,
   'radius': 135.0}` (CP metres; find coordinates in WolvenKit's world view). Kabuki is `(-1185, 2052)`, radius
   135 m: 62 k placements of 2,733 meshes, about 1,900 lights.
2. **Run the whole recipe.**

   ```
   bash tools/levelport/cp77_area.sh <area> <tag> [--map <mapname>]
   ```

   It runs, in order, logging to `<work>/area-<tag>.log` and stopping at the first failure:
   - `sectors` (skip it with `SKIP_SECTORS=1`), `scan`;
   - `fetch --ents`, `fetch`, `fetch --lods`, `fetch --embedded`, `fetch --embedded-ml`;
   - `scene` and `build --no-render --min-size 1 --col-tris 0 --col-prop-tris 0`, side by side;
   - `gltf2map ... --light fast --vis none`;
   - `levelcheck.py spawns`, `aerial`, `compare`, `collide`, `drop --engine 5000`.

   Extra cp2gltf arguments go in `CP2GLTF_ARGS`. Outputs: `build/<map>-<tag>/zz_<map>.pk3` (gameplay) and
   `build/<map>_scene-<tag>/zz_<map>_scene.pk3` (render), with the checks in `look/levelcheck-<area>-<tag>/`.
3. **Or stage by stage.** Each stage records a key and skips an unchanged rerun (`CP2GLTF_RERUN=1` forces it):

   ```
   python tools/levelport/cp2gltf.py sectors --area <area> [--sector-kinds exterior,interior]
   python tools/levelport/cp2gltf.py blocks  --area <area>
   python tools/levelport/cp2gltf.py scan    --area <area>
   python tools/levelport/cp2gltf.py fetch   --area <area> --ents
   python tools/levelport/cp2gltf.py fetch   --area <area>            # then --lods, --embedded, --embedded-ml
   python tools/levelport/cp2gltf.py build   --area <area> --no-render --min-size 1 --tag <n>
   python tools/levelport/gltf2map.py .codex-research/levelport/cp77/<area>/<area>-<n>.glb cp_<area> \
       --out .codex-research/levelport/build/cp_<area>-<n> --sky <name> --sky-haze "66 43 40 61 37 36" \
       --sun "0.55 0.65 1 80 200 40" --skylight 150 --worldspawn _ambient=20 --light fast --vis none
   python tools/levelport/cp2gltf.py scene   --area <area> --tag <n> [--map cp_<area>]
   ```

   The gameplay BSP is one open cell (`--vis none`); its light stage only fills the light grid. Fast loops on a
   finished scene:
   - `scene --relight <scene pk3>` rewrites only the light table, after a `scan`;
   - `--regrade <pk3>` rewrites only the grade;
   - `--repack` rewrites the images as distinct DDS.
4. **Look at it** (hidden, muted, private home; nothing installed):

   ```
   python tools/levelport/look.py <build>/zz_<map>.pk3 <map> --extra-pk3 <scene build>/zz_<map>_scene.pk3 \
       [--viewpos "x y z yaw [pitch]"] [--cvar name=value] [--renderer rd-vulkan]
   ```

5. **Play.** Copy both pk3s into your own `GameData/base` and start the map with rd-vulkan. `Play_Kabuki.bat` is
   the example launcher.
   - The traffic signals need `signal_groups.json` in the work folder: `python -I tools/levelport/cp77_signals.py
     --area kabuki` builds it from the sectors' `compiledTraffic_Intersection` programs. Without it the signal heads
     stay dark.
   - The crossing sounds are your own WAVs (a local script made ours from `sfx_container.bnk`). The `.signals` file
     names them: pass `--signal-sounds <folder>` to the scene stage, with `walk_base`, `walk_layer`,
     `dontwalk_base` and `dontwalk_layer` `.wav` in that folder of a pack
     ([../levelport-scene.md](../levelport-scene.md), traffic signals). Without it the crossings are silent.

### B. A district as a chunked city (Watson / Night City)

1. **Cells and hub data.** An area of cells (`areas/<area>_cells.json`) needs:
   - a hub file (`areas/<area>_hub.json`): the pocket walls, the grid check points and the spawn order;
   - optionally a spawn list (`areas/<area>_spawns.json`).

   These are hand-made JSON with no game data. `night_city_b` is the template: Watson, the 78 tiles south and east,
   and El Coyote Cojo's block.
2. **An inputs file.** Copy `tools/levelport/areas/night_city_b/inputs.example.json` and fill in your folders. Keep
   the copy outside the repository or uncommitted: it names your machine's paths. `build_watson.py`'s docstring lists
   every key. These are required:

   | Key | What |
   |---|---|
   | `area`, `map` | e.g. `night_city_b`, `cp_watson` |
   | `game_data` | your GameData (shader scripts and common textures for the hub) |
   | `sdl2`, `wolvenkit_cli` | `SDL2.dll` for the kit's `bin64`; the WolvenKit CLI the decal and effect fetches use |
   | `collision` | the collision entry point's command, e.g. `["python", "-I", "tools/levelport/cp77_collision.py", ...]` |
   | `ocean_mask` | the bay's sea mask (`.npz`), made by `cp77_seamask.py` (step 3) |
   | `ads_dir` | the ad folder `cp77_ads_fetch.py` writes: `ads.json` and `rendered/` |
   | `decals_root`, `fx_root` | folders for the decal and effect fetches (`wk_fetch.py` layout): create them empty; the stages fill them |

   `env`, `cfg` and `moon` may be left out: the `.xenv` files (night, dawn, day) and the kit's settings template are
   in `tools/levelport/areas/night_city_b/`, and `--extract` fetches the moon. The checks stage's keys
   (`reference_*`, `engine`, `expect`) compare a build with an earlier kit of yours; a first build has none (step 3).
3. **Run, in this order** (what the 2.5 build did). Use a research root of its own for a fresh extraction
   (`XENJK_RESEARCH`): a work folder holds one game patch's extraction.

   ```
   python -I tools/levelport/cp77_ads_fetch.py <ads dir>
   python -I tools/levelport/build_watson.py <tag> --data <inputs.json> --bin <x64 Release folder> --dry-run
   python -I tools/levelport/build_watson.py <tag> --data <inputs.json> --bin <x64 Release folder> --extract --until parts
   python -I tools/levelport/cp77_seamask.py <ocean_mask .npz> --tiles-tag <tag> --map <Realistic Map 32K tiles> \
       --preview <mask.jpg>
   python -I tools/levelport/build_watson.py <tag> --data <inputs.json> --bin <x64 Release folder> --extract --until kit
   ```

   - The ads come first: the indexes and parts stages read them. The sea mask needs the tiles' collision parts and
     the Realistic Map 32K mod's 16 map tiles (your own copy of a third-party mod, uncooked with WolvenKit:
     `base/fx/textures/3dmap/static/image/32k`); look at its preview before going on. The scene stage reads it.
   - Without the mod's tiles you can leave `ocean_mask` out: the driver builds the scene without a sea mask. Nobody has
     looked at such a build yet, so the bay may look wrong.
   - `--until kit` stops before the checks, which need a reference kit. A later build of the same area can name the
     earlier kit as `reference_kit` (with `reference_materials`, `reference_collision` and the rest) and run the
     checks: parity of instances, lights and materials, the chunks, the hub, the packs and the engine corpora.
   - `--extract` runs the WolvenKit stage: sectors (exterior and interior), scan, the fetches (entities first, then
     meshes, LODs, proxies, embedded meshes and multilayer setups), the device graph and the light times, and the
     moon. Without it, the existing extraction is kept. A fetch cache records the identity of the game archives and
     of WolvenKit (`.fetch_identity.json`), so after a game patch the fetch stops instead of mixing versions: start a
     new research root, or keep building from the old one.
   - The other flags: `--from STAGE` reruns that stage and everything that reads it; `--until` and `--only` limit the
     run; `--one-lane` runs the two lanes in turn; `--idle` runs at Idle priority with half the workers;
     `--kit-name` names the kit folder.
   - A stage's `.done` holds a key of its tools' code and inputs, so a changed tool reruns it. An interrupted run
     resumes where it stopped.

   Tag 12 on 269 tiles (32 GB of RAM, from a kept extraction) took about 6 min (tiles), 9 (indexes), 27 (parts),
   10 (scene), 1 (interiors), 5 (decals), 4 (fx) and 106 (collision, 4 workers); the checks took 12. Tag 13, the
   first build from a fresh 2.5 extraction, took about 5 hours: 32 min (extract), 132 (parts, every material bake
   made new) and 94 (collision) the largest. The kit is `build/<map>_baseline-<tag>/`.
   #### What a correct build looks like

   With no reference kit to check against, compare your first build with Watson from 2.5 as built here (tag 15,
   CP2077 2.5, WolvenKit 10.0.0-beta.1). A build from the same patch should land within a few percent of every line;
   a large miss in one line names the stage to look at.

   | Count | Tag 15 (2.5) | Where |
   |---|---|---|
   | tiles | 269 | `tiles/<tag>/tiles.json` |
   | placements (scan) | 5,313,290 | `placements.json` |
   | scene instances | 6,712,721 | `tiles/<tag>/driver/scene.log`, scene merge counts |
   | meshes, materials, images | 63,766, 57,083, 27,876 | the same |
   | static, entity, instance lights | 78,399, 23,981, 5,956 | `lights_*` in the same line |
   | advertisement lights | 14,314 (about 14,100 coloured from their ads) | `lights_ad`, `ad_light_coloured` |
   | collision chunks | 195 | the collision build's chunk packs |

   Tag 15 predates the advertisement-light fix and colours none of its ad lights; the 2.31 build coloured 14,160 of
   14,306, so a build with the fix should come close to that. A build from a later patch differs by that patch's
   content: our 2.31-to-2.5 comparison moved instances by under one percent.

4. **Pieces alone** (what the driver calls):

   ```
   python -I tools/levelport/cp77_tiles.py <area> <tag> --parts-only [--workers 3] [--from-tag <prev>] [--only-tiles FILE]
   python -I tools/levelport/cp77_collision.py <area> <tiles tag> --map cp_watson --out <build>/cp_watson_collision-<tag> \
       --q3map2 <hub q3map2.exe> --q3map2-chunks <chunk q3map2.exe> [--workers 4] [--tiles a,b] [--dry-run]
   python -I tools/levelport/cp77_foliage.py <area> <tag>
   python -I tools/levelport/cp77_ads.py index --area <area>
   python -I tools/levelport/cp77_rotate.py index --area <area>
   python -I tools/levelport/cp77decals.py needs|fetch <work> <root>   then   cp77decals.py <work> <root> <builds>
   python -I tools/levelport/cp77fx.py needs|fetch <work> <root>       then   cp77fx.py <work> <root>/json <out> --png <root>/png
   ```

   `cp77_collision.py` builds through `<out>.partial` and resumes per tile. Its steps:
   - `chunks`: the tiles' glbs in their own frames.
   - `hub`: the ring, the lid, the sky, the pocket and a `trigger_hurt` void kill. The light grid is kept under
     65,536 samples.
   - `compile`: `gltf2map --col-chunk --exact-planes --cook` per tile. Tiles wholly outside the void kill are
     skipped. A compiled chunk is reused by key from `.codex-research/levelport/cache/chunks/`.
   - `packs`: XBSZ members; the manifest and the cfg go in the last pack.
   - `record`: `collision.json`.
5. **Fix without a rebuild ("patch, don't regen").** A kit is many GB, and one wrong class should not cost a rebuild:
   - `patch_chunks.py <name> --base-kit DIR [--tiles a,b] [--fix box-shapes,doors]`: box-only collision fixes in
     minutes. Old brushes point at a non-colliding shader, and the right boxes are compiled alone.
   - `patch_tiles.py <name> --tiles a,b | --report <screenpos.txt> [--ring] --base-kit DIR`: rebuild a few tiles'
     collision, then call `patch_kit.py`.
   - `patch_kit.py <base kit> <name> --builds DIR --manifest M`: a dev kit as hard links plus
     `zz_<map>_colchunks_patch.pk3` (a later pack name wins).
   - `patch_materials.py <base kit> <name> --materials <scene>/materials.json --template X.mt`: re-convert one
     template's material records into a patch pack.
   - `kit_derive.py <base kit> <name> --put home/base/x.pk3=<file> --cfg 'seta ...'`, then `kit_verify.py <kit>`.

   When patching scene tiles, keep every record in place: hide an instance with a tiny hide distance, or zero a
   light's power. The manifest's counts and the global instance indices (`.xrot`, the ad tables) must still hold. The
   renderer refuses a tile whose counts disagree with `.xman`. Write tile packs stored and named to sort last.
6. **Check before you play.**
   - `levelcheck.py`: `spawns`, `aerial`, `compare-tiles` / `stitch`, `collide`, `drop-tiles --engine N`.
   - `engine_trace_ded.py`: corpora traced by the 64-bit dedicated server.
   - `probe.py <screenpos report> --kit DIR`: what collides at a reported spot, offline, to 0.1 u of the engine's
     own trace.
   - `scene_pick.py`: what the scene draws at a pixel.
7. **Play** from the kit folder: its launcher starts `bin64` with the kit's home and settings. Copying a kit's packs
   into GameData is an install, a separate decision.

### Kabuki vs Watson

| | Kabuki | Watson (`night_city_b`, tag 11) |
|---|---|---|
| Extent | 135 m radius, one roundabout | 268 tiles of 256 m (Watson plus the next area plus El Coyote Cojo); far edge about 113 k u |
| Gameplay | one BSP (`cp_kabuki-22`) | hub BSP + 195 chunk BSPs (54 outside the void kill skipped), 5.30 GiB XBSZ |
| Render | one `.xscn`: about 65 k instances and 2,070 lights (scene-21) | tiled scene: 6.70 M instances, 56,444 materials, 121,615 lights, streamed |
| Built by | `cp77_area.sh` | `build_watson.py` |
| Engine needs | nothing beyond rd-vulkan's scene | collision chunks, the 128 k world, the x64 client |

## What xenjk changed to run it

### Collision chunks (engine and server)

The full record is [../levelport-collision.md](../levelport-collision.md). A district does not fit one q3map2 run
(about 40 GB of RAM for 6 M brushes) or one BSP (the format's 2 GB). Collision far from the origin also differs from
collision at the origin, because float bevels change. So collision is per tile, compiled about the tile's own corner,
and the engine dispatches world queries to it:

```
 world trace (model 0)            chunk slot i (manifest order)
 CM_BoxTrace -> CM_TraceInMap(hub)  -> CM_ChunkCandidates(swept box)  (grid index)
                                    -> for each resident chunk: start/end - origin,
                                       CM_TraceInMap(chunk), end/plane + origin, merge
                                    -> for each unloaded chunk: its bounds as a solid box
```

- **The hub** is the map the server loads. It holds the sky shell, the boundary ring and lid, one area and one
  cluster, every entity and inline model, and the light grid. Chunks hold world brushes only, so no entity number
  or model index moves.
- **Residency.**
  - The server drives it from every active client's origin and velocity (`SV_ChunkFrame`). A remote client drives
    it around its own player (`CL_Frame`), so prediction answers as the server does.
  - Chunks load within `cm_chunkLoadDist` and are freed past `cm_chunkEvictDist` (hysteresis), on a worker thread.
    A finished chunk is swapped in at a frame start, so no trace ever sees half a chunk.
  - The worker loads first the chunk a driver will reach soonest.
  - Memory is capped by `cm_chunkMegs`, least recently used first. A chunk inside the load distance is never evicted.
- **Absence is never empty space.** An unloaded chunk traces as its solid placed bounds. A player is held
  (`SVF_CHUNKHOLD`, `PM_FREEZE`) while the chunk under them is loading. At a spawn they are held until the chunks
  within `cm_chunkSpawnNear` are in. At map load, the regions around spawn points load in place, because the game's
  map-load traces need them.
- **XBSZ.** Each chunk is stored as zstd per lump. It decodes to the exact BSP (195 of 195 byte-identical), and the
  installed collision drops from 7.14 to 5.30 GiB. The largest chunk (tile -6_0, 208 MB of XBSZ) loads on the
  worker with a read of 215 ms, a decode of about 1.2 s and a parse of about 0.5 s.
- **Gates.** Traces through chunks placed at +-232,000 / +-207,500 u matched the control in all 31,000 traces of a
  corpus, ends within 0.0083 u, at 1.00-1.12x the time. A residency smoke on the kit: 0 violations, 0 false walls,
  worst frame 1 ms.

### World size and the 64-bit client

- **World size.** `MAX_WORLD_COORD` is +-131,072, and `MAX_MAP_BOUNDS` follows it. Float spacing at 113 k u is
  0.0078 u.
- **Renderer precision.** The renderer draws the eye's views eye-relative (`r_sceneEyeRelative`), because float32
  depth at 131 k u made a facade's panels fight their backing. It also draws CP2077's far proxies past a 1.5 km far
  plane.
- **Memory.** The Watson kits need the 64-bit client. Their smoke runs peak at about 6.3-6.9 GiB, and a city's
  collision, scene and images do not fit a 32-bit address space. Since Install 94 every launcher starts `bin64`.

### Render scene features CP2077 drove (rd-vulkan)

The scene format, tiles, streaming, GPU cull and LOD are general. See [levelport.md](levelport.md) and
[../levelport-scene.md](../levelport-scene.md). What exists because of CP2077:

- **The source's lights.**
  - Cones with their own angles, the haze share, light-channel boxes and day/night bits.
  - `LU_Nit` tubes as capsules.
  - Ad lights in their ads' colours.
  - The game's dynamic lights (sabers, shots) reach scene surfaces (`r_sceneDlights`).
  - Light clusters for surfaces and the haze.
- **Lamp shadows.** Lights with type bit 2 get static shadow maps rendered once at load into a depth atlas
  (16-bit, `r_sceneLightShadowDepth`). Streaming redraws faces as their casters arrive (`r_sceneStreamFaces`,
  `r_sceneSettleFaces`).
- **Colour grade** (`.xlut`). The game's display LUT, applied as a full-frame pass after the tone curve, before the
  HUD. Haze and sky colours are set before the grade.
- **Haze** (`.xfog`). A froxel volume with the lights scattered in it (in shafts where they cast shadows), plus the
  game's fog volumes and steam columns, binned past 1,024 volumes. Eye adaptation is metered within CP2077's
  exposure window for the hour.
- **Wet ground and reflections.**
  - Up-facing surfaces get wetter by porosity (CP2077's `RainAreaSettings`), with puddles where the rain reaches (a
    rain view from above).
  - Reflections are screen-space, traced at half resolution and settled over frames.
  - Water uses FFT waves (`r_sceneWater*`).
- **Windows with rooms.** `window_parallax_interior` rooms are laid out in mesh space from the atlas, room size,
  depth and the dark share. Their exposure follows the scene's (`r_sceneWindowExposure`).
- **Interiors** (`.xint`). Interior geometry is drawn and streamed only near or inside its box.
- **Screens and ads.**
  - Ad stand-ins per billboard; flipbooks (`.xscr`); the screen look (layers, pixel grid, hue-keeping tone).
  - Holograms with CP2077's dot grid.
  - Emissives at 2^(EmissiveEV - 5) with masked glows (lanterns, lightboxes).
- **Decals and particles.** Projected decals go into a DBuffer (`.xdcl`). Placed particle emitters (`.xfx`) are drawn
  with the blends, with drag-limited steam.
- **Motion.** Rotating meshes (`.xrot`) turn on the GPU. Traffic and crossing signals cycle their intersections'
  programs (`.signals`).
- **Time of day** (`.xenv`). Night, dawn and day environment packs, with the moon's path and image.
- **City-size streaming.** Tile geometry and images stream from stored packs, with a 1.5 GB geometry pool. Culling
  uses GPU-listed 512-instance groups, a count-then-scan cull, and two-phase occlusion against the last frame's depth.
  There is a VRAM budget with fail-soft allocations.

### Signals, sound and the bike (cgame and game)

- **`cg_signals.c`.** Plays the crossings' WALK / DON'T WALK sounds on the same clock as the renderer's lamps
  (`cg.time` + `r_sceneSignalShift`), at the game's trigger rates, within 40 m. It is idle on maps without
  `maps/<map>.signals`. Not carried: the Wwise attenuation curve (the engine's default falloff stands in) and each
  intersection's start time (a fixed offset per intersection stands in).
- **The CT-3X bike.** Master has it, and so does the installed game since Install 66:
  - `VH_BIKE`, its own vehicle type (`BikeNPC.c`), so stock vehicles run none of its code.
  - Fixed-step bike dynamics in `bg_bike.c` / `bg_bikemove.c`, shared by game and cgame. The handling is the bike
    pack's `ext_data/vehicles/ct3x.bike` ([../bike-handling.md](../bike-handling.md)), written by
    `tools/bike/ct3x_params.py` from the TweakDB records that `tools/bike/tweakdb.py` reads. Without it the bike
    drives on a generic handling.
  - Wheel spin, the fork, lean and the rider pose in `cg_bike.c`.
  - The model is built by `tools/bike/ct3x_model.py`. Its sounds are rendered from CP2077's recorded layers by
    `tools/bike/ct3x_audio.py`: the player bike's engine is a Wwise plugin model with no recordings.

  Design and status: ../wishlist/vehicles.md.
- **No city ambience yet.** Crowds and traffic are not converted.

### Performance: targets and what was measured

- **Targets.**
  - First, the district holds Kabuki's frame rate (about 120 fps at 4K).
  - Then 240 fps at 4K on Kabuki: lossless first, trade-offs only behind cvars.
  - Later, 1080p low settings on a 6 GB laptop GPU. Not built for yet, but no design may rule it out.
- **Kabuki.** The 1080p lap went from 7.37 to 6.12 ms median (139 fps) with the far cascade's cache. At 4K it went
  from about 11.5 to 9.3 ms after the lossless steps (GPU-driven draws, clusters, receiver culling, async SSR and
  grade).
- **Watson at 4K (night-6 to night-9).**
  - 10.6-13.6 ms GPU median at nine spots at first (75-95 fps).
  - After the group cull (cull 2.0 to 0.5 ms), haze binning and the interior reader: 8.8-10.7 ms at the busy spots
    (93-136 fps).
  - The 1.5 km far plane costs +0.07-0.28 ms on the streets and +2.31 ms from 400 m up.
- **CPU (2026-11-09).** Event-driven texture streaming, GPU rotations and GPU group culling took the backend's
  per-frame CPU from 2-3 ms to under 1 ms. The views are GPU-bound since.
- **Load.** Collision preload is capped at 2 GB in the kits (`cm_chunkMegs 2048`), so a spawn into a cold region
  holds about 7 s.

## Limits and open issues

- **Game patch.** The process reads CP2077 2.5 through WolvenKit 10.0.0-beta.1 (`tools/levelport/toolchain.json`).
  Watson from 2.5 (tag 15, 2026-11-20) against the same district from 2.31 (tag 12), same binaries: 6.66 M of
  6.71 M instances match, about 8,700 placements are only in 2.31 and 26,000 only in 2.5, and the views differ by
  2.5's own changes (props, foliage, a few lamps, other ads). Known 2.5 limits:
  - WolvenKit 10 (a beta) exports 11 of Watson's meshes without their textures.
  - A light whose colour the game sets at run time reads white from WolvenKit 10 where WolvenKit 9 read black, the
    placeholder the readers skip or colour; `cp77_sector25.py` puts white back to black for entity, device, template,
    appearance and advertisement lights. The few lights that are really white become placeholders too (Kabuki: 11);
    no field tells them apart.
  - A patch after 2.5, or another WolvenKit build, may change the format again: build a few tiles and compare them
    with your last kit (the checks stage) before a full run.
- **Inputs made before the build** (each by a repo tool now):
  - `signal_groups.json`: `cp77_signals.py`. The crossing sounds are still your own WAVs;
  - the ad TweakDB dump and its rendered ads: `cp77_ads_fetch.py <ads dir>` (WolvenKit 10.0.0-beta.1 or later), read
    through `CP77_ADS_DIR` or the inputs' `ads_dir`;
  - the bay's sea mask: `cp77_seamask.py`, from the tiles' collision parts, the water patches and a stitch of the
    Realistic Map 32K mod's map tiles (a third-party mod: your own copy, uncooked with WolvenKit; the one input from
    outside the game);
  - the Watson `.xenv` look values and the kit settings: in the repository (`tools/levelport/areas/night_city_b/`);
    the moon: fetched by `--extract`.
  - Still without a tool: the decal contract.
- **Collision is derived from render meshes**, not CP2077's PhysX shapes: walkable, but not the game's exact
  collision.
- **Still tangled** ([engine](../review/2026-11-19/engine.md) and [CP2077](../review/2026-11-19/levelport-cp.md)
  reviews, Liftability): the chunk system's cvars register where they are first read, not in one `CM_ChunkInit`; the
  client's `CL_Frame` and `CL_GetSnapshot` blocks are not in `cm_chunk.cpp`; WolvenKit access is split between
  `cp2gltf.py` and `wk_fetch.py`; three Python readers parse the `.xscn` header besides `xscn.py`; the chunk glb
  writer exists three times in `cp2gltf.py`; and `levelcheck.py` still reaches into `tools/renderer_bench.py` for
  the client.
- **The manifest has no checksum or stable chunk ids in the gamestate.** Client and server must carry the same
  packs. Residency is driven by clients only, not by projectiles. Long traces meet unloaded chunks as whole-bounds
  boxes, not the occupancy proxies the design proposed.
- **`MAX_CM_CHUNKS` is 1024.** Watson uses 195. All of Night City (about 12.7 x 11.4 km) would also pass the 128 k
  world.
- **Renderer.**
  - The `.xlod` reader (CP2077's own LOD distances) is not built.
  - The rain view's tiles and the probe sky are parked off.
  - One ground z-fight report is unreproduced.
- **Not converted:** crowds, traffic, the glitch ambient layers, and the Wwise attenuation. Watson's scene carries
  no traffic signal programs (0 signal heads lit, as in every Watson kit so far); Kabuki's come from
  `cp77_signals.py`, which has not been run on Watson.

## Further reading

- [../levelport-kabuki.md](../levelport-kabuki.md): the single-map port, its features one by one with costs, and the
  playtest fixes.
- [../levelport-watson.md](../levelport-watson.md): the district's scope, phase M measurements, tiles, and the dated
  handoffs. It is long, so start at its "Resume here".
- [../levelport-collision.md](../levelport-collision.md): collision at district size, the chunk runtime design
  (section 16), residency, the 128 k world (34), cooker A (37) and XBSZ (38).
- [../levelport-render-district.md](../levelport-render-district.md): the renderer at district size, occlusion
  culling, and the 64-bit client's memory explained.
- [../levelport-scene.md](../levelport-scene.md): the render scene format, tiled scenes and `.xman`.
- [../levelport-watson-renderer-20261109.md](../levelport-watson-renderer-20261109.md): the latest playtest reports
  and the renderer fixes they drove.
- [../BASELINE.md](../BASELINE.md): the clean imports from committed tools (tag 12 from 2.31, tag 15 from 2.5), the kit
  chain, stage timings and the 2.5 comparison.
- [../../tools/levelport/README.md](../../tools/levelport/README.md): every tool's one-paragraph usage and "Things
  that bit".
- [../../tools/levelport/q3map2/README.md](../../tools/levelport/q3map2/README.md): the q3map2 patches and how to
  build them.
- ../wishlist/levels.md, ../wishlist/vehicles.md: the level and
  vehicle wishlists.
- [levelport.md](levelport.md), [beauclair.md](beauclair.md): the general pipeline, and the other city port.
