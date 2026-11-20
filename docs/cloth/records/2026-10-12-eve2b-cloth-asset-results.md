> Original: `.codex-research/eve2b-cloth-asset-20261012/RESULTS.md` (local research folder, untracked), copied verbatim on 2026-10-12. Script paths below refer to `tools/cloth/pipeline/asset/` (and sibling stages). Superseded for the current pipeline by the np-cloth record; the first-asset `dbg*.py`, `skd_to_ska.py`, `patch_replay.py` and `run_final.sh` were not copied. The data it names (meshes, GLM/GLA, pk3, `.cloth`) stays local and is never committed.

# Eve 2B skirt cloth asset on the JKA `_humanoid` skeleton: results (2026-10-12)

Offline only. No game code, no game launch, `codemp/cgame/cloth/skirt_sim.{c,h}` untouched. Everything here derives from game assets, which stay local. Format: `FORMAT.md`. Work folder: `.codex-research/eve2b-cloth-asset-20261012/`.

## Files
- `out/eve2b_skirt.cloth` (864 KB) the asset (SKA1). `out/eve2b_skirt_rawcollider.cloth` same with the undecimated-edge collider (alternative).
- `out/reel.skp` (23 MB, 6,975 frames, 27 clips at 60 Hz); `reel125.skp`, `reel30.skp` same reel at 125/30 Hz. `out/reel_meta.npz` entity origin/yaw per frame.
- `scripts/`: `build_cloth_asset.py` (asset), `make_reel.py` (reel from stock `_humanoid.gla`), `analyze.py` (metrics), `render.py` (images),
  `skaio.py`, `glaio.py`, `skdio.py`, `common.py`, `skd_to_ska.py`, `run_final.sh` (the whole final run). Harness: `tools/cloth/skirt_replay.c` (`--asset/--pose` mode added; the `--dump` SKD1 path is unchanged).
- Per-clip numbers: `out/metrics_final.json` (+ `_raw`, `_bend1`, `_sub1`, `_double`, `_125`, `_30`, UE baseline `metrics_hare_f.json`). Images: `out/img/`.

## Asset: counts and decisions
| item | value |
|---|---|
| sim mesh | 2,831 verts / 5,361 tris, the UE topology unchanged; 804 kinematic (maxDist <= 0.01); maxDist, animDrive (all -1) copied |
| sim vertex -> glm | every sim vertex is exactly one Eve skirt vertex (UE cm -> Blender m is `(x,-y,z)/100`; match error max 2e-5 cm, bijection 2831/2831). Its rest position is the glm skirt vertex position (glm bind space) x 2.6667 cm/unit |
| units | positions in cm, `unitCm` = 100/37.5 = 2.6667 stored; gravity -980.665 cm/s^2 unchanged; the hook converts bone translations to cm |
| skeleton | 53 gla bones; refBone = `pelvis` (1); `refInv` = inverse of the orthonormalised gla base pose (base matrices carry a 0.64 scale and some are not orthonormal; skin matrix = chain of compressed bone matrices, so the scale cancels) |
| skin | 4 influences, UE influences mapped through `bone_mapping.json` (method A). Never more than 4 distinct humanoid bones after merging, so no truncation. Compared with method B (weights of the matched glm skirt render vertex): L1 difference mean 0.00036, max 0.016 per vertex, i.e. identical up to the glm writer's 10-bit quantisation and its drop < 1 % rule. Chose A (full precision, no dependence on the render build). Kinematic band (804) follows `lower_lumbar` (382 weight-sum), `upper_lumbar` (270), `thoracic` (52), `pelvis` (97). All UE skirt-strand bones map to `pelvis` by design (thigh-following left to the sim) |
| band check | skinned band edges (both ends kinematic) over the whole reel: stretch max 1.25 (idle) .. 2.28 (flips), compression min 0.13 (A7 kick), 0.4 in flips; typically 0.7..1.7. The band follows pelvis/lumbar sensibly; only extreme twists/tucks crush it |
| collider | built from the budget glm's own surfaces `hips, hips_upper, torso, torso_body, r_leg, l_leg, r_leg_body, l_leg_body, r_leg_boots_1/2, l_leg_boots_1/2`, triangle centroids 35 cm <= z <= 118 cm (the old UE trimesh also spans z 40..118, no arms), M_Body triangles under clothing dropped (175, ray test), vertices welded, glm weights (4 influences). Orientation checked against glm vertex normals: file order already has `(c-a)x(b-a)` outward. 3,560 verts / 4,616 tris before refinement |
| collider refinement | **9,907 verts / 16,919 tris** (UE trace: 12,765 / 25,070). The solver finds contacts through collider vertices within `min(cellSize 2.5, thickness+maxEdge+0.5)` cm of a particle; the UE collider has median edge 0.86 cm, the decimated glm 2.0 cm (p99 11.9, max 17). Edges > 2.5 cm are split (shared midpoints, no remesh of the cloth). Without it: inside-collider particle count 4.2 vs 2.5 per sample (mean over the reel) |
| capsules | 12 (L/R: `radius`->`hand` r 2.8, `hand`->`d2_j1` 1.6, `hand`->`d4_j1` 1.6, `d2_j1`->`d2_j2` 1.0, `d4_j1`->`d4_j2` 1.0, `d1_j1`->`d1_j2` 1.0), same radii as UE. `_humanoid` has only j1/j2 per finger, so the fingertip segment is missing |
| render map | glm `skirt_0..3` (880/880/880/502 = 3,142 verts) -> sim vertex via the Eve vertex index; check by nearest neighbour in bind space: identical for all 3,142, max distance **0.0 cm**; every sim vertex has at least one render vertex (UV/normal splits share a sim vertex, so seams stay closed). Not covered: `skirt_rigid_*` (ribbons) stay on the skeleton |

## Pose source (reel)
`_humanoid.gla` decoded directly (14-byte compressed bones, frame index table, same maths as the Blender add-on); validated by skinning the stock kyle glm
(upright, 64 units tall, feet at the -24 hull bottom). The animations have **no root translation**; the cloth only feels the world through the reference bone, so each clip has an
entity origin/yaw path (run 250 u/s, walk 125, back 200, strafe 250, roll 350, wall run 250, jump vz 225, flip 300, force jump 450, gravity 800 u/s^2, yaw 540-720 deg/s turns) applied as `W` in front of the bone matrices; the `*_ip` clips have no entity motion for comparison.
27 clips: idle, walk, run, run_back, strafe_l/r, jump, flip (BOTH_FLIP_F), flip_back, forcejump, alora_flip, cartwheel, roll, wallrun (run/flip off), kick (A7_KICK_F, A7_KICK_S, A1_SPECIAL), saber_chain (8 A5/A3/A1 swings), saber_run (legs run, torso swings, split at `lower_lumbar`), turn_stand (720 deg/s), turn_run (540 deg/s), run_circle, plus in-place variants of run, strafe_l, jump, flip, roll, saber_run, turn_run. Each clip: 0.5 s idle pre-roll (excluded from metrics), 0.08-0.12 s crossfades.
Note for the hook: bone blending must keep the rotation about the joint (blend joint displacement, not the raw compressed translation); a first version blended raw translations and made 60 cm/frame pelvis jumps. Fixed in `make_reel.py`.

## Parity (rerun after the harness change)
- Double build vs JS, full hare (8,040 frames): max per-vertex difference **0.000000 cm at every horizon**, all probe metrics equal (`out/cmp_hare_d_full.json`); mean 7.408, pen worst 2.283, hand pen 0.365/2.080 as before.
- New SKA1+SKP1 loader vs SKD1 loader: hare converted with `skd_to_ska.py`, double build, 8,040 frames: output files byte-identical (`cmp`).
- Solver source not modified.

## Float build on the reel (60 Hz, 2 substeps, 2 iterations, default knobs)
No blow-ups, no NaNs, any run (float, double, 125 Hz, 30 Hz). Mean over the 27 clips: 4.10 ms/step (final collider), 3.73 (raw collider), UE hare in the same session 4.05.
Per step times are 3.95-4.35 ms mean per clip (p95 4.2-5.8, first-frame reset ~18 ms). **The collider is not the lever: ms/step is NOT well below the UE 4.1 ms**, because cost is the cloth solve.
Profile (raw collider, run+kick): dihedral bends 1.29 ms, contact search 1.04, areas 0.38, collide 0.37, aero 0.22, edges 0.19, rest (skinning, frame, predict) ~0.2.
Cheaper settings measured on the same reel (not tuned, owner's call): `bendModel=1` 2.96 ms (inside 2.75, core 0.78, similar), `substeps=1 iterations=3` 2.69 ms but inside 9.5, core 3.7 (clearly worse), 125 Hz reel 2.94 ms (fewer steps worth of work, penetration lower: inside 1.1, core 0.14), 30 Hz 6.6 ms (inside 9.0, core 3.0).

Metric definitions (`analyze.py`, 0.1 s samples after the pre-roll; dynamic particles only): `disp` = distance from the kinematic skinned pose; `inside` = particles behind the skinned collider surface (closest-triangle, Phong normal, > 0.25 cm, mostly along the normal, within backDepth 9 cm); `core` = particles within 4 cm of a thigh axis (femurYZ->tibia) or 6 cm of the pelvis->upper_lumbar axis, i.e. clearly inside a leg or the torso. The old probe's 4-nearest-vertex plane test gives false 5-7 cm spikes against this collider (open thin boot-band ribbons), so it is only listed in the json. UE baseline with the same ruler (hare, 8,010 frames): disp 7.4, inside 0.03, core n/a, fold max 0.011.

| clip | disp mean / max cm | hem mean cm | inside mean / max | core mean / max | skirt fold frac max | ms mean |
|---|---|---|---|---|---|---|
| idle | 5.7 / 19 | 8.0 | 0.2 / 1 | 0 / 0 | 0.01 | 4.07 |
| walk | 7.6 / 34 | 11.1 | 0.9 / 5 | 0 / 0 | 0.01 | 4.15 |
| run | 10.8 / 37 | 17.1 | 1.6 / 11 | 0.15 / 3 | 0.03 | 4.14 |
| run_ip | 9.2 / 40 | 14.7 | 2.9 / 12 | 0.03 / 1 | 0.02 | 4.08 |
| run_back | 10.5 / 39 | 14.6 | 1.1 / 5 | 0.05 / 1 | 0.02 | 4.02 |
| strafe_l | 13.2 / 38 | 20.3 | 1.3 / 7 | 0 / 0 | 0.01 | 4.21 |
| strafe_r | 9.3 / 34 | 13.9 | 1.3 / 6 | 0 / 0 | 0.02 | 4.27 |
| jump | 13.4 / 39 | 19.8 | 2.0 / 19 | 0.10 / 3 | 0.05 | 4.20 |
| flip | 15.8 / 42 | 21.3 | 4.0 / 19 | 2.0 / 14 | 0.05 | 4.07 |
| flip_ip | 14.0 / 43 | 20.7 | 4.2 / 17 | 1.8 / 14 | 0.02 | 4.11 |
| flip_back | 14.8 / 43 | 20.4 | 7.2 / 20 | 3.6 / 28 | 0.04 | 4.05 |
| forcejump | 10.4 / 34 | 15.6 | 1.2 / 5 | 0 / 0 | 0.02 | 4.13 |
| alora_flip | 15.7 / 37 | 21.5 | 2.9 / 12 | 0.7 / 10 | 0.02 | 4.21 |
| cartwheel | 13.3 / 43 | 19.4 | 4.9 / 11 | 4.3 / 13 | 0.02 | 3.95 |
| roll | 14.1 / 40 | 20.0 | 2.8 / 11 | 0 / 0 | 0.03 | 3.97 |
| wallrun | 14.6 / 43 | 21.6 | 4.3 / 22 | 1.3 / 12 | 0.03 | 3.95 |
| kick | 12.2 / 44 | 17.0 | 2.6 / 21 | 0.4 / 8 | 0.03 | 3.94 |
| saber_chain | 13.7 / 45 | 19.5 | 2.9 / 14 | 0.2 / 3 | 0.04 | 4.06 |
| saber_run | 11.0 / 39 | 17.3 | 1.8 / 13 | 0.07 / 1 | 0.03 | 4.07 |
| turn_stand | 19.2 / 37 | 25.6 | 0.9 / 5 | 0 / 0 | 0.03 | 4.05 |
| turn_run | 13.9 / 42 | 20.7 | 4.4 / 31 | 2.4 / 15 | 0.03 | 4.10 |
| run_circle | 15.6 / 40 | 23.9 | 2.0 / 6 | 0.04 / 1 | 0.02 | 4.31 |

(All 27 clips incl. the other `_ip` ones: `metrics_final.json`.) Fold frac = fraction of cloth edges folded > 120 deg (UE hare 0.001 mean, 0.011 max).
Kinematic-band particles inside the collider (they are pinned, harmless): 0..2 per sample (UE 39).

## Observations
- Calm clips (idle, walk, run, strafe, saber stand, turn) are clean: hem through the thighs on the run is 1-2 particles per sample, 0.15 core. Images `img/run.png`, `img/idle.png`, `img/saber.png`, `img/contact_sheet.png`.
- Penetration concentrates in tucks and inversions (flip_back, flip, cartwheel, turn_run with the stop blend): thighs folding onto the torso squeeze the sheet (core peaks 28 in flip_back at a tuck, `img/worst_*`). Not a blow-up; skirt stays connected, no inside-out sheet seen (fold fraction <= 0.05), tethers hold (max displacement from the pose 45 cm, bound by maxDist 40 x 1.1).
- In-place variants penetrate about as much as the rooted ones; root motion mostly raises displacement/hem lift (run 9.2 -> 10.8, strafe_l 9.2 -> 13.2 cm), so the pelvis-frame inertia model works with world-space matrices.
- Speed: JKA leg/hip motion (flips 1,000+ cm/s pelvis) is 2-3 x the dance; more particles per step cross the thin-thickness collider. Lower dt helps a lot (125 Hz reel: inside 1.1, core 0.14; 30 Hz: 9.0 / 3.0), so a variable client frame rate changes the look: the hook should substep to a fixed 1/60 or finer step.
- Edge stretch max 8-26x on 0.5-3 cm edges happens in single particles at the fastest frames (UE hare 9.7), not systematic.
- Relevant knobs (not retuned): `substeps`/`iterations` (penetration vs ms), `bendModel`/`bendPasses` (ms), `cellSize` (contact search radius vs hash cost), `thickness`/`backDepth`, `linVelScale*`/`angVelScale` (hem lift on the run: puff 0.92-1.03 of the skinned radius, hem drop +3..+7 cm above skinned on runs, i.e. the skirt rides higher than the pose; UE hare -5.9), `drag/lift`.

## Open problems
1. ms/step is ~4.1 (4.0-4.3) on this PC, no better than UE. Needs solver/knob work (bends 1.3 ms, contact search 1.0 ms), not asset work.
2. Core penetration in tuck/inverted poses (0.4-3.6 mean in flips/cartwheel, peaks 28). A torso collider up to z 150 was tried: no change except flip_back (7.2 -> 5.9 inside); capsule thighs might close tucks.
3. Collider is a thin-shell mix (boots bands, body skin, garment); the 4-nearest-vertex probe is unreliable against it.
4. Skirt on thighs: the UE skinned the strands to thigh-follow bones with animated helpers; here all strands are pelvis, so the kinematic hem target does not follow thigh swings (only the sim does).
5. Fingertip capsule segments missing (`_humanoid` has no third finger joint). Hand contacts occur (turn_stand 13 per sample, arms swinging through the skirt), forearm/hand capsules rebound correctly.
6. `skirt_rigid_*` ribbons and the glm `skirt_*` bone weights are not touched; the renderer hook still has to rewrite skirt normals from sim positions.
7. Pose blending note above applies to any game-side anim blending feeding the hook.

## Decisions for the owner
- Collider: 9.9k verts refined (default) vs 3.6k raw (0.4 ms cheaper, 1.7x the inside-collider particles). 
- Cost: accept 4 ms, or choose a cheaper config (`bendModel=1`: -1.1 ms with similar quality on this reel; `substeps=1` is not recommended).
- Fixed step: run the cloth at a fixed 1/60 or 1/125 s step independent of the client frame rate.
- Whether to add thigh/shin capsules (closed volumes) for tucks, or accept the current rate.
- Tuning by eye (inertia, drag, tethers) as planned; nothing was retuned.
