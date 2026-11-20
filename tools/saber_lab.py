#!/usr/bin/env python3
"""Saber lab: scripted saber fights on private localhost servers, compared across tickrates.

Each cell is one sv_fps with g_tickSabers on or off. A cell runs one seeded, accelerated world in which two bots
with no AI fight a scripted exchange (`saberlab start`, codemp/game/g_saberlab.c) for --reps repetitions. Every
cell sends the same command stream. The world replays byte for byte for the same build, seed, map and arguments.

    python tools/saber_lab.py --build .codex-research/<build> --scenario clash_medium --cells 30:1,125:1,125:0
    python tools/saber_lab.py --build ... --selfcheck          # replay and 30 Hz on/off identity
    python tools/saber_lab.py --report <out dir>              # re-summarise collected CSVs

Acceleration keeps the game frame exact: sv_fps S and timescale T with (1000 // S) * T equal to the cell's frame
and S / T equal to its nominal sv_fps, so code that reads sv_fps sees the nominal rate. Protocol and output:
docs/gameplay/SABER_LAB.md. Localhost only; at most four worlds at once.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from staged_binary import stage_binary  # noqa: E402
from bot_test_runner import rcon_send  # noqa: E402
from strafe_fixture_runner import spawn_without_crash_dialogs  # noqa: E402
from world_retention import drop_staging, require_free  # noqa: E402

SCENARIOS = ROOT / 'tools/fixtures/saberlab/scenarios.json'
# nominal sv_fps -> (sv_fps, timescale): exact frame, sv_fps / timescale == nominal
RATES = {20: (1000, 50), 30: (90, 3), 40: (1000, 25), 50: (1000, 20), 60: (240, 4), 100: (1000, 10),
         125: (1000, 8), 250: (1000, 4),
         # frames that divide 33 ms; S / T is 90.9 and 333.3, so only g_tickSabers 1 cells are exact here
         90: (1000, 11), 333: (1000, 3)}
TOKENS = ['B', 'D', 'V', 'H', 'K', 'P', 'L']   # bounce, deflected, reflected, broken parry, knockaway, parry, lock
NAMES = dict(B='bounce', D='deflected', V='reflected', H='broken parry', K='knockaway', P='parry', L='lock')


def assets_dir():
    for base in (ROOT, ROOT.parents[2] if ROOT.parent.name == 'worktrees' else ROOT):
        p = base / 'game_directory/Jedi Academy/GameData'
        if (p / 'base').is_dir():
            return p
    raise SystemExit('game assets not found under game_directory/Jedi Academy/GameData')


def parse_cells(text):
    """fps[:tick[:r]] items; r runs the cell in real time instead of accelerated."""
    cells = []
    for item in text.split(','):
        parts = item.split(':')
        fps, tick, real = int(parts[0]), int(parts[1]) if len(parts) > 1 else 1, len(parts) > 2 and parts[2] == 'r'
        if fps not in RATES and not real:
            raise SystemExit('no exact accelerated pair for sv_fps %d; known: %s (or add :r)' % (fps, sorted(RATES)))
        cells.append((fps, tick, '-real' if real else ''))
    return cells


def run_cell(i, fps, tick, args, scenario, out, home, build, tag=''):
    name = '%s-%d-%s%s' % (args.scenario, fps, 'on' if tick else 'off', tag)
    port = args.port + i
    s = home / name
    base = s / 'server/base'
    base.mkdir(parents=True)
    stage_binary(build / 'openjkded.x86.exe', s / 'openjkded.x86.exe')
    stage_binary(build / 'jampgamex86.dll', base / 'jampgamex86.dll')
    pw = secrets.token_hex(8)
    sv_fps, timescale = (fps, 1) if tag.endswith('real') else RATES[fps]
    settings = dict(sv_maxclients=4, sv_pure=0, g_gametype=0, g_doWarmup=0, timelimit=0, fraglimit=0, bot_enable=1,
                    bot_minplayers=0, g_log='games.log', g_logSync=1, rconpassword=pw, g_randomSeed=args.seed,
                    sv_fps=sv_fps, timescale=timescale, g_tickSabers=tick,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', sv_master5='')
    settings.update(scenario.get('cvars', {}))
    settings.update(dict(kv.split('=', 1) for kv in args.cvar))
    lab = dict(scenario['lab'])
    lab.update(name=name, reps=args.reps, seed=args.seed)
    lab.update(dict(kv.split('=', 1) for kv in args.set))
    start = 'saberlab start ' + ' '.join('%s=%s' % kv for kv in lab.items())
    setup = [(1000, 'addbot %s 1 free 0 LabA' % args.bot), (1500, 'addbot %s 1 free 0 LabB' % args.bot), (4000, start)]
    (base / 'lab.cfg').write_text('\n'.join('set %s "%s"' % kv for kv in settings.items()) + '\nmap %s\n' % args.map +
                                  ''.join('botat %d %s\n' % sc for sc in setup))
    log = (s / 'console.log').open('xb')
    began = time.time()
    p = spawn_without_crash_dialogs(
        [str(s / 'openjkded.x86.exe'), '+set', 'fs_basepath', str(args.assets), '+set', 'fs_homepath', str(s / 'server'),
         '+set', 'fs_game', 'base', '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', str(port),
         '+exec', 'lab.cfg'], cwd=s, stdout=log, stderr=subprocess.STDOUT,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), env={k.upper(): v for k, v in os.environ.items()})
    glog = base / 'games.log'
    result = 'timeout'
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if p.poll() is not None:
            result = 'exited'
            break
        text = glog.read_text(errors='replace') if glog.exists() else ''
        console = (s / 'console.log').read_text(errors='replace')
        failed = [l for l in console.splitlines() if 'saberlab: ' in l or "Error: Bot '" in l]
        if failed:
            result = 'setup_failed: ' + failed[0].split(' ', 2)[-1]
            break
        if 'SABERLAB_DONE' in text:
            result = text.split('SABERLAB_DONE', 1)[1].split('result=', 1)[1].split()[0]
            break
        time.sleep(1)
    rcon_send(port, pw, 'quit', timeout=1)
    for _ in range(40):
        if p.poll() is not None:
            break
        time.sleep(.25)
    if p.poll() is None:
        p.kill()
    log.close()
    dest = out / name
    dest.mkdir(parents=True)
    for f in (base / 'saberlab' / (name + '.csv'), glog, s / 'console.log'):
        if f.exists():
            (dest / f.name).write_bytes(f.read_bytes())
    cfg = (base / 'lab.cfg').read_text()   # kept without the run's rcon password
    (dest / 'lab.cfg').write_text(''.join(l for l in cfg.splitlines(True) if not l.startswith('set rconpassword ')))
    drop_staging(s, dest, 'console.log')   # the staged binaries and the copied files leave C:
    return dict(cell=name, fps=fps, tick=tick, result=result, seconds=round(time.time() - began, 1),
                csv=str(dest / (name + '.csv')))


def read_cell(path):
    lines = Path(path).read_text().splitlines()
    head = lines[0]
    rows = list(csv.DictReader(lines[1:]))
    return head, rows


def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (p, max(0.0, c - h), min(1.0, c + h))


def mean_ci(xs, z=1.96):
    n = len(xs)
    if n == 0:
        return (0.0, 0.0)
    m = sum(xs) / n
    if n < 2:
        return (m, 0.0)
    v = sum((x - m) ** 2 for x in xs) / (n - 1)
    return (m, z * math.sqrt(v / n))


def summarise(rows):
    n = len(rows)
    out = dict(n=n)
    for side in 'ab':
        out[side + '_dmg'] = mean_ci([int(r[side + '_dmg']) for r in rows])
        out[side + '_hit_rate'] = wilson(sum(1 for r in rows if int(r[side + '_dmg']) > 0), n)
        out[side + '_first'] = mean_ci([int(r[side + '_first']) for r in rows if int(r[side + '_first']) >= 0])
        for t in TOKENS:
            out[side + '_' + t] = wilson(sum(1 for r in rows if t in r[side + '_seq'].split('>')), n)
    out['lock_ms'] = mean_ci([int(r['lock_ms']) for r in rows])
    frames = sum(int(r['frames']) for r in rows)
    out['pass_share'] = sum(int(r['passes']) for r in rows) / frames if frames else 0
    out['frame_ms'] = sorted({int(r['frame_ms']) for r in rows})
    return out


def differs(a, b):
    """True when two (p, lo, hi) or (mean, half) intervals do not overlap."""
    if len(a) == 3:
        return a[2] < b[1] or b[2] < a[1]
    return a[0] + a[1] < b[0] - b[1] or b[0] + b[1] < a[0] - a[1]


def report(out):
    cells = []
    for d in sorted(p for p in out.iterdir() if p.is_dir()):
        f = d / (d.name + '.csv')
        if f.exists():
            head, rows = read_cell(f)
            cells.append((d.name, head, summarise(rows)))
    if not cells:
        raise SystemExit('no cell CSVs under ' + str(out))
    ref = next((c for c in cells if '-30-on' in c[0]), cells[0])
    keys = ['a_dmg', 'b_dmg', 'a_hit_rate', 'b_hit_rate', 'a_first', 'b_first', 'lock_ms'] + \
        ['%s_%s' % (s, t) for s in 'ab' for t in TOKENS]
    used = [k for k in keys if any((c[2][k][0] if len(c[2][k]) else 0) for c in cells)]
    lines = ['# Saber lab report: %s' % out.name, '',
             'Reference cell: `%s`. A value marked * has a 95%% interval that does not overlap the reference.' % ref[0], '',
             '| metric | ' + ' | '.join(c[0] for c in cells) + ' |', '|---|' + '---|' * len(cells)]
    for k in ['n', 'frame_ms', 'pass_share'] + used:
        cols = []
        for name, _, s in cells:
            v = s[k]
            if k == 'n':
                cols.append(str(v))
            elif k == 'frame_ms':
                cols.append('/'.join(map(str, v)))
            elif k == 'pass_share':
                cols.append('%.3f' % v)
            elif len(v) == 3:
                cols.append('%.1f%% [%.1f-%.1f]%s' % (100 * v[0], 100 * v[1], 100 * v[2], ' *' if differs(v, ref[2][k]) else ''))
            else:
                cols.append('%.1f ±%.1f%s' % (v[0], v[1], ' *' if differs(v, ref[2][k]) else ''))
        label = k.replace('_', ' ')
        for t, nm in NAMES.items():
            label = label.replace(' ' + t, ' ' + nm) if label.endswith(' ' + t) else label
        lines.append('| %s | %s |' % (label, ' | '.join(cols)))
    lines += ['', 'a is the fighter at the anchor, b the one facing it. dmg is damage dealt per rep; hit rate is the share of '
              'reps that dealt any; first is ms from the action start to the first damaging frame; outcome rows are the share '
              'of reps whose saberMove sequence entered that category.']
    (out / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (out / 'summary.json').write_text(json.dumps({c[0]: c[2] for c in cells}, indent=1))
    print('\n'.join(lines))


def rows_without(path, drop):
    head, rows = read_cell(path)
    return [{k: v for k, v in r.items() if k not in drop} for r in rows]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', type=Path)
    ap.add_argument('--scenario', default='clash_medium')
    ap.add_argument('--cells', default='30:1,40:1,60:1,125:1,250:1,30:0,125:0')
    ap.add_argument('--reps', type=int, default=200)
    ap.add_argument('--seed', type=int, default=1021)
    ap.add_argument('--map', default='mp/ffa3')
    ap.add_argument('--bot', default='tavion', help='a one-word bots.txt name; both fighters use it (same model)')
    ap.add_argument('--port', type=int, default=29200)
    ap.add_argument('--workers', type=int, default=4)
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('--set', action='append', default=[], help='extra saberlab key=value, overrides the scenario')
    ap.add_argument('--cvar', action='append', default=[], help='extra world cvar name=value, set before the map loads')
    ap.add_argument('--out', type=Path)
    ap.add_argument('--selfcheck', action='store_true',
                    help='replay (30:1 twice), on/off identity at 30 Hz, accelerated == real time at 30 and 125 Hz')
    ap.add_argument('--report', type=Path, help='summarise an existing output directory and exit')
    args = ap.parse_args()
    if args.report:
        report(args.report)
        return
    if not args.build:
        raise SystemExit('--build is required (strafe_fixture_build.py output)')
    if args.workers > 4:
        raise SystemExit('at most four worlds at once (AGENTS.md)')
    build = args.build.resolve() / 'RelWithDebInfo'
    if not (build / 'jampgamex86.dll').is_file():
        raise SystemExit('no game DLL in ' + str(build))
    scenarios = json.loads(SCENARIOS.read_text())
    if args.scenario not in scenarios:
        raise SystemExit('unknown scenario; known: ' + ', '.join(k for k in scenarios if not k.startswith('_')))
    scenario = scenarios[args.scenario]
    args.assets = assets_dir()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    home = Path(os.environ['LOCALAPPDATA']) / 'xenjk-play' / ('saberlab-' + stamp)
    out = (args.out or ROOT / '.codex-research' / ('saberlab-%s-%s' % (args.scenario, stamp))).resolve()
    require_free(home)
    require_free(out)
    home.mkdir(parents=True)
    out.mkdir(parents=True)
    if args.selfcheck:
        jobs = [(30, 1, '-a'), (30, 1, '-b'), (30, 0, ''), (30, 1, '-real'), (125, 1, ''), (125, 1, '-real')]
    else:
        jobs = parse_cells(args.cells)
    meta = dict(build=str(build), scenario=args.scenario, definition=scenario, reps=args.reps, seed=args.seed, map=args.map,
                bot=args.bot, set=args.set, cvar=args.cvar, jobs=jobs, home=str(home), created_utc=stamp)
    (out / 'run.json').write_text(json.dumps(meta, indent=1))
    with ThreadPoolExecutor(args.workers) as ex:
        results = list(ex.map(lambda a: run_cell(a[0], a[1][0], a[1][1], args, scenario, out, home, build, a[1][2]),
                              enumerate(jobs)))
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    try:
        home.rmdir()   # empty once every cell's staging is dropped; a kept one stays, and is reported
    except OSError:
        print('staging kept: %s' % home)
    for r in results:
        print(json.dumps(r))
    bad = [r for r in results if r['result'] != 'ok']
    if bad:
        raise SystemExit('cells did not finish: ' + ', '.join(r['cell'] for r in bad))
    if args.selfcheck:
        a, b, off, real30, acc125, real125 = (Path(r['csv']) for r in results)
        body = lambda p: p.read_text().splitlines()[1:]
        verdict = dict(replay_identical=body(a) == body(b),
                       tick_on_off_identical_at_30=rows_without(a, {'tick_sabers'}) == rows_without(off, {'tick_sabers'}),
                       accelerated_equals_realtime_30=body(a) == body(real30),
                       accelerated_equals_realtime_125=body(acc125) == body(real125))
        (out / 'selfcheck.json').write_text(json.dumps(verdict, indent=1))
        print('SELFCHECK', json.dumps(verdict))
        if not all(verdict.values()):
            raise SystemExit(1)
        return
    report(out)


if __name__ == '__main__':
    main()
