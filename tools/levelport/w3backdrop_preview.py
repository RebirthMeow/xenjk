"""w3backdrop_preview.py -- render a w3gltf work folder's placements textured (diffuse only), from a game-space eye.

A look check for backdrop dumps (the horizon rings past the terrain): rebuilds each placement's lowest LOD from its
glb (game space, metres, +Z up) with its slots' Diffuse image (materials.json), and renders Workbench views: one
top-down orthographic and --views perspective views around the eye. Optionally adds a w3terrain dump (tiles.npz) as
grey ground for scale.

    blender -b --factory-startup --python tools/levelport/w3backdrop_preview.py -- --work DIR --out DIR
        [--eye X Y Z] [--views 4] [--terrain DIR/terrain_far] [--terrain-step 16] [--lens 24] [--pitch 0.08]
"""
import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gltfio  # noqa: E402


def main():
    import bpy
    from mathutils import Matrix, Vector
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--eye', type=float, nargs=3, default=(-535.0, -1327.5, 150.0),
                    help='game space (m); default the city build origin, 150 m up')
    ap.add_argument('--views', type=int, default=4)
    ap.add_argument('--lens', type=float, default=24.0)
    ap.add_argument('--pitch', type=float, default=0.08, help='view direction z (up) per unit of horizontal')
    ap.add_argument('--terrain', help="a w3terrain.py folder (tiles.npz) drawn grey under the backdrop")
    ap.add_argument('--terrain-step', type=int, default=16)
    ap.add_argument('--size', type=int, nargs=2, default=(1600, 800))
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    work = a.work
    pl = json.load(open(os.path.join(work, 'placements.json'), encoding='utf-8'))['placements']
    index = json.load(open(os.path.join(work, 'meshes', 'index.json'), encoding='utf-8'))
    mats = json.load(open(os.path.join(work, 'materials.json'), encoding='utf-8'))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bmats = {}

    def material(slot):
        if slot in bmats:
            return bmats[slot]
        m = bpy.data.materials.new(slot or 'none')
        m.use_nodes = True
        rec = mats.get(slot) or {}
        diff = next((im for im in rec.get('images') or [] if im.get('node') == 'Diffuse'), None)
        if diff and os.path.exists(diff['file']):
            tex = m.node_tree.nodes.new('ShaderNodeTexImage')
            tex.image = bpy.data.images.load(diff['file'])
            bsdf = m.node_tree.nodes['Principled BSDF']
            m.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
        bmats[slot] = m
        return m

    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for n, p in enumerate(pl):
        rec = index.get(p['mesh'])
        if rec is None:
            continue
        doc = gltfio.Document(os.path.join(work, 'meshes', rec['file']))
        prims = [pr for m in doc.doc['meshes'] for pr in m['primitives']]
        l0 = min(pr.get('extras', {}).get('lod', 0) for pr in prims)
        M = np.array(p['matrix'], dtype=np.float64)
        for k, pr in enumerate(pr for pr in prims if pr.get('extras', {}).get('lod', 0) == l0):
            P = np.asarray(doc.accessor(pr['attributes']['POSITION']), dtype=np.float64)
            T = np.asarray(doc.accessor(pr['indices'])).reshape(-1, 3)
            W = P @ M[:3, :3].T + M[:3, 3]
            me = bpy.data.meshes.new('%d_%d' % (n, k))
            me.from_pydata(W.tolist(), [], T.tolist())
            if 'TEXCOORD_0' in pr['attributes']:
                uv = np.asarray(doc.accessor(pr['attributes']['TEXCOORD_0']), dtype=np.float64).copy()
                uv[:, 1] = 1.0 - uv[:, 1]
                lay = me.uv_layers.new(name='uv')
                lay.data.foreach_set('uv', uv[T.ravel()].ravel())
            mi = pr.get('extras', {}).get('material')
            slots = p['slots'] or [None]
            slot = slots[mi] if isinstance(mi, int) and mi < len(slots) else slots[min(k, len(slots) - 1)]
            me.materials.append(material(slot))
            ob = bpy.data.objects.new(me.name, me)
            scene.collection.objects.link(ob)
            lo, hi = np.minimum(lo, W.min(0)), np.maximum(hi, W.max(0))

    if a.terrain:
        z = np.load(os.path.join(a.terrain, 'tiles.npz'))
        grey = bpy.data.materials.new('terrain')
        grey.diffuse_color = (0.35, 0.33, 0.3, 1.0)
        s = a.terrain_step
        for key in z['tiles']:
            G = z['grid_' + str(key)][::s, ::s].astype(np.float64)
            r, c = G.shape[:2]
            ids = np.arange(r * c).reshape(r, c)
            q = np.stack([ids[:-1, :-1], ids[:-1, 1:], ids[1:, 1:], ids[1:, :-1]], -1).reshape(-1, 4)
            me = bpy.data.meshes.new('t' + str(key))
            me.from_pydata(G.reshape(-1, 3).tolist(), [], q.tolist())
            me.materials.append(grey)
            ob = bpy.data.objects.new(me.name, me)
            scene.collection.objects.link(ob)

    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.render.resolution_x, scene.render.resolution_y = a.size
    scene.world = bpy.data.worlds.new('w')
    scene.world.color = (0.55, 0.7, 0.85)
    cam_data = bpy.data.cameras.new('cam')
    cam_data.clip_start, cam_data.clip_end = 1.0, 50000.0
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    shots = []
    cam_data.type = 'ORTHO'
    cen = (lo + hi) / 2
    cam_data.ortho_scale = float(max(hi[0] - lo[0], (hi[1] - lo[1]) * a.size[0] / a.size[1])) * 1.05
    cam.matrix_world = Matrix.Translation(Vector((cen[0], cen[1], hi[2] + 1000)))
    scene.render.filepath = os.path.join(a.out, 'top.png')
    bpy.ops.render.render(write_still=True)
    shots.append('top.png')
    cam_data.type = 'PERSP'
    cam_data.lens = a.lens
    eye = Vector(a.eye)
    for i in range(a.views):
        yaw = 2 * math.pi * i / a.views          # 0: looking +Y (north), then clockwise
        d = Vector((math.sin(yaw), math.cos(yaw), a.pitch))
        cam.matrix_world = Matrix.Translation(eye) @ d.to_track_quat('-Z', 'Y').to_matrix().to_4x4()
        name = 'view_%03d.png' % round(math.degrees(yaw))
        scene.render.filepath = os.path.join(a.out, name)
        bpy.ops.render.render(write_still=True)
        shots.append(name)
    info = {'work': work, 'eye': list(a.eye), 'bounds': [lo.round(1).tolist(), hi.round(1).tolist()], 'shots': shots}
    with open(os.path.join(a.out, 'preview.json'), 'w') as f:
        json.dump(info, f, indent=1)
    print('W3BACKDROP_PREVIEW', json.dumps(info), flush=True)


if __name__ == '__main__':
    main()
