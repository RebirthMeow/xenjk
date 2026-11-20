"""patch_chunks.py -- a fix to some collision boxes played in minutes, without compiling a tile again.

  python -I patch_chunks.py <name> --base-kit DIR [--tag 8] [--tiles a,b] [--fix box-shapes,doors]

A tile's chunk BSP holds its mesh triangles and its prop boxes together, and compiling one is minutes (2 million
triangles); a fix to boxes alone does not need that. For each tile:
  1. the boxes the fix changes are found among the base kit's brushes (a box is one brush; its axial sides are its
     bounds, matched to 1 u in the chunk's own frame) and switched off: each such brush is pointed at a new shader
     that collides with nothing (the engine takes a brush's contents from its shader, cm_load.cpp CMod_LoadBrushes);
     the BSP is otherwise byte for byte the base kit's;
  2. the right boxes for every switched-off brush (a brush two boxes shared gives both back) are compiled alone into
     one small BSP, chunk_<tile>_p, at the tile's origin (cp2gltf.py's chunk frame; gltf2map --col-chunk);
  3. patch_kit.py makes the dev kit build/cp_watson_dev-<name>/: the base kit's files as hard links, and a patch pack
     with the switched-off chunks and the _p chunks (both placed by the patch's maps/<map>.chunks); --full-pack, a
     whole chunk pack instead (many tiles' BSPs pass the engine's 4 GB limit for one pack).
Steps (--steps): plan (1 and 2, --jobs tiles at once), kit (3), scene (below).

--fix box-shapes: cp2gltf.py stage_chunks drew every box of one mesh in a tile with that tile's first box's shape,
so split frames (frame_pieces: posts and a lintel, boxes of the frame's mesh) came out as
slabs or posts: 30,389 boxes in 64 tiles. The tiles' parts (tiles/<tag>/build/<tile>.npz) are right; the shape each
box was drawn with is recomputed from them, the same way.

--fix doors: door_test's floor check sampled boxes every 0.1 m and missed a floor plate of no thickness (since
fixed: the segment against each box), so doors were kept shut "no floor" (Vic's). Each such door is decided again
from the tile's part alone (its triangles, its boxes, and the closed doors' leaf boxes, which the build appends last
in the order of their keys); a door that now opens has its leaves' brushes switched off and nothing given back for
them. Step scene: those leaves' instances are hidden in the tile's t_<tile>.xscn as the kit serves it (its records
are the tag's scene part's instances in order, checked by count and transform; hidden, not removed, so every count
and index holds), and the file goes in the dev kit as home/base/zz_<map>_scene_zz_zzzdoors.pk3 (stored, read after
every scene overlay).

Idle priority. Logs and the small chunks' glbs in tiles/dev-<name>/. Never a numbered kit; nothing installed.
Converted levels are for local play only."""
import argparse
import json
import os
import re
import struct
import sys
import time
import zipfile
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from types import SimpleNamespace

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import colzbsp  # noqa: E402
import cp2gltf  # noqa: E402
import gltfio  # noqa: E402
import levelport as lp  # noqa: E402
import pk3write  # noqa: E402
from patch_tiles import idle, run  # noqa: E402

PY = sys.executable
NLUMPS = 18                                 # RBSP (JKA): 18 lumps after the 8-byte header
L_SHADERS, L_MODELS, L_BRUSHES, L_BRUSHSIDES, L_PLANES = 1, 7, 8, 9, 2
OFF_SHADER = b'levelport/collision_off'     # contents 0: collides with nothing; never drawn (a brush is not a surface)
MATCH_U = 1.0     # u: a rotated box's brush bounds land up to ~0.5 u off its exact ones (q3map2's axial bevels)
CENTRE, FLOOR = (-1280.0, 2240.0), 12.0      # Watson's area frame (CP2077 m): the parts' glTF origin


def lumps(data):
    return [list(struct.unpack_from('<ii', data, 8 + 8 * i)) for i in range(NLUMPS)]


def brush_bounds(data):
    """Model 0's brushes: (index into the brush lump, bounds (n, 6) from the axial sides, shader index)."""
    L = lumps(data)

    def arr(i, dt):
        o, n = L[i]
        return np.frombuffer(data, dt, n // np.dtype(dt).itemsize, o)
    model = arr(L_MODELS, np.dtype([('mins', '<f4', 3), ('maxs', '<f4', 3), ('fsurf', '<i4'), ('nsurf', '<i4'),
                                    ('fbrush', '<i4'), ('nbrush', '<i4')]))[0]
    br = arr(L_BRUSHES, np.dtype([('fs', '<i4'), ('ns', '<i4'), ('shader', '<i4')]))
    sd = arr(L_BRUSHSIDES, np.dtype([('plane', '<i4'), ('shader', '<i4'), ('surf', '<i4')]))
    pl = arr(L_PLANES, np.dtype([('n', '<f4', 3), ('d', '<f4')]))
    idx = np.arange(model['fbrush'], model['fbrush'] + model['nbrush'])
    b = br[idx]
    ns = b['ns'].astype(np.int64)
    side = np.repeat(b['fs'].astype(np.int64), ns) + (np.arange(int(ns.sum())) - np.repeat(np.r_[0, np.cumsum(ns)[:-1]], ns))
    owner = np.repeat(np.arange(len(b)), ns)
    p = pl[sd['plane'][side]]
    n, d = p['n'].astype(np.float64), p['d'].astype(np.float64)
    bb = np.full((len(b), 6), np.nan)
    for ax in range(3):
        pos, neg = np.abs(n[:, ax] - 1) < 1e-6, np.abs(n[:, ax] + 1) < 1e-6
        bb[owner[pos], ax + 3] = d[pos]
        bb[owner[neg], ax] = -d[neg]
    return idx, bb, b['shader'].copy()


def switch_off(data, brushes):
    """The BSP with these brush-lump indices pointed at OFF_SHADER (a shader lump entry appended at the file's end;
    the old lump's bytes stay where they were, unreferenced)."""
    L = lumps(data)
    o, n = L[L_SHADERS]
    shaders = data[o:o + n]
    off = n // 72
    new_sh = shaders + struct.pack('<64sii', OFF_SHADER, 0, 0)
    buf = bytearray(data)
    pad = (-len(buf)) % 4
    buf += b'\0' * pad
    L[L_SHADERS] = [len(buf), len(new_sh)]
    buf += new_sh
    for i, (lo, ln) in enumerate(L):
        struct.pack_into('<ii', buf, 8 + 8 * i, lo, ln)
    bo = L[L_BRUSHES][0]
    for k in brushes:
        struct.pack_into('<i', buf, bo + 12 * int(k) + 8, off)
    return bytes(buf)


def boxes_aabb_q3(lh, G, offset):
    """Part boxes (local lo/hi (n, 6), G (n, 4, 4) into the area's glTF frame) as bounds in the chunk's frame (game
    units), (n, 6)."""
    sel = np.array([[x, y, z] for x in (0, 3) for y in (1, 4) for z in (2, 5)])
    c = lh[:, sel.ravel()].reshape(-1, 8, 3)
    W = np.einsum('nij,nkj->nki', G[:, :3, :3], c) + G[:, None, :3, 3] - offset
    q = lp.gltf_to_q3(W)
    return np.concatenate([q.min(1), q.max(1)], 1)


def box_shapes(z):
    """--fix box-shapes: per box, the bounds stage_chunks drew it with (the first box of its mesh path in the tile)
    and whether that differs from its own."""
    first, drawn, wrong = {}, [], []
    for dep, lh in zip(z['box_depot'], z['box_lohi']):
        dep = str(dep)
        f = first.setdefault(dep, lh)
        drawn.append(f)
        wrong.append(not np.array_equal(f, lh))
    return np.array(drawn), np.array(wrong)


def reopen_doors(z, plan):
    """--fix doors: the tile's doors kept shut 'no floor' decided again with door_test as it is now: (keys that open,
    indices of their leaf boxes), or None with why when the part's trailing boxes are not its closed doors' leaves."""
    closed = json.loads(str(z['door_closed'])) if 'door_closed' in z.files else {}
    if not any(why == 'no floor' for why in closed.values()):
        return [], []
    leaves = defaultdict(list)
    for q in plan:
        k = cp2gltf.door_leaf(q)
        if k in closed:
            leaves[k].append(q['mesh'])
    # the build appends each closed door's leaves (sorted keys) after every other box, less the leaves the plan's
    # size cut dropped (a handle): read back from the end, a door's run is boxes of its leaves' meshes near its entity
    dep = [str(d) for d in z['box_depot']]
    G, lh = z['box_G'].astype(np.float64), z['box_lohi']
    span, i = {}, len(dep)
    for k in sorted(closed, reverse=True):
        left = list(leaves[k])
        pos = np.array([float(v) for v in k.rsplit('|', 1)[1].split(',')])
        run = []
        while i > 0 and dep[i - 1] in left:
            c = (G[i - 1] @ np.r_[(lh[i - 1][:3] + lh[i - 1][3:]) / 2, 1.0])[:3]
            cp = np.array([c[0] + CENTRE[0], -c[2] + CENTRE[1], c[1] + FLOOR])
            if np.linalg.norm(cp - pos) > 4.0:
                break
            left.remove(dep[i - 1])
            i -= 1
            run.append(i)
        if not run:
            return None, "no leaf box for the closed door %s" % k
        span[k] = run
    start = i
    T = z['col'].astype(np.float64)
    tmin, tmax = (T.min(1), T.max(1)) if len(T) else (np.zeros((0, 3)), np.zeros((0, 3)))
    boxes = [(dep[j], lh[j][:3], lh[j][3:], G[j]) for j in range(start)]
    opened, idx = [], []
    for k in sorted(closed):
        lv = [(None, SimpleNamespace(lo=lh[j][:3], hi=lh[j][3:]), G[j]) for j in span[k]]
        ok, why = cp2gltf.door_test(T, tmin, tmax, boxes, lv)
        if closed[k] == 'blocked':
            # (the blocked test is unchanged: the same answer says the part was read back as the build saw it)
            if why != 'blocked':
                return None, 'the blocked door %s reads back %s' % (k, why)
            continue
        if ok:
            opened.append(k)
            idx += span[k]
    return opened, idx


def plan_tile(z, offset, data, remove=()):
    """Brushes to switch off and boxes to give back, for one tile: (brush-lump indices, box indices, unmatched,
    boxes drawn wrong). remove: box indices that go without coming back (opened doors' leaves)."""
    if not len(z['box_depot']):
        return [], [], 0, 0
    drawn, wrong = box_shapes(z)
    remove = set(remove)
    wrong = wrong.copy()
    wrong[list(remove)] = True
    from scipy.spatial import cKDTree
    G = z['box_G'].astype(np.float64)
    idx, bb, _ = brush_bounds(data)
    ok = np.nonzero(~np.isnan(bb).any(1))[0]
    drawn_bb = boxes_aabb_q3(drawn, G, offset)
    # a box is the brushes whose six bounds are its own within MATCH_U (the largest coordinate difference)
    tree = cKDTree(bb[ok])
    wk = np.nonzero(wrong)[0]
    hits = tree.query_ball_point(drawn_bb[wk], MATCH_U, p=np.inf) if len(wk) else []
    off = sorted({int(ok[i]) for h in hits for i in h})
    unmatched = sum(1 for h in hits if not h)
    # every wrong box comes back with its own shape, matched or not (an unmatched one's brush, if q3map2 cut it, stays
    # as the base kit had it), and so does every box whose drawn brush is switched off (siblings drawn alike, and any
    # other mesh's box that happened to share the brush's bounds)
    back = set(int(k) for k in wk) - remove
    if off:
        near = cKDTree(bb[off]).query_ball_point(drawn_bb, MATCH_U, p=np.inf)
        back |= {k for k, h in enumerate(near) if h and k not in remove}
    return sorted(int(idx[i]) for i in off), sorted(back), unmatched, int(wrong.sum()) - len(remove)


def small_glb(path, z, back, d, tile, origin_cp, area, tag):
    """The boxes to give back as a chunk glb in the tile's frame (stage_chunks' box nodes)."""
    w = gltfio.Writer()
    g_col = w.node('collision')
    mi, cache = w.material('collision_box', extras={'surfaceparms': []}), {}
    for k in back:
        depot, lh = str(z['box_depot'][k]), z['box_lohi'][k]
        lo, hi = lh[:3], lh[3:]
        key = (depot, tuple(lh.tolist()))
        if key not in cache:
            c = np.array([[x, y, zz] for y in (lo[1], hi[1]) for x, zz in
                          ((lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2]))])
            cache[key] = w.mesh('box_%d' % len(cache), [{'position': c, 'indices': cp2gltf.BOX_FACES, 'material': mi}])
        G = z['box_G'][k].astype(np.float64).copy()
        G[:3, 3] += d
        w.node('b%d' % k, mesh=cache[key], matrix=G, parent=g_col, extras={'lp': 'collision', 'contents': ['solid', 'detail']})
    w.save(path, extras={'levelport': {'version': 1, 'units_per_metre': cp2gltf.gltf_upm(), 'source_game': 'cyberpunk_2077',
                                       'source': 'patch boxes of chunk %s of %s, tiles %s' % (tile, area, tag),
                                       'chunk': {'tile': tile, 'origin_cp_m': origin_cp},
                                       'worldspawn': {'message': 'Night City: chunk %s patch' % tile}}})


def tile_job(job):
    """One tile: its plan, its chunk with the brushes switched off (a pk3 of its own), the boxes given back compiled
    alone into chunk_<tile>_p (in the same pk3). (tile, summary) or (tile, None) when nothing changes."""
    idle()
    t, served, row, src, tdir, fixes, area, tag = job
    out = tdir / 'compiled'
    if served is None:
        print('  %s: no chunk in the base kit (empty tile)' % t, flush=True)
        return t, None
    pk, n = served
    with zipfile.ZipFile(pk) as zf:
        data = colzbsp.member_bsp(zf, n)        # (XBSZ in a kit unless built --plain; the patched chunk is plain)
    offset = np.array(row['offset_in_area_gltf_m'], np.float64)
    with np.load(src / 'build' / (t + '.npz'), allow_pickle=True) as z:
        opened, remove = [], []
        if 'doors' in fixes:
            opened, remove = reopen_doors(z, cp2gltf.read_records(src / (t + '.json')))
            if opened is None:
                raise SystemExit('patch_chunks: %s: doors: %s' % (t, remove))
        brushes, back, unmatched, nwrong = plan_tile(z, offset, data, remove)
        if not brushes:
            print('  %s: %d boxes drawn wrong, nothing to switch off' % (t, nwrong), flush=True)
            return t, None
        small_glb(tdir / 'chunks' / (t + '_p.glb'), z, back, -offset, t, row['origin_cp_m'], area, tag)
    run([PY, '-u', str(HERE / 'gltf2map.py'), str(tdir / 'chunks' / (t + '_p.glb')), 'chunk_%s_p' % t, '--out',
         str(out / (t + '_p')), '--col-chunk', '--exact-planes', '--scratch', str(tdir / 'scratch' / t)],
        tdir / 'logs' / ('%s_p.compile.log' % t))
    with zipfile.ZipFile(out / (t + '_p') / ('zz_chunk_%s_p.pk3' % t)) as zf:
        small = zf.read('maps/chunk_%s_p.bsp' % t)
    (out / t).mkdir(parents=True, exist_ok=True)
    tmp = out / t / ('zz_chunk_%s.tmp' % t)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as zf:
        zf.writestr(n, switch_off(data, brushes))
        zf.writestr('maps/chunk_%s_p.bsp' % t, small)
    os.replace(tmp, out / t / ('zz_chunk_%s.pk3' % t))
    return t, {'wrong': nwrong, 'switched_off': len(brushes), 'given_back': len(back), 'unmatched': unmatched,
               'doors_opened': opened, 'leaf_boxes_removed': len(remove)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('name')
    ap.add_argument('--base-kit', type=Path, required=True)
    ap.add_argument('--tag', default='8')
    ap.add_argument('--tiles', default='', help='comma-separated (default: every tile the fix changes)')
    ap.add_argument('--fix', default='box-shapes,doors', help='box-shapes, doors (comma-separated)')
    ap.add_argument('--area', default='watson')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--steps', default='plan,kit,scene')
    ap.add_argument('--jobs', type=int, default=3, help='tiles at once (about 1.5 GB each)')
    ap.add_argument('--full-pack', action='store_true', help='patch_kit.py --full-pack: a whole new chunk pack '
                    '(a patch pack over 4 GB cannot be read)')
    a = ap.parse_args()
    idle()
    if not re.fullmatch(r'[a-z0-9][a-z0-9_.-]*', a.name) or re.fullmatch(r'\d+[a-z]?', a.name):
        sys.exit('patch_chunks: a dev name is lower-case letters, digits, . _ - (not a tag number)')
    work = cp2gltf.work_dir(a.area)
    src = cp2gltf.tile_dir(work, a.tag)
    man = json.loads((src / 'chunks' / 'manifest.json').read_text(encoding='utf-8'))
    rows = {c['tile']: c for c in man['chunks']}
    tdir = cp2gltf.tile_dir(work, 'dev-' + a.name)
    out = tdir / 'compiled'
    (tdir / 'logs').mkdir(parents=True, exist_ok=True)
    (tdir / 'chunks').mkdir(parents=True, exist_ok=True)
    tiles = [t for t in a.tiles.split(',') if t] or list(rows)
    fixes = set(a.fix.split(','))
    if fixes - {'box-shapes', 'doors'} or not fixes:
        sys.exit('patch_chunks: --fix is box-shapes, doors')
    if 'box-shapes' not in fixes:
        sys.exit("patch_chunks: the base kit's chunks are drawn by stage_chunks as it was: --fix doors needs box-shapes")
    steps = a.steps.split(',')
    hb = a.base_kit / 'home' / 'base'
    packs = sorted(hb.glob('zz_%s_colchunks*.pk3' % a.map), key=lambda p: p.name.upper())
    serve = {}
    for pk in packs:
        with zipfile.ZipFile(pk) as zf:
            for n in zf.namelist():
                serve[n.lower()] = (pk, n)
    summary = {}
    if 'plan' in steps:
        t0 = time.time()
        jobs = [(t, serve.get('maps/chunk_%s.bsp' % t), rows[t], src, tdir, fixes, a.area, a.tag) for t in tiles]
        with ProcessPoolExecutor(a.jobs) as ex:
            for t, s in ex.map(tile_job, jobs):
                if s is None:
                    continue
                summary[t] = s
                print('  %s: %d boxes drawn wrong, %d doors opened (%d leaf boxes), %d brushes switched off (%d boxes '
                      'matched none), %d boxes given back, compiled (%.0f s)' % (
                          t, s['wrong'], len(s['doors_opened']), s['leaf_boxes_removed'], s['switched_off'],
                          s['unmatched'], s['given_back'], time.time() - t0), flush=True)
        (tdir / 'patch_chunks.json').write_text(json.dumps({'fix': a.fix, 'tag': a.tag, 'base_kit': str(a.base_kit),
                                                            'tiles': summary}, indent=1), encoding='utf-8')
    else:
        summary = json.loads((tdir / 'patch_chunks.json').read_text(encoding='utf-8'))['tiles']
    if 'kit' in steps:
        run([PY, '-I', str(HERE / 'patch_kit.py'), str(a.base_kit), a.name, '--builds', str(out), '--manifest',
             str(src / 'chunks' / 'manifest.json'), '--tiles=' + ','.join(summary), '--map', a.map]
            + (['--full-pack'] if a.full_pack else []) + ['--note',
             'patch_chunks.py --fix %s from tag %s: brushes switched off in %d chunks, their boxes in chunk_<tile>_p'
             % (a.fix, a.tag, len(summary))], tdir / 'logs' / 'kit.log')

    if 'scene' in steps:
        opened = {t: s['doors_opened'] for t, s in summary.items() if s.get('doors_opened')}
        kit = a.base_kit.parent / ('%s_dev-%s' % (a.map, a.name))
        if opened:
            scene_patch(kit, src, opened, a.map, tdir / 'logs' / 'scene.log')


INST = np.dtype([('W', '<f4', 12), ('mi', '<u4'), ('near', '<f4'), ('slot', '<u4'), ('hide', '<f4')])
HIDDEN = 0.001          # a hide distance (units) no eye is inside: the instance is never drawn


def scene_patch(kit, src, opened, mapname, log):
    """The opened doors' leaves hidden in their tiles' t_<tile>.xscn, in a scene patch pack in the dev kit."""
    hb = kit / 'home' / 'base'
    serve = {}
    for pk in sorted(hb.glob('zz_%s_scene*.pk3' % mapname), key=lambda q: q.name.upper()):
        with zipfile.ZipFile(pk) as zf:
            for n in zf.namelist():
                serve[n.lower()] = (pk, n)
    out, lines = {}, []
    for t, keys in opened.items():
        name = 'maps/%s/t_%s.xscn' % (mapname, t)
        pk, n = serve[name.lower()]
        with zipfile.ZipFile(pk) as zf:
            b = zf.read(n)
        _, version, counts, offs = cp2gltf.xscn_head_of(b)
        sd = src / 'scene' / t
        ar = np.load(sd / 'arrays.npz', allow_pickle=True)
        mk = json.loads((sd / 'part.json').read_text(encoding='utf-8'))['meshkeys']
        rec = np.frombuffer(b, INST, counts[6], offs[6]).copy()
        if counts[6] != len(ar['inst_W']) or not np.allclose(rec['W'].reshape(-1, 3, 4), ar['inst_W'], atol=1e-2):
            sys.exit("patch_chunks: %s: %s is not the tag's scene part (%d instances, part %d)" % (
                t, name, counts[6], len(ar['inst_W'])))
        drop = []
        for q in cp2gltf.read_records(src / (t + '.json')):
            if cp2gltf.door_leaf(q) in keys:
                pos = np.array(q['m'])[[3, 7, 11]]
                for j in np.nonzero(np.linalg.norm(ar['placed_pos'] - pos, axis=1) < 0.01)[0]:
                    if q['mesh'].replace(chr(92), chr(92) * 2) in mk[ar['inst_mesh'][j]]:
                        drop.append(int(j))
        # the leaves hidden, not removed: every record keeps its place, so the manifest's counts (maps/<map>.xman,
        # which the renderer checks) and the global instance indices of .xrot and the ads tables still hold (removing
        # them left all three wrong)
        rec['hide'][drop] = HIDDEN
        rec['near'][drop] = 0.0
        ends = offs[1:] + [len(b)]
        body = [b[o:e] for o, e in zip(offs, ends)]
        body[6] = rec.tobytes()
        out[name] = cp2gltf.xscn_rebuild(version, list(zip(counts, body)))
        lines.append('%s: %d leaf instances hidden (%s)' % (name, len(drop), ', '.join(keys)))
    # named to sort after every scene overlay (uppercased "_ZZZ"; the shapes, ads, foliage, holograms and Q307 packs
    # each carry tiles), and stored: the renderer's scene streaming reads tiles by range from stored entries only
    pk = hb / ('zz_%s_scene_zz_zzzdoors.pk3' % mapname)
    with pk3write.writing(pk, zipfile.ZIP_STORED) as zf:
        for name, data in out.items():
            zf.writestr(name, data)
    # KIT.txt: this pack's section replaced, not appended again on a rerun of the step
    head = "Scene patch: home/base/%s (patch_chunks.py): the opened doors' leaves hidden in\n" % pk.name
    kt = kit / 'KIT.txt'
    old = kt.read_text(encoding='utf-8').splitlines(keepends=True) if kt.exists() else []
    keep, skip = [], False
    for ln in old:
        if ln == head:
            skip = True
            continue
        if skip and ln.startswith('  '):
            continue
        skip = False
        keep.append(ln)
    kt_tmp = kt.with_name('KIT.txt.tmp')        # (a new file: never written through a hard link to the base kit's)
    kt_tmp.write_text(''.join(keep) + head + ''.join('  %s\n' % s for s in lines), encoding='utf-8')
    os.replace(kt_tmp, kt)
    log.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('  scene: %s' % '; '.join(lines), flush=True)


if __name__ == '__main__':
    main()
