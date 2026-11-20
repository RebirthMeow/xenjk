"""fma_mip_fix.py -- rebuild the mip chains of FMA data maps from their own top level.

    python -I tools/characters/fma_mip_fix.py <in.pk3> <out.pk3> [--report r.json] [--dry-run] [--threshold 4]

The FMA game-shading packs (fma-mobile game_shading_variant build_batch_gs.py dds()) made each level below the top with
PIL's Image.resize(BOX) on an RGBA array. PIL premultiplies RGB by alpha when it resizes RGBA, but in these maps alpha
is data (the mask's back-darkening share, the vertex map's continuous alpha), so wherever alpha is low every level from
1 on lost its RGB toward black. The renderer samples the mask and vertex maps mipmapped (tr_shader.cpp), so at a
distance the body's narrow light/shade step (x = half-Lambert x vertex alpha + 2 mask.g - 1 + split) reads a mask
bias of about -1 and a speckle of whatever survived: the coat breaks into dark blotches and the hair into white specks.

For every *_mask.dds and *_vc.dds under models/players/ (BC7, power of two): levels 1.. are compared with a straight
2x2 box reduction of the decoded level 0 (each channel on its own); when any level's mean error passes --threshold
(of 255; levels of 16 texels and more) the chain is rebuilt that way and BC7-encoded, level 0's bytes kept as they are. Every other entry is copied
byte for byte with its compression, order and time.
Proof (exit 1 on a failure): level 0 is byte-identical, every level is present, and the rebuilt chain is closer to the
box reduction than the old one (levels of 16 texels and more: in sum, and no level worse than the larger of its old error
and 8 of 255, BC7's own error on the small levels of these high-contrast atlases). A chain that does not get closer was
not broken and stays as it is.
"""
import argparse
import json
import os
import struct
import sys
import zipfile

import numpy as np
import etcpak
import texture2ddecoder as t2d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packio import box_down, rewrite  # noqa: E402


def parse(d):
    if d[:4] != b'DDS ' or d[84:88] != b'DX10' or struct.unpack_from('<I', d, 128)[0] not in (98, 99):
        return None
    h, w = struct.unpack_from('<II', d, 12)
    mips = max(1, struct.unpack_from('<I', d, 28)[0])
    if w < 4 or h < 4 or w & (w - 1) or h & (h - 1):
        return None
    off, spans = 148, []
    for i in range(mips):
        lw, lh = max(w >> i, 1), max(h >> i, 1)
        n = max(1, (lw + 3) // 4) * max(1, (lh + 3) // 4) * 16
        spans.append((off, n, lw, lh))
        off += n
    if off > len(d):
        return None
    return w, h, mips, spans


def decode(d, span):
    off, n, lw, lh = span
    return np.frombuffer(t2d.decode_bc7(d[off:off + n], lw, lh), np.uint8).reshape(lh, lw, 4)[:, :, [2, 1, 0, 3]]


def encode(a):
    lh, lw = a.shape[:2]
    pad = a if lh >= 4 and lw >= 4 else np.pad(a, ((0, max(0, 4 - lh)), (0, max(0, 4 - lw)), (0, 0)), mode='edge')
    return etcpak.compress_bc7(np.ascontiguousarray(pad).tobytes(), pad.shape[1], pad.shape[0])  # RGBA in


def fix_one(d, threshold):
    p = parse(d)
    if p is None:
        return None, {'skip': 'not a power-of-two BC7 DX10 DDS'}
    w, h, mips, spans = p
    top = decode(d, spans[0])
    ref, worst, levels = top, [], []
    for i in range(1, mips):
        ref = box_down(ref)
        got = decode(d, spans[i])
        if min(spans[i][2], spans[i][3]) >= 16:	# below 16 texels BC7's own error dominates a mean
            worst.append(round(float(np.abs(got.astype(int) - ref.astype(int)).mean((0, 1)).max()), 2))
        levels.append(ref)
    rec = {'w': w, 'h': h, 'mips': mips, 'level_error_before': worst[:6]}
    if not worst or max(worst) <= threshold:
        rec['skip'] = 'mips consistent'
        return None, rec
    out = bytearray(d[:spans[0][0] + spans[0][1]])  # header and level 0 as they are
    after = []
    for i, ref in enumerate(levels, 1):
        enc = encode(ref)
        if len(enc) != spans[i][1]:
            rec['skip'] = 'level %d size mismatch' % i
            return None, rec
        out += enc
        lw, lh = spans[i][2], spans[i][3]
        dec = np.frombuffer(t2d.decode_bc7(enc, lw, lh), np.uint8).reshape(lh, lw, 4)[:, :, [2, 1, 0, 3]]
        if min(lw, lh) >= 16:
            after.append(round(float(np.abs(dec.astype(int) - ref.astype(int)).mean((0, 1)).max()), 2))
    out += d[spans[-1][0] + spans[-1][1]:]
    rec['level_error_after'] = after[:6]
    rec['ok'] = bool(bytes(out[:spans[0][0] + spans[0][1]]) == d[:spans[0][0] + spans[0][1]] and len(out) == len(d)
                     and all(a <= max(8.0, b) for a, b in zip(after, worst)) and sum(after) < sum(worst))
    return bytes(out), rec


def convert(inp, out, dry, threshold):
    z = zipfile.ZipFile(inp)
    new, recs, fails = {}, [], []
    for i in z.infolist():
        n = i.filename
        if not n.lower().startswith('models/players/') or not n.lower().endswith(('_mask.dds', '_vc.dds')):
            continue
        data, rec = fix_one(z.read(i), threshold)
        rec['name'] = n
        recs.append(rec)
        if data is None:
            continue
        if not rec['ok']:
            fails.append(n)
            continue
        new[n] = data
    if not dry:   # always written: a batch's OUT is this run's copy even when nothing changed
        rewrite(z, out, replace=new)
    rep = {'in': inp, 'out': out if not dry else None, 'rebuilt': len(new), 'maps': len(recs),
           'proof_failures': fails, 'maps_detail': recs}
    print('%s: %d of %d FMA data maps had broken mips, rebuilt%s; %d proof failures' % (
        os.path.basename(inp), len(new), len(recs), ' (dry run)' if dry else '', len(fails)), flush=True)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--report')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--threshold', type=float, default=4.0)
    a = ap.parse_args()
    rep = convert(a.inp, a.out, a.dry_run, a.threshold)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
    sys.exit(1 if rep['proof_failures'] else 0)


if __name__ == '__main__':
    main()
