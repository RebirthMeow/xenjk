# Hair and cloth: one decision per frame, and steps that stay on the pose (2026-11-09)

Branch `claude/hair-cloth-views-295e8b`, on 6fe7bb2f (`renderer/install68-models`). Not merged, not installed.
Source review: `.codex-research/review-20261109-SUMMARY.md` and `review-charrender-20261109/{hair,cloth}.md`
(findings HR-1, HR-2, HR-8, CL-1, CL-3).

## What was wrong (confirmed on this branch by reading, line numbers at 6fe7bb2f)

| id | finding | where | status |
|---|---|---|---|
| HR-2 | The main view culls a character before the hair front end runs, shadow views skip the front end, and nothing clears `ready`. A character at the screen edge casts a shadow from the hair palette of the last frame it was on screen, for up to 5 s. | `rd-vulkan/tr_ghoul2.cpp:3674-3677`, `:3761-3766`; `rd-common/tr_hair.cpp:868`; `rd-vulkan/vk_hair.cpp:289-307` | confirmed, and counted in game (below) |
| (cloth twin of HR-2) | The same stale `ready` for cloth. The GPU solver's draw refuses a buffer that was not dispatched this frame, so the shadow shows the unsimulated garment and snaps at the screen edge. The CPU solver re-uploads the stale draw buffer. | `rd-vulkan/vk_cloth.cpp:1247-1268` | confirmed, and counted in game |
| HR-1 / CL-3 | `ready` is cleared by every non-shadow view and the distance is measured from that view's camera. A portal or mirror view runs after the main view (`R_MirrorViewBySurface` from `R_SortDrawSurfs`), so a portal camera over 1500 u from a character near the player turns its hair rigid and its cloth off in every view. | `tr_hair.cpp:868,874`; `tr_cloth.cpp:993,999`; `rd-vulkan/tr_main.cpp:1204,1566` | confirmed as a mechanism; **not reachable on any installed map** (below); not run in game |
| CL-1 | When more than `r_clothMaxSteps` steps are owed, the steps taken cover the first part of the frame, the rest of the time is dropped, and `Mprev = M`. The solver stopped at the last step's pose, so the next step moves the body through the whole dropped interval in one 16.7 ms step. | `tr_cloth.cpp:743-792` | confirmed, reproduced offline and in game |
| HR-8 | The same in hair (`Wprev`, `Hprev`, `Gprev` set to the frame's end). `HC_BeginFrame` divides that displacement by one `dt`, so the next step also measures a frame velocity several times the real one. Spring bones drop time the same way. | `tr_hair.cpp:612-647`, `:682-727`; `hairchains/hair_sim.c:239,283,292` | confirmed, reproduced offline and in game |
| (new) | Cloth lerps the **world** matrices of the two frames for a step's pose. Across a long frame of a spinning body each bone's origin cuts its own chord and the limbs fold toward the axis: in the replay below the collider moved up to 86 cm in the pelvis frame in one step with nothing dropped at all. | `tr_cloth.cpp:747-752` | found while measuring CL-1; fixed with it |

A short off-screen spell (under the 500 ms reset gap) is the common trigger for CL-1 and HR-8, not only a hitch:
the cull returns before either front end, so `lastTime` goes stale and the first frame back owes the whole gap.

**Installed maps (HR-1 / CL-3).** A scan of every pk3 and the loose folders in `GameData/base` (442 packs, 153 BSPs,
none skipped) found seven stock portal shaders (`textures/system/mirror1`, `mirror_yavin`, `portal`, `portal_512`,
`textures/tests/glass_portal`, `mirror_floor`, `mirror_floor2`) and no map that references one, and no
`misc_portal_surface` or `misc_portal_camera` entity. So the portal case cannot happen in the installed game today.
It is fixed anyway because the fix is the same code. Not covered by the scan: the 21 `.zip` files and `base/mods`,
model surfaces, and one truncated pack (`ctf_Basilisk.pk3`).

## Design

One pattern, shared by hair and cloth. The shared parts are `codemp/rd-common/sim_frame.h` (plain C, header only, so
the offline tools run the game's own step plan) and `codemp/rd-common/tr_sim.h` (the cvars and the check counters).

### 1. One decision per frame per instance

- The instance carries a stamp (`tr.frameCount`, `tr.frameSceneNum`) of the frame its draw data was built for.
- **Every view calls the front end, shadow views included.** The first view of the frame that draws the character
  decides, steps and builds the draw data. Every later view of that frame finds the stamp and returns: it neither
  clears nor rebuilds.
- A view that does not draw the character (the first-person body in the main view) decides nothing, so a mirror
  that does draw it still can.
- **Distance is measured from the scene's camera** (`tr.refdef.vieworg`), the same for every view of the scene. A
  portal, mirror or shadow camera never changes the answer.
- A small exit margin on the distance (in at `r_*MaxDist`, out 1/16 beyond it) stops a character at the limit from
  alternating frame by frame (part of CL-15 / HR-15; the fade is not done).
- **The back end only draws data stamped with its own frame.** A character no view reached this frame draws rigid
  hair and the skinned garment. A stale palette or draw buffer can no longer be drawn, whatever the front end did.

### 2. Shadow-only characters are stepped

A character the main view culled but a sun cascade or character shadow view draws is simulated by that shadow view,
at the same cost as when it is on screen. Chosen over drawing a rest pose because:
- its shadow is on screen, so a rigid or unsimulated shadow next to a simulated one is the same visible snap;
- it removes the off-screen gaps that feed CL-1 / HR-8 wherever shadows are drawn, and the "camera turned away for
  half a second, hair resets" cases (HR-6);
- the cost is bounded by `r_hairMaxInstances` / `r_clothMaxInstances` (8) and the distance limit.

Characters that no view draws are still not stepped.

### 3. Steps owed are carried, not dropped

`Sim_PlanSteps` (`sim_frame.h`) replaces the three hand-written accumulator loops (hair chains, hair spring bones,
cloth). When a frame owes more steps than the cap allows:

- **The steps run as usual, one step of pose time each, and what is still owed is carried.** The stored previous
  pose becomes the pose of the last step run, so the next frames continue from exactly where the solver is and catch
  up, the cap's worth a frame. Nothing is dropped and no step moves the body more than one step of animation.
- **How much may be carried** is `r_simCarryFrames` (4) times what a normal frame can repay: the steps it may run
  less the time it takes, with the shorter of this frame and the one before it standing for "normal". A 200 ms hitch
  at 144 fps is caught up in the next frame (hair) or the next three (cloth). Under a lasting shortage (frames
  longer than the cap's worth of steps) nothing is carried, so the solver never settles behind the pose.
- **Only what is owed beyond that is given up**, by stretching this frame's steps over it. Each then covers
  `poseDt > dt` of animation and still integrates one `dt`. **The solver is told** (`HC_BeginFrameDt`,
  `SkirtSim_SetPoseDt`): it measures the frame's linear and angular velocity over `poseDt`, so they stay the real
  ones instead of `poseDt / dt` times them. Cloth's model B boundary displacement stays `felt velocity x dt`: over
  the stretched time the garment is carried with the body. With no pose time given both solvers compute exactly
  what they did (replays of old dumps are bit-identical, below).
- **Cloth's step pose is lerped in the pelvis frame** (`SkirtPose_Lerp`): the reference bone's frame is lerped, every
  bone is lerped in that frame, then put back. Hair already lerped the entity and the model-space bones separately.
- A stretched step's `poseDt` is rounded to whole microseconds and written into the hair and cloth dumps (the flags
  word, bit 8 up), so a dump with stretched steps still replays exactly; `hcd_to_hcr.py`, `hc_replay` and
  `skirt_replay --mats` read it.

Not capped and not behind, the plan gives the same numbers as the loops it replaces.

### Cvars (shared by hair and cloth, registered with cloth)

| cvar | default | meaning |
|---|---|---|
| `r_simPerFrame` | 1 | 1: the per-frame decision above. 0: as before (per view, shadow views skipped, no frame stamp). |
| `r_simSteps` | 1 | 1: carry and stretch, pose time to the solvers, cloth's pelvis-frame lerp. 0: as before (time dropped at the frame's end). |
| `r_simCarryFrames` | 4 | Catch up over about this many frames; 0 = at once (every capped frame stretched over its steps). |
| `r_simCheck` | 0 | Print `SIMCHECK` counters every N seconds, and whenever it is set to another value (a scripted check closes its window that way): draws with data older than the frame, capped and stretched frames, the most pose time and input move per step, particles inside the body. |

## What changed

- `rd-common/sim_frame.h` (new): the step plan, the frame gate, the distance test, the dump flag packing.
- `rd-common/tr_sim.h` (new): cvars and `r_simCheck` counters; defined in `tr_cloth.cpp`.
- `rd-common/tr_hair.cpp`, `tr_cloth.cpp`: the per-frame gate in `R_HairAddGhoul` / `R_ClothAddGhoul` (new
  `shadowView` argument), the plan in `Hair_Advance`, `Hair_AdvanceSprings`, `Cloth_Advance`; spring bones keep their
  own previous pose. A GPU cloth step that made the back end start over now ends the frame's steps (CL-11).
- `rd-vulkan/tr_ghoul2.cpp`: both front ends are called for every view. `rd-vanilla/tr_ghoul2.cpp`: the new argument.
- `rd-vulkan/vk_hair.cpp`, `vk_cloth.cpp`, `rd-vanilla/tr_cloth_rb.cpp`: draw only this frame's data.
- `rd-common/hairchains/hair_sim.{c,h}`: `HC_BeginFrameDt`, `HC_CountInside` (diagnostic).
- `rd-common/cloth/skirt_sim.{c,h}`: `SkirtSim_SetPoseDt`, `SkirtPose_Lerp`, `SkirtSim_CountInside` (diagnostic).
- `tools/hair/hc_frames.c` (new), `hc_reel.h` (new, shared with `hc_replay.c`), `hcd_to_hcr.py`;
  `tools/cloth/skirt_replay.c --frames`; `tools/hair/simcheck_run.py` (new, the in-game check).

## Verification

### The solvers are unchanged where nothing is stretched

- `hc_replay` built from this branch against HEAD's, same reels: outputs identical (`cmp`) on the in-game Tifa dump
  (`m3b_dump.hcr`, 412 steps, game feel) and a Route-A reel (`turn.hcr`, 183 steps).
- `skirt_replay --mats` from this branch against HEAD's on the in-game Eve dump (`passD.cld`, 2,939 steps, profile 1):
  positions identical (`cmp`).

### Offline: the dropped-step case

Both tools run the game's own `Sim_PlanSteps` over recorded motion at a chosen frame rate with long frames in it.
A long frame stands for a hitch and for an off-screen spell alike: the front end sees the same thing. Every run is
compared with the same frames stepped with no cap ("no limit": nothing dropped, nothing carried).
`legacy` = `r_simSteps 0`; `spread` = `r_simSteps 1` with `r_simCarryFrames 0`; `carry` = the defaults.

**Hair** (`hc_frames`, Tifa's game sidecar, the in-game motion demo `m3I21.hcr`, 13.7 s, game feel and flow, cap 4):

| frames | mode | time dropped | most pose time in one step | solver's frame velocity, times the real one | drawn state against no limit, from 100 ms after each long frame: rms / max |
|---|---|---|---|---|---|
| 144 fps, a 200 ms frame every 1.5 s (8) | legacy | 533 ms | 100.0 ms | 3.00 | 2.71 / 29.8 cm |
| | spread | 738 ms | 58.0 ms | 1.00 | 2.73 / 42.8 cm |
| | **carry** | **0** | **33.3 ms** | **1.00** | **0.63 / 10.3 cm** |
| 144 fps, a 400 ms frame every 1.3 s (10) | legacy | 2,667 ms | 300.0 ms | 9.00 | 3.01 / 30.9 cm |
| | spread | 2,803 ms | 107.3 ms | 1.00 | 2.16 / 47.0 cm |
| | **carry** | **0** | **33.3 ms** | **1.00** | **0.83 / 26.6 cm** |
| 60 fps, a 300 ms frame every 1.3 s (10) | legacy | 1,667 ms | 200.0 ms | 6.00 | 3.37 / 30.6 cm |
| | spread | 1,800 ms | 83.3 ms | 1.00 | 3.23 / 33.4 cm |
| | **carry** | **0** | **33.3 ms** | **1.00** | **1.20 / 22.0 cm** |
| 6.67 fps throughout (every frame 17 ms short) | legacy | 1,500 ms | 66.7 ms | 2.00 | whole run max 43.1 cm |
| | spread = carry | 1,500 ms | 45.8 ms | 1.00 | whole run max 32.7 cm |

One step is 33.3 ms. The animated targets move up to 19 cm in a normal step of this demo; legacy's worst step moved
them 31 cm. Overlapping collision pairs (of 1,165) were about the same in every mode: 960 to 1,059 summed over a run,
deepest 1.4 to 2.5 cm.

**Cloth** (`skirt_replay --frames`, Eve's skirt `npuc.cloth`, 2,831 particles, the in-game demo dump `passD.cld`,
49 s with pirouettes and runs, profile 1, CPU solver, 60 Hz, cap 4):

| frames | mode | time dropped | most pose time in one step | collider's largest move in one step (pelvis frame) | solver's frame velocity, times the real one | particles over 0.5 cm inside the body: mean / worst step | drawn state against no limit, from 100 ms after: rms / max |
|---|---|---|---|---|---|---|---|
| 144 fps, a 200 ms frame every 1.5 s (32) | no limit | - | 16.7 ms | 11.8 cm | 1.00 | 1.03 / 45 | - |
| | legacy | 4,133 ms | 150.0 ms | 91.9 cm | 9.00 | 2.34 / 200 | 9.92 / 61.2 cm |
| | spread | 4,531 ms | 54.2 ms | 28.1 cm | 1.00 | 0.94 / 30 | 6.49 / 67.1 cm |
| | **carry** | **0** | **16.7 ms** | **11.8 cm** | **1.00** | **0.98 / 47** | **4.78 / 61.0 cm** |
| 144 fps, a 400 ms frame every 1.5 s (30) | no limit | - | 16.7 ms | 11.5 cm | 1.00 | 1.20 / 23 | - |
| | legacy | 10,000 ms | 350.0 ms | 66.6 cm | 21.00 | 1.69 / 45 | 10.08 / 63.2 cm |
| | spread | 10,326 ms | 104.2 ms | 29.5 cm | 1.00 | 1.09 / 91 | 9.41 / 68.8 cm |
| | **carry** | **3,032 ms** | **44.4 ms** | **12.7 cm** | **1.00** | **1.23 / 86** | **6.52 / 64.1 cm** |
| 60 fps, a 300 ms frame every 1.5 s (31) | no limit | - | 16.7 ms | 13.5 cm | 1.00 | 1.57 / 34 | - |
| | legacy | 7,233 ms | 250.0 ms | 82.3 cm | 15.00 | 1.34 / 91 | 10.77 / 67.1 cm |
| | **carry** | **1,200 ms** | **29.2 ms** | **13.5 cm** | **1.00** | **1.43 / 37** | **7.02 / 61.4 cm** |
| 10 fps throughout (every frame 33 ms short) | no limit | - | 16.7 ms | 11.0 cm | 1.00 | 0.80 / 33 | - |
| | legacy | 16,167 ms | 66.7 ms | 62.7 cm | 4.00 | 5.02 / 122 | - |
| | **carry** | **16,167 ms** | **25.0 ms** | **15.7 cm** | **1.00** | **1.69 / 52** | - |

One step is 16.7 ms. Cloth's cap is 66.7 ms of steps a frame, so a 400 ms frame at 144 fps is more than four frames
can repay and part of it is stretched (44 ms of pose a step); `r_simCarryFrames 6` would carry all of it. The inside
count is every dynamic particle against the collider, every second step.

Reading:
- The per-step pose jump is the defect itself, and `carry` removes it: every step covers one step of animation
  whenever the gap fits what the frames can repay, and the solver's frame velocity is the real one (it was up to 21
  times it).
- The drawn state after a hitch is closer to the uncapped run in every case. It does not reach zero: the motion is
  fast and both materials are sensitive to small differences, so two correct runs drift apart too. No floor was
  measured for that drift.
- Particles inside the body: cloth's mean falls to the uncapped run's where legacy's was above it (2.34 to 0.98,
  1.69 to 1.23, 5.02 to 1.69); at 60 fps with 300 ms frames all three were alike. The single worst step is noisy (one
  step of one run) and went both ways. Hair's overlapping pairs barely react to any of this.
- A lasting shortage with hitches in it (14 fps and a 200 ms frame every 1.5 s, the state the CPU solver is in for
  Aerith's dress in game): legacy 2.77 mean / 319 worst, carry 1.01 / 27, no limit 1.22 / 47; collider 101.5 cm a
  step against 25.9 cm.
- The pelvis-frame lerp alone accounts for the collider jump with nothing dropped: 86.1 cm a step with the world
  lerp, 11.8 cm with it, on the same uncapped frames.

### In game

**Setup.** `tools/hair/simcheck_run.py`: one launch of the installed `GameData/xenjk.x86.exe` with this branch's
renderer (RelWithDebInfo, fresh build tree `.codex-research/hair-cloth-views-20261109/build`), hidden, muted, no
mouse grab, private home, network off, through the shared game lock. Map `xenjk_showcase` (sun cascades, models
cast), 1280x720, `com_maxfps 125`. The player's own model `aerith` (hair chains: 51 particles, 801 collision pairs;
dress: 3,629 cloth particles) runs a circle in third person; `cg_thirdPersonHorzOffset 700` slides the camera
sideways, which takes the model out of the main view while the cascades still draw it. Each arm sets
`r_simPerFrame` / `r_simSteps` / `r_clothGpu` live; each phase is one `r_simCheck` window. The player moved (32
distinct positions sampled). Nothing was timed. Runs: `.codex-research/hair-cloth-views-20261109/runs/T4` (the tables),
`T2` and `T6` (repeats, same pattern), `T5dump`.

Arms: **before** = `r_simPerFrame 0, r_simSteps 0`; **after** = `1, 1` (the defaults).

**Culled at the screen edge, shadows on** (378 frames, about 3 s):

| arm | hair: frames stepped | hair palettes used / of an earlier frame / oldest | GPU cloth: surface draws simulated / fallen back to skinned | CPU cloth: draws / of an earlier frame / oldest |
|---|---|---|---|---|
| before | 0 | 378 / **378** / 398 frames | 0 / **2,658** | 2,556 / **2,556** / 397 frames |
| **after** | **378** | 378 / **0** / - | **2,388** / **0** | 1,968 / **0** / - |
| per frame only (`1, 0`) | 378 | 378 / 0 / - | 2,544 / 0 | - |
| steps only (`0, 1`) | 0 | 378 / 378 / 398 frames | 0 / 2,574 | - |

Before, every frame's shadow was drawn from the palette of the last frame the character was on screen (HR-2), up
to 3.2 s old, and the dress's shadow was the unsimulated garment. After, the shadow views step the character and
nothing older than the frame is drawn.

**Camera whip** (out of view about 300 ms, back about 500 ms, six times; GPU cloth):

| arm | hair: capped frames / time dropped / most pose time in a step / frame velocity x real | cloth: the same | cloth: collider's largest move in a step | draws of an earlier frame (hair) / fallen back (cloth) |
|---|---|---|---|---|
| before | 6 / 1,000 ms / 200.0 ms / 6.00 | 6 / 1,400 ms / 250.0 ms / 15.00 | 85.0 cm | 222 (oldest 37 frames) / 1,548 |
| **after** | **0 / 0 / 33.3 ms / 1.00** | **0 / 0 / 16.7 ms / 1.00** | **13.9 cm** | **0 / 0** |
| per frame only | 0 / 0 / 33.3 ms / 1.00 | 0 / 0 / 16.7 ms / 1.00 | 13.8 cm | 0 / 0 |
| steps only | 12 / 0 / 33.3 ms / 1.00 | 35 / 44 ms / 21.1 ms / 1.00 | 13.9 cm | 222 / 1,386 |

In view the collider moves up to 13.8 cm a step in this run, so 13.9 cm is the animation itself. Each fix removes
the pose jump on its own here: with shadows on, the shadow views keep stepping and there is no gap; with the gap
(steps only), the steps owed are carried.

**Hitch** (six single frames of about 200 ms; GPU cloth):

| arm | hair: capped / dropped / most pose time in a step / velocity x real | cloth: the same | cloth: collider's largest move in a step |
|---|---|---|---|
| before | 6 / 400 ms / 100.0 ms / 3.00 | 6 / 800 ms / 150.0 ms / 9.00 | 89.8 cm |
| **after** | **6 / 0 / 33.3 ms / 1.00** | **21 / 0 / 16.7 ms / 1.00** | **13.8 cm** |
| per frame only | 6 / 400 ms / 100.0 ms / 3.00 | 6 / 800 ms / 150.0 ms / 9.00 | 79.2 cm |
| steps only | 6 / 0 / 33.3 ms / 1.00 | 18 / 191 ms / 41.3 ms / 1.00 | 17.1 cm |

**Three long frames in a row** (5 fps for about 0.6 s; the frames cannot repay each other, so the steps stretch):
before, hair 100.0 ms a step at 3.00 times the velocity and cloth 150.0 ms at 9.00 times, collider 69.8 cm; after,
hair 35.2 ms and cloth 66.2 ms, both at 1.00, collider 21.4 cm.

**Particles inside the body** (CPU cloth arms, `r_clothGpu 0`; estimated from every 4th particle on every 7th step,
over 0.5 cm inside). Aerith's dress on the CPU solver cannot keep up on this PC (13 to 17 stepped frames a second,
a fifth to a third of them capped), so these arms are a lasting shortage throughout:

| phase | before: mean / worst sample (T4, T6) | after: mean / worst sample (T4, T6) |
|---|---|---|
| in view | 9.1 / 28, 10.7 / 44 | 10.4 / 64, 9.9 / 48 |
| whip | 10.3 / 64, 17.9 / 156 | 8.4 / 44, 9.9 / 48 |
| hitch | 8.9 / 52, 14.0 / 264 | 18.7 / 216, 10.3 / 64 |
| three long frames | 7.8 / 20, 9.2 / 28 | 9.4 / 44, 7.3 / 12 |

**This count does not separate the arms in game.** It is 7 to 19 particles of 3,629 in every arm and phase, and the
two runs disagree with each other by more than the arms differ (the hitch phase went both ways). The offline replay
above, where the motion and the frames are the same for every arm, is the measurement to trust for it. The GPU
solver, which is the default, has no such count at all (its particles stay on the GPU).

Hair's overlapping collision pairs (of 801) were 0.2 to 1.2 on average in every arm and phase, at most 16: no
difference there either.

**A dump with stretched steps replays** (`T5dump`: `r_hairDump` through the hitch phase, then `hcd_to_hcr.py`,
`hc_replay`, `hc_replay_check.py`): 163 steps, 4 of them stretched to 44.2 ms. One step from the recorded state is
within 3.3e-5 units of the game's everywhere and 3.8e-6 on the stretched steps (MSVC against MinGW float noise, as in
[hair/README.md](../../hair/README.md)). The same reel with the pose times zeroed is off by 0.23 units on those steps.

**Builds.** `rd-vulkan_x86` and `rd-vanilla_x86` build without new warnings in the touched files. rd-vanilla was
compiled only, not run. The in-game runs used the build of 17:40; the committed source was built once more after
them (a stretch variant was tried and taken out again, see "What is left"; `rd-vulkan_x86.dll` sha256 `02bd3ad2...`)
and that last binary was not run in game: the lock was held by another session's timing batch.

## What is left

- **Portal and mirror views (HR-1 / CL-3) were not run in game.** No installed map has one. A check needs a test map
  with a mirror brush and a `misc_portal_surface`.
- **Particles inside the body are not settled in game.** The count is too noisy on the CPU solver to compare arms,
  and missing on the GPU solver. It needs the two things the review already asked for (CL-6): a readback of the GPU
  solver's particles, and `skirt_replay` reading SKA2 assets (Aerith's dress is SKA2, so the offline cloth numbers
  are Eve's skirt only).
- **Stretched steps still cover more than one step of animation** when the frames cannot repay what is owed: a
  lasting shortage, or a gap beyond `r_simCarryFrames` (cloth after 400 ms at 144 fps: 44 ms a step). Giving all the
  extra pose time to the frame's first step instead was tried offline and in game and not kept: inside counts were a
  little lower offline (0.77 against 1.01 at 14 fps with hitches) and the same in game, but one step then moves the
  body 164 to 367 ms of animation, which is the jump this work removes.
- **No one has looked at it.** The counts say the jumps and stale draws are gone; whether the catch-up (the cap's
  worth of steps a frame for up to about four frames after a hitch) reads well is the owner's call. `r_simSteps 0`
  and `r_simPerFrame 0` give the old behaviour live.
- **Cost was not timed.** Characters that only a shadow view draws now simulate, within the 8-instance and distance
  limits. No timed runs were made.
- **The solver's own teleport clamp (CL-2) now sees the real velocity.** Before, a dropped frame's velocity spike
  tripped it and the garment was carried rigidly for that step; that no longer happens after a hitch. CL-2 itself
  (the clamp sits at 750 u/s, below real falls) is untouched.
- Untouched: CL-5 (draw lead), the rest of CL-15 and HR-15 (stepping for LODs that do not draw the result, the
  distance fade), HR-6 (settling after a reset), HR-3 (`hc_eval` start numbering). A shadow view can still pick
  another LOD than the main view; cloth draws at LOD 0 only, as before.
- One Ghoul2 instance added to a scene more than once in a frame (force shells, afterimages) is now posed from its
  first add of the frame; before, every add rebuilt the palette and the last one's entity transform was kept for
  the next frame's step.
- `docs/xenjk-cvars.md` is generated and was not regenerated: the four `r_sim*` cvars are listed here and in
  [hair/README.md](../../hair/README.md) only.
