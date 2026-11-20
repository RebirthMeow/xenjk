"""sheet.py <prefix> <out.jpg> [tags...]: on/off pairs side by side (crop around the screen centre), label each cell."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, glob, os
from PIL import Image, ImageDraw
W = str(REPO / '.codex-research' / 'eve2b-np-cloth-20261012')
prefix, out = sys.argv[1], sys.argv[2]
tags = sys.argv[3:]
shots = os.path.join(W, 'home', 'base', 'screenshots')
if not tags:
    tags = []
    for f in sorted(glob.glob(os.path.join(shots, prefix + '_*_on.jpg')), key=os.path.getmtime):
        tags.append(os.path.basename(f)[len(prefix) + 1:-len('_on.jpg')])
CW, CH = 520, 600
cols = 2  # pairs per row
rows = (len(tags) + cols - 1) // cols
sheet = Image.new('RGB', (CW * 2 * cols, CH * rows), (20, 20, 20))
d = ImageDraw.Draw(sheet)
for i, t in enumerate(tags):
    for j, s in enumerate(('on', 'off')):
        f = os.path.join(shots, f'{prefix}_{t}_{s}.jpg')
        if not os.path.exists(f): continue
        im = Image.open(f)
        w, h = im.size
        box = (w // 2 - CW // 2, h // 2 - CH // 2 + 40, w // 2 + CW // 2, h // 2 + CH // 2 + 40)
        x = (i % cols) * CW * 2 + j * CW; y = (i // cols) * CH
        sheet.paste(im.crop(box), (x, y))
        d.rectangle((x, y, x + 210, y + 22), fill=(0, 0, 0))
        d.text((x + 4, y + 5), f'{t}  cloth {s.upper()}', fill=(255, 255, 0))
    d.line(((i % cols) * CW * 2, (i // cols) * CH, (i % cols) * CW * 2, (i // cols) * CH + CH), fill=(255, 255, 255), width=3)
sheet.save(out, quality=85)
print(out, len(tags), 'pairs')
