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
"""Transfer baked lighting from a source Quake 3 / Quake Live BSP onto a recompiled Jedi Academy RBSP.

    python tools/levelport/lighttransfer.py <source.bsp> <target.bsp> [--out target.bsp]

Compiled Quake 3 maps keep their lightmaps but drop the light entities that made them, so q3map2 cannot relight a
port faithfully. This keeps q3map2's lightmap layout and replaces texel values by world position:
- every lightmapped texel of the target (planar, triangle soup, and patches tessellated) gets the source lighting
  at the same point and facing: the source surface's lightmap sampled bilinearly, or its vertex colour if vertex lit;
- the light grid (entity and player lighting) is resampled trilinearly from the source grid, skipping solid points.
Texels with no source surface within reach keep q3map2's value; the coverage is reported.

Quake 3 and Quake Live shift map lighting by r_mapOverBrightBits 2 (x4) at load; the JKA renderers default to no
shift. --scale (default 4) applies that shift once here, with Quake 3's colour normalisation (a channel over 255
scales the whole colour down, keeping its hue).
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import q3bsp  # noqa: E402
from bsp2gltf import tessellate_patch  # noqa: E402

LM = 128
L_LIGHTMAPS, L_LIGHTGRID, L_LIGHTARRAY = 14, 15, 17
RGRID = np.dtype([('ambient', 'u1', (4, 3)), ('direct', 'u1', (4, 3)), ('styles', 'u1', 4), ('latlong', 'u1', 2)])


def shift(rgb, scale):
    """Quake 3 R_ColorShiftLightingBytes in floats: scale, then normalise so no channel exceeds 255."""
    out = np.asarray(rgb, dtype=np.float64) * scale
    m = out.max(-1, keepdims=True)
    return np.where(m > 255, out * (255 / np.maximum(m, 1e-9)), out)


def lumps(data, n):
    return [list(struct.unpack_from('<ii', data, 8 + 8 * i)) for i in range(n)]


def triangles(bsp, raven):
    """All drawn triangles with lighting: pos (n,3,3), lm uv (n,3,2), page (n,), vertex colour (n,3,3), normal (n,3)."""
    P, U, G, C = [], [], [], []
    for s in bsp.surfaces:
        page = int(s['lm_num'][0] if raven else s['lm_num'])
        v = bsp.verts[s['first_vert']:s['first_vert'] + s['num_verts']]
        lm = v['lm'][:, 0] if raven else v['lm']
        col = (v['color'][:, 0, :3] if raven else v['color'][:, :3]).astype(np.float64)
        if s['type'] == q3bsp.MST_PATCH:
            w, h = int(s['patch_w']), int(s['patch_h'])
            ctrl = np.concatenate([v['xyz'], lm, col], 1).astype(np.float64)
            grid, tris = tessellate_patch(ctrl, w, h, steps=8)
            pos, uv, cc = grid[:, :3], grid[:, 3:5], grid[:, 5:8]
        elif s['type'] in (q3bsp.MST_PLANAR, q3bsp.MST_TRISOUP) and s['num_indexes']:
            tris = bsp.indexes[s['first_index']:s['first_index'] + s['num_indexes']].reshape(-1, 3)
            pos, uv, cc = v['xyz'].astype(np.float64), lm.astype(np.float64), col
        else:
            continue
        P.append(pos[tris])
        U.append(uv[tris])
        C.append(cc[tris])
        G.append(np.full(len(tris), page))
    P, U, C, G = np.concatenate(P), np.concatenate(U), np.concatenate(C), np.concatenate(G)
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    ln = np.linalg.norm(n, axis=1)
    ok = ln > 1e-6
    n = -n / np.maximum(ln, 1e-9)[:, None]          # Quake winding: front faces are clockwise
    return P[ok], U[ok], G[ok], C[ok], n[ok]


class SourceLight:
    def __init__(self, bsp, data, pos_scale=1.0):
        self.P, self.U, self.G, self.C, self.N = triangles(bsp, False)
        self.P = self.P * pos_scale                    # a target compiled at --scale s sits at s times the source
        ofs, ln = lumps(data, 17)[L_LIGHTMAPS]
        self.pages = np.frombuffer(data, 'u1', ln, ofs).reshape(-1, LM, LM, 3).astype(np.float64)
        self.d = np.einsum('ij,ij->i', self.N, self.P[:, 0])
        lo, hi = self.P.min(1), self.P.max(1)
        self.cell = 128.0
        self.hash = {}
        a, b = np.floor(lo / self.cell).astype(int), np.floor(hi / self.cell).astype(int)
        for i in range(len(self.P)):
            for x in range(a[i, 0], b[i, 0] + 1):
                for y in range(a[i, 1], b[i, 1] + 1):
                    for z in range(a[i, 2], b[i, 2] + 1):
                        self.hash.setdefault((x, y, z), []).append(i)

    def candidates(self, lo, hi):
        a, b = np.floor((lo - 8) / self.cell).astype(int), np.floor((hi + 8) / self.cell).astype(int)
        out = set()
        for x in range(a[0], b[0] + 1):
            for y in range(a[1], b[1] + 1):
                for z in range(a[2], b[2] + 1):
                    out.update(self.hash.get((x, y, z), ()))
        return np.fromiter(out, int) if out else np.zeros(0, int)

    def sample(self, pts, normal):
        """pts (k,3) on a target triangle with `normal` -> (k,3) light or NaN where nothing is close enough."""
        out = np.full((len(pts), 3), np.nan)
        if not len(pts):
            return out
        cand = self.candidates(pts.min(0), pts.max(0))
        if not len(cand):
            return out
        cand = cand[self.N[cand] @ normal > 0.5]
        if not len(cand):
            return out
        T = self.P[cand]
        dist = pts @ self.N[cand].T - self.d[cand]                     # k x c plane distances
        e0, e1 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
        d00, d01, d11 = (e0 * e0).sum(1), (e0 * e1).sum(1), (e1 * e1).sum(1)
        den = d00 * d11 - d01 * d01
        rel = pts[:, None, :] - T[None, :, 0, :]                        # k x c x 3
        d20, d21 = (rel * e0).sum(2), (rel * e1).sum(2)
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        u = 1 - v - w
        outside = np.maximum(0, -np.minimum(np.minimum(u, v), w))       # how far outside in barycentric units
        edge = np.sqrt(np.maximum(d00, d11))
        score = np.abs(dist) + outside * edge                           # world-unit distance proxy
        best = np.argmin(score, axis=1)
        k = np.arange(len(pts))
        good = score[k, best] < 6.0
        ci = cand[best]
        bu, bv, bw = (np.clip(x[k, best], 0, 1) for x in (u, v, w))
        s = bu + bv + bw
        bu, bv, bw = bu / s, bv / s, bw / s
        page = self.G[ci]
        uv = self.U[ci, 0] * bu[:, None] + self.U[ci, 1] * bv[:, None] + self.U[ci, 2] * bw[:, None]
        col = self.C[ci, 0] * bu[:, None] + self.C[ci, 1] * bv[:, None] + self.C[ci, 2] * bw[:, None]
        res = col.copy()
        lit = page >= 0
        if lit.any():
            res[lit] = self.bilinear(page[lit], uv[lit])
        out[good] = res[good]
        return out

    def bilinear(self, page, uv):
        x, y = uv[:, 0] * LM - 0.5, uv[:, 1] * LM - 0.5
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = (x - x0)[:, None], (y - y0)[:, None]
        def px(xx, yy):
            return self.pages[page, np.clip(yy, 0, LM - 1), np.clip(xx, 0, LM - 1)]
        return (px(x0, y0) * (1 - fx) * (1 - fy) + px(x0 + 1, y0) * fx * (1 - fy) +
                px(x0, y0 + 1) * (1 - fx) * fy + px(x0 + 1, y0 + 1) * fx * fy)


def transfer_lightmaps(src, tbsp, tdata, tl, scale):
    ofs, ln = tl[L_LIGHTMAPS]
    pages = np.frombuffer(tdata, 'u1', ln, ofs).reshape(-1, LM, LM, 3).astype(np.float64).copy()
    acc = np.zeros(pages.shape[:3] + (4,))                               # rgb sum + weight
    P, U, G, _, N = triangles(tbsp, True)
    total = hit = 0
    for i in range(len(P)):
        page = G[i]
        if page < 0:
            continue
        uv = U[i] * LM
        lo, hi = np.floor(uv.min(0) - 1).astype(int), np.ceil(uv.max(0) + 1).astype(int)
        xs, ys = np.meshgrid(np.arange(max(lo[0], 0), min(hi[0], LM)), np.arange(max(lo[1], 0), min(hi[1], LM)))
        c = np.stack([xs.ravel() + 0.5, ys.ravel() + 0.5], 1)
        if not len(c):
            continue
        a, b, d = uv
        m = np.array([b - a, d - a]).T
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        bc = np.linalg.solve(m, (c - a).T).T                             # (v, w)
        u = 1 - bc.sum(1)
        bary = np.stack([u, bc[:, 0], bc[:, 1]], 1)
        inside = np.all(bary >= -0.75 / max(np.abs(m).max(), 1), axis=1)  # about a texel of slack for seams
        if not inside.any():
            continue
        bary = np.clip(bary[inside], 0, None)
        bary /= bary.sum(1, keepdims=True)
        pts = bary @ P[i]
        light = src.sample(pts, N[i])
        ok = ~np.isnan(light[:, 0])
        px = c[inside].astype(int)
        total += len(px)
        hit += ok.sum()
        np.add.at(acc, (page, px[ok, 1], px[ok, 0]), np.concatenate([light[ok], np.ones((ok.sum(), 1))], 1))
    w = acc[..., 3:]
    has = w[..., 0] > 0
    pages[has] = shift(acc[has, :3] / w[has], scale)
    return np.clip(np.round(pages), 0, 255).astype('u1'), total, hit


def grid_dims(model, size=(64, 64, 128)):
    size = np.array(size, dtype=np.float64)
    mins = size * np.ceil(model['mins'] / size)
    maxs = size * np.floor(model['maxs'] / size)
    return mins, size, ((maxs - mins) / size).astype(int) + 1


def transfer_grid(sbsp, sdata, tbsp, tdata, tl, scale, pos_scale=1.0):
    s_org, s_size, s_dim = grid_dims(sbsp.models[0])
    s_org, s_size = s_org * pos_scale, s_size * pos_scale
    ofs, ln = lumps(sdata, 17)[L_LIGHTGRID]
    sg = np.frombuffer(sdata, 'u1', ln, ofs).reshape(-1, 8).astype(np.float64)
    if len(sg) != np.prod(s_dim):
        print('  source grid has %d points, expected %d: grid not transferred' % (len(sg), np.prod(s_dim)))
        return None
    sg = sg.reshape(s_dim[2], s_dim[1], s_dim[0], 8)
    t_org, t_size, t_dim = grid_dims(tbsp.models[0])
    n = int(np.prod(t_dim))
    z, y, x = np.meshgrid(np.arange(t_dim[2]), np.arange(t_dim[1]), np.arange(t_dim[0]), indexing='ij')
    pts = t_org + np.stack([x, y, z], -1).reshape(-1, 3) * t_size
    f = (pts - s_org) / s_size
    i0 = np.floor(f).astype(int)
    fr = f - i0
    amb = np.zeros((n, 3))
    dirl = np.zeros((n, 3))
    vec = np.zeros((n, 3))
    wsum = np.zeros(n)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                ix, iy, iz = i0[:, 0] + dx, i0[:, 1] + dy, i0[:, 2] + dz
                ok = (ix >= 0) & (iy >= 0) & (iz >= 0) & (ix < s_dim[0]) & (iy < s_dim[1]) & (iz < s_dim[2])
                g = np.zeros((n, 8))
                g[ok] = sg[iz[ok], iy[ok], ix[ok]]
                valid = ok & (g[:, :6].sum(1) > 0)                       # zero points are inside solid
                w = (fr[:, 0] if dx else 1 - fr[:, 0]) * (fr[:, 1] if dy else 1 - fr[:, 1]) * \
                    (fr[:, 2] if dz else 1 - fr[:, 2]) * valid
                lat, lng = g[:, 6] * (2 * np.pi / 255), g[:, 7] * (2 * np.pi / 255)
                v = np.stack([np.cos(lng) * np.sin(lat), np.sin(lng) * np.sin(lat), np.cos(lat)], 1)
                amb += g[:, 0:3] * w[:, None]
                dirl += g[:, 3:6] * w[:, None]
                vec += v * (w * g[:, 3:6].mean(1))[:, None]
                wsum += w
    has = wsum > 0
    amb[has] /= wsum[has, None]
    dirl[has] /= wsum[has, None]
    nv = vec / np.maximum(np.linalg.norm(vec, axis=1, keepdims=True), 1e-9)
    lat = np.arccos(np.clip(nv[:, 2], -1, 1))
    lng = np.arctan2(nv[:, 1], nv[:, 0]) % (2 * np.pi)
    out = np.zeros(n, RGRID)
    out['ambient'][:, 0] = np.clip(np.round(shift(amb, scale)), 0, 255)
    out['direct'][:, 0] = np.clip(np.round(shift(dirl, scale)), 0, 255)
    out['styles'] = 255
    out['styles'][:, 0] = np.where(has, 0, 255)
    out['latlong'][:, 0] = np.round(lat * 255 / (2 * np.pi)) % 256
    out['latlong'][:, 1] = np.round(lng * 255 / (2 * np.pi)) % 256
    # keep the target's own value where the source has nothing (outside its grid or solid)
    ofs, ln = tl[L_LIGHTARRAY]
    arr = np.frombuffer(tdata, '<u2', ln // 2, ofs)
    ofs, ln = tl[L_LIGHTGRID]
    tgrid = np.frombuffer(tdata, RGRID, ln // RGRID.itemsize, ofs)
    if len(arr) == n:
        out[~has] = tgrid[arr[~has]]
    uniq, inv = np.unique(out.view(np.dtype((np.void, RGRID.itemsize))), return_inverse=True)
    if len(uniq) > 65535:
        print('  grid has %d unique entries (> 65535): grid not transferred' % len(uniq))
        return None
    print('  light grid: %d points, %.0f%% from source, %d unique entries' % (n, 100 * has.mean(), len(uniq)))
    return uniq.tobytes(), inv.astype('<u2').tobytes()


def write_rbsp(data, replace, path):
    """Rewrite an RBSP with some lumps replaced: {lump index: bytes}."""
    n = 18
    L = lumps(data, n)
    blobs = [replace.get(i, data[o:o + ln]) for i, (o, ln) in enumerate(L)]
    out = bytearray(data[:8] + b'\0' * (8 * n))
    for i, b in enumerate(blobs):
        while len(out) % 4:
            out.append(0)
        struct.pack_into('<ii', out, 8 + 8 * i, len(out), len(b))
        out += b
    tmp = Path(str(path) + '.tmp')      # (renamed when whole: without --out the target itself is replaced)
    tmp.write_bytes(bytes(out))
    tmp.replace(path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('source')
    ap.add_argument('target')
    ap.add_argument('--out')
    ap.add_argument('--scale', type=float, default=4.0, help='source map overbright to bake in (Quake 3: 4)')
    ap.add_argument('--source-scale', type=float, default=1.0, help='target geometry size over the source (gltf2map --scale)')
    a = ap.parse_args(argv)
    sdata, tdata = Path(a.source).read_bytes(), Path(a.target).read_bytes()
    sbsp, tbsp = q3bsp.Bsp(a.source), q3bsp.Bsp(a.target)
    if not tbsp.raven or sbsp.raven:
        sys.exit('expects a Quake 3 / Quake Live source and a Jedi Academy RBSP target')
    tl = lumps(tdata, 18)
    src = SourceLight(sbsp, sdata, a.source_scale)
    pages, total, hit = transfer_lightmaps(src, tbsp, tdata, tl, a.scale)
    print('  lightmaps: %d pages, %d texels, %.1f%% transferred' % (len(pages), total, 100.0 * hit / max(total, 1)))
    replace = {L_LIGHTMAPS: pages.tobytes()}
    g = transfer_grid(sbsp, sdata, tbsp, tdata, tl, a.scale, a.source_scale)
    if g:
        replace[L_LIGHTGRID], replace[L_LIGHTARRAY] = g
    write_rbsp(tdata, replace, a.out or a.target)
    return hit / max(total, 1)


if __name__ == '__main__':
    main()
