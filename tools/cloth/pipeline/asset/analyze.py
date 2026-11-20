"""Offline evaluation of a harness run on the reel.
Usage: analyze.py <asset-extra.npz> <reel.skp> <positions.bin> <stats.csv> <out_metrics.json> [--step 6]
Metrics follow ClothResearch probe_metrics.js (sample every 0.1 s after the 0.5 s pre-roll) plus an exact closest-triangle
signed distance against the same skinned collider, edge stretch, maxDist violation and inside-out fraction.
Environment: SKIRT_CLOTH_LAB (the skirt lab folder with Out/chaosref/hare.skd; no default)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os, sys, json, struct, argparse, csv
import numpy as np
from scipy.spatial import cKDTree
import skdio

ap = argparse.ArgumentParser()
ap.add_argument('extra'); ap.add_argument('pose'); ap.add_argument('pos'); ap.add_argument('stats'); ap.add_argument('out')
ap.add_argument('--step', type=int, default=6)
ap.add_argument('--thickness', type=float, default=2.2)
ap.add_argument('--back', type=float, default=9.0)
args = ap.parse_args()

E = np.load(args.extra)
refInv = E['refInv'].astype(np.float64); NB = refInv.shape[0]
sim0 = E['simPosCm']; tri = E['tri'].astype(np.int64); maxDist = E['maxDist']
skinB = E['skinB']; skinW = E['skinW']
cPos = E['cPos']; cT = E['cT'].astype(np.int64); cBI = E['cBI']; cW = E['cW']
N = len(sim0)
if not os.environ.get('SKIRT_CLOTH_LAB'):
    raise SystemExit('set SKIRT_CLOTH_LAB to the skirt lab folder (Out/chaosref/hare.skd)')
ske = skdio.read_skd(str(Path(os.environ['SKIRT_CLOTH_LAB']) / 'Out' / 'chaosref' / 'hare.skd'))
hem = ske['pos'][:, 2] < ske['meta']['hemZ']
dyn = maxDist > 0.01
CORE_LEG = 4.0; CORE_SPINE = 6.0
bn = None

# ---- pose file
f = open(args.pose, 'rb'); assert f.read(4) == b'SKP1'
ver, jb = struct.unpack('<II', f.read(8)); pm = json.loads(f.read(jb).decode('utf-8'))
fb = 12 + NB * 64; data0 = f.tell(); bn = pm['boneNames']
UE_MAP = {'lfemurYZ': 'Bip001-L-Thigh', 'rfemurYZ': 'Bip001-R-Thigh', 'ltibia': 'Bip001-L-Calf', 'rtibia': 'Bip001-R-Calf', 'pelvis': 'Bip001-Pelvis', 'lower_lumbar': 'Bip001-Spine', 'upper_lumbar': 'Bip001-Spine1'}
raw = np.memmap(args.pose, dtype=np.uint8, mode='r', offset=data0)
nfr = len(raw) // fb
def get_cst(i):
    return np.frombuffer(raw[i * fb + 12:(i + 1) * fb], dtype='<f4').reshape(NB, 4, 4).astype(np.float64)

# ---- sim positions
g = open(args.pos, 'rb'); assert g.read(4) == b'SKC1'
nv, nf = struct.unpack('<II', g.read(8))
rec = np.dtype([('i', '<u4'), ('p', '<f4', (nv, 3))])
P = np.fromfile(g, dtype=rec, count=nf)
pos_of = {int(r): k for k, r in enumerate(P['i'])}

def skin(bind, B, W, mats):
    """bind (n,3), B (n,k) bone ids (-1 none), W (n,k) -> (n,3), row-vector mats (NB,4,4)"""
    h = np.concatenate([bind, np.ones((len(bind), 1))], 1)
    out = np.zeros((len(bind), 3))
    for k in range(B.shape[1]):
        ok = B[:, k] >= 0
        m = mats[np.where(ok, B[:, k], 0)]
        out += np.where(ok[:, None], W[:, k:k + 1] * np.einsum('ni,nij->nj', h, m)[:, :3], 0)
    return out

# ---- edges for stretch
e = np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]]); e = np.unique(np.sort(e, 1), axis=0)
rest_len = np.linalg.norm(sim0[e[:, 0]] - sim0[e[:, 1]], axis=1)

def _adj():
    d = {}
    for t, (a, b, c) in enumerate(tri):
        for u, v in ((a, b), (b, c), (c, a)):
            d.setdefault((min(u, v), max(u, v)), []).append(t)
    return np.array([v[:2] for v in d.values() if len(v) == 2])
adj = _adj()

def tri_normals(Pp, T):
    return np.cross(Pp[T[:, 2]] - Pp[T[:, 0]], Pp[T[:, 1]] - Pp[T[:, 0]])

def closest_pt_tri(p, a, b, c):
    """Ericson 5.1.5 vectorised: p,a,b,c (...,3) -> closest point (...,3)"""
    ab = b - a; ac = c - a; ap_ = p - a
    d1 = (ab * ap_).sum(-1); d2 = (ac * ap_).sum(-1)
    bp = p - b; d3 = (ab * bp).sum(-1); d4 = (ac * bp).sum(-1)
    cp = p - c; d5 = (ab * cp).sum(-1); d6 = (ac * cp).sum(-1)
    vc = d1 * d4 - d3 * d2; vb = d5 * d2 - d1 * d6; va = d3 * d6 - d5 * d4
    out = np.empty_like(p)
    # default: interior
    den = va + vb + vc
    den = np.where(np.abs(den) < 1e-20, 1e-20, den)
    v = vb / den; w = vc / den
    res = a + ab * v[..., None] + ac * w[..., None]
    def setm(mask, val): res[mask] = val[mask]
    # regions in reverse priority
    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
    t = (d4 - d3) / np.where((d4 - d3) + (d5 - d6) == 0, 1e-20, (d4 - d3) + (d5 - d6)); setm(m, b + (c - b) * t[..., None])
    m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
    t = d2 / np.where(d2 - d6 == 0, 1e-20, d2 - d6); setm(m, a + ac * t[..., None])
    m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
    t = d1 / np.where(d1 - d3 == 0, 1e-20, d1 - d3); setm(m, a + ab * t[..., None])
    m = (d6 >= 0) & (d5 <= d6); setm(m, c + 0 * res)
    m = (d3 >= 0) & (d4 <= d3); setm(m, b + 0 * res)
    m = (d1 <= 0) & (d2 <= 0); setm(m, a + 0 * res)
    return res

def exact_sd(pts, cpos, T):
    """closest-triangle signed distance against the skinned collider. Sign and depth use the Phong (barycentric vertex-normal) normal at the
    closest point, so a point beside the free edge of an open ribbon is 'outside' (its displacement is tangential).
    returns sd_n (normal component, + outside), d (euclid), tri index"""
    Pt = cpos[T]
    cen = Pt.mean(1)
    tree = cKDTree(cen)
    fnorm = np.cross(Pt[:, 2] - Pt[:, 0], Pt[:, 1] - Pt[:, 0])
    vn = np.zeros_like(cpos)
    for k in range(3): np.add.at(vn, T[:, k], fnorm)
    vn /= np.linalg.norm(vn, axis=1, keepdims=True) + 1e-20
    _, ci = tree.query(pts, k=10)
    a = Pt[ci, 0]; b = Pt[ci, 1]; c = Pt[ci, 2]
    q = closest_pt_tri(pts[:, None, :], a, b, c)
    d = np.linalg.norm(pts[:, None, :] - q, axis=-1)
    j = d.argmin(1); rows = np.arange(len(pts))
    qq = q[rows, j]; t = ci[rows, j]
    A_, B_, C_ = cpos[T[t, 0]], cpos[T[t, 1]], cpos[T[t, 2]]
    v0 = B_ - A_; v1 = C_ - A_; v2 = qq - A_
    d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1); d20 = (v2 * v0).sum(1); d21 = (v2 * v1).sum(1)
    den = d00 * d11 - d01 * d01 + 1e-20
    bv = (d11 * d20 - d01 * d21) / den; bw = (d00 * d21 - d01 * d20) / den; bu = 1 - bv - bw
    nph = bu[:, None] * vn[T[t, 0]] + bv[:, None] * vn[T[t, 1]] + bw[:, None] * vn[T[t, 2]]
    nph /= np.linalg.norm(nph, axis=1, keepdims=True) + 1e-20
    sdn = ((pts - qq) * nph).sum(1)
    return sdn, d[rows, j], t

def probe_pen(pts, cpos, T, mask):
    """ClothResearch probe: 4 nearest collider verts within 8 cm, mean plane, depth behind it"""
    fn = tri_normals(cpos, T)
    vn = np.zeros_like(cpos)
    for k in range(3): np.add.at(vn, T[:, k], fn)
    vn /= np.linalg.norm(vn, axis=1, keepdims=True) + 1e-20
    tree = cKDTree(cpos)
    d, ix = tree.query(pts[mask], k=4, distance_upper_bound=8.0)
    depth = np.zeros(mask.sum())
    ok = np.isfinite(d[:, 0])
    for r in np.where(ok)[0]:
        sel = ix[r][np.isfinite(d[r])]
        pm = cpos[sel].mean(0); nn = vn[sel].sum(0); nl = np.linalg.norm(nn)
        if nl < 1e-4: continue
        depth[r] = max(0.0, -((pts[mask][r] - pm) @ nn) / nl)
    return depth

res = {}
pen_series = {}
for ck in pm['clips']:
    if ck['start'] not in pos_of: continue
    name, start, n, pre = ck['name'], ck['start'], ck['frames'], ck['evalStart']
    fr = list(range(start + pre, start + n))
    samp = fr[::args.step]
    rows = {k: [] for k in ['mean', 'max', 'hem', 'hem_max', 'puff', 'drop', 'probe_max', 'probe_cnt', 'sd_max', 'sd_cnt', 'sd_cnt1', 'sd_hem_cnt', 'sd_deep', 'core', 'band_max', 'band_min', 'skin_max', 'skin_min', 'fold', 'kin_in', 'edge_stretch', 'edge_comp', 'maxdist_viol', 'flip_frac', 'vmax', 'bound']}
    prev = None
    allmax_disp = 0.0
    for gi in fr:
        simp = P['p'][pos_of[gi]].astype(np.float64)
        if gi not in samp:
            # displacement metrics are cheap: use every frame for max displacement / speed only
            cst = get_cst(gi); mats = np.einsum('bij,bjk->bik', refInv, cst)
            sk = skin(sim0, skinB, skinW, mats)
            allmax_disp = max(allmax_disp, np.linalg.norm(simp - sk, axis=1)[dyn].max())
            if prev is not None: rows['vmax'].append(np.linalg.norm(simp - prev, axis=1).max() * 60)
            prev = simp
            continue
        cst = get_cst(gi); mats = np.einsum('bij,bjk->bik', refInv, cst)
        sk = skin(sim0, skinB, skinW, mats)
        cp = skin(cPos, cBI, cW, mats)
        d = np.linalg.norm(simp - sk, axis=1)
        rows['mean'].append(d[dyn].mean()); rows['max'].append(d[dyn].max()); rows['hem'].append(d[dyn & hem].mean()); rows['hem_max'].append(d[dyn & hem].max())
        allmax_disp = max(allmax_disp, d[dyn].max())
        pel = cst[(pm['boneNames'].index('pelvis') if 'pelvis' in pm['boneNames'] else pm['boneNames'].index('Bip001-Pelvis')), 3, :3]
        rr = lambda X: np.hypot(X[hem & dyn, 0] - pel[0], X[hem & dyn, 1] - pel[1]).mean()
        rows['puff'].append(rr(simp) / rr(sk)); rows['drop'].append((simp[hem & dyn, 2] - sk[hem & dyn, 2]).mean())
        pdp = probe_pen(simp, cp, cT, dyn)
        rows['probe_max'].append(pdp.max()); rows['probe_cnt'].append(int((pdp > 0).sum()))
        sd, de, ti = exact_sd(simp[dyn], cp, cT)
        ins = (sd < -0.25) & (-sd > 0.4 * de) & (de < args.back)    # behind the surface, mostly along its normal, within the solver's backDepth
        dd = np.where(ins, -sd, 0.0)
        rows['sd_max'].append(dd.max()); rows['sd_cnt'].append(int(ins.sum())); rows['sd_cnt1'].append(int((ins & (sd < -1.0)).sum())); rows['sd_deep'].append(int(((sd < -0.25) & (de >= args.back)).sum()))
        hm = hem[dyn]; rows['sd_hem_cnt'].append(int((ins & hm).sum()))
        sdk, dek, _ = exact_sd(simp[~dyn], cp, cT); rows['kin_in'].append(int(((sdk < -0.25) & (-sdk > 0.4 * dek) & (dek < args.back)).sum()))
        # core penetration: dynamic particles closer than 4 cm to a thigh axis (clearly inside a leg) or 6 cm to the spine axis
        pel_name = lambda n: cst[bn.index(n if n in bn else UE_MAP[n]), 3, :3]
        core = np.zeros(N, bool)
        for sd_ in 'lr':
            ax_a, ax_b = pel_name(sd_ + 'femurYZ'), pel_name(sd_ + 'tibia'); ab = ax_b - ax_a
            tt = np.clip(((simp - ax_a) @ ab) / (ab @ ab), 0, 1); core |= np.linalg.norm(simp - (ax_a + tt[:, None] * ab), axis=1) < CORE_LEG
        for n0, n1 in (('pelvis', 'lower_lumbar'), ('lower_lumbar', 'upper_lumbar')):
            ax_a, ax_b = pel_name(n0), pel_name(n1); ab = ax_b - ax_a
            tt = np.clip(((simp - ax_a) @ ab) / (ab @ ab), 0, 1); core |= np.linalg.norm(simp - (ax_a + tt[:, None] * ab), axis=1) < CORE_SPINE
        rows['core'].append(int((core & dyn).sum()))
        el = np.linalg.norm(simp[e[:, 0]] - simp[e[:, 1]], axis=1) / np.maximum(rest_len, 1e-6)
        em = rest_len > 0.5
        rows['edge_stretch'].append(el[em].max()); rows['edge_comp'].append(el[em].min())
        fnn = tri_normals(simp, tri); fnn = fnn / (np.linalg.norm(fnn, axis=1, keepdims=True) + 1e-20)
        dot = (fnn[adj[:, 0]] * fnn[adj[:, 1]]).sum(1); rows['fold'].append(float((dot < -0.5).mean()))
        ek = np.linalg.norm(sk[e[:, 0]] - sk[e[:, 1]], axis=1) / np.maximum(rest_len, 1e-6); kk = em & (~dyn[e[:, 0]]) & (~dyn[e[:, 1]])
        rows['band_max'].append(ek[kk].max()); rows['band_min'].append(ek[kk].min()); rows['skin_max'].append(ek[em].max()); rows['skin_min'].append(ek[em].min())
        rows['maxdist_viol'].append((d[dyn] / (0.85 * maxDist[dyn] + 1e-6)).max())
        tn = tri_normals(simp, tri); tk = tri_normals(sk, tri)
        alld = dyn[tri].all(1)
        cs = (tn * tk).sum(1) / (np.linalg.norm(tn, axis=1) * np.linalg.norm(tk, axis=1) + 1e-20)
        rows['flip_frac'].append(float((cs[alld] < 0).mean()))
        rows['bound'].append(float(np.abs(simp).max()))
        if prev is not None: rows['vmax'].append(np.linalg.norm(simp - prev, axis=1).max() * 60)
        prev = simp
    agg = lambda k, f=np.mean: float(f(rows[k])) if rows[k] else 0.0
    res[name] = dict(frames=len(fr), samples=len(samp),
        disp_mean=agg('mean'), disp_max_all_frames=float(allmax_disp), hem_mean=agg('hem'), hem_max=agg('hem_max', np.max),
        puff=agg('puff'), hem_drop=agg('drop'),
        probe_pen_mean_of_max=agg('probe_max'), probe_pen_worst=agg('probe_max', np.max), probe_pen_count=agg('probe_cnt'),
        sd_pen_mean_of_max=agg('sd_max'), sd_pen_worst=agg('sd_max', np.max), sd_inside_count_mean=agg('sd_cnt'), sd_inside_count_max=agg('sd_cnt', np.max),
        sd_deeper_than_1cm_mean=agg('sd_cnt1'), sd_beyond_backdepth_mean=agg('sd_deep'), core_inside_mean=agg('core'), core_inside_max=agg('core', np.max), fold_frac_mean=agg('fold'), fold_frac_max=agg('fold', np.max), sd_deeper_than_1cm_max=agg('sd_cnt1', np.max), sd_hem_inside_mean=agg('sd_hem_cnt'),
        kinematic_inside_mean=agg('kin_in'), kinematic_inside_max=agg('kin_in', np.max),
        band_edge_max=agg('band_max', np.max), band_edge_min=agg('band_min', np.min), skin_edge_max=agg('skin_max', np.max), skin_edge_min=agg('skin_min', np.min), edge_stretch_max=agg('edge_stretch', np.max), edge_compress_min=agg('edge_comp', np.min), maxdist_violation_max=agg('maxdist_viol', np.max),
        insideout_frac_mean=agg('flip_frac'), insideout_frac_max=agg('flip_frac', np.max),
        vertex_speed_max_cm_s=agg('vmax', np.max), bound_max_cm=agg('bound', np.max))
    print(name, {k: round(v, 3) if isinstance(v, float) else v for k, v in res[name].items() if k in ('disp_mean', 'disp_max_all_frames', 'sd_pen_worst', 'sd_inside_count_mean', 'insideout_frac_max', 'edge_stretch_max')}, flush=True)

# timing from stats csv
tm = {}
for r in csv.DictReader(open(args.stats)):
    tm.setdefault(r['clip'], []).append((float(r['ms']), int(r['contacts']), int(r['hand'])))
for k, v in tm.items():
    a = np.array(v)
    res[k]['ms_mean'] = float(a[1:, 0].mean()); res[k]['ms_p95'] = float(np.percentile(a[1:, 0], 95)); res[k]['contacts_mean'] = float(a[:, 1].mean()); res[k]['hand_contacts_mean'] = float(a[:, 2].mean())
json.dump(res, open(args.out, 'w', encoding='utf-8'), indent=1)
