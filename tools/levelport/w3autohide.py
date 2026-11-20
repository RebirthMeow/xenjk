"""w3autohide.py -- the game's hide distances of a Witcher 3 work folder's placed meshes (Beauclair).

Runs inside headless Blender 5.1 with dingdio's Witcher3-Blender-Tools extension (its CR2W readers; w3gltf.py's setup),
reads only headers and properties (no geometry), and writes <work>/autohide.json, which w3scene.py joins:

    blender -b --python tools/levelport/w3autohide.py -- --work DIR [--work DIR2 ...]

What it reads (all as stored in the uncooked depot; nothing inferred):
    mesh       CMesh.autoHideDistance of every placed .w2mesh (metres; None when the property is not serialised: the
               class default, which the files do not carry; the add-on's reader fills in 20.0, its own guess)
    sector     every Mesh / RigidBody block of each layer's CSectorData: SBlockDataMeshObject.forceAutoHide (uint16, m;
               0 = none) and SBlockData.streamingRadius (uint16, m), keyed by layer, mesh and position (1 cm)
    components every mesh component in each layer entity's streamingDataBuffer (CMeshComponent, CStaticMeshComponent,
               CRigidMeshComponent, ...): forceAutoHideDistance (uint16, m), with the entity's streamingDistance (uint8)

autohide.json:
    {'mesh': {depot path: autoHideDistance or None},
     'sector': {layer: {mesh: [[x, y, z, forceAutoHide, streamingRadius, flags], ...]}},
     'component': {layer: {mesh: [[forceAutoHideDistance, entity streamingDistance, component type], ...]}},
     'summary': {...}}
"""
import argparse
import collections
import importlib
import json
import os
import sys
import time

BS = chr(92)
CITY = 'dlc/bob/data/levels/bob/locations/city_of_beauclair/'
PKG = 'bl_ext.user_default.witcher3_tools'
MESH_COMPONENTS = {'CMeshComponent', 'CStaticMeshComponent', 'CRigidMeshComponent', 'CDynamicFoliageComponent',
                   'CSwitchableFoliageComponent', 'CClothComponent', 'CDestructionSystemComponent',
                   'CDestructionComponent', 'CMorphedMeshComponent', 'CPhantomComponent'}


def chunks_of(cr2w):
    c = cr2w.CHUNKS
    return c.CHUNKS if hasattr(c, 'CHUNKS') else c


def value(chunk, name):
    p = chunk.GetVariableByName(name) if hasattr(chunk, 'GetVariableByName') else None
    if p is None:
        return None
    v = getattr(p, 'Value', None)
    return v if isinstance(v, (int, float)) else None


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', action='append', required=True, help='work folder (placements.json, meshes/index.json)')
    a = ap.parse_args(argv)

    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    cf = importlib.import_module(PKG + '.CR2W.CR2W_file')
    ct = importlib.import_module(PKG + '.CR2W.CR2W_types')
    bs = importlib.import_module(PKG + '.CR2W.bStream')
    en = importlib.import_module(PKG + '.CR2W.CR2W_helpers').Enums
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import w3patches
    w3patches.apply()

    for work in a.work:
        t0 = time.time()
        pl = json.load(open(os.path.join(work, 'placements.json'), encoding='utf-8'))['placements']
        meshes = sorted({p['mesh'] for p in pl if p['kind'] == 'w2mesh' and p['mesh'].endswith('.w2mesh')})
        layers = sorted({p['layer'] for p in pl if p.get('layer') and p['layer'].lower().endswith('.w2l')})
        summary = collections.Counter()

        # ---- meshes: CMesh.autoHideDistance
        mesh_out = {}
        for i, path in enumerate(meshes):
            try:
                f = cf.read_CR2W(mat.materialize_repo_path(path, source_game='w3'))
                cm = next((c for c in chunks_of(f) if c.Type == 'CMesh'), None)
                mesh_out[path] = value(cm, 'autoHideDistance') if cm is not None else None
                summary['mesh_read'] += 1
                summary['mesh_has_autohide' if mesh_out[path] is not None else 'mesh_no_autohide'] += 1
            except Exception as e:  # noqa: BLE001 - counted
                summary['mesh_fail'] += 1
                print('W3AUTOHIDE mesh fail', path, repr(e)[:200], flush=True)
            if i % 500 == 0:
                print(f'W3AUTOHIDE mesh {i + 1}/{len(meshes)} {time.time() - t0:.0f}s', flush=True)

        # ---- layers: sector blocks and entity components
        sector, comp = {}, {}
        for i, lay in enumerate(layers):
            full = lay if lay.lower().startswith('dlc/') else CITY + lay
            try:
                f = cf.read_CR2W(mat.materialize_repo_path(full.replace('/', BS), source_game='w3'))
            except Exception as e:  # noqa: BLE001
                summary['layer_fail'] += 1
                print('W3AUTOHIDE layer fail', lay, repr(e)[:200], flush=True)
                continue
            ch = chunks_of(f)
            S = sector.setdefault(lay, {})
            for c in ch:
                if c.Type != 'CSectorData':
                    continue
                res = c.Resources
                for b in c.BlockData or []:
                    if b.packedObjectType not in (en.BlockDataObjectType.Mesh, en.BlockDataObjectType.RigidBody):
                        continue
                    try:
                        mp = str(res[b.packedObject.meshIndex].pathHash or '').lower()
                    except Exception:  # noqa: BLE001
                        summary['sector_unresolved'] += 1
                        continue
                    P = b.position
                    S.setdefault(mp, []).append([round(float(P.x), 2), round(float(P.y), 2), round(float(P.z), 2),
                                                 int(b.packedObject.forceAutoHide), int(b.streamingRadius),
                                                 int(b.flags)])
                    summary['sector_blocks'] += 1
                    summary['sector_force' if b.packedObject.forceAutoHide else 'sector_no_force'] += 1
            C = comp.setdefault(lay, {})
            for c in ch:
                sd = c.GetVariableByName('streamingDataBuffer') if hasattr(c, 'GetVariableByName') else None
                if sd is None:
                    continue
                ent_sd = value(c, 'streamingDistance')
                try:
                    data = cf._extract_streaming_buffer_bytes(f, sd)
                    if not data:
                        continue
                    sub = ct.getCR2W(bs.bReadStream(data, name='DATA_BUFFER'))
                except Exception as e:  # noqa: BLE001
                    summary['entity_buffer_fail'] += 1
                    continue
                summary['entities'] += 1
                for sc in chunks_of(sub):
                    if sc.Type not in MESH_COMPONENTS:
                        continue
                    mp = cf._first_handle_depot_path(sc.GetVariableByName('mesh') or sc.GetVariableByName('resource'))
                    if not mp:
                        continue
                    fa = value(sc, 'forceAutoHideDistance')
                    C.setdefault(mp.lower(), []).append([int(fa or 0), ent_sd, sc.Type])
                    summary['components'] += 1
                    summary['component_force' if fa else 'component_no_force'] += 1
            if i % 20 == 0:
                print(f'W3AUTOHIDE layer {i + 1}/{len(layers)} {lay} {time.time() - t0:.0f}s', flush=True)

        out = {'source': 'w3autohide.py: CMesh.autoHideDistance, SBlockDataMeshObject.forceAutoHide, '
                         'SBlockData.streamingRadius, component forceAutoHideDistance, CEntity.streamingDistance '
                         '(uncooked depot, metres)',
               'mesh': mesh_out, 'sector': sector, 'component': comp, 'summary': dict(summary)}
        with open(os.path.join(work, 'autohide.json'), 'w', encoding='utf-8') as fo:
            json.dump(out, fo, indent=0)
        print('W3AUTOHIDE_DONE', work, json.dumps(dict(summary)), f'{time.time() - t0:.0f}s', flush=True)


if __name__ == '__main__':
    main()
