# levelport

Brings levels from other games into Jedi Academy through one interchange format: a glTF (`.glb`) scene with a
small node convention, compiled to a normal JKA BSP by q3map2. Source readers write the glTF; one compiler reads
it. Blender sits in the middle when a level needs hand work (import the `.glb`, edit, export with
*Include > Custom Properties* so the conventions survive).

```
Quake Live / Q3 BSP --bsp2gltf.py--+
NieR:Automata tile --nier2gltf.py--+--> level.glb --gltf2map.py--> .map + OBJ chunks + shaders --q3map2--> pk3
Blender / any glTF ----------------+                                 (lighttransfer.py: QL lighting)
```

Nothing here installs anything; outputs go to `.codex-research/levelport/`. Converted third-party levels are for
local play only and are never published. The owner installs a build for play by copying its `zz_<map>.pk3` into
`GameData/base` with a `Play_<Map>.bat` beside `Play_Eve.bat` (the Flooded City, 2026-10-15). The converters
need no engine or renderer changes.

## Locations

`paths.py` holds every location the tools use, each from an environment variable; nothing outside the repository has
a default, and a tool that needs one exits naming the variable:

| Variable | What | Default |
|---|---|---|
| `XENJK_RESEARCH` | work and output root (`levelport/` under it) | `<repo>/.codex-research` |
| `XENJK_GAMEDATA` | the game directory | `<repo>/game_directory/Jedi Academy/GameData` |
| `Q3MAP2`, `Q3MAP2_CHUNKS` | the patched q3map2 builds ([q3map2/README.md](q3map2/README.md)) | none |
| `WOLVENKIT_CLI`, `WOLVENKIT_DOTNET` | WolvenKit.CLI.exe and the .NET runtime it runs on | none |
| `CP77_GAME` | the Cyberpunk 2077 install | none |
| `BLENDER` | blender.exe (`cp2gltf.py decimate`) | none |
| `PWSH` | PowerShell 7 | `pwsh` on PATH |
| `XENJK_X64`, `XENJK_X64_GAME` | levelcheck's x64 client build folder, and its optional game module | none |

`toolchain.py` checks these tools against the builds the reference kits were made with (`toolchain.json`). Inputs made
before a build, each by its own tool: `cp77_signals.py` (signal_groups.json), `cp77_ads_fetch.py` (the ads' TweakDB
dump and rendered ads; WolvenKit 10.0.0-beta.1 or later), `cp77_seamask.py` (the bay's sea mask); Watson's env files,
kit settings and an example inputs file (`inputs.example.json`) are in `areas/night_city_b/`. The process reads
CP2077 2.5 through WolvenKit 10.0.0-beta.1; `cp77_sector25.py` rewrites 2.5's sectors and entity resources in the
layout the readers take (docs/guide/night-city.md, section B, has the order to run).

## Node convention

Space: glTF metres, +Y up. 37.5 Quake units per metre (the cloth pipeline's scale; a 64-unit player is 1.71 m).
`levelport.py` holds the axis conversion. Roles are node extras under `"lp"` (a Blender object custom property):

| `lp` | meaning |
|---|---|
| `render` (default for a mesh) | triangles drawn as lightmapped (or `--vertexlit`) `misc_model` chunks (`.glb`) on a `--chunk` grid; material name = shader; node matrices place instances (props) |
| `collision` | default: one convex brush from the node's faces (convex hull if they are not convex). `"shape": "mesh"`: thin detail slabs (floors and slopes `"thickness"` units deep measured vertically, with vertical sides and a cap parallel to the face; ceilings and undersides the same upward; walls along the normal), one per convex group of coplanar edge-neighbour triangles (`--merge-verts` corners, 12; 3 = one per triangle): the same solid in far fewer brushes, since q3map2 `-bsp` time grows faster than the brush count. Nodes run in `--jobs` processes. `"contents"`: Quake names (`solid`, `playerclip`, `water`, ...). `"visible": true`: sides draw with the face materials, texture matrix fitted from the UVs (movers, decals) |
| `patch` | `patchDef2` from `"cp"` (control points: glTF position + st), `"width"`, `"height"`, `"shader"` |
| `entity` | extras are the entity keys; the node position is the origin. Collision and patch descendants become its brushes (doors, triggers) |
| `ignore` | skip the node and its children |

Materials: a name that is a known shader (source scripts given by `--source-base`, or the installed base) keeps
it; an embedded base-colour image becomes `textures/levelport/<map>/<material>` with a generated shader
(`alphaMode` MASK -> alpha test). Other material extras:

| extra | meaning |
|---|---|
| `lp_blend: multiply\|add\|alpha` | non-solid decal: filter, additive, or alpha-blended and vertex-lit |
| `lp_emissive: {color, intensity, tint, light}` | unlit glowing stage (`rgbGen const` when `tint`), plus a q3map2 surface light of 150 x intensity unless `light` is false |
| `lp_water: {color, normal, detail, roughness, waves, opacity, tile, scroll}` | water surface: a vertex-lit stage of `color` with the `normal` texture scrolling (plus a `detail: [tile ratio, strength]` copy), F0 0.02 and `roughness`, so rd-vulkan adds probe reflections; `waves: [[amplitude m, wavelength m, period s]]` become `deformVertexes wave` (the mesh needs the vertices). Without `color`/`normal`: `tint` with crossing scrolls of a `ripple` texture. Swimming needs a separate `contents: water` collision node |
| `lp_scroll: [s, t]` | `tcMod scroll` on a multiply/add decal |
| `lp_layers: [{texture, tile, weight}]` | terrain blend: glTF texture indices with their own `tcMod scale`; `weight` null is the base, `"r"`/`"g"` name the `COLOR_0` channel that blends the layer in |
| `surfaceparms` | ride on collision sides (`slick`, `sky`, `noimpact`, ...) |

A terrain blend compiles Quake 3 terrain style: vertex alpha carries one weight, so each triangle gets a two-layer
shader (base + `r` layer where `g` is zero, else the dominant of those two + the `g` layer), and the weight goes into
the chunk's `COLOR_0` alpha.

Asset extras `levelport`: `units_per_metre`, `source`, `source_game`, `worldspawn` keys.

## Tools

- `bsp2gltf.py <map.bsp> <out.glb> --base <extracted baseq3>`: Quake 3 / Quake Live BSP. Render surfaces, every
  gameplay brush as a collision node with its side shaders, patches, entities, preview textures.
- `nier2gltf.py <tile> ... --out <glb>`: NieR:Automata world tiles (`wd1/gXXXYY`) straight from the CPKs, using
  the PlatinumGames readers in `../2b-smallsword` (`--nier-tools`). LOD0 meshes and albedo, the tile's placed props
  (`Layout.lay`; `--no-props` skips them) with their own collision, COL2 collision as `shape: mesh`, spawns sampled
  on the ground. Materials follow the game's material variables: `g_Tile` baked into UVs, PBS10 terrain as
  `lp_layers` with `g_MaterialTile` per layer, `g_1BitMask` cutouts, `g_Decal` and CNS00 multiply decals, CNS00
  unlit/emissive. `--variant before|after` picks the story state (the camp's `camp_before`/`camp_after` groups).
- `gltf2map.py <in.glb> <mapname> [--preset quakelive] [--source-base DIR] [--sky yavin --sun "r g b I yaw elev"]
  [--vertexlit] [--light-from source.bsp] [--worldspawn k=v] [--scale 1.2]`: writes and compiles the map, packs
  `zz_<map>.pk3`. `--scale` sizes the whole level in Quake units (JKA's player is taller than Quake's; see
  `.codex-research/levelport/scale-20261017/NOTES.md`) and carries over to the light transfer.
- `cp2gltf.py sectors|scan|fetch|decimate|build [--area kabuki]`: Cyberpunk 2077 world areas, stage by stage
  into `.codex-research/levelport/cp77/<area>/` (see "Cyberpunk 2077" below). Needs WolvenKit CLI (`WOLVENKIT_CLI`,
  `WOLVENKIT_DOTNET`) and Blender 5.1 for `decimate`. `cp77mat.py` converts the materials.
- `lighttransfer.py <source.bsp> <target.bsp>`: replaces q3map2's lightmap and light-grid values with the source
  map's baked lighting, by world position (`--light-from` runs it after the light stage).
- `stage.py <build> <map> <q3map2 args> | --pack`: one q3map2 stage on a `--no-compile` build, or the pk3, so each
  stage's result is checked before the next (NieR notes: [docs/levelport-nier.md](../../docs/levelport-nier.md)).
- `relight.py <build> <out> <map> -- <light args>`: reruns only q3map2 `-light` on a copy of a build (lighting
  comparisons without recompiling geometry).
- `look.py <pk3> <map>`: hidden, muted screenshots from spawn points in the installed client; `--set` adds console
  lines (probes), `--cvar` sets cvars before the map loads. It looks as a spectator (no body or saber in view; a saber
  forces third person and its blade lights the scene; `--player` for the old way). `--viewpos "x y z yaw [pitch]"`
  takes views of your own; the pitch (degrees, up positive) is held look-up at a fixed rate, so it lands within a
  degree or so. `setviewpos` pushes the view about 78 units along the yaw.
- `blender_preview.py`: imports a `.glb` in Blender headless and renders a top and a perspective view.
- `probe.py <report>`: what collides where a `/screenpos` report (its `.txt`, its folder's `index.txt`, or
  "x y z [yaw [pitch]]") points, offline: the crosshair ray, the floor below and the player's box against the tiles'
  collision parts, and with `--kit` against the compiled chunk BSPs the kit loads (on col-14's reports it gives the
  engine's own traces to 0.1 u). Dev-speed P8, 2026-11-03.
- `scene_pick.py <report.txt> <px,py> ...`: what the renderer draws at pixels of a `/screenpos` report's
  screenshot, offline: the view ray against the tag's scene tile parts, front to back (placement, mesh, material and
  its images, CP2077 point, draw range). `--every-range`: every placement on the ray whatever its draw range (what
  a hole's data holds). Night-14's review, 2026-11-04.
- `wk_fetch.py <list> <root> [--embedded]`: CP2077 resources by depot path from the installed game into a fetch
  folder: unbundled into `raw/`, serialised into `json/` (flat names; a second path of the same name under
  `json/<depot path>.json`), textures uncooked into `png/`, in batches of 120 paths; what is there is skipped, what
  the archives lack goes into `fetch_absent.txt`. `--embedded` exports the textures embedded in the fetched files
  into `embedded/` (map.json). WolvenKit, dotnet and the game from cp2gltf's settings. 2026-11-06.
- `cp77decals.py needs|fetch <work> <root>` and `cp77fx.py needs|fetch <work> <root>`: the decal and effect fetches
  as one command each. `needs` writes the depot paths still missing (`<root>/needs.txt` or `--out`): decals from the
  visible nodes' materials through parents, templates and multilayer files to their textures; effects from the
  steam, smoke and dust systems and effects on 15 or more nodes (`--match`, `--min-nodes`, or a `--roots` list) to
  the systems they play and their textures. `fetch` runs wk_fetch until nothing new is needed; it exits 1 while
  paths remain. Both read what the converters read (`cp77decals.py <work> <root> <builds>`,
  `cp77fx.py <work> <root>/json <out> --png <root>/png`).
- `cp77_foliage.py <area> <tag>`: CP2077's worldFoliageNode (vegetation and litter: 1.54 M instances in night_city_b)
  from each sector's embedded `.cfoliage` populations into `tiles/<tag>/foliage/<tile>.jsonl`; the scene parts draw
  them out to the node's streamingDistance, collision never reads them. The owner's CP2077 reference views,
  2026-11-05.
- `cp77_ads.py index|stats [--area]`: which CP2077 ad each screen shows. Every worldAdvertisementNode and ad device's
  adGroupTDBID (the district's group), adOverrideTDBID (a pinned ad) and format into `<work>/ad_index.json`;
  `AdMap` gives the scene build each screen's ad as a rendered image of its ink layout (the TweakDB dump and the
  renders are local: `look/ads-20261105`, WolvenKit 10.0.0-beta.1 on the installed game). 2026-11-05.
- `cp77_rotate.py index|stats [--area]`: CP2077's rotating meshes (worldRotatingMeshNode: fans, propellers, the ad
  rings; 17,257 nodes) into `<work>/rotate_index.json`; `RotMap` gives each scene placement its axis, signed speed and
  phase, the scene parts carry them (`inst_rot`) and the merge writes `maps/<map>.xrot` for the renderer. A node turns
  about its mesh's origin, not its record's `Pivot` (a streaming point). 2026-11-05.
- `tag_interiors.py <src scene> <dst scene>`: CP2077's interior streaming boxes into a built tiled scene (.xint);
  `patch_water.py`: every scanned water patch into a built scene's water tile; `light_time.py`: the scene lights'
  day / night bits. 2026-11-04.
- `patch_tiles.py <name> --tiles a,b | --report r.txt [--ring] --base-kit DIR`: a collision fix played within the
  hour: those tiles' parts, chunks and compiles in their own tiling `tiles/dev-<name>/`, then `patch_kit.py`.
- `patch_kit.py <base kit> <name> --builds DIR --manifest M`: a dev kit `build/cp_watson_dev-<name>/`, the base kit
  as hard links plus `zz_<map>_colchunks_patch.pk3` (the rebuilt tiles' BSPs and chunk lists; a later pack name
  wins); `--full-pack` writes a whole chunk pack instead (members copied compressed, as `make_chunks_pk3.py` does),
  for patches past the engine's 4 GB pack limit. Either is split into `<pack>2.pk3`, `<pack>3.pk3`, ... past 3.5 GB
  (`PACK_MAX`; the engine reads no zip64). Never a numbered kit; not installed.
- `patch_chunks.py <name> --base-kit DIR [--tiles a,b] [--fix box-shapes,doors] [--full-pack]`: a box-only
  collision fix in minutes, no tile compiled again: the old boxes' brushes in the base kit's chunk BSPs are pointed
  at a shader that collides with nothing, the right boxes compiled alone into `chunk_<tile>_p` at the tile's origin,
  doors re-decided from the stored parts (their leaves also leave the tile's `t_<tile>.xscn`, in a scene patch
  pack). 2026-11-03, for col-15's playtest (the chunk stage's box shapes, the door floor test).
- `cp77_collision.py <area> <tiles tag> --map M --out DIR --q3map2 EXE`: a kit's whole collision from a tiles tag's
  collision parts (build_watson.py's collision stage): the chunk glbs, every reachable tile's chunk BSP, the chunk
  packs `zz_<map>_colchunks*.pk3` with the residency manifest, and the hub `zz_<map>.pk3` (pocket, void kill, light
  grid check, the spawns of `areas/<area>_hub.json`). Made once through `<out>.partial`, resumable per tile.
  2026-11-06, replacing tag 10's uncommitted chain ([docs/levelport-collision.md](../../docs/levelport-collision.md)
  section 36).
- `slabwriter.py`: gltf2map's mesh-collision slabs in plain float arithmetic, the same bytes as the numpy writer
  (dev-speed P1b: the chunk conversion about 3.6x faster on top of P1a). `gltf2map --col-chunk` also keeps each
  node's brush text in `--slab-cache` (default `.codex-research/levelport/cache/slabs`; `none`) and splits its q3map2
  tiles from memory.
- `jobguard.py`: `kill_children_with_me()`, a kill-on-close Windows Job Object; gltf2map, cp2gltf and cp77_tiles join
  one when run as scripts, so their pools, q3map2 and WolvenKit end with them however they end (dev-speed R3).
- Kept stages (dev-speed R5/R6, 2026-11-03): `cp2gltf.py sectors`, `scan`, `fetch` (each variant) and `fetch --ents`
  record a key of their code, arguments and their inputs' and outputs' sizes and times in `<work>/stage_records.json`,
  and the chunks stage records base.glb's in `<tiling>/chunks/base.key.json`; an unchanged rerun returns at once and
  writes nothing. `CP2GLTF_RERUN=1` runs them anyway.
- Sector variants (quest and state ranges): every reader of sector records goes through `cp2gltf.shown_records`
  (default_records with the streaming blocks), so content the game hides by default stays out of every output alike:
  the scan, the haze volumes (`sector_fogs`), `cp77fx`, `cp77_foliage`, `cp77_rotate` and `cp77_ads` (round 3 review,
  2026-11-06: the side readers took every range; 129 variant-off ad screens in Watson).
- Patching a built scene's tiles: keep every record in place (hide an instance with a tiny hide distance, zero a
  light's power) so the manifest's counts and the global instance indices (.xrot, the ads tables) still hold; if a
  patch must add records, rewrite the tile's line in `maps/<map>.xman` too (the renderer refuses a tile whose counts
  disagree). Write tile packs stored and named to sort after every overlay that carries tiles.
- `stagekey.py`: a stage's code key, the hash of the definitions it can reach (cp77_tiles' part keys, the material,
  mesh and slab caches): an edit to another stage, a comment or a docstring leaves it alone.
- Build process round 4 (dev-speed REPORT4, 2026-11-10):
  - `build_watson.py` checks the whole run before its first stage (inputs, `--bin`'s five files, the chunks' q3map2
    taking `-brushbin`, outputs already made, the disk each drive will take, CP2077 running when WolvenKit will) and
    lists every problem at once; `--dry-run` is that check plus the commands, `--no-preflight` skips it. A stage starts
    only with its own size plus 15 GB free on the drives it writes (cp77_collision likewise checks only its own). New
    stages `clutter` (colclutter scan, drops, patch, verify -> `<map>_collision-<tag>c`) and `spawns`
    (`areas/<area>_spawns.json`, the 120 of kit 11y, into the compiled hub -> `<map>_spawns-<tag>`); the kit takes
    their packs, the inputs' `extra_packs`, `replace_packs`, `renderer` and `cfg_set`, and the decals build its own
    decals stage recorded; `checks` takes `"parity_tiles": "all"` and compares the chunk packs with
    `reference_collision` by CRC (`logs/chunk_crc.json`). A stopped driver ends its stage's tool (job guard).
  - Tile parts (`build/<tile>.npz`, `scene/<tile>/arrays.npz`) are written deflated at level 1 (`savez_deflated`; the
    same np.load reads them): collision parts 0.14 of their stored size, scene parts 0.39.
  - `memgate.py`: starts on a memory reservation (each start's peak, less what the running ones have grown into),
    with free commit capped at physical RAM + 16 GB (the page file no longer counts as room). cp77_tiles' parts use
    it in place of the 10 s gap (`part_peak_gb` by placements; each part's peak is logged), cp77_collision its
    `memory()`.
  - The chunk cache: cp77_collision reuses a compiled chunk whose key holds (the glb's geometry without its
    tag-naming source line, the tile, gltf2map's module closure, the chunks' q3map2 folder, the arguments), raw-copying
    it from any build that holds it after checking its CRC; every finished cooked build is indexed
    (`.codex-research/levelport/cache/chunks/index.json`). `--no-chunk-cache` compiles anyway; `--seed-cache BUILD`
    indexes an older build, only after a cold compile of some of its tiles matched it.
  - `engine_trace_ded.py`: a levelcheck corpus traced by the 64-bit dedicated server (no renderer, GPU or game-lock
    slot); its run home sits on the packs' drive and links them, never a copy.
- Build process round 4, second batch (2026-11-10):
  - `build_watson.py`: a stage's `.done` holds a key (its tools' module closures, the inputs it reads, the run ids
    of the stages it reads): a changed tool or input reruns it, and `--from X` reruns X and every stage reading it.
    After the parts, two lanes at once (collision, clutter_patch, spawns beside scene, interiors, decals, fx, env) on
    one memgate ledger (`MEMGATE_LEDGER`; cp77_collision's compiles and the scene side's reservations see each
    other); `--one-lane` runs them in turn. The clutter rule now runs in the chunks step (`"clutter": {"mode":
    "in_stage"}`, the default; `"patch"` keeps colclutter's patch). The ad and rotation indexes, the decals and the fx
    are kept while their inputs hold (`work/driver_records.json`, `build/<map>_{decals,fx}_records.json`); their
    WolvenKit fetches run only under `--extract`. The tiles stage writes `tiles.key` (no second tiling), takes
    `"from_tag"` (D3). `logs/timings.json`: every command's start, seconds, exit, peak private bytes, free space.
    `--idle`: Idle priority for every stage, half the workers, cp77_collision `--idle`.
  - `cp2gltf.py chunks --clutter-drops DIR`: colclutter's drop lists applied where the chunk glbs are written (the
    boxes leave the glb; node numbers keep the part's box order). cp77_collision passes it through.
  - cp77_collision: the hub's cache key is what its compile reads (gltf2map's module closure, the hub q3map2, the
    arguments, GameData's shader scripts and common textures by CRC), so a tool edit or a model install keeps the
    hub; a stopped run recovers the whole chunks of the pack it had open (`recover_members`).
  - cp77_tiles: `part_jobs` keeps each tile's key inputs by the files' sizes and times (`tiles/<tag>/
    part_inputs.json`: 2.5 min -> 0.5 s on 268 tiles, the same keys); `--from-tag PREV` links PREV's tiling while its
    key holds and every part whose key holds (nothing in PREV is written).
  - `kit_derive.py` (kit B = kit A with these files: links, the settings copied, stored overlays checked, KIT.json)
    and `kit_verify.py`; patch_materials and patch_kit make their kits with it.
  - retain.py stops when the keep list is missing (`--keep-file`) and moves run homes' links with a kit
    (`runhomes/` and `runhomes/registry.tsv`, which engine_trace*.py append to).
- Tag 12 prep (2026-11-11, the two reviews before the rebuild):
  - `tag_verdict.py`: the checks with their expectations written before the run (`parts`, `chunks` byte-equal else
    brush-equal by 64-bit brush hashes, `hub` lump by lump, `packs` and map files, `engine` under the owner's tie rule,
    `scene` table counts) and `judge`, which exits non-zero on any miss. build_watson's parts stage runs the early gate
    (`"parts_reference"`: collision parts array by array against an earlier tag, a difference stops the run); its
    checks stage writes every result to `logs/verdict/` and judges them against `"expect"`.
  - scene_parity compares integers exactly, every material record, both sides' keys, and the instance's mesh.
  - Reruns: preflight names the stage to resume `--from` and what of a stage's key moved (markers keep their key's
    parts); it refuses a collision run from a checkout with `game_directory/` (the chunk cache's identity); the
    memgate and cp77_collision gates count the other lane before starting alone and log every wait with its reason;
    the chunk cache logs each miss's reason and the identity's parts; KIT.json is written by the kit stage.

## Worked examples (2026-10-14)

Quake Live Dueling Keeps (CTF), 2 min including vis:

```
python tools/levelport/bsp2gltf.py <quake dir>/maps/duelingkeeps.bsp .codex-research/levelport/ql/duelingkeeps.glb --base <quake dir>
python tools/levelport/gltf2map.py .codex-research/levelport/ql/duelingkeeps.glb ql_duelingkeeps --preset quakelive \
    --source-base <quake dir> --light-from <quake dir>/maps/duelingkeeps.bsp
```

NieR:Automata Resistance Camp (tile g11420), about 13 min (vertex light dominates):

```
python tools/levelport/nier2gltf.py g11420 --out .codex-research/levelport/nier/g11420_before.glb
python tools/levelport/gltf2map.py .codex-research/levelport/nier/g11420_before.glb nier_camp --sky yavin \
    --sun "1 0.95 0.85 380 35 50" --skylight 800 --worldspawn _ambient=35 --vertexlit --gamma 1.6
```

NieR lights its world with precomputed GI (the tile's `RNT.wtb` atlases are directional data, not lightmaps), which
cannot be carried over; q3map2's sun, sky and bounce stand in. `--gamma 1.6` opens up the shade under the overpass
at no compile cost; `-bounce 3` instead of 1 changed little and cost 5 more minutes (2026-10-14 comparison).

## Cyberpunk 2077 (2026-10-27, Kabuki market)

Night City streams from `base\worlds\03_night_city\_compiled\default\exterior_X_Y_Z_L.streamingsector`: level L
cells are 64 * 2^L metres (L0 64 m ... L4 1024 m). An area is a centre and radius in CP metres (+Z up);
`AREAS` in `cp2gltf.py` holds them; an area can name a cell list (`cells`), a box of its own (`box`), several
sector boxes (`sector_boxes`) and another area's work folder (`work`: `night_city`, Watson and the next area as one
map, shares Watson's; `cp2gltf.work_dir`). `cp77_tiles.py --only-tiles FILE` builds those tiles' parts alone. The
Kabuki market roundabout is centred at (-1185, 2052): the market ring is
~40 m across, the road ring reaches ~80 m, its buildings ~130 m. `--radius 135` keeps 62k mesh placements of
2,733 meshes, 51 M triangles at LOD 0, and 1,900 lights.

```
python tools/levelport/cp2gltf.py sectors     # 101 sectors (L0-L3) unbundled + serialised, 1.2 GB JSON, ~30 s
python tools/levelport/cp2gltf.py scan        # placements.json (meshes + lights), ~15 s
python tools/levelport/cp2gltf.py fetch       # glb + .Material.json + PNG textures per mesh, ~1.1 GB
python tools/levelport/cp2gltf.py fetch --lods  # every LOD, geometry only (meshes_lod), ~1.4 GB
python tools/levelport/cp2gltf.py decimate --budget 1.5e6   # Blender collapse of what exceeds the budget
python tools/levelport/cp2gltf.py build       # <area>.glb for gltf2map
```

**Two layers (the way forward, [docs/levelport-scene.md](../../docs/levelport-scene.md)).** The BSP above cannot
hold the density. The level is now a gameplay BSP plus a render scene that rd-vulkan draws instanced, with GPU
LOD and culling and the source's live lights:

```
python tools/levelport/cp2gltf.py build --no-render --min-size 1 --tag 6     # collision, entities, boundary
python tools/levelport/gltf2map.py .codex-research/levelport/cp77/kabuki/kabuki-6.glb cp_kabuki \
    --out .codex-research/levelport/build/cp_kabuki-5 --sky byss --sun "0.55 0.65 1 80 200 40" --skylight 150 \
    --worldspawn _ambient=20 --light fast --vis none                         # ~3 min, 7.5 MB
python tools/levelport/cp2gltf.py scene --tag 1                              # zz_cp_kabuki_scene.pk3
python tools/levelport/look.py .codex-research/levelport/build/cp_kabuki-5/zz_cp_kabuki.pk3 cp_kabuki \
    --extra-pk3 .codex-research/levelport/build/cp_kabuki_scene-1/zz_cp_kabuki_scene.pk3 \
    --renderer ../../../.codex-research/renderer-bench/builds/xenjk-vk-dev/RelWithDebInfo/rd-vulkan
```

The gameplay BSP is one open cell, so vis has no portals (`--vis none`). Its light stage only fills the
light grid the characters are lit by. Rebuild the scene shaders with `python tools/scene_spirv.py`.

After a change to the light conversion, run `cp2gltf.py scan` (about 20 s), then
`cp2gltf.py scene --map cp_kabuki --tag N --relight <scene pk3>`. That rewrites only the light table and skips the
material pass (about 5 minutes with the bakes cached, 25 cold).

The scene build writes `materials.json` beside its pk3: each material record's index (what `r_sceneDebug 8` shows),
mesh, material and base material.

**Colour grade.** `scene` writes the game's own grade as `maps/<map>.xlut` (`--grade`, a LUT depot path; default the
master environment's `cp2077_gen_lut_nge_v017.xbm`; empty for none). `--regrade <scene pk3>` rewrites only the grade.
The LUT is fetched once into `<work>/grade`. rd-vulkan grades the whole frame with it (docs/levelport-scene.md), so
colours matched by eye to a reference (the sky, `r_sceneFog`) are set before the grade.

How Cyberpunk's lights convert:

- **Direction.** Cyberpunk lights face their local **+Y**, not −Z. Spot lights and area lights become cones with
  Cyberpunk's own inner and outer angles, spreads past 180° included: the ring's seven haze lights are 220° areas
  facing outward, and as spheres they filled the ring's middle with a white disc.
- **Share in the haze.** Each light keeps its `scaleVolFog` (percent) as its share of the light the haze scatters
  (scene format version 3). Most ad lights and about half of the rest have 0: the ring's floods light the towers, not
  the haze. Light components (lamps, spotlights, signs) with `useInFog 0`, nearly all of them, have none.
- **Haze-only lights.** Lights with `sceneDiffuse 0` and `sceneSpecularScale 0` light only the volumetric fog. They
  are kept, flagged to light only rd-vulkan's haze volume, unless their share is 0.
- **Shadows.** Lights with `enableLocalShadows` are flagged (type bit 2); rd-vulkan renders static shadow maps for
  them at load. A `--relight` carries the flag.
- **Entities** (lamps, signs, doors, vending machines, traffic lights) are components of their templates and of the
  appearance (`.app`) each placement shows (its appearance name, else the template's default):
  - `fetch --ents` serialises the placed archive templates into `ents/` and their appearance files into `apps/`
    (the sector embeds the rest), keyed by the whole depot path (`cache_name`: one file name lives in several
    folders). Templates under quest, cinematics, characters and AI folders are left out.
  - It writes their mesh components to `entity_meshes.json` (type `entity`, with visual scale and a partial chunk
    mask), which `fetch`, `fetch --lods` and `scene` place like the sectors' own meshes. Kabuki: 3,224, among them
    the doors, the lamps' fixtures and the traffic heads, which no sector places.
  - Devices are entities too: `worldDeviceNode` (a `worldEntityNode` subclass) places the traffic lights' poles
    (`street_sign_d_dst.ent`), street lamps, electrical poles, terminals and lifts. A destructible device keeps its
    intact mesh in an `entPhysicalDestructionComponent`. Before 2026-10-29 both were skipped: the heads floated.
  - Each light and mesh is placed through its chain of hard transform bindings.
  - A black instance light is a run-time placeholder the device colours in, and is skipped; so are `emissiveOnly`
    lights, which only drive a mesh's glow (indicator LEDs).
  - Traffic lights show their red lamp only (`TRAFFIC_LIT`): nothing in the data says which is lit; a controller
    switches them at run time.
- **Ad lights** (`worldAdvertisementNode.lightsData`) take their colour from the ad's video at run time. The scene
  build gives each the mean colour (its hue; the light keeps its power) of what its own billboard shows: the nearest
  placement of its node within 30 m. It writes them to `ad_lights.json` beside the pk3, and `--relight` reads them
  back. `AD_LIGHT_COLOUR` stands in where no billboard is found. The placeholder is the black colour the data leaves
  them; WolvenKit 10 reads it from 2.5 as white, which `cp77_sector25.py` puts back (tag 15 lost all 14 k colours
  before it did).
- **Screens.**
  - `television_ad.mt` (the roundabout's ring, digital billboards) shows its `AdTexture`; for an animated sheet of
    `TilesWidth` × `TilesHeight` frames, the first frame.
  - UI-driven screens (`metal_base_ui.mt`, `parallaxscreen_transparent_ui.mt`) hold only a placeholder, because the
    game renders their content while it runs. One of the area's own ads stands in, chosen per material and per
    placement (one of `AD_VARIANTS`, 8, by the placement's name and place; such a mesh is one scene mesh per variant,
    sharing its geometry). The transparent ones become translucent glowing panels.
- **Masked glows** (`metal_base` with an `Emissive` mask and a non-black `EmissiveColor`: paper lanterns, lightbox
  faces, banner ads, cigarette ads) glow over the lit surface at 2^(EmissiveEV − 5). A mask that is the colour texture
  itself glows in the surface's own colours; any other rides in the colour image's alpha (material flag 4) and glows
  in `EmissiveColor`. Kabuki uses 25 of the 149 such materials its meshes carry. Neon tubes glow at their own
  `EmissiveEV` too (they used to be fixed at 1).
- **Haze volumes.** `worldStaticFogVolumeNode` boxes (a unit cube scaled by the node; `densityFactor` is a density
  per metre when `absolute`, else a multiple of the haze's) and the steam effects (`worldEffectNode` named
  `e_steam_*`, sized by their names: `e_steam_column_3x3x20m` is 3 x 3 m and 20 m tall) go to `maps/<map>.xfog`
  (docs/levelport-scene.md). `scene` and `--relight` both write it.
- **Windows** (`window_parallax_interior.mt`) carry their RoomAtlas as the image and, in the material record, the
  room size in metres, the dark share (`AmountTurnOffAtNight`), the colour spread (`LightsTempVariationAtNight`),
  the room depth and the atlas's rooms (four per `_1x1` atlas, two per `_2x1`). They glow at 2^(EmissiveEV − 5).
  rd-vulkan lays the rooms out in mesh space.
- **Sky.** `gltf2map --sky NAME --sky-haze "hr hg hb zr zg zb"` generates the sky box as a lit haze (horizon to zenith,
  sRGB), set before the colour grade: Kabuki's `66 43 40 61 37 36` grade to about (56, 37, 22) in shot A's upper left,
  the reference's (54, 36, 22).
- **`LU_Nit` lights** (neon tubes) convert to power as luminance × the capsule's surface, without the Lambertian π
  (2026-11-06: with the π, Misty's ground took the cyan of two nit tubes where CP2077's is warm red; without it the
  ground's hue and brightness match CP2077's at the solved camera).
- **Interior lights.** A light whose `lightChannel` leaves out `LC_ChannelWorld` lights only inside a channel volume
  (`worldLightChannelVolumeNode` outline and height, or `worldLightChannelShapeNode` hull) of its channels.
  - The converter stores the smallest such volume's box with the light (scene format version 2).
  - rd-vulkan skips the light outside that box.
  - A light with no volume around it stays unconfined.
  - Lights that include `LC_ChannelWorld` still reach into interiors.

- **Placement contracts** follow the WolvenKit Blender add-on: nodeData Position/Orientation/Scale per record;
  `worldInstancedMeshNode` transforms are absolute; `worldInstancedDestructibleMeshNode` cooked transforms are
  local to each nodeData record; shared buffers resolve by `HandleId`/`HandleRefId`. nodeData `Bounds` are points.
  That is 2.31's layout. 2.5's (WolvenKit 10: compiled records with `nodeIdx`/`instanceIndex`, cooked nodes holding
  inner nodes, fixed-point instanced transforms) is rewritten into it by `cp77_sector25.py` as the sectors stage
  serialises each sector; a change to that tool upgrades the JSON already on disk the next time the stage runs.
- **WolvenKit CLI drops LODs 2-4** unless its `appsettings.json` has `"MeshExportArgs": {"LodFilter": false}`
  (`fetch --lods` patches and restores it; the file has a BOM). glb nodes are `submesh_NN_LOD_m`: NN the global
  chunk index (the appearance material lists index it), m the LOD mask bit. Material names in the glb are all
  `Default`; materials come from `.Material.json` `Appearances` (key = appearance name + its index).
- **Too dense for a BSP**: coarsest LODs alone are 4.2 M triangles in the radius; most of it is street-level props
  under 10 m. `decimate` solves one k for target = max(floor, k * size^2) per mesh to meet `--budget`.
- **Collision** is not CP's: its world collision is PhysX geometry-cache meshes by hash (26k triangle-mesh and
  2k convex shapes against 20k boxes in the radius). `build` uses the opaque coarsest LOD of placements over
  `--col-min` as `shape: mesh`, plus a playerclip ring `--clip-inset` inside the radius. Props under `--col-box`
  collide as their bounding box, except open ones (`--col-prop-tris`, `--col-prop-fill`) and those whose file name
  says stair, step or ramp: their finest LOD under `--col-name-tris` (400), so stairs are walked, not climbed as
  blocks.
- **Materials** (`cp77mat.py`): multilayered composited per (mlsetup, mlmask) at `--texsize`, the scene's main
  layer left to the renderer at its tiling (`--detail-size`; docs/levelport-scene.md, "Detail"); metal_base family
  BaseColor x BaseColorScale (alpha test when AlphaThreshold and the alpha is bimodal); signages/neon/screens
  unlit glow; window_parallax_interior a dim tinted glow; glass and mesh decals alpha-blended. Emissive masks on
  metal_base are not carried yet. CP normal maps are DirectX (green flipped for rd-vulkan).
- **Vertex order** (2026-10-29): the scene keeps the source meshes' triangle order. `scene --vcache <pk3>` rewrites
  a finished scene in tipsify order (vertices renumbered by first use; blended, emissive and masked-glow geometry
  left alone) into `<build>-vc`. On Kabuki it does not help: the meshes have 1.088 vertices per triangle and their
  own order already reaches 1.086 shader runs per triangle in a 32-entry FIFO; the GPU's counters (`r_sceneInvocations`)
  went from 1.491 vertex runs per primitive to 1.518 (cache 32) and 1.509 (16), the 4K lap's passes moved by
  under 0.01 ms, and same-depth triangles of one mesh traded places (parity 73-80 dB). Worth trying on meshes
  that were never optimised (a BSP's surfaces, a raw extraction).

## Things that bit (keep)

- **q3map2 prefixes `textures/`** to every brush-side and patch shader; write names without it. Shader names cap at
  63 characters and the renderer refuses image paths of 64 or more *with* the extension: generated names stay at
  59 (`Compiler.short`).
- **misc_models are read Y-up by assimp** (netradiant-custom's q3map2): write `(x, z, -y)`. Chunks are `.glb`
  because assimp keeps glTF `COLOR_0` alpha and q3map2 carries it through `-meta` and `-light` into the draw
  vertices (tested 2026-10-14; PLY keeps it too but loses the material name, OBJ has no alpha). glTF `v` is written
  as is: assimp flips it on import. `spawnflags 4` lightmaps them.
- **A `misc_model` origin in the void leaks the map**: chunks are anchored at the first spawn point.
- **Model triangles bypass `-meta`**: a lightmapped dense mesh stays one draw surface per few triangles (the camp
  made 118,876 surfaces and crashed rd-vulkan at load). `--vertexlit` merges them into ~1,200 triangle-soup
  surfaces. Brush-built maps (Quake) do not have this problem.
- **The JKA renderers refuse non-power-of-two images** ("Refusing to load non-power-2-dims"); Quake 3 resampled
  them. Packing resamples NPOT images up. A refused image shows the grey, white-edged default texture.
- **Quake 3 / QL map lighting is 4x brighter at load** (`r_mapOverBrightBits 2`) than JKA's default (0); and a
  compiled QL BSP keeps its lightmaps but not the light entities. `lighttransfer.py --scale 4` bakes the shift in.
- **NieR world tiles**: WMB3 map layouts (vertex flags 14 and 4) keep position, packed tangent, uv0 (half2 at +16),
  lightmap uv (+20) and a blend-weight colour (+24) in the core record, a half4 normal at the start of the extra
  data; indices are u32 and absolute in the vertex group; winding is clockwise. COL2: 20-byte mesh records
  (type, modifier, unknown, surface, name index, batch type, offset, count), batches of float4 vertices and u16
  indices. Type 127 and 255 are world and object collision, 3 the tall invisible boundary walls (playerclip).
  Textures a tile lacks are in `wda/ga000N`.
- **NieR material variables decide the look, not texture alpha.** The 0x30-byte WMB3 material record has the
  variable block at words 10/11. The ground's three layers tile 25-45x (`g_MaterialTile`); PBS10 layer alpha is
  data (treating it as cutout punched holes in the camp floor); only `g_1BitMask` materials are cutouts. The
  PBS10 weights are vertex colour R (layer 2) and G (layer 3); B and A are equal on the camp ground; their meaning is unknown and they are not converted.
- **NieR normal maps are DirectX** (green down; measured against each material's AO channel, 20 camp textures
  agree, one disagrees, the rest are too flat to tell): converted with green flipped to rd-vulkan's OpenGL convention and written as `_n`
  sidecars. Masks are R metal, G glossiness, B occlusion; `g_bGlossinessOverWrite`/`g_bMetalicOverWrite` replace
  the channel with `g_Glossiness`/`g_Metallic`. They become `_orm` sidecars (R occlusion, G roughness, B metal).
  Terrain layers get their own pair. rd-vulkan finds sidecars only on lit stages, so under `--vertexlit` the terrain
  stages are `rgbGen exactVertex` (light in RGB, blend weight in alpha) instead of a trailing lightmap stage.
- **Picking a NieR area**: `.codex-research/levelport/nier/survey/` has every world tile's bounds, mesh groups and
  materials (`tiles.json`, `survey.py`) and a map classified by content (`tile_map.png`). `wd1/g1XXYY` is the open
  world on an XX/YY grid (desert west, factory north-west, camp g11420, Flooded City g115/g116 19-21 east of it,
  amusement park south-east, forest south); `wd3` repeats the city-ruins tiles after the collapse; `wd2`/`wd4` are
  merged distant LODs; `wd5` holds interiors (Copied City). A tile has its own origin, about 200 x 177 m (7,500 x
  6,600 units): one tile is a whole JKA map.
- **Far scenery**: some tiles carry 10 km sea planes or skyline sheets. `nier2gltf --margin` (default 60 m) drops
  render triangles beyond the tile's collision; without it the sky box outgrows the world.
- **Water** (Wtr00 pools, Wtr02 sea) has no albedo: colour, fog colour and scrolling normal maps. It becomes a
  nearly opaque dark surface in NieR's under-surface fog colour with its normal map and low roughness, reflecting
  `misc_cubemap` probes placed over the water (`lp_water`; design in the NieR notes), plus a water-contents brush
  under each flat surface so players swim (verified: swim pose under the surface).
  Flat water is rebuilt as a 3 m grid cut to the region with world-unit UVs (a ripple every 6 m: the sea's own UVs
  give one per 50 m, which reads as a green lawn); the grid carries gentle `deformVertexes wave`s. Collision
  type 88 (`WCOL`) is the water surface's own plane: dropped, or players walk on the sea. Wtr01 is foam
  (waterfalls): additive at 0.3, scrolling. Spawns are never under water.
- **Layout.lay** (v2.01): model id list, 0x70-byte instance records (name[32], position, Euler rotation, scale,
  child count at +0x6C), then 36-byte child transforms in world space; a record with children places its model at
  its own transform and each child's. Prop models are `bg/<id>`, `ba/<id>` in the CPKs (any data0NN). The camp
  places 200 props from 22 models (Lunar Tear beds, trucks, camp units, beds, lamps).
