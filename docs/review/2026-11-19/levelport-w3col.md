# Level port tools, second pass: Witcher 3, collision and patch tools (review 2026-11-19, master 832fa0e05)

Scope: the files of `tools/levelport/` the first pass (`levelport.md`, RL-1..RL-30) pattern-swept or read in part.
`tools/levelport/` and `codemp/rd-vulkan/vk_scene.cpp` are unchanged between a322d9dc4 (the first pass) and 832fa0e05.

- **Read in full here:** `w3_preview.py` (172), `w3autohide.py` (157), `w3decals.py` (693), `w3emitter.py` (576),
  `w3env.py` (171), `w3flags.py` (435), `w3foliage.py` (112), `w3fx.py` (331), `w3fx_dump.py` (134), `w3gltf.py`
  (365), `w3grade.py` (179), `w3terrain.py` (115), `w3trees.py` (1153, lines 84 on; its docstring 1-83 skimmed),
  `w3water.py` (248), `colclutter.py` (688), `colmerge.py` (354), `colexpand.py` (338), `nier2gltf.py` (742),
  `bsp2gltf.py` (351), `patch_chunks.py` (408), `patch_materials.py` (269), `patch_tiles.py` (128), `patch_water.py`
  (148), `tag_interiors.py` (182), `tag_probesky.py` (172), `tag_verdict.py` (481), `probe.py` (458),
  `scene_parity.py` (331).
- **The parts the first pass left:** `w3scene.py` 56-1063 (all of it now), `gltf2map.py` 62-818 and 1000-1100 (the
  `Compiler` class, `slab_planes`, `merge_coplanar`), `build_watson.py` 1-84, 470-784, 860-949, `cp77_collision.py`
  147-438, 556-1035, 1140-1295.
- **Readers checked against their writers** in `codemp/rd-vulkan/vk_scene.cpp`: `.xw3b` (header, 12 B stream, 1248 B
  records, the 416 B card records; `Scene_LoadW3Blend`, `Scene_MarkCardMeshes`, 5122-5411), `.xw3t`
  (`Scene_LoadW3Terrain`, 5019-5112), `.xlut` (`Scene_LoadGrade`), `.xenv` (`Scene_LoadEnv`: every cvar `w3env.py`
  writes is in its `look[]` list), `.xint` (32 B a group), `xscnInstance_t` (64 B: the four tools' `INST` dtype and
  `xscn_blob`'s `<12fIfIf`), `xscnMesh_t`, the 240 B material record, `.xdet` v2, `.xscr`. All of these agree; the
  findings below are elsewhere.
- **Not in this pass:** `cp2gltf.py`, `cp77mat.py`, `cp77decals.py`, `cp77fx.py` (only `xfx_blob`, 569-612),
  `cp77_ads.py`, `cp77_foliage.py`, `cp77_meshlod.py`, `cp77_rotate.py`, `cp77_tiles.py`, `slabwriter.py`,
  `levelcheck.py`, `wk_fetch.py`. They are still as the first pass left them.

One thing was run: `w3fx.emitter_record` handed to `cp77fx.xfx_blob` in memory (no file written), to confirm RL-200.
Nothing else was run or built. The Blender-side tools depend on the Witcher3-Blender-Tools add-on's objects, which
are not in the repository: what the add-on returns (property names, axis conventions of its particle and foliage
readers) is taken as the code assumes it.

Files with no findings: `bsp2gltf.py` (beyond RL-29's `Q3_TO_PARM`), `colmerge.py`, `tag_interiors.py`,
`w3_preview.py`, `w3autohide.py`, `w3flags.py`, `gltf2map.py`'s `Compiler` (plane and winding math, slabs, the
coplanar merge: nothing beyond RL-2, 6, 7, 8, 23).

## Findings

### RL-200 P1 `w3fx.py` cannot write an `.xfx` on master: its emitter records lack ten fields `xfx_blob` now requires
- Where: `tools/levelport/w3fx.py:156-166` (`emitter_record`), `:288` (`xfx_blob(...)`); `tools/levelport/cp77fx.py:66-69`
  (`EMITTER`: `turb_min`, `turb_max`, `turb_interval`, `turb_life`, `noise_amp`, `noise_freq`, `noise_rate`,
  `noise_amp_life`, `size_near`, `size_far`, added "2026-11-07") and `:587-592` (`v['turb_min']` and the rest read
  without a default).
- Scenario: any run of the guide's command (`docs/guide/beauclair.md:351`) with a dump that holds one emitter:
  `KeyError: 'turb_min'` at `cp77fx.py:587`. Confirmed by calling the two functions with one 'splash' record.
  `w3fx.py` was written against `cp77fx.py` before commit d71daaa87 added the fields; nothing ties the two.
- Fix: defaults in `xfx_blob` for the 2026-11-07 fields (`v.setdefault`: zeros, `size_near`/`size_far` `(0, 0, 1)`,
  as `cp77fx.py:399,404` set them), or the same in `w3fx.emitter_record`; a test that packs one W3 record.
- Confidence: confirmed (run in memory)
- Status: fixed (b9235ee05): w3fx.emitter_record fills the ten fields at their neutral values; tools/test_levelport_w3.py packs every W3 kind through xfx_blob

### RL-201 P2 The W3 work folder's `pre-<step>` baselines go stale: a re-run upstream is silently thrown away
- Where: `tools/levelport/w3trees.py:1025-1032` (default mode reads `placements.pre-trees.json`,
  `materials.pre-trees.json`, `meshes/index.pre-trees.json` whenever they exist; copied only when missing) and
  `:1145-1146` (the Blender prepare reads the same copy); `tools/levelport/w3foliage.py:90-93` (`--merge` starts from
  `placements.pre-foliage.json`); `tools/levelport/w3gltf.py:337-341` (rewrites the three work files and leaves every
  `*.pre-*.json` beside them). The same construction as RL-3 (`w3merge.py`).
- Scenario 1 (the guide's own order, `docs/guide/beauclair.md:273-275`): `w3foliage` again with another `--box` or
  `--skip`, `w3foliage --merge` (writes `placements.json` from `pre-foliage` plus the new foliage), `w3trees`:
  it loads `placements.pre-trees.json`, the copy made on the first run, which holds the *old* foliage, converts
  that and overwrites `placements.json`. The new foliage is gone; nothing is printed.
- Scenario 2: `w3gltf` again into the same work folder (a layer added to the list): `w3foliage --merge` and
  `w3trees` both start from the previous export's copies; the new export never reaches the scene. Also
  `w3foliage --merge` after `w3trees` or `w3merge` drops the trees' rewrite and every merged tag.
- Fix: record in each `pre-` copy the size and mtime (or a hash) of the file it was copied from's own inputs and
  refuse a stale one; simplest: `w3gltf` moves existing `*.pre-*.json` aside when it writes a new export, and each
  step refuses to start from a `pre-` copy older than a later step's output unless told `--replay`.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): w3work.py records when each step's copy was made and each step's last write; w3foliage --merge, w3trees and w3merge refuse a stale copy (--rebase sets it aside under <work>/stale-baselines/ and starts from the current files); w3gltf and w3city set every copy aside. Copies made before the record existed are not judged

### RL-202 P2 `patch_chunks.py` and `probe.py` read a kit's chunk BSPs raw; kits hold XBSZ by default
- Where: `tools/levelport/patch_chunks.py:252-253` (`data = zf.read(n)`) into `brush_bounds(data)` (`:66-94`) and
  `switch_off(data, ...)` (`:97-115`); `tools/levelport/probe.py:292-303` (`Chunks.brushes`: the 18 lumps unpacked
  from byte 8 of the member). Neither calls `colbsp.plain`. The members are XBSZ unless `--plain`:
  `cp77_collision.py:740,1066`, `colexpand.py:103-109`. Same class as RL-5 (`patch_kit.model0_bounds`).
- Scenario: `patch_chunks.py <name> --base-kit <a current kit>` or `probe.py <report> --kit <a current kit>`: the
  lump table is read out of the XBSZ container's own header, so the offsets are whatever those bytes are:
  `np.frombuffer` raises, or (offsets that happen to fit) garbage planes are matched and a chunk with a shader lump
  appended to compressed data is packed into the dev kit. `colclutter.py:419`, `tag_verdict.py:95` and
  `colexpand.py:282` do decode.
- Fix: `data = colbsp.plain(zf.read(n))` at both places (probe needs `import colbsp`); `switch_off`'s output then
  goes through `colzbsp.encode` when the source was XBSZ, or stays plain and deflated as now.
- Confidence: confirmed (traced; not run against a kit)
- Status: fixed (b9235ee05): both read through colzbsp.member_bsp / colzbsp.plain; the patched chunk stays a plain deflated member (the engine reads both)

### RL-203 P2 `tag_verdict.get` turns any missing key under `.summary.` into 0, so a check on a class that was never compared passes
- Where: `tools/levelport/tag_verdict.py:418-419` (`elif isinstance(v, dict) and '.summary.' in '.' + path: return 0`),
  used by `judge` (`:431-442`); `tools/levelport/scene_parity.py:148-149` (a class enters `summary` only when
  something of it was compared) and `:303-305` (a tile either side lacks: `tiles.missing`, then `continue`).
- Scenario: the tag's kit lacks a parity tile's `t_<tile>.xscn` (or serves none of them: a wrong `--map`, a scene
  pack not linked): `parity.json`'s summary has `tiles: {missing: N}` and no `instances` or `lights` at all. A check
  `parity.summary.instances.only_a == 0` (the docstring's own example path) gets 0 and is "ok". The same for a
  misspelt path in the expectations file, and for a counter renamed in `scene_parity.py`. The verdict passes on
  what was not measured unless the expectations also list `parity.summary.tiles.missing`.
- Fix: 0 only for a missing *leaf* whose parent class is present in the summary; a missing class is a `KeyError`
  (a miss). `scene_parity` can write every class with `matched: 0` so "compared nothing" is visible, and `judge`
  can always fail on `parity.summary.tiles.missing > 0`.
- Confidence: confirmed (traced); no expectations file is in the repository to see which paths are used
- Status: fixed (b9235ee05): a counter is 0 only inside a class the summary holds; a missing class or a misspelt path is a miss. deferred: failing every verdict on parity.summary.tiles.missing > 0 is a gate-policy choice

### RL-204 P2 `nier2gltf.py`: a prop model's flat water is recorded in model space and becomes the next tile's water volume
- Where: `tools/levelport/nier2gltf.py:573-574` (`primitives`: `self.water_surfaces.append(...)` for every flat
  `Wtr00`/`Wtr02` primitive, also when called for a prop at `:673`, where `pos` is the model's own coordinates and no
  placement has been applied), `:612-613` (`water_volumes` consumes and clears the list), `:650-652` (`tile`: the
  volumes are made *before* the props are loaded).
- Scenario: two tiles in one export (`nier2gltf.py g11420 g11419 ...`), the first tile's layout places a prop
  whose model has a flat water part (the code's own example: "bga100: a 427 m sea box", `:684`; the model is
  loaded and its primitives walked before the region test drops the placement). Its extent, in model space, stays
  in `water_surfaces`; the second tile's `water_volumes` writes a water-contents box there (about the origin, down
  to that tile's lowest collision): swimmable water where there is none, and spawns under it are dropped
  (`spawn_points`' `water`). With one tile, or on the last tile, the entry is left unused.
- Fix: record water surfaces only for the tile's own primitives (a flag to `primitives`), or record a prop's with
  its placement matrix applied when the instance is kept.
- Confidence: likely (needs a prop with a flat `Wtr00`/`Wtr02` part; not run)
- Status: fixed (b9235ee05): primitives(water=False) for prop models; not run (no NieR data here)

### RL-205 P2 `patch_materials.py` rewrites a record's flags without the signal-lamp bits and converts without the material's variant
- Where: `tools/levelport/patch_materials.py:52-59` (`record`: bits 0, 2, 0x200, 0x400, 0x800, 0x1000), against
  the builder's `cp2gltf.py:4121-4130` (the same plus `8 | LAMP_INDEX[lamp] << 4`); `patch_materials.py:164`
  (`mats.convert(rec)`) and `:139` (`scene_material(rec, mats, images)`) against `cp2gltf.py:4087-4090` (the build
  converts `dict(rec, _variant=...)` for a stand-in ad face and `dict(rec, _zone=..., _lamp=..., _crossing=...)` for
  a signal head's lamp).
- Scenario: `--template` names a template some signal lamp or stand-in ad material is drawn with. For a lamp the
  comparison at `:182` finds the flags differ (the built record has 8 | lamp << 4, the new one not), so the record
  is "patched": the lamp bits are gone and the renderer no longer switches it. For an ad variant the image is the
  base record's, so `apath != oldpath` and every variant of the key is rewritten to one image. Both count as
  "patched" in the output.
- Fix: keep the old record's bits `record()` does not model (`flags |= old_flags & 0xF8`), and skip (list as
  "needs a rebuild") any material whose `materials.json` key carries a variant or zone; if the key is not in
  `materials.json`, write it there in the scene build.
- Confidence: confirmed for the flag bits (the two writers compared); whether a patched template has such
  materials needs the data
- Status: fixed (b9235ee05): bits 3-7 of the built record (8 | lamp << 4) are kept; an entry whose key carries a variant or lamp zone is listed as needing a rebuild

### RL-206 P2 `w3decals.py`: a failed validation still publishes the build; a layer that fails to read is not printed
- Where: `tools/levelport/w3decals.py:568` and `:580-581` (`bad`), `:654` (`stage.rename(final)` whatever `bad`
  holds), `:664` (printed, exit 0); `:115-117` (read stage: `per_layer[rel] = {'error': ...}; continue`, no print);
  `:112` (`PREFIX + rel` always) against `w3gltf.py:176` (a line starting `dlc/` is a full depot path).
- Scenario 1: the pack reads back with a record out of range, or the maps do not decode back to the source
  positions: `<map>_decals-<n>/` is renamed into place with its pack, looking like any finished build; only the
  last output line says "validation: ...".
- Scenario 2: the layer list is `w3gltf`'s (the docstring's castle run uses the same file) and holds a full depot
  path: `PREFIX` is put before it, `materialize_repo_path` raises, the layer's decals are missing and the only
  trace is an `error` key in `decals_raw.json`; the build stage then reports "decals N of N".
- Fix: `sys.exit` before the rename when `bad` (leave the `.partial` folder); print each layer error and exit 2 at
  the end of `read` when any layer failed; resolve a layer as `w3gltf` does (one helper in `w3gltf`, already
  imported by `w3flags`).
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): a failed validation exits 2 and leaves the .partial folder; a layer that cannot be read is printed and the read stage exits 2; layers resolved by w3gltf.layer_paths

### RL-207 P2 `w3gltf.py` ends "DONE" with exit 0 when layers or meshes failed
- Where: `tools/levelport/w3gltf.py:191-192` (a layer's import exception becomes its status string), `:284-286`
  (`mesh_fail`), `:359-361` (the last line prints placements, meshes written, lights, shapes, vertex mismatches:
  neither the failed layers nor `mesh_failures`).
- Scenario: one layer of the list fails to import (a bundle the add-on cannot read) or 40 meshes fail the raw
  reader: the work folder is written without them, the next steps run, and the scene has holes; `w3scene` counts
  the placements of a failed mesh as `placements_no_material`. The facts are in `summary.json` only.
- Fix: print the failed layers and the mesh failure count in the `W3GLTF_DONE` line and exit non-zero unless
  `--allow-failures`.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): W3GLTF_DONE names the failed layers and the mesh failure count; exit 3 unless --allow-failures

### RL-208 P2 `w3fx.py` places effects by a default origin and box that are one map's
- Where: `tools/levelport/w3fx.py:321-322` (`--box '-1000 -1750 0 -750'`, `--origin '-535 -1327.5 0'`), `:318`
  (`--map` default `w3_city`); `w3scene.py:520` and `w3decals.py:680` take `--build-json` for the same two values.
- Scenario: `w3fx.py ... --map w3_castle` without `--origin`: every placement is written about the city's origin
  (`xfx_blob`: `(pos - origin) * 37.5`), tens to hundreds of metres from the castle build's frame, exit 0. The guide
  says so in prose (`docs/guide/beauclair.md:355`); the tool does not check.
- Fix: `--build-json` (required) as the other two, or no default for `--origin`/`--box`.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): --build-json gives the box and origin; --box/--origin have no default and both are needed without a record

### RL-209 P3 `colexpand.py all --compare`: a chunk process that dies leaves the last run's result to be read as this run's
- Where: `tools/levelport/colexpand.py:137` (`part = out` under `--compare`: the folder is reused), `:208`
  (`if rc and not (a.compare and dst.with_suffix('.json').exists())`), `:212` (reads that `.json`).
- Scenario: a second `--compare` run into the same folder; a chunk's subprocess is killed (memory) before it writes
  its `.json`: the earlier run's `identical: true` is taken, and `compare.json` counts the chunk identical.
- Fix: remove the tile's `.json` before starting its process; under `--compare` accept only exit codes 0 and 1.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): an earlier result is moved to .prev.json before the chunk starts; only exit 1 with a fresh result is taken

### RL-210 P3 `build_watson.py`'s parts gate ignores tiles that exist only in the reference
- Where: `tools/levelport/build_watson.py:579` (`if (r['differing'] or r['only_b']) ...`); `tag_verdict.py:51`
  (`only_a` is computed).
- Scenario: this tag's parts stage wrote no `build/<tile>.npz` for a tile the reference tag has: the "array by
  array" gate passes and the collision compile starts without that tile.
- Fix: include `only_a` unless the inputs say the tag is a subset (`parity_materials_b_only` already marks one).
- Confidence: confirmed (traced)
- Status: deferred: build_watson.py is the levelport-cp lane's file

### RL-211 P3 `w3scene.py`: smaller silent cases
- `tools/levelport/w3scene.py:948,957,1014` and `cp2gltf.py:3217`: an image path is `scn/<map>/<16 hex><role>.dds`,
  written with `ljust(64)`, which does not cut: a `--map` name of 39 characters or more makes the field longer than
  64 B and every later record of the `.xw3b` / `.xw3t` is read shifted. Fix: assert `len(path) < 64`.
- `:828` (`G[::a.far_step, ::a.far_step]`) and `w3water.py:56`: a tile is 257 x 257 vertices; a `--far-step` or
  `--terrain-step` that does not divide 256 drops the last row and column, a strip without ground along every tile
  edge. `w3water.sea_rects` is called without the two steps (`:874`), so the sea's cells are not the terrain's when
  they differ from 2 and 8. Fix: refuse a step that does not divide the tile's cells; pass the steps on.
- `:641-643`: LOD k's switch distance is taken from level k+1 even when every part of level k+1 was skipped (no
  material): LOD k then hands over to level k+2 at k+1's distance.
- `:1029` with `w3trees.py:846-849`: a card material is named `srtbb_<tree stem>`, `_2`, `_3` for a second `.srt`
  of the same stem; `--card-match` is looked up by `name.split('_', 1)[1]`, so the second tree's key is `<stem>_2`
  and its match is never found (counted only in `billboard_matched`).
- `:378`: `src_value(ins.get('Diffuse'), [0.5, 0.5, 0.5])[0]` raises on an unwired scalar default.
- Confidence: confirmed (read); none seen in the Beauclair data (tiles are 257, map names short)
- Status: fixed (b9235ee05): image paths refused at 64 B, steps that do not divide 256 refused, a skipped LOD level hands over at the next drawn level's distance, a scalar Diffuse default read. deferred: passing the terrain steps to w3water.sea_rects changes the sea cells at the default --terrain-step 1 (a look change, needs a run); the card-match key of a second .srt stem (cardmatch.py, which writes the keys, is not in the repository)

### RL-212 P3 `w3terrain.py` / `w3foliage.py` / `w3water.py`: smaller items
- `tools/levelport/w3terrain.py:45`: the output is always `<work>/terrain`; `w3scene.py:813` and the guide
  (`beauclair.md:271-272`) have the far ring made in another work folder and copied by hand to `terrain_far`. Fix:
  `--sub terrain_far`.
- `w3terrain.py:60-64`, `w3foliage.py:44-46`: the tiles under the box are found by 33 x 33 samples: a box wider than
  32 tiles misses tiles between samples (the far ring is 20 x 20 today).
- `w3terrain.py:99-100`: the grid is ordered by a lexsort on (y, x) of transformed float positions and reshaped,
  with no check that each row holds one y; a tile whose rows differ in the last bit comes out shuffled. Fix: sort
  on rounded grid indices, assert monotone rows.
- `w3foliage.py:51`: each tile is imported at `multires_level=8` only to read its foliage clouds.
- `w3water.py:151,175` (`near_raster`): `near[0]` raises when the work folder has no near terrain; `:224` is two
  statements' worth joined on one line with a run of spaces (a lost continuation).
- Confidence: confirmed (read)
- Status: fixed (b9235ee05): w3terrain --sub, tiles filled between the samples' indices (w3terrain.tile_rect, also w3foliage), the grid ordered on rounded indices with a check, near_raster says what is missing, the joined line split. deferred: w3foliage's multires_level 8 (a lower level needs a run to show the clouds are the same)

### RL-213 P3 `w3emitter.py` to `w3fx.py`: decoded values the record cannot carry are dropped without a count
- Where: `tools/levelport/w3fx.py:170-239` (`decoded_record`), `tools/levelport/w3emitter.py:168-179` (`_blocks`).
- `velocity_life_mode == 'absolute'` (a speed curve, `MOD_BITS` 0) is written as a multiplier (`cp77fx` has
  `F_VEL_LIFE_ABS` for it); a `[start, end]` pair over the emitter's time (`<name>_random` false) is used as a random
  range for life, size, alpha and rotation; `accel_life`, `color_life`, `stacked` and `undecoded` are ignored.
- `_blocks`: 18 samples are split 2+16 only when the first two are equal; a random or start/end pair followed by a
  curve is read as 16+2.
- Fix: count each case in `stats` (`emitters_decoded_partial`), set `F_VEL_LIFE_ABS` for the absolute mode.
- Confidence: confirmed (read); how many of the city's emitters this touches was not counted
- Status: fixed (b9235ee05): emitters_decoded_partial and decoded_dropped_<what> counted. deferred: F_VEL_LIFE_ABS for the absolute mode (changes the effects drawn, needs a run) and the 18-sample split (needs the data)

### RL-214 P3 `cp77_collision.py`: smaller items
- `tools/levelport/cp77_collision.py:369-370` (`spawn_corpus`): `%g` keeps six significant digits; a spawn at
  |coordinate| >= 100,000 u with a fraction (the hub frame reaches 112,800 u) is traced from a rounded point. Fix:
  `%.9g` or `repr`.
- `:587-596` (`recover_members`): the stopped pack (up to `PACK_MAX`) is read whole, then every member sliced and,
  when deflated, inflated in memory to check it.
- `:765-792` (`seed`): the docstring's condition ("only after a cold compile of some of its tiles ... matched its
  bytes") is not checked; a build made by another q3map2 or gltf2map is indexed under the current compile identity
  and every later run reuses its chunks. Fix: compile one tile cold and compare before indexing.
- Confidence: confirmed (read)
- Status: deferred: cp77_collision.py is the levelport-cp lane's file

### RL-215 P3 Efficiency: packs reopened for every member
- `tools/levelport/patch_water.py:56,59,116,129` (`zipfile.ZipFile(pk).read(info)` per shard: the central directory
  of a multi-GB pack parsed once a member, the handles never closed), `tag_probesky.py:113,119,132`,
  `colclutter.py:479`. Same class as RL-16. Fix: open each pack once.
- `tools/levelport/nier2gltf.py:673`: `material_vars(w)` recomputed for every mesh group of a prop model.
- `tools/levelport/w3fx.py:275`: `placements = list(placements) + [...]` once per entity effect.
- Confidence: confirmed (read)
- Status: fixed (b9235ee05): patch_water, tag_probesky and colclutter read each pack's directory once; nier2gltf's material_vars once a model; w3fx appends

### RL-216 P3 Smaller items
- `tools/levelport/patch_water.py:87`: a missing `--sea` file (the default is a research-folder path) gives zero
  sea patches, said only as "sea patches 0" in the last line. Fix: exit when the file is named and missing.
- `tools/levelport/patch_chunks.py:396-402`: the scene patch pack is written in place in the kit and `KIT.txt`
  appended on every run of the `scene` step.
- `tools/levelport/patch_tiles.py:99-102`: a dev name used again with another `--from-tag` keeps the first tag's
  hard-linked `<tile>.json` (`if not d.exists()`).
- `tools/levelport/colexpand.py:60-63`: `head['scripts']` paths from the slab pack are joined under the temporary
  stage without a check for `..` or a drive: a pack from elsewhere can write a text file outside it. The tool is
  meant to run at install on a shipped pack. Fix: reject a path that leaves the stage.
- `tools/levelport/tag_verdict.py:457`: no arguments: `ValueError` from the unpacking instead of the usage.
- `tools/levelport/w3grade.py:16-17` says the `.xenv` sets `r_sceneGradeExposure` to 1; `w3env.py:71,145` writes 8
  by default. `w3grade.py:172`: `out[:, 0, 0] if False else ...` (a dead branch); `:123` `bpath_file` unused.
- `tools/levelport/scene_parity.py:297`: `used = set()` is overwritten at `:308`.
- `tools/levelport/w3fx.py:96-97` (docstring: "its red the density, its green the light") against `:111-114` (red
  the shading, green the density); `:81` comment belongs to the line above it.
- `tools/levelport/build_watson.py:8-36`: the docstring's stage order and its `checks` line (`logs/chunk_crc.json`)
  are older than `STAGES` (`:79`) and the `checks` method (`:860-926`).
- `tools/levelport/w3decals.py:109`: `not l.startswith('#')` is tested on the unstripped line.
- `json.dump(obj, open(path, 'w'))` without a close (RL-30's class): `w3terrain.py:110`, `w3foliage.py:83,97`,
  `w3env.py:165`, `w3grade.py:174`, `w3decals.py:160-161`, `w3flags.py:414-415`, `w3_preview.py:167`,
  `w3fx_dump.py:133`.
- Confidence: confirmed (read)
- Status: fixed (b9235ee05): patch_water --sea checked; the scene patch pack written under a temporary name and its KIT.txt section replaced; patch_tiles relinks another tag's files; colexpand refuses a script path that leaves the stage; tag_verdict prints its usage; w3grade's docstring, dead branch and unused name; scene_parity; w3fx's docstring and comment; w3decals' comment test; every json.dump listed. build_watson's docstring is the levelport-cp lane's

## Publish cleanliness

Beyond the first pass's list (RL-1 and its "Publish cleanliness" hold for these files too):

- `tools/levelport/colclutter.py:46`, `patch_chunks.py:63`, `scene_parity.py:181-182`, `tag_probesky.py:40`,
  `tag_interiors.py:87`, `patch_water.py:48`, `probe.py:100`: Watson's frame (`-1280, 2240`, floor `12.0`, `37.5`)
  as literals beside `cp2gltf.AREAS` and `cp2gltf.gltf_upm()`; `w3decals.py:88` `UPM = 37.5` beside
  `levelport.UNITS_PER_METRE`. One accessor (`cp2gltf.area_frame(area)` -> centre, floor, units per metre).
- `tools/levelport/tag_verdict.py:216,313,317`: `maps/cp_watson...` written out where every other tool takes
  `--map`; `w3decals.py:363`: the figure's title says `w3_castle` whatever `--map`.
- `tools/levelport/colclutter.py:651`: `--json` defaults to `.codex-research/levelport/cp77/watson/json`;
  `patch_water.py:38-39`: `--sea` defaults to `.codex-research/levelport/look/ocean-20261104/sea20.npy` (a dated
  research output as a default).
- Contracts cited that are not in the repository: `tools/levelport/w3scene.py:1018` and
  `codemp/rd-vulkan/vk_scene.cpp:5270`, `w3scene.py:20`, `w3trees.py:73` (`.codex-research/w3-billboards-20261118/CONTRACT.md`), `w3decals.py:3`
  (`.codex-research/levelport/look/decals-20261106/CONTRACT.md`), `w3trees.py` ("contract part 1/2" at `:655,685`).
  The `.xw3b` version 3 layout is written out in both files; move the two contracts to `docs/` or drop the paths.
- Session and agent names in comments: `w3scene.py:364` ("the flags agent"), `:455` ("waterfalls
  REPORT_FABLE_DATA"), `:756,814,822` ("Night City session"), `patch_water.py:5`, `tag_interiors.py:8`,
  `tag_probesky.py:1` ("the renderer session"), `patch_chunks.py:24` ("Vic's"), `build_watson.py:78,240,508,513,535`
  and others ("REPORT3 S3", "REPORT4 T5": reports not in the repository).
- "the owner" with dates: `colclutter.py:1,21,202,209`, `w3trees.py:905`, `w3env.py:76-77`, `w3scene.py:88,351`,
  `patch_materials.py:69`, `cp77_collision.py:892`, `build_watson.py:2,12`: the first pass's item, these lines.
- No credentials, URLs or TODO markers in the files read. `w3emitter.py:8` credits its record layout to WolvenKit
  7's `SParticleEmitterModuleData` ("decompiled"): say in the header under which licence that reading stands
  (WolvenKit is GPL-3.0; a layout description is not its code, but the note should be deliberate). `w3trees.py`,
  `w3gltf.py`, `w3flags.py`, `w3decals.py` call the Witcher3-Blender-Tools add-on's private functions
  (`_srt_json_from_file`, `_choose_srt_texture_cache_item`, `_convert_xbm_to_writable_dds`,
  `_extract_streaming_buffer_bytes`) at run time and copy none of its code; `w3grade.py:136` names the add-on's
  `_sample_balance_slice` as the model for its own `sample`: a credit line in the header.

## Liftability

- **A W3 work folder has no owner.** Six tools rewrite `placements.json`, `materials.json` and
  `meshes/index.json` in place, each with its own `pre-<step>` copy and its own idea of where to start
  (`w3gltf`, `w3foliage --merge`, `w3trees` default and `--update`, `w3merge --tag`, RL-3, RL-201). Shape: one
  `w3work.py` that holds the export as written by `w3gltf` (never edited) and a list of named layers over it
  (foliage, trees, each merge tag) replayed in a fixed order into the three files; each tool only writes its own
  layer.
- **Chunk BSP access.** Four readers of a chunk member's lumps: `colbsp.Rbsp` (decodes XBSZ),
  `patch_chunks.lumps`/`brush_bounds`, `probe.Chunks.brushes`, `patch_kit.model0_bounds` (the last three raw:
  RL-5, RL-202); three `brush_bounds` (`colclutter.py:291`, `patch_chunks.py:70`, `colmerge.py:266-268` inline) and
  three copies of the `MODEL`/`FOG` dtypes (`colclutter.py:324-327`, `colmerge.py:42-44`,
  `cp77_collision.py:313`). Shape: `colbsp` owns `MODEL`, `FOG`, `brush_bounds(b)` and "read a chunk member"
  (`plain` applied); the others import.
- **The scene instance record.** `INST` is declared in `patch_chunks.py:351`, `patch_water.py:28`,
  `tag_interiors.py:38`, `tag_probesky.py:37`, and unpacked by format string in `scene_parity.py:116`; the slot's
  bit meanings (24 interior, 25 no decals, 26 on surface, 27 hide centre, 28-31 probe sky) live in three tools'
  docstrings and `w3scene.py:740-801`. With the first pass's `xscn.py`: the dtype, the slot bits as named
  constants, and "read a tile's instances / write them back" (the same five lines in three tools).
- **"The kit as the engine serves it."** `served()` in `patch_materials.py:41`, `tag_verdict.py:207`, the `serve`
  dicts of `patch_chunks.py:315-319,358-362`, `tag_interiors.py:101-105`, `tag_probesky.py:109-114`,
  `scene_parity.Side`, `probe.Chunks.__init__`: seven copies of "sort by upper-cased name, last pack wins". One
  class (the first pass's `pk3util.py`).
- **Tile splitting of stored shards** (`LIMIT`, the `zz_<map>_scene<k>.pk3` roll-over): `patch_water.py:29,127-133`
  and `tag_interiors.py:48,163-166`, the same loop.
- **`w3fx.py` and `cp77fx.py`** share `xfx_blob` but not the record's field list (RL-200). Shape: `xfx.py` (the
  first pass's proposal) with `new_emitter(**fields)` that fills every field's default, used by both front ends.

## Still open from earlier reviews

- RL-1 (local paths: `w3scene.py:68`, `w3trees.py:105`, `w3fx.py:33`, `w3flags.py:348`, `nier2gltf.py:45` and the
  `--w3` defaults), RL-3 (`w3merge.py`; RL-201 is its siblings), RL-5 (`patch_kit.model0_bounds`; RL-202 is its
  siblings), RL-7 (direct pack writes: `w3fx.py:296`, `w3scene.py:1000`, `w3env.py:158`, `w3grade.py:165`),
  RL-9 (`w3scene.py:1000`), RL-16, RL-29 (`bsp2gltf.py:32`, `tag_verdict.py:80`, `patch_chunks.py:39`,
  `w3foliage.py:19`, `w3decals.py:78`, `w3fx.py:19`, `w3scene.py:34,51`), RL-30: all seen again, still real.
- `docs/review/FINDINGS.md`, `PLAN.md`, `2026-11-09-character-rendering.md` and `2026-11-09-bot2.md` hold no
  finding on these files.
