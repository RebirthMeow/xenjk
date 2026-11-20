# Character rendering review: cloth simulation

Target: commit 6fe7bb2f (`renderer/install68-models`), read from
`.codex-research/wt-review-6fe7bb2f`. Static analysis only: nothing was built, run or timed.
All `file:line` references are at 6fe7bb2f. Paths are relative to the worktree root.

Scope read in full: `codemp/rd-common/cloth/cloth_asset.{c,h}`, `skirt_sim.{c,h}`,
`codemp/rd-common/tr_cloth.cpp`, `tr_cloth.h`, `tr_cloth_local.h`, `codemp/rd-vulkan/vk_cloth.{cpp,h}`,
`shaders/glsl/cloth_step.comp`, `cloth_draw.comp`, `cloth_layout.h`, `tools/cloth_spirv.py`,
`tools/cloth/CMakeLists.txt`, `tools/cloth/pipeline/asset/skaio.py`. Read in part:
`tools/cloth/skirt_replay.c` (asset loader, game-dump mode), `compare_c_js.mjs` (head),
`build_cloth_asset.py` (collider, capsules, writer), the cloth call sites in `rd-vulkan/tr_ghoul2.cpp`,
`tr_backend.cpp`, `vk_frame.cpp`, `vk_upload.cpp`.

Confidence: **confirmed** means the mechanism follows directly from the code (arithmetic or control flow I
traced end to end). **plausible** means the mechanism is in the code but its trigger or visible effect
depends on something I did not verify (asset contents, engine call order, a device limit). No finding
here was reproduced in the game.

Counts: 22 findings. bug 11, perf 5, quality 3, suggestion 3. Severity: high 0, med 8, low 14.

What checked out (no finding): the fixed-step accumulator and the per-step pose lerp are frame-rate
independent while the instance is advanced every frame; stiffness, damping and drive damping are
normalised to step and substep length (`skirt_sim.c:2339-2372`); the C/GLSL step-record offsets in
`cloth_layout.h` are contiguous and non-overlapping (checked by hand, CLR_F through CLR_MATS); the
GPU constraint colouring has no write races (constraints of one colour share no dynamic particle, and
per-particle constraints only read pinned anchors); upload-before-dispatch ordering holds
(`vk_upload_flush` runs before the frame submit, `vk_frame.cpp:909`); buffer destruction is deferred
past every in-flight command buffer (`vk_cloth.cpp:163-177`); the build-time scratch and heap sizes in
`skirt_sim.c` cover their users; `ClothAsset_Parse` bounds every array and index it hands the solver;
the GPU solver resets itself after a non-finite step (`cloth_step.comp:868-892`).

---

## Findings, most severe first

### CL-1: time dropped by the step cap leaves the solver on a stale pose, so the next step sees a pose jump
- bug, **med**, confirmed (mechanism); visible effect plausible
- `codemp/rd-common/tr_cloth.cpp:737-792` (alpha at 745, drop at 790, `Mprev = M` at 792)
- Each catch-up step is driven by a pose lerped to the step's end time,
  `alpha = 1 - (acc - step) / frame`. When the frame is longer than `r_clothMaxSteps` steps (4 steps
  = 66.7 ms at 60 Hz), the loop stops early and line 790 discards the remaining time, but line 792
  still stores the frame's final pose as `Mprev`. The solver's own state is at the pose of the last
  step it ran, not at `M`.
- Worked case: a 200 ms gap gives alpha 0.083, 0.167, 0.25, 0.333. The solver stops at 33 % of the
  pose change; the first step of the next frame then moves the body, the collider and the capsules
  through the other 67 % (133 ms of animation) in one 16.7 ms step.
- Triggers: any frame between 67 ms and 500 ms (streaming or load hitches), and, more often, any
  character that was frustum-culled for under 500 ms and comes back. `R_GCullModel` returns before the
  cloth front end runs (`rd-vulkan/tr_ghoul2.cpp`, around 3674), so `lastTime` and `Mprev` go stale
  while it is off screen; `CLOTH_RESET_GAP_MS` (500) only resets beyond that.
- Consequence: a limb can cross the sheet in one step. Contact tracking limits how deep a particle
  can be lost, but `sidedContacts` is off by default, so a particle swallowed from the far side is
  pushed out the wrong way. Expect snags or a skirt left inside a leg after a camera whip or a hitch.
- Fix: when the cap is hit, spread the steps the solver does run over the whole frame
  (`alpha_k = k / n`, last step on `M`) so the pose it ends on is the pose stored in `Mprev`; the
  dropped time then only slows the cloth, as the comment at 790 intends. Alternatively keep the
  solver's last pose as `Mprev`.

### CL-2: the solver's own teleport clamp is far below the speed range the glue treats as normal motion
- bug, **med**, confirmed (thresholds); visible effect plausible
- `codemp/rd-common/cloth/skirt_sim.h:74` (`maxFrameVel 2000`, cm/s), `skirt_sim.c:1484-1486`,
  `1505-1518`; `codemp/rd-common/tr_cloth.cpp:44-50`
- The glue resets only above 96 units + 2000 units/s and says player motion "stays under ~2000 u/s".
  At the pipeline's 2.667 cm per unit (`tools/cloth/pipeline/asset/common.py:12`) that is 5,333 cm/s.
  The solver separately zeroes all felt motion (`Vs`, `Os`, fictitious forces, model B's boundary
  transform) once the pelvis exceeds 2,000 cm/s, which is 750 units/s.
- 750 u/s is reached after 0.94 s of free fall at JKA gravity (a 350-unit drop), and by knockback
  or Force-speed jumps. From that frame the garment is carried rigidly with the pelvis: the lift
  from the fall disappears mid-air and returns when the speed drops again. Only the aero term keeps
  seeing the raw velocity. With `r_clothProfile 0` (no `maxFeltAcc`) the transition back is a
  one-step change of the felt displacement of about 0.7 * v * dt (over 20 cm).
- Fix: derive `maxFrameVel` from the glue's limit (`CLOTH_MAX_SPEED * unitCm`, set per instance at
  create) so one threshold defines "teleport", and let the glue's reset be the only hard cut.
  `maxFrameAngVel` 30 rad/s deserves the same check against spin moves.

### CL-3: a portal or mirror view can switch a character's cloth off for the frame
- bug, **med**, plausible (same failure the sun-cascade fix addressed; portal path not run)
- `codemp/rd-common/tr_cloth.cpp:993-999`; `codemp/rd-vulkan/tr_ghoul2.cpp:3759-3766`
- `R_ClothAddGhoul` clears `ci->ready` on entry and returns early when the entity is further than
  `r_clothMaxDist` from `tr.viewParms.ori.origin`. It is called once per view. 18b397b9 excluded sun
  cascade views because exactly this cleared the instance ("the skirt drew unsimulated"), but every
  other secondary view still runs it. A portal whose camera is more than 1,500 units from a
  character that is close to the player leaves `ready` false after the main view set it; the back end
  then draws that character's garment skinned in every view of the frame.
- Fix: decide once per frame per instance. Keep `ready` if it was set this frame
  (`drawTime == now` and a stored `tr.frameCount`), and test the distance against the main view
  only, or take the minimum over views.

### CL-4: GPU cloth is dropped for a frame whenever the back end starts before the front end has queued
- bug, **med**, plausible (depends on a mid-frame render-command flush)
- `codemp/rd-vulkan/vk_cloth.cpp:1258` (`drawnFrame != clothVkFrame` returns qfalse), `1191-1198`;
  `codemp/rd-vulkan/vk_frame.cpp:600`
- Step and draw records are recorded once, in `R_ClothVkBeginFrame`. If the frame's command list is
  flushed before the scene's front end ran (a shader, image or model registered mid-frame issues the
  pending render commands), the records queued afterwards wait for the next frame and
  `RB_ClothVkSurface` refuses the instance: the garment draws from the GLM's skinned bind shape for
  that frame. That is a one-frame snap to the unsimulated shape on exactly the frames where
  something loaded.
- Fix: the render buffer still holds the last frame's vertices, and they are stored in the reference
  bone's bind space and placed by the GPU with the current pelvis. Use it as the fallback: accept any
  buffer that has been drawn since the last reset instead of requiring this frame's serial.

### CL-5: `r_clothDrawLead 1` draws a discontinuous position at every step, visible when fps is not the step rate
- bug (frame-rate dependence), **med**, confirmed (mechanism; the +18 % jerk at 240 fps is already
  recorded in `docs/renderer-vulkan-in-xenjk.md`, Install 33)
- `codemp/rd-common/tr_cloth.cpp:801-807`, `835`; `shaders/glsl/cloth_draw.comp:41-45`
- With the lead on, the draw is `A + t (B - A)` with t in [1, 2): a linear extrapolation of the last
  two step states. Just before a step the drawn position is `2B - A`; just after, it is the new state
  `C`. The jump is the second difference `C - 2B + A`, i.e. acceleration * dt^2, or the full velocity
  change * dt at a contact (a 100 cm/s reversal is 1.7 cm).
- At fps equal to the step rate the phase is constant and nothing shows. At 144 or 240 fps the
  cloth saw-tooths between steps, and extrapolated particles can be drawn inside the body the solver
  just cleared. This conflicts with the "visuals must be fps-independent" rule, and it was accepted
  only as a trade against the knee-through-skirt lag.
- Fix: remove the lag at its source instead of extrapolating. Each frame, run one tentative
  step of length `acc` from the committed state to the current pose and draw that; the committed
  fixed-step state stays untouched. On the GPU that is one extra step dispatch into a scratch slot.
  Cheaper stopgap: keep the lead but blend the correction in over the next step instead of applying
  it at once.

### CL-6: the documented debugging loop does not work for the default solver or for current assets
- quality, **med**, confirmed
- `codemp/rd-common/tr_cloth.cpp:754-767` (GPU branch `continue`s before `Cloth_DumpStep` at 785),
  `638-645`; `tools/cloth/skirt_replay.c:316` (`ver != 1` rejected), `337` (unchecked `TAKE`),
  `511-512`; `tools/cloth/CMakeLists.txt:18`
- `r_clothDump` writes nothing while `r_clothGpu 1` (the default): no warning, an empty or missing
  file. `skirt_replay` carries its own copy of the asset reader that accepts only SKA version 1, has
  no bounds checks, and ignores the SKA2 parameter block; `--mats` replay also starts from solver
  defaults, not from `r_clothProfile 1`, so its `game_maxdiff` column is wrong unless every profile
  and asset knob is passed with `--set`. SKA2 arrived with Vincent's cape (Install 16 record in
  `docs/renderer-vulkan-in-xenjk.md`); garments built that way cannot be replayed at all.
- No tool compares the GPU solver with the CPU solver at all, although the shader is a hand-written
  mirror of about 1,000 lines.
- Fix: link `cloth_asset.c` into `skirt_replay` and delete `loadSka`; add `--profile N` and apply the
  asset parameters the way `Cloth_ApplyOverrides` does; make `r_clothDump` either force the CPU solver
  for the dumped instance or print why it is inert. For GPU parity, add a debug readback of the REL
  slot (one small copy per step) and compare it with the CPU solver on the same record.

### CL-7: the CPU fallback is expensive and runs on the render front end
- perf, **med**, plausible (cost estimated, not measured)
- `codemp/rd-common/cloth/skirt_sim.c:38-441`, `764` (`fd_atan2` in double per hinge),
  `2122-2224`; `codemp/rd-common/tr_cloth.cpp:13-14`, `768-770`
- The CPU solver is taken when `r_clothGpu 0`, when GPU setup fails, when an asset has more than 16
  capsules or needs more than 256 constraint colours, and for the knobs the kernels lack. It runs
  serially inside `R_ClothAddGhoul`. Per step it does 2 substeps * 2 iterations of all edges, all
  dihedral hinges (each with a double-precision fdlibm `atan2`, kept for bit parity with the
  JavaScript reference that the float game build cannot have anyway), all triangles, plus the body
  contact search. For a 2,600-particle sheet (about 7,500 edges and hinges) I would expect a few
  milliseconds per step per character, times up to 8 instances and up to 4 catch-up steps.
- Fix, in order of return: (1) compile the fdlibm block only under a `SKIRT_JS_PARITY` define used by
  the replay tool, and use `atan2f` in the game; (2) default the fallback to `bendModel 1` (springs)
  unless the asset asks otherwise; (3) move step 2 to a worker, which the header comment already
  describes as the intended seam; (4) measure with `skirt_replay --stats` once CL-6 is fixed.

### CL-8: GPU step cost lands on step frames only, and the iteration stage uses one workgroup
- perf, **med**, plausible (docs record 2.5 to 3.3 ms per step; distribution not measured)
- `codemp/rd-vulkan/vk_cloth.cpp:1066-1097`, `1216`; `shaders/glsl/cloth_step.comp:399-518`,
  `739-820`
- At fps above the step rate, most frames record three draw dispatches and some record a full step.
  With a 2.5 to 3.3 ms step that is a 2 to 3 ms GPU spike on roughly every second or third frame at
  144 fps, which is frame-time variance rather than average cost.
- The constraint iterations run in a single workgroup of at most 1,024 invocations with a
  `barrier()` after every colour of every set: with about 10 edge, 16 hinge and 10 triangle colours
  that is over 140 barriers per step, and the rest of the GPU idles meanwhile.
- Options: split a step's substeps across consecutive frames when fps allows; give the hinge set a
  Jacobi-with-averaging pass in the wide pipeline (no colours, whole GPU) and keep Gauss-Seidel for
  edges only; reduce colours with a largest-degree-first ordering in `Cloth_VkColour`
  (`vk_cloth.cpp:630-668` colours in input order). Measure before choosing.

### CL-9: `r_clothParam reset` and `r_clothBend -1` restore table defaults, not the asset's or profile's values
- bug, low, confirmed
- `codemp/rd-common/tr_cloth.cpp:891-895`, `876-877`
- After `r_clothParam "linVelScale 0.5"` then `reset`, the three axes go to the table's 0.7 / 0.7 /
  0.9. Profile 1 sets `linVelScaleZ 0.2` specifically to stop the back panel flipping up on run
  starts, so a reset silently reintroduces that. `r_clothBend -1` likewise writes the table default
  over a `bendModel` the asset's parameter block set.
- Fix: use `Cloth_BaseValue` for each of the three `linVelScale*` keys and for `bendModel`.

### CL-10: CPU non-finite detection samples one float in 97 and the header promises a reset the solver does not do
- bug, low, confirmed
- `codemp/rd-common/cloth/skirt_sim.c:2623-2624`; `skirt_sim.h:226`
- `for (i = 0; i < N * 3; i += 97)` checks about 1 % of the state. On a small sheet (under 33
  particles) it checks only `x[0]`, which is normally a pinned particle and never goes bad. A NaN
  confined to a disconnected patch can also be missed. The header says the step after a failed state
  resets, but `SkirtSim_Step` only sets `stats.nan`; the reset is the glue's (`tr_cloth.cpp:778-783`).
- Fix: check every dynamic particle (it is one pass over data already in cache), and set
  `ready = 0` in the solver so the contract in the header holds for the replay tool too.

### CL-11: a parameter rebuild mid-frame queues ordinary steps on a zero-filled GPU state
- bug, low, confirmed (reachable only through `r_clothParam` on eps_kin, massScale, tetherK, geodesic)
- `codemp/rd-vulkan/vk_cloth.cpp:889-901`; `codemp/rd-common/tr_cloth.cpp:754-766`
- On a `buildSerial` change the buffers are freed, `ci->live` is cleared and `true` is returned, but
  `Cloth_Advance` keeps looping. A second catch-up step in the same frame recreates the buffers
  (state all zero) and queues a non-reset step; the back end runs it and draws it before the next
  frame's reset. One frame of collapsed cloth at the pelvis origin.
- Fix: return a distinct "restart" result and make `Cloth_Advance` stop stepping for the frame.

### CL-12: a collider vertex with no triangle is read out of range and can capture a particle's tracking
- bug, low, plausible (needs an asset with an unreferenced collider vertex; the repo's builder
  compacts to used vertices, but drops degenerate triangles afterwards)
- `codemp/rd-common/cloth/skirt_sim.c:1779` (`vtl[vts[vw]]` without `vts[vw] < vts[vw+1]`),
  `1663-1669`; `shaders/glsl/cloth_step.comp:279`; `codemp/rd-common/cloth/cloth_asset.c:94`
- `seedTracking` picks the nearest of all collider vertices. One with an empty triangle list has an
  empty neighbour list too, so the walk never leaves it, and the "clearly outside" test reads the
  first triangle of the next vertex, or one word past `vtList` for the last vertex. The particle
  then never finds deep contacts through tracking.
- Fix: reject assets with unreferenced collider vertices in `ClothAsset_Parse` (one counting pass),
  or skip them in the seed and guard the read.

### CL-13: pinned particles are placed differently by the CPU and GPU draw paths
- bug (CPU/GPU divergence), low, plausible
- `codemp/rd-common/tr_cloth.cpp:819-832` against `shaders/glsl/cloth_draw.comp:41-45`
- The CPU draw skins pinned particles with the current pose. The GPU draw takes every particle,
  pinned ones included, from the blended step states. A dynamic render vertex wrapped on a triangle
  with pinned corners therefore sits on the current pose on the CPU and on the extrapolated step pose
  on the GPU, while its fully pinned neighbour is skinned by the GPU with the current pose. For pins
  weighted to bones other than the pelvis (thigh, spine) the first dynamic row can shear against the
  pinned row: second order with the draw lead on, up to one step of limb motion with it off.
- Fix: in the draw's POS stage, skin pinned particles from this frame's matrices (they are already
  in the frame buffer for the step) instead of blending REL.

### CL-14: asset validation gaps that end in permanent resets or silent mis-binding
- bug, low, confirmed (parser) / plausible (stale sidecar)
- `codemp/rd-common/cloth/cloth_asset.c:45` (`unitCm` accepts +inf), `59-65` (no finite checks on
  `pos`, `maxDist`, `refInv`, weights), `105-106` (signed overflow summing u32 counts);
  `codemp/rd-common/tr_cloth.cpp:178-182`
- A NaN `maxDist` or a singular `refInv` gives a state that goes non-finite every step, so the
  instance resets every step and the garment hangs frozen in its skinned pose with only a developer
  print. The surface bind checks names and vertex counts only: a `.cloth` built for an earlier export
  of the GLM with the same counts but a different vertex order binds and draws garbage.
- Fix: finite checks in the parser (cheap, load time); sum the counts in 64 bits; at bind, compare
  each wrapped render vertex's rest position with the GLM vertex and refuse above a tolerance, or
  store a hash of the GLM's LOD 0 vertex data in the next asset version.

### CL-15: no hysteresis on `r_clothMaxDist`, simulation continues for LODs that never draw it, resets show the unsettled pose
- perf / visual, low, confirmed (control flow)
- `codemp/rd-common/tr_cloth.cpp:999`, `637-652`; `codemp/rd-vulkan/tr_ghoul2.cpp:3763` (cloth
  front end before `G2_ComputeLOD` at 3772); `codemp/rd-vulkan/vk_cloth.cpp:1252` (LOD 0 only)
- A character hovering at 1,500 units alternates between simulated and skinned garment frame by
  frame. A character drawn at LOD 1 or lower is still stepped (CPU or GPU) although only LOD 0
  surfaces take the result. After any reset the garment starts from the skinned shape and visibly
  relaxes for a few tenths of a second.
- Fix: enter and exit distances (for example 1,400 / 1,600); skip the advance when the chosen LOD is
  not 0 (compute the LOD first); on reset run a short hidden settle (8 to 16 steps on the reset pose)
  before the first draw.

### CL-16: C and GLSL layout agreement is by comment only
- quality, low, confirmed
- `codemp/rd-common/tr_cloth_local.h:16` (`CLOTH_GPU_MAX_CAPS 16 // = CL_MAX_CAPS`);
  `codemp/rd-vulkan/vk_cloth.cpp:50-57` (`clothVkVert_t`), `859`, `915-965`;
  `shaders/glsl/cloth_layout.h:76-78`, `148-155`
- Nothing checks `sizeof(clothVkVert_t) == CL_RV_WORDS * 4`, the field offsets the draw shader
  writes (pos 0, normal 4, tangent 12 words), `CLOTH_GPU_MAX_CAPS == CL_MAX_CAPS` (the glue's stack
  arrays at `tr_cloth.cpp:629` are sized by one, the record by the other),
  `CLR_CAPNEW == CLR_CAPOLD + CL_MAX_CAPS * 6`, or `CLR_MATS` as the record's fixed size. They all
  hold at this commit. The step record itself is about 60 hand-written `F()` / `U()` lines.
- Fix: `static_assert`s next to the struct and in `vk_cloth.cpp`; define the capsule-dependent
  offsets from `CL_MAX_CAPS` in the header; consider an X-macro for the record so the C writer and
  the offsets come from one list.

### CL-17: `skirt_sim.c` carries three copies of the frame logic and code the game does not need
- quality, low, confirmed
- `codemp/rd-common/cloth/skirt_sim.c:2529-2557` (`Reset`), `2559-2626` (`Step`), `2676-2746`
  (`StepFrame`, "keep the three in step"), `1390-1406` and `2657-2669` (capsule frame transform
  twice), `38-441` (fdlibm), `1106` (`(void)nt`), `S->bary` / `S->dv` as hidden return values
- The frame part exists once per entry point, and the GLSL mirrors the particle part again. A change
  to the felt-motion maths has to be made in up to three C places; a change to a constraint in C and
  GLSL. `skirt_stats_t` has fields that are never written (`yawRate`, `maxStretch`, `ms`).
- Fix: make `SkirtSim_Step` = `SkirtSim_StepFrame` + a particle step that consumes `skirt_frame_t`,
  so the CPU solver takes the same inputs the GPU record does (that also makes CPU/GPU comparison a
  matter of feeding both one record); split the file into params / build / frame / particles /
  collision; return closest-point results by out-parameter.

### CL-18: hand capsules and the body mesh are tested against particles only, at substep instants
- suggestion, low, plausible
- `codemp/rd-common/cloth/skirt_sim.c:1853-1882`, `1688-1850`;
  `tools/cloth/pipeline/asset/build_cloth_asset.py:186-192` (finger radius 1.0 cm, hand 1.6 to 2.8)
- A finger capsule of 1 cm (+0.5 thickness) is thinner than the particle spacing of a 2,600-particle
  sheet, and a hand at 5 m/s moves 4 cm per substep, so capsules pass between particles and between
  samples. Likewise knees and elbows sharper than the sheet's resolution come through between
  particles (the accepted "knee or flap in a jump tuck" note in the Install 33 record).
- These are constraints on the real body, in line with the owner's rule; the gap is sampling. Options:
  test each particle against the capsule swept from its old to its new ends; add the sheet's edge
  midpoints as extra collision samples for the body; scale `thickness` by local edge length.

### CL-19: per-entity asset lookup is a linear case-insensitive string scan over every model ever seen
- perf, low, confirmed (cost small)
- `codemp/rd-common/tr_cloth.cpp:83-88`
- `clothAssets` gets an entry for every Ghoul2 model drawn, including all those without a sidecar,
  and is scanned with `Q_stricmp` for every Ghoul2 model in every non-shadow view. With a few
  hundred models seen and a few dozen entities that is some thousands of string compares per frame.
- Fix: cache the asset pointer (or a "none" marker) on the `model_t`, or key a hash map by pointer.

### CL-20: instance limit evicts by last-seen time only
- suggestion, low, confirmed
- `codemp/rd-common/tr_cloth.cpp:458-467`
- With 8 garments on screen a ninth never simulates, however close it is, because no instance has
  been unseen for a second. Within the solo scope this matters only for crowded scenes.
- Fix: when full, evict the furthest instance if the newcomer is nearer.

### CL-21: per-frame heap churn in the GPU back end and topology export per step
- perf, low, confirmed (cost small)
- `codemp/rd-vulkan/vk_cloth.cpp:1140`, `1180` (a vector of vectors per frame), `887-888`
  (`SkirtSim_Topology` per queued step only to read `buildSerial`), `917-918` (`pending.resize`)
- Fix: keep the stage lists as members and clear them; expose a `SkirtSim_BuildSerial` accessor.

### CL-22: the checked-in SPIR-V is not tied to its GLSL
- suggestion, low, confirmed
- `tools/cloth_spirv.py:55-60`; `codemp/rd-vulkan/shaders/spirv/cloth_shader_data.c`
- The array is regenerated by hand. At this commit the shader and the array were last changed
  together (a84a83f6), and `cloth_layout.h` / `cloth_draw.comp` earlier (ffc10a5d), so they agree
  now. Nothing would catch an edit to `cloth_layout.h` without a rebuild of the array, and the C side
  would pick up the new offsets while the shader kept the old ones.
- Fix: have the tool write a hash of the three source files into the generated file, and compare it
  in a CMake step or a `static_assert` against a hash constant emitted alongside `cloth_layout.h`.

---

## Needs a high-effort follow-up

1. **CL-1, CL-2 and CL-5 together**, in the game: they are the three places where what is drawn
   depends on frame pacing or speed. Each needs a scripted reproduction (camera whip past a running
   NPC; a 400-unit fall; 60 against 144 against 240 fps on one demo) with per-frame particle-inside-body
   counts, on both solvers, before and after.
2. **GPU against CPU parity.** There is no measurement of how far the two solvers diverge on the same
   input. It needs the REL readback and record replay described in CL-6; without it CL-13 and any
   future shader edit are judged by eye.
3. **CL-3 and CL-4** need the engine's call order confirmed on a map with a portal surface and with
   a mid-frame registration. I read the cloth side only.
4. **Step cost (CL-7, CL-8)** needs profiling on this PC and on the 6 GB 3060 laptop target. The
   shared-memory caps are derived from device limits (`vk_cloth.cpp:524-530`): the float cap of 3,754
   particles and the packed cap of about 5,600 in the docs imply 48 KB of compute shared memory; a
   device reporting 32 KB gives 2,389 and 3,584, so sheets tuned to "under 3,700" would take the slow
   path there.

## Not covered

- `tools/cloth/blender/jediacademy/*` (the Blender add-on) and the `pipeline/model` and
  `pipeline/skeleton` scripts: listed, not reviewed. Of `pipeline/asset` only `skaio.py`, `common.py`
  and the collider, capsule and writer parts of `build_cloth_asset.py` were read.
- `compare_c_js.mjs` beyond its first 60 lines, and the SKD1 and SKP1 modes of `skirt_replay.c`.
- `rd-vanilla/tr_cloth_rb.cpp` and `docs/cloth/FORMAT.md` (format not checked against the parser).
- The SKA2 builders: nothing under `tools/` at this commit writes `render_wrap`, so the tools that
  produce the installed garments are outside the tree I was given.
- Numerical behaviour: no solver run, no stability sweep over `r_clothHz`, no timing. The fdlibm
  port was not checked against a reference.
- Scaled entities: the glue normalises the entity axes (`tr_cloth.cpp:538-542`), so a model drawn
  with a scaled axis is simulated at unscaled size and felt velocities ignore the scale. Not analysed
  further.
- First person: the player's own garment is not simulated (`tr_cloth.cpp:998`), so its shadow is
  the skinned shape; noted, not rated.
- Cloth shading, the SimCloth marker in the lit shaders and self-shadow exclusions (shading lane).
