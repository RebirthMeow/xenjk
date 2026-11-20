from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys; sys.path.insert(0,str(Path(__file__).resolve().parent))
import jka, numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
glm=sys.argv[1]; out=sys.argv[2]
m=jka.read_glm(glm); g=jka.read_gla(str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players') + r'\_humanoid\_humanoid.gla')
fig,ax=plt.subplots(1,3,figsize=(21,10))
cols=plt.cm.tab20(np.linspace(0,1,20))
for i,(s,si) in enumerate(zip(m['lods'][0],m['surfaces'])):
    if si['flags']&2: continue
    P=np.array([v['co'] for v in s['verts']])
    if si['flags']&1:
        ax[0].scatter(P[:,0],P[:,2],s=8,color='k'); ax[1].scatter(P[:,1],P[:,2],s=8,color='k'); continue
    c=cols[i%20]
    ax[0].scatter(P[:,0],P[:,2],s=0.3,color=c); ax[1].scatter(P[:,1],P[:,2],s=0.3,color=c); ax[2].scatter(P[:,0],P[:,1],s=0.3,color=c)
B=np.array([b['base'][:,3] for b in g['bones']])
for b in g['bones']:
    if b['parent']>=0:
        p=B[b['parent']];q=B[b['index']]
        ax[0].plot([p[0],q[0]],[p[2],q[2]],'r-',lw=0.8); ax[1].plot([p[1],q[1]],[p[2],q[2]],'r-',lw=0.8)
ax[0].scatter(B[:,0],B[:,2],s=6,color='r');ax[1].scatter(B[:,1],B[:,2],s=6,color='r')
for a in ax: a.set_aspect('equal')
plt.tight_layout(); plt.savefig(out,dpi=60)
