"""A collision map too big for one q3map2 run, as one BSP: the .map split into tiles of a bounded brush count, each
compiled by q3map2 (its planes and bevels), merged with a tree of our own.

    python tools/levelport/colmerge.py split <map.map> <out dir> [--tile-brushes 500000]
    python tools/levelport/colmerge.py merge <out.bsp> <base.bsp> <tile.bsp>...

q3map2 holds a .map's text and about 7.6 KB a brush while it compiles (tiles of 360 k brushes: 2.7-3.0 GB), so the
district (about 6 M brushes) would need about 40 GB in one run. The tiles are cut by brush count (each half of a tile at the median of its brushes' centres along its longer
side), so they need not be the converter's 256 m tiles.

`split` writes `base.map` (the worldspawn keys, the sky shell, every other entity: what draws and what the game
reads) and `tile_<k>.map` (worldspawn keys and brushes only). `merge` joins the compiled BSPs:

- shaders by name and flags, planes by value (each tile's are q3map2's), brushes and their sides in tile order (the
  base first, so its fogs and draw surfaces keep their brush and side numbers);
- one model, the base's draw surfaces (the sky shell's), entities and fogs;
- a new tree, axial splits at whole units, over every brush's bounds (from its first six sides, as CM_BoundBrush
  reads them): a leaf lists every brush whose bounds come within 1 u of each split that bounds it, so a point or a
  box in the leaf meets every brush it could touch (the engine's point test counts a point on a face as inside;
  both neighbours of a split list a brush that touches it). q3map2's own tree for these all-detail maps is its
  1024 u block grid (Kabuki: 7,509 brushes in one leaf); this one stops near LEAF_BRUSHES. Leaves are cluster 0
  and area 0 (no vis: the engine and the renderer treat every leaf as visible); the sky surfaces are listed in the
  leaves their bounds touch.

Run q3map2 -light on the merged BSP after (its light traces see draw surfaces, not collision brushes). Results:
docs/levelport-collision.md.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import colbsp  # noqa: E402
from colbsp import (BRUSH, FOG, L_BRUSHES, L_BRUSHSIDES, L_LEAFBRUSHES, L_LEAFS, L_NODES, L_PLANES,  # noqa: E402
                    L_SHADERS, LEAF, MODEL, NODE, PLANE, SHADER, SIDE)

L_LEAFSURFACES, L_MODELS, L_DRAWVERTS, L_DRAWINDEXES, L_FOGS, L_SURFACES = 5, 7, 10, 11, 12, 13
L_LIGHTGRID, L_VISIBILITY, L_LIGHTARRAY = 15, 16, 17
RSURFACE_SIZE = 148    # an RBSP draw surface: shader, fog, type, first vert, vert count, ...
DRAWVERT_SIZE = 80     # an RBSP draw vertex: xyz first
LEAF_BRUSHES = 48      # a leaf holds at most about this many brushes, unless splitting stops helping
MIN_LEAF = 128.0       # no box splits below this (a box query visits at most 1,024 leaves: cm_trace.cpp)
SKY = '/sky 0 0 0'


# ---------------------------------------------------------------------------------------------------- split

def map_items(path, lines=None):
    """A gltf2map .map streamed as (entity number, kind, lines): 'keys' once an entity's key lines are complete (they
    come before its brushes), each 'brush' (its lines, braces included), then 'end'. lines: the file's lines from
    elsewhere (a callable giving them again: gltf2map's text in memory)."""
    with (open(path, encoding='utf-8') if lines is None else _Lines(lines)) as f:
        e, depth, keys, cur = -1, 0, None, None
        for ln in f:
            s = ln.rstrip('\n')
            if s == '{':
                depth += 1
                if depth == 1:
                    e, keys = e + 1, []
                    continue
                if depth == 2:
                    if keys is not None:
                        yield e, 'keys', keys
                        keys = None
                    cur = []
            elif s == '}':
                depth -= 1
                if depth == 0:
                    if keys is not None:
                        yield e, 'keys', keys
                        keys = None
                    yield e, 'end', None
                    continue
                if depth == 1:
                    cur.append(s)
                    yield e, 'brush', cur
                    cur = None
                    continue
            if depth == 1:
                keys.append(s)
            else:
                cur.append(s)


class _Lines:
    """map_items' stand-in for an open file: the lines a callable gives."""

    def __init__(self, lines):
        self.lines = lines

    def __enter__(self):
        return self.lines()

    def __exit__(self, *exc):
        return False


def brush_centre(lines):
    pts = [[float(v) for v in ln.split(' ')[k:k + 3]] for ln in lines if ln.startswith('( ') for k in (1, 6, 11)]
    p = np.asarray(pts)
    return (p.min(0) + p.max(0)) / 2


def cut(centres, idx, limit):
    """Brush index groups of at most `limit`: halves at the median along the longer side of the group's centres."""
    out, stack = [], [idx]
    while stack:
        g = stack.pop()
        if len(g) <= limit:
            out.append(g)
            continue
        c = centres[g]
        ax = int(np.argmax(c[:, :2].max(0) - c[:, :2].min(0)))
        o = g[np.argsort(c[:, ax], kind='stable')]
        stack += [o[len(o) // 2:], o[:len(o) // 2]]
    return [np.sort(g) for g in out]


def split(src, out, tile_brushes=500000, lines=None):
    """base.map and tile_<k>.map from a gltf2map .map (two streamed passes: brush centres, then the tiles); lines: its
    lines from memory instead (map_items)."""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    centres, sky, n_world = [], set(), 0
    text = lines
    for e, kind, lines in map_items(src, text):
        if kind != 'brush':
            continue
        if e != 0:
            raise ValueError('entity %d has brushes: brush entities are not merged' % e)
        if any(t.endswith(SKY) for t in lines):
            sky.add(n_world)
        else:
            centres.append(brush_centre(lines))
        n_world += 1
    world = np.array([i for i in range(n_world) if i not in sky], np.int64)
    groups = cut(np.asarray(centres), np.arange(len(world)), tile_brushes)
    tile_of = np.full(n_world, -1, np.int32)
    for k, g in enumerate(groups):
        tile_of[world[g]] = k
    files = [open(out / ('tile_%d.map' % k), 'w', encoding='utf-8', newline='\n') for k in range(len(groups))]
    base = open(out / 'base.map', 'w', encoding='utf-8', newline='\n')
    i = 0
    for e, kind, lines in map_items(src, text):
        if kind == 'keys':
            head = '{\n' + ''.join(k + '\n' for k in lines)
            for f in (files + [base] if e == 0 else [base]):
                f.write(head)
        elif kind == 'brush':
            # the sky shell in every tile too: q3map2 needs a structural brush for its tree's bounds ("Backwards
            # tree volume"); merge keeps the base's
            for f in ([base] + files if i in sky else [files[tile_of[i]]]):
                f.write('\n'.join(lines) + '\n')
            i += 1
        else:
            for f in (files + [base] if e == 0 else [base]):
                f.write('}\n')
    for f in files + [base]:
        f.close()
    rep = {'brushes': int(n_world), 'sky': len(sky), 'tiles': [int(len(g)) for g in groups]}
    (out / 'split.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    return rep


# ---------------------------------------------------------------------------------------------------- merge

def build_tree(mn, mx, lo, hi, leaf_brushes=LEAF_BRUSHES, min_leaf=MIN_LEAF, pad=1.0):
    """Axial kd-tree over brush bounds in the box [lo, hi]: (nodes [axis, dist, front, back, lo, hi], leaves
    [(lo, hi, brush indices)]); children >= 0 are nodes, < 0 are -(leaf + 1). Front is the side above the split."""
    nodes, leaves = [], []
    stack = [(np.arange(len(mn)), np.asarray(lo, np.float64), np.asarray(hi, np.float64), None, 0)]
    while stack:
        idx, blo, bhi, parent, side = stack.pop()
        ext = bhi - blo
        split, best = None, 0.9 * len(idx)          # a split must leave each side under 90% of the brushes
        for a in (np.argsort(-ext) if len(idx) > leaf_brushes else ()):
            if ext[a] < 2 * min_leaf:
                continue
            # at the median of the brushes' centres (clamped to the box) along this axis, a whole unit
            c = (np.maximum(mn[idx, a], blo[a]) + np.minimum(mx[idx, a], bhi[a])) / 2
            s = min(max(float(np.round(np.median(c))), np.ceil(blo[a] + min_leaf)), np.floor(bhi[a] - min_leaf))
            f, b = mx[idx, a] >= s - pad, mn[idx, a] <= s + pad
            # the axis that divides best: brushes bunched along the longest one need not divide along it (the
            # slice's ground layer under a 32,000 u tall box kept 143,649 brushes in one leaf)
            if max(f.sum(), b.sum()) < best:
                best, ax, split, front, back = max(f.sum(), b.sum()), int(a), s, idx[f], idx[b]
        if split is None and parent is None:
            # the engine needs a node (cm_load.cpp "Map has no nodes"): a small chunk splits once anyway, mid-box
            ax = int(np.argmax(ext))
            split = float(np.floor((blo[ax] + bhi[ax]) / 2))
            front, back = idx[mx[idx, ax] >= split - pad], idx[mn[idx, ax] <= split + pad]
        if split is None:
            me = -(len(leaves) + 1)
            leaves.append((blo, bhi, idx))
        else:
            me = len(nodes)
            nodes.append([ax, split, None, None, blo, bhi])
            flo, bhi_back = blo.copy(), bhi.copy()
            flo[ax], bhi_back[ax] = split, split
            stack.append((back, blo, bhi_back, me, 1))
            stack.append((front, flo, bhi, me, 0))
        if parent is not None:
            nodes[parent][2 + side] = me
    return nodes, leaves


def merge(out, paths, offsets=None, chunk=False):
    """One BSP from the base's and the tiles' (the base first): the report. offsets: each part's whole-unit
    translation (tiles compiled about their own origin: the grid's export); chunk: a runtime chunk, every part's sky
    shell (gltf2map --chunk's temporary one) and every draw surface dropped. A part may be a colbsp.Rbsp already
    read; out None: (the base, its replaced lumps, the report) instead of a file (colcook.py)."""
    bsps = [p if isinstance(p, colbsp.Rbsp) else colbsp.read(p) for p in paths]
    offsets = [np.zeros(3) if offsets is None else np.asarray(offsets[k], np.float64) for k in range(len(bsps))]
    if any(np.any(o != np.round(o)) for o in offsets):
        raise ValueError('part offsets must be whole units')
    if any(len(b.lump(L_MODELS, MODEL)) != 1 for b in bsps):
        raise ValueError('a tile has submodels: brush entities are not merged')
    # shaders by (name, flags)
    names, shader_maps = {}, []
    for b in bsps:
        sh = b.lump(L_SHADERS, SHADER)
        m = np.empty(len(sh), np.int32)
        for i, s in enumerate(sh):
            m[i] = names.setdefault((bytes(s['name']), int(s['surface_flags']), int(s['content_flags'])), len(names))
        shader_maps.append(m)
    shaders = np.zeros(len(names), SHADER)
    for (nm, sf, cf), i in names.items():
        shaders[i] = (nm, sf, cf)
    # brushes and sides in tile order, the sides pointing into the tiles' planes stacked (deduplicated below: per
    # plane, not per side, so a district's 70 M side references are never rows of their own)
    brushes, sides, tile_planes, n_sides, n_planes = [], [], [], 0, 0
    for k, (b, sm) in enumerate(zip(bsps, shader_maps)):
        br, sd = b.lump(L_BRUSHES, BRUSH).copy(), b.lump(L_BRUSHSIDES, SIDE).copy()
        if k or chunk:      # a tile's copy of the sky shell (split gives every tile one): the base's is kept
            sky = np.array([s.split(b'\0')[0].lower().endswith(b'/sky') for s in b.lump(L_SHADERS, SHADER)['name']])
            keep = ~sky[br['shader']]
            side_keep = np.repeat(keep, br['num_sides'])
            if not np.array_equal(np.repeat(br['first_side'], br['num_sides']),
                                  np.repeat(np.cumsum(br['num_sides']) - br['num_sides'], br['num_sides'])):
                raise ValueError('%s: brush sides are not in brush order' % paths[k])
            sd, br = sd[side_keep], br[keep]
            br['first_side'] = np.cumsum(br['num_sides']) - br['num_sides']
        br['first_side'] += n_sides
        br['shader'] = sm[br['shader']]
        sd['shader'] = sm[sd['shader']]
        sd['plane'] += n_planes
        pl = b.lump(L_PLANES, PLANE)
        if np.any(offsets[k]):            # the part moved: dist + n.T in float64, float32 as the format stores it
            pl = pl.copy()
            moved = pl['dist'].astype(np.float64) + pl['normal'].astype(np.float64) @ offsets[k]
            pl['dist'] = moved.astype(np.float32)
        tile_planes.append(pl)
        brushes.append(br)
        sides.append(sd)
        n_sides += len(sd)
        n_planes += len(tile_planes[-1])
    brushes, sides, rows = np.concatenate(brushes), np.concatenate(sides), np.concatenate(tile_planes)
    del tile_planes
    # each brush's bounds from its first six sides, -X +X -Y +Y -Z +Z (cm_load.cpp CM_BoundBrush)
    fs = brushes['first_side'].astype(np.int64)
    d = np.stack([rows['dist'][sides['plane'][fs + k]] for k in range(6)], 1).astype(np.float64)
    mn, mx = np.stack([-d[:, 0], -d[:, 2], -d[:, 4]], 1), np.stack([d[:, 1], d[:, 3], d[:, 5]], 1)
    del d, fs
    nodes, leaves = build_tree(mn, mx, np.floor(mn.min(0)) - 1, np.ceil(mx.max(0)) + 1)
    # planes: the sides', then the splits' (+axis, dist), one row per distinct value, in first-use order
    split_rows = np.zeros(len(nodes), PLANE)
    for i, (ax, s, *_) in enumerate(nodes):
        split_rows[i]['normal'][ax] = 1.0
        split_rows[i]['dist'] = s
    rows = np.concatenate([rows, split_rows])
    rows['normal'] += np.float32(0.0)                               # -0.0 -> 0.0: one plane, one key
    _, rep, gid = np.unique(rows.view(np.dtype((np.void, PLANE.itemsize))), return_index=True, return_inverse=True)
    # each distinct plane's first use, the sides in order then the splits; planes no side or split uses (the tiles'
    # own trees') are left out
    ref = gid.ravel()[np.concatenate([sides['plane'].astype(np.int64), n_planes + np.arange(len(nodes))])]
    del gid
    first = np.full(len(rep), np.iinfo(np.int64).max)
    np.minimum.at(first, ref, np.arange(len(ref)))
    used = np.nonzero(first < np.iinfo(np.int64).max)[0]
    order = used[np.argsort(first[used], kind='stable')]
    rank = np.full(len(rep), -1, np.int32)
    rank[order] = np.arange(len(order), dtype=np.int32)
    planes = rows[rep[order]]
    del rows, first
    new_index = rank[ref]
    sides['plane'] = new_index[:len(sides)]
    # the base's draw surfaces (the sky shell's): their shaders renumbered, their bounds for the leaves
    base = bsps[0]
    so, sl = base.lumps[L_SURFACES]
    n_surf = 0 if chunk else sl // RSURFACE_SIZE
    surfaces = np.frombuffer(base.data, '<i4', n_surf * RSURFACE_SIZE // 4, so).copy().reshape(n_surf, RSURFACE_SIZE // 4)
    if n_surf:
        surfaces[:, 0] = shader_maps[0][surfaces[:, 0]]
    vo, vl = base.lumps[L_DRAWVERTS]
    xyz = np.frombuffer(base.data, '<f4', vl // 4, vo).reshape(-1, DRAWVERT_SIZE // 4)[:, :3]
    surf_lo = np.array([xyz[f:f + c].min(0) for f, c in surfaces[:, 3:5]]).reshape(-1, 3)
    surf_hi = np.array([xyz[f:f + c].max(0) for f, c in surfaces[:, 3:5]]).reshape(-1, 3)
    # nodes and leaves
    dnodes = np.zeros(len(nodes), NODE)
    for i, (ax, s, front, back, blo, bhi) in enumerate(nodes):
        dnodes[i] = (new_index[len(sides) + i], (front, back), np.floor(blo), np.ceil(bhi))
    dleafs = np.zeros(len(leaves), LEAF)
    leafbrushes, leafsurfs, nb, ns = [], [], 0, 0
    for i, (blo, bhi, idx) in enumerate(leaves):
        touch = np.nonzero(np.all(surf_lo <= bhi + 1, 1) & np.all(surf_hi >= blo - 1, 1))[0].astype(np.int32)
        dleafs[i] = (0, 0, np.floor(blo), np.ceil(bhi), ns, len(touch), nb, len(idx))
        leafbrushes.append(idx.astype(np.int32))
        leafsurfs.append(touch)
        nb += len(idx)
        ns += len(touch)
    model = base.lump(L_MODELS, MODEL).copy()
    model['mins'], model['maxs'] = mn.min(0), mx.max(0)
    model['first_brush'], model['num_brushes'] = 0, len(brushes)
    replace = {L_SHADERS: shaders, L_PLANES: planes, L_NODES: dnodes, L_LEAFS: dleafs,
               L_LEAFSURFACES: np.concatenate(leafsurfs), L_LEAFBRUSHES: np.concatenate(leafbrushes),
               L_MODELS: model, L_BRUSHES: brushes, L_BRUSHSIDES: sides, L_SURFACES: surfaces.reshape(-1),
               L_FOGS: base.lump(L_FOGS, FOG).copy() if not chunk else np.zeros(0, FOG),
               L_VISIBILITY: np.zeros(0, np.uint8), L_LIGHTGRID: np.zeros(0, np.uint8),
               L_LIGHTARRAY: np.zeros(0, np.uint8)}
    if chunk:
        replace.update({L_DRAWVERTS: np.zeros(0, np.uint8), L_DRAWINDEXES: np.zeros(0, np.uint8)})
    size = colbsp.write(out, base, replace) if out is not None else None
    sizes = np.array([len(l[2]) for l in leaves])
    rep = {'tiles': len(bsps), 'brushes': int(len(brushes)), 'sides': int(len(sides)), 'planes': int(len(planes)),
            'nodes': len(nodes), 'leafs': len(leaves), 'leafbrushes': int(nb),
            'leaf_brushes_median': float(np.median(sizes)), 'leaf_brushes_max': int(sizes.max()),
            'file_bytes': size}
    return rep if out is not None else (base, replace, rep)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('split')
    p.add_argument('map')
    p.add_argument('out')
    p.add_argument('--tile-brushes', type=int, default=500000)
    p = sub.add_parser('merge')
    p.add_argument('out')
    p.add_argument('bsps', nargs='+', help='the base first, then the tiles')
    a = ap.parse_args(argv)
    rep = split(a.map, a.out, a.tile_brushes) if a.cmd == 'split' else merge(a.out, a.bsps)
    print(json.dumps(rep, indent=1))
    return rep


if __name__ == '__main__':
    main()
