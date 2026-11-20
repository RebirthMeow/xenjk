"""make_pbr_pack_np.py <out.pk3> [model] (model name parameter) -- Eve 2B PBR materials for rend2/JKSunny (.mtr; rd-vanilla ignores .mtr).
Ripped commercial assets: local only, never commit, package or upload."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, io, zipfile
import numpy as np
from PIL import Image
if not os.environ.get('CLOTH_RESEARCH'):   # the cloth research checkout with the source model's textures
    raise SystemExit('set CLOTH_RESEARCH to the cloth research folder (characters/eve/tex)')
T = str(Path(os.environ['CLOTH_RESEARCH']) / 'characters' / 'eve' / 'tex')
MODEL = sys.argv[2] if len(sys.argv) > 2 else 'eve2b_np'
P = 'models/players/%s/' % MODEL
out = sys.argv[1]
def img(name): return Image.open(os.path.join(T, name))
def jpg(im):   # JPEG q95: rd-vulkan's PNG path took ~12 s per 2048 map (t1: cgame init 174 s vs 10 s)
    b = io.BytesIO(); im.save(b, 'JPEG', quality=95, subsampling=0); return b.getvalue()
def orm_from_orss(name):   # R occlusion, G roughness, B subsurface, A specular -> ORM with metal 0
    a = np.asarray(img(name).convert('RGBA')).copy()
    o = np.zeros(a.shape[:2] + (3,), np.uint8); o[..., 0] = a[..., 0]; o[..., 1] = a[..., 1]
    return Image.fromarray(o, 'RGB')
# material: (albedo in base pk3, normal src, orm src or ('orss', src), extra stage lines, shader-level lines)
M = {
    'body':  ('body.jpg',  'CH_P_EVE_BB_V03_TypeA_N.png',    ('orss', 'CH_P_EVE_Nier_01_BB_ORSS.png'), [], []),
    'head':  ('head.jpg',  'Tex_P_EVE_Head_N.png',           ('orss', 'CH_P_EVE_Head_ORSS.png'), [], []),
    'upper': ('upper.jpg', 'CH_P_EVE_Nier_01_Upper_N.png',   'CH_P_EVE_Nier_01_Upper_ORM.png', [], []),
    'boots': ('boots.jpg', 'CH_P_EVE_Nier_01_Boots_N.png',   'CH_P_EVE_Nier_01_Boots_ORM.png', [], []),
    'skirt_2s': ('skirt.jpg', 'CH_P_EVE_Nier_01_Skirt_N.png', 'CH_P_EVE_Nier_01_Skirt_ORM.png', [], ['cull none']),
    'feather': ('feather.tga', 'CH_P_EVE_Nier_01_Feather_N.png', 'CH_P_EVE_Nier_01_Feather_ORM.png',
                ['alphaFunc GE128', 'depthWrite'], ['cull none']),
    'teeth': ('teeth.jpg', 'Tex_P_EVE_Teeth_N.png', None, [], []),
}
mtr = []
with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
    for name, (alb, n, orm, stage, top) in M.items():
        base = name.replace('_2s', '')
        z.writestr(P + base + '_n.jpg', jpg(img(n).convert('RGB')))
        lines = ['\tmap ' + P + alb.rsplit('.', 1)[0], '\tnormalMap ' + P + base + '_n']
        if orm:
            im = orm_from_orss(orm[1]) if isinstance(orm, tuple) else img(orm).convert('RGB')
            z.writestr(P + base + '_orm.jpg', jpg(im))
            lines.append('\tormMap ' + P + base + '_orm')
        lines += ['\t' + s for s in stage] + ['\trgbGen lightingDiffuse']
        body = ''.join('\t%s\n' % t for t in top) + '\t{\n' + ''.join('\t%s\n' % l for l in lines) + '\t}\n'
        mtr.append('%s%s\n{\n%s}\n' % (P, name, body))
    z.writestr('shaders/%s_pbr.mtr' % MODEL, '\r\n'.join(mtr).replace('\n', '\r\n'))
print(out, os.path.getsize(out) // 1024, 'KB'); print('\n'.join(mtr))
