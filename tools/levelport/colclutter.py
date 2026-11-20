"""Road clutter: which collision boxes CP2077 simulates as pushable or debris props (hitting small debris in the road gave a bike big air).

cp2gltf.py makes every prop placement from --col-min to --col-box (1-6 m) its bounding box. CP2077 does not treat
those props alike: its physical prop nodes carry how they move and what they collide with.

- worldInstancedDestructibleMeshNode: `simulationType` (Static, Kinematic, Dynamic), `turnDynamicOnImpulse` (a
  Kinematic body that turns Dynamic when hit) and `filterData.preset` (`filterDataSource` Parent: the node's own).
- worldDynamicMeshNode: always a free rigid body (`isDebris` marks debris).
- worldPhysicalDestructionNode: a breakable mesh, its level 0 preset in `destructionLevelData`.
- the presets (collision_presets): Debris collides with neither the player nor vehicles; World Dynamic and
  Destructible bodies are pushed aside by vehicles and the player.

Classes (CLASSES; the rule is which of them collide in our game):
  debris       preset Debris (any node type), or a worldDynamicMeshNode with isDebris: collides with nothing we have
  pushable     Dynamic, or Kinematic + turnDynamicOnImpulse, or a worldDynamicMeshNode: a body the bike shoves aside
  breakable    worldPhysicalDestructionNode: stays put until broken (CP2077's vehicles smash through)
  kinematic    Kinematic without turnDynamicOnImpulse: does not move
  static       Static simulation, or any other node type (worldStaticMeshNode, worldInstancedMeshNode, ...)
A box is "low" when its placed height (world up) is at most --max-height (1 m): the rule drops debris at any height
and pushable boxes that are low, of the names ONLY allows (bags, piles, cardboard, tyres
and pallets; bins, benches, chairs, barrels stay solid); everything else stays solid. Door leaves (entity components) are never physical prop placements, so never dropped (asserted).

    python colclutter.py scan <tiles dir> <out dir> [--jobs N]      per tile: <out>/<tile>.npz (a class per box)
    python colclutter.py report <out dir> [--max-height 1] [--keep REGEX]   counts by class and by name
    python colclutter.py drops <out dir> [--max-height 1] [--keep REGEX]    <out>/drops/<tile>.json: box indices

The box order is the tile npz's (box_depot/box_lohi/box_G), which is the chunk glb's b0..bN order."""
import argparse
import collections
import json
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

CLASSES = ['static', 'kinematic', 'breakable', 'pushable', 'debris']
C_STATIC, C_KINEMATIC, C_BREAKABLE, C_PUSHABLE, C_DEBRIS = range(5)
PHYS_TYPES = ('worldInstancedDestructibleMeshNode', 'worldDynamicMeshNode', 'worldPhysicalDestructionNode')
ORIGIN = np.array([-1280.0, 2240.0, 12.0])     # the Watson area's origin (CP metres): glTF (0, 0, 0)
MATCH_M = 0.05                                  # a box joins the placement of its depot within this distance


def cp_of(G):
    """A box's placement origin in CP metres (glTF is x, z up, -y)."""
    return np.array([G[0, 3], -G[2, 3], G[1, 3]]) + ORIGIN


def preset_name(fd):
    from cp2gltf import cname
    return cname(((fd or {}).get('Data') or {}).get('preset'))


def sector_fields(job):
    """Every physical prop placement of a sector -> its fields, keyed by (mesh, m rounded as cp2gltf emits it)."""
    path, = job
    from cp2gltf import buffer_slice, cname, depot, shared_buffers, trs
    rc = json.load(open(path, encoding='utf-8'))['Data']['RootChunk']
    nodes, records = rc['nodes'], rc['nodeData']['Data']
    buffers = shared_buffers(rc)
    by_node = collections.defaultdict(list)
    for r in records:
        by_node[r['NodeIndex']].append(r)
    out = {}
    for ni, recs in by_node.items():
        n = nodes[ni]['Data']
        t = n['$type']
        if t not in PHYS_TYPES:
            continue
        mesh = depot(n.get('mesh')) or depot(n.get('meshRef'))
        if t == 'worldInstancedDestructibleMeshNode':
            f = {'type': t, 'sim': n.get('simulationType'), 'imp': int(n.get('turnDynamicOnImpulse') or 0),
                 'preset': preset_name(n.get('filterData')), 'fds': n.get('filterDataSource')}
            local = buffer_slice(n, 'cookedInstanceTransforms', buffers)
            ms = [trs(r['Position'], r['Orientation'], r['Scale']) @ trs(tr.get('position', {}), tr.get('orientation'), None)
                  for r in recs for tr in local]
        else:
            if t == 'worldDynamicMeshNode':
                f = {'type': t, 'sim': 'Dynamic', 'imp': 0, 'preset': 'Debris' if n.get('isDebris') else '',
                     'fds': ''}
            else:
                lv = n.get('destructionLevelData') or []
                f = {'type': t, 'sim': 'Breakable', 'imp': 0,
                     'preset': preset_name(lv[0].get('filterData')) if lv else '', 'fds': ''}
            ms = [trs(r['Position'], r['Orientation'], r['Scale']) for r in recs]
        f['debug'] = cname(n.get('debugName'))
        for m in ms:
            key = (mesh, tuple(round(float(v), 5) for v in m[:3].ravel()))
            prev = out.get(key)
            out[key] = f if prev is None or prev == f else dict(f, sim='?', preset='?')
    return path.name.split('.')[0], out


def classify(f):
    """CLASSES index of a physical prop's fields (None: not a physical prop node)."""
    if f is None:
        return C_STATIC
    if f['preset'] == 'Debris':
        return C_DEBRIS
    if f['type'] == 'worldPhysicalDestructionNode':
        return C_BREAKABLE
    if f['sim'] == 'Dynamic' or (f['sim'] == 'Kinematic' and f['imp']):
        return C_PUSHABLE
    if f['sim'] == 'Kinematic':
        return C_KINEMATIC
    return C_STATIC


def tile_boxes(job):
    """A tile's boxes joined to their placements: height, CP origin, the placement (type, sector, m) or none."""
    from scipy.spatial import cKDTree
    tiles, tid = job
    with np.load(tiles / 'build' / ('%s.npz' % tid), allow_pickle=True) as z:
        dep, lh, G = [str(d) for d in z['box_depot']], z['box_lohi'], z['box_G']
        doors = tuple(json.loads(str(z[k])) if k in z.files else [] for k in ('door_open', 'door_closed'))
    pl = collections.defaultdict(list)
    for line in open(tiles / ('%s.json' % tid), encoding='utf-8'):
        p = json.loads(line)
        pl[p['mesh']].append(p)
    n = len(dep)
    h = np.zeros(n, np.float32)
    dist = np.full(n, np.inf, np.float32)
    pick = [None] * n
    by = collections.defaultdict(list)
    for i, d in enumerate(dep):
        by[d].append(i)
    for d, idx in by.items():
        idx = np.array(idx)
        lo, hi = lh[idx, :3], lh[idx, 3:]
        corners = np.stack([np.stack([np.where([cx, cy, cz], hi, lo) for cx in (0, 1) for cy in (0, 1) for cz in (0, 1)], 1)
                            for _ in (0,)])[0]                                            # (k, 8, 3)
        W = np.einsum('kij,kcj->kci', G[idx, :3, :3], corners) + G[idx, None, :3, 3]
        h[idx] = W[:, :, 1].max(1) - W[:, :, 1].min(1)
        cands = pl.get(d) or []
        if not cands:
            continue
        P = np.array([[p['m'][3], p['m'][7], p['m'][11]] for p in cands])
        Q = np.array([cp_of(G[i]) for i in idx])
        dd, j = cKDTree(P).query(Q)
        for i, di, ji in zip(idx, dd, j):
            dist[i] = di
            if di <= MATCH_M:
                pick[i] = cands[ji]
    rows = [(p['type'], p['sector'], p['mesh'], tuple(p['m'])) if p else None for p in pick]
    return tid, dep, h, dist, rows, doors


def cmd_scan(a):
    tiles, out = Path(a.tiles), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ids = sorted(p.stem for p in (tiles / 'build').glob('*.npz'))
    if a.tiles_only:
        ids = [t for t in ids if t in set(a.tiles_only.split(','))]
    with ProcessPoolExecutor(a.jobs) as ex:
        boxes = {r[0]: r[1:] for r in ex.map(tile_boxes, [(tiles, t) for t in ids])}
    print('joined %d tiles, %d boxes' % (len(boxes), sum(len(b[0]) for b in boxes.values())), flush=True)
    sectors = sorted({r[1] for b in boxes.values() for r in b[3] if r and r[0] in PHYS_TYPES})
    js = Path(a.json)
    print('physical prop sectors', len(sectors), flush=True)
    fields = {}
    with ProcessPoolExecutor(a.jobs) as ex:
        for k, (s, f) in enumerate(ex.map(sector_fields, [(js / ('%s.streamingsector.json' % s),) for s in sectors])):
            fields[s] = f
            if k % 200 == 0:
                print(' sectors', k, flush=True)
    summary = collections.Counter()
    for tid, (dep, h, dist, rows, doors) in sorted(boxes.items()):
        cls = np.zeros(len(dep), np.uint8)
        info = []
        for i, r in enumerate(rows):
            f = None
            if r and r[0] in PHYS_TYPES:
                f = fields.get(r[1], {}).get((r[2], r[3]))
                if f is None:
                    summary['physical placement without its node'] += 1
                    f = {'type': r[0], 'sim': '?', 'imp': 0, 'preset': '?', 'fds': '', 'debug': ''}
            cls[i] = classify(f)
            info.append([r[0] if r else '?', (f or {}).get('sim', ''), (f or {}).get('preset', ''),
                         (f or {}).get('imp', 0)])
            summary['matched' if r else 'unmatched'] += 1
        names = [d.split('\\')[-1][:-5] for d in dep]
        door_names = {k.split('|')[1].strip('{}') for k in list(doors[0]) + list(doors[1])}
        np.savez_compressed(out / ('%s.npz' % tid), cls=cls, h=h, dist=dist, names=np.array(names),
                            info=np.array(info, dtype=object) if info else np.zeros((0, 4), object),
                            doors=np.array(sorted(door_names)), allow_pickle=True)
    (out / 'scan.json').write_text(json.dumps({'tiles': len(boxes), **summary}, indent=1), encoding='utf-8')
    print(dict(summary))


def load(out):
    for p in sorted(Path(out).glob('*.npz')):
        with np.load(p, allow_pickle=True) as z:
            yield p.stem, {k: z[k] for k in z.files}


# only bags, piles, cardboard, tyres and pallets lose collision;
# trash bins, benches, chairs, barrels and every other candidate stay solid
ONLY = r'garbage_bag|_pile(_|$)|^cardboard|tire|tyre|pallet'


def drop_mask(z, max_h, keep, only=ONLY):
    """The boxes the rule takes out: debris at any height and pushable ones up to max_h, whose name matches only
    (ONLY, the dropped classes; '' or None: every candidate), never a name matching keep. Only physical prop placements (PHYS_TYPES) are ever classed debris or pushable, so no door leaf (an entity's
    component, placement type 'entity') can be taken: asserted. (Matching door names failed: a door named "door"
    matched outdoor_bench_e.)"""
    cls, h, names = z['cls'], z['h'], z['names']
    m = (cls == C_DEBRIS) | ((cls == C_PUSHABLE) & (h <= max_h))
    if m.any() and only:
        rx = re.compile(only)
        for i in np.nonzero(m)[0]:
            if not rx.search(str(names[i])):
                m[i] = False
    if m.any() and keep:
        for i in np.nonzero(m)[0]:
            if keep.search(str(names[i])):
                m[i] = False
    if m.any():
        bad = [z['info'][i][0] for i in np.nonzero(m)[0] if z['info'][i][0] not in PHYS_TYPES]
        assert not bad, 'a dropped box is not a physical prop placement: %s' % bad[:3]
    return m


def base(nm):
    return re.sub(r'(_\d+)+$', '', nm)


def cmd_report(a):
    keep = re.compile(a.keep) if a.keep else None
    by = collections.Counter()
    names = collections.defaultdict(collections.Counter)
    total = dropped = 0
    for tid, z in load(a.out):
        m = drop_mask(z, a.max_height, keep, a.only)
        low = z['h'] <= a.max_height
        for c in range(len(CLASSES)):
            sel = z['cls'] == c
            by[(CLASSES[c], 'low')] += int((sel & low).sum())
            by[(CLASSES[c], 'tall')] += int((sel & ~low).sum())
        for i in np.nonzero(z['cls'] >= C_BREAKABLE)[0]:
            names[(CLASSES[z['cls'][i]], 'dropped' if m[i] else 'kept')][base(str(z['names'][i]))] += 1
        total += len(z['cls'])
        dropped += int(m.sum())
    print('boxes %d; dropped by the rule %d (max height %.2f m, names %r%s)' % (
        total, dropped, a.max_height, a.only, ', keep %s' % a.keep if a.keep else ''))
    for c in CLASSES:
        print('  %-10s  low %8d  tall %8d' % (c, by[(c, 'low')], by[(c, 'tall')]))
    for k in sorted(names):
        top = names[k].most_common(a.top)
        print('%s %s %d: %s' % (k[0], k[1], sum(names[k].values()), ', '.join('%s %d' % t for t in top)))


def cmd_drops(a):
    keep = re.compile(a.keep) if a.keep else None
    d = Path(a.out) / 'drops'
    d.mkdir(exist_ok=True)
    n = 0
    for tid, z in load(a.out):
        idx = np.nonzero(drop_mask(z, a.max_height, keep, a.only))[0]
        (d / ('%s.json' % tid)).write_text(json.dumps({'tile': tid, 'max_height': a.max_height, 'keep': a.keep,
                                                       'only': a.only,
                                                       'boxes': len(z['cls']), 'drop': idx.tolist()}),
                                           encoding='utf-8')
        n += len(idx)
    print('drop lists in %s: %d boxes' % (d, n))


def box_corners(lohi, M):
    """(k, 8, 3) glTF corners of boxes (local lo/hi) placed by M (k, 4, 4)."""
    lo, hi = lohi[:, :3], lohi[:, 3:]
    c = np.stack([np.where(np.array([cx, cy, cz], bool), hi, lo) for cx in (0, 1) for cy in (0, 1) for cz in (0, 1)], 1)
    return np.einsum('kij,kcj->kci', M[:, :3, :3], c) + M[:, None, :3, 3]


def chunk_box_matrices(glb):
    """The chunk glb's box nodes (b0..bN) matrices, in order (chunk-local glTF)."""
    import struct
    d = Path(glb).read_bytes()
    n = struct.unpack_from('<I', d, 12)[0]
    j = json.loads(d[20:20 + n])
    bn = [nd for nd in j['nodes'] if nd.get('name', '').startswith('b') and nd['name'][1:].isdigit()]
    assert [int(nd['name'][1:]) for nd in bn] == list(range(len(bn))), 'box nodes out of order'
    return np.array([np.array(nd['matrix'], np.float64).reshape(4, 4).T for nd in bn]) if bn else np.zeros((0, 4, 4))


def brush_bounds(b):
    """Every brush's axial bounds (k, 6: mins, maxs) from its first six sides (q3map2 puts the axial planes there,
    -x +x -y +y -z +z, as the engine's CM_BoundBrush reads them); False where a brush's first six are not those."""
    import colbsp
    br, sd, pl = b.lump(colbsp.L_BRUSHES, colbsp.BRUSH), b.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE), b.lump(colbsp.L_PLANES, colbsp.PLANE)
    k = np.arange(6)
    idx = br['first_side'][:, None] + k[None]
    ok = br['num_sides'] >= 6
    idx = np.where(ok[:, None], idx, 0)
    P = pl[sd['plane'][idx]]
    n, d = P['normal'], P['dist'].astype(np.float64)
    ok &= np.all(np.abs(n - colbsp.AXES[None]) < 1e-6, axis=(1, 2))
    sign = np.array([-1, 1, -1, 1, -1, 1], np.float64)
    v = d * sign
    return np.stack([v[:, 0], v[:, 2], v[:, 4], v[:, 1], v[:, 3], v[:, 5]], 1), ok


def match_boxes(lohi, M, b, tol=0.05):
    """For each box (local lo/hi, chunk-local glTF matrix), the brush of the BSP b that is it: the brush whose axial
    bounds are the box's corners' bounds (q3 units) within tol, nearest; -1 where none."""
    import levelport as lp
    from scipy.spatial import cKDTree
    q = lp.gltf_to_q3(box_corners(lohi, M))
    want = np.concatenate([q.min(1), q.max(1)], 1)
    have, ok = brush_bounds(b)
    cand = np.nonzero(ok)[0]
    dd, j = cKDTree(have[cand]).query(want, distance_upper_bound=tol * np.sqrt(6))
    hit = np.isfinite(dd)
    out = np.full(len(want), -1, np.int64)
    out[hit] = cand[j[hit]]
    return out, dd


from colbsp import FOG, L_FOGS, L_MODELS, MODEL  # noqa: E402


def match_unique(lohi, M, b, tol=0.05, k=8):
    """match_boxes, each brush taken by one box at most (identical boxes are identical brushes: the first box takes
    the first brush): box -> brush or -1."""
    import levelport as lp
    from scipy.spatial import cKDTree
    q = lp.gltf_to_q3(box_corners(lohi, M))
    want = np.concatenate([q.min(1), q.max(1)], 1)
    have, ok = brush_bounds(b)
    cand = np.nonzero(ok)[0]
    dd, j = cKDTree(have[cand]).query(want, k=k, distance_upper_bound=tol * np.sqrt(6))
    out = np.full(len(want), -1, np.int64)
    taken = set()
    for i in range(len(want)):
        for d, jj in zip(dd[i], j[i]):
            if not np.isfinite(d):
                break
            c = int(cand[jj])
            if c not in taken:
                taken.add(c)
                out[i] = c
                break
    return out


def is_box_brush(b, bi, M):
    """Brush bi's own faces (its non-bevel sides) are all faces of the box placed by M: no brush of another shape that
    happens to have the box's bounds is taken for it."""
    import colbsp
    import levelport as lp
    br, sd, pl, sh = (b.lump(colbsp.L_BRUSHES, colbsp.BRUSH), b.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE),
                      b.lump(colbsp.L_PLANES, colbsp.PLANE), b.lump(colbsp.L_SHADERS, colbsp.SHADER))
    s = sd[br['first_side'][bi]:br['first_side'][bi] + br['num_sides'][bi]]
    bevel = np.array([sh['name'][x].split(b'\0')[0].lower() == colbsp.BEVEL_SHADER for x in s['shader']])
    n = pl['normal'][s['plane'][~bevel]].astype(np.float64)
    axes = lp.dir_gltf_to_q3(np.linalg.inv(M[:3, :3]))     # rows: the box's face normals (inverse transpose)
    f = np.concatenate([axes, -axes])
    f /= np.linalg.norm(f, axis=1, keepdims=True)
    return len(n) == 6 and bool(np.all(np.max(n @ f.T, 1) > 1 - 1e-3))


def remove_brushes(b, drop):
    """The lumps of b with brushes `drop` (indices) taken out: brushes, their sides, the leaves' brush lists, the
    models' brush ranges and the fogs' brush numbers renumbered; planes, nodes and leaves otherwise as they were
    (colbsp.slim afterwards stores only the planes still used)."""
    import colbsp
    br, sd = b.lump(colbsp.L_BRUSHES, colbsp.BRUSH), b.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE)
    lb, lf = b.lump(colbsp.L_LEAFBRUSHES, np.dtype('<i4')), b.lump(colbsp.L_LEAFS, colbsp.LEAF)
    md, fg = b.lump(L_MODELS, MODEL), b.lump(L_FOGS, FOG)
    keep = np.ones(len(br), bool)
    keep[np.asarray(drop, np.int64)] = False
    kb = np.concatenate([[0], np.cumsum(keep)]).astype(np.int64)
    newidx = kb[:-1] - 0
    nbr = br[keep].copy()
    starts, counts = nbr['first_side'].astype(np.int64), nbr['num_sides'].astype(np.int64)
    sel = np.repeat(starts - np.concatenate([[0], np.cumsum(counts)[:-1]]), counts) + np.arange(int(counts.sum()))
    nsd = sd[sel].copy()
    nbr['first_side'] = np.concatenate([[0], np.cumsum(counts)[:-1]]).astype(np.int32)
    klb = keep[lb]
    nlb = newidx[lb[klb]].astype(np.int32)
    cl = np.concatenate([[0], np.cumsum(klb)]).astype(np.int64)
    nlf = lf.copy()
    f0 = lf['first_brush'].astype(np.int64)
    nlf['first_brush'] = cl[f0]
    nlf['num_brushes'] = cl[f0 + lf['num_brushes']] - cl[f0]
    nmd = md.copy()
    m0 = md['first_brush'].astype(np.int64)
    nmd['first_brush'] = kb[m0]
    nmd['num_brushes'] = kb[m0 + md['num_brushes']] - kb[m0]
    nfg = fg.copy()
    if len(fg):
        if not keep[fg['brush']].all():
            raise ValueError('a fog brush would be removed')
        nfg['brush'] = newidx[fg['brush']]
    return {colbsp.L_BRUSHES: nbr, colbsp.L_BRUSHSIDES: nsd, colbsp.L_LEAFBRUSHES: nlb, colbsp.L_LEAFS: nlf,
            L_MODELS: nmd, L_FOGS: nfg}


def patch_chunk(job):
    """One chunk: its XBSZ member decoded, the drop list's boxes matched to their brushes and removed, slimmed,
    encoded again. -> (tile, new XBSZ path or None, report)"""
    import time
    import colbsp
    import colzbsp
    t0 = time.time()
    tile, pack, tiles, drops, tmp, level = job
    name = 'maps/chunk_%s.bsp' % tile
    import zipfile
    with zipfile.ZipFile(pack) as z:
        data = z.read(name)
    b = colbsp.Rbsp(colbsp.plain(data))
    with np.load(Path(tiles) / 'build' / ('%s.npz' % tile), allow_pickle=True) as z:
        lohi = z['box_lohi']
    M = chunk_box_matrices(Path(tiles) / 'chunks' / ('%s.glb' % tile))
    if len(M) != len(lohi):
        raise ValueError('%s: %d box nodes, %d boxes' % (tile, len(M), len(lohi)))
    idx = match_unique(lohi, M, b)
    flat = (lohi[:, 3:] - lohi[:, :3]).min(1) <= 0
    drop = np.asarray(drops, np.int64)
    rep = {'tile': tile, 'boxes': len(lohi), 'drop_listed': len(drop), 'brushes_before': len(b.lump(colbsp.L_BRUSHES, colbsp.BRUSH))}
    lost = [int(i) for i in drop if idx[i] < 0 and not flat[i]]
    if lost:
        raise ValueError('%s: %d dropped boxes have no brush (first %s)' % (tile, len(lost), lost[:5]))
    rep['unmatched_kept_boxes'] = int(((idx < 0) & ~flat).sum()) - 0
    bad = [int(i) for i in drop if idx[i] >= 0 and not is_box_brush(b, idx[i], M[i])]
    if bad:
        raise ValueError('%s: %d dropped boxes matched a brush of another shape (first %s)' % (tile, len(bad), bad[:5]))
    gone = np.unique(idx[drop][idx[drop] >= 0])
    rep['brushes_removed'] = int(len(gone))
    b2 = colbsp.Rbsp(bytes(colbsp.to_bytes(b, remove_brushes(b, gone))))
    lumps, srep = colbsp.slim(b2, 0.0)
    out = bytes(colbsp.to_bytes(b2, lumps))
    rep.update(brushes_after=int(len(b2.lump(colbsp.L_BRUSHES, colbsp.BRUSH))), planes_before=srep['planes_before'],
               planes_after=srep['planes_after'], bsp_before=len(b.data), bsp_after=len(out))
    x = colzbsp.encode(out, level)
    if colzbsp.decode(x) != out:
        raise ValueError('%s: XBSZ readback differs' % tile)
    p = Path(tmp) / ('chunk_%s.xbsz' % tile)
    p.write_bytes(x)
    rep.update(xbsz_before=len(data), xbsz_after=len(x), seconds=round(time.time() - t0, 1))
    return tile, str(p), rep


def cmd_patch(a):
    """The chunk packs with the drop lists' brushes taken out: <out>/zz_<map>_colchunks*.pk3 as the input's (the
    same packs, members and order; untouched chunks raw-copied, patched ones stored XBSZ; the manifest and cfg as
    they were: model 0's bounds are kept), plus <out>/clutter-patch.json."""
    import time
    import zipfile
    import cp77_collision as cc77
    src, out, drops = Path(a.packs), Path(a.out), Path(a.drops)
    tiles = Path(a.tiles)
    part = out.with_name(out.name + '.partial')
    (part / 'tmp').mkdir(parents=True, exist_ok=True)
    packs = sorted(src.glob('zz_%s_colchunks*.pk3' % a.map))
    where, member_size = {}, {}
    for pk in packs:
        with zipfile.ZipFile(pk) as z:
            for i in z.infolist():
                where[i.filename] = pk
                member_size[i.filename] = i.file_size
    only = set(a.tiles_only.split(',')) if a.tiles_only else None
    jobs = []
    for n, pk in where.items():
        if not (n.startswith('maps/chunk_') and n.endswith('.bsp')):
            continue
        t = n[len('maps/chunk_'):-4]
        f = drops / ('%s.json' % t)
        dl = json.loads(f.read_text(encoding='utf-8'))['drop'] if f.exists() else []
        if dl and (only is None or t in only):
            jobs.append((t, str(pk), str(tiles), dl, str(part / 'tmp'), a.level))
    size = {j[0]: member_size['maps/chunk_%s.bsp' % j[0]] for j in jobs}
    jobs.sort(key=lambda j: -size[j[0]])
    t0 = time.time()
    done, reps = {}, []
    from concurrent.futures import FIRST_COMPLETED, wait
    # a start waits for memory (cp77_collision's S1 gate): its peak, 1 GB + 34 x the XBSZ member (-6_0, 208 MB:
    # 6.9 GB measured with its checks), plus the margin, beside what the running ones still hold in reserve
    est = {t: 1.0 + 34 * s / 2 ** 30 for t, s in size.items()}
    running = {}
    if a.idle:
        import psutil
        psutil.Process().nice(psutil.IDLE_PRIORITY_CLASS if os.name == 'nt' else 19)   # (the workers inherit it)
    margin = cc77.MARGIN_GB if a.margin_gb is None else a.margin_gb
    said = 0.0
    with ProcessPoolExecutor(a.jobs) as ex:
        queue = list(jobs)
        while queue or running:
            while queue and len(running) < a.jobs:
                av, fc = cc77.memory()
                need = est[queue[0][0]] + margin + sum(est[t] for t in running.values())
                # --margin-gb set (someone's game beside it): the margin holds even with nothing running
                if (running or a.margin_gb is not None) and (av < need or fc < need):
                    if not running and time.time() - said > 60:
                        print('waiting for memory: %s needs %.1f GB, %.1f available, %.1f free commit'
                              % (queue[0][0], need, av, fc), flush=True)
                        said = time.time()
                    break
                j = queue.pop(0)
                running[ex.submit(patch_chunk, j)] = j[0]
            if not running:
                time.sleep(5)
                continue
            fin, _ = wait(list(running), timeout=5, return_when=FIRST_COMPLETED)
            for f in fin:
                running.pop(f)
                tile, p, rep = f.result()
                done[tile] = p
                reps.append(rep)
                print('%s: %d brushes removed, %d -> %d bytes XBSZ, %.0f s' % (tile, rep['brushes_removed'],
                      rep['xbsz_before'], rep['xbsz_after'], rep['seconds']), flush=True)
    for pk in packs:
        dst = part / pk.name
        with zipfile.ZipFile(pk) as zi, zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as zo:
            for i in zi.infolist():
                t = i.filename[len('maps/chunk_'):-4] if i.filename.startswith('maps/chunk_') else None
                if t in done:
                    zo.write(done[t], i.filename, compress_type=zipfile.ZIP_STORED)
                else:
                    cc77.raw_copy(zo, pk, i.filename)
            if pk.name == 'zz_%s_colchunks.pk3' % a.map:
                zo.writestr('maps/%s_clutter.json' % a.map, json.dumps({
                    'rule': 'colclutter.py: debris at any height, pushable boxes up to %s m' % a.max_height,
                    'source': str(src), 'chunks': sorted(done)}, indent=1))
    for p in done.values():
        Path(p).unlink()
    (part / 'tmp').rmdir()
    rep = {'source': str(src), 'patched_chunks': len(done), 'brushes_removed': sum(r['brushes_removed'] for r in reps),
           'xbsz_before': sum(r['xbsz_before'] for r in reps), 'xbsz_after': sum(r['xbsz_after'] for r in reps),
           'wall_seconds': round(time.time() - t0, 1), 'chunks': reps}
    (part / 'clutter-patch.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    part.rename(out)
    print('patched %d chunks, %d brushes removed, %.0f s -> %s' % (len(done), rep['brushes_removed'], rep['wall_seconds'], out))


def verify_chunk(job):
    """A patched chunk against its source: brushes removed = the dropped boxes' brushes; every kept brush the same
    (sides, planes' values, shaders) in the same order; every leaf's brush list the old one minus the removed,
    renumbered; model bounds kept; every other lump but the planes and nodes' plane numbers byte for byte; dropped
    boxes no longer found, kept ones found as before. -> report (ok: all of them)"""
    import zipfile
    import colbsp
    tile, src_pack, new_pack, tiles, drops = job
    name = 'maps/chunk_%s.bsp' % tile
    with zipfile.ZipFile(src_pack) as z:
        a = colbsp.Rbsp(colbsp.plain(z.read(name)))
    with zipfile.ZipFile(new_pack) as z:
        info = z.getinfo(name)
        b = colbsp.Rbsp(colbsp.plain(z.read(name)))
    with np.load(Path(tiles) / 'build' / ('%s.npz' % tile), allow_pickle=True) as z:
        lohi = z['box_lohi']
    M = chunk_box_matrices(Path(tiles) / 'chunks' / ('%s.glb' % tile))
    ia, ib = match_unique(lohi, M, a), match_unique(lohi, M, b)
    drop = np.zeros(len(lohi), bool)
    drop[np.asarray(drops, np.int64)] = True
    abr, asd, apl = a.lump(colbsp.L_BRUSHES, colbsp.BRUSH), a.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE), a.lump(colbsp.L_PLANES, colbsp.PLANE)
    bbr, bsd, bpl = b.lump(colbsp.L_BRUSHES, colbsp.BRUSH), b.lump(colbsp.L_BRUSHSIDES, colbsp.SIDE), b.lump(colbsp.L_PLANES, colbsp.PLANE)
    gone = np.zeros(len(abr), bool)
    gone[ia[drop & (ia >= 0)]] = True
    kb = np.cumsum(~gone) - 1
    r = {'tile': tile, 'stored': info.compress_type == zipfile.ZIP_STORED, 'brushes_removed': int(gone.sum())}
    r['brush_count'] = len(bbr) == int((~gone).sum())
    k = ~gone
    if r['brush_count']:
        starts, counts = abr['first_side'][k].astype(np.int64), abr['num_sides'][k].astype(np.int64)
        sa = np.repeat(starts - np.concatenate([[0], np.cumsum(counts)[:-1]]), counts) + np.arange(int(counts.sum()))
        va, vb = apl[asd['plane'][sa]], bpl[bsd['plane']]
        r['kept_brushes'] = bool(np.array_equal(abr['num_sides'][k], bbr['num_sides']) and np.array_equal(abr['shader'][k], bbr['shader'])
                                 and np.array_equal(asd['shader'][sa], bsd['shader']) and np.array_equal(asd['surface'][sa], bsd['surface'])
                                 and np.array_equal(va['normal'], vb['normal']) and np.array_equal(va['dist'], vb['dist']))
    else:
        r['kept_brushes'] = False
    alb, blb = a.lump(colbsp.L_LEAFBRUSHES, np.dtype('<i4')), b.lump(colbsp.L_LEAFBRUSHES, np.dtype('<i4'))
    alf, blf = a.lump(colbsp.L_LEAFS, colbsp.LEAF), b.lump(colbsp.L_LEAFS, colbsp.LEAF)
    keep_lb = ~gone[alb]
    cl = np.concatenate([[0], np.cumsum(keep_lb)])
    f0 = alf['first_brush'].astype(np.int64)
    leaves_ok = np.array_equal(blb, kb[alb[keep_lb]]) and np.array_equal(blf['first_brush'], cl[f0]) \
        and np.array_equal(blf['num_brushes'], cl[f0 + alf['num_brushes']] - cl[f0])
    other = [n for n in ('cluster', 'area', 'mins', 'maxs', 'first_surface', 'num_surfaces') if not np.array_equal(alf[n], blf[n])]
    r['leaves'] = bool(leaves_ok and not other)
    an, bn = a.lump(colbsp.L_NODES, colbsp.NODE), b.lump(colbsp.L_NODES, colbsp.NODE)
    r['nodes'] = bool(np.array_equal(an['children'], bn['children']) and np.array_equal(an['mins'], bn['mins'])
                      and np.array_equal(an['maxs'], bn['maxs']) and np.array_equal(apl[an['plane']], bpl[bn['plane']]))
    am, bm = a.lump(L_MODELS, MODEL), b.lump(L_MODELS, MODEL)
    r['models'] = bool(np.array_equal(am['mins'], bm['mins']) and np.array_equal(am['maxs'], bm['maxs']))
    changed = {colbsp.L_PLANES, colbsp.L_NODES, colbsp.L_LEAFS, colbsp.L_LEAFBRUSHES, colbsp.L_BRUSHES, colbsp.L_BRUSHSIDES, L_MODELS}
    r['other_lumps'] = all(bytes(a.data[a.lumps[i][0]:a.lumps[i][0] + a.lumps[i][1]]) ==
                           bytes(b.data[b.lumps[i][0]:b.lumps[i][0] + b.lumps[i][1]]) for i in range(colbsp.HEADER_LUMPS) if i not in changed)
    r['matches'] = bool(np.all(ib[drop] < 0) and np.array_equal(ib[~drop] >= 0, ia[~drop] >= 0))
    r['ok'] = all(r[x] for x in ('stored', 'brush_count', 'kept_brushes', 'leaves', 'nodes', 'models', 'other_lumps', 'matches'))
    return r


def cmd_verify(a):
    """Every chunk of a patched build against the source's: patched ones by verify_chunk, the others byte for byte;
    the manifest and cfg the same.   -> <out>/clutter-verify.json"""
    import zipfile
    src, new, drops = Path(a.packs), Path(a.out), Path(a.drops)

    def index(d):
        o = {}
        for pk in sorted(d.glob('zz_%s_colchunks*.pk3' % a.map)):
            with zipfile.ZipFile(pk) as z:
                for i in z.infolist():
                    o[i.filename] = pk
        return o
    S, N = index(src), index(new)
    rep = {'missing': sorted(set(S) - set(N)), 'extra': sorted(set(N) - set(S)), 'bad': [], 'byte_identical': 0}
    jobs = []
    for n in sorted(S):
        if n not in N:
            continue
        t = n[len('maps/chunk_'):-4] if n.startswith('maps/chunk_') else None
        f = drops / ('%s.json' % t) if t else None
        dl = json.loads(f.read_text(encoding='utf-8'))['drop'] if f and f.exists() else []
        if dl:
            jobs.append((t, str(S[n]), str(N[n]), a.tiles, dl))
        else:
            with zipfile.ZipFile(S[n]) as zs, zipfile.ZipFile(N[n]) as zn:
                if zs.read(n) == zn.read(n):
                    rep['byte_identical'] += 1
                else:
                    rep['bad'].append(n)
    reps = []
    with ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(verify_chunk, jobs):
            reps.append(r)
            if not r['ok']:
                rep['bad'].append(r['tile'])
            print(r['tile'], 'ok' if r['ok'] else 'FAILED %s' % r, flush=True)
    rep.update(patched_ok=sum(r['ok'] for r in reps), patched=len(reps), chunks=reps,
               ok=not rep['bad'] and not rep['missing'])
    (new / 'clutter-verify.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print(json.dumps({k: v for k, v in rep.items() if k != 'chunks'}))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('scan')
    s.add_argument('tiles')
    s.add_argument('out')
    s.add_argument('--json', default=os.path.join(os.environ.get('XENJK_RESEARCH') or str(HERE.parent.parent / '.codex-research'),
                                                  'levelport', 'cp77', 'watson', 'json'),
                   help="the area's record JSON (default: $XENJK_RESEARCH, else the repository's .codex-research, "
                        "/levelport/cp77/watson/json)")
    s.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    s.add_argument('--tiles-only', default='')
    for name in ('report', 'drops'):
        r = sub.add_parser(name)
        r.add_argument('out')
        r.add_argument('--max-height', type=float, default=1.0)
        r.add_argument('--keep', default='', help='names (regex) that stay solid whatever their class')
        r.add_argument('--only', default=ONLY, help="names (regex) that may lose collision (default: bags, piles, "
                       "cardboard, tyres and pallets, %(default)s; '': every debris and low pushable box)")
        r.add_argument('--top', type=int, default=12)
    p = sub.add_parser('patch')
    p.add_argument('packs', help='the build folder of the chunk packs to patch')
    p.add_argument('drops', help='colclutter drops folder')
    p.add_argument('tiles', help='the tiles folder the packs were built from (build/*.npz, chunks/*.glb)')
    p.add_argument('out', help='a new build folder (made via <out>.partial)')
    p.add_argument('--map', default='cp_watson')
    p.add_argument('--jobs', type=int, default=6)
    p.add_argument('--level', type=int, default=19)
    p.add_argument('--max-height', type=float, default=1.0, help='(recorded in the note only)')
    p.add_argument('--idle', action='store_true', help='Idle priority (the workers inherit it)')
    p.add_argument('--margin-gb', type=float, default=None,
                   help='memory kept free beside every start, even the first (default: the driver\'s 2 GB, waived '
                        'when nothing runs)')
    p.add_argument('--tiles-only', default='')
    v = sub.add_parser('verify')
    v.add_argument('packs', help='the source build folder')
    v.add_argument('drops')
    v.add_argument('tiles')
    v.add_argument('out', help='the patched build folder')
    v.add_argument('--map', default='cp_watson')
    v.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args(argv)
    {'scan': cmd_scan, 'report': cmd_report, 'drops': cmd_drops, 'patch': cmd_patch, 'verify': cmd_verify}[a.cmd](a)


if __name__ == '__main__':
    main()
