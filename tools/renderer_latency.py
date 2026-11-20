"""Mouse-to-desktop latency per renderer, unattended and without admin rights.

    python tools/renderer_latency.py run [--candidates a,b] [--map siege_urban_pbr] [--view start]
                                         [--profile best,...] [--reps 3] [--samples 100]
    python tools/renderer_latency.py report <run dir>

Each rep starts the client like the scene bench (tools/renderer_scenes.py: private bin and homepath, the bench
content, scenes.json settings and profiles), loads the map as a spectator at one saved view and holds still. Then
tools/latency/latprobe.exe (build it with tools/latency/build_latprobe.cmd) brings the window to the front and
injects relative mouse moves with SendInput; for each move it times the first desktop frame (DXGI Desktop
Duplication) whose centre region changed. The number covers input queueing, the game frame, its GPU queue,
presentation and composition, not mouse polling or the monitor, so it is for before/after and renderer
comparisons. Uses the main monitor and the real mouse: don't touch the mouse while it runs.
"""
import argparse
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import renderer_bench as rb   # noqa: E402
import renderer_scenes as rs  # noqa: E402

PROBE = rb.ROOT / '.codex-research' / 'latency' / 'bin' / 'latprobe.exe'
OUT = rb.ROOT / '.codex-research' / 'latency' / 'runs'
READY = 'LATPROBE ready'


def latency_cfg(mapname, view):
    x, y, z, yaw, pitch = view[:5]
    lines = ['devmap %s' % mapname, 'wait 2500', 'team spectator', 'wait 200', 'togglemenu', 'wait 200',
             'setviewpos %s %s %s %s' % (x, y, z, yaw), 'wait 200', 'echo %s' % READY]
    return '\n'.join(lines) + '\n'


def wait_ready(rd, proc, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if proc.poll() is not None:
            return 'exited %d before ready' % proc.returncode
        log = rs.written(rd, 'qconsole.log')
        if log and READY in log.read_text(encoding='utf-8', errors='replace'):
            return 'ready'
        time.sleep(0.5)
    return 'timeout before ready'


def run_one(cand, rd, mapname, view, sets, profiles, args):
    rs.stage_bin(cand, rd)
    rewrites = sets.pop('__rewrites', {})
    b, h = rb.client_bin(cand, rd), rd / 'home' / 'base'
    h.mkdir(parents=True, exist_ok=True)
    (h / 'latency.cfg').write_text(latency_cfg(mapname, view), encoding='utf-8')
    (h / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % (k, v) for k, v in sets.items()), encoding='utf-8')
    rewritten = rs.stage_rewrites(rd, rewrites) + rs.stage_paks(rd, profiles)
    cmd = [str(b / cand['exe']), '+set', 'fs_basepath', str(rb.ASSETS), '+set', 'fs_cdpath', str(rs.CONTENT),
           '+set', 'fs_homepath', str(rd / 'home'), '+set', 'fs_game', 'base', '+set', 'logfile', '2',
           '+set', 'sv_cheats', '1', '+exec', 'latency.cfg']
    proc = rb.client_popen(cmd, str(b))
    status = wait_ready(rd, proc, args.timeout)
    probe = None
    if status == 'ready':
        time.sleep(args.settle)
        csv = rd / 'latprobe.csv'
        r = subprocess.run([str(PROBE), '--pid', str(proc.pid), '--samples', str(args.samples), '--move',
                            str(args.move), '--csv', str(csv)], capture_output=True, text=True, timeout=600)
        probe = {'rc': r.returncode, 'stdout': r.stdout.strip(), 'stderr': r.stderr.strip()}
        status = 'done' if r.returncode == 0 else 'probe failed'
    rb.stop_client(proc)
    time.sleep(5)
    return status, probe, rewritten


def read_csv(path):
    vals = []
    if path.exists():
        for line in path.read_text(encoding='utf-8').splitlines()[1:]:
            parts = line.split(',')
            if len(parts) >= 5 and parts[4] == 'ok':
                vals.append(float(parts[2]))
    return vals


def summarize(vals):
    if not vals:
        return {'n': 0}
    q = statistics.quantiles(vals, n=10) if len(vals) >= 10 else [min(vals)] * 9
    return {'n': len(vals), 'median_ms': round(statistics.median(vals), 2), 'mean_ms': round(statistics.mean(vals), 2),
            'p10_ms': round(q[0], 2), 'p90_ms': round(q[8], 2)}


def report(root):
    root = Path(root)
    rows = []
    for rd in sorted(p for p in root.iterdir() if p.is_dir()):
        info = json.loads((rd / 'run.json').read_text(encoding='utf-8')) if (rd / 'run.json').exists() else {}
        rows.append(dict(candidate=info.get('candidate'), rep=info.get('rep'), status=info.get('status'),
                         **summarize(read_csv(rd / 'latprobe.csv'))))
    by = {}
    for rd in sorted(p for p in root.iterdir() if p.is_dir()):
        info = json.loads((rd / 'run.json').read_text(encoding='utf-8')) if (rd / 'run.json').exists() else {}
        by.setdefault(info.get('candidate'), []).extend(read_csv(rd / 'latprobe.csv'))
    md = ['Mouse-to-desktop latency (ms; latprobe: SendInput move to the first changed Desktop Duplication frame).', '',
          '| candidate | rep | status | n | median | mean | p10 | p90 |', '|---|---|---|---:|---:|---:|---:|---:|']
    for r in rows:
        md.append('| %s | %s | %s | %s | %s | %s | %s | %s |' % (r['candidate'], r['rep'], r['status'], r['n'],
                  r.get('median_ms', ''), r.get('mean_ms', ''), r.get('p10_ms', ''), r.get('p90_ms', '')))
    md += ['', 'Pooled over reps:', '', '| candidate | n | median | mean | p10 | p90 |', '|---|---:|---:|---:|---:|---:|']
    for cand, vals in by.items():
        s = summarize(vals)
        md.append('| %s | %s | %s | %s | %s | %s |' % (cand, s['n'], s.get('median_ms', ''), s.get('mean_ms', ''),
                  s.get('p10_ms', ''), s.get('p90_ms', '')))
    (root / 'report.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    print('\n'.join(md))


def cmd_run(args):
    from world_retention import require_free
    require_free(rb.ROOT)
    if not PROBE.exists():
        raise SystemExit('build the probe first: tools/latency/build_latprobe.cmd')
    cfg = rs.scenes()
    view = cfg['maps'][args.map]['views'][args.view]
    profiles = [cfg['profiles'][n] for n in args.profile.split(',')] if args.profile else []
    names = args.candidates.split(',')
    root = OUT / (time.strftime('%Y%m%dT%H%M%S') + ('-' + args.profile.replace(',', '-') if args.profile else ''))
    for rep in range(args.reps):   # interleave candidates so drift hits them alike
        for cand in rb.candidates(names):
            rd = root / ('%s__%s__%d' % (cand['name'], args.map, rep))
            sets = dict(cfg['common'])
            sets.update(cfg['by_family'].get(cand['family'], {}))
            rewrites = {}
            for prof in profiles:
                sets.update(prof.get('common', {}))
                sets.update(prof.get('by_family', {}).get(cand['family'], {}))
                rewrites.update(prof.get('material_rewrites', {}).get(cand['family'], {}))
            sets.update(cand.get('sets', {}))   # a candidate's own settings (a variant of one build)
            sets.update({'sensitivity': '5', 'm_yaw': '0.022', 'cl_renderer': cand['renderer']})
            sets['__rewrites'] = rewrites
            attempts = []
            for attempt in range(1 + rs.START_RETRIES):
                status, probe, rewritten = run_one(cand, rd, args.map, view, dict(sets), profiles, args)
                attempts.append(status)
                if not status.startswith('exited'):
                    break
                print('%s rep %d: %s, retrying' % (cand['name'], rep, status), flush=True)
            (rd / 'run.json').write_text(json.dumps({'candidate': cand['name'], 'map': args.map, 'view': args.view,
                'rep': rep, 'status': status, 'probe': probe, 'profile': args.profile or None,
                'rewritten_materials': rewritten, 'start_attempts': attempts, 'build': str(rb.binary_dir(cand, quiet=True).parent),
                'settings': sets}, indent=1), encoding='utf-8')
            print('%s rep %d: %s %s' % (cand['name'], rep, status, (probe or {}).get('stdout', '')), flush=True)
    report(root)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--candidates', default='somaz-rend2,jksunny-pbr')
    r.add_argument('--map', default='siege_urban_pbr')
    r.add_argument('--view', default='start')
    r.add_argument('--profile', default='best')
    r.add_argument('--reps', type=int, default=3)
    r.add_argument('--samples', type=int, default=100)
    r.add_argument('--move', type=int, default=400)
    r.add_argument('--settle', type=float, default=3.0)
    r.add_argument('--timeout', type=int, default=180)
    p = sub.add_parser('report')
    p.add_argument('run_dir')
    args = ap.parse_args(argv)
    {'run': cmd_run, 'report': lambda a: report(a.run_dir)}[args.cmd](args)


if __name__ == '__main__':
    main()
