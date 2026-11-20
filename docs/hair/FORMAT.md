# Hair-chains sidecar (HCH1 v2), dump (HCD1) and replay reel (HCR1)

Little endian, every field 4 bytes, no padding.
- **Asset** (`models/<m>/model.chains`): read by `codemp/rd-common/hairchains/hair_asset.c` and `tools/hair/hc_replay.c`.
- **Dump:** written by `codemp/rd-common/tr_hair.cpp` (`r_hairDump`).
- **Reel:** written by `tools/hair/hcd_to_hcr.py`.

Sidecars are derived from a model's source assets. Like the cloth sidecars (`docs/cloth/FORMAT.md`), they ship only in
the model's own (private) pack and are never committed.

## What it describes

- **Particles.** Hair moves as bone chains: one particle sits on each simulated bone's head.
- **Cones.** A cone joins a particle to its parent particle, or to a kinematic anchor bone. It is an asymmetric
  distance constraint plus an angular pull toward the animated direction, with a limit.
- **Links.** Lateral distance constraints between particles of neighbouring chains.
- **Collision.** Receiver shapes (spheres or capsules on particles) collide with repulsor shapes (spheres or capsules
  on body bones), for the listed pairs.
- **Containment.** A repulsor flagged as containment keeps its receivers inside instead (when honoured).
- **Pose output.** Solved particles become bone frames: each cone's parent bone turns to aim at its child, and mirror
  pairs copy a driver bone's local rotation delta onto a target bone.
- **Render entries.** Which GLM vertices, per LOD, ride which hair bones.
- **Spring bones** (flag 16). Body jiggle: a damped spring moves one root hair bone toward where the animation puts
  it, in the entity's model space (`codemp/rd-common/hairchains/hair_spring.h`).

## Units and frames

- **Asset space:** the GLM's bind space, in GLM units. `unitsPerCm` = GLM units per source centimetre.
- **Gravity:** a world vector in GLM units/s².
- **Reference frame:** the solver runs in the entity's frame (model space). `HC_BeginFrame` takes the entity's world
  matrix, so the hair feels the entity's motion and gravity stays down.
- **Matrices:** 4x4 column-major, 16 floats, translation in elements 12..14.
- **Per frame:**
  - `H` = the skinning matrix of the host bone (named in the header);
  - hair bone `b` animates as `H x bind[b]`, or, with flag 4, as `S_h x bind[b]` with `S_h` the (orthonormalised)
    skinning matrix of the game bone its root hangs from (a dress from the pelvis and thighs beside hair on the head);
  - repulsor points are `S_g x bind` with `S_g` their game bone's skinning matrix.
- **Render entries:** the hair bone `k` skinning matrix is `K_k = posed[k] x inverse(bind[k])`. A listed vertex is
  `sum_k w_k K_k p_bind`, where bone -1 is the host bone, or, with flag 8, the vertex's own GLM skinning: that share
  is spread over the vertex's own bones, merged with the hair bones, and the four heaviest kept. With flag 32 a bone
  of -2 or less is game bone `-2 - bone` (skinned with its own matrix), so an entry can spell out a vertex's weights
  exactly: its hair or spring bones beside the skeleton bones the rest of its weight is on.
- **Spring bones:** a spring bone animates as its host frame x bind (as any root). Each fixed step of the spring
  block's dt moves its simulated head toward the animated head (`target`); the bone is drawn moved by the offset
  `loc - target` (limited, times alpha clamped to [0, 1]) and, if it rotates, turned by the shortest arc from
  `target - pivot` to `loc - pivot` about its new head, `pivot` being the record's bind point in the same host frame.
  Bones hanging from a spring bone move with it.

## HCH1 asset

### Header (128 bytes)

| offset | type | field |
|---|---|---|
| 0 | char[4] | `HCH1` |
| 4 | u32 | version = 2 |
| 8 | u32 | headerBytes = 128 |
| 12 | i32 x12 | nParticles, nCones, nLinks, nReceivers, nRepulsors, nPairs, nBones, nMirrors, hostBone (a GLA index `build_chains.py` writes and `hc_replay.c` reads; the game binds the host by the name at 88), substeps, lateralIter, collisionIter |
| 60 | f32 | dt (the authored step) |
| 64 | f32 x3 | gravity |
| 76 | f32 | unitsPerCm |
| 80 | i32 x2 | nRenderSurfaces, nRenderEntries |
| 88 | char[32] | host bone name (bound by name, e.g. `cranium`) |
| 120 | u32 | flags: 1 = repulsor points supplied per frame (offline reels only: the game parser refuses a repulsor game bone below 0, so such an asset does not load in game); 2 = honour containment (`r_hairInverse` sets it at run time); 4 = a bone-host table follows the render entries; 8 = bone -1 render shares follow the vertex's own skinning; 16 = a spring block follows the bone hosts; 32 = render entry bones of -2 or less name game bones |
| 124 | i32 | nGameBones |

### Sections, in order

| section | record | layout |
|---|---|---|
| particles | 48 B | i32 bone, i32 group (bits 0..7 the sidecar's group index; bit 12 `HAIR_PARTICLE_BODY`: body physics, tuned by `r_hairSprings*` in its body group, bits 8..11 as a spring's: 1 breasts, 2 hips, 0 the rest; without it, hair: `r_hairChains`); f32 radius, gravityScale, damping, velocityDamping, constraintDamping, friction, bounciness, parkRate, fictitious, centrifugal |
| cones (root to tip) | 40 B | i32 child, parent (-1 = anchored), anchorBone; f32 limit (rad), inner, outer, stretch, compress, parentStrength, rest |
| links | 20 B | i32 a, b; f32 stretch, compress, rest |
| receivers | 20 B | i32 n (1 sphere, 2 capsule), p0, p1; f32 r0, r1 |
| repulsors | 48 B | i32 n, gameBone0, gameBone1 (index into the game bone names), flags (1 = containment); f32 bind0[3], bind1[3], r0, r1 |
| pairs | 8 B | i32 receiver, repulsor |
| bones (parents first) | 100 B | char[32] name; i32 parent (-1 = hangs from the host); f32 bind[16] |
| mirrors | 8 B | i32 target bone, driver bone |
| game bone names | 32 B | char[32]: skeleton bones that carry repulsor points, bound by name at load |
| render surfaces | 40 B | char[32] GLM surface name; i32 vertex count (a binding check); i32 LOD |
| render entries | 40 B | i32 surface (index into the table above), vertex (within that surface at that LOD); i32 bone[4] (-1 = the host bone, or own GLM skinning with flag 8; with flag 32, -2 - g = game bone g); f32 w[4] |
| bone hosts (flag 4 only) | 4 B | i32 per bone: -1 = the header's host bone, else a game bone index. Read for root bones (parent -1); a child takes its root's |
| spring block (flag 16 only) | 44 B | i32 nSprings; f32 dt (the springs' fixed step, 1/120 s); f32 basis[9], row-major: the source's component axes = basis x asset axes (it may mirror) |
| springs (flag 16 only) | 40 B | i32 bone (a root that carries no particle and anchors no chain), flags (1/2/4 translate x/y/z, 8/16/32 rotate x/y/z, in the source axes; bits 8..11 the body group the per-group settings scale: 0 the rest, 1 breasts, 2 hips / buttocks and upper thighs); f32 stiffness (1/s²), damping (1/s), maxDisplacement (asset units, 0 = no limit), resetThresh (asset units), alpha, pivot[3] (bind space) |
| crc32 | 4 B | zlib crc32 of every preceding byte |

nParticles may be 0 only in a springs-only asset (flag 16 with at least one spring: body springs, no chains);
cones, links and receivers then must be 0 too, and the renderer runs the springs and the render-entry skinning without the chain solver.

## HCD1 dump (`r_hairDump`, one instance)

1. **Header:** `HCD1`, i32 version (2; 1 before 2026-10-19), nParticles, nCones, nRepulsors, asset flags.
2. **Feel:** i32 on, then f32 linWeight, maxLinAccG, linTau, eulerWeight, maxAngAcc, centWeight, maxAngVel, carryLag,
   carryAngLag (angles in radians).
3. **Flow** (version 2 only): f32 springDamp, innerScale, airDrag, coneParent (`hcFlow`; all zero = off). Records start
   at byte 80 (version 1: 64).
4. **Records,** each:
   - i32 step counter, i32 flags (1 = reset);
   - f32 entity world matrix[16];
   - targets[nP*3], anchors[nC*3] (NaN off root cones), repulsor points[nR*6];
   - pre pos/prev, post pos/prev [nP*3] each.

A reset record holds the reset state. The first step after a reset reuses the reset's counter, so the frame history
starts over, exactly as an offline replay does.

## HCR1 reel (offline replay)

### Header (40 bytes)

`HCR1`, u32 version, then i32: nSteps, nParticles, nCones, nRepulsors, nPoseFrames, nBones, resetFrame, nPreroll.
Everything after the header is f64.

### Body

1. **initPos** `[nP][3]`.
2. **Steps**, each:
   - i32 frame, i32 pad;
   - ref `[16]`;
   - targets, anchors, repulsor points;
   - pre/post pos/prev;
   - frame kinematics `[9]` (zero when not recorded).
3. **Pose frames**, each: ref `[16]`, anim `[nBones][16]`, posed `[nBones][16]`, sim `[nP][3]`.
