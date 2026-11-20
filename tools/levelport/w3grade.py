"""w3grade.py -- a Witcher 3 environment's display transform as the render scene's colour grade (maps/<map>.xlut),
Beauclair.

Runs in headless Blender with the Witcher3-Blender-Tools add-on (its CR2W environment readers):

    blender -b --python tools/levelport/w3grade.py -- --out DIR --map w3_castle [--hour 14] [--size 33]
        [--world dlc/bob/data/levels/bob/bob.w2w] [--key 0.5]

The game's chain, as the Blender tools' environment preview reproduces it (ui_environment.py, environment_balance_
preview.py): exposure (m_toneMapping.exposureScale over the shaped key: luminanceLimitShape/Min/Max, the key at 0.5),
the tone curve (newToneMapCurveParameters: shoulder, linear strength and angle, toe strength, numerator, denominator;
normalised at 11.2, times postScale), gamma 1/2.2, the balance map (m_finalColorBalance.balanceMap0, a 64^3 colour
cube laid out 8 x 8 tiles of 64 x 64, blue picking the tile) mixed in by balanceMapAmount, times
balancePostBrightness. Written as rd-vulkan's .xlut (vk_scene.cpp: "XLUT", version 1, size n, input mapping 1 = ARRI
LogC v3 EI 800; n^3 rgba16f texels of display light, red fastest): each texel's input is the scene light the LogC code
stands for, after the renderer's exposure (r_sceneExposure) and r_sceneGradeExposure (w3env.py --grade writes
r_sceneExposure --exposure, default 1, and r_sceneGradeExposure --grade-exposure, default 8).
The balance map's sampling follows the Witcher3-Blender-Tools add-on's _sample_balance_slice.

    DIR/maps/<map>.xlut, DIR/zz_<map>_grade.pk3, DIR/grade.json
"""
import argparse
import importlib
import json
import math
import os
import struct
import sys

import numpy as np

PKG = 'bl_ext.user_default.witcher3_tools'
BS = chr(92)


def logc_decode(t):
    """ARRI LogC v3 at EI 800, code -> scene linear (scene_grade.frag's logC inverse)."""
    cut, a, b, c, d, e, f = 0.010591, 5.555556, 0.052272, 0.247190, 0.385537, 5.367655, 0.092809
    lin_code = e * cut + f
    return np.where(t > lin_code, (10.0 ** ((t - d) / c) - b) / a, (t - f) / e)


def hable(x, p):
    sh, li, an, to, nu, de = p
    de = max(de, 1e-6)
    return np.maximum(0.0, (x * (sh * x + an * li) + to * nu) / (x * (sh * x + li) + to * de) - nu / de)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--map', default='w3_castle')
    ap.add_argument('--hour', type=float, default=14.0)
    ap.add_argument('--world', default='dlc/bob/data/levels/bob/bob.w2w')
    ap.add_argument('--size', type=int, default=33)
    ap.add_argument('--key', type=float, default=0.5, help="the tone key (the Blender tools' fixed 0.5)")
    a = ap.parse_args(argv)

    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    env = importlib.import_module(PKG + '.CR2W.dc_environment')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import pk3write
    import w3patches
    w3patches.apply()
    world = env.load_world_environment(mat.materialize_repo_path(a.world.replace('/', BS), source_game='w3'))
    d = env.load_environment_definition(mat.materialize_repo_path(world.environment_definition, source_game='w3'))
    t = a.hour * 3600.0 / 86400.0

    def cv(name, default):
        c = d.curves.get(name)
        if c is None or env.curve_is_placeholder(c):
            return default
        return float(c.evaluate(t)[3])

    def param(*path):
        v = d.params
        for p in path:
            if isinstance(v, dict):
                v = v.get(p, v.get('m_' + p))
            else:
                return None
        return v

    scale = max(cv('m_toneMapping.exposureScale', 1.0), 1e-6)
    shape = cv('m_toneMapping.luminanceLimitShape', 1.0)
    lmin, lmax = cv('m_toneMapping.luminanceLimitMin', 1.0), cv('m_toneMapping.luminanceLimitMax', 2.0)
    rng = 11.2 * scale
    key = min(max(a.key, lmin), lmax)
    shaped = (max(key, 1e-4) / rng) ** shape * rng
    mult = scale / max(shaped, 1e-6)
    curve = tuple(cv('m_toneMapping.newToneMapCurveParameters.' + n, dflt) for n, dflt in (
        ('shoulderStrength', 0.22), ('linearStrength', 0.30), ('linearAngle', 0.10), ('toeStrength', 0.20),
        ('toeNumerator', 0.01), ('toeDenominator', 0.30)))
    post = cv('m_toneMapping.postScale', 1.0)
    amount = cv('m_finalColorBalance.balanceMapAmount', 1.0)
    bright = cv('m_finalColorBalance.balancePostBrightness', 1.0)
    fcb = param('m_finalColorBalance') or param('finalColorBalance') or {}
    bmap = None
    if isinstance(fcb, dict):
        bmap = fcb.get('balanceMap0') or fcb.get('m_balanceMap0')
        active = fcb.get('activatedBalanceMap', fcb.get('m_activatedBalanceMap', True))
    else:
        active = True
    bpath = env.resource_path(bmap) if bmap is not None and not isinstance(bmap, str) else (bmap or '')
    cube = None
    if bpath and active:
        import bpy      # (Blender's Python has no Pillow; Blender reads the DDS)
        p = mat.materialize_repo_path(bpath, source_game='w3')
        dds = p
        if p.lower().endswith('.xbm'):
            # the add-on's own .xbm -> .dds (materials/material.py), written beside it
            matmod = importlib.import_module(PKG + '.materials.material')
            dds = matmod._convert_xbm_to_writable_dds(p) or p
        im = bpy.data.images.load(dds)
        im.colorspace_settings.name = 'Non-Color'      # the stored codes, not linearised
        w, h = im.size
        px = np.empty(w * h * 4, np.float32)
        im.pixels.foreach_get(px)
        img = px.reshape(h, w, 4)[::-1, :, :3]         # Blender's rows run bottom-up: first row on top, as the DDS
        if (h, w) == (512, 512):
            cube = img      # 8 x 8 tiles of 64 x 64: tile (bx, by) holds blue 8*by + bx, its x red, its y green
    n = a.size
    codes = (np.arange(n, dtype=np.float64)) / (n - 1)
    lin = logc_decode(codes)
    R, G, B = np.meshgrid(lin, lin, lin, indexing='ij')         # [r, g, b]
    x = np.stack([R, G, B], -1) * mult
    tone = hable(x, curve) * (max(post, 0.0) / max(float(hable(np.array(11.2), curve)), 1e-6))
    ctrl = np.abs(tone) ** (1.0 / 2.2)
    out = tone
    if cube is not None:
        c = np.clip(ctrl, 0.0, 1.0)

        def sample(blue_slice):
            # the Blender tools' _sample_balance_slice (DDS rows top first)
            def axis(v):
                return np.clip((v + 0.5 / 64.0) * 255.0 / 256.0, 1.0 / 64.0, 63.0 / 64.0) / 8.0
            bs = np.clip(blue_slice, 0.0, 0.99999)
            row = np.floor(bs * 8.0)
            col = np.floor(bs * 64.0 - row * 8.0)
            u = axis(c[..., 0]) + col / 8.0
            v = axis(c[..., 1]) + row / 8.0
            # bilinear in the 512 x 512 image
            px, py = u * 512 - 0.5, v * 512 - 0.5
            x0, y0 = np.clip(np.floor(px).astype(int), 0, 511), np.clip(np.floor(py).astype(int), 0, 511)
            x1, y1 = np.clip(x0 + 1, 0, 511), np.clip(y0 + 1, 0, 511)
            fx, fy = (px - np.floor(px))[..., None], (py - np.floor(py))[..., None]
            return (cube[y0, x0] * (1 - fx) * (1 - fy) + cube[y0, x1] * fx * (1 - fy) +
                    cube[y1, x0] * (1 - fx) * fy + cube[y1, x1] * fx * fy)
        cb = c[..., 2] * 255.0 / 256.0
        bi = cb * 64.0
        lower = sample(np.floor(bi) / 64.0)
        upper = sample(cb + 1.0 / 64.0)
        s = lower + (upper - lower) * (bi - np.floor(bi))[..., None]
        sampled = np.abs(s) ** 2.2 * bright
        out = np.maximum(tone + (sampled - tone) * amount, 0.0)
    rgba = np.concatenate([out, np.ones(out.shape[:3] + (1,))], -1)
    texels = np.ascontiguousarray(np.transpose(rgba, (2, 1, 0, 3))).astype('<f2')   # b slowest, r fastest
    os.makedirs(os.path.join(a.out, 'maps'), exist_ok=True)
    path = os.path.join(a.out, 'maps', a.map + '.xlut')
    with open(path, 'wb') as f:
        f.write(struct.pack('<4s3I', b'XLUT', 1, n, 1) + texels.tobytes())
    pk3 = os.path.join(a.out, 'zz_%s_grade.pk3' % a.map)
    with pk3write.writing(pk3) as z:
        z.write(path, 'maps/%s.xlut' % a.map)
    rec = {'definition': world.environment_definition, 'hour': a.hour, 'exposure_multiplier': mult,
           'exposure_ev': math.log2(mult), 'curve': curve, 'postScale': post, 'balance_map': bpath,
           'balance_map_used': cube is not None, 'balance_map_active': str(active),
           'balance_map_size': list(im.size) if bpath and active else None, 'balanceMapAmount': amount, 'balancePostBrightness': bright,
           'fcb_keys': sorted(fcb.keys()) if isinstance(fcb, dict) else str(type(fcb)),
           'mid_grey_out': float(np.interp(0.18, lin, np.array([out[i, i, i, 0] for i in range(n)])))}
    with open(os.path.join(a.out, 'grade.json'), 'w') as f:
        json.dump(rec, f, indent=1, default=str)
    print('W3GRADE', pk3, json.dumps(rec, default=str)[:900])


if __name__ == '__main__':
    main()
