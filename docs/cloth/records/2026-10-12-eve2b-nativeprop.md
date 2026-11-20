> Original: `.codex-research/eve2b-nativeprop-20261012/NOTES.md` (local research folder, untracked), copied verbatim on 2026-10-12. Script paths below refer to `tools/cloth/pipeline/skeleton/` (and sibling stages). Its `scripts/build_np.py` etc. are tools/cloth/pipeline/skeleton/; the `eve2b-glm` scripts it reuses are tools/cloth/pipeline/model/. The data it names (meshes, GLM/GLA, pk3, `.cloth`) stays local and is never committed.

# Eve 2B on her own proportions: `eve2b_np` (work notes, 2026-10-12)

Ripped commercial assets: local research only. Never commit, package for distribution, upload or install into
`game_directory/.../GameData`. Nothing in xenjk source was changed.

## Result
- `models/players/eve2b_np/model.glm` (budget mesh, same surfaces/shaders/skins/textures as `eve2b`, 102 surfaces,
  39,268 verts, 30,483 tris) skinned to a NEW skeleton `models/players/_humanoid/eve2b_np.gla` (same 53 bone names,
  hierarchy, flags, frame count 21,376 as `_humanoid.gla`; base pose = humanoid rest rotations at HER joint positions;
  every stock frame retargeted).
- Local test pk3: `zz_eve2b_np_test.pk3` (12 MB; also copied into `home/base`). PBR pack builder for rend2/JKSunny:
  `scripts/make_pbr_pack_np.py <out.pk3> eve2b_np` (not built/tested here).
- Plays every stock animation through the stock humanoid `animation.cfg` with NO code change (see "Naming" below).

## Naming: why the GLA lives in `models/players/_humanoid/`
game/cgame decide by substring `strstr(GLAName, "players/_humanoid/")` (cgame/cg_players.c:531, game/g_client.c:1742/1804,
bg_panimate.c:2335) and the client derives the animation.cfg path from the GLA directory and requires exactly
`models/players/_humanoid/animation.cfg` (cg_players.c:558, otherwise the model is rejected and kyle is used).
A model-local path (Iris style, `models/players/eve2b_np/eve2b_np.gla`) would make the client reject her as a player
model. `models/players/_humanoid/eve2b_np.gla` passes every check, maps to animset index 0 (bgHumanoidAnimations) and the
humanoid animevents; nothing hard-codes the file name `_humanoid.gla`. Bones are looked up by name, and all 53 names exist.
Verified in-game: log shows `Disk-loading "models/players/_humanoid/eve2b_np.gla"`, no "Model does not use supported
animation config", no bone/G2/animation errors (same warning set as the eve2b run).

## Method
1. `scripts/build_np.py` (copy of eve2b-glm build_glm.py): rest fit is ROTATION ONLY. Each Eve key bone gets the same
   direction-alignment rotation as before (her bone direction -> humanoid bone direction, minimal twist, composed down the
   chain), but its joint goes where the parent's rigid transform carries it (FK) instead of being snapped onto the humanoid
   joint. Whole model lifted so boot soles rest at z 3.12 (foot dz -1.01). Weights unchanged (bone_mapping.json).
   Her rest position for each humanoid bone -> `build/lite/joints_np.json`: core bones from her Biped joints, twist bones
   (femurX/humerusX/radiusX) at segment midpoints (as in stock), fingers from her finger joints (d2 = mean index/middle,
   d4 = mean ring/pinky), hang_tag bones = stock offset from the hand x hand ratio 0.891, tails = stock offset x thigh
   ratio 1.140, face bones = stock offset from cranium x head ratio 0.895, Motion x root ratio 1.101.
   Tag surfaces copied from kyle and moved rigidly with their bone to her joint (hand tags ride on *hang_tag_bone*;
   head tags also scaled about cranium by the head ratio).
2. `scripts/make_gla_np.py` (+ `jkagla.py`, `retarget_lib.py`): engine semantics from rd-vanilla G2_TransformBone
   (skin F_child = F_parent * A_child, pool entry = quat (q+2)*16383 + translation (t+512)*64, MC_UnCompressQuat).
   Stock base matrices carry the GLA fScale 0.64 in their rotation; it is factored out to rigid frames G = F*[R|p].
   - Decode check: decode -> local frames -> re-encode with identical skeleton (`--identity`, gla/identity_test.gla)
     reproduces the stock matrices to 3.8e-6 deg / 1.7e-13 units (stock quats are unnormalised, norm 0.998..1.002, so only
     39% of pool entries are byte-identical; ours are unit quats). Local translations of chain bones are constant over all
     frames (std 0.01-0.03 u), which confirms the decode semantics.
   - Retarget: every bone keeps the stock GLOBAL rotation in every frame (= identical local rotations). Positions:
     G'_i.t = G'_anchor * (o'_i + k_i (t_i(f) - o_i)), t_i(f) = stock offset in the anchor frame, o/o' = stock/her rest
     offset, k = |o'|/|o|. Anchor = parent, except humerus -> clavical, finger j1 -> hand, j2 -> j1, hang_tag -> hand
     (stock offsets are constant there: fingers/hang tags are radius children that really follow the hand). Rigid bones
     get exactly her offsets.
   - model_root/pelvis/Motion: stock position scaled by 1.101 about the ground point (0,0,-23.84 = kyle's soles in
     BOTH_STAND1), then per frame the whole skeleton is moved vertically so her lowest contact point (4 boot-sole points,
     knees, elbows, hands, pelvis, head, chest, shoulders) equals kyle's lowest contact point (dz -12.7..+6.0, mean -0.46).
   - Two-handed grip: where kyle's *l_hand tag is on the hilt held by *r_hand (|l-r| < 14, perpendicular < 3.5, smooth
     weight), her *l_hand is put at the same offset from her *r_hand by a two-bone IK on her left arm (hand rotation kept,
     elbow keeps its plane). 4,843 frames, 3,742 at full weight, 67 slightly out of reach (residual <= 1.15 u), max shift 5.6 u.
   - Re-encode: max quantisation 0.0068 deg, 0.0078 u. Pool 601,948 entries (stock 599,570); GLA 11.8 MB.
3. `scripts/package_np.py` = package.py + the GLA at `models/players/_humanoid/eve2b_np.gla`.

## Proportions (JKA units; `proportions.json`, `scripts/proportions.py`)
| measure | her source | eve2b (_humanoid) | eve2b_np |
|---|---|---|---|
| shoulder width (humerus L-R) | 8.65 | 11.85 | 8.60 |
| hip width (femur L-R) | 6.90 | 7.22 | 6.90 |
| neck->head (cervical-cranium) | 2.23 | 2.89 | 2.23 |
| torso pelvis->neck(cervical) | 17.68 | 21.12 | 17.73 |
| upper arm | 9.14 | 10.88 | 9.14 |
| forearm | 9.15 | 8.74 | 9.15 |
| thigh | 16.79 | 14.72 | 16.79 |
| shin | 14.84 | 14.46 | 14.84 |
| hip height (femur z, rest) | 40.20 | 35.59 | 39.20 (A-pose splay) |
| ankle height (talus z) | 8.73 | 7.47 | 8.73 |
Head top 65.9 (kyle 67.2; eve2b was stretched to 67.1). Segment lengths stay at her values in animation (stock
outlier frames stretch some bones by up to ~2 u, scaled the same way).

## Verification
- Ground contact (`contact_report.json`, `scripts/contact_report.py`): in all 19,388 frames where both kyle's and her
  lowest point is a sole, her sole height equals kyle's exactly (max diff 0.0000), e.g. stand/idle/stances/walk/run/
  crouch idle+walk/jump/land/lunges. Frames whose lowest point is not a foot (flips, knockdowns, deaths, wall runs) match
  the lowest body point instead (sole height differs by up to 4 u there, by design).
- Saber grip (`grip_report.json`, `scripts/grip_check.py`, engine tag maths): *r_hand to fist centre 1.8-2.7 u (kyle
  2.0-3.0; her hand is 0.89x). Two-handed (medium/strong stances, A2/A3 attacks): left tag perpendicular to the hilt
  0.30-1.56 u vs kyle 0.30-1.57 (before the IK: 2.2-4.9 u). In-game medium stance shows both hands on the hilt.
- In-game (rd-vanilla, eve2b-glm-20261012/bin exe reused, private homepath `home/`, muted, net_ip 127.0.0.1, window moved
  below the desktop; menu closed by `togglemenu` in the cfg because a posted ESC needs focus): shot list of eve8.cfg moved
  to mp/ffa1 3032 832 (the ffa3 spot put the camera in walls) plus crouch, grounded attack and three saber-style
  close-ups (forcepowers offense 3). Side-by-side sheets (left eve2b, right eve2b_np):
  `sheets/pair_idle.jpg`, `sheets/pair_close.jpg`, `sheets/pair_move.jpg`, `sheets/pair_saber.jpg`, `sheets/pair_hands.jpg`.
  Visible: narrower shoulders, smaller head, longer legs; feet on the floor in idle/run/crouch; no new candy-wrapping
  (weights and local rotations equal eve2b's). Logs: `logs_np_qconsole.log`, `logs_old_qconsole.log`.

## Known defects / open
- Server-side collision: with stock `d_perPlayerGhoul2 0` the server gives every player kyle's ghoul2 (g_client.c:1606),
  so server saber/hit traces use kyle's proportions while the client draws her (hand/blade up to a few units apart; legs
  ~4 u longer). `d_perPlayerGhoul2 1` (server cvar) would load her model+GLA server-side. Same is true for any model, but
  for stock-skeleton models the bolts coincide.
- Foot sliding: longer legs with unchanged stock joint angles give a longer stride than the game's run speed (no stride
  retime). Pelvis lateral sway also scaled by 1.10.
- The `hands_style_c` close-ups miss the hands (strong stance holds high); grip there is only measured numerically.
- The stock rest/airborne attack shots differ in pose between the two runs (wall-clock frame timing), not a model issue.
- Ragdoll/IK code uses BasePoseMat of her GLA (same rotations as stock, her positions): not exercised here.
- Other model variants (`eve2b_full`) not rebuilt; `build_np.py full none <dir> eve2b_np_full` would do it (the GLA is
  shared: joints come from the same skeleton, check joints_np.json is identical).

## Cloth pipeline (not rebuilt)
The cloth asset (codemp/rd-common/cloth cloth_asset: bone names + refInv, cloth mesh rest positions in the bind pose,
body collider, capsules, render map into glm surfaces/vertices) is bound to eve2b's bind pose and vertex layout. For
eve2b_np regenerate: refInv from `eve2b_np.gla` base poses (pelvis/femur/tibia moved), the skirt rest mesh from
`build/lite/transform.json` (rotation-only bone transforms; skirt surfaces `skirt_0..3`, `skirt_rigid_0..2` with
original Eve vertex indices), the body collider and leg capsules (her thighs are 14% longer, hips narrower), and the
render map (vertex counts changed: 39,268 vs 39,621 verts after normal splits).

## Files
- scripts: build_np.py, make_gla_np.py, jkagla.py, retarget_lib.py, proportions.py, contact_report.py, grip_check.py,
  package_np.py, make_pbr_pack_np.py, make_cfg_np.py, run_client_np.py, sheet_pairs.py; t_decode/t_local/t_anchor.py
  (decode experiments).
- build/lite: model.glm, joints_np.json, transform.json, bone_mapping.json, build_report.json, tex/, pkg/.
- gla: eve2b_np.gla (+ _report.json, _contacts.npz, _gripw.npy), identity_test.gla (decode check, regenerable).
- stock_F.npy (72 MB stock skin matrices, regenerable by scripts/t_local.py). Screenshot runs: home/base/screenshots
  (final), screenshots_run1_menu / run2_ffa3 / run3 / run4 (earlier attempts).
Rebuild: `python scripts/build_np.py lite ../eve2b-glm-20261012/targets_lite.json <abs>/build/lite eve2b_np`,
`python scripts/make_gla_np.py build/lite/joints_np.json gla/eve2b_np.gla build/lite/model.glm`,
`python scripts/package_np.py build/lite eve2b_np zz_eve2b_np_test.pk3 gla/eve2b_np.gla` (copy tex/ and icon from the eve2b build first).

## Run bob smoothed (2026-10-13, owner: "she bobs a lot")

The per-frame ground correction jumped whenever the lowest sole changed feet: in BOTH_RUN1 her cranium popped 3.7 u in
one frame twice a stride, and its travel was 4.39 u against kyle's 2.84 (x1.54; RUN_STAFF x1.47, WALK1 x1.10).
`make_gla_np.py --smooth-loops 2` smooths the correction around each looping sequence (animation.cfg loopFrames != -1:
49 sequences, 1,148 frames; circular Gaussian, sigma 2 frames). One-shot sequences keep the exact contact. After:
RUN1 cranium travel 2.98 (x1.05) on a smooth curve, WALK1 unchanged (x1.10), RUN_STAFF 2.21 (x0.80). In the smoothed
frames her soles sit between 1.94 below and 2.73 above kyle's. Without the flag the tool reproduces the installed GLA
byte for byte (sha256 fced7c65...). Build `.codex-research/eve2b-smoothbob-20261013/smooth2/` (report json there);
installed `GameData/base/zz_eve2b.pk3` with only the GLA replaced (sha256 8295fdae...), previous pack in
`.codex-trash/*-gamedata-pre-smoothbob/`. Hidden 4K running shots (old vs new): both load, feet on the ground.
