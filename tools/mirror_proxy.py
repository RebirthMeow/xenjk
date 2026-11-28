"""Bake a model's water-mirror proxy: what its underside looks like from below, for the render scene's water.

    python -I tools/mirror_proxy.py PACK.pk3 models/dir/part0.md3 [more.md3 ...] --out OUT_DIR [--cell 1.0]

The water's reflection is screen space: a model high overhead (the Star Destroyer over Beauclair) is off screen whenever
its reflection is on it. The proxy stands in for it: the model's MD3 parts are rasterised from below onto a grid in model
units (the MD3's own coordinates, before the entity's scale), keeping per cell the lowest surface and its texture's
colour. Written beside the first part, both in model units:

    <first part>.mirror    text: `image <path>` and `box x0 y0 x1 y1 z0 z1` (texel (i, j): x from x0, y from y0)
    <dir>/mirror.png       (power-of-two sides) rgb the albedo as the textures store it; a the height from z0 (0..250 over z0..z1), 255 none

vk_scene.cpp loads <model>.mirror with the model (R_SceneMirrorRegister) and scene.frag's water marches its reflected ray
through the height (mirrorProxy), lighting the albedo as the model is lit (ambient + directed x N.L).
"""
import argparse
import io
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image


def read_md3(data):
    """The MD3's first frame: per surface its shader name, positions (model units), texture coordinates, triangles."""
    ident, version = struct.unpack_from('<4si', data, 0)
    if ident != b'IDP3':
        raise ValueError('not an MD3')
    n_surf, = struct.unpack_from('<i', data, 4 + 4 + 64 + 4 + 4 + 4)
    ofs_surf, = struct.unpack_from('<i', data, 4 + 4 + 64 + 4 * 4 + 4 * 2)
    out = []
    at = ofs_surf
    for _ in range(n_surf):
        (sid, name, flags, nframes, nshaders, nverts, ntris, ofs_tri, ofs_shd, ofs_st, ofs_xyz,
         ofs_end) = struct.unpack_from('<4s64siiiiiiiiii', data, at)
        shader = struct.unpack_from('<64s', data, at + ofs_shd)[0].split(b'\0')[0].decode('latin-1')
        tris = np.frombuffer(data, '<i4', ntris * 3, at + ofs_tri).reshape(-1, 3)
        st = np.frombuffer(data, '<f4', nverts * 2, at + ofs_st).reshape(-1, 2)
        xyz = np.frombuffer(data, '<i2', nverts * 4, at + ofs_xyz).reshape(-1, 4)[:, :3] / 64.0
        out.append((shader, xyz.astype(np.float64), st.astype(np.float64), tris))
        at += ofs_end
    return out


def texture(z, shader, cache):
    if shader not in cache:
        img = None
        for ext in ('.png', '.tga', '.jpg'):
            try:
                img = Image.open(io.BytesIO(z.read(shader + ext))).convert('RGB')
                break
            except KeyError:
                continue
        cache[shader] = np.asarray(img, np.float32) if img is not None else None
    return cache[shader]


def bake(pk3, parts, cell):
    z = zipfile.ZipFile(pk3)
    surfs = [s for p in parts for s in read_md3(z.read(p))]
    allp = np.concatenate([s[1] for s in surfs])
    lo, hi = allp.min(0), allp.max(0)
    x0, y0 = np.floor(lo[:2] / cell) * cell - 2 * cell
    x1, y1 = np.ceil(hi[:2] / cell) * cell + 2 * cell
    # power-of-two sides (the engine refuses other images): the cells stretched to fill them, at most --cell
    W = 1 << int(np.ceil(np.log2((x1 - x0) / cell)))
    H = 1 << int(np.ceil(np.log2((y1 - y0) / cell)))
    sx, sy = (x1 - x0) / W, (y1 - y0) / H
    zbuf = np.full((H, W), np.inf)
    col = np.zeros((H, W, 3), np.float32)
    cache = {}
    cx = x0 + (np.arange(W) + 0.5) * sx
    cy = y0 + (np.arange(H) + 0.5) * sy
    for shader, P, ST, T in surfs:
        tex = texture(z, shader, cache)
        A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
        for k in range(len(T)):
            a, b, c = A[k], B[k], C[k]
            d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
            if abs(d) < 1e-9:	# edge-on from below (a wall): its lowest edge is drawn by its neighbours
                continue
            i0 = max(int(np.floor((min(a[0], b[0], c[0]) - x0) / sx)), 0)
            i1 = min(int(np.ceil((max(a[0], b[0], c[0]) - x0) / sx)), W - 1)
            j0 = max(int(np.floor((min(a[1], b[1], c[1]) - y0) / sy)), 0)
            j1 = min(int(np.ceil((max(a[1], b[1], c[1]) - y0) / sy)), H - 1)
            if i1 < i0 or j1 < j0:
                continue
            X, Y = np.meshgrid(cx[i0:i1 + 1], cy[j0:j1 + 1])
            w1 = ((X - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (Y - a[1])) / d
            w2 = ((b[0] - a[0]) * (Y - a[1]) - (X - a[0]) * (b[1] - a[1])) / d
            w0 = 1.0 - w1 - w2
            inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not inside.any():
                continue
            zz = w0 * a[2] + w1 * b[2] + w2 * c[2]
            sub = zbuf[j0:j1 + 1, i0:i1 + 1]
            take = inside & (zz < sub)
            if not take.any():
                continue
            sub[take] = zz[take]
            if tex is not None:
                s, t = ST[T[k]].T
                u = (w0 * s[0] + w1 * s[1] + w2 * s[2])[take]
                v = (w0 * t[0] + w1 * t[1] + w2 * t[2])[take]
                th, tw = tex.shape[:2]
                px = np.clip((np.mod(u, 1.0) * tw).astype(int), 0, tw - 1)
                py = np.clip((np.mod(v, 1.0) * th).astype(int), 0, th - 1)
                col[j0:j1 + 1, i0:i1 + 1][take] = tex[py, px]
            else:
                col[j0:j1 + 1, i0:i1 + 1][take] = 128.0
    have = np.isfinite(zbuf)
    z0, z1 = float(zbuf[have].min()), float(zbuf[have].max())
    a = np.full((H, W), 255, np.uint8)
    a[have] = np.clip(np.round((zbuf[have] - z0) / max(z1 - z0, 1e-6) * 250.0), 0, 250).astype(np.uint8)
    # the empty cells take their nearest hull colour (no dark rim where filtering mixes them in)
    if (~have).any():
        from scipy import ndimage
        _, (iy, ix) = ndimage.distance_transform_edt(~have, return_indices=True)
        col = col[iy, ix]
    img = np.dstack([np.clip(np.round(col), 0, 255).astype(np.uint8), a])
    return img, (x0, y0, x1, y1, z0, z1), int(have.sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pk3')
    ap.add_argument('parts', nargs='+', help='MD3 paths inside the pack; the first names the .mirror file')
    ap.add_argument('--out', required=True, help='a folder the files are written under (their pack paths)')
    ap.add_argument('--cell', type=float, default=1.0, help='the largest grid cell in model units (default 1)')
    a = ap.parse_args()
    img, box, n = bake(a.pk3, a.parts, a.cell)
    first = a.parts[0]
    folder = first.rsplit('/', 1)[0]
    png = folder + '/mirror.png'
    out = Path(a.out)
    (out / folder).mkdir(parents=True, exist_ok=True)
    Image.fromarray(img).save(out / png)
    text = ('// water-mirror proxy (tools/mirror_proxy.py): the underside from below, in model units\r\n'
            'image %s\r\nbox %.3f %.3f %.3f %.3f %.3f %.3f\r\n' % ((png,) + tuple(box)))
    (out / (first[:-4] + '.mirror')).write_text(text, encoding='ascii', newline='')
    print('%s: %dx%d cells, %d with hull; box %s' % (png, img.shape[1], img.shape[0], n,
                                                      ' '.join('%.2f' % v for v in box)))


if __name__ == '__main__':
    sys.exit(main())
