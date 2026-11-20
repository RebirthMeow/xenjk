"""Compare two Watson scenes record by record, per class, for the tiles given: the baseline's check that every fix
reaches every tile (docs/BASELINE.md, phase 2). Each side is a kit's home/base or a build folder: its pk3s are read in
the engine's order (names uppercased, the last pack wins a file), so a kit compares as it is served, overlays and all.

Records are matched by what they are, not by index (indices differ between builds):
  materials   by (mesh, name) from each side's materials.json, every record of a key (a key with several records
              pairs them by their values): kind, flags, emissive, tint, window, and each image path (paths name images
              by content, so a differing path is a differing image); keys only in A (removed) and only in B (added)
  instances   by transform (position to 1 u, rotation and scale to 0.01; instances at one transform pair by their
              fields): count, near, slot, hide, and the mesh by name (an index is not comparable between builds)
Integer fields (kind, flags, slot, a light's type and has_box, a box's flags) compare exactly; the rest within
0.1% (under a high bit a float tolerance hid the low bits, e.g. any interior group index).
  lights      in the tile's footprint (CP2077 position floor(x / 256), floor(y / 256)), by position (2 u) and colour
              (0.01): every field of the light, box and shape records
Materials compared are every (mesh, name) of either side; --materials-b-only keeps those side B holds (B a
part scene of a few tiles).
  screens     (.xscr) by material key: the 16 values

    python tools/levelport/scene_parity.py <side A> <side B> --materials-a A.json --materials-b B.json
        --tile -7_4 [--tile ...] [--map cp_watson] [--out report.json]

Prints a summary per class (matched, only in A, only in B, differing, by field) and writes every difference to --out.
Reads only; localhost files only."""
import argparse
import collections
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402

LIGHT_FIELDS = ['ox', 'oy', 'oz', 'radius', 'r', 'g', 'b', 'power', 'dx', 'dy', 'dz', 'cos_outer', 'cos_inner', 'type',
                'has_box', 'fog']
BOX_FIELDS = ['box%d' % i for i in range(12)] + ['hide', 'fade', 'range', 'flags']
SHAPE_FIELDS = ['axis_x', 'axis_y', 'axis_z', 'half_length']
MAT_FIELDS = ['kind', 'flags', 'em_r', 'em_g', 'em_b', 'em_w', 'tint_r', 'tint_g', 'tint_b', 'tint_a', 'win_0', 'win_1']


class Side:
    """A kit's home/base or a build folder, read as the engine serves it"""

    def __init__(self, root, materials_json):
        root = Path(root)
        packs = sorted(root.glob('*.pk3'), key=lambda p: p.name.upper())
        if not packs:
            sys.exit('scene_parity: no pk3 in %s' % root)
        self.owner = {}
        self.zips = {}
        for p in packs:
            z = zipfile.ZipFile(p)
            self.zips[p.name] = z
            for n in z.namelist():
                self.owner[n.lower()] = (p.name, n)
        self.root = root
        self.names = {int(m['index']): (m['mesh'], m['name']) for m in
                      json.load(open(materials_json, encoding='utf-8'))}

    def read(self, name):
        hit = self.owner.get(name.lower())
        return None if hit is None else self.zips[hit[0]].read(hit[1])

    def served_by(self, name):
        hit = self.owner.get(name.lower())
        return hit[0] if hit else None


def mesh_names(side, map_name):
    """Each global mesh index's name: the mesh path of its first part's material. Meshes, LODs and parts run on over
    the xman's lib files in order, and a LOD's first part is a global part index."""
    nls, lods, parts = [], [], []
    for line in side.read('maps/%s.xman' % map_name).decode('ascii').splitlines():
        if not line.startswith('lib '):
            continue
        blob = side.read(line.split()[1])
        _, count, ofs = sections(blob)
        nls += [struct.unpack_from('<4f4I', blob, ofs[2] + 32 * m)[5] for m in range(count[2])]
        lods += [struct.unpack_from('<2IfI', blob, ofs[3] + 16 * i)[0] for i in range(count[3])]
        parts += [struct.unpack_from('<IIiI', blob, ofs[4] + 16 * i)[3] for i in range(count[4])]
    out, lod = [], 0
    for nl in nls:
        fp = lods[lod] if lod < len(lods) else None
        mi = parts[fp] if fp is not None and fp < len(parts) else None
        out.append(side.names.get(mi, ('?', mi))[0] if mi is not None else '?')
        lod += nl
    return out


def sections(blob):
    _, version, count, ofs = cp2gltf.xscn_head_of(blob)
    return version, count, ofs


def materials(side, map_name):
    blob = side.read('maps/%s/materials.xscn' % map_name)
    _, count, ofs = sections(blob)
    out = {}
    for i in range(count[5]):
        at = ofs[5] + 240 * i
        paths = [blob[at + 64 * k: at + 64 * (k + 1)].split(b'\0', 1)[0].decode('ascii') for k in range(3)]
        vals = struct.unpack_from('<2I10f', blob, at + 192)
        key = side.names.get(i, ('?', 'index %d' % i))
        out.setdefault(key, []).append((i, paths, vals))
    return out


def tile_tables(side, map_name, tile):
    blob = side.read('maps/%s/t_%s.xscn' % (map_name, tile))
    if blob is None:
        return None
    version, count, ofs = sections(blob)
    inst = [struct.unpack_from('<12fIfIf', blob, ofs[6] + 64 * i) for i in range(count[6])]
    lights = [struct.unpack_from('<16f', blob, ofs[7] + 64 * i) for i in range(count[7])]
    boxes = [struct.unpack_from('<12f3fI', blob, ofs[8] + 64 * i) for i in range(count[8])] if count[8] else []
    shapes = ([struct.unpack_from('<4f', blob, ofs[9] + 16 * i) for i in range(count[9])]
              if version >= 6 and count[9] else [])
    return dict(version=version, inst=inst, lights=lights, boxes=boxes, shapes=shapes)


def close(a, b, rel=1e-3, absol=1e-4):
    return abs(a - b) <= max(absol, rel * max(abs(a), abs(b)))


INT_FIELDS = {'kind', 'flags', 'slot', 'type', 'has_box', 'mesh'}


def diff_fields(names, a, b):
    """the fields that differ: integers (INT_FIELDS) exactly, the rest by close()"""
    def differs(n, x, y):
        if n == 'mesh':
            return x != y
        if n in INT_FIELDS:
            return int(x) != int(y)
        return not close(float(x), float(y))
    return [n for n, x, y in zip(names, a, b) if differs(n, x, y)]


class Report:
    def __init__(self, cap=5000):
        self.cap = cap
        self.summary = collections.OrderedDict()
        self.details = collections.defaultdict(list)

    def add(self, cls, **kv):
        self.summary.setdefault(cls, collections.Counter()).update(kv)

    def note(self, cls, item):
        if len(self.details[cls]) < self.cap:
            self.details[cls].append(item)


def compare_materials(A, B, keys, rep):
    for key in sorted(keys, key=str):
        a, b = A.get(key), B.get(key)
        if not a or not b:
            rep.add('materials', only_a=bool(a and not b), only_b=bool(b and not a))
            rep.note('materials', dict(key=key, only='a' if a else 'b'))
            continue
        # every record of the key, paired by their values (the first record only used to be read: 3,896 of
        # 56,444 records went unread)
        sa = sorted(a, key=lambda r: (r[2], r[1]))
        sb = sorted(b, key=lambda r: (r[2], r[1]))
        if len(sa) != len(sb):
            rep.add('materials', records_only_a=max(0, len(sa) - len(sb)), records_only_b=max(0, len(sb) - len(sa)))
            rep.note('materials', dict(key=key, records_a=len(sa), records_b=len(sb)))
        for (_, pa, va), (_, pb, vb) in zip(sa, sb):
            rep.add('materials', matched=1)
            fields = diff_fields(MAT_FIELDS, va, vb)
            imgs = ['image%d' % k for k in range(3) if pa[k] != pb[k]]
            if fields or imgs:
                rep.add('materials', differing=1, **{f: 1 for f in fields + imgs})
                rep.note('materials', dict(key=key, fields={f: [va[MAT_FIELDS.index(f)], vb[MAT_FIELDS.index(f)]]
                                                           for f in fields}, images={f: [pa[int(f[5])], pb[int(f[5])]]
                                                                                     for f in imgs}))


HUB_ORIGIN = np.array([-1280.0, 2240.0, 12.0])    # cp_watson's hub frame: game = (CP2077 - origin) x UPM
UPM = 37.5


def inst_key(rec):
    W = np.array(rec[:12]).reshape(3, 4)
    return tuple(np.round(W[:, 3]).astype(int)) + tuple(np.round(W[:, :3].ravel() / UPM * 100).astype(int))


def in_tile(pos, tile):
    tx, ty = (int(v) for v in tile.split('_'))
    cp = np.asarray(pos)[:, :3] / UPM + HUB_ORIGIN
    return (np.floor(cp[:, 0] / 256) == tx) & (np.floor(cp[:, 1] / 256) == ty)


def compare_instances(ta, tb, rep, na, nb):
    ka = collections.defaultdict(list)
    for r in ta['inst']:
        ka[inst_key(r)].append(r)
    kb = collections.defaultdict(list)
    for r in tb['inst']:
        kb[inst_key(r)].append(r)
    for k in set(ka) | set(kb):
        a, b = ka.get(k, []), kb.get(k, [])
        if len(a) != len(b):
            rep.add('instances', count_differs=1, only_a=max(0, len(a) - len(b)), only_b=max(0, len(b) - len(a)))
            # named for the report by its first part's material's mesh (shared materials name their first mesh)
            mesh = na[(a or b)[0][12]] if a else nb[b[0][12]]
            rep.add('instances_only_a_by_mesh' if len(a) > len(b) else 'instances_only_b_by_mesh',
                    **{mesh: abs(len(a) - len(b))})
            rep.note('instances', dict(key=list(k), mesh=mesh, a=len(a), b=len(b)))
        for x, y in zip(sorted(a, key=lambda r: r[13:]), sorted(b, key=lambda r: r[13:])):
            rep.add('instances', matched=1)
            f = diff_fields(['near', 'slot', 'hide', 'mesh'], (x[13], x[14], x[15], na[x[12]]),
                            (y[13], y[14], y[15], nb[y[12]]))
            if f:
                rep.add('instances', differing=1, **{n: 1 for n in f})
                rep.note('instances', dict(key=list(k), fields=f, a=list(x[13:]) + [na[x[12]]],
                                           b=list(y[13:]) + [nb[y[12]]]))


def compare_lights(ta, tb, rep, tile):
    from scipy.spatial import cKDTree
    la, lb = np.array(ta['lights']), np.array(tb['lights'])
    ia = np.flatnonzero(in_tile(la, tile)) if len(la) else np.zeros(0, int)
    ib = np.flatnonzero(in_tile(lb, tile)) if len(lb) else np.zeros(0, int)
    pick = lambda t, ix, k: [t[k][i] for i in ix] if t[k] else []
    ta = dict(boxes=pick(ta, ia, 'boxes'), shapes=pick(ta, ia, 'shapes'))
    tb = dict(boxes=pick(tb, ib, 'boxes'), shapes=pick(tb, ib, 'shapes'))
    la, lb = la[ia] if len(la) else la, lb[ib] if len(lb) else lb
    if not len(la) or not len(lb):
        rep.add('lights', only_a=len(la), only_b=len(lb))
        return
    tree = cKDTree(lb[:, :3])
    used = np.zeros(len(lb), bool)
    for i, a in enumerate(la):
        # lights sharing a place and colour (a shadowed and an unshadowed copy, a haze-only twin) pair by the fewest
        # differing fields, then distance
        cand = [j for j in tree.query_ball_point(a[:3], 2.0) if not used[j] and np.abs(lb[j, 4:7] - a[4:7]).max() < 0.01]
        hit = min(cand, key=lambda j: (len(diff_fields(LIGHT_FIELDS, a, lb[j])), np.linalg.norm(lb[j, :3] - a[:3])),
                  default=None)
        if hit is None:
            rep.add('lights', only_a=1)
            rep.note('lights', dict(tile=tile, only='a', light=dict(zip(LIGHT_FIELDS, map(float, a)))))
            continue
        used[hit] = True
        b = lb[hit]
        rep.add('lights', matched=1)
        f = diff_fields(LIGHT_FIELDS, a, b)
        if ta['boxes'] and tb['boxes']:
            f += ['box.' + n for n in diff_fields(BOX_FIELDS, ta['boxes'][i], tb['boxes'][hit])]
        if ta['shapes'] and tb['shapes']:
            f += ['shape.' + n for n in diff_fields(SHAPE_FIELDS, ta['shapes'][i], tb['shapes'][hit])]
        elif bool(ta['shapes']) != bool(tb['shapes']):
            f.append('shape.section')
        if f:
            rep.add('lights', differing=1, **{n: 1 for n in f})
            rep.note('lights', dict(tile=tile, fields=f, a=dict(zip(LIGHT_FIELDS, map(float, a))),
                                    b=dict(zip(LIGHT_FIELDS, map(float, b)))))
    for j in np.flatnonzero(~used):
        rep.add('lights', only_b=1)
        rep.note('lights', dict(tile=tile, only='b', light=dict(zip(LIGHT_FIELDS, map(float, lb[j])))))


def screens(side, map_name):
    blob = side.read('maps/%s.xscr' % map_name)
    if blob is None:
        return {}
    _, _, n, _ = struct.unpack_from('<4s3I', blob)
    out = {}
    for i in range(n):
        rec = struct.unpack_from('<I16f', blob, 16 + 68 * i)
        out[side.names.get(rec[0], ('?', rec[0]))] = rec[1:]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--materials-a', required=True)
    ap.add_argument('--materials-b', required=True)
    ap.add_argument('--tile', action='append', required=True)
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--out')
    ap.add_argument('--max-details', type=int, default=5000, help='differences kept per class in --out')
    ap.add_argument('--materials-b-only', action='store_true',
                    help="compare only the material keys side B holds (B a part scene of a few tiles)")
    a = ap.parse_args()
    SA, SB = Side(a.a, a.materials_a), Side(a.b, a.materials_b)
    rep = Report(a.max_details)
    for side, tag in ((SA, 'a'), (SB, 'b')):
        print('%s: materials.xscn from %s, .xscr from %s' % (tag, side.served_by('maps/%s/materials.xscn' % a.map),
                                                            side.served_by('maps/%s.xscr' % a.map)))
    MA, MB = materials(SA, a.map), materials(SB, a.map)
    NA, NB = mesh_names(SA, a.map), mesh_names(SB, a.map)
    for tile in a.tile:
        ta, tb = tile_tables(SA, a.map, tile), tile_tables(SB, a.map, tile)
        print('tile %s: a from %s (v%s), b from %s (v%s)' % (
            tile, SA.served_by('maps/%s/t_%s.xscn' % (a.map, tile)), ta and ta['version'],
            SB.served_by('maps/%s/t_%s.xscn' % (a.map, tile)), tb and tb['version']))
        if ta is None or tb is None:
            rep.add('tiles', missing=1)
            continue
        compare_instances(ta, tb, rep, NA, NB)
        compare_lights(ta, tb, rep, tile)
    used = set(MB) if a.materials_b_only else set(MA) | set(MB)
    compare_materials(MA, MB, used, rep)
    xa, xb = screens(SA, a.map), screens(SB, a.map)
    for k in sorted(used & (set(xa) | set(xb)), key=str):
        if k not in xa or k not in xb:
            rep.add('screens', only_a=int(k in xa), only_b=int(k in xb))
            rep.note('screens', dict(key=k, only='a' if k in xa else 'b'))
            continue
        rep.add('screens', matched=1)
        f = diff_fields(['v%d' % i for i in range(16)], xa[k], xb[k])
        if f:
            rep.add('screens', differing=1, **{n: 1 for n in f})
            rep.note('screens', dict(key=k, fields=f, a=xa[k], b=xb[k]))
    for cls, c in rep.summary.items():
        top = c.most_common(12 if cls.endswith('_by_mesh') else None)
        print('%-10s %s' % (cls, ', '.join('%s %d' % kv for kv in top)) + (' ... (%d meshes)' % len(c) if
                                                                              len(c) > len(top) else ''))
    if a.out:
        Path(a.out).write_text(json.dumps(dict(summary={k: dict(v) for k, v in rep.summary.items()},
                                               details=rep.details), indent=1, default=str), encoding='utf-8')


if __name__ == '__main__':
    main()
