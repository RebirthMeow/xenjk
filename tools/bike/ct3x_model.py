"""Yaiba Kusanagi CT-3X (CP2077, appearance player_01) -> Ghoul2 vehicle model with its own GLA.

docs/wishlist/vehicles.md section 3.1: the bike is models/players/ct3x/model.glm on a small skeleton of its own, the
CP2077 vehicle rig as authored (27 bones: suspension, steering, wheels, grips), so cgame can turn the wheels and the
fork with G2API_SetBoneAngles. No animation frames: the GLA holds the rest pose only.

  parts   cp_vehicle.parts (rig reference pose * vehicle_slots slot * component local), the way the game assembles
          a vehicle; a rigid part rides its slot's bone, a skinned part (rod = handlebar, axle_f = fork) keeps its
          own joints and weights
  paint   ct3x_materials.Paint: per chunk, multilayer bakes with tiled detail (tools/levelport/cp77mat.py), decals,
          glass, emissive lights; perceptual roughness as CP2077 and rd-vulkan both use
  LODs    GLM LOD 0-3 = CP LOD 1, 2, 4, 8 (the Ghoul2 trace heap wants the traced LODs well under 12k verts)
  frame   CP vehicle space (+Y forward, +X right, +Z up, metres) -> GLM (forward -Y, left +X, up +Z, the stock swoop's
          frame) at the level port's 37.5 u/m: g = (-x, -y, z) * 37.5, then 24 u down: a vehicle's origin is 24 u
          above the bottom of its box, so the ground under the tyres is at z = -24; the model is not moved to fit
          the stock swoop's rider.

Inputs are a fetch folder (default .codex-research/wishlist-20261101/drafts/bike/model): json/ (serialised .app,
.ent, .rig), raw/ (unbundled CR2W) and glb/ (WolvenKit uncook with every LOD, textures and mask layers).
Uses the CP2077 vehicle tools in the folder CP_TOOLS names (cp_vehicle, cp_car, cp_multilayer, fastglb, glm_io; not
in this repository) and tools/cloth/pipeline/skeleton/jkagla.py for the GLA.

Environment: CP_TOOLS (required); XENJK_MAIN (the checkout holding .codex-research, default this repository);
JKA_ASSETS1 (the stock assets1.pk3 with the swoop's tags, default game_directory/Jedi Academy/GameData/base).

  python -I ct3x_model.py [--src DIR] [--stage DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
if not os.environ.get('CP_TOOLS'):
    raise SystemExit('set CP_TOOLS to the folder with the CP2077 vehicle tools (cp_vehicle, cp_car, fastglb, glm_io)')
CPTOOLS = Path(os.environ['CP_TOOLS'])
sys.path[:0] = [str(HERE), str(CPTOOLS),str(REPO / 'tools' / 'cloth' / 'pipeline' / 'skeleton')]
import cp_car  # noqa: E402
import ct3x_materials  # noqa: E402
import cp_vehicle  # noqa: E402
import fastglb  # noqa: E402
import glm_io  # noqa: E402
import jkagla  # noqa: E402

MAIN = Path(os.environ.get('XENJK_MAIN', REPO))   # research data lives in the main checkout
SRC = MAIN / '.codex-research' / 'wishlist-20261101' / 'drafts' / 'bike' / 'model'
ASSETS1 = Path(os.environ.get('JKA_ASSETS1', REPO / 'game_directory' / 'Jedi Academy' / 'GameData' / 'base'
                              / 'assets1.pk3'))   # the stock swoop's tag triangles
NAME = 'ct3x'
NS = f'models/players/{NAME}'
APPEARANCE = 'player_01'
UNITS = 37.5                      # u per metre (tools/levelport/levelport.py)
CP_LODS = (1, 2, 4, 8)            # GLM LOD k = CP lodMask bit CP_LODS[k]
MAX_VERTS, MAX_TRIS = 990, 1900   # per surface (md3.MAX_VERTS: the renderer rejects 1000)
DEPOT = 'base/vehicles/sportbike/v_sportbike1_yaiba_kusanagi'
CFG = dict(app='yaiba_kusanagi_basic.app.json', ent='v_sportbike1_yaiba_kusanagi_basic_01.ent.json',
           rig='v_sportbike1_yaiba_kusanagi.rig.json',
           # fuel port: the car filler under the cap; licence plate: its number is a decal
           skip_parts=('fuelport', 'interaction_collider', 'license'), tex_size=2048)
# tags (GLM bolts) on the bone they move with: (name, bone, offset from the bone in CP metres)
TAGS = (('*driver', 'seat_front', (0.0, 0.0, 0.0)),
        ('*bar_l', 'steering_left', (0.0, 0.0, 0.0)),
        ('*bar_r', 'steering_right', (0.0, 0.0, 0.0)))
SWOOP_TAG = '*driver'             # the stock tag whose triangle (axis) every tag copies
# GLM-frame shifts (u) after the bone placement: on seat_front the retargeted CP2077 rider clips sat the rider 4.8 u
# short of the grips (cg_bikeSeatDebug shows it). ct3x_seat_patch.py
# applies the same shift to a built pack.
TAG_SHIFT_U = {'*driver': (0.0, -5.0, 0.0)}


# CP's rig puts both axles at z = 0 (Trajectory), and the tyres differ: rear radius 0.35 m, front 0.31 m. The model's
# origin is the ground under the rear tyre; the rest frame swings the front suspension arm down until the front tyre
# touches it too (RIDE_ARM_DEG about the arm's pivot, the suspension_front bone: a hub-centre swingarm, not a fork)
GROUND_M = 0.35
# a vehicle's box bottom is DEFAULT_MINS_2 below its origin (bg_pmove.c BG_VehicleAdjustBBoxForOrientation): the tyres
# stand there
BOX_BOTTOM_U = -24.0
RIDE_ARM_DEG = 5.8
# GLM LODs 2 and 3 are traced (Ghoul2 trace heap): vertex budgets, met by clustering (see cluster())
LOD_VERTS = {2: 8000, 3: 3000}


def to_glm(p):
    """CP vehicle space (n,3) metres -> GLM model units, the ground at the box bottom."""
    p = np.asarray(p, np.float64)
    return np.stack([-p[:, 0], -p[:, 1], p[:, 2] + GROUND_M], 1) * UNITS + (0.0, 0.0, BOX_BOTTOM_U)


def cluster(X, N, U, T, Wt, cell, uvq):
    """Vertex-clustering decimation of one shader group: vertices sharing a cell of `cell` units, a UV cell of 1/uvq
    and a first bone merge (at their mean position, the first one's normal, UV and weights); collapsed and repeated
    triangles go. Cheap and seam-keeping, but coarse: for the traced and far LODs only."""
    bone = np.array([w[0][0] for w in Wt])
    key = np.c_[np.floor(X / cell), np.floor(U * uvq), bone].astype(np.int64)
    _u, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1)
    cnt = np.bincount(inv)
    Xm = np.stack([np.bincount(inv, X[:, k]) for k in range(3)], 1) / cnt[:, None]
    t = inv[T]
    t = t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])]
    if len(t):
        t = np.unique(t, axis=0)
    return Xm, N[first], U[first], t, [Wt[i] for i in first]


R_GLM = np.diag([-1.0, -1.0, 1.0])  # the same as a rotation


def part_bones(cfg, appearance):
    """{component name: bone name or None (skinned)} by following parentTransform to a vehicle_slots slot."""
    app = json.load(open(cfg['json'] / cfg['app'], encoding='utf8'))['Data']['RootChunk']
    ent = json.load(open(cfg['json'] / cfg['ent'], encoding='utf8'))['Data']['RootChunk']
    slots = {s['slotName']['$value']: s['boneName']['$value'] for c in ent['components']
             if c.get('name', {}).get('$value') == 'vehicle_slots' for s in c['slots']}
    a = next(x for x in app['appearances'] if x['Data']['name']['$value'] == appearance)
    H = cp_vehicle._handles(a, {})
    comps = {c.get('name', {}).get('$value', ''): c for c in a['Data']['components']}

    def bone(name, depth=0):
        c = comps.get(name)
        if c is None or depth > 8:
            return 'Root'
        pt = c.get('parentTransform') or {}
        b = H.get(pt.get('HandleRefId')) or pt.get('Data') or {}
        bind = b.get('bindName', {}).get('$value')
        if bind == 'deformation_rig':
            return None
        if bind == 'vehicle_slots':
            return slots.get(b.get('slotName', {}).get('$value'), 'Root')
        if bind in comps and bind != name:
            return bone(bind, depth + 1)
        return 'Root'
    return {n: bone(n) for n in comps}


def submeshes(glb, lod):
    """[(chunk index, pos CP, normal CP, uv, tris, joints names (n,4) or None, weights (n,4))] for one CP LOD (a part
    without that LOD uses its next finer one, as cp_car._submeshes)."""
    j, b, off = fastglb._load(str(glb))
    meshes = [m for m in j.get('meshes', []) if 'LOD_' in m.get('name', '')]
    have = sorted({int(m['name'].split('LOD_')[-1]) for m in meshes} - {0})
    want = max([x for x in have if x <= lod] or have[:1])
    skin_of = {n['mesh']: n.get('skin') for n in j.get('nodes', []) if 'mesh' in n}
    out = []
    for mi, m in enumerate(j['meshes']):
        if m not in meshes or int(m['name'].split('LOD_')[-1]) != want:
            continue
        ci = int(m['name'].split('_')[1])
        sk = skin_of.get(mi)
        jn = [j['nodes'][x]['name'] for x in j['skins'][sk]['joints']] if sk is not None else None
        P, N, T, I, J, W, base = [], [], [], [], [], [], 0
        for prim in m['primitives']:
            at = prim['attributes']
            p = fastglb._accessor(j, b, off, at['POSITION']).astype(np.float64)
            n = fastglb._accessor(j, b, off, at['NORMAL']).astype(np.float64) if 'NORMAL' in at else np.zeros_like(p)
            t = (fastglb._accessor(j, b, off, at['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in at
                 else np.zeros((len(p), 2)))
            i = (fastglb._accessor(j, b, off, prim['indices']).astype(np.int64) if 'indices' in prim
                 else np.arange(len(p)))
            P.append(fastglb.to_blender(p))
            N.append(fastglb.to_blender(n))
            T.append(t)
            I.append(i.reshape(-1, 3) + base)
            if jn is not None:
                J.append(fastglb._accessor(j, b, off, at['JOINTS_0']).astype(np.int64))
                W.append(fastglb._accessor(j, b, off, at['WEIGHTS_0']).astype(np.float64))
            base += len(p)
        joints = np.array(jn, object)[np.concatenate(J)] if jn is not None else None
        out.append((ci, np.concatenate(P), np.concatenate(N), np.concatenate(T), np.concatenate(I), joints,
                    np.concatenate(W) if jn is not None else None))
    return out


def skeleton(cfg):
    """GLA bones from the CP rig's reference pose: names (Root -> model_root), parents, GLM-space base matrices."""
    rig = json.load(open(cfg['json'] / cfg['rig'], encoding='utf8'))['Data']['RootChunk']
    names = [b['$value'] for b in rig['boneNames']]
    parents = list(rig['boneParentIndexes'])
    pose = cp_vehicle.rig_pose(rig)
    bones = []
    for i, n in enumerate(names):
        M = pose[n]
        R = R_GLM @ M[:3, :3] @ R_GLM.T
        sc = np.linalg.norm(R, axis=0)
        R = R / sc                               # the rig has unit scale; a non-unit one would not survive the pool
        t = to_glm(M[None, :3, 3])[0]
        base = np.c_[R, t]
        inv = np.c_[R.T, -R.T @ t]
        bones.append(dict(name='model_root' if i == 0 else n, flags=0, parent=parents[i], base=base, baseinv=inv,
                          children=[k for k, p in enumerate(parents) if p == i]))
    return names, bones, pose


def split(tris):
    """Chunks of one triangle soup under the per-surface limits: [(vertex index array, tris remapped)]."""
    out, start = [], 0
    while start < len(tris):
        used, chunk, k = {}, [], start
        while k < len(tris) and len(chunk) < MAX_TRIS:
            new = {v for v in tris[k] if v not in used}
            if len(used) + len(new) > MAX_VERTS:
                break
            for v in tris[k]:
                if v not in used:
                    used[v] = len(used)
            chunk.append([used[v] for v in tris[k]])
            k += 1
        idx = np.empty(len(used), np.int64)
        for v, i in used.items():
            idx[i] = v
        out.append((idx, np.array(chunk, np.int64)))
        start = k
    return out


def light_colours(cfg, appearance):
    """{'head', 'brake', 'utility': rgb 0..1} from the appearance's vehicleLightComponents (the neon rims are utility)."""
    app = json.load(open(cfg['json'] / cfg['app'], encoding='utf8'))['Data']['RootChunk']
    a = next(x for x in app['appearances'] if x['Data']['name']['$value'] == appearance)
    out = {}
    for c in a['Data']['components']:
        if c['$type'] == 'vehicleLightComponent' and c.get('lightType') in ('Head', 'Brake', 'Utility'):
            col = c.get('color') or {}
            out.setdefault(c['lightType'].lower(), np.array([col.get('Red', 255), col.get('Green', 255),
                                                             col.get('Blue', 255)], np.float64) / 255.0)
    return out


def build(src: Path, stage: Path):
    cfg = dict(CFG, json=src / 'json', raw_root=src / 'raw', glb_root=src / 'glb', lod_root=src / 'glb',
               mask_dir=src / 'glb' / DEPOT / 'entities' / 'meshes' / 'textures')
    cp_car.CARS['ct3x'] = cfg
    names, bones, pose = skeleton(cfg)
    bidx = {n: i for i, n in enumerate(names)}
    parts = cp_vehicle.parts(cfg['json'] / cfg['app'], cfg['json'] / cfg['ent'], cfg['json'] / cfg['rig'], APPEARANCE)
    owner = part_bones(cfg, APPEARANCE)
    paint = ct3x_materials.Paint(cfg['glb_root'], cfg['json'], stage, NS, light_colours(cfg, APPEARANCE))
    # pieces[lod] = [(shader, xyz GLM, normal GLM, uv, tris CCW, weights per vertex [(bone, w)...])]
    pieces = [[] for _ in CP_LODS]
    report = []
    for p in parts:
        if any(k in p['name'].lower() for k in cfg['skip_parts']):
            continue
        glb = cfg['glb_root'] / (p['mesh'].rsplit('.', 1)[0] + '.glb')
        if not glb.exists():
            report.append(('MISSING', p['name'], p['mesh']))
            continue
        chunks = cp_car._chunk_materials(cfg, p['mesh'], p['mesh_app'])
        own = owner.get(p['name'], 'Root')
        for li, lod in enumerate(CP_LODS):
            for k, pos, nrm, uv, idx, jn, jw in submeshes(glb, lod):
                chunk, m = chunks[k] if k < len(chunks) else ('', {})
                if own is None:             # skinned: bind pose is vehicle space already
                    w = pos
                    nw = nrm
                    unknown = {str(j) for j in jn.ravel()} - {str(j) for j in bidx}
                    if unknown:
                        print('warning: %s: joints not in the rig bind to bone 0: %s' % (p['name'], sorted(unknown)[:8]))
                    # every vertex keeps at least its heaviest joint (all weights under 1e-4 left none)
                    ws = [[(bidx.get(jn[v, q], 0), float(jw[v, q])) for q in range(4) if jw[v, q] > 1e-4]
                          or [(bidx.get(jn[v, int(np.argmax(jw[v]))], 0), 1.0)]
                          for v in range(len(pos))]
                else:
                    M = p['M']
                    w = (np.c_[pos, np.ones(len(pos))] @ M.T)[:, :3]
                    nw = nrm @ M[:3, :3].T
                    ws = [[(bidx[own], 1.0)]] * len(pos)
                sh = paint.chunk(chunk, m, float(w[:, 1].mean()))
                if sh is None:
                    continue
                nw = nw / np.maximum(np.linalg.norm(nw, axis=1, keepdims=True), 1e-9)
                pieces[li].append((sh, to_glm(w), nw @ R_GLM.T, uv, idx, ws))
                if li == 0:
                    report.append((p['name'], own or 'skinned', chunk, sh, len(idx)))

    # surfaces: one per (shader, chunk index) across LODs; a LOD without that chunk pads with one degenerate triangle
    groups = [{} for _ in CP_LODS]          # groups[lod][shader] = (X, N, U, T CCW, weights)
    lod_cells = {}
    for li in range(len(CP_LODS)):
        per = {}
        for sh, xyz, nrm, uv, tris, ws in pieces[li]:
            per.setdefault(sh, []).append((xyz, nrm, uv, tris, ws))
        for sh, lst in per.items():
            base, X, Nn, U, T, Wt = 0, [], [], [], [], []
            for xyz, nrm, uv, tris, ws in lst:
                X.append(xyz); Nn.append(nrm); U.append(uv); T.append(tris + base); Wt += ws; base += len(xyz)
            X, Nn, U, T = map(np.concatenate, (X, Nn, U, T))
            groups[li][sh] = (X, Nn, U, T, Wt)
    # traced LODs: one cell size per LOD for the whole model, grown until it fits the budget
    for li, budget in LOD_VERTS.items():
        cell, uvq = 0.75, 32
        while True:
            red = {sh: cluster(*g, cell, uvq) for sh, g in groups[li].items()}
            n = sum(len(np.unique(r[3])) for r in red.values() if len(r[3]))
            if n <= budget or cell > 64:
                break
            cell, uvq = cell * 1.4, max(4, int(uvq / 1.4))
        if n > budget:
            print('warning: LOD %d keeps %d vertices, over its budget of %d (clustering stops at a 64 u cell)'
                  % (li, n, budget))
        groups[li] = {sh: r for sh, r in red.items() if len(r[3])}
        lod_cells[li] = dict(cell_u=round(cell, 2), uv_cells=uvq, verts=n)
    lod_chunks = []
    for li in range(len(CP_LODS)):
        out = {}
        for sh, (X, Nn, U, T, Wt) in groups[li].items():
            for vi, ct in split(T):
                verts = [[tuple(X[v]), tuple(Nn[v]), tuple(U[v]), Wt[v]] for v in vi]
                out.setdefault(sh, []).append((verts, [tuple(int(a) for a in t[[0, 2, 1]]) for t in ct]))
        lod_chunks.append(out)
    seat = to_glm(pose['seat_front'][None, :3, 3])[0]
    order = []
    for sh in sorted({s for c in lod_chunks for s in c}):
        n = max(len(c.get(sh, [])) for c in lod_chunks)
        order += [(sh, i) for i in range(n)]
    surfaces = []
    for i, (sh, k) in enumerate(order):
        nm = f"{sh.rsplit('/', 1)[-1][:40]}_{k}" if i else 'body'
        surfaces.append(dict(name=nm, flags=0, shader=sh, parent=-1 if i == 0 else 0, children=[]))
    stock = glm_io.read(zipfile.ZipFile(ASSETS1).read('models/players/swoop/model.glm'))
    sw = {s['name']: i for i, s in enumerate(stock['surfaces'])}
    tag_src = stock['lods'][0][sw[SWOOP_TAG]]
    tag_c = np.array([v[0] for v in tag_src['verts']]).mean(0)
    tags = []
    for t, bone, off in TAGS:
        M = pose[bone]
        pos = to_glm((M[:3, :3] @ np.array(off) + M[:3, 3])[None])[0] + np.array(TAG_SHIFT_U.get(t, (0.0, 0.0, 0.0)))
        tags.append((t, bidx[bone], pos))
        surfaces.append(dict(name=t, flags=glm_io.ISBOLT, shader='', parent=0, children=[]))
    surfaces[0]['children'] = list(range(1, len(surfaces)))
    out_lods = []
    for c in lod_chunks:
        surfs = []
        for sh, k in order:
            lst = c.get(sh, [])
            if k < len(lst):
                verts, tris = lst[k]
            else:
                verts = [[tuple(seat), (0.0, 0.0, 1.0), (0.0, 0.0), [(bidx['Base'], 1.0)]] for _ in range(3)]
                tris = [(0, 1, 2)]
            surfs.append(dict(verts=verts, tris=tris))
        for t, bi, pos in tags:
            surfs.append(dict(verts=[[tuple(np.array(v[0]) - tag_c + pos), v[1], v[2], [(bi, 1.0)]]
                                     for v in tag_src['verts']], tris=tag_src['tris']))
        out_lods.append(surfs)

    mdir = stage / NS
    mdir.mkdir(parents=True, exist_ok=True)
    gla = f'{NS}/{NAME}'
    nb = len(bones)
    # pool entry 0 = identity (rest); entry 1 = the ride-height swing of suspension_front about its pivot. Its children
    # keep identity: a child's skin matrix is its parent's times its own (jkagla), so they follow the arm
    arm = bones[bidx['suspension_front']]['base']
    a = np.radians(RIDE_ARM_DEG)
    # GLM x is CP -x: a CP rotation lowering the wheel (negative about CP +x) is positive about GLM +x
    Rx = np.array([[1, 0, 0], [0, np.cos(a), -np.sin(a)], [0, np.sin(a), np.cos(a)]])
    piv = arm[:, 3]
    ride = np.c_[Rx, piv - Rx @ piv]
    pool = jkagla.encode(np.stack([np.c_[np.eye(3), np.zeros(3)], ride]))
    idx = np.zeros((2, nb), np.int64)
    idx[:, bidx['suspension_front']] = 1
    jkagla.write(mdir / f'{NAME}.gla', gla, 1.0, bones, idx, pool)
    (mdir / 'animation.cfg').write_text('// ct3x_model.py: rest pose only; wheels and fork are set from cgame\n'
                                        '//\n// Format:  enum, targetFrame, frameCount, loopFrame, frameSpeed\n//\n'
                                        'ROOT\t0\t2\t-1\t20\n', encoding='ascii')
    glm_io.write(mdir / 'model.glm', dict(name='model.glm', anim=gla, animIndex=0, numBones=nb,
                                          surfaces=surfaces, lods=out_lods))
    (mdir / 'model_default.skin').write_text(''.join(f"{s['name']},{s['shader']}\n" for s in surfaces if s['shader']),
                                             encoding='ascii')
    paint.write(stage / 'shaders', NAME,
                f'// {NAME}: CP2077 Yaiba Kusanagi CT-3X ({APPEARANCE}), tools/bike/ct3x_model.py -- private build: '
                'CP2077 art\n\n')
    allv = np.concatenate([np.array([v[0] for v in s['verts']]) for s in out_lods[0][:len(order)]])
    info = dict(appearance=APPEARANCE, units_per_m=UNITS, bones=[b['name'] for b in bones],
                surfaces=len(surfaces), verts_per_lod=[sum(len(s['verts']) for s in l) for l in out_lods],
                tris_per_lod=[sum(len(s['tris']) for s in l) for l in out_lods], lod_clustering=lod_cells,
                bounds=[allv.min(0).round(1).tolist(), allv.max(0).round(1).tolist()],
                tags={t: [round(float(x), 2) for x in pos] for t, _b, pos in tags},
                shaders=sorted({s['shader'] for s in surfaces if s['shader']}),
                parts=[list(map(str, r)) for r in report])
    (stage / f'{NAME}_info.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
    return info


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', type=Path, default=SRC)
    ap.add_argument('--stage', type=Path, default=SRC / 'stage')
    a = ap.parse_args()
    i = build(a.src, a.stage)
    print(json.dumps({k: i[k] for k in ('surfaces', 'verts_per_lod', 'tris_per_lod', 'bounds', 'tags')}))
