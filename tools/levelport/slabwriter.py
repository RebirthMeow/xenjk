"""slabwriter.py -- gltf2map's mesh-collision slab brushes in plain float arithmetic.

The same text as gltf2map's slab_planes, q3_duplicate_planes, q3bsp.brush_polygons and mapio.brush give: each value
is computed with the operations numpy performs for those 3-vectors, in the same order, so it has the same bits. Most
of their time was numpy's per-call cost on 3-vectors (a brush made a few thousand calls). Checked against numpy 1.26
on this machine: a 3-vector dot, 1-D @, linalg.norm and a
3-element sum are plain left-to-right sums of products, rounded per step; a (k x 3) @ (3,) of k > 1 rows is not (its
kernel fuses multiply-adds), but it gives a row the same bits wherever the row sits in the matrix, so a brush's
polygons are clipped one plane at a time, each round's rows stacked into one numpy call. q3map2's text comes out byte
for byte the same; the gate is a cmp of whole chunk maps (look/p1b-p2-p10-20261103)."""
import math

import numpy as np

import mapio

_Z = (0.0, 0.0, 1.0)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    a0, a1, a2 = a
    b0, b1, b2 = b
    return (a1 * b2 - a2 * b1, a2 * b0 - a0 * b2, a0 * b1 - a1 * b0)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def slab_planes(pts, n, d, thick, straight_sin):
    """gltf2map.slab_planes on float tuples: [(normal tuple, dist)] or None."""
    if abs(n[2]) > 0.35:
        axis = (-0.0, -0.0, -1.0) if n[2] > 0 else _Z          # -_Z (numpy negates each zero) or _Z
        cap = ((-n[0], -n[1], -n[2]), float(-(d - thick * abs(n[2]))))
    else:
        axis, cap = (-n[0], -n[1], -n[2]), ((-n[0], -n[1], -n[2]), float(-d + thick))
    keep = []
    k = len(pts)
    for i in range(k):
        p, q, r = pts[i - 1], pts[i], pts[(i + 1) % k]
        qp, rq = _sub(q, p), _sub(r, q)
        if _norm(_cross(qp, rq)) > straight_sin * max(_norm(qp) * _norm(rq), 1e-12):
            keep.append(q)
    if len(keep) < 3:
        return None
    # np.mean(keep, axis=0): each coordinate summed over the points in order from the first, divided by their count
    m = len(keep)
    sx, sy, sz = keep[0]
    for p in keep[1:]:
        sx += p[0]
        sy += p[1]
        sz += p[2]
    cen = (sx / m, sy / m, sz / m)
    planes = [(tuple(n), d), cap]
    for i in range(m):
        p, q = keep[i], keep[(i + 1) % m]
        sn = _cross(_sub(q, p), axis)
        sl = _norm(sn)
        if sl < 1e-9:
            return None
        sn = (sn[0] / sl, sn[1] / sl, sn[2] / sl)
        if _dot(sn, _sub(cen, p)) > 0:
            sn = (-sn[0], -sn[1], -sn[2])
        planes.append((sn, float(_dot(sn, p))))
    return planes


def duplicate_planes(planes):
    """gltf2map.q3_duplicate_planes: two sides within 1e-5 per normal component and 0.01 in distance."""
    for i in range(len(planes)):
        (a, da) = planes[i]
        for j in range(len(planes)):
            if i == j:
                continue
            (b, db) = planes[j]
            if (max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) < 1e-5) and abs(da - db) < 0.01:
                return True
    return False


def _base_winding(n, d, size=1 << 17):
    """q3bsp.base_winding: four corners (tuples)."""
    a = (abs(n[0]), abs(n[1]), abs(n[2]))
    ax = 0 if a[0] >= a[1] and a[0] >= a[2] else (1 if a[1] >= a[2] else 2)   # np.argmax: the first largest
    up = (0.0, 0.0, 1.0) if ax != 2 else (1.0, 0.0, 0.0)
    s = _dot(up, n)
    up = (up[0] - n[0] * s, up[1] - n[1] * s, up[2] - n[2] * s)
    ln = _norm(up)
    up = (up[0] / ln, up[1] / ln, up[2] / ln)
    right = _cross(up, n)
    org = (n[0] * d, n[1] * d, n[2] * d)
    r = (right[0] * size, right[1] * size, right[2] * size)
    u = (up[0] * size, up[1] * size, up[2] * size)
    return [(org[0] - r[0] + u[0], org[1] - r[1] + u[1], org[2] - r[2] + u[2]),
            (org[0] + r[0] + u[0], org[1] + r[1] + u[1], org[2] + r[2] + u[2]),
            (org[0] + r[0] - u[0], org[1] + r[1] - u[1], org[2] + r[2] - u[2]),
            (org[0] - r[0] - u[0], org[1] - r[1] - u[1], org[2] - r[2] - u[2])]


def brush_polygons(planes, eps=0.01):
    """q3bsp.brush_polygons: one polygon (a list of tuples) per plane, or None where it is clipped away. Each polygon
    meets the other planes in their order, as there; here one plane at a time for every polygon, its distances from one
    stacked numpy product."""
    polys = [_base_winding(n, d) for n, d in planes]
    for j, (n2, d2) in enumerate(planes):
        live = [i for i in range(len(polys)) if i != j and polys[i] is not None]
        if not live:
            continue
        rows = np.array([p for i in live for p in polys[i]], np.float64)
        dist = (rows @ np.asarray(n2, np.float64) - d2).tolist()
        o = 0
        for i in live:
            poly = polys[i]
            k = len(poly)
            dd = dist[o:o + k]
            o += k
            if all(v <= eps for v in dd):
                continue
            if all(v >= -eps for v in dd):
                polys[i] = None
                continue
            out = []
            for a in range(k):
                p, q = poly[a], poly[(a + 1) % k]
                dp, dq = dd[a], dd[(a + 1) % k]
                if dp <= eps:
                    out.append(p)
                if (dp < -eps and dq > eps) or (dp > eps and dq < -eps):
                    t = dp / (dp - dq)
                    out.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t))
            polys[i] = out if len(out) >= 3 else None
    return polys


def _plane_points(poly, n):
    """mapio.plane_points on tuples."""
    best, tri = -1.0, None
    a = poly[0]
    for i in range(1, len(poly) - 1):
        area = _norm(_cross(_sub(poly[i], a), _sub(poly[i + 1], a)))
        if area > best:
            best, tri = area, (0, i, i + 1)
    p0, p1, p2 = (poly[j] for j in tri)
    if _dot(_cross(_sub(p2, p0), _sub(p1, p0)), n) < 0:
        p1, p2 = p2, p1
    return p0, p1, p2


_TEX = ' ( ( %s %s %s ) ( %s %s %s ) ) ' % tuple('%.8g' % v for v in mapio.DEFAULT_TEX.ravel())


def brush(sides, shader):
    """mapio.brush for sides [(polygon, normal)] of one shader and no texture matrix."""
    out = ['{', 'brushDef', '{']
    tail = _TEX + shader + ' 0 0 0'
    for poly, n in sides:
        p0, p1, p2 = _plane_points(poly, n)
        out.append(mapio._pt(p0) + ' ' + mapio._pt(p1) + ' ' + mapio._pt(p2) + tail)
    out += ['}', '}']
    return '\n'.join(out)
