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
"""Read Quake 3 family BSP files (Quake 3 IBSP v46, Quake Live IBSP v47, Jedi Academy RBSP v1) into numpy arrays.

Only the lumps a level port needs: entities, shaders, planes, models, brushes, brush sides, draw verts and
indexes, fogs and surfaces. Quake Live adds an advertisements lump after the visibility lump; it is ignored.
"""
import re
import struct
from pathlib import Path

import numpy as np

import sys
if __name__ == '__main__':      # (run as python -I, the script's own folder is not on the path)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
from mapio import cross3

# Quake 3 content and surface flags (the BSP stores the compiled Q3 values, not surfaceparm names)
CONTENTS = {
    'solid': 0x1, 'lava': 0x8, 'slime': 0x10, 'water': 0x20, 'fog': 0x40,
    'areaportal': 0x8000, 'playerclip': 0x10000, 'monsterclip': 0x20000, 'teleporter': 0x40000,
    'jumppad': 0x80000, 'clusterportal': 0x100000, 'donotenter': 0x200000, 'botclip': 0x400000,
    'mover': 0x800000, 'origin': 0x1000000, 'detail': 0x8000000, 'structural': 0x10000000,
    'trans': 0x20000000, 'trigger': 0x40000000, 'nodrop': 0x80000000,
}
SURF = {
    'nodamage': 0x1, 'slick': 0x2, 'sky': 0x4, 'ladder': 0x8, 'noimpact': 0x10, 'nomarks': 0x20, 'flesh': 0x40,
    'nodraw': 0x80, 'hint': 0x100, 'skip': 0x200, 'nolightmap': 0x400, 'pointlight': 0x800,
    'metalsteps': 0x1000, 'nosteps': 0x2000, 'nonsolid': 0x4000, 'lightfilter': 0x8000,
    'alphashadow': 0x10000, 'nodlight': 0x20000, 'dust': 0x40000,
}
MST_PLANAR, MST_PATCH, MST_TRISOUP, MST_FLARE = 1, 2, 3, 4

DRAWVERT = np.dtype([('xyz', '<f4', 3), ('st', '<f4', 2), ('lm', '<f4', 2), ('normal', '<f4', 3), ('color', 'u1', 4)])
SURFACE = np.dtype([('shader', '<i4'), ('fog', '<i4'), ('type', '<i4'), ('first_vert', '<i4'), ('num_verts', '<i4'),
                    ('first_index', '<i4'), ('num_indexes', '<i4'), ('lm_num', '<i4'), ('lm_xy', '<i4', 2),
                    ('lm_wh', '<i4', 2), ('lm_origin', '<f4', 3), ('lm_vecs', '<f4', (3, 3)),
                    ('patch_w', '<i4'), ('patch_h', '<i4')])
SHADER = np.dtype([('name', 'S64'), ('surface_flags', '<i4'), ('content_flags', '<i4')])
PLANE = np.dtype([('normal', '<f4', 3), ('dist', '<f4')])
MODEL = np.dtype([('mins', '<f4', 3), ('maxs', '<f4', 3), ('first_surface', '<i4'), ('num_surfaces', '<i4'),
                  ('first_brush', '<i4'), ('num_brushes', '<i4')])
BRUSH = np.dtype([('first_side', '<i4'), ('num_sides', '<i4'), ('shader', '<i4')])
BRUSHSIDE = np.dtype([('plane', '<i4'), ('shader', '<i4')])
FOG = np.dtype([('shader', 'S64'), ('brush', '<i4'), ('visible_side', '<i4')])
# Raven RBSP v1 (Jedi Academy): four lightmap styles per surface and vertex, brush sides name their draw surface
RDRAWVERT = np.dtype([('xyz', '<f4', 3), ('st', '<f4', 2), ('lm', '<f4', (4, 2)), ('normal', '<f4', 3),
                      ('color', 'u1', (4, 4))])
RSURFACE = np.dtype([('shader', '<i4'), ('fog', '<i4'), ('type', '<i4'), ('first_vert', '<i4'), ('num_verts', '<i4'),
                     ('first_index', '<i4'), ('num_indexes', '<i4'), ('lm_styles', 'u1', 4), ('vert_styles', 'u1', 4),
                     ('lm_num', '<i4', 4), ('lm_x', '<i4', 4), ('lm_y', '<i4', 4), ('lm_wh', '<i4', 2),
                     ('lm_origin', '<f4', 3), ('lm_vecs', '<f4', (3, 3)), ('patch_w', '<i4'), ('patch_h', '<i4')])
RBRUSHSIDE = np.dtype([('plane', '<i4'), ('shader', '<i4'), ('surface', '<i4')])

L_ENTITIES, L_SHADERS, L_PLANES, L_MODELS, L_BRUSHES, L_BRUSHSIDES = 0, 1, 2, 7, 8, 9
L_DRAWVERTS, L_DRAWINDEXES, L_FOGS, L_SURFACES = 10, 11, 12, 13


class Bsp:
    def __init__(self, path):
        self.path = Path(path)
        data = self.path.read_bytes()
        ident, self.version = struct.unpack_from('<4si', data, 0)
        self.raven = ident == b'RBSP'
        if not ((ident == b'IBSP' and self.version in (46, 47)) or (self.raven and self.version == 1)):
            raise ValueError('%s: not a Quake 3 / Quake Live IBSP or a Raven RBSP (%r v%d)' % (path, ident, self.version))
        lumps = [struct.unpack_from('<ii', data, 8 + 8 * i) for i in range(17)]

        def lump(i, dtype):
            ofs, ln = lumps[i]
            return np.frombuffer(data, dtype=dtype, count=ln // dtype.itemsize, offset=ofs).copy()

        ofs, ln = lumps[L_ENTITIES]
        self.entity_text = data[ofs:ofs + ln].rstrip(b'\0').decode('latin-1')
        self.shaders = lump(L_SHADERS, SHADER)
        self.planes = lump(L_PLANES, PLANE)
        self.models = lump(L_MODELS, MODEL)
        self.brushes = lump(L_BRUSHES, BRUSH)
        self.brushsides = lump(L_BRUSHSIDES, RBRUSHSIDE if self.raven else BRUSHSIDE)
        self.verts = lump(L_DRAWVERTS, RDRAWVERT if self.raven else DRAWVERT)
        self.indexes = lump(L_DRAWINDEXES, np.dtype('<i4'))
        self.fogs = lump(L_FOGS, FOG)
        self.surfaces = lump(L_SURFACES, RSURFACE if self.raven else SURFACE)
        self.entities = parse_entities(self.entity_text)

    def shader_name(self, i):
        return self.shaders[i]['name'].split(b'\0')[0].decode('latin-1')


def parse_entities(text):
    """Entity lump -> list of ordered dicts (keys keep their first spelling; repeated keys keep the last value)."""
    ents, cur = [], None
    for tok in re.finditer(r'"([^"]*)"|([{}])', text):
        s, brace = tok.group(1), tok.group(2)
        if brace == '{':
            cur, key = {}, None
        elif brace == '}':
            ents.append(cur)
            cur = None
        elif cur is not None:
            if key is None:
                key = s
            else:
                cur[key] = s
                key = None
    return ents


def flag_names(value, table):
    return [n for n, bit in table.items() if value & bit]


def brush_polygons(planes, eps=0.01):
    """Convex brush (list of (normal, dist), inside where n.x <= d) -> one polygon per plane (or None if clipped away)."""
    polys = []
    for i, (n, d) in enumerate(planes):
        poly = base_winding(n, d)
        for j, (n2, d2) in enumerate(planes):
            if j == i or poly is None:
                continue
            poly = clip_winding(poly, n2, d2, eps)
        polys.append(poly)
    return polys


def base_winding(n, d, size=1 << 17):
    n = np.asarray(n, dtype=np.float64)
    ax = int(np.argmax(np.abs(n)))
    up = np.array([0.0, 0.0, 1.0]) if ax != 2 else np.array([1.0, 0.0, 0.0])
    up = up - n * np.dot(up, n)
    up /= np.linalg.norm(up)
    right = cross3(up, n)
    org = n * d
    return np.array([org - right * size + up * size, org + right * size + up * size,
                     org + right * size - up * size, org - right * size - up * size])


def clip_winding(poly, n, d, eps):
    """Keep the part of poly behind plane (n.x <= d)."""
    dist = poly @ np.asarray(n, dtype=np.float64) - d
    if np.all(dist <= eps):
        return poly
    if np.all(dist >= -eps):
        return None
    out = []
    k = len(poly)
    for i in range(k):
        p, q = poly[i], poly[(i + 1) % k]
        dp, dq = dist[i], dist[(i + 1) % k]
        if dp <= eps:
            out.append(p)
        if (dp < -eps and dq > eps) or (dp > eps and dq < -eps):
            t = dp / (dp - dq)
            out.append(p + (q - p) * t)
    return np.array(out) if len(out) >= 3 else None
