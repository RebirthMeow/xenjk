"""Collision BSP size: what a gameplay map's collision costs, and a post-pass that stores fewer planes.

    python tools/levelport/colbsp.py stats <map.bsp | map.pk3> [--json out.json]
    python tools/levelport/colbsp.py slim <in.bsp> <out.bsp> [--snap S]

q3map2 stores every plane as a pair (n, d) and (-n, -d), and gives each brush the axial "box bevels" at its exact
bounds, so almost no two brushes share them. `slim` rewrites only the planes, brush sides and nodes of a compiled
Jedi Academy BSP (RBSP v1); every other lump is copied byte for byte:

- planes are stored once in the orientation a side or node uses (the engine never reads `planeNum ^ 1`: cm_load.cpp
  binds each side and node to its own plane; rd-vulkan's tr_bsp.cpp reads nodes' planes and fogs' first six sides),
  in the order brushes first use them. Exact: nothing moves. (q3map2 with tools/levelport/q3map2/large_maps.patch
  already writes only the used planes; then this merges the few exact duplicates.)
- with --snap S, a box bevel (an axial side q3map2 added: shader "noshader") also moves outward to the next multiple
  of S. The brush's own faces still bound it, so its solid is the same, but a box trace grazing its extreme vertex
  stops up to S early: on Kabuki S = 1 changed 2.5% of the drop check's player drops, for 4% fewer bytes. Off by
  default.

Brushes, sides, their order and the BSP tree are unchanged, so traces visit the same leaves and test the same
brushes. Run it after the last q3map2 stage (-light reads the planes too). Measured results:
docs/levelport-collision.md.
"""
import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HEADER_LUMPS = 18
L_SHADERS, L_PLANES, L_NODES, L_LEAFS, L_LEAFBRUSHES, L_BRUSHES, L_BRUSHSIDES = 1, 2, 3, 4, 6, 8, 9
LUMP_NAMES = ('entities shaders planes nodes leafs leafsurfaces leafbrushes models brushes brushsides drawverts '
              'drawindexes fogs surfaces lightmaps lightgrid visibility lightarray').split()
PLANE = np.dtype([('normal', '<f4', 3), ('dist', '<f4')])
NODE = np.dtype([('plane', '<i4'), ('children', '<i4', 2), ('mins', '<i4', 3), ('maxs', '<i4', 3)])
LEAF = np.dtype([('cluster', '<i4'), ('area', '<i4'), ('mins', '<i4', 3), ('maxs', '<i4', 3),
                 ('first_surface', '<i4'), ('num_surfaces', '<i4'), ('first_brush', '<i4'), ('num_brushes', '<i4')])
BRUSH = np.dtype([('first_side', '<i4'), ('num_sides', '<i4'), ('shader', '<i4')])
SIDE = np.dtype([('plane', '<i4'), ('shader', '<i4'), ('surface', '<i4')])     # RBSP: sides name a draw surface
SHADER = np.dtype([('name', 'S64'), ('surface_flags', '<i4'), ('content_flags', '<i4')])
MODEL = np.dtype([('mins', '<f4', 3), ('maxs', '<f4', 3), ('first_surface', '<i4'), ('num_surfaces', '<i4'),
                  ('first_brush', '<i4'), ('num_brushes', '<i4')])
FOG = np.dtype([('shader', 'S64'), ('brush', '<i4'), ('visible_side', '<i4')])
L_MODELS, L_FOGS = 7, 12
COLLISION_LUMPS = (L_PLANES, L_NODES, L_LEAFS, L_LEAFBRUSHES, L_BRUSHES, L_BRUSHSIDES)
# resident bytes of the engine's collision structures (cm_local.h) on x64: cbrush_t, cbrushside_t, cmPlane_t, leaf
# brush index, cNode_t, cLeaf_t (docs/levelport-collision.md sections 26 and 31). A brush whose first six sides are
# the exact axial planes stores only the rest (an implicit brush; on maps whose sides are each their brush's shader or
# the bevel's, which pipeline chunks are): x64_resident_bytes counts stored sides.
X64 = {'brush': 40, 'side': 4, 'plane': 16, 'leafbrush': 4, 'node': 16, 'leaf': 24}
AXES = np.array([[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1]], np.float32)
BEVEL_SHADER = b'noshader'      # q3map2 gives this shader to the sides AddBrushBevels adds, and to nothing else


class Rbsp:
    def __init__(self, data):
        self.data = data
        ident, version = struct.unpack_from('<4si', data, 0)
        if ident != b'RBSP' or version != 1:
            raise ValueError('not a Jedi Academy RBSP v1 (%r v%d)' % (ident, version))
        self.lumps = [struct.unpack_from('<ii', data, 8 + 8 * i) for i in range(HEADER_LUMPS)]

    def lump(self, i, dtype):
        ofs, ln = self.lumps[i]
        if ln % dtype.itemsize:
            raise ValueError('lump %s: %d bytes is not a whole number of records' % (LUMP_NAMES[i], ln))
        return np.frombuffer(self.data, dtype, ln // dtype.itemsize, ofs)


def read(path):
    """a BSP from a file or the first .bsp of a pack; an XBSZ one (collision option C, colzbsp.py) decoded"""
    path = Path(path)
    if path.suffix.lower() == '.pk3':
        with zipfile.ZipFile(path) as z:
            data = z.read(next(n for n in z.namelist() if n.endswith('.bsp')))
    else:
        data = path.read_bytes()
    return Rbsp(plain(data))


def plain(data):
    """a BSP's bytes as RBSP: an XBSZ file decoded, anything else as it is (colzbsp.plain)"""
    import colzbsp
    return colzbsp.plain(data)


def side_kinds(b):
    """Per brush side: is it a bevel (shader "noshader"), is its plane axial."""
    sh, sd, pl = b.lump(L_SHADERS, SHADER), b.lump(L_BRUSHSIDES, SIDE), b.lump(L_PLANES, PLANE)
    bevel_shader = np.array([s.split(b'\0')[0].lower() == BEVEL_SHADER for s in sh['name']])
    bevel = bevel_shader[sd['shader']]
    n = pl['normal'][sd['plane']]
    axial = (np.abs(n) == 1.0).any(1)
    return bevel, axial


def stats(b, size=None):
    br, sd, pl, nd = b.lump(L_BRUSHES, BRUSH), b.lump(L_BRUSHSIDES, SIDE), b.lump(L_PLANES, PLANE), b.lump(L_NODES, NODE)
    lf, lb = b.lump(L_LEAFS, LEAF), b.lump(L_LEAFBRUSHES, np.dtype('<i4'))
    nb = max(len(br), 1)
    bevel, axial = side_kinds(b)
    used = np.unique(np.concatenate([sd['plane'], nd['plane']]))
    coll = sum(b.lumps[i][1] for i in COLLISION_LUMPS)
    ok6 = br['num_sides'] >= 6
    implicit = 0
    if ok6.any():
        f = br['first_side'][ok6]
        nrm = pl['normal'][sd['plane'][f[:, None] + np.arange(6)[None, :]]].view(np.uint32)
        implicit = int(np.all(nrm == AXES.view(np.uint32)[None], axis=(1, 2)).sum())
    x64 = (X64['brush'] * len(br) + X64['side'] * (len(sd) - 6 * implicit) + X64['plane'] * len(pl)
           + X64['leafbrush'] * len(lb) + X64['node'] * len(nd) + X64['leaf'] * len(lf))
    return {
        'file_bytes': size if size is not None else len(b.data),
        'lumps': {n: b.lumps[i][1] for i, n in enumerate(LUMP_NAMES)},
        'brushes': len(br), 'sides': len(sd), 'planes': len(pl), 'planes_used': len(used), 'nodes': len(nd),
        'leafs': len(lf), 'leafbrushes': len(lb),
        'sides_a_brush': round(len(sd) / nb, 3), 'planes_a_brush': round(len(pl) / nb, 3),
        'box_bevel_sides': int((bevel & axial).sum()), 'edge_bevel_sides': int((bevel & ~axial).sum()),
        'collision_bytes': coll, 'collision_bytes_a_brush': round(coll / nb, 1),
        'file_bytes_a_brush': round((size if size is not None else len(b.data)) / nb, 1),
        'x64_resident_bytes': x64, 'x64_resident_bytes_a_brush': round(x64 / nb, 1),
    }


def slim(b, snap=0.0):
    """Box bevels snapped outward to `snap` units (0: unmoved) and one plane per orientation used: (the replaced
    lumps for write(), report). Memory goes with the planes a side uses, not the side count: a district's 70 M side
    references are sorted once as integers, and only their few tens of millions of distinct planes as records."""
    sd, pl, nd = b.lump(L_BRUSHSIDES, SIDE), b.lump(L_PLANES, PLANE), b.lump(L_NODES, NODE)
    bevel, axial = side_kinds(b)
    # every reference (the sides, then the nodes) as plane index x 2 + "a box bevel to snap"
    pair = np.concatenate([sd['plane'], nd['plane']]).astype(np.int64) * 2
    if snap > 0:
        pair[:len(sd)] += bevel & axial
    del bevel, axial
    upair, first = np.unique(pair, return_index=True)      # first: each pair's first reference
    table = np.empty(2 * len(pl), np.int32)
    table[upair] = np.arange(len(upair), dtype=np.int32)
    inv = table[pair]
    del pair, table
    # each distinct pair's (normal, dist); -0.0 -> 0.0: one plane, one key
    n = pl['normal'][upair >> 1].astype(np.float32) + np.float32(0.0)
    d = pl['dist'][upair >> 1].astype(np.float64)
    box = (upair & 1).astype(bool)
    moved = 0
    if snap > 0:
        # outward: the side keeps n.x <= d, so a larger d; a bevel already on the grid (to float32) stays
        snapped = np.maximum(np.ceil(d[box] / snap - 1e-6) * snap, d[box])
        moved = int(np.bincount(inv[:len(sd)], minlength=len(upair))[box][snapped != d[box]].sum())
        d[box] = snapped
    key = np.zeros(len(upair), PLANE)
    key['normal'], key['dist'] = n, d.astype(np.float32)
    del n, d, box
    raw = key.view(np.dtype((np.void, PLANE.itemsize)))
    _, rep_i, gid = np.unique(raw, return_index=True, return_inverse=True)
    gid = gid.ravel()
    gfirst = np.full(len(rep_i), np.iinfo(np.int64).max)
    np.minimum.at(gfirst, gid, first)
    order = np.argsort(gfirst, kind='stable')               # planes in the order their first reference comes
    rank = np.empty(len(order), np.int32)
    rank[order] = np.arange(len(order), dtype=np.int32)
    new_planes = key[rep_i[order]]
    new_index = rank[gid][inv]
    sides = sd.copy()
    sides['plane'] = new_index[:len(sd)]
    nodes = nd.copy()
    nodes['plane'] = new_index[len(sd):]
    return ({L_PLANES: new_planes, L_BRUSHSIDES: sides, L_NODES: nodes},
            {'snap': snap, 'box_bevels_moved': moved, 'planes_before': len(pl), 'planes_after': len(new_planes)})


def to_bytes(b, replace):
    """write()'s file as bytes: the same order, offsets and padding."""
    order = sorted(range(HEADER_LUMPS), key=lambda i: (b.lumps[i][0], i))
    blobs = {i: replace[i].view(np.uint8) if i in replace else
             memoryview(b.data)[b.lumps[i][0]:b.lumps[i][0] + b.lumps[i][1]] for i in range(HEADER_LUMPS)}
    lumps, pos = [None] * HEADER_LUMPS, 8 + 8 * HEADER_LUMPS
    for i in order:
        lumps[i] = (pos, len(blobs[i]))
        pos += len(blobs[i]) + (-len(blobs[i]) % 4)
    if pos >= 1 << 31:
        raise ValueError('%d bytes: past the BSP format\'s int offsets' % pos)
    out = bytearray(pos)
    out[:8] = b.data[:8]
    out[8:8 + 8 * HEADER_LUMPS] = b''.join(struct.pack('<ii', *lumps[i]) for i in range(HEADER_LUMPS))
    for i in order:
        o, n = lumps[i]
        if n:
            blob = blobs[i]
            src = np.frombuffer(blob, np.uint8) if isinstance(blob, memoryview) else np.ascontiguousarray(blob).reshape(-1)
            np.frombuffer(out, np.uint8, n, o)[:] = src
    return out


def write(path, b, replace):
    """Write the BSP with some lumps replaced (arrays), the others byte for byte, in their original file order,
    4-byte aligned; streamed, through a temporary file renamed over `path`."""
    order = sorted(range(HEADER_LUMPS), key=lambda i: (b.lumps[i][0], i))
    blobs = {i: replace[i].view(np.uint8) if i in replace else
             memoryview(b.data)[b.lumps[i][0]:b.lumps[i][0] + b.lumps[i][1]] for i in range(HEADER_LUMPS)}
    lumps, pos = [None] * HEADER_LUMPS, 8 + 8 * HEADER_LUMPS
    for i in order:
        lumps[i] = (pos, len(blobs[i]))
        pos += len(blobs[i]) + (-len(blobs[i]) % 4)
    if pos >= 1 << 31:
        raise ValueError('%d bytes: past the BSP format\'s int offsets' % pos)
    path = Path(path)
    tmp = path.with_name(path.name + '.tmp')
    with open(tmp, 'wb') as f:
        f.write(b.data[:8] + b''.join(struct.pack('<ii', *lumps[i]) for i in range(HEADER_LUMPS)))
        for i in order:
            f.write(blobs[i])
            f.write(b'\0' * (-len(blobs[i]) % 4))
    tmp.replace(path)
    return pos


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('stats')
    p.add_argument('bsp')
    p.add_argument('--json')
    p = sub.add_parser('slim')
    p.add_argument('src')
    p.add_argument('dst')
    p.add_argument('--snap', type=float, default=0.0, help='box bevels outward to a multiple of this (default 0: unmoved)')
    a = ap.parse_args(argv)
    if a.cmd == 'stats':
        s = stats(read(a.bsp))
        print(json.dumps(s, indent=1))
        if a.json:
            Path(a.json).write_text(json.dumps(s, indent=1), encoding='utf-8')
        return s
    dst = Path(a.dst)
    if dst.resolve() == Path(a.src).resolve():
        sys.exit('write to a new file: %s is the input' % dst)
    src = read(a.src)
    before = stats(src)
    lumps, rep = slim(src, a.snap)
    write(dst, src, lumps)
    del src, lumps
    after = stats(read(dst))
    rep.update({'before': {k: before[k] for k in ('file_bytes', 'planes', 'planes_a_brush', 'collision_bytes_a_brush',
                                                  'x64_resident_bytes_a_brush')},
                'after': {k: after[k] for k in ('file_bytes', 'planes', 'planes_a_brush', 'collision_bytes_a_brush',
                                                 'x64_resident_bytes_a_brush')}})
    print(json.dumps(rep, indent=1))
    return rep


if __name__ == '__main__':
    main()
