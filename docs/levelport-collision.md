# Level-port collision at district size (phase 2, 2026-10-31)

Phase 2 of the CP2077 Watson port ([levelport-watson.md](levelport-watson.md) section 4): make the gameplay collision
map for the whole district fit the BSP format and the engine. This is the record of what was measured, what was
chosen and how it was gated. Evidence, scripts and logs: `.codex-research/levelport/look/collision-20261030/`
(local only, like every converted map); its regenerable maps and BSPs (18.4 GB, the synthetic district's mostly) are
in `.codex-trash/20261031T0438-levelport-collision-bulk/` (`python tools/research_shed.py --restore
20261031T0438-levelport-collision-bulk`).
More of its regenerable bulk (the synthetic district, measurement 1's maps, the gates' map copies, the census exports
and client-home pk3 copies) went on 2026-10-31 to `.codex-trash/20261031T094506-levelport-collision-bulk/`, and
superseded builds to `20261031T094438-levelport-retain/` (`research_shed.py --research .codex-research/levelport
--restore <stamp>`); results, corpora and logs stayed.
Builds: map-24 to map-30 are `build/cp_kabuki_col-24` to `-30`, slice-8 to slice-12 `build/cp_watson_slice_col-8` to
`-12` (renamed from `cp_kabuki-<n>` / `cp_watson_slice-<n>` on 2026-10-31, the tile runner's names); 25, 26, 28, 8
and 9 are failed attempts with nothing in them.

## Result

**q3map2 compiles tiles, we merge them.** gltf2map now compiles a world of more than 500,000 brushes as q3map2 tiles
of at most that many (a tile of 360,000 brushes needs 2.7-3.0 GB), and `tools/levelport/colmerge.py` joins them into one
BSP: q3map2's brushes, sides, bevels and planes as it compiled them, under a tree of our own. Every plane is stored
once (exact: nothing moves). Smaller worlds still compile in one q3map2 run, then get the same plane pass.

| | Kabuki | Slice | Synthetic district (the slice x 6) | Watson, projected |
|---|---|---|---|---|
| Brushes | 240,035 | 959,253 | 5,755,488 | 5.7-6.1 M |
| Compiled as | 1 run (4 tiles to gate the path) | 2 tiles | 16 tiles | 16 tiles |
| BSP, before -> after | 78.4 -> 61.7 MB (one run) / 63.6 MB (tiles) | 325 -> 251 MB / 256 MB | 1.38 GB (one q3map2 run would need about 40 GB) | 1.5-1.6 GB |
| Collision bytes a brush | 312 -> 243 / 251 | 335 -> 259 / 263 | 239 | about 263 |
| Brushes a trace tests (mean) | 3,599 -> 171 | 2,742 -> 150 | | |
| Gate | passed: map-27 equal to map-23 in every check; map-30 (tiles) the same but touching starts | passed: slice-10 equal to slice-7; slice-12 (tiles) the same but touching starts | loads in the 64-bit engine; the slice's traces answer the same in all six copies but the known causes | |

Phase M had concluded that one district compile was feasible (time: about linear). Memory was not measured then, and
it decides it: q3map2 holds the `.map` text and about 7.6 KB a brush while it compiles, so the district (about 6 M
brushes) would need about 40 GB in one run, on a 32 GB machine other sessions share (section 1).

## 1. Why not one q3map2 run

Measured on Kabuki (240,029 brushes, 207 MB `.map`) and the slice (959,222 brushes, 880 MB `.map`), patched q3map2
`-bsp -meta`, 4 threads (`memcurve.ps1`: the process's private bytes every 0.5 s):

| | Kabuki | Slice | One tile of 480 k (slice) | Synthetic district (section 6) |
|---|---|---|---|---|
| `-bsp` time | 22-28 s | 177-186 s | 44-50 s | 16 tiles of 360 k: 34-92 s each, 774 s in all |
| q3map2 peak memory | 1.77 GB | over 4.6 GB (stopped at 135 of about 186 s: the machine was at 1.5 GB free) | not sampled | 2.66-3.01 GB a tile (about 7.6 KB a brush) |

At 7.6 KB a brush, one run over the synthetic district's 5.76 M brushes would need about 40 GB.

The slice's curve: the `.map` read whole (0.88 GB), then about 3.1 KB a brush while brushes are parsed, a little freed,
then more while they are filtered into the tree, still rising when it was stopped. A first measurement this session
reported 1.95 GB for the slice: the sampler's `[math]::Max` took its 32-bit overload and dropped every value over
2 GB; Kabuki's 1.77 GB is below that and stands.

Two file limits also stop one run, and both are patched (section 4):

- **The `.map` read.** q3map2 sizes its read of the `.map` with `fseek` and `ftell`, which take a 32-bit `long` on
  Windows: on a file past 2 GB the seek fails, the length reads 0, and q3map2 parses an empty map and crashes in
  LoadMapFile (the synthetic district's 5.4 GB `.map`; the slice's `.map` is 918 B a brush).
- **Its own BSP.** q3map2 writes every plane with its flipped twin: at the slice's 339 B a brush the district's BSP
  would be about 2.05 GB, within 5% of the format's 2 GB (lump offsets are `int`).

**Why q3map2 tiles and not our own writer from scratch:** q3map2 per brush is what we would otherwise have to
reproduce exactly (the bevels a box trace needs, 53% of the sides, phase M; its plane snapping), and a tile of
360,000 brushes compiles in under a minute and a half in at most 3 GB. The part that does not scale, its tree, is
the part we replace. For these all-detail maps q3map2's tree is just its 1,024 u block grid: one Kabuki leaf holds 7,509
brushes, one slice leaf 16,858, and a trace tests every brush of every leaf it touches.

## 2. Bytes: one plane per orientation used

What one brush costs (`tools/levelport/colbsp.py stats`; collision lumps: planes, nodes, leafs, leaf brushes,
brushes, sides):

| | Kabuki map-23 | Slice-7 |
|---|---|---|
| Sides a brush | 11.84 | 12.19 |
| Planes a brush, as q3map2 writes them | 9.56 | 10.59 |
| Collision bytes a brush (file) | 312.2 | 335.4 |
| Resident on x64 (cm structures) | 434 | 461 |

The sides are fixed by the format (12 B each) and by the bevels. The planes are not: the engine binds each side and
node to its own plane (`cm_load.cpp` CMod_LoadBrushSides / CMod_LoadNodes) and never reads the flipped twin
`planeNum ^ 1`; rd-vulkan reads node planes and fogs' first six sides only (`tr_bsp.cpp`), so about half of q3map2's
planes are never used (Kabuki: 1.25 M of 2.30 M). The third q3map2 patch writes only those; `colbsp.py slim`
(run by gltf2map after the last q3map2 stage) does the same to any BSP and merges exact duplicates. Brushes, sides
and the tree are untouched.

| Kabuki map-23 | planes a brush | collision B a brush | x64 resident B a brush |
|---|---|---|---|
| q3map2 | 9.56 | 312.2 | 434 |
| one plane per orientation used (exact, shipped) | 5.23 | 242.8 | 347 |
| + box bevels snapped outward 1/8 u | 4.96 | 238.5 | 342 |
| + box bevels snapped outward 1 u (phase M's target: about 230, 4.6) | 4.58 | 232.5 | 334 |

**The bevel snap is not free, so it is off.** Phase M proposed moving each box bevel (an axial side q3map2 adds)
outward to a whole unit so brushes share them. The brush's own faces still bound it, so its solid is the same, but a
box trace grazing its extreme vertex then stops up to that much early. Measured offline on every Kabuki fall-through
surface (`snap_effect.py`: 28,117 drops of the player box with levelcheck's drop model, against the exact planes):
the 1 u snap changes 701 (2.5%: 433 rests move, median 0.5 u, a few boxes now catch on a corner they slipped past;
268 starts or stuck results change); the 1/8 u snap still changes 175 (0.6%). In the full checks the 1 u build
(map-24) moved a few m2 between drop results (falls 120 -> 118, holds 459 -> 456, blocked 191 -> 194) and 8 of
900,643 solid intervals by up to 0.02 u. The exact pass changes nothing, for 5% more bytes than the snap; the target
assumed the snap was free, and the district fits without it (section 6). `--slim-snap 1` stays available.

## 3. The tiled compile and the merge

`colmerge.py split` cuts the worldspawn's brushes into tiles of at most `--tile-brushes` (each half of a tile at the
median of its brushes' centres along its longer side): `base.map` holds the worldspawn keys, the sky shell and every
other entity; each `tile_<k>.map` its brushes and a copy of the sky shell (q3map2 needs a structural brush to size its
tree: an all-detail map fails with "Backwards tree volume"). Each part is compiled by q3map2 `-bsp -meta`, one at a
time. `colmerge.py merge` then writes one BSP:

- shaders by name and flags, planes by value, brushes and their sides in part order (the base first, so its fogs and
  draw surfaces keep their numbers; the tiles' copies of the sky shell dropped);
- one model; the base's draw surfaces (the sky shell's), entities and fogs;
- a new tree: axial splits at whole units, at the median of the brushes' centres along whichever axis divides them
  best, until a leaf holds about 48 brushes or is 256 u across. A leaf lists every brush whose bounds (from its first
  six sides, as `CM_BoundBrush` reads them) come within 1 u of each split that bounds it, so a point or a box in the
  leaf meets every brush it could touch (the engine counts a point on a face as inside; both neighbours of a split list
  a brush that touches it). Leaves are cluster 0 and area 0 (no vis: the engine and the renderer treat every leaf as
  visible); the sky surfaces are listed in the leaves their bounds touch.

Then q3map2 `-light` runs on the merged BSP as before (its grid traces see draw surfaces, not collision brushes; its
point-in-solid tests walk the tree) and the plane pass. A first version of the tree split only along the longest
axis: under the slice, a box 32,000 u tall with its 143,649 ground brushes bunched near its top never divided, one
leaf held them all, and `-light` took 326 s instead of 112 s. It now tries every axis.

**Trace cost** (`treecost.py`, offline: the distinct brushes the engine tests on each of the 5,000 engine drop
traces, walking the tree as `CM_TraceThroughTree` does, from the start to where the engine stopped):

| Brushes tested a trace, mean / median / p99 | q3map2's tree | merged tree |
|---|---|---|
| Kabuki | 3,599 / 3,357 / 9,578 | 171 / 131 / 668 |
| Slice | 2,742 / 2,041 / 13,505 | 150 / 105 / 751 |

About 20 times fewer brushes a trace. Not timed in the engine; the count is what a trace's time follows (each listed
brush gets at least its bounds test).

## 4. What changed

- **q3map2** (`tools/levelport/q3map2/large_maps.patch`, the third patch; build in
  `../Xenmap/.tools/netradiant-custom-20260114-xenjk3/`, the two-patch build left as it was for builds in flight):
  the `.map` measured with `_fseeki64` / `_ftelli64` and read in 1 GB pieces; the BSP written with only the planes a
  side or node uses (`CompactPlanes` in writebsp.cpp, original order). On Kabuki every lump but planes, nodes and
  sides is identical to the two-patch build's, and after `colbsp slim` the two are byte for byte the same.
- **colbsp.py** (new): `stats` (the tables above for any RBSP) and `slim` (exact by default; memory goes with the
  distinct planes, not the side references; written streamed).
- **colmerge.py** (new): `split` and `merge`, section 3.
- **gltf2map.py**: `--tile-brushes` (default 500,000; 0 keeps one q3map2 run), `--slim-snap` (default 0, exact;
  `--no-slim` keeps q3map2's planes), the three-patch q3map2 first, and the `.map` written piece by piece instead of
  joined into one string (the district's would be 5.4 GB, held twice; the streamed file is byte for byte the old one
  on Kabuki and the slice).

## 5. Gates

As section 4 of the Watson doc sets them: the same results as the map before in `levelcheck.py compare`, `spawns` and
`drop --engine 5000` (engine traces through the x64 `levelcheck_trace` game module, seed 0), bytes and planes a
brush at the phase M target, Kabuki first, then the slice. Each new map was built by gltf2map from the old one's glb
and arguments; `gate.sh` runs the checks as `cp77_tiles.py` does, the engine run through the shared game lock. The
old map's checks were re-run with today's levelcheck (`gate-kabuki/map23`; the older `look/compare-kabuki23-vec`
predates the canopy and content-flag fixes). Two kinds of build were gated: one q3map2 run with the exact plane pass
(how every map under 500,000 brushes now compiles) and the tiled path (the district's), forced onto Kabuki with
`--tile-brushes 70000`.

Because the tiled path brings a new tree and the random drop sample can differ with a cell or two of the compare,
it was also checked trace for trace: the old map's own 5,000 engine traces replayed in the engine on the new map
(`district_load.py`), each compared for end, fraction and start-in-solid.

| Kabuki | map-23 (q3map2) | map-27 (one run, exact planes) | map-30 (4 tiles, merged) |
|---|---|---|---|
| `.map` | | byte for byte map-24's (old writer) | the same |
| Collision lumps | | map-23 + `colbsp slim`, byte for byte | 6 sides fewer over 240,035 brushes (q3map2 per tile), new tree |
| Spawns stuck | 0 of 16 | 0 of 16 | 0 of 16 |
| Compare: matched, buried, missing | 506,522, 143,395, 28,117 | the same; every array equal | 506,525, 143,387, 28,122 |
| Invisible tops | 56,329 | the same | the same |
| Drop m2: falls, void, holds, covered, low, blocked | 120, 2, 459, 2,035, 163, 191 | the same, every cluster | 120, 2, 459, 2,035, 164, 191 |
| Engine traces agree | 4,991 of 5,000 | 4,991, the same 9 | 4,987 |
| map-27's 5,000 traces replayed | | | 4,980 identical; every one of 1,072 landings identical |
| Planes a brush | 9.56 | **5.23** | 5.32 |
| Collision bytes a brush (file) | 312.2 | **242.8** | 250.5 |
| Resident on x64 | 434 | 347 | 354 |
| BSP / pk3 | 78.4 / 23.5 MB | 61.7 / 18.6 MB | 63.6 / 19.2 MB |
| Compile (gltf2map, all stages) | 127 s (map-23's log) | 162 s | 160 s |

| Slice | slice-7 (q3map2) | slice-10 (one run, exact planes) | slice-12 (2 tiles, merged) |
|---|---|---|---|
| `.map` | | byte for byte slice-7's | the same |
| Collision lumps | | slice-7 + `colbsp slim`, byte for byte | 56 sides fewer over 959,253 brushes, new tree |
| Spawns stuck | 0 of 16 | 0 of 16 | 0 of 16 |
| Compare: matched, buried, missing | 3,090,059, 642,471, 84,676 | the same; every array equal | 3,090,060, 642,469, 84,677 |
| Invisible tops, water beds | 384,128, 122,112 | the same | 384,130, 122,112 |
| Drop m2: falls, void, holds, covered, low, blocked | 439, 133, 1,530, 4,672, 508, 578 | the same, every cluster | the same within 0.2 m2 |
| Engine traces agree | 4,983 of 5,000 | 4,983, the same 17 | 4,989 |
| slice-7's 5,000 traces replayed | | | 4,978 identical; every one of 1,470 landings identical |
| Planes a brush | 10.59 | **5.78** | 5.83 |
| Collision bytes a brush (file) | 335.4 | **258.5** | 262.7 |
| Resident on x64 | 461 | 365 | 370 |
| BSP / pk3 | 325 / 103.5 MB | 251 / 81.3 MB | 256 / 82.8 MB |
| Compile (gltf2map, all stages) | | 773 s | 652 s |

slice-12's random engine sample held three starts where the engine and the offline model disagree on a landing;
slice-10 (q3map2's tree) answers all three the same way as slice-12 (`three/`).

The disagreements with the offline drop model are the exact-touch starts section 11 of the Watson doc describes (the
model calls the start inside, the engine's trace cannot move: fraction 0), and one start the engine calls inside a
brush while the model calls it free, which q3map2's own tree answers the same way (traced on map-27 by hand:
start in solid, end 272.125, fraction 0.000031). In the replays, every difference is a trace that starts touching a
surface (fraction 0 on both maps) with its start-in-solid flag flipped: the engine stops at the first brush that
blocks at fraction 0, so which touching brush it tests first, set by the leaf order, decides the flag. Movement is the
same.

**Against the phase M target** (about 230 B and 4.6 planes a brush): the exact planes reach 242.8 B and 5.23 planes on
Kabuki in one run, 250.5 B and 5.32 tiled (the tree's leaf lists repeat brushes that cross a split, and tiles share
fewer planes). The target assumed the bevel snap was free; measured, it is not (section 2), and the 5% it would save
does not decide whether the district fits (section 6).

## 6. The district

Watson's collision is not built yet (phase 1's tiles run is under way in another session). To test the path at its
size, the slice's `.map` was repeated six times, 3 x 2 copies 28,000 u apart (`tile_map.py`; 5,755,488 brushes, a
5.4 GB `.map`, a new sky shell around them all), and built as gltf2map's tiled path does (`district5.sh`,
`district6.sh`):

| Stage | |
|---|---|
| split into 16 tiles of 359,717-359,718 brushes | 15 min (Python over the 5.4 GB `.map`) |
| q3map2 `-bsp -meta`, a tile at a time | 34-92 s each, 774 s in all, 2.66-3.01 GB peak |
| merge | 2 min: 24,999,508 planes, 168,284 leaves (median 48 brushes, at most 1,244), 12.7 M leaf brushes |
| BSP | **1,376,202,884 bytes** (64% of the format's 2,147,483,647): sides 841 MB, planes 400 MB, brushes 69 MB, leaf brushes 51 MB |
| q3map2 `-light` | 159 s, 2.59 GB peak, 1,025,325 grid points (q3map2 widens the spacing to stay under its 1,048,576 cap); the BSP then 1,378,512,404 bytes. Two earlier tries were stopped by the memory guard while another session's process held 13.9 GB, not by q3map2 |
| Resident collision on x64 (cm structures) | 1.96 GB (340 B a brush) |
| The 64-bit client with the map (no scene) | loaded 67 s after launch; peak private memory 5.0 GB as sampled (psutil, every 0.5 s) |

**In the engine:** the check client loaded it (`devmap`; the BSP before `-light`, whose grid changes no collision)
and traced slice-7's 5,000 engine drop starts in each of the
six copies, shifted by the copy's offset (`district_load.py`), against slice-7's own answers. Every difference has a
known cause: 46 falls a copy end on the sky shell's floor, which the synthetic shell puts 256 u lower; 119 a copy
start inside a brush and fall to that floor; 1-4 a copy land 0.01-0.15 u apart (the copies' planes were recomputed by
q3map2 from coordinates re-written 28,000 u off, at four decimals); 0-2 a copy start touching a surface; the rest
are the touching starts' flag flips seen on Kabuki and the slice. No trace lands where the slice falls or falls where
it lands.

**Watson, projected.** By instances, the district inventory (about 1.64 M scene instances) against Kabuki (3.69 brushes
an instance) and the slice (3.48): about 5.7-6.1 M brushes. At the tiled slice's 266 B a brush on file (the synthetic
district's 239 is low: its copies share every horizontal plane, Watson will not) its BSP is about 1.5-1.6 GB, 70-75%
of the format's limit, and about 2.2 GB of collision resident on x64, plus the renderer's plane copy (section 7) and
the file read whole at load. Compile: 16 tiles of about 370,000 brushes (the split halves until every tile is under
500,000), 13-15 minutes of q3map2 at about 3 GB, one tile at a time, after a 15-minute split.
It fits Watson; it does not leave room for much more in one map: the next districts need their own BSPs (section 7).

## 7. Left for later

- **The renderer's plane copy** (`tr_bsp.cpp` R_LoadPlanes allocates twice the plane count, 40 B a plane): with the
  exact planes about 231 B a brush on the slice, about 1.4 GB at district size, for planes a scene map never draws
  from. The renderer still walks the BSP nodes to find the view leaf (`tr_world.cpp`), so a compact copy of nodes and
  their planes, not none. rd-vulkan work, outside this phase's files.
- **A vis lump with cluster = tile** (Watson doc section 4, phase 2): not done. Every leaf is cluster 0, so every
  entity is offered to every client; it matters once interiors bring doors and items. The merged tree makes it
  straightforward: a cluster per tile box.
- **Character lighting:** q3map2 caps the light grid at 1,048,576 points (`MAX_MAP_LIGHTGRID`) by widening its
  spacing, so a district gets a coarse grid automatically (the slice's: 288 x 272 x 336 u cells, 3.4 MB). The
  renderer lighting characters from the scene is phase 3's.
- **gltf2map's memory:** the whole map's brush texts are still held until the `.map` is written (about 5.4 GB at
  district size, once: no longer joined into a second copy). Measure on the real district build; write the slab jobs'
  results straight to the file if it does not fit beside the other sessions.
- **The bevel snap** stays available (`--slim-snap 1`, 4% fewer bytes) if bytes ever matter more than grazing contacts.
- **Beyond Watson:** the whole city is past one map's coordinates and past the format (about 9x Watson): per-tile
  BSPs, as both district reviews of 2026-10-31 recommend (`.codex-research/levelport/look/district-consult-20261031/`);
  `colmerge.py split` already makes the tiles.

## 8. City coordinates (measurement 1 of the Watson doc's section 14, 2026-10-31)

The question: can collision live at the city's edge (about +-238 k u) in one fixed coordinate space? The test, with
section 14's tolerances fixed before the run: Kabuki map-23's `.map` (kabuki-17.glb, map-23's settings) moved by
whole units to the origin (the control) and to the four corners (+-232,000, +-207,500; Kabuki's own extent keeps it
inside the city's 238 k x 214 k), compiled each time with `-bsp -meta`, `-light fast` and the exact plane pass, and a
corpus of 31,000 engine traces moved the same way (`corpus.py`: map-27's 5,000 drop starts dropped; 1,000 of them
swept 512 u in 8 directions with the standing box, the crouched box and a point; 1,000 zero-length tests; 1,000 point
shots, MASK_SHOT; every coordinate on the 1/64 u grid, so it is the same line at the origin and at a corner).

What it took: q3map2 refuses brushes past +-65,536 (`common/maxworld.h`), raised to +-262,144 in a fourth patch
(`tools/levelport/q3map2/world_coords.patch`, build `netradiant-custom-20260114-xenjk4`, not gltf2map's default);
the engine has no collision clamp (patch windings aside, as the reviews said); `levelcheck_trace`
(`codemp/game/g_levelcheck.c`) now takes any sweep (start, end, box, mask: a line of 13 numbers) and reports the end,
fraction, start and all solid, plane normal, surface flags, contents and entity number at full precision, and its own
time; the old vertical drop lines are unchanged. Scripts: `far.sh`, `far2.sh`, `far_compare.py` (results
`far/far_compare.json`).

| Each corner against the control (31,000 traces) | compiled at the corner | the control's BSP moved there |
|---|---|---|
| Brushes, sides, planes | 240,035; 2,959,235-2,961,193 sides (+4%); +11% planes | equal |
| Endpoints more than 1/64 u apart | 4,521-4,704 | 3,914-4,143 |
| ... more than 1/8 u | about 100 | 84 (NE) |
| ... more than 8 u (a box catches an edge in one, passes in the other) | 5 (NE) | 8 (NE) |
| Start/all-solid differ | 2-12 | 2-10 |
| Contents, surface flags or entity differ | 114-160 | 58-64 |
| Fractions more than 1e-6 apart | about 17,900-18,000 | about 17,800-17,900 |
| Zero-length tests | identical | identical |
| Trace time against the control's 814 ms | 1.07-1.37x | 1.09-1.73x (one run each, machine shared: noise) |

**Both arms fail the tolerances, for two reasons:**

- **q3map2 far from the origin builds different brushes.** Every brush keeps exactly its own faces, but 60,851 of
  240,035 get a different bevel set, nearly all with more (up to 11). q3map2 keeps a side's winding in floats; at
  232,000 u a float steps in 1/64 u, edges that are axial at the origin come out slightly off-axial, and
  AddBrushBevels adds bevels for them. Its plane matching (0.01 u) is also finer than a float's step there.
- **The engine's float trace math far out is coarser than the tolerance.** With the very same brushes and tree
  (the moved control), most differences are about 0.02-0.03 u: a side's test is a 232,000-sized dot product minus a
  232,000-sized distance, a couple of float steps of error, against a 1/64 u tolerance that is one step. A few dozen
  traces in 31,000 differ by more, where a box grazes an edge or two brushes meet with different contents.

**What does pass: collision in each chunk's own frame** (section 9, measured in the engine). A float at the corner
lies on the 1/64 u grid, so subtracting a whole-unit chunk origin is exact: the far corpus minus the origin is the
control's corpus bit for bit (`chunk_local_check.py`). With chunks compiled around their own origin (q3map2 then
builds exactly the origin's brushes) and every collision query moved into the chunk's frame, traces at the city's
edge are the control's. Game state, snapshots and the renderer keep the one fixed coordinate space; only collision
works chunk-local, and the chunk manifest carries each chunk's origin.

Not measured here: movement routes at the corner (pmove positions are floats on the 1/64 u grid there; section 14's
1/8 u a frame tolerance), the projected-vertex test and the look at the far corner (both need the scene moved: the
renderer's).

## 9. Collision chunks in the engine (2026-10-31)

Owner: "before the census, build the chunk-local runtime piece and prove measurement 1 passes with it in the
engine". The first piece of section 14's runtime (world-level dispatch over chunk clip maps), in `codemp/qcommon`:

- **Chunks** (`cm_local.h` `cmChunk_t`, `cm_load.cpp` `CM_LoadChunk`): a BSP loaded by the ordinary loader into a
  clip map of its own, placed at a whole-unit origin (anything else is refused: whole units keep the move exact).
  Its model 0 keeps its tree (a sub-BSP's becomes a flat leaf of every brush, cm_load.cpp CMod_LoadSubmodels) and
  it adds no inline models (`TotalSubModels`, so the main map's model handles do not move). On the hunk with the map;
  `CM_ClearMap` drops them. Up to 1,024.
- **Dispatch** (`cm_trace.cpp` `CM_TraceChunks`, `cm_test.cpp` `CM_PointContents`): the world trace
  (`CM_BoxTrace`, model 0: what `SV_Trace` and the cgame's world trace call) and world point contents also run over
  each chunk whose bounds the query's touch, the query moved into the chunk's frame, the end and the plane's
  distance moved back; traces merge as `SV_ClipMoveToEntities` merges entities' (a lower fraction or all-solid wins,
  start-solid accumulates), so a chunk's hit stays a world hit (`ENTITYNUM_WORLD`, set by `SV_Trace`); contents OR.
  The position test is the zero-length trace, so it comes with them.
- **Surface flags from the traced clip map's own shader table** (`traceWork_t.clipMap`; the brush trace read
  `cmg.shaders` whatever clip map it traced: for sub-BSPs a fix too).
- **`cm_loadchunk <maps/name.bsp> <x> <y> <z>`** (common.cpp): places one, for tests, until the gamestate's chunk
  manifest does. The main map is the hub (leaves, clusters, areas, entities).

`CM_Trace` is split so its body traces a given clip map (`CM_TraceInMap`); nothing else in it changed. Built as its own
64-bit build of this tree (`%LOCALAPPDATA%/xenjk_build_x64_chunks`: engine, rd-vulkan, game, cgame, ui), staged apart
from the shared `levelcheck64.exe` (`engine_trace.py --build`).

**Measurement 1 again, through chunks** (`chunks.sh`, `points.sh`, `chunk_compare.py`; results `chunks/`): a hub
(a 1,024 u sealed box with a spawn at the origin, `hub_map.py`) and the control's BSP, compiled at the origin, placed
by `cm_loadchunk` at each corner; the same 31,000-trace corpus, against the control traced as the world on the same
build (which matches far.sh's control bit for bit):

| Chunk at | Out of tolerance | Fraction, solids, normal, flags, contents, entity identical | End, after moving back | Trace time |
|---|---|---|---|---|
| +232,000, +207,500 | 0 of 31,000 | 31,000 | within 0.0083 u | 1.06x |
| -232,000, +207,500 | 0 | 31,000 | within 0.0083 u | 1.12x |
| +232,000, -207,500 | 0 | 31,000 | within 0.0083 u | 1.02x |
| -232,000, -207,500 | 0 | 31,000 | within 0.0083 u | 1.00x |

Every tolerance of section 14 holds (ends within 1/64 u, the rest identical, time within 1.25x of the control's
859 ms). Point contents, 15,000 points (each drop start, 9 u and 30 u under it; 5,376 inside solid brushes), are
identical at all four corners. Placed at the origin, the chunk differs from the control in 400 traces, all sweeping
through the hub's own box (its floor and sky, which the control lacks); none differ outside it.

**Not yet:** the chunk manifest in the gamestate (a remote client must load the same chunks; here client and server
share one collision model in the listen server), residency and eviction (stable chunk ids, an arena each instead of
the map's hunk), `CM_TransformedBoxTrace` and the leaf queries (`CM_BoxLeafnums`, areas: the hub's) over chunks, and
the movement routes at a corner. The two-chunk experiment (measurement 4) can now run on this runtime.

## 10. Collision by the city grid (2026-10-31)

The order adopted in the Watson doc's section 15, step 2: every Watson build part compiled about its own origin, the
per-tile BSPs being the runtime chunks, and Watson's play export merged from them. The input is the pipeline's
`cp2gltf.py chunks`: one collision glb a city-grid tile (256 m) in the tile's own frame (origin the tile's corner at
the 12 m floor, whole metres, so whole game units), `manifest.json` (each chunk's `offset_in_area_gltf_m`;
game units = (x, -z, y) x 37.5, a tile's offset its min corner), and `base.glb` (ring, lid, spawns, lights, in the
area's frame).

- **`gltf2map.py --col-chunk`**: a chunk BSP, open (no sky, light or vis). q3map2 cannot compile an all-detail map
  ("Backwards tree volume"), so the compile adds a nodraw shell around the geometry, named as a sky shell, and
  `colmerge.merge(..., chunk=True)` writes the BSP again with the shell and every draw surface dropped and colmerge's
  tree. A tile with no collision gets `empty_chunk.txt` and no BSP. One q3map2 run a chunk (Watson's densest, `-5_5`,
  is under the 500 k limit).
- **`gltf2map.py <base.glb> --grid-chunks <chunks dir> [--chunk-builds <dir>]`**: an area's play export. The base
  compiles as an ordinary map (sky shell, entities); every chunk not yet built is compiled `--col-chunk`; then
  `colmerge.merge(..., offsets=...)` joins the base and every chunk at its offset (each plane's distance moved by
  n.T in float64) under one tree; then light and the plane pass as before.
- **colmerge's tree always has a root node** (the engine refuses a map without one, "Map has no nodes": the slice's
  `-6_7`, 16 brushes, was a single leaf).

**The slice, gated against slice-20** (the pipeline's tag-20 build: the same glb, `watson_slice-20.glb`, through the
brush-count split). Chunks: `build/cp_watson_slice_colchunks-21` (11 with collision, 958,164 brushes; `-5_5` 363,707);
export: `build/cp_watson_slice_col-13` (958,211 brushes, 82.3 MB, 156 s with the chunks built). Checks with a frozen
levelcheck (`lc_head/`, the committed 1f1250f8: the working copy's `compare` broke at 06:28, reported to its owner),
run on both maps:

| | slice-20 | slice-13 (grid) |
|---|---|---|
| Spawns stuck | 0 of 16 | 0 of 16 |
| Compare: matched, buried, missing | 3,090,060, 642,468, 84,678 | 3,090,039, 642,494, 84,673 |
| Invisible tops, water beds | 384,124, 122,112 | 384,124, 122,114 |
| Cells whose counts differ | | 134 of 3,265,254 |
| slice-20's 5,000 engine traces replayed | | 4,958 identical; landings 1,502 of 1,505 identical, 3 within 0.033 u |

The drop check (`lc_head/run.py drop`, the same 5,000 starts on both, m² by result):

| | slice-20 | slice-13 (grid) |
|---|---|---|
| Fall through: falls, void, holds | 439, 133, 1,530 | 438, 131, 1,529 |
| Fall through: covered, low, blocked | 4,672, 508, 578 | 4,675, 508, 580 |
| Invisible solid: bump, lump, wall or roof, nothing visible under | 12,281, 8,255, 32,669, 9,171 | 12,281, 8,256, 32,670, 9,170 |

No class moves by more than 3 m². The drop's own engine part stalled on both maps alike (its client was still
loading images when it timed out at 1,816 s, no trace back), so the engine side is the 5,000-trace replay above
(`engine_trace.py`, a dedicated load).

**The chunks in the engine** (`grid_gate.sh`, `grid_compare.py`): the 11 chunks placed by `cm_loadchunk` at their
offsets beside a hub far from the slice, the 31,000-trace corpus from slice-7's drop starts, against slice-20 as the
world. For traces that start free in both (12,646): 270 differ through the ring, lid or sky (not in chunks), 2,585
otherwise, of which 31 by more than 1/64 u at the end, 5 by more than 1/8 u, none by 1 u, and 20 hit a neighbouring
brush with other flags. The noise floor, slice-20 against slice-12 (two whole-area builds, tags 20 and 7): 70, of
which 6 over 1/64 u and none over 1/8 u. The grid path is noisier: q3map2 snaps plane distances in absolute
coordinates (to whole units within 0.01 u) and matches planes against those it has already made, so the same slab
compiled in a tile's frame and in the area's frame can come out with planes a hundredth of a unit apart, and the
slab merge, fed the same triangles in other coordinates, can partition a surface differently (about 9,000 brushes have
no twin within 0.5 u, though the surfaces are the same). The 54 traces that differ by more than 8 u all start inside
solid, where the result depends on the partition. Trace time: 324 ms against slice-20's 842 (0.38x).

## 11. Two chunks against one map (measurement 4, 2026-10-31)

Section 14's design: map-23's `.map` (kabuki-17.glb, map-23's settings), its world brushes split whole by bounds
centre at x = 0 (`split_x.py`: 127,496 and 112,492 brushes, none cut or duplicated; chunk B written in reverse order
so its shader table differs), each compiled open with q3map2 `-bsp -meta` and colmerge's chunk tree; the hub holds
the sky, the ring and the spawns; the control is map-27 (the union compiled whole) traced as the world. Corpus
(`m4/corpus.txt`, 52,500 traces): the 31,000 of section 8 and 21,500 across the seam (`seam_corpus.py`: from the
1,075 drop starts within 600 u of it, sweeps from x -700 to +700 and back with the standing, crouched and point
boxes, zero-length tests at x -8, 0 and +8, drops at x -16, -1, 0, +1, +16). Arms: **seam** (the chunks in the map's
frame at 0) and **corner** (each chunk compiled about its own origin, x -3,072 and +3,072, with the sky and ring as a
third chunk, all placed at each city corner). Results (`m4_compare.py`, `m4/m4_compare.json`):

| Arm (52,500 traces) | Out of tolerance | Start/all-solid | Flags, contents, entity | End over 1/64 u (max) | Fraction over 1e-6 | Time |
|---|---|---|---|---|---|---|
| seam | 1,818 | 50 | 493 | 18 (0.115) | 1,269 | 0.24x |
| corner (each of the four: identical results) | 2,856 | 50 | 501 | 22 (0.130) | 2,295 | 0.16-0.24x |

**It fails the tolerances, and the decomposition says where from** (`m4_parts.py`: parts cut from map-27's own
compiled brushes, so nothing is recompiled):

| Comparison | Out of tolerance | Ties: flags or contents | Start-solid flips | End over 1/64 u | Fraction |
|---|---|---|---|---|---|
| map-27 with colmerge's tree against map-27 (one map, the same brushes) | 971 | 876 | 76 | 0 | 0 |
| two chunks of map-27's brushes against that one map (the dispatch) | 158 | 139 | 19 | 0 | 0 |
| each chunk compiled alone against map-27's brushes | 1,695 | 402 | 37 | 18 | 1,269 |

- **The dispatch is exact.** With the same brushes, two chunks against one map change no end and no fraction. What
  changes is ties: when two brushes (or a brush's face and its bevel) are hit at the same fraction, the engine keeps
  the first it tests, so the surface flags and contents reported depend on test order; and a trace that starts
  touching stops at the first brush it meets, so its start-solid flag does too. Any change of order does this:
  swapping q3map2's tree for colmerge's inside one map gives six times more of it than chunking. Section 14's "no
  flag mismatch" cannot hold for any change of brush order unless ties are broken by a rule that does not depend on
  order (for example a brush's own face before its bevel, then the lower brush in the chunk manifest's order): an
  engine change, proposed, not made.
- **The geometric difference is q3map2 compiling each chunk alone**: plane choices, as in section 10 (18 ends beyond
  1/64 u, the largest 0.115 u). Compiled in shifted frames (the corner arm) it grows (2,295 fraction differences).
- **Corners add nothing**: all four corner placements give identical results to each other, the chunk-local frame of
  section 9.
- **Time**: control 4,017 ms (q3map2's tree), one map with colmerge's tree 1,106 ms, two chunks 979 ms, the corner
  arms 637-950 ms: within the 1.25x by a wide margin.

**Under the owner's amendment** (Watson doc section 14: flag differences on exact ties and touching starts do not
count; endpoints, fractions and contents stay strict). `m4_amended.py` (`m4/m4_amended.json`) excuses a difference
only when the end, fraction, all-solid flag, contents and entity all agree:

| Comparison (52,500 traces) | Left out of tolerance | Ends over 1/64 u | Fractions over 1e-6 | Contents | All-solid, entity | Excused |
|---|---|---|---|---|---|---|
| colmerge's tree against q3map2's, one map | 4 | 0 | 0 | 4 | 0, 0 | 967 |
| the dispatch (two chunks of map-27's brushes against that one map) | 2 | 0 | 0 | 2 | 0, 0 | 156 |
| seam (each chunk compiled alone) | 1,273 | 18 | 1,269 | 4 | 1, 1 | 545 |
| corner, each of the four | 2,299 | 22 | 2,295 | 4 | 1, 1 | 557 |

- **The dispatch passes on ends and fractions.** Its two contents differences are one buried start, swept standing
  and crouched (from 3,080 3,416 83): all-solid in both, the end at the start, and the contents read from whichever
  brush the engine tests first (a brush the box only touches reports none). The same order effect gives the tree swap
  inside one map four. Whether a buried start counts as a touching start is the owner's to say; the measurement does
  not decide it.
- **Compiling each chunk alone fails the fractions**: 1,269 traces (2,295 in the corner frames) hit planes a little
  apart, the largest end 0.13 u. That is q3map2 choosing planes against the planes it has already made in the same
  run, so the same brush comes out slightly differently beside other brushes; the frame adds to it (section 10's
  absolute-coordinate snapping). For the city there is no whole compile to agree with, so the comparison stands for
  how far a chunk's compile can move a surface, not for an error in the runtime.

## 12. Watson by the city grid (2026-10-31)

**Chunks**: the pipeline's 89 Watson tiles (`cp77/watson/tiles/1/chunks`) compiled `--col-chunk` one at a time
(`grid_chunks.sh`, `build/cp_watson_colchunks-1`, 06:46-07:59): 82 with collision, 7 empty, 7,601,933 brushes in
all, the densest 394,439; q3map2 1,252 s in all (the longest 176 s), 652 MB of pk3s.

**In the engine** (`watson.sh`, `watson/chunks`): the 82 chunks loaded by `cm_loadchunk` at their offsets beside the
far hub, and a corpus over every tile with collision (`watson_corpus.py`: 64 drops of the standing box a tile from
z 1,200 u, and 16 of them swept 512 u in eight directions at z 600; 15,744 traces). All 82 loaded; 15,744 traces in
1,755 ms. Of the 5,248 drops 3,153 land (z -576 to 770 u, 1st to 99th percentile) and 2,095 fall through where a
tile has no floor under the point (the ring and lid are the base's, not loaded here); 592 of the 10,496 sweeps
start inside solid. Peak private memory 4.4 GB. The load took 674 s from D: (below: 43 s from the NVMe).

**The play export** (`watson2.sh`, `build/cp_watson_col-2`): the first try (`cp_watson_col-1`, a failed partial) stopped
on the base's circular ring (+/-66,900 u, past q3map2's +/-65,536: "Backwards tree volume"). The pipeline session
redid the base with a box ring 1 m outside the area's box (+/-38,512 by +/-55,312 u). Base compile 61 s, merge of the
base and the 82 chunks 271 s (7,601,944 brushes, 265,996 leaves, a median 47 brushes a leaf, the most 1,612), light
556 s. Spawns: 0 of 16 stuck. In the engine it loads in 40 s at a 7.1 GB peak, and traces the corpus in 128 ms.
Against the chunks: 5,007 traces end on the base's ring, lid or sky shell, which the chunk run did not load. Of the
rest, 4 ends differ by more than 1/64 u (the most 0.045 u), 928 fractions by more than 1e-6, 1 in contents, 7 in flags:
the export holds the chunks' planes moved into the area's frame, so their distances round to other float32 values
(section 14).

**The chunk runtime is not slower; D: was.** Re-timed on the NVMe after the move (`watson4/`, the xenjk7 chunks
`build/cp_watson_colchunks-3` and their export `build/cp_watson_col-4`, the same corpus): the 82 chunks load in 27 s
(43 s from start to loaded, against 674 s from D:, which another session was saturating) and trace the corpus in
130 ms, against 1,755 ms then and the merged export's 151 ms. Peak 4.5 GB, against the export's 7.4 GB. Nothing to
profile in `CM_LoadChunk` or `CM_TraceChunks`.

**Watson on xenjk7**: `cp_watson_colchunks-3` (89 chunks, 82 with collision, 7 empty), `cp_watson_col-4` (7,601,944
brushes, merge 116 s; its light ran 2,037 s with 26 minutes of it suspended for another session's timing window);
0 of 16 spawns stuck. Export against chunks, outside the ring, lid and sky (5,007 traces end there): 3 ends over
1/64 u (the most 0.041 u), 941 fractions over 1e-6, 1 contents difference, the area frame's float rounding as
before. The xenjk5 run (`build/cp_watson_colchunks-2`, `watson3/`) was stopped at 30 of 89 chunks for the xenjk7 fix.

## 13. CP2077's collision on Kabuki (measurement 2, the census, 2026-10-31)

Inputs: CP2077's physics geometry cache exported again (`census/export.sh`: WolvenKit 9.0.1 unbundle and serialise of
`03_night_city.geometry_cache` from the base game and from EP1, 520 and 614 MB of JSON), every collision actor of
the 101 Kabuki sector JSONs (`cp77/kabuki/json`) inside the market box (x -1,377..-993, y 1,860..2,244 m), and the
collision presets (`engine/physics/collision_presets.json`). `census/census.py` (`census/census.json`) decodes every
section of each cache, the per-sector runs and the always-loaded one, and resolves each shape by its hash (EP1 before
the base); `census/presets.py` names the presets stored as hashes (FNV-1a 64 of the name).

**The missing 22% is the always-loaded section.** The cache keeps one section past its per-sector runs
(`alwaysLoadedSector`, entries 115,787-123,961 of the base's 123,962, its blob `alwaysLoadedSectorDDB`; EP1 has 8,284
there), which the earlier probe never read. With it, every Kabuki shape resolves:

| Kabuki, 61,454 actors | Placements | Unique | From a sector run | From the always-loaded section |
|---|---|---|---|---|
| Triangle meshes | 41,140 | 2,254 | 15,155 | 25,985 |
| Convex meshes | 4,101 | 192 | 596 | 3,505 |
| Boxes, capsules, spheres (no geometry to resolve) | 33,413, 3,208, 150 | | | |

**The tier decides the size.** CP2077's presets say what a shape touches: the player's controller collides with the
types Static, PlayerBlocker and a few others. "Complex Environment Collision" has no collision type at all, only
queries (shooting, visibility): the detailed meshes bullets and sight lines hit, which the player never touches.

| Kabuki | Blocks the player | Shooting and visibility only |
|---|---|---|
| Triangle-mesh placements | 24,526 (World Static 16,838, Simple Environment Collision 7,686, Destructible 2) | 16,614 |
| Their triangles, placed | 994,434 | 1,827,247 |
| Convex among them (box-like) | 7,112 (3,951) | 1,212 (55) |
| Non-convex: triangles, distinct planes | 876,878, 439,127 | 1,795,461, 878,493 |
| Unique meshes: count, triangles | 1,523, 143,571 | 731, 106,949 |
| Convex meshes, boxes, capsules, spheres | 3,858, 33,413, 3,128, 28 | 243, 0, 80, 122 |

**Converted size, player collision only** (one brush per box, capsule, sphere, convex mesh and convex triangle
mesh; a non-convex mesh one slab per distinct plane at best, one per triangle at worst): 47,539 single brushes plus
0.44-0.88 M slabs, **about 0.49-0.92 M brushes for the market box**, against map-23's 240,836 from the render meshes
(capped at 16 m). Placed, not unique, is what counts: the BSP has no instancing, and 1,523 unique meshes (143,571
triangles) become 24,526 placements. By actor height, half the placed triangles belong to actors below 24 m
(494,737 of 994,434); the rest stand on roofs and upper floors below 336 m.

So CP2077's own player collision is not smaller than the render-derived collision by this count, and measurement 3
(its shapes through the brush path against map-23) needs a real conversion: the slab merge on these triangles, then
compare and drop. Kabuki only; the district-wide resolution rate follows from the same script on Watson's sectors.

## 14. A chunk compiled alone, as the whole compile (q3map2 `-exactplanes`, 2026-10-31)

Owner: "q3map2 can be whatever we need it to be". Section 11's per-chunk differences had three causes in q3map2,
found and patched in turn (`tools/levelport/q3map2/exact_planes.patch`, build
`../Xenmap/.tools/netradiant-custom-20260114-xenjk5/`; both options off by default):

- **Plane reuse was first-come.** `FindFloatPlane` returns the first existing plane within the epsilons (0.01 u,
  1e-5) that also holds the face's points, so a face's plane depended on what else was in the compile. `-exactplanes`
  reuses a plane only when it is exactly the snapped plane. A plane pair's -0 is stored as +0 (they compare equal,
  so the stored sign depended on which side came first: 138 brushes).
- **Edge bevels came from float windings** in the compile's frame (45,492 bevel normals of one Kabuki half differed
  between frames, every face normal agreed). Under `-exactplanes` they come from the brush's own double windings in
  a frame anchored at a whole-unit point of the brush.
- **Plane points did not move exactly.** The `.map`'s 4-decimal points are not binary fractions, so a whole-unit move
  of the text changed their doubles. `-pointgrid 16384` rounds every point to 1/16,384 u as it is read (it moves a
  point at most 3e-5 u; a 4-decimal value is never halfway, so text moved by a whole unit rounds to the same point
  moved). Distance snapping, (a) of the pipeline session's list, was not a cause: the improved snap moves a distance
  to an integer only for axial normals, which a whole-unit move keeps.

**Gate** (fixed before the run, `look/collision-20261030/m4x/GATE.md`; measurement 4's inputs and corpora, the control
the whole map compiled by the same patched q3map2):

| | Seam (halves compiled alone in the map's frame) | Corner (halves compiled about their own origins) |
|---|---|---|
| Brushes bit-identical to the whole compile (`brush_twins.py`) | **240,035 of 240,035** (before: 129,615) | 51,548; the rest differ only in plane distance (normals bit-identical in all but 4 brushes), at most 0.005 u, the float rounding of the other frame |
| Ends over 1/64 u, fractions over 1e-6 (52,500 traces) | **0, 0** (before: 18, 1,269) | 5, 862 (before: 22, 2,295) |
| All-solid, entity | 0, 0 | 0, 0 |
| Contents | 4 | 4 |
| Excused under the owner's amendment (tie flags, touching starts) | 447 | 530 |
| Time against the control (500 ms) | 1.10x | 1.09x (all four corners identical) |

- **The seam arm passes everything but contents** (and passes outright under the buried-start ruling below); its 4
  contents differences are two buried starts (1,400
  -3,768 575 and 3,080 3,416 83, standing and crouched): all-solid in both, contents from whichever burying brush the
  engine tests first. That is engine order, not geometry (the brushes are bit-identical); an order-free rule (the OR
  of every burying brush's contents) is an engine change, proposed, not made.
- **The corner arm fails on float rounding of plane distances.** The chunk stores its planes in its own frame and
  the whole compile in the map's, so the same plane's distance rounds to a different float32 (0.0001-0.005 u). The
  fraction differences are 0.0007 u along the trace at the median, 0.005 u at the 99th percentile. The worst trace is a
  grazing corner contact: a standing drop from -2,598 -120 527 whose box corner lies on a slanted face (2,584.2634 in
  the map's frame, 2,584.2636 moved back from the chunk's), so one compile catches it on a ledge 495 u up and the other
  lets it fall to the floor. No compile in another frame can store the same floats; the seam arm shows the compiler
  itself is now exact. The same rounding is in colmerge's area-frame export of chunks (Watson's 928 fraction
  differences, section 12). The tolerance for chunks in their own frames is the owner's call.
- **Adoption.** gltf2map passes the options under `--exact-planes` (below). They change Kabuki's collision slightly (no 0.01 u plane
  merging; points moved by at most 3e-5 u). Kabuki's gates (compare, drop, the engine replay against map-27) come
  before they become the default and Watson's chunks are rebuilt.

**Kabuki with the options, gated against map-27** (`gate-kabuki/GATE-col31.md`, fixed before the build; gltf2map
`--exact-planes`, off by default, selected the xenjk5 build here (xenjk7 since section 15) and passes `-exactplanes -pointgrid 16384` to every `-bsp`,
chunks included): `build/cp_kabuki_col-31`, map-27's arguments plus the option, 20 s `-bsp`, 128 s in all. Both maps
through the same working levelcheck (map-27 again as `gate-kabuki/map27x`):

| | map-27 | col-31 |
|---|---|---|
| Spawns stuck | 0 of 16 | 0 of 16 |
| Compare: matched, buried, missing | 506,521, 143,396, 28,117 | 506,521, 143,393, 28,120 |
| Drop, fall through: falls, void, holds, covered, low, blocked (m²) | 120, 2, 459, 2,035, 163, 191 | 120, 2, 459, 2,036, 164, 191 |
| Drop, invisible solid: bump, lump, wall or roof, nothing visible under | 3,292, 1,667, 2,901, 680 | 3,291, 1,667, 2,901, 680 |
| map-27's 5,000 engine drops replayed (`replay-k27-on-k31`) | | 4,998 within 1/64 u (4,371 exact) |

**Passes** every item. The two drops past 1/64 u: a sloped landing 0.019 u apart, and the grazing corner contact of
section 14 (-2,598 -120, on the slanted face), which lands on the ledge in one map and the floor in the other.

**Ruling on buried starts** (2026-10-31; the owner delegated it to the pipeline session: "its whatever you think is
best"; option B): a start buried in solid counts like a touching start, so a contents difference on a start that is
all-solid at fraction 0 in both does not count. No engine change; the dispatch keeps a fixed chunk order (the
manifest's), so a build always answers the same way. Endpoints, fractions and the contents of entered brushes stay
strict. Re-scored (`m4_amended.py`, `m4/m4_amended.json`, `m4x/m4_amended.json`):

| 52,500 traces | Out of tolerance | Excused: ties and touching starts, buried-start contents |
|---|---|---|
| The dispatch (two chunks of map-27's brushes against that one map) | **0: passes** | 156, 2 |
| Halves compiled alone in the map's frame, `-exactplanes -pointgrid 16384` (section 14) | **0: passes** | 447, 4 |
| Halves compiled about their own origins, the same options | 862 (5 ends over 1/64 u, 862 fractions) | 530, 4 |
| Halves compiled alone, stock options (section 11) | 1,269 seam, 2,295 corner | 545-557, 4 |

What stays open for chunks compiled alone is the float rounding of plane distances in another frame (section 14).

## 15. Chunks in their own frames against exact geometry (gate fixed 2026-10-31; adopted by the pipeline session on the owner's delegation)

A compile in another frame cannot store the whole compile's float32 planes (section 14), so the whole compile is not
the reference for chunks compiled about their own origins. Both are measured against the same brushes traced exactly,
and the chunks must be no worse than the one map. Fixed before any run; nothing here changes after it:

- **Reference geometry**: every brush of the Kabuki map as q3map2 builds it with `-exactplanes -pointgrid 16384` in the
  map's frame, each side's plane in double precision (faces from the plane points, bevels from the anchored double
  windings), written by q3map2 beside the BSP (a sidecar option, to be added to `exact_planes.patch`). The brushes are
  the same in every arm (section 14: bit-identical normals, the same bevels).
- **Reference trace**: the engine's box trace (`CM_TraceThroughBrush`, the 0.125 u surface clip epsilon, start and
  all-solid rules) ported to double precision and run over those brushes, for measurement 4's corpora (52,500 traces
  a corner, the seam corpus included). The port is checked first against the engine on the one map: it must agree
  with the engine within the tolerances below on at least 99.9% of the traces before it is used.
- **Measured**: the one map (the whole compile, as the world) and the corner arm (halves compiled about their own
  origins, at each of the four corners, ends moved back), each against the reference.
- **Tolerances against the reference**: endpoints 1/64 u, fractions 1e-6, contents of entered brushes; flags on ties,
  touching starts and buried-start contents excused (the owner's amendment and ruling).
- **Pass, for each corner**: (1) the chunks' count of traces out of tolerance is no greater than the one map's, and
  (2) their 99th-percentile endpoint error is no greater than the one map's. Both counts and the largest errors are
  reported, whichever way they go.

**Amended before scoring** (2026-10-31 12:00; the pass criteria are unchanged): the port's check against the engine runs the port in float32 on the one map's own stored planes, with the engine's operation order, so it tests the port and not precision; a float64 port cannot agree with the float engine within 1e-6 on 99.9% of traces when two float compiles already differ on 1.6%. The float64 reference's agreement with the engine is reported as well.

**Run** (2026-10-31; `m4x/xtrace.py`, `m4x/ownframe_score.py`, `m4x/ownframe_score.json`). Reference brushes: the whole
map compiled again by the xenjk6 build with `-xplanes` (`m4x/ref/whole.xplanes`, 105 MB), its BSP's 240,035 brushes
bit-identical to the gate's one map (the files differ by 4 bytes of entity text).

- **The port's check passes**: in float32 on the one map's planes it gives the engine's results exactly, all 52,500
  endpoints bit-identical, 0 out of tolerance (startsolid flips on touching starts aside). The float64 reference
  against the engine: 699 out of tolerance (98.7% agree).
- **Against the reference, the gate fails as written:**

| | Out of tolerance (fractions over 1e-6) | Ends over 1/64 u | Endpoint error, 99th percentile | Pass |
|---|---|---|---|---|
| The one map | 699 | 23 | 0.00062 u | |
| Each corner (all four identical) | 717 | 22 | 0.00776 u | no, on both criteria |

- **Why**: the endpoint criterion measures where the engine stores the end, not the collision. A chunk at the city's
  corner reports `endpos` as a float32 world coordinate near x = 232,000, whose step is 1/64 u, so every end there is
  off by up to half a step (0.0078 u, within the 1/64 u tolerance). Recomputed from each trace's own fraction in
  double, the corners' 99th-percentile endpoint error is 0.000598 u against the one map's 0.000612, and their
  fraction differences over 1e-5 are 24 against 26. The count criterion fails by 18 traces (717 against 699), all
  fractions just over 1e-6 (median 1.4e-6): the two compiles' float32 rounding falls on different traces.
- Both errors are float32's, in different places: the chunks' at the stored world coordinate, the one map's in its
  planes. The largest error, 507.9 u in both, is one grazing drop that both float compiles resolve the other way from
  the exact trace.

**Amended after the run** (the pipeline session's ruling on the owner's delegation: the first criteria compared the
corners' world-space float32 endpoints, a step of 1/64 u at x 232,000, with the one map's near the origin, which
measures placement, not chunk geometry). Both are scored location-neutrally, each end taken from the trace's own
fraction in double in the brushes' own frame; the tolerance values did not change (`m4x/GATE-ownframe.md`, the raw
numbers beside the amended ones). Under it the xenjk5 build still **failed**, on the count (717 against 699; the
99th-percentile end 0.000598 u against 0.000612 passed).

**The q3map2 cause, and the fix** (`exact_planes.patch`, build `xenjk7`): a face whose normal is not snapped stored
its distance as the double normal's through one of its points beside the float32 normal, so the stored plane was off
the points by (n_float - n_double).p, an error that grows with the coordinate and so differs between frames. Under
`-exactplanes` a face's distance is now the stored normal's through its points' centre, in double, and box bevels sit
at the bounds of the double windings, not of their float copies. Re-run (`m4x2/`, every arm compiled again):

| 52,500 traces | xenjk5 | xenjk7 |
|---|---|---|
| Seam arm: brushes bit-identical to the whole compile; out of tolerance against it | 240,035; 0 | 240,035; 0 |
| Port check (float32 port against the engine) | all 52,500 ends bit-identical | all 52,500 ends bit-identical |
| One map against the exact reference: out of tolerance; end p99 (neutral) | 699; 0.000612 u | 664; 0.000596 u |
| Each corner against the exact reference: out of tolerance; end p99 (neutral) | 717; 0.000598 u | **605; 0.000536 u** |
| Own-frame gate | fails (count) | **passes**, all four corners |

The fix makes both compiles closer to the exact geometry, the chunks more so. The largest error, 603.7 u in both, is
one grazing drop that both float compiles resolve the other way from the exact trace.

**Kabuki with xenjk7 against map-27** (`gate-kabuki/GATE-col32.md`, col-31's criteria, fixed before the build):
`build/cp_kabuki_col-32` passes: 0 of 16 spawns stuck; compare 506,520, 143,393, 28,121 against 506,521, 143,396,
28,117; every drop class within 1 m²; 4,998 of map-27's 5,000 engine drops within 1/64 u (`replay-k27-on-k32`). gltf2map's
`--exact-planes` now selects xenjk7.

## 16. Runtime chunks: a design for review (2026-10-31; not built; reviewed by the pipeline session, its points folded in)

What exists: `cm_loadchunk` places a chunk BSP at a whole-unit origin; `CM_TraceChunks` and `CM_PointContents` see every
loaded chunk, each in its own frame, with hits as `ENTITYNUM_WORLD` (sections 9, 11, 15). It is a console command:
nothing decides what loads, nothing evicts, and the client does not know about chunks. This section designs that.

**As built (update 2026-11-19; `CM_ReadChunkManifest`, `CM_ChunkResidency`, `CM_ListUnloaded` in
`codemp/qcommon/cm_load.cpp`, the trace side in `cm_trace.cpp`):** the design below was simplified.
- The manifest is a plain text file of lines, one chunk each: BSP name, origin (whole units) and bounds (mins, maxs).
  There are no stable ids, build keys or content checksums, and no manifest configstring in the gamestate.
- Residency is driven by the server's active clients (a remote client's own player on the client side) and `cm_drive`
  points only; projectiles do not drive it. A chunk loads within `cm_chunkLoadDist` of a driver and is freed beyond
  `cm_chunkEvictDist`, over `cm_chunkMegs`, or after `cm_chunkIdleEvictSec` without drivers.
- A chunk not loaded (or given up after failing) is not a 4 m occupancy proxy: its whole placed bounds are treated as a
  solid box by traces and point contents (`cm_chunkUnloadedSolid 1`, the default; 0 = empty space).
Read the rest of this section as the design, not the code.

### What is measured so far

| Watson, 82 chunks (xenjk7, NVMe) | |
|---|---|
| Chunk BSPs | 2.1 GB (689 MB compressed); the largest 105 MB |
| Load | 27 s for all, about 80 MB/s with decompression; the largest about 1 s |
| Memory | 4.5 GB peak private for the hub and all 82 (a client process) |
| Trace cost | the corpus in 130 ms, against 151 ms on the merged export: no dispatch cost |

The community servers the owner named have 2 vCPU and 4 GB, possibly 1 GB now (owner, 2026-10-06). All of
Watson's collision does not fit there with room for the game, so the server has to keep only part of it resident.

### The manifest

- One file in the hub's pk3, `maps/<hub>.chunks`: per chunk a stable id (the city grid's tile `i_j` and its build
  key), the BSP's path, its origin (whole units), its bounds and its content checksum. Ids never depend on load order;
  a chunk's slot in `cm_chunks` is found by id, so eviction and reloading cannot renumber anything.
- The gamestate carries one configstring: the manifest's name and checksum. With a few thousand chunks the city's
  manifest is far past the gamestate's 16,000 characters, so the list itself never goes there. The client reads the
  same file from its own pk3 (the pure-server checksums cover it) and refuses a mismatch; demos record the
  configstring and so replay against the same manifest.

### The hub

The hub BSP is the map the server loads: model 0 holds the sky shell, the playable boundary (ring and lid), one area
and one cluster, and the entity string (spawns, lights, items, triggers, movers and their inline models). Chunks hold
world collision only: no entities, no inline models, no areas. Everything that names an entity or a model number stays
in the hub, so chunking changes no entity number or model index the game and cgame read. The hub's sky shell encloses
the whole city, so the world bounds code and `G_DropToFloor`-style code see the city's size.

### Residency

**Absence never means empty space.** A trace that crosses a chunk that is not resident must not pass through
geometry that is there. So residency is tied to where anything can trace, not to what is visible:

- **Players, bots and fast entities** drive it. Each keeps the chunks within `R_load` resident, and a chunk is evicted only past
  `R_evict` > `R_load` (hysteresis) and only when no other driver holds it. `R_load` is at least the distance anything
  can travel in the worst load time plus the longest normal trace: about 1 s for the largest chunk, Force-speed
  running at roughly 1,000 u/s, and a 9,600 u tile. That gives the 3 x 3 tiles around the driver's tile, extended to
  5 x 5 ahead of fast movement. Projectiles and dropped or thrown entities (rockets, thrown sabers, items) leave
  their owner's radius: each drives residency for its lifetime, around itself and along its path for the next
  second (its velocity times the worst load time), so a rocket never bursts on a stand-in at a chunk's edge.
- **The invariant**: every chunk inside `R_load` of a driver is resident on both the server and the client before
  the driver can reach it. `R_load` is sized from the measured worst-case load latency on each side (open point 1),
  so the margin is shown, not assumed.
- **Loading is asynchronous**: a worker thread reads, decompresses and builds the clip map; the main thread swaps the
  finished chunk into its slot at the start of a frame. A chunk is never half-loaded where a trace can see it.
- **Long traces** (sniper-range weapons, view traces, bots' sight checks) can reach past `R_load`. A trace whose path
  enters a chunk that is not resident is answered by its stand-in: a per-chunk occupancy proxy that ships with the
  chunk, 4 m cells solid where any brush is, small enough to keep resident for the whole city. It is answered as
  solid for `MASK_SHOT` and opaque for sight, and a chunk with no proxy falls back to its bounds as solid. The hit's
  content flags mark it "unloaded", and the game treats that as terrain, without effects or decals. A whole-bounds
  stand-in alone would be too coarse: it would block sight lines along open streets. Movement never meets a stand-in, because a player is always inside resident
  space. This is the one place where chunking changes an answer, and it is visible only at long range.
- **Memory**: `cm_chunkMegs` caps resident chunk memory. Past it the least recently used chunk beyond `R_evict` goes
  first. A chunk inside `R_load` is never evicted; if the cap cannot hold every chunk inside `R_load`, the server logs
  it and keeps them anyway.
- **The client** mirrors the rule around its own player (for prediction, which must answer as the server does), and
  around the view origin when spectating or in demos. Because both sides use the same rule, the same manifest and the
  same chunk data, prediction near the player sees the same collision.

### At map start and while streaming

- **At map start**, before the first snapshot: the hub, the manifest, and every chunk within `R_load` of each spawn
  point and of each entity placed statically in the hub that traces when it spawns (items dropping to the floor,
  movers, triggers). Watson has 16 spawns over its 89 tiles, so that is most of Watson at start. A larger area loads a
  fraction of itself.
- **Streaming**: everything else, driven as above. The bots' navigation data should follow the same grid tiles,
  loaded with their collision chunks; that is the bot owner's area, not part of this design.

### Measured: open points 1 and 2 (2026-10-31, `look/collision-20261030/m1/`, plan fixed before the runs)

The chunk build with `CM_LoadChunk` timing itself in milliseconds, and the 64-bit dedicated server
(`openjkded.x86_64.exe`, no clients) on the hub of section 18 with chosen sets of Watson's tag-1 chunks
(`cp_watson_colchunks-3`). Memory is private bytes after 10 s settled.

| Dedicated server | Chunks | BSP | Private memory | Load: total, median, largest chunk |
|---|---|---|---|---|
| The hub alone | 0 | | 0.29 GB | |
| The densest chunk's 3 x 3 (`-5_6`) | 9 | 408 MB | 0.82 GB | 1.7 s, 144 ms, 452 ms |
| Four far corners' 3 x 3s (four players) | 18 | 314 MB | 0.69 GB | 1.4 s, 62 ms, 287 ms |
| The four densest 3 x 3s that do not overlap (four players, the worst case) | 34 | 1,377 MB | 2.08 GB | 5.8 s, 162 ms, 459 ms |
| All of Watson | 82 | 2,118 MB | 3.05 GB | 8.8 s, 93 ms, 469 ms |
| All of Watson, from D: (spinning), cold | 82 | | 3.05 GB | 9.4 s, 93 ms, 493 ms |
| All of Watson, from D:, beside a 2.4 GB unbuffered sequential read (127 MB/s) | 82 | | 3.05 GB | 9.5 s, 97 ms, 501 ms |

The 64-bit listen client loads all 82 with its worst chunk at 441 ms.

- **Memory**: a resident chunk costs about 1.3x its BSP. The worst four-player case fits a 4 GB server at 2.1 GB, and
  so does all of today's Watson (3.05 GB). The xc rebuild (tag 2, about 2x the collision triangles) will not: about
  6 GB for all of Watson, about 4 GB for the four densest neighbourhoods. Residency, not loading everything, is what
  keeps a 4 GB server in budget.
- **Load latency** is CPU, not the disk: under 0.5 s for any chunk on an idle NVMe, an idle spinning disk, or a
  spinning disk beside a sequential read. The 674 s run of section 12 was D: saturated by another session's random
  reads (queue about 22). That extreme is what the margin has to cover.
- **`R_load`**: a 3 x 3 neighbourhood, loaded as a driver enters a tile, leaves at least one 9,600 u tile between the
  driver and any chunk it has not loaded. Force-speed running (about 1,000 u/s) crosses that in about 10 s, 20x the
  worst measured chunk load. So the invariant holds unless a disk is 20x slower than measured, as the saturated D:
  was; such a server needs a 5 x 5 neighbourhood or chunks preloaded at map start.

### Open points, to be measured before building

1. The server's resident memory per chunk, apart from the client's renderer: a dedicated server with the hub and a
   3 x 3 neighbourhood of Watson's densest tiles. Then the union case on a 4 GB budget: four players in four far
   corners of Watson, each driving its own neighbourhood. With it, the worst-case load latency of a chunk on the
   server and on the client, which sizes `R_load`.
2. The worst load time with the disk busy (the 674 s run from D: shows what a slow disk does): whether `R_load` needs
   a 5 x 5 neighbourhood on HDD servers.
3. How many long traces leave resident space in play: bots' sight checks at long range, sniper fire across districts.
4. Snapshot cost with one cluster and no PVS across a city: an entity-distance cull in the snapshot builder is
   likely needed. It is a separate change.

## 17. CP2077's own shapes against the render-derived collision (measurement 3, 2026-10-31)

Watson doc section 14's measurement 3, ordered by the pipeline session on the owner's delegation; its result decides
the city's collision source. Gate fixed before the first build (`m3/GATE-m3.md`, criteria never changed; every
build and defect recorded there). The converter `m3/m3_glb.py` writes a glb from kabuki-17.glb (map-27's input): its
render-derived collision is taken out, its ring, lid, 1,752 lights and 16 spawns are kept. In their place go CP2077's
player-blocking collision (census section 13's presets) and its terrain:

- **Boxes** are half extents. Checked first: the tops of flat boxes sit a median 3 mm from map-27's surfaces read
  that way, 58 mm read as full sizes.
- **Capsules and spheres** become hulls.
- **Convex meshes and convex triangle meshes** become one brush each, always given to gltf2map as the closed hull.
- **Other triangle meshes and the 33 terrain heightfields** become gltf2map's slabs. The heightfields are
  `worldTerrainCollisionNode`, which the census had not read: 264 x 264 int16 heights on a 0.5 m grid, holes kept.
  Map-27's floor sits a median 4.7 cm above them.

Everything is compiled with map-27's arguments and `--exact-planes`.

**Four converter defects were found and fixed on the way**, each recorded with its failed build kept:
- col-33: flat shapes passed the convexity test and became brushes unbounded along their plane.
- col-34: cooked "convex" meshes open on a side ran to the world's edge.
- col-36: PhysX triangle meshes are double-sided with no consistent winding (43% of walkable triangles face down, and
  every heightfield triangle as decoded did), so gltf2map's 8 u slabs sat on top of floors; floors and slopes are now
  turned to face up.
- col-37: a 160 m actor radius left out part of the square the checks measure.

| Kabuki | map-27 (render-derived) | col-35: shapes | col-36: + terrain | col-37: floors up | **col-38: + the full square** |
|---|---|---|---|---|---|
| 1. Spawns stuck | 0 of 16 | 1 (no floor) | 1 | 1 | **1** |
| 2. Holes: falls + void (m²) | 122 | 8,535 | 2,116 | 2,025 | **1,008** (728 + 280) |
| 3. Brushes | 240,029 | 313,709 | 628,717 | 625,336 | **780,035 (3.25x)** |
| Compile, total (q3map2 -bsp) | 128 s (20 s) | 348 s (50 s) | 425 s, 2 tiles | 414 s, 2 tiles | 470 s, 2 tiles (71 s) |
| Visible: matched | 506,521 | 427,511 | 401,269 | 506,934 | **512,227** |
| Visible: buried | 143,396 | 131,385 | 151,713 | 127,880 | 128,504 |
| Visible: missing | 28,117 | 119,138 | 125,052 | 43,220 | 37,303 (1.3x) |
| Invisible collision tops | 56,329 | 135,849 | 223,985 | 166,277 | 245,582 (4.4x) |
| Drop: holds, covered, low, blocked (m²) | 459, 2,035, 163, 191 | 2,317, 5,062, 2,359, 475 | 3,937, 12,821, 608, 289 | 997, 1,366, 552, 246 | 957, 1,345, 552, 245 |
| Drop, invisible solid: bump, lump, wall or roof, nothing visible under | 3,292, 1,667, 2,901, 680 | 3,110, 2,220, 4,002, 12,624 | 12,723, 2,280, 4,067, 17,240 | 2,415, 2,186, 3,979, 17,309 | 2,835, 2,372, 4,287, 27,693 |
| map-27's 5,000 drops: within 1/64 u, 1 u, 8 u | | 278, 1,320, 2,978 | (not run) | 291, 1,325, 2,983 | 267, 1,301, 2,959 |

**Verdict: CP2077's shapes fail all three required criteria.** The correct conversion (col-38) leaves:
- one spawn inside a 0.86 m CP2077 ramp: the spawn was placed 1 m over the render floor, and the shape is likely a
  ramp over steps;
- 1,008 m² of holes, 8x map-27's, all inside the playable ring;
- 3.25x the brushes; the terrain's 0.5 m heightfield alone more than doubles the count.

It covers the visible surfaces at least as well (matched 512,227 against 506,521; missing 1.3x) and buries fewer. It
adds 4.4x the invisible collision tops: collision where nothing visible is, CP2077's blockers and terrain under the
render.

**Where the holes are.** The largest, 428 m² at 77 m from the centre, has 47 floor-level triangle meshes nearby in a
query-only preset (shooting and visibility), beside 58 blocking boxes. At least part of that floor carries only query
collision in CP2077's own data. What holds the player there in the game (a character-controller rule, an entity
collider, a blocker that keeps players off) is not established. The other causes checked are small:
- entity-placed props: 5.8% of the uncovered surfaces, their share of all surfaces;
- the actor filter: 424 shapes, 1,345 m² of floor, fixed in col-38.

**Decision** (the pipeline session's, on the owner's delegation, taken on col-38): **the city's collision stays
render-derived** (the 16 m cap and the current rules). col-38 fails all three required criteria as fixed; its better
coverage of visible surfaces is noted. CP2077's shapes stay a candidate only as a targeted supplement for a class the
render-derived collision handles badly, with a gate of its own. One lead for later: the query-only floor meshes near
the largest hole. The failed builds (col-33 to col-37) are kept.

## 18. Watson played from its chunks: the playtest hub (2026-10-31)

The pipeline session decided this on the owner's delegation. The merged export (`cp_watson_col-4`) is 2,076,065,164
bytes, 97% of the BSP format's 2 GiB (lump offsets are int32: brush sides 1.06 GiB, planes 0.69 GiB), so it cannot
grow. A listen server is one process, and its client shares the engine's collision maps, chunks included. A hub map
plus `cm_loadchunk` therefore gives server collision and client prediction for all of Watson, with no merged export.
The gate was fixed before the build (`look/collision-20261030/hub/GATE-hub.md`).

- **The hub**: `build/cp_watson_hub_col-1/zz_cp_watson.pk3`. It is `base.glb` (ring, lid, 16 spawns, 39,163 lights)
  compiled alone as map `cp_watson`, the name the tiled scene is bound to, with col-4's arguments and
  `--exact-planes`: 5 collision brushes, 2.9 MB, 418 s (the light pass, for the lights alone).
- **The chunk set**: `zz_cp_watson_colchunks.pk3`, the 82 chunk BSPs of `build/cp_watson_colchunks-3` (2.1 GB, 689 MB
  compressed), plus `cp_watson_chunks.cfg`, one `cm_loadchunk` line a chunk at its manifest origin. It is written by
  `hub/make_chunks_pk3.py` from any chunk build and manifest, so a rebuilt chunk set replaces this one pack.

| Check (the 64-bit check client, the chunk build) | Result |
|---|---|
| 1. Spawns: standing box in solid; a floor within 64 u | 0 of 16; 16 of 16 |
| 2. The same 82 chunks beside a far hub (`watson4/chunks`): traces ending on neither hub's brushes | 10,737 of 10,737 identical |
| 3. The merged export col-4 (`watson4/export`), the same corpus | ends over 1/64 u: 3 (the most 0.041 u); fractions over 1e-6: 941; contents 0. Section 12's export rounding exactly. The 5,007 traces ending on the ring, lid or sky are all within tolerance. |
| 4. Load to the first trace; peak private memory | 28 s; 4.68 GB |

**Checks 2 and 3 re-taken** (2026-10-31, `hub/GATE-hub-retake.md`, the same rules). The corpus's 5,248 drops started
at z 1,200, above the hub's sky shell: in the run above 4,931 ended on the shell's top and 317 fell outside the ring,
so the drops tested no chunk. Re-taken with them starting at z 600, under the lid, and a hub-alone run to tell which
traces reach a chunk (their answer differs from the hub alone's):

| Check | Result |
|---|---|
| 2. Traces reaching a chunk and not ending on the hub's brushes, against the far hub | 5,194 (3,135 drops, 2,043 sweeps, 16 spawn checks): all identical |
| 3. The merged export col-4, the same corpus | ends over 1/64 u: 5 (the most 0.047 u); fractions over 1e-6: 941; contents 0, all-solid 0; normals and flags identical: the export's rounding, as before |

18 more drops differ from the hub alone only by starting inside a chunk brush; they end on the hub's shell bottom
(z -7,353), which check 2 leaves out.

**Not installed.** The engine's chunk code (`codemp/qcommon/cm_*`) is uncommitted, and an install needs the owner.
The build folder holds a launch kit:
- `bin64/`: the 64-bit engine with the chunk loader, copied out of the build tree; agents' builds under
  `%LOCALAPPDATA%` are in the Claude app's package storage, invisible outside it;
- `home/base/`: the hub, the chunk set and the three scene packs as hard links, with the 64-bit game modules;
- `Play_Watson_Chunks.bat`: `xenjk.x86_64.exe +set fs_basepath <GameData> +set fs_homepath <this folder>\home +set
  cl_renderer rd-vulkan +set net_ip 127.0.0.1 +set sv_pure 0 +devmap cp_watson +exec cp_watson_chunks`.

**Do not play this kit (found 2026-10-31 23:10).** Its `bin64` engine (built 16:38, before section 19's hunk fix)
crashes right after the cgame loads: access violation 0xC0000005, reproduced (exit code 3221225477; the game module's
handler writes `jampgame_crash.dmp`), most likely the hazard section 19 fixed: the client's `Hunk_ClearToMark`
freeing the chunks placed on the hunk (the dump is not analysed). The same
kit with the current chunk build's engine runs its frames and quits cleanly (exit 0, 82 chunks placed). Its chunk
pack also predates the manifest, so it cannot use residency (`hub/smoke3-kit-exe`, `hub/smoke4-current-exe`).

GameData is only read: the owner's `openjk.cfg` there loads, and changes made in game save to the kit's home. The
renderer DLL for the tiled scene is the renderer session's (asked; not yet in `bin64`).

**Chunks over one q3map2 run.** `gltf2map --col-chunk` used to refuse a chunk over `--tile-brushes` (500,000).
It now compiles such a chunk as q3map2 tiles (`colmerge.split`) merged as a chunk (`colmerge.merge(chunk=True)`,
each tile's shell copy dropped). Checked: Watson's `-6_10` (238,862 brushes) compiled whole and forced into four
tiles (`--tile-brushes 80000`), both `--exact-planes` (`build/cp_watson_coltiletest_col-1`), gives the same
238,862 brushes bit for bit (`m4x/brush_twins.py`). The files differ only in brush and plane order.

**What it does not do yet.** The chunks load after the map, so anything that traces while the hub spawns its
entities sees no chunks. Watson's hub has only spawns and lights, which do not trace. A dedicated server with remote
clients needs section 16's manifest and client residency.

## 19. Chunk residency, first build (2026-10-31)

Section 16's residency, ordered by the pipeline session on the owner's delegation. The gate was fixed before the code
was tested (`look/collision-20261030/res/GATE-residency.md`). Engine, uncommitted, not installed:

- **Manifest.** `cm_chunkmanifest <file>` reads every chunk's BSP, origin and placed bounds, the `maps/<map>.chunks`
  that `hub/make_chunks_pk3.py` now writes into each chunk pack. Each chunk has a fixed slot in manifest order;
  traces and point contents visit the resident slots in that order and skip empty ones.
- **Residency, every server frame**, before the game runs. Drivers are every active client's origin, plus the
  console's `cm_drive` / `cm_drive_move` points (a test's virtual players).
  - A chunk loads when a driver is within `cm_chunkLoadDist` (9,600 u) of its placed bounds in x and y.
  - It is freed when every driver is beyond `cm_chunkEvictDist` (19,200 u).
  - `cm_chunkreport` prints the counters.
- **Memory a chunk can free.** A chunk's clip map is now zone blocks of its own (`TAG_BSP`), recorded per chunk.
  Before, it was on the hunk past the server's mark, where a listen server's client clearing to the mark (cl_main's
  `Hunk_ClearToMark`) would have freed every placed chunk under the traces. That hazard was also in the earlier
  `cm_loadchunk`.
- **Synchronous** in this build: a load runs inside the server frame.

**Test**: the 64-bit dedicated server, hub `cp_watson_hub_col-1`, Watson's tag-1 chunks. One virtual driver walks
127,164 u corner to corner through the densest tile (`-9_3`, `-5_6`, `-3_14`) at 1,000 u/s in 20 Hz frames, 2,544
frames. At 20 checkpoints it traces 256 traces within 4,800 u. The reference run has every chunk loaded.

| | Residency | Every chunk loaded |
|---|---|---|
| 1. Invariant violations (a chunk within the load distance not resident) | **0** | |
| 2. Checkpoint traces identical to the reference | **5,120 of 5,120** (only the 2,560 sweeps test chunks: below) | |
| Loads, evictions | 42, 34 | 82, 0 |
| Resident chunks: peak, at the end | 15, 8 | 82 |
| Private memory: the hub alone, peak, after the walk | 0.29, 1.06, **0.56 GB** | 0.29, 3.05, 3.05 GB |
| 3. After the walk against the hub plus the resident chunks' blocks (0.29 + 0.30 GB) | **within 4%** | |
| Worst server frame | 884 ms (one load) | |

**Passes all three.** One person walking across Watson needs at most a third of its collision resident. Eviction
returns the memory. The traces near the driver answer as if everything were loaded.

**Correction (found 2026-10-31 20:05, in section 20's test).** Half of each checkpoint's traces are drops, and they
started at z 1,200: above the hub's sky shell (its top at 984.5), outside the map. In the reference, 2,492 of the
2,560 drops land on the shell's top, as they do on the hub alone. So check 2 tested chunks only through its 2,560
sweeps at z 600 (774 of them stop on something, against 17 on the hub alone). Section 20's runs start the drops at
z 600, under the lid (675).

**What it costs**: a load stalls the server frame, up to 884 ms here, the largest chunk. For play that has to move off
the frame. One possible way: a worker thread reads and parses the chunk into memory of its own, and the frame only
swaps the finished chunk into its slot. That means a loader that touches neither the zone nor the file system's shared
state. Not built. Also not in this build: projectiles as drivers, and stand-ins for long traces (section 16).

## 20. Chunk residency off the server frame (2026-10-31)

Section 19's 884 ms stall cannot ship (the pipeline session, on the owner's delegation). The gate was fixed before the
build (`look/collision-20261030/res/GATE-residency-async.md`) and is unchanged through three runs. Engine, uncommitted,
not installed.

**What is built**
- **A worker thread loads chunks.** It reads a chunk's BSP from its pack with its own reader (stdio and zlib; the main
  thread gives it the pack's path, `FS_PakPathOf`) and parses it into `malloc` blocks of its own (thread-local
  `cmod_base` and allocator). It first checks every lump the loaders read, so nothing can `Com_Error` off the main
  thread; a bad chunk comes back as a failed job. Jobs run nearest driver first.
- **The swap.** Only the server frame puts a finished chunk into its slot (a pointer store). A chunk no driver wants
  any more is freed when it finishes; a map change abandons the jobs (a generation count).
- **Held, not dropped.** A client standing in the placed bounds of a chunk not swapped in yet gets no usercmd frames:
  it neither moves nor falls through a floor still loading (`CM_ChunkReadyAt` in `SV_ClientThink`). For mid-play
  only: a slow disk, a teleport.
- **The start region is resident before the first frame** (added after run 1, the pipeline session's ruling):
  - at the manifest, the chunks within `cm_chunkLoadDist` of every spawn point in the hub's entities (`info_player_*`,
    the CTF spawns) load in place and are pinned (`cm_chunkPinSpawns 1`): never evicted;
  - a client entering play (connect, or a respawn: `PERS_SPAWN_COUNT` moves) gets no usercmd frames until every
    chunk within the load distance is in, timed as "spawn holds" in `cm_chunkreport`;
  - `cm_chunkpreload <x> <y> <z>` loads a start's region in place; the walk's first position counts as a spawn.
- `cm_chunkAsync 0` keeps section 19's loads in the frame; `cm_chunkTestDelayMs` is the test's slow disk.

**Test** (the 64-bit dedicated server, held to 2 cores: the 2 vCPU target; hub `cp_watson_hub_col-1`, the tag-1
chunks, section 19's walk and checkpoints): (a) the hub alone, (b) asynchronous residency, (c) every chunk loaded,
(d) a slow disk (`cm_chunkTestDelayMs 10000`), the driver placed at once in tile `-5_6`, then 16 drops beside it,
traced the same way with every chunk loaded and on the hub alone.

| Pass | Run 1 (no preload) | Run 3 (preload; drops inside the map) |
|---|---|---|
| 1. Worst server frame in (b), against (a)'s plus 50 ms | 12 ms against 0: pass | **12 ms** against 1: **pass** |
| 2. Checkpoint traces in (b) identical to (c) | 5,116 of 5,120: **fail** (4, all traced in frame 1, before any chunk was in) | **5,120 of 5,120: pass**; 2,557 of them answer otherwise on the hub alone |
| 3. Frames a driver stood in a chunk not swapped in | 2 (the walk's first two): **fail** | **0: pass** |
| 4. Slow disk: held from the first frame until the swap, then free; drops after it as in (c) | held 206 frames, then free; the drops proved nothing (below) | **held 253 frames** (about 10 s by the log clock), **then free; 16 of 16 drops as in (c)**, all 16 otherwise on the hub alone: **pass** |

**Run 3 passes all four.** Run 1's failures were one gap: a driver placed in the world before its region loaded.
Run 2 was stopped for the renderer session's timing window and is kept (`res/b_async_r2-stopped-renderer-window`).

**The drops were outside the map in run 1.** The walk's and the slow test's drops started at z 1,200, above the
hub's sky shell (its top at 984.5): 2,492 of the walk's 2,560 drops landed on the shell's top, as on the hub alone.
Runs 2 and 3 start them at z 600, under the lid (675). Section 19's check 2 had the same corpus (corrected there);
section 18's checks 2 and 3 are re-taken (`hub/GATE-hub-retake.md`).

**The hold, a player's freeze.** The slow disk held its driver for the test's 10 s delay plus the read (198 ms) and
parse (53 ms): the swap came 10 s after the walk's start by the log clock. Counted in frames it is 253: the walk
started right after the 2.3 s spawn preload stalled the server, and the 47 frames the server ran to catch up were
held too. Run 1, no preload, held 206 frames. On the real disk the worker's loads are at most 223 ms read and 50 ms
parse (run 3's walk, 28 loads), so a teleport into an unloaded tile freezes its player for up to about a quarter second.

| Run 3 | Async residency | Every chunk loaded |
|---|---|---|
| Loads, evictions | 28, 25 | 82, 0 |
| Resident chunks: peak, at the end | 28, 21 | 82 |
| Private memory: the hub alone, peak, after the walk | 0.29, 1.72, **1.28 GB** | 0.29, 3.05, 3.05 GB |
| Worker: read, parse (the most; sum) | 223 ms, 50 ms; 1,917 ms, 406 ms | |
| Spawn preload at the manifest | 16 spawns, 13 chunks, **2,331 ms**, all pinned | |
| The walk's start (`cm_chunkpreload`) | 5 chunks, 222 ms | |

**What the preload costs.** The 13 pinned chunks around Watson's spawns hold about 0.77 GB of chunk blocks for the
whole session, which is most of what run 3 has resident after the walk (1.28 GB against run 1's 0.56). With
`cm_chunkPinSpawns 0` they load at map load but may be evicted, and a client spawning there later waits (a spawn
hold) instead. The 2.3 s preload is a stall when the manifest is read; in play that is the map load.

**Not covered by these tests.**
- No real client was connected: the spawn wait (its "spawn holds") is built but not yet exercised. The kit's listen
  client is the first test.
- Bots think through the game's own path, not `SV_ClientThink`: they are not held.
- The manifest is read by a console command after the map loads (the test cfg); a server reading it as part of the
  map load is not built.
- Projectiles and long traces as drivers (section 16) are the next gated step.

## 21. Watson tag 2: the hub, the chunks' resident cost and the walk (2026-10-31)

The pipeline session's tag 2 tiles, compiled as before: the hub `build/cp_watson_hub_col-2` (tiles/2's `base.glb`,
`--exact-planes`) and the chunk pack beside it, `zz_cp_watson_colchunks.pk3`: the 82 chunks of
`build/cp_watson_colchunks-4` (xenjk7; 7 of the 89 tiles have no collision) with the manifest. 4,063 MB of BSP,
1,317 MB packed (tag 1: 688 MB). The gate was fixed before the runs (`look/collision-20261030/m1/GATE-tag2.md`).

**Server memory** (the 64-bit dedicated server, the hub then the chunks placed by `cm_loadchunk`, section 13's runs):

| Chunks placed | Tag 1 private | Tag 2 private | Tag 2 load to placed |
|---|---|---|---|
| None (the hub alone) | 0.29 GB | 0.29 GB | |
| hood (9) | 0.82 GB | 1.23 GB | 12.6 s |
| corners (18) | 0.69 GB | 1.17 GB | 12.3 s |
| dense4 (34) | 2.08 GB | 3.67 GB | 21.5 s |
| all (82) | 3.05 GB | **5.63 GB** | 27.4 s |

Tag 2's largest chunk has 659,422 brushes (tag 1: 394,439); its worst load in place took 654 ms (tag 1: 469).

**The walk on tag 2** (section 20's, asynchronous, 2 cores; spawn regions pinned, as the build then was):

| | Async residency | Every chunk loaded |
|---|---|---|
| Worst server frame (the hub alone: 1 ms) | **12 ms** | |
| Frames a driver stood in a chunk not swapped in | **0** | |
| Checkpoint traces identical | **5,120 of 5,120** | |
| Loads, evictions; resident peak, at the end | 28, 25; 28, 21 | 82, 0; 82 |
| Chunk blocks: peak, at the end | 2.13 GB, 1.86 GB | |
| Private memory: peak, after the walk | 2.93, **2.02 GB** | 5.63, 5.63 GB |
| Worker: read, parse (the most) | 277 ms, 80 ms | |
| Spawn preload; the walk's start | 13 chunks pinned, 3,803 ms; 5 chunks, 803 ms | |

**The hub's checks** (the 64-bit check client, the hub plus the tag 2 pack, every chunk placed): 0 of 16 spawns'
standing boxes in solid, 16 of 16 drops find a floor within 64 u, all 82 chunks placed with no error. Load to the
first trace 31.6 s; peak private memory 7.25 GB (tag 1: 28 s, 4.68 GB).

**Tag 2 passes**, at about 1.85 times tag 1's memory with every chunk loaded. With residency one walker keeps about
2 GB, against tag 1's 1.28 GB with the same pinning; section 20's decisions (spawn regions not pinned) apply to both.

## 22. Residency for play: spawns, bots, the manifest at map load (2026-10-31)

The pipeline session's decisions on section 20 (on the owner's delegation), one build, the gate fixed before it
(`look/collision-20261030/res/GATE-residency-bots.md`). Engine and game, uncommitted, not installed:

1. **Spawn regions not pinned** (`cm_chunkPinSpawns 0` by default): they load at map load, so the first spawns are
   ready, but residency may evict them (pinned, tag 1 held 0.77 GB and tag 2 more for the whole session).
2. **Bots held.** The game runs a bot from its last usercmd every frame (`G_RunClient`), so skipping usercmds alone
   would let a held bot fall. The server now flags every held client `SVF_CHUNKHOLD` (0x00010000) each frame and
   `G_RunClient` returns early for it; `SV_ClientThink` still skips a held client's usercmds. Bots are active
   clients, so they drive residency. `sv_chunkHoldFlag 0` (cheat) turns the flag off: the test's control.
3. **The manifest read at map load.** `CM_LoadMap` reads `maps/<map>.chunks` beside the BSP, so a chunked map cannot
   run without its chunks; `cm_chunkManifest 0` skips it (the tests' hub-alone and all-loaded runs);
   `cm_chunkmanifest` stays as a dev tool.

Two dev commands in the game, for the tests: `levelcheck_teleport <client> <x> <y> <z>` (a teleporter's move) and
`levelcheck_where <client>` (origin, velocity, ground entity, health, held, spawn count).

**Two loader bugs, found by the runs and fixed.**
- **The worker took a farther chunk first** (runs 4 and 5: the slow test held 20 s, not 10). The frame's first
  queued job started the worker thread, which, newly started, checks its queue without waiting for a wake and took
  one of the first one or two jobs in manifest order. The frame now stages its jobs, queues them under one lock, and
  only then starts or wakes the worker.
- **Freeing on the frame** (run 6: a 77 ms frame, over the 51 ms limit; 38 ms in runs 4 and 5). Unpinned spawn
  chunks were loaded into the zone and freed on the main thread when evicted, hundreds of MB in one frame. The spawn
  preload now reads and parses through the worker's own reader (malloc'd blocks, at map load), and the worker frees
  every evicted malloc'd chunk; the frame only empties the slot.

**Run 7** (both fixes; the 64-bit dedicated server held to 2 cores, hub `cp_watson_hub_col-1`, tag 1):

| Check | Result |
|---|---|
| A1. Worst server frame (the hub alone's 1 ms plus 50) | **0 ms** |
| A2. Checkpoint traces identical to every chunk loaded | **5,120 of 5,120** |
| A3. Frames a driver stood in a chunk not swapped in | **0** |
| A4. Slow disk: held to the swap, then free; its drops as with every chunk loaded | **held 206 frames (about 10 s), 16 of 16** (all 16 otherwise on the hub alone) |
| B1. The bot alone loads the chunk it is teleported into | **yes** (`-5_6`, no virtual driver) |
| B2. Held at the teleport point until the swap | **103 of 103 samples, 0.0 u off** |
| B3. (the 21:05 criterion) not below the floor under its (x, y) | **failed**: the floor was measured wrong (below) |
| Control, `sv_chunkHoldFlag 0` | never held; fell to the hub's shell bottom, z -7,353 |
| Memory after the walk; spawn preload | 0.56 GB; 13 chunks in 1,390 ms, none pinned |

**B3 measured the wrong floor.** The bot (the install's one bot, `Runner_v2`, which runs on its own once released)
landed at z 429.1, 245 u from the column, and hopped on the street at z 420 to 619; a drop from z 600 at its positions
lands on a walkway at z 594 above that street. The bot never fell, but the check as written failed by up to 164 u.
A ruled replacement (a trace down from the bot's own origin) could not fail, the control included. Pass 3 from run 8
is a path check: a point trace in the all-loaded reference between each pair of consecutive samples hits nothing,
with a self-test on an all-loaded run first and the control required to fail.

| Run 8 (run 7's build), pass 3 | Path segments | Hits |
|---|---|---|
| Self-test: the same bot, teleport and window with every chunk placed | 159 | **0** |
| The held bot | 159 | **0** |
| The control (`sv_chunkHoldFlag 0`) | 159 | 3 (the first through the street floor at z 405) |

Passes 1 and 2 held again (the bot alone loaded `-5_6`; 104 of 104 samples at the teleport point until its swap).
**The bot gate passes.** First landing (recorded): z 429.1, 7.5 u above the drop under the release point, 245 u away
on another step (`TeleportPlayer` gives the bot 400 u/s).

**Spawn waits** (C, pinning off): the bot moved 30,000 u away for 20 s between deaths, so its spawn regions were
evicted, then killed 8 times. JA's own respawn delay was 1.1 to 1.2 s each time; after it, the spawn holds:
**median 799 ms, most 1,099 ms**; 4 of the 8 respawns waited under one sample (100 ms).

Runs 1 to 7's folders and logs are kept (`res/`), the stopped ones marked so.

## 23. A chunk that is not loaded is solid (residency step 4, 2026-10-31; built, not yet gated)

Section 16's open point, ruled by the pipeline session on the owner's delegation; the gate was fixed before the code
(`look/collision-20261030/res/GATE-residency-traces.md`). Engine and game, uncommitted, not installed.

**The gap.** Every chunk whose placed bounds lie within `cm_chunkLoadDist` (9,600 u) of a driver is resident, so a
trace that stays within 9,600 u of a player in x and y answers as on the full map; JA's longest hitscan (8,192 u) is
inside that. A trace that leaves every driver's region (a projectile flying on, a long sight line, an NPC or turret
far from every player) found no collision where a chunk was not loaded.

**The rule** (`cm_chunkUnloadedSolid 1`, the default; a cheat cvar, so tests can turn it off). The placed bounds of
each manifest chunk not swapped in are one solid box, as a brush is, for every world trace whose mask has
`CONTENTS_SOLID` and for point contents:
- a trace entering the box stops on its entry face (no epsilon: a start on the face is outside, and moving into it
  stops at fraction 0), `CONTENTS_SOLID`, the face's axis normal, `ENTITYNUM_WORLD`;
- a start strictly inside is start-solid, all-solid when the end is inside too; a point inside is `CONTENTS_SOLID`.
The boxes are listed once whenever a slot changes (the residency frame, an in-place load), and a trace tests only them.
- A rocket that flies past every player's region explodes on the boundary, more than 9,600 u from everyone.
- An NPC or bot standing in an unloaded chunk is stuck (all-solid), not falling: a second guard under the hold.
- No drivers are added: a projectile does not load the city. Not chosen: projectiles as drivers; section 16's
  occupancy proxy.

**False walls.** A non-resident box near a player would be a wall where the map is open. The load test measures from
the driver to the chunk's placed bounds (some reach well past their tile: `-4_12`'s are about 11,000 u wide). The
frame now counts any chunk within the load distance of a driver that is neither resident nor loading ("false walls",
`cm_chunkreport`), and reports the nearest a non-resident box came; `cm_chunkpending` lists the chunks loading. A
test-only fault, `cm_chunkLoadByCentre 1` (the distance taken to the tile's centre), shows the count can fail: a
driver beside a large-bounds chunk counted false walls in every frame (400 of 400 in the trial; the box 8,880 u away).

**The limit, for the owner.** Two players more than about 19,200 u apart (twice the load distance) may have chunks
between them that neither loads: sight, long hitscan and projectiles between them stop at the first such chunk. Run 9
measures the shortest sweep at which this first shows.

Test commands added to the game: `levelcheck_fire` (a rocket from a point, as `probe_rocket` makes one) and
`levelcheck_ent <num> | last` (an entity's state, sampled each frame).

**Known differences from a brush** (accepted by the pipeline session, 2026-11-01):
- a capsule trace meets the box as its bounding box, a little earlier at the box's corners than a capsule would: the
  conservative side;
- a body that starts inside a box and moves out leaves it freely (start-solid only, as a brush does): an NPC inside an
  unloaded chunk is stopped for ordinary short moves, not for a long scripted one;
- whether a projectile meets a box or the loaded chunk depends on whether the swap came first, so on the disk's speed:
  with an idle disk the window is a few hundred ms at the region's edge.

Run 9, sized short at the owner's word (about 8 minutes: the walk's first leg, 961 frames, about 16 loads and 20
evictions), is held while the owner plays.

## 24. After the review: remote clients, failures, memory and lifecycle (2026-11-01)

An independent review of the residency diff (`look/review-20261101/fable_B_residency.md`,
`codex_B_residency.out.md`), ruled by the pipeline session on the owner's delegation. Each finding was checked against
the code before it was fixed; the gate was fixed before the build (`look/collision-20261030/res/GATE-review-fixes.md`).
Engine, game and client uncommitted, not installed.

**What was wrong, and what changed**
- **Remote clients had no residency.** Only the server's frame drove it, so a client on a dedicated server kept its
  spawn regions forever and, under section 23's rule, predicted against solid boxes everywhere else (a listen server
  shares one collision map, which hid it). A client with no server in its process (a remote client, a demo) now runs
  its own residency around its player each frame (`CL_Frame`), and `CL_GetSnapshot` freezes its prediction
  (`PM_FREEZE`) while the chunk under its player is not in. The server also freezes a held client's playerstate, so
  prediction never runs ahead of a hold; the game's next think sets `pm_type` again.
- **One parser for every chunk.** A chunk loaded in the frame went through `CM_LoadMap_Actual`: a bad chunk dropped the
  map and left the loader's thread-locals set (every later map load mis-allocated), and a chunk with patches put hunk
  blocks back under the traces. Every chunk now goes through `CM_ParseChunk` into malloc'd blocks: the worker's from
  its pack, the main thread's from `FS_ReadFile`. A guard restores the thread-locals on every exit; a failed block
  allocation fails the parse; nothing calls `Com_Error` for a chunk.
- **Validation** now also rejects a visibility lump shorter than its header, brushes of fewer than six sides, leaf
  surfaces, clusters out of range and node graphs that are not trees. The zip reader bounds each entry's names, folds
  slashes as the file system does and checks the CRC; 64-bit seeks are wrapped for Linux (`fseeko`/`ftello`).
- **A chunk that fails** on the worker is loaded once in place through the file system (zip64, a loose override), then
  given up: it stays a solid box, holds no one, and is reported (`dead`).
- **Memory.** One accounting path for every load, swap and eviction, in 64 bits. `cm_chunkMegs` (0: off) evicts the
  least recently wanted chunks beyond reach when over the cap, never one within reach (misses are counted).
- **Lifecycle.** Finished loads are retired at a map change, on a map without chunks and with no drivers; after
  `cm_chunkIdleEvictSec` (30) with no driver every unpinned chunk goes. The worker is stopped and joined at shutdown.
  map_restart's frames run residency and the hold (`SV_ChunkFrame`).
- **The file system.** `FS_PakPathOf` marks the pack referenced (a joining client is told to download it) and returns
  no pack when a loose file comes first, so the file system reads that one.
- **Reports.** `cm_chunkreport2`: held clients' frames, loading boxes within `cm_chunkNearDist` (2,000 u) of a driver
  and the nearest loading box, given-up chunks, cap evictions and misses.

**Run 10** (short at the owner's word: the walk's first leg, 961 frames; the 64-bit dedicated server held to 2 cores;
the owner's apps running, the owner's ruling):

| Check | Result |
|---|---|
| A1. Worst server frame (the hub alone's 1 ms plus 50) | **0 ms** |
| A2. Checkpoint traces identical to every chunk loaded | **5,120 of 5,120** (3,143 reach a chunk) |
| A3. Frames a driver stood in a chunk not swapped in | **0** (17 loads, 21 evictions) |
| A4. Slow disk | **failed as written** (ruled 2026-11-01): held 210 frames (about 10 s), released at the swap; drops: ends, fractions, normals, contents 16 of 16 as with every chunk loaded, but 7 start-solid flags differ (below) |
| Step 4 (g): sweeps never further than every chunk loaded; each that differs on a non-resident box's face, solid | **1,280 sweeps: 0 further; 25 differ, all on entry faces, all solid** |
| Step 4 (h): the rocket stops on the first non-resident box on its line | **4.5 u from `-9_6`'s face** (one frame of travel is 45 u); with every chunk placed it flew on past 40,000 u |
| Step 4 (j): false walls; the fault counts them | **0** (nearest unloaded 9,220 u; no sweep on a loading box); the fault **400 of 400** |
| Step 4 (k): the shortest sweep that differs | **9,680 u** (about 258 m): the limit for the owner; sight and long traces stop at the first chunk not loaded |
| Bot pass 3: the held bot's path check | **0 hits in 159**; held 114 of 114 samples at the teleport point |
| Bot control, the hold flag off | not held, yet **stayed at the teleport point** (a start inside the unloaded box is all-solid) and landed on the real floor: section 23's rule is a second guard (before it the control fell to z -7,353) |
| Respawn into an evicted region (observation) | 2 respawns, held 13 and 9 samples, **0.0 u moved** |
| R1. A remote client process | after each of three moves the client swapped in its own chunks (17 loads), **0 false walls, 0 prediction misses**; the server held it 12 frames |
| R2. A chunk that cannot load | **given up** (`dead 1`, its warning), a walker beside it held 2 frames, then `map mp/ffa3` loaded and its trace answered |
| R3. The cap | four corners hold 562 MB, under 1,024: **the cap not exercised**; one driver left, 77 MB within 0.2 s |
| R3b, R3c (added after R3: the cap exercised) | 256 MB with four corners: 381 frames over, 0 evicted (all within reach, as required); 256 MB on the walk: **12 evicted behind the walker**, 0 held frames, 0 false walls |
| R4. Idle eviction; quit with a load in flight | **0 resident within 6 s** of no driver (5 s set); **quit to exit within a second** (the exit code was not captured) |

**The 7 start-solid flags in A4.** They are the drops whose start lies inside `-4_6`'s placed bounds, which loaded after
the trace: under section 23 a start strictly inside a non-resident box is start-solid and the trace leaves freely, as
from a brush. Ends, fractions and contents are the full map's. Run 7's reference, taken before section 23, has no such
box.
The gate asked for identity with every chunk loaded, and it is not reinterpreted after the run: A4 is recorded as failed,
explained. For the next gate the method is fixed before running: a checkpoint traces only once no chunk within reach
of the walker is loading (or records the loading ones and compares against section 23's expected answer); A is rerun
then.

**Harness defects found in the run, and redone** (the gate unchanged): R1's server moved the client before it had
connected (it begins about 65 s after the map; the wait is now 90 s); R2's override pack ranked below the chunk pack
in the search order (renamed to rank above it).

**Not changed, by ruling**: held usercmds stay dropped (a hold is a freeze); spectators stay drivers and holdable;
`ClientSpawn`'s direct think is left (all-solid under section 23, then the hold: the respawn observation above); a
capsule meets a box as its bounding box (section 23's known differences).

**The residency radius, measured** (run 11, for the pipeline session's proposal (b): one walker on the first leg at
1,000 u/s, tag 1, 2 cores; `cm_chunkLoadDist` with evict at twice it; 9,600 is run 10's walk on the same build; the
reference is run 10's all-loaded walk):

| Load distance | Chunk blocks: peak, end | Private: peak, end | Resident: peak, end | Held frames | False walls | Nearest loading box | Shortest sweep that differs |
|---|---|---|---|---|---|---|---|
| 9,600 (today) | 805, 712 MB | 1.06, 0.98 GB | 18, 14 | 0 | 0 | 9,220 u | 9,680 u (258 m) |
| 6,400 | 656, 547 MB | 0.93, 0.82 GB | 11, 9 | 0 | 0 | 6,095 u | 8,551 u (228 m) |
| 4,800 | 504, 490 MB | 0.78, 0.77 GB | 9, 7 | 0 | 0 | 4,471 u | 5,961 u (159 m) |
| 3,200 | 394, 394 MB | 0.67, 0.67 GB | 7, 5 | 0 | 0 | 2,878 u | 3,764 u (100 m) |

At every radius the worst server frame was 0 to 1 ms, no sweep ended on a loading box, and every sweep that differs
ends on a non-resident box's face, none further than the full map. Tag 2 holds about 1.85 times tag 1's chunk memory
(section 21). The lead a radius buys is its length over the mover's speed (3,200 u is 3.2 s at 1,000 u/s, against a
worst load of about 0.3 s here); a saturated disk or a faster mover is what the hold is for. Every radius below
8,192 u is shorter than JA's longest hitscan, so a long shot can meet an unloaded box before its range ends (in this
walk the first difference at 6,400 came at 8,551 u, past it, because placed bounds spill past their tiles).

**The owner's choice (2026-11-01): "Keep 9,600".** `cm_chunkLoadDist` stays 9,600 u (evict at 19,200). Memory comes down
through a compact cooked chunk format (proposal c) and a spatial index (a), not through range.

## 25. A spatial index for the chunks (proposal a, 2026-11-01)

Ordered by the pipeline session on the owner's delegation after the owner kept the 9,600 u radius; the gate was fixed
before the code (`look/collision-20261030/res/GATE-spatial-index.md`). Engine uncommitted, not installed.

**What it is.** Every world trace used to visit every chunk slot and then slab-test every non-resident chunk's box,
and point contents looped both: work in the number of chunks, whatever the trace's size. At the manifest a 2D grid of
4,800 u cells now covers the placed bounds, each cell listing the slots whose bounds (widened 2 u: the loader spreads
a model's bounds 1 u, the manifest prints 0.1 u) overlap it. A trace takes the cells its swept box covers and visits
those slots once each, ascending, resident chunks first and then the non-resident boxes, the order of before, so the
answers cannot change. A resident chunk whose own bounds pass its widened manifest bounds, and every slot placed by
hand past the manifest, is visited by every query.

**Run 12** (the 64-bit dedicated server held to 2 cores, hub `cp_watson_hub_col-1`, tag 1; the build before the index
kept as `res/bin_r10` and run beside the index build on one 20,000-trace corpus: drops and 512 u sweeps within
20,000 u, 30,000 u sweeps, point contents):

| State | Lines identical | Per trace, before, after (median) |
|---|---|---|
| Every chunk placed | **20,000 of 20,000** | 72.5, **52.8 us** |
| The hub alone | **20,000 of 20,000** | 6.4, **5.9 us** |
| Mixed: one driver at tile `-5_6`'s centre, nothing pending | **20,000 of 20,000** | 35.9, **32.2 us** (5 repetitions each) |

**The walk**, the first leg with the method fixed after run 10 (2 s at each checkpoint, the chunks loading printed
before its traces): worst server frame 1 ms (the hub alone 1 ms); 0 frames with a driver in a chunk not swapped in;
nothing was loading at any checkpoint; **5,120 of 5,120 checkpoint traces identical to every chunk loaded**; the long
sweeps as in run 10 (25 differ, all on a non-resident box's face, none further). **The gate passes.**

At Watson's 82 chunks the gain is a tenth to a quarter of a trace's cost; it is the flat cost as the city grows that
the index buys.

## 26. Compact collision structures; cooked chunks shelved (proposal c, 2026-11-01)

Ordered by the pipeline session on the owner's delegation; the gate was fixed before the code, and the pipeline
session's conditions were added before the code too (`look/collision-20261030/res/GATE-cooked-chunks.md`). Engine
uncommitted, not installed; landing waits for the owner to see the results. (Part 1 landed with the rest of the engine
work as 970dbb8c, 2026-11-01, the owner: "Yes, commit it".)

**Where a chunk's memory goes.** Planes are 20 bytes and brush sides were 16 (an 8-byte plane pointer each); brushes
were 48 (a sides pointer), nodes 16 (a plane pointer), leaves 40 (two 64-bit offsets). Tag 2's largest chunk has
3.1 M planes, 7.2 M sides and 659 k brushes; its 82 chunks held about 5.46 GB.

**Part 1: compact structures, engine-wide** (its own diff: `res/part1-compact-structures.diff`, 21 hunks). Sides,
brushes, nodes and leaves hold 32-bit indices into their clip map's arrays, not pointers: a side 8 bytes, a brush 40, a
node 12, a leaf 24. The traces and point contents index the clip map they trace (`traceWork_t` carries it). The
structures are private to `codemp/qcommon/cm_*`: `cm_public.h`, `trace_t`, `cplane_t` and the game, cgame, ui and qvm
interfaces are unchanged.

| Part 1 against the build before it (`res/bin_r12`) | Result |
|---|---|
| Identity: Watson every chunk, hub alone, mixed (20,000 traces each) | **all identical** |
| Identity: `mp/ffa3` (526 patches), `mp/siege_hoth` (348 patches, terrain), `cp_kabuki` (5,000 traces each) | **all identical** (2,955, 2,183 and 3,261 of them hit) |
| Chunk bytes, every tag-1 chunk placed | 2,963 to **2,135 MB (27.9% less)** |
| A walker's chunks at the walk's end (the first leg) | 747 to 538 MB; private after the walk 0.98 to 0.79 GB |
| Trace cost, mixed, 5 repetitions (clean retake) | median 632 to 642 ms, within the old build's spread (625 to 653): no regression |
| The settled walk (BSP chunks) | worst frame 0 ms (the hub alone 1 ms); 0 held frames; nothing loading at any checkpoint; 5,120 of 5,120 identical |

**Part 2: cooked chunks (`.cmc`), built, gated and shelved.** A cooked chunk was the compact arrays exactly as in
memory behind a versioned header with an endianness check (fixed-width records, sizes held by `static_assert`), one
read into one block and a validation pass over every index; `cm_cookchunk` wrote one through the same parser and
`hub/cook_pack.py` cooked a pack. It answered exactly as BSP chunks (all three states identical) and held the same
bytes (the arrays are part 1's), and its walk passed (worst frame 2 ms, 5,120 of 5,120). **It failed its own reason
for being**: the cooked load's validation and setup took 206 ms over the walk's 17 loads against the BSP parse's 289
(the most 42 against 58), not the quarter the gate asked for, because checking every index of millions of planes and
sides costs nearly what converting them does; and a load is dominated by read and inflate (up to 283-326 ms), not
parse. Shelved as a patch (`res/part2-cooked-chunks.diff`), out of the working tree.

**A stored pack** (the pipeline session's question: no compression, against today's deflate, the same first-leg walk):
2,118 MB on disk against 689 MB (3.1 times); reads summed 1,187 against 1,605 ms (26% less) but the slowest read was
520 against 301 ms (the 2 GB file only partly in the page cache, one sample). Not the halving that would make it a free
win; deflate stays.

**Recommendation**: land part 1 (the memory, with identical answers on every map tried and no cost); keep part 2 shelved;
load time, if it matters, is in the read and inflate (a faster decompressor, or a smaller resident set per load).

## 27. Watson tag 3: no height cap, the chunks' resident cost, two venue spawns (2026-11-01)

The pipeline session's tag 3 tiles (`cp77/watson/tiles/3/chunks`): every playtest fix (the roll-up shutters are now
collision) and no height cap (`--col-height 400`: the ring and lid at 402 m). 51.9 M collision triangles, 835,519
boxes and 1,601 water volumes, 2.16 times tag 2. Compiled as tag 2 was (xenjk7, `gltf2map --col-chunk --exact-planes`),
two q3map2 at a time at idle priority, heaviest first: `build/cp_watson_colchunks-5`, 89 chunks (none empty) in 90
minutes. The largest (-5_5) has 1,229,739 brushes (tag 2's largest 659,422) and took 526 s. The pack,
`build/cp_watson_hub_col-5/zz_cp_watson_colchunks.pk3`, is 7,990 MB of BSP and 2,523 MB packed (tag 2: 4,063 and 1,317).
Scripts, logs and results: `look/collision-20261030/tag3/`.

**The hubs.** `build/cp_watson_hub_col-5` is tag 3's `base.glb` with col-4's arguments and
`--worldspawn distanceCull=20000` (the owner: the default until the far-city proxies). `build/cp_watson_hub_col-6` is
the same with the `base.glb` the pipeline session rewrote for the owner: two named spawns, outside Misty's Esoterica
(-10106 -39468 190, angle 72) and outside Afterlife (-7275 -44812 420, angle 90), 18 spawns in all, the other 16
unchanged. The sky shell tops out at 15,384.5, 309.5 u above the lid (tag 2's margin too). Three chunks rise above it
(-6_3 to 18,994 u, 506 m; -7_6 and -8_4: tall buildings whose faces start under the cap) and eight edge chunks pass it
sideways by 62 to 1,408 u, as tag 2's did. Above the playerclip lid or outside the ring, that collision is unreachable;
the pipeline session will trim collision above the lid in a converter pass to save memory.

**The spawn gate** (hub/GATE-hub.md check 1: the 64-bit check client, every chunk placed): col-5 0 of 16 standing boxes
in solid, 16 of 16 drops find a floor within 64 u; col-6 0 of 18 and 18 of 18 (Misty's floor 2.9 u under the box,
Afterlife's 2.1 u). All 89 chunks placed with no error; load to the first trace 66 s, peak private 9.4 GB.

**The kits** (not installed; the owner plays after the far-city proxies): `cp_watson_hub_col-5` is the engine as
committed at 970dbb8c, rd-vulkan as committed at 7fb72585 (the renderer session's newest), the tiled scene
`cp_watson_scene-3`, tag 3's hub and pack. Its smoke: exit 0, 89 chunks placed, chunks ready 30.7 s after launch, peak
private 16.5 GB with all 8.0 GB of chunks resident. `cp_watson_hub_col-6` is col-5 with col-6's hub (smoke: 31.7 s,
16.4 GB).

**Resident cost** (section 13's server runs, `m1/run_ded.py`: the 64-bit dedicated server, residency off with
`cm_chunkManifest 0`, the chunks placed by `cm_loadchunk`, as tag 2 was measured):

| Chunks placed | Tag 2 private | Tag 3 private | Tag 3 load to placed |
|---|---|---|---|
| None (the hub alone) | 0.29 GB | 0.29 GB | |
| dense4 (34) | 3.67 GB | **4.95 GB** | 24.3 s |
| all | 5.63 GB (82) | **7.78 GB** (89) | 34.3 s |

Tag 3's dense4 is its own, by section 16's rule (`m1/dense4.py`: the four densest 3 x 3 neighbourhoods by BSP bytes
that do not overlap; on tag 2 it gives back tag 2's 34 tiles exactly): centres -6_5, -5_8, -4_12 and -9_5, 34 chunks,
4,739 MB of BSP. A chunk's load in place: median 278 ms, the most 881 (tag 2: 654). Tag 3 holds 2.0 times tag 2's BSP
in 1.4 times its memory, because this engine has section 26's compact structures and tag 2's runs did not.

**The settled walk** (`res/walk.py`, the first leg from the corner to the densest tile, 2 cores, residency at 9,600 u,
2 s settled at each checkpoint; the all-loaded reference on the same leg; idle priority, the pipeline session's jobs
suspended):

| | Residency | Every chunk loaded |
|---|---|---|
| Worst server frame (the hub alone, retaken quiet: 0 ms) | **1 ms** | 1 ms |
| Held frames; false-wall frames; sweeps ending on a chunk loading | **0; 0; 0** | |
| Loads, evictions; resident peak, at the end | 17, 22; 19, 14 | 89 |
| Chunk bytes: peak, at the end | 2.82, 2.42 GB | 8.03 GB |
| Private memory: peak, after the walk | 3.18, 2.55 GB | 7.78 GB |
| Worker: read, parse (the most) | 697 ms, 132 ms | |
| Long sweeps (section 23's (g) and (k)) | 0 of 1,280 further; 26 differ, every one ending on an unloaded chunk's entry face, solid; the shortest 9,887 u | |

**Tag 3 passes**: both spawn gates, both smokes, (g) with no sweep further, no frame over 1 ms. The cost is memory: a
walker keeps 2.4 to 2.8 GB of chunks and four players in the densest neighbourhoods about 5 GB, against the real
servers' 1 to 4 GB. The proposal to bring it down is collision by bands, section 28.

**Measurement traps** (each measured wrongly once; the wrong runs are kept, labelled):
1. The manifest is read at map load (section 22's decision 3), so a test that places every chunk by hand must turn
   residency off first. m1's first runs (`server_*-manifest-evicted`) counted the spawn regions' preload as "the hub
   alone" (2.0 GB), and with no driver idle eviction (`cm_chunkIdleEvictSec`, 30 s) freed the placed chunks before the
   second sample (0.29 GB "with chunks"). col-6's first spawn gate: the check client's own player drove residency,
   the chunks around the two new spawns, about 40,000 u from it, were evicted during the 600-frame wait, and both
   spawns were traced against unloaded-chunk boxes (contents 1 and no surface; the chunks' geometry reads
   134,250,497) and failed. col-5's 16 spawns all hit real geometry: that gate stands. The rule:
   `run_ded.py --set cm_chunkManifest=0`; for the check client,
   `engine_trace.py --pre "set cm_chunkManifest 0" --pre "devmap <map>" --pre "wait 600" --pre "exec <map>_chunks"`.
2. `cm_chunkManifest 0` typed bare runs the command `cm_chunkmanifest` (a command wins over a cvar of the same name):
   it printed "chunks are already placed on this map" and left the cvar at 1. `set` it.
3. Under the owner's low-priority rule the server runs at idle priority too, so other sessions' idle jobs compete with
   it on equal terms. The hub-alone baseline overlapped the pipeline session's jobs for 35 s and was retaken quiet
   (`a_hub2`); the other timed runs ran with those jobs suspended.

## 28. Collision by bands: a design for review (2026-11-01; not built)

Asked by the pipeline session on the owner's delegation: CP2077 streams small deco collision only within about 64 to
128 m of the player (`worldCollisionNode` streaming distances by level) and structure out to 300 to 600 m. Could two
bands per chunk bring tag 3's dense4 (4.95 GB, section 27) down? The gate, proposed before any code:
`look/collision-20261030/bands/GATE-bands.md`.

**What is in tag 3's collision** (`bands/parts_share.py` over `cp77/watson/tiles/3/build/*.npz`). Each part holds:
- `col`: the mesh triangles, structure and prop meshes merged, with no tag saying which came from a prop;
- `box_G`: one box per small prop.

So each tile's triangles were split into connected pieces (shared vertices), and a piece was called prop-sized by its
bounding box diagonal. That is an estimate:
- a structure mesh's detached small parts count as props;
- a prop welded to its neighbour by the 0.1 m vertex grid counts as structure.

| Tag 3 | All 89 chunks | dense4 (34) |
|---|---|---|
| Boxes (one brush each) | 835,519 (830,016 box and hull brushes) | 514,015 brushes |
| Mesh brushes (51.9 M triangles, merged in pairs where flat) | 31.0 M | 19.8 M |
| Triangles in pieces under 2, 4, 6, 12 m | 10.5%, 16.5%, **21.3%**, 40.9% | 9.8%, 15.9%, **20.9%**, 40.9% |
| Near share of brushes, under 6 m (the converter's prop rule) | 23.4% | **22.9%** |
| Near share of brushes, under 12 m | 42.4% | 42.4% |

A box brush and a merged-triangle brush both have about six sides. The chunk sizes could not separate them: boxes and
mesh brushes rise together across chunks, and the fit gave boxes a negative cost. So bytes were taken as proportional
to brushes. **Most of our collision is structure, not deco**: boxes are 2.6% of the brushes. CP2077's bands rest on
deco being the bulk of collision; in our render-derived collision it is about a quarter.

**The estimate.** A tile is 9,600 u. With a near radius R, the near chunks within R of a player number
(L + 2R)² − (4 − π)R², over L², on average:
- 3.79 at R = 4,800 u (128 m);
- 2.20 at R = 2,400 u (64 m);
- against dense4's 9 for each player (8.14 for structure at 9,600 u).

| dense4 (4.95 GB; chunks 4.66 GB) | Near radius 4,800 u | Near radius 2,400 u |
|---|---|---|
| Near = under 6 m (22.9%) | **4.33 GB** (0.62 saved) | **4.14 GB** (0.81 saved) |
| Near = under 12 m (42.4%) | 3.80 GB (1.14 saved) | 3.45 GB (1.50 saved) |

The bands alone bring the four-player worst case to about 4.1 to 4.3 GB. More is possible only by moving bigger
pieces into the near band, which drops structure collision at range.

**The design.**
- **Which collision goes where** (the converter, by placement, not by these pieces):
  - Near: boxes, prop meshes, thin meshes, and walked meshes (stairs) under `--col-box`.
  - Structure: everything else. That covers placements of 6 m and over, flat and large cutouts (floors, lawns,
    grates, terrain mounds: walkable surfaces stay structure whatever their size), water, and the hub's ring and lid.
- **Each tile compiles two chunks**, `chunk_<t>.bsp` and `chunk_<t>_near.bsp`, both `--exact-planes` in the same
  frame. Together they are the same world.
- **The manifest** gains an optional band token per line.
- **Residency** uses a per-slot load distance: `cm_chunkLoadDist` (9,600) for structure and a new
  `cm_chunkPropDist` (4,800 or 2,400) for near, each evicting at twice its distance.
- **An unloaded near chunk is not solid.** `cm_slotSolid` stays structure-only: a solid box over a whole tile's
  deco would be a false wall inside loaded structure.
- **Holds stay structure-only** (the chunk underfoot). The near radius is far beyond a frame's movement: 4,800 u is
  4.8 s at 1,000 u/s.
- **The spatial index, the worker and the cap** take both bands unchanged.
- **The remote client** runs the same bands around its own player.

What changes for players: long shots, missiles and bot sight lines pass through deco beyond the near radius, as in
CP2077 (a disruptor shot 200 m away goes through a lamp post). Within it nothing changes.

**Before engine code**, the split is measured with no engine change (the gate's step 0):
1. The converter writes the two parts per tile.
2. This session compiles dense4's 34 tiles both ways and measures structure, near and both with residency off.

That replaces this estimate with bytes: an estimated saving is not built on until it is measured.

**Bigger levers, noted, not designed.**
- Structure is about 77% of the bytes, at 1.6 triangles a mesh brush.
- A far structure band at a coarser collision LOD (CP2077 also streams collision by level) or fewer, larger brushes
  per surface would act on the 77%.
- Trimming collision above the lid (the pipeline session's converter pass) acts on three chunks.

## 29. Where the structure's brushes come from (2026-11-01; measured offline, nothing built)

The pipeline session shelved section 28's bands and asked: does gltf2map make one brush per triangle? If so, would
merging coplanar neighbours cut the structure's brushes several-fold with an identical surface?

**gltf2map already merges.** `merge_coplanar` joins a collision node's edge-neighbours into convex polygons, one slab
brush each, when they share:
- a plane, within 0.1 u at every corner;
- a facing;
- an edge.

Each polygon has at most `--merge-verts` 12 corners. Tag 3's 51.9 M triangles became 31.0 M mesh brushes.

**The census** (`look/collision-20261030/merge/merge_census.py`): gltf2map's own `merge_coplanar` run offline on
dense4's 34 chunk glbs. It gives 19.76 M brushes, against 19.79 M in the compile logs.

| dense4: 33.7 M triangles, 19.76 M brushes (1.7 triangles a brush) | Triangles | Left alone (one-triangle brushes) |
|---|---|---|
| Floors (normal z > 0.99) | 4.63 M | 0.99 M (21%) |
| Axial walls | 7.61 M | 0.92 M (12%) |
| Rotated walls | 8.53 M | 1.74 M (20%) |
| Slopes (everything between) | 9.22 M | **6.78 M (74%)** |
| Undersides | 3.69 M | 0.82 M (22%) |
| All | 33.7 M | **11.25 M** (57% of the brushes) |

A further 6.40 M brushes are pairs, quads in effect, and 2.1 M hold three or more triangles.

Of the 11.25 M triangles left alone, by the smallest angle to an edge-neighbour's plane:
- under 0.25°: 2.08 M;
- 0.25 to 1°: 0.35 M;
- 1 to 5°: 1.55 M;
- 5 to 20°: 3.33 M;
- over 20°: 3.59 M;
- no neighbour: 0.35 M.

By area, 6.09 M of them are under 100 u² (about 27 cm square) and 3.96 M under 1,000 u².

**The brush count comes from small sloped and curved pieces.** Rounded edges, bevels, pipes, roofs, roads and terrain
come finely tessellated. The flat facades and floors already merge. More corners change nothing: 32 or 64 instead of
12 gave the same count on tile -4_7.

**An exact grouping changes nothing either.** Edge-neighbours were joined when each one's corners lie within 0.1 u of
the other's plane, instead of the binned normals and distances (`merge/exact_group.py`, gltf2map's own merge with
only the group key changed). On -4_7 that gave 472,887 brushes against 468,689: 0.9% more. Today's binning loses
nothing. So the 2.4 M nearly flat triangles left alone are not coplanar within 0.1 u. They are flat surfaces bent by
up to about 1.9 u, most likely by cp2gltf's 0.1 m vertex grid (every vertex sits on it): a vertex snapped on a gentle
slope leaves the plane.

**What follows:**
1. **No exact merge cuts the structure several-fold.** The surface is genuinely many small non-coplanar pieces.
2. **Recovering the snapped flats** (upstream: cp2gltf without the vertex grid, or a grid that keeps planes) can save
   at most the 2.4 M nearly flat triangles left alone: about 12% of dense4's brushes. That holds only if the original
   CP2077 meshes are coplanar there. It needs a test export of one dense tile to count.
3. **Larger cuts need a different surface.** One way is a coarser collision LOD for structure: today's
   `--col-walk-lod` takes the coarsest LOD of at most `--col-tris` 4,000 that keeps walking. Another is simplifying
   collision meshes to a tolerance. Their gate cannot be trace identity. It has to be geometric, as the level checks
   are:
   - floors within a few units of today's;
   - no new holes, falls or stuck spawns;
   - walls not moved past a tolerance.

**The vertex grid tested** (the pipeline session's export of -4_7 with `--col-cell 0`, tag 3's arguments otherwise;
the same census):

| -4_7 | Tag 3 (0.1 m grid) | No grid |
|---|---|---|
| Collision triangles | 828,176 | 1,328,281 |
| Brushes | **468,689** | **1,052,310 (2.25x)** |
| Single-triangle brushes | 262,655 | 862,852 |
| Of them, under 0.25 degrees to a neighbour | 42,768 | 394,072 |
| Of them, under 100 u² | 135,689 | 643,639 |

Without the grid there are many more nearly flat lone triangles, not fewer. CP2077's own meshes are not coplanar within
0.1 u either, so taking the grid away gains no merges and keeps the tiny triangles the grid collapses. **The grid
stays.** The remaining lever is a coarser structure surface, gated geometrically:
`look/collision-20261030/merge/GATE-coarse-structure.md`, accepted by the pipeline session 2026-11-01 and fixed before
its arms (`--col-tris` 2000 and 1000 under `--col-walk-lod`, dense4's 34 tiles).

## 30. A coarser structure collision: both arms fail (2026-11-01)

The gate (`look/collision-20261030/merge/GATE-coarse-structure.md`) was drafted after section 29, and the pipeline
session accepted it unchanged before the arms were built. The arms are tag 3's dense4 tiles (34) rebuilt by the
pipeline session with `--col-walk-lod`. Walkable surfaces keep the LOD that walks, and everything else is as tag 3:
- **ct2000:** `--col-tris 2000`, 28.7 M collision triangles (tag 3: 33.7 M on those tiles);
- **ct1000:** `--col-tris 1000`, 24.0 M.

This session compiled them (xenjk7, `--col-chunk --exact-planes`, one q3map2 per arm, idle priority) into
`.codex-research/collision-arms/build/cp_watson_colchunks-6` and `-7` (on D:: C: was short of space then). Each arm's
full pack is its 34 chunks plus colchunks-5's other 55, under tag 3's manifest:
`look/collision-20261030/merge/<arm>/zz_cp_watson_colchunks.pk3`. The packs are on C:, as tag 3's reference was
measured there.

| dense4 (34 tiles) | Tag 3 (reference) | ct2000 | ct1000 |
|---|---|---|---|
| M1: collision triangles | 33.7 M | 28.7 M | 24.0 M |
| M1: BSP | 4,739 MB | about 3,990 MB | about 3,320 MB |
| M1: compile, summed over the tiles | 6,438 s | 5,283 s | 4,693 s |
| M2: server private memory, residency off | 4.95 GB | **4.24 GB** | **3.61 GB** |
| M3: brushes (the census) | 19.76 M | 16.70 M (-15.5%) | 13.87 M (-29.8%) |
| Pass 2: spawns in solid; drops finding a floor | 0; 18 of 18 | 0; 18 of 18 | 0; 18 of 18 |
| Pass 3: sweeps ending within 8 u of the reference (99% needed) | | **98.34%** | **97.45%** |
| Pass 3: leaks, of the 15,759 stopped (0.1% allowed) | | **106 (0.67%)** | **177 (1.12%)** |
| Pass 3: new blocks (0.5% allowed) | | 61 (0.31%) | 74 (0.37%) |
| Pass 4: placement | | 89, no error | 89, no error |

19,826 sweeps were scored: the 174 the reference starts in solid are left out. No trace in any run met an
unloaded-chunk box.

**Both arms fail pass 3, so neither is taken.** Their leaks run 6.7 and 11 times the allowed share. Some of the largest
differences are whole sweeps: a 512 u run in tag 3 blocked at once in ct2000, and runs the reference stops at once
passing a full 512 u in the arms. So the coarser LODs move or drop wall faces, not only rounded detail. The timed walk
(pass 4) was not run, because an arm that fails a pass is not taken. M4 (each arm's five worst tiles, by falls + void
and by missing surfaces) was not run: the owner kept the checks off while playing. The verdict stands on passes 2 and
3; if M4 is run later, it is added here.

**Where this leaves the structure's memory.** Neither an exact merge (section 29) nor a coarser LOD at these caps
holds the walls. Tag 3's collision stays as compiled: dense4 4.95 GB, a walker's 2.4 to 2.8 GB. Any further cut
needs a surface simplification that keeps wall faces. One example: decimate to a distance tolerance with the vertical
faces held. It would be gated the same way.

## 31. The round 2 review: fixes and exact memory (2026-11-01; built, not committed)

The second review of the streamed collision (`look/review-20261101b`: Fable's report C and Codex's), done in the
order the owner approved ("Yes, that order"). Every gate was fixed before its code; the gate files are in
`look/collision-20261030/c1/` and `mem/`.

**C1: the brush check counter's wrap** (the review's D1; `c1/GATE-checkcount.md`). `cbrush_t.checkcount` is 16
bits against the clip map's int counter, a bug that predates this work (upstream JA has it). Past 65,535 traces on a
clip map the marks never match, so a brush that spans several leaves is tested once per leaf for the map's life.

The fix: every increment goes through `CM_NextCheckCount`, which at the wrap clears every brush's and patch's mark
and restarts at 1. The brush stays 40 bytes. Quake 3's `c_brush_traces` is restored to count it.

| A single-chunk corpus (-5_5), five runs of 20,000 sweeps | Runs 1-3 | Run 4 | Run 5 |
|---|---|---|---|
| Brush tests, the fix | 7,272,818 | 7,272,818 | 7,272,818 |
| Brush tests, the bug | 7,272,818 | +11.6% | +16.1% |

Every output is identical across runs and builds.

**M1 + M2: sides without shaders or the axial six** (`mem/GATE-sides.md`). The census (`mem/side_census.py`) found:
- on dense4's 34 chunks (237 M sides), every side's shader is its brush's own or the bevel's (`noshader`);
- every brush's first six sides are the axial planes at its bounds, normals bit-exact;
- the stock maps break the shader rule (13-25% of sides), and Kabuki's axial normals are mostly -0.0.

The design:
- A side stores its plane alone (8 to 4 B), a bit marking a bevel.
- A brush whose six axial normals are bit-exact and whose sides follow the rule stores only its other sides. The six
  are built from its bounds when traced, with a 6-bit mask for which of them are faces.
- Any other brush stores all its sides, and a map that breaks the rule keeps an explicit shader per side. So every
  map stays exact, Kabuki included.

**M3: smaller planes** (`mem/GATE-planes.md`). The census: dense4's 104.9 M planes have 33.5 M distinct normals and
are all referenced, so a remap of unreferenced planes saves nothing on the chunks. Four forms were built and gated:

| Planes | dense4 private | Untimed cost, mixed state, against M1 + M2 |
|---|---|---|
| Codex's exact-normal dictionary (8 B and a shared normal) | 2.93 GB | +31% (a scattered second read per plane) |
| Flat 16 B, type and signbits computed on read (M3b) | 3.21 GB | +7% |
| M3b, read in place, the type kept on each node (M3c) | 3.22 GB | +7% |
| **M3c, each side keeping its plane's signbits (M3d)** | **3.22 GB** | **-6%** |

M3d: a plane is its normal and dist. A node keeps its plane's type, and a side keeps its plane index in 27 bits, the
signbits in 3 and the bevel bit. A clip map is limited to 2^27 planes; tag 3's largest chunk has 5.3 M. The
dictionary is shelved (`mem/bin_m3/source.diff`).

**The gates.**
- Identity: Watson's three states (`res/index_test.py`, 20,000 traces each), mp/ffa3, mp/siege_hoth and cp_kabuki
  (`res/stockmaps_test.py`, 5,000 each) and the C1 corpus. Every line identical at every step.
- The timed checks, in the pipeline session's slots on two cores:

| Step | Mixed median, before (range) | After (range) | Every chunk, before and after | The hub alone, before and after |
|---|---|---|---|---|
| C1 to M1 + M2 | 3,056 ms (2,924-3,112) | 2,954 ms (2,906-3,268) | 4,059 and 4,091 ms | 194 and 191 ms |
| M1 + M2 to M3d | 2,908 ms (2,877-2,913) | **2,639 ms (2,629-2,689)** | 3,942 and 3,537 ms | 203 and 199 ms |

| dense4 (34 chunks, residency off) | private | the 34 loads, summed |
|---|---|---|
| Tag 3 as committed (section 27) | 4.95 GB | 15.0 s |
| M1 + M2 | 3.61 GB | 17.0 s |
| **M3d** | **3.22 GB (-35%)** | 15.8 s |

The first M1 + M2 timing run is set aside: a burst outside it slowed one arm. Both M1 + M2 runs also overlapped
another session's client, which fell on both arms. The M3d run had the owner's DaVinci Resolve open, the same for
both.

**C2 and C3** (`mem/GATE-c2c3.md`):
- **C2:** an out-of-memory failure (the parser's, the validator's) is retried after 5 s instead of giving the slot up;
  a missing or bad file is still given up.
  - The spawn preload loads the chunk under each spawn first, then the rest of each region only while the resident
    bytes are under `cm_chunkMegs`.
  - With the test fault `cm_chunkTestFailMem 3`, three spawn chunks failed, were retried and loaded 5 s later, and none
    died.
  - With `cm_chunkMegs 1500` the preload stopped at 17 chunks and 1,504.6 MiB (24 and 3.67 GB uncapped), all 6
    under-spawn chunks in.
- **C3:** a client's map load reads the manifest but skips the spawn preload. The preload had loaded 20-25 chunks in
  place at every remote connect and evicted most on the first frame.
  - Tested with section 24's R1, a remote client through game_lock (`mem/c3_r1`): the client read the manifest, logged
    no spawn preload (`preloads 0`), and its own residency streamed 27 loads around its player over three teleports.
  - No prediction misses, no held frames, no errors.
- **Codex's smaller items:**
  - the worker's stop flag is set under the job lock;
  - the validator runs inside the parse's catch;
  - per-chunk bytes are 64-bit;
  - extreme manifest bounds leave no index rather than overflow the grid;
  - more than 65,535 sides to a brush is rejected;
  - `colbsp.py`'s sizes now match.
  - Identity holds against M3d.

**Instancing, measured** (`mem/instancing_census.py`, the review's M5). Dense4's structure meshes are placed 27.6
times over: 126 M placed triangles against 4.56 M unique, 18.6 times counting bent roads as unique. The top meshes are
beams (placed 4,508 times), fans, walls, windows, freight and canopies. Far over the review's 3x bar: instancing is
the lever left, as a different collision architecture with a geometric gate (section 30's), for the owner to weigh.

**D4, D9, D10 and a zip64 defect found by C3's run** (`mem/GATE-c2c3.md` passes 5-7, build `mem/bin_d2`):
- **D4:** the idle clock resets at a map change.
- **D9:** chunk load lines are developer-only on a remote client.
- **D10:** a slot's spawn wait resets on connect (its spawn count already did).
- **zip64:** the worker's zip reader refused zip64 entries, which Python's zipfile writes for every entry past 2 GB of a
  pack. On tag 3's 2.52 GB pack, the last chunks (-3_13, -3_14, -2_13 in pack order) could never stream: each fell
  back to a load in place on the server frame, a 387 ms frame in C3's run, and a hitch for a player entering those
  tiles in every kit since col-5. The reader now takes sizes and offsets from the zip64 extra field. A pack past 4 GB
  (a zip64 directory) still falls back to the file system.
- **Identity:** against the C2 build, Watson's three states, the stock maps and the C1 corpus, every line identical.
  - The first identity attempt lost its new-build every-chunk run: the server ended without a line during its spawn
    preload, at 13:23, while the other session's workers held about 11 GB.
  - It did not recur in three runs (`mem/idx_d2`, `repro1`, `repro2`).
  - A failed allocation in the file system's read is fatal (Z_Malloc), unlike the parser's, which C2 now retries. It
    is noted, not fixed.

**After the commit, on the committed build** (6b3ebcd7; `mem/r1_d2`, `mem/c4_committed`):
- **R1 again:** `failed 0`, no zip64 line; -3_14, -3_13 and -2_13 swapped in from the worker; the server's worst frame
  0 ms (387 before); no load lines on the client's console (D9).
- **C4, the respawn hold into an evicted region** (the review's D11), measured as the owner plays (a listen server):
  - the player moved to (30000, 40000), which evicted every spawn region, then killed itself and respawned at one of
    the central spawns;
  - **the hold was 3.78 s, on 9 chunks** streamed one after another (worker read 3,072 ms, parse 678 ms);
  - 151 held frames; the worst server frame 1 ms; no violation, no false wall.
  - Round 1's P6 (wait only for the chunk under the spawn and its near neighbours) would bring it to one or two
    loads; not built, the owner's call.
- The review's D7 and D8 (hold details) were not ordered.

## 32. A shorter respawn freeze (P6) (2026-11-01; built, not committed)

Owner, through the pipeline session: "Build the shorter respawn freeze", round 1's P6 on top of de01a42d. The gate
was fixed before the runs: `look/collision-20261030/mem/GATE-p6.md` (build `mem/bin_p6`).

**The change** (`sv_client.cpp` SV_ChunkHeld, `cm_load.cpp` CM_ChunksMissingNear):
- A respawning client waits only for the chunks within `cm_chunkSpawnNear` (2,000 u, to their placed bounds in x and
  y) of its spawn: the chunk under it and the neighbours its first moves can reach.
- The rest of the region streams in behind it, nearest first, each a solid box until it lands. The in-play hold (the
  chunk the player stands in) is unchanged.
- A client's first entry (connect, map load, map_restart) still waits for its whole region.
- `cm_chunkSpawnNear 0` restores the old rule for respawns too: the control.
- `cm_chunkreport2` adds the last spawn hold (ms, chunks missing at entry, loads landed during it) and `pending stops`,
  the traces a loading chunk's box stopped or started in.

**The runs** (`mem/c6_run.py`: C4's respawn into an evicted region, three cycles a run on c4kit's packs, a listen
server; after each kill the player holds +forward, so it runs the moment it is released):

| Same binary, col-9's packs | Respawn holds | Chunks waited on | Median |
|---|---|---|---|
| P6, cm_chunkSpawnNear 2000 (bin_p6) | 2,168, 734, 714 ms | 3, 2, 2 | **734 ms** |
| Control, cm_chunkSpawnNear 0 (bin_p6) | 4,057, 3,557, 3,909 ms | 9, 8, 9 | 3,909 ms |
| P6 again (bin_c5, below) | 805, 762, 734 ms | 2, 2, 2 | 762 ms |
| Control again with samples (bin_c5) | 4,031, 3,911, 3,744 ms | 9, 9, 9 | 3,911 ms |

C4's committed run was 3.78 s on 9 chunks.
- **Identity: pass.** Watson's three states, ffa3, siege_hoth, cp_kabuki and the C1 corpus are identical to bin_d2.
- **The hold: pass.** The median is 734 ms against the 0.9 s mark.
- **False walls 0, violations 0, no death but the cycle's own kill: pass.**
- **"Pending stops 0": failed as fixed.** The P6 arm counted 496, 306 and 131, and the control 396, 156 and 256. The
  counter took every trace a loading box stopped, including the frozen player's own view and crosshair traces on the
  box it waits for. The mark was not changed.
- **The diagnosis, added after the failure and named so** (`mem/GATE-c5.md` pass 3, on bin_c5): the stops split by
  whether a client is held, and into short ones (at most 256 u: a move or a camera) with no client held.
  - Every stop in the three cycles came while the client was held: 166, 151 and 146.
  - Short stops with no client held: 0, 0, 0.
  - **Correction (2026-11-02): this does not show the claim made from it.** The scripted `+forward` never moved the
    player in these runs. Every cycle's final position is its spawn point (`viewpos` prints the third-person camera,
    80 u behind the player), and M-2's walk found the same: velocity 0 in every sample. A standing player cannot
    meet a loading box, so the zero says nothing about a player released into a streaming region. Whether the
    shorter hold lets a moving player reach a loading box is **unmeasured** until the scripted movement works. The
    hold times, false walls 0 and violations 0 above stand.
- The first spawn at map load still takes the whole-region path (preloaded: spawn holds of 0 ms).

## 33. The round 2 open items (2026-11-01; built or measured, not committed)

The owner, through the pipeline session: "work through all the feedback items from fable and astra". The tracker is
`look/review-20261101b/OPEN_ITEMS.md`; this session's items are C-a to C-d and M-1 to M-5. Gates:
`mem/GATE-c5.md` (build `mem/bin_c5`: P6 plus C-a to C-c) and `mem/GATE-m5.md` (`mem/bin_m5`: plus M-5).

**C-a: the remote client's prediction hold trails by the ping** (Fable r2 D7). The hold now also tests the point the
prediction can reach: the snapshot's origin plus its velocity times the ping and a 50 ms snapshot interval (at most
0.5 s). R1's remote client on bin_c5 (`mem/r1_c5`, measured with no mark) matches bin_d2's R1: 0 prediction misses,
0 held frames on the client, 30 against 31 on the server. The case it guards needs a client disk slower than the
server's, which one machine does not give.

**C-b: held clients show the lag icon** (Fable r2 D8). A client the server holds keeps its last-command time current,
so it is not flagged EF_CONNECTION; `levelcheck_where` prints the flag. In the control's three 3.7-4.0 s holds, the
12 samples that caught the client held, at least half of them more than 1 s in, had none: **pass**.

**C-c: static items against unloaded boxes** (Codex r2 #5). An item drops to the floor once, about 200 ms after it
spawns (FinishSpawningItem), and is removed if the drop starts solid. Over an unloaded chunk it starts in the chunk's
box. Now the chunks within 64 u of every static item (item_*, weapon_*, ammo_*, holdable_*, team_CTF_*flag, not
suspended) are loaded in place at the manifest and again before map_restart's entities. They are kept from eviction
for 40 residency frames.
- The test (`mem/cc_run.py`): a copy of col-8's hub with eight items, over three chunks far from every spawn and one by
  spawn 1. The dedicated server loads it, waits 85 s (idle eviction empties every chunk), then runs map_restart.

| | Removed at the load | Removed after map_restart |
|---|---|---|
| bin_p6 (the defect) | the 6 far items | the 6 far items and the 2 by spawn 1 |
| bin_c5 | none | none (its restart preload loaded 5 chunks) |
| Every chunk placed (the truth, bin_p6) | none | none |

- **Pass.** Two fixture errors were found and corrected before the verdict, and both are written into GATE-c5.md.
  - The first wait (900 frames) was 19 s on an idle dedicated server, too short for the idle eviction.
  - The first truth arm (cm_chunkManifest 0) placed no chunk at all. Its runs are kept, marked invalid.
- The two items by spawn 1 show the restart half of the defect: once the server has been empty for 30 s, map_restart
  meets an empty city.
- Not covered: NPC spawners and other entities that trace at spawn (Fable r1 D13); Watson's hub has none.

**C-d: round 1's collision list, audited against the code.**
- **Patch fallback hunk lifetime: closed.** Every chunk load (the worker, in place, cm_loadchunk) goes through
  CM_ParseChunk, which rejects surfaces, so no patch reaches the hunk.
- **Validation: closed.** The visibility header, 6 to 65,535 sides, no surfaces or leaf surfaces, the tree-cycle check
  and the cluster bound are all checked.
- **The chunk pack marked referenced: closed.** FS_PakPathOf sets FS_GENERAL_REF; a loose file ahead of the pack makes
  the load go through the file system.
- **map_restart: closed except one residual.** Its frames run SV_ChunkFrame and C-c covers its items. ClientSpawn still
  runs one ClientThink directly (g_client.c:3834) before the next frame's hold; inside an unloaded box that move is
  all-solid and goes nowhere. Low, left as is.

**Identity** for bin_c5 and bin_m5 against bin_d2: Watson's three states, ffa3, siege_hoth, cp_kabuki and the C1
corpus, every line identical.

**M-1: keep only the referenced planes** (Fable r2, pipeline B). The census (`mem/plane_census.py`, dense4's 34
chunks):
- 104.9 M planes in the lumps, none unused (q3map2's slim pass already drops them);
- 13.3 M (12.7%) are used only by implicit axial sides, which the engine builds from bounds since M1/M2;
- dropping them at load would save **0.20 GB** of M3d's 3.22, against the review's 0.6-0.8 GB estimate.
- Parked (the pipeline session), unless it comes nearly free with M-2.

**M-2: collision instancing.** A design and its geometric gate for the owner:
`look/collision-20261030/inst/DESIGN-M2.md`. The frames census (`mem/instancing_frames.py`) sorts the instanced
placements' triangles:

| Frame (unit scale unless named) | Triangles | Share |
|---|---|---|
| axis: 90 degree turns and mirrors | 3.51 M | 2.8% |
| yaw: a turn about z only | 65.45 M | 52.0% |
| tilted | 13.33 M | 10.6% |
| scaled (16.7% of them uniform) | 43.68 M | 34.7% |

The first build would take axis and yaw at unit scale: 54.8% of the instanced triangles, about 1.3 GB (40%) of
dense4 by estimate. The gate's step 0 (the converter's split, compiled and measured before any engine code) goes on
only at 25% or more. M-3 (triangle soup) waits for step 0.

**M-4: section 28's step 0.** The chunk glbs hold collision merged by grid cell (`col_<i>_<j>`), with no tag for which
placement a triangle came from, so the split into structure and near bands has to come from the converter (step 1,
the pipeline session's). This session compiles and measures it when the converter writes it.

**M-5: residency by time to reach** (round 1's P3; `cm_load.cpp` CM_ChunkResidency):
- **Order:** the worker takes the queued chunk a driver reaches first: its bounds distance over the driver's closing
  speed toward it, at least `cm_chunkPrioSpeed` (400 u/s). A standing driver keeps the old nearest-first order.
  - A client's velocity comes from its playerstate: the server passes every client's, a remote client its own.
  - A virtual driver's velocity is its move since the last frame.
- **Cancel:** a queued chunk no driver wants any more is dropped before its read (`cancelled` in cm_chunkreport2).
- **Prefetch:** `cm_chunkPrefetchSec` (off by default) also wants the region around where a driver's velocity takes
  it in that time.
- Identity holds.
- The walk A/B is decided by load timing, so the first walks (shared machine) are a smoke test only:

| Smoke walk (res/walk.py, 1,000 u/s, two legs) | Loads | Violations | Nearest pending | Cancelled | Peak |
|---|---|---|---|---|---|
| bin_c5 | 37 | 0 | 8,773 u | | 2.26 GB |
| bin_m5 | 37 | 0 | 8,866 u | 0 | 2.28 GB |
| bin_m5, prefetch 1 s | 37 | 0 | 9,641 u | 0 | 2.23 GB |

- **The quiet rerun** (the pipeline session's slot, its levelcheck suspended; `mem/walk2_*`):

| Quiet walk | Loads | Violations | Nearest pending | Cancelled | Peak |
|---|---|---|---|---|---|
| bin_c5 | 37 | 0 | 8,866 u | | 27 chunks, 2.26 GB |
| bin_m5 | 37 | 0 | 8,823 u | 0 | 27 chunks, 2.20 GB |
| bin_m5, prefetch 1 s | 37 | 0 | 9,672 u | 0 | 27 chunks, 2.24 GB |

- **Pass, with no measured gain.** At 1,000 u/s the 9,600 u radius gives a walker about 9 s of lead, so the queue
  never holds competing chunks near it. The order and the cancel would act only when the worker is saturated
  (several players, a slow disk, sharp turns), which this walk does not make.
- Prefetch moves the nearest loading box about 850 u further out at no memory cost here; it stays off.

## 34. A 128 k world for the joined map (2026-11-03; built and tested, not committed)

Watson and the next area (WISHLIST 15, AREAS 'night_city') play as one map in Watson's frame. The far edge is
112.8 k u out, past the engine's +-64 k. Worktree `.codex-research/wt-world128` (83b7b748), four edits:

- `q_shared.h`: MAX/MIN_WORLD_COORD +-128 k (131,072). Float spacing at 113 k is 0.0078 u.
- `cm_polylib.h`: MAX_MAP_BOUNDS (65535) is now MAX_WORLD_COORD. This was the one hidden collision limit:
  CM_ValidateFacet drops every patch facet whose bounds pass it, so patches past +-65,535 were not solid.
- `cg_draw.c`: the crosshair trace's literal 131072 becomes WORLD_SIZE (the joined diagonal is about 226 k).
- `vk_scene.cpp`: SCENE_GRID_MAX 64 -> 128 (the renderer session's, for the merged scene pack).

Checked and unchanged:

- msg.cpp sends origins and trBase as full floats, and pmove's snapping is exact at this size.
- sv_world sizes its sectors from model 0.
- Bot2 hashes integer cells, and bot2_corner already accepts +-131,072.
- Chunk residency (MAX_CM_CHUNKS 1024, the 4,800 u grid) holds the 263 chunks of tag 9.
- The renderer session's review found nothing else bounded in rd-vulkan. Its zFar, cascades, sky and culling work
  relative to the eye or in floats fine enough at this size.
- code/ and codeJK2/ (single player) are left alone.

**The test** (`look/collision-20261030/world128/`: `make_map.py`, `ab.py`, `test.sh`):

- A stand-in map compiled with q3map2 xenjk4 (+-262,144): three identical rooms, at the origin, at (74,000,
  110,000) and at (-74,000, -110,000), each with a patchDef2 hump and a brush step.
- The same check client ran twice: kit col-13's binaries (the same codemp source without the edits) and the new
  ones.
- Each run traced 30 lines through levelcheck_trace (drops, sweeps, point contents), then dropped the player onto
  each target with setviewpos and read viewpos.

| Far rooms against the control | Old (64 k) | New (128 k) |
|---|---|---|
| Brush drops, wall sweep, point contents | identical | identical |
| Box drops on the patch hump | end on the floor (z 24 against 152) | identical |
| Sweep across the hump | passes through | identical |
| Player dropped on the hump | falls through to the floor | stands on it (eye z 168) |

The far rooms render like the control: the sky, the walls, and a character lit by the BSP light grid (523
distinct samples, under the 65,536 an unsigned short index holds). The console has no warnings.

Two earlier runs are kept as evidence, invalid for the settled check only:

- Run 1: the first spawn stays a spectator under the player menu, so the client must `team free` and then
  `togglemenu`.
- Run 2: TeleportPlayer pushes the player out at 400 u/s along the yaw for 160 ms, which carried every drop from
  50-100 u up off its target. Drops now start 2 u above where the box rests.

**The joined hub** (`look/collision-20261030/hub9/`) is built from tag 9's base.glb:

- **Compiler:** q3map2 xenjk8 = xenjk7 + `world_coords.patch`. Chunks stay on xenjk7, in their own frame.
- **Ring:** cp2gltf's own, along night_city's 'half' box.
- **Void kill:** a trigger_hurt (dmg 9999) over the ring's box, its top 2 m under the lowest collision, so a fall
  into the bay or the badlands kills instead of landing on the sky shell's floor.
- **Spawns:** col-12's 18 (Misty's and the Afterlife among them).
- **Light grid:** must stay under 65,536 distinct samples; past that the hub compiles again with the vertical
  spacing doubled.
- **Dry run on tag 8 passed:** 54,451 samples, and the trigger is model 1 over the ring's box.

**The joined hub on tag 9** (`hub9/tag9/`, 2026-11-04): -bsp 1,052 s and -light 7,839 s, 43,126 grid samples (grid
928 x 912 x 976, no second compile), worldspawn, the trigger_hurt (model 1: x +-76,875, y +-112,875, z -9,446.25 to
-6,446.25) and the 18 spawns. The lowest chunk (-3_12, z -6,371.4) is 74.85 u over the kill's top; the farthest chunk
bound is 122,677.5, inside +-131,072. 18 chunks reach outside the ring and some rise to z 22,531 over the sky shell,
unreachable as in tag 3.

**The joined kit** (`build/cp_watson_dev-night-2`: the pipeline session's, the world-128 binaries, 224 chunks in three
packs, 21.1 GiB of BSP):

- **Spawn check** (`hub9/night2_check.sh`; residency off, the 11 chunks within 2,048 u of a spawn placed, since every
  chunk is more than the free memory): 0 of 18 standing boxes in solid, 18 of 18 drops find a floor (Misty's 2.95 u
  under the box, the Afterlife's 2.12), no unloaded-box hits, 11 of 11 placed, no warnings; 49 s to load, 3.18 GB peak.
- **Residency smoke** (`tag7/col13_smoke.sh`, the kit as built): exit 0, the manifest's 224 chunks, the spawn regions'
  11 loaded in 4,656 ms, chunks ready 10.8 s after launch, 7 loads and 7 evictions, 0 violations, 0 false walls, the
  worst server frame 1 ms, 4.07 GB private peak. The smoke's screenshot command is unknown to this engine (no image).

**The kit binaries with water** (`world128/bin-kit`): the renderer session's `water2.patch` applied to wt-world128
(uncommitted) and built in the same tree. Only rd-vulkan changed (sha256 6d84dd52...); the other five modules are
byte for byte world128/bin's. Not yet run: the renderer session's look check comes first.

## 35. The round 3 review's collision items (2026-11-06; built and tested, branch collision/r3-residency)

The review (`look/review-20261106/REPORT.md` item 15 and P11; Astra's D4, 8 and P3, Fable's D9 and P3-P6). Each was
checked in the source first; all were real. Branch `collision/r3-residency` on 2e3d981f; tested merged with the
world-128 commit (5be30d23), as every kit since night-2 runs it.

- **A loading chunk crashed vehicles (Astra D4).** CM_ClipUnloaded's stand-in box returned surfaceFlags 0, so
  PM_VehicleImpact took it for the world and dealt crash damage. The box now carries `SURF_CHUNKBOX | SURF_NODAMAGE`
  (`SURF_CHUNKBOX` is the unused surface bit 0x04000000), and PM_VehicleImpact returns at once on it, on the server and
  in prediction: the box still stops the vehicle, it no longer damages it. SURF_NODAMAGE also spares a fall onto a box
  (PM_CrashLand).
- **The spawn audit read a failed chunk as a complete set (Astra 8).** A given-up chunk releases the hold, as before;
  the release now counts it apart: `CM_ChunksDeadNear`, cm_chunkreport2's `spawned beside dead`.
- **Two radii that had to agree (Fable D9).** The spawn preload's radius is never less than the first spawn's hold
  (`cm_chunkSpawnNear`, or the whole region with either at 0); `cm_chunkSpawnPreload`'s default is 0 (the hold's), -1
  still the whole region. The manifest line and cm_chunkreport2 print both.
- **Memory bound (P11, Fable P3).** `cm_chunkMegs` defaults to -1: 45% of physical memory, at least 512 MB
  (`Sys_PhysicalMemoryMB`; 14,651 MB on the 31.8 GB owner box). 0 is still no cap. The cap counts transient bytes as
  well (`cm_transientBytes`: the worker's read buffer, parsed chunks not swapped in, retired chunks not freed). Over
  it, residency evicts beyond reach as before, and the spawn preload stops, the chunk under a spawn included (`preload
  cap skips`; a spawn left out waits in its hold). A chunk within the load distance still loads over the cap: the
  first build put those beyond `cm_chunkNearDist` off, and smoke B (below) showed the cost.
- **The preload to the worker (P11, Fable P4): built, measured, off.** With `cm_chunkPreloadAsync 1` only the chunk
  under each spawn loads in place and the rest of each set is queued. On dev-night-2's packs the map load's preload
  took 3,501-3,600 ms against 4,656 (8 in place, 3 queued, all swapped in before the first client). But Bot2's world
  graph is built during the game's initialisation, straight after: it traced 2,196 times against the three boxes still
  loading (`pending stops 2196`, 0 with every chunk in place), so the graph saw walls there. Default 0.
- **Spatial readiness queries (Astra P3).** CM_ChunksMissingNear, CM_ChunksDeadNear and CM_ChunkReadyAt take their
  slots from the chunk index (section 25) for the square around the point, every manifest slot without an index.

**Tests** (`look/collision-20261030/r3col/`: `check.sh`, `check.log`; runs 1 and 2 kept as `*-run1`, `*-run2`):

- **Trace** (levelcheck_trace on dev-night-2's packs): vertical player-box sweeps onto three chunks more than 25,000 u
  from any spawn stop on their boxes' tops (z 4,779, 13,821.7 and 4,040.2, each the placed top + 24) with surfaceFlags
  0x04040000; the drop at a spawn ends on the real floor (z -92.125, 0x00300000).
- **Residency smokes** (tag 7's, `hub/smoke.py --residency`, this engine on dev-night-2's packs).
  A is the default cvars, B is `cm_chunkMegs 600` (the 1,024 of run 1 never bound: 942 MB peak). Night-2's own
  run: 11 in place at 4,656 ms, loads 7, evictions 7.

  | run | engine | A | B |
  | --- | --- | --- | --- |
  | 1 | 054de50f, queued preload on | pass; 8 in place + 3 queued, 3,501-3,600 ms; pending stops 2,196 | pass at 1,024 MB (cap never bound) |
  | 2 | fb0fa1a1, queued preload off | pass; 11 in place, 4,300 ms; pending stops 0 | **fail**: 7 in place, 10 preload cap skips, 9 deferred loads; one false-wall frame at 915 u, client held 10 frames |
  | 3 | 85f0cd26, no deferral | pass; 11 in place, 4,542 ms; loads 8, evictions 7 | pass; 7 in place, 10 preload cap skips; violations, false walls, client held all 0 |

  Every run: violations 0, failed 0, peak transient 235 MB. Run 3's B still peaks at 967 MB, over its cap
  (`cap misses 210`): the chunks within reach of the players weigh more than 600 MB. Below the working set,
  the cap bounds the preload and what lies beyond reach, not what lies within it.
- Not exercised in a game: a bike driven into a loading chunk (the impact path is the one early return above).

## 36. The kit's collision from committed tools (2026-11-06; built and checked, branch collision/kit-driver)

The baseline (docs/BASELINE.md) rebuilds the Watson kit from the CP2077 extraction with committed tools only. A
provenance trace of dev-night-29 found its collision built entirely by uncommitted scripts: tag10b.sh's chunks step,
tag10c.sh with tag10c_list.py and mem_ok.py, kit10.py chunks and patch_kit.py for the packs, and hub9/hub10.sh with
hub_inject.py and spawn_corpus.py for the hub, whose El Coyote Cojo spawn was picked by hand. 26 of the kit's 195
chunk BSPs were still tag 8 and 9 builds. `tools/levelport/cp77_collision.py` replaces that chain;
build_watson.py's collision stage calls it:

    python -I tools/levelport/cp77_collision.py <area> <tiles tag> --map cp_watson
        --out <build>/cp_watson_collision-<tag> --q3map2 <xenjk8 q3map2.exe> --q3map2-chunks <xenjk7 q3map2.exe>

- **Input.** The tiles tag's collision parts (`tiles/<tag>/build/<tile>.npz`, the driver's parts stage, with
  cp77_tiles' collision arguments). Its own steps: cp2gltf chunks; the hub's glb; every reachable tile's chunk BSP; the
  packs; collision.json.
- **Reachable tiles.** A tile wholly outside the void kill's box is not compiled (kit10's rule). Tag 10: 54 of 268.
- **Compiles.** Largest glb first, three workers at `--jobs 4 --tile-parallel 2`. Each start waits for 6 GB
  available, 8 GB of free commit, 25 GB free on C: and D:, and 45 s after the last start (tag 10c's gate). The hub
  compiles beside them. A tile stopped half way is moved aside and compiled again; `--idle` (while the owner games)
  is Idle priority with one worker.
- **Packs.** `zz_<map>_colchunks.pk3` (then 2, 3, ... at 3.5 GB) with `maps/<map>.chunks` and `<map>_chunks.cfg`
  in the first, each BSP copied as compressed (make_chunks_pk3.py's R2), and the hub `zz_<map>.pk3`. They are the
  only `.pk3` files at the top of `--out`, which is made once through `<out>.partial`.
- **The hub's data** (`tools/levelport/areas/night_city_b_hub.json`, hand-made, not CP2077 content): the pocket
  (El Coyote Cojo's block), the grid check's point, the hub compile's arguments, and the spawns. Those are kit
  col-12's 18 in their order and El Coyote Cojo's, with the note on how it was picked.
- **Two q3map2 builds.** The hub needs world_coords (xenjk8: it reaches past +-65,536 u). The chunks must not take it:
  xenjk8 makes other chunk BSPs than xenjk7 (below), as section 8 found for coordinates far out, because
  world_coords' larger base windings round other float bevels.

**The check** (`.codex-research/collision-colkit/`: `compare.py`, `compare-*.json`, `check*.out`). Tiles tag `10colck`
is tag 10's 268 collision parts, copied, so tag 10's own outputs are the reference:

| Check | Result |
| --- | --- |
| The 268 chunk glbs and the manifest against tiles/10/chunks | geometry identical (each glb's binary chunk byte for byte); the JSON differs only in the tag named in its source line, the manifest only in `tiles_tag` |
| Four tiles' chunk BSPs (-4_14, -2_9, -2_-3, -1_3; 35-155 MB) against tag 10's colchunks-12 | **xenjk7: identical** (size and CRC). xenjk8 (run 1): 176-640 bytes smaller each, so the chunks take their own build |
| Those tiles' `maps/cp_watson.chunks` and `cp_watson_chunks.cfg` lines against dev-night-29's | identical (origin and placed bounds) |
| The hub against collision-hub10's | every lump identical but the light grid and its array; classes, model 0, the 19 spawns in order and the trigger_hurt (model 1) identical; 42,265 grid samples against 42,876 |
| hub10's own `base_hub.glb` compiled with the data file's arguments | every lump identical to hub10's compile, light grid included (42,876): the arguments reproduce it, and -light is deterministic |
| The tile selection (manifest and void kill box, less tag 10's and 9's empty builds) against dev-night-29's 195 | nothing extra, nothing outside the ring; the 12 night-29 tiles not accounted for have no tag 9 or 10 build (tag 8's, the inherited ones), and will compile in the full run |

The hub's light grid comes from the base glb's lights. Today's cp2gltf writes 115,705 of them, against tag 10's
119,874 (the converter changes since tag 10, a6d78961 and ad5d22b1). q3map2 drops light entities from the BSP, so the
entity lump is still identical. That is a converter input, not this tool.

**Timings** (the four-tile check; the full run compiles about 200 tiles):

| Run | Chunk glbs (268) | Four compiles | Hub | Whole |
| --- | --- | --- | --- | --- |
| 1: Idle, one worker, owner playing; xenjk8 for both | 30 min | 30-215 s each | 80 min | 80 min |
| 2: normal priority, three workers; xenjk7 and xenjk8 (commit 6c684045) | 5 min | 14-108 s each (the slab cache warm from run 1) | 45 min | 50 min |

Tag 10c compiled 171 tiles in 4 h at Idle with three workers. A full run at normal priority should take about 3-4 h,
with the hub beside it. Disk: about 7 GiB of packs (dev-night-29's three: 7.1 GiB) and as much again in the
per-tile builds under `<out>/chunks`, plus the tiles tag's chunk glbs (7.5 GB).

**A worker lost a tile (tag 11, 2026-11-07).** The tag 11 run compiled 213 of 214 tiles and ended "1 tiles not
compiled (-4_-3)" with nothing logged for it. Worker 2's last tile ended at 21:55:29; the other two worked on to
00:14. The only exit on a worker's path before it logs is free_ok's stop in wait_start (C: or D: under 25 GB), a
SystemExit, which a thread drops without a word. The output was on C:. Now every exit or exception of a worker or
the hub compile is logged (an exception's traceback too), stops further starts, and ends the run with its reason
and the tiles left. A low drive still stops the run, now aloud. Checked offline with the real compile_all and
wait_start and stand-in compiles: a stop on the third start, an exception, a hub failure and a clean run.

Not run here: the full clean build (the tag 11 import's), and a game client on the result. The packs follow
dev-night-29's layout, and the residency smoke ran on that layout (section 35).

## 37. Collision cooker A: chunk BSPs from brush records (2026-11-07; built and gated, branch collision/brushcook)

The owner's direction after section 36's scope ("Option 1: keep format", then "ok go" on cooker A): the chunks'
brushes are made by q3map2's own brush code from binary records, with no .map text, no split into tiles on disk and
no q3map2 BSP; the BSP format, the engine and colmerge's tree are unchanged. The chunk BSPs had already been our own
container since section 14 (colmerge.merge keeps only q3map2's shaders, planes, brushes and sides and writes its own
kd-tree), so what q3map2 still did for a chunk was its brush steps.

**What runs** (commits d8a0b00f and 077c5984):

- `gltf2map --col-chunk --exact-planes --cook`: the converter's brushes as records (`colcook.Recs`): per side the
  three points the .map text would hold and its shader. Each value is exactly the double q3map2 reads from
  `mapio._num`'s text: `float('%.4f' % v)`, -0 as 0, computed as `rint(v * 1e4) / 1e4` (a correctly rounded
  division gives the double nearest the decimal), with the text itself for the few values within 1e-6 of a half.
  Mesh slabs come from `_slab_job_cook` (the slabs `_slab_job` makes; a function of its own so the slab cache's key,
  `_slab_job`'s code, stays), box and hull brushes from `brush_text`, the sky shell from its text. The slab cache is
  not used.
- `colcook.cook`: colmerge.split's tiles (the same centres, the same cut), each tile's records through
  `q3map2 -brushbin - -` on a pipe, its shaders, planes, brushes and sides back; colmerge.merge (chunk=True) and
  colbsp.slim in memory; the file laid out as a q3map2 tile's, with the entity text a tile's `-bsp` writes (its
  command line included). `--cook-compare <packs>` checks the result against a kit's member instead of packing it.
- `brush_bin.patch` (q3map2 build xenjk9 = xenjk7 + `brush_cook.patch` + `brush_bin.patch`): `-brushbin` runs, per
  record, the steps `ParseRawBrush` (the plane from three points on the 1/16384 grid, `-xplanes`' plane, the shader's
  flags), `ParseBrush` (duplicate and mirrored planes, contents) and `FinishBrush` (`CreateBrushWindings`,
  `AddBrushBevels`) take for a world brush, then `EmitBrushes`' order (opaque brushes reversed, translucent after) and
  `EmitShader`, each tile with an empty plane table as its own run would have. `-brushcook` (`brush_cook.patch`), the
  oracle: a .map's brushes only, as `-bsp` emits them, no tree.
- cp77_collision.py: the chunks cooked (`--text`: the old path), no scratch; each chunk's BSP raw-copied into the
  open merged pack as its compile ends and the tile's pack removed. Full packs close as `zz_<map>_colchunks2.pk3`,
  3, ...; the last, with the manifest and the cfg, as `zz_<map>_colchunks.pk3`. A pack open when a run stops is
  moved aside on the resume and its tiles compile again.

**Why byte identity is reachable.** A 4-decimal value never lies on a 1/16384 half step (the difference is an odd
multiple of 1/20,480,000, at least 4.9e-8), so a point within a few ulps of the text's value snaps to the same grid point: the
binary records give q3map2 the same snapped points as the text. Everything after that is q3map2's own code. With
`-exactplanes` a brush's planes depend on the brush alone, so the tiles' plane tables are reproduced by giving each
tile its own. q3map2 kept every brush it read (93,225,562 of 93,225,562 across the 195 chunks), and the only order it
adds is the opaque/translucent split.

**Gate 1: byte identity** (`.codex-research/collision-cook/gate11/`, `gate.py`, `summary.json`). Every placed chunk
of tag 11 cooked from its glb against kit baseline-11's packs:

| | |
| --- | --- |
| Chunk BSPs identical to the kit's, whole file | **195 of 195** (24.06 GB of BSP) |
| -6_0, the largest (2.58 M brushes) | 288 s against 577 s; peak 7.5 GB in the converter's process |
| -1_3 | 45 s; peak 4.1 GB with its four converter workers |
| Compile time, the 195 (two workers, BelowNormal, beside the drop runs) | 10,173 s of chunk time, 170 worker-minutes (the text path: 310) |

**The kit** (`cp_watson_collision-11cook`, cp77_collision.py with xenjk9; kit `cp_watson_cook-11`: baseline-11 with
this collision, `importgate/assemble_kit.py`, `kit-compare.json`):

| | |
| --- | --- |
| Chunk BSP members against baseline-11's | 195 of 195 the same size and CRC |
| Manifest and cfg lines | identical |
| The hub pack | the same 10 members, the same contents (only the zip dates differ) |
| Disk on the way | no .map, tile or scratch files; per-tile packs removed as they are packed; 7.4 GB at the end, as before |
| Run | 3.0 h, 209 compile worker-minutes at BelowNormal beside four measurement jobs (tag 11's text run: 3.6 h, 310) |

**The import gate**:

| Check | Result |
| --- | --- |
| Spawn gate (engine.sh: residency off, the 13 chunks within 2,048 u of a spawn) | 0 of 19 boxes in solid, 19 of 19 drops find a floor, 0 unloaded-box hits; every line identical to baseline-11's |
| Residency smoke | PASS: violations 0, false walls 0, failed 0, worst server frame 1 ms |
| Per-tile compare, collide and drop (levelcheck, the 210 tiles phase 4 runs; `importgate/cmp_levelcheck.py`) | **identical to baseline-11's**: stats.json, collide.json, drop.json, the stitched cats, samples and solid arrays and all 210 tiles' drop rows (NaN as NaN). The same comparison against dev-night-29 finds its differences |
| The settled walk (`importgate/walk2.py`: res/walk.py taking the three packs; the first leg, 2 cores, 2 s settled at each checkpoint; the same dedicated server for both kits) | **5,120 of 5,120 checkpoint lines identical** to baseline-11's; worst server frame 0 ms (baseline-11: 1 ms); 0 held frames, 0 violations, 0 false walls, nothing pending at any checkpoint; the same 17 loads and 23 evictions |
| The same walk with every chunk loaded (the reference) | 5,120 of 5,120 identical to the residency walk; the counter: 195 resident, 15,267,852,376 bytes (14.22 GiB), the server's private memory 14.61 GiB |

Not changed: the hub (still q3map2 -bsp with world_coords, xenjk8) and its 1.1 M-leaf tree (section 35's stutter).

## 38. Collision options C and A: compressed installed chunks and a slab package (2026-11-08; built and gated, branch collision/brushcook)

The owner chose from section 37's measurements: "can we do a and c", then "Yes, that way": **C first**, the installed
chunks compressed (the engine decodes them to today's exact BSP), then **A**, the chunks shipped as their brushes'
inputs and expanded once to C's form. The package is moved to `.codex-trash/` after a verified expansion (195 of 195
byte-identical) and kept if the check fails. zstd's window stays at level 19's own (windowLog 23, 8 MB): no long-range
mode (a 1-4 GB server decodes several chunks at once).

### C: XBSZ chunks

- **Format** (`tools/levelport/colzbsp.py`): the RBSP's header, then each lump as one zstd frame with its content
  checksum. Brush sides, brushes, leaf brushes, nodes and leaves are byte-shuffled first (byte k of every record
  together); planes are not. Decoding gives the original file, layout and padding included. The member is stored in the
  pack (its lumps are compressed already) under its usual name, `maps/chunk_<tile>.bsp`.
- **Writer**: gltf2map `--cook --xbsz` (`--xbsz-level`, 19); cp77_collision.py makes it the default (`--plain`: deflated
  BSPs as before); `colzbsp.py packs` converts a kit's chunk packs. colbsp.read decodes XBSZ, so levelcheck, probe and
  the other offline tools read both.
- **Engine** (`codemp/qcommon/cm_zbsp.cpp`): CM_ZipRead's or FS_ReadFile's buffer, when it is XBSZ, decoded to the BSP's
  bytes before CM_ParseChunk, on the residency worker or the synchronous load; the load log gains `decode N ms` and
  `cm_chunkreport3` the decode totals. zstd's decoder is built in: `lib/zstd` (upstream 1.5.7, decoder sources only,
  lib/VENDORED.md) is a static library of the engine and the dedicated server, so no zstd library ships. (Built without
  it, without `XENJK_ZSTD_LINKED`, cm_zbsp.cpp loads `libzstd.dll` or `libzstd.so.1` at run time, and a binary that has
  neither reads plain chunks as before.) A plain lump decodes in one shot into its place; a shuffled one streams out of
  the decoder in 128 KB blocks, each byte put straight at its record's place, so no lump-sized scratch is held.

| C, tag 11 (all 195 chunks) | |
| --- | --- |
| Installed collision | **5.30 GiB** (5,691,591,287 bytes; deflated today 7.14 GiB: 0.742x), two packs |
| Compression, level 19, the city | 595 s on 12 workers, 7,101 CPU-seconds (with a decode check of each) |
| Decode identity, the engine's decoder (cm_zbsp.cpp in a test program) | **195 of 195** byte for byte; every frame checksummed; the largest window 8 MB |
| A load's memory, peak private bytes of the decoder's test program | -6_0 **0.99 GiB**, the compressed member and the decoded BSP (today compressed + inflated, 1.06 GB). The first decoder, one shot per lump into a lump-sized scratch, held the largest shuffled lump as well: 1.41 GB |
| Decode identity, the built-in decoder (lib/zstd, the same test program) | **195 of 195** byte for byte |
| Decode time, idle machine, five alternated runs each (the test program) | -6_0 (208 MB of XBSZ): msys2's libzstd.dll 853-970 ms (median 876), built in 866-892 ms (877); -5_2: 122-130 against 120-133. The built-in decoder has no assembly (MSVC cannot build zstd's) and is no slower |
| Load on the residency worker, the same server on both kits (3 runs) | -6_0: read 215 + decode 1,145-1,233 ms against read (with inflate) 2,080-2,290 ms; -5_2: 21-31 + 157-212 against 259-278; -9_4: 1-2 + 5-6 against 16-18. Parse unchanged (-6_0 435-529 ms) |
| Trace cost, every chunk loaded, 51,200 traces a run (each checkpoint's 256 ten times) | plain 1,461-1,507 ms, C 1,459-1,478 ms: unchanged. (Runs of 5,120 traces, about 100 ms, read +13% once: too short) |
| Spawn gate (kit cp_watson_zc-11) | every line identical to baseline-11's |
| Residency smoke | PASS: violations 0, false walls 0, worst frame 1 ms; 6 chunks decoded on the worker |
| Settled walk, twice on each kit | 5,120 of 5,120 identical to cook-11's; 0 violations, 0 held frames |
| The driver's own run (XBSZ default, cp_watson_collision-11z, tag 11, normal priority, 3 workers) | 195 of 195 members stored XBSZ decoding to cp_watson_collision-11cook's BSPs; the manifest and cfg the same; 5.69 GB; 5,825 s with the hub beside it; 268 compile worker-minutes (plain: 220; the encode and its check about 48) |
| Its kit (cp_watson_z-11): spawn gate, residency smoke | spawn lines identical to baseline-11's; smoke PASS (violations 0, false walls 0; 5 loads decoded on the worker, 227-270 ms each) |
| The same packs, the engine with the built-in decoder and no zstd library (cp_watson_zv-11) | spawn lines identical to baseline-11's; smoke PASS (violations 0, false walls 0; 5 loads decoded on the worker, 244-492 ms each, with the driver's 4-worker build running beside it) |

### A: the slab package

- **Package** (`tools/levelport/colpack.py`): a chunk as its brushes' inputs: per mesh slab the polygon's corners, face
  plane and thickness exactly as gltf2map gives them to slabwriter (doubles); the box and hull brushes and the sky
  shell as their records, each point value as the integer k = v x 1e4 (exact: v is the double nearest k / 10,000); the
  shader names, the worldspawn keys, the chunk's shader file and the cooker's settings; no planes, sides or tree.
  Columns, the whole zstd -19. `colpack.slab_points` is the one slab arithmetic: the converter's `--cook` path and the
  expansion both run it.
- **Writer**: gltf2map `--cook --package FILE [--package-only | --no-pack]` (`--no-pack`: cooked once, `FILE.json` holds the
  BSP's sha256 and size); cp77_collision.py `--slabs` builds `zz_<map>_colslabs.pk3` (each package stored, and
  `maps/<map>.colslabs.json`: the placed tiles, their origins, brush counts, the sha256 and size of each BSP they expand
  to, the manifest's note). `colexpand.py hashes` fills those for a pack made before them, from a kit's chunk packs.
- **Expansion** (`tools/levelport/colexpand.py all <slab pack> <out> --q3map2 <xenjk9> [--drop-package TRASH]`): each
  chunk's records rebuilt, cooked (colcook), checked against the hash its pack records, written as XBSZ (C's form;
  `--plain`: deflated), raw-copied into the chunk packs as the driver does, the manifest and cfg last; starts gated on
  memory (2.5 GB + 2.5 GB a million brushes). With `--drop-package`, once every chunk has verified, the slab pack moves
  to TRASH/<stamp>-<name>/ with a manifest (kept if any fails; only the owner empties the trash). Needs Python with
  numpy, q3map2 xenjk9 and GameData.

| A, tag 11 | |
| --- | --- |
| The slab pack | **1,802 MB** (1.68 GiB) for 195 chunks |
| Its build (the driver, 3 workers, the hub beside it) | 5,454 s |
| Expansion, normal priority, 5 chunk workers x 4 | **2,367 s**; -6_0 alone 209 s at a peak of 8.9 GB; system commit at most 53.3 of 63.6 GB, at least 7.8 GB free |
| Identity | **195 of 195** chunk BSPs byte-identical to cp_watson_collision-11cook's, the manifest and cfg too (twice, deflated) |
| Expansion to C's form (XBSZ), with the drop | 2,539 s; 195 of 195 verified against the pack's hashes, and decoding to 11cook's BSPs (checked apart); the slab pack moved to `.codex-trash/`; packs 5.69 GB; at least 11.9 GB free |

The first full expansion held each slab as Python objects: -6_0 peaked at 12.7 GB and free memory fell to 0.1 GB. The
slabs now go to the workers as array slices and come back as arrays.

### The driver: four workers and a reused hub (2026-11-08)

The owner's "Yes, both": a fourth chunk worker, kept only if it gains, and the hub compiled once and reused on an exact
key of its inputs.

- **Workers**: `--workers` defaults to 4. S1's start gate is unchanged: each start waits until its own peak plus 2 GB
  fits beside the running compiles' outstanding growth.
- **Hub cache**: the hub's key is a sha256 over the hub glb, the driver's own scripts (`tools/levelport/*.py`), every
  file of the hub's q3map2 folder, its arguments (the map, the hub's gltf2map arguments, `--exact-planes`, the grid
  limit) and the GameData listing. A compiled hub is kept under `.codex-research/levelport/cache/hub/<key>/` (the pack
  and `hub_compile.json` with its sha256) and copied in when the key matches exactly; `--no-hub-cache` compiles anyway.
  Editing any of those scripts, this one included, changes the key, so the next run compiles the hub again.

| Tag 11, normal priority, XBSZ default, the hub compiled beside the chunks | 3 workers (cp_watson_collision-11z) | 4 workers (cp_watson_collision-11w4) |
| --- | --- | --- |
| Wall time | 5,825 s | **4,850 s** (-17%) |
| Compile worker-minutes | 268 | 293 (each compile slower beside a fourth) |
| The hub's compile | 47 min | 43.5 min |
| Least available memory (sampled every 2 s) | 11.17 GB | 9.26 GB |
| Peak system commit (of 63.6 GB) | 43.38 GB | 48.76 GB |
| Members | 195 of 195 decoding to 11cook's BSPs | **195 of 195** decoding to 11cook's BSPs; the manifest and cfg the same |

| Hub reuse | |
| --- | --- |
| Fresh hubs (11z, 11w4) | the hub pack's 10 members identical (the build is deterministic) |
| A driver run that found the key (`--tiles=-9_4`, cp_watson_collision-11hubr) | "reused ... its inputs' key matches exactly"; 9 s in all; its hub pack's 10 members identical to both fresh builds'; its one chunk decoding to 11cook's |
