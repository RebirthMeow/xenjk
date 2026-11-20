"""Play CTF against the movement runner on a private localhost server.

Starts a dedicated server on 127.0.0.1 from a built, hash-checked Bot2 game
(`strafe_fixture_build.py` output), stages the runner's bot files and, on
Kejim, the production graph seed and decision library, opens the installed
client with a private home directory, puts you on your team and adds
`Runner_v2` on the other. The runner plays in the gates' production
configuration and fights back when pinned with the flag
(`bot_carrierMelee`). Nothing in the installed game directory is written.

    python tools/play_vs_runner.py --build .codex-research/<build>
    python tools/play_vs_runner.py --stop .codex-research/play-vs-runner-<stamp>

With --defenders, Defender_v2 (base D) and Chaser_v2 (chase D) join the
runner's opponents at their posts (their settings come with --set, as
defense_laps.py sets them); --team spectator watches the three.

The server's writable files live on a fast local drive (`--home`, default
%LOCALAPPDATA%/xenjk-play): measured, the server's small synced
writes (the runner's play log, the server demo) blocked the game thread for
0.8-1.5 s at a time on the busy D: hard disk, which players saw as ping 999.
`--stop` copies the live directory into .codex-research/play-vs-runner-<stamp>/
live: server console log (every [MOVE] line), the runner's play log
(server/base/bot2-play/*.csv) and the server's demos. Nothing is deleted.
Localhost only; never a real server.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from staged_binary import stage_binary  # noqa: E402
from world_retention import copy_session, require_free, share_graphs  # noqa: E402
from bot_test_runner import rcon_send  # noqa: E402
from strafe_fixture_runner import spawn_without_crash_dialogs  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'game_directory/Jedi Academy/GameData'
SEED = ROOT / '.codex-research/move-explore-20260916-b/seed.txt'
LIBRARY = ROOT / '.codex-research/reexplore-chain-20261014-b/library/mp_ctf_kejim_-1301924053'  # re-explored with strafe skill 90
# Prepared graph seeds for the other maps (tools/movement_graph_prepare.py; records/2026-09-27-eight-maps.md):
# <map>/<map>_<checksum>.txt, the built graph without the candidates that failed and never succeeded.
PREPARED = ROOT / '.codex-research/mapprep-20260927-a/seeds'
GUIDE_GOALS = ROOT / 'tools/fixtures/movement/guide'
# The gates' production configuration (records/2026-09-26-gates-current-defaults.md).
# These are also the game DLL's defaults; they stay explicit so older builds run the same way.
PRODUCTION = dict(bot_moveEdges=1, bot_moveParity=0, bot_moveExitSpeed=500,
                  bot_moveStatePrefer=1, bot_moveReroute=1, bot_carrierMelee=1, bot_playDecisionSeed=0,
                  bot_navmeshOff=1)
# Play only, on top of PRODUCTION. Strafe skill is the DLL default, bot_moveStrafeSkill 90
# (about 80% of perfect air strafe).
# Human commands as on a hosted base_enhanced server (default pmove_fixed 0): a fixed server step
# discards every other command of a client above 125 fps, force presses with it. The runner steps
# fixed on its own (addrunner's bot_pmoveFixed; records/2026-09-26-play-env-and-threats.md).
PLAY = dict(pmove_fixed=0)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepared_seed(map_name):
    """The prepared graph seed whose header names this map, or None."""
    for seed in sorted(PREPARED.glob('*/*.txt')):
        if (' map=%s ' % map_name) in seed.read_text(encoding='utf-8', errors='replace').split('\n', 1)[0]:
            return seed
    return None


def stage(session, build, kejim, map_name):
    manifest = sorted(build.glob('*.sources.json'))
    if not manifest:
        raise SystemExit('build must contain a source/binary manifest from strafe_fixture_build.py')
    provenance = json.loads(manifest[-1].read_text())
    base = session / 'server/base'
    for sub in ('scripts', 'botfiles'):
        (base / sub).mkdir(parents=True, exist_ok=True)
    for name, dest in (('openjkded.x86.exe', session), ('jampgamex86.dll', base)):
        stage_binary(build / 'RelWithDebInfo' / name, dest / name)
        if digest(dest / name) != provenance['binaries'][name]:
            raise SystemExit('staged binary does not match the build manifest: ' + name)
    shutil.copy2(manifest[-1], session / 'build.sources.json')
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.bot', base / 'scripts/kejim_runner.bot')
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.jkb', base / 'botfiles/kejim_runner.jkb')
    # The defenders' bot files are read at map load, so they are staged whether or not --defenders adds them.
    shutil.copy2(ROOT / 'tools/fixtures/movement/defender/kejim_defender.bot', base / 'scripts/kejim_defender.bot')
    shutil.copy2(ROOT / 'tools/fixtures/movement/defender/kejim_defender.jkb', base / 'botfiles/kejim_defender.jkb')
    shutil.copy2(ROOT / 'tools/fixtures/movement/defender/kejim_base.jkb', base / 'botfiles/kejim_base.jkb')
    staged = {}
    if kejim:
        header = dict(t.partition('=')[::2] for t in SEED.read_text().split('\n', 1)[0].split()[2:])
        (base / 'bot2-graph').mkdir()
        shutil.copy2(SEED, base / 'bot2-graph' / f"{header['map'].replace('/', '_')}_{header['checksum']}.txt")
        shutil.copytree(LIBRARY, base / 'bot2-decisions' / LIBRARY.name)
        staged = dict(graph=str(SEED), graph_sha256=digest(SEED), library=str(LIBRARY))
    elif prepared_seed(map_name):
        seed = prepared_seed(map_name)
        (base / 'bot2-graph').mkdir()
        shutil.copy2(seed, base / 'bot2-graph' / seed.name)
        staged = dict(graph=str(seed), graph_sha256=digest(seed))
    # The planner guidance's goal list for the staged seed (tools/movement_guide_goals.py); read only when
    # bot_movePlanGuide is on at the load.
    for graph in sorted((base / 'bot2-graph').glob('*.txt')) if (base / 'bot2-graph').is_dir() else ():
        guide = GUIDE_GOALS / (graph.stem + '.guide')
        if guide.is_file():
            shutil.copy2(guide, graph.with_suffix('.guide'))
            staged.update(guide=str(guide), guide_sha256=digest(guide))
    return base, staged


def wait_for(predicate, seconds, what, proc=None):
    for _ in range(int(seconds * 2)):
        if proc is not None and proc.poll() is not None:
            raise SystemExit(what + ': process exited; see the session logs')
        if predicate():
            return
        time.sleep(.5)
    raise SystemExit(what + ': timed out; see the session logs')


def start(args):
    if not (ASSETS / 'openjk.x86.exe').is_file():
        raise SystemExit('game client missing: ' + str(ASSETS / 'openjk.x86.exe'))
    build = args.build.resolve()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', args.port))
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    record = ROOT / '.codex-research' / ('play-vs-runner-' + stamp)
    require_free(args.home, record)
    record.mkdir(parents=True)
    session = Path(args.home).resolve() / ('play-vs-runner-' + stamp)
    kejim = args.map == 'mp/ctf_kejim'
    base, staged = stage(session, build, kejim, args.map)
    password = secrets.token_hex(16)
    settings = dict(dedicated=1, net_ip='127.0.0.1', net_port=args.port, sv_hostname='Bot2 runner test',
                    sv_maxclients=4, sv_pure=0, g_gametype=8, g_doWarmup=0, g_teamForceBalance=0,
                    g_teamAutoJoin=0, capturelimit=0, timelimit=0, fraglimit=0,
                    bot_enable=1, bot_minplayers=0, logfile=0, g_log='play-game.log', g_logSync=1,
                    com_hunkMegs=256, rconpassword=password,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', sv_master5='')
    settings.update(PRODUCTION)
    settings.update(PLAY)
    if kejim:
        settings['bot_moveTapeLibrary'] = 1
    settings.update(dict(item.split('=', 1) for item in args.set))
    (base / 'play_start.cfg').write_text(
        '\n'.join('set %s "%s"' % (k, v) for k, v in settings.items()) + '\ndevmap %s\n' % args.map)
    server_log = session / 'server-console.log'
    with (open(os.devnull, 'wb') if args.quiet_console else server_log.open('xb')) as log:
        server = spawn_without_crash_dialogs(
            [str(session / 'openjkded.x86.exe'), '+set', 'fs_basepath', str(ASSETS),
             '+set', 'fs_homepath', str(session / 'server'), '+set', 'fs_game', 'base',
             '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', str(args.port),
             '+safe', '+exec', 'play_start.cfg'], cwd=session, stdout=log, stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
            env={k.upper(): v for k, v in os.environ.items()})
    info = dict(port=args.port, password=password, server_pid=server.pid, session=str(session), record=str(record),
                build=str(build), map=args.map, team=args.team, settings={k: v for k, v in settings.items()
                                                                        if k != 'rconpassword'},
                staged=staged, client_sha256=digest(ASSETS / 'openjk.x86.exe'), created_utc=stamp)
    (session / 'session.json').write_text(json.dumps(info, indent=2))
    (record / 'session.json').write_text(json.dumps(info, indent=2))
    token = 'PLAY_READY_' + stamp
    wait_for(lambda: token in str(rcon_send(args.port, password, 'echo ' + token, timeout=1)),
             60, 'server start', server)
    print('SERVER ' + str(record) + ' (live files: ' + str(session) + ')', flush=True)

    if args.no_client:
        # Server-side repro: the runner alone, no human, for timing and stall checks.
        reply = str(rcon_send(args.port, password, 'addrunner blue', timeout=15, collect=.5))
        (session / 'addrunner.txt').write_text(reply)
        if 'Adding blue flag runner' not in reply:
            raise SystemExit('runner not added: ' + reply.strip()[:300])
        print('READY (no client): Runner_v2 is blue on %s.' % args.map, flush=True)
        print('Stop with: python tools/play_vs_runner.py --stop ' + str(record), flush=True)
        return
    client_home = session / 'client/base'
    client_home.mkdir(parents=True)
    if (ASSETS / 'base/jampconfig.cfg').exists():
        shutil.copy2(ASSETS / 'base/jampconfig.cfg', client_home / 'jampconfig.cfg')
    extra = []
    if args.client_exec:
        # A scripted client (probes): the script runs from the client's private home.
        shutil.copy2(args.client_exec, client_home / args.client_exec.name)
        extra = ['+exec', args.client_exec.name]
    options = {}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 1  # the player's own game window
        options['startupinfo'] = startup
    with (session / 'client-console.log').open('ab') as log:
        client = spawn_without_crash_dialogs(
            [str(ASSETS / 'openjk.x86.exe'), '+set', 'fs_basepath', str(ASSETS),
             '+set', 'fs_homepath', str(session / 'client'), '+set', 'fs_game', 'base',
             '+set', 'r_fullscreen', '0', '+set', 'logfile', '2', '+set', 'cl_autoRecordDemo', '0',
             '+set', 'cl_motd', '0', '+connect', '127.0.0.1:' + str(args.port)] + extra, cwd=ASSETS, stdout=log, stderr=subprocess.STDOUT,
            env={k.upper(): v for k, v in os.environ.items()}, **options)
    wait_for(lambda: re.search(r'ClientBegin: \d+', server_log.read_text(errors='replace')), 120,
             'client join', client)
    human = re.search(r'ClientBegin: (\d+)', server_log.read_text(errors='replace')).group(1)
    rcon_send(args.port, password, 'forceteam %s %s' % (human, args.team), timeout=2)
    time.sleep(1)
    # --offense: the runner is added on your team (it sets the world up) and kicked once the defenders are in;
    # with no runner bot left, the defenders take you for the runner.
    runner_team = args.team if args.offense else 'red' if args.team == 'blue' else 'blue'
    reply = str(rcon_send(args.port, password, 'addrunner ' + runner_team, timeout=15, collect=.5))
    (session / 'addrunner.txt').write_text(reply)
    if 'Adding %s flag runner' % runner_team not in reply:
        raise SystemExit('runner not added: ' + reply.strip()[:300])
    if args.defenders:
        defender_team = 'blue' if runner_team == 'red' else 'red'
        for role in args.roles.split(','):
            reply = str(rcon_send(args.port, password, 'adddefender ' + defender_team + (' chase' if role == 'chase' else ''),
                                  timeout=5, collect=.5))
            if 'Adding' not in reply:
                raise SystemExit('defender not added: ' + reply.strip()[:300])
        time.sleep(3)
        rcon_send(args.port, password, 'defenderpost', timeout=5, collect=.5)
        print('%s %s %s.' % (' and '.join({'base': 'Defender_v2', 'chase': 'Chaser_v2'}[r] for r in args.roles.split(',')),
                             'are' if ',' in args.roles else 'is', defender_team), flush=True)
    if args.offense:
        rcon_send(args.port, password, 'kick Runner_v2', timeout=5, collect=.5)
        print('READY: you are %s on offense against the defenders on %s.' % (args.team, args.map), flush=True)
    else:
        print('READY: you are %s, Runner_v2 is %s on %s.' % (args.team, runner_team, args.map), flush=True)
    print('Stop with: python tools/play_vs_runner.py --stop ' + str(record), flush=True)


def wait_for_exit(pid, seconds):
    """Wait for a process to exit without signalling it; True once it has.

    Never os.kill(pid, 0): on Windows a signal the console cannot deliver
    falls through to TerminateProcess, which would cut the server's shutdown
    graph save short. A process handle is waited on instead.
    """
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        synchronize, wait_timeout = 0x00100000, 0x00000102
        handle = kernel32.OpenProcess(synchronize, False, pid)
        if not handle:
            return True  # no such process: already gone
        try:
            return kernel32.WaitForSingleObject(handle, int(seconds * 1000)) != wait_timeout
        finally:
            kernel32.CloseHandle(handle)
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)  # POSIX: signal 0 only probes
        except OSError:
            return True
        time.sleep(.5)
    return False


def stop(session):
    info = json.loads((session / 'session.json').read_text())
    try:
        rcon_send(info['port'], info['password'], 'quit', timeout=2)
    except Exception as error:  # the server may already be gone
        print('quit not acknowledged: %r' % error)
    live = Path(info['session'])
    if live.resolve() != session.resolve():
        # Wait for the server to close its files, then copy the live evidence
        # beside the record; the live copy is kept (archive-only policy).
        if not wait_for_exit(info['server_pid'], 60):
            print('server %d still running after 60 s; copying what it has written so far' % info['server_pid'])
        copy_session(live, session / 'live')
        share_graphs(session / 'live' / 'server' / 'base')
    print('Stopped. Evidence retained in ' + str(session))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--map', default='mp/ctf_kejim')
    parser.add_argument('--team', choices=('red', 'blue', 'spectator'), default='red',
                        help='your team (default red); spectator watches a blue runner')
    parser.add_argument('--defenders', action='store_true',
                        help='add base D and chase D on the opposing team to the runner (Kejim; settings with --set)')
    parser.add_argument('--offense', action='store_true',
                        help="with --defenders: you take the runner's place (no Runner_v2); Kejim's posts are red, so use --team blue")
    parser.add_argument('--roles', choices=('base', 'chase', 'base,chase'), default='base,chase',
                        help='which defenders --defenders adds (default both; one alone needs --set bot_defenderRole=0)')
    parser.add_argument('--port', type=int, default=29095)
    parser.add_argument('--home', default=os.path.join(os.environ.get('LOCALAPPDATA', str(ROOT / '.codex-research')),
                                                       'xenjk-play'),
                        help='fast local directory for the server and client writable files')
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE')
    parser.add_argument('--quiet-console', action='store_true',
                        help='discard the server console instead of writing server-console.log (with --no-client only)')
    parser.add_argument('--no-client', action='store_true',
                        help='start the server and the runner only (stall and timing checks)')
    parser.add_argument('--client-exec', type=Path, metavar='CFG',
                        help='copy this config into the client home and exec it after connecting (scripted probes)')
    parser.add_argument('--stop', type=Path, metavar='SESSION')
    args = parser.parse_args()
    if args.stop:
        return stop(args.stop.resolve())
    if not args.build:
        parser.error('--build is required to start a session')
    if args.offense and (not args.defenders or args.team == 'spectator'):
        parser.error('--offense needs --defenders and a team')
    if args.quiet_console and not args.no_client:
        # The client join is read from server-console.log, which --quiet-console never writes.
        parser.error('--quiet-console needs --no-client: the client join is read from the server console')
    start(args)


if __name__ == '__main__':
    main()
