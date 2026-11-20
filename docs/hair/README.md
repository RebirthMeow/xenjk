# Hair chains (rd-vulkan)

Bone-chain hair dynamics for Ghoul2 models that ship a `models/<m>/model.chains` sidecar (format: [FORMAT.md](FORMAT.md)).
It was added 2026-10-18 for FF7 Rebirth Tifa. The solver is a clean-room position-based-dynamics reading of the
source game's bone-dynamics parameters ("Route A"): particles on bone heads, cones, lateral links, capsule/sphere
collision, containment shapes, and a mirror step for chains the source drives by copying a neighbour.

Models without source dynamics data get their sidecar from a rig description, named presets and colliders fitted to
their own mesh: [PRESETS.md](PRESETS.md) (`tools/hair/build_chains.py`, first used for Eve 2B on 2026-10-21).

## Where it runs

- **Solver:** `codemp/rd-common/hairchains/hair_sim.{h,c}` (C99, float); the asset parser is `hair_asset.{h,c}`.
- **Glue:** `codemp/rd-common/tr_hair.cpp`, front end. It is called next to cloth from `R_AddGhoulSurfaces`, for
  every view.
  - One decision per instance and frame (since 2026-11-09, shared with cloth: `rd-common/tr_sim.h`,
    [the record](../cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md)). The first view of a frame
    that draws the model decides and steps, a shadow view included, so a character the main view culled still casts
    its simulated shadow. The distance is measured from the scene's camera. The back end only uses a palette built
    for the frame it draws.
  - It steps each instance at the asset's dt (1/30 s) from scene time, with the inputs lerped between the stored
    previous pose and the frame's. After a long frame or an off-screen moment the steps owed are carried to the next
    frames, `r_hairMaxSteps` a frame (`rd-common/sim_frame.h`).
  - Resets follow cloth: first sight, time backwards, unseen over 500 ms, a teleport, NaN.
  - Each step's particles are kept in the host bone's frame and blended at render rate. The hair bones' skinning
    matrices are then built each frame.
- **Drawing:** `codemp/rd-vulkan/vk_hair.cpp`.
  - The bone palette (`vkUniformBones_t`, `shaders/glsl/global.h`) holds 256 matrices (160 before 2026-10-21; vertex
    bone references are bytes, so 256 is the ceiling): the skeleton's bones, then the hair bones. Each skinned model
    consumes only the bones it writes; the descriptor range is checked against the buffer end.
  - With `r_hairSharedVbo 1` (the default) the listed vertices are patched into the model's own vertex buffer. With
    0 each listed LOD draws from a substitute static VBO built once per model (the old way, one more buffer per LOD),
    the same vertices packed as `R_BuildMDXM` packs them. Either way only the listed vertices are re-pointed at the hair
    palette slots.
  - Every pass (lighting, shadows, depth prepass, the character hair map) draws the simulated hair. Nothing per
    vertex is uploaded per frame.
- **Not supported:** rd-vanilla (hair stays rigid there). `charshadowdump` skins on the CPU and still shows rigid hair.

## Cvars

| cvar | default | meaning |
|---|---|---|
| `r_hair` | 1 | 0 rigid, 1 simulate, 2 posed without simulation (an identity check: draws like 0) |
| `r_hairInverse` | 1 | keep hair inside the asset's containment shapes (0 = push out of them, as the source reading does) |
| `r_hairFeel` | 1 | 1 game profile (the hair feels the entity's runs, turns and jumps), 0 the asset's own weights only |
| `r_hairFeelLin` / `r_hairFeelMaxG` / `r_hairFeelLag` | 1 / 1.5 / 300 | linear inertia weight, most acceleration felt (in g), velocity change carried at most (u/s) |
| `r_hairFeelEuler` / `r_hairFeelAngAcc` | 1 / 9000 | turn drag weight, most angular acceleration felt (deg/s²) |
| `r_hairFeelCent` / `r_hairFeelAngVel` / `r_hairFeelAngLag` | 1 / 720 / 1080 | spin flare weight, most spin felt (deg/s), spin change carried (deg/s) |
| `r_hairFlow` | 1 | 1 damp the cone springs and add air drag (below), 0 the source reading's springs (Install 20) |
| `r_hairFlowDamp` / `r_hairFlowInner` | 2 / 0.5 | damper beside each cone spring (times the asset's constraint damping), cone spring strength (times the asset's) |
| `r_hairFlowDrag` / `r_hairFlowParent` | 3 / 0 | air drag (1/s) on the hair's motion and the entity's run and spin; cones following their parent segment (experimental) |
| `r_hairMaxDist` | 1500 | simulate only within this distance of the scene's camera (out again 1/16 beyond it) |
| `r_hairMaxSteps` / `r_hairMaxInstances` | 4 / 8 | catch-up steps per frame, instances |
| `r_simPerFrame` | 1 | hair and cloth: 1 decided once per frame by the first view that draws the character, shadow views included; 0 per view, as before 2026-11-09 |
| `r_simSteps` / `r_simCarryFrames` | 1 / 4 | hair and cloth after a long frame: 1 carry the steps owed over about that many frames and stretch the rest over the frame, 0 drop the time at the frame's end (as before) |
| `r_simCheck` | 0 | hair and cloth: print `SIMCHECK` counters every N seconds (stale draws, capped frames, pose jump per step, overlapping collision pairs) |
| `r_hairFade` | 0.2 | the drawn hair and spring offsets blend to the animation over the last share of `r_hairMaxDist` (0.2 = the last 20%), so hair does not snap rigid at the limit; 0 = the hard cut |
| `r_hairPreroll` | 12 | solver steps run on the reset pose before the hair's first draw after a reset (spawn, teleport, back in range); 0 = it falls into place visibly |
| `r_hairWorldScale` | 1 | hair on an entity drawn scaled (modelScale) feels gravity and the entity's motion in its own units; 0 = as before (1/scale times too strong) |
| `r_hairSharedVbo` | 1 | 1 patch the hair vertices into the model's own buffer; 0 draw from a copy of each listed LOD (one more vertex buffer per LOD, about 64 bytes a vertex). Latched |
| `r_hairStats` | 0 | print cost every N seconds (`HAIRSTATS`) |
| `r_hairDump` | (empty) | record one instance's steps to this file under the home path (HCD1) |
| `r_hairSprings` | 1 | body physics on sidecars that have it: spring bones (Stellar Blade's body jiggle) and body chains (FF7R's breasts, skirts, ties: particles tagged `HAIR_PARTICLE_BODY`). 0 off (the animated pose), 1 on. With `r_hairStats` a `HAIRSPRINGS` line reports the springs' steps and drawn offsets |
| `r_hairSpringLimit` | 1 | how far body physics may move: a spring bone's limit times this (1 = the source game's: Eve's breasts 1 cm, hips 2 cm), a body chain's drawn offset from its animated pose times this (1 = as simulated); 0 = off, 2 = twice. Takes effect at once |
| `r_hairSpringChest` / `r_hairSpringHips` / `r_hairSpringOther` | 1 / 1 / 1 | the same per body group, on top of `r_hairSpringLimit`: breasts; hips, buttocks and upper thighs; the rest (skirts, ties, accessories, wires, belly, arms). 0 turns that group off. One set of settings for every model, spring bones or chains |
| `r_hairChains` | 1 | 0: the hair chains (hair and what hangs from the head) follow the animation while the body physics still moves (they keep stepping, so turning them back on doesn't jump) |

The game profile exists because the source reading only lets particles feel a small authored share of the root
frame's linear acceleration, and no rotation at all. In JKA, where the entity snaps, that means no response to turns.
The profile works like cloth's: a felt velocity and spin follow the entity's at bounded rates, so a one-frame JKA
velocity change is felt in full but spread out.

The flow settings exist because the source reading's cone pull toward the animated direction adds velocity that
nothing damps. The long locks then ring at 3-4 Hz, which reads as springy. Owner feedback on Install 20 was "a little
springy, not quite as flowing". With the defaults:
- each pull carries a damper along it;
- the springs are half as strong;
- air drag trails the locks behind runs and turns.

On a replayed in-game run the long lock's ringing at rest falls from 6.7 to 2.8 cm (RMS), and its share of motion above
2 Hz from 0.44 to 0.15. It swings and settles instead.

## Spring bones (body jiggle)

Added 2026-10-26 for Stellar Blade's Eve and Tachy (the whole port: stellar-blade-characters.md), whose breast, hip, belly, thigh-twist and accessory jiggle is a
set of spring bones in the source game's animation data (per spring: stiffness, damping, a maximum displacement, a
reset threshold, per-axis translate and rotate, an alpha).
- **Solver:** `codemp/rd-common/hairchains/hair_spring.{h,c}` (C99): a damped spring per bone, integrated at a fixed
  1/120 s step in the entity's model space (the animated body moves the springs, not the entity's travel or turn),
  with axis locks, a distance limit, a reset on large jumps, and an optional turn about the bone's pivot. Written
  2026-11-19 from that model alone; on all 174 spring assets it matches the earlier implementation to 1e-4 cm and
  1e-5 degrees along six synthetic paths (walk, turns, hops, resets, rest, shake).
- **Glue:** a spring is a root hair bone in the `.chains` sidecar (flag 16), hosted like any root. `tr_hair.cpp`
  steps the springs beside the chains from their own accumulator, toward their animated heads lerped within the
  frame, and blends the last two steps' offsets at draw, so the jiggle doesn't depend on the frame rate. The moved
  bone and the bones hanging from it go to the hair palette as usual.
- **Skinning:** the vertices are re-pointed exactly (flag 32): each listed vertex's render entry spells out its
  source weights, its spring bones beside the skeleton bones the rest of its weight is on. With the springs at rest
  the vertex is skinned as the GLM skins it (Eve: 12,419 vertices within 0.004 weight).

## Verification

- **Harness:** `tools/hair/hc_replay.c`, built against the engine solver:
  `gcc -std=c99 -O2 -ffp-contract=off -o hc_replay.exe tools/hair/hc_replay.c codemp/rd-common/hairchains/hair_sim.c -lm`.
- **Dump replay:** `tools/hair/hcd_to_hcr.py <dump.hcd> <model.chains> <out.hcr>` turns an in-game dump into a reel and
  prints the matching `hc_replay` arguments. The offline steps then match the game's to float-library noise. Tifa,
  2026-10-18: 412 steps, one step within 1.9e-5 units, free run within 3.6e-4 units, MSVC against MinGW libm.
- **Per-frame stepping:** `tools/hair/hc_frames.c` runs the glue's step plan (`rd-common/sim_frame.h`) over a reel at
  a chosen frame rate with long frames in it, against the same frames with no step limit; build line in its header.
  `tools/hair/simcheck_run.py` is the in-game check (one hidden client, `r_simCheck` windows per phase).
- **Research records:** the solver's parity against the source-game solver it ports, the sidecar build and the feel
  study (private: the inputs are the source games' assets) live in `.codex-research/tifa-hair-chains-20261019/NOTES.md` and
  `PLAN_HOOK.md`.
