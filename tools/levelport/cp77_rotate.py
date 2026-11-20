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
"""cp77_rotate.py -- CP2077's rotating meshes for the render scene: the fans, propellers and ad rings.

A worldRotatingMeshNode is a mesh node that turns about one of its own axes (rotationAxis X / Y / Z) once every
fullRotationTime seconds, the other way with reverseDirection, about the mesh's own origin: in 850 of 850 placements
of five Watson tiles the origin lies on the axis through the mesh's bounds centre (the hub), while the placement
record's Pivot is a streaming point (a median 1.8 m off, 1,362 nodes over 200 m off). The renderer (H15) reads
maps/<map>.xrot: per rotating instance its axis and pivot in the mesh's own frame, a signed angular speed and a start
phase (cp2gltf.py writes it from the parts' inst_rot rows).

  python -I cp77_rotate.py index [--area night_city_b]     work/rotate_index.json: every rotating node of the sectors
  python -I cp77_rotate.py stats [--area night_city_b]

Converted levels are for local play only."""
import argparse
import collections
import hashlib
import json
import math
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

AXES = {'X': (1.0, 0.0, 0.0), 'Y': (0.0, 1.0, 0.0), 'Z': (0.0, 0.0, 1.0)}
AXIS_DEFAULT = 'X'      # (the enum's first value: a node that writes none; every Watson node writes one)
POS_TOL = 0.05          # m: a placement's translation against its record's Position


def cv(v):
    return v.get('$value') if isinstance(v, dict) and '$value' in v else v


def _sector(f):
    s = f.read_text(encoding='utf-8')
    if 'worldRotatingMeshNode' not in s:
        return []
    sector = f.name[:-len('.streamingsector.json')]
    rc = json.loads(s)['Data']['RootChunk']
    rot = {i: x['Data'] for i, x in enumerate(rc['nodes']) if x['Data'].get('$type') == 'worldRotatingMeshNode'}
    out = []
    import cp2gltf          # (here, not at the top: cp2gltf imports this module)
    for r in cp2gltf.shown_records(f.parent.parent, sector, rc):     # variant-off ranges stay out
        nd = rot.get(r['NodeIndex'])
        if nd is None:
            continue
        p = r['Position']
        out.append(('%s|%s' % (sector, cv(nd.get('debugName'))),
                    [round(float(p['X']), 4), round(float(p['Y']), 4), round(float(p['Z']), 4),
                     str(nd.get('rotationAxis') or AXIS_DEFAULT), float(nd.get('fullRotationTime') or 0.0),
                     int(nd.get('reverseDirection') or 0)]))
    return out


def index(work, workers=8):
    """every rotating node of the scanned sectors: sector|name -> [[x, y, z, axis, period s, reverse]]
    (several nodes of a sector can share a name: a placement takes the one at its translation)"""
    out = collections.defaultdict(list)
    files = sorted((work / 'json').glob('*.streamingsector.json'))
    with ProcessPoolExecutor(workers) as ex:
        for recs in ex.map(_sector, files, chunksize=16):
            for k, v in recs:
                out[k].append(v)
    return dict(out)


class RotMap:
    """The scene build's side: a placement -> its turn in the mesh's frame (axis, signed rad/s, pivot, phase), or None"""

    def __init__(self, work):
        f = work / 'rotate_index.json'
        self.idx = json.load(open(f, encoding='utf-8')) if f.exists() else None
        self.ok = self.idx is not None

    def rec(self, p, m):
        """p: a worldRotatingMeshNode placement; m: its 3x4 world matrix (CP2077 metres). Axis and pivot in the mesh's
        own frame (the pivot its origin); the speed 2 pi / fullRotationTime, negative with reverseDirection; the phase from its name and place (CP2077
        starts each when it streams in, so the fans of a roof never turn in step), the same every build."""
        if not self.ok or p.get('type') != 'worldRotatingMeshNode':
            return None
        t = np.asarray(m, np.float64)[:, 3]
        best = None
        for e in self.idx.get('%s|%s' % (p.get('sector'), p.get('name')), ()):
            d = max(abs(e[0] - t[0]), abs(e[1] - t[1]), abs(e[2] - t[2]))
            if d <= POS_TOL and (best is None or d < best[0]):
                best = (d, e)
        if best is None:
            return None
        _, (x, y, z, ax, period, rev) = best
        if period <= 0:
            return None
        speed = 2.0 * math.pi / period * (-1.0 if rev else 1.0)
        h = int(hashlib.sha1(('%s|%s|%.1f|%.1f' % (p.get('sector'), p.get('name'), x, y)).encode()).hexdigest()[:8], 16)
        return list(AXES.get(ax, AXES[AXIS_DEFAULT])) + [speed, 0.0, 0.0, 0.0, h / 2 ** 32 * 2 * math.pi]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=('index', 'stats'))
    ap.add_argument('--area', default='night_city_b')
    ap.add_argument('--workers', type=int, default=8)
    a = ap.parse_args()
    import cp2gltf          # (here, not at the top: cp2gltf imports this module)
    work = cp2gltf.work_dir(a.area)
    f = work / 'rotate_index.json'
    if a.cmd == 'index':
        idx = index(work, a.workers)
        text = json.dumps(idx, indent=0)
        if f.exists() and f.read_text(encoding='utf-8') == text:
            print('%s: unchanged, not rewritten (the scene parts\' keys stat it)' % f.name)
        else:
            tmp = f.with_name(f.name + '.tmp')      # (renamed when whole: the scene parts' keys stat it)
            tmp.write_text(text, encoding='utf-8')
            tmp.replace(f)
        print('rotate_index: %d nodes under %d names -> %s' % (sum(map(len, idx.values())), len(idx), f))
    idx = json.load(open(f, encoding='utf-8'))
    recs = [e for v in idx.values() for e in v]
    print('axes', collections.Counter(e[3] for e in recs).most_common())
    print('periods (s)', collections.Counter(round(e[4], 1) for e in recs).most_common(10))
    print('reverse', sum(e[5] for e in recs), 'of', len(recs))


if __name__ == '__main__':
    main()
