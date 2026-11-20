"""Start an isolated Kejim human-route recording session (no game installation).

F8 arms the first-input-to-enemy-pickup recorder, F9 stops, F10 shows status.
Recordings live in the printed session/server/base/bot2-route directory.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import time

from staged_binary import stage_binary
from bot_test_runner import rcon_send
from strafe_fixture_runner import spawn_without_crash_dialogs

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'game_directory/Jedi Academy/GameData'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def latest_build():
    candidates = [p for p in (ROOT / '.codex-research').glob('move-build-human-route-*')
                  if list(p.glob('*.sources.json'))]
    if not candidates:
        raise SystemExit('No recorder build found. Build with tools/strafe_fixture_build.py --out .codex-research/move-build-human-route-<unique-name>')
    return max(candidates, key=lambda p: max(f.stat().st_mtime for f in p.glob('*.sources.json')))


def connect(session, info, scripted_check=False):
    """Launch the installed client with a private home/config and this server."""
    client_base = session / 'client/base'
    client_base.mkdir(parents=True, exist_ok=True)
    config = client_base / 'jampconfig.cfg'
    original = ASSETS / 'base/jampconfig.cfg'
    if not config.exists() and original.exists():
        shutil.copy2(original, config)
    bindings = client_base / 'human_route_keys.cfg'
    bindings.write_text('bind F8 "cmd route_record arm"\n'
                        'bind F9 "cmd route_record stop"\n'
                        'bind F10 "cmd route_record status"\n'
                        'echo "Route recorder: F8 arm, F9 stop, F10 status"\n')
    action = 'exec human_route_check.cfg' if scripted_check else ''
    if scripted_check:
        (client_base / 'human_route_check.cfg').write_text(
            'team ' + info['team'] + '\nwait 1500\ntogglemenu\nwait 1500\ncmd route_record arm\nwait 1500\n'
            '+forward\nwait 500\n-forward\ncmd route_record stop\nwait 1500\n'
            # Declared synthetic pickup fixture, positioned BEFORE arming.
            'setviewpos 2464 5000 1924 90\nwait 1500\n'
            'cmd route_record arm\nwait 1500\n+forward\nwait 1500\n-forward\n'
            'wait 1500\ncmd route_record stop\nwait 1500\n'
            'cmd route_record arm\nwait 1500\nquit\n')
    command = [str(ASSETS / 'openjk.x86.exe'), '+set', 'fs_basepath', str(ASSETS),
               '+set', 'fs_homepath', str(session / 'client'), '+set', 'fs_game', 'base',
               '+set', 'r_fullscreen', '0', '+set', 'logfile', '2',
               '+set', 'timescale', '1', '+set', 'timedemo', '0', '+set', 'fixedtime', '0',
               '+set', 'cl_autoRecordDemo', '0', '+set', 'activeAction', action,
               '+exec', 'human_route_keys.cfg', '+set', 'cl_motd', '0', '+connect', '127.0.0.1:' + str(info['port'])]
    with (session / 'client-console.log').open('ab') as log:
        client_options = {}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 1  # This is the user's interactive game window.
            client_options['startupinfo'] = startup
        client = spawn_without_crash_dialogs(command, cwd=ASSETS, stdout=log,
                                             stderr=subprocess.STDOUT,
                                             env={k.upper(): v for k, v in os.environ.items()},
                                             **client_options)
    for _ in range(90):
        if client.poll() is not None:
            raise RuntimeError('Client exited before joining; inspect ' + str(session / 'client-console.log'))
        # This private world has one slot and no bots; only our human client can occupy it.
        response = str(rcon_send(info['port'], info['password'], 'status', timeout=1))
        import re
        server_log = (session / 'server-console.log').read_text(errors='replace')
        if (re.search(r'^\s*0\s+-?\d+\s+\d+\s+', response, re.MULTILINE)
                and 'ClientBegin: 0' in server_log):
            if not scripted_check:
                rcon_send(info['port'], info['password'], 'forceteam 0 ' + info['team'], timeout=2)
            print('Connected. Stand still at your home flag, press F8, then run to the enemy flag.', flush=True)
            print('F9 stops an attempt; F10 shows status. F8 can arm another attempt.', flush=True)
            return client
        time.sleep(0.5)
    raise RuntimeError('Client did not join. Inspect the retained client/server logs.')


def start(args):
    if not (ASSETS / 'openjk.x86.exe').is_file():
        raise SystemExit('Game client missing: ' + str(ASSETS / 'openjk.x86.exe'))
    build = args.build.resolve() if args.build else latest_build()
    manifests = sorted(build.glob('*.sources.json'))
    if not manifests:
        raise SystemExit('Recorder build must contain a source/binary manifest')
    manifest = manifests[-1]
    provenance = json.loads(manifest.read_text())
    for name, expected in provenance['binaries'].items():
        if digest(build / 'RelWithDebInfo' / name) != expected:
            raise SystemExit('Build binary does not match its manifest: ' + name)
    # Refuse port reuse rather than relying on the engine's automatic port increment.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', args.port))
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    session = ROOT / '.codex-research' / ('human-route-' + stamp)
    base = session / 'server/base'
    base.mkdir(parents=True, exist_ok=False)
    for name, dest in [('openjkded.x86.exe', session), ('jampgamex86.dll', base)]:
        stage_binary(build / 'RelWithDebInfo' / name, dest / name)
        if digest(dest / name) != provenance['binaries'][name]:
            raise RuntimeError('Staged binary hash mismatch')
    shutil.copy2(manifest, session / 'build.sources.json')
    shutil.copy2(Path(__file__), session / 'recorder-launcher.py')
    shutil.copy2(Path(__file__).with_name('staged_binary.py'), session / 'staged_binary.py')
    password = secrets.token_hex(16)
    settings = dict(dedicated=1, net_ip='127.0.0.1', net_port=args.port,
                    sv_hostname='Human route recording', sv_maxclients=1, sv_pure=0,
                    sv_fps=40, g_gametype=8, g_doWarmup=0, g_teamForceBalance=0,
                    g_teamAutoJoin=0, capturelimit=0, timelimit=0, fraglimit=0,
                    g_speed=250, g_gravity=800, g_forceRegenTime=200,
                    g_stepSlideFix=1, g_forcePowerDisable=0, g_maxForceRank=7,
                    g_forceBasedTeams=0, g_weaponDisable=0, g_jediVmerc=0,
                    pmove_fixed=1, pmove_msec=8, pmove_float=1,
                    fixedtime=0, timedemo=0, timescale=1,
                    bot_enable=1, bot_minplayers=0, bot_routeRecord=1,
                    logfile=2, g_log='human-route-game.log', g_logSync=1,
                    sv_autoDemo=1, sv_autoDemoBots=0, sv_autoDemoMaxMaps=0,
                    com_hunkMegs=256, rconpassword=password,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', sv_master5='')
    (base / 'human_route_start.cfg').write_text(
        '\n'.join('set %s "%s"' % (k, v) for k, v in settings.items()) + '\ndevmap mp/ctf_kejim\n')
    with (session / 'server-console.log').open('xb') as log:
        server = spawn_without_crash_dialogs(
            [str(session / 'openjkded.x86.exe'), '+set', 'fs_basepath', str(ASSETS),
             '+set', 'fs_homepath', str(session / 'server'), '+set', 'fs_game', 'base',
             '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', str(args.port),
             '+safe', '+exec', 'human_route_start.cfg'], cwd=session, stdout=log,
            stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
            env={k.upper(): v for k, v in os.environ.items()})
    info = dict(port=args.port, password=password, server_pid=server.pid,
                session=str(session), build=str(build), created_utc=stamp,
                team=args.team, settings=settings, manifest_sha256=digest(manifest),
                launcher_sha256=digest(session / 'recorder-launcher.py'),
                staging_helper_sha256=digest(session / 'staged_binary.py'),
                client_sha256=digest(ASSETS / 'openjk.x86.exe'), scripted_check=args.scripted_check)
    (session / 'session.json').write_text(json.dumps(info, indent=2))
    token = 'HUMAN_ROUTE_READY_' + stamp
    for _ in range(90):
        if server.poll() is not None:
            raise RuntimeError('Server exited; inspect ' + str(session / 'server-console.log'))
        response = str(rcon_send(args.port, password, 'echo ' + token, timeout=1))
        if token in response:
            print('READY ' + str(session), flush=True)
            print('Recordings: ' + str(base / 'bot2-route'), flush=True)
            if not args.no_client:
                client = connect(session, info, args.scripted_check)
                if args.scripted_check:
                    client.wait(timeout=90)
                    rcon_send(args.port, password, 'quit', timeout=1)
                    server.wait(timeout=15)
                    summaries = list((base / 'bot2-route').glob('*.summary.json'))
                    if not summaries:
                        raise RuntimeError('No route summary produced by scripted check; evidence retained')
                    print('Scripted recorder check produced: ' + str(summaries[0]), flush=True)
                    outcomes = [json.loads(p.read_text())['reason'] for p in summaries]
                    if not {'manual_stop', 'enemy_flag_pickup', 'disconnect'}.issubset(outcomes):
                        raise RuntimeError('Missing scripted lifecycle outcomes: ' + str(outcomes))
                    print('Lifecycle checks passed: ' + ', '.join(outcomes), flush=True)
            return
        time.sleep(0.5)
    raise RuntimeError('Server authentication/readiness failed; evidence retained in ' + str(session))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--port', type=int, default=29090)
    parser.add_argument('--team', choices=('red', 'blue'), default='red')
    parser.add_argument('--no-client', action='store_true')
    parser.add_argument('--scripted-check', action='store_true', help=argparse.SUPPRESS)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--connect', type=Path, metavar='SESSION')
    group.add_argument('--stop', type=Path, metavar='SESSION')
    args = parser.parse_args()
    from world_retention import require_free   # both drives, as defense_laps
    require_free(ROOT / '.codex-research')
    if args.scripted_check and (args.team != 'red' or args.no_client):
        parser.error('The synthetic lifecycle check requires a red client.')
    if args.connect or args.stop:
        session = (args.connect or args.stop).resolve()
        info = json.loads((session / 'session.json').read_text())
        if args.stop:
            rcon_send(info['port'], info['password'], 'quit', timeout=1)
            print('Shutdown requested. All recordings remain in ' + str(session))
        else:
            connect(session, info)
    else:
        start(args)


if __name__ == '__main__':
    main()
