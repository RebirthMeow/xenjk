"""Bake a high-resolution HUD charset with the stock Jedi Academy grid (gfx/2d/charsgrid_med).

The stock charset is a 256x256 texture: 16x16 cells of 16x16 texels, each glyph in the left 8x16 texels of its cell
(the game draws a cell's left half, see CG_DrawChar). At 4K a glyph covers about 54x72 pixels, so the 8x16 glyph is
magnified six to twelve times and looks soft. This tool renders the same grid from a TrueType font at a size that
covers the glyph at native resolution (2048x2048: 64x128 texels a glyph), keeps the stock glyphs it does not render
(the console cursor, control codes), and writes a pack with a nopicmip, mipmapped 2D shader. cgame's cg_hudCharset
names it; without the pack the game keeps the stock charset.

Use a font whose licence allows shipping (Noto Sans is SIL OFL 1.1). The stock glyphs are a bold condensed sans;
the font is condensed to the cell width.

    python tools/fonts/make_hud_charset.py --font C:/Windows/Fonts/NotoSans_Bold.ttf \
        --stock <GameData>/base/assets1.pk3 --out <dir>/xen_hud_charset.pk3 [--preview <png>]
"""
import argparse
import io
import zipfile

from PIL import Image, ImageDraw, ImageFont

SHADER_NAME = 'gfx/2d/charsgrid_hd'
SHADER = '''// xenjk HUD charset: the stock charsgrid_med grid at native resolution (tools/fonts/make_hud_charset.py)
%s
{
	nopicmip
	{
		map %s.png
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
'''


def glyph_codes():
    # printable ASCII and the Latin-1 range the stock grid fills (the game's text is Windows-1252)
    codes = list(range(32, 127)) + list(range(160, 256))
    return [c for c in codes if c != 32 and c != 160]


def render(font_path, size, stock_png):
    cell = size // 16
    gw, gh = cell // 2, cell  # the drawn half of a cell
    atlas = Image.new('L', (size, size), 0)

    # the stock glyphs, upscaled, for the codes not rendered below
    stock = stock_png.getchannel('A').resize((size, size), Image.BICUBIC)
    rendered = set(glyph_codes())
    for code in range(256):
        if code in rendered:
            continue
        r, c = code >> 4, code & 15
        box = (c * cell, r * cell, c * cell + cell, r * cell + cell)
        atlas.paste(stock.crop(box), box[:2])

    # cap top at 2/16 of the cell and baseline at 13/16 (stock: 1.5 and 14.5, which clips descenders), glyph 7/8 of the half-cell wide
    cap_top, baseline = gh * 2.0 / 16, gh * 13.0 / 16
    cap_h = baseline - cap_top
    probe = ImageFont.truetype(font_path, 100)
    hb = probe.getbbox('H')
    px = round(100 * cap_h / (hb[3] - hb[1]))
    font = ImageFont.truetype(font_path, px)
    ascent = font.getmetrics()[0]
    # condense so a digit fills 6.5/8 of the half-cell like the stock glyphs; wider glyphs (M, W, @, %) are
    # condensed further, each to the 7.5/8 limit
    digit = max(font.getbbox(ch)[2] - font.getbbox(ch)[0] for ch in '0123456789')
    squeeze = min(1.0, gw * 6.5 / 8 / digit)
    limit = gw * 7.5 / 8

    for code in rendered:
        ch = bytes([code]).decode('cp1252')
        bbox = font.getbbox(ch)
        if bbox[2] <= bbox[0]:
            continue
        tile = Image.new('L', (bbox[2] - bbox[0] + 8, gh * 2), 0)
        # draw so the font baseline sits at the stock baseline
        ImageDraw.Draw(tile).text((4 - bbox[0], baseline - ascent), ch, font=font, fill=255)
        ink = (bbox[2] - bbox[0]) * squeeze
        k = squeeze * min(1.0, limit / ink) if ink > 0 else squeeze
        w = max(1, round(tile.width * k))
        tile = tile.resize((w, tile.height), Image.LANCZOS).crop((0, 0, w, gh))
        r, c = code >> 4, code & 15
        x = c * cell + (gw - w) // 2
        atlas.paste(tile, (max(c * cell, x), r * cell), tile)

    rgba = Image.merge('RGBA', [Image.new('L', atlas.size, 255)] * 3 + [atlas])
    return rgba


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--font', required=True)
    ap.add_argument('--stock', required=True, help='a pk3 with gfx/2d/charsgrid_med.tga (assets1.pk3)')
    ap.add_argument('--out', required=True, help='the pack to write')
    ap.add_argument('--size', type=int, default=2048)
    ap.add_argument('--preview', help='also write the atlas over a dark backdrop')
    a = ap.parse_args()

    with zipfile.ZipFile(a.stock) as z:
        stock = Image.open(io.BytesIO(z.read('gfx/2d/charsgrid_med.tga'))).convert('RGBA')
    atlas = render(a.font, a.size, stock)
    buf = io.BytesIO()
    atlas.save(buf, 'PNG', optimize=True)
    with zipfile.ZipFile(a.out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(SHADER_NAME + '.png', buf.getvalue())
        z.writestr('shaders/xen_hud_charset.shader', SHADER % (SHADER_NAME, SHADER_NAME))
    if a.preview:
        bg = Image.new('RGBA', atlas.size, (40, 40, 90, 255))
        bg.alpha_composite(atlas)
        bg.convert('RGB').save(a.preview)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
