"""Build the game cloth asset (SKA1) for Eve 2B's skirt on the JKA _humanoid skeleton.
Usage: build_cloth_asset.py <out.cloth> [--collider shell|full] [--report report.json]
Inputs are read-only (GLM work folder, hare.skd). Units inside the file: cm, glm axes (X left, Y back, Z up)."""
import sys, json, struct, zlib, argparse, numpy as np
from scipy.spatial import cKDTree
import common, glaio

ap = argparse.ArgumentParser()
ap.add_argument('out'); ap.add_argument('--collider', default='shell'); ap.add_argument('--report', default=None)
ap.add_argument('--zmin', type=float, default=35.0); ap.add_argument('--zmax', type=float, default=128.0)
ap.add_argument('--extra', default='asset_extra.npz')
ap.add_argument('--maxedge', type=float, default=2.5, help='split collider edges longer than this (cm); the solver finds contacts through collider vertices within ~2.5 cm')
args = ap.parse_args()

def subdivide(P, T, BI, W, Lmax, max_iter=8):
    """split every edge longer than Lmax at its midpoint (shared splits, so the mesh stays watertight where it was);
    new vertices take the mean of the two end positions and the merged, top-4 skin weights."""
    P = [p for p in P]; BI = [b for b in BI]; W = [w for w in W]
    T = [tuple(t) for t in T]
    for it in range(max_iter):
        mid = {}; Pa = np.array(P)
        def edge_long(a, b): return np.linalg.norm(Pa[a] - Pa[b]) > Lmax
        any_split = False
        newT = []
        for (a, b, c) in T:
            es = [(a, b), (b, c), (c, a)]
            lng = [edge_long(*e) for e in es]
            if not any(lng): newT.append((a, b, c)); continue
            any_split = True
            m = []
            for (u, v), l in zip(es, lng):
                if not l: m.append(None); continue
                k = (min(u, v), max(u, v))
                if k not in mid:
                    P.append((Pa[u] + Pa[v]) / 2)
                    acc = {}
                    for src in (u, v):
                        for bb, ww in zip(BI[src], W[src]):
                            if bb >= 0 and ww > 0: acc[int(bb)] = acc.get(int(bb), 0.0) + 0.5 * ww
                    top = sorted(acc.items(), key=lambda x: -x[1])[:4]
                    tw = sum(x[1] for x in top)
                    BI.append(np.array([x[0] for x in top] + [-1] * (4 - len(top)), np.int32))
                    W.append(np.array([x[1] / tw for x in top] + [0.0] * (4 - len(top))))
                    mid[k] = len(P) - 1
                m.append(mid[k])
            ab, bc, ca = m
            n = sum(x is not None for x in m)
            if n == 3:
                newT += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
            elif n == 1:
                if ab is not None: newT += [(a, ab, c), (ab, b, c)]
                elif bc is not None: newT += [(a, b, bc), (a, bc, c)]
                else: newT += [(a, b, ca), (ca, b, c)]
            else:   # two long edges: the corner shared by them is cut off, the remaining quad is split in two
                if ca is None: newT += [(ab, b, bc), (a, ab, bc), (a, bc, c)]
                elif ab is None: newT += [(bc, c, ca), (a, b, bc), (a, bc, ca)]
                else: newT += [(ca, a, ab), (ab, b, c), (ab, c, ca)]
        T = newT
        if not any_split: break
    return np.array(P), np.array(T), np.array(BI, np.int32), np.array(W)

K = common.CM_PER_UNIT
A = common.load_all(); d = A['skd']; glm = A['glm']; g = glaio.Gla()
NB = g.numBones; N = len(d['pos']); NT = len(d['tri'])
bm = {e['eve']: e for e in A['bm']}
ue_names = d['meta']['bones']
rep = {}

# ---------------------------------------------------------------- skeleton: base_ns (orthonormal rotation), refInv in cm
base_ns = np.zeros((NB, 4, 4)); base_ns[:, 3, 3] = 1
for b in range(NB):
    M = g.base[b, :3, :3]; u, s, vt = np.linalg.svd(M)
    R = u @ vt
    if np.linalg.det(R) < 0: R = -R
    base_ns[b, :3, :3] = R; base_ns[b, :3, 3] = g.base[b, :3, 3] * K
refInv = np.stack([np.linalg.inv(base_ns[b]).T for b in range(NB)]).astype(np.float32)   # row-vector convention
REF = g.names.index('pelvis')

# ---------------------------------------------------------------- sim mesh in glm bind space (cm)
ev, dist = common.sim_to_eve(A)
sg = common.skirt_glm_verts(A)
lut = {}
for s in sg:
    for i, e in enumerate(s['eve']): lut.setdefault(int(e), (s, i))
pos = np.array([lut[int(e)][0]['pos'][lut[int(e)][1]] for e in ev], dtype=np.float64) * K
rep['sim_eve_match_max_cm'] = float(dist.max())

# ---------------------------------------------------------------- skin weights, method A (UE influences through bone_mapping.json), top 4
NI = 4
WA = np.zeros((N, NB))
for v in range(N):
    for b, w in zip(d['skinB'][v], d['skinW'][v]):
        if b < 0 or w <= 0: continue
        for h in bm[ue_names[b]]['humanoid']: WA[v, g.names.index(h['bone'])] += w * h['fraction']
WB = np.zeros((N, NB))   # method B: weights of the matched glm skirt render vertex
for v in range(N):
    s, i = lut[int(ev[v])]
    for b, w in zip(s['bone'][i], s['w'][i]):
        if b >= 0: WB[v, b] += w
def topk(W, k):
    idx = np.argsort(-W, axis=1)[:, :k]; w = np.take_along_axis(W, idx, 1)
    w = w / w.sum(1, keepdims=True); idx = np.where(w > 0, idx, -1)
    return idx.astype(np.int32), w.astype(np.float32)
skinB, skinW = topk(WA, NI)
rep['skin_nnz_hist_A'] = np.bincount((WA > 1e-6).sum(1)).tolist()
rep['skin_A_vs_B_L1_mean'] = float(np.abs(WA - WB).sum(1).mean()); rep['skin_A_vs_B_L1_max'] = float(np.abs(WA - WB).sum(1).max())

# ---------------------------------------------------------------- body collider from the budget glm
BODY = ['hips', 'hips_upper', 'torso', 'torso_body', 'torso_upper_1', 'torso_upper_2', 'r_leg', 'l_leg', 'r_leg_body', 'l_leg_body',
        'r_leg_boots_1', 'r_leg_boots_2', 'l_leg_boots_1', 'l_leg_boots_2']
names = [s['name'] for s in glm['surfaces']]
mat = {r['name']: r['material'] for r in json.load(open(common.NP + 'build/lite/build_report.json', encoding='utf-8'))['surfaces']}
parts = []
for nm in BODY:
    k = names.index(nm); sf = glm['lods'][0][k]
    P = np.array([v['co'] for v in sf['verts']], dtype=np.float64) * K
    Nn = np.array([v['n'] for v in sf['verts']], dtype=np.float64)
    bi = np.full((len(P), 4), -1, np.int32); wt = np.zeros((len(P), 4))
    for i, v in enumerate(sf['verts']):
        for j, (b, w) in enumerate(zip(v['b'], v['w'])): bi[i, j] = sf['brefs'][b]; wt[i, j] = w
    parts.append(dict(name=nm, mat=mat[nm], P=P, N=Nn, bi=bi, wt=wt, T=np.array(sf['tris'], dtype=np.int64)))

def tri_cross(P, T):   # (c-a) x (b-a)
    return np.cross(P[T[:, 2]] - P[T[:, 0]], P[T[:, 1]] - P[T[:, 0]])
orient = []
for p in parts:
    c = tri_cross(p['P'], p['T']); nv = p['N'][p['T']].sum(1)
    orient.append(float(((c * nv).sum(1) > 0).mean()))
rep['collider_outward_fraction_file_order'] = dict(zip(BODY, orient))
for p, o in zip(parts, orient):
    if o < 0.5: p['T'] = p['T'][:, [0, 2, 1]]       # make (c-a)x(b-a) point out of the body
    p['tri_flipped'] = bool(o < 0.5)

allP = np.concatenate([p['P'] for p in parts]); allN = np.concatenate([p['N'] for p in parts])
allBI = np.concatenate([p['bi'] for p in parts]); allW = np.concatenate([p['wt'] for p in parts])
allT = []; allM = []; allS = []; off = 0
for p in parts:
    allT.append(p['T'] + off); allM += [p['mat']] * len(p['T']); allS += [p['name']] * len(p['T']); off += len(p['P'])
allT = np.concatenate(allT); allM = np.array(allM); allS = np.array(allS)
cz = allP[allT][:, :, 2].mean(1)
keep = (cz >= args.zmin) & (cz <= args.zmax)
rep['collider_candidates_tris'] = int(len(allT)); rep['collider_height_band_tris'] = int(keep.sum())

if args.collider == 'shell':
    # drop M_Body triangles that lie under clothing: a ray from each body vertex along its normal hits M_Upper / M_Boots within 6 cm
    cloth = np.where((allM != 'M_Body') & keep)[0]
    cp = allP[allT[cloth]]; cen = cp.mean(1); tree = cKDTree(cen)
    def covered(v):
        o = allP[v]; n = allN[v]
        if np.linalg.norm(n) < 1e-9: return False
        n = n / np.linalg.norm(n)
        for ci in tree.query(o + n * 2.0, k=24)[1]:
            a, b, c = cp[ci]
            e1, e2 = b - a, c - a; h = np.cross(n, e2); det = e1 @ h
            if abs(det) < 1e-12: continue
            s = o - a; u = (s @ h) / det
            if u < -0.02 or u > 1.02: continue
            q = np.cross(s, e1); w = (n @ q) / det
            if w < -0.02 or u + w > 1.02: continue
            t = (e2 @ q) / det
            if 0.0 < t < 6.0: return True
        return False
    body_v = np.unique(allT[keep & (allM == 'M_Body')])
    cov = {int(v): covered(int(v)) for v in body_v}
    drop = np.array([bool(keep[i]) and (allM[i] == 'M_Body') and all(cov[int(v)] for v in allT[i]) for i in range(len(allT))])
    rep['collider_body_tris_under_clothing_dropped'] = int((keep & drop).sum())
    keep &= ~drop

T = allT[keep]
used = np.unique(T)
key = np.concatenate([np.round(allP[used] * 1e3).astype(np.int64), allBI[used].astype(np.int64), np.round(allW[used] * 1e3).astype(np.int64)], axis=1)
_, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
vmap = np.zeros(len(allP), np.int64); vmap[used] = inv.ravel()
cPos = allP[used][first]; cBI = allBI[used][first]; cW = allW[used][first]
cT = vmap[T]
good = (cT[:, 0] != cT[:, 1]) & (cT[:, 1] != cT[:, 2]) & (cT[:, 0] != cT[:, 2])
cT = cT[good]
cSurf = allS[keep][good]
ws = cW.sum(1, keepdims=True); cW = cW / np.where(ws > 0, ws, 1)
cBI = np.where(cW > 0, cBI, -1).astype(np.int32)
rep['collider_before_subdivision'] = dict(verts=int(len(cPos)), tris=int(len(cT)))
if args.maxedge > 0:
    cPos, cT, cBI, cW = subdivide(cPos, cT, cBI, cW, args.maxedge)
rep['collider_verts'] = int(len(cPos)); rep['collider_tris'] = int(len(cT)); rep['collider_mode'] = args.collider
rep['collider_surface_tri_counts'] = {n: int((allS[keep] == n).sum()) for n in BODY}


# ---------------------------------------------------------------- capsules (solver table, rebound to _humanoid)
cap_defs = []
for p in ('l', 'r'):
    cap_defs += [(f'{p}radius', f'{p}hand', 2.8), (f'{p}hand', f'{p}_d2_j1', 1.6), (f'{p}hand', f'{p}_d4_j1', 1.6),
                 (f'{p}_d2_j1', f'{p}_d2_j2', 1.0), (f'{p}_d4_j1', f'{p}_d4_j2', 1.0), (f'{p}_d1_j1', f'{p}_d1_j2', 1.0)]
capA = np.array([g.names.index(a) for a, b, r in cap_defs], np.int32); capB = np.array([g.names.index(b) for a, b, r in cap_defs], np.int32)
capR = np.array([r for a, b, r in cap_defs], np.float64)
rep['capsules'] = [f'{a}->{b} r={r}' for a, b, r in cap_defs]

# ---------------------------------------------------------------- render map: glm skirt_0..3 vertex -> sim vertex
sim_of_eve = {int(e): i for i, e in enumerate(ev)}
rmap = []
tree = cKDTree(pos)
for s in sg:
    m = np.array([sim_of_eve[int(e)] for e in s['eve']], np.uint32)
    dd, nn = tree.query(s['pos'] * K)
    rmap.append((s['name'], m, dd.astype(np.float32), bool((nn == m).all())))
rep['render_surfaces'] = {n: int(len(m)) for n, m, _, _ in rmap}
rep['render_verts'] = int(sum(len(m) for _, m, _, _ in rmap))
rep['render_to_sim_max_dist_cm'] = float(max(dd.max() for _, _, dd, _ in rmap))
rep['render_nearest_equals_identity'] = bool(all(x[3] for x in rmap))
rep['sim_verts_without_render_vert'] = int(N - len(set(np.concatenate([m for _, m, _, _ in rmap]).tolist())))

# ---------------------------------------------------------------- write SKA1
import skaio
cfg = d['meta']['config']
config = [cfg['gravity'][0], cfg['gravity'][1], cfg['gravity'][2], cfg['density'], cfg['minPerParticleMass'],
          cfg['edge'][0], cfg['bend'][0], cfg['area'][0], cfg['tetherStiffness'][0], cfg['drag'][0], cfg['lift'][0]]
nbytes = skaio.write_ska(args.out, dict(unit_cm=K, ref_bone=REF, bone_names=g.names, refInv=refInv, pos=pos, tri=d['tri'], maxDist=d['maxDist'],
    animDrive=d['animDrive'], skinB=skinB, skinW=skinW, col_pos=cPos, col_tri=cT, col_skinB=cBI, col_skinW=cW, capA=capA, capB=capB, capR=capR,
    config=config, skeleton='_humanoid', render=[(n, m, dd) for n, m, dd, _ in rmap]))
hb = struct.calcsize(skaio.HEADER)   # the SKA1 header's size, from its format
rep['file_bytes'] = nbytes; rep['headerBytes'] = hb
rep['counts'] = dict(bones=NB, simVerts=N, simTris=NT, nInfl=NI, colVerts=len(cPos), colTris=len(cT), caps=len(cap_defs))
rep['kinematic_verts'] = int((d['maxDist'] <= 0.01).sum())
np.savez(args.extra, simPosCm=pos, simEve=ev, WA=WA, WB=WB, cPos=cPos, cT=cT, cBI=cBI, cW=cW, refInv=refInv, base_ns=base_ns,
         skinB=skinB, skinW=skinW, tri=d['tri'], maxDist=d['maxDist'], capA=capA, capB=capB, capR=capR)
if args.report: json.dump(rep, open(args.report, 'w', encoding='utf-8'), indent=1)
print(json.dumps(rep, indent=1))
