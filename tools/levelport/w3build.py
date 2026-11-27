"""w3build.py -- the gameplay glb of a Witcher 3 work folder (Beauclair), for gltf2map.

Plain Python (no Blender). Reads w3gltf.py's collision.json and placements.json (lights) and w3terrain.py's
terrain/tiles.npz, and writes <work>/<name>-<tag>.glb in the levelport convention (glTF metres, +Y up, nodes with
"lp" extras; tools/levelport/README.md):

- every game collision shape as a collision node with its JKA surface material (footsteps, impacts):
  boxes and convex hulls as one brush; spheres and capsules as the hull of their 26 support points; PhysX triangle
  meshes as slabs ("shape": "mesh");
- the terrain inside --box as slabs: --terrain-step cells, split down to every vertex where they miss the
  heightfield by more than --terrain-tol;
- a playerclip wall around --box and a lid at --top;
- the source's lights as q3map2 lights (they only fill the light grid characters are lit by);
- spawns on up-facing collision under open sky near --spawn-centre, spread out.

    python tools/levelport/w3build.py --work DIR --name castle --tag 1 --box MINX MINY MAXX MAXY
        [--spawn-centre X Y] [--spawn-radius 60] [--spawns 16] [--terrain-step 2] [--terrain-tol 0.15] [--top Z]
"""
import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gltfio  # noqa: E402
import levelport  # noqa: E402
from cp2gltf import BOX_FACES, box_ring_boxes, clearance  # noqa: E402
from nier2gltf import headroom  # noqa: E402

# game (Witcher 3: metres, +Z up) -> glTF (metres, +Y up): (x, z, -y), the same axes as CP2077's
GAME_TO_GLTF = np.array([[1.0, 0, 0], [0, 0, 1.0], [0, -1.0, 0]])

# Witcher 3 physical materials -> JKA surface materials (codemp/game/surfaceflags.h)
JKA_MATERIAL = {
    'stone_solid': 'rock', 'stone_debris': 'gravel', 'stone': 'rock', 'clay_tile': 'tiles', 'wood_solid': 'solidwood',
    'wood_hollow': 'hollowwood', 'wood': 'solidwood', 'glass': 'glass', 'metal': 'solidmetal',
    'metal_hollow': 'hollowmetal', 'dirt_hard': 'dirt', 'dirt_soil': 'dirt', 'dirt': 'dirt', 'mud': 'mud',
    'sand': 'sand', 'grass': 'shortgrass', 'carpet': 'carpet', 'fabric': 'fabric', 'flesh': 'flesh',
    'rubber': 'rubber', 'water': 'water', 'snow': 'snow', 'ice': 'ice', 'default': None,
    # (the city's: hay as long grass, roofs as tiles, iron as solid metal; bob_rich_01 is a stone house's)
    'grass_long': 'longgrass', 'grass_short': 'shortgrass', 'gravel_small': 'gravel', 'hay': 'longgrass',
    'iron': 'solidmetal', 'leaves': 'greenleaves', 'roof': 'tiles', 'wood_debris': 'hollowwood', 'bob_rich_01': 'rock',
    'Physical_Materials_From_Engine': None,
}

# 26 directions: the faces, edges and corners of a cube
DIRS = np.array([d for d in ((x, y, z) for x in (-1, 0, 1) for y in (-1, 0, 1) for z in (-1, 0, 1)) if any(d)],
                dtype=np.float64)
DIRS /= np.linalg.norm(DIRS, axis=1, keepdims=True)


def control_lattice(props):
    """A terrain tile's control words as the game's shader reads them: (res + 1)^2 u16, row +y, column +x, the
    positive neighbours' first column, row and corner stitched on (the Witcher 3 Blender tools' terrain_detail).
    None when the tile has no texture buffer."""
    res = int(props.get('witcher_terrain_control_res') or props.get('tile_res') or 0)

    def read(key):
        p = props.get(key) or ''
        if not p or not os.path.isfile(p):
            return None
        d = np.fromfile(p, dtype='<u2')
        return d.reshape(res, res) if d.size == res * res else None
    cur = read('witcher_terrain_texture_buffer')
    if cur is None:
        return None
    right, up, diag = (read('witcher_terrain_positive_%s_texture_buffer' % k) for k in ('x', 'y', 'xy'))
    out = np.empty((res + 1, res + 1), np.uint16)
    out[:res, :res] = cur
    out[:res, res] = right[:, 0] if right is not None else cur[:, -1]
    out[res, :res] = up[0, :] if up is not None else cur[-1, :]
    out[res, res] = diag[0, 0] if diag is not None else cur[-1, -1]
    return out


def solid_share(lattice, bounds, xy):
    """The share of the game's terrain drawn at world points xy (N x 2): its four control taps' bilinear weights on
    the taps that are not holes (control word 0), as its shader's alpha. bounds: the tile's min x, min y, max x, max y."""
    # The taps sit at texel centres, (k + 0.5) / res of the tile (a texture sampled at the tile UV): measured over the
    # city's 1694 hole cells, the ground under the cut is steepest and least often flat at +0.5 tap (wall feet; slope
    # 2.06 against 1.99 at 0 and 1.97 at +1). Read at the taps' corners, every hole sat 0.24 m off its wall's foot:
    # flat walkway cut (open sky) beside a ramp left standing (grass through the pavement), terrain-20261126/
    m = lattice.shape[0]
    x0, y0, x1, y1 = bounds
    gx = np.clip((xy[:, 0] - x0) / (x1 - x0) * (m - 1) - 0.5, 0.0, m - 1.0)
    gy = np.clip((xy[:, 1] - y0) / (y1 - y0) * (m - 1) - 0.5, 0.0, m - 1.0)
    cx = np.minimum(np.floor(gx).astype(int), m - 2)
    cy = np.minimum(np.floor(gy).astype(int), m - 2)
    fx, fy = gx - cx, gy - cy
    solid = (lattice != 0).astype(np.float64)
    return (solid[cy, cx] * (1 - fx) * (1 - fy) + solid[cy, cx + 1] * fx * (1 - fy) +
            solid[cy + 1, cx] * (1 - fx) * fy + solid[cy + 1, cx + 1] * fx * fy)


def square_error(Z, s):
    """Per step-s cell of height grid Z (rows +y, columns +x): the largest height miss of the full-resolution
    vertices inside it against the cell's two triangles (split a-c, as the slabs: (a, b, c), (a, c, d))."""
    n, m = (Z.shape[0] - 1) // s, (Z.shape[1] - 1) // s
    a = Z[0:n * s:s, 0:m * s:s]
    b = Z[0:n * s:s, s:m * s + 1:s]
    c = Z[s:n * s + 1:s, s:m * s + 1:s]
    d = Z[s:n * s + 1:s, 0:m * s:s]
    err = np.zeros((n, m))
    for u in range(s + 1):              # u: rows (+y, fy), v: columns (+x, fx)
        for v in range(s + 1):
            fy, fx = u / s, v / s
            p = a + fx * (b - a) + fy * (c - b) if fx >= fy else a + fy * (d - a) + fx * (c - d)
            err = np.maximum(err, np.abs(Z[u:u + n * s:s, v:v + m * s:s] - p))
    return err


def terrain_cells(Z, step, tol):
    """The terrain as cells (row, column, size) in full-resolution grid steps: step-sized cells, each split in four
    until its two triangles miss no full-resolution height by more than tol (down to single grid steps). A uniform
    step-4 grid (3.9 m) cuts 45 degree slopes across street edges and wall feet, a metre under the street a player
    sinks into. Seams between sizes leave height steps under tol; the slabs are thicker."""
    out = []
    want = None                         # cells of this size that the bigger size split
    s = step
    while s >= 1:
        n, m = (Z.shape[0] - 1) // s, (Z.shape[1] - 1) // s
        split = square_error(Z, s) > tol if s > 1 else np.zeros((n, m), bool)
        here = ~split if want is None else want & ~split
        ii, jj = np.nonzero(here)
        out.extend(zip((ii * s).tolist(), (jj * s).tolist(), [s] * len(ii)))
        live = split if want is None else want & split
        if s == 1 or not live.any():
            break
        h = s // 2
        nn, mm = (Z.shape[0] - 1) // h, (Z.shape[1] - 1) // h
        want = np.zeros((nn, mm), bool)
        want[:2 * n, :2 * m] = np.repeat(np.repeat(live, 2, 0), 2, 1)
        s = h
    return out


def terrain_tiles(work, sub='terrain'):
    """[(tile key, grid (rows +y, columns +x, xyz), props, lattice or None, bounds)] from w3terrain.py's dump (sub:
    its folder; 'terrain_far' holds the render-only ring past the city, w3scene.py)"""
    tz = Path(work) / sub / 'tiles.npz'
    tj = Path(work) / sub / 'tiles.json'
    if not tz.exists() or not tj.exists():
        return []
    z = np.load(tz)
    meta = json.load(open(tj, encoding='utf-8'))['tiles']
    out = []
    for key, rec in sorted(meta.items()):
        props = rec['props']
        b = (float(props['tile_bounds_min_x']), float(props['tile_bounds_min_y']),
             float(props['tile_bounds_max_x']), float(props['tile_bounds_max_y']))
        out.append((key, border_filler(z['grid_' + key].astype(np.float64)), props, control_lattice(props), b))
    return out


def border_filler(G, jump=100.0):
    """A tile's grid with a filler edge replaced by its neighbouring line: the world's last tiles (+x and +y, at 4000 m
    in Toussaint) end in a row or column all at one height (884.99 m, the height range's top) that stood as a wall
    along the horizon; a whole border line at one height, more than `jump` metres from the line inside it"""
    for line, inner in (((-1, slice(None)), (-2, slice(None))), ((0, slice(None)), (1, slice(None))),
                        ((slice(None), -1), (slice(None), -2)), ((slice(None), 0), (slice(None), 1))):
        h, hi = G[line + (2,)], G[inner + (2,)]
        if np.ptp(h) == 0.0 and abs(float(h[0]) - float(np.median(hi))) > jump:
            G[line + (2,)] = hi
    return G


def jka_material(name, missing):
    if name is None:
        return None
    if name in JKA_MATERIAL:
        return JKA_MATERIAL[name]
    missing.add(name)
    return None


def tri_faces(faces):
    """Polygons (lists of vertex indices) as triangles (fans)."""
    out = []
    for f in faces:
        for k in range(1, len(f) - 1):
            out.append((f[0], f[k], f[k + 1]))
    return np.array(out, dtype=np.int64).reshape(-1, 3)


def hull_tris(pts):
    from scipy.spatial import ConvexHull
    h = ConvexHull(pts)
    tris = h.simplices.copy()
    c = pts.mean(0)
    for t in tris:                       # outward winding (counter-clockwise from outside, as glTF)
        a, b, d = pts[t]
        if np.dot(np.cross(b - a, d - a), a - c) < 0:
            t[1], t[2] = t[2], t[1]
    return tris


def fill_from_compare(folder, work, pl, origin, box, w, g_col, mat_for, T_all, tol=16.0):
    """Slabs for the compare's missing visible floors: every placed mesh's LOD 0 triangles that face up (normal z >
    0.5, the compare's walkable test), lie in the box and have a missing sample in their centroid's 16 u cell within `tol` units of their height.
    Vegetation is left out (leaf cards are not floors). Map units = (game - origin) x units per metre, the compare's."""
    upm = levelport.UNITS_PER_METRE
    s = np.load(os.path.join(folder, 'samples.npz'))
    lo, (nx, ny), cell = s['lo'], s['grid'], float(s['cell'])
    miss = {}
    for c, z in zip(s['miss_cell'].tolist(), s['miss_z'].tolist()):
        miss.setdefault(c, []).append(z)
    miss = {c: np.array(v) for c, v in miss.items()}
    index = json.load(open(work / 'meshes' / 'index.json', encoding='utf-8'))
    x0, y0, x1, y1 = box
    docs = {}
    out = {'fill_tris': 0, 'fill_nodes': 0, 'fill_cells': len(miss)}
    mat = mat_for('rock')
    for pi, p in enumerate(pl['placements']):
        if p.get('kind') != 'w2mesh' or 'vegetation' in p['mesh'].lower() or 'blockout_base' in p['mesh'].lower():
            continue
        M = np.array(p['matrix'], dtype=np.float64)
        if not (x0 - 50 <= M[0, 3] <= x1 + 50 and y0 - 50 <= M[1, 3] <= y1 + 50):
            continue
        rec = index.get(p['mesh'])
        if rec is None:
            continue
        if rec['file'] not in docs:
            d = gltfio.Document(work / 'meshes' / rec['file'])
            prims = d.doc['meshes'][0]['primitives']
            parts = [(pos, tri) for pr, (pos, _n, _uv, tri, _m) in zip(prims, d.primitives(0))
                     if pr.get('extras', {}).get('lod', 0) == 0]
            docs[rec['file']] = parts
        tris = []
        for pos, tri in docs[rec['file']]:
            Wp = pos @ M[:3, :3].T + M[:3, 3]
            T = Wp[tri.reshape(-1, 3)]
            n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
            ln = np.linalg.norm(n, axis=1)
            k = (n[:, 2] > 0.5 * ln) & (ln > 1e-6)     # (the compare's walkable test: roofs a force jump reaches)
            if not k.any():
                continue
            T = T[k]
            C = (T.mean(1) - origin) * upm
            ii = np.floor((C[:, 0] - lo[0]) / cell).astype(np.int64)
            jj = np.floor((C[:, 1] - lo[1]) / cell).astype(np.int64)
            ok = (ii >= 0) & (ii < nx) & (jj >= 0) & (jj < ny)
            keep = np.zeros(len(T), bool)
            for t in np.nonzero(ok)[0]:
                zs = miss.get(int(jj[t] * nx + ii[t]))
                if zs is not None and np.abs(zs - C[t, 2]).min() <= tol:
                    keep[t] = True
            if keep.any():
                tris.append(T[keep])
        if not tris:
            continue
        T = np.concatenate(tris)
        P = (T.reshape(-1, 3) - origin) @ GAME_TO_GLTF.T
        F = np.arange(len(P)).reshape(-1, 3)
        T_all.append(P[F])
        w.node('fill%d' % pi, mesh=w.mesh('fill%d' % pi, [{'position': P, 'indices': F, 'material': mat}]),
               parent=g_col, extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['solid'], 'thickness': 8.0,
                                     'jka_material': 'rock'})
        out['fill_tris'] += len(F)
        out['fill_nodes'] += 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--work', required=True)
    ap.add_argument('--name', default='castle')
    ap.add_argument('--tag', default='1')
    ap.add_argument('--box', type=float, nargs=4, required=True, help='playable box, game metres: minx miny maxx maxy')
    ap.add_argument('--origin', type=float, nargs=3, help='game point at the map origin (default: the box centre, z 0)')
    ap.add_argument('--spawn-centre', type=float, nargs=2)
    ap.add_argument('--spawn-radius', type=float, default=60.0)
    ap.add_argument('--spawns', type=int, default=16)
    ap.add_argument('--spawn-sky', type=float, default=6.0, help='metres clear above a spawn')
    ap.add_argument('--spawn-above-water', type=float, default=0.5,
                    help="metres a spawn's ground stands above the game's water there (w3water.water_level: "
                         "<work>/water_shapes.json's shapes, the sea elsewhere)")
    ap.add_argument('--terrain-step', type=int, default=2, help='terrain collision every Nth vertex (~1 m apart)')
    ap.add_argument('--terrain-tol', type=float, default=0.15,
                    help='metres: a terrain cell splits (down to every vertex) where its triangles miss the full '
                         'heightfield by more; 0 or less: the plain every-Nth-vertex grid')
    ap.add_argument('--terrain-material', default='dirt')
    ap.add_argument('--top', type=float, help='lid height, game metres (default: highest collision + 40)')
    ap.add_argument('--light-scale', type=float, default=1.0)
    ap.add_argument('--fill', help="a levelcheck compare folder of this map and its scene (compare --zmax 1e5): its "
                                   "missing visible floors get slabs from the render meshes")
    a = ap.parse_args()

    work = Path(a.work)
    x0, y0, x1, y1 = a.box
    origin = np.array(a.origin if a.origin else [(x0 + x1) / 2, (y0 + y1) / 2, 0.0])

    def g(p):
        return (np.asarray(p, dtype=np.float64) - origin) @ GAME_TO_GLTF.T

    col = json.load(open(work / 'collision.json', encoding='utf-8'))['shapes']
    pl = json.load(open(work / 'placements.json', encoding='utf-8'))
    w = gltfio.Writer()
    g_col = w.node('collision')
    g_ent = w.node('entities')
    mats = {}

    def mat_for(jm):
        key = 'col_' + (jm or 'none')
        if key not in mats:
            mats[key] = w.material(key, extras={'surfaceparms': []})
        return mats[key]

    stats = {'shapes_in': len(col), 'brushes': 0, 'slab_nodes': 0, 'outside_box': 0, 'terrain_tris': 0,
             'terrain_hole_tris': 0}
    missing = set()
    T_all = []                           # every collision triangle, glTF metres, for spawns
    zmax = -1e9
    for i, s in enumerate(col):
        V = np.array(s['verts'], dtype=np.float64)
        if V[:, 0].max() < x0 or V[:, 0].min() > x1 or V[:, 1].max() < y0 or V[:, 1].min() > y1:
            stats['outside_box'] += 1
            continue
        zmax = max(zmax, float(V[:, 2].max()))
        jm = jka_material(s.get('mat'), missing)
        kind = s['kind']
        if kind in ('sphere', 'capsule'):
            c = V.mean(0)
            V = V[np.unique(np.argmax((V - c) @ DIRS.T, axis=0))]
        P = g(V)
        if kind == 'trimesh':
            F = tri_faces(s['faces'])
            extras = {'lp': 'collision', 'shape': 'mesh', 'contents': ['solid'], 'thickness': 8.0}
            stats['slab_nodes'] += 1
        else:
            try:
                F = hull_tris(P)
            except Exception:  # noqa: BLE001 - a flat or degenerate shape
                stats['degenerate'] = stats.get('degenerate', 0) + 1
                continue
            # detail: thousands of rotated, overlapping props as structural brushes kept q3map2 -bsp past 12 min
            # (castle-1); the playerclip ring seals the map and vis is off
            extras = {'lp': 'collision', 'contents': ['solid', 'detail']}
            stats['brushes'] += 1
        if jm:
            extras['jka_material'] = jm
        T_all.append(P[F])
        w.node('c%d' % i, mesh=w.mesh('c%d' % i, [{'position': P, 'indices': F, 'material': mat_for(jm)}]),
               parent=g_col, extras=extras)

    # terrain: the grid inside the box (plus a step), as slabs in 32 m cells; triangles over the game's terrain holes
    # (control word 0: under the castle's buildings and the tower cell, where its interiors are) left out
    tmin = 1e9
    jm = a.terrain_material
    k = 0
    st = a.terrain_step
    for key, Gfull, props, lattice, bounds in terrain_tiles(work):
        adaptive = a.terrain_tol > 0 and all((d - 1) % st == 0 for d in Gfull.shape[:2])
        if adaptive:
            cells = np.array(terrain_cells(Gfull[..., 2], st, a.terrain_tol), dtype=np.int64).reshape(-1, 3)
            stats['terrain_cells'] = stats.get('terrain_cells', {})
            for s_, c_ in zip(*np.unique(cells[:, 2], return_counts=True)):
                stats['terrain_cells'][str(s_)] = stats['terrain_cells'].get(str(s_), 0) + int(c_)
            G = Gfull
            span = 16 * st                  # a node per 16 step-sized cells, as the plain grid
        else:
            G = Gfull[::st, ::st]
            span = 16
        n, m = G.shape[:2]
        for i0 in range(0, n - 1, span):
            for j0 in range(0, m - 1, span):
                S = G[i0:i0 + span + 1, j0:j0 + span + 1]
                if S[..., 0].max() < x0 or S[..., 0].min() > x1 or S[..., 1].max() < y0 or S[..., 1].min() > y1:
                    continue
                sn, sm = S.shape[:2]
                if adaptive:
                    k_ = cells[(cells[:, 0] >= i0) & (cells[:, 0] < i0 + span) &
                               (cells[:, 1] >= j0) & (cells[:, 1] < j0 + span)]
                    r_, c_, z_ = k_[:, 0] - i0, k_[:, 1] - j0, k_[:, 2]
                    qa, qb = r_ * sm + c_, r_ * sm + c_ + z_
                    qc, qd = (r_ + z_) * sm + c_ + z_, (r_ + z_) * sm + c_
                    F = np.concatenate([np.stack([qa, qb, qc], 1), np.stack([qa, qc, qd], 1)])
                else:
                    ii, jj = np.meshgrid(np.arange(sn - 1), np.arange(sm - 1), indexing='ij')
                    q = (ii * sm + jj).ravel()
                    # rows run along +X and step +Y: (q, q+1, q+sm+1) winds counter-clockwise seen from above
                    F = np.concatenate([np.stack([q, q + 1, q + sm + 1], 1), np.stack([q, q + sm + 1, q + sm], 1)])
                V = S.reshape(-1, 3)
                if lattice is not None:
                    keep = solid_share(lattice, bounds, V[F].mean(1)[:, :2]) >= 0.5
                    stats['terrain_hole_tris'] += int((~keep).sum())
                    F = F[keep]
                if not len(F):
                    continue
                used, F = np.unique(F, return_inverse=True)
                F = F.reshape(-1, 3)
                V = V[used]
                P = g(V)
                tmin = min(tmin, float(V[:, 2].min()))
                T_all.append(P[F])
                stats['terrain_tris'] += len(F)
                w.node('terrain%d' % k, mesh=w.mesh('terrain%d' % k, [{
                    'position': P, 'indices': F, 'material': mat_for(jm)}]), parent=g_col,
                    extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['solid'], 'thickness': 16.0,
                            'jka_material': jm})
                k += 1
    stats['terrain_nodes'] = k

    # --fill: floors the game draws but gives no collision (balcony, wall and column tops Geralt never reaches; a
    # force jump does): levelcheck compare's missing visible surfaces, filled with the render meshes' up-facing LOD 0
    # triangles that lie on them, as slabs (castle-5 against scene-11: 664 m2 a player fell through inside the box)
    if a.fill:
        stats.update(fill_from_compare(a.fill, work, pl, origin, (x0, y0, x1, y1), w, g_col, mat_for, T_all))

    # boundary: playerclip walls around the box and a lid
    top = a.top if a.top is not None else zmax + 40.0
    floor = (tmin if tmin < 1e9 else 0.0) - 20.0
    half = ((x1 - x0) / 2, (y1 - y0) / 2)
    centre = np.array([(x0 + x1) / 2, (y0 + y1) / 2])
    clip = mat_for('clip')
    for i, box in enumerate(box_ring_boxes(half, floor, top)):
        box = box + np.r_[centre, 0.0]
        w.node('clip%d' % i, mesh=w.mesh('clip%d' % i, [{'position': g(box), 'indices': BOX_FACES,
                                                          'material': clip}]),
               parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    hx, hy = half[0] + 3.0, half[1] + 3.0
    lid = np.array([[centre[0] + sx * hx, centre[1] + sy * hy, z] for z in (top, top + 1.0)
                    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    w.node('lid', mesh=w.mesh('lid', [{'position': g(lid), 'indices': BOX_FACES, 'material': clip}]),
           parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    stats['top'] = round(top, 1)
    stats['floor'] = round(floor, 1)

    # lights: the game's radius is the reach; q3map2 linear falloff reaches about `light` units
    upm = levelport.UNITS_PER_METRE
    nl = 0
    for i, L in enumerate(pl.get('lights', [])):
        M = np.array(L['matrix'])
        p = M[:3, 3]
        if not (x0 <= p[0] <= x1 and y0 <= p[1] <= y1):
            continue
        radius = float(L['props'].get('witcher_light_radius') or 5.0)
        if not L['props'].get('witcher_light_enabled', True):
            continue
        c = L['color']
        w.node('light%d' % i, translation=g(p), parent=g_ent, extras={
            'lp': 'entity', 'classname': 'light', 'light': '%d' % round(radius * upm * a.light_scale),
            '_color': '%.3f %.3f %.3f' % tuple(c[:3]), 'spawnflags': '1'})
        nl += 1
    stats['lights'] = nl

    # spawns: up-facing collision near the spawn centre under open sky, spread out
    T = np.concatenate(T_all)
    sc = np.array(a.spawn_centre if a.spawn_centre else centre)
    scg = g([sc[0], sc[1], 0.0])[[0, 2]]
    nrm = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    area = np.linalg.norm(nrm, axis=1)
    up = (nrm[:, 1] / np.maximum(area, 1e-9) > 0.9) & (area > 0.5)
    cand = T[up].mean(1)
    cand = cand[np.hypot(cand[:, 0] - scg[0], cand[:, 2] - scg[1]) < a.spawn_radius]
    # not under the game's water (its water shapes' levels, the sea elsewhere): a harbour riverbed spawned players
    # 8 m under the surface. Candidates are glTF (x, z, -y) about the origin; game metres back
    import w3water
    wl = w3water.water_level(w3water.load_shapes(work), cand[:, 0] + origin[0], origin[1] - cand[:, 2])
    dry = cand[:, 1] + origin[2] >= wl + a.spawn_above_water
    stats['spawn_wet_candidates'] = int((~dry).sum())
    cand = cand[dry]
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
    stats['collision_tris'] = int(len(T))

    out = work / ('%s-%s.glb' % (a.name, a.tag))
    w.save(out, extras={'levelport': {'version': 1, 'units_per_metre': upm, 'source_game': 'witcher_3',
                                      'source': '%s box %s origin %s' % (a.name, a.box, origin.tolist()),
                                      'worldspawn': {'message': 'Beauclair: %s' % a.name}}})
    stats['missing_materials'] = sorted(missing)
    with open(work / ('%s-%s.build.json' % (a.name, a.tag)), 'w') as fp:
        json.dump({'box': a.box, 'origin': origin.tolist(), **stats}, fp, indent=1)
    print(f'w3build -> {out} ({out.stat().st_size / 1e6:.1f} MB)')
    print('  ' + ', '.join(f'{k} {v}' for k, v in stats.items()))


if __name__ == '__main__':
    main()
