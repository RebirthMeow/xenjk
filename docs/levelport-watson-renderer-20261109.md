# Watson scene renderer: kit 11s playtest reports and `renderer/perf-2` (2026-11-09)

The work list for the CP2077 Watson scene renderer (`vk_scene.cpp` and `shaders/glsl/scene/`) and its converter
(`tools/levelport/`) after the owner's kit 11s playtest. It covers every report with its status, the builds, what is in
flight, the owner's decisions and the next actions. Character rendering has its own list:
[review/2026-11-09-character-rendering.md](review/2026-11-09-character-rendering.md).

## Picking this up

1. **Read first:** [AGENTS.md](../AGENTS.md), INTEGRATION.md,
   [levelport-scene.md](levelport-scene.md) (the scene format and renderer) and
   [levelport-watson.md](levelport-watson.md) (the city port; its "Resume here" is older than this document).
2. **Where the code is:** worktree `.codex-research/wt-vram-b1`, branch `renderer/perf-2` at 87e37a5f
   (`baseline/1` with `claude/cpm-freestyle` merged). **Everything is uncommitted** (section "Commit split"). Modified:
   `scene.frag`, `scene.vert`, `scene_cull.comp`, `scene_fx.vert`, `spirv/scene_shader_data.c`, `vk_scene.cpp`,
   `tools/levelport/cp2gltf.py`, `cp77mat.py`, `patch_materials.py`; new: `tools/levelport/cp77_meshlod.py`,
   `make_xlod.py`, `tag_probesky.py`. Do not edit these while a build or a collection runs.
3. **Where the evidence is:** `.codex-research/levelport/look/reports-20261109/` (a junction to
   `<levelport root>\levelport`). Its `HANDOFF.md` has the harness, run names and commands; `src/apply_*.py` are the
   patch scripts that produced the diff, applied in the order r1 (`look/perf-20261109/src`), r2, placeholders, r3, r4,
   r5, r6, r6b; `dll/<build>/` holds each DLL with `SHA256.txt` and `source_87e37a5f.diff`; `view/` holds the owner's
   report shots and every A/B sheet.
4. **Kits:** `.codex-research/levelport/build/cp_watson_baseline-11*`; each `KIT.txt` lists the chain. **11z is
   current** (11y + R6b + the day env pack). Not installed.
5. **Owner rules that apply here:** localhost only. Game clients go through
   `.codex-research/anime-batch-20261030/scripts/game_lock.py`, hidden, one at a time, and no timed runs while the
   owner plays. Look changes go to the owner as an A/B behind a cvar. Fix classes, not views: find the differing
   input, and read the report's own words and run the debug views (`r_sceneDebug` 5 ambient, 6 lights, 8 material,
   9 rain) before forming a hypothesis. Flicker is judged by per-frame counts. Hashes come only from `sha256sum`
   output. WolvenKit never runs beside CP2077 or another level-port fetch. Stop under 20 GB free on C: or D:. Never
   delete research data. Commit only on the owner's word.

## Reports

The owner's words and shots are in `view/2026110*_*.jpg` (the file names carry them).

| # | Report | Cause | Fix and switch | Status |
| --- | --- | --- | --- | --- |
| 1 | Smoke in a straight line | `scene_fx.vert` added the acceleration undamped while damping the velocity: the steam coasted 231 m | R3, `r_sceneFxAccelDrag` 1 (terminal velocity a/k); 125 emitters | owner picked it as the default; in kits since 11v |
| 2 | Pagoda window not blacked out as in CP2077 | CP2077's fake interior (`window_parallax_interior`, EmissiveEV 4); the day xenv's `r_sceneEmissiveExposure 0` held window rooms at night brightness. Not interior lighting | R6/R6b, `r_sceneWindowExposure` (-1 as before, 0.3 CP2077 exposureScale, 1 physical), an xenv look value; 411 materials, 205,218 instances | owner picked 1 by day; in 11z (day xenv only; dawn and night unchanged) |
| 3 | Ground z-fighting under the plaza tiles | paving `ml_street_a_02_masksset` coplanar with `Terrain_material` at CP2077 z 23.2 | R2, terrain flag 0x1000, renderer code 128, `r_sceneTerrainPush` | **not reproduced** on 11s or 11y (push 0/1, 2000 px and 4K, near and far, turning and walking). The owner said "turn camera i believe, and walk". Waits for the owner's demo |
| 4 | Solid fences and grates | the `metal_base_det` tiled detail carried the openings and was dropped | detail baked in as a cutout (`cp77mat.detail_cutout_image`, `patch_materials --detail-cutout`); 432 records | owner picked it; in kits since 11v |
| 5, 6 | DEBUG textures on rocks | materials on CP2077's placeholder `multilayer_default.mlsetup` | a sibling's setup (`cp77mat.placeholder_setups`); 55 records | in kits since 11v |
| 7 | Facade flicker | LOD switch at about 40 m; LOD0 has openings, LOD1 closes them; `SCENE_LOD_SCREEN` gives every mesh the same thresholds | R4, dithered crossfade `r_sceneLodFade` 0.25 (needs `r_sceneHideFade`); pops become ramps, no measurable cost | in kits since 11x. CP2077's own `lodLevelInfo` is extracted (`meshlod/`, `maps/cp_watson.xlod`); the renderer reader is not built |

## Builds

| Build | sha256 prefix | Contents | Kits |
| --- | --- | --- | --- |
| R3 | f9c23130 | R1 rain tiles (off) + R2 terrain push + steam fix + H2 (stb_dxt MIN/MAX) | 11v, 11w |
| R4 | f317c816 | R3 + LOD crossfade | 11x, 11y |
| R5 | 746d9545 | R4 + probe sky (off) | none |
| R6 | 656b09cc | R5 + window exposure (cvar only) | none |
| R6b | e4390f93 | R6 + window exposure as an xenv look value | 11z |

Build in the worktree: `GLSLANG_EXE=... python -I tools/scene_spirv.py`, then the `rd-vulkan_x86_64` target of
`build_vramb1` (exact commands in the evidence `HANDOFF.md`). Kits keep `r_sceneRainTiles 0` in `openjk.cfg`.

## Commit split (proposed, awaiting the owner)

1. Rain-view tiles (R1; buggy, off).
2. Terrain push (R2).
3. DEBUG placeholders and detail cutouts (converter only).
4. Steam drag (R3; diagnostic `r_sceneFxOnly`).
5. LOD crossfade (R4).
6. Meshlod tools (`cp77_meshlod.py`, `make_xlod.py`; no reader).
7. Probe sky (R5; parked, `r_sceneProbeSky` 0, `tag_probesky.py`).
8. Window exposure (R6/R6b).

## Parked

- **R1 rain tiles:** with `r_sceneCullGroups 1` every partial tile redraw is empty; with groups off the diagonal
  3-frame refill is still wrong. Gate with `r_sceneDebug 9`; beauty-pass image checks miss both.
- **Probe sky (R5):** reflection probes are G-buffer captures (12 slices of 128² RGBA8: albedo faces 0-5, normal faces
  6-11 with depth in alpha; sky is alpha >= 250; face 5 is up). `haveSkyData` is absent = true and unreliable. All 3,812
  Watson captures are measured (`probe_survey.json`, `probe_skyshare.json`, `envprobe/`, 3.8 GB, keep). Removing sky
  ambient where a probe sees none blacks covered streets: it needs a bounce term first.
- **xlod reader:** a calibrated scale for CP2077's LOD distances (raw, the median LOD0->1 switch is 10 m against our
  29 m).
- Hair alpha-to-coverage A/B at 2x MSAA.

## Next actions

1. Report 3: when the owner's demo (`.dm_26`) arrives, replay it frame for frame (`stream_load.py --play`) and count
   the floor's per-frame changes (`src/walk_ab.py`).
2. The commit split above, on the owner's word.
3. R1's two bugs, then the xlod reader, then the probe-sky bounce term.
