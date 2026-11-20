"""patch_tiles.py -- a collision fix played within the hour: some tiles' collision rebuilt into a dev kit.
Chunks are per tile in their own frame, so a few tiles' BSPs can stand in for the last kit's.

  python -I patch_tiles.py <name> --tiles -7_4,-6_4 | --report <screenpos .txt> [--ring] --base-kit DIR
                           [--from-tag 8] [--jobs 2] [--steps build,chunks,compile,kit]

  build    the tiles' collision parts (cp2gltf.py build --tile, cp77_tiles.BUILD_ARGS) in the dev tiling
           tiles/dev-<name>/: its tiles.json lists only these tiles; their placements and the ad pool are hard links
           to --from-tag's (the placements of a code fix are the tag's)
  chunks   cp2gltf.py chunks over that tiling: these tiles' chunk glbs, their manifest
  compile  gltf2map --col-chunk --exact-planes per tile (q3map2: the xenjk7 build), into tiles/dev-<name>/compiled/
  kit      patch_kit.py: build/cp_watson_dev-<name>/, the base kit with these chunks over its own

--report: the tile under the report's player (probe.py's frame); --ring adds its eight neighbours (a fix near an
edge). Idle priority for every step (set on this process; the steps inherit it); a compile starts only with 8 GB of
RAM available. Never a numbered tag or kit; nothing installed. Logs in tiles/dev-<name>/logs/."""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import cp77_tiles  # noqa: E402

PY = sys.executable


def idle():
    try:
        import psutil
        psutil.Process().nice(psutil.IDLE_PRIORITY_CLASS if os.name == 'nt' else 19)
    except Exception as e:      # (no psutil: the caller's priority stands)
        print('patch_tiles: priority unchanged (%s)' % e)


def wait_ram(gb):
    import psutil
    while psutil.virtual_memory().available < gb * 2 ** 30:
        time.sleep(30)


def run(cmd, log):
    t0 = time.time()
    with open(log, 'w', encoding='utf-8') as f:
        rc = subprocess.run(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT).returncode
    print('  %s: %s in %.0f s (%s)' % (Path(log).stem, 'ok' if rc == 0 else 'FAILED (%d)' % rc, time.time() - t0, log),
          flush=True)
    if rc:
        sys.exit('patch_tiles: %s failed' % Path(log).stem)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('name')
    ap.add_argument('--tiles', default='')
    ap.add_argument('--report', type=Path, help='a /screenpos report: the tile under its player')
    ap.add_argument('--ring', action='store_true', help='with the tiles\' eight neighbours')
    ap.add_argument('--base-kit', type=Path, required=True)
    ap.add_argument('--from-tag', default='8')
    ap.add_argument('--area', default='watson')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--jobs', type=int, default=2, help='gltf2map\'s conversion processes (about 0.6 GB each)')
    ap.add_argument('--steps', default='build,chunks,compile,kit')
    a = ap.parse_args()
    idle()
    if not re.fullmatch(r'[a-z0-9][a-z0-9_.-]*', a.name) or re.fullmatch(r'\d+[a-z]?', a.name):
        sys.exit('patch_tiles: a dev name is lower-case letters, digits, . _ - (not a tag number)')
    work = cp2gltf.work_dir(a.area)
    src = cp2gltf.tile_dir(work, a.from_tag)
    tag = 'dev-' + a.name
    tdir = cp2gltf.tile_dir(work, tag)
    man = json.loads((src / 'tiles.json').read_text(encoding='utf-8'))
    have = {t['id']: t for t in man['tiles']}
    tiles = [t for t in a.tiles.split(',') if t]
    if a.report:
        import probe
        r = probe.parse_report(a.report)
        tiles.append(probe.Area(work, a.from_tag, a.area).tile(r['origin']))
    if a.ring:
        ring = []
        for t in tiles:
            i, j = map(int, t.split('_'))
            ring += ['%d_%d' % (i + di, j + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)]
        tiles = ring
    tiles = sorted({t for t in tiles if t in have}, key=lambda t: list(have).index(t))
    if not tiles:
        sys.exit('patch_tiles: no tiles of tag %s chosen' % a.from_tag)
    steps = a.steps.split(',')
    print('patch_tiles %s: %s from tag %s' % (a.name, ', '.join(tiles), a.from_tag), flush=True)
    (tdir / 'logs').mkdir(parents=True, exist_ok=True)
    # the dev tiling: these tiles only, their placements and the ad pool linked to the tag's
    for f in [t + '.json' for t in tiles] + ['ad_pool.json']:
        d = tdir / f
        if not (src / f).exists():
            continue
        if not d.exists():
            os.link(src / f, d)
        elif not os.path.samefile(d, src / f):
            # (the dev name used before with another --from-tag: its link to that tag's file is replaced; a file that
            # is no tag's link is left alone and the run stops)
            if os.stat(d).st_nlink < 2:
                sys.exit('patch_tiles: %s is not a link to a tag\'s file; move it away to use tag %s' % (d, a.from_tag))
            tmp = d.with_name(d.name + '.link')
            os.link(src / f, tmp)
            os.replace(tmp, d)
    dm = dict(man, tiles=[have[t] for t in tiles], dev={'from_tag': a.from_tag, 'name': a.name})
    (tdir / 'tiles.json').write_text(json.dumps(dm, indent=1), encoding='utf-8')
    C = [PY, '-u', str(HERE / 'cp2gltf.py')]
    area = ['--area', a.area]
    if 'build' in steps:
        for t in tiles:
            run(C + ['build'] + cp77_tiles.BUILD_ARGS + area + ['--tiles-tag', tag, '--tile=' + t, '--map', a.map],
                tdir / 'logs' / ('%s.build.log' % t))
    if 'chunks' in steps:
        run(C + ['chunks'] + area + ['--tiles-tag', tag], tdir / 'logs' / 'chunks.log')
    out = tdir / 'compiled'
    if 'compile' in steps:
        for t in tiles:
            wait_ram(8)
            run([PY, '-u', str(HERE / 'gltf2map.py'), str(tdir / 'chunks' / (t + '.glb')), 'chunk_' + t, '--out',
                 str(out / t), '--col-chunk', '--exact-planes', '--jobs', str(a.jobs), '--scratch',
                 str(tdir / 'scratch')], tdir / 'logs' / ('%s.compile.log' % t))
    if 'kit' in steps:
        run([PY, '-I', str(HERE / 'patch_kit.py'), str(a.base_kit), a.name, '--builds', str(out), '--manifest',
             str(tdir / 'chunks' / 'manifest.json'), '--tiles=' + ','.join(tiles), '--map', a.map, '--note',
             'tiles %s rebuilt from tag %s by patch_tiles.py' % (', '.join(tiles), a.from_tag)],
            tdir / 'logs' / 'kit.log')


if __name__ == '__main__':
    main()
