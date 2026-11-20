# q3map2 patches for level-port collision maps

Seven patches to netradiant-custom's q3map2 (upstream https://github.com/Garux/netradiant-custom, tested on be3b369).
The first three make `-bsp` on level-port collision maps about linear in the brush count (`cullsides_sweep`,
`plane_hash`) and let it read and write a district-sized map (`large_maps`); the other four add options for chunked
and cooked collision. Measured on Kabuki map-22 (240,883 brushes, 4 threads): shipped q3map2 1,250 s, patched 22.7 s;
482 k brushes 40.9 s. Summary in [docs/levelport-watson.md](../../../docs/levelport-watson.md) section 7.

| Patch | File | What |
|---|---|---|
| `cullsides_sweep.patch` | `tools/quake3/q3map2/surface.cpp` | `CullSides` tested every pair of world brushes (about 2.9e10 pairs on map-22, 4.07 M overlapping). A sweep along x visits only pairs whose bounds overlap, in the original order: output identical. |
| `plane_hash.patch` | `tools/quake3/q3map2/map.cpp` | `FindFloatPlane` hashed planes by `(int)abs(dist) & 8191` only. The patch keys on the distance bin and the normal, unbounded, and searches every bin a plane within epsilon could fall in. The .map load drops from 141 s to 12 s; 6 of 2,850,604 brush sides pick a different, already existing plane within q3map2's epsilon (a tie-break). |
| `large_maps.patch` | `tools/quake3/common/vfs.cpp`, `tools/quake3/q3map2/writebsp.cpp` | For district-sized maps ([docs/levelport-collision.md](../../../docs/levelport-collision.md)). The `.map` is measured with `_fseeki64` / `_ftelli64` and read in 1 GB pieces (Windows `fseek` and `ftell` take a 32-bit `long`: on a `.map` past 2 GB, the district's is about 5.4 GB, the seek fails, the length reads 0, and q3map2 parsed an empty map and crashed in LoadMapFile), and the BSP keeps only the planes a brush side or node uses (q3map2 stores each plane with its flipped twin; collision maps use about half: Kabuki 1.25 M of 2.30 M), so the district's BSP is about 1.6 GB, not 2.05 GB at the format's 2 GB limit. Kabuki map-23's .map: every lump but the planes, sides and nodes identical, and those the same planes by value. |
| `world_coords.patch` | `tools/quake3/common/maxworld.h` | q3map2's world bound +-65,536 -> +-262,144 (a city's coordinates), for the coordinate test only: compiled 232,000 u out, q3map2's float windings give a quarter of Kabuki's brushes a different bevel set ([docs/levelport-collision.md](../../../docs/levelport-collision.md) section 8). Build `netradiant-custom-20260114-xenjk4`; compile chunks around their own origin instead. |
| `exact_planes.patch` | `tools/quake3/q3map2/map.cpp`, `brush.cpp`, `bsp.cpp`, `writebsp.cpp`, `q3map2.h` | For chunked collision ([docs/levelport-collision.md](../../../docs/levelport-collision.md) section 14), applies after `plane_hash.patch`. Two options, off by default. `-exactplanes`: `FindFloatPlane` reuses a plane only when it is exactly the snapped plane (not the first within the epsilons), a new plane's -0 is stored as +0, a face's distance is the stored (float) normal's through its points' centre in double, box bevels sit at the bounds of the double windings, and edge bevels come from the brush's own double windings in a whole-unit anchored frame, so a brush's planes depend on the brush alone. `-pointgrid N` rounds every brush plane point to 1/N as it is read, so a whole-unit move of the map text is exact and plane normals are the same in any frame. Kabuki split in two: each half compiled alone gives the whole compile's 240,035 brushes bit for bit (before: 110,420 differed). `-xplanes` also writes each brush's sides as double-precision planes to `<map>.xplanes` (the exact reference of the own-frame gate, [docs/levelport-collision.md](../../../docs/levelport-collision.md) section 15). |
| `brush_cook.patch` | `tools/quake3/q3map2/bsp.cpp` | For collision cooker A ([docs/levelport-collision.md](../../../docs/levelport-collision.md) section 37), applies after `exact_planes.patch`. `-brushcook`: the world's brushes only, as the map load made them (planes, windings, bevels, bad brushes dropped), and one empty leaf: no tree, portals, flood, draw surfaces or vis. The brushes, sides, planes and shaders are `-bsp`'s; colmerge.py's merge of `-brushcook` tiles gives a chunk BSP identical to the `-bsp` one but for the command line in its entities. The oracle for `brush_bin.patch`. |
| `brush_bin.patch` | `tools/quake3/q3map2/bsp.cpp`, `map.cpp`, `main.cpp`, new `brushbin.inc` | Collision cooker A (section 37), after `brush_cook.patch`. `-brushbin <in> <out>` (`-` for stdin and stdout; the log then goes to stderr from the first line): the world's brushes as binary records, each side's three points as doubles and its shader, through the steps `ParseRawBrush`, `ParseBrush` and `FinishBrush` take for a world brush (the point grid, `MapPlaneFromPoints`, duplicate and mirrored planes, contents, `CreateBrushWindings`, `AddBrushBevels`) and `EmitBrushes`' order and `EmitShader`; per tile of records its shaders, planes, brushes and sides come back as records, each tile with an empty plane table as its own run would have. No .map, no BSP. `tools/levelport/colcook.py` is the other end. |

Build (msys2 mingw64, from a netradiant-custom checkout at be3b369):

    git apply -p1 plane_hash.patch cullsides_sweep.patch large_maps.patch
    make -j8 MAKEFILE_CONF=msys2-Makefile.conf BUILD=release OS=Win32 ASSIMP_INTERNAL=yes DEPENDENCIES_CHECK=off \
      DOWNLOAD_GAMEPACKS=no INSTALL_DLLS=no INSTALL_DATA=no install/q3map2.exe

(pass the libpng and libjpeg include and library paths to make; msys make drops TMP/TEMP, so pass them to the
compilers through `env`.) Copy `q3map2.exe` with its mingw runtime DLLs into one folder. Apply the further patches
the same way, in the table's order, for the builds below.

The builds the tools use (names as in the level-port records):

| Build | Patches | Used by |
|---|---|---|
| `xenjk3` | the first three | `gltf2map.py` (`$Q3MAP2`) |
| `xenjk7` | the three and `exact_planes` | `gltf2map.py --exact-planes`, `cp77_collision.py --text` chunks |
| `xenjk8` | `xenjk7` and `world_coords` | `cp77_collision.py --q3map2` (the hub, past +-65,536 u) |
| `xenjk9` | `xenjk7` and `brush_cook`, `brush_bin` | `gltf2map.py --cook`, `cp77_collision.py --q3map2-chunks` (`$Q3MAP2_CHUNKS`); on .map input it runs `xenjk7`'s code (the two patches only add options) |

Earlier states of `exact_planes`, kept for builds made with them: `xenjk5` (no `-xplanes`, face distances through
one point, box bevels from float bounds; `cp_kabuki_col-31`) and `xenjk6` (`xenjk5` plus `-xplanes`).

Nothing is looked for beside the repository: `gltf2map.py` runs `$Q3MAP2`, and for `--exact-planes` and `--cook`
`$Q3MAP2` when set, else `$Q3MAP2_CHUNKS` (`--cook` checks the build has `-brushbin`). `cp77_collision.py` takes
`--q3map2` and `--q3map2-chunks`. The game definitions are built in.
