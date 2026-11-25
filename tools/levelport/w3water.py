"""Witcher 3 water as the render scene draws it (w3scene.py), from the terrain and the floating plants.

The game's water in the exported data: the global water at sea level (z 0; the world's per-tile minWaterHeight values
are depth bounds at or under 0, not levels), lakes above it with no shape in the layers (the palace's swan lake at
~80.5 m: no water component, its level only shown by what floats on it), and a few water meshes (fountain basins,
the water under the palace bridge, cave pools). This module finds the surfaces:

- sea: every terrain cell (near tiles at 2 grid steps, the far ring at its own) with a corner under 0;
- lakes: water lilies (nymphaea*.srt) grouped within 40 m (3 or more); a group's level is its median height; the near
  terrain under that level flooded from the group (4-neighbour, cells with a corner under the level and one above
  level - MAX_DEPTH_M), held within REACH_M of one of its lilies. (Beauclair's upper river and palace lakes, 80.2-80.8 m, drain over the waterfall dam,
  a mesh: flooded freely the level ran on down the valley.)

Each surface is a list of rectangles (x0, y0, x1, y1, game metres) at its level: one per run of wet cells in a row.
    python tools/levelport/w3water.py <work folder>     (a report: the bodies, their levels and areas)
"""
import ntpath
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
SEA = 0.0
LILY = ('nymphaea',)
WATER_MESHES = ('cave_water', 'castle_fountain', 'bridge_water')     # (w3scene.py's water mesh placements)
GROUP_M = 40.0
REACH_M = 900.0
MAX_DEPTH_M = 12.0
CORE_M = 1.2
SHORE_M = 3.0
POND_M = 6.0
DROWN_MAX = 100
LAND_CLEAR_M = 1.5
MIN_LILIES = 3


def runs(wet, xs, ys):
    """Row runs of wet cells as rectangles: wet (rows, cols) over cell corners xs (cols + 1), ys (rows + 1)."""
    out = []
    for i in range(wet.shape[0]):
        row = wet[i]
        if not row.any():
            continue
        d = np.diff(np.r_[0, row.astype(np.int8), 0])
        for j0, j1 in zip(np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]):
            out.append((float(xs[j0]), float(ys[i]), float(xs[j1]), float(ys[i + 1])))
    return out


SHAPE_STEP = 2.0     # m: the shapes' water cells


def load_shapes(work):
    """The game's local water shapes (w3watershapes.py's <work>/water_shapes.json), each with its triangulation;
    [] without the file"""
    f = Path(work) / 'water_shapes.json'
    if not f.exists():
        return []
    import json
    out = []
    for s in json.load(open(f, encoding='utf-8'))['shapes']:
        P = np.array(s['points'], np.float64)
        if len(P) > 3 and np.allclose(P[0, :2], P[-1, :2]):
            P = P[:-1]
        out.append(dict(s, P=P, tris=ear_clip(P[:, :2])))
    return out


def ear_clip(xy):
    """The triangles (index triples) of a simple polygon"""
    n = len(xy)
    idx = list(range(n))
    area = 0.5 * np.sum(xy[:, 0] * np.roll(xy[:, 1], -1) - np.roll(xy[:, 0], -1) * xy[:, 1])
    if area < 0:
        idx.reverse()

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    tris, guard = [], 0
    while len(idx) > 3 and guard < 10 * n * n:
        guard += 1
        m = len(idx)
        for k in range(m):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % m]
            a, b, c = xy[i0], xy[i1], xy[i2]
            if cross(a, b, c) <= 1e-9:
                continue
            inside = False
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = xy[j]
                if cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0:
                    inside = True
                    break
            if not inside:
                tris.append((i0, i1, i2))
                idx.pop(k)
                break
        else:
            break       # (degenerate: the rest as a fan)
    for k in range(1, len(idx) - 1):
        tris.append((idx[0], idx[k], idx[k + 1]))
    return np.array(tris, np.int64).reshape(-1, 3)


def in_shapes(shapes, X, Y):
    from matplotlib.path import Path as MPath
    pts = np.c_[np.ravel(X), np.ravel(Y)]
    m = np.zeros(len(pts), bool)
    for s in shapes:
        P = s['P']
        lo, hi = P[:, :2].min(0), P[:, :2].max(0)
        c = ~m & (pts[:, 0] >= lo[0]) & (pts[:, 0] <= hi[0]) & (pts[:, 1] >= lo[1]) & (pts[:, 1] <= hi[1])
        if c.any():
            m[c] = MPath(P[:, :2]).contains_points(pts[c])
    return m.reshape(np.shape(X))


def shape_level(s, X, Y):
    """A shape's water level at points (its triangulation, linear in each triangle; a point outside every triangle
    takes the nearest point's level)"""
    P, T = s['P'], s['tris']
    X, Y = np.ravel(X), np.ravel(Y)
    L = np.full(len(X), np.nan)
    if s.get('flat'):
        L[:] = P[0, 2]
        return L
    for a, b, c in T:
        A, B, C = P[a], P[b], P[c]
        d = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1])
        if abs(d) < 1e-12:
            continue
        w0 = ((B[1] - C[1]) * (X - C[0]) + (C[0] - B[0]) * (Y - C[1])) / d
        w1 = ((C[1] - A[1]) * (X - C[0]) + (A[0] - C[0]) * (Y - C[1])) / d
        w2 = 1 - w0 - w1
        ok = np.isnan(L) & (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        L[ok] = w0[ok] * A[2] + w1[ok] * B[2] + w2[ok] * C[2]
    miss = np.isnan(L)
    if miss.any():
        k = np.argmin((X[miss, None] - P[None, :, 0]) ** 2 + (Y[miss, None] - P[None, :, 1]) ** 2, axis=1)
        L[miss] = P[k, 2]
    return L


def water_level(shapes, X, Y):
    """The game's water level at points: a shape's level inside it (the first that holds the point), else the sea's.
    (For spawns and checks: a point under it is under water. The lily lakes, outside every shape, are not counted.)"""
    X, Y = np.ravel(np.asarray(X, np.float64)), np.ravel(np.asarray(Y, np.float64))
    L = np.full(len(X), SEA)
    done = np.zeros(len(X), bool)
    for s in shapes:
        m = ~done & in_shapes([s], X, Y)
        if m.any():
            L[m] = shape_level(s, X[m], Y[m])
            done |= m
    return L


def height_sampler(tiles):
    """Terrain height at points (the near tiles before the far ring; nearest grid point; +inf off the terrain)"""
    order = sorted(tiles, key=lambda t: t[-1])

    def h(X, Y):
        X, Y = np.ravel(X), np.ravel(Y)
        Z = np.full(len(X), np.inf)
        for key, G, props, lattice, b, far in order:
            m = np.isinf(Z) & (X >= b[0]) & (X <= b[2]) & (Y >= b[1]) & (Y <= b[3])
            if not m.any():
                continue
            gx0, gy0 = G[0, 0, 0], G[0, 0, 1]
            dx, dy = G[0, 1, 0] - gx0, G[1, 0, 1] - gy0
            j = np.clip(np.rint((X[m] - gx0) / dx).astype(int), 0, G.shape[1] - 1)
            i = np.clip(np.rint((Y[m] - gy0) / dy).astype(int), 0, G.shape[0] - 1)
            Z[m] = G[i, j, 2]
        return Z
    return h


def shape_quads(tiles, shapes, step=SHAPE_STEP):
    """Each shape's water: cells of `step` inside it whose ground (a corner or the centre) lies under its level there,
    as (x0, y0, x1, y1, z at (x0,y0), (x1,y0), (x1,y1), (x0,y1)) rows, a flat run of cells merged into one. Not
    flooded from anywhere: the game draws the shape's surface wherever it is over the ground (the depth test hides it
    under the ground), so nothing in the water (a boat's hull, a jetty's posts) holds it back.
    Returns [(level or None for sloped, quads (n, 8), shape)]."""
    h = height_sampler(tiles)
    out = []
    for s in shapes:
        P = s['P']
        lo, hi = np.floor(P[:, :2].min(0) / step) * step, np.ceil(P[:, :2].max(0) / step) * step
        xs = np.arange(lo[0], hi[0] + step / 2, step)
        ys = np.arange(lo[1], hi[1] + step / 2, step)
        if len(xs) < 2 or len(ys) < 2:
            continue
        GX, GY = np.meshgrid(xs, ys)
        Hc = h(GX, GY).reshape(GX.shape)                         # ground at the corners
        cx, cy = (GX[:-1, :-1] + GX[1:, 1:]) / 2, (GY[:-1, :-1] + GY[1:, 1:]) / 2
        Hm = h(cx, cy).reshape(cx.shape)
        Lc = shape_level(s, GX, GY).reshape(GX.shape)           # level at the corners
        Lm = shape_level(s, cx, cy).reshape(cx.shape)
        low = np.minimum(np.minimum(Hc[:-1, :-1], Hc[1:, :-1]), np.minimum(Hc[:-1, 1:], Hc[1:, 1:]))
        wet = in_shapes([s], cx, cy) & ((np.minimum(low, Hm) < Lm))
        if not wet.any():
            continue
        if s.get('flat'):
            R = np.array(runs(wet, xs, ys)).reshape(-1, 4)
            Q = np.c_[R, np.full((len(R), 4), P[0, 2])]
            out.append((float(P[0, 2]), Q, s))
            continue
        i, j = np.nonzero(wet)
        z = np.stack([Lc[i, j], Lc[i, j + 1], Lc[i + 1, j + 1], Lc[i + 1, j]], 1)
        Q = np.c_[xs[j], ys[i], xs[j + 1], ys[i + 1], z]
        # merge row runs of cells whose four corners share one level (most of a river is flat between its points)
        flat = np.ptp(z, axis=1) < 1e-4
        keep = [Q[~flat]]
        if flat.any():
            F = Q[flat]
            order = np.lexsort((F[:, 0], F[:, 4], F[:, 1]))
            F = F[order]
            merged, cur = [], F[0].copy()
            for r in F[1:]:
                if r[1] == cur[1] and r[4] == cur[4] and abs(r[0] - cur[2]) < 1e-6:
                    cur[2] = r[2]
                else:
                    merged.append(cur)
                    cur = r.copy()
            merged.append(cur)
            keep.append(np.array(merged))
        out.append((None, np.concatenate(keep), s))
    return out


def sea_rects(tiles, near_step=2, far_step=8, shapes=None):
    """Rectangles at sea level over every tile's cells with a corner under it, except inside a local water shape
    (shapes: load_shapes()): the shape sets the level there (the game's inland hollows under the sea's level are
    shapes with a level under their ground: dry)."""
    out = []
    for key, G, props, lattice, bounds, far in tiles:
        g = G[::far_step, ::far_step] if far else G[::near_step, ::near_step]
        z = g[..., 2]
        low = np.minimum(np.minimum(z[:-1, :-1], z[1:, :-1]), np.minimum(z[:-1, 1:], z[1:, 1:]))
        wet = low < SEA
        if shapes and wet.any():
            cx = (g[:-1, :-1, 0] + g[1:, 1:, 0]) / 2
            cy = (g[:-1, :-1, 1] + g[1:, 1:, 1]) / 2
            wet &= ~in_shapes(shapes, cx, cy)
        if wet.any():
            out += runs(wet, g[0, :, 0], g[:, 0, 1])
    return out


def near_raster(tiles):
    """The near tiles' heights as one grid: (Z (rows y, cols x), x of each column, y of each row)."""
    near = [t for t in tiles if not t[-1]]
    if not near:
        raise SystemExit('w3water: the lakes need the near terrain (<work>/terrain, w3terrain.py); this work folder has '
                         'none (w3scene --no-water leaves the water out)')
    cell = float(near[0][1][0, 1, 0] - near[0][1][0, 0, 0])
    x0 = min(t[4][0] for t in near)
    y0 = min(t[4][1] for t in near)
    x1 = max(t[4][2] for t in near)
    y1 = max(t[4][3] for t in near)
    nx, ny = int(round((x1 - x0) / cell)) + 1, int(round((y1 - y0) / cell)) + 1
    Z = np.full((ny, nx), np.inf)
    for key, G, props, lattice, bounds, far in near:
        j = int(round((G[0, 0, 0] - x0) / cell))
        i = int(round((G[0, 0, 1] - y0) / cell))
        Z[i:i + G.shape[0], j:j + G.shape[1]] = G[..., 2]
    return Z, x0 + cell * np.arange(nx), y0 + cell * np.arange(ny), cell


WALL_BAND = (-0.3, 0.8)     # m under and over a level: geometry crossing it holds the water back


def wall_cells(walls, level, shape, x0, y0, cell):
    """Cells under the walls at a level: walls (n, 6) the triangles' boxes (x0, y0, x1, y1, z0, z1, game metres) of the
    scene's solid meshes (w3scene.py); every box crossing level + WALL_BAND marks the cells it covers. The terrain
    alone let a raised lily pool's water run out past its rim (a mesh) and hang off the cliff below it."""
    m = np.zeros(shape, bool)
    W = walls[(walls[:, 4] <= level + WALL_BAND[1]) & (walls[:, 5] >= level + WALL_BAND[0])]
    j0 = np.clip(((W[:, 0] - x0) / cell).astype(int), 0, shape[1] - 1)
    j1 = np.clip(((W[:, 2] - x0) / cell).astype(int), 0, shape[1] - 1)
    i0 = np.clip(((W[:, 1] - y0) / cell).astype(int), 0, shape[0] - 1)
    i1 = np.clip(((W[:, 3] - y0) / cell).astype(int), 0, shape[0] - 1)
    for a, b, c, d in zip(i0, i1, j0, j1):
        m[a:b + 1, c:d + 1] = True
    return m


def lily_groups(placements):
    pts = np.array([np.array(p['matrix'])[:3, 3] for p in placements
                    if p.get('kind') == 'w2mesh' and any(k in ntpath.basename(p['mesh']).lower() for k in LILY)])
    groups = []
    left = list(range(len(pts)))
    while left:
        g = [left.pop(0)]
        k = 0
        while k < len(g):
            near = [i for i in left if np.linalg.norm(pts[i, :2] - pts[g[k], :2]) < GROUP_M]
            for i in near:
                left.remove(i)
            g += near
            k += 1
        P = pts[g]
        groups.append({'centre': P[:, :2].mean(0), 'level': float(np.median(P[:, 2])), 'count': len(g), 'pts': P})
    return groups


def lake_rects(tiles, placements, walls=None, shapes=None):
    """The lakes: (level, rectangles, report) per body. Each lily group (most lilies first) floods; one that overlaps a
    body within MERGE_M of its level joins it (one river at slightly different heights along its course: the body takes
    the median of all its lilies), one that overlaps a body at another level is dropped (a raised pool: Beauclair's
    fountain basin with lilies at 83 m, drawn by its own water mesh, flooded the river's banks 2.5 m deep)."""
    groups = [g for g in lily_groups(placements) if g['count'] >= MIN_LILIES]
    groups.sort(key=lambda g: -g['count'])
    bodies, dropped = [], []
    # (lilies in a basin that has its own water mesh: that mesh is the water; a second surface 4 cm off it would fight)
    meshed = np.array([np.array(p['matrix'])[:3, 3] for p in placements if p.get('kind') == 'w2mesh'
                       and any(k in p['mesh'].lower() for k in WATER_MESHES)]).reshape(-1, 3)
    keep = []
    for g in groups:
        if len(meshed) and np.min(np.linalg.norm(meshed[:, :2] - g['centre'], axis=1)) < 10.0:
            dropped.append(dict(g, note='a water mesh there'))
        else:
            keep.append(g)
    groups = keep
    if shapes:
        # (lilies in a game water shape float on that shape's water: w3scene draws the shape, shape_quads)
        keep = []
        for g in groups:
            if in_shapes(shapes, g['pts'][:, 0], g['pts'][:, 1]).mean() > 0.5:
                dropped.append(dict(g, note='in a water shape'))
            else:
                keep.append(g)
        groups = keep
    for L, body, g in _flood(tiles, groups, walls, land_plants(placements)):
        if body is None:
            dropped.append(dict(g, note='no wet cell under the lilies'))
            continue
        hit = [b for b in bodies if (b['mask'] & body).any()]
        if not hit:
            bodies.append({'mask': body, 'pts': [g['pts']], 'groups': 1})
        elif all(abs(L - float(np.median(np.concatenate(b['pts'])[:, 2]))) < MERGE_M for b in hit):
            b = hit[0]
            b['mask'] |= body
            b['pts'].append(g['pts'])
            b['groups'] += 1
        else:
            dropped.append(dict(g, note='overlaps water at another level'))
    Z, xs, ys, cell = near_raster(tiles)
    out = []
    for b in bodies:
        P = np.concatenate(b['pts'])
        L = float(np.median(P[:, 2]))
        out.append((L, runs(b['mask'], xs, ys), {'count': len(P), 'centre': P[:, :2].mean(0), 'groups': b['groups'],
                                                  'area': float(b['mask'].sum()) * cell * cell}))
    return out, dropped


MERGE_M = 1.0


def land_plants(placements):
    """Where land plants stand (trees, shrubs, flowers, grass: SpeedTree or vegetation meshes, not lilies, reeds or
    underwater plants): the game grows none under water, so a flood that would cover them is covering land."""
    P = [np.array(p['matrix'])[:3, 3] for p in placements if p.get('kind') == 'w2mesh'
         and (p.get('srt_source') or 'vegetation' in p['mesh'].lower())
         and not any(k in p['mesh'].lower() for k in LILY + ('underwater', 'reed', 'typha', 'water'))]
    return np.array(P).reshape(-1, 3)


def _flood(tiles, groups, walls=None, land=None):
    from scipy import ndimage
    Z, xs, ys, cell = near_raster(tiles)
    low = np.minimum(np.minimum(Z[:-1, :-1], Z[1:, :-1]), np.minimum(Z[:-1, 1:], Z[1:, 1:]))
    for g in groups:
        L = g['level']
        # (under the level, and no deeper than MAX_DEPTH_M: Beauclair's river beds lie 2-10 m under 80.5 m, the valley
        # past the waterfall dam 20 m and more, so the flood stops at the dam)
        high = np.maximum(np.maximum(Z[:-1, :-1], Z[1:, :-1]), np.maximum(Z[:-1, 1:], Z[1:, 1:]))
        wet = (low < L) & (high > L - MAX_DEPTH_M)
        if walls is not None:
            wet &= ~wall_cells(walls, L, low.shape, xs[0], ys[0], cell)
        mark = np.ones(low.shape, bool)
        for x, y, _z in g['pts']:
            mark[int(np.clip((y - ys[0]) / cell, 0, low.shape[0] - 1)), int(np.clip((x - xs[0]) / cell, 0, low.shape[1] - 1))] = False
        dist = ndimage.distance_transform_edt(mark) * cell
        wet &= dist <= REACH_M
        # the water itself: the cells at least CORE_M under the level (river beds, lake basins) joined to the lilies,
        # then SHORE_M more to reach the bank; a shallow pond with no such core: what lies within POND_M of its lilies.
        # (Under the level is not enough: the formal lily pool's garden lies under it too, its flower beds and a tree
        # stood in the water, and the game's own pond shape is not in the export)
        core = wet & (high < L - CORE_M)
        lab, _ = ndimage.label(core)
        seeds = set()
        for x, y, _z in g['pts']:
            j = int(np.clip((x - xs[0]) / cell, 0, low.shape[1] - 1))
            i = int(np.clip((y - ys[0]) / cell, 0, low.shape[0] - 1))
            w = lab[max(0, i - 5):i + 6, max(0, j - 5):j + 6]
            seeds |= set(int(v) for v in np.unique(w) if v)
        if seeds:
            body = ndimage.binary_dilation(np.isin(lab, list(seeds)), iterations=max(1, int(round(SHORE_M / cell)))) & wet
            body |= wet & (dist <= POND_M)     # (the shallows the lilies float in)
        else:
            body = wet & (dist <= POND_M)
        if land is not None and len(land) and body.any():
            # a flood that drowns land plants covers land, not water (Beauclair's formal lily pool and its garden: 745
            # plants, a river body 28-63): only the water about the lilies, clear of where the plants stand
            j = ((land[:, 0] - xs[0]) / cell).astype(int)
            i = ((land[:, 1] - ys[0]) / cell).astype(int)
            ok = (j >= 0) & (i >= 0) & (j < body.shape[1]) & (i < body.shape[0])
            ok[ok] = body[i[ok], j[ok]]
            drowned = ok & (land[:, 2] < L - 0.15)
            g['drowned'] = int(drowned.sum())
            if g['drowned'] > DROWN_MAX:
                clear = np.ones(body.shape, bool)
                clear[i[drowned], j[drowned]] = False
                # (and no deeper than the ground under the lilies, half a metre spare: the formal pool is a shallow
                # hollow at a cliff's edge, and its front lilies' reach hung a stepped sheet over the drop)
                li = np.clip(((g['pts'][:, 1] - ys[0]) / cell).astype(int), 0, low.shape[0] - 1)
                lj = np.clip(((g['pts'][:, 0] - xs[0]) / cell).astype(int), 0, low.shape[1] - 1)
                deep = float(np.percentile(L - low[li, lj], 75)) + 0.5     # (a few lilies at the rim lie over the drop)
                body = wet & (dist <= POND_M) & (low > L - deep) & \
                    (ndimage.distance_transform_edt(clear) * cell > LAND_CLEAR_M)
                g['pond'] = True
        yield L, (body if body.any() else None), g


def main():
    import json
    from w3build import terrain_tiles
    work = Path(sys.argv[1])
    tiles = [t + (False,) for t in terrain_tiles(work)]
    keys = {t[0] for t in tiles}
    tiles += [t + (True,) for t in terrain_tiles(work, 'terrain_far') if t[0] not in keys]
    pl = json.load(open(work / 'placements.json', encoding='utf-8'))['placements']
    sea = sea_rects(tiles)
    print('sea: %d rectangles, %.0f m2' % (len(sea), sum((r[2] - r[0]) * (r[3] - r[1]) for r in sea)))
    lakes, dropped = lake_rects(tiles, pl)
    for L, rects, rep in lakes:
        print('lake at %.2f m: %d lilies in %d groups about (%.0f, %.0f), %d rectangles, %.0f m2' % (
            L, rep['count'], rep['groups'], rep['centre'][0], rep['centre'][1], len(rects), rep['area']))
    for g in dropped:
        print('dropped: %d lilies at %.2f m about (%.0f, %.0f): %s' % (g['count'], g['level'], *g['centre'], g['note']))


if __name__ == '__main__':
    main()
