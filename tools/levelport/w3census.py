"""w3census.py -- what a Witcher 3 world holds inside a level port's box, against what a work folder has (Beauclair).

Plain Python, read straight from the game's bundles (w3bundle.py; no Blender, no uncook): every layer (.w2l) under a
world folder is opened and counted inside the box --
- its CSectorData objects (the cooked static content: type 1 mesh, 2 collision, 3 decal, 4 dimmer, 5 point light,
  6 spot light, 7 rigid body, 8 cloth, 10 particles), by type, at each object's world position;
- its entities (CEntity and subclasses with a position), and those made from a template (.w2ent).
Then the work folder's placements are counted per layer, and every layer with meshes in the box is reported as
dumped (placements >= 90% of its in-box sector meshes; trimming and the template entities make exact counts
meaningless), short, dropped by a --drop-layers rule (w3city's file), or never dumped.

    python tools/levelport/w3census.py --box X0 Y0 X1 Y1 --work <city work> [--drop-layers FILE]
        [--world dlc/bob/data/levels/bob/] [--city locations/city_of_beauclair/] [--json OUT] [--gate]

Layer names in the work folder are relative to --city (w3gltf's layer lists) or to --world (full depot paths of other
folders). --gate: exit 1 when a layer under --city that has in-box meshes is neither dumped nor dropped (the gate:
the city folder fully extracted); other folders are reported, not gated (a choice: hills, quests, community).
$W3_GAME is the game folder; the bundle index is cached at <work>/../bundle_index.pkl unless --index-cache.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3bundle  # noqa: E402
import w3emitter  # noqa: E402

B = chr(92)
TYPES = {1: 'mesh', 2: 'collision', 3: 'decal', 4: 'dimmer', 5: 'point_light', 6: 'spot_light', 7: 'rigid_body',
         8: 'cloth', 9: 'destruction', 10: 'particles'}


def vlq(b, p):
    """CR2W's variable-length count: 6 bits, then 7 a byte while the continue bit is set"""
    c = b[p]
    v, p, sh = c & 0x3F, p + 1, 6
    if c & 0x40:
        while True:
            c = b[p]
            p += 1
            v |= (c & 0x7F) << sh
            sh += 7
            if not c & 0x80:
                break
    return v, p


def sector_objects(f, d):
    """(type, x, y, z) of every CSectorData object: after the (empty) properties, a byte and a u32 size, the
    resources (32 B: a local box and a path hash) and the objects (24 B: type, flags, radius u16, block offset u64,
    position)"""
    out = []
    for cn, _par, off, sz in f.chunks:
        if cn != 'CSectorData':
            continue
        b = d[off:off + sz]
        nres, p = vlq(b, 8)
        p += 32 * nres
        nobj, p = vlq(b, p)
        for _ in range(nobj):
            t = b[p]
            x, y, z = struct.unpack_from('<3f', b, p + 12)
            out.append((t, x, y, z))
            p += 24
    return out


def entities(f, d):
    """(class, template path or None, position) of every entity chunk with a position in its transform"""
    out = []
    for cn, _par, off, sz in f.chunks:
        if not cn.endswith('Entity'):
            continue
        p, pos, tpl = off + 1, None, None
        while p + 2 <= off + sz:
            ni = struct.unpack_from('<H', d, p)[0]
            if ni == 0:
                break
            _ti, size = struct.unpack_from('<HI', d, p + 2)
            name = f.names[ni]
            if name == 'transform' and d[p + 8] & 1:
                pos = struct.unpack_from('<3f', d, p + 9)
            elif name == 'template':
                r = f.ref(struct.unpack_from('<i', d, p + 8)[0])
                tpl = r[1] if r and r[0] == 'import' else None
            p += 4 + size
        if pos:
            out.append((cn, tpl, pos))
    return out


def census(world, box, index_cache=None):
    """{layer path under world: {'sector': {type name: n}, 'entities': n, 'templated': n}} for layers with content
    in the box"""
    idx = w3bundle.index(cache=index_cache)
    world = world.replace('/', B).lower()
    inb = lambda x, y: box[0] <= x <= box[2] and box[1] <= y <= box[3]  # noqa: E731
    res, errors = {}, {}
    for n in sorted(k for k in idx if k.endswith('.w2l') and k.lower().startswith(world)):
        rel = n[len(world):].replace(B, '/')
        try:
            d = w3bundle.read(idx, n)
            f = w3emitter.CR2W(d)
            so, en = sector_objects(f, d), entities(f, d)
        except Exception as e:  # noqa: BLE001  (reported; a layer this reader cannot open)
            errors[rel] = str(e)[:120]
            continue
        types = collections.Counter(TYPES.get(t, str(t)) for t, x, y, _z in so if inb(x, y))
        ein = [tpl for _c, tpl, (x, y, _z) in en if inb(x, y)]
        if types or ein:
            res[rel] = {'sector': dict(types), 'entities': len(ein), 'templated': sum(1 for t in ein if t)}
    return res, errors


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--box', type=float, nargs=4, required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--drop-layers')
    ap.add_argument('--world', default='dlc/bob/data/levels/bob/')
    ap.add_argument('--city', default='locations/city_of_beauclair/')
    ap.add_argument('--json')
    ap.add_argument('--index-cache')
    ap.add_argument('--gate', action='store_true')
    a = ap.parse_args()
    cache = a.index_cache or os.path.join(os.path.dirname(os.path.abspath(a.work)), 'bundle_index.pkl')
    res, errors = census(a.world, a.box, cache)
    drops = []
    if a.drop_layers:
        drops = [re.compile(ln.strip(), re.I) for ln in open(a.drop_layers, encoding='utf-8')
                 if ln.strip() and not ln.lstrip().startswith('#')]
    got = collections.Counter()
    pl = json.load(open(os.path.join(a.work, 'placements.json'), encoding='utf-8'))
    world = a.world.lower()
    for p in pl['placements']:
        lay = (p.get('layer') or '').replace(B, '/').lower()
        if lay.startswith(world):
            lay = lay[len(world):]
        elif lay:
            lay = a.city.lower() + lay
        got[lay] += 1
    rows, groups = [], collections.defaultdict(lambda: collections.Counter())
    for rel, v in res.items():
        meshes = v['sector'].get('mesh', 0)
        if not meshes:
            continue
        n = got.get(rel.lower(), 0)
        short_rel = rel[len(a.city):] if rel.lower().startswith(a.city.lower()) else rel
        if any(r.search(short_rel) for r in drops):
            st = 'dropped'
        elif n >= 0.9 * meshes:
            st = 'dumped'
        elif n:
            st = 'short'
        else:
            st = 'never'
        folder = '/'.join(rel.split('/')[:2])
        g = groups[(folder, st)]
        g['layers'] += 1
        g['meshes'] += meshes
        g['placements'] += n
        rows.append({'layer': rel, 'status': st, 'meshes_in_box': meshes, 'placements': n,
                     'templated_entities': v['templated']})
    print('w3census: %d layers with content in the box, %d with meshes; %d unreadable' % (
        len(res), len(rows), len(errors)))
    print('  %-44s %-8s %6s %8s %10s' % ('folder', 'status', 'layers', 'meshes', 'placements'))
    for (folder, st), g in sorted(groups.items(), key=lambda t: (t[0][1] != 'never', -t[1]['meshes'])):
        print('  %-44s %-8s %6d %8d %10d' % (folder, st, g['layers'], g['meshes'], g['placements']))
    city = a.city.lower()
    missing = [r for r in rows if r['layer'].lower().startswith(city) and r['status'] in ('never', 'short')]
    for r in sorted(missing, key=lambda r: -r['meshes_in_box'])[:30]:
        print('  CITY %-6s %5d meshes, %5d placements  %s' % (r['status'], r['meshes_in_box'], r['placements'],
                                                              r['layer']))
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as fp:
            json.dump({'box': a.box, 'layers': rows, 'errors': errors}, fp, indent=0)
    print('W3CENSUS %s' % json.dumps({'city_missing': len(missing), 'city_missing_meshes': sum(
        r['meshes_in_box'] for r in missing), 'errors': len(errors)}))
    return 1 if a.gate and missing else 0


if __name__ == '__main__':
    sys.exit(main())
