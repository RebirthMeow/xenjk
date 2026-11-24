# Shape collision: the game's own shapes as the world's collision (design, 2026-11-24; not built)

Owner, 2026-11-24: "i do not feel beholden to bsp, and invite better ways to do stuff". This proposes a world collision
format built directly from a source game's physics shapes, traced by the engine without a q3map2 compile. Beauclair
(Witcher 3) is the first user; Night City's CP2077 shapes fit the same format.

## 1. Why

What the BSP still does for a level port, now that `rd-vulkan` draws the scene from its own packs and maps compile with
`--vis none`: collision, contents (water, lava, fog), surface types (footsteps, impacts), entities, inline models for
movers and triggers, and leaf/area bookkeeping for entity linking and snapshots.

What it costs, measured on our own ports:

| | Castle (`w3_castle`, Install 85) | City (`w3_city-2`) | Night City (Watson tag 2) |
|---|---|---|---|
| Game shapes | 5,184 | 68,260 | |
| BSP brushes | 108,082, of which 4,981 are the shapes and **103,101 slabs** (terrain and triangle meshes) | 1.76 M brushes, ~20 M planes | 82 chunks, **5.46 GB** in memory, the largest 3.1 M planes |
| Compile | 60 s | 12 min, in tiles | per chunk, tiled |
| Terrain | slabs every 2 m | slabs every 4 m (2 m would be ~700 k slabs) | |

The source data is small: the castle's terrain is 16 heightmap tiles of 257 x 257 (5.2 MB as stored by `w3terrain`),
and its shapes are mostly boxes. The BSP turns that into ~20 times as many brushes and approximates the parts that
matter most for feel (stairs, slopes, ground) with slabs or wedges (the Kabuki stairs, `cp77-kabuki-levelport`).

## 2. The census (castle, remaster 5.0, `run2-20261124/work-castle/collision.json`)

| Shape | Count | Faces | Notes |
|---|---|---|---|
| Box | 3,816 | 6 each | exact as a brush |
| Convex hull (`_col`) | 977 | median 46, at most 223 | exact as a brush |
| Triangle mesh (`_tri`) | 208 | 63,369 triangles, at most 921 a shape | floors, walls, stairs: slabs today |
| Capsule | 125 | tessellated, 128 faces each | the add-on's import tessellates; radius and length are not kept |
| Sphere | 58 | tessellated | the same |

Materials: `stone_solid` 4,739, `glass` 154, `clay_tile` 126, `flesh` 73, `wood_solid` 65, then `dirt_hard`, `carpet`,
`stone_debris`, `metal`, `dirt_soil`, `rubber`, `default`. Bounds about 245 x 230 x 167 m. The city census follows
when its extraction finishes.

## 3. What in the engine depends on the BSP

From a survey of `codemp/` (file:line references in the survey's notes; summary here):

- **Every collision query goes through `cm_*`**: `CM_BoxTrace`, `CM_TransformedBoxTrace`, `CM_PointContents` and
  friends (`qcommon/cm_public.h`). Player movement, weapons, the saber against the world, cgame prediction and Bot2 all
  reach the world only through these. **Bot2 navigates by its own graph built from traces** (`bot2_graph.c`), not AAS.
- **Chunks already exist** (`qcommon/cm_chunk.cpp`): `CM_BoxTrace` traces the hub map, then every candidate chunk in
  its own frame (`CM_TraceChunks`), merging by fraction; point contents likewise. Chunks carry collision only; the hub
  keeps entities, leaves, clusters and areas. Residency streams chunks on a worker thread, and a missing chunk is solid.
- **Surface types** come from the hit's shader (`trace_t.surfaceFlags`, material in the low 5 bits): footsteps, landing
  damage, slick, impact effects, no-impact. Whatever replaces brushes must still return a shader per hit.
- **Contents**: water, lava, slime and fog are volumes found by point contents and traces.
- **Entities and inline models**: the entity string (`CM_EntityString`), and `*N` brush models for movers, doors and
  triggers (`SV_SetBrushModel`), which the server traces as entities, never through chunks.
- **Leaves and areas**: `SV_LinkEntity` files every entity into leaves; snapshots test clusters and areas. With
  `--vis none` the PVS is all-visible but the leaf lookup still runs.
- **Decals** (`R_MarkFragments`) walk the renderer's BSP surfaces, not collision; a scene-pack map has no world
  surfaces there today, so this design changes nothing for marks.
- Not BSP-dependent: sound, G2 collision (entities only), botlib AAS (off; Bot2 does not use it).

## 4. Design

(As first proposed. Section 7 records the review and what it changed: no patch-facet stage, bevelled solids, a
BSP-lump writer first, volume semantics for triangles and terrain. Where they differ, section 7 holds.)

**A hub plus shape chunks.** Keep a tiny BSP as the hub: one box around the map, the entity string, the inline models
for movers and triggers, a single leaf and area. Everything the server's entity linking, snapshots and movers need
stays exactly as today. The world's collision moves into **shape chunks**, a second kind of chunk beside the BSP chunks
`cm_chunk.cpp` already streams, dispatched by the same candidate grid and merged by the same fraction rule. Nothing
above `cm_*` changes: no game, cgame, Bot2 or interface change.

**A shape chunk** (`.xcol`, one per tile of the existing world-origin grid; versioned header, fixed-width records):

- **Material table**: per entry the JKA shader name, contents and surface flags. Physics materials map to JKA surfaces
  once per game (the city build already chose surfaces for W3's materials).
- **Convex solids**: boxes, hulls, and the water/lava volumes as planes. At load they become `cbrush_t` in the chunk's
  clip map, so `CM_TraceThroughBrush` and point contents work on them unchanged. Exact, as today, but with no
  compile and no slabs around them.
- **Triangle meshes**: vertices and triangles, traced as triangles. The first version can reuse the patch path:
  `cm_patch`'s facets are planes with border planes, which a triangle is, so `CM_TraceThroughPatchCollide` traces them
  today. A dedicated box-against-triangle sweep (separating axes) is the later, faster form. Triangles are surfaces,
  not volumes: point contents ignores them, as it ignores patches.
- **Heightfield terrain**: the source game's height grid as is (spacing, origin, heights, a hole mask, a material per
  cell or per layer), traced by walking the cells under the swept box and testing their two triangles each. Exact to
  the game's terrain at any resolution, a few MB per city.
- **Capsules and spheres**: as convex brushes of a fixed tessellation first; analytic later if it shows. Needs the dump
  to record the analytic parameters (the add-on tessellates them today).
- **A bounding-volume tree per chunk** over all of the above, so a trace tests only nearby shapes. No planes beyond
  the solids' own, no BSP tree, no leaves.

**Writing it** is a Python tool reading the work folder (`collision.json`, `terrain/tiles.npz`) and the build box: no
compile. The castle should write in seconds.

**Frames.** Chunks keep whole-unit origins and their own frame, as BSP chunks do, so the precision rules of
`docs/levelport-collision.md` (sections 14-15) carry over.

## 5. Gates (fixed before any code)

On the castle, against the exact geometry (double precision, from the work folder) and against the installed
`w3_castle` BSP, with `engine_trace` sets (straight, random, player-box, point contents):

1. **Boxes and hulls**: no worse than the BSP against the exact geometry; contents identical.
2. **Triangle meshes and terrain**: closer to the exact geometry than the BSP's slabs (endpoint error, wrong
   start-solid, missed hits), reported per kind.
3. **Surface types**: every hit's material equals the shape's mapped material.
4. **Feel**: the Kabuki-style stair walk and a spawn gate pass; bots on the map still build their graph and run.
5. **Cost**: trace time per 10,000 traces no worse than the BSP's; memory and load time reported.
6. Tie and touching-start rules as in `chunk-collision-tie-rule` (flag differences on exact ties do not fail).

## 6. Steps

1. **City census** once the extraction is done: shape kinds and triangle counts for all 68 k shapes, terrain extent.
2. **Writer and loader, solids only**: `.xcol` with the material table and convex solids, loaded as a shape chunk; the
   castle's boxes and hulls through gate 1, 3 and 5.
3. **Heightfield**: the castle's terrain; gate 2 for terrain.
4. **Triangles**: patch-facet form first; gate 2 for triangle meshes; then gate 4.
5. **The city**, tiled, through residency.
6. Later: the dedicated triangle sweep, analytic capsules, Night City's shapes into the same writer.

## 7. Review round 1 (2026-11-24): Codex Astra 6.2 high and Fable 5.2 high

Reports: `.codex-research/shape-collision-review-20261124/` (`CODEX_ASTRA_REPORT.md`, `FABLE_REPORT.md`). Both
approve the direction (source shapes, no q3map2, native terrain) and a prototype; both reject the implementation
order in sections 4 and 6. Claims checked in the code before adoption: the patch sweep never sets start-solid
(`cm_patch.cpp`, no write of `startsolid`/`allsolid`) and never clips a facet's last border (`cm_patch.cpp:1503`);
brushes test sides from 6 on after the bounds (`cm_trace.cpp:158`, the implicit-axial contract `cm_local.h:84`);
point contents returns 0 on a node-less map (`cm_test.cpp:246`); `CM_TraceInMap` returns on a node-less clip map
(`cm_trace.cpp:1235`).

**What changes in the design** (both reviewers agree unless marked):

1. **No patch-facet stage.** Patch collide is built from quadratic grids, ignores its last border as the back face,
   reports no start-solid (a mover can push a player into a mesh wall and the push is accepted; items and Bot2's floor
   checks read clear), uses zone/hunk scratch the chunk worker may not touch, and drops at 4,096 planes (the 921-triangle
   mesh). Triangles get a dedicated box-triangle sweep instead.
2. **Solids need bevels, in the loader's contract.** The six axial bound planes first (the implicit-brush contract),
   then the faces, then edge-cross-axis bevels, in the placed orientation, in double precision. Without them a rotated
   box stops a player about 15 u short of its corners. Bevels take the brush's own shader (a `CM_SIDE_BEVEL` side with no
   bevel shader indexes `shaders[-1]`).
3. **Step 2 becomes a direct BSP-lump writer, with no engine change.** A Python tool writes ordinary collision-chunk
   lumps: bevelled brushes for boxes and hulls, an axial tree as nodes and leaves, one cluster. It removes q3map2 for
   solids, reuses the chunk validator, worker, compression, memory accounting and every trace convention, and is the
   control against which a new runtime format must prove its value.
4. **Triangles and terrain must behave like volumes.** Start-solid on overlap, all-solid when still overlapped, the
   engine's epsilon rule (fraction `(d1 - 0.125)/(d1 - d2)` on the hit plane, clamped), unit face normals, capsule input
   treated as the box path treats it. Fable: give each triangle a back thickness (about 4 u); give each terrain cell
   triangle an implicit prism (about 64 u deep, built on the fly like the implicit axial planes), so caves under holes
   stay open.
5. **No ghost edges.** The writer welds vertices and records each edge's neighbour and convexity; the sweep ignores
   edge contacts on coplanar or concave shared edges, so a slide along a floor or wall made of many triangles never
   catches on a seam.
6. **Terrain search** walks the full swept footprint (not the centre line), front to back, with a min/max height
   pyramid per tile, stopping when the next cell starts after the best hit. Recover the source's cell diagonal, hole
   granularity and the material rule (per vertex in the source).
7. **Chunk housekeeping.** A node-less shape chunk needs its own dispatch, validator (no `Com_Error` on the worker)
   and tracked allocations; one chunk kind per tile, since an unloaded slot is a solid box over the other kind's
   geometry; the hub keeps real nodes, a leaf with a cluster and area, and bounds spanning the playable world (Astra:
   keep the existing hub topology for the first trial and measure snapshots before simplifying).
8. **Gates fixed before code, extended:** corner-graze and zero-length position-test sets for solids; a penetration
   soak (long pmove and bot runs asserting no overlap or fall-through); slide sets counting non-face normals; a fixed
   query mix reported at median, p95, p99 and worst, cold and warm load, peak memory, on the 2-vCPU target; prediction
   on 32- and 64-bit clients; (Astra) two references, the exact geometry and the engine-visible contract, and a two-tile
   mixed-format fixture with eviction and a remote client before the whole castle.
9. **Extended census** before freezing a format: two-sided surfaces, negative-scale instances, per-triangle materials,
   open versus closed meshes, the source-shape-to-output manifest, the analytic capsule and sphere parameters, the
   city's triangle count.

Alternatives they raised: a thin prism brush per triangle in BSP lumps (no engine code; about 300 B a triangle, 19 MB
for the castle, too large for Night City); selective convex decomposition for closed props and awkward stairs; a
physics library for world queries (Jolt: shape casts, heightfields, active edges) at the cost of a dependency and
matching the engine's conventions.

**Revised steps:**

1. Extended census (castle now, city when its extraction finishes).
2. Contract fixtures: small adversarial trace sets with both references, fixed before code.
3. The BSP-lump writer for bevelled solids; gates on the castle's boxes and hulls against the installed BSP.
4. The triangle sweep, spiked on the castle's stair and floor meshes with the slide, position-test and penetration
   sets, before any loader work.
5. The heightfield (prisms, footprint walk, pyramid).
6. The shape-chunk loader and dispatch; the two-tile fixture with eviction and a remote client; then the castle, then
   the city.

## 8. Step 3 done first: q3map2's brush stage in Python (2026-11-24; uncommitted)

Owner, 2026-11-24: "if we can do something easy like drop q3map2, no engine change, and get most of the benefits,
then lets do that". The cooked collision path (`gltf2map --col-chunk --cook`, `colcook.py`) already built chunk BSPs
in memory with our own tree; its one q3map2 step was `-brushbin` (planes, windings, box and edge bevels, shader
flags per brush). `tools/levelport/colbrushes.py` reimplements that step in numpy, matching xenjk9's arithmetic and
order; `gltf2map --cook --cook-python` builds a chunk with no q3map2 process (`--cook-gate` runs both and compares).

| Input | Brushes | Sides | Planes | Python against q3map2 |
|---|---|---|---|---|
| W3 castle (`run2-20261124/work-castle`) | 57,375 | 1,109,070 | 1,773,556 | tile arrays, chunk BSP (29,711,964 B) and pack identical |
| W3 castle area with terrain (`remaster-check-20261124/work`) | 271,370 | 3,604,348 | 3,536,486 | identical (79,263,048 B) |

Checked independently: the two packs' members hash the same, and the Python build's log names no q3map2. Builds in
`<levelport root>/levelport/build/colpy-castle` and `colpy-remaster`.

What it does and does not buy:
- **No q3map2 in the collision build**, with byte-identical results: no gameplay risk, no engine change.
- **Speed: on a par with q3map2 after two converter fixes** (profiled 2026-11-24; the castle was 104 s on a quiet
  machine, 60 s of it `q3bsp.brush_polygons` for the 4,981 box and hull shapes on one core, 12 s opening ~900 packs'
  directories for the shader table on every build because the stage's generated shaders changed the cache key).
  Now the polygons run on the conversion's process pool (`_PendingBrush`, finished in queue order) and the packs'
  shader index is cached apart (`colbrushes.pak_index`, keyed by the packs alone). Castle 104 s to about 34 s; castle
  with terrain 38 s against 29 s through q3map2 (the Python brush stage 20.5 s against q3map2's 12.4 s). The pool
  registers the generated collision shaders in another order (`c0`/`c1` swap places, flags unchanged), so builds
  differ by name from earlier ones; Python and q3map2 on the new converter are byte-identical again on both inputs.
  The 12-minute city compile was the older text path; the cooked path had already removed most of that.
- **Not more exact.** Stairs and terrain are slabs as before. The exact triangle and terrain work (section 7) builds
  on this.
- Not yet proven on data: shader-script features beyond the generated level-port shaders and GameData
  (`q3map_baseShader`, deprecation, damage), and q3map2's fatal-error paths (the Python raises instead).
- Still on q3map2: the hub map (entities, sky, light grid: `-light` lights characters), `colexpand.py`,
  `cp77_collision.py` (a one-argument change each).

## 9. Open questions for the owner

- **Hub map**: keep a minimal BSP for entities and movers (proposed; no game-code change), or move entities to a
  plain file too (more engine work, no BSP at all).
- **Where the game has no collision** (the city's ivy, creepers, flower beds: 7,156 m² of fall-through in `w3_city-2`),
  keep the game's choice or keep the visible-floor fill we added (`levelcheck compare` → `w3build --fill`).
- **The clean-room engine**: this format is a candidate for its requirements doc once proven.
