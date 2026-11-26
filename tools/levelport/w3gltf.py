"""w3gltf.py -- Witcher 3 world layers into a levelport work folder (Beauclair).

Runs inside headless Blender 5.1 with dingdio's Witcher3-Blender-Tools extension (witcher3_tools), whose readers are
the only ones that read the Next-Gen bundles (WolvenKit 7.2's CLI cannot). The add-on imports the layers (it places
meshes, entity components and lights and resolves the materials); this script then reads the result back out, and
reads every placed .w2mesh again with the add-on's raw reader to keep what the Blender import drops: every LOD with its
distance, both UV sets, the raw vertex colour and the tangent frame.

    blender -b --python tools/levelport/w3gltf.py -- --layers LIST --work DIR [--allow-failures]

LIST: one layer a line, relative to dlc/bob/data/levels/bob/locations/city_of_beauclair/ (or a full depot path).
A line naming a .w2ent (full depot path) imports that entity template at the world origin (an unplaced backdrop).
A layer that fails to import or a mesh the raw reader cannot read ends the run with exit 3 after the work folder is
written (W3GLTF_DONE names them; --allow-failures: exit 0). The later steps' *.pre-*.json copies of an earlier export
in the folder are set aside (w3work.py).

Work folder (one space throughout: Witcher 3 game space, metres, +Z up, right-handed; mesh files are mesh-local in
the same axes, so they are NOT glTF's +Y up):
    placements.json  {'placements': [{mesh, kind, layer, entity, component, matrix (4x4 row-major), slots}],
                      'lights': [...], 'fx': [...], 'skipped': {...}}
    materials.json   {name: {base, params, images, source_mesh}}
    collision.json   {'shapes': [{layer, name, kind, mat, verts (world), faces}]}
    meshes/<key>.glb one per .w2mesh: a primitive per submesh, extras {lod, distance, material}; POSITION, NORMAL,
                     TANGENT (w = bitangent sign), TEXCOORD_0, TEXCOORD_1 (glTF's v-down, as the game stores them),
                     COLOR_0 (the game's bytes, not linearised)
    meshes/index.json {depot path: {file, lods: [{lod, distance, vertices, triangles}], materials}}
    summary.json     counts and per-layer import status
"""
import argparse
import collections
import hashlib
import importlib
import json
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gltfio  # noqa: E402
import w3work  # noqa: E402

BS = chr(92)
CITY = 'dlc/bob/data/levels/bob/locations/city_of_beauclair/'
PKG = 'bl_ext.user_default.witcher3_tools'


def depot(p):
    """A depot path, lower case with backslashes, from an absolute uncook path or a depot path."""
    if not p:
        return None
    p = str(p).replace('/', BS)
    low = p.lower()
    i = low.find(BS + 'uncook' + BS)
    if i >= 0:
        p = p[i + len('uncook') + 2:]
    return p.lower()


def mesh_key(path):
    stem = path.rsplit(BS, 1)[-1].rsplit('.', 1)[0][:40]
    return stem + '_' + hashlib.sha1(path.encode('utf-8')).hexdigest()[:10]


def plain(v):
    """IDProperty values as JSON."""
    if hasattr(v, 'to_dict'):
        return {k: plain(x) for k, x in v.to_dict().items()}
    if hasattr(v, 'to_list'):
        return [plain(x) for x in v.to_list()]
    if isinstance(v, dict):
        return {k: plain(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [plain(x) for x in v]
    if isinstance(v, (int, float, str, bool)) or v is None:
        return v
    return str(v)


def image_file(img):
    import bpy
    p = bpy.path.abspath(img.filepath, library=img.library) if img.filepath else ''
    return os.path.normpath(p) if p else (img.name if img.packed_file else '')


def socket_source(sock, depth=0):
    """What feeds a material node input, resolved the way the add-on wired it: the full parameter chain (instance,
    .w2mi, .w2mg defaults) ends up as these links and values. An image: its file, colour space, the output used
    (Color or Alpha) and the UV map or node feeding it; a value/colour node: its value; an unlinked input: its default;
    another group (a texture array): its own inputs."""
    if not sock.is_linked:
        dv = getattr(sock, 'default_value', None)
        if dv is None:
            return None
        try:
            return [float(x) for x in dv] if hasattr(dv, '__len__') and not isinstance(dv, str) else plain(dv)
        except TypeError:
            return str(dv)
    link = sock.links[0]
    n = link.from_node
    d = {'node': n.type, 'name': n.name, 'out': link.from_socket.name}
    if n.type == 'TEX_IMAGE':
        if n.image:
            d['image'] = image_file(n.image)
            d['colorspace'] = n.image.colorspace_settings.name
            d['packed'] = bool(n.image.packed_file)
        d['extension'] = n.extension
        if n.inputs and n.inputs[0].is_linked and depth < 4:
            d['uv'] = socket_source(n.inputs[0], depth + 1)
    elif n.type == 'UVMAP':
        d['uvmap'] = n.uv_map
    elif n.type in ('VERTEX_COLOR', 'ATTRIBUTE'):
        d['attribute'] = getattr(n, 'layer_name', None) or getattr(n, 'attribute_name', None)
    elif n.type == 'VALUE':
        d['value'] = float(n.outputs[0].default_value)
    elif n.type == 'RGB':
        d['value'] = [float(x) for x in n.outputs[0].default_value]
    elif n.type == 'GROUP' and n.node_tree is not None:
        d['group'] = n.node_tree.name
        if depth < 4:
            d['in'] = {i.name: socket_source(i, depth + 1) for i in n.inputs}
    elif depth < 4:
        if n.type == 'MATH' or n.type == 'VECT_MATH':
            d['operation'] = n.operation
        d['in'] = {i.identifier: socket_source(i, depth + 1) for i in n.inputs if i.is_linked or n.type in (
            'COMBXYZ', 'MATH', 'VECT_MATH', 'MAPPING')}
    return d


def material_nodes(m):
    """Every group node of a material with its inputs resolved (socket_source), and what drives the output."""
    out = {}
    if not m.node_tree:
        return out
    for n in m.node_tree.nodes:
        if n.type == 'GROUP' and n.node_tree is not None:
            out[n.name] = {'tree': n.node_tree.name, 'inputs': {i.name: socket_source(i) for i in n.inputs}}
    return out


def material_record(m):
    """materials.json's record of a Blender material the add-on made"""
    imgs = []
    if m.node_tree:
        for n in m.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                imgs.append({'node': n.name, 'label': n.label, 'image': n.image.name,
                             'file': n.image.filepath, 'colorspace': n.image.colorspace_settings.name})
    return {'base': plain(m.get('witcher3_mat_base')),
            'params': plain(m.get('witcher3_mat_params')),
            'source': plain(m.get('witcher3_mat_source_path')),
            'source_mesh': plain(m.get('w3_source_mesh_path')),
            'groups': sorted({n.node_tree.name for n in (m.node_tree.nodes if m.node_tree else [])
                              if n.type == 'GROUP' and n.node_tree}),
            'blend_method': getattr(m, 'blend_method', None),
            'nodes': material_nodes(m),
            'images': imgs}


def is_collision(o):
    while o is not None:
        if o.get('witcher_layer_visibility_kind') == 'collision':
            return True
        o = o.parent
    return False


def shape_kind(o):
    nm = o.name.split('.')[0]
    for s in ('box', 'sphere', 'capsule', 'col', 'tri'):
        if nm.endswith('_' + s):
            return {'col': 'convex', 'tri': 'trimesh'}.get(s, s)
    return 'other'


def layer_paths(list_file):
    """A layer list's depot paths (forward slashes): one a line, relative to CITY unless it starts with dlc/; blank lines
    and # comments skipped"""
    layers = []
    with open(list_file, encoding='utf-8') as f:
        for line in f:
            s = line.strip().replace(BS, '/')
            if s and not s.startswith('#'):
                layers.append(s if s.lower().startswith('dlc/') else CITY + s)
    return layers


def main():
    import bpy
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--layers', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--save-blend', action='store_true', help='also save the imported scene (large)')
    ap.add_argument('--allow-failures', action='store_true', help='exit 0 when layers or meshes failed')
    a = ap.parse_args(argv)

    work = a.work
    os.makedirs(os.path.join(work, 'meshes'), exist_ok=True)
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    dcm = importlib.import_module(PKG + '.CR2W.dc_mesh')
    import w3patches
    w3patches.apply()

    layers = layer_paths(a.layers)

    # ---- 1-2. the add-on imports each layer; its objects are read back and removed before the next layer
    # (2026-11-24: a scene of thousands of objects slows the add-on: one layer took 16 min in a filled scene, 6.4 in a
    # fresh one; Blender helpers such as Object.children scan every object). Meshes and materials stay for reuse.
    placements, lights, fx, shapes = [], [], [], []
    materials = {}
    skipped = collections.Counter()
    blender_nv = {}

    def readback(objs):
        for o in objs:
            lay = o.get('lp_layer')
            M = [list(map(float, r)) for r in o.matrix_world]
            if o.type == 'LIGHT':
                L = o.data
                rec = {'layer': lay, 'name': o.name, 'type': L.type, 'matrix': M, 'color': list(L.color),
                       'energy': float(L.energy), 'props': {k: plain(o[k]) for k in o.keys() if k.startswith('witcher')}}
                if L.type == 'SPOT':
                    rec['spot_size'] = float(L.spot_size)
                    rec['spot_blend'] = float(L.spot_blend)
                lights.append(rec)
                continue
            if o.type == 'EMPTY' or o.get('witcher_particle_system') is not None:
                # particle systems: placeholder empties and the add-on's preview meshes (embers, smoke, flames)
                if o.get('witcher_particle_view_root') or o.get('witcher_particle_system') is not None or \
                        'Particle' in o.name:
                    fx.append({'layer': lay, 'name': o.name, 'matrix': M,
                               'props': {k: plain(o[k]) for k in o.keys() if k.startswith('witcher')}})
                continue
            if o.type != 'MESH':
                skipped['type ' + o.type] += 1
                continue
            if is_collision(o):
                me = o.data
                W = np.array([(o.matrix_world @ v.co)[:] for v in me.vertices], dtype=np.float64)
                shapes.append({'layer': lay, 'name': o.name, 'kind': shape_kind(o),
                               'mat': me.materials[0].name.split('.')[0] if me.materials and me.materials[0] else None,
                               'verts': W.round(4).tolist(), 'faces': [list(f.vertices) for f in me.polygons]})
                continue
            path = depot(o.get('witcher_path')) or depot(o.get('witcher_resolved_mesh_path')) or depot(o.get('repo_path'))
            if not path:
                skipped['mesh without a source path'] += 1
                continue
            kind = path.rsplit('.', 1)[-1]
            slots = []
            for s in o.material_slots:
                m = s.material
                if m is None:
                    slots.append(None)
                    continue
                slots.append(m.name)
                # read once every layer is in (material_record): the add-on completes some materials' node
                # graphs while it imports later layers (79 of 473 read early lacked their graph's inputs)
                materials.setdefault(m.name, None)
            placements.append({'mesh': path, 'kind': kind, 'layer': lay, 'name': o.name, 'matrix': M,
                               'entity': depot(o.get('witcher_entity_path')), 'component': o.get('witcher_type'),
                               'expected_materials': o.get('witcher_expected_material_count'), 'slots': slots,
                               'leaf_cards': any(md.type == 'NODES' and md.node_group and
                                                 md.node_group.name.startswith('Leaf_Card') for md in o.modifiers),
                               'cast_shadows': plain(o.get('witcher_drawableFlags_has_DF_CastShadows'))})
            if kind == 'w2mesh':
                blender_nv.setdefault(path, len(o.data.vertices))

    bpy.ops.wm.read_homefile(use_empty=True)
    t0 = time.time()
    status = {}
    for i, lay in enumerate(layers):
        before = set(bpy.data.objects.keys())
        t = time.time()
        try:
            p = mat.materialize_repo_path(lay.replace('/', BS), source_game='w3')
            if lay.lower().endswith('.w2ent'):
                # an entity template no layer places, at the world origin: Toussaint's bcg_mountains.w2ent holds the
                # horizon ring (bob_background_merged.w2mesh) with its vertices already in world space
                bpy.ops.witcher.import_w2ent('EXEC_DEFAULT', filepath=p)
            else:
                bpy.ops.witcher.import_w2l('EXEC_DEFAULT', filepath=p, do_import_Mesh=True, do_import_Collision=True,
                                           do_import_RigidBody=True, do_import_Redcloth=False, do_import_Redapex=False,
                                           keep_lod_meshes=False)
            st = 'ok'
        except Exception as e:  # noqa: BLE001 - a failed layer is reported, the rest go on
            st = 'fail ' + repr(e)[:300]
        rel = lay[len(CITY):] if lay.lower().startswith(CITY) else lay
        new = [n for n in bpy.data.objects.keys() if n not in before]
        objs = [bpy.data.objects[n] for n in new]
        for o in objs:
            o['lp_layer'] = rel
        readback(objs)
        bpy.data.batch_remove(objs)
        status[rel] = {'status': st, 'objects': len(new), 'seconds': round(time.time() - t, 1)}
        print(f'W3GLTF layer {i + 1}/{len(layers)} {rel} {st[:60]} objects={len(new)} {time.time() - t:.0f}s',
              flush=True)
    t_import = time.time() - t0
    for name in materials:
        materials[name] = material_record(bpy.data.materials[name])

    # ---- 3. every placed .w2mesh read raw, all LODs
    dcm._srgb2lin_vec = lambda s: s          # keep the game's colour bytes (blend weights, not colours)
    index = {}
    mesh_fail = {}
    nv_mismatch = []
    paths = sorted({p['mesh'] for p in placements if p['kind'] == 'w2mesh'})
    t1 = time.time()
    for i, path in enumerate(paths):
        try:
            ab = mat.materialize_repo_path(path, source_game='w3')
            CData, _bi, mnames, _mats, _name, _mf = dcm.load_bin_mesh(ab, keep_lod_meshes=True)
        except Exception as e:  # noqa: BLE001
            mesh_fail[path] = repr(e)[:300]
            continue
        w = gltfio.Writer()
        prims, lods = [], collections.defaultdict(lambda: {'vertices': 0, 'triangles': 0, 'distance': 0.0})
        for md in CData.meshDataAllMeshes:
            mi = md.meshInfo
            pos = np.asarray(md.vertex3DCoords, dtype=np.float64).reshape(-1, 3)
            if len(pos) == 0:
                continue
            nrm = np.asarray(md.normals, dtype=np.float64).reshape(-1, 3)
            tan = np.asarray(md.tangent_vector, dtype=np.float64).reshape(-1, 3)
            bit = np.asarray(md.extra_vectors, dtype=np.float64).reshape(-1, 3)
            sign = np.where(np.einsum('ij,ij->i', np.cross(nrm, tan), bit) < 0, -1.0, 1.0)
            uv0 = np.asarray(md.UV_vertex3DCoords, dtype=np.float32).reshape(-1, 2).copy()
            uv0[:, 1] = 1.0 - uv0[:, 1]              # the reader flips v for Blender; glTF wants the game's
            uv1 = None
            if md.UV2_vertex3DCoords is not None and len(md.UV2_vertex3DCoords) == len(pos):
                uv1 = np.asarray(md.UV2_vertex3DCoords, dtype=np.float32).reshape(-1, 2).copy()
                uv1[:, 1] = 1.0 - uv1[:, 1]
            col = None
            if md.vertexColor is not None and len(md.vertexColor) == len(pos):
                col = np.clip(np.rint(np.asarray(md.vertexColor, dtype=np.float64) * 255.0), 0, 255).astype(np.uint8)
            idx = np.asarray(md.faces, dtype=np.int64).reshape(-1, 3)
            lod = int(mi.lod)
            dist = float(getattr(mi, 'distance', 0.0) or 0.0)
            prims.append({'position': pos, 'normal': nrm, 'tangent': np.column_stack([tan, sign]), 'uv': uv0,
                          'uv1': uv1, 'color': col, 'indices': idx,
                          'extras': {'lod': lod, 'distance': dist, 'material': int(mi.materialID)}})
            L = lods[lod]
            L['vertices'] += len(pos)
            L['triangles'] += len(idx)
            L['distance'] = dist
        if not prims:
            mesh_fail[path] = 'no submeshes'
            continue
        key = mesh_key(path)
        w.mesh(key, prims)
        w.node(key, mesh=0)
        w.save(os.path.join(work, 'meshes', key + '.glb'),
               extras={'source': path, 'space': 'witcher3 mesh-local, metres, +Z up', 'materials': list(mnames)})
        index[path] = {'file': key + '.glb', 'materials': list(mnames),
                       'lods': [{'lod': k, **v} for k, v in sorted(lods.items())]}
        if path in blender_nv and 0 in lods and blender_nv[path] != lods[0]['vertices']:
            nv_mismatch.append((path, blender_nv[path], lods[0]['vertices']))
        if i % 50 == 0:
            print(f'W3GLTF mesh {i + 1}/{len(paths)} {time.time() - t1:.0f}s', flush=True)

    # ---- 4. write
    w3work.new_export(work)

    def dump(name, obj):
        with open(os.path.join(work, name), 'w', encoding='utf-8') as f:
            json.dump(obj, f, indent=0)

    dump('placements.json', {'space': 'witcher3 game, metres, +Z up; matrix row-major mesh-to-world',
                             'placements': placements, 'lights': lights, 'fx': fx, 'skipped': dict(skipped)})
    dump('materials.json', materials)
    dump('collision.json', {'space': 'witcher3 game, metres, +Z up (world)', 'shapes': shapes})
    dump(os.path.join('meshes', 'index.json'), index)
    lods_n = collections.Counter(len(v['lods']) for v in index.values())
    summary = {
        'layers': status, 'import_seconds': round(t_import), 'mesh_seconds': round(time.time() - t1),
        'placements': len(placements), 'placements_by_kind': dict(collections.Counter(p['kind'] for p in placements)),
        'w2mesh_paths': len(paths), 'meshes_written': len(index), 'mesh_failures': mesh_fail,
        'lod_chains': {str(k): v for k, v in sorted(lods_n.items())},
        'triangles_lod0_unique': sum(next((L['triangles'] for L in v['lods'] if L['lod'] == 0), 0)
                                     for v in index.values()),
        'triangles_all_lods_unique': sum(L['triangles'] for v in index.values() for L in v['lods']),
        'lod0_vertex_mismatch_vs_blender': nv_mismatch[:50], 'lod0_vertex_mismatches': len(nv_mismatch),
        'materials': len(materials), 'lights': len(lights),
        'lights_by_type': dict(collections.Counter(L['type'] for L in lights)), 'fx': len(fx),
        'collision_shapes': len(shapes), 'collision_kinds': dict(collections.Counter(s['kind'] for s in shapes)),
        'skipped': dict(skipped)}
    dump('summary.json', summary)
    if a.save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(work, 'import.blend'), compress=True)
    failed = sorted(k for k, v in status.items() if v['status'] != 'ok')
    print('W3GLTF_DONE', json.dumps(dict({k: summary[k] for k in ('placements', 'meshes_written', 'lights',
                                                                  'collision_shapes', 'lod0_vertex_mismatches')},
                                         layers_failed=failed, mesh_failures=len(mesh_fail))), flush=True)
    if (failed or mesh_fail) and not a.allow_failures:
        print('W3GLTF failed: %d layers (%s), %d meshes (summary.json: layers, mesh_failures)' % (
            len(failed), ', '.join(failed[:10]), len(mesh_fail)), flush=True)
        sys.exit(3)


if __name__ == '__main__':
    main()
