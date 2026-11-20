from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os
from PIL import Image
import numpy as np
if not os.environ.get('CLOTH_RESEARCH'):   # the cloth research checkout with the source model's textures
    raise SystemExit('set CLOTH_RESEARCH to the cloth research folder (characters/eve/tex)')
T = str(Path(os.environ['CLOTH_RESEARCH']) / 'characters' / 'eve' / 'tex')
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
J = os.path.join
def jpg(src, name, size=2048):
    im = Image.open(J(T, src)).convert('RGB')
    if im.size[0] != size: im = im.resize((size, size), Image.LANCZOS)
    im.save(J(out, name + '.jpg'), quality=90)
jpg('CH_P_EVE_BB_V03_TypeA_A.png', 'body'); jpg('CH_P_EVE_Nier_01_Upper_A.png', 'upper'); jpg('CH_P_EVE_Nier_01_Skirt_A.png', 'skirt')
jpg('CH_P_EVE_Nier_01_Boots_A.png', 'boots'); jpg('Tex_P_EVE_Head_A.png', 'head'); jpg('Tex_P_EVE_Teeth_A.png', 'teeth', 512)
# feather: rgba, 1024
f = Image.open(J(T, 'CH_P_EVE_Nier_01_Feather_A.png')).convert('RGBA').resize((1024, 1024), Image.LANCZOS)
f.save(J(out, 'feather.tga'), rle=True)
# hair: flat colour (materials.json), alpha = R channel of the ADIR card atlas
h = Image.open(J(T, 'EVE_HR_Nier_01_ADIR.png')).convert('RGBA').resize((1024, 1024), Image.LANCZOS)
r = np.array(h)[..., 0].astype(np.float32)
a = np.clip(r * 1.6, 0, 255).astype(np.uint8)
col = np.zeros((1024, 1024, 4), np.uint8)
col[..., 0] = int(0.82 * 255); col[..., 1] = int(0.80 * 255); col[..., 2] = int(0.78 * 255); col[..., 3] = a
Image.fromarray(col, 'RGBA').save(J(out, 'hair.tga'), rle=True)
# brow: dark, alpha from mask
b = np.array(Image.open(J(T, 'Tex_P_EVE_eyebrow_O.png')).convert('RGB')).mean(-1)
bc = np.zeros(b.shape + (4,), np.uint8); bc[..., :3] = (5, 4, 3); bc[..., 3] = np.clip(b * 4, 0, 255).astype(np.uint8)
Image.fromarray(bc, 'RGBA').save(J(out, 'brow.tga'), rle=True)
def solid(name, rgba): Image.new('RGBA', (8, 8), rgba).save(J(out, name + '.tga'))
solid('eye', (5, 5, 6, 255)); solid('mouth', (10, 5, 5, 255)); solid('glass', (153, 178, 191, 90))
print(sorted(os.listdir(out)))
