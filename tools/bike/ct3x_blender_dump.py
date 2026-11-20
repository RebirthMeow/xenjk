"""Dump a staged Ghoul2 model (rest frame, one LOD) and its .mtr stages for ct3x_blender.py: one .npz per surface
(positions, normals, uv, CCW triangles, in metres) and scene.json (surface -> shader, shader -> stage maps as absolute
paths). The GLM and .mtr are read back, so the render shows what was staged.

  python -I ct3x_blender_dump.py STAGE OUT [--lod 0] [--name ct3x]
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ct3x_preview as pv  # noqa: E402

UNITS = 37.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stage', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--lod', type=int, default=0)
    ap.add_argument('--name', default='ct3x')
    a = ap.parse_args()
    ns = f'models/players/{a.name}'
    model = pv.glm_io.read(open(a.stage / ns / 'model.glm', 'rb').read())
    gla = pv.jkagla.read(str(a.stage / ns / f'{a.name}.gla'))
    mats = pv.parse_mtr((a.stage / 'shaders' / f'{a.name}.mtr').read_text(encoding='ascii'))
    geo = pv.gather(model, pv.skin(model, gla), a.lod, mats)
    a.out.mkdir(parents=True, exist_ok=True)
    surfaces = []
    for i, (sh, P, N, _T, UV, T) in enumerate(geo):
        f = a.out / f's{i:03d}.npz'
        np.savez(f, P=(P / UNITS).astype(np.float32), N=N.astype(np.float32), UV=UV.astype(np.float32),
                 T=T[:, [0, 2, 1]].astype(np.int32))
        surfaces.append({'file': str(f), 'shader': sh})

    def absp(rel):
        return str((a.stage / rel).resolve()) if rel else None
    shaders = {}
    for sh in {s['shader'] for s in surfaces}:
        m = mats[sh]
        d = {'blend': m['blend'], 'map': absp(m.get('map')), 'normal': absp(m.get('normalMap')),
             'orm': absp(m.get('ormMap')), 'mask': absp(m.get('materialMask'))}
        if 'emissive' in m:
            d['emissive'] = [absp(m['emissive'][0]), list(m['emissive'][1]), m['emissive'][2]]
        if 'detailN' in m:
            d['detailN'] = [absp(m['detailN'][0]), m['detailN'][1], m['detailN'][2]]
        if 'detail' in m:
            d['detail'] = [absp(m['detail'][0]), m['detail'][1]]
        shaders[sh] = d
    (a.out / 'scene.json').write_text(json.dumps({'surfaces': surfaces, 'shaders': shaders}, indent=1))
    print(len(surfaces), 'surfaces,', len(shaders), 'shaders ->', a.out)


if __name__ == '__main__':
    main()
