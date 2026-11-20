# Hair chains for any model: rig, presets, colliders

Tifa's sidecar was built from FF7 Rebirth's own bone-dynamics parameters. Models without such data use the generic
path: a rig description, named presets and colliders fitted to the model's own mesh. The engine is unchanged; it
loads the same HCH1 sidecar ([FORMAT.md](FORMAT.md)).

## Pieces (`tools/hair/`)

| file | what |
|---|---|
| `build_chains.py` | rig.json + presets + (optional) GLM/GLA + render map -> `model.chains` |
| `presets.json` | named presets and the solver rate they were fitted at |
| `hc_eval.c` / `hc_eval.py` | the engine's solver and asset parser as a shared library, driven from Python with the game's runtime defaults (`FEEL_GAME`, `FLOW_GAME`) |
| `chainsio.py` | HCH1 / HCR1 writer and reader |

Build the library from the repo root:

```bash
gcc -std=c99 -O2 -ffp-contract=off -shared -o hc_eval.dll tools/hair/hc_eval.c codemp/rd-common/hairchains/hair_sim.c codemp/rd-common/hairchains/hair_asset.c -lm
```

## What a model needs

- **Hair bones.** A chain is a root bone rigid with the host (the kinematic anchor) and 1..n bones below it, one
  particle on each head. A source rig that ships hair bones gives them directly. A model without them needs chains
  authored along its strands (not covered by the builder).
- **A render map** (`render.npz`): which GLM vertices ride which hair bones, with weights. It comes from the source
  mesh's skin weights, matched to the GLM's vertices. This is the one model-specific step: the adapter knows how its
  converter moved the vertices.
- **Bind frames.** Bones are written translation-only: skinning uses `posed x bind^-1`, and posing turns bones about
  their heads, so a bone's bind orientation never shows.
- **The palette budget.** Skeleton bones plus hair bones must fit 256 (`HAIR_MAX_PALETTE` in `codemp/rd-common/tr_hair_local.h`; 53 + 107 = 160 on the humanoid, which was the old limit).

## Presets

- **Units.** Values are physical and scale-free: radius in cm, damping per second, cone limit in degrees, stiffnesses
  as per-iteration fractions. A `[root, tip]` pair interpolates along the chain, and `base` inherits from another
  preset.
- **Solver rate.** The `solver` block (dt, substeps, iterations) belongs to the presets: the fractions were fitted at
  that rate.
- **Runtime knobs.** Presets assume the runtime defaults. `r_hairFlow` scales the cone springs and adds dampers and
  air drag on top; `r_hairFeel` adds the entity's motion. Both apply to every model.

| preset | for | source |
|---|---|---|
| `bob` | cheek-length bob, 4-6 bone chains, 12-19 cm | fitted 2026-10-21 to Eve 2B's hair motion in the SkirtClothLab trace (our own AnimDynamics tuning there, which the owner accepted for form) |

## Colliders

`collider_specs` fit a sphere or capsule to the skin each bone dominates in the GLM:
- **Shape.** A sphere sits on the vertices' centroid. A capsule runs from the bone's joint to the joint of `to`, and
  its end rides `to`.
- **Radius.** The `pct` quantile of the vertices' distances, plus `pad_cm`.
- **Shrinking.** A collider is shrunk until no chain root lies inside it: enclosing a kinematic root flings its chain.
  Particles resting partly inside are reported (`particles_inside`, `deepest_cm`), not shrunk for. Hair lying on a
  skull is always partly inside a sphere fitted to it, and the collider then holds it off the skin. `particle_depth_cm`
  caps that depth when wanted.
- **Eve's set.** A cranium sphere (pct 0.75, hair excluded), plus neck and clavicle capsules (pct 0.75, pad 1 cm).
  This reproduces the UE head ball within 1.3 cm of centre and 0.7 cm of radius.

## Fitting a preset to reference motion

When a reference simulation of the same chains exists, a preset can be fitted to it as a black box, observing only its
output:
1. Drive the engine solver (`hc_eval`) with the reference's head and collider-bone motion.
2. Pose each chain with rigid segment lengths along the solved directions, and along the reference's.
3. Score the trajectory error, plus the motion-energy mismatch per frequency band and chain group, plus the mean-form
   difference.

The trajectory term alone rewards moving less, since phase never matches exactly: the first Eve fit damped the hair to
a quarter of the reference's motion. The research record for Eve is `.codex-research/eve2b-hair-20261021/NOTES.md`
(private: the inputs are the source games' assets).
