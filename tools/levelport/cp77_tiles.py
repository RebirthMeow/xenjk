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
"""cp77_tiles.py <area> <tag> -- a CP2077 area built through tiles (docs/levelport-watson.md, phase 1), after its
sectors, scan and fetch stages (cp77_area.sh runs them):

  tiles     cp2gltf.py tiles: the area's placements split into --tile-size squares, each owned by one tile
  parts     per tile, its scene part and its collision part, on --workers processes; a part is kept while its key
            (the tile's placements, the code its own stage runs (stagekey.py), the arguments, and for a scene part the
            opened doors among its own placements) is unchanged: an interrupted or repeated run builds only what is
            missing or out of date; --rebuild builds them all (before a numbered kit)
  merge     the scene (cp2gltf.py scene --merge) and the collision glb (build --merge), then gltf2map
  checks    levelcheck spawns, aerial, compare (per tile, stitched), collide, drop --engine
  summary   tiles/<tag>/summary.json and summary.md: per tile its placements, instances, materials, collision
            triangles, and the checks' holes and falls inside it

Outputs as cp77_area.sh: build/<map>-<tag>/, build/<map>_scene-<tag>/, look/levelcheck-<area>-<tag>/. Logs:
<work>/tiles/<tag>/logs/. Converted levels are local only."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import paths  # noqa: E402
import stagekey  # noqa: E402

PY = sys.executable
# collision: a large mesh as the coarsest LOD (of at most 4,000 triangles) that keeps LOD 0's walkable surfaces, and
# one drawn only in alpha-tested materials as those (Watson's four worst tiles' falls and voids
# 10,269 -> 2,998 m2 at 1.9x the brushes; with --col-tris 0, the coarsest LOD, before)
# and no height cap: collision wherever the level takes a player (a player fell through a highway
# deck above the old 16 m cap; CP2077 has none). 400 m is over Watson's tallest building, so the ring and lid close
# the area above everything (cap-2: the six cap tiles 2.34x the triangles at 64 m)
BUILD_ARGS = ['--no-render', '--min-size', '1', '--col-tris', '4000', '--col-walk-lod', '--col-cutout-large',
              '--col-prop-tris', '0', '--col-height', '400']


def log(tdir, msg):
    line = '%s %s' % (time.strftime('%H:%M:%S'), msg)
    print(line, flush=True)
    with open(tdir / 'run.log', 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def run(cmd, logf):
    with open(logf, 'w', encoding='utf-8') as f:
        return subprocess.run(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT).returncode


def link_tiling(work, prev, tdir, tcmd, prev_cmd):
    """the new tag's tiling as links of PREV's when PREV's tiles.key holds now (the same placements, fetches, code and
    arguments but the tag): tiles.json, every tile record, the ad pool; tiles.key written new. -> linked or not"""
    pk = prev / 'tiles.key'
    if not (tiles_done(prev) and pk.exists() and pk.read_text() == tiles_key(work, prev_cmd)):
        return False
    tdir.mkdir(parents=True, exist_ok=True)
    man = json.load(open(prev / 'tiles.json', encoding='utf-8'))
    for f in ['tiles.json', 'ad_pool.json'] + [t['id'] + '.json' for t in man['tiles']]:
        d = tdir / f
        if not d.exists():
            os.link(prev / f, d)
    (tdir / 'tiles.key').write_text(tiles_key(work, tcmd))
    return True


def link_parts(prev, tdir, jobs):
    """Each job whose part PREV built under the same key (part keys hold no tag) linked, not built:
    build/<tile>.npz, or the files of scene/<tile>/; its key file a new file. -> (the jobs left, linked count)"""
    left, n = [], 0
    for job in jobs:
        tid, kind, cmd, done, keyf, key = job
        pkf = prev / keyf.relative_to(tdir)
        src = prev / done.relative_to(tdir)
        if not (pkf.exists() and pkf.read_text() == key and src.exists()):
            left.append(job)
            continue
        if kind == 'build':
            done.parent.mkdir(parents=True, exist_ok=True)
            if not done.exists():
                os.link(src, done)
        else:
            done.parent.mkdir(parents=True, exist_ok=True)
            for f in src.parent.iterdir():
                if f.is_file() and f.name != keyf.name and not (done.parent / f.name).exists():
                    os.link(f, done.parent / f.name)
        keyf.parent.mkdir(parents=True, exist_ok=True)
        keyf.write_text(key)
        n += 1
    return left, n


# a part's reservation (GB) against its tile's placements, interpolated between these points: measured values
# (the largest scene parts 7-11 GB; collision parts of the largest tiles 3.7-4.5 GB, a
# one-placement part's process 0.7 GB; scene parts 1.5-2.1 GB at 1,382-3,149 placements and 3.2 GB at
# 6,460, build parts 0.8-1.1 and 1.4 at 6,460), each with some room above what was measured. The tenth and
# fortieth largest tiles (75,099 and 48,102 placements) carry the old steps' large values; above the last point it holds
PART_PEAKS = {'scene': ((1000, 1.8), (6500, 3.7), (48000, 7.0), (75000, 12.0)),
              'build': ((1000, 1.2), (6500, 1.7), (48000, 3.5), (75000, 5.0))}


def part_peak_gb(kind, placements):
    """a part's reservation (GB) by its tile's placements: 0.8 under the first point of PART_PEAKS, then linear between
    its points, then the last point's"""
    pts = PART_PEAKS['scene' if kind == 'scene' else 'build']
    if placements < pts[0][0]:
        return 0.8
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if placements < x1:
            return round(y0 + (y1 - y0) * (placements - x0) / (x1 - x0), 2)
    return pts[-1][1]


class PeakScale:
    """the reservations scaled to what this run's parts of a kind took: after SCALE_AFTER of them, each estimate times
    the largest measured peak / estimate seen so far, with SCALE_ROOM above it (never more than the table's, never under
    SCALE_FLOOR of it). The jobs start largest first, so the parts measured bound the smaller ones after them. (Tag 12's
    scene parts peaked at 2.1-2.8 GB on reservations of 7.7-8.7 and started one at a time with 14 GB free.)"""
    SCALE_AFTER, SCALE_ROOM, SCALE_FLOOR = 5, 1.3, 0.3

    def __init__(self):
        self.lock = threading.Lock()
        self.ratios = {}

    def add(self, kind, peak, est):
        if est > 0 and peak > 0:
            with self.lock:
                self.ratios.setdefault(kind, []).append(peak / est)

    def scale(self, kind):
        with self.lock:
            r = self.ratios.get(kind, [])
            if len(r) < self.SCALE_AFTER:
                return 1.0
            return min(1.0, max(self.SCALE_FLOOR, max(r) * self.SCALE_ROOM))


def source_key(extra, files=('cp2gltf.py', 'cp77mat.py', 'gltfio.py')):
    h = hashlib.sha1()
    for f in files:
        h.update((HERE / f).read_bytes())
    h.update(json.dumps(extra, sort_keys=True).encode())
    return h.hexdigest()[:16]


# the stage function each kind of part runs (cp2gltf.main dispatches to it): its key holds only the code it can reach,
# so a collision fix keeps the scene parts and the other way round
PART_STAGES = {'build': ['stage_build'], 'scene': ['stage_scene_tile']}


def part_key(kind, extra):
    """A tile part's key: its stage's code (stagekey.code_key over cp2gltf.py and the local modules it reaches) and
    the part's inputs."""
    if kind not in _CODE_KEYS:
        # once per process: the same for every tile (sources do not change during a run), and 0.5 s each, 268 s of a
        # 268-tile part_jobs
        _CODE_KEYS[kind] = stagekey.code_key(HERE / 'cp2gltf.py', PART_STAGES[kind])
    h = hashlib.sha1(_CODE_KEYS[kind].encode())
    h.update(json.dumps(extra, sort_keys=True).encode())
    return h.hexdigest()[:16]


_CODE_KEYS = {}


def fetch_identity():
    """What every fetched file is a function of besides its depot path (cp2gltf.fetch_identity)."""
    return cp2gltf.fetch_identity()


def presence(work, placements_file, records=None):
    """Which of a tile's meshes are fetched (glb, materials, every-LOD glb): a mesh fetched after a part was built
    changes its key, so exactly the tiles placing it rebuild (keys once missed fetched content)."""
    h = hashlib.sha1()
    meshes = sorted({p['mesh'] for p in (cp2gltf.read_records(placements_file) if records is None else records)})
    for m in meshes:
        stem = cp2gltf.mesh_stem(m)
        bits = [(work / 'meshes' / (stem + '.glb')).exists(), (work / 'meshes' / (stem + '.Material.json')).exists(),
                (work / 'meshes_lod' / (stem + '.glb')).exists()]
        h.update((m + ''.join('1' if b else '0' for b in bits) + '\n').encode())
    return h.hexdigest()[:16]


def tiles_key(work, cmd):
    """What the tiles stage reads: its code (stagekey), the scan's placement store and placements.json (proxies),
    the entity meshes, the mesh bounds cache, the fetch's record files (fetch_missing.txt and stage_records.json, which
    every fetch that ran rewrites: bounds and the ad pool come from the fetched meshes) and its arguments. One tag ran it
    four times, 9 min. (Not the meshes folders' times: NTFS moves a folder's time only for
    its direct children, and a fetch writes under meshes/base/...)"""
    h = hashlib.sha1(stagekey.code_key(HERE / 'cp2gltf.py', ['stage_tiles']).encode())
    h.update(json.dumps(cmd[3:]).encode())

    def stat(f):
        st = f.stat()
        h.update(('%s %d %d\n' % (f.name, st.st_size, st.st_mtime_ns)).encode())
    store = work / cp2gltf.PlacementStore.DIR
    for f in sorted(store.glob('*')) if store.is_dir() else ():
        stat(f)
    for f in ('placements.json', 'entity_meshes.json', 'mesh_bounds.json', 'fetch_missing.txt', 'stage_records.json'):
        if (work / f).exists():
            stat(work / f)
    return h.hexdigest()[:16]


# what a scene merge reads from the work folder besides the parts (cp2gltf.scene_light_records, scene_map_files): the
# lights and channel volumes (placements.json), the day/night table (light_time.py), the signal groups, and the
# stages' record file (a sectors, scan or fetch run that changed anything rewrites it: the haze volumes from the
# sector json, the entities' templates for their lights). A lighting change alone left the parts' keys and so the
# merged scene as they were
MERGE_INPUTS = ('placements.json', 'light_time.json', 'signal_groups.json', 'stage_records.json')


def merge_inputs(work):
    return [[f, (work / f).stat().st_size, (work / f).stat().st_mtime_ns] if (work / f).exists() else [f, None]
            for f in MERGE_INPUTS]


def tiles_done(tdir):
    """Whether a finished tiles run's files are all there: tiles.json, the ad pool and every tile's placements."""
    try:
        man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    except (OSError, ValueError):
        return False
    return (tdir / 'ad_pool.json').exists() and all((tdir / (t['id'] + '.json')).exists() for t in man['tiles'])


def part_jobs(work, tdir, a, ids, name, C, area):
    """The tile parts to build: (tile, kind, command, output, key file, key) for each part whose key differs from the
    one written beside it (or every part with a.rebuild), and every part's key by kind. Reads only."""
    jobs, part_keys = [], {'scene': [], 'build': []}
    pool = tdir / 'ad_pool.json'          # the parts' shared ad stand-ins (cp2gltf.py tiles)
    pool = pool.read_bytes() if pool.exists() else b''
    fid = fetch_identity()
    # the doors the collision parts opened: a scene part is keyed on the opened doors among its own placements (it
    # draws them without their leaves), so a door opened or shut rebuilds the scene parts holding it, not all of them
    opened = cp2gltf.door_open_keys(work, a.tag)
    import cp77_ads
    ah = hashlib.sha1()
    for f in (work / 'ad_index.json', cp77_ads.ADS_DIR / 'ads.json', cp77_ads.ADS_DIR / 'rendered' / 'manifest.json'):
        if f.exists():
            st = f.stat()
            ah.update(('%s %d %d\n' % (f.name, st.st_size, st.st_mtime_ns)).encode())
    ads_key = ah.hexdigest()[:16] if (work / 'ad_index.json').exists() else None
    admap = cp2gltf.ad_map(work) if ads_key else None
    rf = work / 'rotate_index.json'     # (the rotating meshes: cp77_rotate.py index)
    rot_key = '%d %d' % (rf.stat().st_size, rf.stat().st_mtime_ns) if rf.exists() else None
    sf = work / 'signal_groups.json'    # (a signal head's slot is its signal's rank in this table: cp2gltf Signals)
    sig_key = '%d %d' % (sf.stat().st_size, sf.stat().st_mtime_ns) if sf.exists() else None
    # what a tile's key reads from its records, kept by the files' sizes and times (every record read and
    # hashed twice a run, 2.4 min each): the record, the ad pool, the foliage, the fetch's record files (every fetch
    # rewrites them, so a newly fetched mesh recomputes presence), the ad index and manifests, the fetch identity
    def stamp_of(f):
        try:
            x = f.stat()
            return [x.st_size, x.st_mtime_ns]
        except OSError:
            return None
    # (and the code that derives them: presence, door_leaf, read_records, AdMap.image)
    code = [stagekey.code_key(HERE / 'cp77_tiles.py', ['presence']),
            stagekey.code_key(HERE / 'cp2gltf.py', ['door_leaf', 'read_records', 'mesh_stem']),
            stagekey.code_key(HERE / 'cp77_ads.py', ['AdMap'])]
    shared = [stamp_of(tdir / 'ad_pool.json'), fid, ads_key, code] + [stamp_of(work / f) for f in (
        'stage_records.json', 'fetch_missing.txt', 'entity_meshes.json')]
    cf = tdir / 'part_inputs.json'
    try:
        cache = json.loads(cf.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        cache = {}
    fresh = {}
    for tid in ids:
        tf = tdir / (tid + '.json')
        fol = tdir / 'foliage' / (tid + '.jsonl')
        stamp = [stamp_of(tf), stamp_of(fol)] + shared
        c = cache.get(tid)
        if c is None or c.get('stamp') != json.loads(json.dumps(stamp)):
            recs = cp2gltf.read_records(tf)
            fol_recs = cp2gltf.read_records(fol) if fol.exists() else []
            # the rendered ads this tile's screens show (cp77_ads.AdMap.image, as the part picks them), by size and
            # time: a re-render under the same file name rebuilds the tiles showing it (the key once
            # stat'd only the index and the manifests, so the old face stayed)
            c = {'stamp': stamp, 'pl': hashlib.sha1(tf.read_bytes() + pool).hexdigest()[:16],
                 'have': presence(work, tf, recs),
                 'fol': hashlib.sha1(fol.read_bytes()).hexdigest()[:16] if fol.exists() else None,
                 'fol_present': presence(work, fol, fol_recs) if fol.exists() else None,
                 'art': sorted({x for x in (admap.image(q) for q in recs) if x}) if admap is not None and admap.ok
                 else [],
                 'leaves': sorted({cp2gltf.door_leaf(q) for q in recs} - {None})}
        fresh[tid] = c
        pl, have, art = c['pl'], c['have'], c['art']
        art_key = None
        if art:
            arh = hashlib.sha1()
            for x in art:
                try:
                    st = os.stat(x)
                    arh.update(('%s %d %d\n' % (x, st.st_size, st.st_mtime_ns)).encode())
                except OSError:
                    arh.update(('%s -\n' % x).encode())
            art_key = arh.hexdigest()[:16]
        mine = sorted(set(c['leaves']) & opened) if opened else []
        doors = hashlib.sha1('\n'.join(mine).encode('utf-8')).hexdigest()[:16] if mine else None
        # (args: what changes a part's output, in its key; run_args: how it is run, not in it: --bake-workers is a
        # process count, and keyed it rebuilt every scene part when it changed)
        for kind, args, run_args, done in (
                ('scene', ['scene'] + (['--detail-slots', str(a.detail_slots)] if a.detail_slots > 1 else []),
                 ['--bake-workers', str(a.bake_workers)], tdir / 'scene' / tid / 'part.json'),
                ('build', ['build'] + BUILD_ARGS, [], tdir / 'build' / (tid + '.npz'))):
            if kind == 'build' and a.scene_only or kind == 'scene' and a.build_only:
                continue
            k = {'placements': pl, 'present': have, 'fetch': fid, 'args': args, 'map': name}
            if kind == 'scene' and doors:
                k['doors'] = doors          # (absent where the tile holds no opened door)
            if kind == 'scene' and c['fol'] is not None:
                k['foliage'] = c['fol']     # (cp77_foliage.py, scene only)
                # and which of its meshes are fetched: a foliage mesh fetched later rebuilds the tiles placing it
                # (presence() once saw the tile's placements only)
                k['foliage_present'] = c['fol_present']
            if kind == 'scene' and ads_key:
                k['ads'] = ads_key          # (which ad each screen shows: cp77_ads.AdMap's inputs)
            if kind == 'scene' and art_key:
                k['ad_art'] = art_key
            if kind == 'scene' and rot_key:
                k['rot'] = rot_key          # (which instances turn: cp77_rotate.RotMap's input)
            if kind == 'scene' and sig_key:
                k['signals'] = sig_key      # (the signal heads' slots: the ranks in signal_groups.json)
            key = part_key(kind, k)
            part_keys[kind].append(key)
            keyf = done.parent / ('%s.%s.key' % (tid, kind))
            if not getattr(a, 'rebuild', False) and done.exists() and keyf.exists() and keyf.read_text() == key:
                continue
            jobs.append((tid, kind, C + args[:1] + run_args + args[1:] + area +
                         ['--tiles-tag', a.tag, '--tile=' + tid, '--map', name], done, keyf, key))
    try:
        tmp = cf.with_suffix('.tmp')
        tmp.write_text(json.dumps(fresh), encoding='utf-8')
        tmp.replace(cf)
    except OSError:
        pass
    return jobs, part_keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('area', choices=sorted(cp2gltf.AREAS))
    ap.add_argument('tag')
    ap.add_argument('--map', help='map name (default cp_<area>)')
    ap.add_argument('--tile-size', type=float, default=256.0)
    ap.add_argument('--workers', type=int, default=3, help='tile processes at once (each up to a few GB of RAM)')
    ap.add_argument('--bake-workers', type=int, default=4, help='per tile: multilayer bake processes')
    ap.add_argument('--detail-slots', type=int, default=1,
                    help='scene parts and merge: tiled layer groups per multilayered material (cp2gltf.py --detail-slots)')
    ap.add_argument('--min-free-gb', type=float, default=2.0,
                    help='memory kept free beyond the running parts\' reservations: a part starts when available '
                         'memory and free commit hold its estimated peak (part_peak_gb) plus this, after what the '
                         'running parts may still grow into (memgate.py; --workers caps how many run)')
    ap.add_argument('--start-gap', type=float, default=0.0,
                    help='seconds to wait after each start besides the reservation (default none: a fixed '
                         'gap of 10 s held 10 workers to 25-45%% use)')
    ap.add_argument('--no-checks', action='store_true')
    ap.add_argument('--only-tiles', help="a file of tile ids, one a line: build these tiles' parts alone (the next "
                                         "area's tiles beside kept parts)")
    ap.add_argument('--from-tag', help='an earlier tiles tag: its tiling linked when its key holds, and each part '
                                       'whose key holds linked, not built; nothing in it is written')
    ap.add_argument('--rebuild', action='store_true',
                    help='the tiles stage and every tile part whatever their keys say (a numbered kit: the safety net '
                         'for a dependency a stage key misses)')
    ap.add_argument('--scene-only', action='store_true',
                    help='build the scene parts only (collision parts come from another tag): proxies '
                         'change the scene, never collision')
    ap.add_argument('--build-only', action='store_true',
                    help='build the collision parts only, with --parts-only: doors are opened by the '
                         'collision build, and the scene parts read its decisions, so a tag with doors builds these '
                         'first and its scene parts after')
    ap.add_argument('--parts-only', action='store_true',
                    help='stop after the tile parts and their summary: no district merge, map or checks (the chunked '
                         'city: the parts are the product, the merge only a play export)')
    ap.add_argument('--engine', type=int, default=5000, help='drop: traces checked by the engine (0: none)')
    a = ap.parse_args()
    t0 = time.time()
    name = a.map or 'cp_' + a.area
    work = cp2gltf.work_dir(a.area)
    tdir = cp2gltf.tile_dir(work, a.tag)
    (tdir / 'logs').mkdir(parents=True, exist_ok=True)
    B = paths.LEVELPORT / 'build'
    LC = paths.LEVELPORT / 'look' / ('levelcheck-%s-%s' % (a.area, a.tag))
    C = [PY, '-u', str(HERE / 'cp2gltf.py')]
    area = ['--area', a.area]
    log(tdir, 'tiles %s %s (%g m)' % (a.area, a.tag, a.tile_size))
    # the tiles carry CP2077's proxies too (the far city; collision skips them)
    tcmd = C + ['tiles'] + area + ['--tiles-tag', a.tag, '--tile-size', str(a.tile_size), '--proxies']
    tkf = tdir / 'tiles.key'
    prev = cp2gltf.tile_dir(work, a.from_tag) if a.from_tag else None
    if prev is not None and not tiles_done(tdir):
        pcmd = C + ['tiles'] + area + ['--tiles-tag', a.from_tag, '--tile-size', str(a.tile_size), '--proxies']
        if link_tiling(work, prev, tdir, tcmd, pcmd):
            log(tdir, 'tiles: linked from tag %s (its tiles.key holds)' % a.from_tag)
        else:
            log(tdir, 'tiles: tag %s\'s tiling does not hold now (its key): tiled again' % a.from_tag)
    if not a.rebuild and tiles_done(tdir) and tkf.exists() and tkf.read_text() == tiles_key(work, tcmd):
        log(tdir, 'tiles kept (placements, fetched meshes, code and arguments unchanged)')
    else:
        if run(tcmd, tdir / 'logs' / 'tiles.log'):
            sys.exit('tiles failed: %s' % (tdir / 'logs' / 'tiles.log'))
        tkf.write_text(tiles_key(work, tcmd))       # (after: the run may have added mesh bounds)
    man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    ids = [t['id'] for t in man['tiles']]
    if a.only_tiles:
        only = {l.strip() for l in open(a.only_tiles, encoding='utf-8') if l.strip()}
        if only - set(ids):
            sys.exit('--only-tiles: not in the tiling: %s' % ' '.join(sorted(only - set(ids))[:20]))
        ids = [t for t in ids if t in only]

    # parts: a tile's scene and collision, each kept while its key holds
    if a.build_only and not a.parts_only:
        sys.exit('--build-only needs --parts-only')
    jobs, part_keys = part_jobs(work, tdir, a, ids, name, C, area)
    if prev is not None:
        jobs, n = link_parts(prev, tdir, jobs)
        log(tdir, 'parts: %d linked from tag %s (their keys hold)' % (n, a.from_tag))
    # biggest tiles first (by placements), so the long ones do not start last and run alone at the end
    size = {t['id']: t.get('placements', 0) for t in man['tiles']}
    jobs.sort(key=lambda j: -size.get(j[0], 0))
    kinds = 1 if a.scene_only or a.build_only else 2
    log(tdir, '%d tiles; %d parts to build (%d kept) on %d workers' % (len(ids), len(jobs),
                                                                       kinds * len(ids) - len(jobs), a.workers))
    G = work / ('%s-%s.glb' % (a.area, a.tag))
    M = B / ('%s-%s' % (name, a.tag)) / ('zz_%s.pk3' % name)
    S = B / ('%s_scene-%s' % (name, a.tag)) / ('zz_%s_scene.pk3' % name)

    import memgate
    gate = memgate.Gate(a.min_free_gb, log=lambda m: log(tdir, '  ' + m))
    scales = PeakScale()

    def one(job):
        """a part on a memory reservation (a fixed 10 s gap left 10 workers 25-45% used): it starts when
        available memory and free commit, less what the running parts may still take, hold its peak plus
        --min-free-gb; its process tree's peak is sampled and logged"""
        tid, kind, cmd, done, keyf, key = job
        label = '%s %s' % (tid, kind)
        est = part_peak_gb(kind, size.get(tid, 0))
        peak = round(est * scales.scale(kind), 2)
        if a.workers > 1:
            gate.wait_start(label, peak)
            if a.start_gap:
                time.sleep(a.start_gap)
        t = time.time()
        top = 0.0
        try:
            with open(tdir / 'logs' / ('%s.%s.log' % (tid, kind)), 'w', encoding='utf-8') as f:
                p = subprocess.Popen(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT)
                gate.bind(label, p.pid)
                while p.poll() is None:
                    top = max(top, memgate.tree_private_gb(p.pid))
                    time.sleep(0.5)
                rc = p.returncode
        finally:
            gate.release(label)
        if rc == 0 and done.exists():
            keyf.parent.mkdir(parents=True, exist_ok=True)
            keyf.write_text(key)
            scales.add(kind, top, est)
        log(tdir, '  %s %s %s in %.0f s (peak %.1f GB, reserved %.1f)' % (
            tid, kind, 'ok' if rc == 0 else 'FAILED (%d)' % rc, time.time() - t, top, peak))
        return rc
    def run_all(js):
        with ThreadPoolExecutor(max(1, a.workers)) as ex:
            failed = sum(1 for rc in ex.map(one, js) if rc)
        if failed:
            sys.exit('%d parts failed (logs in %s)' % (failed, tdir / 'logs'))
    # the collision parts first, all of them: they decide which doors open (door_open in their npz), and a scene part
    # draws those doors without their leaves, so its key and its run must read the finished decisions. Run together, a
    # scene part was keyed and built on the last run's doors, or read a collision part being written.
    # The scene parts are keyed again after.
    built = [j for j in jobs if j[1] == 'build']
    if built:
        run_all(built)
        if not a.build_only:
            jobs, part_keys = part_jobs(work, tdir, a, ids, name, C, area)
            if prev is not None:
                jobs, n = link_parts(prev, tdir, jobs)
                log(tdir, 'scene parts: %d linked from tag %s (their keys hold)' % (n, a.from_tag))
            jobs.sort(key=lambda j: -size.get(j[0], 0))
            log(tdir, '%d collision parts built; %d scene parts to build on their doors' % (
                len(built), sum(1 for j in jobs if j[1] == 'scene')))
    rest = [j for j in jobs if j[1] == 'scene' or not built]
    run_all(rest)
    jobs = built + rest
    if a.parts_only:
        summary(work, tdir, man, None)
        log(tdir, 'parts done in %.0f s' % (time.time() - t0))
        return

    # the district: merges, the map compile, checks. Every step has a key over its inputs' keys, its tool and its
    # arguments (steps.json); it runs when the key differs or its last output is missing. A numbered build (glb,
    # map, scene pack) is never overwritten: a different key over one that exists needs a new tag.
    import gltf2map
    q3 = gltf2map.q3map2_exe()
    q3 = [str(q3), q3.stat().st_size, int(q3.stat().st_mtime)]
    k_scene = source_key({'parts': part_keys['scene'], 'map': name, 'inputs': merge_inputs(work)})
    k_build = source_key({'parts': part_keys['build']})
    # (gltf2map's tiled compile and plane pass, when present; the checks' client and game module)
    k_map = source_key({'glb': k_build, 'q3map2': q3}, files=tuple(
        f for f in ('gltf2map.py', 'gltfio.py', 'presets.py', 'colmerge.py', 'colbsp.py') if (HERE / f).exists()))
    import levelcheck
    bins = [[str(f), f.stat().st_size, int(f.stat().st_mtime)]
            for f in sorted(Path(levelcheck.BIN64).rglob('*.exe')) + sorted(Path(levelcheck.BIN64).rglob('*.dll'))
            + ([levelcheck.X64_GAME] if levelcheck.X64_GAME else []) if f.exists()]
    k_check = source_key({'map': k_map, 'scene': k_scene, 'engine': a.engine, 'bins': bins},
                         files=('levelcheck.py',))
    steps = [
        ('scene merge', C + ['scene'] + area + ['--tiles-tag', a.tag, '--merge', '--tag', a.tag, '--map', name] +
         (['--detail-slots', str(a.detail_slots)] if a.detail_slots > 1 else []), S, k_scene, True),
        ('build merge', C + ['build'] + area + BUILD_ARGS + ['--tiles-tag', a.tag, '--merge', '--tag', a.tag], G,
         k_build, True),
        ('gltf2map', [PY, '-u', str(HERE / 'gltf2map.py'), str(G), name, '--out',
                      str(B / ('%s-%s' % (name, a.tag))), '--sky', 'cp_kabuki_haze', '--sky-haze', '66 43 40 61 37 36',
                      '--sun', '0.55 0.65 1 80 200 40', '--skylight', '150', '--worldspawn', '_ambient=20',
                      '--light', 'fast', '--vis', 'none'], M, k_map, True)]
    if a.scene_only:
        # the scene merge only: no collision part was built, so no build merge, map or checks (the
        # flag once skipped the parts but still ran the collision merge, which stopped on "no part for tile")
        steps = steps[:1]
    if not a.no_checks and not a.scene_only:
        L = [PY, '-I', str(HERE / 'levelcheck.py')]
        # (the checks' 64-bit client takes no slot of the sessions' game lock: its test for other clients counts
        # xenjk.x86.exe only; the client waits only while a timing run holds the lock, levelcheck.client_popen)
        radius = man['radius'] * cp2gltf.gltf_upm()
        steps += [('spawns', L + ['spawns', str(M), '--out', str(LC)], LC / 'spawns.json', k_check, False),
                  ('aerial', L + ['aerial', str(M), str(S), str(LC), '--map', name, '--radius', str(radius)],
                   LC / 'holes.json', k_check, False),
                  # per tile with its neighbours' geometry, then stitched (a whole compare took 16.6 GB on the slice)
                  ('compare', L + ['compare-tiles', str(M), str(S), str(LC / 'collide'), '--tiles',
                                   str(tdir / 'tiles.json'), '--key', k_check, '--workers', '2'],
                   LC / 'collide' / 'overview.png', k_check, False),
                  ('collide', L + ['collide', str(LC / 'collide' / 'cats.npz'), str(LC)], LC / 'collide.json',
                   k_check, False),
                  # per tile on the compare's tile folders, merged; the engine's sample once, in one client
                  ('drop', L + ['drop-tiles', str(M), str(LC / 'collide'), str(LC), '--tiles',
                                       str(tdir / 'tiles.json'), '--key', k_check, '--workers', '2', '--engine',
                                       str(a.engine), '--scene', str(S), '--map', name], LC / 'drop.png', k_check, False)]
    sf = tdir / 'steps.json'
    done_keys = json.load(open(sf, encoding='utf-8')) if sf.exists() else {}
    for label, cmd, out, key, numbered in steps:
        if numbered and out.exists() and done_keys.get(label) not in (None, key):
            sys.exit('%s: tag %s holds a build of other inputs (%s): use a new tag' % (label, a.tag, out))
    for label, cmd, out, key, numbered in steps:
        if out.exists() and (done_keys.get(label) == key or (label not in done_keys and not jobs)):
            # (each step's last file: a step stopped half way runs again; an older run's output without a record
            # is taken when no part was rebuilt)
            log(tdir, '%s kept (%s)' % (label, out.name))
        else:
            t = time.time()
            rc = run(cmd, tdir / 'logs' / (label.replace(' ', '_') + '.log'))
            log(tdir, '%s %s in %.0f s' % (label, 'ok' if rc == 0 else 'FAILED (%d)' % rc, time.time() - t))
            if rc:
                sys.exit('%s failed: %s' % (label, tdir / 'logs' / (label.replace(' ', '_') + '.log')))
        done_keys[label] = key
        tmp = sf.with_suffix('.tmp')
        tmp.write_text(json.dumps(done_keys, indent=1), encoding='utf-8')
        os.replace(tmp, sf)
    summary(work, tdir, man, LC if not (a.no_checks or a.scene_only) else None)
    log(tdir, 'done in %.0f s' % (time.time() - t0))


def summary(work, tdir, man, lc):
    """Per tile: what it holds and what the checks found inside its square (game units from the area's centre)."""
    import numpy as np
    U = cp2gltf.gltf_upm()
    cx, cy = man['centre']
    rows = []
    holes = json.load(open(lc / 'holes.json')).get('clusters', []) if lc and (lc / 'holes.json').exists() else []
    drop = json.load(open(lc / 'drop.json')) if lc and (lc / 'drop.json').exists() else {}
    for t in man['tiles']:
        x0, y0, x1, y1 = [(v - c) * U for v, c in zip(t['box'], (cx, cy, cx, cy))]

        def inside(x, y):
            return x0 <= x < x1 and y0 <= y < y1
        r = {'tile': t['id'], 'placements': t['placements']}
        sp = tdir / 'scene' / t['id'] / 'part.json'
        if sp.exists():
            P = json.loads(sp.read_text(encoding='utf-8'))
            r.update(meshes=len(P['meshkeys']), materials=len(P['mkeys']), images=len(P['images']))
        bp = tdir / 'build' / (t['id'] + '.npz')
        if bp.exists():
            with np.load(bp, allow_pickle=True) as z:
                r.update(col_tris=int(len(z['col'])), boxes=int(len(z['box_G'])))
        cs = lc / 'collide' / 'tiles' / t['id'] / 'stats.json' if lc else None
        if cs and cs.exists():
            c = json.load(open(cs))
            r.update(missing_cells=c['visible_surfaces']['missing'], invisible_tops=c['invisible_collision_tops'])
        r['holes_m2'] = round(sum(c['area_m2'] for c in holes if inside(c['x'], c['y'])), 1)
        ft = drop.get('fall_through', {}).get('clusters', [])
        r['falls_m2'] = round(sum(c['m2'].get('falls', 0) + c['m2'].get('void', 0) for c in ft if inside(c['x'], c['y'])), 1)
        inv = drop.get('invisible_solid', {}).get('clusters', [])
        r['bumps_lumps_m2'] = round(sum(c['m2'].get('bump', 0) + c['m2'].get('lump', 0) for c in inv
                                        if inside(c['x'], c['y'])), 1)
        rows.append(r)
    json.dump(rows, open(tdir / 'summary.json', 'w'), indent=1)
    cols = ['tile', 'placements', 'meshes', 'materials', 'images', 'col_tris', 'boxes', 'missing_cells',
            'invisible_tops', 'holes_m2', 'falls_m2', 'bumps_lumps_m2']
    md = ['| ' + ' | '.join(cols) + ' |', '|' + '---|' * len(cols)]
    md += ['| ' + ' | '.join(str(r.get(c, '')) for c in cols) + ' |' for r in rows]
    (tdir / 'summary.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    print('\n'.join(md))


if __name__ == '__main__':
    import jobguard
    jobguard.kill_children_with_me()      # (its parts' processes end with it)
    main()
