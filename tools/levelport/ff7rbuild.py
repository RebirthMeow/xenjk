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
"""ff7rbuild.py -- the gameplay glb of an FF7 Rebirth area for gltf2map, from its render scene (ff7rscene.py).

Collision: the game's own (--game-collision: decode_ff7_collision.py's Terrain_Grid output; heightfields thinned to
--hf-tol, trimeshes as slabs, convexes, boxes and capsules as hull brushes), plus the render meshes' wherever their
surface stands over --fill-above metres above the game's highest collision in that --fill-cell column (or the game has
none): the game walls off its outer rocks and cliff tops, which a Jedi reaches (costa-4: 114 k 16 u cells of drawn
surface had nothing under them, 80 % with no game collision at all). Without --game-collision, collision is what is
drawn:

- every opaque part of every instance as slabs ("shape": "mesh"), at the coarsest LOD with --col-density triangles a
  square metre of the mesh's extent (the Massive chains halve down to 62 triangles: a 34 m cliff's last LOD missed its
  drawn surface by metres, costa-3; --col-lod N forces one) (the Massive chains' LOD 1 keeps
  the shape at about half the triangles), except grass, small nature and decals (--skip, by stream or mesh name),
  meshes under --col-min metres across, and cutout or blend parts (leaves, fronds, cloth);
- a playerclip wall around the scene's box and a lid;
- with --game-collision, swimmable sea (--sea-depth) over a solid bed wherever a column has no game collision at or
  above sea level;
- the scene's lights as q3map2 lights (they only fill the light grid characters are lit by);
- spawns on up-facing collision under open sky near the box centre, spread out.

    python tools/levelport/ff7rbuild.py --scene <builds>/ff7r_costa_scene-N --work DIR --name costa --tag 1
"""
import argparse
import collections
import json
import os
import re
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gltfio  # noqa: E402
import levelport  # noqa: E402
from cp2gltf import BOX_FACES, box_ring_boxes, clearance  # noqa: E402
from nier2gltf import headroom  # noqa: E402
from xscn import SCENE_VERT, xscn_head_of  # noqa: E402
from w3build import DIRS, hull_tris  # noqa: E402

UPM = levelport.UNITS_PER_METRE
GAME_TO_GLTF = np.array([[1.0, 0, 0], [0, 0, 1.0], [0, -1.0, 0]])
SKIP = r'11Grass|12JonnyHotel_Before_Grass|13JonnyHotel_After_Grass|NatureMini|Decal|_Leaf|_Leaves|Grass_'
# JKA surface material (footsteps, impacts) by the source's mesh or stream name, first match
JKA_BY_NAME = [(r'Sand|Beach', 'sand'), (r'Grass|Lawn', 'shortgrass'), (r'Wood|Plank|Deck|Pier', 'solidwood'),
               (r'Metal|Iron|Steel|Pipe|Rail', 'solidmetal'), (r'Glass|Window', 'glass'), (r'Tile|Roof', 'tiles'),
               (r'Carpet|Rug', 'carpet'), (r'Cloth|Fabric|Curtain|Parasol', 'fabric'),
               (r'Rock|Cliff|Stone|Brick|Wall|Terrain|Floor|Stair|Building|Concrete', 'rock')]

LIGHT_DT = np.dtype([('o', '<f4', 3), ('r', '<f4'), ('c', '<f4', 3), ('pw', '<f4'), ('d', '<f4', 3), ('co', '<f4'),
                     ('ci', '<f4'), ('ty', '<f4'), ('box', '<f4'), ('fog', '<f4')])
MESH_DT = np.dtype([('c', '<f4', 4), ('lod', '<u4'), ('nlod', '<u4'), ('pad', '<u4', 2)])
LOD_DT = np.dtype([('part', '<u4'), ('npart', '<u4'), ('screen', '<f4'), ('pad', '<u4')])
PART_DT = np.dtype([('fi', '<u4'), ('ni', '<u4'), ('vo', '<i4'), ('mi', '<u4')])
MAT_DT = np.dtype([('paths', 'S64', 3), ('kind', '<u4'), ('flags', '<u4'), ('rest', '<f4', 10)])
INST_DT = np.dtype([('m', '<f4', 12), ('mesh', '<u4'), ('flags', '<u4'), ('pad', '<u4', 2)])


def read_scene(folder):
    pk3 = next(Path(folder).glob('zz_*_scene.pk3'))
    with zipfile.ZipFile(pk3) as z:
        blob = z.read(next(n for n in z.namelist() if n.endswith('.xscn')))
    magic, version, count, ofs = xscn_head_of(blob)
    assert magic == b'XSCN' and version == 6, (magic, version)
    tab = lambda i, dt: np.frombuffer(blob, dt, count[i], ofs[i])
    return {'V': tab(0, SCENE_VERT), 'I': tab(1, '<u4'), 'meshes': tab(2, MESH_DT), 'lods': tab(3, LOD_DT),
            'parts': tab(4, PART_DT), 'mats': tab(5, MAT_DT), 'inst': tab(6, INST_DT), 'lights': tab(7, LIGHT_DT)}


def jka_for(name):
    for pat, jm in JKA_BY_NAME:
        if re.search(pat, name, re.I):
            return jm
    return None


def heightfield_tris(local, tol, top=32):
    """A decoded heightfield's triangles (decode_ff7_collision.py: local (n, 3, 3), x and y whole grid steps, z raw/128;
    holes left out), thinned: grid cells of size top, 16, ... 2 whose two triangles (diagonal (0,0)-(s,s)) miss every
    sample inside by at most tol (local z units) and hold no hole; under that, the game's own triangles. The -32768
    samples (z -256: skirts beside holes, decode notes) count as holes."""
    v = local.reshape(-1, 3)
    n = int(max(v[:, 0].max(), v[:, 1].max())) + 1
    Z = np.full((n, n), np.nan)
    xi, yi = np.rint(v[:, 0]).astype(np.int64), np.rint(v[:, 1]).astype(np.int64)
    Z[xi, yi] = v[:, 2]
    Z[Z <= -255.99] = np.nan
    c = local.mean(1)
    cx, cy = np.floor(c[:, 0]).astype(np.int64), np.floor(c[:, 1]).astype(np.int64)
    good = ~np.isnan(Z[xi.reshape(-1, 3), yi.reshape(-1, 3)]).any(1)
    ok = np.zeros((n - 1, n - 1), np.int64)
    np.add.at(ok, (cx[good], cy[good]), 1)
    full = (ok >= 2).astype(np.int64)                 # both triangles of the unit cell present
    S = np.zeros((n, n), np.int64)
    S[1:, 1:] = full.cumsum(0).cumsum(1)
    out = []
    todo = np.array([(x, y) for x in range(0, n - 1, top) for y in range(0, n - 1, top)], np.int64).reshape(-1, 2)
    size = top
    while size >= 2 and len(todo):
        x0, y0 = todo[:, 0], todo[:, 1]
        fits = (x0 + size <= n - 1) & (y0 + size <= n - 1)
        x1, y1 = np.minimum(x0 + size, n - 1), np.minimum(y0 + size, n - 1)
        cnt = S[x1, y1] - S[x0, y1] - S[x1, y0] + S[x0, y0]
        acc = np.zeros(len(todo), bool)
        idx = np.where(fits & (cnt == size * size))[0]
        if len(idx):
            r = np.arange(size + 1)
            X = x0[idx, None, None] + r[None, :, None]
            Y = y0[idx, None, None] + r[None, None, :]
            G = Z[X, Y]
            z00, zs0 = G[:, 0, 0, None, None], G[:, -1, 0, None, None]
            z0s, zss = G[:, 0, -1, None, None], G[:, -1, -1, None, None]
            u = (r / size)[None, :, None]
            w_ = (r / size)[None, None, :]
            I1 = z00 + u * (zs0 - z00) + w_ * (zss - zs0)
            I2 = z00 + w_ * (z0s - z00) + u * (zss - z0s)
            err = np.abs(np.where(u >= w_, I1, I2) - G)
            err = np.where(np.isnan(err), np.inf, err).reshape(len(idx), -1).max(1)
            good_c = idx[err <= tol]
            acc[good_c] = True
            for i in good_c:
                a0, b0, s_ = x0[i], y0[i], size
                p00 = (a0, b0, Z[a0, b0])
                ps0 = (a0 + s_, b0, Z[a0 + s_, b0])
                p0s = (a0, b0 + s_, Z[a0, b0 + s_])
                pss = (a0 + s_, b0 + s_, Z[a0 + s_, b0 + s_])
                out.append((p00, ps0, pss))
                out.append((p00, pss, p0s))
        rest = np.where(~acc & (cnt > 0))[0]          # split the rest (empty blocks drop)
        h = size // 2
        if len(rest):
            todo = np.concatenate([np.stack([x0[rest] + dx, y0[rest] + dy], 1) for dx in (0, h) for dy in (0, h)])
            todo = todo[(todo[:, 0] < n - 1) & (todo[:, 1] < n - 1)]
        else:
            todo = np.zeros((0, 2), np.int64)
        size = h
    # unit cells left: the game's own triangles there
    leaf = np.zeros((n - 1, n - 1), bool)
    if len(todo):
        leaf[todo[:, 0], todo[:, 1]] = True
    keep = good & leaf[np.clip(cx, 0, n - 2), np.clip(cy, 0, n - 2)]
    T = np.array(out, np.float64).reshape(-1, 3, 3) if out else np.zeros((0, 3, 3))
    # (the coarse cells wind as the game's own triangles do)
    ref = local[good][:1]
    if len(T) and len(ref):
        nz_ref = np.cross(ref[:, 1] - ref[:, 0], ref[:, 2] - ref[:, 0])[0, 2]
        nz = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])[:, 2]
        flip = np.sign(nz) != np.sign(nz_ref)
        T[flip] = T[flip][:, [0, 2, 1]]
    return np.concatenate([T, local[keep]])


def load_game_collision(dirs, tol_cm):
    """decode_ff7_collision.py's output folders -> (slab triangles, hull point sets), both UE world cm"""
    import glob
    slabs, hulls, st = [], [], collections.Counter()
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, '*.npz'))):
            z = np.load(f)
            if 'world_tris' not in z.files or not len(z['tris']):
                st['files_empty'] += 1
                continue
            j = json.load(open(f[:-4] + '.json', encoding='utf-8'))
            kinds = j.get('kinds') or {}
            if 'heightfield' in kinds:
                loc, sc = z['placements'][0][:3], z['placements'][0][3:6]
                T = heightfield_tris(z['tris'].astype(np.float64), tol_cm / float(sc[2]))
                slabs.append(T * sc + loc)
                st['heightfield_tris_in'] += len(z['tris'])
                st['heightfield_tris_out'] += len(T)
                continue
            W, sh = z['world_tris'].astype(np.float64), z['shape']
            kind_of = {int(k): (v or {}).get('kind') for k, v in enumerate(j.get('shapes') or [])}
            for k in np.unique(sh):
                t = W[sh == k]
                kind = kind_of.get(int(k), 'convex')
                if kind == 'trimesh':
                    slabs.append(t)
                    st['trimesh_tris'] += len(t)
                else:
                    hulls.append(np.unique(t.reshape(-1, 3), axis=0))
                    st['shapes_' + str(kind)] += 1
    return (np.concatenate(slabs) if slabs else np.zeros((0, 3, 3))), hulls, st


def grid_rects(m):
    """A boolean grid's true cells as rectangles (i0, j0, i1, j1), ends exclusive: each row's runs, a run grown over
    the following rows while they hold the same run free."""
    m = np.asarray(m, bool).copy()
    out = []
    for i in range(m.shape[0]):
        row = m[i]
        j = 0
        while j < m.shape[1]:
            if not row[j]:
                j += 1
                continue
            j1 = j
            while j1 < m.shape[1] and row[j1]:
                j1 += 1
            i1 = i + 1
            while i1 < m.shape[0] and m[i1, j:j1].all():
                i1 += 1
            m[i:i1, j:j1] = False
            out.append((i, j, i1, j1))
            j = j1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--scene', required=True, help="ff7rscene.py's build folder (pack, scene.json, meshes.json)")
    ap.add_argument('--work', required=True)
    ap.add_argument('--name', default='costa')
    ap.add_argument('--tag', default='1')
    ap.add_argument('--col-lod', type=int, help="force this LOD (a mesh's last when it has fewer)")
    ap.add_argument('--col-density', type=float, default=0.5,
                    help="triangles per square metre of the mesh's extent its collision LOD keeps at least (0.5: "
                         "about 4.8 M on Costa; the terrain's LOD 1 alone is 42 M)")
    ap.add_argument('--hull-under', type=float, default=4.0,
                    help='metres: smaller meshes collide as one convex brush, the hull of their LOD, not slabs')
    ap.add_argument('--game-collision', help="comma list of decode_ff7_collision.py output folders (the game's own "
                                             "collision, its Terrain_Grid maps): used in place of the render meshes'")
    ap.add_argument('--hf-tol', type=float, default=10.0, help='cm: heightfield thinning tolerance')
    ap.add_argument('--fill-above', type=float, default=1.0,
                    help="metres: with --game-collision, render-mesh collision where the drawn surface stands this far "
                         "over the game's top in its column (negative: none)")
    ap.add_argument('--fill-cell', type=float, default=2.0, help='metres: the --fill-above columns')
    ap.add_argument('--seabed', type=float, default=-2.0,
                    help='metres (source z): triangles wholly under it get no collision (the sea floor)')
    ap.add_argument('--sea-depth', type=float, default=4.0,
                    help='metres: swimmable sea under z 0 where the game has no collision above it (0: none)')
    ap.add_argument('--collision-box', type=float, nargs=4, metavar=('X0', 'Y0', 'X1', 'Y1'),
                    help="UE cm: the playable box (collision and boundary); default the scene's play_box_cm (the "
                         "area's own box, without its drawn surround), else its box_cm")
    ap.add_argument('--col-min', type=float, default=1.2, help='metres: meshes smaller across get no collision')
    ap.add_argument('--skip', default=SKIP, help='regex on the stream / mesh name: no collision')
    ap.add_argument('--cell', type=float, default=48.0, help='metres: collision nodes gather triangles by this grid')
    ap.add_argument('--spawn-centre', type=float, nargs=2, help='game units about the origin (default 0 0)')
    ap.add_argument('--spawn-radius', type=float, default=60.0, help='metres')
    ap.add_argument('--spawns', type=int, default=16)
    ap.add_argument('--spawn-sky', type=float, default=6.0, help='metres clear above a spawn')
    ap.add_argument('--spawn-min-z', type=float, default=0.5, help='metres above the origin (the sea): no wet spawns')
    ap.add_argument('--top', type=float, help='lid height, metres (default: highest collision + 40)')
    ap.add_argument('--light-scale', type=float, default=1.0)
    a = ap.parse_args()

    rec = json.loads((Path(a.scene) / 'scene.json').read_text(encoding='utf-8'))
    names = json.loads((Path(a.scene) / 'meshes.json').read_text(encoding='utf-8'))
    s = read_scene(a.scene)
    V, I, meshes, lods, parts, mats, inst = (s[k] for k in ('V', 'I', 'meshes', 'lods', 'parts', 'mats', 'inst'))
    # box about the origin, metres
    o = np.array(rec['origin_cm'])
    bx0, by0, bx1, by1 = a.collision_box or rec.get('play_box_cm') or rec['box_cm']
    box = np.array([bx0 - o[0], by0 - o[1], bx1 - o[0], by1 - o[1]]) / 100.0
    skip = re.compile(a.skip)
    stats = {'meshes': len(meshes), 'instances': len(inst)}

    def keep_mesh(mi):
        n = names[mi] or ['?']
        if skip.search('/'.join(n)):
            return False
        return float(meshes[mi]['c'][3]) * 2 / UPM >= a.col_min

    w = gltfio.Writer()
    g_col = w.node('collision')
    g_ent = w.node('entities')
    gmats = {}

    def mat_for(jm):
        key = 'col_' + (jm or 'none')
        if key not in gmats:
            gmats[key] = w.material(key, extras={'surfaceparms': []})
        return gmats[key]

    # per mesh: the collision LOD's opaque triangles (game units, mesh space)
    cells = {}                      # (cell x, cell y, jka) -> [triangles (n, 3, 3) metres, glTF axes]
    kept, skipped = 0, 0
    hulls = []                      # (points (n, 3) metres, jka) for the small meshes
    order = np.argsort(inst['mesh'], kind='stable')
    ms, starts = np.unique(inst['mesh'][order], return_index=True)
    tri_total = 0
    if a.game_collision:
        slab_ue, hull_ue, gst = load_game_collision(a.game_collision.split(','), a.hf_tol)
        stats.update({'game_' + k: v for k, v in gst.items()})

        def q(P):           # UE cm -> metres about the origin, Quake axes
            return np.stack([P[..., 0] - o[0], -P[..., 1] - o[1], P[..., 2] - o[2]], -1) / 100.0
        P = q(slab_ue)[:, [0, 2, 1]]                      # (mirrored: the winding reversed keeps floors up)
        c = P.mean(1)
        inb = (c[:, 0] >= box[0]) & (c[:, 0] <= box[2]) & (c[:, 1] >= box[1]) & (c[:, 1] <= box[3])
        inb &= P[:, :, 2].max(1) >= a.seabed
        P, c = P[inb], c[inb]
        tri_total = len(P)
        key = np.floor(c[:, :2] / a.cell).astype(np.int64)
        o_ = np.lexsort((key[:, 1], key[:, 0]))
        P, key = P[o_], key[o_]
        brk = np.r_[0, np.where(np.any(np.diff(key, axis=0) != 0, 1))[0] + 1, len(key)]
        for b0, b1 in zip(brk[:-1], brk[1:]):
            cells.setdefault((int(key[b0, 0]), int(key[b0, 1]), None), []).append(P[b0:b1])
        for pts in hull_ue:
            Pw = q(pts)
            cc = Pw.mean(0)
            if not (box[0] <= cc[0] <= box[2] and box[1] <= cc[1] <= box[3]) or Pw[:, 2].max() < a.seabed:
                continue
            if len(Pw) > 40:
                Pw = Pw[np.unique(np.argmax(Pw @ DIRS.T, axis=0))]
            hulls.append((Pw, None))
        # the game's highest collision a column (each triangle's and hull's top over its whole xy box: high, so the
        # fill errs toward none)
        fc = a.fill_cell
        g0 = np.floor(box[:2] / fc).astype(np.int64)
        gn = np.floor(box[2:] / fc).astype(np.int64) - g0 + 1
        gtop = np.full((gn[0], gn[1]), -np.inf)

        def stamp(lo_, hi_, z):
            i0 = np.clip(np.floor(lo_ / fc).astype(np.int64) - g0, 0, gn - 1)
            i1 = np.clip(np.floor(hi_ / fc).astype(np.int64) - g0, 0, gn - 1)
            for (x0, y0), (x1, y1), zz in zip(i0.tolist(), i1.tolist(), z.tolist()):
                v = gtop[x0:x1 + 1, y0:y1 + 1]
                np.maximum(v, zz, out=v)
        stamp(P[:, :, :2].min(1), P[:, :, :2].max(1), P[:, :, 2].max(1))
        if hulls:
            stamp(np.array([h[0][:, :2].min(0) for h in hulls]), np.array([h[0][:, :2].max(0) for h in hulls]),
                  np.array([h[0][:, 2].max() for h in hulls]))
        stats['game_columns_with_collision'] = int(np.isfinite(gtop).sum())

        def over_game(c_xy, ztop):          # True where render collision fills in
            i = np.clip(np.floor(c_xy / fc).astype(np.int64) - g0, 0, gn - 1)
            return ztop > gtop[i[:, 0], i[:, 1]] + a.fill_above
        if a.fill_above < 0:
            ms = np.zeros(0, np.int64)                    # (no render-mesh collision)
    else:
        over_game = None
    for mi, s0, s1 in zip(ms.tolist(), starts.tolist(), list(starts[1:].tolist()) + [len(order)]):
        if not keep_mesh(mi):
            skipped += s1 - s0
            continue
        m = meshes[mi]
        if a.col_lod is not None:
            k = min(a.col_lod, m['nlod'] - 1)
        else:
            need = a.col_density * (float(m['c'][3]) * 2 / UPM) ** 2
            k = 0
            for j in range(m['nlod']):
                Lj = lods[m['lod'] + j]
                if sum(int(parts[q]['ni']) // 3 for q in range(Lj['part'], Lj['part'] + Lj['npart'])) >= need:
                    k = j
        L = lods[m['lod'] + k]
        tris = []
        for p in parts[L['part']:L['part'] + L['npart']]:
            if mats[p['mi']]['kind'] != 0 and mats[p['mi']]['kind'] != 3:      # opaque and emissive only
                continue
            idx = I[p['fi']:p['fi'] + p['ni']].astype(np.int64) + p['vo']
            tris.append(V['pos'][idx].reshape(-1, 3, 3).astype(np.float64))
        if not tris:
            skipped += s1 - s0
            continue
        t = np.concatenate(tris)
        jm = jka_for('/'.join(names[mi] or []))
        M = inst['m'][order[s0:s1]].reshape(-1, 3, 4).astype(np.float64)
        if float(meshes[mi]['c'][3]) * 2 / UPM < a.hull_under:
            # one convex brush an instance: the hull of the LOD's points
            pts = np.unique(t.reshape(-1, 3), axis=0)
            # (bounded: the extreme points along 26 directions; a detailed piece's own hull ran past q3map2's
            # MAX_BUILD_SIDES, costa-2)
            pts = pts[np.unique(np.argmax(pts @ DIRS.T, axis=0))]
            for j, Mi in enumerate(M):
                Pw = (pts @ Mi[:, :3].T + Mi[:, 3]) / UPM
                c = Pw.mean(0)
                if not (box[0] <= c[0] <= box[2] and box[1] <= c[1] <= box[3]) or Pw[:, 2].max() < a.seabed:
                    continue
                if over_game is not None and not over_game(c[None, :2], np.array([Pw[:, 2].max()]))[0]:
                    continue
                hulls.append((Pw, jm))
                stats['fill_hulls'] = stats.get('fill_hulls', 0) + (over_game is not None)
            kept += s1 - s0
            continue
        P = np.einsum('kij,ntj->knti', M[:, :, :3], t) + M[:, None, None, :, 3]      # (k, n, 3, 3) game units
        P = P.reshape(-1, 3, 3) / UPM
        c = P.mean(1)
        inb = (c[:, 0] >= box[0]) & (c[:, 0] <= box[2]) & (c[:, 1] >= box[1]) & (c[:, 1] <= box[3])
        inb &= P[:, :, 2].max(1) >= a.seabed
        if over_game is not None:
            inb &= over_game(c[:, :2], P[:, :, 2].max(1))
        P, c = P[inb], c[inb]
        tri_total += len(P)
        if over_game is not None:
            stats['fill_tris'] = stats.get('fill_tris', 0) + len(P)
        if not len(P):
            continue
        kept += s1 - s0
        key = np.floor(c[:, :2] / a.cell).astype(np.int64)
        for k in np.unique(key, axis=0):
            sel = (key[:, 0] == k[0]) & (key[:, 1] == k[1])
            cells.setdefault((int(k[0]), int(k[1]), jm), []).append(P[sel])
    stats.update(instances_collision=kept, instances_no_collision=skipped, collision_tris=tri_total)

    T_all = []
    zmax, zmin = -1e9, 1e9
    for n, ((cx, cy, jm), Ts) in enumerate(sorted(cells.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2] or ''))):
        T = np.concatenate(Ts)
        zmax, zmin = max(zmax, float(T[..., 2].max())), min(zmin, float(T[..., 2].min()))
        G = T.reshape(-1, 3) @ GAME_TO_GLTF.T
        Gt = G.reshape(-1, 3, 3)
        T_all.append(Gt)
        ex = {'lp': 'collision', 'shape': 'mesh', 'contents': ['solid'], 'thickness': 8.0}
        if jm:
            ex['jka_material'] = jm
        w.node('c%d' % n, mesh=w.mesh('c%d' % n, [{'position': G.astype(np.float32),
                                                    'indices': np.arange(len(G)).reshape(-1, 3),
                                                    'material': mat_for(jm)}]), parent=g_col, extras=ex)
    stats['collision_nodes'] = len(cells)
    nh = 0
    for Pw, jm in hulls:
        G = Pw @ GAME_TO_GLTF.T
        try:
            F = hull_tris(G)
        except Exception:  # noqa: BLE001 - flat or degenerate
            stats['hull_degenerate'] = stats.get('hull_degenerate', 0) + 1
            continue
        zmax, zmin = max(zmax, float(Pw[:, 2].max())), min(zmin, float(Pw[:, 2].min()))
        T_all.append(G[F])
        ex = {'lp': 'collision', 'contents': ['solid', 'detail']}
        if jm:
            ex['jka_material'] = jm
        w.node('h%d' % nh, mesh=w.mesh('h%d' % nh, [{'position': G.astype(np.float32), 'indices': F,
                                                      'material': mat_for(jm)}]), parent=g_col, extras=ex)
        nh += 1
    stats['hull_brushes'] = nh

    # the sea: swimmable water (z --sea-depth .. 0, the source's sea level) over a solid bed in every --fill-cell
    # column the game has no collision at or above sea level in (the game's seabed under --seabed has none)
    if a.game_collision and a.sea_depth > 0:
        wat_mi = w.material('collision_water', extras={'surfaceparms': []})
        bed_mi = w.material('collision_water_bed', extras={'surfaceparms': []})
        nw = 0
        for i0, j0, i1, j1 in grid_rects(~(gtop >= 0.0)):
            x0, y0 = (g0 + [i0, j0]) * fc
            x1, y1 = (g0 + [i1, j1]) * fc
            x0, y0, x1, y1 = max(x0, box[0]), max(y0, box[1]), min(x1, box[2]), min(y1, box[3])
            if x1 <= x0 or y1 <= y0:
                continue
            for tag, z0, z1, contents, mi in (('water', -a.sea_depth, 0.0, ['water', 'trans', 'detail'], wat_mi),
                                              ('wbed', -a.sea_depth - 1.0, -a.sea_depth, ['solid', 'detail'], bed_mi)):
                b = np.array([[x, y, z] for z in (z0, z1) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))])
                w.node('%s%d' % (tag, nw), mesh=w.mesh('%s%d' % (tag, nw), [{
                    'position': b @ GAME_TO_GLTF.T, 'indices': BOX_FACES, 'material': mi}]),
                    parent=g_col, extras={'lp': 'collision', 'contents': contents})
            nw += 1
        stats['water_volumes'] = nw

    # the game's invisible blockers (ff7rscene.py's clip.npz): playerclip slabs
    cf = Path(a.scene) / 'clip.npz'
    if cf.exists():
        C = np.load(cf)['tris'].astype(np.float64) / UPM
        c = C.mean(1)
        C = C[(c[:, 0] >= box[0]) & (c[:, 0] <= box[2]) & (c[:, 1] >= box[1]) & (c[:, 1] <= box[3])]
        if len(C):
            G = C.reshape(-1, 3) @ GAME_TO_GLTF.T
            w.node('blockers', mesh=w.mesh('blockers', [{'position': G.astype(np.float32),
                                                         'indices': np.arange(len(G)).reshape(-1, 3),
                                                         'material': mat_for('clip')}]), parent=g_col,
                   extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['playerclip'], 'thickness': 8.0})
        stats['blocker_tris'] = int(len(C))

    # boundary: playerclip walls around the box and a lid
    top = a.top if a.top is not None else zmax + 40.0
    floor = zmin - 20.0
    half = ((box[2] - box[0]) / 2, (box[3] - box[1]) / 2)
    centre = np.array([(box[0] + box[2]) / 2, (box[1] + box[3]) / 2])
    g = lambda p: np.asarray(p, np.float64) @ GAME_TO_GLTF.T
    clip = mat_for('clip')
    for i, b in enumerate(box_ring_boxes(half, floor, top)):
        b = b + np.r_[centre, 0.0]
        w.node('clip%d' % i, mesh=w.mesh('clip%d' % i, [{'position': g(b), 'indices': BOX_FACES, 'material': clip}]),
               parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    hx, hy = half[0] + 3.0, half[1] + 3.0
    lid = np.array([[centre[0] + sx * hx, centre[1] + sy * hy, z] for z in (top, top + 1.0)
                    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    w.node('lid', mesh=w.mesh('lid', [{'position': g(lid), 'indices': BOX_FACES, 'material': clip}]),
           parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    stats.update(top=round(top, 1), floor=round(floor, 1))

    # lights for the light grid: the scene's, reach = their radius
    nl = 0
    for i, L in enumerate(s['lights']):
        p = np.array(L['o'], np.float64) / UPM
        w.node('light%d' % i, translation=g(p), parent=g_ent, extras={
            'lp': 'entity', 'classname': 'light', 'light': '%d' % round(float(L['r']) * a.light_scale),
            '_color': '%.3f %.3f %.3f' % tuple(L['c']), 'spawnflags': '1'})
        nl += 1
    stats['lights'] = nl

    # spawns: up-facing collision near the spawn centre under open sky, spread out, above the sea
    T = np.concatenate(T_all)
    sc = np.array(a.spawn_centre if a.spawn_centre else [0.0, 0.0]) / UPM
    scg = g([sc[0], sc[1], 0.0])[[0, 2]]
    nrm = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    area = np.linalg.norm(nrm, axis=1)
    up = (nrm[:, 1] / np.maximum(area, 1e-9) > 0.9) & (area > 0.5)
    cand = T[up].mean(1)
    cand = cand[(np.hypot(cand[:, 0] - scg[0], cand[:, 2] - scg[1]) < a.spawn_radius) & (cand[:, 1] > a.spawn_min_z)]
    cand = cand[np.lexsort(cand.T[::-1])]
    if len(cand) > 4000:
        cand = cand[np.random.default_rng(1).choice(len(cand), 4000, replace=False)]
    r = a.spawn_radius + 2.0
    lo_xz, hi_xz = T[:, :, [0, 2]].min(1), T[:, :, [0, 2]].max(1)
    over = T[np.all(hi_xz >= scg - r, 1) & np.all(lo_xz <= scg + r, 1)]
    cand = cand[headroom(cand, over[:, [0, 2, 1]], low=0.2, high=max(2.5, a.spawn_sky))]
    cand = cand[clearance(cand, over)]
    order = [int(np.argmin(np.hypot(cand[:, 0] - scg[0], cand[:, 2] - scg[1])))] if len(cand) else []
    while len(order) < min(a.spawns, len(cand)):
        d = np.min(np.linalg.norm(cand[:, None] - cand[order][None], axis=2), axis=1)
        order.append(int(np.argmax(d)))
    for k, i in enumerate(order):
        w.node('spawn%d' % k, translation=cand[i] + np.array([0, 1.0, 0]), parent=g_ent, extras={
            'lp': 'entity', 'classname': 'info_player_deathmatch', 'angle': str(90 * (k % 4))})
    stats['spawns'] = len(order)

    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    out = work / ('%s-%s.glb' % (a.name, a.tag))
    if out.exists():
        sys.exit('%s exists: builds are numbered, never overwritten' % out)
    w.save(out, extras={'levelport': {'version': 1, 'units_per_metre': UPM, 'source_game': 'ff7_rebirth',
                                      'source': '%s scene %s' % (a.name, Path(a.scene).name),
                                      'worldspawn': {'message': 'Costa del Sol'}}})
    with open(work / ('%s-%s.build.json' % (a.name, a.tag)), 'w') as fp:
        json.dump({'scene': str(a.scene), 'origin_cm': rec['origin_cm'], 'box_cm': [bx0, by0, bx1, by1], **stats}, fp,
                  indent=1)
    print(f'ff7rbuild -> {out} ({out.stat().st_size / 1e6:.1f} MB)')
    print('  ' + ', '.join(f'{k} {v}' for k, v in stats.items()))


if __name__ == '__main__':
    main()
