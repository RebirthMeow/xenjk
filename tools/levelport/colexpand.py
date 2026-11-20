"""colexpand.py -- collision option A's expansion: a kit's slab pack (zz_<map>_colslabs.pk3: one
colpack package a chunk, maps/chunk_<tile>.slabs, and maps/<map>.colslabs.json: the placed tiles, their origins and
brush counts) into the chunk packs cp77_collision.py writes, byte for byte the same chunk BSPs (as XBSZ, collision option C's installed
form, unless --plain): each chunk's records
rebuilt from its package (colpack.read, the slabs on --jobs processes), cooked (colcook: q3map2 -brushbin, the tree,
slim), its BSP raw-copied into the open chunk pack as it ends (cp77_collision.PackWriter), the manifest and the cfg
last. Run once, at install or first launch; it needs Python with numpy, q3map2 build xenjk9 and the game's GameData.

    python -I tools/levelport/colexpand.py all <slab pack> <out dir> --q3map2 <xenjk9 q3map2.exe>
                                           [--workers 3] [--jobs 4] [--compare <packs>] [--tiles a,b]
    python -I tools/levelport/colexpand.py chunk <slab pack> <out pk3> --tile=<tile> --q3map2 <exe> [--jobs 4]

Starts wait only for memory (cp77_collision's S1 gate): a chunk's peak taken as 2.5 GB + 2.5 GB a million brushes
(-6_0's expansion, --jobs 4: 8.9 GB for 2.58 M). --compare <packs>: each chunk's BSP checked against a kit's members (size, CRC and
bytes) instead of packed; compare.json at the end. Converted levels are for local play only."""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
PY = sys.executable
GAME = Path(os.environ.get('XENJK_GAMEDATA') or HERE.parent.parent / 'game_directory' / 'Jedi Academy' / 'GameData')  # gltf2map's


def peak_gb(brushes):
    return 2.5 + 2.5 * brushes / 1e6


def slab_member(pack, tile):
    with zipfile.ZipFile(pack) as z:
        return z.read('maps/chunk_%s.slabs' % tile)


def pack_index(pack, mapname):
    with zipfile.ZipFile(pack) as z:
        return json.loads(z.read('maps/%s.colslabs.json' % mapname).decode('utf-8'))


def expand_chunk(pack, tile, q3map2, jobs=4, log=None):
    """one chunk's BSP (bytes) and a report"""
    import colcook
    import colpack
    t0 = time.time()
    blob = slab_member(pack, tile)
    with ProcessPoolExecutor(jobs) as ex:
        r, head = colpack.read(blob, ex)
    t1 = time.time()
    with tempfile.TemporaryDirectory(prefix='colexpand_%s_' % tile) as td:
        stage = Path(td)
        for rel, text in head['scripts'].items():
            f = (stage / 'base' / rel).resolve()
            if (stage / 'base').resolve() not in f.parents:
                sys.exit('colexpand: %s: a script path leaves the stage: %r' % (tile, rel))
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(text, encoding='utf-8')
        q = [str(q3map2), '-bsp', '-game', 'ja', '-fs_basepath', str(GAME), '-fs_basepath', str(stage), '-meta',
             '-exactplanes', '-pointgrid', '16384', str(stage / 'base' / 'maps' / (head['name'] + '.map'))]
        data, rep = colcook.cook([r], dict(head['worldspawn']), q, head['tile_brushes'], head['slim_snap'], log,
                                 str(Path(q3map2).parent))
    rep['seconds_records'] = round(t1 - t0, 1)
    rep['seconds'] = round(time.time() - t0, 1)
    rep['package_bytes'] = len(blob)
    return data, rep


def cmd_chunk(a):
    import hashlib
    data, rep = expand_chunk(a.pack, a.tile, a.q3map2, a.jobs, a.log)
    name = 'maps/chunk_%s.bsp' % a.tile
    out = Path(a.out)
    want = pack_index(a.pack, a.map).get('bsp', {}).get(a.tile)
    got = hashlib.sha256(data).hexdigest()
    rep['verified'] = None if want is None else (want['bsp_sha256'] == got and want['bsp_bytes'] == len(data))
    if want is not None and not rep['verified']:
        out.with_suffix('.json').write_text(json.dumps({'tile': a.tile, 'verified': False, 'sha256': got,
                                                        'want': want}, indent=1), encoding='utf-8')
        sys.exit('colexpand: %s does not expand to the BSP its pack records (%s, not %s)' % (
            a.tile, got[:16], want['bsp_sha256'][:16]))
    if a.compare:
        ref = None
        for pk in a.compare.split(','):
            with zipfile.ZipFile(pk) as z:
                if name in z.namelist():
                    ref = z.read(name)
                    break
        res = {'tile': a.tile, 'identical': ref is not None and data == ref, 'bytes': len(data),
               'ref_bytes': None if ref is None else len(ref), 'crc': zlib.crc32(data),
               'report': {k: v for k, v in rep.items() if k != 'read_kept'}}
        out.with_suffix('.json').write_text(json.dumps(res, indent=1), encoding='utf-8')
        print('expand: %s %s, %d bytes, %.0f s' % (a.tile, 'IDENTICAL' if res['identical'] else 'DIFFERENT',
                                                  len(data), rep['seconds']))
        return 0 if res['identical'] else 1
    tmp = out.with_suffix('.tmp')
    t0 = time.time()
    if a.plain:
        member = data
    else:                                # collision option C's form: XBSZ, stored (colzbsp.py)
        import colzbsp
        member = colzbsp.encode(data, a.xbsz_level)
        if colzbsp.decode(member) != data:
            sys.exit('colexpand: %s: the XBSZ does not decode to the BSP' % a.tile)
    rep['seconds_xbsz'] = round(time.time() - t0, 1)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        if a.plain:
            z.writestr(name, member)
        else:
            z.writestr(zipfile.ZipInfo(name, time.localtime()[:6]), member, zipfile.ZIP_STORED)
    with zipfile.ZipFile(tmp) as z:
        if z.testzip() is not None or z.getinfo(name).file_size != len(member):
            sys.exit('colexpand: %s does not read back' % tmp)
    tmp.replace(out)
    out.with_suffix('.json').write_text(json.dumps({'tile': a.tile, 'bytes': len(data), 'crc': zlib.crc32(data),
                                                    'verified': rep['verified'],
                                                    'report': {k: v for k, v in rep.items() if k != 'read_kept'}},
                                                   indent=1), encoding='utf-8')
    print('expand: %s, %d bytes, %.0f s' % (a.tile, len(data), rep['seconds']))
    return 0


def cmd_all(a):
    import cp77_collision as C
    import psutil
    idx = pack_index(a.pack, a.map)
    placed = [(t, o) for t, o in idx['placed']]
    if a.tiles:
        want = set(a.tiles.split(','))
        placed = [(t, o) for t, o in placed if t in want]
    out = Path(a.out).resolve()
    part = out.with_name(out.name + '.partial') if not a.compare else out
    for sub in ('state', 'chunks', 'stopped', 'logs'):
        (part / sub).mkdir(parents=True, exist_ok=True)
    logf = part / 'expand.log'
    C.log('== colexpand %s -> %s (%d chunks, %d workers, --jobs %d%s)' % (
        a.pack, out, len(placed), a.workers, a.jobs, ', compare' if a.compare else ''), logf)
    packs = None if a.compare else C.PackWriter(part, a.map, logf)
    order = sorted(placed, key=lambda to: -idx['brushes'][to[0]])
    gate, lock = threading.Lock(), threading.Lock()
    running = {}
    stop = threading.Event()
    results = {}
    t_all = time.time()

    def outstanding():
        out_ = 0.0
        with lock:
            runs = list(running.values())
        for need, pid in runs:
            used = 0.0
            if pid is not None:
                try:
                    root = psutil.Process(pid)
                    for q in [root] + root.children(recursive=True):
                        try:
                            used += q.memory_info().private / 2 ** 30
                        except psutil.Error:
                            pass
                except psutil.Error:
                    pass
            out_ += max(0.0, need - used)
        return out_

    def one(to):
        t, o = to
        if stop.is_set():
            return
        if packs is not None and packs.packed(t):
            return
        need = peak_gb(idx['brushes'][t])
        with gate:
            C.free_ok('chunk %s' % t)
            while True:
                avail, commit = C.memory()
                out_ = outstanding()
                if avail - out_ >= need + C.MARGIN_GB and commit - out_ >= need + C.MARGIN_GB:
                    break
                with lock:
                    alone = not running
                if alone:
                    break
                time.sleep(C.POLL_S)
            with lock:
                running[t] = [need, None]
        dst = part / 'chunks' / ('zz_chunk_%s.pk3' % t)
        cmd = [PY, '-I', str(Path(__file__).resolve()), 'chunk', a.pack, str(dst), '--tile=' + t, '--map', a.map,
               '--q3map2', a.q3map2,
               '--jobs', str(a.jobs), '--log', str(part / 'logs' / ('%s.q3map2.log' % t)),
               '--xbsz-level', str(a.xbsz_level)] + (['--plain'] if a.plain else [])
        if a.compare:
            cmd += ['--compare', a.compare]
            res_json = dst.with_suffix('.json')
            if res_json.exists():           # (an earlier run's result is never read as this one's)
                os.replace(res_json, res_json.with_suffix('.prev.json'))
        t0 = time.time()
        try:
            with open(part / 'logs' / ('%s.log' % t), 'w', encoding='utf-8') as f:
                p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT)
                with lock:
                    running[t][1] = p.pid
                rc = p.wait()
        finally:
            with lock:
                running.pop(t, None)
        if rc and not (a.compare and rc == 1 and dst.with_suffix('.json').exists()):
            stop.set()
            C.log('chunk %s FAILED (exit %d): %s' % (t, rc, part / 'logs' / ('%s.log' % t)), logf)
            return
        res = json.loads(dst.with_suffix('.json').read_text(encoding='utf-8'))
        results[t] = res
        C.log('chunk %s: %s in %.0f s (at its start available %.1f GB, %.1f GB reserved, its peak taken as %.1f GB)' % (
            t, ('identical' if res.get('identical') else 'DIFFERENT') if a.compare else 'expanded', time.time() - t0,
            avail, out_, need), logf)
        if packs is not None:
            packs.add(t, dst, o)

    with ThreadPoolExecutor(a.workers) as ex:
        list(ex.map(one, order))
    seconds = round(time.time() - t_all)
    if stop.is_set():
        sys.exit('colexpand: a chunk failed; a rerun resumes')
    if a.compare:
        bad = [t for t, _ in placed if not results.get(t, {}).get('identical')]
        rep = {'chunks': len(placed), 'identical': len(placed) - len(bad), 'not_identical': bad, 'seconds': seconds,
               'chunk_seconds': {t: results[t]['report']['seconds'] for t in results}}
        (part / 'compare.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
        C.log('compare: %d of %d identical %s, %d s' % (rep['identical'], len(placed), bad[:10], seconds), logf)
        return 0 if not bad else 1
    pks, size = packs.finish(placed, idx['note'])
    C.log('packs: %s, %d chunks, %.0f MB of BSP, %d s' % (', '.join(p.name for p in pks), len(placed), size / 1e6,
                                                         seconds), logf)
    verified = [t for t, _ in placed if results.get(t, {}).get('verified')]
    C.log('verified against the pack\'s hashes: %d of %d' % (len(verified), len(placed)), logf)
    part.rename(out)
    if a.drop_package:
        if a.tiles or len(verified) != len(idx['placed']):
            C.log('the slab pack is kept: %d of %d chunks verified%s' % (
                len(verified), len(idx['placed']), ' (a --tiles expansion)' if a.tiles else ''), out / 'expand.log')
        else:
            drop_package(Path(a.pack), Path(a.drop_package), out / 'expand.log')
    return 0


def drop_package(pack, trash, logf):
    """the slab pack moved (never deleted) to <trash>/<stamp>-<name>/ with MANIFEST.tsv; only a person empties it"""
    import cp77_collision as C
    import hashlib
    import shutil
    dst = trash / ('%s-%s' % (time.strftime('%Y%m%dT%H%M%S'), pack.stem))
    dst.mkdir(parents=True)
    h = hashlib.sha256()
    with open(pack, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    size = pack.stat().st_size
    shutil.move(str(pack), str(dst / pack.name))
    if (dst / pack.name).stat().st_size != size:
        sys.exit('colexpand: the moved slab pack is not its size')
    (dst / 'MANIFEST.tsv').write_text('kind\tbytes\tsha256\toriginal\nslab pack, expanded and verified\t%d\t%s\t%s\n' % (
        size, h.hexdigest(), pack), encoding='utf-8')
    C.log('slab pack moved to %s (every chunk verified)' % dst, logf)


def cmd_hashes(a):
    """a slab pack made before its index held the BSPs' hashes: the hashes from a kit's chunk packs (the BSPs it
    expands to, checked identical elsewhere), the index rewritten"""
    import hashlib
    import colbsp
    idx = pack_index(a.pack, a.map)
    ref = {}
    for pk in a.ref.split(','):
        with zipfile.ZipFile(pk) as z:
            for n in z.namelist():
                if n.startswith('maps/chunk_') and n.endswith('.bsp'):
                    ref[n[len('maps/chunk_'):-4]] = pk
    idx['bsp'] = {}
    for t, _ in idx['placed']:
        with zipfile.ZipFile(ref[t]) as z:
            data = colbsp.plain(z.read('maps/chunk_%s.bsp' % t))
        idx['bsp'][t] = {'bsp_sha256': hashlib.sha256(data).hexdigest(), 'bsp_bytes': len(data)}
    idx['bsp_from'] = 'reference packs: %s' % a.ref
    pack = Path(a.pack)
    tmp = pack.with_suffix('.tmp')
    name = 'maps/%s.colslabs.json' % a.map
    with zipfile.ZipFile(pack) as zi, zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as zo:
        for i in zi.infolist():
            if i.filename != name:
                zo.writestr(i, zi.read(i.filename))
        zo.writestr(name, json.dumps(idx, indent=1))
    with zipfile.ZipFile(tmp) as z:
        if z.testzip() is not None:
            sys.exit('colexpand: %s does not read back' % tmp)
    tmp.replace(pack)
    print('hashes: %d chunks into %s' % (len(idx['bsp']), pack))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('all')
    p.add_argument('pack')
    p.add_argument('out')
    p.add_argument('--map', default='cp_watson')
    p.add_argument('--q3map2', required=True)
    p.add_argument('--workers', type=int, default=3)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--compare')
    p.add_argument('--tiles', default='')
    p.add_argument('--plain', action='store_true', help='deflated BSPs, not XBSZ (collision option C, the default)')
    p.add_argument('--drop-package', metavar='TRASH',
                   help='after every chunk is verified against the pack\'s hashes, move the slab pack to TRASH/<stamp>-<name>/'
                        ' (kept if any chunk fails)')
    p.add_argument('--xbsz-level', type=int, default=19)
    p = sub.add_parser('chunk')
    p.add_argument('pack')
    p.add_argument('out')
    p.add_argument('--tile', required=True)
    p.add_argument('--q3map2', required=True)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--compare')
    p.add_argument('--log')
    p.add_argument('--map', default='cp_watson')
    p.add_argument('--plain', action='store_true')
    p.add_argument('--xbsz-level', type=int, default=19)
    p = sub.add_parser('hashes')
    p.add_argument('pack')
    p.add_argument('ref', help='the chunk packs of the BSPs the pack expands to, comma-separated')
    p.add_argument('--map', default='cp_watson')
    a = ap.parse_args()
    sys.exit({'all': cmd_all, 'chunk': cmd_chunk, 'hashes': cmd_hashes}[a.cmd](a))


if __name__ == '__main__':
    main()
