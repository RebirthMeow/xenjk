# Ghoul2 models bent about their own joints (r_g2Joints), 2026-10-18

Owner decision 2026-10-18 (option A of `.codex-research/ff7r-chars-20261015/EXPLORE_20261018.md`): converted
characters keep their own proportions and the renderer bends them about their own joints, client side, so they look
right on any server and with any mod's cgame.

## The problem

A GLM carries no skeleton. Ghoul2 skins every vertex with `animMatrix * inverse(basePoseMat)` from the GLA, so every
humanoid is bent about `_humanoid.gla`'s joints, Kyle's. xen_conversion turns a model's joints to Kyle's directions
(`retarget_pose`) but keeps their positions, so the model keeps its proportions, and its joints sit away from the
pivots the engine uses: FF7R Tifa's elbows ~4 game units and wrists ~4.5 units up the arm (the converter's `pivots`
report: 0.074 and 0.079 of Kyle's height). Every bend then shears the limb sideways (4-5 cm at her elbows and wrists
in the stand pose): kinked elbows, curved gauntlets, hands hanging off the wrists, fingers that swing instead of curl.

## What changed

- `codemp/rd-common/tr_g2joints.{h,cpp}` (new): parse `<model>.joints` against a GLA (per bone: own joint minus GLA
  pivot, inherited down the GLA for bones the file omits; the anchor bone that carries each joint, with a cycle check)
  and the per-bone correction `G2J_Correct`.
- `codemp/rd-vulkan/tr_ghoul2.cpp`:
  - `R_LoadMDXM` reads `<model>.joints` beside the GLM (`R_LoadG2Joints`) into `mdxmData_t::joints`.
  - `G2_TransformBone`: after the stock transform (animation, blends, angle overrides), the bone keeps its rotation
    and its translation moves by `delta_c = delta_a + R_a d - R_c d` (`d` = own joint - GLA pivot, `a` = anchor,
    `R` = skinning rotations). That is the bone turning about its own joint, the joint carried by its own parent.
    `CBoneCache::mJointShift` keeps each bone's shift; `EvalLow` transforms the anchor first.
  - `G2_GetBoltMatrixLow`: a bone bolt sits on the model's own joint. Surface bolts (tags such as `*r_hand`) follow
    the skinned mesh already.
- `tr_init.cpp` cvar, `tr_local.h` field and extern, `CMakeLists.txt` inputs. rd-vanilla, rd-rend2 and rd-dedicated
  are untouched.

Because the cgame of whatever mod is running bends models through the renderer (`cl_cgameapi.cpp` forwards every
Ghoul2 call to `re->G2API_*`), the correction applies to drawing, bolts (the saber hilt) and client traces on any
server. The server's own traces keep Kyle's joints, as they do for every custom model today. Ragdoll internals
(`G2_GetBoneMatrixLow`, basepose queries) are unchanged.

## The file

Written by xen_conversion's export stage when the plan sets `joints_file: true`
(`stages/export_glm.py: _own_joints`, `_write_joints`): every bone of the converted armature, in the space the GLM's
vertices are written in.

```
// comments
xenjk_joints 1
<bone> <the bone of the model's own skeleton that carries it, or -> <x> <y> <z>
```

Bones the GLA does not have are skipped; a bad or empty file is ignored with a warning and the model keeps the
skeleton's joints. Models without the file are drawn exactly as before, and other renderers and clients never open
it.

The anchor matters for fingers: the GLA hangs the finger bones off the forearm (`lradius`), so anchored there a
knuckle would follow Kyle's hand geometry (her knuckles ~0.4 units off at a 30 degree wrist bend). Anchored to the
model's own parent (`lhand`), it rides her hand.

## Setting

| cvar | default | meaning |
|---|---|---|
| `r_g2Joints` | 1 | 1 = models that ship `<model>.joints` bend about their own joints; 0 = stock |

## Converter companions (same day)

- `plan["joints_file"]`: write `model.joints`. `plan["finger_chains"] = "descend"`: fill each digit's JKA finger
  joints along one chain (standard Tifa's glove knuckle helper `L_KnuckleMiddleA_Spo` took `d4_j2` under the depth
  rule, leaving middle/ring/pinky with one joint). Both opt-in; converter defaults unchanged.
- The FF7R pipeline (`.codex-research/ff7r-chars-20261015/run_char.py`) sets both; `make_pack.py` packs the file.

## Verification (2026-10-18, FF7R Tifa, BOTH_STAND1, dev DLL)

- The game's own mesh dump (charshadowdump) against an independent Python port of the same math
  (`.codex-research/ff7r-chars-20261015/cycles_grid/g2joints_ref.py`, Blender + the JKA GLM importer): median 0.3 mm,
  p95 0.4 mm; `r_g2Joints 0` against stock Blender: the same; the crossed pairs differ by 20-24 mm at p95.
- Bind-pose face shots are unchanged; vertices move up to 4.3 cm in the stand pose; soles rise 3 mm.
- The saber hilt stays in the corrected hand (surface bolt).
- Record: `.codex-research/ff7r-chars-20261015/EXPLORE_20261018.md` section 4.
