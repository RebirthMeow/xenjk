"""Export a JKA map's (RBSP) draw surfaces near a point to OBJ, for reference renders.

    python tools/bsp_to_obj.py --gamedata <GameData/base> --map mp/ctf_kejim --center X Y Z --radius 600 \
        [--floor-only --floor-z Z --floor-band 48] --out world.obj

Planar surfaces and triangle soups are written as their triangles, patches tessellated (biquadratic Bezier,
8 steps per span). A triangle is kept when its centroid is within --radius of the centre horizontally and
--height vertically. --floor-only keeps upward-facing triangles (normal z > 0.7) whose centroid is within
--floor-band of --floor-z: a receiver plane with an open sky above, so ambient light reaches it as the light
grid's ambient term assumes. Sky and nodraw surfaces are skipped. Coordinates stay in game units, z up.
"""
import argparse
import math
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_order import pack_sort_key  # noqa: E402  (the engine's pk3 order)

SURF_SKY = 0x00002000
SURF_NODRAW = 0x00000080


def lumps(data):
    if data[:4] != b'RBSP':
        raise SystemExit('not an RBSP map')
    return [struct.unpack_from('<ii', data, 8 + 8 * i) for i in range(18)]


def find_bsp(gamedata, name):
    found = None
    for pk in sorted(Path(gamedata).glob('*.pk3'), key=pack_sort_key):   # later packs win
        try:
            z = zipfile.ZipFile(pk)
        except zipfile.BadZipFile:
            continue
        member = 'maps/%s.bsp' % name
        for n in z.namelist():
            if n.lower() == member.lower():
                found = (pk, n)
    if not found:
        raise SystemExit('map not found: %s' % name)
    return zipfile.ZipFile(found[0]).read(found[1])


def bezier(c, s, t):
    def q(a, b, d, u):
        return [(1 - u) ** 2 * a[i] + 2 * u * (1 - u) * b[i] + u * u * d[i] for i in range(3)]
    rows = [q(c[r * 3], c[r * 3 + 1], c[r * 3 + 2], s) for r in range(3)]
    return q(rows[0], rows[1], rows[2], t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gamedata', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--center', type=float, nargs=3, required=True)
    ap.add_argument('--radius', type=float, default=600.0)
    ap.add_argument('--height', type=float, default=400.0)
    ap.add_argument('--floor-only', action='store_true')
    ap.add_argument('--floor-z', type=float)
    ap.add_argument('--floor-band', type=float, default=48.0)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.floor_only and a.floor_z is None:
        ap.error('--floor-only needs --floor-z')

    data = find_bsp(a.gamedata, a.map)
    L = lumps(data)
    sofs, sln = L[1]
    sflags = [struct.unpack_from('<64sii', data, sofs + 72 * k)[1] for k in range(sln // 72)]
    vofs, vln = L[10]
    xyz = [struct.unpack_from('<3f', data, vofs + 80 * k) for k in range(vln // 80)]
    iofs, iln = L[11]
    idx = struct.unpack_from('<%di' % (iln // 4), data, iofs)
    fofs, fln = L[13]

    tris = []                        # (triangle, +1 if cross(e1, e2) is its front normal, -1 if the back)
    for k in range(fln // 148):
        shnum, _fog, stype, fv, nv, fi, ni = struct.unpack_from('<7i', data, fofs + 148 * k)
        if sflags[shnum] & (SURF_SKY | SURF_NODRAW):
            continue
        if stype in (1, 3):          # planar, triangle soup
            for t in range(0, ni, 3):
                tris.append((tuple(xyz[fv + idx[fi + t + j]] for j in range(3)), -1))   # Q3 winds clockwise
        elif stype == 2:             # patch: control grid patchWidth x patchHeight
            pw, ph = struct.unpack_from('<2i', data, fofs + 148 * k + 140)
            steps = 8
            for py in range(0, ph - 1, 2):
                for px in range(0, pw - 1, 2):
                    c = [xyz[fv + (py + r) * pw + px + q] for r in range(3) for q in range(3)]
                    g = [[bezier(c, i / steps, j / steps) for i in range(steps + 1)] for j in range(steps + 1)]
                    for j in range(steps):
                        for i in range(steps):
                            tris.append(((g[j][i], g[j][i + 1], g[j + 1][i + 1]), 1))
                            tris.append(((g[j][i], g[j + 1][i + 1], g[j + 1][i]), 1))

    cx, cy, cz = a.center
    out = []
    for t, front in tris:
        m = [sum(p[i] for p in t) / 3 for i in range(3)]
        if math.hypot(m[0] - cx, m[1] - cy) > a.radius or abs(m[2] - cz) > a.height:
            continue
        if a.floor_only:
            e1 = [t[1][i] - t[0][i] for i in range(3)]
            e2 = [t[2][i] - t[0][i] for i in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            ln = math.sqrt(sum(x * x for x in n)) or 1.0
            if front * n[2] / ln < 0.7 or abs(m[2] - a.floor_z) > a.floor_band:
                continue
        out.append(t)

    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# %s near %s r %.0f%s\no world\n' % (a.map, a.center, a.radius, ' floor only' if a.floor_only else ''))
        for t in out:
            for p in t:
                f.write('v %.3f %.3f %.3f\n' % p)
        for i in range(len(out)):
            f.write('f %d %d %d\n' % (3 * i + 1, 3 * i + 2, 3 * i + 3))
    print('%d triangles -> %s' % (len(out), a.out))


if __name__ == '__main__':
    sys.exit(main())
