"""tag_verdict.py -- a numbered tag's checks with the expected results written down before the run, and one verdict
that fails on any miss (the checks once printed whatever they found and passed). build_watson's parts
and checks stages call it; each check also runs alone:

  python -I tag_verdict.py parts <tiles A> <tiles B> <out.json>        collision parts array by array (the early gate)
  python -I tag_verdict.py chunks <collision A> <collision B> <out.json>  chunk BSPs: byte-equal, else brush-equal;
                                                                        the residency manifest's lines
  python -I tag_verdict.py hub <hub pk3 A> <hub pk3 B> <out.json>       the hub's BSP lump by lump, other members
  python -I tag_verdict.py packs <kit A> <kit B> <out.json> NAME...     packs' members by CRC; map files by bytes
  python -I tag_verdict.py engine <dir A> <dir B> <out.json>            levelcheck outputs under the tie rule
  python -I tag_verdict.py scene <kit> <materials.json> <out.json>      the materials table's counts (terrain, stand-in)
  python -I tag_verdict.py materials <parity.json> <materials B.json> <expect.json> <out.json>
                                                                        material keys removed and added against the
                                                                        expectations' "materials" rules
  python -I tag_verdict.py judge <expect.json> <results dir>            every result against its expectation; exit 1
                                                                        on any miss, the misses listed
  (--map NAME anywhere: the map the kits serve, default cp_watson)

The tie rule (docs/levelport-watson.md section 14): flag differences on exact ties and touching
starts do not count (any brush-order change makes them); a start buried in solid counts as touching; endpoints
(1/64 u), fractions (1e-6) and contents stay strict."""
import collections
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
MAP = 'cp_watson'           # the map the kits serve (--map NAME, anywhere on the command line)


def write(out, rep):
    Path(out).write_text(json.dumps(rep, indent=1, default=str), encoding='utf-8')
    print(json.dumps({k: (v if not isinstance(v, (list, dict)) or len(v) < 12 else len(v)) for k, v in rep.items()},
                     default=str))
    return rep


# ---- collision parts, array by array ----

def parts(ta, tb, out):
    """every tile's build/<tile>.npz of tiles B against tiles A: arrays equal (dtype, shape, values; object arrays
    element by element); 'stats' (counts and notes) reported apart"""
    ta, tb = Path(ta), Path(tb)
    rep = {'a': str(ta), 'b': str(tb), 'tiles': 0, 'equal': 0, 'differing': {}, 'only_a': [], 'only_b': [],
           'stats_differing': []}
    na = {p.stem for p in (ta / 'build').glob('*.npz')}
    nb = {p.stem for p in (tb / 'build').glob('*.npz')}
    rep['only_a'], rep['only_b'] = sorted(na - nb), sorted(nb - na)
    for t in sorted(na & nb):
        rep['tiles'] += 1
        with np.load(ta / 'build' / (t + '.npz'), allow_pickle=True) as x, \
                np.load(tb / 'build' / (t + '.npz'), allow_pickle=True) as y:
            bad = [k for k in sorted(set(x.files) | set(y.files)) if k != 'stats' and not same(x, y, k)]
            if 'stats' in x.files and 'stats' in y.files and str(x['stats']) != str(y['stats']):
                rep['stats_differing'].append(t)
        if bad:
            rep['differing'][t] = bad
        else:
            rep['equal'] += 1
    return write(out, rep)


def same(x, y, k):
    if k not in x.files or k not in y.files:
        return False
    u, v = x[k], y[k]
    if u.dtype != v.dtype or u.shape != v.shape:
        return False
    if u.dtype == object:
        return all(np.array_equal(np.asarray(p), np.asarray(q)) if not isinstance(p, str) else p == q
                   for p, q in zip(u.ravel(), v.ravel()))
    return np.array_equal(u, v)


# ---- chunk BSPs ----

def mix(x):
    """splitmix64's finaliser, elementwise on uint64 (wrapping)"""
    x = (x ^ (x >> np.uint64(30))) * np.uint64(0xBF58476D1CE4E5B9)
    x = (x ^ (x >> np.uint64(27))) * np.uint64(0x94D049BB133111EB)
    return x ^ (x >> np.uint64(31))


def brushes(data):
    """a chunk BSP as collision: one 64-bit hash a brush (its shader record, its sides' planes exactly, each side's
    shader record, in side order), sorted: two chunks collide alike when the arrays are equal, whatever their trees"""
    import hashlib
    import colbsp
    b = colbsp.Rbsp(colbsp.plain(data))
    br, sd, pl = (b.lump(colbsp.L_BRUSHES, colbsp.BRUSH), b.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE),
                  b.lump(colbsp.L_PLANES, colbsp.PLANE))
    sh = b.lump(colbsp.L_SHADERS, colbsp.SHADER)
    shash = np.array([int.from_bytes(hashlib.sha1(r.tobytes()).digest()[:8], 'little') for r in sh], np.uint64)
    with np.errstate(over='ignore'):
        pb = np.ascontiguousarray(pl[sd['plane']]).view(np.uint64).reshape(-1, 2)      # (normal, dist) as 2 words
        n = br['num_sides'].astype(np.int64)
        first = br['first_side'].astype(np.int64)
        idx = np.repeat(first, n) + (np.arange(int(n.sum())) - np.repeat(np.cumsum(n) - n, n))
        pos = np.arange(int(n.sum())) - np.repeat(np.cumsum(n) - n, n)
        v = mix(pb[idx, 0] ^ mix(pb[idx, 1] + np.uint64(0x9E3779B97F4A7C15))
                ^ mix(shash[sd['shader'][idx]] + pos.astype(np.uint64)))
        starts = np.concatenate([[0], np.cumsum(n)[:-1]])
        acc = np.zeros(len(br), np.uint64)
        nz = n > 0
        if nz.any():
            acc[nz] = np.add.reduceat(v, starts[nz])
        h = mix(acc ^ mix(shash[br['shader']] ^ n.astype(np.uint64)))
    return np.sort(h)


def multiset_diff(x, y):
    """(entries only in x, only in y) of two sorted hash arrays, with multiplicity"""
    ux, cx = np.unique(x, return_counts=True)
    uy, cy = np.unique(y, return_counts=True)
    allk = np.union1d(ux, uy)
    ax = np.zeros(len(allk), np.int64)
    ay = np.zeros(len(allk), np.int64)
    ax[np.searchsorted(allk, ux)] = cx
    ay[np.searchsorted(allk, uy)] = cy
    return int(np.clip(ax - ay, 0, None).sum()), int(np.clip(ay - ax, 0, None).sum())


def members(build):
    out = {}
    for pk in sorted(Path(build).glob('zz_*_colchunks*.pk3')):
        with zipfile.ZipFile(pk) as z:
            for i in z.infolist():
                out[i.filename] = (pk, i.CRC, i.file_size)
    return out


def chunks(ca, cb, out):
    """B's chunk BSPs against A's: byte-equal, else brush-equal (the in-stage clutter compiles other glbs than a
    patched build: other bytes, the same brushes), else different; tiles only on one side; the residency manifest's
    lines (bounds move when a dropped box was at a chunk's edge) reported by tile"""
    a, b = members(ca), members(cb)
    rep = {'a': str(ca), 'b': str(cb), 'byte_equal': 0, 'brush_equal': 0, 'different': [], 'only_a': [],
           'only_b': [], 'manifest_lines_differing': [], 'chunks': 0}
    bsp = lambda m: {n for n in m if n.endswith('.bsp')}
    rep['only_a'], rep['only_b'] = sorted(bsp(a) - bsp(b)), sorted(bsp(b) - bsp(a))
    for n in sorted(bsp(a) & bsp(b)):
        rep['chunks'] += 1
        if a[n][1:] == b[n][1:]:
            rep['byte_equal'] += 1
            continue
        with zipfile.ZipFile(a[n][0]) as za, zipfile.ZipFile(b[n][0]) as zb:
            x, y = brushes(za.read(n)), brushes(zb.read(n))
        if len(x) == len(y) and np.array_equal(x, y):
            rep['brush_equal'] += 1
        else:
            oa, ob = multiset_diff(x, y)
            rep['different'].append({'chunk': n, 'brushes_a': len(x), 'brushes_b': len(y), 'only_a': oa, 'only_b': ob})
    man = [n for n in a if n.endswith('.chunks')]
    if man and man[0] in b:
        with zipfile.ZipFile(a[man[0]][0]) as za, zipfile.ZipFile(b[man[0]][0]) as zb:
            la = {ln.split()[0]: ln for ln in za.read(man[0]).decode().splitlines() if ln and not ln.startswith('//')}
            lb = {ln.split()[0]: ln for ln in zb.read(man[0]).decode().splitlines() if ln and not ln.startswith('//')}
        rep['manifest_lines_differing'] = sorted(k for k in set(la) | set(lb) if la.get(k) != lb.get(k))
    return write(out, rep)


# ---- the hub ----

def hub(pa, pb, out):
    """the hub pack's BSP lump by lump (which lumps differ, and their sizes), the other members by CRC"""
    import colbsp
    rep = {'a': str(pa), 'b': str(pb)}
    with zipfile.ZipFile(pa) as za, zipfile.ZipFile(pb) as zb:
        ia, ib = {i.filename: i.CRC for i in za.infolist()}, {i.filename: i.CRC for i in zb.infolist()}
        rep['members_differing'] = sorted(k for k in set(ia) | set(ib) if ia.get(k) != ib.get(k))
        name = next(n for n in ia if n.endswith('.bsp'))
        x, y = colbsp.read(pa), colbsp.read(pb)
    lumps = {}
    for i, ln in enumerate(colbsp.LUMP_NAMES):
        u = bytes(x.data[x.lumps[i][0]:x.lumps[i][0] + x.lumps[i][1]])
        v = bytes(y.data[y.lumps[i][0]:y.lumps[i][0] + y.lumps[i][1]])
        lumps[ln] = 'equal' if u == v else 'differs (%d -> %d bytes)' % (len(u), len(v))
    rep['bsp'] = name
    rep['lumps'] = lumps
    rep['lumps_differing'] = sorted(k for k, v in lumps.items() if v != 'equal')
    return write(out, rep)


# ---- packs and map files ----

MAP_FILES = ('xdet', 'xint', 'xrot', 'xscr', 'xfog', 'xwat', 'xman', 'xlut')


def packs(ka, kb, out, names):
    """named packs' members by CRC between two kits' home/base, and maps/<map>.<x> files by bytes as served"""
    rep = {'a': str(ka), 'b': str(kb), 'packs': {}, 'map_files': {}}
    for n in names:
        pa, pb = Path(ka) / n, Path(kb) / n
        if not pa.exists() or not pb.exists():
            rep['packs'][n] = 'only in %s' % ('a' if pa.exists() else 'b' if pb.exists() else 'neither')
            continue
        with zipfile.ZipFile(pa) as za, zipfile.ZipFile(pb) as zb:
            ia, ib = {i.filename: i.CRC for i in za.infolist()}, {i.filename: i.CRC for i in zb.infolist()}
        d = sorted(k for k in set(ia) | set(ib) if ia.get(k) != ib.get(k))
        rep['packs'][n] = 'equal' if not d else {'members_differing': len(d), 'first': d[:10]}
    def served(kit):
        own = {}
        for p in sorted(Path(kit).glob('*.pk3'), key=lambda p: p.name.upper()):
            with zipfile.ZipFile(p) as z:
                for nm in z.namelist():
                    own[nm.lower()] = (p, nm)
        return own
    sa, sb = served(ka), served(kb)
    for ext in MAP_FILES:
        n = 'maps/%s.%s' % (MAP, ext)
        if n not in sa and n not in sb:
            continue
        if n not in sa or n not in sb:
            rep['map_files'][ext] = 'only in %s' % ('a' if n in sa else 'b')
            continue
        with zipfile.ZipFile(sa[n][0]) as za, zipfile.ZipFile(sb[n][0]) as zb:
            rep['map_files'][ext] = 'equal' if za.read(sa[n][1]) == zb.read(sb[n][1]) else 'differs'
    return write(out, rep)


# ---- engine corpora under the tie rule ----

def score(A, B):
    """levelcheck_out rows (13 inputs, then end xyz, fraction, allsolid, startsolid, normal xyz, contents, surface
    flags, entity): strict = endpoint over 1/64 u, fraction over 1e-6, or contents unless the start is buried;
    excused = the rest of the flag and normal differences, and touching-start flips"""
    de = np.abs(A[:, 13:16] - B[:, 13:16]).max(1)
    df = np.abs(A[:, 16] - B[:, 16])
    buried = (A[:, 18] != 0) | (B[:, 18] != 0) | (A[:, 17] != 0) | (B[:, 17] != 0)
    contents = (A[:, 22] != B[:, 22]) & ~buried
    strict = (de > 1 / 64) | (df > 1e-6) | contents
    start_flip = ((A[:, 17] != B[:, 17]) | (A[:, 18] != B[:, 18])) & ~strict
    tie = (((A[:, 23] != B[:, 23]) | (A[:, 24] != B[:, 24]) | (np.abs(A[:, 19:22] - B[:, 19:22]).max(1) > 1e-5)
            | ((A[:, 22] != B[:, 22]) & buried)) & ~strict & ~start_flip)
    return {'traces': len(A), 'strict': int(strict.sum()), 'endpoint_over_1_64': int((de > 1 / 64).sum()),
            'fraction_over_1e-6': int((df > 1e-6).sum()), 'contents': int(contents.sum()),
            'excused_touching_start': int(start_flip.sum()), 'excused_tie_flags': int(tie.sum()),
            'identical_rows': int((~strict & ~start_flip & ~tie).sum())}


def engine(da, db, out):
    """every corpus folder under A with a levelcheck_out.txt against B's of the same name; each run's
    engine_trace.json checked (chunks loaded, errors)"""
    rep = {'a': str(da), 'b': str(db), 'corpora': {}, 'strict': 0, 'runs_with_errors': []}
    for d in sorted(p for p in Path(da).iterdir() if (p / 'levelcheck_out.txt').exists()):
        e = Path(db) / d.name
        if not (e / 'levelcheck_out.txt').exists():
            rep['corpora'][d.name] = 'missing in b'
            rep['strict'] += 1
            continue
        load = lambda p: np.array([[float(v) for v in ln.split()] for ln in open(p) if ln.strip()])
        A, B = load(d / 'levelcheck_out.txt'), load(e / 'levelcheck_out.txt')
        if A.shape != B.shape:
            rep['corpora'][d.name] = 'rows %s against %s' % (A.shape, B.shape)
            rep['strict'] += 1
            continue
        s = score(A, B)
        rep['corpora'][d.name] = s
        rep['strict'] += s['strict']
        for x in (d, e):
            j = x / 'engine_trace.json'
            if j.exists():
                r = json.loads(j.read_text(encoding='utf-8'))
                if r.get('errors'):
                    rep['runs_with_errors'].append(str(x))
    return write(out, rep)


# ---- the scene parts' own counts ----

def part_stats(scene_dir, out):
    """every scene part's stats (tiles/<tag>/scene/<tile>/part.json) summed, the tiles each count came from, and
    the mesh keys a part marks open water (a trailing 'open_water') by mesh"""
    import ast
    tot, where, water = collections.Counter(), collections.defaultdict(dict), []
    for f in sorted(Path(scene_dir).glob('*/part.json')):
        d = json.loads(f.read_text(encoding='utf-8'))
        for k, n in (d.get('stats') or {}).items():
            if isinstance(n, (int, float)) and not isinstance(n, bool):
                tot[k] += n
                if n:
                    where[k][f.parent.name] = n
        for mk in d.get('meshkeys') or []:
            try:
                t = ast.literal_eval(mk) if isinstance(mk, str) else mk
            except (ValueError, SyntaxError):
                continue
            if isinstance(t, (list, tuple)) and t and t[-1] == 'open_water':
                water.append({'tile': f.parent.name, 'mesh': str(t[0]).replace('\\', '/')})
    rep = dict(tot)
    rep.update(parts=len(list(Path(scene_dir).glob('*/part.json'))), tiles_by_count={k: where[k] for k in sorted(where)},
               open_water_meshes=water)
    for w in water:
        print('open water mesh key: tile %s, %s' % (w['tile'], w['mesh']))
    return write(out, rep)


# ---- the scene table's counts ----

def scene(kit, mjson, out):
    """the served materials table: records, terrain (0x1000), stand-in (0x2000) and cutout-bit counts; the
    metal_base_det.mt materials by kind"""
    import cp2gltf
    import scene_parity as sp
    S = sp.Side(kit, mjson)
    m = json.load(open(mjson, encoding='utf-8'))
    blob = S.read('maps/%s/materials.xscn' % MAP)
    _, count, ofs = sp.sections(blob)
    inv = {v: k for k, v in cp2gltf.SCENE_KIND.items()}
    rep = {'kit': str(kit), 'materials': count[5], 'terrain': 0, 'standin': 0, 'cutout_bit': 0,
           'metal_base_det_by_kind': collections.Counter(), 'served_by': S.served_by('maps/%s/materials.xscn' % MAP)}
    for i in range(count[5]):
        k, fl = struct.unpack_from('<2I', blob, ofs[5] + 240 * i + 192)
        rep['terrain'] += bool(fl & 0x1000)
        rep['standin'] += bool(fl & 0x2000)
        rep['cutout_bit'] += bool(fl & 1)
        if i < len(m) and m[i]['base'].replace('\\', '/').split('/')[-1] == 'metal_base_det.mt':
            rep['metal_base_det_by_kind'][inv.get(k, str(k))] += 1
    rep['metal_base_det_by_kind'] = dict(rep['metal_base_det_by_kind'])
    return write(out, rep)


# ---- material keys against rules ----

def materials(parity_json, materials_b, expect, out, materials_a=None):
    """the keys parity found only in A (removed) and only in B (added), against expect's "materials" rules:
    {"removed_exactly": [[mesh, name], ...], "added_classes": [{"class": ..., "base_regex": "<regex on the template
    file name>"}, ...]}. unexpected_removed, missing_removed and unexpected_added count the misses; every added key is
    listed with its class (or none). Keys whose record counts differ (the stand-in ad variants: one record a variant
    under the same key) are listed too: added_records sums B's extra records, unexpected_records counts keys with fewer
    records in B or whose template no class names.
    A tag of a few tiles ("subset": true in the rules, with materials_a, the reference's materials.json): the table
    names each material by the first (mesh, name) that drew it, so a subset names some by other meshes and holds fewer
    records per key. An added key whose (name, template) the reference has is then an alias (added_aliases; of them
    added_same_content have a reference record of the same kind and images), not unexpected; fewer records are not
    unexpected, nor more when every record of the key has the kind and images of a reference record"""
    import re
    rules = json.loads(Path(expect).read_text(encoding='utf-8')).get('materials', {})
    det = json.loads(Path(parity_json).read_text(encoding='utf-8')).get('details', {}).get('materials', [])
    key = lambda k: tuple(k) if isinstance(k, (list, tuple)) else tuple(re.findall(r"'([^']*)'", str(k)))
    removed = sorted({key(d['key']) for d in det if d.get('only') == 'a'})
    added = sorted({key(d['key']) for d in det if d.get('only') == 'b'})
    base = {}
    for m in json.load(open(materials_b, encoding='utf-8')):
        base.setdefault((m['mesh'], m['name']), m['base'].replace('\\', '/').split('/')[-1])
    want = {tuple(k) for k in rules.get('removed_exactly', [])}
    classes = rules.get('added_classes', [])
    subset = bool(rules.get('subset')) and materials_a
    ref_names, ref_content = set(), set()
    if subset:
        for m in json.load(open(materials_a, encoding='utf-8')):
            ref_names.add((m['name'].lower(), m['base'].lower()))
            ref_content.add((m['kind'], tuple(m.get('images') or [])))
    full_b, all_b = {}, collections.defaultdict(list)
    for m in json.load(open(materials_b, encoding='utf-8')):
        full_b.setdefault((m['mesh'], m['name']), m)
        all_b[(m['mesh'], m['name'])].append(m)
    rows = []
    def cls(b):
        return next((x['class'] for x in classes if re.search(x.get('base_regex') or re.escape(x['base']), b, re.I)),
                    None)
    for k in added:
        b = base.get(k, '?')
        r = {'key': list(k), 'base': b, 'class': cls(b)}
        mb = full_b.get(k)
        if subset and r['class'] is None and mb and (mb['name'].lower(), mb['base'].lower()) in ref_names:
            r['class'] = 'alias (subset)'
            r['same_content'] = (mb['kind'], tuple(mb.get('images') or [])) in ref_content
        rows.append(r)
    recs = []
    for d in det:
        if 'records_a' in d:
            k = key(d['key'])
            b = base.get(k, '?')
            r = {'key': list(k), 'base': b, 'records_a': d['records_a'], 'records_b': d['records_b'],
                 'class': cls(b) if d['records_b'] > d['records_a'] else 'fewer (subset)' if subset else None}
            if subset and r['class'] is None and all_b.get(k) and all(
                    (m['kind'], tuple(m.get('images') or [])) in ref_content for m in all_b[k]):
                r['class'] = 'alias (subset)'      # more records, each one a material the reference has
            recs.append(r)
    rep = {'removed': [list(k) for k in removed], 'added': rows, 'records_differing': recs,
           'unexpected_removed': len(set(removed) - want), 'missing_removed': len(want - set(removed)),
           'unexpected_added': sum(1 for r in rows if r['class'] is None),
           'added_records': sum(max(0, r['records_b'] - r['records_a']) for r in recs),
           'unexpected_records': sum(1 for r in recs if r['class'] is None),
           'added_aliases': sum(1 for r in rows if r['class'] == 'alias (subset)'),
           'added_same_content': sum(1 for r in rows if r.get('same_content')),
           'added_by_class': dict(collections.Counter(r['class'] or 'UNLISTED' for r in rows + recs))}
    for r in rows:
        print('added material key %s (template %s): %s' % (r['key'], r['base'], r['class'] or 'UNLISTED'))
    for r in recs:
        print('material key %s (template %s): %d records -> %d: %s' % (r['key'], r['base'], r['records_a'],
                                                                       r['records_b'], r['class'] or 'UNLISTED'))
    return write(out, rep)


# ---- the verdict ----

def get(results, path):
    """results/<file>.json's value at a dotted path ('chunks.byte_equal', 'parity.summary.instances.only_a'); a
    counter absent from a class that a parity summary holds is 0 (its counters omit zeros), but a class the summary
    does not hold (nothing of it was compared) is a miss; a list or object counts its entries"""
    f, *keys = path.split('.')
    p = Path(results) / (f + '.json')
    if not p.exists():
        return KeyError('no %s' % p.name)
    v = json.loads(p.read_text(encoding='utf-8'))
    seen = []
    while keys:
        # a key may hold dots itself ('zz_cp_watson_env_em.pk3'): the longest run of the path's parts the object has
        n = next((n for n in range(len(keys), 0, -1) if isinstance(v, dict) and '.'.join(keys[:n]) in v), 0)
        if n:
            seen.append('.'.join(keys[:n]))
            v, keys = v['.'.join(keys[:n])], keys[n:]
        elif isinstance(v, dict) and len(keys) == 1 and 'summary' in seen[:-1]:
            return 0                    # (a leaf counter of a class the summary holds)
        else:
            return KeyError('no %s in %s' % (keys[0], p.name))
    return len(v) if isinstance(v, (list, dict)) else v


def judge(expect, results):
    """expect.json: {"checks": [{"what": <dotted path>, "op": "==" | "<=" | ">=" | "in", "value": ..., "why": ...}]}
    -> every check's result; exit 1 on any miss or missing result"""
    exp = json.loads(Path(expect).read_text(encoding='utf-8'))
    rows, misses = [], 0
    for c in exp['checks']:
        v = get(results, c['what'])
        op, want = c.get('op', '=='), c['value']
        if isinstance(v, KeyError):
            ok = False
        elif op == '==':
            ok = v == want
        elif op == '<=':
            ok = v <= want
        elif op == '>=':
            ok = v >= want
        elif op == 'in':
            ok = want[0] <= v <= want[1]
        else:
            ok = False
        misses += not ok
        rows.append({'what': c['what'], 'op': op, 'expected': want, 'got': str(v) if isinstance(v, KeyError) else v,
                     'ok': ok, 'why': c.get('why', '')})
        print('%-4s %-48s %s %s, got %s%s' % ('ok' if ok else 'MISS', c['what'], op, want, rows[-1]['got'],
                                            '' if ok else '  (%s)' % c.get('why', '')))
    Path(results, 'verdict.json').write_text(json.dumps({'misses': misses, 'checks': rows}, indent=1),
                                             encoding='utf-8')
    print('verdict: %s (%d of %d checks met)' % ('PASS' if not misses else 'FAIL', len(rows) - misses, len(rows)))
    return misses


def main():
    global MAP
    args = sys.argv[1:]
    if '--map' in args:
        i = args.index('--map')
        MAP = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    cmd, *a = args
    if cmd == 'parts':
        parts(*a[:3])
    elif cmd == 'chunks':
        chunks(*a[:3])
    elif cmd == 'hub':
        hub(*a[:3])
    elif cmd == 'packs':
        packs(a[0], a[1], a[2], a[3:])
    elif cmd == 'engine':
        engine(*a[:3])
    elif cmd == 'part_stats':
        part_stats(*a[:2])
    elif cmd == 'scene':
        scene(*a[:3])
    elif cmd == 'materials':
        materials(*a[:5])
    elif cmd == 'judge':
        sys.exit(1 if judge(*a[:2]) else 0)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
