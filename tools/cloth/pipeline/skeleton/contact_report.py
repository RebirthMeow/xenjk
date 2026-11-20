"""Per-animation ground contact: kyle lowest sole z vs eve2b_np lowest sole z (model space; ground ZG ~ -23.84)."""
import os
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root
NPD = str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012')
GLMD = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
import numpy as np, json, re, sys
c = np.load(os.path.join(NPD, 'gla', 'eve2b_np_contacts.npz')); zk, zh, dz = c['zk'], c['zh'], c['dz']
cfg = open(os.path.join(GLMD, 'src', 'stock', 'models', 'players', '_humanoid', 'animation.cfg')).read()
A = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in re.finditer(r'^(\w+)\s+(\d+)\s+(\d+)', cfg, re.M)}
names = sys.argv[1:] or ['BOTH_STAND1', 'BOTH_STAND2', 'BOTH_STAND1IDLE1', 'BOTH_SABERFAST_STANCE', 'BOTH_SABERSLOW_STANCE', 'BOTH_SABERSTAFF_STANCE',
                         'BOTH_SABERDUAL_STANCE', 'BOTH_WALK1', 'BOTH_RUN1', 'BOTH_RUN2', 'BOTH_RUN_STAFF', 'BOTH_CROUCH1IDLE', 'BOTH_CROUCH1WALK',
                         'BOTH_JUMP1', 'BOTH_INAIR1', 'BOTH_LAND1', 'BOTH_FORCEJUMP1', 'BOTH_FLIP_F', 'BOTH_KNOCKDOWN1', 'BOTH_DEATH1',
                         'BOTH_A1_T__B_', 'BOTH_A2_STABBACK1', 'BOTH_LUNGE2_B__T_', 'BOTH_WALL_RUN_RIGHT']
rows = []
print('%-24s %5s %22s %22s %16s %14s' % ('anim', 'nfr', 'kyle sole min..max', 'eve sole min..max', 'sole diff max', 'dz min..max'))
for n in names:
    s, k = A[n]; sl = slice(s, s + k)
    ks = zk[sl, :4].min(1); hs = zh[sl, :4].min(1)
    d = np.abs(hs - ks)
    rows.append(dict(anim=n, frames=k, kyle_sole=[float(ks.min()), float(ks.max())], eve_sole=[float(hs.min()), float(hs.max())],
                     sole_diff_max=float(d.max()), dz=[float(dz[sl].min()), float(dz[sl].max())],
                     contact_is_sole_frac=float((zh[sl].argmin(1) < 4).mean())))
    print('%-24s %5d %10.2f..%9.2f %10.2f..%9.2f %16.3f %6.2f..%6.2f' % (n, k, ks.min(), ks.max(), hs.min(), hs.max(), d.max(), dz[sl].min(), dz[sl].max()))
allsole = np.abs(zh[:, :4].min(1) - zk[:, :4].min(1))
both = (zh.argmin(1) < 4) & (zk.argmin(1) < 4)
print('all frames where both lowest points are soles: %d / %d, sole diff max %.4f' % (both.sum(), len(zk), allsole[both].max()))
json.dump(dict(rows=rows, frames_sole_contact=int(both.sum()), sole_diff_max_when_sole_contact=float(allsole[both].max())),
          open(os.path.join(NPD, 'contact_report.json'), 'w'), indent=1)
