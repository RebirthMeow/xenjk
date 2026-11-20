# Cloth asset (SKA1) and pose reel (SKP1) formats

Little endian, no padding except where stated. Written by `tools/cloth/pipeline/asset/skaio.py`, read by `tools/cloth/skirt_replay.c`
(`loadSka`) and `skaio.read_ska`. Ripped-asset derived data: never commit, never leave this machine.

## Units and spaces

- The solver keeps its tuning in **centimetres, +Z up, 980.665 cm/s^2**. The asset stores cm, and records the unit scale.
- `unitCm` = cm per skeleton unit = 100 / 37.5 = 2.6666667 (the glm scale is 37.5 units per metre).
  All asset positions are glm-bind-space positions x `unitCm`. Axes are the glm/gla model axes (X = her left, Y = back,
  the face looks to -Y, Z up, right-handed; no axis change from Blender).
- Matrices are **row-vector**, 16 floats, translation in elements 12..14 (the same bytes as a column-major column-vector matrix).
- **Game side contract** (what the cgame hook must hand the solver every frame): `mats[b*16..]` for every bone b of the gla, the
  *skinning matrix* of the bone in a world-aligned frame, translation in cm. In column-vector terms, with S_b the 3x4 bone
  matrix that maps a glm bind vertex of a 100 %-weighted bone to its posed position (the gla chain of the compressed bone
  matrices, glm units), and W the entity transform (origin, yaw; world Z up): `mats[b] = transpose(W * S_b)` with the translation
  column multiplied by `unitCm`. The solver derives the pelvis frame, its velocity and angular velocity from `mats[refBone]`
  (frame = refPose[ref] * mats[ref] = pelvis pose), so **the matrices must be in a frame that is fixed in the world** (not
  entity-relative): the entity's own motion is felt only through the reference bone, which is exactly how the cloth feels a
  run or a jump. The sim output (`SkirtSim_Read`) is in the same world-aligned frame, in cm.
- The reel (SKP1) stores `cst[b] = transpose(W * S_b * base_b)` (pose, not skinning matrix; `base_b` = orthonormalised gla base
  matrix, cm); `mats = refInv * cst` is computed by the harness, as for SKD1 traces. A game hook can skip cst/refInv and
  pass `mats` directly.

## SKA1 asset (`eve2b_skirt.cloth`)

Header (180 bytes):

| offset | type | field |
|---|---|---|
| 0 | char[4] | `SKA1` |
| 4 | u32 | version = 1 |
| 8 | u32 | headerBytes = 180 |
| 12 | f64 | unitCm |
| 20 | u32 x10 | nBones, refBone, nVerts, nTris, nInfl, nColVerts, nColTris, nCaps, nRenderSurfaces, nRenderVerts |
| 60 | f64 x11 | gravity xyz (cm/s^2), density, minPerParticleMass, edge, bend, area, tetherStiffness, drag, lift |
| 148 | char[32] | skeleton name (`_humanoid`) |

Sections, in order (counts from the header; `n` = nVerts, `ni` = nInfl = 4):

1. `boneNames` char[32] x nBones, in gla bone order (`model_root`=0, `pelvis`=1 = refBone, ... 53 bones). The hook binds by name.
2. `refInv` f32[nBones*16]: inverse of the orthonormalised gla base pose (cm, row-vector).
3. cloth mesh: `pos` f32[n*3] (bind, cm), `tri` u32[nTris*3], `maxDist` f32[n] (cm; <= eps_kin pins the vertex to its skinned pose),
   `animDrive` f32[n] (all -1 = "map absent" as in the UE trace), `skinBone` i32[n*ni] (-1 unused), `skinW` f32[n*ni].
4. body collider: `pos` f32[nColVerts*3], `tri` u32[nColTris*3] (`(c-a)x(b-a)` points out of the body), `skinBone` i32[nColVerts*ni], `skinW` f32[nColVerts*ni].
5. capsules: `boneA` i32[nCaps], `boneB` i32[nCaps], zero pad to 8-byte alignment from the start of the file, `radius` f64[nCaps] (cm).
   Segment = bind origins of the two bones; same table as the UE trace (forearm, hand, three finger runs, per side).
6. render map: nRenderSurfaces x { char[32] surface name, u32 vertex count }, zero pad to 4, then `simIndex` u32[nRenderVerts]
   (all surfaces concatenated, glm vertex order within each surface) and `distCm` f32[nRenderVerts] (distance in bind space
   between the render vertex and its sim vertex; 0 for all here, see RESULTS.md).
7. `crc32` u32 of every preceding byte (zlib crc32).

The cloth mesh is the UE 2831-vertex sim topology unchanged (no remesh; same `tri`, `maxDist`, `animDrive`); only the positions
(into glm bind space) and the skin (onto `_humanoid`) are new.

## Version 2: wrapped render map, garment parameters (2026-10-15)

Same magic (`SKA1`), header and sections 1-5; `version` = 2. Written by `skaio.write_ska(..., render_wrap=..., params=...)`;
first used for Vincent's cape (a 2,603-particle sheet driving 11,175 render vertices: both cape shells and the frayed
edge cards). The renderer reads `model.cloth` next to `model.glm` for either version.

6. render map: the surface records as in version 1 (name[32], u32 count; zero pad to 4), then, over all render vertices
   (surfaces concatenated, glm vertex order):
   `renderTri` u32[nRenderVerts*3] (the three particles a vertex is wrapped on), `renderW` f32[nRenderVerts*3] (their
   barycentric weights; they may extrapolate past the triangle, -4..5 accepted, for cards hanging past a hem),
   `renderOff` f32[nRenderVerts] (offset in **glm units** along the normalised interpolation of the three particles'
   sim normals), `renderSgn` f32[nRenderVerts] (+1/-1: the render normal's side of that normal, so a reversed back
   shell on the same positions is lit from its own side).
   Render position = sum w_k x_k + off * n, normal = sgn * n, n = normalize(sum w_k n_k), with n_k the area-weighted,
   unsigned vertex normals of the sim mesh.
7. parameters: u32 byte count (<= 65536), then that many bytes of `key value` lines (solver parameters from
   `SKIRT_PARAM_LIST`, e.g. `flareCone 0` for a cape), zero pad to 4. They apply after the game profile
   (`r_clothProfile`) and before `r_clothParam` overrides; profile switches and `r_clothParam reset` return to them.
8. `crc32`.

A render vertex is skinned like the body (from the glm's own weights) when every particle it is wrapped on with a
non-zero weight is pinned; otherwise it follows the sim. Version 1 assets are converted to the same form at bind time
(one particle at weight 1, no offset, the asset-wide normal sign), so all back ends draw from one representation.

## SKP1 pose reel (`reel.skp`)

| | |
|---|---|
| char[4] | `SKP1` |
| u32 | version = 1 |
| u32 | jsonBytes |
| json | `nBones`, `fps`, `clips` [{`name`, `start` (frame), `frames`, `evalStart` (frames of idle pre-roll), `rooted`}], `boneNames`, `unitCmPerSkelUnit`, `note` |
| frames | u32 index, f32 dt, u32 flags (1 = valid), f32 cst[nBones*16] |

The solver is reset on the first frame of every clip. Frame record size is 12 + nBones*64 bytes.

## Harness

```
skirt_replay_f.exe --asset eve2b_skirt.cloth --pose reel.skp [--clips a,b] --out pos.bin [--stats per_frame.csv] [--set key=value ...] [--quiet]
skirt_replay_f.exe --dump hare.skd --out pos.bin          (SKD1 path, unchanged)
```
`pos.bin` = `SKC1` u32 nVerts u32 nFrames, then per frame u32 index + f32[nVerts*3] (cm, world-aligned frame).
`--stats` = csv frame, clip, ms, contacts, hand contacts, tethers, nan. Prints one `CLIP` line per clip (mean / median / p95 / max ms).
`tools/cloth/pipeline/asset/skd_to_ska.py` converts an SKD1 trace to SKA1 + SKP1 (used to prove the new loader is bit-identical to the SKD1 path).
