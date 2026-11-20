from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import json, numpy as np, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jka
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
tj = json.load(open(W + r'\build\lite\transform.json'))
o = np.load(W + r'\eve_mesh.npz'); meta = json.load(open(W + r'\eve_mesh.npz.json'))
VG = meta['vgroups']
glm = jka.read_glm(W + r'\build\lite\model.glm')
names = [s['name'] for s in glm['surfaces']]
S, Z0 = tj['scale'], tj['z_offset']
maxerr = 0; n = 0
for sk in tj['skirt_surfaces']:
    surf = glm['lods'][0][names.index(sk['surface'])]
    idx = sk['original_vertex_indices']
    assert len(idx) == len(surf['verts']) == sk['glm_vertex_count']
    for k in range(0, len(idx), 7):
        v = idx[k]
        rows = np.flatnonzero(o['wr'] == v)
        tot = o['wv'][rows].sum(); acc = np.zeros(3)
        p = o['pos'][v] * S + np.array([0, 0, Z0])
        for r in rows:
            M = np.array(tj['bone_transforms'][VG[o['wc'][r]]])
            acc += o['wv'][r] / tot * (M[:3, :3] @ p + M[:3, 3])
        err = np.linalg.norm(acc - np.array(surf['verts'][k]['co'])); maxerr = max(maxerr, err); n += 1
print('checked', n, 'skirt verts, max error (JKA units)', maxerr)
