"""Situation tests: short laps worlds started from recorded moments, paired across arms.

A full laps world measures everything at once and needs many worlds to resolve a
change. A situation test starts every arm from the same recorded moments (all
four bots' positions, velocities, health, Force, Speed and the carried flag,
read from a retained world's bot2-play CSV) and runs each for a few game
seconds, so the arms differ only by the change under test, and the comparison is
paired per moment.

    python tools/laps_scenarios.py extract --worlds '.codex-research/camper-20261002-b/ce-*' \\
        --where "R.carrying and R.force < 30 and dist(R, C) < 1200" --out tools/fixtures/movement/defender/situations/handoff.json \\
        [--count 24] [--spacing 15000] [--name handoff]
    python tools/laps_scenarios.py run --scenarios <file.json> --out .codex-research/<dir> --build .codex-research/<build> \\
        --arm off: --arm on:"--set bot_runnerAbsorb=1" [--game-seconds 20] [--parallel 4] [--rate 25] [--common "..."]

extract: `--where` is a Python expression over the moment, one sample row per
bot: R (runner), C (camper), B (base D), H (chase D), each with x y z vx vy vz
health force carrying ground yaw speed (Speed active) alive, plus dist(a, b) and
FLAG_HOME (the runner team's enemy flag on its stand). Moments are at least
--spacing ms apart in a world; every bot must be alive. The output is the
scenario format of tools/fixtures/movement/defender/scenarios.json with a camper
role added (defense_laps.py --scenario).

run: every scenario under every arm, up to --parallel worlds at once (at most
four, AGENTS.md). Outcomes are read from each world's summary.json and server console:
captures by name, deaths by bot, defender pickups of the runner team's flag,
and counts of selected log lines (--count-line, default the B1 lines). The
report gives each arm's means and, against the first arm, the mean paired
difference per scenario with its standard error and the scenarios that changed.
Localhost only; nothing is deleted.
"""
import argparse
import csv
import glob
import json
import math
import os
import queue
import re
import shlex
import statistics
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMON = ('--camper --defender-rearm --defender-reset --defender-mines 2 --chaser-ambush --set bot_defenderWeapons=8a,11,10a '
          '--set bot_chaserGoal=1 --set bot_defenderCarryLog=1 --set g_fixGolanDamage=1 --set bot_defender=2')
NAMES = {'R': 'Runner_v2', 'C': 'Camper_v2', 'B': 'Defender_v2', 'H': 'Chaser_v2'}
ROLE = {'R': 'runner', 'C': 'camper', 'B': 'base', 'H': 'chase'}
LINES = [r'CAMPER_PROTECT ', r'\[RUNNER\] PROTECT ', r'\[DEFENDER\] CAMPER level']


class Bot(dict):
    __getattr__ = dict.get


def dist(a, b):
    return math.dist((a['x'], a['y'], a['z']), (b['x'], b['y'], b['z']))


def client_names(world):
    game = Path(world, 'live/server/base/play-game.log').read_text(errors='replace')
    names = {}
    for c, n in re.findall(r'ClientConnect: (\d+) \[[^\]]*\] \([^)]*\) "([^"]+)"', game):
        names[int(c)] = re.sub(r'\^\d', '', n)
    for c, n in re.findall(r'ClientUserinfoChanged: (\d+) n\\([^\\]+)\\', game):
        names.setdefault(int(c), re.sub(r'\^\d', '', n))
    return names


def extract(args):
    found = []
    for world in sorted(glob.glob(args.worlds)):
        if not os.path.isdir(world):
            continue
        names = client_names(world)
        letter = {c: k for c, n in names.items() for k, v in NAMES.items() if n == v}
        # Each bot is sampled every 250 ms at its own phase: a moment is the runner's
        # sample with every other bot's nearest sample within 150 ms.
        per = {}
        for f in glob.glob(os.path.join(world, 'live/server/base/bot2-play/*.csv')):
            for r in csv.DictReader(open(f, encoding='utf-8')):
                k = letter.get(int(r['client']))
                if k:
                    per.setdefault(k, []).append((int(r['level_time']), r))
        rows = {}
        if 'R' in per:
            import bisect
            index = {k: ([t for t, _ in sorted(v, key=lambda x: x[0])], [r for _, r in sorted(v, key=lambda x: x[0])]) for k, v in per.items()}
            for t, r in index['R'][0] and zip(*index['R']) or []:
                frame = {'R': r}
                for k, (ts, rs) in index.items():
                    if k == 'R':
                        continue
                    i = bisect.bisect_left(ts, t)
                    near = [j for j in (i - 1, i) if 0 <= j < len(ts) and abs(ts[j] - t) <= 150]
                    if near:
                        frame[k] = rs[min(near, key=lambda j: abs(ts[j] - t))]
                rows[t] = frame
        last = -10 ** 9
        for level in sorted(rows):
            frame = rows[level]
            if len(frame) < 4 or level - last < args.spacing or level < 5000:
                continue
            bots = {}
            for k, r in frame.items():   # ground is groundEntityNum: 1023 is none
                b = Bot(x=float(r['x']), y=float(r['y']), z=float(r['z']), vx=float(r['vx']), vy=float(r['vy']), vz=float(r['vz']),
                        health=int(r['health'] or 0), force=int(r['force'] or 0), carrying=r['carrying'] == '1',
                        ground=r['ground'] not in ('', '1023', '-1'), yaw=float(r['yaw'] or 0), speed=bool(int(r['powers_active'] or 0) & 4))
                b['alive'] = b['health'] > 0
                bots[k] = b
            if not all(b.alive for b in bots.values()):
                continue
            env = dict(bots, dist=dist, FLAG_HOME=frame['R'].get('enemy_flag_status') == '0', abs=abs, min=min, max=max)
            try:
                hit = eval(args.where, {'__builtins__': {}}, env)
            except Exception as error:
                raise SystemExit('--where failed at %s level %d: %r' % (world, level, error))
            if not hit:
                continue
            last = level
            sc = dict(name='%s_%s_%d' % (args.name, os.path.basename(world), level), cls=args.name,
                      source=dict(world=world.replace('\\', '/'), level=level, where=args.where))
            for k, b in bots.items():
                d = dict(origin=[round(b.x, 1), round(b.y, 1), round(b.z, 1)], velocity=[round(b.vx, 1), round(b.vy, 1), round(b.vz, 1)],
                         yaw=round(b.yaw, 1))
                if k == 'R':
                    d.update(carrying=int(b.carrying), force=b.force, speed_ms=8000 if b.speed else 0, health=b.health)
                    if not b.ground:
                        d['air'] = 1
                else:
                    state = dict(health=b.health, force=b.force)
                    if b.speed:
                        state['speed_ms'] = 8000
                    if not b.ground:
                        state['air'] = 1
                    d['state'] = state
                sc[ROLE[k]] = d
            found.append(sc)
    if len(found) > args.count:   # spread over the worlds rather than the first ones
        step = len(found) / args.count
        found = [found[int(i * step)] for i in range(args.count)]
    out = dict(format='DEFENDERSCENARIOS1', map='mp/ctf_kejim', declared=time.strftime('%Y-%m-%d'),
               note='tools/laps_scenarios.py extract; Speed remaining is not recorded, a live Speed starts with 8 s; '
                    'weapons are the loadout, not the recorded ones',
               where=args.where, worlds=args.worlds, scenarios=found)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(out, indent=1) + '\n', encoding='utf-8')
    print('%d scenarios -> %s' % (len(found), args.out))


def outcome(world, patterns):
    s = json.load(open(os.path.join(world, 'summary.json')))
    console = Path(world, 'live/server-console.log').read_text(errors='replace')
    caps = Counter(s.get('team_captures', {}))
    o = dict(team_caps=caps['Runner_v2'] + caps['Camper_v2'], runner_caps=caps['Runner_v2'], camper_caps=caps['Camper_v2'],
             runner_deaths=sum(s.get('runner_deaths', {}).values()),
             runner_falls=s.get('runner_deaths', {}).get('MOD_FALLING', 0),
             camper_deaths=sum(s.get('camper_deaths', {}).values()),
             camper_falls=s.get('camper_deaths', {}).get('MOD_FALLING', 0),
             # as laps_compare.py counts them: self-kills are not deaths, and only the declared
             # defenders' touches of the runner team's flag are theirs
             defender_deaths=sum(n for v in s.get('defender_deaths', {}).values() for k, n in v.items() if k != 'MOD_SUICIDE'),
             defender_enemy_flag=caps['Defender_v2'] + caps['Chaser_v2'])
    for p in patterns:
        o['line:' + p] = len(re.findall(p, console))
    return o


def run(args):
    scenarios = json.loads(Path(args.scenarios).read_text(encoding='utf-8'))['scenarios']
    arms = {}
    for spec in args.arm:
        name, _, extra = spec.partition(':')
        arms[name] = extra
    if args.parallel > 4:
        raise SystemExit('at most four native worlds at once (AGENTS.md)')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ports = queue.Queue()
    for k in range(args.parallel):
        ports.put(args.port + k)
    log = (out / 'scenarios.log').open('a', encoding='utf-8')

    def one(job):
        arm, sc = job
        world = out / arm / sc['name']
        if (world / 'summary.json').exists():
            return
        if world.exists() or Path(str(world) + '.out.txt').exists():
            # A failed or partial world: keep its folder and log (logs stay put); rename it to rerun it.
            log.write('%s %s/%s kept (failed or partial; not rerun)\n' % (time.strftime('%H:%M:%S'), arm, sc['name']))
            log.flush()
            return
        port = ports.get()
        try:
            extra = shlex.split(arms[arm])
            build = [] if '--build' in extra else ['--build', args.build]
            cmd = [sys.executable, str(ROOT / 'tools/defense_laps.py'), *build, '--out', str(world), '--rate', str(args.rate),
                   '--game-seconds', str(args.game_seconds), '--port', str(port),
                   '--scenario', '%s:%s' % (Path(args.scenarios).resolve(), sc['name']), *shlex.split(args.common), *extra]
            world.parent.mkdir(parents=True, exist_ok=True)
            with open(str(world) + '.out.txt', 'w', encoding='utf-8') as fh:
                code = subprocess.call(cmd, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT)
        finally:
            ports.put(port)
        log.write('%s %s/%s exit=%d\n' % (time.strftime('%H:%M:%S'), arm, sc['name'], code))
        log.flush()

    jobs = [(arm, sc) for sc in scenarios for arm in arms]
    with ThreadPoolExecutor(args.parallel) as pool:
        list(pool.map(one, jobs))
    patterns = args.count_line or LINES
    table = {arm: {} for arm in arms}
    for arm in arms:
        for sc in scenarios:
            w = out / arm / sc['name']
            if (w / 'summary.json').exists():
                table[arm][sc['name']] = outcome(str(w), patterns)
    text = report(table, list(arms), [sc['name'] for sc in scenarios])
    (out / 'report.txt').write_text(text + '\n', encoding='utf-8')
    (out / 'outcomes.json').write_text(json.dumps(table, indent=1), encoding='utf-8')
    print(text)
    log.write('ALL_DONE\n')


def report(table, arms, names):
    base = arms[0]
    keys = sorted({k for arm in table.values() for o in arm.values() for k in o})
    rows = ['%-34s' % 'metric (per scenario)' + ''.join(' %12s' % a for a in arms) +
            ''.join(' %24s' % ('%s-%s paired' % (a, base)) for a in arms[1:])]
    for k in keys:
        cells = []
        for a in arms:
            v = [o[k] for o in table[a].values()]
            cells.append(' %12.2f' % statistics.mean(v) if v else ' %12s' % '-')
        for a in arms[1:]:
            d = [table[a][n][k] - table[base][n][k] for n in names if n in table[a] and n in table[base]]
            if len(d) > 1:
                se = statistics.stdev(d) / math.sqrt(len(d))
                changed = sum(1 for x in d if x)
                flag = '*' if se and abs(statistics.mean(d)) >= 2.5 * se else ' '
                cells.append(' %+8.2f+-%5.2f %3d/%-3d%s' % (statistics.mean(d), se, changed, len(d), flag))
            else:
                cells.append(' %24s' % '-')
        rows.append('%-34s' % k[:34] + ''.join(cells))
    rows.append('paired: mean (arm - %s) +- se over scenarios, scenarios that changed / run; * at 2.5 se' % base)
    return '\n'.join(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('extract')
    e.add_argument('--worlds', required=True, help='glob of retained laps worlds')
    e.add_argument('--where', required=True)
    e.add_argument('--out', required=True)
    e.add_argument('--name', default='situation')
    e.add_argument('--count', type=int, default=24)
    e.add_argument('--spacing', type=int, default=15000)
    r = sub.add_parser('run')
    r.add_argument('--scenarios', required=True)
    r.add_argument('--out', required=True)
    r.add_argument('--build', required=True)
    r.add_argument('--arm', action='append', required=True, metavar='NAME:ARGS')
    r.add_argument('--common', default=COMMON)
    r.add_argument('--game-seconds', type=float, default=20)
    r.add_argument('--rate', type=float, default=25)
    r.add_argument('--parallel', type=int, default=4)
    r.add_argument('--port', type=int, default=29760)
    r.add_argument('--count-line', action='append', help='regex to count in each world console (default: the B1 lines)')
    args = ap.parse_args()
    (extract if args.cmd == 'extract' else run)(args)


if __name__ == '__main__':
    main()
