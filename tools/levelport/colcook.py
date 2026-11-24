"""colcook.py -- a collision chunk's BSP from brush records, without .map text, tiles on disk or q3map2's BSP.

gltf2map --col-chunk --cook hands its brushes here as records: per side the three plane points the .map's text would
hold (each value exactly the double q3map2 reads from mapio._num's text: float('%.4f' % v), -0 as 0) and its shader.
Then, as the text path does, but in memory:

1. colmerge.split's tiles: brushes of more than --tile-brushes cut at the median of their centres (the same centres:
   the middle of each brush's points' bounds), the sky shell in every tile;
2. q3map2 -brushbin (map.cpp, brushbin.inc in tools/levelport/q3map2/brush_bin.patch): each tile's records through
   ParseRawBrush, ParseBrush and FinishBrush's steps and EmitBrushes' order and shaders, with the tile's own plane
   table, as the tile's own -bsp run; its shaders, planes, brushes and sides come back on a pipe;
3. colmerge.merge (chunk=True: the shell dropped, our tree) and colbsp.slim on those, and the BSP's bytes, laid out
   as q3map2 lays out a tile's file (the base's lump order) and the entity text a tile's -bsp writes.

The result is the chunk BSP the text path writes, byte for byte (the gate: gltf2map --cook-compare against a kit's
packs; docs/levelport-collision.md section 37: 195 of 195 chunks of one kit). Nothing is written to
disk on the way: no .map, no tiles, no BSP but the one the caller packs.
"""
import hashlib
import json
import struct
import subprocess
import threading
import time

import numpy as np

import sys
from pathlib import Path
if __name__ == '__main__':      # (run as python -I, the script's own folder is not on the path)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import colbsp
import colmerge
from colbsp import BRUSH, PLANE, SHADER, SIDE

# a tile's RBSP as q3map2 writes it: the lumps in its file order (from a -bsp tile; colmerge.merge keeps the base's)
Q3MAP2_ORDER = (1, 2, 4, 3, 8, 9, 5, 6, 7, 0, 10, 13, 14, 15, 16, 17, 11, 12)
Q3MAP2_VERSION = '2.5.17n'
TEXT_ARGS = '-bsp -meta -exactplanes -pointgrid 16384'      # the text path's -bsp command line, in its entities
SIDE_REC = np.dtype([('shader', '<u4'), ('pts', '<f8', 9)])


def dec4(v):
    """float(mapio._num(c)) for every value: the double nearest the 4-decimal text, +0.0 for zero. k = rint(c * 1e4)
    is '%.4f''s rounding wherever c * 1e4 is not within 1e-6 of a half (those few go through the text itself), and
    k / 1e4 is the double nearest k / 10,000, as float() of the text is."""
    v = np.asarray(v, np.float64)
    x = v * 1e4
    k = np.rint(x)
    near = np.abs(np.abs(x - np.floor(x)) - 0.5) < 1e-6
    out = k / 1e4
    if near.any():
        idx = np.nonzero(near.ravel())[0]
        flat = out.ravel()
        src = v.ravel()
        for i in idx.tolist():
            flat[i] = float('%.4f' % src[i])
        out = flat.reshape(v.shape)
    return out + 0.0


class Recs:
    """Brush records: ns[b] sides of brush b, then per side its shader (an index into names) and its three points
    (9 values, dec4'd), in the .map's order. slabs: for a mesh node's records made with gltf2map --package, each
    brush's slab as the converter gave it to slab_planes (colpack.py stores these instead of the points)."""
    __slots__ = ('ns', 'shader', 'pts', 'names', 'slabs')

    def __init__(self, ns, shader, pts, names, slabs=None):
        self.ns, self.shader, self.pts, self.names, self.slabs = ns, shader, pts, names, slabs

    @staticmethod
    def build(brushes, names):
        """brushes: [(local shader per side, [9 raw floats per side])]; the points rounded here as mapio._num does."""
        if not brushes:
            return Recs(np.zeros(0, np.uint16), np.zeros(0, np.uint32), np.zeros((0, 9)), list(names))
        ns = np.array([len(s) for s, _ in brushes], np.uint16)
        sh = np.fromiter((i for s, _ in brushes for i in s), np.uint32, int(ns.sum()))
        pts = dec4(np.array([p for _, ps in brushes for p in ps], np.float64).reshape(-1, 9))
        return Recs(ns, sh, pts, list(names))


    @staticmethod
    def uniform(brushes, name):
        """brushes: [flat list of 9 raw floats a side], every side of shader `name`."""
        ns = np.array([len(b) // 9 for b in brushes], np.uint16)
        pts = dec4(np.fromiter((c for b in brushes for c in b), np.float64, 9 * int(ns.sum())).reshape(-1, 9))
        return Recs(ns, np.zeros(len(pts), np.uint32), pts, [name])


def recs_from_text(texts):
    """Records from brushDef text (mapio.brush, slabwriter.brush): the values as q3map2 reads them (atof of the
    tokens: the same doubles). For the few brushes made as text (the sky shell) and the check."""
    names, index, brushes = [], {}, []
    for t in texts:
        sh, ps = [], []
        for ln in t.split('\n'):
            if not ln.startswith('( '):
                continue
            tok = ln.split(' ')
            ps.append([float(tok[k + j]) for k in (1, 6, 11) for j in range(3)])
            name = tok[-4]
            if name not in index:
                index[name] = len(names)
                names.append(name)
            sh.append(index[name])
        brushes.append((sh, ps))
    if not brushes:
        return Recs.build([], names)
    ns = np.array([len(s) for s, _ in brushes], np.uint16)
    return Recs(ns, np.array([i for s, _ in brushes for i in s], np.uint32),
                np.array([p for _, ps in brushes for p in ps], np.float64).reshape(-1, 9) + 0.0, names)


def join(parts):
    """One Recs from many, their shader names merged in first-seen order."""
    names, index = [], {}
    ns, sh, pts = [], [], []
    for r in parts:
        m = np.empty(len(r.names), np.uint32)
        for i, n in enumerate(r.names):
            if n not in index:
                index[n] = len(names)
                names.append(n)
            m[i] = index[n]
        ns.append(r.ns)
        sh.append(m[r.shader] if len(r.shader) else r.shader.astype(np.uint32))
        pts.append(r.pts)
    if not ns:
        return Recs.build([], [])
    return Recs(np.concatenate(ns), np.concatenate(sh), np.concatenate(pts), names)


def tiles(r, tile_brushes):
    """colmerge.split's tiles: [brush indices in .map order] (each with the sky shell's brushes where they come)."""
    nb = len(r.ns)
    first = np.concatenate([[0], np.cumsum(r.ns.astype(np.int64))])
    sky_name = np.array([n.endswith('/sky') for n in r.names] or [False])
    side_sky = sky_name[r.shader] if len(r.shader) else np.zeros(0, bool)
    sky = np.zeros(nb, bool)
    if nb:
        sky = np.logical_or.reduceat(side_sky, first[:-1]) if len(side_sky) else sky
    world = np.nonzero(~sky)[0]
    if not (tile_brushes and len(world) > tile_brushes):
        return [np.arange(nb)], int(len(world)), int(sky.sum())
    # brush_centre: the middle of the bounds of every point of the brush's side lines
    p = r.pts.reshape(-1, 3)
    pf = first * 3
    lo = np.minimum.reduceat(p, pf[:-1], axis=0)
    hi = np.maximum.reduceat(p, pf[:-1], axis=0)
    centres = ((lo + hi) / 2)[world]
    groups = colmerge.cut(centres, np.arange(len(world)), tile_brushes)
    tile_of = np.full(nb, -1, np.int64)
    for k, g in enumerate(groups):
        tile_of[world[g]] = k
    return [np.nonzero(sky | (tile_of == k))[0] for k in range(len(groups))], int(len(world)), int(sky.sum())


def _stream(r, groups):
    """The -brushbin input, piece by piece."""
    yield b'XBRUSH01' + struct.pack('<I', len(r.names))
    for n in r.names:
        b = n.encode('utf-8')
        yield struct.pack('<H', len(b)) + b
    first = np.concatenate([[0], np.cumsum(r.ns.astype(np.int64))])
    for g in groups:
        ns = r.ns[g]
        # each brush's sides in order: its first side, then the run within it
        cnt = ns.astype(np.int64)
        start = np.cumsum(cnt) - cnt
        idx = np.repeat(first[g], cnt) + (np.arange(int(cnt.sum())) - np.repeat(start, cnt))
        yield struct.pack('<III', 1, len(g), int(ns.sum()))
        yield np.ascontiguousarray(ns, '<u2').tobytes()
        for s in range(0, len(idx), 1 << 20):
            part = idx[s:s + (1 << 20)]
            rec = np.empty(len(part), SIDE_REC)
            rec['shader'], rec['pts'] = r.shader[part], r.pts[part]
            yield rec.tobytes()
    yield struct.pack('<I', 2)


def _read(f, n):
    out = bytearray()
    while len(out) < n:
        b = f.read(n - len(out))
        if not b:
            raise RuntimeError('q3map2 -brushbin: its output ended early (see its log)')
        out += b
    return bytes(out)


def _vec(f, dtype):
    n = struct.unpack('<I', _read(f, 4))[0]
    return np.frombuffer(_read(f, n * dtype.itemsize), dtype).copy()


def layout(lumps):
    """An RBSP's bytes with every lump given, in q3map2's file order (an empty lump in its place there too)."""
    order = colbsp.Rbsp(b'RBSP' + struct.pack('<i', 1) + bytes(8 * colbsp.HEADER_LUMPS))
    order.lumps = [(Q3MAP2_ORDER.index(i), 0) for i in range(colbsp.HEADER_LUMPS)]   # (the sort key to_bytes reads)
    full = {i: np.zeros(0, np.uint8) for i in range(colbsp.HEADER_LUMPS)}
    full.update(lumps)
    return colbsp.to_bytes(order, full)


def tile_bsp(shaders, planes, brushes, sides, entities):
    """A tile's RBSP in memory (colmerge.merge reads it as a part): its world model and the lumps -brushbin gives."""
    model = np.zeros(1, colmerge.MODEL)
    model['num_brushes'] = len(brushes)
    return colbsp.Rbsp(bytes(layout({0: np.frombuffer(entities, np.uint8), 1: shaders, 2: planes, 7: model,
                                     8: brushes, 9: sides})))


def entity_text(worldspawn):
    """The entity lump a tile's -bsp writes: the worldspawn's keys as the .map has them, then q3map2's two."""
    keys = dict(worldspawn)
    keys['_q3map2_cmdline'] = TEXT_ARGS
    keys['_q3map2_version'] = Q3MAP2_VERSION
    body = ''.join('"%s" "%s"\n' % (k, str(v).replace('"', "'")) for k, v in keys.items())
    return ('{\n' + body + '}\n').encode('utf-8') + b'\0'


def brushbin_tiles(r, groups, q3map2_args, log=None, cwd=None):
    """q3map2 -brushbin on a pipe: per tile (shaders, planes, brushes, sides, (read, kept)), and the seconds.
    q3map2_args: the q3map2 command up to -brushbin and the .map name it would read, last."""
    t0 = time.time()
    cmd = list(q3map2_args[:-1]) + ['-brushbin', '-', '-', q3map2_args[-1]]
    logf = open(log, 'a', encoding='utf-8') if log else subprocess.DEVNULL
    if log:
        logf.write('\n$ %s\n' % ' '.join(map(str, cmd)))
        logf.flush()
    p = subprocess.Popen([str(c) for c in cmd], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=logf,
                         cwd=cwd, bufsize=1 << 20)
    err = []

    def feed():
        try:
            for piece in _stream(r, groups):
                p.stdin.write(piece)
            p.stdin.close()
        except Exception as e:          # (a q3map2 that stopped reading: its exit code and log say why)
            err.append(e)
    th = threading.Thread(target=feed, daemon=True)
    th.start()
    out = []
    try:
        for _ in groups:
            if _read(p.stdout, 8) != b'XTILE001':
                raise RuntimeError('q3map2 -brushbin: not a tile record')
            sh, pl, br, sd = _vec(p.stdout, SHADER), _vec(p.stdout, PLANE), _vec(p.stdout, BRUSH), _vec(p.stdout, SIDE)
            out.append((sh, pl, br, sd, struct.unpack('<II', _read(p.stdout, 8))))
        if _read(p.stdout, 8) != b'XDONE001':
            raise RuntimeError('q3map2 -brushbin: no end record')
    except BaseException:
        p.kill()                # (else the feeder waits on a q3map2 that waits on us)
        raise
    finally:
        th.join()
        rc = p.wait()
        if log:
            logf.close()
    if rc or err:
        raise RuntimeError('q3map2 -brushbin exit %d %s (see %s)' % (rc, err[:1], log))
    return out, time.time() - t0


def basepaths_of(q3map2_args):
    """the -fs_basepath folders of a q3map2 command, in order"""
    a = [str(x) for x in q3map2_args]
    return [a[k + 1] for k in range(len(a) - 1) if a[k] == '-fs_basepath']


def merge_tiles(tiles_out, worldspawn, slim_snap):
    """the chunk BSP's bytes from -brushbin's tiles (colmerge.merge, colbsp.slim): (data, merge report, slim report,
    merge seconds, slim seconds)"""
    t1 = time.time()
    ent = entity_text(worldspawn)
    parts = [tile_bsp(sh, pl, br, sd, ent) for sh, pl, br, sd, _ in tiles_out]
    base, replace, rep = colmerge.merge(None, parts, chunk=True)
    # the lumps merge copies from the base (its entities; lightmaps: none), the file laid out as the base's
    replace.update({i: base.lump(i, np.dtype(np.uint8)).copy() for i in range(colbsp.HEADER_LUMPS) if i not in replace})
    del parts, base
    data = layout(replace)
    del replace
    t2 = time.time()
    srep = None
    if slim_snap is not None:
        b = colbsp.Rbsp(bytes(data))
        del data
        lumps, srep = colbsp.slim(b, slim_snap)
        data = colbsp.to_bytes(b, lumps)
        del b, lumps
    return data, rep, srep, t2 - t1, time.time() - t2


def cook(r, worldspawn, q3map2_args, tile_brushes=500000, slim_snap=0.0, log=None, cwd=None, groups=None,
         brushes='q3map2', basepaths=None, gate_dir=None):
    """The chunk BSP's bytes and a report. q3map2_args: the q3map2 command up to -brushbin (the exe, -bsp, -game,
    basepaths, -meta, the exact-planes options) and the .map name it would read, last. groups: the tiles' brushes
    given (a check), else colmerge.split's. r may be given as a one-item list, which cook empties: the records are
    then freed once they are cooked, before the merge.

    brushes: 'q3map2' (its -brushbin on a pipe), 'python' (colbrushes.py: the same tiles with no q3map2 process;
    q3map2_args may be None, basepaths then the -fs_basepath folders whose shader scripts it reads) or 'gate' (both on
    the same records: every tile's arrays and the two chunk BSPs compared byte for byte into
    <gate_dir>/colbrushes_gate.json, the records and q3map2's tiles kept in <gate_dir>/records.npz for
    `colbrushes.py gate`; the Python BSP is returned)."""
    t0 = time.time()
    if isinstance(r, list):
        r = r.pop()
    if groups is None:
        groups, n_world, n_sky = tiles(r, tile_brushes)
    else:
        n_world, n_sky = None, None
    if brushes not in ('q3map2', 'python', 'gate'):
        raise ValueError('brushes: %r' % brushes)
    if basepaths is None and q3map2_args is not None:
        basepaths = basepaths_of(q3map2_args)
    secs = {}
    ref = got = grep = None
    if brushes in ('q3map2', 'gate'):
        ref, s = brushbin_tiles(r, groups, q3map2_args, log, cwd)
        secs['q3map2'] = round(s, 2)
    if brushes in ('python', 'gate'):
        import colbrushes
        t = time.time()
        table = colbrushes.ShaderTable.from_basepaths(basepaths, cache=colbrushes.default_cache())
        secs['shaders'] = round(time.time() - t, 2)
        t = time.time()
        got = colbrushes.cook_tiles(r, groups, table)
        secs['python'] = round(time.time() - t, 2)
    if brushes == 'gate':
        gd = Path(gate_dir)
        gd.mkdir(parents=True, exist_ok=True)
        colbrushes.save_dump(gd / 'records.npz', r, groups, basepaths, ref)
        grep = colbrushes.gate(ref, got)
    use = got if got is not None else ref
    counts = [t[4] for t in use]
    r = None                    # (the records, read: freed here unless the caller holds them)
    data, rep, srep, s_merge, s_slim = merge_tiles(use, worldspawn, slim_snap)
    secs.update({'merge': round(s_merge, 1), 'slim': round(s_slim, 1)})
    if grep is not None:
        rdata = merge_tiles(ref, worldspawn, slim_snap)[0]
        grep['bsp'] = {'identical': bytes(rdata) == bytes(data), 'bytes_python': len(data),
                       'bytes_q3map2': len(rdata), 'sha256_python': hashlib.sha256(data).hexdigest(),
                       'sha256_q3map2': hashlib.sha256(rdata).hexdigest()}
        grep['identical'] = grep['identical'] and grep['bsp']['identical']
        grep['seconds'] = dict(secs)
        (gd / 'colbrushes_gate.json').write_text(json.dumps(grep, indent=1), encoding='utf-8')
        del rdata
    rep.update({'tiles': [len(g) for g in groups], 'world_brushes': n_world, 'sky_brushes': n_sky,
                'read_kept': counts, 'slim': srep, 'file_bytes': len(data), 'brushes_by': brushes,
                'seconds': secs, 'total_seconds': round(time.time() - t0, 1)})
    if grep is not None:
        rep['gate'] = {k: grep[k] for k in ('identical', 'compared', 'bsp')}
    return data, rep
