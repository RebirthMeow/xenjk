"""Row 5 of the level-1 exit gate: what the rewrite's movement costs per frame.

The gate's cost row asks for the search enabled **without** the parity harness,
a declared per-frame CPU budget met with **at least four bots on one server**,
and no idle wait induced by the controller. This runs that configuration and
reports what the cost clock measured. It does not tune anything to meet the
budget, and one run is a measurement of the instrument, not of the row.

**Why this is a new tool and not a flag on `strafe_cost_runner.py`.** That
runner drives the archived strafe engine's workload: supported hops, a route
catalog, a baked flight artifact, a forecast guard, rendezvous and hold cases.
Every one of those cvars is meaningless to the rewrite, and its required
`--catalog` is a route file the rewrite never reads. The two share the engine's
`bot2_movecost.c` fixture and nothing else, so they share the fixture and not
the command line.

**The configuration.** The rewrite (unconditional) with edges, planner, state and the
straight search on, the tape library on, `bot_moveParity` **unset**, four bots
on one dedicated localhost server, no graph seed. The fixture spawns the bots
at their own team spawns, gives each the enemy flag as its objective and steps
them through production dispatch for a fixed 20,000 ms window at `sv_fps` 40,
writing one row per server frame.

**What is measured.** `movement_us` in `movement_cost.csv` is the frame's
complete movement accounting **summed over every bot of that frame**: the
command dispatch the fixture times around `Bot2_ExecuteMovement`, plus the work
at the Pmove boundary - the straight, gap, planned and spike searches and the
parity harness when it is armed - charged to the same frame through
`Bot2_MoveCostPreparation`. That is the number the row is about, and it is
reported as p50, p95, p99 and the worst frame against the 25 ms server frame,
with the count of frames over a declared budget.

**The budget is declared, not fitted**: 5,000 us per frame for all four bots
together, a fifth of the frame. It is `--budget-us` only so a record can state
it; the default is the declared one and nothing in the engine reads it.

Usage:
    python tools/movement_cost_runner.py --build .codex-research/<build> \\
        --out .codex-research/<campaign>/<world> [--bots 4] [--port 30600] \\
        [--budget-us 5000] [--parity 0] [--library <dir>] [--graph <cache>]

Localhost only, one server, and the process is the tool's own.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from staged_binary import stage_binary
from world_retention import share_graphs
from bot_test_runner import rcon_send, kill_server
from strafe_fixture_runner import spawn_without_crash_dialogs

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / 'game_directory' / 'Jedi Academy' / 'GameData'
# The declared budget, in microseconds of movement work per server frame for
# every bot on it together. Declared before the measurement, as the row says.
BUDGET_US = 5000
# The server frame the budget sits inside, at sv_fps 40.
FRAME_US = 25000
# The fixture's own window, fixed in bot2_movecost.c.
WINDOW_MS = 20000


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def percentile(values, fraction):
    """The nearest-rank percentile of an already-sorted list.

    Nearest rank rather than an interpolation: a frame cost is a frame that
    happened, and a p99 that no frame ever cost is not evidence about frames.
    """
    if not values:
        return None
    index = max(0, min(len(values) - 1, math.ceil(fraction * len(values)) - 1))
    return values[index]


def summarize(rows, budget_us=BUDGET_US, frame_us=FRAME_US):
    """The per-frame movement cost distribution, from the capture's own rows.

    `rows` are `movement_cost.csv` dictionaries. Only frames the fixture
    actually dispatched on are counted: a row with no dispatches is a frame
    where every bot was held or dead and it measures nothing about movement.
    """
    frames = [dict(level_time=int(r['level_time']), elapsed_ms=int(r['elapsed_ms']),
                   movement_us=float(r['movement_us']), dispatch_us=float(r['dispatch_us']),
                   prepare_us=float(r['prepare_us']), clients=int(r['clients']),
                   dispatches=int(r['dispatches']), alive=int(r['alive']),
                   moving=int(r['moving']), bad_clocks=int(r['bad_clocks']))
              for r in rows]
    active = [f for f in frames if f['dispatches'] > 0]
    costs = sorted(f['movement_us'] for f in active)
    worst = max(active, key=lambda f: f['movement_us']) if active else None
    return dict(
        frames=len(frames), active_frames=len(active),
        clients=max((f['clients'] for f in frames), default=0),
        budget_us=budget_us, frame_us=frame_us,
        p50_us=percentile(costs, .50), p95_us=percentile(costs, .95),
        p99_us=percentile(costs, .99),
        max_us=costs[-1] if costs else None,
        mean_us=round(sum(costs) / len(costs), 3) if costs else None,
        worst_frame=dict(worst) if worst else None,
        over_budget_frames=sum(1 for c in costs if c > budget_us),
        over_frame_frames=sum(1 for c in costs if c > frame_us),
        over_budget_share=round(sum(1 for c in costs if c > budget_us) / len(costs), 6)
        if costs else None,
        budget_share_of_frame=round(budget_us / frame_us, 6),
        max_share_of_frame=round(costs[-1] / frame_us, 6) if costs else None,
        dispatch_us_total=round(sum(f['dispatch_us'] for f in active), 3),
        prepare_us_total=round(sum(f['prepare_us'] for f in active), 3),
        bad_clock_frames=sum(1 for f in active if f['bad_clocks']),
        alive_min=min((f['alive'] for f in active), default=None),
        moving_frames=sum(1 for f in active if f['moving']))


def read_capture(path):
    with Path(path).open(newline='') as stream:
        return list(csv.DictReader(stream))


def status_response(port, password):
    """Rcon status can span several 1 KB UDP print packets."""
    parts = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(.5)
        sock.sendto(b'\xff\xff\xff\xff' + f'rcon {password} status'.encode(), ('127.0.0.1', port))
        while True:
            try:
                data, _ = sock.recvfrom(8192)
            except (socket.timeout, ConnectionResetError, ConnectionRefusedError):
                break
            if data.startswith(b'\xff\xff\xff\xffprint\n'):
                data = data[10:]
            parts.append(data.decode('utf-8', errors='replace'))
            sock.settimeout(.1)
    return ''.join(parts)


def stage_build(build, source_root=ROOT):
    """The build's manifest, refused when a C source has moved since it was made."""
    manifests = sorted(Path(build).glob('*.sources.json'))
    if not manifests:
        raise SystemExit('build must carry a source/binary hash manifest from strafe_fixture_build.py')
    provenance = json.loads(manifests[-1].read_text())
    changed = [name for name, expected in provenance['sources'].items()
               if not (source_root / name).is_file() or digest(source_root / name) != expected]
    if changed:
        raise SystemExit('build sources differ from source root: ' + ', '.join(changed[:10]))
    return manifests[-1], provenance


def settings_for(args, port, password):
    settings = dict(
        dedicated=1, net_ip='127.0.0.1', net_port=port, rconpassword=password,
        fs_basepath=str(args.assets.resolve()), fs_homepath=str((args.out / 'home').resolve()),
        fs_game='base', sv_pure=0, sv_maxclients=args.bots, sv_fps=40,
        g_gametype=8, bot_enable=1, bot_minplayers=0,
        fixedtime=0, timedemo=0, timescale=1, g_doWarmup=0,
        capturelimit=0, timelimit=0, fraglimit=0, g_spawnInvulnerability=0,
        g_forcePowerDisable=0, g_speed=250, g_gravity=800, g_forceRegenTime=200,
        g_stepSlideFix=1, pmove_fixed=1, pmove_msec=8, pmove_float=1, com_hunkMegs=256,
        # The fixture.
        bot_moveCost=args.bots, bot_moveCostTrace=int(getattr(args, 'trace', False)),
        # Telemetry off for a timing world, and this is not negotiable: a
        # print inside the search measures a different bot
        # (docs/movement/LESSONS.md). It exists as a switch only so a
        # companion world can show what the searches did, and such a world's
        # timings are declared invalid.
        bot_telemetry=int(args.telemetry),
        bot_movebench_hold=1,
        # The rewrite's production configuration. The parity harness is off:
        # the searches run under their own safety gates and
        # do not wait for it.
        bot_movementOnly=1, bot_moveParity=args.parity,
        bot_moveEdges=1, bot_movePlanner=1, bot_moveState=1, bot_moveStraight=1,
        bot_moveSpike=0, bot_moveDecisionTape='',
        bot_moveTapeLibrary=1 if args.library else 0,
        bot_moveSpend=1,
        # The search budget: the engine's default, 4; 0 is
        # the configuration every earlier cost world ran.
        bot_moveSearchBudget=args.search_budget,
        # The deferred graph-cache write, on by default, as
        # the engine's own default is: 1 writes the cache at each leg boundary
        # and at shutdown; 0 rewrites the whole cache on every learned edge, as
        # every cost world before that date did.
        bot_moveGraphSaveDefer=args.graph_save_defer,
        bot_moveReplay=0, bot_moveStatePrefer=1,
        bot_moveReroute=1, bot_moveExitSpeed=500,
        bot_forecastItems=1, bot_forecastTriggerShadow=0,
        bot_moveExplore=0)
    # Extra world cvars, recorded in the world's settings like every other one.
    for pair in getattr(args, 'set', None) or []:
        name, _, value = pair.partition('=')
        if not name or not _:
            raise SystemExit('--set needs CVAR=VALUE, got %r' % pair)
        settings[name] = value
    return settings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--assets', type=Path, default=GAME)
    ap.add_argument('--source-root', type=Path, default=ROOT)
    ap.add_argument('--bots', type=int, default=4, choices=(1, 2, 4, 8, 16),
                    help="clients on the one server; the gate row asks for one")
    ap.add_argument('--port', type=int, default=30600)
    ap.add_argument('--budget-us', type=int, default=BUDGET_US,
                    help='the declared per-frame movement budget in microseconds, for all '
                         'bots together (default: the declared 5,000). Reporting only.')
    ap.add_argument('--parity', type=int, default=0,
                    help='bot_moveParity horizon; 0 is the production configuration and what '
                         'the row asks for. A non-zero value arms the harness as a diagnostic.')
    ap.add_argument('--library', type=Path,
                    help='a decision-tape library directory, staged under the name the engine '
                         'binds by; omitted means the library is off')
    ap.add_argument('--graph', type=Path,
                    help='a prepared graph cache to seed the world with. The production '
                         'configuration has none and builds its own at load.')
    ap.add_argument('--search-budget', type=int, default=4,
                    help='bot_moveSearchBudget: candidate rollouts a bot may issue per '
                         'server frame while it pre-searches the next boundary. 4 is the '
                         'engine default; 0 turns the pre-search off.')
    ap.add_argument('--graph-save-defer', type=int, default=1, choices=(0, 1),
                    help='bot_moveGraphSaveDefer: 1 (the engine default) snapshots '
                         'the graph cache on a learn and writes it at each leg boundary and at shutdown '
                         'instead of rewriting it on the frame. 0 rewrites it on the '
                         'frame.')
    ap.add_argument('--telemetry', action='store_true',
                    help='print the movement events, so a companion world can show that the '
                         'searches ran and committed. Printing happens inside the measured '
                         'interval, so the timings of such a world are not evidence.')
    ap.add_argument('--set', action='append', metavar='CVAR=VALUE',
                    help='extra world cvar, repeatable; recorded in the settings like the others')
    ap.add_argument('--trace', action='store_true',
                    help='bot_moveCostTrace 1: the fixture buffers one row per client per frame (position, '
                         'velocity, command, owner) into movement_cost_trace.csv, written after recording. '
                         'A diagnostic: the fixture declares the timings of such a world invalid.')
    ap.add_argument('--timeout', type=float, default=180.0)
    args = ap.parse_args(argv)

    manifest, provenance = stage_build(args.build, args.source_root)
    out = args.out.resolve()
    from world_retention import require_free   # both drives, as defense_laps
    require_free(out)
    out.mkdir(parents=True, exist_ok=False)
    base = out / 'home/base'
    base.mkdir(parents=True)
    for name, dest in (('openjkded.x86.exe', out), ('jampgamex86.dll', base)):
        stage_binary(args.build / 'RelWithDebInfo' / name, dest / name)
        if digest(dest / name) != provenance['binaries'][name]:
            raise SystemExit('staged binary does not match the build manifest: ' + name)
    if args.graph:
        header = dict(token.partition('=')[::2]
                      for token in args.graph.read_text().split('\n', 1)[0].split()[2:])
        (base / 'bot2-graph').mkdir()
        shutil.copy2(args.graph,
                     base / 'bot2-graph' / f"{header['map'].replace('/', '_')}_{header['checksum']}.txt")
    if args.library:
        shutil.copytree(args.library, base / 'bot2-decisions' / args.library.name,
                        dirs_exist_ok=True)

    password = secrets.token_hex(16)
    settings = settings_for(args, args.port, password)
    (base / 'cost_start.cfg').write_text(
        '\n'.join(f'set {k} "{v}"' for k, v in settings.items()) + '\ndevmap mp/ctf_kejim\n')

    (out / 'manifest.json').write_text(json.dumps(dict(
        build=str(args.build.resolve()), build_manifest=str(manifest),
        build_manifest_sha256=digest(manifest),
        binaries={name: provenance['binaries'][name]
                  for name in ('openjkded.x86.exe', 'jampgamex86.dll')},
        runner_sha256=digest(Path(__file__)), settings={k: v for k, v in settings.items() if k != 'rconpassword'},
        budget_us=args.budget_us, window_ms=WINDOW_MS,
        library=str(args.library.resolve()) if args.library else None,
        graph=str(args.graph.resolve()) if args.graph else None,
        host=dict(platform=platform.platform(), processor=platform.processor(),
                  cpu_count=os.cpu_count())), indent=2))

    cmd = [str(out / 'openjkded.x86.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game', 'dedicated', 'net_ip', 'net_port'):
        cmd += ['+set', key, str(settings[key])]
    cmd += ['+safe', '+exec', 'cost_start.cfg']

    lines = []
    started = time.monotonic()
    next_probe = started + 2
    added = 0
    with (out / 'server.log').open('x', encoding='utf-8') as log:
        proc = spawn_without_crash_dialogs(
            cmd, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env={k.upper(): v for k, v in os.environ.items()},
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))

        def read():
            for raw in iter(proc.stdout.readline, b''):
                line = raw.decode('utf-8', errors='replace')
                lines.append(line)
                log.write(line)
                log.flush()

        thread = threading.Thread(target=read, daemon=True)
        thread.start()
        try:
            while time.monotonic() - started < args.timeout:
                if proc.poll() is not None or any('MOVECOST] REFUSED' in l for l in lines):
                    break
                if any('[MOVECOST] DONE' in l for l in lines):
                    break
                if time.monotonic() >= next_probe:
                    response = status_response(args.port, password)
                    next_probe = time.monotonic() + 1.25
                    if response:
                        (out / 'status.log').open('a').write(response + '\n')
                        added = len(re.findall(
                            r'^\s*\d+\s+-?\d+\s+(?:\d+|CON|ZMB)\s+.*Alora', response,
                            re.MULTILINE))
                        if added < args.bots:
                            rcon_send(args.port, password,
                                      'addbot alora 4 ' + ('r' if added % 2 == 0 else 'b'),
                                      timeout=.5)
                        elif not any('[MOVECOST] START' in l for l in lines):
                            reply = rcon_send(args.port, password, 'bot_movecost_start', timeout=.5)
                            if reply:
                                lines.append(str(reply))
                                (out / 'rcon.log').open('a').write(str(reply) + '\n')
                time.sleep(.05)
        finally:
            if proc.poll() is None:
                rcon_send(args.port, password, 'quit', timeout=.2)
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    kill_server(proc)
            thread.join(timeout=3)
            share_graphs(base)

    text = ''.join(lines)
    capture = base / 'movement_cost.csv'
    report = dict(complete='[MOVECOST] DONE' in text, refused='MOVECOST] REFUSED' in text,
                  timing_claims_valid=not args.telemetry and not args.trace,
                  bots=args.bots, parity=args.parity, budget_us=args.budget_us,
                  search_budget=args.search_budget, graph_save_defer=args.graph_save_defer,
                  library=bool(args.library), graph=str(args.graph) if args.graph else None,
                  wall_seconds=round(time.monotonic() - started, 3),
                  capture=str(capture) if capture.is_file() else None,
                  graph_line=next((l.strip() for l in lines if '[GRAPH] map_load' in l), None),
                  # Both of these are telemetry's: with printing off, as a
                  # timing world must have it, they are zero and empty and say
                  # nothing about what the searches did.
                  telemetry=bool(args.telemetry),
                  owners=sorted({m for m in re.findall(r'owner=(\w+)', text)}),
                  commits=text.count('reason=sequence'),
                  searches=text.count('[SPIKE]'),
                  parity_mismatches=text.count('event=parity') and
                  len(re.findall(r'event=parity reason=(?!tally|horizon)', text)),
                  start=next((l.strip() for l in lines if '[MOVECOST] START ' in l), None),
                  done=next((l.strip() for l in lines if '[MOVECOST] DONE' in l), None))
    if capture.is_file():
        report['cost'] = summarize(read_capture(capture), args.budget_us)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if report['complete'] and not report['refused'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
