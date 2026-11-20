"""Offline look check of a staged Ghoul2 model with its rd-vulkan .mtr (ct3x_model.py output), on the CPU.

Not the game's renderer: a per-pixel z-buffer with the stage's albedo, tangent-space normal map (+ detailNormalMap
weighted by materialMask's alpha), ORM (perceptual roughness), GGX key and fill lights, a sky-gradient reflection
blurred by roughness, emissive with a bloom, and blended stages (decals, glass) over the opaque pass. It shows
whether the maps are wired and plausible before anything is installed; the in-game look is the real check.

  python -I ct3x_preview.py STAGE [--lod 1] [--out DIR] [--size 1280x720]

Environment: CP_TOOLS (required: the folder with the CP2077 tools' glm_io, not in this repository).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

if not os.environ.get('CP_TOOLS'):
    raise SystemExit('set CP_TOOLS to the folder with the CP2077 vehicle tools (glm_io)')
sys.path[:0] = [os.environ['CP_TOOLS'],
                str(Path(__file__).resolve().parents[1] / 'cloth' / 'pipeline' / 'skeleton')]
import glm_io  # noqa: E402
import jkagla  # noqa: E402


def parse_mtr(text):
    """{shader: {map, normalMap, ormMap, emissive (img, rgb, level), detailN (img, tile, k), detail (img, tile),
    mask, blend}} from a .mtr (first stage only)."""
    out = {}
    for m in re.finditer(r'^(\S+)\s*\n\{(.*?)\n\}', text, re.S | re.M):
        name, body = m.group(1), m.group(2)
        d = {'blend': 'blendFunc blend' in body}
        for line in body.split('\n'):
            t = line.split()
            if not t:
                continue
            k = t[0]
            if k in ('map', 'normalMap', 'ormMap', 'materialMask'):
                d[k] = t[1]
            elif k == 'emissiveMap':
                d['emissive'] = (t[1], np.array(list(map(float, t[2:5]))), float(t[5]) if len(t) > 5 else 1.0)
            elif k == 'detailNormalMap':
                d['detailN'] = (t[1], float(t[2]), float(t[3]))
            elif k == 'detailMap':
                d['detail'] = (t[1], float(t[2]))
        out[name] = d
    return out


class Tex:
    def __init__(self, root):
        self.root, self.cache = Path(root), {}

    def get(self, rel, mode):
        key = (rel, mode)
        if key not in self.cache:
            p = self.root / rel
            self.cache[key] = (np.asarray(Image.open(p).convert(mode), np.float32) / 255.0 if p.exists() else None)
        return self.cache[key]

    def sample(self, rel, mode, uv):
        a = self.get(rel, mode) if rel else None
        if a is None:
            return None
        h, w = a.shape[:2]
        x = np.mod(uv[:, 0], 1.0) * (w - 1)
        y = np.mod(uv[:, 1], 1.0) * (h - 1)
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
        fx, fy = (x - x0)[:, None], (y - y0)[:, None]
        if a.ndim == 2:
            a = a[..., None]
        return ((a[y0, x0] * (1 - fx) + a[y0, x1] * fx) * (1 - fy) + (a[y1, x0] * (1 - fx) + a[y1, x1] * fx) * fy)


def skin(gla):
    pool = jkagla.decode_pool(gla['pool'])
    F = []
    for i, b in enumerate(gla['bones']):
        A = np.vstack([pool[gla['idx'][0, i]], [0, 0, 0, 1]])
        F.append(A if b['parent'] < 0 else F[b['parent']] @ A)
    return F


def gather(model, F, lod, mats):
    """Per surface with a shader: (shader, P (n,3), N, T tangent, UV, tris (CW file order))."""
    out = []
    for s, sf in zip(model['surfaces'], model['lods'][lod]):
        if s['flags'] & 1 or not s['shader'] or s['shader'] not in mats:
            continue
        v = sf['verts']
        P = np.array([sum(w * (F[b] @ np.r_[x[0], 1])[:3] for b, w in x[3]) for x in v])
        N = np.array([sum(w * (F[b][:3, :3] @ np.array(x[1])) for b, w in x[3]) for x in v])
        UV = np.array([x[2] for x in v], np.float64)
        T = np.array(sf['tris'], np.int64)
        if len(T) == 1 and len(P) == 3 and np.allclose(P[0], P[1]):
            continue
        # tangents from UV derivatives (file winding is CW: swap back to CCW for the math)
        tc = T[:, [0, 2, 1]]
        e1, e2 = P[tc[:, 1]] - P[tc[:, 0]], P[tc[:, 2]] - P[tc[:, 0]]
        d1, d2 = UV[tc[:, 1]] - UV[tc[:, 0]], UV[tc[:, 2]] - UV[tc[:, 0]]
        r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        r = np.where(np.abs(r) < 1e-12, 1e-12, r)
        t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]
        Tg = np.zeros_like(P)
        for k in range(3):
            np.add.at(Tg, tc[:, k], t)
        out.append((s['shader'], P, N, Tg, UV, T))
    return out


def raster(tris_px, depth, ids, bary, tri_ids, test_depth=None):
    """Z-buffer triangles (screen x, y, z) into depth / ids / bary. test_depth: also behind this buffer's depth."""
    H, W = depth.shape
    for k in range(len(tris_px)):
        a, b, c = tris_px[k]
        xmin, xmax = int(max(0, np.floor(min(a[0], b[0], c[0])))), int(min(W - 1, np.ceil(max(a[0], b[0], c[0]))))
        ymin, ymax = int(max(0, np.floor(min(a[1], b[1], c[1])))), int(min(H - 1, np.ceil(max(a[1], b[1], c[1]))))
        if xmin > xmax or ymin > ymax:
            continue
        ys, xs = np.mgrid[ymin:ymax + 1, xmin:xmax + 1]
        px, py = xs + 0.5, ys + 0.5
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-9:
            continue
        w0 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / den
        w1 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            continue
        z = w0 * a[2] + w1 * b[2] + w2 * c[2]
        cur = depth[ymin:ymax + 1, xmin:xmax + 1]
        ok = inside & (z < cur)
        if test_depth is not None:
            ok &= z <= test_depth[ymin:ymax + 1, xmin:xmax + 1] + 0.02
        if not ok.any():
            continue
        cur[ok] = z[ok]
        ids[ymin:ymax + 1, xmin:xmax + 1][ok] = tri_ids[k]
        bb = bary[ymin:ymax + 1, xmin:xmax + 1]
        bb[ok] = np.stack([w0[ok], w1[ok], w2[ok]], -1)


def sky(d):
    """Night-city sky gradient (a stand-in environment): dark zenith, warm horizon glow, dark ground."""
    z = d[:, 2:3]
    top = np.array([0.05, 0.07, 0.12])
    hor = np.array([0.55, 0.42, 0.38])
    gnd = np.array([0.06, 0.05, 0.05])
    up = np.clip(z, 0, 1)
    return np.where(z >= 0, hor * (1 - up) ** 3 + top * (1 - (1 - up) ** 3), gnd + (hor - gnd) * np.exp(z * 8))


def render(yaw, elev, size, dist, tex, mats, geo):
    W, H = size
    a, e = np.radians(yaw), np.radians(elev)
    # camera looks at the bike's centre from (yaw, elev); GLM frame: forward -Y, up +Z
    target = np.array([0.0, -6.0, 18.0])
    eye = target + dist * np.array([np.cos(e) * np.sin(a), -np.cos(e) * np.cos(a), np.sin(e)])
    fwd = (target - eye) / np.linalg.norm(target - eye)
    right = np.cross(fwd, [0, 0, 1.0]); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    f = 0.5 * H / np.tan(np.radians(15))

    def proj(P):
        q = P - eye
        x, y, z = q @ right, q @ up, q @ fwd
        return np.stack([W / 2 + f * x / z, H / 2 - f * y / z, z], -1)

    allP, allN, allT, allUV, owner = [], [], [], [], []
    opaque, blended = [], []
    base = 0
    for si, (sh, P, N, Tg, UV, T) in enumerate(geo):
        allP.append(P); allN.append(N); allT.append(Tg); allUV.append(UV)
        tri = T[:, [0, 2, 1]] + base
        (blended if mats[sh]['blend'] else opaque).append((si, tri))
        base += len(P)
    P, N, Tg, UV = map(np.concatenate, (allP, allN, allT, allUV))
    S = proj(P)
    tri_all, tri_sh = [], []
    for si, tri in opaque + blended:
        tri_all.append(tri); tri_sh.append(np.full(len(tri), si))
    tri_all, tri_sh = np.concatenate(tri_all), np.concatenate(tri_sh)
    n_op = sum(len(t) for _, t in opaque)

    def buffers():
        return np.full((H, W), np.inf), np.full((H, W), -1, np.int64), np.zeros((H, W, 3))
    depth, ids, bary = buffers()
    raster(S[tri_all[:n_op]], depth, ids, bary, np.arange(n_op))
    bdepth, bids, bbary = buffers()
    raster(S[tri_all[n_op:]], bdepth, bids, bbary, np.arange(n_op, len(tri_all)), test_depth=depth)

    keyL = np.array([-0.45, -0.55, 0.70]); keyL /= np.linalg.norm(keyL)
    fillL = np.array([0.7, 0.4, 0.25]); fillL /= np.linalg.norm(fillL)

    def shade(ids, bary, mask_px):
        out = np.zeros((H, W, 3)); alpha = np.zeros((H, W)); emis = np.zeros((H, W, 3))
        py, px = np.nonzero(mask_px)
        if not len(py):
            return out, alpha, emis
        t = ids[py, px]
        bw = bary[py, px]
        vi = tri_all[t]
        uv = (UV[vi] * bw[..., None]).sum(1)
        pos = (P[vi] * bw[..., None]).sum(1)
        nrm = (N[vi] * bw[..., None]).sum(1); nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9
        tg = (Tg[vi] * bw[..., None]).sum(1)
        tg = tg - nrm * (tg * nrm).sum(1, keepdims=True); tg /= np.linalg.norm(tg, axis=1, keepdims=True) + 1e-9
        bt = np.cross(nrm, tg)
        V = eye - pos; V /= np.linalg.norm(V, axis=1, keepdims=True)
        flip = (nrm * V).sum(1) < 0                 # back faces of two-sided glass and thin parts
        nrm[flip] *= -1
        col = np.zeros((len(py), 3)); al = np.ones(len(py)); em = np.zeros((len(py), 3))
        for si in np.unique(tri_sh[t]):
            sel = tri_sh[t] == si
            m = mats[geo[si][0]]
            u = uv[sel]
            alb = tex.sample(m.get('map'), 'RGBA', u)
            alb = np.ones((len(u), 4)) if alb is None else alb
            a_ = alb[:, 3]
            alb = alb[:, :3] ** 2.2
            nt = tex.sample(m.get('normalMap'), 'RGB', u)
            sl = np.zeros((len(u), 2)) if nt is None else nt[:, :2] * 2 - 1
            w_det = tex.sample(m.get('materialMask'), 'RGBA', u)
            w_det = np.ones(len(u)) if w_det is None else w_det[:, 3]
            if 'detailN' in m:
                dn = tex.sample(m['detailN'][0], 'RGB', u * m['detailN'][1])
                if dn is not None:
                    sl = sl + (dn[:, :2] * 2 - 1) * m['detailN'][2] * w_det[:, None]
            orm = tex.sample(m.get('ormMap'), 'RGB', u)
            rough = np.full(len(u), 0.6) if orm is None else orm[:, 1]
            metal = np.zeros(len(u)) if orm is None else orm[:, 2]
            if 'detail' in m:
                dd = tex.sample(m['detail'][0], 'RGBA', u * m['detail'][1])
                if dd is not None:
                    alb = alb * (1 + (dd[:, :3] * 2 - 1) * w_det[:, None])
                    rough = np.clip(rough * (1 + (dd[:, 3] * 2 - 1) * w_det), 0.02, 1)
            n_ = nrm[sel]
            nn = n_ + tg[sel] * sl[:, :1] + bt[sel] * sl[:, 1:2]
            nn /= np.linalg.norm(nn, axis=1, keepdims=True) + 1e-9
            v_ = V[sel]
            f0 = 0.04 * (1 - metal[:, None]) + alb * metal[:, None]
            diff = alb * (1 - metal[:, None])
            c = np.zeros((len(u), 3))
            for L, I in ((keyL, np.array([2.6, 2.45, 2.3])), (fillL, np.array([0.45, 0.55, 0.9]))):
                ndl = np.clip(nn @ L, 0, 1)[:, None]
                h = v_ + L; h /= np.linalg.norm(h, axis=1, keepdims=True)
                ndh = np.clip((nn * h).sum(1), 0, 1)
                a2 = np.maximum(rough ** 4, 1e-4)
                D = a2 / (np.pi * ((ndh ** 2) * (a2 - 1) + 1) ** 2)
                ndv = np.clip((nn * v_).sum(1), 1e-3, 1)
                k = (rough + 1) ** 2 / 8
                G = ndv / (ndv * (1 - k) + k) * (ndl[:, 0] / (ndl[:, 0] * (1 - k) + k + 1e-6))
                Fs = f0 + (1 - f0) * ((1 - np.clip((v_ * h).sum(1), 0, 1)) ** 5)[:, None]
                spec = Fs * (D * G / (4 * ndv * np.maximum(ndl[:, 0], 1e-3) + 1e-6))[:, None]
                c += (diff / np.pi * (1 - Fs) + spec) * ndl * I
            # ambient: sky irradiance by the normal, reflection by the mirror direction blurred toward the mean
            ndv = np.clip((nn * v_).sum(1), 0, 1)[:, None]
            R = 2 * ndv * nn - v_
            env = sky(R) * (1 - rough[:, None]) + np.array([0.22, 0.2, 0.2]) * rough[:, None]
            Fa = f0 + (np.maximum(1 - rough[:, None], f0) - f0) * (1 - ndv) ** 5
            c += diff * sky(nn) * 0.9 + env * Fa
            if 'emissive' in m:
                et = tex.sample(m['emissive'][0], 'L', u)
                e_ = (et[:, 0] if et is not None else np.ones(len(u)))[:, None] * m['emissive'][1] * m['emissive'][2]
                em[sel] = e_
                c += e_
            col[sel] = c
            al[sel] = a_ if m['blend'] else 1.0
        out[py, px] = col; alpha[py, px] = al; emis[py, px] = em
        return out, alpha, emis

    bg = np.zeros((H, W, 3)) + np.linspace(0.07, 0.02, H)[:, None, None] * np.array([1.0, 1.05, 1.3])
    op, _, e1 = shade(ids, bary, ids >= 0)
    img = np.where((ids >= 0)[..., None], op, bg)
    bl, ba, e2 = shade(bids, bbary, bids >= 0)
    img = img * (1 - ba[..., None]) + bl * ba[..., None]
    glow = np.clip(e1 + e2 * ba[..., None], 0, 4)
    g = np.asarray(Image.fromarray(np.uint8(np.clip(glow / 4, 0, 1) * 255)).filter(ImageFilter.GaussianBlur(6)),
                   np.float32) / 255 * 4
    img = img + g * 0.6
    img = img / (1 + img)                             # Reinhard
    return Image.fromarray(np.uint8(np.clip(img ** (1 / 2.2), 0, 1) * 255))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stage', type=Path)
    ap.add_argument('--lod', type=int, default=1)
    ap.add_argument('--out', type=Path, default=None)
    ap.add_argument('--size', default='1280x720')
    ap.add_argument('--name', default='ct3x')
    a = ap.parse_args()
    W, H = map(int, a.size.split('x'))
    ns = f'models/players/{a.name}'
    model = glm_io.read(open(a.stage / ns / 'model.glm', 'rb').read())
    gla = jkagla.read(str(a.stage / ns / f'{a.name}.gla'))
    mats = parse_mtr((a.stage / 'shaders' / f'{a.name}.mtr').read_text(encoding='ascii'))
    F = skin(gla)
    geo = gather(model, F, a.lod, mats)
    tex = Tex(a.stage)
    out = a.out or a.stage.parent
    for tag, yaw, elev, dist in (('side', 90, 4, 125), ('front34', 35, 12, 115), ('rear34', 215, 18, 115), ('close', 60, 25, 60)):
        im = render(yaw, elev, (W, H), dist, tex, mats, geo)
        p = out / f'look_{tag}_lod{a.lod}.png'
        im.save(p)
        print(p)


if __name__ == '__main__':
    main()
