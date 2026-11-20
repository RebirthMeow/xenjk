> Original: `.codex-research/eve2b-np-cloth-20261012/NOTES.md` (local research folder, untracked), copied verbatim on 2026-10-12. Script paths below refer to `tools/cloth/pipeline/asset/` (and sibling stages). The scripts it copies from `eve2b-cloth-asset-20261012` are the tools/cloth/pipeline/asset/ versions. The data it names (meshes, GLM/GLA, pk3, `.cloth`) stays local and is never committed.

# Skirt cloth on `eve2b_np` (work notes, 2026-10-12)

Ripped commercial assets and derived data (asset, reel, pk3s, screenshots): local research only. Never commit, upload or
copy into `game_directory/.../GameData`. The old eve2b asset folder (`eve2b-cloth-asset-20261012`) and the old client
folder (`cloth-ingame-20261012`, except its `bin/rd-vanilla_x86.dll`, see below) are untouched.

## 1. Reset bug ("jump" resets during play)
Cause: every `jump` reset in the old client logs (reset1, fps1-3, shots1/2) is a real teleport made by the test cfgs'
`setviewpos`. The count per log equals the number of `setviewpos` executed while the cloth was live; idle/run/attack stretches
without a teleport have none. MP `setviewpos` calls TeleportPlayer, which also throws the player forward at 400 u/s, so a
follow-up `setviewpos` "to the same spot" moves her 100+ units. No pose, bone-cache, LOD or Mprev fault was found
(`Mprev` is the pose of the last advanced frame and `lastTime` its time, so stale frames are covered by the gap test).

Real weakness fixed: the teleport test was a fixed 96 units per advanced frame, blind to the elapsed time. A long frame
(hitch, screenshot, a few unseen frames under the 500 ms gap) at real speed could trip it. Now
`dist > 96 + 2000 u/s * dt` (tr_cloth.cpp `CLOTH_MAX_SPEED`, Cloth_Advance teleport block), and each teleport reset
prints distance/time/limit (PRINT_DEVELOPER, PRINT_ALL with r_clothDebug).

Verified (`logs/verify1.log`): spawn, setviewpos, then 20 s of running, circling, jumps, running jump, attacks, run+attack,
screenshots (long frames) and a com_maxfps 30 stretch: resets = first 1 + jump 1 (the initial setviewpos) and 0 during
normal play; the deliberate final setviewpos gave `teleport reset, pelvis moved 711 units in 8 ms (limit 112)`.
shots1: 5 jump resets = 5 setviewpos. fps1: first/gap resets only after r_cloth 0 phases or the instance being freed
during an 8 s client stall.

## 2. Asset (`out/eve2b_np_skirt.cloth`, 860 KB, SKA1)
Scripts copied from `eve2b-cloth-asset-20261012/scripts` into `scripts/`; changes:
- `common.py`: glm = `eve2b-nativeprop-20261012/build/lite/model.glm`; `transform.json`, `bone_mapping.json` (identical
  to the old one), `build_report.json` from that build. Eve source mesh (`eve_mesh.npz`) and hare.skd unchanged.
- `glaio.py`: default GLA = `eve2b-nativeprop-20261012/gla/eve2b_np.gla` (refInv/base pose and the reel).
- collider band `--zmin 44.6 --zmax 123.6` (old 35/118): low edge follows her hip/hem (+9.6 cm), top edge her lower_lumbar
  (+5.6 cm); pelvis-relative 127.6 would have taken 791 of 880 torso triangles (old 201).
- new: `compare.py` (old/new table), `diag_band.py` (worst band edges), `make_cfg_npc.py`, `run_client_npc.py`, `sheet.py`.

| item | old eve2b | eve2b_np |
|---|---|---|
| sim mesh | 2,831 v / 5,361 t, 804 kinematic | same topology; rest positions from the np glm skirt (rotation-only fit), match 2e-5 cm |
| skin | method A, 4 infl | same (A vs B L1 mean 0.00036, max 0.016) |
| refBone / refInv | pelvis, stock base | pelvis, eve2b_np.gla base (pelvis z 104.5 cm vs 94.9) |
| collider before refine | 3,560 v / 4,616 t | 3,617 v / 4,650 t (190 body tris under clothing dropped) |
| collider refined (2.5 cm) | 9,907 v / 16,919 t | 9,615 v / 16,249 t |
| capsules | 12, hand/finger radii 2.8/1.6/1.0 | 12, same radii, her bones |
| render map | skirt_0..3 = 880/880/880/502 | same counts, 3,142 verts, nearest = identity, 0.0 cm |
| skirt / kinematic band in bind | skirt z 60.8..126.8, band 104.9..126.8 cm | skirt z 70.4..128.2, band 114.1..128.2 cm |

## 3. Reel and float harness (60 Hz, default knobs; `out/final_f.log`, `out/metrics_final.json`)
Reel: `make_reel.py` with the eve2b_np GLA, same 27 clips, same entity paths, 6,975 frames (clip lengths identical).
Harness: the existing `eve2b-cloth-asset-20261012/build/Release/skirt_replay_f.exe` (built from the current
skirt_sim.c). No blow-ups, no NaN. Full table: `out/compare_final.md` (old / new per cell).

| clip | disp mean | hem mean | inside mean / max | core mean / max | fold max | ms |
|---|---|---|---|---|---|---|
| idle | 5.7 / 5.6 | 8.0 / 7.8 | 0.2/1 -> 0.5/3 | 0 / 0 | .01 / .00 | 4.07 / 3.87 |
| run | 10.8 / 10.6 | 17.1 / 16.7 | 1.6/11 -> 2.6/10 | .15/3 -> .05/1 | .03 / .03 | 4.14 / 4.10 |
| jump | 13.4 / 13.2 | 19.8 / 19.5 | 2.0/19 -> 2.9/22 | .10/3 -> .05/2 | .05 / .03 | 4.20 / 3.97 |
| flip | 15.8 / 15.5 | 21.3 / 21.4 | 4.0/19 -> 5.6/19 | 2.0/14 -> 2.4/18 | .05 / .04 | 4.07 / 3.95 |
| flip_back | 14.8 / 14.9 | 20.4 / 20.6 | 7.2/20 -> 8.1/43 | 3.6/28 -> 3.0/25 | .04 / .04 | 4.05 / 4.03 |
| cartwheel | 13.3 / 12.9 | 19.4 / 18.9 | 4.9/11 -> 3.8/15 | 4.3/13 -> 0.4/5 | .02 / .02 | 3.95 / 4.00 |
| roll | 14.0 / 13.4 | 20.0 / 19.0 | 2.8/11 -> 5.0/51 | 0/0 -> .1/2 | .03 / .04 | 3.97 / 4.04 |
| wallrun | 14.6 / 14.4 | 21.6 / 21.1 | 4.3/22 -> 4.8/22 | 1.3/12 -> 2.1/13 | .03 / .03 | 3.95 / 4.06 |
| kick | 12.2 / 12.3 | 17.0 / 17.3 | 2.6/21 -> 3.3/32 | .4/8 -> .3/5 | .03 / .02 | 3.94 / 4.09 |
| saber_chain | 13.7 / 13.6 | 19.5 / 19.7 | 2.9/14 -> 4.3/29 | .2/3 -> .1/2 | .04 / .04 | 4.06 / 4.29 |
| turn_run | 13.9 / 14.0 | 20.7 / 21.2 | 4.4/31 -> 6.4/27 | 2.4/15 -> 3.9/20 | .03 / .03 | 4.10 / 4.15 |
| **mean of 27 clips** | 12.3 / 12.2 | 18.0 / 18.0 | 2.5/12.0 -> 3.0/15.6 | 0.66/5.3 -> 0.52/4.5 | .02 / .02 | 4.10 / 4.06 |

Reading: motion, hem lift, cost and folding are unchanged. Particles behind the collider surface rise ~20 % (mean 2.5 -> 3.0,
peaks roll 51, flip_back 43, kick 32, saber_chain 29: single tuck/lunge frames), while particles deep inside a leg/torso
(core) fall slightly (0.66 -> 0.52; cartwheel 4.3 -> 0.4, flip_ip 1.8 -> 0.2; worse in turn_run 2.4 -> 3.9, wallrun
1.3 -> 2.1). Cause: her thighs are 14 % longer and the hip joint sits 9.6 cm higher while the hem only rose 9.6 cm and the
skirt top stayed (the skirt is shorter relative to the leg), so in lunges/tucks more thigh length sweeps through the sheet.
Not retuned (thickness/backDepth, thigh capsules are the knobs listed in the old RESULTS).

Kinematic band (pinned, skinned): idle stretch 0.66..1.44 (old 0.76..1.25), run 0.68..2.08 (0.78..1.69), flips
0.08..3.15 (0.15..2.28). `scripts/diag_band.py`: the worst edge is the same vertex pair with the same weights
(pelvis 0.55 / lower_lumbar 0.38 / lfemurYZ 0.06 vs lower_lumbar 0.52 / pelvis 0.44), but its rest length is 0.98 cm on
eve2b_np vs 1.74 cm on eve2b: her waistband is 14 cm tall instead of 22 (real proportions), so the same differential
pelvis/lumbar/thigh motion is a larger ratio. The band follows pelvis/lumbar sensibly; the same stretch shows on the plainly
skinned render skirt (glm weights equal method A within quantisation).

## 4. In game (rd-vanilla x86, private home `home/`, muted, net_ip 127.0.0.1, window below the desktop)
- Renderer rebuilt in `cloth-ingame-20261012/build` (target rd-vanilla_x86, RelWithDebInfo, log `cloth-ingame-20261012/build_rd2.log`)
  and copied to `cloth-ingame-20261012/bin/rd-vanilla_x86.dll` (same exe path, no firewall prompt); the previous dll is kept
  as `bin/rd-vanilla_x86.dll.20261012-0936`.
- Home: game dlls copied from the old home, `zz_eve2b_np_test.pk3` (copy) and `zz_eve2b_np_zcloth.pk3`
  (`models/players/eve2b_np/model.cloth`). Load line: `2831 particles, 5361 tris, collider 9615 verts, 12 capsules, 4 render surfaces / 3142 verts`; binds to the eve2b_np glm without warnings.
- CLOTHSTATS (one instance, 60 Hz, 2 s windows):
  - verify1 at com_maxfps 125: 122-125 fps with the sim, step 4.8-5.5 ms mean (max 13.4), 2.5-2.8 ms cloth per frame,
    contacts 380-680 per step, hand 0-9; com_maxfps 30 stretch: 56-71 fps window, 4.3-6.5 ms per frame, no resets.
  - fps1 uncapped (hidden window, noisy machine: frames up to 2.2 s and one 8 s stall also with the sim off):
    idle 244-274 fps on vs 287 off; circle run 264-307 on vs 409 off; attack 266-348 on vs 430 off. Step 5.0-5.7 ms
    mean (in-game x86 build; harness x64 4.1), cloth 1.1-1.3 ms per frame at ~60 steps/s.
  - Resets by cause: first-spawn, setviewpos teleports, gap after r_cloth 0 phases; none from normal play.
- Sheets (cloth ON left / OFF right of each pair, same moment at timescale 0.02): `sheets/idle.jpg` (front, side, back,
  close), `sheets/run.jpg` (run, side, stop, fast turns), `sheets/jump.jpg` (jump, land, running flip),
  `sheets/attack.jpg` (attacks, run+attack, crouch). Screenshots: `home/base/screenshots/`.
- Visual: idle drapes like the skinned skirt with a little less flare; no leg poking through the outside of the skirt seen
  in any shot.

## Open defects
1. Run/turn: the back and side panels fly up to the waist and expose her (run_a/run_b, run_side, turn, runatk); the
   skinned skirt stays down. This is the hem-lift/inertia tuning item from the old RESULTS (hem rides above the pose on
   runs); the old client tested `linVelScale 0.35` (tune1.cfg) but no result was recorded. Tuning by eye is still open.
2. Wide-stance attacks (atk_a/b): the front panel is pushed behind/between the thighs; possibly thigh pass-through of the
   front panel (cannot be confirmed from these angles). Offline inside/peaks rose in lunges (kick, saber_chain, roll).
3. Jump/flip: the skirt bunches upward around the thighs (jump_b, flip_b), consistent with 1.
4. Bone matrices handed to the solver are not orthonormal during animation blending (CLOTHDEBUG row-length deviation up
   to 0.08; old eve2b showed 0.14) - same matrices as the body skinning, not new, not investigated.
5. Fps numbers here were taken with the window hidden (owner: timing runs visible); use them only as on/off ratios.
6. Server side still uses kyle's ghoul2 unless d_perPlayerGhoul2 1 (from the eve2b_np notes); the cloth is client-only.
