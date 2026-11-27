"""pack_bc1.py -- opaque non-colour maps of a character pack re-encoded as BC1 when BC1 holds them closely.

    python -I tools/characters/pack_bc1.py <in.pk3> <out.pk3> [--min-psnr 40] [--colour [--colour-psnr 37]] [--report r.json] [--dry-run]

A converted character pack stores its physical maps (ORM, RMO ...), masks and detail maps as BC7 (16 bytes a 4x4
block) or as PNG/TGA. BC1 is 8 bytes a block. A map is re-encoded when all of this holds:
- the shader texts of the pack read it only in non-colour roles: physical maps (ormMap, rmoMap ... and the companion
  names the renderer searches: X_orm, X_rmo ...), materialMask, tintMask, specularMap. Colour maps (map, clampMap,
  diffuseMap, implicit shaders, detailMap, emissive maps, layer images), normal maps (_n, _nh, normalMap,
  detailNormalMap) and layer masks are never touched, and nor is a texture no shader names. --colour also takes
  colour maps read only as a stage's map, clampMap or diffuseMap (an opaque albedo), against --colour-psnr (default
  37 dB: BC1 albedo as games ship it; the 40 dB bar stays for the non-colour maps);
- every pixel of its top level is opaque (alpha >= 254: BC1 keeps one bit of alpha, written as opaque);
- its sides are powers of two, at least 4;
- a BC1 encode of its top level, decoded, is within --min-psnr dB of the decoded source in each of R, G and B.
Only DDS entries are converted, in place under their own names; eligible PNG/TGA sources are reported, not converted. Mips are rebuilt from the decoded top level with the
engine's 2x2 box filter, down to 1x1. Every other entry is copied byte for byte with its compression, order and time.
Read-only on the input. Proof (always run; exit 1 on a failure): the written top level decodes within the threshold
of the source in every channel, and every mip level's size is right.
"""
import argparse
import io
import json
import os
import re
import struct
import sys
import zipfile

import numpy as np
import etcpak
import texture2ddecoder as t2d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packio import box_down, rewrite  # noqa: E402

TEX_EXT = ('.dds', '.png', '.tga', '.jpg', '.jpeg')
KEYTOK = re.compile(r'(?i)(\S+)\s+(models/players/[^\s"]+)')
NON_COLOUR_KEYS = {'ormmap', 'ormsmap', 'rmomap', 'rmosmap', 'moxrmap', 'mosrmap', 'specmap', 'specularmap',
                   'physicalmap', 'materialmask', 'tintmask'}
# --colour: albedo read as a stage's base map; '}' is the name of a shader that follows a closing brace, the texture
# that shader's implicit image (a colour read too)
COLOUR_KEYS = {'map', 'clampmap', 'diffusemap', '}'}
PHYS_SUFFIXES = ('_rmo', '_rmos', '_moxr', '_mosr', '_orm', '_orms')
DXGI = {71: 'bc1', 72: 'bc1', 80: 'bc4', 83: 'bc5', 98: 'bc7', 99: 'bc7', 77: 'bc3', 78: 'bc3'}
FOURCC = {b'DXT1': 'bc1', b'DXT5': 'bc3', b'ATI1': 'bc4', b'BC4U': 'bc4', b'ATI2': 'bc5', b'BC5U': 'bc5'}


def base(n):
    b, e = os.path.splitext(n.lower())
    return b if e in TEX_EXT else n.lower()


def dds_top(data):
    """(rgba uint8 array, format) of a DDS's top level, or None"""
    if data[:4] != b'DDS ':
        return None
    h, w = struct.unpack_from('<II', data, 12)
    fourcc = data[84:88]
    if fourcc == b'DX10':
        fmt, off = DXGI.get(struct.unpack_from('<I', data, 128)[0]), 148
    else:
        fmt, off = FOURCC.get(fourcc), 128
    if fmt is None:
        return None
    bpb = 8 if fmt in ('bc1', 'bc4') else 16
    n = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bpb
    dec = getattr(t2d, 'decode_' + fmt)(data[off:off + n], w, h)
    return np.frombuffer(dec, np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0, 3]].copy(), fmt


def bc1(rgba):
    h, w = rgba.shape[:2]
    return etcpak.compress_bc1(np.ascontiguousarray(rgba).tobytes(), w, h)  # etcpak takes RGBA


def bc1_decode(enc, w, h):
    return np.frombuffer(t2d.decode_bc1(enc, w, h), np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0]]  # BGRA out


def psnr(a, b):
    m = float(np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2))
    return 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m)


def dds_bc1(rgba):
    """a DXT1 DDS with a full mip chain from the top level (alpha written opaque)"""
    h, w = rgba.shape[:2]
    top = rgba.copy()
    top[:, :, 3] = 255
    levels, cur = [], top
    while True:
        lh, lw = cur.shape[:2]
        pad = cur
        if lh < 4 or lw < 4:  # BC blocks are 4x4: repeat the edge
            pad = np.pad(cur, ((0, max(0, 4 - lh)), (0, max(0, 4 - lw)), (0, 0)), mode='edge')
        levels.append(bc1(pad))
        if lh == 1 and lw == 1:
            break
        cur = box_down(cur)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, flags, h, w, len(levels[0]), 0, len(levels))
    pf = struct.pack('<II4sIIIII', 32, 0x4, b'DXT1', 0, 0, 0, 0, 0)
    caps = struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    return hdr + pf + caps + b''.join(levels), levels


def roles_of(z):
    """{texture base: set of keywords naming it} from the pack's shader texts"""
    roles = {}
    for i in z.infolist():
        if i.filename.lower().endswith(('.shader', '.mtr')):
            text = re.sub(r'//[^\n]*', '', z.read(i).decode('latin-1'))
            for k, p in KEYTOK.findall(text):
                roles.setdefault(base(p), set()).add(k.lower())
    return roles


def eligible(name, roles, colour=False):
    b = base(name)
    r = roles.get(b)
    if b.endswith(PHYS_SUFFIXES) and not r:
        return True  # a companion the renderer searches for by name (X_orm ...) that no keyword names
    return bool(r) and r <= (NON_COLOUR_KEYS | COLOUR_KEYS if colour else NON_COLOUR_KEYS)


def convert(inp, out, min_psnr, dry, colour=False, colour_psnr=37.0):
    z = zipfile.ZipFile(inp)
    roles = roles_of(z)
    new, recs, fails = {}, [], []
    for i in z.infolist():
        n = i.filename
        if not n.lower().startswith('models/players/') or not n.lower().endswith(TEX_EXT):
            continue
        if not eligible(n, roles, colour):
            continue
        rec = {'name': n, 'roles': sorted(roles.get(base(n), ())), 'in_bytes': i.file_size}
        recs.append(rec)
        if not n.lower().endswith('.dds'):
            rec['skip'] = 'not a DDS (reported only)'
            continue
        top = dds_top(z.read(i))
        if top is None:
            rec['skip'] = 'unsupported DDS'
            continue
        a, fmt = top
        rec['fmt'] = fmt
        h, w = a.shape[:2]
        if fmt in ('bc1', 'bc4'):   # 8 bytes a block already: BC1 would save nothing and lose BC4's precision
            rec['skip'] = 'already BC1' if fmt == 'bc1' else 'BC4: already 8 bytes a block'
            continue
        if w < 4 or h < 4 or w & (w - 1) or h & (h - 1):
            rec['skip'] = 'sides not powers of two >= 4'
            continue
        if a[:, :, 3].min() < 254:
            rec['skip'] = 'not opaque'
            continue
        dec = bc1_decode(bc1(a), w, h)
        ch = [round(psnr(a[:, :, k], dec[:, :, k]), 2) for k in range(3)]
        rec['psnr'] = ch
        bar = colour_psnr if colour and not eligible(n, roles) else min_psnr
        rec['bar'] = bar
        if min(ch) < bar:
            rec['skip'] = 'BC1 below %.1f dB in a channel' % bar
            continue
        data, levels = dds_bc1(a)
        # proof: the written top level decodes as tested, and each level has its size
        got = dds_top(data)
        ok = got is not None and got[1] == 'bc1' and min(psnr(a[:, :, k], got[0][:, :, k]) for k in range(3)) >= bar
        lw, lh = w, h
        for lv in levels:
            ok = ok and len(lv) == max(1, (lw + 3) // 4) * max(1, (lh + 3) // 4) * 8
            lw, lh = max(1, lw // 2), max(1, lh // 2)
        if not ok:
            fails.append(n)
            rec['skip'] = 'proof failed'
            continue
        new[n] = data
        rec['out_bytes'] = len(data)
    if not dry:   # always written: a batch's OUT is this run's copy even when nothing changed
        rewrite(z, out, replace=new)
    saved = sum(r['in_bytes'] - r['out_bytes'] for r in recs if 'out_bytes' in r)
    rep = {'in': inp, 'out': out if not dry else None, 'converted': len(new), 'candidates': len(recs),
           'saved_bytes': saved, 'proof_failures': fails, 'textures': recs}
    print('%s: %d of %d %s to BC1, %.1f MB saved, %d proof failures%s' % (
        os.path.basename(inp), len(new), len(recs), 'opaque maps' if colour else 'non-colour maps', saved / 2**20, len(fails), ' (dry run)' if dry else ''), flush=True)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--min-psnr', type=float, default=40.0)
    ap.add_argument('--colour', action='store_true')
    ap.add_argument('--colour-psnr', type=float, default=37.0)
    ap.add_argument('--report')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rep = convert(a.inp, a.out, a.min_psnr, a.dry_run, a.colour, a.colour_psnr)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
    sys.exit(1 if rep['proof_failures'] else 0)


if __name__ == '__main__':
    main()
