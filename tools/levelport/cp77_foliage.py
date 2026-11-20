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
"""cp77_foliage.py -- CP2077's foliage into a tiles tag's render layer (the pines on the hills).

CP2077 places vegetation and litter with worldFoliageNode (29,701 in Watson's scan, never converted before): each node
names one mesh and a span (populationSpanInfo stancesBegin/stancesCount) of its sector's embedded .cfoliage
(worldFoliageCompiledResource, the sector json's EmbeddedFiles): population instances, each a position offset from the
node, a rotation quaternion and a uniform scale. night_city_b: 1.54 M instances (grass 688 k, litter ~440 k, shrubs
176 k, weeds 145 k, trees and palms ~10 k), 290 of their 304 meshes already fetched.

Writes tiles/<tag>/foliage/<tile>.jsonl: one record per instance inside the area's cells, in the tile records' form
(mesh, app, type worldFoliageNode, sector, name '<node>#<i>', m: CP 3x4, hide: the node's streamingDistance, the
distance CP2077 streams it out at (grass 40 m), foliage 1). cp2gltf.stage_scene_tile adds them to the tile's scene
part; the collision build never reads them (render layer only, as proxies). Reads the sectors' json (patch 2.31).
    python -I cp77_foliage.py night_city_b <tag> [--tile-size 256]"""
import argparse
import collections
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402


def cv(v):
    return v.get('$value') if isinstance(v, dict) and '$value' in v else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('area', choices=sorted(cp2gltf.AREAS))
    ap.add_argument('tag')
    ap.add_argument('--tile-size', type=float, default=256.0)
    a = ap.parse_args()
    work = cp2gltf.work_dir(a.area)
    # the area's own rule, as the converter keeps a placement (in_area: its circle, and its cells and floor if any)
    cp2gltf.use_area(a.area)
    centre, radius = cp2gltf.AREAS[a.area]['centre'], cp2gltf.AREAS[a.area]['radius']
    out_dir = cp2gltf.tile_dir(work, a.tag) / 'foliage'
    out_dir.mkdir(parents=True, exist_ok=True)
    # each tile's records streamed to its file as they come (night_city_b holds 1.54 M instances)
    tiles, files = collections.Counter(), {}
    stats = collections.Counter()
    for f in sorted((work / 'json').glob('*.streamingsector.json')):
        s = f.read_text(encoding='utf-8')
        if 'worldFoliageNode' not in s:
            continue
        sector = f.name[:-len('.streamingsector.json')]
        d = json.loads(s)
        rc = d['Data']['RootChunk']
        pops = {}
        for e in d['Data'].get('EmbeddedFiles') or []:
            c = e.get('Content') or {}
            if 'populationCount' in c:
                pops[cv(e.get('FileName'))] = ((c.get('dataBuffer') or {}).get('Data') or {}).get('Populations') or []
        fol = {i: n['Data'] for i, n in enumerate(rc['nodes']) if n['Data'].get('$type') == 'worldFoliageNode'}
        for r in cp2gltf.shown_records(work, sector, rc):     # variant-off ranges stay out
            nd = fol.get(r['NodeIndex'])
            if nd is None:
                continue
            stats['nodes'] += 1
            if not nd.get('isVisibleInGame', 1):
                stats['nodes_hidden'] += 1
                continue
            mesh = cv((nd.get('mesh') or {}).get('DepotPath')) or ''
            res = cv((nd.get('foliageResource') or {}).get('DepotPath')) or ''
            span = nd.get('populationSpanInfo') or {}
            b, n = int(span.get('stancesBegin') or 0), int(span.get('stancesCount') or 0)
            P = pops.get(res)
            if not mesh or P is None:
                stats['nodes_no_data'] += 1
                continue
            base = cp2gltf.trs(r['Position'], r.get('Orientation'), r.get('Scale'))
            hide = round(float(nd.get('streamingDistance') or 0.0) * float(nd.get('autoHideDistanceScale') or 1.0), 3)
            app = cv(nd.get('meshAppearance'))
            app = 'default' if app in (None, 'None', '') else app
            name = cv(nd.get('debugName')) or 'foliage'
            for i, p in enumerate(P[b:b + n]):
                q = p.get('Rotation') or {}
                sc = float(p.get('Scale') or 1.0)
                local = cp2gltf.trs(p['Position'], {'i': q.get('X', 0), 'j': q.get('Y', 0), 'k': q.get('Z', 0),
                                                    'r': q.get('W', 1)}, {'X': sc, 'Y': sc, 'Z': sc})
                m = base @ local
                x, y = float(m[0, 3]), float(m[1, 3])
                if not cp2gltf.in_area(x, y, centre, radius, float(m[2, 3])):
                    stats['outside'] += 1
                    continue
                tid = '%d_%d' % (math.floor(x / a.tile_size), math.floor(y / a.tile_size))
                if tid not in files:
                    files[tid] = open(out_dir / (tid + '.jsonl.tmp'), 'w', encoding='utf-8')
                files[tid].write(json.dumps({'mesh': mesh, 'app': app, 'type': 'worldFoliageNode', 'sector': sector,
                                             'name': '%s#%d' % (name, i),
                                             'm': [round(float(v), 5) for v in m[:3].ravel()],
                                             'hide': hide, 'foliage': 1}) + '\n')
                tiles[tid] += 1
                stats['instances'] += 1
    for tid, fh in files.items():
        fh.close()
        (out_dir / (tid + '.jsonl.tmp')).replace(out_dir / (tid + '.jsonl'))
    # a tile with foliage before and none now is published empty (its old file kept the vegetation in its scene part): an empty file reads as no records, and changes the part's foliage key so the part rebuilds
    emptied = sorted(f for f in out_dir.glob('*.jsonl') if f.name[:-len('.jsonl')] not in tiles and f.stat().st_size)
    for f in emptied:
        tmp = f.with_name(f.name + '.tmp')
        tmp.write_text('', encoding='utf-8')
        tmp.replace(f)
    if emptied:
        stats['tiles_emptied'] = len(emptied)
    print('foliage: %d instances in %d tiles -> %s; %s' % (stats['instances'], len(tiles), out_dir, dict(stats)))


if __name__ == '__main__':
    main()
