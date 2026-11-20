from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import bpy, sys, json, os
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
sys.path.insert(0, str(REPO / 'tools' / 'cloth' / 'blender'))
import addon_utils
bpy.ops.preferences.addon_enable(module='jediacademy')
rep = json.load(open(sys.argv[sys.argv.index('--') + 1]))
base = W + r'\verify_tree'
res = bpy.ops.import_scene.glm(filepath=base + r'\models\players\eve2b\model.glm', basepath=base, skin='default', scale=100, loadAnimations='NONE')
print('IMPORT_RESULT', res)
objs = [o for o in bpy.data.objects if o.type == 'MESH']
print('mesh objects', len(objs))
by = {}
for o in objs:
    name = o.name.rsplit('_', 1)[0]
    by[name] = (len(o.data.vertices), len(o.data.polygons), len(o.vertex_groups))
bad = 0
for s in rep['surfaces']:
    got = by.get(s['name'])
    ok = got is not None and got[0] == s['verts'] and got[1] == s['tris']
    if not ok:
        bad += 1; print('MISMATCH', s['name'], s['verts'], s['tris'], got)
print('SURFACES expected', len(rep['surfaces']), 'imported', len(by), 'mismatch', bad)
print('TOTAL verts', sum(v[0] for v in by.values()), 'tris', sum(v[1] for v in by.values()))
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE']
print('armatures', [(a.name, len(a.data.bones)) for a in arm])
