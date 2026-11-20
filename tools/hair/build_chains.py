"""build_chains.py -- a hair-chains sidecar (HCH1 v2, docs/hair/FORMAT.md) for any Ghoul2 model, from a rig description,
named presets and colliders fitted to the model's own mesh. No source-game dynamics data is needed.

    python tools/hair/build_chains.py rig.json --out model.chains [--presets tools/hair/presets.json]
        [--glm model.glm --gla skeleton.gla] [--render render.npz] [--report report.json]

rig.json (the model adapter writes it; positions in the asset's bind space, i.e. the GLM's for a game sidecar):
  units_per_cm      asset units per centimetre (GLM units: JKA units per metre / 100)
  up                world up in the asset's bind space (gravity is -up x 980.665 cm/s^2 x units_per_cm)
  host_bone         skeleton bone the hair hangs from (bound by name in game), e.g. "cranium"
  bones             [{name, parent (hair bone name or null = the host), head [x, y, z]}], parents before children
  chains            [{name, bones [root .. tip], preset, group, overrides {}}]; bones[0] is the chain's kinematic root,
                    rigid with the host; every later bone carries a particle on its head
  links             [[chainA, chainB], ...]: lateral links between same-index particles of neighbouring chains
  colliders         [{name, bone, p0 [x,y,z], r0, (bone1, p1, r1 for a capsule), containment}] explicit, and/or
  collider_specs    [{name, bone, shape "sphere"|"capsule", to (capsule end: that bone's joint), pct, pad_cm,
                      surfaces_exclude [prefix, ...], max_cm, particle_depth_cm, end_on_to (default true: the capsule's end rides
                      bone `to`)}] fitted from --glm/--gla (see fit_colliders)

Presets (presets.json) are physical and scale-free: lengths in cm, rates per second, angles in degrees, stiffnesses as
the solver's per-iteration fractions. A value given as [root, tip] is interpolated along the chain. The file's "solver"
block (dt, substeps, iterations) belongs with the presets: the fractions were fitted at that rate.

render.npz (optional, written by the model adapter): which GLM vertices ride which hair bones --
  surfaces [names], lods [int], counts [int] (vertex count, a binding check), ent_surf, ent_vert [int],
  ent_bone [N,4] (index into rig bones, -1 = the vertex's own GLM skinning), ent_w [N,4].
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import chainsio  # noqa: E402

G_CM = 980.665
PARTICLE_KEYS = ('radius_cm', 'gravity_scale', 'damping', 'velocity_damping', 'constraint_damping', 'friction',
                 'bounciness', 'park_rate')
CONE_KEYS = ('limit_deg', 'inner', 'outer', 'stretch', 'compress', 'parent_strength')


def load_presets(path):
    p = json.load(open(path, encoding='utf-8'))
    return p['solver'], p['presets']


def resolve(presets, name, overrides=None):
    """preset by name, following "base" inheritance, with per-chain overrides on top"""
    chain = []
    while name:
        if name not in presets:
            raise SystemExit('unknown preset ' + name)
        chain.append(presets[name])
        name = presets[name].get('base')
    out = {}
    for d in reversed(chain):
        out.update({k: v for k, v in d.items() if k not in ('base', 'description', 'source')})
    out.update(overrides or {})
    return out


def along(v, t):
    """a preset value at chain fraction t (0 root .. 1 tip): scalar, or [root, tip] interpolated"""
    if isinstance(v, (list, tuple)):
        return float(v[0] + (v[1] - v[0]) * t)
    return float(v)


def translation(head):
    m = np.eye(4)
    m[:3, 3] = head
    return chainsio.colmajor(m)


# ---------------------------------------------------------------------------------------------- colliders from a GLM
def glm_vertices(glm, gla, exclude=()):
    """LOD 0 vertices of every non-tag surface not excluded by prefix: positions and dominant GLA bone name"""
    names = [b['name'] for b in gla['bones']]
    P, B = [], []
    for sf, d in zip(glm['surfaces'], glm['lods'][0]):
        if sf['name'].startswith('*') or any(sf['name'].startswith(x) for x in exclude) or not d['verts']:
            continue
        for v in d['verts']:
            P.append(v['co'])
            B.append(names[d['brefs'][v['b'][int(np.argmax(v['w']))]]])
    return np.array(P), np.array(B)


def joint(gla, name):
    for b in gla['bones']:
        if b['name'] == name:
            return np.asarray(b['base'])[:3, 3].copy()
    raise SystemExit('no bone ' + name + ' in the GLA')


def seg_dist(P, a, b):
    ab = b - a
    t = np.clip(((P - a) @ ab) / max(ab @ ab, 1e-12), 0, 1)
    return np.linalg.norm(P - (a + t[:, None] * ab), axis=1)


def fit_colliders(specs, glm, gla, units_per_cm, roots, parts, parts_r):
    """Each spec fits one sphere or capsule to the skin its bone dominates: a sphere is centred on those vertices'
    centroid, a capsule runs from the bone's joint to the joint of bone `to`; the radius is the pct quantile of the
    vertices' distances plus pad_cm (at most max_cm). It is then shrunk so that no chain root lies inside (a collider
    that encloses a kinematic root flings its chain), and, with particle_depth_cm, so that no particle's bind position
    lies deeper inside than that. Hair resting on a skull sits partly inside any sphere fitted to it; the collider
    then holds it off the skin, so particles are only reported by default (inside, deepest_cm)."""
    out, info = [], {}
    for s in specs:
        P, B = glm_vertices(glm, gla, s.get('surfaces_exclude', ()))
        sel = P[B == s['bone']]
        if len(sel) < 8:
            raise SystemExit('collider %s: only %d vertices on %s' % (s['name'], len(sel), s['bone']))
        if s['shape'] == 'sphere':
            c = sel.mean(0)
            d = np.linalg.norm(sel - c, axis=1)
            p0 = p1 = c
        else:
            p0, p1 = joint(gla, s['bone']), joint(gla, s['to'])
            d = seg_dist(sel, p0, p1)
        r = float(np.quantile(d, s.get('pct', 0.75))) + s.get('pad_cm', 0.0) * units_per_cm
        if 'max_cm' in s:
            r = min(r, s['max_cm'] * units_per_cm)
        r_fit = r
        root_clear = float(seg_dist(roots, p0, p1).min())
        r = min(r, root_clear)
        part_d = seg_dist(parts, p0, p1) + parts_r      # a particle touches when its distance < r + its radius
        if 'particle_depth_cm' in s:
            r = min(r, float((part_d + s['particle_depth_cm'] * units_per_cm).min()))
        depth = r - part_d
        info[s['name']] = dict(bone=s['bone'], verts=int(len(sel)), radius_fit_cm=r_fit / units_per_cm,
                               radius_cm=r / units_per_cm, shrunk=bool(r < r_fit),
                               nearest_root_cm=root_clear / units_per_cm,
                               particles_inside=int((depth > 0).sum()),
                               deepest_cm=float(max(depth.max(), 0.0) / units_per_cm))
        e = dict(name=s['name'], bone=s['bone'], p0=p0.tolist(), r0=r)
        if s['shape'] == 'capsule':
            e.update(bone1=s['to'] if s.get('end_on_to', True) else s['bone'], p1=p1.tolist(), r1=r)
        out.append(e)
    return out, info


# ---------------------------------------------------------------------------------------------- the asset
def build(rig, solver, presets, colliders, gla_names=None):
    upc = float(rig['units_per_cm'])
    bones = rig['bones']
    bidx = {b['name']: i for i, b in enumerate(bones)}
    for i, b in enumerate(bones):
        if b.get('parent') is not None and bidx[b['parent']] >= i:
            raise SystemExit('bone %s listed before its parent' % b['name'])
    head = np.array([b['head'] for b in bones], float)

    particles, cones, receivers, pidx, groups = [], [], [], {}, []
    chain_of = {}
    for ch in rig['chains']:
        pr = resolve(presets, ch['preset'], ch.get('overrides'))
        g = ch.get('group', ch['name'])
        if g not in groups:
            groups.append(g)
        names = ch['bones']
        n = len(names) - 1
        if n < 1:
            raise SystemExit('chain %s needs a root and at least one particle bone' % ch['name'])
        for k in range(1, len(names)):
            t = (k - 1) / max(n - 1, 1)
            pi = len(particles)
            pidx[names[k]] = pi
            chain_of[pi] = (ch['name'], k)
            particles.append(dict(bone=bidx[names[k]], group=groups.index(g),
                                  radius=along(pr['radius_cm'], t) * upc,
                                  gravity_scale=along(pr['gravity_scale'], t), damping=along(pr['damping'], t),
                                  velocity_damping=along(pr['velocity_damping'], t),
                                  constraint_damping=along(pr['constraint_damping'], t),
                                  friction=along(pr['friction'], t), bounciness=along(pr['bounciness'], t),
                                  park_rate=along(pr.get('park_rate', 0.0), t), fictitious=0.0, centrifugal=0.0))
            root = k == 1
            cones.append(dict(child=pi, parent=-1 if root else pidx[names[k - 1]],
                              anchor_bone=bidx[names[0]] if root else -1,
                              limit=np.radians(along(pr['limit_deg'], t)), inner=along(pr['inner'], t),
                              outer=along(pr['outer'], t), stretch=along(pr['stretch'], t),
                              compress=along(pr['compress'], t), parent_strength=along(pr['parent_strength'], t),
                              rest=float(np.linalg.norm(head[bidx[names[k]]] - head[bidx[names[k - 1]]]))))
            r = particles[-1]['radius']
            if root:
                receivers.append(dict(particles=[pi], radii=[r]))
            else:
                receivers.append(dict(particles=[pidx[names[k - 1]], pi], radii=[particles[pidx[names[k - 1]]]['radius'], r]))
    links = []
    chains = {c['name']: c for c in rig['chains']}
    for a_, b_ in rig.get('links', []):
        A, B = chains[a_]['bones'], chains[b_]['bones']
        pa, pb = resolve(presets, chains[a_]['preset'], chains[a_].get('overrides')), resolve(presets, chains[b_]['preset'], chains[b_].get('overrides'))
        for k in range(1, min(len(A), len(B))):
            st = 0.5 * (along(pa.get('link_stretch', 0.0), (k - 1) / max(len(A) - 2, 1)) + along(pb.get('link_stretch', 0.0), (k - 1) / max(len(B) - 2, 1)))
            cp = 0.5 * (along(pa.get('link_compress', 0.0), (k - 1) / max(len(A) - 2, 1)) + along(pb.get('link_compress', 0.0), (k - 1) / max(len(B) - 2, 1)))
            if st <= 0 and cp <= 0:
                continue
            links.append(dict(a=pidx[A[k]], b=pidx[B[k]], stretch=st, compress=cp,
                              rest=float(np.linalg.norm(head[bidx[A[k]]] - head[bidx[B[k]]]))))

    game_bones = sorted({c['bone'] for c in colliders} | {c.get('bone1', c['bone']) for c in colliders})
    repulsors = []
    for c in colliders:
        ents = [dict(game_bone=game_bones.index(c['bone']), bind=list(c['p0']), radius=float(c['r0']))]
        if 'p1' in c:
            ents.append(dict(game_bone=game_bones.index(c.get('bone1', c['bone'])), bind=list(c['p1']), radius=float(c['r1'])))
        repulsors.append(dict(entries=ents, flags=chainsio.REP_INVERSE if c.get('containment') else 0))
    pairs = [(ri, pj) for ri in range(len(receivers)) for pj in range(len(repulsors))]

    up = np.asarray(rig.get('up', [0, 0, 1]), float)
    up /= np.linalg.norm(up)
    host = rig['host_bone']
    asset = dict(
        particles=particles, cones=cones, links=links, receivers=receivers, repulsors=repulsors, pairs=pairs,
        bones=[dict(name=b['name'], parent=-1 if b.get('parent') is None else bidx[b['parent']],
                    bind=translation(head[i])) for i, b in enumerate(bones)],
        mirrors=[], host_bone=gla_names.index(host) if gla_names and host in gla_names else -1, host_bone_name=host,
        substeps=int(solver['substeps']), lateral_iterations=int(solver['lateral_iterations']),
        collision_iterations=int(solver['collision_iterations']), dt=float(solver['dt']),
        gravity=(-up * G_CM * upc).tolist(), units_per_cm=upc, flags=0, game_bones=game_bones)
    return asset, dict(groups=groups, particle_of_bone=pidx, chain_of_particle=chain_of)


def add_render(asset, path):
    r = np.load(path, allow_pickle=False)
    asset['render_surfaces'] = [dict(name=str(n), count=int(c), lod=int(l)) for n, c, l in zip(r['surfaces'], r['counts'], r['lods'])]
    asset['render_entries'] = (r['ent_surf'].tolist(), r['ent_vert'].tolist(), r['ent_bone'].tolist(), r['ent_w'].tolist())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('rig')
    ap.add_argument('--out', required=True)
    ap.add_argument('--presets', default=os.path.join(HERE, 'presets.json'))
    ap.add_argument('--glm')
    ap.add_argument('--gla')
    ap.add_argument('--render')
    ap.add_argument('--report')
    a = ap.parse_args(argv)
    rig = json.load(open(a.rig, encoding='utf-8'))
    solver, presets = load_presets(a.presets)
    colliders, info, gla_names = list(rig.get('colliders', [])), {}, None
    if a.gla:
        sys.path.insert(0, os.path.join(HERE, '..', 'cloth', 'pipeline', 'model'))
        import jka
        gla = jka.read_gla(a.gla)
        gla_names = [b['name'] for b in gla['bones']]
    if rig.get('collider_specs'):
        if not (a.glm and a.gla):
            raise SystemExit('collider_specs need --glm and --gla')
        glm = jka.read_glm(a.glm)
        asset0, m0 = build(rig, solver, presets, [], gla_names)
        upc = float(rig['units_per_cm'])
        head = np.array([b['head'] for b in rig['bones']], float)
        names = [b['name'] for b in rig['bones']]
        roots = np.array([head[names.index(c['bones'][0])] for c in rig['chains']])
        parts = np.array([head[p['bone']] for p in asset0['particles']])
        parts_r = np.array([p['radius'] for p in asset0['particles']])
        fitted, info = fit_colliders(rig['collider_specs'], glm, gla, upc, roots, parts, parts_r)
        colliders += fitted
    asset, meta = build(rig, solver, presets, colliders, gla_names)
    if a.render:
        add_render(asset, a.render)
    n = chainsio.write_asset(a.out, asset)
    chk = chainsio.read_asset(a.out)
    rep = dict(bytes=n, counts={k: len(chk[k]) for k in ('particles', 'cones', 'links', 'receivers', 'repulsors', 'pairs',
                                                         'bones', 'render_surfaces', 'render_entries', 'game_bones')},
               groups=meta['groups'], colliders=info, collider_list=colliders,
               presets={c['name']: c['preset'] for c in rig['chains']})
    if a.report:
        json.dump(rep, open(a.report, 'w', encoding='utf-8'), indent=1)
    print(json.dumps(dict(out=a.out, **{k: rep[k] for k in ('bytes', 'counts', 'colliders')}), indent=1))
    return asset, meta, colliders


if __name__ == '__main__':
    main()
