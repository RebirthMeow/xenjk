"""w3flags.py -- the castle's cloth flags and banners as static meshes for a levelport work folder (Beauclair).

What the flags are: APEX cloth. The layer palace/main/solid/flags/decorations_ns.w2l holds CEntity objects with a
CClothComponent each, pointing at two cloth resources:

    dlc/bob/data/environment/decorations/gameplay/flags_banners/flag_racing_a.redcloth   8 pennants, one per pole
    dlc/ep1/environment/decorations/wedding/wedding_banner_px.redcloth                    5 garlands strung between poles

A .redcloth (CR2W) names its .apb (APEX ClothingAsset, binary NvParameterized) and its materials. The add-on converts
the .apb to an .apx (XML) with NVIDIA ParamTool (`tools/apex14/ParamToolNU.cmd -nu`, set as io_mesh_apx's
`apex_sdk_cli`); when that tool is not configured (or with --apb-python) w3patches.use_w3apb() makes it write the
same .apx with w3apb.py instead. io_mesh_apx builds the graphical mesh (LOD 0; the add-on trims the other graphical LODs and the
physical meshes), a one-bone armature and a geometry-nodes ClothSimulation modifier that it leaves switched off. The
add-on's layer importer does this when called with do_import_Redcloth / do_import_Redapex (w3gltf.py calls it with both
off, and skips the armatures, so the dump had only the poles). The 8 flag_racing_pole.w2mesh poles share each pennant
entity's transform; the 8 box_cloth.w2mesh in the same layer are blockout_base boxes the add-on hides from render.

This script imports that layer with cloth on, takes each cloth resource's graphical mesh in its bind pose (the mesh
data as authored: the armature's one bone sits at rest and the cloth solver stays off, so this is the pose the APEX
asset was built in; the script measures the armature-evaluated mesh against it), and writes the work-folder shape:

    blender -b --python tools/levelport/w3flags.py -- --out DIR [--layer L] [--apb-python] [--uncook U]
    blender -b --python tools/levelport/w3flags.py -- --out DIR --render --work WORK3   (verification only)

Writes, under --out (one space: Witcher 3 game space, metres, +Z up; meshes mesh-local in the same axes, as w3gltf):
    meshes/<key>.glb   one per .redcloth: a primitive per material, extras {lod 0, distance 0, material (slot index)};
                       POSITION, NORMAL (the add-on's custom corner normals), TANGENT (w = bitangent sign),
                       TEXCOORD_0 (game's v-down, as w3gltf writes it). Single-sided triangles, no COLOR_0.
    meshes/index.json  {redcloth depot path: {file, kind 'redcloth', apb, materials, lods: [{lod 0, ...}]}}
    materials.json     {material name: w3gltf's record (base, params, source, source_mesh, groups, blend_method, nodes,
                        images) + 'cloth': {double_sided, alpha, class}}
    placements.json    {'placements': [{mesh (the .redcloth depot path), kind 'redcloth', layer, name, matrix (4x4
                        row-major mesh-to-world), entity, component 'CClothComponent', expected_materials, slots,
                        leaf_cards False, cast_shadows}]}
    verify_poles.json  the layer's pole/box placements from the same import (mesh path, matrix), for --render only
    summary.json       counts, bind-pose check, skipped templates

kind is 'redcloth', not 'w2mesh': the source is a cloth resource; the mesh key is the .redcloth depot path. Materials:
every cloth texture's alpha is 1 (horse_racing_a_d, white_cloth, rope), so the outline is the geometry: class them
double-sided opaque, not cutout. wedding_banner's red pennants are white_cloth tinted by pbr_std's VarianceColor
(128 38 35, VarianceOffset 5); the flags have Translucency 0.7.

Verification (--render): rebuilds the flags from these files alone plus the pole glbs from WORK3/meshes (found through
WORK3/meshes/index.json, placed from verify_poles.json), with each material's Diffuse image, and renders an overview,
a close-up and a top view to DIR/render/flags_*.png.
"""
import argparse
import collections
import importlib
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gltfio  # noqa: E402
import w3gltf  # noqa: E402  (helpers only: depot, mesh_key, plain, material_nodes, image_file)

PKG = w3gltf.PKG
CITY = w3gltf.CITY
LAYER = 'palace/main/solid/flags/decorations_ns.w2l'


def ancestors(o):
    while o is not None:
        yield o
        o = o.parent


def cloth_entity(o):
    """The CEntity empty a cloth instance hangs under; None for the add-on's reuse template (an unparented copy at
    the origin it keeps per resource)."""
    for p in ancestors(o):
        if p.get('witcher_type') == 'CEntity':
            return p
    return None


def mesh_arrays(o, uv_name=None):
    """Corner-split vertex arrays of a mesh object's own data, one primitive per material slot."""
    me = o.data
    uvl = me.uv_layers[uv_name] if uv_name else me.uv_layers.active
    me.calc_tangents(uvmap=uvl.name)
    co = np.array([v.co[:] for v in me.vertices], dtype=np.float64)
    prims = []
    for k in range(max(1, len(me.materials))):
        verts, remap, tris = [], {}, []
        for p in me.polygons:
            if p.material_index != k:
                continue
            if len(p.loop_indices) != 3:
                raise ValueError(f'{o.name}: non-triangle face')
            t = []
            for li in p.loop_indices:
                lp = me.loops[li]
                n = tuple(round(x, 5) for x in me.corner_normals[li].vector)
                uv = tuple(round(x, 6) for x in uvl.data[li].uv)
                key = (lp.vertex_index, uv, n)
                if key not in remap:
                    remap[key] = len(verts)
                    verts.append((lp.vertex_index, uv, n, tuple(lp.tangent), lp.bitangent_sign))
                t.append(remap[key])
            tris.append(t)
        if not tris:
            continue
        pos = co[[v[0] for v in verts]]
        uv = np.array([v[1] for v in verts], dtype=np.float32)
        uv[:, 1] = 1.0 - uv[:, 1]                 # Blender's v-up to the game's v-down, as w3gltf stores it
        prims.append({'position': pos, 'normal': np.array([v[2] for v in verts], dtype=np.float64),
                      'tangent': np.array([v[3] + (v[4],) for v in verts], dtype=np.float64), 'uv': uv,
                      'indices': np.array(tris, dtype=np.int64), 'extras': {'lod': 0, 'distance': 0.0, 'material': k}})
    me.free_tangents()
    return prims


def bind_pose_error(o):
    """Largest distance between the mesh data and the mesh as the armature deforms it (cloth solver off)."""
    import bpy
    sims = [m for m in o.modifiers if m.type == 'NODES']
    keep = [(m, m.show_viewport) for m in sims]
    for m in sims:
        m.show_viewport = False
    dg = bpy.context.evaluated_depsgraph_get()
    e = o.evaluated_get(dg)
    me = e.to_mesh()
    a = np.array([v.co[:] for v in me.vertices])
    e.to_mesh_clear()
    for m, v in keep:
        m.show_viewport = v
    b = np.array([v.co[:] for v in o.data.vertices])
    return float(np.abs(a - b).max()) if a.shape == b.shape else float('inf')


def material_record(m, source_mesh):
    imgs = []
    for n in m.node_tree.nodes if m.node_tree else []:
        if n.type == 'TEX_IMAGE' and n.image:
            imgs.append({'node': n.name, 'label': n.label, 'image': n.image.name, 'file': n.image.filepath,
                         'colorspace': n.image.colorspace_settings.name})
    rec = {'base': w3gltf.plain(m.get('witcher3_mat_base')), 'params': w3gltf.plain(m.get('witcher3_mat_params')),
           'source': w3gltf.plain(m.get('witcher3_mat_source_path')), 'source_mesh': source_mesh,
           'groups': sorted({n.node_tree.name for n in (m.node_tree.nodes if m.node_tree else [])
                             if n.type == 'GROUP' and n.node_tree}),
           'blend_method': getattr(m, 'blend_method', None), 'nodes': w3gltf.material_nodes(m), 'images': imgs}
    # cloth is a single-sided sheet seen from both sides; cutout only if the diffuse alpha actually cuts
    alpha_min = None
    for g in rec['nodes'].values():
        src = g['inputs'].get('Alpha') or {}
        if src.get('node') == 'TEX_IMAGE':
            img = next((n.image for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.name == src.get('name')),
                       None)
            if img is not None and img.size[0]:
                alpha_min = float(np.array(img.pixels[:]).reshape(-1, 4)[:, 3].min())
    cut = alpha_min is not None and alpha_min < 0.5
    rec['cloth'] = {'double_sided': True, 'alpha_min': alpha_min, 'alpha': 'cutout' if cut else 'opaque',
                    'class': 'double-sided cutout' if cut else 'double-sided opaque'}
    return rec


def dump(a):
    import bpy
    out = a.out
    os.makedirs(os.path.join(out, 'meshes'), exist_ok=True)
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import w3patches
    w3patches.apply()
    import addon_utils
    if not addon_utils.check('io_mesh_apx')[1]:
        addon_utils.enable('io_mesh_apx', default_set=True)
    if a.uncook:
        os.makedirs(a.uncook, exist_ok=True)
        bpy.context.preferences.addons[PKG].preferences.uncook_path = a.uncook
    apx_used = w3patches.use_w3apb(force=a.apb_python)

    lay = a.layer.replace(w3gltf.BS, '/')
    full = lay if lay.lower().startswith('dlc/') else CITY + lay
    rel = full[len(CITY):] if full.lower().startswith(CITY) else full
    bpy.ops.wm.read_homefile(use_empty=True)
    p = mat.materialize_repo_path(full.replace('/', w3gltf.BS), source_game='w3')
    bpy.ops.witcher.import_w2l('EXEC_DEFAULT', filepath=p, do_import_Mesh=True, do_import_Collision=False,
                               do_import_RigidBody=False, do_import_Redcloth=True, do_import_Redapex=True,
                               keep_lod_meshes=False)

    placements, poles, materials, index = [], [], {}, {}
    skipped = collections.Counter()
    bind_err = {}
    meshes_by_res = {}
    for o in bpy.data.objects:
        M = [list(map(float, r)) for r in o.matrix_world]
        if o.type != 'MESH':
            continue
        arm_mod = next((m for m in o.modifiers if m.type == 'ARMATURE' and m.object), None)
        res = w3gltf.depot(arm_mod.object.get('witcher_redcloth_resource')) if arm_mod else None
        if not res:
            path = w3gltf.depot(o.get('witcher_resolved_mesh_path')) or w3gltf.depot(o.get('witcher_path'))
            if path and path.endswith('.w2mesh'):
                poles.append({'mesh': path, 'name': o.name, 'matrix': M, 'hide_render': bool(o.hide_render)})
            continue
        if o.data and o.data.name.startswith('OptimisedSphere'):
            # APEX collision spheres the importer adds beside some cloth (the Nilfgaardian tall flag): not drawn
            skipped['cloth collision sphere'] += 1
            continue
        ent = cloth_entity(o)
        if ent is None:
            skipped['reuse template (no CEntity parent)'] += 1
            continue
        meshes_by_res.setdefault(res, []).append(o)
        for s in o.material_slots:
            if s.material and s.material.name not in materials:
                materials[s.material.name] = material_record(s.material, res)
        placements.append({'mesh': res, 'kind': 'redcloth', 'layer': rel, 'name': o.name, 'matrix': M,
                           'entity': w3gltf.depot(ent.get('witcher_entity_path')), 'entity_name': ent.name,
                           'component': 'CClothComponent', 'expected_materials': len(o.material_slots),
                           'slots': [s.material.name if s.material else None for s in o.material_slots],
                           'leaf_cards': False, 'cast_shadows': True})

    for res, objs in sorted(meshes_by_res.items()):
        datas = {o.data.name for o in objs}
        if len(datas) != 1:
            raise RuntimeError(f'{res}: instances do not share one mesh ({sorted(datas)})')
        o = objs[0]
        bind_err[res] = max(bind_pose_error(x) for x in objs)
        prims = mesh_arrays(o)
        key = w3gltf.mesh_key(res)
        w = gltfio.Writer()
        w.mesh(key, prims)
        w.node(key, mesh=0)
        names = [s.material.name if s.material else None for s in o.material_slots]
        w.save(os.path.join(out, 'meshes', key + '.glb'),
               extras={'source': res, 'space': 'witcher3 mesh-local, metres, +Z up', 'materials': names,
                       'pose': 'APEX graphical mesh LOD0, bind pose (no simulation)'})
        apb = res[:-len('.redcloth')] + '.apb'
        index[res] = {'file': key + '.glb', 'kind': 'redcloth', 'apb': apb, 'materials': names,
                      'lods': [{'lod': 0, 'distance': 0.0, 'vertices': int(sum(len(p['position']) for p in prims)),
                                'triangles': int(sum(len(p['indices']) for p in prims))}]}

    def write(name, obj):
        with open(os.path.join(out, name), 'w', encoding='utf-8') as f:
            json.dump(obj, f, indent=0)

    write('placements.json', {'space': 'witcher3 game, metres, +Z up; matrix row-major mesh-to-world',
                              'placements': placements})
    write('materials.json', materials)
    write(os.path.join('meshes', 'index.json'), index)
    write('verify_poles.json', {'layer': rel, 'placements': poles})
    summary = {'layer': rel, 'placements': len(placements),
               'by_resource': dict(collections.Counter(p['mesh'] for p in placements)),
               'meshes': {k: v['lods'][0] for k, v in index.items()}, 'materials': len(materials),
               'material_classes': {k: v['cloth']['class'] for k, v in materials.items()},
               'bind_pose_max_error_m': bind_err, 'skipped': dict(skipped),
               'layer_w2mesh': dict(collections.Counter(p['mesh'] for p in poles))}
    write('summary.json', summary)
    print('W3FLAGS_DONE', json.dumps({k: summary[k] for k in ('placements', 'by_resource', 'bind_pose_max_error_m',
                                                              'skipped')}), 'apx', json.dumps(apx_used), flush=True)


def render(a):
    """Verification: the flags from the staged files alone, the poles from WORK3's glbs, textured, rendered."""
    import bpy
    from mathutils import Matrix, Vector
    out = os.path.join(a.out, 'render')
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    fl_index = json.load(open(os.path.join(a.out, 'meshes', 'index.json'), encoding='utf-8'))
    fl_mats = json.load(open(os.path.join(a.out, 'materials.json'), encoding='utf-8'))
    fl_pl = json.load(open(os.path.join(a.out, 'placements.json'), encoding='utf-8'))['placements']
    poles = json.load(open(os.path.join(a.out, 'verify_poles.json'), encoding='utf-8'))['placements']
    w_index = json.load(open(os.path.join(a.work, 'meshes', 'index.json'), encoding='utf-8'))

    mat_cache = {}

    def make_mat(name, diffuse, tint=None, two_sided=True):
        if name in mat_cache:
            return mat_cache[name]
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        m.use_backface_culling = not two_sided
        nt = m.node_tree
        bsdf = nt.nodes.get('Principled BSDF')
        if diffuse and os.path.exists(diffuse):
            tex = nt.nodes.new('ShaderNodeTexImage')
            tex.image = bpy.data.images.load(diffuse, check_existing=True)
            src = tex.outputs['Color']
            if tint:
                mix = nt.nodes.new('ShaderNodeMix')
                mix.data_type = 'RGBA'
                mix.blend_type = 'MULTIPLY'
                mix.inputs['Factor'].default_value = 1.0
                nt.links.new(src, mix.inputs[6])
                mix.inputs[7].default_value = tint
                src = mix.outputs[2]
            nt.links.new(src, bsdf.inputs['Base Color'])
        else:
            bsdf.inputs['Base Color'].default_value = (1, 0, 1, 1)
        bsdf.inputs['Roughness'].default_value = 0.8
        mat_cache[name] = m
        return m

    def diffuse_of(rec):
        for g in rec.get('nodes', {}).values():
            d = g['inputs'].get('Diffuse')
            if isinstance(d, dict) and d.get('image'):
                return d['image']
        return None

    def tint_of(rec):
        p = rec.get('params') or {}
        if 'VarianceColor' in p and float(p.get('VarianceOffset', 0) or 0) > 1:
            c = [float(x) / 255.0 for x in p['VarianceColor'].split(';')]
            lin = [((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c[:3]]
            m = max(lin) or 1
            return tuple(x / m for x in lin) + (1.0,)       # hue only: a preview stand-in for pbr_std's variance
        return None

    def load_glb(path, mats):
        doc = gltfio.Document(path)
        me = bpy.data.meshes.new(os.path.basename(path))
        V, F, U, MI = [], [], [], []
        for pr, (pos, _n, uv, tri, _m) in zip(doc.doc['meshes'][0]['primitives'], doc.primitives(0)):
            if (pr.get('extras') or {}).get('lod', 0) != 0:
                continue
            base = len(V)
            V.extend(pos.tolist())
            F.extend((tri + base).tolist())
            U.append((uv, tri))
            MI.extend([(pr.get('extras') or {}).get('material', 0)] * len(tri))
        me.from_pydata(V, [], F)
        uvl = me.uv_layers.new(name='UV')
        li = 0
        for uv, tri in U:
            for t in tri:
                for vi in t:
                    uvl.data[li].uv = (uv[vi][0], 1.0 - uv[vi][1])
                    li += 1
        for m in mats:
            me.materials.append(m)
        for p, k in zip(me.polygons, MI):
            p.material_index = k
        me.update()
        return me

    flag_meshes = {}
    for res, v in fl_index.items():
        mats = [make_mat(n, diffuse_of(fl_mats.get(n, {})), tint_of(fl_mats.get(n, {}))) for n in v['materials']]
        flag_meshes[res] = load_glb(os.path.join(a.out, 'meshes', v['file']), mats)
    pole_meshes = {}
    import w3paths
    wood = os.path.join(w3paths.uncook(a.w3), 'environment', 'textures_tileable', 'wood', 'village_wood_pole_big.dds')
    for p in poles:
        if p['hide_render'] or p['mesh'] in pole_meshes:
            continue
        ent = w_index.get(p['mesh'])
        if ent is None:
            print('W3FLAGS_RENDER missing pole glb in work index', p['mesh'])
            continue
        pole_meshes[p['mesh']] = load_glb(os.path.join(a.work, 'meshes', ent['file']),
                                          [make_mat('pole', wood, two_sided=False)])
    pts = []
    for p in fl_pl:
        o = bpy.data.objects.new(p['name'], flag_meshes[p['mesh']])
        o.matrix_world = Matrix(p['matrix'])
        sc.collection.objects.link(o)
        pts.extend(o.matrix_world @ Vector(c) for c in [v.co for v in o.data.vertices][::7])
    npole = 0
    for p in poles:
        if p['mesh'] in pole_meshes:
            o = bpy.data.objects.new(p['name'], pole_meshes[p['mesh']])
            o.matrix_world = Matrix(p['matrix'])
            sc.collection.objects.link(o)
            npole += 1
    lo = Vector([min(v[i] for v in pts) for i in range(3)])
    hi = Vector([max(v[i] for v in pts) for i in range(3)])
    ctr = (lo + hi) / 2
    # ground disc for context
    bpy.ops.mesh.primitive_circle_add(vertices=64, radius=30, fill_type='NGON', location=(ctr.x, ctr.y, lo.z - 4.5))
    g = bpy.context.active_object
    gm = bpy.data.materials.new('ground')
    gm.use_nodes = True
    gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.25, 0.25, 0.22, 1)
    g.data.materials.append(gm)

    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    sc.collection.objects.link(sun)
    sc.world = bpy.data.worlds.new('w')
    sc.world.use_nodes = True
    sc.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.65, 0.8, 1)
    sc.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.8
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = 48
    sc.cycles.device = 'CPU'
    sc.render.resolution_x, sc.render.resolution_y = 1600, 1000
    sc.view_settings.view_transform = 'Standard'

    def shot(name, eye, target, lens=35):
        cam.location = eye
        cam.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = lens
        sc.render.filepath = os.path.join(out, name)
        bpy.ops.render.render(write_still=True)
        print('W3FLAGS_RENDER', sc.render.filepath, flush=True)

    shot('flags_overview.png', ctr + Vector((-14, -22, 6)), ctr)
    f0 = next(p for p in fl_pl if 'flag_racing' in p['mesh'])
    c0 = Vector([f0['matrix'][i][3] for i in range(3)]) + Vector((0, 0, 4.5))
    shot('flags_closeup.png', c0 + Vector((-4.5, -6, 0.5)), c0, lens=40)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = max(hi.x - lo.x, hi.y - lo.y) * 1.25
    shot('flags_top.png', ctr + Vector((0, 0, 40)), ctr)
    with open(os.path.join(out, 'render.json'), 'w') as f:
        json.dump({'flags': len(fl_pl), 'poles': npole, 'bounds': [list(lo), list(hi)],
                   'images': sorted(os.listdir(out))}, f, indent=1)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--layer', default=LAYER)
    ap.add_argument('--w3', help='--render: a research folder whose uncook/ holds the pole texture (default '
                    '$W3_UNCOOK, else $W3_RESEARCH/uncook; see w3paths.py)')
    ap.add_argument('--apb-python', action='store_true',
                    help='convert the cloth .apb to .apx with w3apb.py even when ParamTool is configured (without '
                    'ParamTool w3apb.py is used anyway)')
    ap.add_argument('--uncook', help="set the add-on's uncook folder for this session (a folder of its own, so "
                    'parallel imports do not race on the files they materialize)')
    ap.add_argument('--render', action='store_true', help='verification render from the staged files')
    ap.add_argument('--work', help='work folder whose meshes/index.json has the pole glbs (--render)')
    a = ap.parse_args(argv)
    a.out = os.path.abspath(a.out)
    if a.render:
        render(a)
    else:
        dump(a)


if __name__ == '__main__':
    main()
