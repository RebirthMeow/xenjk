# Level port, CP2077 front end (review 2026-11-19, master 832fa0e05)

Scope: the second pass over `tools/levelport/`, the files the first pass (`levelport.md`, RL-1..RL-30) read in part
or only swept. Read in full, every code line: `cp2gltf.py` (5,656; the first pass's lines 36-362, 2262-2277 and
5345-5366 were read again in context), `cp77mat.py` (1,393), `cp77decals.py` (1,113), `cp77fx.py` (800),
`cp77_tiles.py` (583), `cp77_foliage.py` (121), `cp77_ads.py` (255), `cp77_rotate.py` (125), `cp77_meshlod.py` (97),
`levelcheck.py` (1,728), `wk_fetch.py` (317): 12,188 lines. Nothing was run or built; three facts were checked on disk
(`.w2mesh` exports' file names, the cells files' form, `UNITS_PER_METRE`).

Files with no correctness finding: `cp77_rotate.py`, `cp77_meshlod.py` (small items only, RL-124).

**Binary layouts checked against `codemp/rd-vulkan/vk_scene.cpp`, all matching:** `.xscn` header (88 B, 10 counts
and offsets; `Scene_ReadHeader` :204) and its ten records (28, 4, 32, 16, 16, 240, 64, 64, 64, 16 B; `xsRecordSize`
:190, the structs :222-237) against `xscn_blob` / `scene_light_section` / `scene_box_section` /
`scene_shape_section`, including the instance's `nearDist` at the old flags word and `hide` last; the tiled
manifest's `lib` / `mat` / `tile` lines (:8830-8862); `.xfog` (16 B + 64 B records, kind at float 15); `.xdet` v1
(80 B) and v2 (table in `pad`, 288 B records, shares at +160 and +224); `.xwat` v1-3 (104 B records, the 32 B ocean
record, the mask block); `.xrot` (36 B); `.xscr` (68 B); `.xlut` (ident, version, size, input; rgba16f); `.xfx`
(48 B header, 80 / 512 / 16 / 64 B records, every emitter word offset the loader reads: 13, 14-17, 49, 50-66, 75,
104-112, 124, 127); `.xdcl` header (32 B) and record sizes (64, 112, 80); the DDS DX10 header of `dds_bytes`. The
axis maps (`CP_TO_GLTF`, `G2C`, `G = to_g M from_g`, `bend`'s frames, `box_map`, `light_box`, the scene's
`upm * (m - origin)`) are consistent with each other. No layout or axis finding.

## Findings

### RL-100 P1 The collision compare judges nothing above 16 m, while collision is built to 400 m
- Where: `tools/levelport/levelcheck.py:770` (`ZMAX = 600.0`, "cp2gltf --col-height 16 m"), `:1165-1166` (visible
  surfaces above it dropped), `:1183` (collision tops above it dropped); `compare-tiles` has no `--zmax`
  (`:1680-1689`) and its child command passes none (`:1102-1103`); callers `cp77_tiles.py:505-507`,
  `cp77_area.sh:61`; the builds use `--col-height 400` since 2026-11-01 (`cp77_tiles.py:39-40`).
- Scenario: any district check: a highway deck, a roof or an upper street at 20 m with no collision under it is
  never counted "missing", so `collide` and `drop` (which read the compare's samples) report no fall there. The
  case that removed the height cap (a fall through a highway deck) is the case the check cannot see.
- Fix: the default from the build (`--col-height x 37.5`, or no limit); `--zmax` on `compare-tiles`, forwarded.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): ZMAX is 400 m (15,000 u); compare-tiles takes --zmax and forwards it; cp77_area.sh passes --zmax 600, since its build keeps cp2gltf's default 16 m

### RL-101 P2 `--from-tag` links the earlier tag's tile records, and a later tiles run rewrites them in place
- Where: `tools/levelport/cp77_tiles.py:63-66` (`os.link` of `tiles.json` and every `<i>_<j>.json`);
  `tools/levelport/cp2gltf.py:4810` (`open(out / '%d_%d.json', 'w')`), `:4828-4829` (`open(out / 'tiles.json', 'w')`).
- Scenario: `cp77_tiles.py night_city_b t12 --from-tag t11` links the tiling; after the next scan or fetch (or
  with `--rebuild`) `t12`'s tiles stage runs and truncates each record through its link: `t11`'s records and
  `tiles.json` now hold `t12`'s placements, while `t11`'s parts and keys describe the old ones. The option's help
  says "nothing in it is written".
- Fix: `stage_tiles` writes each record and `tiles.json` to a temporary name and renames (as it does for
  `ad_pool.json`), or `link_tiling` copies.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): stage_tiles writes each record and tiles.json to a temporary name and renames, which breaks the link

### RL-102 P2 A scene part's key does not hold the signal table it bakes slots from
- Where: `tools/levelport/cp2gltf.py:4295` (`slot = sig.slot(p.get('ent'))` into `inst_slot`), `:3691-3697`
  (a slot is the signal's rank in `signal_groups.json`), `:4713`; the part key `cp77_tiles.py:288-301`;
  `signal_groups.json` is only a merge input (`cp77_tiles.py:190`).
- Scenario: `signal_groups.json` is rebuilt with one signal more or fewer: the merge reruns and writes the new
  `.signals` table and the lights' slots, but every kept tile part still carries the old ranks, so signal heads
  follow another intersection's cycle and disagree with their own lights.
- Fix: the file's size and time (or the ranks of the tile's own signals) in the scene part's key.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): signal_groups.json's size and time are in the scene part key (the first run after this rebuilds the scene parts once)

### RL-103 P2 A tiled scene of an area with a scalar `half` dies writing the ocean record
- Where: `tools/levelport/cp2gltf.py:4665` (`hx, hy = AREAS.get(a.area, {}).get('half', (radius, radius))`),
  reached from `:5419-5420`; `AREAS` `kabuki` and `watson_slice` (`:55`, `:58`: `'half': 192.0`, `384.0`).
- Scenario: `scene --merge --tiled --area kabuki` with any default water patch (the canals): after every shard is
  written, `TypeError: cannot unpack non-iterable float`; the build stays as `.partial`.
- Fix: normalise as `area_box` does (`:165`).
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0)

### RL-104 P2 `wk_fetch.fetch` records a crashed WolvenKit batch's paths as absent from the archives
- Where: `tools/levelport/wk_fetch.py:110-122` (`run` returns the exit code), `:190-196` (ignored), `:201-206`
  (everything not there afterwards goes to `fetch_absent.txt`); `cp77decals.py:706,728` and `cp77fx.py:714,733`
  (absent paths leave `need`), `cp77decals.py:744`, `cp77fx.py:744` (the loop ends when nothing is new).
- Scenario: one `uncook` batch of 120 textures crashes (`cp2gltf.py:1458` records such crashes): all 120 are
  written as absent, the fetch ends "0 needed" with exit 0, and the decals or effects are built without them
  until someone passes `--retry`.
- Fix: a batch that exits nonzero is retried in small batches (as `stage_fetch` does, `cp2gltf.py:1476-1479`),
  and its paths are not written to `fetch_absent.txt`.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): a call that exits nonzero is retried eight paths a call; what still fails is not written to fetch_absent.txt

### RL-105 P2 The multilayer bake cache is written in place and read while being written
- Where: `tools/levelport/cp77mat.py:520-535` (`disk.exists()` then `np.load`; `np.savez(disk, ...)`).
- Scenario: two tile parts (`cp77_tiles --workers`, each with its own prebake pool) need the same setup: one
  finds the other's half-written `.npz` and stops on `BadZipFile`. A part killed mid-write leaves a short file
  that every later run loads and fails on until it is moved away by hand.
- Fix: write `<name>.tmp<pid>.npz`, `os.replace` (as `ResultCache.put` does, `:1387-1392`); on a load error
  bake again.
- Confidence: confirmed (traced); the race itself needs a run
- Status: fixed (c4aafcfd0): written to <name>.tmp<pid>.npz and renamed; a bake that does not read is baked again

### RL-106 P2 `fetch_serialised` can file another template's JSON under a missing template's name
- Where: `tools/levelport/cp2gltf.py:1526-1538` (`raw/_round<k>` and `_round<k>_json` are never emptied).
- Scenario: run 1 fetches `a\hangout_1.ent` (round 0). Run 2 asks for `b\hangout_1.ent`, which the archives do
  not give: nothing is copied into `_round0`, the old `hangout_1.ent` is serialised again, and its JSON is copied
  to `ents/<cache_name(b\hangout_1.ent)>.json`. The entity gets the other template's meshes and lights, and
  `stage_fetch_ents` no longer counts it missing. Every run also serialises every earlier run's files again.
- Fix: a fresh round folder per call (`staging()`), removed afterwards; or call `wk_fetch.serialise`.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): a fresh staging folder a round, removed after it

### RL-107 P2 `scene --relight / --regrade / --repack` destroy the source pack when the output is the same file
- Where: `tools/levelport/cp2gltf.py:5619-5633` (`out / Path(src).name`), `:3871-3889`, `:3473`, `:3263-3279`.
- Scenario: `scene --relight build/cp_x_scene-5/zz_cp_x_scene.pk3 --tag 5` (or `--out` that folder):
  `ZipFile(dst, 'w')` truncates the pack the open reader is still copying from; the run fails and the pack is gone.
- Fix: exit when `dst` is `src`; write to a temporary name and rename.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): exits when the output is the source; the three write to a temporary name and rename

### RL-108 P2 A decal pack that fails its own validation is published and the run exits 0
- Where: `tools/levelport/cp77decals.py:1000` (`validate`), `:1034` (`stage.rename(final)`), `:1050` (a printed
  line only); `--check` `:1104-1107` returns nothing either.
- Scenario: an out-of-range image index or a singular box map: "validation: ..." is one line of the log, the
  build takes its final numbered name, and a driver that reads the exit code installs it.
- Fix: leave the `.partial` folder and exit nonzero when `bad`; `--check` likewise.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): a failed validation exits and leaves the .partial folder; --check exits 1

### RL-109 P2 `cp77_ads.template_ad` picks a template by file name, not by depot path
- Where: `tools/levelport/cp77_ads.py:67` (`ents.glob('*_' + stem + '.json')`, the first match), `:72`
  (`rc.get('components')` only); against `cp2gltf.cache_name` (`cp2gltf.py:1494-1497`, whose docstring names the
  case: "hangout_1.ent under street signs and LCD screens") and `template_chunks` (`:754-757`, the compiled data
  first).
- Scenario: two ad device templates of one file name in different folders: both screens get the group and pinned
  ad of whichever cache file sorts first. A template whose widget component is only in its compiled data gets none.
- Fix: `work / 'ents' / (cp2gltf.cache_name(tpl) + '.json')` and `cp2gltf.template_chunks(rc)`.
- Confidence: confirmed (traced); how many Watson templates collide was not counted
- Status: fixed (c4aafcfd0): the template's own cache file (cache_name of its depot path), its compiled data then its component list

### RL-110 P2 `holes` skips a shot of another size and then reports no holes
- Where: `tools/levelport/levelcheck.py:696-697` (`if im.shape[:2] != (h, w): continue`), `:721-722`.
- Scenario: the client's window does not come up at `--size` (3840x2160 on a smaller desktop): every shot is
  skipped, `seen` is 0 everywhere, the result is "0 clusters over 0 m2". The file's own rule (`:594-598`) is that
  unseen ground is not ground without holes.
- Fix: exit on a shot of another size; exit when a cell inside `radius - edge` was never seen.
- Confidence: likely (whether the window is clamped needs a run)
- Status: fixed (c4aafcfd0): a shot of another size exits; cells inside radius - edge never seen are reported (not an exit: whether an aerial run leaves such gaps by design needs a run)

### RL-111 P2 A decal's explicit `DiffuseAlpha` 0 is drawn at 1
- Where: `tools/levelport/cp77mat.py:1144` (`float(d.get('DiffuseAlpha') or 1.0)`), used at `:1153-1160`.
- Scenario: a `mesh_decal` that writes only its normal (DiffuseAlpha 0, NormalAlpha 1): its albedo is written at
  full strength. `cp77decals.py:441` keeps the 0 for the projected decals; `first_ev` (`:56-59`) was added for the
  same `a or b` mistake.
- Fix: `1.0 if d.get('DiffuseAlpha') is None else float(...)`.
- Confidence: likely (no material with an explicit 0 was looked up)
- Status: fixed (c4aafcfd0)

### RL-112 P2 `lods_unfiltered` leaves WolvenKit's `appsettings.json` patched when the run is killed
- Where: `tools/levelport/cp2gltf.py:1219-1227`, used at `:1315` (with `MaterialRepo` = a staging folder that is
  then removed) and `:1469`.
- Scenario: `fetch --embedded` or `fetch --lods` is stopped (the driver's job object kills it; `__exit__` does
  not run): the tool's settings keep `LodFilter false` and a `MaterialRepo` that no longer exists for every later
  WolvenKit call from any tool. Two fetches at once restore each other's patched copy.
- Fix: keep the original beside it (`appsettings.json.orig`) and restore it at the next start; or a private copy
  of the settings per call if the CLI takes one.
- Confidence: likely (what a later `uncook` does with the stale `MaterialRepo` needs a run)
- Status: fixed (c4aafcfd0): the original is kept as appsettings.json.orig while patched and taken from there by the next run; two fetches at once still share the one settings file (no per-call settings known for the CLI)

### RL-113 P2 `Materials.ml_cache` keeps every bake of a process in memory
- Where: `tools/levelport/cp77mat.py:376`, `:514-527` (three 512 x 512 arrays and a 256 x 256 x 8 share map per
  bake, about 2.9 MB, never dropped; `tex_cache` beside it is capped at 768 MB, `:416-446`).
- Scenario: a scene part or an untiled `scene` with some thousands of multilayered materials: several GB held
  for the life of the process, although `scene_material` keeps only image names once the DDS is cached.
- Fix: an LRU by bytes as `tex_cache`; the bake is on disk.
- Confidence: likely (not measured)
- Status: fixed (c4aafcfd0): an LRU of 768 MB (ML_CACHE_BYTES); a dropped bake comes back from the bake cache

### RL-114 P2 `cp77_foliage.py` holds every instance of the area in memory before writing
- Where: `tools/levelport/cp77_foliage.py:51`, `:98-107`.
- Scenario: `night_city_b`: 1.54 M record dicts (a 12-float list each) are kept until the last sector is read,
  some GB; `stage_tiles` streams the same kind of record to per-tile files for this reason (`cp2gltf.py:4764`).
- Fix: append to `<tile>.jsonl.tmp` handles as records come, rename at the end.
- Confidence: likely (not measured)
- Status: fixed (c4aafcfd0): each tile's records are streamed to <tile>.jsonl.tmp and renamed at the end

### RL-115 P3 A signal's slot has 13 bits in a light's type word and nothing checks it
- Where: `tools/levelport/cp2gltf.py:3764-3767` (`slot << 6`, then bits 20 and 21); `vk_scene.cpp:9474` reads
  bit 19; the word is packed as f32 (`:3810`), exact below 2^24.
- Scenario: more than 8,191 signals in `signal_groups.json`: slot bits read as the renderer's bit 19 and the
  day/night bits.
- Fix: exit when `slot >= 1 << 13`.
- Confidence: confirmed (read); no area has that many today (not counted)
- Status: fixed (c4aafcfd0)

### RL-116 P3 A signal head's dark-face geometry is keyed without the set of lit zones
- Where: `tools/levelport/cp2gltf.py:4243-4251` (`gk = (depot, ci, j, uid, z)`; for `z = -1` the triangles are
  `~np.isin(tz, lit)`, which depends on the placement's `zones`).
- Scenario: one head mesh placed by two entities whose lamp sets differ: the second mesh key reuses the first's
  dark-face geometry, so a lit zone is drawn twice or a dark zone not at all.
- Fix: `tuple(lit)` in the key for the `-1` group.
- Confidence: needs a run (whether two such entities share a head mesh)
- Status: fixed (c4aafcfd0): the dark face's key holds the lit zones

### RL-117 P3 `cp77fx.py`: small items
- `:783-796`: no `--area`; `build`'s `centre`, `radius` (`:488`) cannot be set, so every sector of the work
  folder places effects, where the decals and haze volumes are cut to the area.
- `:230-232`: a `...MultiCurve` evaluator anywhere but the noise amplitude (`:383`) has no `Elements` and
  silently becomes the default curve (1, or 0 for birth and drag).
- `:452`: an effect track's alpha, size, emission and velocity are taken at the curve's first sample: a track
  that fades in from 0 is placed with 0 for good.
- `:498-500`, `:531`: a fetched system whose emitters are all off counts as `system_not_fetched`.
- `:705`: `png_of` walks the whole png tree once per texture (`pack` builds the name index once, `:621-623`).
- Confidence: confirmed (read); the second and third need a look at the fetched files
- Status: deferred: an --area cut changes which effects a build places (a design choice); the MultiCurve, first-sample and system_not_fetched items need a look at the fetched files; png_of is speed only

### RL-118 P3 A zero scale in a fog or light channel volume stops the scene
- Where: `tools/levelport/cp2gltf.py:3499` (`np.linalg.inv(M)` per channel volume), `:3635` (per fog volume).
- Scenario: a volume node with a scale of 0 on an axis (mesh placements have them: `:652-655`; the decals drop
  them, `cp77decals.py:939`): `LinAlgError` in the merge, after the parts.
- Fix: skip and count a volume whose determinant is under 1e-9.
- Confidence: likely (no such volume in Watson's builds so far)
- Status: fixed (c4aafcfd0): channel and fog volumes of a near-zero determinant are skipped (counted)

### RL-119 P3 Fetch and mesh small items
- `tools/levelport/cp2gltf.py:1481-1488`: `fetch`, `fetch --lods` and `fetch --proxies` share one
  `fetch_missing.txt`; a run compares its missing set with the other mode's, so with different sets neither is
  recorded as done and both run whole every time.
- `:1322-1323`: `base[:-5]`, `name[:-5]` assume `.mesh`; an embedded `.w2mesh` would be published as
  `<name>.w.glb` and never found (`mesh_stem` exists). The same slice names glTF meshes at `:2423`, `:2597`,
  `:4903`, `:5078` (cosmetic).
- `:1772-1774` against `:1782`: `bend` with one frame does not subtract `y0` as the many-frame path does.
- `:1100`, `:4810`: files of cells or tiles an earlier run had and this one has not stay in the folder (the
  index names the live ones; a reader that globs would take them).
- `tools/levelport/cp77_meshlod.py:78`: `rglob('*.mesh')` leaves out `.w2mesh`.
- Confidence: confirmed (read); `bend` needs a one-frame node to compare
- Status: fixed (c4aafcfd0): each fetch mode keeps its own last missing set (<mode>.missing.txt beside fetch_missing.txt, which the keys still stat); fetch --embedded names .w2mesh exports by mesh_stem; cp77_meshlod reads .w2mesh too. deferred: bend's one frame (needs a one-frame node to compare); stale cell and tile files (removing outputs is the owner's; the indexes name the live ones); the cosmetic glTF names left

### RL-120 P3 The bake cache's key is bumped by hand
- Where: `tools/levelport/cp77mat.py:501-508` (setup, mask, `lv2`, slots, size).
- Scenario: an edit to `_multilayer`, `levels`, `SHARE_SIZE`, `layer_size` or `detail_size`'s value: the
  result cache misses (its key holds the code), converts again, loads the old bake from disk and keeps it as the
  new result. The 2026-11-06 levels fix needed the `lv2` tag for this.
- Fix: `stagekey.code_key(..., ['Materials._multilayer'])` and the two sizes in the tag.
- Confidence: confirmed (read)
- Status: done 2026-11-20, before tag 12: stagekey keys a class's methods and attributes (`Materials._multilayer` as a root follows what it reads on self); a bake file holds its key (`Materials.bake_code`: `_multilayer`'s code key with layer_size, detail_size and detail_slots) and one made by other code or sizes is baked again in its place, so the 39 GB of bakes are neither renamed nor doubled. A bake without a key is taken while the code key equals `LEGACY_BAKE_CODE` fc93196e5ec2b415, the same at all eight commits of cp77mat.py from the lv2 bakes (a6d789618) to master. Tests: tools/test_levelport_bake_key.py

### RL-121 P3 `levelcheck.py`: small items
- `:804-805`: `Path(pk3).with_name('materials.json')` on a comma-separated pack list names no folder: no
  record is read, so water surfaces and tree canopies are judged as floors, silently; the same when the file is
  absent.
- `:545`: `aerial` opens `a.map` as one zip; `spawns`, `compare` and `drop` take a hub and its chunk packs.
- `:484-485`: several packs and no spawn: `sb` is unbound at the last print.
- `:1592`, `:1694`: `drop --bits` is parsed and never used (the engine check is always the 64-bit client);
  `--scene` is documented as unused (`:1700`, `:1718`).
- `:292`: every pack's `ZipFile` in `BspLumps` stays open.
- `:1083-1100`, `:1499-1517`: without `psutil` the tile workers start with no memory test, with no message.
- `tools/levelport/cp2gltf.py:4651`: `stats['screen_materials']` is set inside the `.xrot` block.
- Confidence: confirmed (read)
- Status: fixed (c4aafcfd0): materials.json beside the first pack of a list (a message when there is none), the spawn summary's brush count with no spawn, drop --bits' help says unused, a message without psutil, screen_materials counted with the screens. deferred: aerial's hub-and-chunks input (a feature) and BspLumps' open handles

### RL-122 P3 `cp77_foliage.py` has its own area test
- Where: `tools/levelport/cp77_foliage.py:32-38`, `:94` (cells of a fixed 128 m, no `z_min`, no radius; with an
  area that has no cells file, no test at all), against `cp2gltf.use_area` / `in_area` (`cp2gltf.py:111-134`).
- Scenario: `kabuki` or `watson_slice`: every foliage instance of the work folder's sectors is written, those
  outside the area's circle too; `:43` takes any string as the area (`KeyError`).
- Fix: `cp2gltf.use_area(a.area)` and `in_area(x, y, centre, radius, z)`.
- Confidence: confirmed (read)
- Status: fixed (c4aafcfd0): cp2gltf.use_area and in_area (circle, cells, floor); the area is a choice of AREAS

### RL-123 P3 Files written in place that RL-7 does not list; packs that may become zip64
- In place: `cp2gltf.py:1046-1050` (`placements.json`), `:1107` (the store's `index.json`), `:1695`
  (`entity_meshes.json`), `:4697-4698`, `:5437-5438` (`materials.json`, `ad_lights.json`); `cp77_ads.py:244`,
  `cp77_rotate.py:115` (the indexes the part keys stat); `cp77fx.py:547` (`.xfx`). Several pass an unclosed
  `open()` to `json.dump` (`:1050`, `:1107`, `:1308`, `:1377`, `:1633`, `:1695`, `:4829`; `cp77_tiles.py:571`).
- zip64: `cp2gltf.py:4693` (the untiled scene, images and all in one pack), `:3279`, `:3428`, `:3473`, `:3884`,
  `cp77decals.py:926`, `cp77fx.py:625` open with the default `allowZip64`; only the tiled writer refuses
  (`:5359-5362`, `:5435`). Past 4 GB the pack is written without a word and the engine cannot open it (RL-9).
- Confidence: confirmed (read)
- Status: fixed (c4aafcfd0): placements.json, the store index, entity_meshes.json, materials.json, ad_lights.json, the fetch JSON, the tile records and the ad and rotate indexes through a temporary name (cp2gltf.write_json); no zip64 on the untiled scene, repack, relight, regrade, vcache, fx and decal packs. The .xfx itself is still written in place (packed right after)

### RL-124 P3 Dead code and stale comments
- `tools/levelport/levelcheck.py:770`: the `ZMAX` comment (RL-100); `:765-767`: an empty "collide" banner over
  the "compare" one.
- `tools/levelport/cp77mat.py:41-44`: the comment for `FX_ONLY` sits above `_num`, 75 lines from it; `:858`:
  `AD_NOT`'s entries after the first are unused since `AD_NOT_RE`.
- `tools/levelport/cp77decals.py:329`, `:334`, `:346-350`, `:609-612`: the side-image path ("none since v2"):
  no caller passes `side=True`.
- `tools/levelport/cp2gltf.py:3257-3282` (`scene_repack`: the pre-DDS pack format), `:5247-5298`
  (`stage_scene_hide`: parts from before 2026-11-01): one-time migrations kept as stages. `:1247`, `:1269`,
  `:1288`, `:1339-1340`, `:3211`: `hashlib` / `shutil` imported again inside functions. `:107`: the `AREA_CELLS`
  comment is glued under `work_dir`.
- `tools/levelport/cp77fx.py:488`: `centre`, `radius` (RL-117).
- Confidence: confirmed (each read at its place)
- Status: fixed (c4aafcfd0): the empty banner, ZMAX's comment, FX_ONLY's comment moved to it, AD_NOT's unused entries, the decals' side-image path (the summary keeps its fields), the inner imports, AREA_CELLS' spacing. deferred: scene_repack and stage_scene_hide (one-time migrations still reachable from the CLI: whether old builds need them is the owner's)

## Publish cleanliness

Beyond the first pass's list (which already has these files' machine paths, the reviewer names and the "owner
<date>" wording):

- Inputs the tools need whose generators are not in the repository: `signal_groups.json` (`cp2gltf.py:3674-3675`:
  `.codex-research/levelport/look/signals-agent-20261029/build_groups.py`); `ads.json` and `rendered/` under
  `look/ads-20261105` with `ads_dump.ps1` (`cp77_ads.py:15-17`, `:33-34`); the sea mask `.npz` (`cp2gltf.py:5583`).
  Commit the generators or say in the README that those features are off without them.
- Contracts cited by path into research folders: `cp2gltf.py:2122-2123`, `:5555-5556` (STEP0-FORMATS.md),
  `:1916-1922` (`look/water-20261103/xwat.py`), `cp77decals.py:3` (CONTRACT.md), `cp77mat.py:13`
  (`rend2_comparison/tools/cp_multilayer.py`), `cp77mat.py:71` (screen_params_ask.md): move the text into `docs/`
  or drop the path.
- Watson baked in as defaults: `cp77decals.py:85` (`MISTY`), `:1086-1090`, `cp77fx.py:790-791` (`--map cp_watson`,
  `--origin -1280 2240 12`), `cp77_ads.py:233`, `cp77_rotate.py:103` (`--area night_city_b`),
  `cp77_tiles.py:489-490` (`--sky cp_kabuki_haze` for every area): derive from `cp2gltf.AREAS`.
- `tools/levelport/levelcheck.py:21`: "look/kabuki_collide.py vectorised": a script that is not in the repository.
- No credentials or URLs in the eleven files. None carries a licence header (as the first pass notes).
- Status: generators done 2026-11-20 (before tag 12): `cp77_signals.py` (signal_groups.json; Kabuki's rebuilt equal but for its `sources` field), `cp77_ads_fetch.py` with `cp77_ads_dump.ps1` (ads.json and all 1,748 rendered ads byte-identical; WolvenKit 10.0.0-beta.1), `cp77_seamask.py` (the sea mask byte-identical from tiles/11; it reads a stitch of the Realistic Map 32K mod's tiles, a third-party input). Still open: the research-folder contract paths and levelcheck.py:21 are comments, left for the comment sweep; the Watson defaults (--map cp_watson, --origin, --area night_city_b, --sky cp_kabuki_haze) are tool behaviour, a design choice

## Liftability

- **Two fetchers.** `cp2gltf.fetch_serialised` (`:1500-1538`) and `wk_fetch.serialise` / `fetch`
  (`wk_fetch.py:125-208`) both unbundle and serialise in rounds of distinct file names; the older one has RL-106.
  Shape: `wk_fetch.py` owns WolvenKit (`WOLVENKIT`, `DOTNET`, `ARCHIVES`, `wolvenkit()`, `staging`, `publish`,
  `check_cache_identity`, `fetch_identity`, `lods_unfiltered`), `cp2gltf` imports it, not the reverse
  (`wk_fetch.py:32`).
- **The `.xscn` format has three readers in Python.** `cp2gltf.xscn_head_of` and the dtype in
  `scene_vcache_blob` (`:3380`), `levelcheck.XSCN_DT` / `xscn_head` / `SCENE_VERT` (`levelcheck.py:144-160`,
  `:773`), and `scene_relight`'s own header parse (`cp2gltf.py:3873-3875`). With the first pass's `xscn.py`: one
  module for the dtypes, header, sections and the tiled manifest, used by the writer and by `levelcheck`.
- **Chunk glb writer three times.** The cell nodes, box meshes and water volumes are written by `stage_build`
  (`:2591-2603`, `:2939-2981`), `stage_chunks` (`:4882-4925`) and `stage_inst_chunks` (`:5057-5100`) from copies
  of the same loops. Shape: `write_collision(w, T, boxes, water_quads, d, bands=None)`.
- **`levelcheck.py` reaches outside the lane**: `tools/renderer_bench.py` for the client (`:368`, `:414`, `:529`,
  `:1561`), another lane's game lock folder (`:391`), `cp2gltf.COL_NONE_NAMES` (`:807-812`), and `U = 37.5`
  (`:83`) beside `cp77decals.UPM`, `cp77fx.UPM` and `levelport.UNITS_PER_METRE`. Shape: the offline checks
  (`spawns`, `compare`, `stitch`, `collide`, `drop`) in one file with no client code; the client runs (`aerial`,
  `drop --engine`) in another; one unit constant.
- **Area rules.** `in_area` is applied by hand in `cp2gltf` (lights, entities, water, fogs, signals),
  `cp77decals`, `cp77fx` (not at all, RL-117) and `cp77_foliage` (its own, RL-122). Shape: an `Area` object from
  `AREAS` with `holds(x, y, z)`, `box`, `half_xy`, `origin(floor)`; RL-103 is the same missing normalisation.
- Status: done (1c1aac344): xscn.py holds the writer (records, sections, blob, images). not done: the readers' merge into xscn.py (levelcheck's and scene_relight's parses are code to unify, not move); wk_fetch owning WolvenKit and the decal/fx fetch scaffolding (reverses the cp2gltf/wk_fetch import and changes every fetch stage's key: worth doing after the merge, with a run); the chunk glb writer and the Area object are rewrites; the levelcheck offline/client split crosses into tools/renderer_bench.py and the game-lock folder

## Still open from earlier reviews

None from `docs/review/FINDINGS.md`, `PLAN.md` or the two 2026-11-09 reviews touches these files. From the first
pass of this review, still as written there: RL-1 (path defaults, `cp2gltf.py:44-50`), RL-7 (`cp77fx.py:625`),
RL-13 (`blender_decimate.py` and `wk_read_lenient.ps1` outside the stage keys), RL-18 (`stage_decimate`), RL-29
(`cp77_foliage.py:21`).
