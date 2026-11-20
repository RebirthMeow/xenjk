"""diag_band.py: worst kinematic-band edges (skinned pose / rest) per clip for the old (eve2b) and new (eve2b_np) asset+reel."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import json, struct, numpy as np
R = str(REPO / '.codex-research') + '/'
def load(folder, extra, reel):
    E = np.load(R + folder + extra); f = open(R + folder + reel, 'rb'); f.read(4); v, jb = struct.unpack('<II', f.read(8)); pm = json.loads(f.read(jb)); d0 = f.tell()
    NB = E['refInv'].shape[0]; fb = 12 + NB * 64; raw = np.memmap(R + folder + reel, dtype=np.uint8, mode='r', offset=d0)
    return E, pm, raw, fb, NB
for tag, folder, ex, reel in (('old', 'eve2b-cloth-asset-20261012/out/', 'asset_extra.npz', 'reel.skp'), ('new', 'eve2b-np-cloth-20261012/out/', 'asset_extra.npz', 'reel.skp')):
    E, pm, raw, fb, NB = load(folder, ex, reel)
    P = E['simPosCm']; tri = E['tri'].astype(int); kin = E['maxDist'] <= 0.01; B = E['skinB']; W = E['skinW']; refInv = E['refInv'].astype(np.float64)
    e = np.unique(np.sort(np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]]), 1), axis=0)
    rl = np.linalg.norm(P[e[:, 0]] - P[e[:, 1]], axis=1); kk = (rl > 0.5) & kin[e[:, 0]] & kin[e[:, 1]]; e = e[kk]; rl = rl[kk]
    names = pm['boneNames']
    print(tag, 'band z range %.1f..%.1f' % (P[kin, 2].min(), P[kin, 2].max()), 'band edges', len(e))
    for c in pm['clips']:
        if c['name'] not in ('idle', 'run', 'flip', 'flip_back', 'kick'): continue
        worst = (0, None, None); best = (9, None, None)
        for i in range(c['start'] + c['evalStart'], c['start'] + c['frames'], 3):
            cst = np.frombuffer(raw[i * fb + 12:(i + 1) * fb], dtype='<f4').reshape(NB, 4, 4).astype(np.float64)
            mats = refInv @ cst
            h = np.concatenate([P, np.ones((len(P), 1))], 1); sk = np.zeros((len(P), 3))
            for k in range(4):
                ok = B[:, k] >= 0; m = mats[np.where(ok, B[:, k], 0)]
                sk += np.where(ok[:, None], W[:, k:k + 1] * np.einsum('ni,nij->nj', h, m)[:, :3], 0)
            r = np.linalg.norm(sk[e[:, 0]] - sk[e[:, 1]], axis=1) / rl
            j = r.argmax()
            if r[j] > worst[0]: worst = (r[j], j, i)
            j = r.argmin()
            if r[j] < best[0]: best = (r[j], j, i)
        for lab, (val, j, fr) in (('max', worst), ('min', best)):
            a, b = e[j]
            wa = {names[B[a, k]]: round(float(W[a, k]), 2) for k in range(4) if B[a, k] >= 0}
            wb = {names[B[b, k]]: round(float(W[b, k]), 2) for k in range(4) if B[b, k] >= 0}
            print(f'  {c["name"]:10s} {lab} {val:.2f} rest {rl[j]:.2f} cm  z {P[a,2]:.1f}/{P[b,2]:.1f}  {wa} | {wb}')
