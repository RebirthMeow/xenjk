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
"""w3enttmpl.py -- the mesh and light components of every entity template a work folder places, from the game files.

    blender -b --python tools/levelport/w3enttmpl.py -- --work DIR [--out DIR/entity_templates.json]

Blender with the witcher3_tools add-on (its CR2W reader; $W3_GAME, $W3_UNCOOK). The templates are the entity paths
of placements.json's meshes (entity) and lights (props.witcher_entity_path), .w2l layers aside. Each template file is
read as the game stores it: its chunks, their streamingDataBuffer (the cooked components) and the CEntityTemplate's
includes, followed. Writes {entity depot path: {'meshes': {mesh depot path: count}, 'lights': {component class:
count}, 'includes', 'buffer_errors', 'include_errors'}} or {'error': ...}; w3entfill.py's --templates.
"""
import argparse
import collections
import importlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import w3patches  # noqa: E402

BS = chr(92)
PKG = 'bl_ext.user_default.witcher3_tools'
MESH = {'CMeshComponent', 'CStaticMeshComponent', 'CRigidMeshComponent'}
LIGHT = {'CPointLightComponent', 'CSpotLightComponent'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--out', default=None)
    a = ap.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    out_path = a.out or os.path.join(a.work, 'entity_templates.json')
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    cf = importlib.import_module(PKG + '.CR2W.CR2W_file')
    ct = importlib.import_module(PKG + '.CR2W.CR2W_types')
    bs = importlib.import_module(PKG + '.CR2W.bStream')
    w3patches.apply()

    def read(path):
        return cf.read_CR2W(mat.materialize_repo_path(str(path).replace('/', BS), source_game='w3'))

    def chunks_of(f):
        c = f.CHUNKS
        return c.CHUNKS if hasattr(c, 'CHUNKS') else c

    def walk(f, acc, depth=0):
        for c in chunks_of(f):
            if c.Type in MESH:
                try:
                    m = cf._first_handle_depot_path(c.GetVariableByName('mesh'))
                except Exception:  # noqa: BLE001
                    m = None
                if m:
                    acc['meshes'][m.lower()] += 1
            elif c.Type in LIGHT:
                acc['lights'][c.Type] += 1
            elif c.Type == 'CEntityTemplate':
                inc = c.GetVariableByName('includes') if hasattr(c, 'GetVariableByName') else None
                for h in (getattr(inc, 'Handles', None) or []):
                    dp = getattr(h, 'DepotPath', None)
                    if dp and depth < 4:
                        acc['includes'] += 1
                        try:
                            walk(read(dp), acc, depth + 1)
                        except Exception:  # noqa: BLE001
                            acc['include_errors'] += 1
            sd = c.GetVariableByName('streamingDataBuffer') if hasattr(c, 'GetVariableByName') else None
            if sd is not None and depth < 6:
                try:
                    data = cf._extract_streaming_buffer_bytes(f, sd)
                    if data:
                        walk(ct.getCR2W(bs.bReadStream(data, name='DATA_BUFFER')), acc, depth + 1)
                except Exception:  # noqa: BLE001
                    acc['buffer_errors'] += 1

    pl = json.load(open(os.path.join(a.work, 'placements.json'), encoding='utf-8'))
    paths = set()
    for r in pl['placements']:
        if r.get('entity'):
            paths.add(str(r['entity']).lower())
    for r in pl['lights']:
        e = (r.get('props') or {}).get('witcher_entity_path')
        if e:
            paths.add(str(e).lower())
    paths = sorted(p for p in paths if not p.endswith('.w2l'))
    out = {}
    for i, p in enumerate(paths):
        acc = {'meshes': collections.Counter(), 'lights': collections.Counter(), 'includes': 0, 'buffer_errors': 0,
               'include_errors': 0}
        try:
            walk(read(p), acc)
            out[p] = {k: (dict(v) if isinstance(v, collections.Counter) else v) for k, v in acc.items()}
        except Exception as e:  # noqa: BLE001
            out[p] = {'error': repr(e)[:200]}
        if i % 50 == 0:
            print('W3ENTTMPL', i, len(paths), flush=True)
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
    print('W3ENTTMPL_DONE', len(out), sum(1 for v in out.values() if 'error' in v), 'errors ->', out_path)


main()
