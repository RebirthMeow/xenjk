# Bringing characters in from other games

This chapter is the path from a character in another game (FF7 Rebirth, Stellar Blade, Cyberpunk 2077, Witcher 3
Next-Gen, Marvel Rivals, anime and toon models, fan Blender models) to a JKA player model that loads and plays in xenjk:
skeleton fitting onto the JKA humanoid, mesh and GLM export, materials, hair chains, cloth, packs, the model menu, voices
and the checks. It also collects the rules we learned the hard way. You extract every asset from your own copy of the
game; converted characters stay on your machine and are never shipped, committed or uploaded, and nothing in this repo
contains one.

**Status.** The engine side (own-skeleton GLAs under `_humanoid/`, the 10,000-vertex surface cap, `.mtr` materials,
`.chains` and `.cloth` sidecars, `.joints`, the `cg_xenAnims` set, the character picker) is in master and in the
installed game. The repo carries the *generic* tools: the Blender GLM add-on, the hair-chains builder, the cloth
pipeline written for one model, the pack post-processors and the roster check. Most per-game converters (UE, CP2077,
FF7R, Witcher 3, the toon makers) are local research scripts under `.codex-research/` and are **not in the repo**; this
chapter says where that is the case and what you need to recreate them.

## What it does

```
your game install
  -> exporter (CUE4Parse-based for UE games, WolvenKit for CP2077, a Blender add-on for W3 .blend files)
  -> .glb / .fbx / .blend + material json (+ physics json where the game has it)
  -> prep in Blender (facing, scale, rig fixes, drop proxies, bake multi-UV materials to one set)
  -> extract: mesh, top-4 skin weights, skeleton, calm face from morphs        -> .npz
  -> build: bones mapped to the 53 _humanoid bones, rest fit, split surfaces   -> model.glm
  -> own GLA: every stock frame retargeted to this model's joints              -> _humanoid/<model>.gla
  -> pack: textures (BC7 DDS) + .shader + .mtr + .skin + icons                 -> zz_<model>.pk3
  -> sidecars: model.chains (hair, springs), model.cloth (garments)            -> zz_<model>_phys.pk3
  -> checks offline, then one in-game look/motion pass, then install
```

### The skeleton contract

- **A GLM carries no skeleton.** Ghoul2 skins every vertex with `animMatrix x inverse(basePose)` from the GLA. Every
  player model in stock JKA bends about `_humanoid.gla`'s joints (Kyle's). A converted body with its own proportions
  bent about Kyle's pivots shears at the elbows, wrists and knees.
- Two fixes exist; both are client side:
  - **Own GLA** (what every hero convert uses): a copy of `_humanoid.gla` whose base pose sits on this model's
    joints, with every stock frame retargeted (`make_gla_np.py`). It must be at
    `models/players/_humanoid/<name>.gla` and keep the 53 bone names and the stock frame layout, because
    `animation.cfg` frame numbers index into it. The game's own check (`codemp/game/g_client.c`, `SetupGameGhoul2Model`)
    only tests that the GLA path contains `players/_humanoid/`.
  - **A `.joints` sidecar** beside the GLM (`r_g2Joints 1`): the renderer moves each bone to the model's own joint after
    the stock transform. Used by converts that stay on `_humanoid.gla` ([g2-joints.md](../g2-joints.md)).
- The server traces every ordinary player against Kyle regardless, so hitboxes never depend on a convert.
- **Animations are deltas from the rest pose.** A T-pose or raised-shoulder source rig keeps a shrug and a bowed head
  in every animation even with a correct GLA. The fix is twofold: do not level the clavicles to stock in the rest fit
  (`clavicle_fit 0`), and post-process the own GLA so collarbones turn about 10 deg down and the cranium about 13 deg
  chin-up (a research script, `gla_relax.py`). Measure the idle against Kyle (shoulder below neck, head nod; Kyle's
  nod is 9 deg in BOTH_STAND1) before and after.
- **Ground smoothing.** The retargeter's per-frame floor correction jumps when the lowest point switches between toe,
  heel and foot: a hip pop twice per run stride, invisible in stills. The repo's `make_gla_np.py` smooths only with
  `--smooth-loops [sigma]`; always pass it. Check with the pelvis rise per key on BOTH_RUN1 (stock 0.79 u; installed
  own GLAs 0.67-0.93).
- **Twist check.** A model prepped at the wrong facing looks right in stills but the hip joints sit front-to-back, and
  every animation twists the body. Measure the hip line against the shoulder line in BOTH_STAND1 (stock Kyle 7 deg,
  a broken GLA 85 deg).

### Scale and units

- 37.5 JKA units per metre, Z up, the model faces -Y in GLM/GLA space (X = the model's left). Stock reads 1 u as about
  an inch; Kyle stands about 64 u.
- **Keep the source scale.** One unit conversion, the height that is in the source model. No canon heights, no
  per-character targets. If a whole game's characters are uniformly off, one factor per source game is acceptable
  (we used x1.17 for one toon game whose cast all came out at 1.45 m). Check that a rig did not come in 100x (FBX) or
  1.2x (UE mannequin rigs).
- Feet: the stock soles sit at about +3.1 u. A converter that puts the floor at z = 0 sinks the feet; lift by about
  3.2 u. A long gown sets the floor after retarget, not before.

### Mesh limits

| Limit | Value | Where |
|---|---|---|
| Vertices per surface | 10,000 (stock 1,000); split at about 9,000 | `SHADER_MAX_VERTEXES`, `codemp/qcommon/qfiles.h` |
| Bones per surface | 32 (5-bit index); split at 31 | `codemp/rd-common/mdx_format.h` |
| Weights per vertex | 4 | `mdx_format.h` |
| UV sets | 1; no vertex colour, no tangents, no morph targets | GLM format |
| Name length | 63 characters for image and material names (`MAX_QPATH` 64) | a 64+ name is silently cut, or `R_CreateImage "... is too long"` drops you to the menu |
| `.chains` surface names | 31 characters | a longer GLM surface name drops the whole hair sidecar |
| Bone palette | 256 matrices: 53 skeleton bones + hair and spring bones | `HAIR_MAX_PALETTE`, `codemp/rd-common/tr_hair_local.h` |

Hero converts are built at source quality first and cut back with numbers; stock caps are not the target.

### Materials

- rd-vulkan reads **`X.mtr` only in place of an existing `X.shader`**: `tr_shader.cpp` lists `shaders/*.shader`
  and, for each, opens the `.mtr` of the same name if there is one. A pack with only a `.mtr` loads none of it, and
  the albedo still shows by name, which hides the mistake. Ship both and look for
  `...loading 'shaders/<x>.mtr'` with `developer 1`.
- Without explicit maps, a lit stage looks beside its albedo `X` for `X_rmo`, `_rmos`, `_moxr`, `_mosr`, `_orm`,
  `_orms` (physical) and `X_n`, `X_nh` (normal). Explicit keywords (`ormMap`, `normalMap`, `detailNormalMap`,
  `materialMask`, `subsurface`, `sheen`, `hairAnisotropy`, `layer`, the `eye` mode, `toon`/`toonLight`) are listed in
  renderer-vulkan-in-xenjk.md, section "Material masks, combined modes ...".
- **Roughness is perceptual** (UE / glTF convention; GGX alpha = r^2). Store the source value. Packs baked under the
  older convention carry `roughnessEncoding alpha` or `metalpow` on their stages.
- **Normal maps:** DirectX green (UE, Stellar, FF7R, CP2077, W3) needs G flipped; OpenGL sources (FFXVI) do not.
- **Mask channels** (`materialMask`): R subsurface, G sheen, B hair shift, A detail weight.
- **Textures:** BC7 DDS at source size, power-of-two sides. The renderer looks up every texture name as `<name>.dds`
  first, so a DDS beside a PNG replaces it.
- **Layer images** (`layer` keyword) are named as PNG; the renderer builds the layer array from BC7 DDS files beside
  them only when every layer of the array has one of the same size and format (`R_LoadArrayDDS`, `vk_dds.cpp`).
  Use `pack_layer_dds.py`; a layer shipped as a DDS alone, with no PNG, fails to load ("could not load layer image").
- **Pillow premultiplies RGBA when resizing.** Resize every channel on its own, or alpha that is data (masks) blackens
  the colour.

## Take this piece

**Depends on:** [renderer.md](renderer.md) (rd-vulkan, the material keywords; a GLM convert itself loads on any
renderer, the PBR materials, hair and cloth need rd-vulkan); [characters-rendering.md](characters-rendering.md) (how
the hair and cloth sidecars are simulated and drawn, `.joints`, G2 blending, the texture pool, model loading).

### Files

New files (copy whole):

| Path | Role |
|---|---|
| `tools/cloth/blender/jediacademy/` | Blender GLM/GLA import/export add-on (mrwonko + cagelight fork, GPL). Stock GLM round trips; verifying converts |
| `tools/cloth/pipeline/model/` | Stage 1, model to GLM on the stock skeleton: `jka.py` (standalone GLM writer), `build_glm.py`, `extract.py` (stock assets from your `assets*.pk3`), `package.py` |
| `tools/cloth/pipeline/skeleton/` | Stage 2, own skeleton: `build_np.py` (re-skin on own joints), `make_gla_np.py` (own GLA, all stock frames retargeted, grip IK), `jkagla.py` + `retarget_lib.py` (GLA maths), `proportions.py`, `contact_report.py`, `grip_check.py`, `package_np.py` |
| `tools/cloth/pipeline/asset/` | Stage 3, SKA1 cloth asset: `build_cloth_asset.py`, `skaio.py` (SKA1 writer/reader), `make_reel.py`, `analyze.py` |
| `tools/cloth/skirt_replay.c`, `tools/cloth/CMakeLists.txt` | Offline cloth harness against the engine solver |
| `tools/hair/build_chains.py`, `presets.json`, `chainsio.py` | Hair sidecar (HCH1) from a rig description, presets and fitted colliders |
| `tools/hair/hc_eval.c`, `hc_eval.py`, `hc_replay.c`, `hcd_to_hcr.py`, `hc_frames.c`, `simcheck_run.py` | Hair solver as a library for fitting, dump replay, per-frame step check, in-game check |
| `tools/characters/pack_dds_convert.py` | PNG/TGA/JPG in a pack get BC7 (or BC1) DDS beside them |
| `tools/characters/pack_layer_dds.py`, `pack_layer_rgba8.py` | DDS for layer arrays (BC7; RGBA8 for arrays BC7 cannot hold) |
| `tools/characters/pack_bc1.py` | Opaque non-colour maps re-encoded as BC1 when within 40 dB; `--colour` adds opaque albedo at 37 dB |
| `tools/characters/pack_mip_cap.py` | DDS top mips above `r_ddsMaxSize` (4096) dropped, no re-encode; `--detail-psnr 45` also drops a top level with no detail of its own |
| `tools/characters/pack_texture_dedupe.py` | One file per byte-identical texture group, shader references rewritten |
| `tools/characters/fma_mip_fix.py` | Rebuild mip chains a premultiplying resize blackened |
| `tools/characters/packio.py` | The pack rewrite (copy with entries replaced, dropped or added) and box filter those tools share |
| `tools/pack_order.py` | The engine's pk3 order as a Python sort key (character and BSP tools) |
| `tools/characters/cook_tangents.py` | Pre-computes the renderer's MikkTSpace tangent cache for installed GLMs |
| `tools/characters/manifests.py`, `xen_character.menu` | `character.cfg` per model folder and the picker menu, as a pack |
| `tools/characters/roster_check.py`, `roster_driver.c` | Builds the picker roster from installed packs with `ui_roster.c` itself and checks it |

Engine pieces the converts rely on (owned by other chapters; listed so you know what a convert needs):

| Path | What a convert needs from it |
|---|---|
| `codemp/qcommon/qfiles.h` | `SHADER_MAX_VERTEXES` 10000 (one-line hook; needed for any hero mesh) |
| `codemp/game/g_client.c` `SetupGameGhoul2Model` | accepts any GLA under `players/_humanoid/` (stock behaviour) |
| `codemp/cgame/cg_xenanims.c` (`XEN_ANIM_DIR`, `CG_XenAnimSetForModel`) | plays `models/players/_humanoid_xen/animation.cfg` on models whose GLA holds the appended frames; `cg_players.c` only calls `CG_AnimSet` where it picks an animation set |
| `codemp/rd-vulkan/tr_shader.cpp` | `.mtr` beside `.shader`, material keywords, implicit companion maps |
| `codemp/rd-common/tr_hair.cpp`, `tr_cloth.cpp` | load `models/<m>/model.chains` and `model.cloth` next to `model.glm` |
| `codemp/rd-common/tr_g2joints.cpp` | `<model>.joints` |
| `codemp/ui/ui_roster.c`, `ui_picker.c` | `character.cfg`, the picker (`ui_characters`) |
| `codemp/qcommon/files.cpp` (`paksort`, `FS_PathCmp`) | pack order (stock behaviour) |

### Build inputs

- Python 3 with `numpy`, `scipy`, `Pillow`, `matplotlib`; the pack tools also need `etcpak` (BC7/BC1 encode) and
  `texture2ddecoder` (decode for the proofs). Run them with `python -I`.
- Blender 5.x (the add-on ran unpatched on 5.1 and 5.2.1). Put `tools/cloth/blender` on Blender's script path, or copy
  `jediacademy` into its add-ons folder.
- gcc for the hair library and harness (build lines in [hair/README.md](../hair/README.md) and
  [hair/PRESETS.md](../hair/PRESETS.md)), gcc for `roster_driver.c` (`roster_check.py` compiles it), CMake for
  `tools/cloth/CMakeLists.txt`, MSVC for `cook_tangents.py`.
- No game build change is needed to convert a character. `CMakeLists.txt` entries for the engine pieces belong to their
  chapters.

### Environment

The converters need no location variables except where noted. They find the installed game at
`game_directory/Jedi Academy/GameData` in the checkout (`roster_check.py` takes `--base`).

| Variable | Read by | Meaning |
| --- | --- | --- |
| `CLOTH_RESEARCH` | `tools/cloth/pipeline/model/make_textures.py`, `skeleton/make_pbr_pack_np.py` | the cloth research checkout holding the source model's textures (`characters/eve/tex`); the Eve-specific scripts exit without it |
| `SKIRT_CLOTH_LAB` | `tools/cloth/pipeline/asset/analyze.py`, `common.py` | the skirt cloth lab folder (reference skeleton and meshes) |
| `ProgramFiles(x86)` | `tools/characters/cook_tangents.py` | Windows only: where `vswhere` finds MSVC to compile the tangent cooker |

### Cvars and commands

| Cvar / command | Default | Why a converter cares |
|---|---|---|
| `r_g2Joints` | 1 | models with a `.joints` sidecar bend about their own joints |
| `cg_xenAnims` | 1 | the `_humanoid_xen` animation set on models whose GLA holds it; 0 = stock set |
| `r_hair`, `r_hairSprings`, `r_hairStats` | 1, 1, 0 | hair and body physics on/off; print `HAIRSTATS` / `HAIRSPRINGS` |
| `r_cloth` | 1 | garment cloth |
| `r_toon` | 1 | toon stages; 0 to isolate outline vs texture |
| `developer 1` | 0 | shows which `.shader` / `.mtr` files load |
| `ui_characters` | command | opens the character picker |

Everything else: [xenjk-cvars.md](../xenjk-cvars.md).

### Data formats

| File | In the pack at | Spec |
|---|---|---|
| `model.glm`, `<name>.gla` | `models/players/<m>/`, `models/players/_humanoid/` | stock Ghoul2 (`codemp/rd-common/mdx_format.h`) |
| `model.chains` (HCH1 v2) | `models/players/<m>/` | [hair/FORMAT.md](../hair/FORMAT.md) |
| `model.cloth` (SKA1 v1/v2) | `models/players/<m>/` | [cloth/FORMAT.md](../cloth/FORMAT.md) |
| `<model>.joints` | beside the GLM | [g2-joints.md](../g2-joints.md) |
| `.shader` + `.mtr` | `shaders/` | rd-vulkan keywords: renderer-vulkan-in-xenjk.md |
| `character.cfg` | `models/players/<m>/` | comment block at the top of `codemp/ui/ui_roster.c` |
| `animation.cfg`, `animevents.cfg`, `glas.txt` | `models/players/_humanoid_xen/` | the comment above `XEN_ANIM_DIR` in `codemp/cgame/cg_xenanims.c` |
| `sounds.cfg` | `models/players/<m>/` | stock JKA |

### Porting notes

- **The GLM and own GLA are stock formats.** A convert on its own GLA loads in any OpenJK renderer. What does not carry
  over: `.mtr` PBR materials (rd-vulkan's `USE_VK_PBR`; rend2 has its own `.mtr` dialect with a different stage
  order), `.chains`, `.cloth` and `.joints` (rd-vulkan only; rd-vanilla keeps hair rigid), BC7 DDS loading (needs
  DDS support in the renderer).
- **Raise `SHADER_MAX_VERTEXES`** in the target tree, or keep surfaces under 1,000 vertices. It sizes `tess` arrays
  in every renderer and the server's trace code: rebuild everything.
- **32 vs 64-bit:** nothing in the formats depends on it. A hero pack's 100-370 MB of textures is the practical limit
  on a 32-bit client's address space.
- Stock `paksort` and `FS_PathCmp` behave the same in every OpenJK tree, so the pack naming rules below apply
  everywhere.

## Do it yourself

Placeholders: `<work>` your conversion folder (outside the repo or in an ignored research folder), `<m>` the model
folder name (29 characters at most, lower case), `<base>` your `GameData/base`.

### 1. Export the character from your own copy

- **Unreal games** (Stellar Blade, FF7 Rebirth, Marvel Rivals, many anime games): a CUE4Parse-based exporter (FModel,
  or a CUE4Parse script of your own) for meshes as glTF/PSK, textures, skeletons and the material instance chain as
  JSON. No Unreal Editor. Export the material instances' parameters too: you need them to audit materials later.
  Physics where it exists: PhysicsAsset bodies, AnimDynamics/Kawaii nodes, SpringBone nodes, NvCloth assets.
- **Cyberpunk 2077:** WolvenKit (CLI) to extract the appearance's meshes, `.mi`/`.mt` materials and textures. Its mesh
  exporter can crash on a part and the part silently vanishes: count parts against the appearance.
- **Witcher 3 Next-Gen:** a Blender `.blend` from the community Witcher 3 Blender tools add-on; its material node
  groups are the reference.
- **Fan Blender models:** check the licence, and keep the licence file with the pack.

Render the raw asset in Blender Cycles first. That render, or screenshots of the game, is the look reference you judge
the convert against.

### 2. Prep in Blender

Fix these before any conversion; each one cost us a rebuild:

- **Facing.** The research extractor turns the prep glb -90 deg about Z, so prep leaves the model facing +X. Witcher
  3 faces +Y (prep -90 deg), XPS +90 deg. Confirm with the twist check (step 4), not a still.
- **Scale.** Source scale; check for 100x and odd rigs.
- **Rotation mode.** Blender's glTF import leaves bones in QUATERNION mode, which ignores `rotation_euler`.
- **Inspect the skeleton.** Mirrored inverse binds (an upside-down skeleton with a fine mesh), bones parked under the
  feet, dangle/face bones with no parent (parent them to the head), hair skinned to a non-rig bone, unskinned rigid
  props (bind them to their slot bone), a missing thumb bone (add a weightless one).
- **List which bones carry each limb.** Some rigs skin the shins to a helper chain under the pelvis, hang the thighs off
  the spine, or leave UpperArm/Forearm/Thigh weightless and put the skin on twist bones. Map such bones explicitly
  (`fit_alias`, twist bones to `humerusX` / `radiusX` / `femurX`).
- **Drop what is never drawn:** UE cloth proxy sections (`bDisabled`), shadow meshes, collision bodies, engine
  placeholder materials, shape-key-only expression cards. A drawn proxy z-fights the real panels.
- **One UV set.** Bake any material that samples several UV sets to one atlas on the kept set. Bake painted faces at
  4096 if 2048 smears features.
- **Calm face.** The GLM has no morph targets: bake the default face (apply its morph deltas, then clear shape keys).
- **Witcher 3:** a character is a hierarchy of armatures (main skeleton, one per part, face rig, dangles). Merge them
  into one armature from each bone's posed world matrix by name; FBX export drops the Copy Transforms constraints.

### 3. Extract and build the GLM

The repo's stage 1 and 2 scripts were written for one model and hard-code its object names, bone tables, materials and
surface limits; `tools/cloth/pipeline/README.md`, section "Eve-specific: change these for a new model", lists every
line to change. The research builders generalise them with a per-character JSON config. What either has to do:

1. Read mesh, normals, one UV set, top-4 skin weights (renormalised) and the skeleton.
2. Map bones to the 53 `_humanoid` bones: a fixed core table (pelvis, lower/upper/thoracic spine, cervical, cranium,
   clavicles, humerus, radius, hand, femur, tibia, talus, the d1/d2/d4 finger joints) plus overrides; every other bone
   merges into its nearest mapped ancestor. JKA has 3 digits x 2 joints per hand: middle, ring and pinky all go to `d4`.
3. **Rest fit:** rotate each mapped bone so it points along the stock bone's direction, keeping the model's own joint
   positions. Per-character options we needed: `clavicle_fit 0` (keep source clavicles) when the shoulder joint moves
   more than about 1 u; `hair_hang` (slerp hair bones from the head's fit toward identity down the strand, else long
   hair fans out); `floor_lift` / `floor_meshes`; `pose_hands` for open source hands.
4. Split surfaces at 31 bones and about 9,000 vertices; name surfaces short (31 characters for `.chains`).
5. Keep, per GLM vertex, its source vertex index (`transform.json` / `.npz`): the hair, spring and cloth builders need it
   to re-skin from source weights.
6. Write the GLM (`tools/cloth/pipeline/model/jka.py`, or the Blender add-on's exporter with the GLA under the export
   base path).

Repo commands (Eve 2B's chain; `G` and `N` are your work folders):

```
python tools/cloth/pipeline/model/extract.py
blender -b <source.blend> --python tools/cloth/pipeline/model/export_eve_mesh.py -- G/eve_mesh.npz
python tools/cloth/pipeline/model/build_glm.py lite G/targets_lite.json G/build/lite <m>
python tools/cloth/pipeline/skeleton/build_np.py lite G/targets_lite.json <absolute N>/build/lite <m>
```

`extract.py` reads the stock `_humanoid.gla`, `animation.cfg`, `kyle` and `jan` from `game_directory/.../base/assets*.pk3`
inside the checkout. Stock GLMs round-trip through Blender with the add-on: weld the UV-seam splits before subdividing,
triangulate after, limit to 4 weights, and hide one of any twin surface sets before baking AO.

### 4. Own GLA

```
python tools/cloth/pipeline/skeleton/make_gla_np.py <joints.json> <out>/<m>.gla <model.glm> --smooth-loops 2
```

- Reuse an existing GLA when the joints match within 0.05 u; otherwise build one per model.
- `--identity` re-encodes the stock skeleton unchanged (a decode check). `--no-ik` skips the two-hand grip IK.
- Check: pelvis rise per key on BOTH_RUN1 (hip pop), `grip_check.py` (left-hand miss per frame), `proportions.py`,
  `contact_report.py`, and the twist check (hip line vs shoulder line in BOTH_STAND1, near Kyle's 7 deg). The twist
  checker (`twist_check.py`) and the T-pose relaxer (`gla_relax.py`) are local research scripts; both are short: pose
  the GLA's frame with `jkagla.py`, measure, and for the relax rotate the clavicles and cranium about their pivots in
  every frame.
- The GLA ships at `models/players/_humanoid/<m>.gla`. If several packs carry the same GLA, the one in the
  last-sorting pack wins (see packs below); ship it in one pack, or as an overlay `gla/zz_<m>_gla.pk3`.

### 5. Materials and textures

There is no generic material converter in the repo; each source game needs a material reader that turns its material
graph into rd-vulkan stages. What ours do, per family:

- **Base:** albedo, normal (flip G for DirectX), the source's packed roughness/metal/AO repacked to ORM.
- **Skin:** `subsurface` plus a `sheen` on `materialMask` G, `detailNormalMap` at the game's tiling. Subsurface
  strength depends on how warm the albedo paint already is.
- **Eyes:** the `eye` mode with the source's iris radius (0.09-0.25 UV depending on the game). A UE iris-only texture
  goes into a shared sclera.
- **Hair:** `hairAnisotropy`; the root/tip gradient baked into albedo.
- **Layered materials** (UE flake/carbon layers, CP2077 multilayer): a `layer` where the renderer has the feature,
  baked into the albedo where not.
- **Audit every parameter** against the source material instance chain plus the master defaults. Every "flat" look we
  had was a parameter the exporter missed: the real normal under another name, a mask read as a tint, a static-switch
  dye.
- **Specular workflow** (W3 `pbr_spec*`): read the specular map as F0 and solve to metal/rough; keep metal only where
  the specular clearly outshines the diffuse, or leather turns silver.

**CP2077 multilayer** (the hardest family; local research `cp_multilayer.py`): composite each layer as tint x 2 x the
template's colour detail, levels tables for roughness and metal read as `[value at t=1, value at t=0]`, microblend
breakup, normal detail by strength. Engine placeholder textures (`grey.xbm`) are values (0.5), not white. Decal
textures whose RGB equals alpha are coverage masks. Skin tone is `TintColor x TintScale x TintColorMask` over a shared
albedo. Template suffixes (`_30`, `_300`) are not sizes: calibrate each template's world period across the cast.
character-conversions.md section 4 has the full list; the transfer of CP2077 layers onto
other games' garments is there too.

**Toon sources:** `toonLight 0.3 0.85 0.2` on every toon material; cel games' `_shadow` textures are multipliers, not
shade colours; outline hulls need per-region width rules and winding fixes (section 8 of the same doc).

Write the pack's texts as `shaders/<m>.shader` (stock stages, the fallback) **and** `shaders/<m>.mtr` (the PBR stages),
plus `models/players/<m>/model_default.skin` (and `red`/`blue` skins; mapping stock team textures works when UVs
match). Then post-process the pack (each tool writes a new pack and proves its output):

```
python -I tools/characters/pack_texture_dedupe.py zz_<m>_a.pk3 --out zz_<m>_b.pk3 --base <base> --report dedupe.json
python -I tools/characters/pack_dds_convert.py zz_<m>_b.pk3 zz_<m>_c.pk3 --report dds.json
python -I tools/characters/pack_layer_dds.py   zz_<m>_c.pk3 zz_<m>_d.pk3 --report layers.json
python -I tools/characters/pack_bc1.py         zz_<m>_d.pk3 zz_<m>_e.pk3 --report bc1.json
```

For a smaller pack, first drop the levels the renderer never uploads, then encode the opaque albedo as BC1 too:
`pack_mip_cap.py zz_<m>_e.pk3 zz_<m>_f.pk3 --detail-psnr 45` and `pack_bc1.py zz_<m>_f.pk3 zz_<m>_g.pk3 --colour`. Keep the pk3 stored
(not deflated): textures stream from it by byte range.

`pack_layer_rgba8.py <in.pk3> <dump dir> <out.pk3>` covers layer arrays BC7 cannot hold, from the renderer's own
`r_arrayDump 1` output. `cook_tangents.py --out DIR [--pack OUT.pk3]` pre-computes tangents so a hero's first load
skips MikkTSpace.

### 6. Hair chains and springs

- **From source dynamics** where the game has them (FF7R bone dynamics, UE PhysicsAsset / Kawaii / SpringBone nodes,
  W3 dangle families): each source chain becomes a chain, its parameters converted to cm, per-second damping and
  degrees. These readers are research scripts.
- **Generic** (`tools/hair/build_chains.py`): write a `rig.json` (bones with bind heads, chains with presets, links,
  collider specs) and a `render.npz` (which GLM vertices ride which hair bones, from source weights through the
  per-vertex source index), then

```
python tools/hair/build_chains.py rig.json --out model.chains --glm model.glm --gla <m>.gla --render render.npz --report chains.json
```

  The `rig.json` and `render.npz` keys are in the script's docstring. `presets.json` ships one preset (`bob`); add your
  own, fitted with `hc_eval` against a reference motion (method in [hair/PRESETS.md](../hair/PRESETS.md)).
- Rules we learned: keep collider roots out but allow resting particles inside (`particle_depth_cm`); link long free
  strands round the back of the head or they ribbon; body springs soft (k about 250, d about 20) with the visible
  amount set by `alpha` (about 0.3), not a stiff cap; a springs-only sidecar (no particles) is rejected by the loader;
  a W3 strand is a family of numbered nodes, not a bone hierarchy.
- The palette (skeleton + hair + springs) must stay at or below 256.
- **Rebuild the sidecar after every GLM rebuild.** It binds by surface vertex counts; a mismatch rejects the whole asset
  with a `^3` line after the `hair: ... N particles` load line.

### 7. Cloth

- The repo pipeline (`tools/cloth/pipeline/asset/`) builds an SKA1 asset from a cooked UE cloth trace for one model;
  its README lists what to change. Later garments (capes, coats, skirts) were built by research scripts that write the
  same format with `skaio.write_ska(..., render_wrap=..., params=...)` (SKA1 v2: render vertices wrapped
  barycentrically on sim triangles, per-asset solver parameters).
- Keep sheets at or below about 3,700 particles. Build the sim sheet from the converted GLM. Pins and reach from the
  source's max-distance paint where it exists (UE NvCloth, APEX `PhysXMaximumDistance`), otherwise synthetic (pinned
  above a height or where not leg-skinned, reach = geodesic distance).
- Leg weight on cloth above the hip pivots swings belt flaps the wrong way; move it to the pelvis on cloth vertices.
  Settle offline only where the authored rest is wrong (a flared bell coat), never an authored A-line.
- Offline check: `skirt_replay` against a pose reel (`make_reel.py`), then `analyze.py`. Commands in
  [cloth/FORMAT.md](../cloth/FORMAT.md) "Harness" and the pipeline README.
- Skirts can also be synthetic hair chains (columns from hip to hem with thigh/shin capsules) in the same
  `model.chains`; the engine loads one `.chains` per model, so every chain set shares one sidecar.

### 8. Animation sets

- A convert on its own GLA plays the stock set. To play the `cg_xenAnims` set, its GLA must hold the set's frames
  appended after the stock frames. The set's `animation.cfg`, `animevents.cfg` and `glas.txt` live in
  `models/players/_humanoid_xen/`.
- Build own GLAs from a `_humanoid.gla` that already has the frames appended, or append them afterwards. The tools for
  that (`build_set_pack.py`, `build_inplace_gla.py`) are in a separate animation-library repository, not this tree.
  After any re-convert of a model whose GLA carries the set, rebuild that pack or the old mesh wins.

### 9. Packs

- **Order:** `paksort` compares names upper-cased (`FS_PathCmp`), so `_` (0x5F) sorts after every letter, and a later
  pack wins. So `zz_x.pk3` beats `zzz_x.pk3`, `zz_zz_` beats `zz_`, and `z__` beats everything we install.
  `dl_` packs sort last. `tools/pack_order.py` is the sort key.
- Naming we use: look `zz_<m>_<letter>.pk3`, physics `zz_<m>_phys_<letter>.pk3`, GLA overlay `gla/zz_<m>_gla.pk3`,
  voice `zz_zz_<game>_voice_<char>.pk3`. **Never rebuild a pk3 in place**: bump the letter and keep the old one.
- A GLA override only wins against packs that sort before it. A pack named differently that carries the same GLA can
  still win (`..._nocoat` sorts after `..._gla` because N > G).
- Packs in a private home path outrank `GameData/base`: an install smoke must load from `GameData` only.
- Store DDS entries uncompressed in the zip so they range-read and stream; the pack tools keep each entry's
  compression.

### 10. The model menu and the picker

- **Stock model grid** (`UI_BuildQ3Model_List`): lists a folder only through an explicit `models/players/<m>/`
  **directory entry** in the pk3 plus `icon_<skin>.jpg/png/tga` per skin. Python `zipfile` never writes directory
  entries: add one with `zipfile.ZipInfo('models/players/<m>/')`. Folders over 29 characters are awkward in the menu;
  we ship short folder copies (GLM, skins, sidecars; textures stay in the long folder).
- **Character picker** (`ui_characters`): groups folders into characters and outfits from `character.cfg`
  (`character`, `name`, `variant`, `source`, `sourcelabel`, `sourcename`, `sourceorder`, `order`, `variantorder`,
  `framing`, `idle`, `skin`, `hide`). The source tabs are the `source` tags the installed folders declare: the tab
  shows `sourcelabel` (default the tag in capitals), the details show `sourcename` (default the label), and
  `sourceorder` places the tab (default after the ordered tabs, by its first character). `jka` (stock models) and
  `other` (no source) are built in and come last. The keys are in `codemp/ui/ui_roster.c`'s header. Add your
  folder to the `TABLE` in `tools/characters/manifests.py`, then

```
python tools/characters/manifests.py --pack zz_xen_characters_<n>.pk3
python tools/characters/roster_check.py --base <base> --manifests <dir written by: manifests.py <dir>>
```

  `roster_check.py` exits 1 when a grid entry is missing, listed twice, or a character has two variants of one name.

### 11. Voices

- `models/players/<m>/sounds.cfg` picks the voice. Copying a female convert's file makes every model female; a male
  convert needs a male entry (`mp_generic_male` + `m`).
- Our own voice packs hold short pitched grunts and taunts in English, transcribed with faster-whisper to check every
  slot. CP2077 grunts are Wwise events in `sfx_container.bnk`. These tools are research scripts.

### 12. Check it

Offline, before any game launch:
- GLM: every LOD 0 surface under 10,000 vertices, names short, the facing and height (head top in units).
- GLA: hip pop, twist, clavicle shrug, grip.
- Pack: `.mtr` / `.shader` pairs, every texture side a power of two, names at most 63 characters, pack order against
  installed packs (`pack_order.py`), directory entries and icons, `sounds.cfg` gender.
- Sidecars: palette at most 256, vertex counts match the GLM.

The cross-model audit that runs all of these over every installed pack (`audit_check.py`, `spring_audit.py`,
`hull_audit.py`, `voice_audit.py`) is local research; character-conversions.md section
9 has what it checks and found.

In game, once, over everything that changed: a hidden, muted client (`s_volume 0`, `s_mute 1`) with a private home path, the look shots in the
same order for each variant, then a motion clip with physics off beside on (stills never showed a hip pop, a twisted
GLA or ribboning hair). Read the log for `hair:` / `cloth:` load lines, `^3` bind rejections, and warnings naming your
folder. Judge colour in a neutral studio light, not a warm sunset map. Isolate look faults with A/B skins (hulls off,
`r_toon 0`). Our look runners are research scripts; any localhost client with `+set fs_homepath` and screenshots does
the job.

## Rules we learned

- **True to source.** Start from the game's own meshes. Community body kits that change the body are flagged before
  any work.
- **Keep authored materials.** A surprising authored value (a mod's metal-gold fabric) ships as authored; flag it with
  numbers and a still. A value that renders broken in our renderer is a fix; a value the conversion lost is a gap.
- **Flag missing look values.** When a defining value is missing (hair colour as a dropped material parameter), look for
  indirect evidence (brows in the face texture, other outfits), then say it is a choice instead of guessing silently.
- **Perfect one case before a batch.** One model matching its reference side by side, then the batch with the same
  recipe.
- **Offline first, game last.** Judge from reports and data; one game pass at the end.
- **Hero quality first, then cut with numbers.**
- **Keep the source scale.**
- **When a fix lands, check which copy of the script the pipelines import.** The hip-pop fix sat in one copy of the GLA
  builder for weeks while every new GLA came from the other.
- **Judge physics in motion and against in-game footage**, not the bind render: authored rests can be folded or
  flared in ways the game never shows.

## Limits and open issues

- The repo has no general converter: the model and skeleton stages are written for one model, and every per-game reader
  (UE, CP2077, FF7R, W3, toon) lives in local research folders. Recreating one means: a mesh/skeleton extractor to
  `.npz`, a bone-mapping table onto the 53 `_humanoid` bones, the rest fit, a material reader for that game's graph,
  and a pack writer. A separate bulk converter repository (`xen_conversion`, Blender-based, targeting stock JKA
  limits) also exists outside this tree and writes `.joints` files; it is not needed for hero converts.
- No morph targets, one UV set, 4 weights: faces are baked calm, expressions and multi-UV detail are lost
  (wishlist/character-format.md plans a face sidecar).
- JKA's 53-bone skeleton has 2 joints on 3 digits; finger detail is merged.
- A springs-only `.chains` is rejected (no body motion without particles).
- The repo's `make_gla_np.py` still needs `--smooth-loops` passed explicitly.
- `.chains`, `.cloth`, `.joints` and the PBR materials are rd-vulkan only.

## Further reading

- character-conversions.md: every lesson by topic, the pipelines table, the 2026-10-31
  audit and the Witcher 3 batch.
- stellar-blade-characters.md: one pipeline end to end, with its trap table.
- converter-feature-gap.md: what each source game's materials need vs what rd-vulkan
  draws.
- [tools/cloth/pipeline/README.md](../../tools/cloth/pipeline/README.md): the repo's model / skeleton / cloth stages and
  every model-specific line.
- [hair/README.md](../hair/README.md), [hair/FORMAT.md](../hair/FORMAT.md), [hair/PRESETS.md](../hair/PRESETS.md):
  hair runtime, sidecar format, generic builder.
- [cloth/FORMAT.md](../cloth/FORMAT.md): SKA1 / SKP1 and the harness.
- [g2-joints.md](../g2-joints.md): bending about the model's own joints.
- wishlist/character-format.md: GLM/GLA limits table and the planned face sidecar.
- wishlist/animation-retarget-handoff.md: how the `_humanoid_xen` set
  was built and installed.
- wishlist/MODELS_HANDOFF.md: a large batch's state and the in-game look runners.
- renderer-vulkan-in-xenjk.md: material keywords and the install log.
- [characters-rendering.md](characters-rendering.md): how the renderer draws what this chapter produces.
