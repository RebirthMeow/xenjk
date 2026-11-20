# NieR:Automata level conversion (2026-10-14)

These are notes on converting NieR:Automata world tiles into Jedi Academy maps with `tools/levelport`. The
general pipeline and its node convention are in [tools/levelport/README.md](../tools/levelport/README.md).
This file holds the NieR-specific findings, the current state of each map, and what is still open.

The converted maps are built from commercial game data. They stay local: they are never committed, installed or
published. Builds go to `.codex-research/levelport/` (ignored by Git).

## Status

| Map | Tile | Best build | State |
|---|---|---|---|
| Resistance Camp | `wd1/g11420` | `build/nier_camp-8` | Done for now. 200 props, PBR maps, gamma 1.6 |
| Flooded City | `wd1/g11520` | `build/nier_flooded-7` | Paused (owner played it 2026-10-15; installed, see below). Water is a good start; the level is not |
| Desert oasis | `wd1/g10619` | none | Not started |
| City ruins by the camp | `wd1/g11320` | none | Not started |

**Owner review of `nier_flooded-7` (played in game, 2026-10-15):**

- The water is "a really good start". The waves look good, and so does the sun on the water. The black is "so
  black", but "pretty accurate".
- The level itself is "still chaotic/bit of a mess". Some of the main standing areas did not make it, and there
  are many ways to fall into the water, out of the map. See Open issues 7 and 8.
- Level conversion is paused there ("done for now"). The build stays installed:
  - `GameData/base/zz_nier_flooded.pk3`;
  - the launcher `GameData/Play_NieR_Flooded.bat` (devmap on the local listen server).
- No engine, renderer or game code was changed for any NieR map. All of it is in `tools/levelport` (Python).

The owner reviews each map before the next one is built. The owner also asked that builds be run in the session
one stage at a time, with each result checked before the next stage starts (see Recipe). A single long command
that runs everything hides problems; that is how the Flooded City's 800 m size was missed.

`nier_flooded-1` to `-4` and `nier_oasis-1` are kept as failed or partial evidence:

- `-1`: the sky box overflowed.
- `-2`: the map came out 800 m across, and full vis would have taken about an hour.
- `-3`: waterfalls were too bright, and spawns were under water.
- `-4`: the sea read as a flat green lawn.
- `nier_oasis-1`: built while the code was being edited.

## Picking an area

The survey of all 221 world tiles is in `.codex-research/levelport/nier/survey/`:

- `survey.py`: the scanner.
- `tiles.json`: bounds, mesh groups, materials, shader counts and whether the tile has a prop layout.
- `table.txt`: one line per tile.
- `tile_map.png`: the grid, classified by content.

How the tiles are organised:

- `wd1/g1XXYY` is the open world on an XX/YY grid. It matches the game map:
  - Desert in the west (`g102`–`g110`). The oasis is `g10218` (a large far tile) and `g10619`.
  - Factory in the north-west (`g108`–`g110`, rows 20–24).
  - Resistance Camp: `g11420`.
  - City ruins around the camp (`g111`–`g114`).
  - Flooded City east of the camp: `g11519`, `g11520`, `g11521`, `g11619`, `g11620`.
  - Amusement park in the south-east (`g116`–`g119`).
  - Forest in the south (`g115`–`g118`, rows 11–15). The forest castle is `g11812`.
- `wd3/g31XXX` repeats the city-ruins tiles after the collapse.
- `wd2` and `wd4` are merged distant LODs. Do not convert them.
- `wd5` holds interiors (Copied City `g50010`/`g50100`, bunker, factory interior).

Each tile has its **own origin** and is about 200 × 177 m (≈ 7,500 × 6,600 units). One tile is already a full
JKA map. Combining tiles would need their world offsets, which are not decoded yet.

Mesh-group and material name hints: `bigdesert`, `sabaku` and `sand` mean desert; `oasis`; `suibotu` means
submerged; `shigeri` means overgrowth; `kousoku` is the highway; `MtRobot` is the factory; `polytree` and
`ohi` are forest; `dry`, `yam` and `building` are city ruins.

## Recipe (stage by stage)

```
python tools/levelport/nier2gltf.py g11520 --out .codex-research/levelport/nier/g11520-N.glb
python tools/levelport/gltf2map.py .codex-research/levelport/nier/g11520-N.glb nier_flooded \
    --out .codex-research/levelport/build/nier_flooded-N --sky yavin --sun "1 0.95 0.85 380 35 50" \
    --skylight 800 --worldspawn _ambient=35 --vertexlit --gamma 1.6 --no-compile
python tools/levelport/stage.py .codex-research/levelport/build/nier_flooded-N nier_flooded -bsp -meta
python tools/levelport/stage.py .codex-research/levelport/build/nier_flooded-N nier_flooded -vis -saveprt
python tools/levelport/stage.py .codex-research/levelport/build/nier_flooded-N nier_flooded \
    -light -fast -filter -patchshadows -bounce 1 -samples 2 -gamma 1.6
python tools/levelport/stage.py .codex-research/levelport/build/nier_flooded-N nier_flooded --pack "NieR:Automata Flooded City"
python tools/levelport/look.py .codex-research/levelport/build/nier_flooded-N/zz_nier_flooded.pk3 nier_flooded \
    --views 8 --cvar g_spawnInvulnerability=0 --set "setviewpos x y z yaw" --set "wait 400" --set "screenshot name"
```

What to check at each stage:

- **Export.** Read the stats line: `clipped_tris`, `props_outside`, `water_volumes`, `missing_textures`.
  Then check the glTF bounds: a single tile should come out at about 300 × 300 m including the margin.
- **`-bsp`.** Look at `Size:` in `q3map2.log` (the camp is about 9.6k units across, the Flooded City about 12.6k)
  and confirm there is no leak.
- **`-vis`.** Check the portal count: the camp has about 3.2k and the Flooded City 3.4k. If a map shows tens of
  thousands, its bounds are wrong.
- **`-light`.** It takes 7–10 minutes, longer than one foreground call allows. Run it in the background and wait
  in foreground chunks of at most 10 minutes.
- **`look.py`.** Pick camera positions from data, not guesses. A top-down plot of collision height, water and
  spawns is how the Flooded City's water was found (`look/nier_flooded-4/topview.png`).

## Format facts

- **Archives.** A tile's `.dat` holds:
  - the collision (`.col`, COL2);
  - the prop layout (`Layout.lay`);
  - the texture table (`.wta`);
  - other files still unread: `.rsc`, `.iws`, `.mwm`, `.caw`, `.pvd`, `.rcc` and similar.

  Its `.dtt` holds the model (`.wmb`, WMB3), the textures (`.wtp`) and `RNT.wtb`. Textures a tile lacks are in
  `wda/ga000N`. Prop models are `bg/<id>` and `ba/<id>`, spread across `data0NN.cpk`.
- **WMB3 vertex layouts.** These cover the map and prop flags 4, 5, 11 and 14.
  - The 28-byte core record holds the position, a packed tangent (+12), uv0 (+16, half2), uv1 (+20) and the
    vertex colour (+24).
  - The extra data starts with a half4 normal. For flag 14 it then has uv2 (half2) and a copy of uv0.
  - Indices are u32 and absolute within the vertex group. Winding is clockwise.
- **Material variables.** The 0x30-byte material record keeps its variable block at words 10/11 as (name offset,
  float) pairs. These variables decide the look, not texture alpha.
- **Layout.lay (v2.01).**
  - A list of model ids (4-byte entries).
  - 0x70-byte instance records: name[32], position, Euler rotation (radians), scale, and the child count at
    +0x6C.
  - 36-byte child transforms in world space.
  - A record places its model at its own transform and at each child's.
  - Rotation is applied as Ry · Rz · Rx, which is NieR2Blender's convention; placement was checked by eye only.
  - The camp has 200 placements of 22 models; the Flooded City has 33, of which 5 are dropped.
- **COL2 collision types.**
  - 127 and 255 are world and object collision (solid).
  - 3 is the tall invisible boundary walls (playerclip).
  - 1 is solid.
  - **88 (`WCOL`) is the water surface's own plane.** It is dropped: as solid it let players walk on the sea,
    and its extent widened the clip region.
- **Normal maps are DirectX.** The green channel was compared against each material's AO channel used as a height
  proxy: 20 camp textures agree, 1 disagrees, and the rest are too flat to tell. They are converted to OpenGL
  (green flipped).
- **Masks** are R metal, G glossiness, B AO. The `g_bGlossinessOverWrite` / `g_bMetalicOverWrite` flags replace
  the channel with `g_Glossiness` / `g_Metallic`.
- **Lighting can't be carried over.** The world is lit by precomputed GI (`RNT.wtb` holds directional atlases,
  not lightmaps). q3map2's sun, sky and bounce stand in, with `--gamma 1.6`. Using `-bounce 3` changed little and
  cost 5 minutes.

## Material policy (nier2gltf)

| NieR | Converted to |
|---|---|
| `g_Tile` (PBS00, Plt00, CNS00) | baked into UVs |
| PBS10/PBS11 terrain, three albedo layers | `lp_layers`, each with its `g_MaterialTile`. Weight: R = layer 2, G = layer 3; B and A are unknown and not converted. Compiled Quake 3 terrain style: one two-layer shader per triangle, the weight in vertex alpha (the `.glb` chunks are what keep vertex alpha through q3map2). Each layer gets its own `_n`/`_orm`. |
| `g_1BitMask` | alpha-tested cutout (any format, including BC1 punch-through). Other alpha is data and is drawn opaque; treating PBS10 layer alpha as a cutout punched holes in the camp floor. |
| `g_Decal` | alpha-blended decal, vertex-lit |
| CNS00 + `g_UseMultiplicationBlend` / `g_UseSubtractionBlend` | multiply decal |
| CNS00 otherwise | emissive (`lp_emissive`): glow plus a q3map2 surface light of 150 × `g_Intensity` |
| Wtr00 / Wtr02 (pools, sea) | `lp_water`, see below |
| Wtr01 (foam, waterfalls) | additive at 0.3, scrolling by `g_UvAnimation1` / 10 |
| normal + mask | `_n` / `_orm` sidecars that rd-vulkan finds by name; other renderers ignore them. Generated names are capped at 55 characters so the sidecar paths stay under 64. |

## Water and far scenery

- **Far scenery.** Some tiles carry a 10 km sea plane, far skyline sheets, or helper boxes (`bga100` is a
  427 m box at sea level).
  - `--margin 60` keeps render triangles only when all three vertices lie within 60 m of the tile's collision
    (type 88 excluded).
  - Prop placements that leave that region are dropped.
  - Without this, the Flooded City came out at 800 m with 34,510 portals, and vis and light took about an hour.
- **Flat water** (height spread under 5 cm) is rebuilt as a grid of 3 m cells cut to the region.
  - It uses world UVs with a ripple every 6 m. The sea's own UVs give one ripple per 50 m, which reads as a
    green lawn at player scale.
  - The grid is there for two gentle `deformVertexes wave`s (7 cm over 9 m every 4 s, 3 cm over 3.5 m every
    2.2 s). The waves were verified with a 15x test amplitude (`look/water-var/c`). They are visual only: the
    water brush stays flat.
- **Water shader (2026-10-15, owner picks: deep dark water, real reflections, gentle waves).**
  - Reference: in-game screenshots (NieR wiki, Flooded City gallery) show dark grey-green, nearly opaque water
    with fine ripples, no reflections and haze at distance.
  - NieR's own sea values: albedo (0.076, 0.106, 0.038) and an under-surface fog of (0.03, 0.042, 0.015) that
    its refraction fades to. rd-vulkan has no scene depth for translucent surfaces, so the converter draws the
    water nearly opaque (0.92) in that deep fog colour, used as is.
  - The single stage is vertex-lit (`rgbGen exactVertex`), so rd-vulkan's lit path applies.
    - `normalMap`: the water's own normal map, green flipped, scrolling.
    - `detailNormalMap`: the same map at NieR's second-layer tile ratio.
    - `specMap $whiteimage` with `specularReflectance 0.02` (water's F0) and `roughness` 0.05–0.15 (from
      `g_Glossiness1`).
    - The look is the reflection-probe reflection and the sun glints on a dark body.
  - Other renderers skip the material keywords and draw a flat dark surface.
  - Variants on build 6 (`look/water-var`):
    - (a) NieR's albedo, gamma-encoded: read as a green lawn, because q3map2's vertex light (sun, gamma 1.6)
      brightens it.
    - (b) near black: reads as deep water.
  - **Reflection probes.** `misc_cubemap` entities go on a 50 m grid over each water surface, 3 m up, only
    where the water is open from 1 m below to 10 m above (not inside buildings or under bridges). One more goes
    at each spawn, because any placed probe turns off rd-vulkan's spawn-point fallback. The Flooded City gets
    32 water probes and 8 spawn probes; `--no-probes` turns them off. Planar mirrors exist in rd-vulkan but are
    unsuitable here: one per view, they need a game entity, and PBR is stripped inside them.
- **Swimming.** A brush with water and `trans` contents goes under each flat water height. It reaches down to
  the tile's lowest collision. Swimming was verified (`look/nier_flooded-5`, `swim.jpg`).
- **Spawns.** The spawn picker samples up-facing type-127 ground. It requires 4 m of headroom against solid
  collision and stays above any water. The Flooded City's streets are under about 20 m of water, and earlier
  builds spawned players on the sea floor.

## Open issues

1. **Sky.** The stock `yavin` sky has a pink gas giant that NieR does not have. Options:
   - `desert`: a clear blue sky, which suits the oasis.
   - `hevil`: overcast.
   - A sky box built from NieR's own sky textures (not located yet).

   This needs an owner decision.
2. **Waterfall foam** shows round blotches. NieR fades foam with `g_GradationMap` and UV2 (`g_UV2Use`), neither
   of which is read yet.
3. **Underwater** has no fog, and past the region edge the sky box shows below the surface. A fog volume
   (`fogparms`) on the water brush would fix both.
4. **Spawns.** Some spawn views face walls, and one spawn landed among storage shelves in the camp.
5. **Not decoded:** the effect and light files, the `.rsc`/`.iws`/`.mwm`/... companions, `RNT.wtb`, and tile
   world offsets. Tile offsets are needed for multi-tile maps.
6. **Camp vs the owner's .blend** (`<godot helper>\levels\Resistance_camp-png.blend`, a hand-edited second
   reference that is not always available): its open floor reads brighter because the author swapped the
   concrete texture by hand and added 29 lights.
7. **Missing standing areas and open edges (Flooded City, owner).** Main places to stand did not make it, and
   the walkable area has open sides into the water and out of the map. Likely causes, not yet checked:
   - The city spans five tiles (`g11519`, `g11520`, `g11521`, `g11619`, `g11620`), and only `g11520` is
     converted. Its walkways continue into the neighbours, and multi-tile maps need the tile world offsets
     (issue 5).
   - NieR keeps the player in with collision this converter drops or does not read: COL types other than
     127/255/3, the water plane (88), and effect- or script-driven bounds.
   - Props outside the 60 m region are dropped (`props_outside` 5).
   First step: plot the collision by type against the render geometry, and compare with the game's own map
   (`Flooded_City_map.png` on the NieR wiki).
8. **Level reads as chaotic.** The single tile has floating far scenery, sunken buildings cut at the region
   edge, and no haze to hide either. Haze (issue 3), a better sky (issue 1) and choosing the map's bounds by
   design rather than by the tile grid would all help.
9. **Water darkness.** The owner judged the near-black accurate. If it is ever lifted, raise the 4x4 colour
   in `lp_water.color`; do not raise the opacity or brighten the albedo (that made the lawn look).

## Evidence

All paths are under `.codex-research/levelport/` unless absolute.

- Builds: `build/nier_camp-5` to `-8`, `build/nier_flooded-1` to `-7` (`-6`: first lit water, read as a lawn).
- Screenshots:
  - `look/nier_camp-5-ab`, `-6-ab`, `-7-ab`, `-7-gamma`, `-7-bounce3`, `-8`;
  - `look/nier_flooded-3` to `-5`;
  - the water shader test, `look/nier_flooded-4w`; water variants on build 6, `look/water-var` (a, b, c);
  - `look/nier_flooded-6`, `look/nier_flooded-7`; `compare/water_5_vs_7.jpg`.
- Comparison sheets in `compare/`:
  - `before_after_build6.jpg`, `ab_6_7.jpg`, `lighting_variants.jpg`, `camp_now.jpg`;
  - the .blend renders, `blend_*`.
- Earlier material survey: `.codex-research/converter-gap-20261014/nier/`, which holds `scan_all.json` (192
  tiles' material variables), `shader_vars.json` and `rnt_sheet.png`.
- Research probes from this session are in the session scratchpad and are not needed. The findings above
  replace them.
