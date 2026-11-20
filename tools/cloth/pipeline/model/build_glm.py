"""Eve 2B -> JKA _humanoid GLM. Standalone (numpy). Usage: build_glm.py <variant> <targets.json|none> <outdir> [model]
Inputs: eve_mesh.npz(+.json) [original, indices intact], eve_<variant>.npz [decimated pieces], stock gla/kyle."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, json, math, collections
import numpy as np
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jka

variant = sys.argv[1]; targets = json.load(open(sys.argv[2])) if sys.argv[2] != 'none' else {}
outdir = sys.argv[3]; os.makedirs(outdir, exist_ok=True)
MODEL = sys.argv[4] if len(sys.argv) > 4 else 'eve2b'
S = 37.5            # blender metres -> JKA units
Z0 = 3.12           # stock foot-sole height in model space (kyle min z)
MAXV, MAXT = 880, 1800

gla = jka.read_gla(W + r'\src\stock\models\players\_humanoid\_humanoid.gla')
HB = {b['name']: b for b in gla['bones']}
HP = {n: b['base'][:, 3].copy() for n, b in HB.items()}
kyle = jka.read_glm(W + r'\src\stock\models\players\kyle\model.glm')

orig = np.load(W + r'\eve_mesh.npz'); ometa = json.load(open(W + r'\eve_mesh.npz.json'))
VG = ometa['vgroups']; MATS = ometa['materials']
EB = ometa['bones']; EIDX = {b['name']: i for i, b in enumerate(EB)}
EPAR = [EIDX[b['parent']] if b['parent'] else -1 for b in EB]
EHEAD = np.array([b['head'] for b in EB]) * S + np.array([0, 0, Z0])
vg2bone = np.array([EIDX.get(n, -1) for n in VG])

# ------------------------------------------------------------------ mapping table (Eve bone -> humanoid bones)
core = {'Root': 'pelvis', 'Bip001': 'pelvis', 'Bip001-Pelvis': 'pelvis', 'Bip001-Spine': 'lower_lumbar',
        'Bip001-Spine1': 'upper_lumbar', 'Bip001-Spine2': 'thoracic', 'Bip001-Neck': 'cervical', 'Bip001-Head': 'cranium'}
for s, p in (('L', 'l'), ('R', 'r')):
    core.update({f'Bip001-{s}-Clavicle': f'{p}clavical', f'Bip001-{s}-UpperArm': f'{p}humerus',
                 f'Bip001-{s}-Forearm': f'{p}radius', f'Bip001-{s}-Hand': f'{p}hand',
                 f'Bip001-{s}-Thigh': f'{p}femurYZ', f'Bip001-{s}-Calf': f'{p}tibia',
                 f'Bip001-{s}-Foot': f'{p}talus', f'Bip001-{s}-Toe0': f'{p}talus'})
    for f, d in ((0, 1), (1, 2), (2, 2), (3, 4), (4, 4)):
        core[f'Bip001-{s}-Finger{f}'] = f'{p}_d{d}_j1'
        core[f'Bip001-{s}-Finger{f}1'] = f'{p}_d{d}_j2'
        core[f'Bip001-{s}-Finger{f}2'] = f'{p}_d{d}_j2'
override = {}   # name -> list[(bone, frac)]
for s, p in (('L', 'l'), ('R', 'r')):
    override[f'Ab-{s}-UpperArm-Tw1'] = [(f'{p}humerusX', 1.0)]
    override[f'Ab-{s}-Forearm-Tw0'] = [(f'{p}radiusX', 1.0)]
    override[f'Ab-{s}-Forearm-Tw1'] = [(f'{p}radiusX', 1.0)]
    override[f'Ab-{s}-Thigh-Tw1'] = [(f'{p}femurX', 1.0)]
    for n in ('Dm-%s-Knee' % s, 'Ab-%s-Knee' % s, 'Dm-%s-Knee-Sub' % s):
        override[n] = [(f'{p}femurX', 0.5), (f'{p}tibia', 0.5)]
    for n in ('Dm-%s-Elbow' % s, 'Dm-%s-Elbow-Point' % s, 'Ab-%s-Elbow' % s):
        override[n] = [(f'{p}humerusX', 0.5), (f'{p}radius', 0.5)]
mapping = {}
rule = {}
for i, b in enumerate(EB):
    n = b['name']
    if n in override:
        mapping[n] = override[n]; rule[n] = 'override'; continue
    j = i
    while j >= 0 and EB[j]['name'] not in core: j = EPAR[j]
    if j < 0:
        mapping[n] = [('pelvis', 1.0)]; rule[n] = 'default-pelvis'
    else:
        mapping[n] = [(core[EB[j]['name']], 1.0)]; rule[n] = 'core' if j == i else 'nearest-core-ancestor:' + EB[j]['name']
# descendants of an override bone follow it (e.g. children of Dm-L-Knee-Sub)
for i, b in enumerate(EB):
    n = b['name']
    if rule[n].startswith('nearest') or rule[n] == 'default-pelvis':
        j = EPAR[i]
        while j >= 0:
            if EB[j]['name'] in override:
                mapping[n] = override[EB[j]['name']]; rule[n] = 'under-override:' + EB[j]['name']; break
            if EB[j]['name'] in core: break
            j = EPAR[j]
json.dump([dict(eve=n, humanoid=[dict(bone=a, fraction=f) for a, f in mapping[n]], rule=rule[n]) for n in (b['name'] for b in EB)],
          open(outdir + r'\bone_mapping.json', 'w'), indent=1)

# ------------------------------------------------------------------ rest-pose fitting transforms (per Eve bone, 4x4)
def rot_between(u, v):
    u = u / np.linalg.norm(u); v = v / np.linalg.norm(v)
    c = float(u @ v)
    if c > 1 - 1e-9: return np.eye(3)
    if c < -1 + 1e-9:
        a = np.cross(u, [1, 0, 0])
        if np.linalg.norm(a) < 1e-6: a = np.cross(u, [0, 1, 0])
        a /= np.linalg.norm(a); return 2 * np.outer(a, a) - np.eye(3)
    a = np.cross(u, v); s = np.linalg.norm(a); a /= s
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + s * K + (1 - c) * (K @ K)

def E(n): return EHEAD[EIDX[n]]

def build_T(foot_dz):
    R = [None] * len(EB); T = [None] * len(EB)
    keyspec = {}   # eve bone -> (hum bone, eve child pos, hum child pos, mode)
    def add(e, h, ec, hc, mode='dir'): keyspec[e] = (h, ec, hc, mode)
    add('Root', 'pelvis', None, None, 'ident'); add('Bip001', 'pelvis', None, None, 'ident')
    add('Bip001-Pelvis', 'pelvis', E('Bip001-Spine'), HP['lower_lumbar'])
    add('Bip001-Spine', 'lower_lumbar', E('Bip001-Spine1'), HP['upper_lumbar'])
    add('Bip001-Spine1', 'upper_lumbar', E('Bip001-Spine2'), HP['thoracic'])
    add('Bip001-Spine2', 'thoracic', E('Bip001-Neck'), HP['cervical'])
    add('Bip001-Neck', 'cervical', E('Bip001-Head'), HP['cranium'])
    add('Bip001-Head', 'cranium', None, None, 'inherit')
    for s, p in (('L', 'l'), ('R', 'r')):
        add(f'Bip001-{s}-Clavicle', f'{p}clavical', E(f'Bip001-{s}-UpperArm'), HP[f'{p}humerus'])
        add(f'Bip001-{s}-UpperArm', f'{p}humerus', E(f'Bip001-{s}-Forearm'), HP[f'{p}radius'])
        add(f'Bip001-{s}-Forearm', f'{p}radius', E(f'Bip001-{s}-Hand'), HP[f'{p}hand'])
        fing = np.mean([E(f'Bip001-{s}-Finger{k}') for k in (1, 2, 3, 4)], axis=0)
        add(f'Bip001-{s}-Hand', f'{p}hand', fing, (HP[f'{p}_d2_j1'] + HP[f'{p}_d4_j1']) / 2)
        add(f'Bip001-{s}-Thigh', f'{p}femurYZ', E(f'Bip001-{s}-Calf'), HP[f'{p}tibia'])
        add(f'Bip001-{s}-Calf', f'{p}tibia', E(f'Bip001-{s}-Foot'), HP[f'{p}talus'] + np.array([0, 0, foot_dz]))
        add(f'Bip001-{s}-Foot', f'{p}talus', None, None, 'ident')
    for i, b in enumerate(EB):
        n = b['name']; par = EPAR[i]
        Rp = R[par] if par >= 0 else np.eye(3)
        if n in keyspec:
            h, ec, hc, mode = keyspec[n]
            hp = HP[h].copy()
            if n.endswith('-Foot'): hp[2] += foot_dz
            if mode == 'dir': Rb = rot_between(Rp @ (ec - EHEAD[i]), hc - hp) @ Rp
            elif mode == 'inherit': Rb = Rp
            else: Rb = np.eye(3)
            M = np.eye(4); M[:3, :3] = Rb; M[:3, 3] = hp - Rb @ EHEAD[i]
            R[i] = Rb; T[i] = M
        else:
            R[i] = Rp; T[i] = T[par].copy() if par >= 0 else np.eye(4)
    return np.array(T)

# ------------------------------------------------------------------ pieces
def lbs(pos_s, wr, wc, wv, Tm):
    n = len(pos_s)
    bone = vg2bone[wc]; ok = bone >= 0
    wr, bone, wv = wr[ok], bone[ok], wv[ok].astype(np.float64)
    tot = np.bincount(wr, weights=wv, minlength=n)
    tot[tot == 0] = 1
    wv = wv / tot[wr]
    M = Tm[bone]
    p = np.einsum('kij,kj->ki', M[:, :3, :3], pos_s[wr]) + M[:, :3, 3]
    out = np.zeros((n, 3)); np.add.at(out, wr, p * wv[:, None])
    unw = np.bincount(wr, minlength=n) == 0
    return out, unw

def merge_weights(n, wr, wc, wv):
    bone = vg2bone[wc]; ok = bone >= 0
    acc = [collections.defaultdict(float) for _ in range(n)]
    for r, b, w in zip(wr[ok], bone[ok], wv[ok]):
        for hb, f in mapping[EB[b]['name']]: acc[r][hb] += float(w) * f
    out = []
    for d in acc:
        if not d: out.append([('pelvis', 1.0)]); continue
        items = sorted(d.items(), key=lambda x: -x[1])[:4]
        items = [(a, w) for a, w in items if w > 0.01] or items[:1]
        s = sum(w for _, w in items); out.append([(a, w / s) for a, w in items])
    return out

def smooth_normals(P, tris, crease_deg=60.0):
    fn = np.cross(P[tris[:, 1]] - P[tris[:, 0]], P[tris[:, 2]] - P[tris[:, 0]])
    area = np.linalg.norm(fn, axis=1); fnu = fn / np.maximum(area, 1e-12)[:, None]
    q = np.round(P[tris] / 1e-3).astype(np.int64)
    keys = (q[..., 0] * 73856093) ^ (q[..., 1] * 19349663) ^ (q[..., 2] * 83492791)
    keys = keys.reshape(-1)
    order = np.argsort(keys, kind='stable'); ks = keys[order]
    bounds = np.flatnonzero(np.diff(ks)) + 1
    groups = np.split(order, bounds)
    out = np.zeros((len(tris) * 3, 3)); ct = math.cos(math.radians(crease_deg))
    for g in groups:
        f = g // 3
        nf = fnu[f]; a = area[f]
        d = nf @ nf.T
        w = (d > ct) * a[None, :]
        nn = w @ nf
        l = np.linalg.norm(nn, axis=1); l[l == 0] = 1
        out[g] = nn / l[:, None]
    return out.reshape(-1, 3, 3), fnu

dec = None
pieces = {}
decfile = W + r'\eve_%s.npz' % variant
if targets and os.path.exists(decfile):
    dec = np.load(decfile)
for mi, mname in enumerate(MATS):
    if mname == 'M_Hidden': continue
    ntri = int((orig['mat'] == mi).sum())
    if dec is not None and targets.get(mname) and (mname + '/pos') in dec.files and targets[mname] < ntri:
        pos = dec[mname + '/pos']; tv = dec[mname + '/tv']; uv = dec[mname + '/uv']
        wr, wc, wv = dec[mname + '/wr'], dec[mname + '/wc'], dec[mname + '/wv']; src = 'decimated'
        oidx = None
    else:
        sel = orig['mat'] == mi
        tv_o = orig['tv'][sel]; uv = orig['uv0'][sel]
        used = np.unique(tv_o); remap = -np.ones(len(orig['pos']), dtype=np.int64); remap[used] = np.arange(len(used))
        pos = orig['pos'][used]; tv = remap[tv_o]
        m = np.isin(orig['wr'], used)
        wr = remap[orig['wr'][m]]; wc = orig['wc'][m]; wv = orig['wv'][m]; src = 'original'
        oidx = used
    pieces[mname] = dict(pos=pos, tv=tv.astype(np.int64), uv=uv, wr=wr, wc=wc, wv=wv, src=src, oidx=oidx)

T0 = build_T(0.0)
def lowest(Tm):
    p = pieces['M_Boots']
    fp, _ = lbs(p['pos'] * S + np.array([0, 0, Z0]), p['wr'], p['wc'], p['wv'], Tm)
    return fp[:, 2].min()
lo0 = lowest(T0)
FOOT_DZ = Z0 - lo0
Tm = build_T(FOOT_DZ)
print('foot dz', FOOT_DZ, 'lowest before', lo0, 'after', lowest(Tm))

H2I = {b['name']: b['index'] for b in gla['bones']}
region_of = {}
for n in HB:
    r = 'hips'
    if n in ('lower_lumbar', 'upper_lumbar', 'thoracic', 'cervical', 'lclavical', 'rclavical'): r = 'torso'
    elif n in ('cranium', 'face', 'jaw', 'ceyebrow', 'leye', 'reye') or 'lip' in n: r = 'head'
    elif n.startswith(('rhum', 'rrad', 'rhand', 'r_d', 'rhang')): r = 'r_arm'
    elif n.startswith(('lhum', 'lrad', 'lhand', 'l_d', 'lhang')): r = 'l_arm'
    elif n in ('rfemurYZ', 'rfemurX', 'rtibia', 'rtalus'): r = 'r_leg'
    elif n in ('lfemurYZ', 'lfemurX', 'ltibia', 'ltalus'): r = 'l_leg'
    region_of[n] = r

for mname, p in pieces.items():
    ps = p['pos'] * S + np.array([0, 0, Z0])
    fp, unw = lbs(ps, p['wr'], p['wc'], p['wv'], Tm)
    if unw.any():
        pel = Tm[EIDX['Bip001-Pelvis']]
        fp[unw] = (pel[:3, :3] @ ps[unw].T).T + pel[:3, 3]
    p['fit'] = fp; p['unweighted'] = int(unw.sum())
    p['w'] = merge_weights(len(fp), p['wr'], p['wc'], p['wv'])
    p['dom'] = [max(w, key=lambda x: x[1])[0] for w in p['w']]
    p['cn'], p['fn'] = smooth_normals(fp, p['tv'])
    print('fitted', mname, len(fp), flush=True)

# ------------------------------------------------------------------ surfaces
MATKEY = {'M_Body': 'body', 'M_Upper': 'upper', 'M_Skirt': 'skirt', 'M_Boots': 'boots', 'M_Glass': 'glass', 'M_Feather': 'feather',
          'M_SkirtRigid': 'skirtrigid', 'M_Head': 'head', 'M_Eye': 'eye', 'M_Mouth': 'mouth', 'M_Brow': 'brow', 'M_Teeth': 'teeth', 'M_Hair': 'hair'}
SHADER = 'models/players/%s/' % MODEL

def make_surface(name, shader, parent_name, p, tri_ids, mname):
    tv = p['tv'][tri_ids]; uv = p['uv'][tri_ids]; cn = p['cn'][tri_ids]
    keymap = {}; verts = []; src = []; tris = []
    for t in range(len(tv)):
        idx = []
        for c in range(3):
            v = int(tv[t, c]); u = (round(float(uv[t, c, 0]), 6), round(float(1 - uv[t, c, 1]), 6))
            n = tuple(np.round(cn[t, c], 3))
            k = (v, u, n)
            j = keymap.get(k)
            if j is None:
                j = len(verts); keymap[k] = j
                ws = p['w'][v]
                verts.append(dict(co=tuple(float(x) for x in p['fit'][v]), n=tuple(float(x) for x in cn[t, c]), uv=u,
                                  w=[w for _, w in ws], hb=[b for b, _ in ws]))
                src.append(v)
            idx.append(j)
        tris.append((idx[2], idx[1], idx[0]))   # glm stores the reverse of Blender CCW
    brefs = sorted({H2I[b] for v in verts for b in v['hb']})
    for v in verts: v['b'] = [brefs.index(H2I[b]) for b in v['hb']]
    return dict(name=name, flags=0, shader=shader, parent_name=parent_name, verts=verts, tris=tris, brefs=brefs,
                src=src, mname=mname)

def chunk_tris(p, tri_ids, order_key):
    tri_ids = tri_ids[np.argsort(order_key, kind='stable')]
    chunks = []; cur = []; vs = set(); bs = set()
    for t in tri_ids:
        ks = {(int(p['tv'][t, c]), round(float(p['uv'][t, c, 0]), 6), round(float(1 - p['uv'][t, c, 1]), 6), tuple(np.round(p['cn'][t, c], 3))) for c in range(3)}
        nb = {H2I[b] for c in range(3) for b, _ in p['w'][int(p['tv'][t, c])]}
        if cur and (len(vs | ks) > MAXV or len(cur) + 1 > MAXT or len(bs | nb) > 31):
            chunks.append(np.array(cur)); cur = []; vs = set(); bs = set()
        cur.append(int(t)); vs |= ks; bs |= nb
    if cur: chunks.append(np.array(cur))
    return chunks

region_names = ['hips', 'torso', 'head', 'r_arm', 'l_arm', 'r_leg', 'l_leg']
surf_list = []
body_groups = collections.OrderedDict()
for mname, p in pieces.items():
    key = MATKEY[mname]
    tr = p['tv']
    if key in ('skirt', 'skirtrigid'):
        P = p['fit']
        cen = P[tr].mean(axis=1)
        q = np.round(P[tr] / 0.01).astype(np.int64)
        tk = [tuple(sorted(map(tuple, q[t].tolist()))) for t in range(len(tr))]
        cnt = collections.Counter(tk)
        fnrm = p['fn']
        ctr = np.array([P[:, 0].mean(), P[:, 1].mean()])
        radial = cen[:, :2] - ctr
        rl = np.linalg.norm(radial, axis=1); rl[rl == 0] = 1; radial = radial / rl[:, None]
        outward = (fnrm[:, :2] * radial).sum(axis=1) > 0
        cls = np.where(np.array([cnt[k] for k in tk]) > 1, np.where(outward, 'outer', 'lining'), 'single')
        ang = np.arctan2(cen[:, 1] - ctr[1], cen[:, 0] - ctr[0])
        for c in ('outer', 'lining', 'single'):
            ids = np.flatnonzero(cls == c)
            if len(ids) == 0: continue
            for k, ch in enumerate(chunk_tris(p, ids, ang[ids])):
                prefix = 'skirt_' if key == 'skirt' else 'skirt_rigid_'
                sh = SHADER + ('skirt_2s' if c == 'single' else 'skirt')
                surf_list.append(make_surface(f'{prefix}{k}' if c == 'single' else f'{prefix}{c}_{k}', sh, 'hips', p, ch, mname))
        print(mname, 'skirt classes', {c: int((cls == c).sum()) for c in ('outer', 'lining', 'single')})
        continue
    reg = np.array([region_of[d] for d in p['dom']])
    rt = reg[tr]
    treg = np.empty(len(tr), dtype=object)
    for t in range(len(tr)):
        treg[t] = collections.Counter(rt[t].tolist()).most_common(1)[0][0]
    for r in region_names:
        ids = np.flatnonzero(treg == r)
        if len(ids) == 0: continue
        cz = p['fit'][tr[ids]].mean(axis=1)
        order = cz[:, 2] * 10000 + np.arctan2(cz[:, 1], cz[:, 0])
        for k, ch in enumerate(chunk_tris(p, ids, order)):
            body_groups.setdefault(r, []).append((mname, k, p, ch))

parent_of_region = {'hips': 'stupidtriangle_off', 'torso': 'hips', 'head': 'torso', 'r_arm': 'torso', 'l_arm': 'torso', 'r_leg': 'hips', 'l_leg': 'hips'}
body_surfs = []
for r in region_names:
    lst = body_groups.get(r, [])
    if not lst: continue
    big = max(lst, key=lambda x: len(x[3]))
    for (mname, k, p, ch) in lst:
        if (mname, k) == (big[0], big[1]): nm = r
        else: nm = f'{r}_{MATKEY[mname]}' + (f'_{k}' if k else '')
        body_surfs.append(make_surface(nm, SHADER + MATKEY[mname], r if nm != r else parent_of_region[r], p, ch, mname))

final = []
final.append(dict(name='stupidtriangle_off', flags=2, shader='', parent_name=None,
                  verts=[dict(co=(0, 0, 0), n=(0, 0, 1), uv=(0, 0), w=[1.0], b=[0]), dict(co=(1, 0, 0), n=(0, 0, 1), uv=(0, 0), w=[1.0], b=[0]),
                         dict(co=(0, 1, 0), n=(0, 0, 1), uv=(0, 0), w=[1.0], b=[0])], tris=[(0, 1, 2)], brefs=[H2I['pelvis']], src=[], mname=None))
for r in region_names:
    for s in body_surfs:
        if s['name'] == r: final.append(s)
for r in region_names:
    for s in body_surfs:
        if s['name'] not in region_names and s['parent_name'] == r: final.append(s)
final += surf_list
for i, ks in enumerate(kyle['surfaces']):
    if ks['flags'] & 1:
        ps = kyle['surfaces'][ks['parent']]['name'] if ks['parent'] >= 0 else 'torso'
        reg = 'torso'
        if ps.startswith('r_leg'): reg = 'r_leg'
        elif ps.startswith('l_leg'): reg = 'l_leg'
        elif ps.startswith('head'): reg = 'head'
        elif ps.startswith('r_'): reg = 'r_arm'
        elif ps.startswith('l_'): reg = 'l_arm'
        elif ps.startswith('hips'): reg = 'hips'
        kl = kyle['lods'][0][i]
        verts = [dict(co=v['co'], n=v['n'], uv=v['uv'], w=v['w'], b=v['b']) for v in kl['verts']]
        final.append(dict(name=ks['name'], flags=1, shader='', parent_name=reg, verts=verts, tris=[tuple(t) for t in kl['tris']],
                          brefs=list(kl['brefs']), src=[], mname=None))
names = [s['name'] for s in final]
assert len(set(names)) == len(names), ('dup names', [n for n,c in collections.Counter(names).items() if c>1])
for s in final:
    pn = s['parent_name']
    s['parent'] = names.index(pn) if pn in names else -1
    assert s['parent'] < names.index(s['name']), (s['name'], pn)
    assert len(s['verts']) <= 1000, (s['name'], len(s['verts']))
    assert len(s['tris']) <= 2000, s['name']
    assert len(s['brefs']) <= 32, (s['name'], len(s['brefs']))
size = jka.write_glm(outdir + r'\model.glm', f'models/players/{MODEL}/model.glm', 'models/players/_humanoid/_humanoid', 53, final)

rep = dict(variant=variant, scale=S, z0=Z0, foot_dz=FOOT_DZ, bytes=size, surfaces=[])
for s in final:
    rep['surfaces'].append(dict(name=s['name'], flags=s['flags'], parent=s['parent'], verts=len(s['verts']), tris=len(s['tris']),
                                bone_refs=len(s['brefs']), material=s['mname'], shader=s['shader']))
rep['totals'] = dict(surfaces=len(final), verts=sum(len(s['verts']) for s in final if not s['flags']), tris=sum(len(s['tris']) for s in final if not s['flags']))
rep['pieces'] = {m: dict(src=p['src'], verts=len(p['pos']), tris=len(p['tv']), unweighted=p['unweighted']) for m, p in pieces.items()}
json.dump(rep, open(outdir + r'\build_report.json', 'w'), indent=1)
Tk = {EB[i]['name']: Tm[i].round(6).tolist() for i in range(len(EB))}
skirts = []
for s in final:
    if s['name'].startswith('skirt'):
        oi = pieces[s['mname']]['oidx']
        if oi is None: raise SystemExit('skirt must not be decimated')
        skirts.append(dict(surface=s['name'], material=s['mname'], glm_vertex_count=len(s['verts']),
                           original_vertex_indices=[int(oi[v]) for v in s['src']], triangles=[list(t) for t in s['tris']]))
tj = dict(
    description='Eve blend space -> glm space: x_glm = T_bone(S*x_eve + (0,0,z_offset)) per vertex blended (LBS) over the Eve skin weights of all 382 bones, using the per-bone 4x4 matrices in bone_transforms (applied to the already scaled+lifted point). No axis change: both spaces are X=left, Y=back (face -Y), Z=up, right-handed.',
    scale=S, z_offset=Z0, foot_dz=FOOT_DZ, axis_change='none', units='1 Eve metre = scale JKA units',
    glm_triangle_order='glm stores each Blender CCW triangle reversed (i2,i1,i0); uv v is flipped (1-v)',
    skirt_vertex_index_space='original Eve blend mesh (object Eve, 112384 vertices) vertex indices; one entry per glm vertex (a vertex split by UV/normal appears repeatedly)',
    bone_transforms=Tk, skirt_surfaces=skirts)
json.dump(tj, open(outdir + r'\transform.json', 'w'))
print(json.dumps(rep['totals']), 'bytes', size, 'surfaces', len(final))
for m, d in rep['pieces'].items(): print(m, d)
