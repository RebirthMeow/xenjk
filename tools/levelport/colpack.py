"""colpack.py -- collision option A's package: a chunk shipped as the inputs of its brushes, and today's
chunk BSP rebuilt from them byte for byte (colexpand.py: the kit ships this, a one-time
expansion at install makes the chunk BSPs).

A chunk's package is the converter's brushes before their points: per mesh slab the polygon, face plane and
thickness gltf2map gave slabwriter.slab_planes (doubles, exactly); the other brushes (box and hull brushes, the sky
shell) as their records, each point value v stored as the integer k = v x 1e4 (v is the double nearest the 4-decimal
k / 10,000, so k / 1e4 gives it back exactly); the shader names, the worldspawn keys, the chunk's generated shader
scripts (q3map2 reads the contents and flags there) and the cooker's settings. No planes, sides or tree: colcook
makes those again. Columns (the slabs' corner counts, normals, distances, thicknesses and corners, then the raw
records), the whole zstd -19 (windowLog 23: no long-range window, so a decode needs at most 8 MB of window).

The zstd library: $XENJK_LIBZSTD, else libzstd.dll beside $Q3MAP2's executable, else one the system's loader finds
(ctypes.util.find_library).

slab_points is the one slab arithmetic: gltf2map's --cook path and the expansion both call it, so the points the
expansion gives q3map2 are the converter's.

    member layout: "XSLABPK1", u32 header length, the header (JSON), the columns; then zstd over all of it
"""
import ctypes
import json
import os
import struct

import numpy as np

import sys
from pathlib import Path
if __name__ == '__main__':      # (run as python -I, the script's own folder is not on the path)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import colcook
import slabwriter

STRAIGHT_SIN = 1e-4          # gltf2map.STRAIGHT_SIN (gltf2map asserts they agree)
LEVEL = 19
MAGIC = b'XSLABPK1'

_z = None


def libzstd():
    """The zstd shared library's path (see the module's docstring)"""
    p = os.environ.get('XENJK_LIBZSTD')
    if p:
        return p
    q = os.environ.get('Q3MAP2')
    if q and os.path.exists(os.path.join(os.path.dirname(q), 'libzstd.dll')):
        return os.path.join(os.path.dirname(q), 'libzstd.dll')
    import ctypes.util
    p = ctypes.util.find_library('zstd') or ctypes.util.find_library('libzstd')
    if not p:
        raise OSError('colpack: no zstd library found; set XENJK_LIBZSTD to libzstd.dll (or libzstd.so)')
    return p


def _lib():
    global _z
    if _z is None:
        z = ctypes.CDLL(libzstd())
        sz, vp = ctypes.c_size_t, ctypes.c_void_p
        z.ZSTD_compressBound.restype, z.ZSTD_compressBound.argtypes = sz, [sz]
        z.ZSTD_compress.restype, z.ZSTD_compress.argtypes = sz, [vp, sz, vp, sz, ctypes.c_int]
        z.ZSTD_decompress.restype, z.ZSTD_decompress.argtypes = sz, [vp, sz, vp, sz]
        z.ZSTD_isError.restype, z.ZSTD_isError.argtypes = ctypes.c_uint, [sz]
        z.ZSTD_getErrorName.restype, z.ZSTD_getErrorName.argtypes = ctypes.c_char_p, [sz]
        z.ZSTD_getFrameContentSize.restype, z.ZSTD_getFrameContentSize.argtypes = ctypes.c_ulonglong, [vp, sz]
        _z = z
    return _z


def _check(z, r):
    if z.ZSTD_isError(r):
        raise RuntimeError('zstd: %s' % z.ZSTD_getErrorName(r).decode())
    return r


def compress(data, level=LEVEL):
    """one zstd frame (its content size in the header), at `level` with that level's own window"""
    z = _lib()
    src = bytes(data)
    out = ctypes.create_string_buffer(z.ZSTD_compressBound(len(src)))
    n = _check(z, z.ZSTD_compress(out, len(out), src, len(src), level))
    return out.raw[:n]


def decompress(data):
    z = _lib()
    src = bytes(data)
    size = z.ZSTD_getFrameContentSize(src, len(src))
    if size >= 2 ** 63:
        raise RuntimeError('zstd: no content size in the frame')
    out = ctypes.create_string_buffer(max(1, size))
    n = _check(z, z.ZSTD_decompress(out, size, src, len(src)))
    if n != size:
        raise RuntimeError('zstd: %d bytes, the frame says %d' % (n, size))
    return out.raw[:n]


def slab_points(pts, n, d, thick):
    """a mesh slab's record, the three points of each side as slabwriter.brush would write them (flat, raw: dec4'd
    later), or None where the converter makes no brush. pts: the polygon's corners (tuples), n: the face normal
    (tuple), d: its distance, as gltf2map hands them to slabwriter.slab_planes."""
    planes = slabwriter.slab_planes(pts, n, d, thick, STRAIGHT_SIN)
    if planes is None or (len(pts) > 3 and slabwriter.duplicate_planes(planes)):
        return None
    polys = slabwriter.brush_polygons(planes)
    sides = [(poly, pn) for poly, (pn, _) in zip(polys, planes) if poly is not None]
    if len(sides) != len(planes):
        return None
    return [c for poly, pn in sides for q in slabwriter._plane_points(poly, pn) for c in q]


def _slab_batch(job):
    """slab_points over a run of slabs given as arrays (corner counts, corners, normals, distances, thicknesses): their
    side counts and raw points as arrays (a process-pool job; the caller holds no per-slab Python objects)"""
    npts, P, N, D, T = job
    first = np.concatenate([[0], np.cumsum(npts.astype(np.int64))])
    ns, flat = np.empty(len(npts), np.uint16), []
    for i in range(len(npts)):
        p = slab_points([tuple(q) for q in P[first[i]:first[i + 1]].tolist()], tuple(N[i].tolist()), float(D[i]),
                        float(T[i]))
        if p is None:
            raise RuntimeError('colpack: a packaged slab makes no brush')
        ns[i] = len(p) // 9
        flat.extend(p)
    return ns, np.array(flat, np.float64).reshape(-1, 9)


def write(parts, worldspawn, scripts, tile_brushes, slim_snap, name):
    """the chunk's package (bytes): parts as gltf2map --cook --package's world body (colcook.Recs in the .map's order;
    a mesh node's with its slabs), worldspawn the entity keys, scripts {relative path: text} the stage's shader files"""
    names, index = [], {}
    cols = {k: [] for k in ('kind', 'shader', 'npts', 'n', 'd', 'thick', 'pts', 'rns', 'rshader', 'rk')}
    for r in parts:
        m = np.empty(len(r.names), np.uint32)
        for i, nm in enumerate(r.names):
            if nm not in index:
                index[nm] = len(names)
                names.append(nm)
            m[i] = index[nm]
        if r.slabs is not None:
            if len(r.slabs) != len(r.ns) or len(r.names) != 1:
                raise ValueError('colpack: a node\'s slabs do not match its records')
            for pts, n, d, thick in r.slabs:
                cols['kind'].append(0)
                cols['shader'].append(int(m[0]))
                cols['npts'].append(len(pts))
                cols['n'].append(n)
                cols['d'].append(d)
                cols['thick'].append(thick)
                cols['pts'].extend(pts)
        else:
            k = np.rint(r.pts * 1e4).astype(np.int64)
            if not np.array_equal((k / 1e4 + 0.0).view(np.uint64), (r.pts + 0.0).view(np.uint64)):
                raise ValueError('colpack: a record value is not a 4-decimal double')
            cols['kind'].extend([1] * len(r.ns))
            cols['rns'].append(np.asarray(r.ns, np.uint16))
            cols['rshader'].append(m[r.shader] if len(r.shader) else np.zeros(0, np.uint32))
            cols['rk'].append(k)
    arrays = [
        ('kind', np.array(cols['kind'], np.uint8)),
        ('shader', np.array(cols['shader'], np.uint32)),
        ('npts', np.array(cols['npts'], np.uint16)),
        ('n', np.array(cols['n'], np.float64).reshape(-1, 3)),
        ('d', np.array(cols['d'], np.float64)),
        ('thick', np.array(cols['thick'], np.float64)),
        ('pts', np.array(cols['pts'], np.float64).reshape(-1, 3)),
        ('rns', np.concatenate(cols['rns']) if cols['rns'] else np.zeros(0, np.uint16)),
        ('rshader', np.concatenate(cols['rshader']).astype(np.uint32) if cols['rshader'] else np.zeros(0, np.uint32)),
        ('rk', np.concatenate(cols['rk']).reshape(-1, 9) if cols['rk'] else np.zeros((0, 9), np.int64)),
    ]
    if np.any(np.array(cols['npts']) > 65535):
        raise ValueError('colpack: a slab of more than 65,535 corners')
    head = {'version': 1, 'name': name, 'names': names, 'worldspawn': [[k, str(v)] for k, v in worldspawn.items()],
            'scripts': scripts, 'tile_brushes': tile_brushes, 'slim_snap': slim_snap,
            'arrays': [[k, a.dtype.str, list(a.shape)] for k, a in arrays]}
    hb = json.dumps(head).encode('utf-8')
    raw = MAGIC + struct.pack('<I', len(hb)) + hb + b''.join(np.ascontiguousarray(a).tobytes() for _, a in arrays)
    return compress(raw)


def header(data):
    """a package's header (its JSON) and its decompressed bytes"""
    raw = decompress(data)
    if raw[:8] != MAGIC:
        raise ValueError('colpack: not a slab package')
    hl = struct.unpack_from('<I', raw, 8)[0]
    return json.loads(raw[12:12 + hl].decode('utf-8')), raw


def brush_count(data):
    head, _ = header(data)
    return int(next(shape for k, _, shape in head['arrays'] if k == 'kind')[0])


def read(data, pool=None, batch=20000):
    """a package's (colcook.Recs, header): the records rebuilt (the slabs through slab_points, on `pool` if given)"""
    raw = decompress(data)
    if raw[:8] != MAGIC:
        raise ValueError('colpack: not a slab package')
    hl = struct.unpack_from('<I', raw, 8)[0]
    head = json.loads(raw[12:12 + hl].decode('utf-8'))
    A, o = {}, 12 + hl
    for k, dt, shape in head['arrays']:
        dt = np.dtype(dt)
        cnt = int(np.prod(shape)) if shape else 1
        A[k] = np.frombuffer(raw, dt, cnt, o).reshape(shape)
        o += cnt * dt.itemsize
    kind = A['kind']
    ns_slab = len(A['npts'])
    first = np.concatenate([[0], np.cumsum(A['npts'].astype(np.int64))])
    P, N, D, T = A['pts'], A['n'], A['d'], A['thick']

    def jobs():
        for i in range(0, ns_slab, batch):
            j = min(ns_slab, i + batch)
            yield A['npts'][i:j], P[first[i]:first[j]], N[i:j], D[i:j], T[i:j]
    got = list(pool.map(_slab_batch, jobs()) if pool is not None else map(_slab_batch, jobs()))
    slab_ns = np.concatenate([g[0] for g in got]) if got else np.zeros(0, np.uint16)
    slab_pts = colcook.dec4(np.concatenate([g[1] for g in got])) if got else np.zeros((0, 9))
    del got
    # every brush in order: the slabs' and the raw records', interleaved by kind
    nb = len(kind)
    ns = np.empty(nb, np.uint16)
    is_slab = kind == 0
    ns[is_slab] = slab_ns
    ns[~is_slab] = A['rns']
    sfirst = np.concatenate([[0], np.cumsum(slab_ns.astype(np.int64))])
    rfirst = np.concatenate([[0], np.cumsum(A['rns'].astype(np.int64))])
    nside = int(ns.astype(np.int64).sum())
    pts = np.empty((nside, 9), np.float64)
    shader = np.empty(nside, np.uint32)
    # side ranges: brush b's sides come from its kind's array at its rank within that kind
    rank = np.where(is_slab, np.cumsum(is_slab) - 1, np.cumsum(~is_slab) - 1)
    cnt = ns.astype(np.int64)
    dst0 = np.cumsum(cnt) - cnt
    off = np.arange(nside) - np.repeat(dst0, cnt)
    src_slab = np.repeat(np.where(is_slab, sfirst[np.minimum(rank, len(sfirst) - 1)], 0), cnt) + off
    src_raw = np.repeat(np.where(~is_slab, rfirst[np.minimum(rank, len(rfirst) - 1)], 0), cnt) + off
    side_is_slab = np.repeat(is_slab, cnt)
    pts[side_is_slab] = slab_pts[src_slab[side_is_slab]]
    pts[~side_is_slab] = A['rk'][src_raw[~side_is_slab]] / 1e4 + 0.0
    shader[side_is_slab] = np.repeat(A['shader'], slab_ns)
    shader[~side_is_slab] = A['rshader'][src_raw[~side_is_slab]]
    return colcook.Recs(ns, shader, pts, list(head['names'])), head
