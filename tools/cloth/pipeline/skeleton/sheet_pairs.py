"""sheet_pairs.py <out.jpg> <shot> [<shot> ...] -- rows of (eve2b | eve2b_np) for the given shot names, labelled."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os
from PIL import Image, ImageDraw
D = os.path.join(str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012'), 'home', 'base', 'screenshots')
out = sys.argv[1]; shots = sys.argv[2:]
w, h = 640, 360
sheet = Image.new('RGB', (2 * w, h * len(shots)), 'black'); dr = ImageDraw.Draw(sheet)
for r, s in enumerate(shots):
    for c, pre in enumerate(('old', 'np')):
        im = Image.open(os.path.join(D, f'{pre}_{s}.jpg')).convert('RGB').resize((w, h))
        sheet.paste(im, (c * w, r * h))
        dr.text((c * w + 8, r * h + 6), ('eve2b (_humanoid)' if pre == 'old' else 'eve2b_np (her proportions)') + '  ' + s, fill=(255, 255, 0))
sheet.save(out, quality=88); print(out, sheet.size)
