"""Render preview images of a levelport glTF in Blender (checks it imports cleanly; Workbench, textured).

    blender -b --factory-startup --python tools/levelport/blender_preview.py -- <in.glb> <out_prefix> [--hide-collision]

Writes <out_prefix>_top.png (orthographic, from above) and <out_prefix>_persp.png (three-quarter view), and prints
the object count by levelport role (custom property "lp", imported from glTF extras).
"""
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
src, prefix = argv[0], argv[1]
hide_col = '--hide-collision' in argv

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
roles = {}
for ob in bpy.data.objects:
    role = ob.get('lp', 'none')
    roles[role] = roles.get(role, 0) + 1
    if hide_col and role == 'collision':
        ob.hide_render = True
print('LEVELPORT objects by role:', roles)

meshes = [o for o in bpy.data.objects if o.type == 'MESH' and not o.hide_render]
lo = Vector((1e9, 1e9, 1e9))
hi = Vector((-1e9, -1e9, -1e9))
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))
centre, size = (lo + hi) / 2, hi - lo
print('LEVELPORT bounds', tuple(round(x, 2) for x in lo), tuple(round(x, 2) for x in hi))

scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.show_shadows = True
scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
world = bpy.data.worlds.new('w')
scene.world = world

cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
scene.collection.objects.link(cam)
scene.camera = cam
cam.data.clip_end = max(size) * 10

cam.data.type = 'ORTHO'
cam.data.ortho_scale = max(size.x, size.y) * 1.05
cam.location = centre + Vector((0, 0, size.z + 50))
cam.rotation_euler = (0, 0, 0)
scene.render.filepath = prefix + '_top.png'
bpy.ops.render.render(write_still=True)

cam.data.type = 'PERSP'
cam.data.lens = 24
d = max(size.x, size.y) * 0.75
cam.location = centre + Vector((d * 0.7, -d * 0.7, d * 0.45))
direction = centre - cam.location
cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
scene.render.filepath = prefix + '_persp.png'
bpy.ops.render.render(write_still=True)
