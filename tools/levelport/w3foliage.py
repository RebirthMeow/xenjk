"""w3foliage.py -- a Witcher 3 world's terrain foliage under a box as SpeedTree placements (Beauclair).

The hillsides' vineyards, the stone pines, chestnuts, cypresses, roses on walls and the water lilies are not layer
objects: they are the world's foliage (source_foliage/*.flyr, per terrain cell), which w3terrain.py leaves out. The
fly-through's "trees as cyan sticks" were 1,528 vineyard poles (layer meshes) with their vines missing.

Runs in headless Blender with the witcher3_tools extension. The add-on imports a tile's foliage as one point cloud per
SpeedTree and cell (attributes: position, world metres; rot, Euler XYZ radians; scale), drawn by geometry nodes. Each
point becomes a placement in the work folder's shape, kind 'srt' (mesh = the .srt depot path), so w3trees.py converts
them as it converts the layers' trees:

    blender -b --python tools/levelport/w3foliage.py -- --work DIR --box MINX MINY MAXX MAXY [--skip REGEX]
        -> DIR/terrain/foliage.json (placements, counts by tree)
    python tools/levelport/w3foliage.py --work DIR --merge [--rebase]
        -> appends them to DIR/placements.json (copied once to placements.pre-foliage.json; a re-merge starts from it;
           a copy older than another step's last run is refused, --rebase starts from the current file: w3work.py)
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3work  # noqa: E402

PKG = 'bl_ext.user_default.witcher3_tools'
BS = chr(92)


def dump(a):
    import bpy
    import importlib
    from mathutils import Euler, Matrix, Vector
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    reader = importlib.import_module(PKG + '.CR2W.CR2W_reader')
    w2w = importlib.import_module(PKG + '.importers.import_w2w')
    import w3patches
    from w3terrain import tile_rect
    w3patches.apply()
    world = mat.materialize_repo_path(a.world.replace('/', BS), source_game='w3')
    wf = reader.load_w2w(world, include_groups=False)
    spec = w2w.inspect_world_terrain(wf, world)
    x0, y0, x1, y1 = a.box
    tiles = set()
    for i in range(33):
        for j in range(33):
            tiles.add(tuple(w2w.terrain_tile_from_world_position(spec, (x0 + (x1 - x0) * i / 32, y0 + (y1 - y0) * j / 32, 0))))
    tiles = tile_rect(tiles)
    skip = re.compile(a.skip, re.I) if a.skip else None
    placements, seen, counts = [], set(), {}
    for tx, ty in sorted(tiles):
        bpy.ops.wm.read_homefile(use_empty=True)
        w2w.import_world_tile_with_foliage(wf, world, tx, ty, context=bpy.context, multires_level=8, world_root_collection=None,
                                           include_foliage=True, detail_material=False)
        n_tile = 0
        for o in bpy.data.objects:
            if not o.get('_is_foliage_instancer'):
                continue
            srt = str(o.get('_depot_path', '')).replace('_inst_', '', 1)
            if not srt.lower().endswith('.srt') or (skip and skip.search(srt)):
                continue
            cell = str(o.get('_foliage_source_context_path', ''))
            key = (srt, cell)
            if key in seen:          # (a cell's cloud comes again with a neighbouring tile)
                continue
            seen.add(key)
            at = o.data.attributes
            pos, rot, sca = at.get('position'), at.get('rot'), at.get('scale')
            M0 = o.matrix_world
            for k in range(len(pos.data)):
                p = M0 @ Vector(pos.data[k].vector)
                if not (x0 <= p.x <= x1 and y0 <= p.y <= y1):
                    continue
                r = rot.data[k].vector if rot else (0, 0, 0)
                s = sca.data[k].vector if sca else (1, 1, 1)
                M = Matrix.Translation(p) @ Euler(r, 'XYZ').to_matrix().to_4x4() @ Matrix.Diagonal((s[0], s[1], s[2], 1.0))
                placements.append({'mesh': srt, 'kind': 'srt', 'layer': 'terrain_foliage/' + os.path.basename(cell),
                                   'name': 'foliage', 'matrix': [list(map(float, row)) for row in M],
                                   'component': 'foliage'})
                counts[srt] = counts.get(srt, 0) + 1
                n_tile += 1
        print('W3FOLIAGE tile', tx, ty, n_tile, flush=True)
    out = os.path.join(a.work, 'terrain', 'foliage.json')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w') as f:
        json.dump({'box': a.box, 'placements': placements, 'counts': counts}, f)
    print('W3FOLIAGE_DONE', len(placements), json.dumps(dict(sorted(counts.items(), key=lambda kv: -kv[1])[:30])), flush=True)


def merge(a):
    work = a.work
    pf = os.path.join(work, 'placements.json')
    pre = w3work.start(work, 'foliage', keys=('placements',), rebase=a.rebase)['placements']
    with open(pre, encoding='utf-8') as f:
        pl = json.load(f)
    with open(os.path.join(work, 'terrain', 'foliage.json'), encoding='utf-8') as f:
        fol = json.load(f)
    pl['placements'] += fol['placements']
    tmp = pf + '.partial'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(pl, f)
    os.replace(tmp, pf)
    w3work.wrote(work, 'foliage')
    print('w3foliage merge: %d foliage placements added (%d in all)' % (len(fol['placements']), len(pl['placements'])))


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--box', type=float, nargs=4)
    ap.add_argument('--world', default='dlc/bob/data/levels/bob/bob.w2w')
    ap.add_argument('--skip', default=None, help='a regex of .srt paths to leave out')
    ap.add_argument('--merge', action='store_true')
    ap.add_argument('--rebase', action='store_true', help='merge: start from the current placements when the '
                                                          'pre-foliage copy is stale (w3work.py)')
    a = ap.parse_args(argv)
    merge(a) if a.merge else dump(a)
