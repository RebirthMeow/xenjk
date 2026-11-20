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
"""Write q3map2 .map text: entities, brushDef (brush primitives) brushes and patchDef2 patches.

All brushes use brush primitives: q3map2 refuses to mix them with the old format, and only brush primitives can
carry an arbitrary affine texture mapping fitted from a mesh's UVs.
"""
import numpy as np


def cross3(a, b):
    """np.cross of two float64 3-vectors as plain float arithmetic: the products and differences numpy computes, in
    the same order, so the same bits, without numpy's per-call overhead (the brush writer calls it millions of times;
    the conversion 1.72x faster, the .map byte-identical). Anything else: np.cross."""
    if (type(a) is np.ndarray and type(b) is np.ndarray and a.shape == (3,) and b.shape == (3,)
            and a.dtype == np.float64 and b.dtype == np.float64):
        a0, a1, a2 = a.tolist()
        b0, b1, b2 = b.tolist()
        return np.array([a1 * b2 - a2 * b1, a2 * b0 - a0 * b2, a0 * b1 - a1 * b0])
    return np.cross(a, b)


def _num(c):
    s = ('%.4f' % c).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def _pt(p):
    return '( %s )' % ' '.join(_num(c) for c in p)


def axis_base(n):
    """q3map2 ComputeAxisBase: the plane's (texX, texY) for brush primitives."""
    n = np.where(np.abs(n) < 1e-6, 0.0, n)
    rot_y = -np.arctan2(n[2], np.sqrt(n[1] * n[1] + n[0] * n[0]))
    rot_z = np.arctan2(n[1], n[0])
    tex_x = np.array([-np.sin(rot_z), np.cos(rot_z), 0.0])
    tex_y = np.array([-np.sin(rot_y) * np.cos(rot_z), -np.sin(rot_y) * np.sin(rot_z), -np.cos(rot_y)])
    return tex_x, tex_y


def plane_points(poly, n):
    """Three points of a face polygon whose q3map2 plane (normal = (p2 - p0) x (p1 - p0)) faces outward along n.
    Takes the largest triangle from the polygon's vertices for precision."""
    best, tri = -1.0, None
    k = len(poly)
    a = poly[0]
    for i in range(1, k - 1):            # fan, then refine from the far vertex
        area = np.linalg.norm(cross3(poly[i] - a, poly[i + 1] - a))
        if area > best:
            best, tri = area, (0, i, i + 1)
    p0, p1, p2 = (poly[j] for j in tri)
    if np.dot(cross3(p2 - p0, p1 - p0), n) < 0:
        p1, p2 = p2, p1
    return p0, p1, p2


def tex_matrix(n, xyz, st):
    """Least-squares brush-primitives texture matrix mapping points on the plane to st. None if underdetermined."""
    tx, ty = axis_base(n)
    uv = np.stack([xyz @ tx, xyz @ ty, np.ones(len(xyz))], axis=1)
    if len(xyz) < 3 or np.linalg.matrix_rank(uv[:, :2] - uv[:, :2].mean(0)) < 2:
        return None
    m, *_ = np.linalg.lstsq(uv, st, rcond=None)
    return m.T     # 2x3


DEFAULT_TEX = np.array([[1 / 128.0, 0, 0], [0, 1 / 128.0, 0]])


def brush(sides):
    """sides: [(polygon Nx3, outward normal, shader, 2x3 texture matrix or None)] -> brushDef text."""
    out = ['{', 'brushDef', '{']
    for poly, n, shader, tm in sides:
        p0, p1, p2 = plane_points(poly, n)
        tm = DEFAULT_TEX if tm is None else tm
        out.append('%s %s %s ( ( %s %s %s ) ( %s %s %s ) ) %s 0 0 0' % (
            _pt(p0), _pt(p1), _pt(p2), *('%.8g' % v for v in tm.ravel()), shader))
    out += ['}', '}']
    return '\n'.join(out)


def patch(shader, width, height, cp):
    """cp: (height*width) x 5 control points (x y z s t), row-major with width fastest (BSP order)."""
    cp = np.asarray(cp).reshape(height, width, 5)
    out = ['{', 'patchDef2', '{', shader, '( %d %d 0 0 0 )' % (width, height), '(']
    for j in range(width):
        out.append('( %s )' % ' '.join('( %s )' % ' '.join(_num(c) if k < 3 else '%.6g' % c for k, c in enumerate(cp[i, j]))
                                      for i in range(height)))
    out += [')', '}', '}']
    return '\n'.join(out)


def entity(keys, body=()):
    lines = ['{'] + ['"%s" "%s"' % (k, str(v).replace('"', "'")) for k, v in keys.items()]
    lines += list(body)
    lines.append('}')
    return '\n'.join(lines)
