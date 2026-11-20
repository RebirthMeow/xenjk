"""patch_water.py -- the bay's water into a built tiled scene.

  python -I patch_water.py <src scene build> <dst scene build> [--sea look/ocean-20261104/sea20.npy] [--area night_city]

The bay was drawn as background: the scene's water part took only the patches
inside the area's kept cells (water_records: in_area), 15,642 of the scan's 29,018 worldWaterPatchNode patches, and the
open sea between them and the coast was left out. This writes the water tile (t_water.xscn) again with every scanned
patch within the hub ring box, plus 20 m sea patches over the cells a sea mask found water on the
Realistic Map reference with no patch and nothing collidable from 0.3 to 6 m over sea level. Each patch is the scene's
own water mesh by its appearance (the existing instances' mesh indices, matched by position), placed the way
scene_part places them (CP metres times units per metre, about the hub's origin); the sea patches are the default
appearance's mesh at sea level scaled to 20 m. The manifest's water tile line takes the new count and bounds. The
other shards are copied as they are (stored), the image packs hard links; a new build, never over an existing one.
Converted levels are for local play only."""
import argparse
import json
import os
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import pk3write  # noqa: E402

INST = np.dtype([('W', '<f4', 12), ('mi', '<u4'), ('near', '<f4'), ('slot', '<u4'), ('hide', '<f4')])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', type=Path)
    ap.add_argument('dst', type=Path)
    # (the default under the research folder's levelport/, cp2gltf.work_dir's root, resolved through its junction, so
    # the path is the one earlier builds recorded)
    ap.add_argument('--sea', type=Path, default=(Path(os.environ.get('XENJK_RESEARCH') or cp2gltf.REPO / '.codex-research')
                                                 / 'levelport' / 'look' / 'ocean-20261104' / 'sea20.npy').resolve(),
                    help="the sea cells' centres (.npy, n x 2; default under $XENJK_RESEARCH, else the repository's "
                         ".codex-research); '' for none")
    ap.add_argument('--sea-cell', type=float, default=20.0)
    ap.add_argument('--area', default='night_city')
    ap.add_argument('--map', default='cp_watson')
    a = ap.parse_args()
    if a.dst.exists():
        sys.exit('patch_water: %s exists' % a.dst)
    if str(a.sea) not in ('', '.') and not a.sea.exists():
        sys.exit("patch_water: no sea file %s (--sea '' for none)" % a.sea)
    work = cp2gltf.work_dir(a.area)
    cx, cy = cp2gltf.AREAS[a.area]['centre']
    floor, upm, half = 12.0, cp2gltf.gltf_upm(), cp2gltf.WATER_HALF
    hx, hy = cp2gltf.AREAS[a.area]['half']
    origin = np.array([cx, cy, floor])

    packs = sorted(a.src.glob('zz_%s_scene*.pk3' % a.map))
    shard_packs = [p for p in packs if '_img' not in p.name]
    where = {}
    zin = {pk: zipfile.ZipFile(pk) for pk in shard_packs}     # (each pack's directory read once)
    for pk in shard_packs:
        for n in zin[pk].namelist():
            where[n] = pk
    tname = 'maps/%s/t_water.xscn' % a.map
    old = zin[where[tname]].read(tname)
    _, version, counts, offs = cp2gltf.xscn_head_of(old)
    if any(c for k, c in enumerate(counts) if k != 6):
        sys.exit('patch_water: t_water holds more than instances: %s' % counts)
    rec = np.frombuffer(old, INST, counts[6], offs[6])
    # each appearance's mesh: the existing instances, matched to the scan's patches by position
    pl = json.load(open(work / 'placements.json', encoding='utf-8')).get('water', [])
    pos = {(round(p['m'][3], 1), round(p['m'][7], 1), round(p['m'][11], 1)): p['app'] for p in pl}
    mesh_of = {}
    for r in rec:
        W = r['W'].reshape(3, 4)
        cp = W[:, 3] / upm + origin
        app = pos.get((round(float(cp[0]), 1), round(float(cp[1]), 1), round(float(cp[2]), 1)))
        if app is not None:
            mesh_of.setdefault(app, int(r['mi']))
    if 'default' not in mesh_of:
        sys.exit('patch_water: no default water mesh among the existing instances')
    near, hide = float(rec['near'][0]), float(rec['hide'][0])
    out = []
    for p in pl:
        m = np.asarray(p['m'], np.float64).reshape(3, 4)
        if abs(m[0, 3] - cx) > hx or abs(m[1, 3] - cy) > hy:
            continue
        W = np.zeros((3, 4))
        W[:, :3] = upm * m[:, :3]
        W[:, 3] = upm * (m[:, 3] - origin)
        out.append((W.ravel(), mesh_of.get(p['app'], mesh_of['default']), near, 0, hide))
    n_patch = len(out)
    sea = np.load(a.sea) if str(a.sea) not in ('', '.') else np.zeros((0, 2))
    s = a.sea_cell / (2 * half)
    for x, y in sea:
        W = np.zeros((3, 4))
        W[0, 0], W[1, 1], W[2, 2] = upm * s, upm * s, upm
        W[:, 3] = upm * (np.array([x, y, 0.0]) - origin)
        out.append((W.ravel(), mesh_of['default'], near, 0, hide))
    new = np.zeros(len(out), INST)
    for k, (W, mi, nr, sl, hd) in enumerate(out):
        new[k] = (W, mi, nr, sl, hd)
    body = new.tobytes()
    counts[6] = len(new)
    # the sections laid out as xscn_blob lays them (each at the end of the one before, 16-byte aligned): the empty
    # ones before the instances at the header's end, the ones after them at the instances' end
    blob = cp2gltf.xscn_rebuild(version, [(c, body if k == 6 else b'') for k, c in enumerate(counts)])
    # bounds of the water tile: the patches' centres, padded by a (scaled) patch
    T = new['W'].reshape(-1, 3, 4)[:, :, 3]
    pad = half * upm * max(1.0, s)
    lo, hi = T.min(0) - pad, T.max(0) + pad

    part = a.dst.with_name(a.dst.name + '.partial')
    part.mkdir(parents=True)
    shards_out = pk3write.ShardWriter(part, a.map)
    small, shards = [], []
    for pk in shard_packs:
        for info in zin[pk].infolist():
            (shards if info.filename.endswith('.xscn') else small).append((pk, info))
    for pk, info in small:
        data = zin[pk].read(info)
        if info.filename == 'maps/%s.xman' % a.map:
            lines = data.decode().splitlines()
            for i, line in enumerate(lines):
                if line.startswith('tile water '):
                    f = line.split()
                    lines[i] = 'tile water %s %d %s %s %s' % (f[2], len(new), f[4], f[5], ' '.join(
                        '%.1f' % v for v in (*lo, *hi)))
            data = ('\n'.join(lines) + '\n').encode()
        shards_out.first.writestr(zipfile.ZipInfo(info.filename, info.date_time), data,
                                  compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
    for pk, info in shards:
        shards_out.stored(info, blob if info.filename == tname else zin[pk].read(info))
    shards_out.close()
    for z in zin.values():
        z.close()
    pk3write.link_unchanged(a.src, packs, part)
    os.replace(part, a.dst)
    print('patch_water: %s: water instances %d -> %d (scanned patches %d, sea patches %d of %g m); meshes %s' % (
        a.dst.name, len(rec), len(new), n_patch, len(sea), a.sea_cell, mesh_of))


if __name__ == '__main__':
    main()
