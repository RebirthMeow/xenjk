"""w3terrain.py -- Witcher 3 terrain tiles under a box into a levelport work folder (Beauclair).

Runs in headless Blender with the witcher3_tools extension, like w3gltf.py. Imports the world's terrain tiles that
cover the box (no foliage), then writes each tile's vertex grid in game space (metres, +Z up) and its metadata:

    blender -b --python tools/levelport/w3terrain.py -- --work DIR --box MINX MINY MAXX MAXY
        [--world dlc/bob/data/levels/bob/bob.w2w] [--multires 8] [--sub terrain]

(--sub terrain_far: the far ring, which w3scene.py reads from <work>/terrain_far.)

    terrain/tiles.npz   per tile: grid_<tx>_<ty> (rows x cols x 3 world points), plus 'tiles' (the ids)
    terrain/tiles.json  per tile: bounds, resolution, the object's custom properties (buffers, layer metadata)
"""
import argparse
import importlib
import json
import os
import sys
import time

import numpy as np

PKG = 'bl_ext.user_default.witcher3_tools'
BS = chr(92)


def plain(v):
    if hasattr(v, 'to_dict'):
        return {k: plain(x) for k, x in v.to_dict().items()}
    if hasattr(v, 'to_list'):
        return [plain(x) for x in v.to_list()]
    if isinstance(v, (int, float, str, bool)) or v is None:
        return v
    return str(v)


def tile_rect(sampled):
    """Every tile (tx, ty) between the sampled ones' lowest and highest indices: the box's 33 x 33 samples alone miss
    tiles between them once the box spans more than 32 tiles"""
    xs, ys = [t[0] for t in sampled], [t[1] for t in sampled]
    return [(tx, ty) for tx in range(min(xs), max(xs) + 1) for ty in range(min(ys), max(ys) + 1)]


def main():
    import bpy
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--box', type=float, nargs=4, required=True)
    ap.add_argument('--world', default='dlc/bob/data/levels/bob/bob.w2w')
    ap.add_argument('--multires', type=int, default=8)
    ap.add_argument('--sub', default='terrain', help="the folder under --work (terrain_far: w3scene's far ring)")
    a = ap.parse_args(argv)
    out = os.path.join(os.path.abspath(a.work), a.sub)
    os.makedirs(out, exist_ok=True)

    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    reader = importlib.import_module(PKG + '.CR2W.CR2W_reader')
    w2w = importlib.import_module(PKG + '.importers.import_w2w')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import w3patches
    w3patches.apply()

    world = mat.materialize_repo_path(a.world.replace('/', BS), source_game='w3')
    wf = reader.load_w2w(world, include_groups=False)
    spec = w2w.inspect_world_terrain(wf, world)
    x0, y0, x1, y1 = a.box
    tiles = set()
    steps = 32
    for i in range(steps + 1):
        for j in range(steps + 1):
            tiles.add(tuple(w2w.terrain_tile_from_world_position(
                spec, (x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * j / steps, 0))))
    tiles = tile_rect(tiles)
    print('W3TERRAIN tiles', tiles, flush=True)

    bpy.ops.wm.read_homefile(use_empty=True)
    t = time.time()
    meta, grids = {}, {}
    for tx, ty in tiles:
        before = set(bpy.data.objects.keys())
        w2w.import_world_tile_with_foliage(wf, world, tx, ty, context=bpy.context, multires_level=a.multires,
                                           world_root_collection=None, include_foliage=False, detail_material=True)
        new = [bpy.data.objects[n] for n in bpy.data.objects.keys() if n not in before]
        tile_objs = [o for o in new if o.type == 'MESH' and o.get('witcher_terrain_tile')]
        if len(tile_objs) != 1:
            raise SystemExit('tile %d %d: expected one tile mesh, got %s' % (tx, ty, [o.name for o in new]))
        o = tile_objs[0]
        # the add-on sets the tile's location (its centre, and the world's lowest elevation in Z: -15 m for Toussaint)
        # after creating it: matrix_world holds it only once the view layer updates (castle terrain-1 sat 15 m high)
        bpy.context.view_layer.update()
        me = o.data
        P = np.empty(len(me.vertices) * 3, dtype=np.float64)
        me.vertices.foreach_get('co', P)
        P = P.reshape(-1, 3)
        M = np.array(o.matrix_world, dtype=np.float64)
        W = P @ M[:3, :3].T + M[:3, 3]
        # check against the world's tile bounds
        tb = w2w.terrain_tile_bounds(spec, tx, ty)
        tb = np.array([[tb.min_x, tb.min_y], [tb.max_x, tb.max_y]], dtype=np.float64)
        off = tb.mean(0) - (W[:, :2].min(0) + W[:, :2].max(0)) / 2
        if np.abs(off).max() > 0.5:
            raise SystemExit('tile %d %d: placed %s m off its bounds' % (tx, ty, off.round(2).tolist()))
        n = int(round(len(W) ** 0.5))
        if n * n != len(W):
            raise SystemExit('tile %d %d: %d vertices, not a square grid' % (tx, ty, len(W)))
        # order the grid by (y, x) so rows run along +X whatever order the importer used: on the rounded grid indices
        # (a row whose points differ in the last bits of y must not be shuffled into its neighbours)
        lo, hi = W[:, :2].min(0), W[:, :2].max(0)
        cell = (hi - lo) / (n - 1)
        ij = np.round((W[:, :2] - lo) / cell).astype(np.int64)
        order = np.lexsort((ij[:, 0], ij[:, 1]))
        if not (np.array_equal(ij[order, 1].reshape(n, n), np.repeat(np.arange(n), n).reshape(n, n))
                and np.array_equal(ij[order, 0].reshape(n, n), np.tile(np.arange(n), n).reshape(n, n))):
            raise SystemExit('tile %d %d: the vertices are not one regular grid' % (tx, ty))
        G = W[order].reshape(n, n, 3)
        key = '%d_%d' % (tx, ty)
        grids['grid_' + key] = G.astype(np.float32)
        meta[key] = {'object': o.name, 'res': n, 'bounds': [float(G[..., 0].min()), float(G[..., 1].min()),
                                                            float(G[..., 0].max()), float(G[..., 1].max())],
                     'z': [float(G[..., 2].min()), float(G[..., 2].max())],
                     'props': {k: plain(o[k]) for k in o.keys()},
                     'materials': [s.material.name for s in o.material_slots if s.material]}
        print('W3TERRAIN tile', key, n, meta[key]['bounds'], meta[key]['z'], f'{time.time() - t:.0f}s', flush=True)
    np.savez_compressed(os.path.join(out, 'tiles.npz'), tiles=np.array(sorted(meta)), **grids)
    with open(os.path.join(out, 'tiles.json'), 'w') as f:
        json.dump({'world': a.world, 'box': a.box, 'tiles': meta}, f, indent=1)
    print('W3TERRAIN_DONE', len(meta), flush=True)


if __name__ == '__main__':
    main()
