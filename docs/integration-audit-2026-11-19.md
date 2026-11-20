# Integration audit (2026-11-19)

The owner, 2026-11-19: "can we re-start that integration analysis then. any issues? i want to fully resolve all xenjk
variants/branches". This is the measured state of every branch and worktree in this repository on 2026-11-19, with trial
merges of everything still live, and the plan to reach one line. INTEGRATION.md (2026-10-26) predates
`baseline/1` and is out of date in sections 1, 3 and 4; [BASELINE.md](BASELINE.md)
(on `baseline/1`) last resumed on 2026-11-10. Nothing here was merged, built or installed. Method: `git merge-tree`
(no ref or checkout changed), `git cherry`, and the share of each branch's added code lines found in the target's file.

## 1. What is where

| Line | Tip | State |
| --- | --- | --- |
| `baseline/1` | 6e47de09 (2026-11-16) | **The code superset.** Contains `master` (413 ahead, 0 behind) and 42 other branches in full: tick independence (B), Bot2 horizons (C), CPM/freestyle `g_moveStyle`, the bike, the player screen, xenui, the Watson renderer and collision work (perf-2..4, stream, water-fft, terrain push, cutout coverage, settle faces, eye-relative), FMA game shading, the loading work, toon, hair springs/palette. Built x86 and x64; the Night City kits (11zd) run its 64-bit build. |
| `renderer/pbr-materials` | abc6693b (2026-11-19) | **The installed line and the main checkout.** Forked from `baseline/1` history at 2e7da623 (2026-11-06); its 90 commits since touch **only `tools/` and `docs/`** (59 files: levelport, characters, review records). Every W3, FF7R and review record of the last two weeks is here. |
| `renderer/w3-vertblend` | d405a544 (2026-11-18) | `baseline/1` + 20 renderer commits (Witcher 3 materials, terrain, water, billboards, sun cascades, r_sceneVrs). Beauclair's kit renderers come from it. **Merges into `baseline/1` with no conflict.** |
| `master` | a265c31c (2026-10-13) | Behind; a pure fast-forward to `baseline/1`. |
| `origin/master` | scrubbed twin | Publishing only, through `tools/publish_scrub.py`. Never merge. |

The installed game (`GameData`) runs **none of these tips**: `xenjk.x86.exe` from Install 59 (2026-11-01, the loading
engine), `rd-vulkan_x86.dll` from Install 68 (`renderer/install68-models` 6fe7bb2f: the player screen plus FMA shading,
2026-11-09), `base/jampgamex86.dll` and `cgamex86.dll` from Install 66 (83b7b748 plus the bike, 2026-11-05),
`base/uix86.dll` from Install 67 (player screen). So the installed game has no tick independence, no Bot2 horizons or
review fixes, no `g_moveStyle`, no character-review renderer fixes (Install 76's TX-15 layer DDS files sit unread
until the char-fixes-vram renderer is installed), and no Watson renderer work. Beauclair and Night City avoid this only
by loading kit binaries: the Beauclair bats a `w3-vertblend` renderer, the Night City kits a 64-bit build of
`baseline/1` from their own folders.

**32-bit and 64-bit are one source tree.** CMake builds both; nothing in git is split by architecture. The split the
owner hit (CPM in Night City, not in Beauclair) is a branch split: the 32-bit install comes from an older commit, the
64-bit kits from `baseline/1`.

## 2. Branches with work in neither main line

Trial-merged one by one into `baseline/1`, then chained in the order of section 4 (conflicts in the generated
`shader_data.c` / `scene_shader_data.c` are left out: they are regenerated from the GLSL).

| Branch | Unique commits | Content | Against `baseline/1` |
| --- | --- | --- | --- |
| `renderer/w3-vertblend` | 20 | Witcher 3 render-scene work, Installs 85-91 | clean |
| `renderer/pbr-materials` | 90 | tools and docs only | 1 block, 15 lines: `tools/levelport/gltf2map.py` |
| `kejim/holo-g2` | 8 | cgame: `misc_model_ghoul2` (clips, playlists, skin, stage lights), hologram-hand pit deaths (2026-11-11) | clean |
| `claude/bot2-review-next` | 35 (includes `bot2-exec-deep`, `bot2-replan-budget`) | Bot2 review batches 1-2, the owner's 2026-11-09/10 defaults (pool scopes, defender drop reach, reach edge, carrier home, avoid full, batch-2 fixes on), BP-7 autosave, fold cap | 8 blocks, 55 lines: `codemp/game/bot2_move.c` |
| `claude/char-fixes-vram` (includes `char-vram-budget`) | 14 | character review: VRAM budget and streaming, TX-15 layer DDS | 26 blocks, 759 lines; **714 of them in `vk_stream.cpp`** (the Watson scene streaming on `baseline/1` against the character streaming here), plus `vk_image.cpp`, `vk_vram.cpp`, `tr_local.h` |
| `claude/char-fixes-hc` (includes `hair-cloth-views`) | 9 | character review: hair and cloth fixes, `r_fmaDebug`, SH-5b off | 15 blocks, 191 lines: `fma.glsl`, `gen_frag.tmpl`, `tr_shader.cpp`, `vk_local.h`, `vk_shade_geometry.cpp` |
| `claude/sh21-material-set`, `hr10-hair-buffer`, `sk13-packed-vertex`, `sh23-lit-shader` | 6-7 each, each on `char-fixes-hc` | the review's SH-21, HR-10, SK-13, SH-23 | after hc: `vk_hair.cpp` (sk13), generated shaders only (sh21, sh23), clean (hr10) |
| `claude/pdecal-edge-normal` | 5 (2 already patch-equivalent) | Watson renderer, 2026-11-11: decal normals, particle masks, `r_sceneHideDither`, `r_sceneWaterBed`, distant water stand-ins | 24 blocks, 220 lines in the scene shaders, `vk_scene.cpp`, `cp2gltf.py`; 84% of its lines are already in `baseline/1` in a later form |

Settled by BASELINE.md (2026-11-06/10) and confirmed here, staying out: the `wip/*` experiments and
`collision/world128` (80-99% adopted by hand into `wip/stream`, the rest rewritten later), `wip/p3-viewcull1` (not
adopted), `wip/p3-fixes` (kept parts ported, the rest dropped on purpose), `collision/m2` (stopped, evidence),
`wishlist/drafts` (early bike draft, UI motion core, walkable-area tool, as they stood), `renderer/install68-models`
(its one commit is in `baseline/1` as 28ee75a2, 100% of its lines). `collision/kit-driver-2` is patch-equivalent to a
`baseline/1` commit. The two `worktree-agent-*` branches are pre-scrub history (366 of 420 commits patch-equivalent).

## 3. Worktrees

74, the main checkout included. 64 are clean. Of the 10 with uncommitted edits, none holds unique work:

- `wt-vram`, `wt-hotfix`, `wt-charlight`, `wt-install45`, `wt-col12`, `.claude/worktrees/hair-palette`: every added
  line is in `baseline/1` (the generated shader blob aside).
- `wt-p3-order`: 213 of 223 lines in `baseline/1`; the rest is the `p3` viewcull order that later builds rewrote.
- `wt-col13`, `wt-preload` (the same diff): the spawn preload, committed as 36db208d (in `baseline/1`, later reworked by
  the round-3 residency fixes).
- The main checkout: another session's uncommitted docs (`INTEGRATION.md`, `levelport-video-script.md`,
  `renderer-vulkan-in-xenjk.md`) and untracked build logs.

42 branches are fully contained in `baseline/1` (list: `git branch --merged baseline/1`). Their worktrees can go once
each tree's ignored output (build folders, run homes, evidence) is checked: a clean `git status` does not mean an
empty folder.

## 4. Plan to one line

1. **Freeze.** Each session commits or parks its work; nothing starts on an old branch while the merge runs.
2. **Integration branch `integration/1` from `baseline/1`**, merged in this order, each step building cleanly before
   the next: `renderer/w3-vertblend` (clean), `renderer/pbr-materials` (gltf2map), `kejim/holo-g2` (clean),
   `claude/bot2-review-next` (bot2_move.c), `claude/char-fixes-vram`, `claude/char-fixes-hc`, then sh21, hr10, sk13,
   sh23, and `claude/pdecal-edge-normal` if the owner keeps it. Shader blobs regenerated with the pinned glslang.
   **The character-streaming merge is the real work:** two streaming designs meet in `vk_stream.cpp`, and the result
   must keep both the Watson tile streaming and the character VRAM budget. That merge belongs to the renderer session
   that wrote the review fixes (`docs/review/2026-11-09-character-rendering.md`).
3. **Gates (the cheapest that show each risk):** x86 and x64 build warning-clean from one commit;
   `tools/tick_rules_sim.py`; the Python suite from the repo root; Bot2: the b2 control at `sv_fps 40` decision-
   identical to `bot2-review-next`'s own build (it is the new control); character review: `char_vram_check.py` and the
   review's stills against the char-fixes builds; Watson: the 11zd load checks; Beauclair: the city and castle load with
   the merged renderer and their Install 91 views match the kit renderer's stills. No full movement gates.
4. **Fast-forward `master` to `integration/1`.** `renderer/pbr-materials` and `baseline/1` are then merged lines;
   new work branches from `master`.
5. **One combined install** (owner's OK): exe, renderer, cgame, jampgame, ui, all from that commit. The Beauclair,
   Night City and Kabuki bats then load the installed binaries instead of kit ones, so `g_moveStyle`, the character
   fixes and the W3 renderer are in every launch.
6. **Retire** (archive-only): branch refs that are fully merged or settled as out become tags under `archive/`
   (nothing lost; `git tag -l 'archive/*'`); their worktrees move to `.codex-trash/` after the ignored-output check;
   `KEEP.txt` names any that hold live dependencies.
7. **Publish** only by the owner, through the scrub.

## 5. Decisions for the owner

**Taken 2026-11-19** (the owner, on each item below): 1 merge the character-review fixes now; 2 merge
`claude/pdecal-edge-normal`; 3 install both clients from one commit, the launchers on 64-bit, 32-bit kept as a fallback;
4 archive merged and settled branches as tags and move their worktrees to the trash after the output check.

1. Merge the character-review renderer fixes (the hc and vram stacks) in this pass. Their look decisions were taken
   on 2026-11-10; the streaming merge is the largest risk in the plan.
2. `claude/pdecal-edge-normal` (five 2026-11-11 Watson renderer commits, never merged): merge, or archive.
3. The installed client: 32-bit as today, 64-bit as the kits, or both from the same commit. Every Night City kit since
   tag 11 and every Beauclair timing ran the 64-bit client; Beauclair also runs on the 32-bit one.
4. Retiring: archive the merged and out branches as tags and move their worktrees to the trash (section 4, step 6).
5. The old INTEGRATION.md decisions on B and C are moot: both are already merged in `baseline/1`, so the installed
   game gets tick independence and the Bot2 horizons with the combined install. B's saber margin at 125 Hz (phase 7
   there) stays open.

## 6. Integration run (2026-11-19)

Branch `integration/1` (worktree `.codex-research/wt-integration`), from `baseline/1` 6e47de09; evidence in
`.codex-research/integration-1/`. Nothing installed; `master` not moved.

**Merges**, in order (first-parent history of `integration/1`):

| Merge | Branch | Resolution |
| --- | --- | --- |
| 694b3172 | `renderer/w3-vertblend` d405a544 | clean |
| c48e4bd3 | `renderer/pbr-materials` (its message says abc6693b; the tip merged was da35e06b, with this audit) | `gltf2map.py`: baseline's collision-cooker block, calling `twin()` with the W3 `jka_material` argument |
| ddb787e7 | `kejim/holo-g2` 078d7487 | clean |
| 4847b7cd | `claude/bot2-review-next` c418a057 | `bot2_move.c`, 8 blocks: C's ms horizons (always on) over BM-8's frame scaling (behind `bot_moveFixBM8`, default 0; equal counts at `sv_fps 40`); the parity horizon's boundary branch keeps BX-4's `searchPredictExtra` |
| 30da41c6 | `claude/pdecal-edge-normal` 0cb15517 | every block already in baseline in the perf-3 review's later form; no content change |
| 8c1702a9 | `claude/char-fixes-vram` 2b5c4f19 | `vk_stream.cpp`, `vk_vram.cpp` merged three-way against 657bd66f (the branch copied both from it unchanged): no conflict. `vk_image.cpp`, `tr_local.h` against the branches' own base (657bd66f would drop baseline's DDS_AllocRange crash-2 fix): four blocks, the review's side. Review F3: TX-10's `R_StreamHold` call in `vk_scene.cpp` dropped |
| 8862d83d | `claude/char-fixes-hc` 3dd4b577 | code clean; INTEGRATION.md: baseline's rows plus the hair-cloth-views row |
| 1ef0fca3, 9323c7cb, 69275d35, bf7999ec | sh21, hr10, sk13, sh23 | `vk_hair.cpp`: SK-13's packed writes; generated shaders rebuilt below |
| 1cf9271b | fix-up | SH-23's camera names (`selfShadowInMask`, `hairMapTexel`, `GridContactChannel()`) in the FMA block; `shader_data.c` rebuilt (624 shaders, glslang 16.6.0); `scene_shader_data.c` rebuilt identical |
| 0c074394 | test fix | `test_movement_graph_reset` stubs BP-7's `SaveDirtyAll` (it failed on the review branch too) |

**Gates:**

- Builds from 1cf9271b (`integration-1/build.sh`): Win32 and x64 Release, every MP target, 0 errors; Win32 7 warnings
  as `baseline/1`'s; x64 9, the 2 more being the H2 `stb_dxt` MIN/MAX guard that `baseline/1`'s x64 build (2026-11-07)
  predates.
- Python suite from the root: 491 tests, as `baseline/1` after the test fix; the one error, `ctf_kejim.pk3` under the
  worktree's missing `game_directory`, is `baseline/1`'s too. `tools/tick_rules_sim.py` exit 0.
- Beauclair city (`integ_stills.py`, the same seven poses on Install 91's kit renderer and on the integration exe,
  modules and renderer): mean difference 0.08-0.40 grey levels on six views; the waterfall view 2.05, all of it in the
  animated water (`diff_cascade_lower.jpg`). Castle: loads and draws as `look-castle14` at its four poses; walls and
  paving read slightly less blue (the W3 branch's neutral ambient reflection, 49b3f181, which the castle bat's kit-1
  renderer lacks).
- Character VRAM (`char_vram_check.py`, the installed exe with the integration renderer): eight heroes and the
  picker, 0 failed, 0 dropped, no crash; heroes as in the review's final run. Whole-loaded textures 23 against 140
  (Install 78 repacked those packs as range-readable DDS). Picker client 3.46 GB at the end against 4.57 GB.
- Bot2 laps control (`laps_ctl.py`, build `integration-1/laps-build`, 16 Kejim + 8 Dantooine, seeds 35-50 / 35-42,
  the b2 campaign's settings): 24 worlds, 0 crashes. Against `batch2-20261110-a` b2 (48 + 16 worlds; build without B):
  team captures Kejim -0.10 +- 0.37, Dantooine -0.27 +- 0.41 (undecided, within noise). Past the cut-off: Kejim
  runners' weapon deaths -2.98 +- 0.85, Dantooine team returns +2.31 +- 0.76 (two of 42 metric rows; B's timer shifts
  at 40 Hz are the expected cause, not attributed). `integration-1/laps/worlds/int-*` is the control for later arms.

**Done 2026-11-19** (owner: "Yes, both"): `master` fast-forwarded to `integration/1` (913d91a6); the main checkout
moved from `renderer/pbr-materials` to `master` (another session's uncommitted docs carried over unchanged); the combined
install is Install 94 (renderer-vulkan-in-xenjk.md). New work branches from `master`.

**Then:** the owner's OK to fast-forward `master` to `integration/1` and for the combined install (exe, modules,
renderer, both clients; the launchers on 64-bit), then the retirement (section 4, step 6).

**Retired 2026-11-19** (decision 4): every branch but `master` tagged `archive/<name>` at its tip (74 tags,
`git tag -l 'archive/*'`). 66 worktrees retired (`.codex-research/install94/wt_retire.py`): uncommitted changes as
patches, untracked files copied and each worktree's own `.codex-research` output moved (not trashed) to
`.codex-research/worktree-retire-20261119/<name>/`; the folders moved to `.codex-trash/20261119T180602-worktrees/`
(D:) and `<levelport root>/.codex-trash/20261119T180602-worktrees/` (C:); their 69 branch refs deleted, each after
its tag check. Kept (KEEP.txt names them): the bot2-exec-deep, bot2-replan-budget and bot2-review-next worktrees,
`wt-m2`, `wt-p3-remap`, `wt-preload`, `wt-review-next-c`, `wt-world128`, with their 5 branches. Links in older docs
into retired worktrees now dangle; their tracked docs are on `master`.
