from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import numpy as np, jkagla, time
G=str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players') + r'\_humanoid\_humanoid.gla'
g=jkagla.read(G); print(g['numFrames'],g['numBones'],len(g['pool']))
P=jkagla.decode_pool(g['pool'])
enc=jkagla.encode(P)
d=np.abs(enc.astype(int)-g['pool'].astype(int))
# sign ambiguity: compare with negated quat
neg=enc.copy().astype(int); neg[:,:4]=np.rint((-(enc[:,:4]/16383.0-2.0)+2)*16383)
d2=np.abs(neg-g['pool'].astype(int))
dm=np.minimum(d[:,:4].max(1),d2[:,:4].max(1))
print('roundtrip quat max diff',dm.max(),'frac exact',(dm==0).mean(),'trans max',d[:,4:].max())
q=g['pool'][:,:4]/16383.0-2; print('quat norm range',np.linalg.norm(q,axis=1).min(),np.linalg.norm(q,axis=1).max())
# base pose check
for b in g['bones'][:3]+[g['bones'][16]]:
    B=b['base'];R=B[:,:3]; s=np.cbrt(np.linalg.det(R)); print(b['name'],'s',s,'orth err',abs((R/s)@(R/s).T-np.eye(3)).max(), 'inv err', abs(np.vstack([b['baseinv'],[0,0,0,1]])@np.vstack([B,[0,0,0,1]])-np.eye(4)).max())
qs=q/np.linalg.norm(q,axis=1,keepdims=True); qs[qs[:,0]<0]*=-1
qm=jkagla.mat_to_quat(P[:,:,:3])
e=np.abs(qs-qm).max(1); print('normalized quat diff max',e.max(),'p99.9',np.quantile(e,0.999))
i=e.argmax(); print(q[i],qm[i], np.linalg.norm(q[i]))
# angle error of decoded (non-unit) matrices vs pure rotation
