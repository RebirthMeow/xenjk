# Tools, second pass (review 2026-11-19, master a322d9dc4)

Scope: the tools files the first pass (`tools.md`, RT-1..RT-21) listed as not read or partly read, each read in full
here: `tools/pbr_heights.py` (1,049 lines), `bsp_light_survey.py` (400), `bsp_to_obj.py` (121),
`charshadow_reference.py` (175), `xenjk_icon.py` (97); all of `tools/bike/` (16 files, 3,920 lines); `tools/hair/`
(11 files, 1,749); `tools/characters/` (12 files, 2,089); `tools/cloth/pipeline/skeleton/` (15 files, 1,246); the rest
of `tools/cloth/pipeline/asset/` (`analyze`, `make_reel`, `render`, `compare`, `diag_band`, `sheet`, `summarize`,
`worst`: 654); `tools/cloth/compare_c_js.mjs` (150); `tools/cloth/skirt_replay.c` (1,141); `tools/latency/`
(`latprobe.cpp` 303, `build_latprobe.cmd` 11); and the other C under tools, `tools/movement/pmove_style_test.c` (434)
and `tools/levelport/vcache.c` (133). About 14,400 lines.

Read against the engine where a tool mirrors it: `bike_twin.py` line by line against `codemp/game/bg_bike.c:266-837`
(same equations, same order, same constants); `hc_eval.py` FEEL_GAME / FLOW_GAME against the cvar defaults at
`codemp/rd-common/tr_hair.cpp:1103-1116` (equal); `hc_frames.c`'s asset offsets against `chainsio.py`'s header and
record formats (equal); `bsp_light_survey.py` / `bsp_to_obj.py` lump indices and record sizes against the RBSP
structures (equal); `sim_frame.h` for the two frame-stepping harnesses.

Not done (read only, no build): the link of `bike_pmove_test.c` and `pmove_style_test.c`'s stub lists against the
current `bg_*.c`, and anything inside the modules the bike tools import from outside the repository (`cp_car`,
`cp_vehicle`, `cp_multilayer`, `fastglb`, `glm_io`, `cc`; RT-10).

Files with no findings: `hair/hc_eval.c`, `hc_eval.py`, `hcd_to_hcr.py`, `hc_replay_check.py`, `chainsio.py`,
`build_chains.py`, `presets.json`; `bike/bike_twin.py`, `bike_test_main.c`, `bike_ct3x.py`, `ct3x_params.py`,
`ct3x_blender_dump.py`, `tweakdb.py` (beyond RT-1); `characters/manifests.py`, `pack_order.py`, `roster_driver.c`,
`xen_character.menu`; `latency/latprobe.cpp`, `build_latprobe.cmd`; `charshadow_reference.py`;
`cloth/pipeline/skeleton/jkagla.py`, `retarget_lib.py`, `build_np.py`, `contact_report.py`, `grip_check.py`,
`make_cfg_np.py`, `proportions.py`, `run_client_np.py`, `sheet_pairs.py`; `cloth/pipeline/asset/compare.py`,
`diag_band.py`, `sheet.py`; `cloth/compare_c_js.mjs`; `levelport/vcache.c` (logic).

## Findings

### RT-22 P2 The two BSP tools resolve "later pack wins" with a lower-cased sort, which is not the engine's order
- Where: `tools/bsp_light_survey.py:368-370` (`sorted(base.glob("*.pk3"), key=lambda p: p.name.lower())`, then
  `shader_index(list(reversed(pk3s)))`), `:372-379`; `tools/bsp_to_obj.py:31`. The engine's order:
  `tools/characters/pack_order.py:1-12` (review TX-9: upper-cased compare, `dl_` last).
- Scenario: a map or a sky shader present in both `zz_x.pk3` and `zzz_x.pk3` (the install is full of `zz_` / `zzz_`
  packs). The game reads `zz_x` (`_` sorts after `Z`); both tools read `zzz_x` (`_` sorts before `z`). The survey
  then reports another BSP's light grid and another shader's sun for that map name, and `bsp_to_obj.py` exports
  another version's geometry for the reference render, with nothing in the output to show it.
- Fix: sort with `pack_sort_key` (move `pack_order.py` to `tools/` so tools outside `characters/` can import it).
- Confidence: confirmed (read both; the sort difference is the one TX-9 fixed in the character tools).
- Status: fixed (34cdcc9): pack_sort_key (f7f259c moved pack_order.py to tools/)

### RT-23 P2 The frame harnesses' "no step limit" reference drops time on a gap longer than 64 steps
- Where: `tools/hair/hc_frames.c:451` and `tools/cloth/skirt_replay.c:921` (`SIM_PLAN_MAX_STEPS`,
  `SIM_STEPS_LEGACY`); `codemp/rd-common/sim_frame.h:29` (64), `:58`, `:80-83` (legacy: time over the cap is dropped).
- Scenario: `skirt_replay --frames --hitch 3000:1500` at 60 Hz, or `hc_frames --hitch 3000:2500` at the hair's
  1/30 s (both tools name "a character the main view culled for that long" as the case): the reference run can take
  64 steps (1.07 s of cloth, 2.13 s of hair) and drops the rest. Every "against nolimit" number (drawn-state error,
  settle time) is then measured against a run that lost time itself. Only the reference line's own "dropped N ms"
  shows it.
- Fix: run the reference with a loop of plans until nothing is owed (or raise the cap for it), and fail when
  `stRef.dropped > 0`.
- Confidence: confirmed by reading; not run.
- Status: fixed (34cdcc9): both harnesses fail when the nolimit reference dropped time (a reference that loops until nothing is owed was not added)

### RT-24 P2 `ct3x_audio.py pack` destroys the pack when IN and OUT are the same file
- Where: `tools/bike/ct3x_audio.py:298` (`zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, 'w') as zo`), `:320`.
- Scenario: `ct3x_audio.py pack bike.pk3 bike.pk3`: the same failure as RT-6 (`ct3x_seat_patch.py`): opening for
  writing truncates the file the reads then come from.
- Fix: refuse `os.path.samefile`, or write `dst + '.tmp'` and `os.replace` (the `characters/pack_*.py` pattern).
- Confidence: confirmed by reading; not run.
- Status: fixed (cb2d899): IN and OUT the same file is refused

### RT-25 P2 `pack_texture_dedupe.py` groups and proves on one file per name, not on the file the renderer loads
- Where: `tools/characters/pack_texture_dedupe.py:185-186` (`tex.setdefault(base(n), i)`: the first entry of a
  base name, any extension), `:199-200`, `:203-212` (groups), `:345-346` (the proof's CRC), `:355` (a dropped base
  drops every extension).
- Scenario: a pack that went through `pack_dds_convert.py` (Install 78: every `X.png` has `X.dds` beside it, added
  at the end of the zip) is deduplicated again. `tex` holds the PNG entries only. `a.png` and `b.png` are
  byte-identical, so `b` redirects to `a`; but the renderer loads `<name>.dds` first, and `a.dds` / `b.dds` need not
  be equal (`pack_dds_convert.py:79-89` picks BC1 or BC7 by the roles each name is read in). The drawn image changes
  and the proof, which compares the PNG CRCs, reports no failure. The other way round (DDS first in the zip) the
  PNG fallbacks are never compared.
- Fix: key identity on the entry the renderer resolves (DDS first, then the image), and require every extension of
  a base name to match before a redirect.
- Confidence: confirmed by reading; the installed order so far was dedupe (Install 72) before convert (Install 78),
  so no installed pack is known to be affected.
- Status: fixed (34cdcc9): grouped by the entry the renderer loads (DDS first), redirected only when every extension matches; a synthetic pack with equal PNGs and different DDS keeps both

### RT-26 P3 Five pack tools write no output pack when nothing converts
- Where: `tools/characters/pack_bc1.py:186`, `pack_dds_convert.py:96`, `pack_layer_dds.py:183`,
  `pack_layer_rgba8.py:63`, `fma_mip_fix.py:123` (`if new and not dry:`); each docstring says every other entry is
  copied.
- Scenario: a batch `tool in.pk3 staged/in.pk3` over many packs: for a pack with nothing to do the staged file is
  missing, or is the one an earlier run left there. Only the report's `"out": null` says so; `pack_texture_dedupe.py`
  always writes.
- Fix: always write the copy (or exit with a distinct status and print "unchanged, not written").
- Confidence: confirmed.
- Status: fixed (34cdcc9): OUT is always written unless --dry

### RT-27 P3 `pack_bc1.py` re-encodes maps that are already 8 bytes a block
- Where: `tools/characters/pack_bc1.py:38-39` (BC4 is in the decode tables), `:157-171` (only "already BC1" is
  skipped).
- Scenario: an `ATI1` / BC4 `materialMask` (the owner's rule names BC4 for masks) that BC1 holds within 40 dB is
  rewritten as DXT1: the same size, 5 or 6 bits a channel instead of 8, and counted in "converted" with 0 saved.
- Fix: skip when the source's bytes per block are not above BC1's.
- Confidence: likely (depends on a BC4 map passing the 40 dB test).
- Status: fixed (34cdcc9): BC4 is skipped

### RT-28 P3 `pack_layer_dds.py`: empty arrays are counted and misreported, and a present DDS leaves a partial set
- Where: `tools/characters/pack_layer_dds.py:110-111`, `:134-135`, `:154`.
- Scenario: a stage whose layers all have `-` for the normal appends an empty array, reported as "a layer image is
  not in this pack" and counted in `arrays` and `skipped`. An array where one layer already has a DDS (another
  format or size) gets new BC7 files for the other layers only; the engine needs all layers alike
  (`R_LoadArrayDDS`), so the array stays on the RGBA8 path and the new files are dead weight.
- Fix: do not record empty arrays; skip an array when any layer's DDS exists and does not match the plan.
- Confidence: confirmed by reading.
- Status: fixed (34cdcc9)

### RT-29 P3 `pack_texture_dedupe.py`'s documented command line does not run
- Where: `tools/characters/pack_texture_dedupe.py:3` (`<in.pk3> <out.pk3>`), `:152-159` (`inp nargs='+'`, `--out` /
  `--out-dir` required).
- Scenario: the docstring's command exits with "give --out for one pack or --out-dir".
- Fix: correct the docstring.
- Confidence: confirmed.
- Status: fixed (34cdcc9)

### RT-30 P3 `ct3x_materials.py` classifies by substring and keys shared materials by name only
- Where: `tools/bike/ct3x_materials.py:263` (`'ui' in t or 'ui' in cn`), `:219-223` (`light_<kind>`), `:266`
  (`metal_<first 12 characters>`), `:130-131` (`missing`).
- Scenario: any chunk whose name or template contains "ui" ("fluid", "guide", "build") and reaches that line becomes
  the dark screen material. Two light chunks of one kind with different emissive maps, or two metal chunks whose
  names share 12 characters with different colours, share the first one's maps. A multilayer that fails to bake
  becomes flat grey with no line in the report.
- Fix: match `ui` as a path component; key by the values (as `glass` does); list the fallbacks in `info`.
- Confidence: confirmed by reading; whether the CT-3X has such chunks was not checked.
- Status: deferred: needs the CT-3X's chunk data (not in this repository) to see which chunks would change, and a change alters the bike's look

### RT-31 P3 `ct3x_audio.py render` can pitch an engine step from fewer samples than it needs
- Where: `tools/bike/ct3x_audio.py:237-241` (`x[start:start + n_src]`), `:265` (windows checked for 1.6 s), `:270`.
- Scenario: a step needs `(1.0 + 0.12) s x f_target / f0` of source. Above a ratio of 1.43 that is more than the
  1.6 s the window was checked to be voiced and steady for, and past the end of the recording the slice is short:
  `signal.resample` stretches it to the full length, so the loop is lower than its step's pitch, silently.
- Fix: require `start + n_src <= len(x)` and a window as long as `n_src` when picking the candidate.
- Confidence: likely (depends on the sources; not run).
- Status: deferred: depends on the source recordings; needs a run

### RT-32 P3 `--hitch` with `--hitch-every` fires the periodic hitches at once
- Where: `tools/hair/hc_frames.c:419-423`, `:436-437`; `tools/cloth/skirt_replay.c:894-897`, `:906`.
- Scenario: `--hitch 5000:200 --hitch-every 1000:100`: the periodic entries are appended after the explicit one and
  the schedule takes hitches in list order, so after 5 s every periodic hitch fires on consecutive frames.
- Fix: sort the merged list by start time.
- Confidence: confirmed by reading.
- Status: fixed (34cdcc9): merged list sorted by start

### RT-33 P3 `wheel_gate.py` passes over shots it could not pair
- Where: `tools/bike/wheel_gate.py:56-60` (a `SHOT` line whose second has no screenshot file is dropped),
  `:104-105` (a parked label missing from the reference is skipped), `:71` (a wheel partly off screen gives a
  negative slice start).
- Scenario: the `echo SHOT` line and the screenshot's file name fall either side of a second: that label is not
  checked and the gate can still print PASS with fewer checks. Off screen, `patch` raises instead of failing the
  wheel.
- Fix: count unpaired and skipped labels as failures; clamp the patch box.
- Confidence: confirmed by reading.
- Status: fixed (34cdcc9): unpaired shots, parked labels missing from the reference and off-screen patches fail

### RT-34 P3 Bike harness: a crash path, a counter that cannot move, a stale build note
- Where: `tools/bike/tape_harness.py:345-347` (`stop / 1000.0` with `stop` None when the bike has not stopped in
  4 s: `TypeError`); `:143` and `:226-229` with `bike_twin.py:22` (`EV_STEP` is counted and printed; neither the
  twin nor `bg_bike.c` ever sets it, `codemp/game/bg_bike.h` `BIKE_EV_STEP`); `:121` (`peak['minz']` never
  updated); `:288` (`drive(...)` result unused: 5,000 substeps for nothing); `:251` (a dict copied for no reason);
  `tools/bike/bike_pmove_test.c:7` ("tape_harness.py --pmove builds and runs it": the option is `--pmove-exe` and
  nothing builds).
- Fix: guard `stop`; drop the step counter (and the event bit, in the engine lane); correct the comment.
- Confidence: confirmed.
- Status: fixed (34cdcc9): stop guarded, the step counter, minz, the unused drive and the dict copy removed, comment corrected; the engine's BIKE_EV_STEP bit is the engine lane's

### RT-35 P3 `bsp_to_obj.py --floor-only` without `--floor-z` raises
- Where: `tools/bsp_to_obj.py:60`, `:106` (`abs(m[2] - a.floor_z)` with `None`).
- Fix: default `--floor-z` to the centre's z, or `ap.error`.
- Confidence: confirmed.
- Status: fixed (34cdcc9): ap.error

### RT-36 P3 Leftovers in the C harnesses
- Where and what:
  - `tools/hair/hc_replay.c:28-47`: `slurp` duplicates `reel_slurp` (`hc_reel.h:20`); `geti` and `getf` are unused
    since the parser moved to `HairAsset_ParseEx` (review HR-12).
  - `tools/hair/hc_frames.c:38`: "(as hc_replay.c)" is stale: `hc_replay.c` no longer has this parser;
    `:146-148`: a comment inside `lerp_n` describing code that is not there.
  - `tools/cloth/skirt_replay.c:687`, `:699`, `:718`, `:762`, `:814`: `lastOrigin` is maintained and only cast to
    void; `:1018`, `:1099`: `magic` likewise; `:1058`: the usage text lists two of the four modes.
  - `tools/movement/pmove_style_test.c:191-194` (`dummy`), `:289` / `:380` / `:390` (`landedAt`), `:406-411`
    (the ramp's `peak` is computed and never printed), `:18` ("the summary says which style gained what": there is
    no summary).
  - `tools/levelport/vcache.c:31`: `__declspec( dllexport )` without a platform guard (`hc_eval.c:20-24` has one).
- Fix: delete; guard the export.
- Confidence: confirmed.
- Status: fixed (34cdcc9) except tools/levelport/vcache.c (levelport lane)

### RT-37 P3 Five scratch scripts are tracked
- Where: `tools/cloth/pipeline/skeleton/t_anchor.py`, `t_decode.py` (ends on a comment with nothing after it),
  `t_local.py`; `tools/cloth/pipeline/asset/summarize.py` (seven fixed `metrics_*.json` names in the working
  folder), `asset/worst.py` (sets `sys.argv`, then `exec`s the first half of `analyze.py`'s text, cut at the string
  `res = {}`).
- Scenario: no docstring, no arguments, bound to one day's working folder; `worst.py` breaks on any edit of
  `analyze.py` that moves that string.
- Fix: remove from the tracked tree (they are research scratch), or fold what is worth keeping into `analyze.py`.
- Confidence: confirmed.
- Status: deferred: owner decision; the pipeline README cites them as part of the asset record (t_local.py regenerates stock_F.npy, which t_anchor.py reads), so they go with the decision on the pipeline as a record (first pass, publish cleanliness)

### RT-38 P3 Leftovers in the Python tools
- Where and what:
  - `tools/bike/ct3x_model.py:196` (`split(xyz, rest, tris)`: two parameters unused), `:258` (an unknown joint
    name silently binds to bone 0; a vertex whose weights are all under 1e-4 gets an empty list and `cluster`
    indexes `w[0][0]`), `:292` (the clustering gives up at a 64 u cell and writes the LOD over its budget without a
    warning).
  - `tools/bike/ct3x_preview.py:76` (`skin(model, gla)`: `model` unused), `:155` (`render(stage, lod, ...)`: both
    unused).
  - `tools/bike/ct3x_blender.py:47` (a UV node replaced at `:78-79` and left unlinked), `:140` (a link replaced at
    `:146`), `:148-149` (sets a default value: no effect).
  - `tools/bike/ct3x_materials.py:28` (comment about "this drafts branch": master is the one line now).
  - `tools/pbr_heights.py:472` (`[:, None][:, 0]`), `:1013` (one lambda with a 40-space gap), `:233`, `:40`, `:287`
    (`DANTOONINE_V2`, misspelt, and named in the docstring as the external builders' entry).
  - `tools/characters/roster_check.py:34-39` (a `for ... else` over a one-element tuple), `:41-44` (blank lines);
    `tools/characters/cook_tangents.py:95` (the generated comment names an `ms` column that is not printed),
    `:149-150` ("the game would let them win": in the packs' own folder the packs are searched before loose files,
    `codemp/qcommon/files.cpp:3103-3104` and the loop below; `roster_check.py:60-75` has it the right way round).
  - `tools/xenjk_icon.py:20` and `:78` (`BEGIN` defined, the literal repeated).
  - `tools/cloth/pipeline/skeleton/make_gla_np.py:57` (`Pj` unused), `:217` (a second import of `pool_rigid`);
    `make_pbr_pack_np.py:13` (`png()` returns JPEG bytes).
  - `tools/cloth/pipeline/asset/analyze.py:194` (`tn0` unused, the normals recomputed at `:199`);
    `make_reel.py:21` (`TORSO = set()` then reassigned), `:80` (`land_hold` unused), `:126` (`name` unused);
    `render.py:2` (usage names `--shots` / `--sheet`; the parser takes a fifth positional and `--tag`), `:87`
    (`'none' if not draft else 'none'`).
- Fix: delete or correct.
- Confidence: confirmed.
- Status: fixed (34cdcc9) except DANTOONINE_V2 (the external builders import that name), make_gla_np's unused Pj parameter and make_reel's land_hold / name (one-model scripts, left as they are), and ct3x_blender's Factor default (not confirmed to be without effect)

### RT-39 P3 `cook_tangents.py` leaves its build folder and cooks with the 32-bit compiler only
- Where: `tools/characters/cook_tangents.py:196` (`tempfile.mkdtemp`, never removed: `cook.exe`, objects and the
  last GLM stay in `%TEMP%` per run); `:9`, `:31-32`, `:136` (vcvars32, `/arch:SSE2`).
- Scenario: the installed client is the 64-bit one since Install 94; the cooked files are keyed by GLM, so the game
  loads them whatever built them. Whether the 32-bit cooker's bytes equal what the 64-bit renderer computes is only
  shown by `--compare` against files a 64-bit game wrote, which the docstring does not ask for.
- Fix: remove the folder at exit; build the cooker for the architecture of the client it serves (or say that
  `--compare` against that client's cache is required).
- Confidence: confirmed (the folder); needs a run (byte equality across architectures).
- Status: fixed (cb2d899): the build folder is removed and the docstring asks for --compare against the 64-bit client's files; byte equality across architectures needs a run

### RT-40 P3 The reel loader checks the size after it has copied
- Where: `tools/hair/hc_reel.h:50-83` (every `memcpy` from `b + o`), `:86` (`o != n` checked last);
  `tools/hair/hc_replay.c:180`, `:214`, `:221` (`HC_Init` unchecked), `:251` (`fopen` unchecked);
  `tools/hair/hc_frames.c:448` (a CSV that cannot be opened is silently skipped).
- Scenario: a truncated or foreign `.hcr` (a version-1 reel read with the wrong counts): reads past the buffer
  before the mismatch message; an unwritable output path is a null `FILE` in `fwrite`.
- Fix: compute the expected size from the header first; check the returns.
- Confidence: confirmed by reading.
- Status: fixed (34cdcc9): the size from the header is checked before any copy; HC_Init, fopen and the CSV are checked

## Publish cleanliness

Still as the first pass has them (RT-1, RT-10 and its "passes the scrub" list); the lines below are additions.

- `tools/bike/ct3x_audio.py:1-2` ("Local play only ... nothing here is published"), `tools/bike/ct3x_model.py:365-366`
  (the shader header it writes: "private build: CP2077 art"), `tools/cloth/pipeline/skeleton/make_pbr_pack_np.py:2`
  ("Ripped commercial assets: local only, never commit, package or upload"), `package_np.py:1-2` ("never
  install/commit/upload"): statements about private use inside files that will be public. The tools carry no art;
  reword to "the outputs are not distributed", or leave the tools out of the public tree. Owner's call.
- `tools/cloth/pipeline/skeleton/make_pbr_pack_np.py:21-28`, `tools/bike/ct3x_audio.py:55-74`: source asset file
  names and Wwise source ids of commercial games. Names only. Owner's call.
- `tools/bike/bike_ct3x.py` (generated) and the header `ct3x_params.py:118` writes
  (`codemp/game/bg_bike_ct3x.h`): CP2077's TweakDB handling values for one vehicle, verbatim, in tracked files.
  Owner's call whether game data values ship.
- Owner quotes in docstrings: `tools/bike/ct3x_materials.py:1-2`, `ct3x_blender.py:1`, `ct3x_audio.py:1-2`,
  `wheel_gate.py:1`, `wheel_check.py:1`, `tools/characters/pack_bc1.py:2`, `pack_dds_convert.py:2`,
  `pack_layer_rgba8.py:1`, `tools/movement/pmove_style_test.c:3`, `tools/bike/bike_pmove_test.c:426`. Not private
  data, but chat text; replace with the decision.
- `tools/bike/ct3x_preview.py:20-21`: two absolute paths with no environment override (`:21` is the repository's
  own `tools/cloth/pipeline/skeleton`; use `Path(__file__)`). `tools/bike/ct3x_model.py:50` (`ASSETS1`, no
  override).
- `tools/bike/wheel_gate.py:14-20`, `tools/bike/bike_twin.py:8-9`: research run folders named as the calibration
  record; they do not exist in a public clone. Move the numbers to a doc record.
- No licence header on any C or C++ file under tools: `tools/hair/hc_eval.c`, `hc_frames.c`, `hc_replay.c`,
  `hc_reel.h`, `tools/cloth/skirt_replay.c`, `tools/characters/roster_driver.c`, `tools/bike/bike_test_main.c`,
  `bike_pmove_test.c`, `tools/movement/pmove_style_test.c`, `tools/latency/latprobe.cpp`, `tools/levelport/vcache.c`
  (credits Forsyth's 2006 paper for the algorithm; the code is ours). They compile engine sources into the same
  program: GPL-2.0 header as the tree.
- `tools/cloth/pipeline/**` one-model scripts with Windows-only path joins and import-time execution: as the first
  pass says; RT-37 names the five that are scratch.
- Not found in the files read here: e-mail addresses, credentials, private URLs, player names.

## Liftability

- **Ghoul2 file readers**: three GLA/GLM modules live inside the one-asset cloth pipeline
  (`cloth/pipeline/model/jka.py`, `skeleton/jkagla.py`, `asset/glaio.py`) and are imported from outside it by path
  insertion: `tools/hair/build_chains.py:248-249`, `tools/bike/ct3x_model.py:40`, `ct3x_preview.py:21`,
  `skeleton/make_gla_np.py:18`. Proposed shape: `tools/g2/` with one GLM and one GLA module (`jka.py` and
  `jkagla.py`; `glaio.py`'s sampling on top), imported by package name; the pipeline keeps only its asset scripts.
- **Pack rewriting**: the "copy every entry with its time, compression and attributes, replace or add some" loop is
  written six times (`pack_bc1.py:186-194`, `pack_dds_convert.py:96-108`, `pack_layer_dds.py:183-195`,
  `pack_layer_rgba8.py:63-75`, `fma_mip_fix.py:123-131`, `pack_texture_dedupe.py:351-361`), the 2x2 box filter three
  times (`pack_bc1.py:79`, `pack_layer_dds.py:37`, `fma_mip_fix.py:56`), the DDS header twice, the texture-name
  `base()` four times. Proposed shape: `tools/characters/packio.py` (`rewrite(inp, out, replace, add)`, which also
  settles RT-26 and the same-file case in one place, `box_down`, `dds_header`, `base`).
- **Pack order**: `pack_sort_key` is under `tools/characters/` while map tools need it (RT-22). Proposed shape:
  `tools/pack_order.py`, imported by the character tools, the BSP tools and any level tool that lists packs.
- **Frame-stepping harness**: `hc_frames.c:216-339`, `:341-355`, `:419-444`, `:455-506` and
  `skirt_replay.c:674-816`, `:818-829`, `:892-911`, `:926-960` are the same program twice (hitch list, frame
  schedule, the plan loop around `Sim_PlanSteps`, the drawn-state comparison), with the same two defects (RT-23,
  RT-32). Proposed shape: `tools/sim_frames.h` with the schedule builder, the reference rule and the report; each
  harness supplies its pose sampling and its step.
- **Hair asset parsing**: `hc_frames.c:39-112` is a second reader of HCH1 without the CRC, index or count checks;
  `hc_replay.c:59-74` already uses the game's (`HairAsset_ParseEx`, `HAIR_PARSE_OFFLINE`). Proposed shape: the same
  call in `hc_frames.c` (and `hair_asset.c` added to its build line).
- **Pmove test world**: `bike_pmove_test.c:24-181` and `pmove_style_test.c:32-168` share about 140 identical lines
  (the game-module stubs, `BoxClearance`, `PlaneTrace`, `PointContents`). Each new bg import must be stubbed in
  both. Proposed shape: `tools/pmove_stub.h`, included by both.

## Still open from earlier reviews

From this review's first pass, in the files read here at the same commit: RT-1 (the absolute paths in
`bike/ct3x_*.py`, `tweakdb.py`, `wheel_check.py`), RT-6 (`ct3x_seat_patch.py`), RT-10 (the modules outside the
repository) and RT-21 (`cook_tangents.py`'s fixed Visual Studio path) are as reported. The earlier reviews'
findings (`FINDINGS.md`, 2026-11-09) were not rechecked in this pass; none of the files read here contradicts a
fix those reviews record (HR-3, HR-7, HR-12, HR-13, HR-18, F1, F8, F12, CL-5, CL-6 are in place where cited).

## Fix status (review/20261119-tools)

- Publish cleanliness: the GPL-2.0 header is on the ten C/C++ files under tools outside `tools/levelport`
  (`vcache.c` is the levelport lane's); `ct3x_preview.py` and `ct3x_model.py` take `CP_TOOLS` / `JKA_ASSETS1`; owner
  quotes are reworded where the docstring is printed (`ct3x_audio.py`, `ct3x_seat_patch.py`, `wheel_gate.py`) and in
  `ct3x_materials.py`, `ct3x_blender.py`, `wheel_check.py`; the others stay for the comment sweep. Left for the owner:
  the private-use statements, the CP2077 asset names and Wwise ids, the TweakDB values in `bike_ct3x.py` /
  `bg_bike_ct3x.h`, and the research run folders named as calibration records.
- Liftability: done (f7f259c): `tools/characters/packio.py` (`rewrite`, `box_down`) used by the six pack tools, and
  `tools/pack_order.py` shared by the character and BSP tools. Not done: `tools/g2/` (the readers are also imported by
  `tools/levelport/gltf2map.py`, another lane); `sim_frames.h` (the two harnesses differ in types and pose sampling: a
  rewrite, not a move); `HairAsset_ParseEx` in `hc_frames.c` (stricter checks change what it accepts); `pmove_stub.h`
  (the two stub sets differ: the bike's counters, contact instrumentation, plane count).
