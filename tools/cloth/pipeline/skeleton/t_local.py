from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os
import numpy as np, jkagla
from retarget_lib import *
G=str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players') + r'\_humanoid\_humanoid.gla'
g=jkagla.read(G); par=[b['parent'] for b in g['bones']]
PR=pool_rigid(g['pool'])
A=PR[g['idx']]           # nfr,nb,4,4
F=skin_mats(A,par)
Bh=np.array([rigid_base(b)[0] for b in g['bones']])
Gm=F@Bh[None]
inv=np.linalg.inv(Gm)
for i,p in enumerate(par):
    if p<0: L=Gm[:,i]
    else: L=inv[:,p]@Gm[:,i]
    rest = (np.linalg.inv(Bh[p])@Bh[i])[:3,3] if p>=0 else Bh[i][:3,3]
    t=L[:,:3,3]
    print(i,g['bones'][i]['name'],'rest',np.round(rest,2),'mean',np.round(t.mean(0),2),'std',np.round(t.std(0),3),'maxdev',round(np.abs(t-rest).max(),3))
np.save(os.path.join(str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012'), 'stock_F.npy'),F.astype(np.float32))
