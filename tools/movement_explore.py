"""Offline strafe-edge exploration: one dedicated localhost world per start.

Stages the hash-bound build as the movement benchmark does, starts a private
dedicated server per chosen case, adds the movement-only runner, applies the
case's start through the corner fixture and sets ``bot_moveExplore``, so the
game explores the route corridor from that start (bot2_move.c) and writes
its graph cache once at the end. The world's cache and its ``[EXPLORE]``
lines are retained under --out/<case>/; the caches of every world are then
merged into --out/seed.txt with movement_graph_merge.py, ready for
``movement_bench.py --graph``. Localhost only; never a real server.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from staged_binary import stage_binary
from world_retention import share_graphs  # noqa: E402
from bot_test_runner import rcon_send  # noqa: E402
from movement_bench import GAME, ROOT, CORNER_START, digest, read_log, stage_build, world_settings  # noqa: E402
from movement_bench_lib import load_cases, select_cases  # noqa: E402
from strafe_cost_runner import status_response  # noqa: E402
from strafe_fixture_runner import spawn_without_crash_dialogs  # noqa: E402

EXPLORE_LINE = re.compile(r'\[EXPLORE\] (\w+) (.*)$', re.M)


def parse_explore(text):
    lines = []
    for match in EXPLORE_LINE.finditer(text):
        fields = {}
        for token in match.group(2).split():
            key, _, value = token.partition('=')
            fields[key] = value
        lines.append(dict(kind=match.group(1), **fields))
    return lines


def run_world(case, args, provenance, port, log):
    out = args.out / case['name']
    base = out / 'base'
    base.mkdir(parents=True, exist_ok=False)
    (base / 'scripts').mkdir()
    (base / 'botfiles').mkdir()
    for name, dest in (('openjkded.x86.exe', out), ('jampgamex86.dll', base)):
        stage_binary(args.build / 'RelWithDebInfo' / name, dest / name)
        if digest(dest / name) != provenance['binaries'][name]:
            raise RuntimeError('staged binary does not match the build manifest: ' + name)
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.bot', base / 'scripts/kejim_runner.bot')
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.jkb', base / 'botfiles/kejim_runner.jkb')
    if args.graph:
        header = dict(token.partition('=')[::2] for token in args.graph.read_text().split('\n', 1)[0].split()[2:])
        (base / 'bot2-graph').mkdir()
        shutil.copy2(args.graph, base / 'bot2-graph' / f"{header['map'].replace('/', '_')}_{header['checksum']}.txt")
    password = secrets.token_hex(16)
    settings = world_settings(case, args, port, password)
    # Negative arms an idle bot; only a confirmed fixture enables exploration.
    settings.update(bot_moveExplore=-1, bot_moveExploreRounds=args.rounds, bot_moveExploreBatch=args.batch,
                    bot_moveExploreVerbose=int(args.verbose), bot_moveExploreRadius=args.radius)
    (base / 'start.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) +
                                  '\ndevmap mp/ctf_kejim\n')
    corner = 'bot_corner_start ' + ' '.join(format(v, '.9g') for v in case['start']['origin']) + \
        f" {case['resources']['force']} {case['start']['carrying']}"
    record = dict(case=case, port=port, settings={k: v for k, v in settings.items() if k != 'rconpassword'},
                  corner_command=corner, corner_level=None, corner_response=None,
                  explore_enable_response=None, done=False, explore=[], graph=None,
                  shutdown_clean=False, wall_seconds=None, error=None)
    engine = out / 'openjkded.x86.exe'
    started = time.monotonic()
    with (out / 'server.log').open('xb') as server_log:
        proc = spawn_without_crash_dialogs(
            [str(engine), '+set', 'fs_basepath', str(GAME), '+set', 'fs_homepath', str(out),
             '+set', 'fs_game', 'base', '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1',
             '+set', 'net_port', str(port), '+safe', '+exec', 'start.cfg'], cwd=out,
            stdout=server_log, stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
            env={k.upper(): v for k, v in os.environ.items()})
        added = False
        try:
            deadline = started + args.timeout
            while time.monotonic() < deadline and proc.poll() is None:
                if not added:
                    if status_response(port, password):
                        time.sleep(.6)
                        response = str(rcon_send(port, password, 'addrunner ' + case['team'], timeout=15, collect=.5))
                        (out / 'command.log').write_text(response)
                        if f"[BOT2 PLAY] Adding {case['team']} flag runner." not in response:
                            raise RuntimeError('runner creation not acknowledged; see command.log')
                        added = True
                    time.sleep(.5)
                    continue
                if record['corner_level'] is None:
                    files = list((base / 'bot2-play').glob('*.csv'))
                    rows = []
                    if files:
                        with files[0].open() as stream:
                            rows = list(csv.DictReader(stream))
                    if rows:
                        # A reply lost to a busy start is retried; a refusal is final.
                        for attempt in range(3):
                            response = str(rcon_send(port, password, corner, timeout=5, collect=.5))
                            record['corner_response'] = response
                            match = CORNER_START.search(response)
                            if match or 'REFUSED' in response or 'FAILED' in response:
                                break
                            time.sleep(1)
                        if not match:
                            raise RuntimeError('corner start refused: ' + response.strip()[:300])
                        record['corner_level'] = int(match.group(1))
                        record['explore_enable_response'] = str(rcon_send(
                            port, password, 'set bot_moveExplore 1', timeout=5, collect=.5))
                    time.sleep(.5)
                    continue
                text = read_log(out / 'server.log')
                if '[EXPLORE] done' in text:
                    record['done'] = True
                    break
                time.sleep(2)
        except Exception as error:  # retained as evidence, never hidden
            record['error'] = repr(error)
        finally:
            record['wall_seconds'] = time.monotonic() - started
            for attempt in range(3):
                rcon_send(port, password, 'quit', timeout=1)
                try:
                    proc.wait(timeout=5)
                    record['shutdown_clean'] = True
                    break
                except subprocess.TimeoutExpired:
                    pass
            if not record['shutdown_clean']:
                proc.terminate()
                proc.wait(timeout=10)
    share_graphs(base, args.out)
    text = read_log(out / 'server.log')
    record['explore'] = parse_explore(text)
    caches = sorted((base / 'bot2-graph').glob('*.txt'))
    record['graph'] = str(caches[0]) if caches else None
    (out / 'result.json').write_text(json.dumps(record, indent=2))
    done = [e for e in record['explore'] if e['kind'] == 'done']
    summary = done[-1] if done else {}
    log(f"{case['name']}: done={record['done']} cells={summary.get('cells')} learned={summary.get('learned')} "
        f"merged={summary.get('merged')} edges={summary.get('edges')} plan_cost_ms={summary.get('plan_cost_ms')} "
        f"plan_strafe={summary.get('plan_strafe')} overflow={summary.get('overflow')} "
        f"wall_s={record['wall_seconds']:.0f} error={record['error']}")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=Path, default=ROOT / 'tools/fixtures/movement/kejim/cases.json')
    parser.add_argument('--level', type=int, choices=(1, 2, 3), default=1)
    parser.add_argument('--filter', default=r'_stand$', help='regular expression on case names (the starts explored)')
    parser.add_argument('--graph', type=Path, help='graph cache to seed every world with before exploring')
    parser.add_argument('--rounds', type=int, default=4, help='bot_moveExploreRounds: corridor widening rounds')
    parser.add_argument('--radius', type=float, default=96, help='bot_moveExploreRadius: corridor half-width in units around the plan')
    parser.add_argument('--batch', type=int, default=4, help='bot_moveExploreBatch: entry states explored per frame')
    parser.add_argument('--verbose', action='store_true', help='bot_moveExploreVerbose 1: every candidate printed')
    parser.add_argument('--parity', type=int, default=256, help='bot_moveParity horizon (must be nonzero for the boundary hook)')
    parser.add_argument('--timeout', type=float, default=3600, help='seconds to wait for [EXPLORE] done per world')
    parser.add_argument('--parallel', type=int, default=1, choices=(1, 2, 3, 4))
    parser.add_argument('--port', type=int, default=29280)
    args = parser.parse_args()
    # The benchmark's world settings read these; exploration runs the full stack at timescale 1.
    args.momentum_trace = False
    args.spike = False
    args.edges = True
    args.planner = True
    args.straight = True
    args.state = True
    args.ground_trace = False
    args.timescale = 1
    args.out = args.out.resolve()
    from world_retention import require_free   # both drives, as defense_laps
    require_free(args.out)
    args.build = args.build.resolve()
    if args.graph:
        args.graph = args.graph.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    manifest, provenance = stage_build(args.build)
    doc, cases_sha = load_cases(args.cases)
    chosen = select_cases(doc['cases'], args.level, args.filter)
    if not chosen:
        raise SystemExit('no case selected')
    lines = []

    def log(text):
        lines.append(text)
        print(text, flush=True)

    ports = iter(range(args.port, args.port + 4 * len(chosen)))
    results = []
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = [pool.submit(run_world, case, args, provenance, next(ports), log) for case in chosen]
        for future in futures:
            results.append(future.result())
    seed = args.out / 'seed.txt'
    merge = subprocess.run([sys.executable, str(ROOT / 'tools/movement_graph_merge.py'), str(args.out), '--out', str(seed)],
                           capture_output=True, text=True)
    log('merge: ' + (merge.stdout.strip() or merge.stderr.strip()))
    summary = dict(build=str(args.build), build_manifest=str(manifest), build_manifest_sha256=digest(manifest),
                   staging_helper_sha256=digest(Path(__file__).with_name('staged_binary.py')),
                   cases_file=str(args.cases.resolve()), cases_sha256=cases_sha, runner_sha256=digest(Path(__file__)),
                   rounds=args.rounds, batch=args.batch, radius=args.radius, parity=args.parity,
                   graph=str(args.graph) if args.graph else None,
                   seed=str(seed) if seed.is_file() else None, seed_sha256=digest(seed) if seed.is_file() else None,
                   merge_returncode=merge.returncode,
                   worlds=[dict(name=r['case']['name'], done=r['done'], error=r['error'], wall_seconds=r['wall_seconds'],
                                explore=[e for e in r['explore'] if e['kind'] in ('begin', 'round', 'done')]) for r in results])
    (args.out / 'result.json').write_text(json.dumps(summary, indent=2))
    (args.out / 'log.txt').write_text('\n'.join(lines) + '\n')
    return 0 if all(r['done'] for r in results) and merge.returncode == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
