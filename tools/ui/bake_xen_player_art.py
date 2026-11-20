# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""Bakes the player screen's art (docs/wishlist/player-screen.md 10.11, the look) into a pack-layout folder.

  python tools/ui/bake_xen_player_art.py OUT_DIR [--mockup MOCKUP.html] [--chrome PATH]

The screen draws everything with stretch pics (no engine change; ui_player_look.c), so the look is baked:

- fonts/xp_<face>_<em>.png + .xfont: glyph atlases of the mockup's faces (Big Shoulders Display 900, Chakra Petch 700)
  with the screen's one shear (-12 degrees, about the baseline) baked in (and upright twins, dispu / tagu), white on alpha, several em sizes; and a
  blurred twin (_s.png, same cells) for the text shadow. Headless Chrome draws them with the web fonts the mockup
  loads, so the type is the mockup's. The bake fetches the two faces from Google Fonts (FONTS_CSS) at run time, so it
  needs the network; both are under the SIL Open Font License 1.1, so ship each family's OFL.txt beside the atlases
  made from them.
- gfx/xp/brush_c.png, brush_p.png: the mockup's two brush strokes (cyan, paper), baked at 1200 x 200 and stored at
  1024 x 256 (the renderer refuses sizes that are not powers of two; every picture here is drawn stretched).
- gfx/xp/cap.png (a parallelogram's end: a diagonal half), dot.png, glow.png, white.png, grad.png: shapes, drawn here
  with PIL.
- gfx/xp/bg<section>.jpg: the painted backdrop per section (its bloom on), with the ink wash, vignette and grain,
  captured from the mockup itself with the UI and the figure hidden (needs --mockup), stored at 2048 x 1024.
- --layers DIR (needs --mockup): the painted world in pieces for xenui (ui/xen/paint/), so the document moves them as the
  mockup does: world.jpg (the bands on ink, the 2800 x 1800 canvas at half size), bloom_c.png and bloom_p.png (the
  section blooms, 1200 x 800), menuwash.png (the menu's ink wash, 1500 x 1260), veil.png (1100 x 1200), bleed.png (the
  commit bloom, 900 x 900) and grain.png (the paper grain, a 640 tile), each the picture the mockup bakes for itself,
  stored at a power of two and drawn stretched to the mockup's size.

.xfont is text: "em", "ascent", "descent", "size W H", then one "g code advance x y w h ox oy" per glyph (pixels at
the em; ox oy put the cell's top left relative to the pen on the baseline). Codes are bytes: ASCII, plus 0xB7 middle
dot, 0x8B / 0x9B single angle quotes, 0x96 en dash, 0x18-0x1B arrows up, down, right, left.
"""
import argparse
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

# Chrome on PATH first, then the stock Windows install
CHROME = (shutil.which('chrome') or shutil.which('google-chrome') or shutil.which('chromium')
          or r'C:\Program Files\Google\Chrome\Application\chrome.exe')
FONTS_CSS = ('https://fonts.googleapis.com/css2?family=Big+Shoulders+Display:wght@900'
             '&family=Chakra+Petch:wght@700&display=block')
FACES = [  # name, css family, weight, em sizes, sheared (the mockup's list words, hints and player names are upright)
    ('disp', 'Big Shoulders Display', 900, [32, 64, 128], 1),
    ('dispu', 'Big Shoulders Display', 900, [32, 64, 128], 0),
    ('tag', 'Chakra Petch', 700, [18, 36], 1),
    ('tagu', 'Chakra Petch', 700, [18, 36], 0),
]
EXTRA = {0xB7: '\u00b7', 0x8B: '\u2039', 0x9B: '\u203a', 0x96: '\u2013',
         0x18: '\u2191', 0x19: '\u2193', 0x1A: '\u2192', 0x1B: '\u2190'}
SHEAR = 0.21255656  # tan 12 degrees

BRUSH_SVG = ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 600 100' preserveAspectRatio='none'>"
             "<filter id='b' x='-5%' y='-40%' width='110%' height='180%'><feTurbulence type='fractalNoise' "
             "baseFrequency='0.012 0.3' numOctaves='3' seed='4'/><feDisplacementMap in='SourceGraphic' scale='16' "
             "xChannelSelector='R' yChannelSelector='G'/></filter><g filter='url(#b)' fill='COL'>"
             "<path d='M10 60 C 70 30, 220 20, 420 18 C 500 17, 562 22, 594 34 C 578 54, 566 72, 520 82 C 380 90, 180 88, "
             "34 90 C 14 84, 4 72, 10 60 Z'/><path opacity='.55' d='M30 92 C 200 96, 400 95, 560 84 L 556 90 C 400 99, "
             "200 99, 40 97 Z'/><path opacity='.5' d='M60 14 C 200 8, 380 6, 520 10 L 516 14 C 380 11, 200 13, 64 18 Z'/>"
             "</g></svg>")

PAGE = r'''<!doctype html><html><head><meta charset="utf-8">
<link href="FONTS_CSS" rel="stylesheet"></head><body><pre id="out">pending</pre><script>
const FACES=FACES_JSON, CODES=CODES_JSON, SHEAR=SHEAR_VAL, BRUSHES=BRUSHES_JSON;
function atlas(fam,wt,em,sh){
  const SH=sh?SHEAR:0;
  const font=`${wt} ${em}px "${fam}"`;
  const c=document.createElement("canvas").getContext("2d"); c.font=font;
  const fm=c.measureText("Hg"), asc=fm.fontBoundingBoxAscent, desc=fm.fontBoundingBoxDescent;
  const pad=Math.ceil(em*0.25)+2, W=em>=96?2048:(em>=48?1024:512);
  const cells=[]; let x=0,y=0,rowH=0;
  for(const [code,ch] of CODES){
    const m=c.measureText(ch);
    const l=-m.actualBoundingBoxLeft-SH*m.actualBoundingBoxDescent, r=m.actualBoundingBoxRight+SH*m.actualBoundingBoxAscent;
    const top=-m.actualBoundingBoxAscent, bot=m.actualBoundingBoxDescent;
    const w=Math.ceil(Math.max(r-l,1))+2*pad, h=Math.ceil(Math.max(bot-top,1))+2*pad;
    if(x+w>W){ x=0; y+=rowH; rowH=0; }
    cells.push({code,ch,adv:m.width,x,y,w,h,ox:Math.floor(l)-pad,oy:Math.floor(top)-pad});
    x+=w; rowH=Math.max(rowH,h);
  }
  const H=Math.pow(2,Math.ceil(Math.log2(y+rowH)));
  const out=[];
  for(const blur of [0,1]){
    const cv=document.createElement("canvas"); cv.width=W; cv.height=H;
    const g=cv.getContext("2d"); g.font=font; g.fillStyle="#fff";
    if(blur) g.filter=`blur(${(em*0.09).toFixed(2)}px)`;
    for(const k of cells){
      g.setTransform(1,0,-SH,1,k.x-k.ox,k.y-k.oy);   /* pen on the baseline; x' = x - tan12 * y */
      g.fillText(k.ch,0,0);
    }
    out.push(cv.toDataURL("image/png"));
  }
  return {em,asc,desc,W,H,cells:cells.map(k=>[k.code,+k.adv.toFixed(3),k.x,k.y,k.w,k.h,k.ox,k.oy]),png:out[0],shadow:out[1]};
}
function bake(svg,w,h){ return new Promise(res=>{ const img=new Image();
  img.onload=()=>{ const c=document.createElement("canvas"); c.width=w; c.height=h; c.getContext("2d").drawImage(img,0,0,w,h); res(c.toDataURL("image/png")); };
  img.onerror=()=>res(""); img.src="data:image/svg+xml,"+encodeURIComponent(svg); }); }
(async()=>{
  const res={fonts:{},brushes:{}};
  for(const [name,fam,wt,sizes,sh] of FACES){
    await document.fonts.load(`${wt} 40px "${fam}"`);
    if(!document.fonts.check(`${wt} 40px "${fam}"`)){ document.getElementById("out").textContent="NOFONT "+fam; return; }
    for(const em of sizes) res.fonts[name+"_"+em]=atlas(fam,wt,em,sh);
  }
  for(const [name,svg] of BRUSHES) res.brushes[name]=await bake(svg,1200,200);
  document.getElementById("out").textContent=JSON.stringify(res);
})();
</script></body></html>'''

HIDE = '''<style>#bar,#ui,#figwrap,#veil,#toast,#field,#bleed,#join,#ghost{display:none!important}</style>
<script>
(function(){
  const sec=+(new URLSearchParams(location.search).get('sec')||0);
  setTimeout(()=>{ setMotion(0); fit(); setBloom(sec); },1500);
})();
</script>'''


def chrome(args, chrome_path, timeout=180):
    prof = tempfile.mkdtemp(prefix='xp_chrome_')
    try:
        return subprocess.run([chrome_path, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--mute-audio',
                               f'--user-data-dir={prof}'] + args, capture_output=True, timeout=timeout,
                              stdin=subprocess.DEVNULL)
    finally:
        shutil.rmtree(prof, ignore_errors=True)


def data_png(url, path):
    raw = base64.b64decode(url.split(',', 1)[1])
    with open(path, 'wb') as f:
        f.write(raw)


def bake_fonts_and_brushes(out, chrome_path, tmp):
    codes = [[c, chr(c)] for c in range(32, 127)] + [[k, v] for k, v in sorted(EXTRA.items())]
    page = (PAGE.replace('FONTS_CSS', FONTS_CSS).replace('FACES_JSON', json.dumps([list(f) for f in FACES]))
            .replace('CODES_JSON', json.dumps(codes)).replace('SHEAR_VAL', repr(SHEAR))
            .replace('BRUSHES_JSON', json.dumps([['brush_c', BRUSH_SVG.replace('COL', '#36DEEE')],
                                                 ['brush_p', BRUSH_SVG.replace('COL', '#F4F4EF')]])))
    src = os.path.join(tmp, 'atlas.html')
    with open(src, 'w', encoding='utf-8') as f:
        f.write(page)
    r = chrome(['--virtual-time-budget=30000', '--dump-dom', 'file:///' + src.replace('\\', '/')], chrome_path)
    dom = r.stdout.decode('utf-8', 'replace')
    start, end = dom.find('<pre id="out">'), dom.rfind('</pre>')
    text = dom[start + len('<pre id="out">'):end]
    if not text.startswith('{'):
        sys.exit(f'atlas page failed: {text[:200]!r}')
    res = json.loads(text.replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>').replace('&quot;', '"'))
    os.makedirs(os.path.join(out, 'fonts'), exist_ok=True)
    for key, a in res['fonts'].items():
        base = os.path.join(out, 'fonts', f'xp_{key}')
        data_png(a['png'], base + '.png')
        data_png(a['shadow'], base + '_s.png')
        lines = ['xfont 1', f'em {a["em"]}', f'ascent {a["asc"]:.3f}', f'descent {a["desc"]:.3f}',
                 f'size {a["W"]} {a["H"]}']
        lines += ['g ' + ' '.join(str(v) for v in cell) for cell in a['cells']]
        with open(base + '.xfont', 'w', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print('font', key, a['W'], 'x', a['H'], len(a['cells']), 'glyphs')
    os.makedirs(os.path.join(out, 'gfx', 'xp'), exist_ok=True)
    for name, url in res['brushes'].items():
        path = os.path.join(out, 'gfx', 'xp', name + '.png')
        data_png(url, path)
        Image.open(path).resize((1024, 256), Image.LANCZOS).save(path)     # the renderer takes power-of-two sizes
        print('brush', name)


def bake_shapes(out):
    d = os.path.join(out, 'gfx', 'xp')
    os.makedirs(d, exist_ok=True)
    ss = 8
    # cap: the left end of a parallelogram leaning right; filled right of the diagonal (bottom left to top right)
    n = 64 * ss
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).polygon([(n, 0), (n, n), (0, n)], fill=255)
    cap = Image.new('RGBA', (64, 64), (255, 255, 255, 0))
    cap.putalpha(im.resize((64, 64), Image.LANCZOS))
    cap.save(os.path.join(d, 'cap.png'))
    # dot: a filled circle; glow: a soft disc
    n = 64 * ss
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).ellipse([ss, ss, n - ss, n - ss], fill=255)
    dot = Image.new('RGBA', (64, 64), (255, 255, 255, 0))
    dot.putalpha(im.resize((64, 64), Image.LANCZOS))
    dot.save(os.path.join(d, 'dot.png'))
    im = Image.new('L', (128, 128), 0)
    ImageDraw.Draw(im).ellipse([32, 32, 96, 96], fill=255)
    glow = Image.new('RGBA', (128, 128), (255, 255, 255, 0))
    glow.putalpha(im.filter(ImageFilter.GaussianBlur(12)))
    glow.save(os.path.join(d, 'glow.png'))
    # white: a plain 8 x 8 for fills that need a texture; grad: alpha 1 to 0, left to right (the status line's wash)
    Image.new('RGBA', (8, 8), (255, 255, 255, 255)).save(os.path.join(d, 'white.png'))
    grad = Image.new('RGBA', (256, 4), (255, 255, 255, 0))
    grad.putalpha(Image.linear_gradient('L').rotate(-90).resize((256, 4)))
    grad.save(os.path.join(d, 'grad.png'))
    print('shapes cap dot glow white grad')


def bake_backdrops(out, mockup, chrome_path, tmp):
    with open(mockup, encoding='utf-8') as f:
        html = f.read()
    i = html.rfind('</body>')
    html = html[:i] + HIDE + html[i:]
    src = os.path.join(os.path.dirname(os.path.abspath(mockup)), '_xp_backdrop.html')
    with open(src, 'w', encoding='utf-8') as f:
        f.write(html)
    try:
        for sec in range(4):
            png = os.path.join(tmp, f'bg{sec}.png')
            for _ in range(3):
                chrome(['--window-size=1920,1080', '--virtual-time-budget=8000', f'--screenshot={png}',
                        'file:///' + src.replace('\\', '/') + f'?sec={sec}'], chrome_path)
                if os.path.exists(png):
                    break
            Image.open(png).convert('RGB').resize((2048, 1024), Image.LANCZOS).save(
                os.path.join(out, 'gfx', 'xp', f'bg{sec}.jpg'), quality=92)
            print('backdrop', sec)
    finally:
        os.remove(src)


LAYERS = '''<script>
(function(){
  const want={world:"#drift img",bloom_c:"#world .bloom[data-s='0'] img",bloom_p:"#world .bloom[data-s='1'] img",
    menuwash:"#menuwash img",veil:"#veil img",bleed:"#bleed img"};
  const grain=`<svg xmlns='http://www.w3.org/2000/svg' width='640' height='640'><filter id='g'><feTurbulence type='fractalNoise' baseFrequency='.85' numOctaves='2' stitchTiles='stitch'/>`+
    `<feColorMatrix values='0 0 0 0 .96  0 0 0 0 .96  0 0 0 0 .94  1.7 0 0 0 -.8'/></filter><rect width='640' height='640' filter='url(#g)'/></svg>`;
  const out=document.createElement("pre"); out.id="layers"; out.textContent="pending"; document.body.appendChild(out);
  let tries=0;
  (function poll(){
    const res={}; let ok=true;
    for(const k in want){ const el=document.querySelector(want[k]); const src=el&&el.src||"";
      if(!src.startsWith("data:image/png")) ok=false; res[k]=src; }
    if(!ok && ++tries<200){ setTimeout(poll,100); return; }
    bake(grain,640,640).then(u=>{ res.grain=u; out.textContent=JSON.stringify(res); });
  })();
})();
</script>'''

# stored sizes: the PNG loader takes powers of two only (world is a JPEG); the document stretches each to its box
LAYER_SIZES = {'world': (1400, 900), 'bloom_c': (512, 512), 'bloom_p': (512, 512), 'menuwash': (1024, 1024),
               'veil': (512, 512), 'bleed': (512, 512), 'grain': (512, 512)}


def bake_layers(out, mockup, chrome_path):
    with open(mockup, encoding='utf-8') as f:
        html = f.read()
    i = html.rfind('</body>')
    html = html[:i] + LAYERS + html[i:]
    src = os.path.join(os.path.dirname(os.path.abspath(mockup)), '_xp_layers.html')
    with open(src, 'w', encoding='utf-8') as f:
        f.write(html)
    try:
        r = chrome(['--window-size=1920,1080', '--virtual-time-budget=30000', '--dump-dom',
                    'file:///' + src.replace('\\', '/')], chrome_path)
    finally:
        os.remove(src)
    dom = r.stdout.decode('utf-8', 'replace')
    start, end = dom.find('<pre id="layers">'), dom.find('</pre>', dom.find('<pre id="layers">'))
    text = dom[start + len('<pre id="layers">'):end].replace('&amp;', '&').replace('&quot;', '"')
    if not text.startswith('{'):
        sys.exit(f'layer page failed: {text[:200]!r}')
    res = json.loads(text)
    os.makedirs(out, exist_ok=True)
    for name, url in res.items():
        if not url.startswith('data:image/png'):
            sys.exit(f'layer {name} did not bake')
        png = os.path.join(out, name + '.png')
        data_png(url, png)
        img = Image.open(png).convert('RGBA')
        full = img.size
        img = img.resize(LAYER_SIZES[name], Image.LANCZOS)
        if name == 'world':     # opaque: the bands over the canvas's ink
            ink = Image.new('RGBA', img.size, (11, 12, 16, 255))
            ink.alpha_composite(img)
            ink.convert('RGB').save(os.path.join(out, 'world.jpg'), quality=92)
            os.remove(png)
        else:
            img.save(png)
        print('layer', name, full, '->', img.size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--mockup')
    ap.add_argument('--chrome', default=CHROME)
    ap.add_argument('--layers', help='only the paint layers for xenui, into this folder (needs --mockup)')
    a = ap.parse_args()
    if a.layers:
        if not a.mockup:
            sys.exit('--layers needs --mockup')
        bake_layers(a.layers, a.mockup, a.chrome)
        return
    tmp = tempfile.mkdtemp(prefix='xp_art_')
    try:
        bake_fonts_and_brushes(a.out, a.chrome, tmp)
        bake_shapes(a.out)
        if a.mockup:
            bake_backdrops(a.out, a.mockup, a.chrome, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
