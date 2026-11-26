"""w3grade.py -- a Witcher 3 environment's display transform as the render scene's colour grade (maps/<map>.xlut),
Beauclair.

Runs in headless Blender with the Witcher3-Blender-Tools add-on (its CR2W environment readers):

    blender -b --python tools/levelport/w3grade.py -- --out DIR --map w3_castle [--hour 14] [--size 33]
        [--world dlc/bob/data/levels/bob/bob.w2w] [--key 0.5]
        [--hours 0,0.5,...,23.5 [--exposure-hour 14]]

The game's chain, as the Blender tools' environment preview reproduces it (ui_environment.py, environment_balance_
preview.py): exposure (m_toneMapping.exposureScale over the shaped key: luminanceLimitShape/Min/Max, the key at 0.5),
the tone curve (newToneMapCurveParameters: shoulder, linear strength and angle, toe strength, numerator, denominator;
normalised at 11.2, times postScale), gamma 1/2.2, the balance map (m_finalColorBalance.balanceMap0, a 64^3 colour
cube laid out 8 x 8 tiles of 64 x 64, blue picking the tile) mixed in by balanceMapAmount, times
balancePostBrightness. Written as rd-vulkan's .xlut (vk_scene.cpp: "XLUT", version 1, size n, input mapping 1 = ARRI
LogC v3 EI 800; n^3 rgba16f texels of display light, red fastest): each texel's input is the scene light the LogC code
stands for, after the renderer's exposure (r_sceneExposure) and r_sceneGradeExposure (w3env.py --grade writes
r_sceneExposure --exposure, default 1, and r_sceneGradeExposure --grade-exposure, default 8).
The balance map's sampling follows the Witcher3-Blender-Tools add-on's _sample_balance_slice. With --balance-map1,
balanceMap1 is mixed over balanceMap0 by the balanceMapLerp curve, a reading of the curve's name that nothing has
checked against the game (Beauclair's sunny_bob_v7 holds it at 1 all day: balans_warming over sunny_bob_v0, a
yellower day); by default balanceMap0 alone, as the add-on's preview and the installed grade.

--hours bakes one LUT per hour (the day/night cycle's grade) into XLUT version 2: "XLUT", 2, n, input 1, count, then
count float hours ascending, then count LUTs of n^3 texels each; the renderer blends the two around the clock. The
exposure multiplier is the --exposure-hour's in every LUT: the cycle's brightness already comes from the env
keyframes (w3env.py --hours) and the renderer's exposure, so a LUT per hour holds only the curve and the balance.

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
    ap.add_argument('--hours', default='', help='comma-separated hours: one LUT each (XLUT version 2)')
    ap.add_argument('--exposure-hour', type=float, default=14.0,
                    help='with --hours: the hour whose exposure multiplier every LUT uses')
    ap.add_argument('--balance-map1', action='store_true',
                    help='mix balanceMap1 in by the balanceMapLerp curve (unchecked against the game)')
    ap.add_argument('--balance', type=float, default=1.0,
                    help="times the balance map's amount (0: the tone curve alone, Toussaint without its olive filter as "
                         "the Filterless Toussaint mod shows it)")
    ap.add_argument('--parametric', action='store_true',
                    help="apply m_finalColorBalance's levels and parametric balance (shadow/mid/highlight tint and "
                         "saturation) after the balance map: an approximation, the bands' split guessed (see parametric())")
    a = ap.parse_args(argv)

    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    env = importlib.import_module(PKG + '.CR2W.dc_environment')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import pk3write
    import w3patches
    w3patches.apply()
    world = env.load_world_environment(mat.materialize_repo_path(a.world.replace('/', BS), source_game='w3'))
    d = env.load_environment_definition(mat.materialize_repo_path(world.environment_definition, source_game='w3'))
    def cv(name, default, hour):
        t = hour * 3600.0 / 86400.0
        c = d.curves.get(name)
        if c is None or env.curve_is_placeholder(c):
            return default
        return float(c.evaluate(t)[3])

    def cv4(name, default, hour):
        c = d.curves.get(name)
        if c is None or env.curve_is_placeholder(c):
            return list(default)
        return [float(x) for x in c.evaluate(hour * 3600.0 / 86400.0)]

    def has_curve(name):
        c = d.curves.get(name)
        return c is not None and not env.curve_is_placeholder(c)

    def param(*path):
        v = d.params
        for p in path:
            if isinstance(v, dict):
                v = v.get(p, v.get('m_' + p))
            else:
                return None
        return v

    def exposure_mult(hour):
        scale = max(cv('m_toneMapping.exposureScale', 1.0, hour), 1e-6)
        shape = cv('m_toneMapping.luminanceLimitShape', 1.0, hour)
        lmin, lmax = cv('m_toneMapping.luminanceLimitMin', 1.0, hour), cv('m_toneMapping.luminanceLimitMax', 2.0, hour)
        rng = 11.2 * scale
        key = min(max(a.key, lmin), lmax)
        shaped = (max(key, 1e-4) / rng) ** shape * rng
        return scale / max(shaped, 1e-6)

    fcb = param('m_finalColorBalance') or param('finalColorBalance') or {}
    active = fcb.get('activatedBalanceMap', fcb.get('m_activatedBalanceMap', True)) if isinstance(fcb, dict) else True

    def load_cube(key):
        """a balance map's 512 x 512 cube (None when unset, inactive or not 512 x 512) and its path and size"""
        bmap = (fcb.get(key) or fcb.get('m_' + key)) if isinstance(fcb, dict) else None
        bpath = env.resource_path(bmap) if bmap is not None and not isinstance(bmap, str) else (bmap or '')
        if not (bpath and active):
            return None, bpath, None
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
        # 8 x 8 tiles of 64 x 64: tile (bx, by) holds blue 8*by + bx, its x red, its y green
        return (img if (h, w) == (512, 512) else None), bpath, [w, h]

    cube0, bpath, bsize = load_cube('balanceMap0')
    cube1, bpath1, bsize1 = load_cube('balanceMap1') if a.balance_map1 else (None, '', None)
    lerp_curve = has_curve('m_finalColorBalance.balanceMapLerp')
    n = a.size
    codes = (np.arange(n, dtype=np.float64)) / (n - 1)
    lin = logc_decode(codes)
    R, G, B = np.meshgrid(lin, lin, lin, indexing='ij')         # [r, g, b]
    hours = [float(h) for h in a.hours.split(',') if h.strip()] if a.hours else [a.hour]
    hours = sorted(set(h % 24.0 for h in hours))
    fixed = exposure_mult(a.exposure_hour) if a.hours else None
    luts, recs = [], []
    for hour in hours:
        mult = fixed if fixed is not None else exposure_mult(hour)
        out, rec = bake(hour, mult, R, G, B, lin, n, cv, cube0, cube1, lerp_curve, a.balance)
        if a.parametric:
            out, rec['parametric'] = parametric(out, hour, cv, cv4)
        luts.append(out)
        recs.append(rec)
    os.makedirs(os.path.join(a.out, 'maps'), exist_ok=True)
    path = os.path.join(a.out, 'maps', a.map + '.xlut')

    def texels(out):
        rgba = np.concatenate([out, np.ones(out.shape[:3] + (1,))], -1)
        return np.ascontiguousarray(np.transpose(rgba, (2, 1, 0, 3))).astype('<f2').tobytes()   # b slowest, r fastest
    with open(path, 'wb') as f:
        if a.hours:
            f.write(struct.pack('<4s4I', b'XLUT', 2, n, 1, len(hours)) + struct.pack('<%df' % len(hours), *hours))
            for out in luts:
                f.write(texels(out))
        else:
            f.write(struct.pack('<4s3I', b'XLUT', 1, n, 1) + texels(luts[0]))
    pk3 = os.path.join(a.out, 'zz_%s_grade.pk3' % a.map)
    with pk3write.writing(pk3) as z:
        z.write(path, 'maps/%s.xlut' % a.map)
    head = {'definition': world.environment_definition, 'balance_map': bpath, 'balance_map1': bpath1,
            'balance_map_used': cube0 is not None, 'balance_map1_used': cube1 is not None,
            'balance_map_lerp_curve': lerp_curve, 'balance_map_active': str(active),
            'balance_map_size': bsize, 'balance_map1_size': bsize1,
            'exposure_hour': a.exposure_hour if a.hours else None,
            'fcb_keys': sorted(fcb.keys()) if isinstance(fcb, dict) else str(type(fcb))}
    rec = dict(head, **recs[0]) if not a.hours else dict(head, hours=recs)
    with open(os.path.join(a.out, 'grade.json'), 'w') as f:
        json.dump(rec, f, indent=1, default=str)
    print('W3GRADE', pk3, json.dumps(rec, default=str)[:900])


def bake(hour, mult, R, G, B, lin, n, cv, cube0, cube1, lerp_curve, balance=1.0):
    """one hour's LUT [r, g, b, 3] of display light and its record"""
    curve = tuple(cv('m_toneMapping.newToneMapCurveParameters.' + nm, dflt, hour) for nm, dflt in (
        ('shoulderStrength', 0.22), ('linearStrength', 0.30), ('linearAngle', 0.10), ('toeStrength', 0.20),
        ('toeNumerator', 0.01), ('toeDenominator', 0.30)))
    post = cv('m_toneMapping.postScale', 1.0, hour)
    amount = cv('m_finalColorBalance.balanceMapAmount', 1.0, hour) * balance
    bright = cv('m_finalColorBalance.balancePostBrightness', 1.0, hour)
    lerp = min(max(cv('m_finalColorBalance.balanceMapLerp', 0.0, hour), 0.0), 1.0) if cube1 is not None else 0.0
    x = np.stack([R, G, B], -1) * mult
    tone = hable(x, curve) * (max(post, 0.0) / max(float(hable(np.array(11.2), curve)), 1e-6))
    ctrl = np.abs(tone) ** (1.0 / 2.2)
    out = tone
    if cube0 is not None:
        c = np.clip(ctrl, 0.0, 1.0)

        def sample(blue_slice, cube):
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

        def balance(cube):
            lower = sample(np.floor(bi) / 64.0, cube)
            upper = sample(cb + 1.0 / 64.0, cube)
            return lower + (upper - lower) * (bi - np.floor(bi))[..., None]
        s = balance(cube0)
        if lerp > 0.0:
            s = s + (balance(cube1) - s) * lerp
        sampled = np.abs(s) ** 2.2 * bright
        out = np.maximum(tone + (sampled - tone) * amount, 0.0)
    rec = {'hour': hour, 'exposure_multiplier': mult, 'exposure_ev': math.log2(mult),
           'curve': curve, 'postScale': post, 'balanceMapAmount': amount,
           'balancePostBrightness': bright, 'balanceMapLerp': lerp,
           'lerp_curve': lerp_curve,
           'mid_grey_out': float(np.interp(0.18, lin, np.array([out[i, i, i, 0] for i in range(n)]))),
           'mid_grey_rgb': [float(np.interp(0.18, lin, np.array([out[i, i, i, k] for i in range(n)]))) for k in range(3)]}
    return out, rec


def parametric(out, hour, cv, cv4):
    """m_finalColorBalance's levels and parametric balance over display light, in gamma 2.2 codes -- an approximation:
    nothing here is checked against the game's shader. Levels: black point levelsShadows, white point levelsHighlights,
    gamma levelsMidtones. Then each of three bands by the code's luminance (shadows under ~0.3, highlights over ~0.7,
    smooth between; the game's midtoneRange/Margin curves are not used) takes its parametricBalance colour (rgb / 255
    times its scale) and saturation (about the band's luminance)."""
    lo, hi = cv('m_finalColorBalance.levelsShadows', 0.0, hour), cv('m_finalColorBalance.levelsHighlights', 1.0, hour)
    mid = max(cv('m_finalColorBalance.levelsMidtones', 1.0, hour), 1e-3)
    c = np.abs(out) ** (1.0 / 2.2)
    c = np.clip((c - lo) / max(hi - lo, 1e-3), 0.0, None) ** (1.0 / mid)
    w = np.array([0.2126, 0.7152, 0.0722])
    lum = (c * w).sum(-1, keepdims=True)

    def smooth(e0, e1, x):
        t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
        return t * t * (3.0 - 2.0 * t)
    wl = 1.0 - smooth(0.15, 0.45, lum)
    wh = smooth(0.55, 0.85, lum)
    wm = np.clip(1.0 - wl - wh, 0.0, 1.0)
    rec = {}
    res = np.zeros_like(c)
    for band, wt in (('Low', wl), ('Mid', wm), ('High', wh)):
        col = cv4('m_finalColorBalance.parametricBalance%s.color' % band, (255.0, 255.0, 255.0, 1.0), hour)
        sat = cv('m_finalColorBalance.parametricBalance%s.saturation' % band, 1.0, hour)
        tint = np.array([max(x, 0.0) / 255.0 * col[3] for x in col[:3]])
        rec[band] = {'tint': tint.tolist(), 'saturation': sat}
        res += wt * (lum + (c - lum) * sat) * tint
    rec['levels'] = [lo, mid, hi]
    return np.maximum(res, 0.0) ** 2.2, rec


if __name__ == '__main__':
    main()
