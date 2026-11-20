# Tools (review 2026-11-19, master a322d9dc4)

Scope: the 365 tracked files under `tools/` outside `tools/levelport/` and `tools/ui/`: 256 code and text files
(51,941 lines) and 109 fixture files (2.9 MB). This review did not reach every file at the same depth; the three levels
are listed so nothing below is read as broader than it is.

- **Read in full**: `research_shed.py`, `staged_binary.py`, `dedupe_staged_pk3.py`, `cvar_list.py`,
  `renderer_spirv.py`, `scene_spirv.py`, `cloth_spirv.py`, `xenui_spirv.py`, `renderer_bench.py`,
  `renderer_scenes.py`, `renderer_latency.py`, `renderer_parity.py`, `renderer_gpu_passes.py`, `char_vram_check.py`,
  `char_vram_report.py`, `saber_lab.py`, `laps_work.py`, `laps_edge_charges.py`, `tick_rules_sim.py`,
  `showcase_map/look.py`, `hair/simcheck_run.py`, `bike/wheel_check.py`, `bike/ct3x_seat_patch.py`,
  `cloth/CMakeLists.txt`, `cloth/pipeline/README.md` (first half), `cloth/pipeline/asset/{common,build_cloth_asset,
  skaio,skdio,glaio,skd_to_ska,run_client_npc,make_cfg_npc}.py`, all eleven `cloth/pipeline/model/*.py` except
  `build_glm.py`, `decimate_eve.py`, `export_eve_mesh.py`, `recon_eve.py`; and the diff since the 2026-10-09 review
  merge (`fc1187ba9..HEAD`) of every movement, bot and laps tool that changed (`defense_laps`, `laps_identity`,
  `map_pairs`, `movement_graph_merge`, `play_vs_runner`, `position_camper`, `human_rush_study`,
  `mine_placement_study`, `upstream_watch`, two test files).
- **Partly read** (header, entry point and write path; the rest by pattern search for deletes, moves, network use,
  local paths, dead branches): `characters/pack_bc1.py`, `pack_dds_convert.py`, `pack_layer_rgba8.py`,
  `cook_tangents.py`, `reflection_map/build_reflection_map.py`, `showcase_map/build_showcase_map.py`,
  `upstream_watch.py`, `bike/tape_harness.py`, the two `.ps1` files.
- **Not read beyond pattern searches** (no correctness claim is made for these): `pbr_heights.py`,
  `bsp_light_survey.py`, `bsp_to_obj.py`, `charshadow_reference.py`, `xenjk_icon.py`,
  `characters/{pack_texture_dedupe,pack_layer_dds,fma_mip_fix,roster_check,manifests,pack_order}.py`,
  `characters/xen_character.menu`, `hair/{build_chains,chainsio,hcd_to_hcr,hc_eval,hc_replay_check}.py`,
  `bike/{bike_twin,ct3x_model,ct3x_audio,ct3x_preview,ct3x_materials,ct3x_blender,ct3x_blender_dump,ct3x_params,
  tweakdb,wheel_gate}.py`, `cloth/pipeline/skeleton/*`, `cloth/pipeline/asset/{analyze,make_reel,render,compare,
  diag_band,sheet,summarize,worst}.py`, `cloth/compare_c_js.mjs`, `latency/latprobe.cpp`, and the bodies of the C
  harnesses. These need a second pass.
- **C harnesses**: `hair/hc_eval.c`, `hc_frames.c`, `hc_replay.c`, `cloth/skirt_replay.c`,
  `characters/roster_driver.c`, `bike/bike_test_main.c`, `bike/bike_pmove_test.c`, `movement/pmove_style_test.c`
  were checked with `gcc -fsyntax-only` against the current engine headers (all eight pass, the first four with
  `-Wall -Wextra` clean); their logic was not reviewed.
- **Third party**: `tools/WinSymbol` (Willi Schinmeyer, MIT, upstream OpenJK since 2015, built from the top
  `CMakeLists.txt:451`) and `tools/cloth/blender/jediacademy` (mrwonko/Cagelight Blender add-on, GPL headers):
  provenance only.
- **Fixtures**: searched for paths, user names and player names, not reviewed as values.
- **Tests**: the suite was run from the repository root (`python -m unittest discover -s tools -p "test_*.py"`):
  491 tests pass in 13 s. The six script-style test files outside the suite were run one by one (RT-2).

Files with no findings (of those read in full): `staged_binary.py`, `laps_work.py`, `laps_edge_charges.py`,
`tick_rules_sim.py`, `renderer_parity.py`, `renderer_gpu_passes.py`, `renderer_latency.py`, `xenui_spirv.py`,
`showcase_map/look.py`, `cloth/CMakeLists.txt`, `cloth/pipeline/asset/{skaio,skdio,glaio,skd_to_ska}.py`,
`upstream_watch.py` (talks to git remotes only with `--fetch`), `tools/WinSymbol`.

`tools/publish_scrub.py` is not tracked (`.gitignore:269`), which is right: its pattern list holds the private
strings. It was read for the publish section.

## Findings

### RT-1 P1 Thirteen lines in ten tracked tools files carry the strings the scrub rewrites, so the public tip cannot match the local tip
- Where: `tools/bike/ct3x_audio.py:48` (`%USERPROFILE%\AppData\...`), `:50`; `tools/bike/ct3x_materials.py:29`;
  `tools/bike/ct3x_model.py:48`; `tools/bike/ct3x_params.py:20`; `tools/bike/ct3x_preview.py:21`;
  `tools/bike/tweakdb.py:35`; `tools/bike/wheel_check.py:13`, `:16` (`%LOCALAPPDATA%\...`), `:18`;
  `tools/char_vram_check.py:27`; `tools/char_vram_report.py:17`; `tools/hair/simcheck_run.py:36` (all
  `the repository root...`). Related: `tools/publish_scrub.py:43-45` (replacements), `:152-155` (the gate).
- Scenario: `publish_scrub.py build` replaces these with `<repo root>` and `<home>`. The published files then hold
  `ROOT = Path(r'<repo root>')` and `sys.path.insert(0, r'<repo root>\.codex-research\...')`, which run nowhere,
  and the tip tree differs from local `master`, so `build` prints "tip tree DIFFERS" and returns failure
  (`return check(out) and ff and local_tree == public_tree`). Commit e673c8f04 (2026-10-12) removed exactly this
  class from the older tools; these arrived after it.
- Fix: derive each from `__file__` (`Path(__file__).resolve().parents[n]`) or an environment variable with a
  repo-relative default, as `human_rush_study.py:33` and `cook_tangents.py:41-47` do; then the scrub has nothing
  to rewrite at the tip.
- Confidence: confirmed for the strings and the gate's logic (read); the failing `build` itself was not run.
- Status: fixed (cb2d899): repository root from __file__, outside folders from documented environment variables with no default; renderer_spirv.py's <glslang> docstring and drive-root Godot/glslang defaults are left to the shading lane, and cvar_list.py's ../JKSunny-EternalJK default is kept (only RT-8 there; it now exits when that checkout is missing)

### RT-2 P2 Five of the six source-extraction test scripts fail on current master, and none of the six is in the suite
- Where: `tools/test_movement_reference.py:249`, `tools/test_movement_reroute.py` (imports the same `extract`),
  `tools/test_movement_parity_guard.py:152`, `tools/test_movement_landing_progress.py:186`,
  `tools/test_movement_trigger_shadow.py:660`; `tools/test_movement_short_landing.py` passes.
- Scenario: these files define `main()` and no `unittest.TestCase`, so discovery collects nothing from them and
  "491 tests pass" does not cover them. Run directly today: `reference` and `reroute` stop at
  `source.index('static void StraightJudge(')` (the function is `static int StraightJudge(` now,
  `codemp/game/bot2_move.c:6125`); `landing_progress` fails to compile (`'bot2_moveExecutor_t' has no member named
  'prepress'`: its stub struct is behind the source); `parity_guard` and `trigger_shadow` stop on markers written
  with `\n` against sources read as bytes from a CRLF working tree (`core.autocrlf true`). The records that cite
  them (`docs/movement/records/2026-09-17-independent-reference.md:57`, `2026-09-21-parity-review-fix.md:14`,
  `2026-09-21-trigger-shadow.md:104`) describe guards that no longer run.
- Fix: either retire them with their records marked historical, or repair each (normalise `\r\n` on read, update
  the signature and stubs) and wrap `main()` in a `TestCase` that skips without a C compiler, as
  `test_movement_graph_reset.py:150` does.
- Confidence: confirmed (each was run; the output folders are under `.codex-research/*-test-20261120T0101*`).
- Status: fixed (cb2d899) for parity_guard and trigger_shadow (CRLF read, stubs for the helpers the extracted code now calls; they run in the suite); deferred for reference, reroute and landing_progress: their stubs predate StraightJudge's resumable judge and the executor's prepress/touch/stepOff, a movement-lane repair; they are suite cases that skip with that reason

### RT-3 P2 `dedupe_staged_pk3.py` writes a trash manifest that `research_shed.py --restore` puts back in the wrong place
- Where: `tools/dedupe_staged_pk3.py:66-73` (rows relative to the repository root, with a header row);
  `tools/research_shed.py:155-169` (`target = research / rel`).
- Scenario: an unmatched pack is moved to `.codex-trash/<stamp>-staged-pk3/.codex-research/levelport/look/...`
  with the row `staged-pk3\t<bytes>\t.codex-research/levelport/look/...`. `research_shed.py --restore <stamp>`
  (the one documented way back, and the docstring here says "for the owner to empty or restore") resolves the row
  against `.codex-research`, so the pack lands in `.codex-research/.codex-research/levelport/look/...`; the header
  row is reported as "not in trash".
- Fix: write rows relative to `.codex-research` with no header (the shed's format), or import the shed's `Shed.move`.
- Confidence: confirmed (traced both files).
- Status: fixed (cb2d899): moves go through research_shed's Shed.move (restorable rows, no header); a unit test restores one

### RT-4 P2 `dedupe_staged_pk3.py --apply` that stops part-way leaves moved packs with no manifest and a stray `.link`
- Where: `tools/dedupe_staged_pk3.py:60-62` (`os.link(match, tmp)`, `os.replace(tmp, p)`), `:69-73` (manifest
  written once, after the loop).
- Scenario: a look client still holds one staged pack open. `os.replace` on it raises `PermissionError`; the run
  ends with packs already in the trash and no `MANIFEST.tsv`, and `<name>.pk3.link` left beside the locked pack.
  The next run then dies at `os.link` with `FileExistsError` on that `.link`. `research_shed.py:103-107` writes
  the row before each move for this reason (review T3). Also `.codex-research/levelport` is a link to `C:` here
  while `.codex-trash` is on `D:`: with the defaults every unmatched move is a cross-volume `os.replace` and fails
  the same way.
- Fix: append the manifest row before each move, catch `OSError` per file and continue, remove a leftover `.link`
  before linking, and refuse when trash and look root are on different volumes (as `research_shed.py:180`).
- Confidence: confirmed by reading; not run.
- Status: fixed (cb2d899): row before each move (Shed), per-file OSError caught, leftover .link removed, cross-volume trash refused

### RT-5 P2 `simcheck_run.py` deletes an earlier run's folder when a tag is reused
- Where: `tools/hair/simcheck_run.py:49-51` (`if rd.exists(): shutil.rmtree(rd)`).
- Scenario: `simcheck_run.py a1 --dll ...` twice: the first run's `qconsole.log`, screenshots, `simcheck.json` and
  any hair dump are permanently deleted. AGENTS.md: agents never delete research data; logs and results stay put.
  `char_vram_check.py:236` refuses instead ("already ran: new --tag").
- Fix: exit with that same message when `rd` exists.
- Confidence: confirmed.
- Status: fixed (cb2d899): a reused tag exits ('already ran: new tag')

### RT-6 P2 `ct3x_seat_patch.py` destroys the pack when input and output are the same path
- Where: `tools/bike/ct3x_seat_patch.py:57` (`zipfile.ZipFile(a.src) as zi, zipfile.ZipFile(a.dst, 'w') as zo`);
  the docstring's first line says the tag is moved "in a built bike pack, in place".
- Scenario: `ct3x_seat_patch.py bike.pk3 bike.pk3`: the central directory is read, then opening the same file for
  writing truncates it, and every `zi.read` reads the new file's bytes. The only copy of the pack is gone or corrupt.
- Fix: refuse `os.path.samefile(src, dst)`, or write to `dst + '.tmp'` and `os.replace` after the source closes (the
  `pack_*.py` tools' pattern).
- Confidence: confirmed by reading; not run.
- Status: fixed (cb2d899): IN and OUT the same file is refused

### RT-7 P2 Seven tools raise `IndexError` at import in a checkout less than two folders deep
- Where: `tools/renderer_spirv.py:38-39` (`ROOT.parents[1]`), imported by `scene_spirv.py:17`,
  `cloth_spirv.py:20`, `xenui_spirv.py:16`; `tools/human_rush_study.py:34`, `tools/mine_placement_study.py:44`,
  `tools/position_camper.py:32` (`Path(__file__).resolve().parents[3]`).
- Scenario: the repository cloned to `C:\xenjk` (or `/xenjk`): `ROOT.parents` has one entry and
  `Path(__file__).parents` three, so the default is an `IndexError` before `GLSLANG_EXE` / `JSON_ROOT` is even
  looked at (`os.environ.get(name, default)` evaluates the default first). No shader header can be regenerated
  there, including `cloth_spirv.py --check`, which needs no compiler.
- Fix: `ROOT.parent.parent` / `Path(__file__).resolve().parent.parent.parent.parent` never raise (they stop at the
  drive root); better, read the environment variable first and only then build the default.
- Confidence: confirmed (`PureWindowsPath(r'C:\xenjk').parents[1]` raises).
- Status: fixed (cb2d899) for human_rush_study, mine_placement_study and position_camper (JSON_ROOT / DM26_DIR read first, no drive-root default); deferred for renderer_spirv.py: shading lane

### RT-8 P2 `cvar_list.py` writes an empty upstream section when the JKSunny checkout is missing
- Where: `tools/cvar_list.py:16` (`../JKSunny-EternalJK`), `:26-28` (`git()` ignores the return code), `:138-142`.
- Scenario: anyone regenerating `docs/xenjk-cvars.md` without that sibling checkout: both `scan(JKSUNNY, ...)`
  return `{}`, so groups 1 and 2 print "0 cvars" with empty tables (group 3 does not depend on that checkout and is
  unchanged). The document loses two sections and the tool exits 0.
- Fix: `raise SystemExit` when `git ls-tree` fails for a ref the groups depend on; or commit the two name lists as
  a fixture so the tool needs only this repository.
- Confidence: confirmed by reading.
- Status: fixed (cb2d899): exits when the JKSunny checkout lacks either ref

### RT-9 P2 `renderer_spirv.py build` without `--tree` rebuilds a research clone, not the repository's renderer
- Where: `tools/renderer_spirv.py:36` (`DEFAULT_TREE = .codex-research/renderer-bench/src/jksunny-perf/codemp/rd-vulkan`),
  `:320`; the three sibling tools default to `ROOT / 'codemp' / 'rd-vulkan'` (`scene_spirv.py:55`).
- Scenario: a GLSL edit in `codemp/rd-vulkan/shaders/glsl`, then `python tools/renderer_spirv.py build` as the
  tool's own docstring and `docs/review/2026-11-09-character-rendering.md:81` give it
  (`docs/renderer-vulkan-in-xenjk.md:413` adds `--tree codemp/rd-vulkan` and notes the default): the tool reports
  success on the clone, `codemp/rd-vulkan/shaders/spirv/shader_data.c` keeps the old bytes, and the
  engine builds with stale shaders. In a public clone the default tree does not exist at all.
- Fix: default to `ROOT / 'codemp' / 'rd-vulkan'`.
- Confidence: confirmed by reading.
- Status: deferred: renderer_spirv.py belongs to the shading lane

### RT-10 P2 Four tracked tools import modules that are not in the repository
- Where: `tools/char_vram_check.py:29-30` (`gw_common` from `.codex-research/wishlist-20261101/gpu-window`);
  `tools/bike/wheel_check.py:13-14` (`cc` from `.codex-research/client-checks-20261014-a`);
  `tools/bike/ct3x_preview.py:20` and `tools/bike/ct3x_model.py:39` (`<newmod tree>\rend2_comparison\tools`);
  `tools/bike/wheel_check.py:6` names a `game_lock.py` under `.codex-research/anime-batch-20261030/scripts`.
- Scenario: a public clone, or this machine after `.codex-research` is shed: the tools fail at import. The client
  harness (`gw_common`, `cc`, `game_lock`) is what every scripted client check depends on and it is untracked.
- Fix: move the harness modules into `tools/` (they are code, not research data), or move these four tools out of
  the tracked tree beside the harness they need.
- Confidence: confirmed.
- Status: deferred: owner decision whether the client-check harness (gw_common, cc, game_lock) and the CP2077 tools become tracked code; the four tools now take their folders from CP_TOOLS / CLIENT_CHECKS / GPU_WINDOW (cb2d899)

### RT-11 P2 `test_strafe_map_preparation.py` needs the installed game and does not skip without it
- Where: `tools/test_strafe_map_preparation.py:51-59` (`game_directory/Jedi Academy/GameData/base/ctf_kejim.pk3`,
  `ctf_dantooine_v3.pk3`, `.../maps/ctf_dantooine_v3.navmesh`); `game_directory` is ignored (`.gitignore:220`).
- Scenario: a clean clone runs the suite: `test_repository_kejim_and_dantooine_reparse` errors with
  `FileNotFoundError`. `test_movement_cases_from_bsp.py:14` shows the intended form (`skipUnless`).
- Fix: `@unittest.skipUnless` on the two packs and the navmesh.
- Confidence: confirmed by reading (the test passes here because the game is installed).
- Status: fixed (cb2d899): the case skips when the packs or navmeshes are missing

### RT-12 P3 Only the cloth arrays can be checked for staleness, and nothing runs that check
- Where: `tools/cloth_spirv.py:28-36`, `:62-73` (`--check`); `tools/scene_spirv.py`, `tools/xenui_spirv.py`,
  `tools/renderer_spirv.py` write no source hash. No build file, test or hook calls `cloth_spirv.py --check`.
- Scenario: a scene shader is edited and committed without `scene_spirv.py`: the 38 arrays in
  `scene_shader_data.c` stay old and nothing reports it (`renderer_spirv.py verify` needs the compiler). Today's
  state is consistent (cloth `--check` passes; `glsl/scene` and `scene_shader_data.c` last changed in the same
  commit d405a544a).
- Fix: move the stamp helpers into `renderer_spirv.py`, stamp all four outputs, and add one unit test that runs
  the four checks.
- Confidence: confirmed.
- Status: deferred: the stamp helpers would move into renderer_spirv.py (shading lane)

### RT-13 P3 In `scene_spirv.py` an empty defines tuple changes the SPIR-V target
- Where: `tools/scene_spirv.py:64-65`; entries at `:21`, `:25`, `:41-50`.
- Scenario: an entry with no fourth element compiles for the default Vulkan 1.0; an entry ending in `()` compiles
  with `--target-env vulkan1.2`. Whether `scene_cull.comp` (no tuple) and `scene_cluster.comp` (`()`) are meant to
  differ cannot be told from the list; adding or dropping `()` silently moves a shader between SPIR-V versions.
- Fix: make the target an explicit field of every entry.
- Confidence: confirmed (behaviour); intent unknown.
- Status: fixed (cb2d899): every entry names its defines and target; scene_shader_data.c regenerates byte-identical

### RT-14 P3 `scene_spirv.py` and `cloth_spirv.py` do not check their compiler exists
- Where: `tools/scene_spirv.py:66`, `tools/cloth_spirv.py:90`; compare `tools/xenui_spirv.py:27`.
- Scenario: no glslang (or no Godot for cloth): a `FileNotFoundError` traceback instead of "set GLSLANG_EXE".
  `cloth_spirv.py` also has only the Godot path although glslang is the default everywhere else.
- Fix: the `xenui_spirv.py` check; give cloth the glslang path.
- Confidence: confirmed.
- Status: fixed (cb2d899): scene_spirv and cloth_spirv check their compiler; a glslang path for cloth_spirv is deferred: it changes how the arrays are built and needs a verify against the committed bytes

### RT-15 P3 `research_shed.py` counts items it failed to move, and restore reports rows it skipped
- Where: `tools/research_shed.py:97-99` (tally before the rename at `:107`), `:169` (`restored {len(rows)} entries`).
- Scenario: a locked folder: its bytes are in the "MOVED" total although `tryMove` printed "skip"; a restore that
  left items in the trash still prints the full row count.
- Fix: add to the tally after the rename; count actual restores.
- Confidence: confirmed.
- Status: fixed (cb2d899): counted after the rename; restore reports what it restored

### RT-16 P3 `dedupe_staged_pk3.py`: a pack that is the original itself goes to the trash; identity is sampled
- Where: `tools/dedupe_staged_pk3.py:51-52` (`if os.path.samefile(o, p): break` leaves `match` None, so the
  `else` at `:63` moves it), `:18-26` (ten 1 MiB samples).
- Scenario: `--look` overlapping `--builds`: every build output with one link is moved to the trash. Separately, a
  stored pack rebuilt within 2 s to the same size differing outside the samples would be replaced by a link to the
  other build.
- Fix: `continue` on `samefile`; compare whole files (or a full hash) before `os.replace`.
- Confidence: confirmed for the first; the second is an edge case.
- Status: fixed (cb2d899): an original is skipped, and whole files are compared before a link replaces a copy

### RT-17 P3 `renderer_bench.py run --demo <path>` puts the path into the run folder's name
- Where: `tools/renderer_bench.py:358-367` (`root = OUT / 'runs' / ('%s-%s' % (stamp, args.demo))`).
- Scenario: `--demo D:\x\run.dm_26` (allowed: `:359` accepts absolute paths): the folder name contains `:` and `\`
  and `stage` fails at `mkdir`; a relative path with `/` creates nested folders.
- Fix: use `demo_copy.stem` in the name.
- Confidence: confirmed by reading.
- Status: fixed (cb2d899): the run folder is named by the demo copy's stem

### RT-18 P3 `saber_lab.py` leaves its staging on `C:` and copies the rcon password into the kept folder
- Where: `tools/saber_lab.py:263` (`%LOCALAPPDATA%/xenjk-play/saberlab-<stamp>`, never removed or reported),
  `:74-88` and `:123-125` (`lab.cfg` with `rconpassword` copied to the output folder).
- Scenario: every run adds a folder with staged binaries' masters and logs under `xenjk-play` (the 2026-10-05
  cause of a full `C:`); review T15 removed the password from the cost manifest for the same reason.
- Fix: use `world_retention`'s staging removal after the copy, and write `lab.cfg` to the output without the
  password line.
- Confidence: confirmed.
- Status: fixed (cb2d899): each cell's staging is dropped once copied (world_retention.drop_staging), lab.cfg kept without the rcon password

### RT-19 P3 Dead and leftover code in the cloth pipeline
- Where: `tools/cloth/pipeline/model/jka.py:74-76` (a loop whose body is `... if False else None`);
  `tools/cloth/pipeline/asset/analyze.py:38` (same form); `tools/cloth/pipeline/asset/build_cloth_asset.py:206`
  and `:216` (`rep['render_verts']` set twice), `:220-221` (`hb = 180` hard-coded header size reported as
  measured); `tools/cloth/pipeline/model/dump_stock.py:11` (`'\%s\model.glm'`, an invalid escape: a
  `SyntaxWarning` on Python 3.12); `tools/cloth/pipeline/model/preview_glm.py:14` (`for a in ax: pass`).
- Fix: delete; use a raw string.
- Confidence: confirmed.
- Status: fixed (cb2d899)

### RT-20 P3 `renderer_scenes.py` docstring is cut mid-sentence and contradicts itself
- Where: `tools/renderer_scenes.py:12-15` ("Borderless window ... rather than exclusive fullscreen ... fs_cdpath;
  / either, and newmod's files are never copied or loaded. Fullscreen on the main monitor").
- Fix: rewrite the paragraph.
- Confidence: confirmed.
- Status: fixed (cb2d899)

### RT-21 P3 `cook_tangents.py` finds the compiler at a fixed path
- Where: `tools/characters/cook_tangents.py:31-32` (`C:\Program Files\Microsoft Visual Studio\2022\<edition>`).
- Scenario: Visual Studio installed elsewhere or a later version: "no Visual Studio 2022 vcvars32.bat".
  `tools/latency/build_latprobe.cmd:5-8` uses `vswhere`.
- Fix: `vswhere -latest -property installationPath`.
- Confidence: confirmed.
- Status: fixed (cb2d899): vswhere

## Publish cleanliness

What `publish_scrub.py` does with each private string in the tracked tools files. "Rewritten" means a replacement
pattern changes the text (and so the tip tree, see RT-1); "refused" means `check` fails the publish; "passes" means
neither pattern list sees it.

Rewritten (`the repository root` to `<repo root>`, `%USERPROFILE%` to `<home>`); fix each as in RT-1:
- `tools/bike/ct3x_audio.py:48`, `:50`; `tools/bike/ct3x_materials.py:29`; `tools/bike/ct3x_model.py:48`;
  `tools/bike/ct3x_params.py:20`; `tools/bike/ct3x_preview.py:21`; `tools/bike/tweakdb.py:35`;
  `tools/bike/wheel_check.py:13`, `:16`, `:18`; `tools/char_vram_check.py:27`; `tools/char_vram_report.py:17`;
  `tools/hair/simcheck_run.py:36`.

Refused only: none found (no other private path form in the lane).

Passes the scrub unchanged (local paths the pattern list does not know):
- `<newmod tree>\...` (a private sibling tree): `tools/bike/ct3x_audio.py:45`; `tools/bike/ct3x_model.py:20`,
  `:39`, `:50`; `tools/bike/ct3x_preview.py:20`; `tools/pbr_heights.py:4`, `:36`, `:600`;
  `tools/fixtures/renderer_bench/scenes.json:3`, `:4` (`content_source`, read by `renderer_scenes.py:47`). Fix:
  environment variable with no default, or a repo-relative path.
- `<UnrealJKA>/JKA/Content/GlobalRoutes/...`: `tools/fixtures/strafe_flag/README.md:7`. Fix: name the file only.
- `<glslang>`: `tools/renderer_spirv.py:7`, `tools/xenui_spirv.py:6` (docstrings; the code is relative). Fix:
  "a glslang folder beside the workspace".
- Steam and vendor install paths (generic, not private): `tools/bike/ct3x_audio.py:44`, `tools/bike/tweakdb.py:33`
  (Cyberpunk 2077), `tools/renderer_bench.py:36-37` (PresentMon), `tools/characters/cook_tangents.py:31`.
- Sibling project names used as path defaults (not paths, but they name private work): `Xenmap`
  (`tools/showcase_map/build_showcase_map.py:28`, `tools/showcase_map/README.md:12`); `SkirtClothLab` and
  `ClothResearch` (`tools/cloth/pipeline/asset/common.py:9`, `analyze.py:3`, `:25`, `skdio.py:1`,
  `model/make_textures.py:6`, `skeleton/make_pbr_pack_np.py:8`, `tools/cloth/compare_c_js.mjs:2`,
  `tools/cloth/skirt_replay.c:2-3`, `tools/cloth/pipeline/README.md:22`); `JKA_UE5_Design`
  (`tools/human_rush_study.py:36`, `tools/position_pilot.py:37`); `JSON/jkastats/data/discord/sheets`
  (`tools/human_rush_study.py:5`, `:126`, `tools/mine_placement_study.py:44`, `tools/position_camper.py:32`);
  `JKSunny-EternalJK` (`tools/cvar_list.py:16`, `tools/fixtures/renderer_bench/candidates.json:128`); `tools-ext`
  (`tools/renderer_bench.py:35`). Owner's call whether these names may be public.
- "the owner's capture.py" (`tools/renderer_scenes.py:9`, `tools/fixtures/renderer_bench/scenes.json:961`): a
  private script named as the source of a recipe.

Not found: player names or handles, e-mail addresses, URLs to
private hosts, credentials. Fixture `source` fields in `tools/fixtures/strafe_flag/kejim_m1_20260905/*.json` are
repo-relative (`.codex-research\...`, `tools\fixtures\...`).

Other cleanliness:
- `tools/cloth/blender/jediacademy/`: a vendored third-party add-on (24 Python files, 5,794 lines) with its own
  `CLAUDE.md`, `notes.txt`, `todo.txt`, `.pep8`, `pyrightconfig.json`, `Makefile`, a `.tex` manual and
  `io_mesh_md3 readme.txt`; one script uses it (`tools/cloth/pipeline/model/verify_import.py:5-7`). No upstream URL
  or commit is recorded beside it (the pipeline README names the version only). Fix: replace with a pointer to the
  upstream release, or add `UPSTREAM.md` with URL, commit and licence and drop the upstream's working files.
- `tools/cloth/pipeline/**`: one-model scripts that run at import (no `main` guard), join Windows paths with `\`,
  and are bound to three dated `.codex-research` folders; `model/package.py:8`, `:78` and
  `skeleton/package_np.py:10`, `:82` `rmtree` and `os.remove` their own earlier outputs. The README says all of
  this; they read as a record of how one asset was made, not as tools. Fix: move under `docs/cloth/` as an
  appendix or keep and say so at the top of each file.
- `tools/renderer_scenes.py:257-258`: `CGAME_BUILD` points at a dated build folder under `.codex-research`
  (`benchcam-cgame-20261013`); the comment explains a worktree that is gone. Stale once that folder is shed.
- `tools/play_vs_runner.py:47`: `LIBRARY` points at `.codex-research/reexplore-chain-20261014-b/library/...`
  (a live dependency on research data; it should be in `.codex-research/KEEP.txt`, not checked here).
- `tools/fixtures/renderer_bench/candidates.json`: 1,390 lines of dated candidates pinned to build folders and
  worktrees that mostly no longer exist (`"uncommitted dev build of the perf/vulkan worktree"`, lines 396-416 and
  on). Not private, but a public reader cannot run them.

## Liftability

- **Client check harness**: scripted client launches are written six times (`renderer_bench.client_popen` +
  `hide_client_windows`, `renderer_scenes.launch_scene`, `renderer_latency.run_one`, `showcase_map/look.py`,
  `cloth/pipeline/*/run_client*.py`, `hair/simcheck_run.py`) and twice more in untracked modules (`gw_common`,
  `cc`, RT-10). Each sets its own mute, network and window flags, so the owner's rules (music muted, net off, window
  hidden, game lock) hold only where someone remembered them. Proposed shape: one tracked `tools/client_run.py`
  with the launch, the fixed safety settings, the log wait and the stop; the callers pass a cfg and extra sets.
- **SPIR-V embedding**: four tools share a compiler lookup and `c_array` through `renderer_spirv.py`, whose module
  level also holds the JKSunny task list and the Godot fallback. Proposed shape: `tools/spirv_common.py` (compiler
  lookup with the environment variable first, `c_array`, the source stamp of RT-12), and the four tools as thin
  lists of shaders. Another OpenJK derivative that takes the scene or xenui renderer then needs two files.
- **Trash moves**: `research_shed.py` and `dedupe_staged_pk3.py` each implement "move to `.codex-trash/<stamp>`
  with a manifest", differently (RT-3, RT-4). Proposed shape: `research_shed.Shed.move` as the one function,
  imported by the dedupe tool.
- **Source-extraction tests**: eleven test files cut functions out of `bot2_move.c` / `bot2_graph.c` by text
  markers and compile them with stubs; five have drifted (RT-2). Proposed shape: one helper
  (`extract(source, name)` with CRLF normalisation and a clear "marker not found: the source moved" failure) used
  by all, each wrapped as a `TestCase`.

## Still open from earlier reviews

None of area 10's T1-T15 is open in the code read here. `FINDINGS.md` O19 (`bot_telemetry` is `CVAR_ARCHIVE`) is
outside this lane and was not rechecked.

## Fix status (review/20261119-tools)

- Publish cleanliness: every listed local path is gone from the lane except `tools/renderer_spirv.py` (shading lane)
  and `cvar_list.py`'s `../JKSunny-EternalJK` default; the owner's capture.py wording is gone; `PRESENTMON`,
  `SCENE_CONTENT_SOURCE` and `Q3MAP2` replace `tools-ext`, `<newmod tree>` and `Xenmap`. Left for the owner: whether
  the sibling-project names (`JKSunny-EternalJK` and the other trees in `candidates.json`) may be public, the vendored
  `tools/cloth/blender/jediacademy` (no UPSTREAM record added), the one-model cloth pipeline as tools or as a record,
  `renderer_scenes.py`'s dated `CGAME_BUILD`, `play_vs_runner.py`'s `LIBRARY`, and `candidates.json`'s dated builds.
- Liftability: trash moves done in phase A (`dedupe_staged_pk3.py` uses `research_shed.Shed`). Not done: the client
  check harness (the two harness modules are untracked, RT-10, and six launchers would be rewritten); SPIR-V common
  (needs `renderer_spirv.py`, shading lane); one source-extraction helper (each of the eleven test files cuts different
  markers and stubs; RT-2's repairs added CRLF handling where it was missing).
