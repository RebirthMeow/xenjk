"""Placeholder xenjk client icon: a bold "XJ" monogram drawn from plain shapes (no font files), so it is ours to ship.

    python tools/xenjk_icon.py            # writes shared/icons/xenjk.ico, shared/icons/PNG/xenjk<N>.png and the
                                          # MP window icon in shared/sdl/sdl_icon.h (32x32, between its markers)
    python tools/xenjk_icon.py --from art.png   # the same outputs from your own square artwork

The .ico (16, 20, 24, 32, 40, 48, 64, 128, 256) is the client exe's icon (codemp/win32/openjk.rc). Needs Pillow.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ICO = ROOT / 'shared' / 'icons' / 'xenjk.ico'
PNG_DIR = ROOT / 'shared' / 'icons' / 'PNG'
SDL_ICON = ROOT / 'shared' / 'sdl' / 'sdl_icon.h'
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
PNG_SIZES = [16, 32, 48, 64, 128, 256, 512]
BEGIN, END = '#else // OpenJK Icon', '#endif'

BG = (14, 23, 38, 255)        # near-black navy
EDGE = (40, 64, 96, 255)
INK = (76, 201, 240, 255)     # saber blue


def monogram(size=1024):
    s = size
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = s * 0.03
    d.rounded_rectangle([pad, pad, s - pad, s - pad], radius=s * 0.2, fill=BG, outline=EDGE, width=int(s * 0.025))

    w = s * 0.13              # stroke width
    top, bottom = s * 0.25, s * 0.75
    # X: two diagonal bars in the left half
    xl, xr = s * 0.13, s * 0.53
    for (x0, y0, x1, y1) in ((xl, top, xr, bottom), (xl, bottom, xr, top)):
        dx, dy = x1 - x0, y1 - y0
        n = (dx * dx + dy * dy) ** 0.5
        ox, oy = -dy / n * w / 2, dx / n * w / 2
        d.polygon([(x0 + ox, y0 + oy), (x1 + ox, y1 + oy), (x1 - ox, y1 - oy), (x0 - ox, y0 - oy)], fill=INK)
    # J: top bar, stem, and a hook to the left
    wj = s * 0.11
    jl, jr = s * 0.55, s * 0.88
    stem_l = jr - wj
    hook_y = bottom - (jr - jl) / 2
    d.rectangle([jl + s * 0.08, top, jr, top + wj], fill=INK)
    d.rectangle([stem_l, top, jr, hook_y], fill=INK)
    r_out = (jr - jl) / 2
    cx, cy = jl + r_out, hook_y
    d.pieslice([cx - r_out, cy - r_out, cx + r_out, cy + r_out], 0, 180, fill=INK)
    r_in = r_out - wj
    d.pieslice([cx - r_in, cy - r_in, cx + r_in, cy + r_in], 0, 180, fill=BG)
    d.rectangle([jl, cy - wj * 0.6, jl + wj, cy], fill=INK)   # hook tip
    return img


def scaled(master, n):
    return master.resize((n, n), Image.LANCZOS)


def sdl_block(img32):
    data = img32.convert('RGBA').tobytes()
    lines, row = [], ''
    for i, b in enumerate(data):
        row += '\\%03o' % b
        if (i + 1) % 32 == 0:
            lines.append('  "%s"' % row)
            row = ''
    if row:
        lines.append('  "%s"' % row)
    return BEGIN + ' (xenjk: tools/xenjk_icon.py)\n' + '\n'.join(lines) + ',\n'


def write_sdl_icon(img32):
    text = SDL_ICON.read_text(encoding='utf-8')
    start = text.index(BEGIN)
    end = text.index(END, start)
    SDL_ICON.write_text(text[:start] + sdl_block(img32) + text[end:], encoding='utf-8', newline='\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--from', dest='src', help='square artwork to use instead of the generated monogram')
    args = ap.parse_args()
    master = Image.open(args.src).convert('RGBA').resize((1024, 1024), Image.LANCZOS) if args.src else monogram()
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    for n in PNG_SIZES:
        scaled(master, n).save(PNG_DIR / ('xenjk%d.png' % n))
    scaled(master, 256).save(ICO, sizes=[(n, n) for n in ICO_SIZES])
    write_sdl_icon(scaled(master, 32))
    print('wrote %s, %s/xenjk*.png, %s' % (ICO, PNG_DIR, SDL_ICON))


if __name__ == '__main__':
    main()
