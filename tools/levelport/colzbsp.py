"""colzbsp.py -- collision option C: a chunk BSP stored with each lump zstd-compressed, decoded to the
exact original file (the installed chunks).

An XBSZ file is the RBSP's header (its 18 lump offsets and lengths, so the decoded file has the original layout and
padding) and per lump one zstd frame with its content checksum. The integer lumps whose records compress better
regrouped (brush sides, brushes, leaf brushes, nodes, leaves) are byte-shuffled first: byte k of every record
together; the planes and the rest are not. Level 19 by default, its own window (windowLog 23 at most: no long-range
mode, so a decode needs at most 8 MB of window besides its output).

    "XBSZ", u32 version 1, u32 the original file's size, the RBSP header (8 + 18 x 8 bytes), then per lump u32
    flags (bits 0-7: the shuffle's record size, 0 for none), u32 the frame's size, the frame (0 bytes for an empty
    lump)

    python -I colzbsp.py encode <in.bsp> <out.xbsz> [--level 19]
    python -I colzbsp.py check <in.bsp> [--level 19]       (encode, decode, compare; sizes and times)
    python -I colzbsp.py packs <chunk packs, comma-separated> <out dir> [--level 19] [--workers 16]
"""
import argparse
import collections
import ctypes
import json
import struct
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import colpack  # noqa: E402   (its libzstd)

MAGIC = b'XBSZ'
VERSION = 1
LUMPS = 18
SHUFFLE = {9: 12, 8: 12, 6: 4, 3: 36, 4: 48}     # brush sides, brushes, leaf brushes, nodes, leaves: record sizes
LEVEL = 19
ZSTD_c_compressionLevel, ZSTD_c_checksumFlag = 100, 201


def _cctx(level):
    z = colpack._lib()
    if not hasattr(z, '_xbsz'):
        vp, sz = ctypes.c_void_p, ctypes.c_size_t
        z.ZSTD_createCCtx.restype = vp
        z.ZSTD_freeCCtx.argtypes = [vp]
        z.ZSTD_CCtx_setParameter.restype, z.ZSTD_CCtx_setParameter.argtypes = sz, [vp, ctypes.c_int, ctypes.c_int]
        z.ZSTD_compress2.restype, z.ZSTD_compress2.argtypes = sz, [vp, vp, sz, vp, sz]
        z._xbsz = True
    c = z.ZSTD_createCCtx()
    colpack._check(z, z.ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level))
    colpack._check(z, z.ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1))
    return z, c


def _shuffle(b, rec):
    return np.ascontiguousarray(np.frombuffer(b, np.uint8).reshape(-1, rec).T).tobytes()


def _unshuffle(b, rec):
    return np.ascontiguousarray(np.frombuffer(b, np.uint8).reshape(rec, -1).T).tobytes()


def is_xbsz(data):
    return bytes(data[:4]) == MAGIC


def plain(data):
    """a chunk BSP's bytes as RBSP: an XBSZ file decoded, anything else as it is"""
    return decode(data) if is_xbsz(data) else data


def member_bsp(zf, name):
    """one chunk BSP member of an open pack (zipfile.ZipFile) as RBSP bytes, whether it was packed plain or XBSZ"""
    return plain(zf.read(name))


def encode(bsp, level=LEVEL):
    """an RBSP's bytes as an XBSZ file"""
    bsp = bytes(bsp)
    if bsp[:4] != b'RBSP':
        raise ValueError('colzbsp: not an RBSP')
    head = bsp[:8 + 8 * LUMPS]
    lumps = [struct.unpack_from('<ii', bsp, 8 + 8 * i) for i in range(LUMPS)]
    z, c = _cctx(level)
    try:
        out = [MAGIC, struct.pack('<II', VERSION, len(bsp)), head]
        for i, (o, n) in enumerate(lumps):
            raw = bsp[o:o + n]
            rec = SHUFFLE.get(i, 0) if n and n % SHUFFLE.get(i, 1) == 0 else 0
            if rec:
                raw = _shuffle(raw, rec)
            frame = b''
            if n:
                buf = ctypes.create_string_buffer(z.ZSTD_compressBound(n))
                m = colpack._check(z, z.ZSTD_compress2(c, buf, len(buf), raw, n))
                frame = buf.raw[:m]
            out += [struct.pack('<II', rec, len(frame)), frame]
    finally:
        z.ZSTD_freeCCtx(c)
    return b''.join(out)


def decode(data):
    """an XBSZ file's original RBSP bytes (each frame's checksum checked by zstd)"""
    data = bytes(data)
    if data[:4] != MAGIC:
        raise ValueError('colzbsp: not an XBSZ file')
    version, size = struct.unpack_from('<II', data, 4)
    if version != VERSION:
        raise ValueError('colzbsp: version %d' % version)
    head = data[12:12 + 8 + 8 * LUMPS]
    lumps = [struct.unpack_from('<ii', head, 8 + 8 * i) for i in range(LUMPS)]
    out = bytearray(size)
    out[:len(head)] = head
    p = 12 + len(head)
    for i, (o, n) in enumerate(lumps):
        rec, m = struct.unpack_from('<II', data, p)
        p += 8
        if n:
            raw = colpack.decompress(data[p:p + m])
            if len(raw) != n:
                raise ValueError('colzbsp: lump %d: %d bytes, not %d' % (i, len(raw), n))
            out[o:o + n] = _unshuffle(raw, rec) if rec else raw
        p += m
    return bytes(out)


PACK_MAX = 3_500_000_000         # cp77_collision's: under the engine's 4 GB (no zip64)


def _encode_member(job):
    """one chunk BSP member of a pack, XBSZ-encoded (a process-pool job): (name, xbsz bytes, raw size, seconds)"""
    pk, name, level = job
    t0 = time.time()
    with __import__('zipfile').ZipFile(pk) as z:
        bsp = z.read(name)
    x = encode(bsp, level)
    if decode(x) != bsp:
        raise RuntimeError('colzbsp: %s does not decode to itself' % name)
    return name, x, len(bsp), time.time() - t0


def cmd_packs(a):
    """a kit's chunk packs with every chunk BSP as XBSZ, stored (each lump is already compressed): the same
    members, names and pack names; maps/<map>.chunks and the cfg in the first pack"""
    import zipfile
    from concurrent.futures import ProcessPoolExecutor
    src = [Path(p) for p in a.packs.split(',')]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    jobs, extra, size = [], [], {}
    for pk in src:
        with zipfile.ZipFile(pk) as z:
            for i in z.infolist():
                (jobs if i.filename.endswith('.bsp') else extra).append((pk, i.filename))
                size[pk, i.filename] = i.file_size
    jobs.sort(key=lambda j: -size[j])
    t0 = time.time()
    first = out / ('zz_%s_colchunks.pk3' % a.map)
    zs, cur, k = [], None, 1
    raw_total = x_total = cpu = 0

    def open_next():
        nonlocal cur, k
        name = first if not zs else out / ('zz_%s_colchunks%d.pk3' % (a.map, k + 1))
        if zs:
            k += 1
        cur = zipfile.ZipFile(name.with_suffix('.tmp'), 'w', zipfile.ZIP_STORED)
        zs.append((name, cur))
    open_next()
    for pk, name in extra:                 # the manifest and the cfg first, in the first pack
        with zipfile.ZipFile(pk) as z:
            cur.writestr(name, z.read(name), zipfile.ZIP_DEFLATED)
    per = {}

    def results(ex):
        """the jobs' results in job order, at most a few per worker submitted ahead (finished chunks are written as
        soon as those before them are, not held until the whole map is done)"""
        ahead, todo = collections.deque(), iter(jobs)
        for pk, nm in todo:
            ahead.append(ex.submit(_encode_member, (pk, nm, a.level)))
            if len(ahead) >= 4 * a.workers:
                yield ahead.popleft().result()
        while ahead:
            yield ahead.popleft().result()
    with ProcessPoolExecutor(a.workers) as ex:
        for name, x, n, s in results(ex):
            if cur.fp.tell() + len(x) + 4096 > PACK_MAX:
                open_next()
            cur.writestr(name, x)
            raw_total += n
            x_total += len(x)
            cpu += s
            per[name] = [n, len(x), round(s, 2)]
    for name, z in zs:
        z.close()
        name.with_suffix('.tmp').replace(name)
    rep = {'level': a.level, 'chunks': len(jobs), 'bsp_bytes': raw_total, 'xbsz_bytes': x_total,
           'packs': {n.name: n.stat().st_size for n, _ in zs}, 'wall_s': round(time.time() - t0),
           'encode_cpu_s': round(cpu), 'workers': a.workers, 'per_chunk': per}
    (out / 'xbsz.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print(json.dumps({k: v for k, v in rep.items() if k != 'per_chunk'}))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('packs')
    p.add_argument('packs', help='the chunk packs, comma-separated')
    p.add_argument('out')
    p.add_argument('--map', default='cp_watson')
    p.add_argument('--level', type=int, default=LEVEL)
    p.add_argument('--workers', type=int, default=16)
    p = sub.add_parser('encode')
    p.add_argument('src')
    p.add_argument('dst')
    p.add_argument('--level', type=int, default=LEVEL)
    p = sub.add_parser('check')
    p.add_argument('src')
    p.add_argument('--level', type=int, default=LEVEL)
    a = ap.parse_args()
    if a.cmd == 'packs':
        return cmd_packs(a)
    bsp = Path(a.src).read_bytes()
    t0 = time.time()
    x = encode(bsp, a.level)
    t1 = time.time()
    if a.cmd == 'encode':
        Path(a.dst).write_bytes(x)
    y = decode(x)
    t2 = time.time()
    print(json.dumps({'bytes': len(bsp), 'xbsz': len(x), 'ratio': round(len(x) / len(bsp), 4), 'level': a.level,
                      'encode_s': round(t1 - t0, 2), 'decode_s': round(t2 - t1, 3), 'identical': y == bsp}))


if __name__ == '__main__':
    main()
