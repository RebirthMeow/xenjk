from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os
import numpy as np, jkagla
from retarget_lib import *
G=str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players') + r'\_humanoid\_humanoid.gla'
g=jkagla.read(G); par=[b['parent'] for b in g['bones']]; names=[b['name'] for b in g['bones']]
F=np.load(os.path.join(str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012'), 'stock_F.npy')).astype(np.float64)[::5]
Bh=np.array([rigid_base(b)[0] for b in g['bones']])
Gm=F@Bh[None]; inv=np.linalg.inv(Gm)
for i in [1,2,25,30,31,32,33,34,35,36,38,43,47,51,49,50,16,19,29,42]:
    res=[]
    for a in range(53):
        if a==i: continue
        t=(inv[:,a]@Gm[:,i])[:,:3,3]
        med=np.median(t,0); dev=np.linalg.norm(t-med,axis=1)
        res.append((np.quantile(dev,0.99),a,dev.max()))
    res.sort()
    print(names[i],'parent',names[par[i]],[(names[a],round(q,3),round(m,2)) for q,a,m in res[:3]])
