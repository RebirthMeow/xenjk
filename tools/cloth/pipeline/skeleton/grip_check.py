"""Saber-grip check from the bolt tags (engine G2_ProcessSurfaceBolt2 maths, normal tag path).
kyle+_humanoid.gla vs eve2b(lite, _humanoid.gla) vs eve2b_np(+eve2b_np.gla).
Per frame: r = *r_hand origin, l = *l_hand origin, hilt axis = r_hand bolt column most parallel to (l-r) for kyle in the
two-handed medium stance. Reports |l-r|, perpendicular distance of l from the hilt line through r, and the distance of the
r_hand origin from the right fist centre (mean of rhand + finger j1 joints)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, re, json, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'model'))
import jkagla, jka
from retarget_lib import pool_rigid, rigid_base, skin_mats
R = str(REPO / '.codex-research')
ST = R + r'\eve2b-glm-20261012\src\stock\models\players'
cfg = open(ST + r'\_humanoid\animation.cfg').read()
AN = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in re.finditer(r'^(\w+)\s+(\d+)\s+(\d+)', cfg, re.M)}
ANIMS = ['BOTH_STAND1', 'BOTH_SABERFAST_STANCE', 'BOTH_STAND2', 'BOTH_SABERSLOW_STANCE', 'BOTH_SABERSTAFF_STANCE', 'BOTH_SABERDUAL_STANCE',
         'BOTH_RUN2', 'BOTH_A2_T__B_', 'BOTH_A3_T__B_', 'BOTH_A1_T__B_', 'BOTH_A2__L__R', 'BOTH_A3__L__R']
frames = np.concatenate([np.arange(AN[a][0], AN[a][0] + AN[a][1]) for a in ANIMS])

def load_F(gla_path):
    g = jkagla.read(gla_path); par = [b['parent'] for b in g['bones']]
    A = pool_rigid(g['pool'])[g['idx'][frames]]
    Bh = np.array([rigid_base(b)[0] for b in g['bones']])
    F = skin_mats(A, par)
    return F, F @ Bh[None], [b['name'] for b in g['bones']]

def tags(glm_path, F):
    m = jka.read_glm(glm_path); out = {}
    for s, l in zip(m['surfaces'], m['lods'][0]):
        if s['name'] not in ('*r_hand', '*l_hand'): continue
        pts = []
        for v in l['verts'][:3]:
            p = np.zeros((len(F), 3))
            for w, b in zip(v['w'], v['b']):
                M = F[:, l['brefs'][b]]
                p += w * (np.einsum('fij,j->fi', M[:, :3, :3], np.array(v['co'])) + M[:, :3, 3])
            pts.append(p)
        sides = [pts[(j + 1) % 3] - pts[j] for j in range(3)]
        def nrm(x): return x / np.linalg.norm(x, axis=1, keepdims=True)
        a1 = nrm(sides[2]); a0 = nrm(sides[0]); a0 = nrm(a0 - (a0 * a1).sum(1, keepdims=True) * a1)
        a2 = nrm(np.cross(sides[0], sides[2]))
        out[s['name']] = dict(o=pts[2], cols=np.stack([a1, a0, -a2], 2))   # bolt matrix columns X,Y,Z
    return out

models = {
    'kyle': (ST + r'\_humanoid\_humanoid.gla', ST + r'\kyle\model.glm'),
    'eve2b': (ST + r'\_humanoid\_humanoid.gla', R + r'\eve2b-glm-20261012\build\lite\model.glm'),
    'eve2b_np': (R + r'\eve2b-nativeprop-20261012\gla\eve2b_np.gla', R + r'\eve2b-nativeprop-20261012\build\lite\model.glm')}
res = {}; cache = {}
for k, (gp, mp) in models.items():
    if gp not in cache: cache[gp] = load_F(gp)
    F, G, names = cache[gp]
    t = tags(mp, F)
    NI = {n: i for i, n in enumerate(names)}
    fist = np.mean([G[:, NI[n], :3, 3] for n in ('rhand', 'r_d2_j1', 'r_d4_j1', 'r_d1_j1')], 0)
    res[k] = dict(r=t['*r_hand']['o'], l=t['*l_hand']['o'], cols=t['*r_hand']['cols'], fist=fist,
                  lfist=np.mean([G[:, NI[n], :3, 3] for n in ('lhand', 'l_d2_j1', 'l_d4_j1', 'l_d1_j1')], 0))
# hilt axis column from kyle, two-handed medium stance
s0 = list(frames).index(AN['BOTH_STAND2'][0])
kv = res['kyle']; d = kv['l'][s0] - kv['r'][s0]; d /= np.linalg.norm(d)
dots = kv['cols'][s0].T @ d; col = int(np.argmax(np.abs(dots))); sign = float(np.sign(dots[col]))
print('hilt axis = bolt column', 'XYZ'[col], 'sign', sign, 'cos', dots[col])
out = dict(hilt_axis_column='XYZ'[col], sign=sign, anims={})
pos = 0
print('%-22s %-9s %8s %8s %8s %8s' % ('anim', 'model', '|l-r|', 'l_perp', 'r-fist', 'l-lfist'))
for a in ANIMS:
    n = AN[a][1]; sl = slice(pos, pos + n); pos += n
    out['anims'][a] = {}
    for k, v in res.items():
        ax = v['cols'][sl, :, col] * sign
        dl = v['l'][sl] - v['r'][sl]
        along = (dl * ax).sum(1); perp = np.linalg.norm(dl - along[:, None] * ax, axis=1)
        rf = np.linalg.norm(v['r'][sl] - v['fist'][sl], axis=1); lf = np.linalg.norm(v['l'][sl] - v['lfist'][sl], axis=1)
        e = dict(lr_med=float(np.median(np.linalg.norm(dl, axis=1))), l_perp_med=float(np.median(perp)), l_perp_max=float(perp.max()),
                 l_along_med=float(np.median(along)), r_fist_med=float(np.median(rf)), l_fist_med=float(np.median(lf)))
        out['anims'][a][k] = e
        print('%-22s %-9s %8.2f %8.2f %8.2f %8.2f' % (a, k, e['lr_med'], e['l_perp_med'], e['r_fist_med'], e['l_fist_med']))
json.dump(out, open(R + r'\eve2b-nativeprop-20261012\grip_report.json', 'w'), indent=1)
