"""Convert an SKD1 trace (UE) into SKA1 + SKP1, with the harness's own capsule rules, so the new loader can be checked
against the original SKD1 path (must be bit-identical) and the same analysis ruler applied to the UE baseline.
Usage: skd_to_ska.py <in.skd> <out.cloth> <out.skp> <out_extra.npz> [--seconds N]"""
import sys, json, argparse
import numpy as np
import skdio, skaio

ap = argparse.ArgumentParser()
ap.add_argument('skd'); ap.add_argument('cloth'); ap.add_argument('skp'); ap.add_argument('extra'); ap.add_argument('--seconds', type=float, default=1e9)
a = ap.parse_args()
d = skdio.read_skd(a.skd); m = d['meta']; NB, N = m['nBones'], m['nVerts']
names = m['bones']
cfg = m['config']
config = [cfg['gravity'][0], cfg['gravity'][1], cfg['gravity'][2], cfg.get('density', 0.35), cfg.get('minPerParticleMass', 1e-4),
          cfg['edge'][0], cfg['bend'][0], cfg['area'][0], cfg['tetherStiffness'][0], cfg['drag'][0], cfg['lift'][0]]
caps = [('Forearm', 'Hand', 2.8), ('Hand', 'Finger1', 1.6), ('Hand', 'Finger4', 1.6), ('Finger1', 'Finger12', 1.0), ('Finger3', 'Finger32', 1.0), ('Finger0', 'Finger02', 1.0)]
capA, capB, capR = [], [], []
for side in 'LR':
    for f_, t_, r in caps:
        na, nb_ = f'Bip001-{side}-{f_}', f'Bip001-{side}-{t_}'
        if na in names and nb_ in names: capA.append(names.index(na)); capB.append(names.index(nb_)); capR.append(r)
t0 = d['trimeshes'][0]
ref = m['config'].get('referenceBone', m['pelvisBone'])
skaio.write_ska(a.cloth, dict(unit_cm=1.0, ref_bone=ref, bone_names=names, refInv=d['refInv'], pos=d['pos'], tri=d['tri'], maxDist=d['maxDist'], animDrive=d['animDrive'],
    skinB=d['skinB'], skinW=d['skinW'], col_pos=t0['pos'], col_tri=t0['tri'], col_skinB=t0['skinB'], col_skinW=t0['skinW'],
    capA=np.array(capA, np.int32), capB=np.array(capB, np.int32), capR=np.array(capR), config=config, skeleton='SK_Eve (UE)'))
# frames
fb = 16 + 64 + NB * 64 + N * 12
raw = np.memmap(a.skd, dtype=np.uint8, mode='r', offset=d['headerBytes'])
nfr = len(raw) // fb
cst = []; dts = []
for i in range(nfr):
    r = raw[i * fb:(i + 1) * fb]
    valid = np.frombuffer(r[12:16], '<u4')[0]
    if not valid: continue
    dts.append(float(np.frombuffer(r[8:12], '<f4')[0]))
    cst.append(np.frombuffer(r[80:80 + NB * 64], '<f4').reshape(NB, 4, 4))
    if len(cst) >= a.seconds * 60 + 0.5: break
cst = np.stack(cst)
skaio.write_skp(a.skp, NB, 60.0, [dict(name='ue_trace', start=0, frames=len(cst), evalStart=30, rooted=True)], cst, dts, extra=dict(boneNames=names))
# analysis extra (same keys as build_cloth_asset.py)
np.savez(a.extra, simPosCm=d['pos'].astype(np.float64), refInv=d['refInv'], skinB=d['skinB'], skinW=d['skinW'], tri=d['tri'], maxDist=d['maxDist'],
         cPos=t0['pos'], cT=t0['tri'], cBI=t0['skinB'], cW=t0['skinW'])
print('frames', len(cst), 'caps', len(capA))
