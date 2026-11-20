"""CT-3X chunk materials -> rd-vulkan stages (ct3x_model.py): the bike's own CP2077 materials instead of flat colours.

Per chunk, by its CP2077 material (the chunk's own values over its .mi parent's):
  multilayered   tools/levelport/cp77mat.py's bake (colour, normal, ORM; perceptual roughness), its most visible layer
                 left to the renderer as tiled detail (detailNormalMap + detailMap, weighted by materialMask's alpha),
                 and the part's GlobalNormal (the tyre's tread) under the layers' slopes
  decal          vehicle_mesh_decal(_parallax): stickers and the decal atlas, alpha-blended over the body with their
                 normal maps (the old cp_car route dropped them)
  glass          vehicle_glass: the windshield, its TintColor (the CT-3X's is orange) as a thin glossy blend
  light          vehicle_lights: emissive in the colour of the bike's own light components by where the chunk sits
                 (front: head, rear: brake/tail at running level) or, for the neon rims, the utility lights' colour;
                 with `glow`
  blinker / ui / metal_base   small flat materials (amber lens, dark screen, the rims' "off" metal)
Textures are written next to the model as PNG (normals, masks) or JPG (colour, ORM). Roughness is perceptual
(`roughnessEncoding perceptual`, rd-vulkan's default)."""
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# the level port's material bake (XENJK_MAIN: another checkout's tools/levelport instead of this repository's)
MAIN = Path(os.environ.get('XENJK_MAIN', Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(MAIN / 'tools' / 'levelport'))
import cp77mat  # noqa: E402

SL = chr(92)
PBR_KEYS = ('normalMap', 'ormMap', 'roughnessEncoding', 'detailNormalMap', 'detailMap', 'materialMask', 'emissiveMap',
            'glow')
NEUTRAL_NORMALS = ('engine' + SL + 'textures' + SL + 'editor' + SL + 'normal.xbm', 'small_flat_normal.xbm')


def values(m):
    out = {}
    for v in (m or {}).get('values', []):
        k = [x for x in v if x != '$type'][0]
        val = v[k]
        out[k] = val['DepotPath']['$value'] if isinstance(val, dict) and 'DepotPath' in val else val
    return out


def rgb(c, default=(1, 1, 1)):
    if isinstance(c, dict) and 'Red' in c:
        return np.array([c['Red'], c['Green'], c['Blue']], np.float64) / 255.0
    if isinstance(c, dict) and 'X' in c:
        return np.array([c['X'], c['Y'], c['Z']], np.float64)
    return np.array(default, np.float64)


class Paint:
    def __init__(self, glb_root: Path, json_dir: Path, stage: Path, ns: str, lights: dict, size=2048, detail=512):
        self.root, self.json, self.stage, self.ns = Path(glb_root), Path(json_dir), Path(stage), ns
        self.mats = cp77mat.Materials(glb_root, size=size, layer_size=1024, ml_size=size, detail_size=detail)
        self.size = size
        self.lights = lights            # {'head': rgb, 'brake': rgb, 'utility': rgb} from the app's light components
        self.blocks, self.made = {}, {}
        self.mi_cache = {}
        (self.stage / ns).mkdir(parents=True, exist_ok=True)

    # ---- inputs

    def parent(self, depot):
        """(template file name, values) of a .mi chain, parent values under the child's."""
        if depot in self.mi_cache:
            return self.mi_cache[depot]
        name = depot.split(SL)[-1]
        f = self.json / (name + '.json')
        if not depot.endswith('.mi') or not f.exists():
            got = (name.lower(), {})
        else:
            r = json.load(open(f, encoding='utf-8'))['Data']['RootChunk']
            base = r.get('baseMaterial', {}).get('DepotPath', {}).get('$value', '')
            t, pv = self.parent(base)
            got = (t, dict(pv, **values(r)))
        self.mi_cache[depot] = got
        return got

    def resolve(self, m):
        base = (m or {}).get('baseMaterial', {}).get('DepotPath', {}).get('$value', '')
        t, pv = self.parent(base)
        return t, dict(pv, **values(m)), base.split(SL)[-1].lower()

    def img(self, depot, mode='RGBA'):
        p = self.mats.png(depot)
        if p is None or not p.exists():
            return None
        return Image.open(p).convert(mode)

    def save(self, name, arr_or_img, ext):
        im = arr_or_img if isinstance(arr_or_img, Image.Image) else Image.fromarray(arr_or_img)
        rel = f'{self.ns}/{name}{ext}'
        p = self.stage / rel
        if ext == '.jpg':
            im.convert('RGB').save(p, quality=93)
        else:
            im.save(p)
        return rel

    def flat(self, key, colour, rough, metal, extra=''):
        """A one-colour lit material (8x8 maps)."""
        name = f'{self.ns}/{key}'
        if name in self.blocks:
            return name
        c = np.uint8(np.round(np.clip(colour, 0, 1) ** (1 / 2.2) * 255))
        self.save(key, np.full((8, 8, 3), c, np.uint8), '.png')
        self.save(key + '_orm', np.full((8, 8, 3), (255, round(rough * 255), round(metal * 255)), np.uint8), '.png')
        self.blocks[name] = (f'{name}\n{{\n\tsurfaceparm nomarks\n{extra}\t{{\n\t\tmap {name}.png\n'
                             f'\t\tormMap {name}_orm.png\n\t\troughnessEncoding perceptual\n'
                             f'\t\trgbGen lightingDiffuse\n\t}}\n}}\n')
        return name

    # ---- kinds

    def multilayer(self, v):
        setup, mask = v.get('MultilayerSetup'), v.get('MultilayerMask')
        gn = v.get('GlobalNormal')
        gn = gn if gn and not any(gn.endswith(x) for x in NEUTRAL_NORMALS) else None
        stem = setup.split(SL)[-1].rsplit('.', 1)[0]
        key = f"{stem[:16]}_{hashlib.md5(f'{setup}|{mask}|{gn}'.encode()).hexdigest()[:5]}"
        name = f'{self.ns}/{key}'
        if name in self.blocks:
            return name
        got = self.mats.multilayer(setup, mask, self.size)
        if got is None:
            return self.flat('missing', (0.5, 0.5, 0.5), 0.6, 0.0)
        col, nrm, orm, det = got
        n = col.shape[0]
        slope = nrm[..., :2].astype(np.float32) / 255 * 2 - 1
        slope = slope / np.maximum(nrm[..., 2:3].astype(np.float32) / 255 * 2 - 1, 0.2)
        if gn is not None:
            g = self.img(gn, 'RGB')
            if g is not None:
                g = np.asarray(g.resize((n, n), Image.Resampling.BILINEAR), np.float32) / 255
                # CP normal maps are DirectX (green down), as cp77mat reads the templates'
                slope = slope + np.stack([g[..., 0] * 2 - 1, -(g[..., 1] * 2 - 1)], -1)
        nv = np.concatenate([slope, np.ones((n, n, 1), np.float32)], -1)
        nv /= np.linalg.norm(nv, axis=-1, keepdims=True)
        shown = orm[..., 0].copy() if det is not None else None
        orm = orm.copy()
        orm[..., 0] = 255                           # no occlusion bake: red was the main layer's share (cp77mat v1)
        self.save(key, col, '.jpg')
        self.save(key + '_n', np.uint8(np.round((nv * 0.5 + 0.5) * 255)), '.png')
        self.save(key + '_orm', orm, '.jpg')
        stage = (f'\t\tmap {name}.jpg\n\t\tnormalMap {name}_n.png\n\t\tormMap {name}_orm.jpg\n'
                 f'\t\troughnessEncoding perceptual\n')
        if det is not None and shown is not None:
            d = self.mats.detail_image(det['template'])
            dn = np.stack([d[..., 0], d[..., 1], np.full(d.shape[:2], 255, np.uint8)], -1)
            dm = np.stack([d[..., 2], d[..., 2], d[..., 2], d[..., 3]], -1)
            dk = f"det_{hashlib.md5(det['template'].encode()).hexdigest()[:8]}"
            if dk not in self.made:
                self.save(dk + '_n', dn, '.png')
                self.save(dk, dm, '.png')
                self.made[dk] = True
            mk = np.zeros((n, n, 4), np.uint8)
            mk[..., 2] = 128
            mk[..., 3] = shown
            self.save(key + '_mask', Image.fromarray(mk).resize((n // 2, n // 2), Image.Resampling.BILINEAR), '.png')
            stage += (f"\t\tdetailNormalMap {self.ns}/{dk}_n.png {det['tile']:g} {max(det['normal'], 0.0):g}\n"
                      f"\t\tdetailMap {self.ns}/{dk}.png {det['tile']:g} 1\n\t\tmaterialMask {name}_mask.png\n")
            self.made[key] = det
        self.blocks[name] = f'{name}\n{{\n\tsurfaceparm nomarks\n\t{{\n{stage}\t\trgbGen lightingDiffuse\n\t}}\n}}\n'
        return name

    def decal(self, v, chunk):
        dif = v.get('DiffuseTexture')
        if not dif:
            return None
        nt = v.get('NormalTexture')
        key = f"decal_{hashlib.md5(f'{dif}|{nt}|{chunk}'.encode()).hexdigest()[:6]}"
        name = f'{self.ns}/{key}'
        if name in self.blocks:
            return name
        im = self.img(dif, 'RGBA')
        if im is None:
            return None
        a = np.asarray(im, np.float32) / 255
        tint = rgb(v.get('DiffuseColor'), (1, 1, 1))
        a[..., :3] *= tint
        a[..., 3] *= float(v.get('DiffuseAlpha', 1.0) or 0.0) if 'DiffuseAlpha' in v and float(v['DiffuseAlpha']) > 0 else 1.0
        self.save(key, np.uint8(np.round(np.clip(a, 0, 1) * 255)), '.png')
        stage = f'\t\tmap {name}.png\n'
        n = self.img(nt, 'RGB') if nt and not any(nt.endswith(x) for x in NEUTRAL_NORMALS) else None
        if n is not None:
            g = np.asarray(n, np.uint8).copy()
            g[..., 1] = 255 - g[..., 1]             # DirectX -> OpenGL
            g[..., 2] = 255
            self.save(key + '_n', g, '.png')
            stage += f'\t\tnormalMap {name}_n.png\n'
        self.save(key + '_orm', np.full((8, 8, 3), (255, 110, 0), np.uint8), '.png')
        stage += f'\t\tormMap {name}_orm.png\n\t\troughnessEncoding perceptual\n'
        self.blocks[name] = (f'{name}\n{{\n\tsurfaceparm nomarks\n\tsurfaceparm trans\n\tpolygonOffset\n\tsort decal\n'
                             f'\t{{\n{stage}\t\tblendFunc blend\n\t\trgbGen lightingDiffuse\n\t}}\n}}\n')
        return name

    def glass(self, v):
        tint = rgb(v.get('TintColor'), (0.9, 0.9, 0.9))
        key = f"glass_{''.join('%02x' % int(round(c * 255)) for c in tint)}"
        name = f'{self.ns}/{key}'
        if name in self.blocks:
            return name
        c = tint * 0.25
        self.save(key, np.full((8, 8, 4), (*np.uint8(np.round(c ** (1 / 2.2) * 255)), 110), np.uint8), '.png')
        self.save(key + '_orm', np.full((8, 8, 3), (255, 13, 0), np.uint8), '.png')
        self.blocks[name] = (f'{name}\n{{\n\tsurfaceparm nomarks\n\tsurfaceparm trans\n\tcull none\n\t{{\n'
                             f'\t\tmap {name}.png\n\t\tormMap {name}_orm.png\n\t\troughnessEncoding perceptual\n'
                             f'\t\tblendFunc blend\n\t\trgbGen lightingDiffuse\n\t}}\n}}\n')
        return name

    def light(self, v, kind):
        colour = self.lights.get(kind, np.ones(3))
        colour = colour / max(float(colour.max()), 1e-6)
        level = {'head': 2.0, 'brake': 0.6, 'utility': 1.5}.get(kind, 1.0)
        key = f'light_{kind}'
        name = f'{self.ns}/{key}'
        if name in self.blocks:
            return name
        em = self.img(v.get('Emissive'), 'L')
        if em is None:
            em = Image.new('L', (8, 8), 255)
        self.save(key + '_e', em, '.png')
        base = rgb(v.get('BaseColorScale'), (0.69, 0.69, 0.69))
        self.save(key, np.full((8, 8, 3), np.uint8(np.round(np.clip(base, 0, 1) ** (1 / 2.2) * 255)), np.uint8), '.png')
        self.save(key + '_orm', np.full((8, 8, 3), (255, 60, 0), np.uint8), '.png')
        ln = self.img(v.get('Normal'), 'RGB')
        nrm = ''
        if ln is not None:
            g = np.asarray(ln, np.uint8).copy()
            g[..., 1] = 255 - g[..., 1]
            g[..., 2] = 255
            self.save(key + '_n', g, '.png')
            nrm = f'\t\tnormalMap {name}_n.png\n'
        self.blocks[name] = (f'{name}\n{{\n\tsurfaceparm nomarks\n\t{{\n\t\tmap {name}.png\n{nrm}'
                             f'\t\tormMap {name}_orm.png\n\t\troughnessEncoding perceptual\n'
                             f'\t\temissiveMap {name}_e.png {colour[0]:.3f} {colour[1]:.3f} {colour[2]:.3f} {level:g}\n'
                             f'\t\tglow\n\t\trgbGen lightingDiffuse\n\t}}\n}}\n')
        return name

    # ---- a chunk

    def chunk(self, chunk_name, m, centre_y_cp):
        """Shader name for one chunk, or None to leave it out. centre_y_cp: the chunk's mean CP y (forward +)."""
        t, v, mi = self.resolve(m)
        cn = chunk_name.lower()
        if v.get('MultilayerSetup') and v.get('MultilayerMask'):
            return self.multilayer(v)
        if 'vehicle_mesh_decal' in t:
            return self.decal(v, cn)
        if 'vehicle_glass' in t or 'glass' in cn:
            return self.glass(v)
        if 'vehicle_lights' in t:
            if 'neon' in cn or 'rim' in cn:
                return self.light(v, 'utility')
            return self.light(v, 'head' if centre_y_cp > 0 else 'brake')
        if 'blinker' in cn:
            return self.flat('blinker', (0.9, 0.42, 0.05), 0.15, 0.0)
        if 'ui' in t or 'ui' in cn:
            return self.flat('screen', (0.02, 0.025, 0.03), 0.1, 0.0)
        if 'metal_base' in t:
            return self.flat(f'metal_{cn[:12]}', np.clip(rgb(v.get('BaseColorScale'), (0.5, 0.5, 0.5)), 0, 1),
                             0.35, float(v.get('MetalnessScale', 1.0) or 0) if 'MetalnessScale' in v else 1.0)
        return self.flat('dark', (0.02, 0.02, 0.022), 0.6, 0.0)

    def write(self, shader_dir: Path, name: str, header: str):
        """The .mtr (rd-vulkan reads it only in place of a .shader of the same name) and that .shader: the same stages
        without the material keywords, for other renderers."""
        txt = header + '\n'.join(self.blocks[k] for k in sorted(self.blocks))
        plain = '\n'.join(ln for ln in txt.split('\n') if ln.strip().split(' ')[0] not in PBR_KEYS)
        shader_dir.mkdir(parents=True, exist_ok=True)
        (shader_dir / f'{name}.shader').write_text(plain, encoding='ascii')
        (shader_dir / f'{name}.mtr').write_text(txt, encoding='ascii')
