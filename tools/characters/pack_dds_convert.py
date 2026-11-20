"""pack_dds_convert.py -- a character pack's PNG/TGA/JPG textures given a block-compressed DDS beside them, so they
range-read and stream like the converted packs instead of decoding on the main thread when a hero first appears.

    python -I tools/characters/pack_dds_convert.py <in.pk3> <out.pk3> [--report r.json] [--dry-run]

The renderer looks every texture name up as <name>.dds first (vk_image.cpp R_LoadDDSImage), so each image gets
<base>.dds beside it and the PNG/TGA/JPG stays (the fallback; other tools read it). For each image under
models/players/ with no DDS beside it:
- skipped: sides not powers of two (or under 4), images read by a `layer` line (TX-15's arrays build their own DDS),
  images read as a specularMap (that path converts the PNG itself);
- BC1 when it is an opaque non-colour map (pack_bc1.eligible: physical maps, masks) and BC1 holds it within 40 dB in
  R, G and B (as TX-20);
- otherwise BC7;
- mips: the full chain from the top level with the engine's 2x2 box filter, per channel (never PIL's RGBA resize: an
  alpha that carries data would be multiplied into the colour).
Proof (per image): the written top level decodes within 30 dB of the source in every channel, alpha included (BC1: R,
G, B, and the source opaque). An image that fails keeps only its PNG/TGA/JPG (reported). New entries are stored; every
other entry is copied byte for byte with its compression, order and time.
"""
import argparse
import io
import json
import os
import re
import sys
import zipfile

import numpy as np
from PIL import Image
import texture2ddecoder as t2d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_bc1 as b1  # noqa: E402
import pack_layer_dds as ld  # noqa: E402
from packio import rewrite  # noqa: E402

IMG_EXT = ('.png', '.tga', '.jpg', '.jpeg')
MIN_PSNR = 30.0
BC1_PSNR = 40.0
LAYER = re.compile(r'(?im)^\s*layer\s+(\S+)\s+(\S+)')
SPEC = re.compile(r'(?im)^\s*specularMap\s+(\S+)')


def pot(n):
    return n >= 4 and not (n & (n - 1))


def convert(inp, out, dry):
    z = zipfile.ZipFile(inp)
    names = {i.filename.lower() for i in z.infolist()}
    roles = b1.roles_of(z)
    layer_imgs, spec_imgs = set(), set()
    for i in z.infolist():
        if i.filename.lower().endswith(('.shader', '.mtr')):
            text = re.sub(r'//[^\n]*', '', z.read(i).decode('latin-1'))
            for a, n in LAYER.findall(text):
                layer_imgs.update({b1.base(a), b1.base(n)})
            spec_imgs.update(b1.base(s) for s in SPEC.findall(text))
    new, recs, skipped = {}, [], []
    for i in z.infolist():
        low = i.filename.lower()
        if not low.startswith('models/players/') or not low.endswith(IMG_EXT):
            continue
        b = b1.base(i.filename)
        dname = os.path.splitext(i.filename)[0] + '.dds'
        if dname.lower() in names or dname in new:
            continue
        if b in layer_imgs or b in spec_imgs:
            skipped.append({'image': i.filename, 'why': 'layer image' if b in layer_imgs else 'specularMap'})
            continue
        im = np.asarray(Image.open(io.BytesIO(z.read(i))).convert('RGBA'))
        h, w = im.shape[:2]
        if not (pot(w) and pot(h)):
            skipped.append({'image': i.filename, 'why': 'not a power of two (%dx%d)' % (w, h)})
            continue
        opaque = bool(im[:, :, 3].min() >= 254)
        fmt, data, ch = None, None, None
        if opaque and b1.eligible(i.filename, roles):
            data, levels = b1.dds_bc1(im)
            dec = b1.bc1_decode(levels[0], w, h)
            ch = [round(b1.psnr(im[:, :, k], dec[:, :, k]), 2) for k in range(3)]
            if min(ch) >= BC1_PSNR:
                fmt = 'bc1'
        if fmt is None:
            data, levels = ld.dds_bc7(im)
            dec = np.frombuffer(t2d.decode_bc7(levels[0], w, h), np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0, 3]]
            ch = [round(b1.psnr(im[:, :, k], dec[:, :, k]), 2) for k in range(4)]
            fmt = 'bc7'
        nlev = int(np.log2(max(w, h))) + 1
        ok = bool(min(ch) >= MIN_PSNR and len(levels) == nlev)
        recs.append({'image': i.filename, 'dds': dname, 'format': fmt, 'w': w, 'h': h, 'psnr': ch, 'ok': ok,
                     'image_bytes': i.file_size, 'dds_bytes': len(data)})
        if ok:
            new[dname] = data
    if not dry:   # always written: a batch's OUT is this run's copy even when nothing changed
        rewrite(z, out, add=new.items())
    fails = [r['image'] for r in recs if not r['ok']]
    rep = {'in': inp, 'out': out if not dry else None, 'dds_written': len(new), 'skipped': skipped,
           'proof_failures': fails, 'images': recs}
    print('%s: %d images to DDS (%d BC1, %d BC7), %d skipped, %d proof failures (left as they are), min %.1f dB' % (
        os.path.basename(inp), len(new), sum(1 for r in recs if r['ok'] and r['format'] == 'bc1'),
        sum(1 for r in recs if r['ok'] and r['format'] == 'bc7'), len(skipped), len(fails),
        min((min(r['psnr']) for r in recs if r['ok']), default=99.0)), flush=True)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--report')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rep = convert(a.inp, a.out, a.dry_run)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)


if __name__ == '__main__':
    main()
