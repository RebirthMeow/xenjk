"""pack_mip_cap.py -- a character pack's DDS textures cut to a largest side by dropping their top mip levels.

    python -I tools/characters/pack_mip_cap.py <in.pk3> <out.pk3> [--max 4096] [--detail-psnr 45 [--detail-min 2048]]
                                               [--report r.json] [--dry-run]

The renderer never uploads a DDS level wider or taller than r_ddsMaxSize (default 4096, vk_dds.cpp): it skips the top
levels until the side fits. Those levels are still read from disk by a whole-file load and still fill the pack. This
tool removes them: for each DDS under models/players/ with a side above --max and enough stored levels, the header gets
the new size and level count and the data keeps the remaining levels' bytes as they are (no re-encode, so what the
renderer shows at the default r_ddsMaxSize is unchanged). A DDS without enough levels to reach --max is left as it is
and reported. --detail-psnr D also drops a top level that carries no detail of its own: while the top is larger than
--detail-min, it goes when the next level, bilinearly upsampled, is within D dB of it in every channel (alpha included:
an upscaled source, or a flat map). Every other entry is copied byte for byte with its compression, order and time.
Proof (always run; exit 1 on a failure): every written level is byte-identical to the same level of the source.
"""
import argparse
import json
import os
import struct
import sys
import zipfile

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packio import rewrite  # noqa: E402

DXGI_FMT = {71: 'bc1', 72: 'bc1', 80: 'bc4', 81: 'bc4', 77: 'bc3', 78: 'bc3', 83: 'bc5', 84: 'bc5', 98: 'bc7', 99: 'bc7'}
FOURCC_FMT = {b'DXT1': 'bc1', b'ATI1': 'bc4', b'BC4U': 'bc4', b'DXT5': 'bc3', b'ATI2': 'bc5', b'BC5U': 'bc5'}
DXGI_BLOCK = {71: 8, 72: 8, 80: 8, 81: 8, 77: 16, 78: 16, 83: 16, 84: 16, 98: 16, 99: 16}
FOURCC_BLOCK = {b'DXT1': 8, b'ATI1': 8, b'BC4U': 8, b'DXT5': 16, b'ATI2': 16, b'BC5U': 16}
DDSD_LINEARSIZE = 0x80000
DDSD_MIPMAPCOUNT = 0x20000


def layout(data):
    """(width, height, levels, data offset, block bytes) of a 2D block-compressed DDS, or None"""
    if len(data) < 128 or data[:4] != b'DDS ':
        return None
    h, w = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    caps2 = struct.unpack_from('<I', data, 112)[0]
    if caps2 & (0x200 | 0x200000):   # cubemap, volume
        return None
    fourcc = data[84:88]
    if fourcc == b'DX10':
        if len(data) < 148:
            return None
        fmt, dim, misc, arr = struct.unpack_from('<IIII', data, 128)
        if dim != 3 or misc & 4 or arr != 1:
            return None
        bb, off = DXGI_BLOCK.get(fmt), 148
    else:
        bb, off = FOURCC_BLOCK.get(fourcc), 128
    if bb is None or w < 1 or h < 1:
        return None
    return w, h, mips, off, bb


def level_size(bb, w, h):
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bb


def levels_of(data, lay):
    """the stored levels' byte strings, top first"""
    w, h, mips, off, bb = lay
    out = []
    for i in range(mips):
        n = level_size(bb, max(1, w >> i), max(1, h >> i))
        out.append(data[off:off + n])
        off += n
    return out


def fmt_of(data):
    if data[84:88] == b'DX10':
        return DXGI_FMT.get(struct.unpack_from('<I', data, 128)[0])
    return FOURCC_FMT.get(data[84:88])


def level_rgba(data, lay, k):
    """level k decoded to float32 RGBA"""
    import texture2ddecoder as t2d
    w, h = max(1, lay[0] >> k), max(1, lay[1] >> k)
    dec = getattr(t2d, 'decode_' + fmt_of(data))(levels_of(data, lay)[k], w, h)
    return np.frombuffer(dec, np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0, 3]].astype(np.float32)


def level_psnr(data, lay, k):
    """the lowest per-channel PSNR of level k against level k+1 bilinearly upsampled to its size"""
    top, half = level_rgba(data, lay, k), level_rgba(data, lay, k + 1)
    worst = 99.0
    for c in range(4):
        up = np.asarray(Image.fromarray(np.ascontiguousarray(half[:, :, c])).resize((top.shape[1], top.shape[0]), Image.BILINEAR))
        m = float(np.mean((top[:, :, c] - up) ** 2))
        worst = min(worst, 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m))
    return worst


def detail_skip(data, lay, skip, min_side, bar, rec):
    """how many more top levels carry no detail (see --detail-psnr)"""
    if not bar or fmt_of(data) is None:
        return 0
    extra = 0
    while max(lay[0] >> (skip + extra), lay[1] >> (skip + extra)) > min_side and skip + extra + 1 < lay[2]:
        p = level_psnr(data, lay, skip + extra)
        rec.setdefault('detail_psnr', []).append(round(p, 2))
        if p < bar:
            break
        extra += 1
    return extra


def cap(data, lay, limit, extra=0):
    """the DDS with its top levels dropped until both sides are at most limit (and extra more), and how many"""
    w, h, mips, off, bb = lay
    skip = 0
    while max(w >> skip, h >> skip) > limit:
        skip += 1
    skip += extra
    if skip == 0 or skip > mips - 1:
        return None, skip
    lv = levels_of(data, lay)
    nw, nh = max(1, w >> skip), max(1, h >> skip)
    head = bytearray(data[:off])
    struct.pack_into('<II', head, 12, nh, nw)
    flags = struct.unpack_from('<I', head, 8)[0]
    struct.pack_into('<I', head, 8, flags | DDSD_LINEARSIZE | DDSD_MIPMAPCOUNT)
    struct.pack_into('<I', head, 20, level_size(bb, nw, nh))   # pitch or linear size: the top level's bytes
    struct.pack_into('<I', head, 28, mips - skip)
    return bytes(head) + b''.join(lv[skip:]), skip


def convert(inp, out, limit, dry, detail_bar=0.0, detail_min=2048):
    z = zipfile.ZipFile(inp)
    new, recs, fails = {}, [], []
    for i in z.infolist():
        n = i.filename
        if not n.lower().startswith('models/players/') or not n.lower().endswith('.dds'):
            continue
        data = z.read(i)
        lay = layout(data)
        if lay is None or max(lay[0], lay[1]) <= (min(limit, detail_min) if detail_bar else limit):
            continue
        rec = {'name': n, 'in_size': [lay[0], lay[1]], 'in_levels': lay[2], 'in_bytes': i.file_size}
        recs.append(rec)
        if len(data) < lay[3] + sum(level_size(lay[4], max(1, lay[0] >> k), max(1, lay[1] >> k)) for k in range(lay[2])):
            rec['skip'] = 'shorter than its levels'
            continue
        base_skip = 0
        while max(lay[0] >> base_skip, lay[1] >> base_skip) > limit:
            base_skip += 1
        if base_skip > lay[2] - 1:
            rec['skip'] = 'too few levels to reach %d' % limit
            continue
        res, skip = cap(data, lay, limit, detail_skip(data, lay, base_skip, detail_min, detail_bar, rec))
        if res is None:
            rec['skip'] = 'no level to drop'
            continue
        got = layout(res)
        ok = (got is not None and got[2] == lay[2] - skip and got[3] == lay[3]
              and levels_of(res, got) == levels_of(data, lay)[skip:] and len(res) == got[3] + sum(map(len, levels_of(res, got))))
        if not ok:
            fails.append(n)
            rec['skip'] = 'proof failed'
            continue
        new[n] = res
        rec.update(out_size=[got[0], got[1]], out_levels=got[2], out_bytes=len(res))
    if not dry:
        rewrite(z, out, replace=new)
    saved = sum(r['in_bytes'] - r['out_bytes'] for r in recs if 'out_bytes' in r)
    rep = {'in': inp, 'out': out if not dry else None, 'max': limit, 'detail_psnr': detail_bar, 'capped': len(new), 'candidates': len(recs),
           'saved_bytes': saved, 'proof_failures': fails, 'textures': recs}
    print('%s: %d of %d DDS capped (max %d%s, %.1f MB saved, %d proof failures%s' % (
        os.path.basename(inp), len(new), len(recs), limit, (', detail %.0f dB)' % detail_bar) if detail_bar else ')', saved / 2**20, len(fails), ' (dry run)' if dry else ''), flush=True)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--max', type=int, default=4096)
    ap.add_argument('--detail-psnr', type=float, default=0.0)
    ap.add_argument('--detail-min', type=int, default=2048)
    ap.add_argument('--report')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rep = convert(a.inp, a.out, a.max, a.dry_run, a.detail_psnr, a.detail_min)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
    sys.exit(1 if rep['proof_failures'] else 0)


if __name__ == '__main__':
    main()
