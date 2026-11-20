# Characters: Ghoul2 skinning, hair, cloth (review 2026-11-19, master a322d9dc4)

Scope: what draws or simulates characters, changed since the 2026-11-09 character review (6fe7bb2f), read in context,
plus the xenjk-authored files of `codemp/rd-common`.

- Read in full at master: `rd-common/tr_g2blend.{cpp,h}` (177, 49), `tr_g2joints.{cpp,h}` (252, 56), `sim_frame.h` (171),
  `tr_sim.h` (67), `tr_hair.cpp` (1,134), `tr_hair.h` (35), `tr_hair_local.h` (100), `tr_cloth.cpp` (1,361), `tr_cloth.h`
  (43), `tr_cloth_local.h` (137), `rd-vulkan/vk_hair.cpp` (487), `vk_hair.h`.
- Read as the diff 6fe7bb2f..master with the code around it: `rd-vulkan/tr_ghoul2.cpp` (5,661; +257), `vk_cloth.cpp`
  (1,378; +140), `shaders/glsl/cloth_draw.comp`, `cloth_layout.h`, `cloth_step.comp`, `rd-common/cloth/cloth_asset.c`,
  `skirt_sim.{c,h}` (2,889, 316; +138), `hairchains/hair_asset.{c,h}`, `hair_sim.{c,h}` (835, 173; +102),
  `hair_spring.c`, and the character parts of the `G2_misc.cpp`, `vk_vbo.cpp`, `vk_mesh_jobs.cpp`,
  `vk_tangent_cache.cpp` diffs.
- Was rd-common covered on 2026-11-09? Yes for hair and cloth: the hair and cloth lanes list every file of
  `rd-common/hairchains`, `rd-common/cloth`, `tr_hair*` and `tr_cloth*` as read in full. Not as files: `tr_g2blend.cpp`
  (the skinning lane checked its call order in `tr_ghoul2.cpp` only), `tr_g2joints.cpp` (cycle handling only), and
  `sim_frame.h` / `tr_sim.h` (written after that review). Those four are reviewed here in full.
- Checked: `python tools/cloth_spirv.py --check` passes at master (the cloth SPIR-V matches its GLSL). Nothing built or run.
- Not covered: `rd-common/xenui_render.h` (UI), the lit shaders (`gen_frag.tmpl`, `common/pbr.glsl`, `common/fma.glsl`),
  `vk_stream.cpp` / `vk_image.cpp` (textures), rd-vanilla beyond checking that `tr_cloth_rb.cpp` and `tr_ghoul2.cpp`
  match the changed rd-common signatures (they do), `tools/`.

Files with no findings: `tr_g2blend.{cpp,h}`, `tr_g2joints.h`, `sim_frame.h`, `tr_sim.h`, `cloth_asset.c`, `skirt_sim.{c,h}`
(diff), `hair_spring.c` (diff), `cloth_draw.comp`, `cloth_layout.h`, `cloth_step.comp` (diff), `G2_misc.cpp` (diff).

Counts: P0 0, P1 1, P2 2, P3 13.

## Findings

### RC-1 P1 The hair's world scale changes by one float step while a character turns, and each change drops the solver's motion history
- Where: `codemp/rd-common/tr_hair.cpp:489-491` (`worldScale` = the length `VectorNormalize` returns for the entity's
  axis 0), `:797` (`HC_SetWorldScale` before every step, `r_hairWorldScale` default 1);
  `codemp/rd-common/hairchains/hair_sim.c:212-221` (`scale != old` clears `nOrigin`, `nRot`, `haveFelt`), `:258-260`
  (the origin history is divided by any scale that is not exactly 1).
- Scenario: an unscaled player. The length of a rotation matrix's row in float is 1.0 or 0.99999994 depending on the
  yaw (36,000 yaws in 0.01 degree steps: 29,285 give 1.0 and 6,715 give 0.99999994; a turn of 1.3 degrees a frame
  changes the value on 29% of frames). Each change is "another scale": the frame history starts over, so that step
  has no frame velocity, spin or acceleration, the next has no acceleration or angular acceleration, and the carried
  felt velocity snaps to the measured one. While the character turns, the hair's turn drag, spin flare and linear
  inertia cut out on roughly a third of the 30 Hz steps, at random. Standing or running straight keeps one value, which
  is why the 2026-11-10 replay check (0 units over 309 steps) did not see it. A dump does not record the scale, so a
  replay of a turning character will also differ from the game.
- Fix: decide the scale with a tolerance. In `Hair_GatherPose` snap a length within 1e-3 of 1 to exactly 1, and in
  `HC_SetWorldScale` keep the old scale (no history reset) when the new one is within 1e-4 of it.
- Confidence: likely (code and float arithmetic traced; the visible size needs a run with a turning character and
  `r_hairWorldScale` 0 against 1, or a dump replayed with the per-step scale).
- Status: fixed (ed9cd5eb4); confirmed offline: tools/hair/hc_replay with a one-ulp scale flicker on odd steps moved the ZAYNE in-game reel by up to 10 units before, bit-identical to the constant scale after; unflickered and `--world-scale 2` outputs unchanged. The look in game is not run.

### RC-2 P2 Patching hair into the model's buffer repacks the whole LOD and uploads 8 bytes per vertex through a 256-byte ring slot each
- Where: `codemp/rd-vulkan/vk_hair.cpp:156-212` (every vertex of the LOD repacked, MikkTSpace run for every surface
  when the tangent cache has nothing prepared), `:291-303` (one `vk_upload_buffer` call per listed vertex);
  `codemp/rd-vulkan/vk_upload.cpp:40`, `:269` (`UPLOAD_ALIGN` 256 per call, one `vkCmdCopyBuffer` each).
- Scenario: `r_hairSharedVbo 1` (default). A hair LOD with 20,000 listed vertices records 20,000 copy commands and
  takes 5 MB of the 64 MB staging ring to move 160 KB; a quarter of a million listed vertices across a warm-up fill the
  ring and `vk_upload_finish` waits for the GPU on the main thread. The full repack (positions, normals, tangents) is
  thrown away on this path: only bone refs and weights of the listed vertices are used. With `r_tangentCache 0`, or a
  model whose tangents are not in the cache, the tangent pass runs for the whole LOD for nothing.
- Fix: on the shared path compute refs and weights for the listed vertices only (no repack, no tangents), sort their
  offsets, and upload each run of adjacent vertices in one call (or one staging block with a region list).
- Confidence: confirmed (traced); the cost in ms is not measured.
- Status: fixed in part (ed9cd5eb4): the shared path no longer runs MikkTSpace for the LOD. deferred: one upload per run of adjacent vertices (or a region list) needs a `vk_upload.cpp` API change (another lane) and a timing run.

### RC-3 P2 A model's buffer can stay patched for hair while nothing uploads the hair palette slots
- Where: `codemp/rd-vulkan/vk_hair.cpp:298-301` (a failed upload returns after earlier vertices were patched; the LOD
  is then neither `ok` nor `shared`), `:337-347` (`Hair_VkSharedBones` counts hair bones only for an `ok && shared`
  LOD of the asset's current record), `:359-373`; `codemp/rd-common/tr_hair.cpp:248-252` (`Hair_BindAsset` frees the
  back-end record when the asset binds to another `model_t`; the first model's buffer keeps its patched refs).
- Scenario: (a) `vk_upload_buffer` fails part way through a LOD (staging allocation refused under memory pressure):
  the vertices already written reference palette slots `numGlaBones + bone`, `RB_HairVkPaletteCount` returns 0, and
  those vertices skin with whatever follows the skeleton's matrices in the uniform buffer (the next character's
  bones). (b) The same asset record rebinds to a second `model_t` of the same name: the first model's patched buffer
  loses its record the same way. Both leave visibly exploded hair vertices until the renderer restarts.
- Fix: record "this model's LOD buffer carries hair refs" on the model (`mdxmVBOModel_t`), set before the first patch
  write, and let the palette count and the host-matrix fallback read that flag and the asset by model, not the asset's
  current record; or stage the patch so a failure writes nothing.
- Confidence: likely for (a) (needs an allocation failure); (b) needs two models with one name, not seen in normal play.
- Status: fixed for (a) (ed9cd5eb4): a patch that fails part way keeps the LOD shared, so the slots its written vertices use are uploaded (the rest keep their own skinning); nothing written still fails cleanly. deferred (b): needs the "patched" flag on `mdxmVBOModel_t` (`vk_vbo.cpp`, another lane), the Liftability `R_MdxmPatchBoneRefs` shape.

### RC-4 P3 `RB_G2BoneRotation` caches a bone index by GLA pointer
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:4282-4296`.
- Scenario: the model cache frees a GLA at a level change and another GLA lands at the same address. The cached index
  of "cranium" is kept (same pointer, same name), so the FMA face shadow (`vk_shade_geometry.cpp:3176`) turns with
  whatever bone has that index, bounded only by `mBones.size()`. The same class as SK-16.
- Fix: clear the cache where `R_TangentCacheForgetPointers` is called, or cache the index on the bone cache.
- Confidence: likely (needs the address reuse).
- Status: fixed (ed9cd5eb4): the cached index is used only while the bone at it still has the name.

### RC-5 P3 The back end poses an RDF_STAGE scene at the shared clock, the front end at the scene's own time
- Where: `codemp/rd-vulkan/tr_scene.cpp:430` (`g2Time = G2API_GetTime( fd->time )`), `codemp/rd-vulkan/tr_ghoul2.cpp:3748`
  (front end: `fd->time | G2_TIME_EXACT` for a stage), `:4082`, `:4157-4159`.
- Scenario: the two agree today only because `UI_Refresh` writes `realtime` into both Ghoul2 clocks before the picker
  draws (`ui_main.c:10138`). A stage scene drawn from anywhere else (the new UI, cgame) gets its hair, cloth and LOD
  from the stage time and its palette from the shared clock, and the SK-3 reuse test fails every frame (a second
  transform and smoothing generation).
- Fix: `tr.refdef.g2Time = G2API_GetTime( R_IS_STAGE( fd->rdflags ) ? fd->time | G2_TIME_EXACT : fd->time )`.
- Confidence: confirmed by reading; no visible effect with today's one caller.
- Status: fixed (ed9cd5eb4), as proposed (`tr_scene.cpp`, one line).

### RC-6 P3 A cloth instance whose skeleton lacks a bone rebinds on every view of every frame
- Where: `codemp/rd-common/tr_cloth.cpp:1304-1307`, `:561-583`.
- Scenario: a garment model drawn with a skeleton that misses one of the asset's bones: `Cloth_BindBones` fails, sets
  `aHeader` NULL, and the next view runs the asset-bones x skeleton-bones name compare again. Hair got `bindFailed`
  for this (HR-16); cloth did not.
- Fix: the same `bindFailed` pointer on `clothInst_t`.
- Confidence: confirmed (traced).
- Status: fixed (ed9cd5eb4).

### RC-7 P3 `r_clothMaxSteps` above 8 makes the GPU solver start over after a hitch instead of catching up
- Where: `codemp/rd-vulkan/vk_cloth.cpp:952-957` (nine records queued in one frame count as "the back end has not
  run"); `codemp/rd-common/tr_cloth.cpp:987`, `:1011`.
- Scenario: `r_clothMaxSteps 12`, a 200 ms frame at 60 Hz: the ninth step clears the queue and marks the instance
  dead; the garment resets next frame.
- Fix: clamp `maxSteps` to 8 when `ci->gpu`, or size the limit from the cvar.
- Confidence: confirmed (traced); non-default cvar.
- Status: fixed (ed9cd5eb4): `CLOTH_GPU_MAX_QUEUED` (8) in `tr_cloth_local.h` caps the glue's steps and sizes the back end's queue test.

### RC-8 P3 `&vector[0]` on vectors that can be empty
- Where: `codemp/rd-common/tr_hair.cpp:620`, `:627`, `:630` (`&hi->G[0]`; `G` has `nGameBones * 16` floats and the
  parser accepts 0 game bones: a hair asset with no colliders and no bone hosts), `:875`.
- Scenario: undefined behaviour; harmless in the MSVC release build, an abort under `_GLIBCXX_ASSERTIONS` (the default
  flags of several Linux distributions) or MSVC debug iterators.
- Fix: `.data()`.
- Confidence: confirmed.
- Status: fixed (ed9cd5eb4).

### RC-9 P3 Hair render weights are checked one by one, not as a sum
- Where: `codemp/rd-common/hairchains/hair_asset.c:322-327`; `codemp/rd-vulkan/vk_hair.cpp:272-280`.
- Scenario: a sidecar whose four weights each pass [0, 1] but sum to 2: `wts[big] + ( 255 - sum )` goes negative and
  wraps as a byte, so the vertex gets arbitrary weights.
- Fix: refuse an entry whose weights do not sum to 1 within 1e-2, or renormalise before the byte conversion.
- Confidence: confirmed (traced); no installed asset is known to do it.
- Status: fixed (ed9cd5eb4): renormalised in `vk_hair.cpp` when the sum is over 1.01 (sums at or under that pack exactly as before); the parser is unchanged.

### RC-10 P3 `.joints` parsing: a repeated bone with a bad number spoils the good line, and a short line shifts every later one
- Where: `codemp/rd-common/tr_g2joints.cpp:186-200` (`own[b]` is written before the coordinates are known to parse,
  and `listed[b]` stays set from the earlier line), `:158-178` (five tokens are taken across line ends).
- Scenario: a bone listed twice, the second time with `x` unreadable: the joint becomes the half-written values with
  only the "bad coordinate" warning. A line with four fields takes the next line's name as its `z`.
- Fix: parse into a local and copy on success; read line by line.
- Confidence: confirmed.
- Status: fixed (ed9cd5eb4): both; checked with a small G2J_Parse test (old: a=(0 9 9) for the repeated bad line, the short line spoiled the next; new: the earlier line kept, the short line skipped with its warning).

### RC-11 P3 Every Ghoul2 model in every view pays two heap allocations and a cvar sweep for hair and cloth it does not have
- Where: `codemp/rd-common/tr_hair.cpp:1020-1028`, `:179-187`; `codemp/rd-common/tr_cloth.cpp:1273-1281`, `:212-220`.
- Scenario: sabers, weapons and NPCs reach `Hair_AssetForModel` / `Cloth_AssetForModel` in each of the frame's views
  (cascades, character shadows, main): the lower-cased name becomes a `std::string` for the map lookup (model paths
  are longer than the small-string buffer), and `Hair_CheckCvars` / `Cloth_CheckCvars` test their `modified` flags per
  call. 30 models in 6 views: about 360 allocations a frame.
- Fix: a table by `model_t::index` (asset pointer or "none"), cleared in the shutdowns; move the cvar checks inside
  the once-per-frame housekeeping.
- Confidence: confirmed; not timed.
- Status: fixed in part (ed9cd5eb4): a lookup by model pointer (the asset's name checked, so a reused address falls back to the name map), cleared in the shutdowns, before the name key is built. The `modified` flag tests stay per call (a few loads; moving them changes when a cvar change applies within a frame). Not timed.

### RC-12 P3 `r_hairSharedVbo` is registered inside the first hair buffer build
- Where: `codemp/rd-vulkan/vk_hair.cpp:284-285`.
- Scenario: the cvar does not exist (no help text, not in `cvarlist` or the generated cvar document) until a hair
  model loads; it is also the only hair cvar outside `R_HairRegister`.
- Fix: register it from a back-end hook called by `R_HairRegister`, as `R_ClothRBRegister` does for cloth.
- Confidence: confirmed.
- Status: fixed (ed9cd5eb4): `R_HairRBRegister`.

### RC-13 P3 Every review fix keeps its old arm behind a cvar
- Where: `r_simPerFrame`, `r_simSteps`, `r_simCarryFrames` (`tr_cloth.cpp:193-195`, `SIM_STEPS_LEGACY` in
  `sim_frame.h:59`, `:80-84`, `tr_cloth.cpp:940-942`), `r_hairWorldScale`, `r_hairPreroll`, `r_hairFade`,
  `r_clothSettle`, `r_clothFeltSpeed`, `r_clothDrawLead 0/1`, `r_clothGpuPinSkin`, `r_hairSharedVbo` (the copy path,
  `vk_hair.cpp:310-334`, `:467-487`), `r_g2SmoothFix`, `r_fixG2SceneTime`, `r_fixG2SceneLod`.
- Scenario: none wrong; each is a second code path that is no longer exercised (all default to the fix) and that a
  later change can break unseen. For a published tree they read as unfinished A/B switches.
- Fix: owner's call per cvar: keep the ones that are settings (`r_hairFade`, `r_hairPreroll`, `r_clothSettle`,
  `r_clothDrawLead`), delete the legacy arms of the rest.
- Confidence: confirmed.
- Status: deferred: owner decision per cvar (which are settings, which legacy arms to delete).

### RC-14 P3 Measurement code from the review sits in the Ghoul2 hot paths
- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:146-181` (`G2_LodStat`: 40 KB of static tables for `r_g2LodStats`), `:372`,
  `:377` (two global counters in `SmoothLow`, per bone), `:4084-4091` (the G2SMOOTH print; `r_g2SmoothFix` -1 and 2
  select it), `:2494-2592` and `:3983-4060` (`r_g2BlendStats`); `tr_hair.cpp:1002-1003` (HAIRDIST).
- Fix: one `#ifdef XENJK_G2_STATS` (off in release) around them, or a `tr_ghoul2_stats.cpp`; `r_g2SmoothFix` becomes
  0/1 and the counters get their own cvar.
- Confidence: confirmed.
- Status: deferred: owner decision (whether the measurement cvars ship; compiling them out removes `r_g2LodStats`, `r_g2BlendStats` and two `r_g2SmoothFix` values). All are off by default.

### RC-15 P3 Dead code
- `codemp/rd-common/tr_cloth.cpp:926`, `:933`: `Tp` is computed and voided.
- `codemp/rd-common/tr_cloth.cpp:441-443`: `Cloth_SetParam` only forwards to `SkirtSim_SetParam`.
- `codemp/rd-vulkan/vk_hair.cpp:19-25`: the 40/44 fallback for `G2_VERT_OFS_*`; `tr_local.h:1200-1201` defines them
  (28/32) since SK-13 merged.
- `codemp/rd-vulkan/vk_stream.cpp:560` `R_StreamHold` (TX-10): no caller since its call was dropped at integration.
- `codemp/rd-common/cloth/skirt_sim.h:209` `maxStretch` "unused (kept from the JS)".
- Status: fixed (ed9cd5eb4) except `R_StreamHold`. deferred: `vk_stream.cpp` belongs to the textures lane (TX-10 is still open there).

### RC-16 P3 Comments that no longer match the code
- `codemp/rd-vulkan/vk_cloth.cpp:4-6`: the vertex is "pos vec4, normal vec4 ... tangent vec4"; it is the packed
  44-byte vertex (`clothVkVertGpu_t`).
- `codemp/rd-vulkan/vk_hair.cpp:4-12`, `vk_hair.h:17`, `codemp/rd-common/tr_hair.h:8`, `:19`: hair "draws from a
  substitute VBO"; by default it is patched into the model's buffer. `vk_hair.cpp:285`: "about 64 bytes a vertex" (44).
- `codemp/rd-common/tr_cloth_local.h:121`: "state slot relSlot (0/1)" (0..2); `:124`: the draw's `relZ`, `s`, `lead2`
  are not described.
- `codemp/rd-common/tr_cloth.cpp:14-16`: the draw step describes `r_clothDrawLead` 1 and 0; the default is 2.
  `:486-488`: the comment for `Cloth_AssetParam` sits above `R_ClothWarmModel`.
- `codemp/rd-common/tr_cloth.h:5`: "(SKA1 ...)"; SKA2 is read too.
- `codemp/rd-vulkan/tr_ghoul2.cpp:1836`: "line ~1719" (a line number in a comment).
- `codemp/rd-vulkan/tr_ghoul2.cpp:142`: "RB_TransformBones leaves it set into the next server frame": not with
  `r_g2SmoothFix 1` (`:4231-4234`).
- Status: fixed (ed9cd5eb4).

## Publish cleanliness

- No licence header (the tree's GPL-2.0 block; `tr_g2blend.*` and `tr_g2joints.*` have it): `rd-common/sim_frame.h`,
  `tr_sim.h`, `tr_hair.cpp`, `tr_hair.h`, `tr_hair_local.h`, `tr_cloth.cpp`, `tr_cloth.h`, `tr_cloth_local.h`,
  `rd-common/cloth/cloth_asset.{c,h}`, `skirt_sim.{c,h}`, `rd-common/hairchains/hair_asset.{c,h}`, `hair_sim.{c,h}`,
  `hair_spring.{c,h}`, `rd-vulkan/vk_hair.{cpp,h}`, `vk_cloth.{cpp,h}`, `shaders/glsl/cloth_draw.comp`,
  `cloth_step.comp`, `cloth_layout.h`. Add the header.
- Status: fixed (ed9cd5eb4), also `rd-vanilla/tr_cloth_rb.cpp`; the cloth SPIR-V was rebuilt with `tools/cloth_spirv.py` (arrays byte-identical, stamp updated).
- `codemp/rd-common/tr_cloth.cpp:446`, `:454`, `:465`: private research paths (`.codex-research/eve2b-np-tune-20261012/NOTES.md`).
  Replace with a `docs/cloth/` record or drop.
- Status: deferred: comments citing `.codex-research` records are left for the separate comment sweep (fix contract).
- `codemp/rd-common/cloth/skirt_sim.h:4`, `skirt_sim.c:2`: "a line-for-line port of ClothResearch/skirt/skirt_sim.js":
  a private project. State the origin as the owner's own work under the tree's licence; `skirt_sim.h:176` "(12 in the
  Chaos dump)" names a dump of another engine's data.
- Status: deferred: provenance wording, owner decision (left unchanged).
- `codemp/rd-common/cloth/skirt_sim.c:38-42`: the fdlibm notice is kept (required by its licence). Fine.
- `codemp/rd-common/hairchains/hair_sim.h:5-6`: "our own ... reading of the BNM parameter set (owner OK 2026-10-19)";
  `hair_sim.h:69` "Bonamik centimetre"; `tr_hair.cpp:7`, `:671`. `hairchains/hair_spring.h:3-13` named another engine's
  spring node as what it read (the springs were since rewritten from the plain damped-spring model, 9e4787a1). These
  are provenance statements about other companies'
  middleware and engine code; the owner should decide their wording before publishing (what was read, that no source
  was copied), and drop the internal approval note and the `lab/` path.
- Status: deferred: provenance wording, owner decision (left unchanged; the `lab/` path and the approval note are inside those statements).
- Internal decision notes in user-visible cvar help: `tr_hair.cpp:1117`, `:1118`, `:1120`, `tr_cloth.cpp:1354`
  ("(owner 2026-11-10: on, 12) Review HR-6: ..."), `vk_hair.cpp:285`, `vk_cloth.cpp:1371`; `tr_init.cpp:1347-1349`,
  `:1393` and 17 more "owner 20.." strings in `tr_init.cpp`. Rewrite as plain descriptions.
- Status: fixed in this lane's files (ed9cd5eb4): `tr_hair.cpp`, `tr_cloth.cpp`, `vk_hair.cpp`, `vk_cloth.cpp` (also the r_sim* "as before 2026-11-09" and r_hairFlow "Install 20" notes). deferred: `tr_init.cpp` is shared with the shading and textures lanes; its strings are left to one owner of that file.
- Review IDs as comments ("review SK-3", "independent review F7"): 288 lines across `codemp`. They point at
  `docs/review/`, which is fine if those documents are published; otherwise reword to say what the code does.
- Status: deferred: the separate comment sweep (fix contract).
- Outside this lane's files, seen while checking: `tools/hair/simcheck_run.py:36` (default `--root`
  `the repository root`), `:48` and `tools/cloth/CMakeLists.txt:3-4`, `tools/cloth/skirt_replay.c:20`
  (`.codex-research/...` paths).
- Status: deferred: tools lane.

## Liftability

- **Shared stepping lives in the cloth file.** `r_sim*` cvars, `r_simCheckStats` and every `R_SimCheck*` function are
  defined in `tr_cloth.cpp:47-198`, so hair cannot be lifted without cloth. Shape: `rd-common/tr_sim.cpp` beside
  `tr_sim.h`, registered from the renderer's init, not from `R_ClothRegister`.
  - Status: done (adb938eb2): moved verbatim to `rd-common/tr_sim.cpp`; registered from `R_ClothRegister` and `R_HairRegister` (no `tr_init.cpp` edit, which other lanes share).
- **Hair reaches Ghoul2 through a cloth-named accessor.** `tr_hair.cpp:28` and `vk_hair.cpp:16` include `tr_cloth.h`
  for `R_ClothEvalBone`; `R_BoneCacheModel` is declared in `tr_hair_local.h:95`. Shape: one `rd-common/tr_g2ext.h`
  (`R_G2EvalRenderBone`, `R_G2BoneCacheModel`, the forget hook) that `tr_ghoul2.cpp` implements and hair, cloth and
  the shading code include.
  - Status: done in part (adb938eb2): `rd-common/tr_g2ext.h` declares `R_ClothEvalBone` and `R_BoneCacheModel` (names kept, so `tr_ghoul2.cpp` is untouched); hair includes it instead of `tr_cloth.h`. The rename and the forget hook are not done (they reach `tr_ghoul2.cpp` in both renderers and the shading code).
- **xenjk state and features inline in `CBoneCache` and `tr_ghoul2.cpp`.** The class (`tr_ghoul2.cpp:283-640`) carries
  the UBO cache, the joint box, the SK-3 generation, joints and blend flags; the file also holds `R_LoadG2Joints`
  (`:4860-4925`), the joint box, `RB_G2BoneRotation`, the LOD rule (`:1146-1159`) and three statistics blocks. Shape:
  declare `CBoneCache` in a private header (`tr_ghoul2_local.h`), move the xenjk functions to `tr_ghoul2_xen.cpp`
  (joint box, bone rotation, joints loading, statistics), and leave in the stock functions one call each:
  `G2X_LocalBone()` for the blend, `G2X_CorrectJoint()` after the transform, `G2X_Lod()` in `G2_ComputeLOD`,
  `G2X_SimAdd()` for hair and cloth in `R_AddGhoulSurfaces`.
  - Status: not done: declaring `CBoneCache` in a header and moving the xenjk functions out means un-static'ing the stock helpers they call in a 5,700-line stock file; not a mechanical move, wants its own review.
- **`r_g2Blend` glue is written twice.** `G2_BlendBoneDraw`, the curve choice, `G2B_Stats`, `G2B_CheckBoltQuery` and
  `G2B_CheckCollisionQuery` exist in both `rd-vanilla/tr_ghoul2.cpp` (`:1501-1570`, `:2258-2351`, `:3662-3708`) and
  `rd-vulkan/tr_ghoul2.cpp`. Shape: into `rd-common/tr_g2blend.cpp`, taking the GLA header, the `SBoneCalc` fields and
  an evaluate callback. `r_g2Joints` is rd-vulkan only: say so in `docs/g2-joints.md` or give rd-vanilla the same
  three call sites.
  - Status: not done: the two copies differ in their bone-calc types and evaluate paths; sharing them needs a callback interface (a rewrite, not a move).
- **Hair patches another module's vertex buffer.** `vk_hair.cpp:286-309` writes into `mdxmVBOModel_t::vbo` with offsets
  it checks by number. Shape: `R_MdxmPatchBoneRefs( mod, lod, runs )` in `vk_vbo.cpp`, which owns the layout and can
  record the patch on the model (RC-3).
  - Status: not done: `vk_vbo.cpp` is another lane's file (with RC-2's batching and RC-3 b).

## Still open from earlier reviews

The review document (`docs/review/2026-11-09-character-rendering.md`) still words every row as "on branch ...";
all of those branches are in master. SK, HR and CL rows were re-read in the code; TX and SH rows were checked by the
presence of the fix's code path (its cvar or function) in master, not re-reviewed.

Fixed in master (verified):
- Skinning: SK-1, SK-2, SK-3, SK-4, SK-5, SK-6, SK-7, SK-8, SK-9, SK-10, SK-11, SK-13 (44-byte vertex), SK-14, SK-15,
  SK-16, SK-17, SK-18, SK-19, SK-20, SK-22, SK-23.
- Hair: HR-1, HR-2, HR-3, HR-4, HR-5, HR-6, HR-7 (hardened), HR-8, HR-9, HR-10 (see RC-2, RC-3), HR-13, HR-15, HR-16,
  HR-18 (see RC-1), HR-19.
- Cloth: CL-1, CL-2, CL-3, CL-5, CL-9, CL-10, CL-11, CL-12, CL-13, CL-16, CL-19, CL-20, CL-21, CL-22 (the hash check
  passes; still not run by CMake), CL-23.
- Textures (code path present): TX-1, TX-2, TX-3, TX-4, TX-5, TX-8, TX-11, TX-12, TX-15, TX-17.
- Shading (code path present): SH-1, SH-2, SH-3, SH-4, SH-5, SH-9, SH-10, SH-12, SH-13, SH-14, SH-15, SH-16, SH-17,
  SH-18, SH-20, SH-21, SH-22, SH-23, SH-24, SH-25.

Still open:
- SK-12: the per-(model, skin) surface-to-shader table (the probe lookup part is fixed).
- SK-21: the inherited merge-both-ways branch, the duplicated tangent and weight routines, `R_LoadMDXM_Server`'s
  empty `vboModels` (only `boneMatrices[72]` was removed).
- HR-11: `HC_PoseFrom`'s per-cone subtree walk.
- HR-12: an automated fixture test (the parser is shared now).
- HR-14: no settings record in a dump, springs not dumped (and, new with HR-18, the world scale: RC-1).
- HR-17: the spring part (`maxDisplacement 0`, group scales).
- CL-6: `skirt_replay --profile N` and the GPU readback.
- CL-7, CL-8: parked for timing; nothing changed.
- CL-14: the bind's vertex-order check for version-2 assets.
- CL-15: stepping continues for LODs that never draw the cloth (the settle and hysteresis parts are done).
- CL-17: the three copies of the frame logic in `skirt_sim.c` (`SkirtSim_StepFrame` still "mirrors" Reset and Step).
- TX-6: the worker decode for raw and deflated textures (the range read and the pack conversions are in).
- TX-10: not addressed in master: `R_StreamHold` exists (`vk_stream.cpp:560`) and nothing calls it.
- TX-14: a cancel call for the picker. TX-16: the staging-buffer cap. TX-18: one mip level, two targets, streamed
  albedos skipped.
- TX-19: the write-to-a-temporary-name-then-rename half (`vk_init.cpp:416` still says it needs an `FS_Rename` export).
- SH-6, SH-7: parked; the light mask is still `#if 0` (`common/pbr.glsl:264`).
- SH-11: no `demote` path.

Closed without a code change, or data only (nothing to verify in the code): HR-20, CL-4 (refuted), CL-18, CL-24
(refuted), TX-7, TX-9, TX-13, TX-20, TX-21, SH-8, SH-19, SH-26, SK-24 (the docs still name
`zz_xen_tangents_v1.pk3`; whether v2 was installed is not visible in the repository).
