# Ghoul2 animation smoothing (r_g2Blend), 2026-10-12

Owner decision 2026-10-12: rotation-space bone blending plus an eased animation-to-animation blend curve, draw-only,
behind a setting, verified with a synced side-by-side video. Moves must not look slower to come out; gameplay timing
must not change. Built on `renderer/g2-blend` (from bb8eaa95), merged
the same day (208e2a9f, branch deleted) and installed in rd-vulkan (`openjk.cfg`: `r_g2Blend 1`, `r_g2BlendCurve 2`).

## What changed

- `codemp/rd-common/tr_g2blend.{h,cpp}` (new, shared by rd-vulkan and rd-vanilla): pose = unit quaternion + joint
  displacement; decode from the GLA's compressed quaternion bones, slerp/displacement-lerp blend, matrix rebuild,
  blend curves, orthonormality measure.
- `rd-vulkan` and `rd-vanilla` `tr_ghoul2.cpp`: `G2_BlendBoneDraw` replaces the component lerp for keyframe
  interpolation (backlerp) and animation blends (BONE_ANIM_BLEND / blendLerp) when the bone cache's current transform
  generation is a draw generation; eased `blendLerp`; `G2_TransformGhoulBonesDraw` marks the renderer's own draw
  transforms (R_AddGhoulSurfaces, and RB_TransformBones in rd-vulkan); `r_g2BlendStats` diagnostics.
  `G2_API.cpp`: one diagnostic hook each in G2API_GetBoltMatrix and the two collision entry points.
- `tr_init.cpp` cvars, `CMakeLists.txt` inputs. rd-dedicated and rd-rend2 untouched.

## Settings

| cvar | default | meaning |
|---|---|---|
| `r_g2Blend` | 1 | 1 rotation-space blending in draw traversals; 0 stock matrix lerp |
| `r_g2BlendCurve` | 2 | animation blend curve: 0 linear (stock), 1 ease-out, 2 smoothstep into looping animations, ease-out otherwise |
| `r_g2BlendStats` | 0 | 1: `G2BS` line per drawn humanoid (stock vs new on the same frame); 2: `G2BQ` query check (gameplay) |
| `r_g2BlendStatsModel` | "" | stats only for model file names containing this |

Blend lengths are unchanged. Owner 2026-10-12: default curve 2 (smoothstep into loops: A/D strafe spam reverses from
zero speed instead of the ease-out kick; attack starts get ease-out either way, so saber feel is the same).

## Method and math

Ghoul2 applies a bone's per-frame local matrix L as `Final_child = Final_parent * L`; vertices are skinned with Final
and bolts are `Final * BasePoseMat`. L fixes the bone's joint p (the translation of its BasePoseMat, model space)
unless the joint itself moves. With `L = [R | t]` the joint displacement is `d = R p + t - p`.

- Stock lerps the 12 floats: the joint stays put but R shrinks/skews (squash, candy-wrapping).
- Slerp R with a lerp of t keeps R a rotation but swings the joint along a chord (the offline 60 cm/frame pelvis
  jumps, eve2b-cloth-asset RESULTS.md).
- Here: slerp the quaternion (shortest arc, nlerp under 3.6 degrees) and lerp d; `t = p + d - R p`. The joint
  rotates about its pivot, real joint motion (pelvis/root translation) blends linearly, R stays orthonormal. The
  GLA base-pose scale lives in BasePoseMat, never in L, so no rescale is needed. Unblended bones use the stock path
  bit for bit.
- Curves: ease-out `1-(1-x)^2` starts at twice the linear rate (the new move comes in sooner than stock) and settles
  with zero velocity; smoothstep `x^2(3-2x)` starts at zero speed, so it is only offered for blends into looping
  animations (idle/walk/run loops, BONE_ANIM_OVERRIDE_LOOP).

### Draw-only

The mode is chosen per bone-cache transform generation: `G2_TransformGhoulBones` sets `mDrawBlend` when called from
the renderer's draw code (`g2bDrawTraversal`) or for a cache the renderer has drawn before (`mEverDrawn`), so lazy
evaluation later in the frame (rd-vanilla skins in the back end) keeps the generation's mode and cgame's bolts on the
client's own instances (saber blade, effects) follow the drawn hand. Server instances are never drawn, so they never
get the new math. (First version applied it to draw traversals only: mid-blend the blade, placed by cgame with the
stock math, floated up to ~9 units off the drawn hilt; fixed the same day.)
`HackadelicOnClient` was not usable: rd-vulkan's `RB_TransformBones` sets it and never clears it, so it stays true
into the next server frame on a listen server (pre-existing; it also turns on render smoothing / newMatrix for the
server's own G2 evaluations there; not changed here, see "Owner decisions").

## Verification

All runs: localhost, `net_enabled 0`, `s_initsound 0` (no audio device), hidden window, below-normal priority,
private homepath `.codex-research/g2blend-20261012/home`, one client at a time. Exe reused:
`.codex-research/vulkan-port-20261012/build-df960cc8/RelWithDebInfo/xenjk.x86.exe` (= installed GameData exe); this
branch's renderers staged next to it as `rd-g2bvk_x86.dll` / `rd-g2bvan_x86.dll` (`+set cl_renderer`), nothing
replaced. Scripts and logs: `.codex-research/g2blend-20261012/{scripts,logs}`.

### Gameplay unchanged (listen server)

- In-run query check (`r_g2BlendStats 2`, r_g2Blend 1, rd-vulkan): every non-draw `G2API_GetBoltMatrix` result
  (server saber blade/hit bolts and cgame bolts) recomputed from a fresh stock transform and compared bit for bit.
  Wall-swing session: 20,000+ bolt queries, 0 mismatches (stock baseline same). Frozen-NPC session (saber strikes
  on stormtroopers/reborn): 28,000+ bolt queries, 0 mismatches; 2,851 G2 collision queries (saber traces), 0 on a
  draw generation (`logs/hit_q0.log`, `hit_q1.log`, `hit_q2.log`). After the blade fix (client instances use the new
  math for every query): 28,000 bolt queries, 0 mismatches on never-drawn (server) instances; the 2,196 collision
  reads of new-math bones are cgame's client-side traces on its own instances (`logs/hit_q3.log`).
- Deterministic replay: with `+set sv_cheats 1 +set fixedtime 8` on the command line (set later, fixedtime does not
  take, and the map load runs on wall-clock time) the scripted session repeats exactly. Stock (w1, w2) and new
  (x1, x2) recordings decode (`cl_shownet 4`) to identical snapshot streams: every player state, animation, timer
  and saber move field (`logs/net_hit_*.txt`). The server's blade position itself is not in snapshots, hence the
  query check above.
- Note: one `wait` is 4 ms here (the command buffer runs twice per 8 ms frame); the hit sessions predate that fix and
  ran at half the scripted durations, which does not affect either check.

### Showcase

`scripts/make_rec.py` records `demos/blendshow_r4.dm_26` (stock Jedi player model with a saber, mp/ffa1, facing one
way, 22 isolated transitions: idle->run, run->stop, walk->run, strafe/back-run start/stop, crouch, standing and
running jump, first swing of each stance, stance switches, an attack chain, force push). `scripts/make_play.py`
replays it per arm at 250 fps capture (4 ms game time per frame), side-on chase camera; `scripts/encode.py` builds the
3-up; `scripts/analyze.py` gives per-transition metrics from the `G2BS` lines.

Results (`r_g2BlendStats 1`, stock and new evaluated from the same inputs every frame):

- Stock bone matrices lose orthonormality mid-blend: worst bone error 0.4-0.66 at the transitions (attack starts,
  back-run start/stop, force push, walk start, jumps, crouch); new: under 0.003 everywhere.
- The visible effect: stock collapses the rotation, so arms are pulled in toward the body and travel a shortcut
  through it mid-blend; the right hand sits up to 8.8 units (a hand length) closer to the body than the full-length
  arm (`logs/showD.log`, linear curve so timing matches stock).
- Videos (`.codex-research/g2blend-20261012/out`, local): `g2blend_3up_slow5x.mp4` / `_realtime.mp4` and `_small`
  copies (stock | ease-out | smoothstep; recorded before the blade fix, so the new arms show the floating blade);
  same-instant stills `frames/pair_*.png` (stock vs new with stock timing, after the blade fix).

## Cost

Not timed in game: the owner was playing on this PC during the session and timing runs must run alone. Per blended
bone the new path decodes 2-4 quaternion bones (same as stock), does 1-3 slerps (acos + 3 sin + normalise each,
nlerp for small angles) and one matrix rebuild; unblended bones take the stock path. With 72 bones that is at most a
few hundred slerps per model per frame (microseconds). r_g2BlendStats 1/2 re-evaluate skeletons and are diagnostics
only.

## Owner decisions

1. Defaults: decided 2026-10-12, `r_g2Blend 1`, `r_g2BlendCurve 2`.
2. Install into GameData (rd-vulkan_x86.dll, rd-vanilla_x86.dll) or not.
3. Client-side effects now follow the new hand (blade fix above); cgame-only collision effects (client saber sparks)
   use the new bones too. Server-side gameplay is unchanged.
4. The pre-existing rd-vulkan `HackadelicOnClient` leak (listen server: server G2 evaluations after a Vulkan frame run
   in "render" mode, with render smoothing). Fixing it changes listen-server behaviour vs the installed build, so it
   needs a decision; rd-vanilla and dedicated servers are not affected.
5. Main checkout merge: the cloth hooks in rd-vanilla `tr_ghoul2.cpp` (RemoveBoneCache, R_AddGhoulSurfaces,
   RB_SurfaceGhoul) are on different lines; the cloth's `R_ClothEvalBone -> EvalRender` will see the new blend.

## Kept for later (owner 2026-10-12: on the list, not now)

- Interrupted-blend pop. When a new blend starts before the previous one finished (A/D strafe spam), Ghoul2 takes
  the old animation's current frame as the source (`G2_Set_Bone_Anim_Index`, rd-*/G2_bones.cpp), discarding the
  in-progress blend: the legs jump to a pose that was never drawn. Stock does the same. Fix, draw-only: keep the last
  drawn local pose per bone in each drawn CBoneCache and use it as the blend source when a bone's blendStart changes
  mid-blend. Blend lengths, timing and server unchanged; a few KB per drawn model. Verify with a scripted A/D-spam
  recording, a 20x slow GIF and the r_g2BlendStats 2 server check.
