"""Scene bench: each renderer on maps with real rend2 content, at fixed viewpoints, 4K fullscreen, 240 fps cap.

    python tools/renderer_scenes.py content                       # copy the listed content packs (once, ~6 GB)
    python tools/renderer_scenes.py run [--maps a,b] [--candidates a,b] [--reps 1]
    python tools/renderer_scenes.py report <run dir>

Maps, views, packs and settings: tools/fixtures/renderer_bench/scenes.json; candidates and builds as in
tools/renderer_bench.py (its `build`). Per run the client loads the map with `devmap`, goes spectator, closes the menu
(the rend2 capture recipe), and at each view waits `settle_frames`, times `measure_frames` frames with the
cgame's `benchview` (frame-to-frame wall time on a microsecond clock, GPU waits included; xenjk's cgame is staged
for every engine), and takes a screenshot. Metrics: fps, mean/p99/p99.9/max ms per view and overall, frames over
5, 8.3 and 16.7 ms, and load seconds (launch to the first view).

The client runs in a borderless window at 3840x2160 on the main monitor rather than in exclusive fullscreen (an
unattended fullscreen client loses focus to the desktop and renders black at a fraction of its rate); the screen is
taken over while it runs. The installed GameData is fs_basepath and the copied packs fs_cdpath; nothing is written to
either, and the content source's own engine files are never copied or loaded. Protocol: docs/renderer-bench.md.

Environment: SCENE_CONTENT_SOURCE (the base folder the content packs are copied from; scenes.json's content_source
when unset).
"""
import argparse
import json
import os
import re
import shutil
import statistics
import sys
import time
import subprocess
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import renderer_bench as rb   # noqa: E402

CONTENT = rb.OUT / 'content'
MARK_RE = re.compile(r'BENCHVIEW (\S+) (begin|end)')
TARGET_MS = 1000.0 / 240


def scenes():
    return rb.load('scenes.json')


def cmd_content(args):
    """Copy the listed packs. A copy that differs from the source (size or mtime) is moved aside to
    content-old/<stamp>/base/ (kept, never deleted) before the new one is copied. content/manifest.json records
    each pack's size, mtime and sha256 (the map packs; the multi-GB asset packs by size and mtime) and an id that
    every scene run stores in run.json."""
    from world_retention import require_free
    require_free(rb.OUT if rb.OUT.exists() else rb.ROOT)
    cfg = scenes()
    source = os.environ.get('SCENE_CONTENT_SOURCE') or cfg.get('content_source')
    if not source:
        raise SystemExit('set SCENE_CONTENT_SOURCE to the base folder that holds the content packs')
    src = Path(source)
    dst = CONTENT / 'base'
    dst.mkdir(parents=True, exist_ok=True)
    aside = rb.OUT / 'content-old' / time.strftime('%Y%m%dT%H%M%S') / 'base'
    for name in cfg['packs']:
        if 'newmod' in name.lower():
            raise SystemExit('refusing to copy %s' % name)
        s, d = src / name, dst / name
        if not s.exists():
            print('missing: %s' % s)
            continue
        if d.exists() and d.stat().st_size == s.stat().st_size and int(d.stat().st_mtime) == int(s.stat().st_mtime):
            print('have    %s' % name)
            continue
        if d.exists():
            aside.mkdir(parents=True, exist_ok=True)
            d.rename(aside / name)
            print('kept old copy as %s' % (aside / name))
        print('copying %s (%.0f MB)' % (name, s.stat().st_size / 2**20), flush=True)
        shutil.copy2(s, d)
    write_manifest(cfg)


def write_manifest(cfg):
    import hashlib
    packs = {}
    for name in cfg['packs']:
        p = CONTENT / 'base' / name
        if not p.exists():
            continue
        st = p.stat()
        row = {'bytes': st.st_size, 'mtime': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(st.st_mtime))}
        if st.st_size < 2**30:
            h = hashlib.sha256()
            with open(p, 'rb') as f:
                for chunk in iter(lambda: f.read(1 << 20), b''):
                    h.update(chunk)
            row['sha256'] = h.hexdigest()
        packs[name] = row
    ident = hashlib.sha256(json.dumps(packs, sort_keys=True).encode()).hexdigest()[:12]
    (CONTENT / 'manifest.json').write_text(json.dumps({'id': ident, 'source': source,
        'written': time.strftime('%Y-%m-%d %H:%M:%S'), 'packs': packs}, indent=1), encoding='utf-8')
    print('content id %s' % ident)


def content_id():
    try:
        return json.loads((CONTENT / 'manifest.json').read_text(encoding='utf-8'))['id']
    except (OSError, ValueError, KeyError):
        return None


def scene_cfg(mapname, views, settle, measure, pre_cmds=()):
    # `wait` counts command-buffer passes, two a frame on a listen server; benchview counts frames itself
    lines = ['devmap %s' % mapname, 'wait 2500', 'team spectator', 'wait 200', 'togglemenu', 'wait 200']
    lines += list(pre_cmds)   # a profile's "pre_cmds": console lines run once before the first view (npc spawns)
    for name, spec in views.items():
        x, y, z, yaw, pitch = spec[:5]
        # setviewpos places the spectator (and so the snapshot's entities); the cgame's benchcam (cg_bench.c) then pins
        # the drawn yaw and pitch exactly. The pitch used to come from a timed +lookdown, which followed the
        # frame rate and differed between renderers by several degrees.
        lines += ['setviewpos %s %s %s %s' % (x, y, z, yaw), 'benchcam %s %s' % (yaw, pitch), 'wait 60']
        lines += ['wait %d' % settle, 'echo BENCHVIEW %s begin' % name, 'benchview %s %d' % (name, measure),
                  'wait %d' % (2 * measure + 40), 'screenshot view_%s' % name, 'wait 30']
    lines += ['echo BENCHSCENE done', 'wait 30', 'quit']   # a clean exit flushes the log
    return '\n'.join(lines) + '\n'


def launch_scene(cand, rd, cfg_text, sets, timeout, hide=False):
    """Like renderer_bench.launch, but waits for the scene's own end marker and records when the first view began."""
    b, h = rb.client_bin(cand, rd), rd / 'home' / 'base'
    h.mkdir(parents=True, exist_ok=True)
    (h / 'bench.cfg').write_text(cfg_text, encoding='utf-8')
    (h / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % (k, v) for k, v in sets.items()), encoding='utf-8')
    args = [str(b / cand['exe']), '+set', 'fs_basepath', str(rb.ASSETS), '+set', 'fs_cdpath', str(CONTENT),
            '+set', 'fs_homepath', str(rd / 'home'), '+set', 'fs_game', 'base', '+set', 'logfile', '1',
            '+set', 'sv_cheats', '1', '+exec', 'bench.cfg']
    if 'cl_renderer' in sets:   # xenjk.* takes cl_renderer only from the command line (cl_renderer_select.cpp)
        args[-2:-2] = ['+set', 'cl_renderer', sets['cl_renderer']]
    log = h / 'qconsole.log'
    t0 = time.time()
    fg = rb.foreground_window() if hide else None
    proc = rb.client_popen(args, str(b))
    status, last_size, last_change = 'timeout', -1, time.time()
    while time.time() - t0 < timeout:
        if hide:   # a profile's "hide_window": keep the client off the visible desktop and out of the foreground
            rb.hide_client_windows(proc.pid, fg)
        if proc.poll() is not None:
            status = 'exited %d' % proc.returncode
            break
        try:
            size = (written(rd, 'qconsole.log') or log).stat().st_size
            if size != last_size:
                last_size, last_change = size, time.time()
        except OSError:
            pass
        if time.time() - last_change > rb.STALL_S:   # buffered log: quiet stretches are normal, minutes are not
            status = 'stalled (no log output for %d s)' % rb.STALL_S
            break
        time.sleep(0.1 if hide else 0.5)   # hidden: catch the new window within a frame or so
    wall = time.time() - t0
    rb.stop_client(proc)
    log = written(rd, 'qconsole.log') or log
    first_view = load_seconds(log)
    if status.startswith('exited') and log.exists() and 'BENCHSCENE done' in log.read_text(encoding='utf-8', errors='replace'):
        status = 'done' if status == 'exited 0' else 'done (%s at quit)' % status
    time.sleep(5)   # let a crashed client's process and crash reporter go before the next one starts
    return wall, first_view, status


STAMP_RE = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)')


def written(rd, name):
    """A file the client wrote into its homepath: base/ for OpenJK, the engine's own game dir for others
    (TaystJK writes home/taystjk/ even with fs_game base)."""
    hits = sorted(rd.glob('home/*/' + name), key=lambda p: p.stat().st_mtime)
    return hits[-1] if hits else None


def load_seconds(log):
    """Seconds from the log's first line to the first view (whole-second log timestamps)."""
    try:
        lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return None
    stamps = [STAMP_RE.match(l) for l in lines]
    first = next((m.group(1) for m in stamps if m), None)
    view = next((STAMP_RE.match(l).group(1) for l in lines if 'BENCHVIEW' in l and STAMP_RE.match(l)), None)
    if not first or not view:
        return None
    fmt = '%Y-%m-%d %H:%M:%S'
    return time.mktime(time.strptime(view, fmt)) - time.mktime(time.strptime(first, fmt))


def parse_views(text):
    """{view: [frame-to-frame ms]} from benchframes.txt (cgame/cg_bench.c: 'view <name> <count> <ms>...')."""
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == 'view':
            out[parts[1]] = [float(x) for x in parts[3:]]
    return out


def summarize(ms):
    if not ms:
        return {}
    mean = statistics.mean(ms)
    return {'frames': len(ms), 'fps': round(1000.0 / mean, 1) if mean else None, 'mean_ms': round(mean, 3),
            'p99_ms': round(rb.percentile(ms, 99), 2), 'p999_ms': round(rb.percentile(ms, 99.9), 2),
            'max_ms': round(max(ms), 2), 'over_5': sum(v > 5.0 for v in ms), 'over_8': sum(v > 8.3 for v in ms),
            'over_16': sum(v > 16.7 for v in ms)}


def cmd_run(args):
    from world_retention import require_free
    require_free(rb.ROOT)
    cfg = scenes()
    if not (CONTENT / 'base').exists():
        raise SystemExit('no content yet: python tools/renderer_scenes.py content')
    maps = args.maps.split(',') if args.maps else list(cfg['maps'])
    names = args.candidates.split(',') if args.candidates else rb.load('candidates.json').get('default_run')
    profiles = [cfg['profiles'][n] for n in args.profile.split(',')] if args.profile else []
    root = rb.OUT / 'scenes' / (time.strftime('%Y%m%dT%H%M%S') + ('-' + args.profile.replace(',', '-') if args.profile else ''))
    for cand in rb.candidates(names):
        for mapname in maps:
            for rep in range(args.reps):
                rd = root / ('%s__%s__%d' % (cand['name'], mapname, rep))
                stage_bin(cand, rd)
                sets = dict(cfg['common'])
                sets.update(cfg['by_family'].get(cand['family'], {}))
                rewrites = {}
                for prof in profiles:   # layered in the order given
                    sets.update(prof.get('common', {}))
                    sets.update(prof.get('by_family', {}).get(cand['family'], {}))
                    rewrites.update(prof.get('material_rewrites', {}).get(cand['family'], {}))
                sets.update(cand.get('sets', {}))   # a candidate's own settings (a variant of one build)
                sets['cl_renderer'] = cand['renderer']
                rewritten = stage_rewrites(rd, rewrites) + stage_paks(rd, profiles)
                text = scene_cfg(mapname, cfg['maps'][mapname]['views'], cfg['settle_frames'], cfg['measure_frames'],
                                [c for prof in profiles for c in prof.get('pre_cmds', [])])
                attempts = []
                for attempt in range(1 + START_RETRIES):
                    wall, first_view, status = launch_scene(cand, rd, text, sets, args.timeout,
                                                             hide=any(p.get('hide_window') for p in profiles))
                    attempts.append(status)
                    if not start_crash(status, wall):
                        break
                    print('%s %s rep %d: %s at start, retrying' % (cand['name'], mapname, rep, status), flush=True)
                (rd / 'run.json').write_text(json.dumps({'candidate': cand['name'], 'map': mapname, 'rep': rep,
                    'status': status, 'wall_s': round(wall, 1), 'load_s': round(first_view, 1) if first_view else None,
                    'build': str(rb.binary_dir(cand, quiet=True).parent), 'profile': args.profile or None,
                    'content': content_id(),
                    'rewritten_materials': rewritten, 'start_attempts': attempts, 'settings': sets}, indent=1), encoding='utf-8')
                print('%s %s rep %d: %s, load %s s, %.0f s' % (cand['name'], mapname, rep, status,
                      round(first_view, 1) if first_view else '-', wall), flush=True)
    report(root)


# JKSunny clients exit 0xC0000005 within seconds of launch in streaks (5 of 8 one batch, 4 in a row
# another), before or right after "setting mode", stock and patched builds alike; a relaunch runs clean. Such a
# start crash is retried; run.json keeps every attempt's status.
START_RETRIES = 3


def start_crash(status, wall):
    return status.startswith('exited') and wall < 30


# xenjk's cgame with benchcam, built from master
CGAME_BUILD = rb.ROOT / '.codex-research' / 'renderer-bench' / 'builds' / 'benchcam-cgame-20261013' / 'RelWithDebInfo'


def stage_bin(cand, rd):
    bins = rb.binary_dir(cand, quiet=True)
    if not bins:
        raise SystemExit('%s is not built (python tools/renderer_bench.py build %s)' % (cand['name'], cand['name']))
    if not cand.get('installed'):   # an installed client runs in place (renderer_bench.client_bin)
        (rd / 'bin').mkdir(parents=True, exist_ok=True)
        for name in [cand['exe'], *cand['dlls']]:
            shutil.copy2(bins / name, rd / 'bin' / name)
            pdb = bins / (Path(name).stem + '.pdb')   # symbols for a build's crashlog-seh report, if it writes one
            if pdb.exists() and cand.get('pdbs'):
                shutil.copy2(pdb, rd / 'bin' / pdb.name)
        for name in rb.RUNTIME_DLLS:
            if (rb.ASSETS / name).exists():
                shutil.copy2(rb.ASSETS / name, rd / 'bin' / name)
    # one cgame for every engine: xenjk's, which carries the frame logger (cgame/cg_bench.c)
    (rd / 'home' / 'base').mkdir(parents=True, exist_ok=True)
    cgame = (rb.ROOT / cand['cgame']) if cand.get('cgame') else CGAME_BUILD / 'cgamex86.dll'
    shutil.copy2(cgame, rd / 'home' / 'base' / 'cgamex86.dll')
    # engines that unpack game DLLs from pk3s first (EternalJK) would find an older packed cgame: ship this one in
    # the last-sorting pk3 too
    import zipfile
    with zipfile.ZipFile(rd / 'home' / 'base' / 'zzzz_bench_cgame.pk3', 'w', zipfile.ZIP_STORED) as z:
        z.write(cgame, 'cgamex86.dll')


def stage_rewrites(rd, rewrites):
    """Per-renderer material fixes for one run: copies of the content packs' shader files with whole-word keyword
    replacements, under their own names in a pk3 in the run's homepath (searched before fs_cdpath, so only this run
    sees them; the packs are never changed). Returns the rewritten file names."""
    if not rewrites:
        return []
    import zipfile
    pattern = re.compile(r'\b(%s)\b' % '|'.join(map(re.escape, rewrites)))
    found = {}
    for pk3 in sorted((CONTENT / 'base').glob('*.pk3'), key=lambda p: p.name.lower()):   # later packs win, as in game
        with zipfile.ZipFile(pk3) as z:
            for name in z.namelist():
                if name.lower().startswith('shaders/') and name.lower().endswith(('.mtr', '.shader')):
                    text = z.read(name).decode('latin-1')
                    if pattern.search(text):
                        found[name] = pattern.sub(lambda m: rewrites[m.group(1)], text)
    with zipfile.ZipFile(rd / 'home' / 'base' / 'zzzz_bench_materials.pk3', 'w', zipfile.ZIP_DEFLATED) as z:
        for name, text in found.items():
            z.writestr(name, text.encode('latin-1'))
    return sorted(found)


def stage_paks(rd, profiles):
    """Override packs a profile lists under "paks" (paths relative to the repo), copied into the run's homepath,
    which is searched before fs_cdpath: content variants such as a rebuilt map's materials, for this run only."""
    staged = []
    for prof in profiles:
        for rel in prof.get('paks', []):
            src = rb.ROOT / rel
            if not src.exists():
                raise SystemExit('profile pak missing: %s' % src)
            shutil.copy2(src, rd / 'home' / 'base' / src.name)
            staged.append(src.name)
    return staged


def report(root):
    root = Path(root)
    rows, views_by_map = [], {}
    for rd in sorted(p for p in root.iterdir() if p.is_dir()):
        info = json.loads((rd / 'run.json').read_text(encoding='utf-8')) if (rd / 'run.json').exists() else {}
        info.pop('settings', None)
        frames = written(rd, 'benchframes.txt')
        per_view = parse_views(frames.read_text(encoding='utf-8', errors='replace')) if frames else {}
        everything = [v for ms in per_view.values() for v in ms]
        row = dict(info, **summarize(everything))
        row['views'] = {k: summarize(v) for k, v in per_view.items()}
        rows.append(row)
        if info.get('rep') == 0:
            views_by_map.setdefault(info.get('map'), []).append((info.get('candidate'), rd))
    (root / 'report.json').write_text(json.dumps(rows, indent=1), encoding='utf-8')
    cols = ['candidate', 'map', 'rep', 'status', 'load_s', 'fps', 'mean_ms', 'p99_ms', 'p999_ms', 'max_ms', 'over_5',
            'over_8', 'over_16']
    md = ['Frame-to-frame time (ms, cgame clock, 240 fps cap: 4.17 ms is on target); over_5 = a frame clearly late, '
          'over_16 = below 60 fps for that frame.',
          'Content (renderer-bench/content/manifest.json id): %s' % (', '.join(sorted({str(r.get('content')) for r in rows}))), '',
          '| ' + ' | '.join(cols) + ' |', '|' + '---|' * len(cols)]
    for r in rows:
        md.append('| ' + ' | '.join(str(r.get(c, '')) for c in cols) + ' |')
    md += ['', 'Per view (fps / p99 / max ms):', '']
    for r in rows:
        if r['views']:
            md.append('- %s %s: %s' % (r.get('candidate'), r.get('map'), ', '.join(
                '%s %s/%s/%s' % (k, v.get('fps'), v.get('p99_ms'), v.get('max_ms')) for k, v in r['views'].items())))
    for mapname, runs in views_by_map.items():
        sheet = contact_sheet(root, mapname, runs)
        if sheet:
            md.append('\nScreenshots %s: %s (rows: %s)' % (mapname, sheet.name, ', '.join(c for c, _ in runs)))
    (root / 'report.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    print('\n'.join(md))


def contact_sheet(root, mapname, runs):
    try:
        from PIL import Image
    except ImportError:
        return None
    grid = []
    for cand, rd in runs:
        shots = sorted(rd.glob('home/*/screenshots/view_*.jpg'))
        grid.append(shots)
    if not any(grid):
        return None
    names = sorted({p.stem for shots in grid for p in shots})
    w, h = 640, 360
    sheet = Image.new('RGB', (w * len(names), h * len(grid)))
    for y, shots in enumerate(grid):
        byname = {p.stem: p for p in shots}
        for x, n in enumerate(names):
            if n in byname:
                sheet.paste(Image.open(byname[n]).convert('RGB').resize((w, h)), (x * w, y * h))
    out = root / ('sheet_%s.jpg' % mapname)
    sheet.save(out, quality=88)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('content')
    r = sub.add_parser('run')
    r.add_argument('--maps', default='')
    r.add_argument('--candidates', default='')
    r.add_argument('--reps', type=int, default=1)
    r.add_argument('--profile', default='', help='scenes.json "profiles" entries, comma-separated and layered in order: settings and material rewrites '
                   'layered on the default presets per renderer family')
    r.add_argument('--timeout', type=int, default=900)
    p = sub.add_parser('report')
    p.add_argument('run_dir')
    args = ap.parse_args(argv)
    {'content': cmd_content, 'run': cmd_run, 'report': lambda a: report(a.run_dir)}[args.cmd](args)


if __name__ == '__main__':
    main()
