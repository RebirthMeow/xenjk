# Cloth authoring pipeline

Turns a rigged character model into a JKA player model with its own skeleton
and a skirt cloth asset (`SKA1`, see [FORMAT](../../../docs/cloth/FORMAT.md)).
It was written for one model, Eve 2B (`eve2b`, `eve2b_np`), on 2026-10-12.
Work notes with the measured results are in
[docs/cloth/records](../../../docs/cloth/records/). The solver and its offline
harness are `codemp/cgame/cloth/skirt_sim.c` and
[`tools/cloth/skirt_replay.c`](../skirt_replay.c).

Every script finds the repository from its own location
(`REPO = Path(__file__).resolve().parents[4]`). Data stays in
`.codex-research/<dir>` (untracked); the scripts only read and write there.
The scripts were written for Windows (`\` joins in a few places) and are run
from the repository root.

## Never committed

The source model is commercial game data. These stay local and out of
Git, and none of them is in this tree:

- the source model and its textures: `Import/eve_2b.blend` in the skirt lab folder (environment variable
  `SKIRT_CLOTH_LAB`), `characters/eve` in the cloth research folder (`CLOTH_RESEARCH`; `eve_2b.blend`, `tex/*.png`);
- the Unreal skirt trace `Out/chaosref/hare.skd` in the skirt lab folder (the sim
  mesh, collider and skin of the original skirt);
- everything the stages write: `eve_mesh.npz(.json)`, `eve_*.npz`,
  `build/`, `*.glm`, `*.gla`, `*.pk3`, `*.cloth`, `*.skp`, `stock_F.npy`,
  `transform.json`, `bone_mapping.json`, `targets_*.json`, screenshots, logs;
- the stock JKA assets extracted from `base/assets*.pk3`.

## Requirements

- Python 3 with `numpy`, `scipy` (asset stage), `Pillow` (textures, sheets),
  `matplotlib` (`preview_glm.py`, `asset/render.py`).
- Blender with the add-on in [`tools/cloth/blender/jediacademy`](../blender/jediacademy)
  (Cagelight/mrwonko Blender-Jedi-Academy-Tools v2.1.0, GPL; installed by
  putting `tools/cloth/blender` on Blender's path, as `model/verify_import.py`
  does). It ran unpatched on Blender 5.2.1. Used only by `verify_import.py`;
  the GLM is written by the standalone `model/jka.py`.
  Scripts that `import bpy` (`recon_eve.py`, `export_eve_mesh.py`,
  `decimate_eve.py`, `verify_import.py`) run only inside Blender:
  `blender -b <file.blend> --python <script> -- <args>`.
- `game_directory/Jedi Academy/GameData/base/assets0..3.pk3` for the stock
  `_humanoid.gla`, `animation.cfg`, `kyle` and `jan` (extracted by `model/extract.py`).
- For in-game checks only: a client exe under the research folders and
  `tools/renderer_bench.py` (`run_client*.py`, localhost, private homepath).

## The chain

Work dirs below are under `.codex-research/`: `G` = `eve2b-glm-20261012`,
`N` = `eve2b-nativeprop-20261012`, `C` = `eve2b-np-cloth-20261012`.

| stage | folder | reads | writes |
|---|---|---|---|
| 0 stock assets | `model/extract.py`, `dump_stock.py` | `assets0..3.pk3` | `G/src/stock/models/players/{_humanoid,kyle,jan}` |
| 1 model to GLM | `model/` | `eve_2b.blend`, stock | `G/eve_mesh.npz(.json)`, `G/eve_lite.npz`, `G/build/lite/model.glm`, `bone_mapping.json`, `transform.json`, textures, pk3 |
| 2 own skeleton | `skeleton/` | stage 1 data, stock GLA | `N/build/lite/{model.glm,joints_np.json,...}`, `N/gla/eve2b_np.gla`, pk3 |
| 3 cloth asset | `asset/` | `hare.skd`, `G/eve_mesh.npz`, `N/build/lite/*`, `N/gla/eve2b_np.gla` | `C/out/eve2b_np_skirt.cloth`, `asset_extra.npz`, `reel.skp`, metrics |

Stage 1 puts the model on the stock `_humanoid` skeleton (`eve2b`); stage 2
re-skins it onto a new skeleton with her own proportions, written as
`models/players/_humanoid/eve2b_np.gla` so the game accepts it as a player model
(see the nativeprop record for why the path matters). Stage 3 needs the stage 2
GLA and GLM; the stage 1 `eve2b` GLM is only the older, stock-skeleton variant.

### Stage 1: model (`model/`)

```
python tools/cloth/pipeline/model/extract.py
blender -b eve_2b.blend --python tools/cloth/pipeline/model/recon_eve.py -- G/recon_eve.json
blender -b eve_2b.blend --python tools/cloth/pipeline/model/export_eve_mesh.py -- G/eve_mesh.npz
blender -b eve_2b.blend --python tools/cloth/pipeline/model/decimate_eve.py -- G/targets_lite.json G/eve_lite.npz
python tools/cloth/pipeline/model/build_glm.py lite G/targets_lite.json G/build/lite eve2b
python tools/cloth/pipeline/model/make_textures.py G/build/lite/tex
python tools/cloth/pipeline/model/package.py G/build/lite eve2b G/zz_eve2b_test.pk3
```

`targets_lite.json` maps material to triangle target, e.g. `{"M_Body":2400,
"M_Upper":5500,...,"M_Skirt":0,"M_SkirtRigid":0}` (0 keeps the material; the
skirt must stay undecimated, `build_glm.py:371`). `check_transform.py`,
`preview_glm.py`, `sheet.py`, `verify_import.py` check the result;
`make_cfg.py` and `run_client.py` make and run the in-game screenshot list.

### Stage 2: native-proportions skeleton (`skeleton/`)

From the nativeprop record ("Rebuild"; the `build_np.py` argument is the
stage 1 targets file and an absolute output directory):

```
python tools/cloth/pipeline/skeleton/build_np.py lite G/targets_lite.json <abs N>/build/lite eve2b_np
python tools/cloth/pipeline/skeleton/make_gla_np.py N/build/lite/joints_np.json N/gla/eve2b_np.gla N/build/lite/model.glm
python tools/cloth/pipeline/skeleton/package_np.py N/build/lite eve2b_np N/zz_eve2b_np_test.pk3 N/gla/eve2b_np.gla
```

Copy `tex/` and the icon from the stage 1 build into `N/build/lite` first.
Checks: `proportions.py`, `contact_report.py`, `grip_check.py` (write
`proportions.json`, `contact_report.json`, `grip_report.json` into `N`);
`make_gla_np.py --identity` re-encodes the stock skeleton unchanged as a decode
check. `retarget_lib.py` and `jkagla.py` are the GLA maths (engine
`G2_TransformBone` semantics). `t_decode.py`, `t_local.py`, `t_anchor.py` are
decode experiments; `t_local.py` regenerates `N/stock_F.npy` (72 MB stock skin
matrices) that `t_anchor.py` reads. `make_pbr_pack_np.py <out.pk3> eve2b_np`
builds a rend2 PBR pack (not tested). `make_cfg_np.py`, `run_client_np.py`,
`sheet_pairs.py` are the in-game comparison.

### Stage 3: SKA1 asset (`asset/`)

This is the current pipeline. It is the earlier `eve2b-cloth-asset-20261012`
script set rebound to the stage 2 files (record: eve2b-np-cloth). Run from `C/out`:

```
python <repo>/tools/cloth/pipeline/asset/build_cloth_asset.py eve2b_np_skirt.cloth --zmin 44.6 --zmax 123.6 --maxedge 2.5 --report asset_report.json
python <repo>/tools/cloth/pipeline/asset/make_reel.py reel.skp reel_meta.npz
<harness build>/skirt_replay_f.exe --asset eve2b_np_skirt.cloth --pose reel.skp --out final_f.bin --stats final_f.csv
python <repo>/tools/cloth/pipeline/asset/analyze.py asset_extra.npz reel.skp final_f.bin final_f.csv metrics_final.json
```

The harness is built from `tools/cloth/CMakeLists.txt` (`skirt_replay.c`).
`compare.py` (two metrics files), `diag_band.py`, `summarize.py`, `worst.py`,
`render.py` produce tables and images; `make_cfg_npc.py`, `run_client_npc.py`,
`sheet.py` run the in-game check. `skaio.py`, `skdio.py`, `glaio.py`,
`common.py` are the file readers and the Eve/sim/glm matching. The zmin/zmax
values are the record's collider band for `eve2b_np`; the command above is
reconstructed from the record, not from a saved shell line.

Not copied: the first-asset `dbg*.py`, `t_h2.py`, `shots_extra.py`,
`skd_to_ska.py` (SKD1 to SKA1 converter named in FORMAT.md), `patch_replay.py`,
`reel_code.c.txt`, `run_final.sh`; nothing in this tree imports them.

## Eve-specific: change these for a new model

Source rig and mesh
- Object name `'Eve'`, armature lookup, 112,384-vertex mesh, shape keys cleared:
  `model/export_eve_mesh.py:3-4`, `model/decimate_eve.py:8`.
- Bone names (`Bip001-*`, `Ab-*`, `Dm-*`, `Root`) to `_humanoid` bones:
  `model/build_glm.py:31-41` (`core`), `:42-` (`override`: twist, knee, elbow);
  `skeleton/build_np.py:35-45`, `:46-` (same tables). Rest-fit chain
  `keyspec`: `build_glm.py:94-111`, `build_np.py:98-115`; her joint table for
  the new GLA: `build_np.py:241-257`.
- Materials and surface tree: `M_*` names and skip of `M_Hidden`
  (`build_glm.py:181`, `build_np.py:188`), material key table `MATKEY`
  (`build_glm.py:232`, `build_np.py:291`), bone to body region
  (`region_of`, `build_glm.py:208`, `build_np.py:215-224`), surface limits
  `MAXV, MAXT = 880, 1800` and 31 bones (`build_glm.py:16`, `build_np.py:20`),
  skirt surface names `skirt_*`, `skirt_rigid_*` (`build_glm.py:294`,
  `build_np.py:353`), skirt must not be decimated (`build_np.py:437`).
- Decimation targets: `targets_lite.json` (per material).
- Textures and file names: `model/make_textures.py:6-28`,
  `skeleton/make_pbr_pack_np.py`.

Transform
- Scale 37.5 JKA units per metre (`S`) and sole height `Z0 = 3.12`:
  `build_glm.py:14-15`, `build_np.py:18-19`, `asset/common.py:11`. Axes are
  assumed identical to JKA (X left, face -Y, Z up), no object transform.
- Boot-sole lift `FOOT_DZ` from the `M_Boots` surface: `build_np.py:210`.

Skeleton retargeting (stage 2)
- Proportion ratios (hand, thigh, head, root) and the bones derived from
  them (`hang_tag_bone`, `tail`, face bones, `Motion`): `build_np.py:259-275`.
- Ground reference frame 14651 (`BOTH_STAND1`, kyle's soles): `make_gla_np.py:69`;
  anchors for humerus/fingers/hang tags: `make_gla_np.py:72-78`; root scale
  `:80-82`; two-handed grip IK block from `:117`.

Cloth asset (stage 3)
- Unreal skirt trace used as sim mesh, collider and skin: `asset/common.py:9`
  (`SKD`), `asset/analyze.py:25`. The sim vertex to Eve vertex match
  (UE cm to Blender m is `(x,-y,z)/100`): `common.py:24-30`.
- Skirt vertex selection: glm surfaces `skirt_0..3` (`common.py:13`) and the
  `skirt_surfaces` / `original_vertex_indices` of `transform.json`
  (`common.py:27,39`).
- Collider band in glm cm: `--zmin/--zmax` defaults 35/128
  (`build_cloth_asset.py:10`); the record used 44.6/123.6 for `eve2b_np`.
  Collider surface list `BODY` (`build_cloth_asset.py:109`); clothing-covered
  `M_Body` triangles dropped by ray test against `M_Upper`/`M_Boots`
  (`:144-165`).
- Reference bone `pelvis` (`build_cloth_asset.py:77`), hand/finger capsule table
  and radii (`:188-195`), UE to `_humanoid` bone map for metrics
  (`analyze.py:35`).
- Which GLA and `animation.cfg` supply `refInv`, the pose and the reel:
  `asset/glaio.py:8-9`; reel clip list and entity speeds
  (`BOTH_*` sequences, `RUNV`): `asset/make_reel.py:94-122`.
