"""Coplanar overlaps (z-fighting) between placements of a tiled scene part.

Reads a scene tile part (tiles/<tag>/scene/<tile>/arrays.npz + part.json, stage_scene_tile). Every placement's
finest LOD, opaque and emissive surfaces only (cutout and blended ones are drawn after and over; mesh decals go
through the DBuffer), triangles of at least --min-tri m2; two placements are compared only where their draw ranges
(inst_near to inst_hide) overlap, so a proxy and the detail it replaces never count. Triangles are bucketed by their world plane (normal to
--angle degrees, offset to --gap metres, both ways across a bucket edge, the normal's too); two placements fight where same-facing
triangles of theirs in one bucket overlap. The overlap is estimated from sample points of each triangle (its centroid
and edge midpoints) falling inside the other placement's triangles. A pair of the same mesh at the same place is a
duplicate the tile split should have dropped (stage_tiles).

    python tools/levelport/zfight.py <work>/tiles/8/scene/-7_4 [--out report.json]

Prints the pairs by overlap area (m2) and writes them as JSON: placement indices and names, meshes, materials,
CP2077 position. Report only: what to do with a pair (drop one, offset one, leave it) is decided from the report.
"""
import argparse, json, math, sys
from collections import defaultdict
from pathlib import Path
import numpy as np

KIND = {0: 'opaque', 1: 'cutout', 2: 'blend', 3: 'emissive'}
NBIN = 4            # a normal bin is this many --angle wide


def load(part):
    z = np.load(part / 'arrays.npz', allow_pickle=True)
    meta = json.load(open(part / 'part.json', encoding='utf-8'))
    return z, meta


def mesh_tris(z, meta, mi, min_area_u2, cache):
    """A mesh's finest LOD as local triangles (k, 3, 3), their material indices; drawn-solid surfaces only."""
    if mi in cache:
        return cache[mi]
    V, I = z['V']['pos'], z['I']
    fl = int(z['meshes'][mi][4])
    fp, npart, _ = z['lods'][fl]
    T, M = [], []
    for fi, ni, vo, m in z['parts'][int(fp):int(fp) + int(npart)]:
        kind = meta['mnames'][m]['kind'] if m < len(meta['mnames']) else 'opaque'
        if kind not in ('opaque', 'emissive'):
            continue
        t = V[I[fi:fi + ni].reshape(-1, 3) + vo].astype(np.float64)
        a = 0.5 * np.linalg.norm(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]), axis=1)
        keep = a >= min_area_u2
        T.append(t[keep])
        M.append(np.full(keep.sum(), m))
    cache[mi] = (np.concatenate(T) if T else np.zeros((0, 3, 3)), np.concatenate(M) if M else np.zeros(0, int))
    return cache[mi]


def inside(tri2, P):
    """Points P (n, 2) inside the 2-D triangle tri2 (3, 2), either winding."""
    a, b, c = tri2

    def side(u, v):
        return (v[0] - u[0]) * (P[:, 1] - u[1]) - (v[1] - u[1]) * (P[:, 0] - u[0])
    d1, d2, d3 = side(a, b), side(b, c), side(c, a)
    return ((d1 >= 0) & (d2 >= 0) & (d3 >= 0)) | ((d1 <= 0) & (d2 <= 0) & (d3 <= 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('part', type=Path)
    ap.add_argument('--upm', type=float, default=37.5, help='game units a metre (the part is in game units)')
    ap.add_argument('--min-tri', type=float, default=0.005, help='smallest triangle considered (m2)')
    ap.add_argument('--gap', type=float, default=0.005, help='planes this close (m) count as one')
    ap.add_argument('--angle', type=float, default=1.0, help='normals this close (degrees) count as one')
    ap.add_argument('--min-overlap', type=float, default=0.02, help='report pairs overlapping at least this (m2)')
    ap.add_argument('--out', type=Path)
    ap.add_argument('--top', type=int, default=40)
    a = ap.parse_args()
    z, meta = load(a.part)
    upm = a.upm
    W, IM = z['inst_W'], z['inst_mesh']
    # draw ranges (game units): a proxy drawn from `near` out never fights the detail it stands in for, which hides
    # inside that distance (the first run on one tile was led by *_mproxy and prx pairs); hide 0 = no limit
    NEAR = z['inst_near'].astype(np.float64) if 'inst_near' in z.files else np.zeros(len(W))
    FAR = z['inst_hide'].astype(np.float64) if 'inst_hide' in z.files else np.zeros(len(W))
    FAR = np.where(FAR > 0, FAR, np.inf)
    names = meta.get('placed_name', [])
    cache = {}
    # normal bins NBIN times the angle wide; a triangle within one angle of a bin's edge (its normal's component a
    # quarter bin or less from it) goes in the bin across that edge too, as a candidate only: two nearly parallel
    # surfaces either side of an edge then meet in one bucket (with bins one angle wide and no
    # neighbours, they never reached the angle test)
    nq = 1.0 / (NBIN * math.radians(a.angle))
    dq = upm * a.gap
    buckets = defaultdict(list)          # (normal bin, offset bin, cell) -> [(instance, tri index, own)]
    world = {}
    cos_a = math.cos(math.radians(a.angle))
    for i in range(len(W)):
        T, M = mesh_tris(z, meta, int(IM[i]), 0.0, cache)       # (the area test is made as placed, below)
        if not len(T):
            continue
        Tw = T @ W[i][:, :3].T + W[i][:, 3]
        e1, e2 = Tw[:, 1] - Tw[:, 0], Tw[:, 2] - Tw[:, 0]
        n = np.cross(e1, e2)
        area = 0.5 * np.linalg.norm(n, axis=1)
        ok = area >= a.min_tri * upm * upm
        if not ok.any():
            continue
        Tw, n, area, M = Tw[ok], n[ok] / (2 * area[ok, None]), area[ok], M[ok]
        d = np.einsum('ij,ij->i', n, Tw[:, 0])
        world[i] = (Tw, n, area, M)
        x = n * nq
        kn = np.round(x).astype(int)
        edge = np.where(np.abs(x - kn) >= 0.5 - 1.0 / NBIN, np.sign(x - kn).astype(int), 0)   # (k, 3): across which
        kd = np.floor(d / dq).astype(int)
        kc = np.floor(Tw.mean(1) / (16.0 * upm)).astype(int)   # a 16 m cell: candidates are near (cells either side)
        for t in range(len(Tw)):
            rest = (kd[t], kc[t, 0], kc[t, 1], kc[t, 2])
            b = (kn[t, 0], kn[t, 1], kn[t, 2])
            buckets[b + rest].append((i, t, True))
            if edge[t].any():
                for sx in {0, edge[t, 0]}:
                    for sy in {0, edge[t, 1]}:
                        for sz in {0, edge[t, 2]}:
                            if sx or sy or sz:
                                buckets[(b[0] + sx, b[1] + sy, b[2] + sz) + rest].append((i, t, False))
    # each triangle against the other placements' triangles of its bucket and the two either side in offset and cell:
    # its sample points (centroid, edge midpoints) inside theirs give the share of its area they cover
    directed = defaultdict(float)        # (i, j) -> m2 of i's surface j covers
    for key, entries in buckets.items():
        own = [(i, t) for i, t, mine in entries if mine]
        if not own:
            continue
        cand = [(i, t) for dd in (-1, 0, 1) for x in (-1, 0, 1) for y in (-1, 0, 1) for zz in (-1, 0, 1)
                for i, t, _ in buckets.get(key[:3] + (key[3] + dd, key[4] + x, key[5] + y, key[6] + zz), ())]
        if len({i for i, _ in cand}) < 2:
            continue
        ci = np.array([i for i, _ in cand])
        Q = np.stack([world[i][0][t] for i, t in cand])             # (k, 3, 3)
        QN = np.stack([world[i][1][t] for i, t in cand])
        # only the candidates facing and on the plane of one of this bucket's own triangles, at once (the wider
        # neighbourhood is mostly other facings; the per-triangle test below then sees what can pass it)
        ON = np.stack([world[i][1][t] for i, t in own])            # (o, 3)
        OD = np.einsum('ij,ij->i', ON, np.stack([world[i][0][t][0] for i, t in own]))
        keep = ((QN @ ON.T >= cos_a) & (np.abs(Q[:, 0] @ ON.T - OD[None, :]) <= dq)).any(1)
        if len(set(ci[keep].tolist())) < 2:
            continue
        ci, Q, QN = ci[keep], Q[keep], QN[keep]
        for i, t in own:
            Ti, ni, ai, _ = world[i]
            nrm, tri = ni[t], Ti[t]
            sel = ((ci != i) & (QN @ nrm >= cos_a) & (np.abs((Q[:, 0] - tri[0]) @ nrm) <= dq)
                   & (np.maximum(NEAR[ci], NEAR[i]) < np.minimum(FAR[ci], FAR[i])))
            if not sel.any():
                continue
            u = np.cross(nrm, [0.0, 0.0, 1.0] if abs(nrm[2]) < 0.9 else [1.0, 0.0, 0.0])
            u /= np.linalg.norm(u)
            v = np.cross(nrm, u)
            S = np.vstack([tri.mean(0), (tri + np.roll(tri, -1, 0)) / 2])
            P = np.stack([S @ u, S @ v], -1)                         # (4, 2)
            Qs = Q[sel]
            Q2 = np.stack([Qs @ u, Qs @ v], -1)                      # (m, 3, 2)
            A, B, C = Q2[:, 0, None], Q2[:, 1, None], Q2[:, 2, None]

            def side(U, V):
                return (V[..., 0] - U[..., 0]) * (P[None, :, 1] - U[..., 1]) - (V[..., 1] - U[..., 1]) * (P[None, :, 0] - U[..., 0])
            d1, d2, d3 = side(A, B), side(B, C), side(C, A)
            hit = ((d1 >= 0) & (d2 >= 0) & (d3 >= 0)) | ((d1 <= 0) & (d2 <= 0) & (d3 <= 0))   # (m, 4)
            for j in np.unique(ci[sel]):
                h = hit[ci[sel] == j].any(0)
                if h.any():
                    directed[(i, int(j))] += ai[t] * h.mean() / (upm * upm)
    pairs = defaultdict(float)
    for (i, j), ov in directed.items():
        k = (min(i, j), max(i, j))
        pairs[k] = max(pairs[k], ov)
    rows = []
    for (i, j), ov in sorted(pairs.items(), key=lambda kv: -kv[1]):
        if ov < a.min_overlap:
            break
        mi, mj = int(IM[i]), int(IM[j])
        same = mi == mj and np.allclose(W[i], W[j], atol=0.05)
        mats = sorted({meta['mnames'][m]['name'] for m in set(world[i][3]) | set(world[j][3])
                       if m < len(meta['mnames'])})
        rows.append({'a': i, 'b': j, 'overlap_m2': round(ov, 3), 'duplicate': bool(same),
                     'name_a': names[i] if i < len(names) else None, 'name_b': names[j] if j < len(names) else None,
                     'mesh_a': meta['meshkeys'][mi][:160], 'mesh_b': meta['meshkeys'][mj][:160],
                     'pos_a': (z['placed_pos'][i].round(2).tolist() if i < len(z['placed_pos']) else None),
                     'materials': mats[:6]})
    print('%s: %d placements, %d with surfaces, %d pairs overlapping >= %.2f m2 (%d duplicates), %.1f m2 in all'
          % (a.part.name, len(W), len(world), len(rows), a.min_overlap, sum(r['duplicate'] for r in rows),
             sum(r['overlap_m2'] for r in rows)))
    for r in rows[:a.top]:
        print('  %7.2f m2 %s %s | %s @ %s' % (r['overlap_m2'], 'DUP' if r['duplicate'] else '   ',
                                            r['mesh_a'].split('\\\\')[-1][:50], r['mesh_b'].split('\\\\')[-1][:50],
                                            r['pos_a']))
    if a.out:
        a.out.write_text(json.dumps(rows, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
