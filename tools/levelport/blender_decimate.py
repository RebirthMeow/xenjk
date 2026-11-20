"""Headless Blender batch decimation for levelport sources (cp2gltf `decimate`).

blender --background --factory-startup --python blender_decimate.py -- jobs.json

jobs.json: [{"src": glb, "dst": glb, "keep": [node names], "ratio": 0..1}]. Each job imports src, deletes every
mesh object not in `keep`, collapses the rest to `ratio` of their triangles (Decimate modifier, UV seams kept by
Blender's collapse), and writes dst with the node names intact. Blender's glTF importer and exporter convert Y-up
both ways, so the round trip keeps WolvenKit's axes.
"""
import json
import os
import sys
import time

import bpy


def run(job):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=job['src'])
    keep = set(job['keep'])
    for ob in list(bpy.data.objects):
        if ob.type != 'MESH' or ob.name.split('.')[0] not in keep:
            bpy.data.objects.remove(ob, do_unlink=True)
    for ob in bpy.data.objects:
        mod = ob.modifiers.new('decimate', 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = max(0.0, min(1.0, float(job['ratio'])))
        mod.use_collapse_triangulate = True
    part = os.path.splitext(job['dst'])[0] + '.part.glb'      # (renamed once whole: a stopped run leaves no short dst)
    bpy.ops.export_scene.gltf(filepath=part, export_format='GLB', export_apply=True, export_yup=True,
                              export_materials='NONE', export_texcoords=True, export_normals=True,
                              export_attributes=False, export_animations=False, export_skins=False,
                              export_morph=False, export_extras=False)
    os.replace(part, job['dst'])


def main():
    jobs = json.load(open(sys.argv[sys.argv.index('--') + 1], encoding='utf-8'))
    t0, bad = time.time(), 0
    for i, job in enumerate(jobs):
        try:
            run(job)
        except Exception as e:      # one broken source must not stop the batch; the converter keeps the original
            bad += 1
            print('decimate failed: %s: %s' % (job['src'], e))
        if i % 100 == 0:
            print('decimate %d/%d %.0fs' % (i, len(jobs), time.time() - t0), flush=True)
    print('decimate done: %d jobs, %d failed, %.0fs' % (len(jobs), bad, time.time() - t0))


main()
