"""Path-traced reference for character shadows (docs/renderer-character-shadows.md), run inside Blender:

    blender -b --factory-startup --python tools/charshadow_reference.py -- \
        --dump <dir with dump.obj, dump.txt> --world <receivers.obj> --out <dir> \
        [--angles 2,10,25] [--samples 256] [--width 960]

Rebuilds the scene from the renderer's `charshadowdump` (casters skinned in world space, the camera, each
caster's light) and receivers from tools/bsp_to_obj.py, all grey Lambert. Per caster and per sun angle it lights
the scene as the renderer's model does: a sun of irradiance 1 toward the caster's light, a uniform sky of
radiance ratio / pi (the light grid's ambient / directed), so an open, unshadowed floor gets directed * NL +
ambient. It renders the receivers without and with that caster (invisible to the camera, still casting and
bouncing) and writes their ratio: the light kept. The product over casters is what the renderer's
`r_charShadowDebug 1` shows; `kept_<angle>.npy/.png` hold it at the dump camera, height width / aspect.
"""
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector


def args():
    import argparse
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', required=True)
    ap.add_argument('--world', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--angles', default='2,10,25')
    ap.add_argument('--samples', type=int, default=256)
    ap.add_argument('--width', type=int, default=960)
    ap.add_argument('--bounces', type=int, default=2)
    return ap.parse_args(argv)


def read_dump(path):
    cam, casters = None, []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        t = line.replace('|', ' ').split()
        if t and t[0] == 'camera':
            f = [float(x) for x in t[1:13]]
            cam = {'org': f[0:3], 'fwd': f[3:6], 'left': f[6:9], 'up': f[9:12],
                   'fov_x': float(t[t.index('fov') + 1]), 'size': (int(t[t.index('size') + 1]), int(t[t.index('size') + 2]))}
        elif t and t[0] == 'caster':
            casters.append({'name': 'caster%s' % t[1], 'light': [float(x) for x in t[t.index('light') + 1:t.index('light') + 4]],
                            'ratio': float(t[t.index('ratio') + 1]), 'strength': float(t[t.index('strength') + 1])})
    return cam, casters


def setup_device(scene):
    scene.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    for kind in ('OPTIX', 'CUDA', 'HIP', 'ONEAPI', 'METAL'):
        try:
            prefs.compute_device_type = kind
            prefs.get_devices()
            devs = [d for d in prefs.devices if d.type == kind]
            if devs:
                for d in prefs.devices:
                    d.use = d.type == kind
                scene.cycles.device = 'GPU'
                return kind
        except TypeError:
            continue
    scene.cycles.device = 'CPU'
    return 'CPU'


def main():
    a = args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cam, casters = read_dump(Path(a.dump) / 'dump.txt')

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    device = setup_device(scene)

    grey = bpy.data.materials.new('grey')
    grey.use_nodes = True
    nt = grey.node_tree
    nt.nodes.clear()
    bsdf = nt.nodes.new('ShaderNodeBsdfDiffuse')
    bsdf.inputs['Color'].default_value = (0.5, 0.5, 0.5, 1.0)
    outn = nt.nodes.new('ShaderNodeOutputMaterial')
    nt.links.new(bsdf.outputs['BSDF'], outn.inputs['Surface'])

    def load(path):
        before = set(bpy.data.objects)
        bpy.ops.wm.obj_import(filepath=str(path), forward_axis='Y', up_axis='Z')
        objs = [o for o in bpy.data.objects if o not in before]
        for o in objs:
            o.data.materials.clear()
            o.data.materials.append(grey)
        return objs

    load(a.world)
    loaded = {o.name: o for o in load(Path(a.dump) / 'dump.obj')}
    for c in casters:
        c['obj'] = loaded[c['name']]
        c['obj'].visible_camera = False      # casts and bounces, never seen: the image is the receivers only

    # camera: Blender looks down its local -Z with +Y up; game axes are forward, left, up
    camobj = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    scene.collection.objects.link(camobj)
    right = -Vector(cam['left'])
    rot = Matrix((right, Vector(cam['up']), -Vector(cam['fwd']))).transposed()
    camobj.matrix_world = Matrix.Translation(Vector(cam['org'])) @ rot.to_4x4()
    camobj.data.sensor_fit = 'HORIZONTAL'
    camobj.data.angle = math.radians(cam['fov_x'])
    camobj.data.clip_start = 1.0
    camobj.data.clip_end = 30000.0
    scene.camera = camobj

    w = a.width
    h = round(w * cam['size'][1] / cam['size'][0])
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = w, h, 100
    scene.cycles.samples = a.samples
    scene.cycles.use_denoising = False
    scene.cycles.seed = 0
    scene.cycles.max_bounces = a.bounces
    scene.cycles.diffuse_bounces = a.bounces
    scene.view_settings.view_transform = 'Standard'
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_depth = '32'

    world = bpy.data.worlds.new('sky')
    world.use_nodes = True
    bg = world.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (1.0, 1.0, 1.0, 1.0)
    scene.world = world

    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    scene.collection.objects.link(sun)
    sun.data.energy = 1.0

    def render(name):
        path = out / ('%s.exr' % name)
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(str(path))
        px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1, :, 0]
        bpy.data.images.remove(img)
        return px

    angles = [float(x) for x in a.angles.split(',')]
    print('CHARREF device %s, %d casters, %dx%d, %d samples' % (device, len(casters), w, h, a.samples), flush=True)
    for ang in angles:
        sun.data.angle = math.radians(ang)
        kept = np.ones((h, w), dtype=np.float32)
        for c in casters:
            sun.rotation_euler = Vector(c['light']).to_track_quat('Z', 'Y').to_euler()   # local +Z toward the light
            bg.inputs['Strength'].default_value = c['ratio'] / math.pi
            for o in (x['obj'] for x in casters):
                o.hide_render = True
            base = render('base_%s_%g' % (c['name'], ang))
            c['obj'].hide_render = False
            lit = render('with_%s_%g' % (c['name'], ang))
            ratio = np.where(base > 1e-4, lit / np.maximum(base, 1e-4), 1.0)
            np.save(out / ('kept_%s_%g.npy' % (c['name'], ang)), ratio)
            kept *= ratio
        np.save(out / ('kept_%g.npy' % ang), kept)
        g = (np.clip(kept, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        img = bpy.data.images.new('kept', w, h, alpha=False)
        img.pixels[:] = np.dstack([g, g, g, np.full_like(g, 255)])[::-1].astype(np.float32).ravel() / 255.0
        img.filepath_raw = str(out / ('kept_%g.png' % ang))
        img.file_format = 'PNG'
        img.save()
        bpy.data.images.remove(img)
        print('CHARREF angle %g: min kept %.3f' % (ang, float(kept.min())), flush=True)


main()
