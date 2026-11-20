"""Blender look render of a model dumped by ct3x_blender_dump.py.

Principled BSDF per rd-vulkan stage: albedo, tangent-space normal map (OpenGL) plus the tiled detail normal and
detailMap weighted by materialMask's alpha, ORM (green perceptual roughness, blue metal), emissive x colour x level,
alpha blend for decals and glass. Studio lighting (large soft key, two rims, a dim world), a dark ground, Cycles.

  blender -b --factory-startup --python ct3x_blender.py -- DUMP OUTDIR [--samples 96] [--size 1920x1080] [--blend]
"""
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
DUMP, OUT = Path(argv[0]).resolve(), Path(argv[1]).resolve()
SAMPLES = int(argv[argv.index('--samples') + 1]) if '--samples' in argv else 96
W, H = map(int, (argv[argv.index('--size') + 1] if '--size' in argv else '1920x1080').split('x'))
OUT.mkdir(parents=True, exist_ok=True)
scene_doc = json.loads((DUMP / 'scene.json').read_text())

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
images = {}


def img(path, colour):
    if not path:
        return None
    key = (path, colour)
    if key not in images:
        im = bpy.data.images.load(path, check_existing=False)
        im.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'
        images[key] = im
    return images[key]


def material(name, d):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    N, L = nt.nodes, nt.links
    bsdf = N['Principled BSDF']
    x = -900

    def tex(path, colour, scale=None, interp='Linear'):
        t = N.new('ShaderNodeTexImage')
        t.image = img(path, colour)
        t.interpolation = interp
        t.location = (x, len(N) * -40)
        if scale is None:
            L.new(uv.outputs['UV'], t.inputs['Vector'])
        else:
            mp = N.new('ShaderNodeMapping')
            mp.inputs['Scale'].default_value = (scale, scale, 1)
            L.new(uv.outputs['UV'], mp.inputs['Vector'])
            L.new(mp.outputs['Vector'], t.inputs['Vector'])
        return t

    def math_(op, a, b):
        n = N.new('ShaderNodeMath')
        n.operation = op
        for i, v in enumerate((a, b)):
            if isinstance(v, (int, float)):
                n.inputs[i].default_value = v
            else:
                L.new(v, n.inputs[i])
        return n.outputs[0]

    # Q3 texture coordinates have v down; Blender's v is up: the dump keeps the file's uv, so flip v here
    flip = N.new('ShaderNodeMapping')
    flip.inputs['Scale'].default_value = (1, -1, 1)
    flip.inputs['Location'].default_value = (0, 1, 0)
    L.new(N.new('ShaderNodeUVMap').outputs['UV'], flip.inputs['Vector'])
    uv = type('UV', (), {'outputs': {'UV': flip.outputs['Vector']}})()

    mask = tex(d['mask'], False) if d.get('mask') else None
    w_det = mask.outputs['Alpha'] if mask else None
    base = tex(d['map'], True) if d.get('map') else None
    col = base.outputs['Color'] if base else None
    if d.get('detail') and col is not None and w_det is not None:
        dt = tex(d['detail'][0], False, d['detail'][1])
        # rgb scales the albedo by 1 + (2 d - 1) * weight (rd-vulkan detailMap)
        sep = N.new('ShaderNodeSeparateColor')
        L.new(dt.outputs['Color'], sep.inputs['Color'])
        f = math_('ADD', math_('MULTIPLY', math_('SUBTRACT', math_('MULTIPLY', sep.outputs['Red'], 2.0), 1.0), w_det), 1.0)
        mul = N.new('ShaderNodeMix')
        mul.data_type = 'RGBA'
        mul.blend_type = 'MULTIPLY'
        mul.inputs['Factor'].default_value = 1.0
        L.new(col, mul.inputs['A'])
        cc = N.new('ShaderNodeCombineColor')
        for k in ('Red', 'Green', 'Blue'):
            L.new(f, cc.inputs[k])
        L.new(cc.outputs['Color'], mul.inputs['B'])
        col = mul.outputs['Result']
    if col is not None:
        L.new(col, bsdf.inputs['Base Color'])
    if base is not None and d.get('blend'):
        L.new(base.outputs['Alpha'], bsdf.inputs['Alpha'])
        try:
            m.blend_method = 'BLEND'        # (EEVEE; Cycles takes the alpha as it is)
        except (AttributeError, TypeError):
            pass
    if d.get('orm'):
        o = tex(d['orm'], False)
        sep = N.new('ShaderNodeSeparateColor')
        L.new(o.outputs['Color'], sep.inputs['Color'])
        L.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
        L.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    nrm_out = None
    if d.get('normal'):
        nt_ = tex(d['normal'], False)
        nm = N.new('ShaderNodeNormalMap')
        L.new(nt_.outputs['Color'], nm.inputs['Color'])
        nrm_out = nm.outputs['Normal']
    if d.get('detailN') and w_det is not None:
        dn = tex(d['detailN'][0], False, d['detailN'][1])
        nm2 = N.new('ShaderNodeNormalMap')
        L.new(dn.outputs['Color'], nm2.inputs['Color'])
        L.new(math_('MULTIPLY', w_det, d['detailN'][2]), nm2.inputs['Strength'])
        geo = N.new('ShaderNodeNewGeometry')
        # n = normalize(n_base + n_detail - n_geometry)
        add = N.new('ShaderNodeVectorMath'); add.operation = 'ADD'
        L.new(nrm_out if nrm_out is not None else geo.outputs['Normal'], add.inputs[0])
        L.new(nm2.outputs['Normal'], add.inputs[1])
        sub = N.new('ShaderNodeVectorMath'); sub.operation = 'SUBTRACT'
        L.new(add.outputs['Vector'], sub.inputs[0]); L.new(geo.outputs['Normal'], sub.inputs[1])
        nz = N.new('ShaderNodeVectorMath'); nz.operation = 'NORMALIZE'
        L.new(sub.outputs['Vector'], nz.inputs[0])
        nrm_out = nz.outputs['Vector']
    if nrm_out is not None:
        L.new(nrm_out, bsdf.inputs['Normal'])
    if d.get('emissive'):
        e = tex(d['emissive'][0], False)
        rgb = d['emissive'][1]
        tint = N.new('ShaderNodeMix'); tint.data_type = 'RGBA'; tint.blend_type = 'MULTIPLY'
        tint.inputs['Factor'].default_value = 1.0
        L.new(e.outputs['Color'], tint.inputs['A'])
        tint.inputs['B'].default_value = (*rgb, 1)
        L.new(tint.outputs['Result'], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = 1.5 * d['emissive'][2]
    if 'glass' in name:
        bsdf.inputs['Transmission Weight'].default_value = 0.0
    return m


mats = {sh: material(sh.rsplit('/', 1)[-1], d) for sh, d in scene_doc['shaders'].items()}
root = bpy.data.objects.new('ct3x', None)
bpy.context.collection.objects.link(root)
for s in scene_doc['surfaces']:
    z = np.load(s['file'])
    P, Nn, UV, T = z['P'], z['N'], z['UV'], z['T']
    me = bpy.data.meshes.new(Path(s['file']).stem)
    me.from_pydata(P.tolist(), [], T.tolist())
    me.update()
    uvl = me.uv_layers.new(name='UVMap')
    loops = np.zeros(len(me.loops), np.int64)
    me.loops.foreach_get('vertex_index', loops)
    uvl.data.foreach_set('uv', UV[loops].reshape(-1))
    me.normals_split_custom_set_from_vertices(Nn.tolist())
    me.materials.append(mats[s['shader']])
    ob = bpy.data.objects.new(me.name, me)
    ob.parent = root
    bpy.context.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = True

# ground: dark satin asphalt
bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
g = bpy.context.active_object
gm = bpy.data.materials.new('ground'); gm.use_nodes = True
gb = gm.node_tree.nodes['Principled BSDF']
gb.inputs['Base Color'].default_value = (0.025, 0.025, 0.028, 1)
gb.inputs['Roughness'].default_value = 0.35
g.data.materials.append(gm)

# lights: soft key above front-left, cool and warm rims, a big top softbox for paint reflections
def area(name, loc, size, power, colour, target=(0, -0.15, 0.45)):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.size, ld.energy, ld.color = size, power, colour
    ob = bpy.data.objects.new(name, ld)
    ob.location = loc
    bpy.context.collection.objects.link(ob)
    d = Vector(target) - Vector(loc)
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return ob
area('key', (2.6, -3.0, 3.0), 3.0, 900, (1.0, 0.97, 0.93))
area('top', (0, 0, 4.0), 4.0, 500, (1.0, 1.0, 1.0))
area('rim_cool', (-3.2, 2.4, 1.6), 1.5, 450, (0.55, 0.7, 1.0))
area('rim_warm', (3.2, 2.8, 1.2), 1.5, 300, (1.0, 0.6, 0.45))
world = bpy.data.worlds.new('w'); bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.012, 0.013, 0.018, 1)

sc = bpy.context.scene
sc.render.engine = 'CYCLES'
prefs = bpy.context.preferences.addons['cycles'].preferences
for kind in ('OPTIX', 'CUDA'):
    try:
        prefs.compute_device_type = kind
        prefs.get_devices()
        if any(d.type == kind for d in prefs.devices):
            for d in prefs.devices:
                d.use = d.type == kind
            sc.cycles.device = 'GPU'
            break
    except Exception:
        pass
sc.cycles.samples = SAMPLES
sc.cycles.use_denoising = True
sc.render.resolution_x, sc.render.resolution_y = W, H
# Standard, not AgX: AgX pulls saturated reds toward salmon (the CT-3X's paint read pink)
sc.view_settings.view_transform = 'Standard'
sc.view_settings.look = 'Medium High Contrast'
sc.view_settings.exposure = -0.3

cam_d = bpy.data.cameras.new('cam'); cam_d.lens = 70
cam = bpy.data.objects.new('cam', cam_d); bpy.context.collection.objects.link(cam); sc.camera = cam
VIEWS = (('side', 90, 6, 5.4), ('front34', 35, 12, 4.8), ('rear34', 215, 16, 4.8), ('close_tank', 55, 30, 2.4),
         ('close_rear_wheel', 120, 12, 2.2))
target = Vector((0, -0.18, 0.5))
print('device', sc.cycles.device)
for tag, yaw, elev, dist in VIEWS:
    a, e = math.radians(yaw), math.radians(elev)
    t = target + (Vector((0, -0.55, 0.05)) if tag == 'close_tank' else Vector((0, 0.95, -0.1)) if 'wheel' in tag
                  else Vector())
    cam.location = t + dist * Vector((math.cos(e) * math.sin(a), -math.cos(e) * math.cos(a), math.sin(e)))
    cam.rotation_euler = (t - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = str(OUT / f'blender_{tag}.png')
    bpy.ops.render.render(write_still=True)
    print('RENDERED', sc.render.filepath)
if '--blend' in argv:
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ct3x_look.blend'))
