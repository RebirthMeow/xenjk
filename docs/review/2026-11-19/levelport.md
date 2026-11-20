# Level port tools (review 2026-11-19, master a322d9dc4)

Scope: `tools/levelport/`: 87 Python files (32,714 lines), `vcache.c` (133), `cp77_area.sh` (66),
`wk_read_lenient.ps1` (52), `areas/*.json` (5 files), `q3map2/` (7 patches, 996 lines, and its README).

**Coverage is not uniform, and this report does not meet the contract's "every file".** The lane was to be split
over parallel readers; the session's subagent limit was already reached, so one reader did it. What was done:

- **Read in full (every code line):** `levelport.py`, `stage.py`, `presets.py`, `mapio.py`, `q3bsp.py`, `gltfio.py`,
  `stagekey.py`, `seed_work.py`, `jobguard.py`, `look.py`, `look_stats.py`, `relight.py`, `make_xlod.py`,
  `vcache.py`, `vcache.c`, `retain.py`, `memgate.py`, `engine_trace_ded.py`, `light_time.py`, `lighttransfer.py`,
  `blender_decimate.py`, `blender_preview.py`, `scene_pick.py`, `zfight.py`, `colpack.py`, `colzbsp.py`,
  `colcook.py`, `colbsp.py`, `patch_kit.py`, `kit_derive.py`, `kit_verify.py`, `w3bundle.py`, `w3city.py`,
  `w3merge.py`, `w3build.py`, `cp77_area.sh`, `wk_read_lenient.ps1`, all seven q3map2 patches and
  `q3map2/README.md`. The five `areas/*.json` parse and hold no local paths.
- **Read in part (the orchestration, caching, subprocess and pack-writing paths):** `gltf2map.py` (lines 44-135,
  819-1000, 1100-1583; the `Compiler` class 136-818 was not read), `build_watson.py` (84-470, 784-860, 949-1076),
  `cp2gltf.py` (36-362, 2262-2277, 5345-5366 only: about 7% of it), `cp77_collision.py` (60-147, 375-400, 438-560,
  1030-1140), `w3scene.py` (28-135, 880-1063).
- **Pattern-swept only** (every subprocess call, every absolute path and environment lookup, every broad `except`,
  every pack writer, unused top-level names and imports, helper names defined in more than one file), **not read:**
  `bsp2gltf.py`, `nier2gltf.py`, `slabwriter.py`, `levelcheck.py`, `probe.py`, `scene_parity.py`, `colclutter.py`,
  `colexpand.py`, `colmerge.py`, `cp77_ads.py`, `cp77_foliage.py`, `cp77_meshlod.py`, `cp77_rotate.py`,
  `cp77_tiles.py`, `cp77decals.py`, `cp77fx.py`, `cp77mat.py`, `patch_chunks.py`, `patch_materials.py`,
  `patch_tiles.py`, `patch_water.py`, `tag_interiors.py`, `tag_probesky.py`, `tag_verdict.py`, `wk_fetch.py`,
  `w3_preview.py`, `w3autohide.py`, `w3decals.py`, `w3emitter.py`, `w3env.py`, `w3flags.py`, `w3foliage.py`,
  `w3fx.py`, `w3fx_dump.py`, `w3gltf.py`, `w3grade.py`, `w3terrain.py`, `w3trees.py`, `w3water.py`.
  Correctness findings in these files (wrong units, struct layouts, transforms, silent skips) are **not** ruled out;
  they need their own pass. About 20,000 lines are in this group.

Nothing was run or built. The q3map2 patches were reviewed as diffs only: no netradiant-custom source tree was at
hand, so anything that depends on unpatched upstream code is marked "likely".

Files read in full with no findings: `levelport.py`, `presets.py`, `q3bsp.py`, `seed_work.py`, `jobguard.py`,
`look_stats.py`, `scene_pick.py`, `colcook.py`, `colbsp.py`, `kit_verify.py`, `kit_derive.py`,
`cullsides_sweep.patch`, `world_coords.patch`, `brush_cook.patch`.

No shell is involved in any subprocess call in the lane (no `shell=True`, `os.system` or string commands; every
call passes an argument list), so there is no quoting finding.

## Findings

### RL-1 P1 Local absolute paths are the defaults of 20 tools; some have no override at all
- Where: see "Publish cleanliness" for every line. No override: `w3bundle.py:17` (`DOBOZ`), `w3scene.py:68`
  (`W3_UNCOOK`), `w3fx.py:33` (`UNCOOK`), `w3flags.py:348` (a texture path inside a function),
  `engine_trace_ded.py:31-32,69` (`RUN_HOMES`, the registry file).
- Scenario: on any other machine `w3scene.py` looks for flow textures under `<w3 extraction>\uncook`
  whatever `--w3` the other W3 tools were given (`self.tex.get` counts the file as missing and the waterfall draws
  untextured: silent); `w3bundle.read_entry` raises on the first doboz-compressed file; `engine_trace_ded.py`
  cannot place a run home. The paths also publish the machine's folder layout.
- Fix: one `tools/levelport/lp_paths.py` that reads each location from an environment variable or a small untracked
  `levelport.local.json`, with no machine default (exit with the variable's name when unset); the W3 module
  constants take `--w3`.
- Confidence: confirmed (traced)
- Status: fixed in the W3 and collision files (b9235ee05): w3paths.py (W3_GAME, else W3_ROOT; W3_UNCOOK; W3_RESEARCH or --w3; W3_DOBOZ), colpack's XENJK_LIBZSTD (else beside $Q3MAP2, else the loader's search; no machine default), nier2gltf's NIER_DATA / NIER_TOOLS (--game / --nier-tools), w3patches.py now in tools/levelport; the rest is the levelport-cp lane's
- Status: fixed (c4aafcfd0) in the levelport-cp files: tools/levelport/paths.py holds the repository, $XENJK_RESEARCH, $XENJK_GAMEDATA and every outside tool by its variable with no machine default (cp2gltf, gltf2map, levelcheck, engine_trace_ded, retain, seed_work, look, vcache, cp77_ads, cp77_collision, cp77_tiles, build_watson, wk_read_lenient.ps1, cp77_area.sh, README); the W3 tools, colpack.py and nier2gltf.py are the levelport-w3col lane's, to adopt paths.py after the merge

### RL-2 P1 `import gltf2map` raises StopIteration unless a sibling `../Xenmap/.tools/` q3map2 exists
- Where: `tools/levelport/gltf2map.py:54-58` (`Q3MAP2 = next(p for p in (...) if p is not None and p.exists())`),
  importers `stage.py:16`, `relight.py:13`, `cp77_tiles.py:469`; `gltf2map.py:61` (`Q3MAP2_EXACT`, same layout).
- Scenario: a checkout with no `$Q3MAP2` and no `Xenmap` folder beside the repository: `python gltf2map.py --help`,
  `stage.py --pack` (which runs no q3map2) and `gltf2map.py --no-compile` all die at import with a bare
  `StopIteration` traceback.
- Fix: `next(..., None)`; exit with "set $Q3MAP2 (see tools/levelport/q3map2/README.md)" in `q3map2()` and in the
  `--cook` path when it is None or missing.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): gltf2map.q3map2_exe() resolves q3map2 at first use and exits naming $Q3MAP2 (or $Q3MAP2_CHUNKS); import, --help and --no-compile need none

### RL-3 P1 `w3merge.py`: re-running an earlier tag silently removes every later tag's merge
- Where: `tools/levelport/w3merge.py:35-39` (each run starts from `<name>.pre-<tag>.json`) and `:75-78` (writes
  the three work files from that).
- Scenario: `--tag flags`, then `--tag water` (both in `docs/guide/beauclair.md:253,283`), then `--tag flags`
  again after rebuilding the flags: the run loads `placements.pre-flags.json` (no water), appends the flags and
  writes it. The water placements, index records and materials are gone from the work folder; nothing is printed
  about them, and `placements.pre-water.json` still exists, so a later `--tag water` run starts from the *old*
  flags.
- Fix: keep one `pre-merge` baseline and a recorded list of (tag, stage) merges, and replay every recorded tag in
  order on each run; or refuse to re-run a tag while a later tag's `pre-` copy exists.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): w3work.py; a re-run whose copies are older than another step's last run is refused (--rebase), and a step that runs again says which later steps must follow

### RL-4 P2 `build_watson.py`: a stage's key never includes its `METHODS` code, so an edited `Run.env` is not re-run
- Where: `tools/levelport/build_watson.py:193-207` (`stage_key`: tools, inputs, arguments, upstream runs),
  `:209-216` (`key_parts` does add `'code Run.' + m`), `:95` (`METHODS = {'env': ['env']}`), `:88-93`
  (`TOOLS['env'] = []`), `:251` (`plan` compares `m.get('key')` with `stage_key`).
- Scenario: change how `Run.env` builds the env packs, run the same tag: `plan` finds the env marker's key
  unchanged and keeps the old packs; the kit ships them. `moved()` would name the method, but it is only called
  after the keys already differ.
- Fix: hash `method_sha(m)` for `METHODS.get(stage, [])` in `stage_key` (or derive the key from `key_parts`).
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): stage_key hashes METHODS' method_sha (the env stage reruns once)

### RL-5 P2 `patch_kit.py` carries an older copy of `model0_bounds` with no XBSZ branch
- Where: `tools/levelport/patch_kit.py:59-81` against `tools/levelport/cp77_collision.py:388-400`; used at
  `patch_kit.py:197`.
- Scenario: `--builds` holds chunk packs made with `gltf2map --cook --xbsz` (cp77_collision's default,
  `cp77_collision.py:740,1066`): the member is stored and starts with `XBSZ`. The copy reads the models lump's
  offset from byte 64 of the XBSZ container (a length field of the embedded header) and takes 24 bytes of
  compressed data there as mins and maxs. The `.chunks` line gets garbage bounds (the engine places and pages the
  chunk by them) or the tool exits with "ended before its models lump". Today's `patch_tiles.py` compiles the text
  path (plain BSPs), so it is latent.
- Fix: import `member`, `model0_bounds`, `raw_copy` from one module (see Liftability) instead of the copies.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): an XBSZ member is decoded (colzbsp.member_bsp); dead chunk_line removed

### RL-6 P2 `gltf2map.py --grid-chunks` compiles its chunks with default settings, not the run's
- Where: `tools/levelport/gltf2map.py:1468-1472`.
- Scenario: the parent run is given `--merge-verts 3`, `--no-slim` / `--slim-snap 1`, `--tile-brushes N` or
  `--slab-cache none`; the child command forwards only `--scale`, `--jobs`, `--exact-planes` and `--scratch`, so
  each chunk is built with merge 12, slim 0, 500,000 and the default cache, and is merged into a base compiled with
  the other values. No message.
- Fix: forward every option that changes a chunk's bytes (build the child's argument list from `a`).
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): the child gets --merge-verts, --tile-brushes, --tile-parallel, --slab-cache and --slim-snap / --no-slim

### RL-7 P2 Packs are written straight to their final names; a stopped write is then trusted by name
- Where: `tools/levelport/gltf2map.py:1545` (and `:1566`, `:1574`: a pack that failed its read-back is left in
  place), trusted by `cp77_collision.py:1034` (`tile_done`: the pack exists) and `:1087-1089` (packs it, through
  `model0_bounds` -> `zipfile.ZipFile`), by `gltf2map.py:1465` and by `retain.py:72`. Same direct writes:
  `cp77fx.py:625`, `w3fx.py:296`, `w3scene.py:1000`, `w3env.py:158`, `w3grade.py:165`, `stage.py:28`,
  `relight.py:30`, `make_xlod.py:111`.
- Scenario: the collision stage is stopped (the driver's job object kills its children) while a tile's
  `zz_chunk_<t>.pk3` is being written. On every rerun `compile_all` finds the file, calls `packs.add`, and
  `zipfile.BadZipFile` is raised in the main thread before any worker starts: "a rerun resumes" never does until the
  file is moved away by hand. For the fx/scene writers a stopped run leaves a short pack under the name
  `build_watson`'s preflight then reports as "exists already ... Resume with --from <next> (it keeps this output)".
- Fix: write `<name>.tmp`, close, `os.replace` (as `colbsp.write`, `colzbsp.cmd_packs`, `patch_chunks.py:276` and
  the `.partial` folders already do); on a failed read-back rename the pack to `.bad`.
- Confidence: confirmed for the write and the trust; the stop mid-write itself needs a run
- Status: fixed for w3fx, w3scene, w3env, w3grade (b9235ee05; pk3write.writing, 845c9047e); the other writers are the levelport-cp lane's
- Status: fixed (c4aafcfd0) for gltf2map (temporary name, read back, renamed; a failed read-back becomes .bad), stage.py, relight.py, make_xlod.py and cp77fx.py; the w3*.py writers are the levelport-w3col lane's

### RL-8 P2 `gltf2map.py --cook` without `$Q3MAP2` runs the build that has no `-brushbin`
- Where: `tools/levelport/gltf2map.py:61` (`Q3MAP2_EXACT` = `...-xenjk7`), `:1319-1322`, `:1402-1405`;
  `tools/levelport/q3map2/README.md` ("The build with `brush_cook.patch` and `brush_bin.patch` ... is `xenjk9`
  (gltf2map's `--cook` ...)").
- Scenario: `python gltf2map.py tile.glb chunk_x --col-chunk --exact-planes --cook` as the option's help describes:
  `--exact-planes` selects xenjk7, `colcook.cook` adds `-brushbin - -`, xenjk7 does not know the option and the run
  ends in "q3map2 -brushbin: its output ended early". It only works through `cp77_collision.py`, which sets
  `Q3MAP2` itself (`:1072`).
- Fix: a `Q3MAP2_COOK` (xenjk9) chosen when `a.cook`, or check the exe for `brushbin` as
  `build_watson.py:329` does and say which build is needed.
- Confidence: confirmed (traced; the README and code disagree)
- Status: fixed (c4aafcfd0): --exact-planes and --cook take $Q3MAP2 when set, else $Q3MAP2_CHUNKS; --cook checks the build has -brushbin; q3map2/README.md says which build is which

### RL-9 P2 `w3scene.py` writes one scene pack with zip64 allowed and no size check
- Where: `tools/levelport/w3scene.py:1000` (`zipfile.ZipFile(pk3, 'w')`), against `cp2gltf.py:5359-5362`
  (`allowZip64=False`, packs split under the engine's limit).
- Scenario: Beauclair's city pack is 2.98 GB of the engine's 4 GB (`docs/levelport-beauclair.md:434`). A larger
  `--box` or `--tex-size` passes 4 GB; Python switches to zip64 without a word and the engine cannot open the pack.
- Fix: `allowZip64=False` (the write then fails loudly) and split the images into `zz_<map>_scene_img<k>.pk3` as
  the CP2077 scene does.
- Confidence: likely (the limit is the engine's as documented; not run)
- Status: fixed (b9235ee05): allowZip64=False and a temporary name, so a pack past the limit fails. deferred: splitting the images into zz_<map>_scene_img<k>.pk3 (Beauclair's 2.98 GB fits; needs a build to check the split)

### RL-10 P2 `retain.py`: with no `game_directory` found, installed builds lose their protection silently
- Where: `tools/levelport/retain.py:34` (`GAME`), `:232-233` (`game = list(files(game_dir))`; `os.walk` of a
  missing folder yields nothing), `:292`.
- Scenario: run from a checkout whose `game_directory` is absent or elsewhere (a worktree: the KEEP.txt case at
  `:127-134` was fixed for exactly this, `GAME` was not): `installed` is empty, no build is kept as 'installed',
  and `--apply` moves the installed kit's builds to the trash. Recoverable (a move), but the game's packs are hard
  links whose other names are now in the trash.
- Fix: exit when `GAME` is not a directory, as `load_keep` does for KEEP.txt; resolve it like
  `engine_trace_ded.game_data()`.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): retain exits when GAMEDATA/base is not a folder ($XENJK_GAMEDATA)

### RL-11 P2 `retain.py`: a linked file outside the level port is moved to a path outside the trash folder
- Where: `tools/levelport/retain.py:281-283` (run homes named by the registry join `links`), `:337-338` (every
  link to a moved file moves), `:375-379` (`rel = os.path.relpath(p, LP)`; `dest / rel`).
- Scenario: the registry names a run home on the level port's volume but not under it; it hard-links a pack that
  moves. `rel` is `..\..\<home>\...`, so the rename's target is `<trash>\<stamp>\..\..\<home>\...`: the file lands
  outside the stamped folder (possibly over the trash root's parent tree), and MANIFEST.tsv records a path
  `research_shed.py --restore` cannot map back. A path on another drive makes `relpath` raise `ValueError`, which
  the `except OSError` does not catch: the run stops with part of the plan moved.
- Fix: for a path not under `LP`, store it under `dest / '_outside' / <drive> / <path>` and write the absolute
  source in the manifest; catch `ValueError` with `OSError`.
- Confidence: likely (needs a registry entry outside the level port; the default homes are inside it)
- Status: fixed (c4aafcfd0): a link outside the level port (or on another drive) is left in place and counted as skipped, so the manifest stays level-port-relative and research_shed --restore keeps working; the ValueError cannot escape

### RL-12 P2 `retain.py` and `seed_work.py` walk through directory junctions (Python 3.11)
- Where: `tools/levelport/retain.py:83` (`os.walk`), `:253`, `:320` (`e.is_dir(follow_symlinks=False)` is true for a
  junction before Python 3.12); `seed_work.py:48`.
- Scenario: a build or work folder that is a junction to another tree (the level port itself is one): its target's
  files are listed as the build's and, if the build is superseded, renamed into the trash from the target tree.
- Fix: skip entries for which `os.path.isjunction` (3.12) or `os.lstat(...).st_file_attributes &
  FILE_ATTRIBUTE_REPARSE_POINT` is set, in `files()` and in the `scandir` loops.
- Confidence: needs a run (the interpreter here is 3.11.9; no junction below `build/` was looked for)
- Status: fixed (c4aafcfd0): retain.is_junction (os.path.isjunction, else the reparse-point attribute) prunes junctions in files(), the build and work scans and seed_work's walk; checked true on the level-port junction, false on .codex-research. A dry run over the real tree was not made

### RL-13 P2 Stage keys follow Python imports only: three pieces of stage code are outside every key
- Where: `tools/levelport/stagekey.py:119-185` and `cp77_collision.py:438-454` (`module_closure`); the unkeyed
  code: `blender_decimate.py` (run by path, `cp2gltf.py:2272`), `wk_read_lenient.ps1` (`cp2gltf.py:326`),
  `vcache.c` (compiled by `vcache.py`, which is keyed; the C source is not).
- Scenario: change the decimation modifier, the lenient reader or the vertex-cache scoring: the outputs differ, but
  `stage_record_key`, `cp77_tiles`' part keys and `build_watson`'s stage keys do not move, so parts and stages are
  kept from before the change.
- Fix: let a module declare extra key files (`STAGEKEY_FILES = ['vcache.c']`) that both closures hash.
- Confidence: confirmed (traced)
- Status: deferred: keying wk_read_lenient.ps1 and vcache.c (blender_decimate.py is the levelport-w3col lane's) moves every stage and part key that reaches cp2gltf or vcache once, a full rebuild of the parts: owner's call on when

### RL-14 P2 `w3bundle.index` cache is never checked against the bundles
- Where: `tools/levelport/w3bundle.py:24-26,48-50`.
- Scenario: the game is patched or a mod bundle is added after the pickle was written: `read()` seeks to old
  offsets and returns other bytes (or a decompression error) for every moved file.
- Fix: store each bundle's (path, size, mtime) in the cache and rebuild on a mismatch; JSON instead of pickle.
- Confidence: confirmed (traced)
- Status: fixed (b9235ee05): the cache holds each bundle's path, size and mtime and is rebuilt on a mismatch (still a pickle: the cache file name the guide uses is kept)

### RL-15 P2 `vcache.py`: first use from several processes races the build, and a load failure is not caught
- Where: `tools/levelport/vcache.py:40-55`.
- Scenario: a scene build's worker pool calls `order()` for the first time in several workers at once: each
  writes the same `vcache_build.bat` and runs `cl` on the same output; one sees `dll.exists()` while another's
  linker is still writing and `ctypes.CDLL` raises `OSError`, which ends that worker (the docstring promises "on
  failure, order() returns the triangles unchanged").
- Fix: build to `vcache-<tag>.<pid>.dll` and `os.replace`; wrap `CDLL` in `try/except OSError` -> `_lib = False`.
- Confidence: likely (not run)
- Status: fixed (c4aafcfd0): built under per-process names and renamed, CDLL failure caught (built and loaded here with cl; reorders, keeps every triangle)

### RL-16 P2 `colzbsp.py packs` holds finished chunks in memory and re-opens each pack per member
- Where: `tools/levelport/colzbsp.py:145` (`jobs.sort(key=lambda j: -zipfile.ZipFile(j[0]).getinfo(...))`),
  `:163-164` (`ex.map` over all jobs, largest first).
- Scenario: a kit's chunks: the sort parses a pack's whole directory once per member and never closes the handles;
  `ex.map` returns in order, so while the largest (slowest, level 19) chunks encode, every smaller chunk already
  encoded waits in memory.
- Fix: read each pack's `infolist()` once; submit futures and write them as they complete (the member order in a
  stored pack does not matter), or bound the window.
- Confidence: likely (sizes not measured)
- Status: fixed (b9235ee05): each pack's directory read once; results consumed in job order with at most 4 per worker ahead, so the member order (and the packs) are unchanged

### RL-17 P2 `gltfio.Writer.save` copies the binary buffer twice more and truncates the output before it can fail
- Where: `tools/levelport/gltfio.py:146-150`.
- Scenario: a city-sized glb: `bytes(self.bin)` and the `+` each make another copy of a multi-GB buffer; past
  4 GB `struct.pack('<4sII', ..., total)` raises after `open(path, 'wb')` already emptied the file.
- Fix: compute and check `total` before opening; `f.write(self.bin)` directly; write to a temporary name.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): size checked before anything is written, the buffer written without copies, to a temporary name renamed

### RL-18 P2 `cp2gltf.stage_decimate` ignores Blender's exit code
- Where: `tools/levelport/cp2gltf.py:2272-2276`; `blender_decimate.py:35-46` exits 0 whatever failed.
- Scenario: Blender crashes part way through the batch (or during an export): the stage prints the last
  "decimate" lines it saw and returns; later stages take each `dec_path` that exists, a half-written one included.
- Fix: check `r.returncode` and the "decimate done: N jobs, M failed" line; export to a temporary name and rename
  in `blender_decimate.run`.
- Confidence: likely (the reader of a short glb was not traced)
- Status: blender_decimate side fixed (b9235ee05): each dst written as <name>.part.glb and renamed; the batch still exits 0 by design (a failed job keeps the original). deferred: checking the exit code and the done line in cp2gltf.stage_decimate is the levelport-cp lane's
- Status: fixed (c4aafcfd0) in cp2gltf: Blender's exit code and its "decimate done" line are checked; the exports go to <name>.tmp.glb and are renamed only after the whole batch. blender_decimate.py's own exit 0 is the levelport-w3col lane's file

### RL-19 P2 `brush_bin.patch` makes q3map2 Windows-only
- Where: `tools/levelport/q3map2/brush_bin.patch`: `main.cpp` (`#include <io.h>`, `_dup`, `_dup2`, unguarded),
  `brushbin.inc` (`<io.h>`, `_setmode`, `_fdopen`, `_O_BINARY`).
- Scenario: applying the patch set to netradiant-custom on Linux or macOS: `main.cpp` no longer compiles.
  `large_maps.patch` in the same folder does guard its Windows calls.
- Fix: `#ifdef _WIN32` around the includes and the mode calls; `dup`/`dup2`/`fdopen` elsewhere.
- Confidence: confirmed (read)
- Status: deferred: needs the netradiant-custom tree to edit the patch, rebuild it on Windows and another platform and check it applies

### RL-20 P2 `engine_trace_ded.py` registers run homes in a fixed file that `retain.py` reads by another rule
- Where: `tools/levelport/engine_trace_ded.py:69` (`<levelport root>\levelport\runhomes\registry.tsv`),
  `tools/levelport/retain.py:104` (`REGISTRY = LP / 'runhomes' / 'registry.tsv'`).
- Scenario: any checkout whose `.codex-research/levelport` is not that junction: the trace tool writes (or, with
  no C: folder, silently fails to write: `except OSError: pass`) a registry retain never reads, so the homes' hard
  links to moved packs stay behind and the trash "holds names, not space".
- Fix: both from one constant (RL-1's paths module).
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): engine_trace_ded writes paths.LEVELPORT/runhomes/registry.tsv, retain.REGISTRY's file; the run-home roots are the level port's and the research root's runhomes, picked by drive

### RL-21 P3 `build_watson.py` writes the kit's launchers with CR CR LF line ends
- Where: `tools/levelport/build_watson.py:840-844` (`'...\r\n'` through `Path.write_text`, which turns each `\n`
  into `\r\n` on Windows).
- Scenario: every `Play_Watson_*.bat` of a kit. cmd runs them, but `kit_derive.py:127-128` reads them with
  universal newlines and writes them back with a blank line after every line.
- Fix: `\n` in the string, or `newline=''`.
- Confidence: confirmed (checked: `write_text('a\r\nb\r\n')` gives `b'a\r\r\nb\r\r\n'`)
- Status: fixed (c4aafcfd0): newline=''

### RL-22 P3 `build_watson.py`: a `.done` marker cut short reads as a finished legacy stage
- Where: `tools/levelport/build_watson.py:1028-1031` (written in place), `:187-191` (unparseable JSON ->
  `{'legacy': text}`), `:251` (`'legacy' not in m` skips the key test).
- Scenario: the driver is killed while writing a marker: the next run takes the stage as done by an old run and
  never compares its key again.
- Fix: write through a temporary file; treat non-JSON that starts with `{` as no marker.
- Confidence: confirmed (traced); the window is small
- Status: fixed (c4aafcfd0): the marker is written to a temporary name and renamed; unparseable text starting with { is no marker

### RL-23 P3 `gltf2map.q3map2_tiles` waits for memory with no way out
- Where: `tools/levelport/gltf2map.py:1191-1196`.
- Scenario: under 5 GB available and nothing of this run's still running: the loop sleeps forever (memgate's
  "starts alone" rule is not applied here); it also holds the start lock for 30 s after the last tile.
- Fix: use `memgate.Gate`, as `cp77_collision` and `cp77_tiles` do.
- Confidence: confirmed (traced)
- Status: fixed (c4aafcfd0): q3map2_tiles starts through memgate.Gate (a tile with nothing else running starts alone)

### RL-24 P3 `cp77_area.sh` reads exit files a previous run may have left, and takes the map name from `$4`
- Where: `tools/levelport/cp77_area.sh` (`step_bg` writes `$log.exit`; the loop `cat`s it; `MAP=${4:-cp_$AREA}`).
- Scenario: a rerun where a background step's subshell dies before writing its `.exit`: the loop reads the last
  run's `0`. `cp77_area.sh watson 3 mymap` (no `--map` word) names the map `cp_watson`.
- Fix: `rm -f "$W/area-$TAG".{scene,build}.log.exit` before starting them; parse `--map`.
- Confidence: confirmed (read)
- Status: fixed (c4aafcfd0): the exit files are removed before the background steps start (a missing one reads "missing"); --map is parsed, anything else is a usage error

### RL-25 P3 `cp77_collision.py`: drive check falls back to `C:/` and `D:/`; a second `PERF` struct is unused
- Where: `tools/levelport/cp77_collision.py:126`, `:141-145` (`memory()` at `:148` delegates to memgate).
- Scenario: `free_ok` before `Run` sets `WRITES`, on a machine with no D: drive: `shutil.disk_usage('D:/')` raises.
- Fix: fall back to the repository's and the level port's drives; delete `PERF`.
- Confidence: confirmed (read)
- Status: fixed (c4aafcfd0): the fallback is the checkout's and the level port's drives; PERF and its ctypes import removed

### RL-26 P3 `vcache.c` trusts `verts`; `__declspec(dllexport)` only
- Where: `tools/levelport/vcache.c:27-39`.
- Scenario: a caller passes `nverts` smaller than an index: `count[idx[k]]++` writes past the allocation.
- Fix: return -1 when an index is >= `verts` (one pass); an export macro for other compilers.
- Confidence: confirmed (read); the callers' `nverts` were not traced
- Status: fixed (c4aafcfd0): -1 on an index not below verts; VCACHE_EXPORT for other compilers

### RL-27 P3 q3map2 patches: small items
- Where: `q3map2/large_maps.patch` (`MemBuffer( _ftelli64( f ) )`: -1 on failure becomes a huge size, as the
  original `ftell` did); `q3map2/exact_planes.patch` (`EmitXplanes`: `fflush` per brush, the file never closed;
  `extern` declarations inside `main`'s option loop in `bsp.cpp`, also in `brush_cook.patch`, `brush_bin.patch`);
  `q3map2/plane_hash.patch` (`int planehash[ PLANE_HASHES ]` stays declared after both of its users are replaced).
- Scenario: an unreadable .map allocates by a negative length; a 240 k-brush `-xplanes` run flushes 240 k times;
  an unused-variable warning.
- Fix: check the length; flush at the end; declare the options in `q3map2.h`; drop the array.
- Confidence: likely (no upstream tree to check other uses of `planehash`)
- Status: deferred: the patch edits need the netradiant-custom tree to rebuild and test

### RL-28 P3 `light_time.py`: nearest-fuse tie compares timetables
- Where: `tools/levelport/light_time.py:129-130` (`min` over `(distance, name, tt)` tuples).
- Scenario: two fuse nodes at the same distance with the same name, one with a timetable and one with `None`:
  `TypeError`.
- Fix: `min(near, key=lambda t: t[0])`.
- Confidence: likely
- Status: fixed (c4aafcfd0): min keyed on (distance, name), never the timetables

### RL-29 P3 Dead code
- `tools/levelport/build_watson.py:956` `chunk_crcs`; `colzbsp.py:62` `is_xbsz`; `mapio.py:99` `hull_planes`;
  `bsp2gltf.py:32` `Q3_TO_PARM`; `patch_kit.py:108` `chunk_line`; `tag_verdict.py:80` `M64`; `zfight.py:23`
  `KIND` and `:54` `inside` (main re-implements it at `:161`); `cp77_collision.py:141` `PERF`;
  `retain.py:135` (`if keep.exists()` after the exit above it).
- Unused imports: `blender_preview.py:8` math; `cp77_collision.py:59` ThreadPoolExecutor; `cp77_foliage.py:21`
  numpy; `look.py:10` io; `patch_chunks.py:39` subprocess; `w3build.py:20`, `w3decals.py:78`, `w3foliage.py:19`
  math; `w3fx.py:19` re; `w3scene.py:34` hashlib, `:51` `scene_box_section`, `scene_light_section`,
  `scene_shape_section`; `zfight.py:18` sys.
- Confidence: confirmed (each name occurs once under `tools/` and `docs/`)
- Status: fixed in these files (b9235ee05): colzbsp.is_xbsz is used now, bsp2gltf Q3_TO_PARM, patch_kit chunk_line, tag_verdict M64, the unused imports of blender_preview, patch_chunks, w3build, w3decals, w3foliage, w3fx, w3scene; the rest is the levelport-cp lane's
- Status: fixed (c4aafcfd0) for the levelport-cp files: build_watson chunk_crcs, mapio hull_planes, cp77_collision PERF and ThreadPoolExecutor, retain's second keep-list test, look.py io, cp77_foliage numpy; the rest are the levelport-w3col lane's

### RL-30 P3 Smaller items
- `tools/levelport/w3city.py:95,99`, `w3merge.py:77`, `w3build.py:396`: `json.dump(data, open(tmp, 'w'))` then
  `os.replace`: the file is closed only by reference counting.
- `engine_trace_ded.py:131`: `--set NAME` without `=` shifts the command line; `:147` re-reads the whole console
  log four times a second.
- `look.py:43-46`: the temporary .bsp stays behind when `q3bsp.Bsp` raises.
- `lighttransfer.py:296`: without `--out` the target BSP is overwritten in place, not through a temporary file.
- `memgate.py:78-93`: ledger files of dead owners are ignored but never removed.
- `cp77_collision.py:457-462` hashes the tools' raw bytes while `build_watson.py:949-953` normalises CRLF for the
  same purpose (a worktree with other line ends misses the chunk cache).
- `retain.py:113`: a literal tab character inside the source string.
- `wk_read_lenient.ps1`: the "dto ctor(...)" and "serialize:" lines are debugging output left on.
- Confidence: confirmed (read)
- Status: fixed for w3city, w3merge, w3build (b9235ee05); the rest is the levelport-cp lane's
- Status: fixed (c4aafcfd0): engine_trace_ded --set without = exits, look.py removes its temporary BSP on a raise, lighttransfer writes through a temporary file, memgate removes dead owners' ledger files, retain's tab is \t, wk_read_lenient.ps1's debug lines removed. deferred: cp77_collision's raw-byte hash (normalising CRLF changes the chunk cache identity: every cached chunk misses once) and engine_trace_ded's console reread (timing, needs a run); the w3 items are the other lane's

## Publish cleanliness

- `tools/levelport/cp2gltf.py:45-47`: `<newmod tree>\rend2_comparison\build\tools\wolvenkit\...` and
  `...\dotnet10` as defaults: no default, name `WOLVENKIT_CLI` / `WOLVENKIT_DOTNET` in the error.
- `tools/levelport/wk_read_lenient.ps1:7`: the same folder as `$Wk`'s default: make the parameter mandatory.
- `tools/levelport/cp2gltf.py:44,49,50`: Steam, Blender 5.1 and PowerShell 7 install paths as defaults: acceptable
  as documented fallbacks, better from the paths module.
- `tools/levelport/colpack.py:31`: `C:\msys64\mingw64\bin\libzstd.dll`: look beside the q3map2 build, then
  `XENJK_LIBZSTD`, else an error naming the variable.
- `tools/levelport/engine_trace_ded.py:31-32,69`: `<levelport root>\levelport\runhomes`,
  `.codex-research\runhomes`: derive from the repository (RL-20).
- `tools/levelport/w3bundle.py:16-17`: `<Steam library>\...\The Witcher 3`, `<w3 extraction>\...\Doboz.dll`.
- `tools/levelport/w3scene.py:68`, `w3trees.py:105`, `w3fx.py:33`, `w3flags.py:348`: `<w3 extraction>`
  constants; `--w3` defaults of the same at `w3autohide.py:55`, `w3decals.py:676`, `w3env.py:81`,
  `w3flags.py:423`, `w3foliage.py:110`, `w3gltf.py:160`, `w3grade.py:58`, `w3terrain.py:43`; in usage text at
  `w3autohide.py:6`, `w3flags.py:22`, `w3gltf.py:9`, `w3trees.py:9`.
- `tools/levelport/w3decals.py:18`: a dated run note with `<levelport root>/...`: remove.
- `tools/levelport/nier2gltf.py:45`: `<Steam library>\...\NieRAutomata\data`.
- `tools/levelport/retain.py:39-41`: docstring names `<levelport root>`: say "the level port's own volume".
- `tools/levelport/levelcheck.py:77,80`: `%LOCALAPPDATA%\xenjk_build_x64_20261030` and `..._levelcheck` as
  default build folders; `:391` the game lock in `.codex-research/anime-batch-20261030` (another lane's folder).
- `tools/levelport/gltf2map.py:55-57,61` and `q3map2/README.md`: `../Xenmap/.tools/netradiant-custom-20260114-xenjk*`
  beside the repository; the README also cites `.codex-research/levelport/look/watson-m-collision-20261030/` as its
  evidence and a `build_q3map2.sh` "in the research folder": neither is in the repository.
- `tools/levelport/q3map2/README.md:3`: "Three patches" above a table of seven; the build line applies three.
- `tools/levelport/README.md:216,218`: `<quake folder>` in the example commands: `<quake dir>`.
- Reviewer and model names in comments: `cp2gltf.py:1245,1250,1394`, `cp77_tiles.py:153,474` ("Astra"),
  `w3bundle.py:3` ("Fable 5.2", and a path into `.codex-research/waterfalls-20261117/`),
  `tools/levelport/README.md:199` ("the two Fable reviews").
- "the owner" with dates and session notes in comments and docstrings: about 125 lines over 32 files (most in
  `cp2gltf.py` 34, `cp77mat.py` 16, `levelcheck.py` 9, `gltf2map.py` 7, `w3scene.py` 6): the facts are useful, the
  attributions read as a private log; a pass to keep the reason and drop the "owner <date>" wording.
- Every tool's default work and output roots are `<repo>/.codex-research/levelport/...` (`cp2gltf.py:106`,
  `gltf2map.py:1287,1324`, `look.py:83`, `seed_work.py:21`, `retain.py:33`, `vcache.py:20`, `w3scene.py:995`,
  `cp77_collision.py:91,97`, `build_watson.py:74`, `cp77_ads.py:33-34`, `levelcheck.py:78`): a research-folder name
  as the public default; one `LEVELPORT_HOME` with that as the fallback.
- No credentials, URLs other than the netradiant-custom upstream, or TODO/FIXME markers were found. No file looks
  like an accidental commit (`git ls-files tools/levelport`: 95 files, all sources or the area JSON).
- Licence headers: none of the files carries one. `vcache.c` implements Forsyth's published algorithm (credited
  in its header); the q3map2 patches modify GPL code and are distributed as diffs, which is fine, but the README
  should say they are under q3map2's licence. The W3 tools import the Witcher3-Blender-Tools add-on at run time
  (`PKG = 'bl_ext.user_default.witcher3_tools'`); whether code was copied from it into `w3emitter.py`,
  `w3bundle.py` or `w3trees.py` was not checked (those files were not read in full).
- Status (levelport-cp files): fixed (c4aafcfd0): the machine paths (RL-1), README.md's <quake folder> and "Fable", q3map2/README.md's count, sibling-folder builds and research citations, the research defaults now <research> = $XENJK_RESEARCH. Left for the comment sweep: reviewer names and owner dates in comments. Licence headers: the xenjk GPL-2.0 header added to every levelport-cp source file. Owner question: whether q3map2/README.md should state that the patches are under q3map2's licence (GPL)

## Liftability

- **Locations.** Machine paths and the research-tree layout are spread over 25 files (above). Shape: one
  `lp_paths.py` (repository root, level-port home, GameData, q3map2 builds, WolvenKit, Blender, W3 research tree,
  libzstd), every tool importing from it; nothing else reads `os.environ` for a path.
- **The scene format lives in the CP2077 front end.** `w3scene.py:51-52` imports `SCENE_KIND`, `SCENE_VERT`,
  `SceneImages`, `snorm8`, `vertex_frame`, `xscn_blob`, `xscn_head_of`, `xscn_rebuild` from `cp2gltf.py`;
  `make_xlod.py:28`, `scene_pick.py:22`, `light_time.py:33` and `w3build.py:31` (`BOX_FACES`, `box_ring_boxes`,
  `clearance`) import it too, and `w3fx.py` imports `cp77fx` for `xfx_blob`. Importing `cp2gltf` resolves
  WolvenKit and CP2077 paths. Shape: `xscn.py` (records, sections, blob, images) and `xfx.py` hold the formats;
  `geomutil.py` holds `hull_tris` (`cp2gltf.py:2062`, `w3build.py:131`), `clearance`, `headroom`
  (`nier2gltf.py`), the box helpers; both front ends import those.
- **pk3 member helpers, three copies.** `member`, `model0_bounds`, `raw_copy`: `cp77_collision.py:375,388,559`
  and `patch_kit.py:46,59,84` (already diverged: RL-5); `sha256`: `cp77_collision.py:113`, `kit_derive.py:36`;
  the "stage folder to zz_<map>.pk3" writer with the same skip list: `gltf2map.py:1545-1556`, `stage.py:21-34`,
  `relight.py:28-34`; `PACK_MAX` in `cp77_collision.py:98`, `patch_kit.py:43`, `colzbsp.py:117`. Shape: `pk3util.py`.
- **Fetch scaffolding duplicated between decals and fx.** `_init_worker`, `fetch_roots`, `needs`, `fetch_loop`,
  `fetch_main` in `cp77decals.py:625,687,697,732,1054` and `cp77fx.py:675,692,709,736,753`; `depot_of_archive` in
  `cp77decals.py:94` and `wk_fetch.py:43`. Shape: they belong in `wk_fetch.py`.
- **Small helpers.** `cv` (`cp77_ads.py:42`, `cp77_foliage.py:28`, `cp77_rotate.py:33`; `cp2gltf` has `cname`,
  `depot`); `long` (`retain.py:76`, `seed_work.py:25`); `lumps` (`lighttransfer.py:39`, `patch_chunks.py:66`);
  `brush_bounds` (`colclutter.py:291`, `patch_chunks.py:70`); `entity_text` (`colcook.py:208`,
  `cp77_collision.py:284`); `mesh_key` (`w3gltf.py:57`, `w3trees.py:109`); `box_map` (`cp77decals.py:756`,
  `w3decals.py:173`); `_num` (`mapio.py:21`, `cp77mat.py:45`); `lin` (`cp77fx.py:100`, `cp77mat.py:295`); `log`
  (`cp77_collision.py:105`, `cp77_tiles.py:43`); the part reader in `scene_pick.py:46-78` and `zfight.py:33-51`;
  the glb reader `cp77_collision.read_glb` (`:157`) beside `gltfio.Document`; `colbsp.to_bytes` and `colbsp.write`
  (`:172-215`) lay the file out twice. The bodies were compared only for the pk3 helpers, `cv`, `sha256` and
  `fetch_identity`; the others are same-named and should be diffed before merging.
- **Deliberate twins, keep.** `slabwriter.py` re-implements `gltf2map.slab_planes`, `q3bsp.brush_polygons` and
  `mapio.brush` bit for bit for speed, and `colpack.STRAIGHT_SIN` mirrors `gltf2map.STRAIGHT_SIN` behind an
  assert (`gltf2map.py:859`). They need a test that pins the equality, not a merge.
- **Stage keys.** Two closure mechanisms (`stagekey.code_key`, by syntax tree; `cp77_collision.module_closure` +
  file hashes) and a third rule in `build_watson` (`METHODS`). Shape: `stagekey` owns all three and the extra key
  files of RL-13.
- **Memory gate.** `memgate.Gate` is used by `build_watson`, `cp77_collision` and `cp77_tiles`;
  `gltf2map.q3map2_tiles` has its own loop (RL-23) and `cp77_collision.memory()` is a pass-through.
- Status (levelport-cp): done (1c1aac344): the scene format into xscn.py (a pure move, cp2gltf re-exports the names); locations: paths.py (c4aafcfd0). not done: pk3util.py and the stage-key merge cross into the levelport-w3col lane's files (patch_kit, colzbsp, colpack) and move keys; geomutil.py touches w3build and nier2gltf (other lane); xfx.py waits for w3fx's import to move with it

## Still open from earlier reviews

None: `docs/review/FINDINGS.md`, `PLAN.md`, `2026-11-09-character-rendering.md` and `2026-11-09-bot2.md` hold no
finding on `tools/levelport/`.
