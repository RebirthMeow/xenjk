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
"""The Witcher 3's local water shapes (CWaterComponent) read from the game's layers, for w3water.py.

A world's lakes and rivers above or below the sea are CWaterComponent areas (REDkit's "Local Water Areas"), mostly in
two layers no location dump covers: <world>/local_water_volumes.w2l and <world>/water/*.w2l. Each is an area
component: a polygon (localPoints, in its entity's space; worldPoints the same points placed) extruded by `height`
(default 2) along the entity's local +Z. The water surface is the extrusion's LOWER face: per point, the lower of
the placed point's Z and that Z plus height x the entity's Z scale. (Read off the data: Beauclair's harbour shape
sits at 117.5 m with Z scale -18.465, its lower face at 80.58 m where its 140 lilies and 36 boats float at 80.4-80.9;
its north arm's points at 104 m give 67.1 m, the dry bed's level; every flat shape's lower face lies at its terrain's
lowest ground.) A point's Z varies along a river: the surface is the polygon triangulated with those levels.

    python tools/levelport/w3watershapes.py <work folder> [--world dlc\\bob\\data\\levels\\bob] [--index-cache PKL]
writes <work>/water_shapes.json: [{layer, entity, points [[x, y, level] ...] (game metres), flat (one level)}]."""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import w3bundle  # noqa: E402
from w3emitter import CR2W  # noqa: E402

DEFAULT_HEIGHT = 2.0


def _raw_props(c, i):
    cn, _par, off, sz = c.chunks[i]
    d, p, out = c.d, off + 1, {}
    while p + 2 <= off + sz:
        ni = struct.unpack_from('<H', d, p)[0]
        if ni == 0:
            break
        ti, size = struct.unpack_from('<HI', d, p + 2)
        out[c.names[ni]] = (c.names[ti], d[p + 8:p + 4 + size])
        p += 4 + size
    return out


def entity_scale_z(raw):
    """The Z scale of a layer entity's EngineTransform (a flags byte: 1 position, 2 rotation, 4 scale; then 3 floats
    each), 1 without a scale"""
    if 'transform' not in raw:
        return 1.0
    b = raw['transform'][1]
    fl = b[0]
    f = np.frombuffer(b[1:1 + (len(b) - 1) // 4 * 4], '<f4')
    k = 3 * bool(fl & 1) + 3 * bool(fl & 2)
    return float(f[k + 2]) if fl & 4 and len(f) >= k + 3 else 1.0


def shapes_in(data, layer=''):
    c = CR2W(data)
    out = []
    for i, ch in enumerate(c.chunks):
        if ch[0] != 'CWaterComponent':
            continue
        _cn, pr, _tail = c.chunk(i)
        par = ch[1] - 1
        sz = entity_scale_z(_raw_props(c, par)) if 0 <= par < len(c.chunks) and c.chunks[par][0] == 'CEntity' else 1.0
        h = pr.get('height')
        h = DEFAULT_HEIGHT if not isinstance(h, float) else h
        P = np.array([[p['X'], p['Y'], p['Z']] for p in pr.get('worldPoints', [])], np.float64).reshape(-1, 3)
        if len(P) < 3:
            continue
        lv = np.minimum(P[:, 2], P[:, 2] + h * sz)
        out.append({'layer': layer, 'entity': par, 'height': h, 'scale_z': sz,
                    'points': [[float(x), float(y), float(z)] for (x, y, _), z in zip(P, lv)],
                    'flat': bool(np.ptp(lv) < 1e-3)})
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('work')
    ap.add_argument('--world', default='dlc\\bob\\data\\levels\\bob')
    ap.add_argument('--index-cache', default=None)
    a = ap.parse_args(argv)
    idx = w3bundle.index(cache=a.index_cache)
    pre = a.world.lower().rstrip('\\') + '\\'
    layers = sorted(k for k in idx if k.startswith(pre) and k.endswith('.w2l'))
    shapes, n = [], 0
    for k in layers:
        d = w3bundle.read(idx, k)
        if not d or b'CWaterComponent' not in d:
            continue
        n += 1
        shapes += shapes_in(d, k)
    Path(a.work, 'water_shapes.json').write_text(json.dumps({'world': a.world, 'shapes': shapes}, indent=1),
                                                 encoding='utf-8')
    print('%d layers read, %d with water shapes, %d shapes -> %s' % (len(layers), n, len(shapes),
                                                                    Path(a.work, 'water_shapes.json')))
    for s in shapes:
        P = np.array(s['points'])
        print('  %-70s %3d points, x %.0f..%.0f y %.0f..%.0f, level %.2f..%.2f' % (
            s['layer'][-70:], len(P), P[:, 0].min(), P[:, 0].max(), P[:, 1].min(), P[:, 1].max(), P[:, 2].min(),
            P[:, 2].max()))


if __name__ == '__main__':
    main()
