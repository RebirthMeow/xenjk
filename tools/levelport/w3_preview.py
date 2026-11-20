"""w3_preview.py -- rebuild a w3gltf work folder in headless Blender from its own files and render it (a check that
the dump alone reproduces the level, independent of the add-on's scene).

    blender -b --python tools/levelport/w3_preview.py -- --work DIR [--lod 0] [--out DIR] [--colors]

Places every placement's mesh at the chosen LOD (or its last LOD if it has fewer) and the collision shapes, then
renders Workbench views: top, two perspectives, and the collision alone. --colors shades by COLOR_0 (the blend
weights) instead of solid. Writes preview_*.png and preview.json (bounds, counts).
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
    ap.add_argument('--lod', type=int, default=0)
    ap.add_argument('--out')
    ap.add_argument('--colors', action='store_true')
    ap.add_argument('--res', type=int, default=1600)
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out or a.work)     # Blender reads a relative render path against the drive root
    os.makedirs(out, exist_ok=True)

    pl = json.load(open(os.path.join(a.work, 'placements.json'), encoding='utf-8'))
    index = json.load(open(os.path.join(a.work, 'meshes', 'index.json'), encoding='utf-8'))
    col = json.load(open(os.path.join(a.work, 'collision.json'), encoding='utf-8'))

    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    vis = bpy.data.collections.new('visual')
    cls = bpy.data.collections.new('collision')
    sc.collection.children.link(vis)
    sc.collection.children.link(cls)

    meshes = {}
    tris = 0
    missing = 0
    for p in pl['placements']:
        rec = index.get(p['mesh'])
        if rec is None:
            missing += 1
            continue
        if p['mesh'] not in meshes:
            doc = gltfio.Document(os.path.join(a.work, 'meshes', rec['file']))
            prims = doc.doc['meshes'][0]['primitives']
            lods = sorted({pr.get('extras', {}).get('lod', 0) for pr in prims})
            want = a.lod if a.lod in lods else lods[-1]
            P, F, C = [], [], []
            base = 0
            for pr, (pos, _n, _uv, tri, _m), c in zip(prims, doc.primitives(0), doc.colors(0)):
                if pr.get('extras', {}).get('lod', 0) != want:
                    continue
                P.append(pos)
                F.append(tri + base)
                C.append(c if c is not None else np.ones((len(pos), 4)))
                base += len(pos)
            P, F, C = np.concatenate(P), np.concatenate(F), np.concatenate(C)
            me = bpy.data.meshes.new(rec['file'])
            me.vertices.add(len(P))
            me.vertices.foreach_set('co', P.astype(np.float32).ravel())
            me.loops.add(len(F) * 3)
            me.loops.foreach_set('vertex_index', F.astype(np.int32).ravel())
            me.polygons.add(len(F))
            me.polygons.foreach_set('loop_start', np.arange(0, len(F) * 3, 3, dtype=np.int32))
            me.polygons.foreach_set('loop_total', np.full(len(F), 3, dtype=np.int32))
            ca = me.color_attributes.new('blend', 'BYTE_COLOR', 'POINT')
            ca.data.foreach_set('color', C.astype(np.float32).ravel())
            me.update()
            meshes[p['mesh']] = (me, len(F))
        me, nt = meshes[p['mesh']]
        o = bpy.data.objects.new('p', me)
        o.matrix_world = Matrix(p['matrix'])
        vis.objects.link(o)
        tris += nt

    tpath = os.path.join(a.work, 'terrain', 'tiles.npz')
    if os.path.exists(tpath):          # w3terrain.py's tiles, every 2nd vertex, in both views
        z = np.load(tpath)
        for k in z.files:
            if not k.startswith('grid_'):
                continue
            G = z[k][::2, ::2]
            n, m = G.shape[:2]
            ii, jj = np.meshgrid(np.arange(n - 1), np.arange(m - 1), indexing='ij')
            q = (ii * m + jj).ravel()
            faces = np.stack([q, q + 1, q + m + 1, q + m], 1).tolist()
            for coll in (vis, cls):
                me = bpy.data.meshes.new(k)
                me.from_pydata(G.reshape(-1, 3).tolist(), [], faces)
                o = bpy.data.objects.new(k, me)
                coll.objects.link(o)

    for s in col['shapes']:
        me = bpy.data.meshes.new('c')
        me.from_pydata(s['verts'], [], s['faces'])
        o = bpy.data.objects.new('c', me)
        cls.objects.link(o)

    bpy.context.view_layer.update()
    lo = np.full(3, 1e18)
    hi = np.full(3, -1e18)
    for o in vis.objects:
        for c in o.bound_box:
            w = np.array((o.matrix_world @ Vector(c))[:])
            lo = np.minimum(lo, w)
            hi = np.maximum(hi, w)
    centre = (lo + hi) / 2
    size = float(np.max(hi - lo))

    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'VERTEX' if a.colors else 'SINGLE'
    sc.display.shading.show_cavity = True
    sc.display.shading.show_shadows = True
    sc.render.resolution_x = a.res
    sc.render.resolution_y = int(a.res * 0.75)
    sc.render.film_transparent = False
    cam_data = bpy.data.cameras.new('cam')
    cam = bpy.data.objects.new('cam', cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam_data.clip_end = size * 10

    def look(eye, target):
        d = Vector(target) - Vector(eye)
        cam.location = Vector(eye)
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()

    def shot(name):
        sc.render.filepath = os.path.join(out, f'preview_{name}.png')
        bpy.ops.render.render(write_still=True)

    cls.hide_render = True
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = size * 1.05
    look(centre + np.array([0, 0, size * 2]), centre)
    shot('top')
    cam_data.type = 'PERSP'
    cam_data.lens = 35
    for name, yaw in (('persp_sw', 225), ('persp_ne', 45)):
        r = size * 0.9
        eye = centre + np.array([r * math.cos(math.radians(yaw)), r * math.sin(math.radians(yaw)), size * 0.45])
        look(eye, centre)
        shot(name)
    vis.hide_render = True
    cls.hide_render = False
    look(centre + np.array([size * 0.9 * math.cos(math.radians(225)), size * 0.9 * math.sin(math.radians(225)),
                            size * 0.45]), centre)
    shot('collision_sw')

    info = {'placements': len(pl['placements']), 'missing_meshes': missing, 'meshes': len(meshes),
            'triangles_placed': tris, 'collision_shapes': len(col['shapes']), 'lod': a.lod,
            'bmin': lo.round(1).tolist(), 'bmax': hi.round(1).tolist()}
    with open(os.path.join(out, 'preview.json'), 'w') as f:
        json.dump(info, f, indent=1)
    print('W3PREVIEW_DONE', json.dumps(info), flush=True)


if __name__ == '__main__':
    main()
