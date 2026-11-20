"""Proportions table: source Eve (blend joints, x37.5, +3.12), eve2b (= _humanoid joints, snapped fit), eve2b_np (joints_np.json)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import json, numpy as np
W = str(REPO / '.codex-research')
jn = json.load(open(W + r'\eve2b-nativeprop-20261012\build\lite\joints_np.json'))
NP = {k: np.array(v) for k, v in jn['joints'].items()}; ST = {k: np.array(v) for k, v in jn['stock'].items()}
m = json.load(open(W + r'\eve2b-glm-20261012\eve_mesh.npz.json'))
EB = {b['name']: np.array(b['head']) * 37.5 + np.array([0, 0, 3.12]) for b in m['bones']}
SRC = {'lhumerus': EB['Bip001-L-UpperArm'], 'rhumerus': EB['Bip001-R-UpperArm'], 'cervical': EB['Bip001-Neck'], 'cranium': EB['Bip001-Head'],
       'pelvis': EB['Bip001-Pelvis'], 'lradius': EB['Bip001-L-Forearm'], 'lhand': EB['Bip001-L-Hand'], 'lfemurYZ': EB['Bip001-L-Thigh'],
       'rfemurYZ': EB['Bip001-R-Thigh'], 'ltibia': EB['Bip001-L-Calf'], 'ltalus': EB['Bip001-L-Foot']}
def d(J, a, b): return float(np.linalg.norm(J[a] - J[b]))
rows = [('shoulder width (humerus L-R)', lambda J: d(J, 'lhumerus', 'rhumerus')), ('hip width (femur L-R)', lambda J: d(J, 'lfemurYZ', 'rfemurYZ')),
        ('neck->head (cervical-cranium)', lambda J: d(J, 'cervical', 'cranium')), ('torso pelvis->neck', lambda J: d(J, 'pelvis', 'cervical')),
        ('upper arm', lambda J: d(J, 'lhumerus', 'lradius')), ('forearm', lambda J: d(J, 'lradius', 'lhand')),
        ('thigh', lambda J: d(J, 'lfemurYZ', 'ltibia')), ('shin', lambda J: d(J, 'ltibia', 'ltalus')),
        ('hip height (femur z)', lambda J: float(J['lfemurYZ'][2])), ('ankle height (talus z)', lambda J: float(J['ltalus'][2]))]
out = []
print('%-32s %8s %8s %8s' % ('measure (JKA units)', 'source', 'eve2b', 'eve2b_np'))
for n, f in rows:
    r = (f(SRC), f(ST), f(NP)); out.append(dict(measure=n, source=r[0], eve2b=r[1], eve2b_np=r[2]))
    print('%-32s %8.2f %8.2f %8.2f' % ((n,) + r))
print('head top: eve2b_np %.2f, kyle %.2f; ratios %s' % (jn['head_top'], jn['kyle_head_top'], jn['ratio']))
json.dump(out, open(W + r'\eve2b-nativeprop-20261012\proportions.json', 'w'), indent=1)
