"""Retarget every _humanoid.gla frame onto Eve's own proportions -> models/players/_humanoid/eve2b_np.gla
Usage: make_gla_np.py <joints_np.json> <out.gla> <her model.glm> [--identity] [--no-ik] [--smooth-loops [sigma]]
Method (see NOTES.md):
  stock pool entry A (unit-normalised quat + translation), skin F_i = F_parent A_i, rigid bone frame G_i = F_i [R_i|p_i]
  (R_i = stock base rotation with the 0.64 GLA scale removed).
  Her bones keep the stock GLOBAL rotation of every bone in every frame (= identical local rotations).
  Her bone positions: G'_i.t = G'_anchor * (o'_i + k_i (t_i(f) - o_i)), t_i(f) = stock offset in the anchor frame,
  o_i / o'_i = stock / her rest offset in the anchor frame, k_i = |o'_i|/|o_i| (rigid bones: t_i(f) == o_i, so her own offset).
  Anchor = parent, except humerus -> clavical, finger j1 -> hand, finger j2 -> j1 (stock offsets are constant there).
  model_root / pelvis / Motion: stock position scaled about the ground point (0,0,ZG) by her leg ratio, then the whole
  skeleton is moved vertically per frame so her lowest contact point (boot soles, knees, elbows, hands, pelvis, head)
  is at the same height as kyle's lowest contact point in that frame (feet meet the ground; airborne clearance kept).
  F'_i = G'_i [R_i|p'_i]^-1, A'_i = F'_parent^-1 F'_i, re-encoded (unit quat, 1/64 unit translation)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, json, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'model'))
import jkagla, jka
from retarget_lib import pool_rigid, rigid_base, skin_mats, topo

ST = str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players')
jn = json.load(open(sys.argv[1])); out = sys.argv[2]; identity = '--identity' in sys.argv
g = jkagla.read(ST + r'\_humanoid\_humanoid.gla')
names = [b['name'] for b in g['bones']]; NI = {n: i for i, n in enumerate(names)}
par = [b['parent'] for b in g['bones']]; nb = len(names); nfr = g['numFrames']
Bh = np.array([rigid_base(b)[0] for b in g['bones']]); sc = [rigid_base(b)[1] for b in g['bones']]
P = Bh[:, :3, 3].copy()
Pn = np.array([jn['joints'][n] for n in names]) if not identity else P.copy()
Bh2 = Bh.copy(); Bh2[:, :3, 3] = Pn
r = jn['ratio']['root'] if not identity else 1.0

# ---------------------------------------------------------------- stock decode
A = pool_rigid(g['pool'])[g['idx']]            # (nfr, nb, 4, 4)
F = skin_mats(A, par)
G = F @ Bh[None]
del A

# ---------------------------------------------------------------- contact points
kyle = jka.read_glm(ST + r'\kyle\model.glm')
def kyle_soles():
    pts = {}
    for p, sgn in (('l', 1), ('r', -1)):
        ti = NI[p + 'talus']; vs = []
        for s, l in zip(kyle['surfaces'], kyle['lods'][0]):
            if s['flags']: continue
            for v in l['verts']:
                bi = [l['brefs'][b] for b in v['b']]
                if bi[int(np.argmax(v['w']))] == ti: vs.append(v['co'])
        vs = np.array(vs); ymid = P[ti][1]
        toe = vs[vs[:, 1] < ymid]; heel = vs[vs[:, 1] >= ymid]
        pts[p] = dict(toe=toe[toe[:, 2].argmin()], heel=heel[heel[:, 2].argmin()])
    return pts
ks = kyle_soles()
hs = {p: {k: np.array(v) for k, v in d.items()} for p, d in jn['soles'].items()} if not identity else ks
CONTACT_JOINTS = ['ltibia', 'rtibia', 'lradius', 'rradius', 'lhand', 'rhand', 'pelvis', 'cranium', 'thoracic', 'lhumerus', 'rhumerus']
def contacts(Gm, base, soles, Pj):
    """z of contact points per frame: soles rigid with talus (rest offsets), plus joint positions."""
    zs = []
    for p in 'lr':
        ti = NI[p + 'talus']; inv = np.linalg.inv(base[ti])
        for k in ('toe', 'heel'):
            o = inv[:3, :3] @ soles[p][k] + inv[:3, 3]
            zs.append(np.einsum('fj,j->f', Gm[:, ti, 2, :3], o) + Gm[:, ti, 2, 3])
    for n in CONTACT_JOINTS: zs.append(Gm[:, NI[n], 2, 3])
    return np.stack(zs, 1)
zk = contacts(G, Bh, ks, P)
# ground point ZG: kyle's lowest sole in BOTH_STAND1 (frame 14651)
ZG = float(zk[14651, :4].min())

# ---------------------------------------------------------------- her positions
anchor = list(par)
for p in 'lr':
    anchor[NI[p + 'humerus']] = NI[p + 'clavical']
    for d in ('d1', 'd2', 'd4'):
        anchor[NI[f'{p}_{d}_j1']] = NI[p + 'hand']; anchor[NI[f'{p}_{d}_j2']] = NI[f'{p}_{d}_j1']
    anchor[NI[p + 'hang_tag_bone']] = NI[p + 'hand']   # the saber bolt tags (*r_hand/*l_hand) ride on this bone
ROOTS = [NI['model_root'], NI['pelvis'], NI['Motion']]
G2 = np.empty_like(G)
G2[:, :, :3, :3] = G[:, :, :3, :3]; G2[:, :, 3] = G[:, :, 3]
def S(t): return np.stack([r * t[..., 0], r * t[..., 1], ZG + r * (t[..., 2] - ZG)], -1)
for i in ROOTS: G2[:, i, :3, 3] = S(G[:, i, :3, 3])
# topological order over anchors
order = []; seen = set(ROOTS)
def visit(i):
    if i in seen: return
    visit(anchor[i]); seen.add(i); order.append(i)
for i in range(nb): visit(i)
kinfo = {}
for i in order:
    a = anchor[i]
    o = np.linalg.inv(Bh[a]) @ np.append(P[i], 1); o2 = np.linalg.inv(Bh2[a]) @ np.append(Pn[i], 1)
    o, o2 = o[:3], o2[:3]
    k = np.linalg.norm(o2) / np.linalg.norm(o) if np.linalg.norm(o) > 0.5 else 1.0
    k = float(np.clip(k, 0.5, 2.0)); kinfo[names[i]] = (names[a], round(k, 4))
    Ga_inv = np.linalg.inv(G[:, a])
    t = np.einsum('fij,fj->fi', Ga_inv[:, :3, :3], G[:, i, :3, 3]) + Ga_inv[:, :3, 3]
    t2 = o2 + k * (t - o)
    G2[:, i, :3, 3] = np.einsum('fij,fj->fi', G2[:, a, :3, :3], t2) + G2[:, a, :3, 3]
# two-handed grip: where kyle's *l_hand tag is on/near the hilt held by *r_hand, put hers at the same place relative to
# her *r_hand (same world offset; global rotations are shared) with a two-bone IK on her left arm (hand rotation kept).
def tag_offsets(glm, Pref, Bref):
    m = jka.read_glm(glm); o = {}
    for s, l in zip(m['surfaces'], m['lods'][0]):
        if s['name'] in ('*r_hand', '*l_hand'):
            v = l['verts'][2]; assert len(v['b']) == 1
            bi = l['brefs'][v['b'][0]]
            o[s['name']] = (bi, (np.linalg.inv(Bref[bi]) @ np.append(v['co'], 1))[:3])
    return o
def tag_pos(Gm, o): return np.einsum('fij,j->fi', Gm[:, o[0], :3, :3], o[1]) + Gm[:, o[0], :3, 3]
def rot_between_v(u, v):
    u = u / np.linalg.norm(u, axis=1, keepdims=True); v = v / np.linalg.norm(v, axis=1, keepdims=True)
    ax = np.cross(u, v); s_ = np.linalg.norm(ax, axis=1); c = (u * v).sum(1)
    ax = ax / np.maximum(s_, 1e-12)[:, None]
    K = np.zeros((len(u), 3, 3)); K[:, 0, 1] = -ax[:, 2]; K[:, 0, 2] = ax[:, 1]; K[:, 1, 0] = ax[:, 2]; K[:, 1, 2] = -ax[:, 0]; K[:, 2, 0] = -ax[:, 1]; K[:, 2, 1] = ax[:, 0]
    return np.eye(3)[None] + s_[:, None, None] * K + (1 - c)[:, None, None] * (K @ K)
grip = dict(frames=0)
if not identity and '--no-ik' not in sys.argv:
    ko = tag_offsets(ST + r'\kyle\model.glm', P, Bh); ho = tag_offsets(sys.argv[3], Pn, Bh2)
    kr, kl = tag_pos(G, ko['*r_hand']), tag_pos(G, ko['*l_hand'])
    hr, hl = tag_pos(G2, ho['*r_hand']), tag_pos(G2, ho['*l_hand'])
    dl = kl - kr; d = np.linalg.norm(dl, axis=1)
    # hilt axis = -Z bolt column; computed from the tag triangle like the engine
    def bolt_z(Gm, o_bone, verts):
        pts = [np.einsum('fij,j->fi', Gm[:, o_bone, :3, :3], v) + Gm[:, o_bone, :3, 3] for v in verts]
        sd = [pts[(j + 1) % 3] - pts[j] for j in range(3)]
        z = np.cross(sd[0], sd[2]); return -z / np.linalg.norm(z, axis=1, keepdims=True)
    km = jka.read_glm(ST + r'\kyle\model.glm')
    kv = [l for s_, l in zip(km['surfaces'], km['lods'][0]) if s_['name'] == '*r_hand'][0]['verts']
    hb = ko['*r_hand'][0]; kvl = [(np.linalg.inv(Bh[hb]) @ np.append(v['co'], 1))[:3] for v in kv]
    zax = -bolt_z(G, hb, kvl)
    along = (dl * zax).sum(1); perp = np.linalg.norm(dl - along[:, None] * zax, axis=1)
    w = np.clip((14.0 - d) / 4.0, 0, 1) * np.clip((3.5 - perp) / 1.5, 0, 1)
    sel = np.flatnonzero(w > 0)
    grip.update(frames=int(len(sel)), full=int((w >= 1).sum()))
    if len(sel):
        Sh, El, Hd = (G2[sel, NI[n], :3, 3] for n in ('lhumerus', 'lradius', 'lhand'))
        T = Hd + w[sel, None] * ((hr[sel] + dl[sel]) - hl[sel])
        a_ = np.linalg.norm(El - Sh, axis=1); b_ = np.linalg.norm(Hd - El, axis=1)
        dv = T - Sh; dd = np.linalg.norm(dv, axis=1); dn = dv / dd[:, None]
        dd_c = np.clip(dd, np.abs(a_ - b_) + 1e-3, a_ + b_ - 1e-3)
        grip['unreachable'] = int((dd > a_ + b_ - 1e-3).sum())
        pole = (El - Sh) - ((El - Sh) * dn).sum(1)[:, None] * dn
        pole /= np.maximum(np.linalg.norm(pole, axis=1), 1e-9)[:, None]
        ca = np.clip((a_ ** 2 + dd_c ** 2 - b_ ** 2) / (2 * a_ * dd_c), -1, 1)
        E2 = Sh + a_[:, None] * (ca[:, None] * dn + np.sqrt(1 - ca ** 2)[:, None] * pole)
        H2 = Sh + dd_c[:, None] * dn
        Q1 = rot_between_v(El - Sh, E2 - Sh)
        Q2 = rot_between_v(np.einsum('fij,fj->fi', Q1, Hd - El), H2 - E2)
        for n, Q, piv_old, piv_new in (('lhumerus', Q1, Sh, Sh), ('lhumerusX', Q1, Sh, Sh)):
            i = NI[n]; G2[sel, i, :3, :3] = Q @ G2[sel, i, :3, :3]
            G2[sel, i, :3, 3] = piv_new + np.einsum('fij,fj->fi', Q, G2[sel, i, :3, 3] - piv_old)
        Q21 = Q2 @ Q1
        for n in ('lradius', 'lradiusX'):
            i = NI[n]; G2[sel, i, :3, :3] = Q21 @ G2[sel, i, :3, :3]
            G2[sel, i, :3, 3] = E2 + np.einsum('fij,fj->fi', Q21, G2[sel, i, :3, 3] - El)
        shift = H2 - Hd
        for n in ('lhand', 'l_d1_j1', 'l_d1_j2', 'l_d2_j1', 'l_d2_j2', 'l_d4_j1', 'l_d4_j2', 'lhang_tag_bone'):
            G2[sel, NI[n], :3, 3] += shift
        hl2 = tag_pos(G2, ho['*l_hand'])
        err = np.linalg.norm(hl2[sel] - (hr[sel] + dl[sel]), axis=1)
        grip['residual_full_weight_max'] = float(err[w[sel] >= 1].max()) if (w[sel] >= 1).any() else None
        grip['shift_max'] = float(np.linalg.norm(shift, axis=1).max())
        np.save(os.path.splitext(out)[0] + '_gripw.npy', w.astype(np.float32))

# vertical ground correction
zh = contacts(G2, Bh2, hs, Pn)
dz = zk.min(1) - zh.min(1)
if identity: dz[:] = 0
dz_raw = dz.copy()
# --smooth-loops [sigma]: in looping sequences (animation.cfg loopFrames != -1: runs, walks, idles, stances) the
# per-frame correction jumps when the lowest sole changes from one foot to the other (BOTH_RUN1: her head popped
# 3.7 u in one frame, twice a stride, and bobbed 1.5x kyle's). Smooth it around each cycle (circular Gaussian);
# one-shot sequences (landings, flips, deaths) keep the exact per-frame contact.
smooth_info = None
if '--smooth-loops' in sys.argv and not identity:
    k = sys.argv.index('--smooth-loops')
    sigma = float(sys.argv[k + 1]) if k + 1 < len(sys.argv) and not sys.argv[k + 1].startswith('--') else 2.0
    loops = set()
    for line in open(ST + r'\_humanoid\animation.cfg', encoding='latin-1'):
        f = line.split('//')[0].split()
        if len(f) >= 5 and f[0].upper().startswith(('BOTH_', 'TORSO_', 'LEGS_')):
            s0, n0, lp = int(f[1]), int(f[2]), int(f[3])
            if lp != -1 and n0 >= 4 and s0 + n0 <= nfr:
                loops.add((s0, n0))
    touched = np.zeros(nfr, bool)
    for s0, n0 in sorted(loops):
        x = dz_raw[s0:s0 + n0]
        o = np.arange(-int(3 * sigma), int(3 * sigma) + 1)
        w = np.exp(-0.5 * (o / sigma) ** 2); w /= w.sum()
        dz[s0:s0 + n0] = sum(wi * np.roll(x, -oi) for oi, wi in zip(o, w))
        touched[s0:s0 + n0] = True
    smooth_info = dict(sigma=sigma, sequences=len(loops), frames=int(touched.sum()),
                       sole_vs_kyle_min=float((dz - dz_raw)[touched].min()), sole_vs_kyle_max=float((dz - dz_raw)[touched].max()))
G2[:, :, 2, 3] += dz[:, None]
zh = contacts(G2, Bh2, hs, Pn)

# ---------------------------------------------------------------- back to pool entries
F2 = G2 @ np.linalg.inv(Bh2)[None]
F2inv = np.linalg.inv(F2)
A2 = np.empty_like(F2)
for i in range(nb):
    A2[:, i] = F2[:, i] if par[i] < 0 else F2inv[:, par[i]] @ F2[:, i]
del F2inv
flat = A2[:, :, :3, :].reshape(-1, 3, 4)
enc = jkagla.encode(flat)
pool, inv_idx = np.unique(enc, axis=0, return_inverse=True)
idx = inv_idx.reshape(nfr, nb)
bones = []
for i, b in enumerate(g['bones']):
    base = np.eye(4); base[:3, :3] = Bh[i, :3, :3] * sc[i]; base[:3, 3] = Pn[i]
    bi = np.linalg.inv(base)
    bones.append(dict(name=b['name'], flags=b['flags'], parent=b['parent'], base=base[:3], baseinv=bi[:3], children=b['children']))
size = jkagla.write(out, 'models/players/_humanoid/eve2b_np', g['scale'], bones, idx, pool)

# ---------------------------------------------------------------- report
from retarget_lib import pool_rigid as _pr
dec = _pr(pool)[idx].reshape(-1, 4, 4)
ang = np.degrees(np.arccos(np.clip((np.einsum('nii->n', dec[:, :3, :3].transpose(0, 2, 1) @ flat[:, :, :3]) - 1) / 2, -1, 1)))
terr = np.abs(dec[:, :3, 3] - flat[:, :, 3]).max(1)
rep = dict(out=out, bytes=size, pool=len(pool), stock_pool=len(g['pool']), ZG=ZG, root_ratio=r, identity=identity,
           quant_rot_deg_max=float(ang.max()), quant_trans_max=float(terr.max()), anchors=kinfo,
           dz=dict(min=float(dz.min()), max=float(dz.max()), mean=float(dz.mean())), grip=grip, smooth_loops=smooth_info)
if identity:
    Ast = pool_rigid(g['pool'])[g['idx']].reshape(-1, 4, 4)
    rep['identity_vs_stock_rot_deg_max'] = float(np.degrees(np.arccos(np.clip((np.einsum('nii->n', Ast[:, :3, :3].transpose(0, 2, 1) @ flat[:, :, :3]) - 1) / 2, -1, 1))).max())
    rep['identity_vs_stock_trans_max'] = float(np.abs(Ast[:, :3, 3] - flat[:, :, 3]).max())
    rep['identity_pool_bytes_equal_frac'] = float((enc == g['pool'][g['idx'].reshape(-1)]).all(1).mean())
# segment length constancy (her)
L = {}
for i in range(nb):
    if par[i] >= 0 and anchor[i] == par[i] and names[i] not in ('pelvis', 'Motion') and 'hang' not in names[i] and 'tail' not in names[i]:
        d = np.linalg.norm(G2[:, i, :3, 3] - G2[:, par[i], :3, 3], axis=1)
        L[names[i]] = [round(float(d.min()), 3), round(float(d.max()), 3), round(float(np.linalg.norm(Pn[i] - Pn[par[i]])), 3)]
rep['segment_len_min_max_rest'] = L
np.savez_compressed(os.path.splitext(out)[0] + '_contacts.npz', zk=zk, zh=zh, dz=dz)
json.dump(rep, open(os.path.splitext(out)[0] + '_report.json', 'w'), indent=1)
print(json.dumps({k: v for k, v in rep.items() if k not in ('anchors', 'segment_len_min_max_rest')}, indent=1))
