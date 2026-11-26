# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""w3entfill.py -- the parts of templated entities the Blender import left out (Beauclair work folders).

    python -I w3entfill.py --work DIR --templates TEMPLATES.json [--dry-run]

The add-on (w3gltf.py's import) builds the first instance of an entity template in a layer whole and later instances
of the same template with one component: of the 32 gen_brazier_c_3lights braziers in road_to_castle_always_on.w2l the
first has its bowl, fuel and three lights, the other 31 their fuel alone (the template, read from the game's file:
bowl, fuel, two spot and a point light, and the layer instances carry no components of their own). The same for the
city lamp posts (gen_city_lantern_c_4lights_a: the column without its coal, lamps and wall base) and the hanging
braziers: floating columns and firewood in play.

TEMPLATES.json: {entity depot path: {'meshes': {mesh: n}, 'lights': {type: n}}}, each template's components as its
game file stores them (entity chunk, streaming buffer and includes followed).

Per entity template (its parts: meshes, and lights by their witcher_entity_path), the template instance is a whole
one: a group of parts within --link metres whose mesh and light counts are the template file's, the most common part
set among those (the largest group is not it: neighbouring candleholders on one table merge into one group). The
other placed parts of the template are peeled into instances one at a time, mesh parts first (some instances came in
as their lights alone; a light anchors only when another part agrees within --tol): a part's placement in the template,
T = M(part) M(template part)^-1, the one that maps the most other parts onto template parts of the same kind within
--tol (lights within --light-tol: a layer instance's lights carry their own values and the spot's height; light
matrices carry the entity's rotation); the template parts nothing maps onto are placed at T M(template part) with the template part's record (mesh,
slots, light values). Templates with no whole instance placed are left as imported.

Effects: the export's effect roots (CFXPreview, an effect played at an entity's slot: the braziers' and candles' fire),
also built for the first instance alone, belong to the entity of the nearest part within --fx-reach and are filled like
its lights (their witcher_entity_path set, for w3fx.py's night-only fires).

Writes placements.json in place; the import's file is kept as placements.pre-entfill.json (w3work's convention) and a
rerun starts from it.
Reads and writes only the work folder; converted levels are for local play only.
"""
import argparse
import collections
import json
import os
import shutil

import numpy as np

BS = chr(92)


def key_of(rec, light):
    if light:
        # by component class alone: a layer instance's lights carry their own values (energy, the spot's height)
        return ('l', (rec.get('props') or {}).get('witcher_light_red_type'))
    return ('m', rec['mesh'], rec.get('component'))


def entity_of(rec, light):
    if light:
        e = (rec.get('props') or {}).get('witcher_entity_path')
    else:
        e = rec.get('entity')
    return str(e).lower() if e else None


def clusters(parts, link):
    """Single-linkage groups of parts (index lists) by their positions."""
    n = len(parts)
    pos = np.array([p['M'][:3, 3] for p in parts])
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(n):
        d = np.linalg.norm(pos[i + 1:] - pos[i], axis=1)
        for j in np.nonzero(d < link)[0]:
            a, b = find(i), find(i + 1 + int(j))
            if a != b:
                parent[a] = b
    g = collections.defaultdict(list)
    for i in range(n):
        g[find(i)].append(i)
    return list(g.values())


def nearest(T, q, pool, used, tol):
    """(The unused pool part of q's key nearest T M(q) within tol m, or None; its distance)."""
    want = (T @ q['M'])[:3, 3]
    best, bd = None, tol
    for p in pool:
        if id(p) in used or p['key'] != q['key']:
            continue
        d = float(np.linalg.norm(p['M'][:3, 3] - want))
        if d < bd:
            best, bd = p, d
    return best, bd


def peel(anchor, pool, proto, tol, light_tol):
    """The instance anchor belongs to: (T, {id(template part): pool part}, parts within tol besides the anchor) for
    the anchor's placement in the template that maps the most pool parts onto template parts (meshes within tol,
    lights within light_tol)."""
    best = (None, {}, 0)
    for q in proto:
        if q['key'] != anchor['key']:
            continue
        T = anchor['M'] @ np.linalg.inv(q['M'])
        got, used, tight = {id(q): anchor}, {id(anchor)}, 0
        for q2 in proto:
            if q2 is q:
                continue
            p, d = nearest(T, q2, pool, used, light_tol if (q2['light'] or q2['fx']) else tol)
            if p is not None:
                got[id(q2)] = p
                used.add(id(p))
                tight += d < tol
        if (len(got), tight) > (len(best[1]), best[2]):
            best = (T, got, tight)
    return best


def shape(P):
    """(meshes, lights): the template file's counts (its effects are not components)"""
    return (sum(1 for p in P if not p['light'] and not p['fx']), sum(1 for p in P if p['light']))


def is_root(r):
    """An effect's root in the export (CFXPreview: one per effect played on an entity, at its slot)"""
    p = r.get('props') or {}
    return p.get('witcher_type') == 'CFXPreview' and bool(p.get('witcher_particle_system'))


def attach_fx(pl, parts, reach):
    """Each effect root to the entity of the nearest placed part (mesh or light) in its layer within reach m: its
    props.witcher_entity_path set; {entity: [root records]}"""
    by_layer = collections.defaultdict(list)
    for e, P in parts.items():
        for q in P:
            by_layer[q['layer']].append((q['M'][:3, 3], e))
    pos = {k: (np.array([x[0] for x in v]), [x[1] for x in v]) for k, v in by_layer.items()}
    out = collections.defaultdict(list)
    for r in pl.get('fx') or []:
        if not is_root(r) or r['layer'] not in pos:
            continue
        P, E = pos[r['layer']]
        d = np.linalg.norm(P - np.array(r['matrix'], dtype=np.float64)[:3, 3], axis=1)
        i = int(np.argmin(d))
        if d[i] <= reach:
            r['props'] = dict(r['props'], witcher_entity_path=E[i])
            out[E[i]].append(r)
    return out


def keyset(P):
    return tuple(sorted(str(p['key']) for p in P))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--templates', required=True, help='the templates\' components from the game files (JSON)')
    ap.add_argument('--link', type=float, default=3.0,
                    help='parts within this many metres may be one instance (finding the template instance only)')
    ap.add_argument('--tol', type=float, default=0.02, help='a mesh part maps onto the template part within this (m)')
    ap.add_argument('--light-tol', type=float, default=0.3,
                    help='a light maps onto the template light within this (m): instances place their own')
    ap.add_argument('--fx-reach', type=float, default=1.0,
                    help="an effect's root belongs to the entity of the nearest part within this (m)")
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    path = os.path.join(a.work, 'placements.json')
    pre = os.path.join(a.work, 'placements.pre-entfill.json')
    # (a rerun starts again from the import: the previous fill is replaced, not added to)
    pl = json.load(open(pre if os.path.exists(pre) else path, encoding='utf-8'))
    tmpl = {k.lower().replace('/', BS): v for k, v in json.load(open(a.templates, encoding='utf-8')).items()}
    parts = collections.defaultdict(list)       # entity -> parts
    for light, recs in ((False, pl['placements']), (True, pl['lights'])):
        for r in recs:
            e = entity_of(r, light)
            if not e or e.endswith('.w2l'):
                continue
            parts[e].append({'rec': r, 'light': light, 'fx': False, 'key': key_of(r, light),
                             'M': np.array(r['matrix'], dtype=np.float64), 'layer': r.get('layer')})
    roots = attach_fx(pl, parts, a.fx_reach)
    for e, R in roots.items():
        for r in R:
            q = r['props']
            parts[e].append({'rec': r, 'light': False, 'fx': True,
                             'key': ('f', q['witcher_particle_system'].lower(), q.get('witcher_effect_slot')),
                             'M': np.array(r['matrix'], dtype=np.float64), 'layer': r.get('layer')})

    stats = collections.Counter()
    stats['fx_roots_attached'] = sum(len(v) for v in roots.values())
    added_m, added_l, added_f = [], [], []
    per_entity = []
    for e, P in parts.items():
        t = tmpl.get(e.replace('/', BS))
        if not t or 'error' in t:
            stats['entity_no_template'] += 1
            continue
        want = (sum(t['meshes'].values()), sum(t['lights'].values()))
        by_layer = collections.defaultdict(list)
        for p in P:
            by_layer[p['layer']].append(p)
        whole = []
        for LP in by_layer.values():
            for idx in clusters(LP, a.link):
                g = [LP[i] for i in idx]
                if shape(g) == want:
                    whole.append(g)
        if not whole:
            stats['entity_no_whole_instance'] += 1
            continue
        # (the first instance in a layer has its effects too: those, if any)
        lit = [g for g in whole if any(q['fx'] for q in g)] or whole
        top = collections.Counter(keyset(g) for g in lit).most_common(1)[0][0]
        proto = next(g for g in lit if keyset(g) == top)
        if not any(not q['light'] and not q['fx'] for q in proto):
            stats['entity_lights_only'] += 1
            continue
        stats['entities'] += 1
        protoset = {id(q) for q in proto}
        filled = 0
        for lay, LP in by_layer.items():
            pool = [p for p in LP if id(p) not in protoset]
            while pool:
                meshes = [p for p in pool if not p['light'] and not p['fx']]
                lights = [p for p in pool if p['light']]
                if not meshes and not lights:
                    stats['fx_left'] += len(pool)
                    break
                anchor = meshes[0] if meshes else lights[0]
                T, got, tight = peel(anchor, pool, proto, a.tol, a.light_tol)
                if T is None or (anchor['light'] and tight < 1):
                    stats['light_alone' if anchor['light'] else 'parts_not_in_template'] += 1
                    pool = [p for p in pool if p is not anchor]
                    continue
                taken = {id(p) for p in got.values()}
                pool = [p for p in pool if id(p) not in taken]
                if len(got) == len(proto):
                    stats['complete'] += 1
                    continue
                for q in proto:
                    if id(q) in got:
                        continue
                    r = dict(q['rec'])
                    r['matrix'] = [[float(v) for v in row] for row in (T @ q['M'])]
                    r['layer'] = lay
                    r['name'] = str(r.get('name', '')) + '#entfill'
                    (added_f if q['fx'] else added_l if q['light'] else added_m).append(r)
                    filled += 1
                stats['instances_filled'] += 1
        if filled:
            per_entity.append((filled, e.split(BS)[-1], len(P), len(proto)))
    for n, e, npl, npt in sorted(per_entity, reverse=True)[:40]:
        print('W3ENTFILL %5d parts  %-60s placed parts %d, template %d parts' % (n, e, npl, npt))
    print('W3ENTFILL meshes +%d lights +%d effects +%d %s' % (len(added_m), len(added_l), len(added_f), dict(stats)))
    if a.dry_run:
        return
    if not os.path.exists(pre):
        shutil.copyfile(path, pre)
    pl['placements'].extend(added_m)
    pl['lights'].extend(added_l)
    pl.setdefault('fx', []).extend(added_f)
    pl['entfill'] = {'added': {'meshes': len(added_m), 'lights': len(added_l), 'fx': len(added_f), 'stats': dict(stats)}}
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(pl, f)
    print('W3ENTFILL wrote', path)


if __name__ == '__main__':
    main()
