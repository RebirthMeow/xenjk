"""Bake sharp variants of the stock Jedi Academy fonts for OpenJK's r_fontSharpness.

OpenJK picks `fonts/<name>_sharpN` (N = 1..8, rising point sizes) when a string's on-screen size is above the base
font's point size (rd-common/tr_font.cpp, RE_Font_GetVariant). The game ships none, so at 4K a 20-point atlas glyph is
magnified four times and looks soft. This tool makes the variants from the stock atlases themselves, so the typeface
and metrics stay exactly the same: each atlas is upscaled 2x and 4x, its coverage edges re-sharpened (a smoothstep
around half coverage), and the fontdat's pixel metrics scaled with it (texture coordinates are fractions and stay).

    python tools/fonts/make_sharp_fonts.py --stock <GameData>/base/assets1.pk3 --out <dir>/xen_sharp_fonts.pk3
"""
import argparse
import io
import struct
import zipfile

import numpy as np
from PIL import Image, ImageFilter

FONTS = ('ocr_a', 'ergoec', 'anewhope', 'arialnb')
FACTORS = (2, 4)            # _sharp1, _sharp2
GLYPHS = 256
GLYPH = struct.Struct('<4hi4f')   # width, height, horizAdvance, horizOffset, baseline, s, t, s2, t2
HEADER = struct.Struct('<5h')     # pointSize, height, ascender, descender, koreanHack


def scale_fontdat(data, k):
    out = bytearray(data)
    for i in range(GLYPHS):
        w, h, adv, off, base, s, t, s2, t2 = GLYPH.unpack_from(data, i * GLYPH.size)
        GLYPH.pack_into(out, i * GLYPH.size, w * k, h * k, adv * k, off * k, base * k, s, t, s2, t2)
    pt, hgt, asc, desc, kor = HEADER.unpack_from(data, GLYPHS * GLYPH.size)
    HEADER.pack_into(out, GLYPHS * GLYPH.size, pt * k, hgt * k, asc * k, desc * k, kor)
    return bytes(out)


def sharpen_up(img, k):
    rgba = img.convert('RGBA')
    size = (rgba.width * k, rgba.height * k)
    rgb = rgba.convert('RGB').resize(size, Image.BICUBIC)
    a = rgba.getchannel('A').resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(k * 0.35))
    x = np.asarray(a).astype(np.float32) / 255.0
    lo, hi = 0.30, 0.62          # a little below half, so one-texel strokes keep their weight
    t = np.clip((x - lo) / (hi - lo), 0.0, 1.0)
    t = t * t * (3.0 - 2.0 * t)
    return Image.merge('RGBA', (*rgb.split(), Image.fromarray((t * 255.0 + 0.5).astype(np.uint8))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--stock', required=True, help='a pk3 with the stock fonts (assets1.pk3)')
    ap.add_argument('--out', required=True, help='the pack to write')
    ap.add_argument('--fonts', nargs='*', default=list(FONTS))
    a = ap.parse_args()

    with zipfile.ZipFile(a.stock) as src, zipfile.ZipFile(a.out, 'w', zipfile.ZIP_STORED) as dst:
        names = {n.lower(): n for n in src.namelist()}
        for font in a.fonts:
            dat = src.read(names['fonts/%s.fontdat' % font])
            img = None
            for ext in ('.tga', '.png', '.jpg'):
                if 'fonts/%s%s' % (font, ext) in names:
                    img = Image.open(io.BytesIO(src.read(names['fonts/%s%s' % (font, ext)])))
                    break
            if img is None:
                raise SystemExit('no atlas for ' + font)
            for n, k in enumerate(FACTORS, 1):
                name = 'fonts/%s_sharp%d' % (font, n)
                dst.writestr(name + '.fontdat', scale_fontdat(dat, k))
                buf = io.BytesIO()
                sharpen_up(img, k).save(buf, 'PNG', optimize=True)
                dst.writestr(name + '.png', buf.getvalue())
                print(name, img.size, 'x%d' % k)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
