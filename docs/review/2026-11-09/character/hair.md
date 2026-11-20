# Character rendering review: hair chains and spring bones

Lane: hair chains and spring bones. Target: commit 6fe7bb2f, read from `.codex-research/wt-review-6fe7bb2f`.
Read-only review, 2026-11-09. All `file:line` references are at 6fe7bb2f.

## Summary

- 20 findings: 7 bug, 4 perf, 5 quality, 4 suggestion. Severity: 0 high, 7 med, 13 low.
- Nothing here is a crash or a visible defect in the common case (own character, third person, in view). The
  medium findings are view-dependent draw state, a tool that does not start a run the way the game does, missing
  bounds in the asset parser, and a spring state that never recovers from one bad input.
- Hair CPU cost is small. On a synthetic 80-particle, 98-bone, 960-pair asset (gcc -O2, this PC): 74 us per solver
  step (48 us without collision pairs) and 15 us per `HC_PoseFrom` call. That is roughly 0.03 ms per frame per
  character at 144 fps. The cost worth attention is VRAM (HR-10), not CPU.
- The stepping scheme is frame-rate independent to within what linear input lerping allows (measured, HR-20).
  I found no frame-rate dependence in the spring-bone path that would explain a stutter.

### What was checked and held

- `hc_replay.c` and `hcd_to_hcr.py` reproduce the game's reset numbering (reset at counter c, first step reuses c),
  so the dump replay path matches the engine. The reset record's own matrix is discarded by the history drop, so
  using step 0's matrix for the reset offline changes nothing.
- `Hair_VkBuildLod` packs vertices the same way as `R_PackMDXMLod` (stride 64, same offsets, same weight rounding).
- `chainsio.py` record layouts and header offsets match `hair_asset.c` field for field, springs block included.
- Parser fuzz: 300,000 mutated copies of a synthetic asset (1 to 3 random byte changes, CRC repaired, the three
  iteration-count fields and bone counts over 4096 excluded, see HR-4): 132,290 parsed and ran a step and a pose,
  167,710 were rejected, no crash.
- The own-skin weight merge (`vk_hair.cpp:207-248`) cannot overrun its 8-entry arrays.

### Scratch checks

Built outside the repo against the worktree's `hair_sim.c`, `hair_asset.c`, `hair_spring.c`. Sources and the
synthetic asset are in
`<session scratchpad>`
(`mkasset.py`, `check.c`). The synthetic asset is 16 chains of 5 particles with the `bob` preset values, 12 capsule
colliders, all-pairs collision, and 2 spring bones. No real model asset was used, so absolute numbers are indicative.

---

## Findings, most severe first

### HR-1 (bug, med, plausible): a later view in the same frame can switch a character's hair to rigid for every view

- Where: `codemp/rd-common/tr_hair.cpp:868`, `:873-874`; `codemp/rd-vulkan/vk_hair.cpp:289-294`;
  `codemp/rd-vulkan/tr_ghoul2.cpp:3760-3766`.
- What: `hi->ready` is one flag per instance. `R_HairAddGhoul` clears it on entry for every non-shadow view and only
  sets it again if that view passes the `personalModel` and `r_hairMaxDist` tests. The distance test uses
  `tr.viewParms.ori.origin`, which is the mirror or portal camera in those views. The back end runs after all front-end
  views and reads the final value, so the last view to visit the entity decides for all of them.
- Failure: a character within 1500 units of the player is also inside a mirror or portal view whose camera is more
  than 1500 units from it. The portal view runs after the main view (`R_MirrorViewBySurface`, `tr_main.cpp:1204`),
  clears `ready`, and the main view draws that character with rigid hair for as long as the portal is on screen. It
  flips back when the portal leaves view, so the hair snaps.
- Fix: make readiness per frame, not per view. Run the front end once per bone cache per frame (stamp
  `tr.frameCount` and the scene), test distance against the scene's main camera (`tr.refdef.vieworg`), and let later
  views reuse the result instead of clearing it. Cloth has the same pattern (`tr_cloth.cpp:993-999`); the cloth lane
  should check it.
- Not confirmed in game. It needs a map with a mirror or portal camera.

### HR-2 (bug, med, confirmed by reading): shadow views draw a stale hair palette for characters the main view culled

- Where: `codemp/rd-vulkan/tr_ghoul2.cpp:3673-3677` (cull returns before hair), `:3760-3766` (shadow views skip the
  front end); `codemp/rd-common/tr_hair.cpp:868`, `:907-909`; `codemp/rd-vulkan/vk_hair.cpp:289-294`.
- What: `ready` is only ever cleared inside `R_HairAddGhoul`. When the main view frustum-culls a character, that
  function is not called, so `ready` stays true from the last frame it was visible and `hi->palette` keeps that
  frame's matrices. Sun cascade and character shadow views still draw the character and take that stale palette.
- Failure: a character just outside the screen edge casts a shadow whose hair sits where the head was (in model
  space) when it was last on screen, while the body shadow animates. A crouch, roll or death animation moves the head
  well away from it. After 5 s the instance is freed (`HAIR_IDLE_FREE_MS`) and the shadow hair becomes rigid.
- Fix: same as HR-1. With a per-frame stamp, `Hair_VkReadyInst` can reject a palette that is not from this frame;
  better, let the first view of any kind that reaches the entity run the front end, so shadow-only casters simulate.

### HR-3 (bug, med, confirmed by scratch run): `hc_eval` does not start a run the way the engine does

- Where: `tools/hair/hc_eval.c:64-65` against `codemp/rd-common/tr_hair.cpp:564-572` and
  `codemp/rd-common/hairchains/hair_sim.c:222-223`, `:293-299`.
- What: the engine resets with two `HC_BeginFrame` calls at counter c and then runs its first step at c again, so the
  frame history starts over and the felt velocity adopts the entity's real velocity on step 1 with no force.
  `hc_eval` resets at `frame[0] - 1` and steps at `frame[0]`. That is consecutive, so the history is kept: step 0 sees
  a zero velocity, and step 1 sees the full velocity as a sudden change that the carry spreads out as a push. The
  header comment (`hc_eval.c:8-10`) says the run "starts like the engine's reset".
- Measured: same asset, game feel and flow defaults, reference frame moving at 250 units/s from step 0. Engine
  numbering against `hc_eval` numbering differ by up to 2.97 units (4.64 cm) at step 8.
- Impact: any fit or scenario run through `hc_eval.py` that starts from a moving state gets a start-up kick the game
  does not produce. `presets.json` says the `bob` preset was fitted against the game runtime defaults; if those
  scenarios started in motion, the fit absorbed the kick.
- Fix: reset at `frame[0]` twice (as `hc_replay.c:202-203` does with `resetFrame`), or pass the reset frame in.

### HR-4 (bug, med, confirmed by reading): the asset parser leaves counts unbounded; a large bone count overflows the heap in `HC_Init`

- Where: `codemp/rd-common/hairchains/hair_asset.c:93-96` (count checks), `hair_sim.c:183-190`,
  `codemp/rd-common/tr_hair.cpp:356` against `:420`.
- What:
  - `nBones` has no upper bound beyond file size. `HC_Init` allocates `a->nBones * a->nBones` ints, multiplied as
    `int`. At 65,536 bones (a 6.5 MB file) the product wraps to 0 and the allocation is one element; at 92,682 it
    wraps to 18,532. The loop at `:185-190` then writes at least `nBones` entries.
  - The 256-bone palette check is in `Hair_BindBones`, which runs after `Hair_CreateInst` has already called `HC_Init`.
  - `substeps`, `lateralIter` and `collisionIter` only have a lower bound. A value near 2^31 hangs the renderer in
    `HC_Step` on the first frame the model is drawn.
- Failure: a malformed or hostile `.chains` in a pack crashes or hangs the client. Our own producers never write
  such values, so this is hardening, but the file is read from any pk3.
- Fix: in `HairAsset_Parse`, reject `nBones > 256` (the palette ceiling), cap the three iteration counts (16 is far
  above any preset), and cap `nParticles`, `nCones`, `nPairs` to sane values. Compute the subtree size in `size_t`.

### HR-5 (bug, med, confirmed by scratch run): a spring bone never recovers from one non-finite input

- Where: `codemp/rd-common/hairchains/hair_spring.c:102-109`, `:138-143`; `codemp/rd-common/tr_hair.cpp:704-716`.
- What: the per-step finite check covers chain particles only. In `HS_Step`, a NaN location fails the
  `> reset * reset` comparison, so the reset branch is skipped and the NaN is written back to `st->loc`. Nothing sets
  `fresh` again except a full `Hair_Reset` (gap, teleport) or switching the spring's group off.
- Measured: one NaN target, then 1000 good steps: `loc` is still NaN and `HS_Delta` returns a matrix with a NaN
  translation.
- Failure: one bad bone matrix for one frame (the chains self-heal through their reset) leaves the spring bone's
  palette matrix NaN. Every vertex weighted to it (Eve's chest or hips) disappears or streaks until the character
  is unseen for 500 ms.
- Fix: after the spring loop in `Hair_AdvanceSprings`, test `loc` and `vel` for finiteness and set `fresh = 1` with a
  zero offset when the test fails. Make `HS_Step` treat a non-finite `d` as past the reset threshold.

### HR-6 (suggestion, med): every reset starts the hair in the animated pose with no settle, and resets are frequent

- Where: `codemp/rd-common/tr_hair.cpp:561-585`, `:652` (`HAIR_RESET_GAP_MS` 500); `tr_ghoul2.cpp:3673-3677`.
- What: `Hair_Reset` puts every particle on its animated target. A character that is frustum-culled is not stepped,
  so turning the camera away for more than half a second resets it. The same happens on spawn, respawn, teleport,
  crossing `r_hairMaxDist` inward, and leaving first person.
- Effect: each time, the hair starts in its bind shape and visibly falls and swings into place over the next half
  second or so. With long locks or a skirt hosted through bone hosts this reads as a pop.
- Fix: pre-roll on reset. Run 10 to 15 steps with the reset pose as static input before the first draw. At the
  measured 74 us a step that is about 1 ms once per reset. Separately, HR-2's fix (step characters that only a shadow
  view sees) removes most of the camera-turn resets.

### HR-7 (bug, med, plausible): the sidecar stores names in 32 bytes and the writer truncates silently

- Where: `tools/hair/chainsio.py:35-37` (`_name`), used for bones, game bones, render surfaces and the host name;
  `codemp/rd-common/tr_hair.cpp:243`, `:401` (full-length compares); `rd-common/mdx_format.h:210`, `:400` (GLM
  surface and GLA bone names are `MAX_QPATH`, 64 bytes).
- Failure: a converted model with a surface or skeleton bone name of 32 characters or more. The writer cuts it to
  31, the build reports success, and at run time the compare fails: "has no surface" or "skeleton has no bone", and
  the whole character draws rigid. Two long names that share their first 31 characters would also collide.
- Fix: make `_name` raise on names longer than 31 bytes. If long names exist in converted rigs, widen the fields in
  a format version 3.
- I did not scan the installed packs for names this long.

### HR-8 (bug, low, confirmed by reading and scratch run): when steps are dropped, the next step sees a velocity spike

- Where: `codemp/rd-common/tr_hair.cpp:682-722`; `codemp/rd-common/hairchains/hair_sim.c:239`, `:283`, `:292`.
- What: with more than `r_hairMaxSteps` steps owed, the steps taken cover the earliest part of the frame interval
  (their lerp fractions are computed from the full accumulator), the rest of the time is dropped, and `Wprev` is set
  to the frame's end. The next step's input is then up to the whole dropped interval further on, but
  `HC_BeginFrame` divides the displacement by one `dt`.
- Effect: below 7.5 fps (4 steps of 1/30 s), or at any frame rate with `r_hairMaxSteps 1`, the hair feels a
  velocity several times the real one. The carry lag and the 1.5 g clamp bound it. Measured at 150 ms frames: 39
  drops in 12 s and a tip error of 5.0 units rms against a 250 fps run.
- Fix: when clamping, spread the steps over the whole frame (fraction `(k + 1) / n`), or drop the frame history for
  the step after a drop.

### HR-9 (perf, low): the asset lookup is a linear string compare over every model ever drawn

- Where: `codemp/rd-common/tr_hair.cpp:159-164`, called from `:871` for every Ghoul2 model in every non-shadow view.
- What: `hairAssets` gets an entry for each distinct GLM that has been rendered, including the ones with no sidecar
  (sabers, NPCs, weapons), and never shrinks within a level. Each call does `Q_stricmp` down the list.
- Cost: entries x Ghoul2 models x views per frame. With 100 entries and 40 Ghoul2 models that is about 4,000
  64-byte compares a frame; small, but it grows with session length and it is paid for models that have no hair.
- Fix: cache the result on the model (an index or pointer slot on `model_t` or its `mdxmData`), or key a hash map by
  the model's index.

### HR-10 (perf, med, confirmed by reading): the substitute buffer duplicates each listed LOD's whole vertex buffer

- Where: `codemp/rd-vulkan/vk_hair.cpp:127-144`, `:263-266`, `:337-379`; `tr_ghoul2.cpp:4183-4205`.
- What: for every LOD that lists any hair surface, `Hair_VkBuildLod` builds a second device-local copy of all the
  LOD's vertices (every surface, not only hair), 64 bytes a vertex. The model warm-up builds them for every hair model
  whether or not it is ever drawn simulated.
- Cost: a 60,000-vertex LOD 0 is 3.8 MB again, plus the lower LODs, per hair model loaded. This is VRAM that the
  character picker and the 6 GB target both care about.
- Second cost: a hair surface and a plain surface of the same shader and entity can no longer share a batch
  (`tr_ghoul2.cpp:4195-4205`).
- Fix: keep one buffer. Patch the listed vertices into the model's own LOD buffer at build time and always upload
  the hair palette slots: the simulated matrices when the instance is ready, otherwise each hair bone's host matrix
  (which reproduces the rigid pose exactly, since `H x bind x bind^-1 = H`). That removes the duplicate, the batch
  break and the per-surface `RB_HairVkSurfaceVBO` lookup. The palette upload then costs `nBones` extra matrices for
  hair models that are not simulating.
- Also: `Hair_VkBuildLod` re-implements `R_PackMDXMLod` (`vk_vbo.cpp:967`). The two match today, guarded only by a
  stride check (`vk_hair.cpp:130`). Calling `R_PackMDXMLod` and patching its output removes the drift risk.

### HR-11 (perf, low, measured): per-frame work that can be trimmed

Hair is not a hot spot, so these are tidy-ups, in order of size:

- `HC_PoseFrom` runs every render frame and multiplies each cone's whole bone subtree
  (`hair_sim.c:680-716`): 15 us per call on the synthetic asset with `r_hairFast 1`, 30 us without. At 144 fps that
  is as much as the 30 Hz stepping. A single root-to-tip pass that carries an accumulated rotation per bone is O(bones).
- Heap allocations every frame per instance: `tr_hair.cpp:618` (`Gs`), `:675-676` (`Gs`, `prePos`, `prePrev`, the
  last two only used when dumping). Keep them on the instance.
- `R_HairRBPrepare` rescans `listed[lod][surface]` for every instance every frame (`vk_hair.cpp:364-367`). Store a
  per-LOD "any listed" flag at bind.
- Spring bones: `tr_hair.cpp:782-784` scans all bones for each spring. Precompute each spring's bone list.
- Collision is all receiver-repulsor pairs (`build_chains.py:212`): 26 us of the 74 us step for 960 pairs. A
  bounding-sphere reject per repulsor would remove most of it if assets grow.

### HR-12 (quality, med): `hc_replay.c` carries its own unvalidated copy of the parser, and nothing is tested automatically

- Where: `tools/hair/hc_replay.c:50-123`; `docs/hair/FORMAT.md:4`.
- What: the replay harness re-reads the HCH1 sections by hand with no CRC check and no index validation, and
  `calloc(a->nParticles, ...)` with a zero count for springs-only assets. `hc_eval.c` already links `hair_asset.c`.
  A format change has to be made in three places (C parser, replay parser, `chainsio.py`).
- There is no test under `tools/` for hair: no `chainsio` round trip against `HairAsset_Parse`, no fixture replay.
  Parity rests on manual runs against private assets.
- Fix: build `hc_replay` against `hair_asset.c`. Add one small test: write a synthetic asset with `chainsio`, parse
  it through `hc_eval`, run N steps and compare against a stored result. The scratch `mkasset.py` is a starting point.

### HR-13 (quality, low): comments and the format doc describe things the code does not do

- `tr_hair.cpp:514-517`: the dump comment says version 1 and omits the flow block; the code writes version 2
  (`:531`).
- `FORMAT.md:64` (flag 1, repulsor points supplied per frame), `hair_sim.h:51` ("-1 = supplied per frame") and
  `chainsio.py:16`: the parser rejects any repulsor game bone below 0 (`hair_asset.c:249`), so such an asset cannot
  load.
- `vk_hair.cpp:12`: "81 matrices per character" is Tifa's count, not a property of the system.
- `FORMAT.md:58`: header offset 44 is "(unused, -1)" but `hc_replay.c:61` reads it as `hostBone` and
  `build_chains.py:221` writes a GLA index there.

### HR-14 (bug, low, confirmed by reading): dump edge cases

- `tr_hair.cpp:531-541`: feel, flow and the containment flag are written once when the file opens. A cvar change
  during the dump (`Hair_CheckCvars`) changes the solver but not the header, so the replay diverges with no sign of why.
  Close the dump when settings change, or write a settings record.
- `tr_hair.cpp:549-553`: `&an[0]` on an empty vector when an asset has particles but no cones. Undefined behaviour;
  an MSVC debug build asserts.
- Spring bones are not in the dump at all, so a spring problem seen in game cannot be replayed offline.

### HR-15 (suggestion, low): which characters get hair is first come, and the distance limit is a hard cut

- Where: `codemp/rd-common/tr_hair.cpp:343-352`, `:874`.
- What: with 8 instances alive, a ninth character draws rigid until one of the eight has been unseen for a second,
  however close the ninth is. At `r_hairMaxDist` the hair snaps to rigid on the way out and resets on the way in.
- Fix: when full, evict the farthest instance for a nearer one. Fade near the limit with the existing draw scale
  (`Hair_ParticleScale`, `:750-754`) so the pose blends to the animation over the last 20% of the distance.

### HR-16 (quality, low): failure paths repeat their work every frame

- `tr_hair.cpp:888-891`: when `Hair_BindBones` fails, `aHeader` is set to NULL, so the bind is retried every frame:
  `(1 + nGameBones) x numBones` name compares per frame for a model that will never bind. Remember the failed
  skeleton.
- `tr_hair.cpp:356-361`: when `HC_Init` fails, the instance is deleted without `HC_Free` (the arrays that did
  allocate leak), and the next frame tries again and prints the warning again.

### HR-17 (bug, low, confirmed by reading): a zero limit means "no limit" in the feel clamps

- Where: `codemp/rd-common/hairchains/hair_sim.c:125-130`, used at `:288`, `:310`, `:367`, `:370`, `:374`.
- What: `clamp_len` only clamps when `maxLen > 0`. `r_hairFeelMaxG 0`, `r_hairFeelAngAcc 0` or `r_hairFeelAngVel 0`
  therefore remove the limit instead of removing the effect, and an asset with zero gravity gets an unclamped linear
  response (the limit is `maxLinAccG x |gravity|`).
- Related: a spring with `maxDisplacement 0` ignores `r_hairSpringLimit` and the group scales except for 0
  (`tr_hair.cpp:593-596`), while chain body particles scale with them.
- Fix: clamp to zero when the limit is zero and document "negative = no limit" if that is wanted.

### HR-18 (bug, low, plausible): entity scale is dropped from the solver's world frame

- Where: `codemp/rd-common/tr_hair.cpp:436-442`.
- What: the entity axes are normalised into `W`, so a scaled entity's hair is solved in unscaled model units while
  gravity and the entity's accelerations arrive in world units. On an entity drawn at scale s the hair feels gravity
  and runs 1/s times too strongly relative to its size.
- Effect: only for entities with `modelScale` other than 1. Packs that bake their scale into the model are unaffected.
- Fix: multiply the model-space gravity and felt accelerations by 1/s, or keep the scale in `W` and take it out of the
  rotation only.

### HR-19 (quality, low): semantic checks the parser does not make

- Where: `codemp/rd-common/hairchains/hair_asset.c:226-291`.
- Bind matrices are assumed rigid by `M4_InvRigid` (`tr_hair.cpp:194`) and by `RB_HairVkJointBox`, but the parser
  accepts any matrix. A bind with scale gives wrong skinning matrices with no warning.
- Render entry weights are not range-checked. A weight above 1 wraps in the byte conversion
  (`vk_hair.cpp:254-259`).
- A spring bone's descendants may carry particles; only the spring bone itself is checked (`:278-291`). Such a chain
  would be moved by the spring delta after the solver posed it.
- Cone order (root to tip) and float finiteness are not checked. A NaN rest length makes the instance reset every
  step, reported only at developer print level.
- `Hair_ApplySettings` writes the containment flag into the shared asset through an instance
  (`tr_hair.cpp:337-339`). It works because the setting is global, but the asset is otherwise treated as read-only.

### HR-20 (quality, low, measured): how frame-rate independent the scheme is

- Where: `codemp/rd-common/tr_hair.cpp:612-647`, `:682-727`, `:740-746`, `:762-770`.
- What: fixed steps from accumulators, inputs lerped linearly between the previous and current frame, the last two
  step states blended at draw (drawn one step behind: 33 ms for chains, 8 ms for springs). The design is right. The
  residual frame-rate dependence is the linear lerp: a step that falls inside a long frame sees a chord of the
  animation's curve.
- Measured on the synthetic asset, host bone moving on 0.9 to 3.1 Hz sines, 12 s, against a 250 fps run. Tip is the
  drawn end particle of one chain in the host frame; spring is the drawn offset (limit 0.64 units):

  | frame time | tip max | tip rms | spring max | spring rms |
  |---|---|---|---|---|
  | 7 ms (143 fps) | 0.26 | 0.02 | 0.014 | 0.001 |
  | 16 ms (62 fps) | 0.55 | 0.08 | 0.013 | 0.004 |
  | 16 +/- 6 ms jitter | 1.40 | 0.26 | 0.034 | 0.009 |
  | 33 ms (30 fps) | 1.99 | 0.40 | 0.061 | 0.021 |
  | 50 ms (20 fps) | 1.71 | 0.52 | 0.106 | 0.044 |
  | 100 ms (10 fps) | 5.51 | 1.50 | 0.600 | 0.379 |

  Units are asset units (0.64 per cm in this asset).
- Reading: at 60 fps and above the difference is about a millimetre rms. Springs stay within 3% of their limit down
  to 30 fps. Nothing in this path produces a periodic stutter. If body springs stutter in game, the cause is more
  likely in what feeds them (the host bone matrices frame to frame) than in the stepping.
- Possible improvement: none needed. Jittered frame times cost more than steady ones because the lerp chords vary.

---

## Needs a high-effort follow-up

1. HR-1 and HR-2 in game: a mirror or portal map for HR-1; a character at the screen edge with sun or character
   shadows on for HR-2. Both fixes touch the same per-frame readiness logic, and cloth shares the pattern.
2. HR-10: the single-buffer design changes the palette upload and the model build path. It needs the skinning lane's
   agreement and a VRAM measurement on real hair models before and after.
3. Body-spring stutter: capture host bone matrices per frame for a spring model with the springs' offsets. This needs
   spring state in the dump (HR-14) and an in-game run. The stepping itself measured clean (HR-20).
4. Real-asset cost: `hc_replay --time` on a Tifa and an Eve reel, and `r_hairStats` with 8 characters, to replace the
   synthetic numbers.
5. One Ghoul2 instance added to the scene more than once in a frame (force shells, speed afterimages at old
   origins): the instance's `W` is overwritten by each add, and only the first add of a frame steps. I did not trace
   which refEntity comes first in cgame.
6. HR-3: check whether the scenarios used to fit `presets.json` started in motion, and refit if they did.

## Not covered

- The private sidecar builders for FF7R and Stellar Blade (in `.codex-research`), and any real `.chains` file.
- `docs/hair/PRESETS.md` and the fit procedure itself.
- The GLSL skinning path and the uniform range reservation for the 256-bone palette (skinning lane).
- Hair shading, the hair opacity map and sorting (shading lane).
- Cloth, except where it shares the readiness pattern (HR-1).
- rd-vanilla, which does not run hair.
- In-game behaviour of any kind: nothing was built or launched.
- The solver's physical model against its source (the Route-A parity records are private). I checked the C for
  internal consistency only. One note: the rotating-frame terms have no Coriolis term, which is a modelling choice.
