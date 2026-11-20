from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys; sys.path.insert(0,str(Path(__file__).resolve().parent))
import jka, json, numpy as np
R=str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players')
g=jka.read_gla(R+r'\_humanoid\_humanoid.gla')
print('gla',g['name'],g['numBones'],g['numFrames'])
for b in g['bones']:
    print(b['index'],b['name'],b['parent'],np.round(b['base'][:,3],2))
for mdl in ('kyle','jan'):
    m=jka.read_glm(R+r'\%s\model.glm'%mdl)
    print(mdl,m['anim'],m['numBones'],len(m['surfaces']),'lods',len(m['lods']))
    for i,s in enumerate(m['surfaces']):
        l=m['lods'][0][i]
        print(i,s['name'],'flags',s['flags'],s['shader'],'par',s['parent'],'v',len(l['verts']),'t',len(l['tris']),'br',len(l['brefs']))
