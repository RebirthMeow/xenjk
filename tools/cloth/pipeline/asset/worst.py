import numpy as np, sys, os
sys.argv=['x','asset_extra.npz','reel.skp','reel_f.bin','reel_f_stats.csv','/dev/null']
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'analyze.py')).read().split('res = {}')[0])
def seg(simp,cst,n0,n1):
    a=cst[bn.index(n0),3,:3]; b=cst[bn.index(n1),3,:3]; ab=b-a; t=np.clip(((simp-a)@ab)/(ab@ab),0,1); return np.linalg.norm(simp-(a+t[:,None]*ab),axis=1)
out=[]
for name in ['cartwheel','flip_back','turn_run','wallrun','flip']:
    ck=[c for c in pm['clips'] if c['name']==name][0]
    best=[]
    for k in range(ck['evalStart'],ck['frames'],2):
        gi=ck['start']+k
        simp=P['p'][pos_of[gi]].astype(float); cst=get_cst(gi)
        core=np.zeros(N,bool)
        for s in 'lr': core|=seg(simp,cst,s+'femurYZ',s+'tibia')<CORE_LEG
        core|=seg(simp,cst,'pelvis','lower_lumbar')<CORE_SPINE
        best.append(((core&dyn).sum(),round(k/60,2)))
    best.sort(reverse=True); print(name,best[:6])
