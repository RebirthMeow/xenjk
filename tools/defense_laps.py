"""Laps worlds for the fixture defenders: the runner laps Kejim against base D
and chase D for a fixed stretch of game time, many lives on both sides.

The bench (movement_bench.py --defender) runs one trip per world, where a
defender dies at most once; rearming, self-kill resets and mines re-laid for
each approach only show over many lives. Records
docs/movement/records/2026-09-29-defender-learnings.md.

    python tools/defense_laps.py --build .codex-research/<build> --out .codex-research/<dir> [--game-seconds 600]
        [--defender-rearm] [--defender-reset] [--defender-mines 3] [--chaser-ambush] [--set CVAR=VALUE ...]

The server runs on 127.0.0.1 from a hash-checked build (play_vs_runner.py
staging, production runner settings), at botplayrate --rate (default 5; 12.5
and 25 run as fast as the CPU allows), always a 25 ms game step; the world
lasts --game-seconds of game time, read from the server (botleveltime).
The runner is blue; Defender_v2 (base D) and Chaser_v2 (chase D) are red,
with the roles campaign's settings. Writes the live files
under --out/live and summary.json; nothing is deleted. Localhost only.
"""
import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
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
import play_vs_runner as pvr  # noqa: E402
import movement_bench as mb  # noqa: E402
from world_retention import copy_session, drop_staging, require_free, share_graphs  # noqa: E402

MAP = 'mp/ctf_kejim'
RUNNER_TEAM, DEFENDER_TEAM = 'blue', 'red'
RATE = 5
# The roles campaign's defender settings.
DEFENDERS = dict(bot_defender=2, bot_defenderReactMs=250, bot_defenderRole=1, bot_defenderStrafe=1, bot_defenderSpeed=1,
                 bot_defenderGuard=1, bot_carrierMelee=1, bot_carrierMeleeRange=96, bot_defenderPullCarrier=350,
                 bot_defenderPullMax=700, bot_aimStyle=2, bot_aimErrorScale=1, bot_aimGate=2, bot_defenderWeapon=11)
# The performance spec: the server on two cores of the dev CPU. Logical CPUs 2 and 4 are two
# separate performance cores of the i7-12700K (0-15 are eight cores' hyperthread pairs, 16-19 efficiency cores);
# CPU 0 is left to Windows. Timing worlds pass --cpus spec; outcome worlds may use more. The engine's own com_affinity
# (a hex mask, applied at startup) does it: a mask set on the process from outside is reset by the engine's init.
SPEC_CPUS = (2, 4)


def parse_cpus(text):
    cpus = SPEC_CPUS if text == 'spec' else tuple(int(c) for c in text.split(','))
    if not cpus or min(cpus) < 0 or max(cpus) >= (os.cpu_count() or 1):
        raise argparse.ArgumentTypeError('logical CPUs 0..%d, comma-separated, or "spec"' % ((os.cpu_count() or 1) - 1))
    return cpus


def summarise(live):
    base = live / 'server' / 'base'
    console = (live / 'server-console.log').read_text(errors='replace')
    game = (base / 'play-game.log').read_text(errors='replace') if (base / 'play-game.log').exists() else ''
    # "Kill: a v m: <killer> killed <victim> by MOD" (names carry no spaces here)
    kills = [(k, v, re.sub(r'\^\d', '', killer), re.sub(r'\^\d', '', victim), mod) for k, v, killer, victim, mod in
             re.findall(r'Kill: (\d+) (\d+) \d+: (\S+) killed (\S+) by (\S+)', game)]
    names = {int(v): victim for k, v, killer, victim, mod in kills}
    names.update({int(k): killer for k, v, killer, victim, mod in kills if int(k) < 64})
    for c, n in re.findall(r'ClientConnect: (\d+) \[[^\]]*\] \([^)]*\) "([^"]+)"', game):
        names[int(c)] = re.sub(r'\^\d', '', n)
    items = re.findall(r'Item: (\d+) (\S+)', game)
    runner = next((c for c, n in names.items() if n == 'Runner_v2'), None)
    out = dict(game_seconds=None,
               runner_deaths=Counter(m for k, v, killer, victim, m in kills if victim == 'Runner_v2'),
               runner_killed_by=Counter(killer for k, v, killer, victim, m in kills if victim == 'Runner_v2'),
               defender_deaths={n: Counter(m for k, v, killer, victim, m in kills if victim == n) for n in ('Defender_v2', 'Chaser_v2')},
               captures=max((int(r['captures']) for f in base.glob('bot2-play/*.csv')
                             for r in csv.DictReader(f.open(encoding='utf-8')) if r['movement_only'] == '1'), default=0),
               runner_pickups=sum(1 for c, i in items if runner is not None and int(c) == runner and i == 'team_CTF_redflag'),
               # Touches of the runner team's own flag: captures in a one-sided world (no own flag ever drops).
               team_captures=Counter(names.get(int(c), c) for c, i in items if i == 'team_CTF_blueflag'),
               red_flag_pickups=Counter(names.get(int(c), c) for c, i in items if i == 'team_CTF_redflag'),
               camper_deaths=Counter(m for k, v, killer, victim, m in kills if victim == 'Camper_v2'),
               camper_kills=Counter(victim for k, v, killer, victim, m in kills if killer == 'Camper_v2' and victim != 'Camper_v2'),
               defender_items={n: Counter(i for c, i in items if names.get(int(c)) == n) for n in ('Defender_v2', 'Chaser_v2')},
               lines=Counter(re.findall(r'\[DEFENDER\] ([A-Z_]+)', console)))
    # Captures per bot and per team from the play CSV (native PERS_CAPTURES, every client logged):
    # the two-sided worlds' measure, where touches of a flag are also returns.
    last = {}
    for f in base.glob('bot2-play/*.csv'):
        for r in csv.DictReader(f.open(encoding='utf-8')):
            last[int(r['client'])] = (int(r['team']), int(r['captures'] or 0))
    out['captures_by_name'] = {names.get(c, str(c)): caps for c, (team, caps) in last.items() if caps}
    out['captures_by_team'] = {t: sum(caps for team, caps in last.values() if team == k) for k, t in ((1, 'red'), (2, 'blue'))}
    out['runner2_deaths'] = Counter(m for k, v, killer, victim, m in kills if victim == 'Runner2_v2')
    out['defender2_deaths'] = Counter(m for k, v, killer, victim, m in kills if victim == 'Defender2_v2')
    level = [int(x) for x in re.findall(r'\[DEFENDER\] \w+ level=(\d+)', console)]
    if level:
        out['game_seconds'] = (max(level) - min(level)) / 1000
    out['camper_goals'] = Counter(re.findall(r'\[DEFENDER\] CAMPER level=\d+ goal=(\w+)', console))
    for kind in ('REARM', 'RESET', 'MINE', 'AMBUSH'):
        out[kind.lower()] = re.findall(r'\[DEFENDER\] %s ([^\n]*)' % kind, console)[:200]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--game-seconds', type=float, default=600)
    ap.add_argument('--rate', type=float, default=RATE, choices=(1, 2.5, 5, 12.5, 25),
                    help='botplayrate: game seconds per wall second, at most (the game step stays 25 ms)')
    ap.add_argument('--map', default=MAP, help='a map with defender posts in tools/fixtures/movement/defender/posts.json; '
                    'Kejim stages its seed and tape library, other maps their prepared seed')
    ap.add_argument('--port', type=int, default=29110)
    ap.add_argument('--home', default=os.path.join(os.environ.get('LOCALAPPDATA', str(pvr.ROOT / '.codex-research')), 'xenjk-play'))
    ap.add_argument('--keep-staging', action='store_true',
                    help='keep the staging folder under --home after the world is copied to --out (default: removed)')
    ap.add_argument('--roles', choices=('base', 'chase', 'base,chase'), default='base,chase')
    ap.add_argument('--camper', action='store_true', help="Camper_v2 on the runner's team (milestone B), at posts.json camper_posts")
    ap.add_argument('--two-sided', action='store_true',
                    help='milestone B2: a red runner (Runner2_v2), and --roles mirrored on blue (Defender2_v2, Chaser2_v2 at '
                         "the blue posts, mine spots and ambush posts); a true 2v2 is --roles chase (one chaser and one runner a side)"
                         '; the Kejim tape library off (records/2026-10-02-b2-structure.md)')
    ap.add_argument('--seed', type=int, default=0,
                    help='a replayable world: g_randomSeed N, and the bots added and released at fixed game times '
                         '(botat in play_start.cfg, at 1x) instead of over rcon; two runs alike replay alike')
    ap.add_argument('--kill-before-release', metavar='NAME', action='append', default=[],
                    help="with --seed: the named bot kills itself 100 ms before defenderpost (a defender dead at the "
                         "release); repeatable")
    ap.add_argument('--defender-rearm', action='store_true')
    ap.add_argument('--defender-reset', action='store_true')
    ap.add_argument('--defender-mines', type=int, default=0)
    ap.add_argument('--defender-walk', action='store_true')
    ap.add_argument('--defender-patience', action='store_true')
    ap.add_argument('--chaser-ambush', action='store_true')
    ap.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE')
    ap.add_argument('--cpus', type=parse_cpus, metavar='LIST|spec',
                    help='pin the server to these logical CPUs (comma-separated), or "spec" for the performance spec, two '
                         'cores %s; unpinned by default' % ','.join(map(str, SPEC_CPUS)))
    ap.add_argument('--scenario', metavar='FILE:NAME',
                    help='start from a recorded moment instead of the spawns: the runner (origin, yaw, velocity, carrying, '
                         'force, speed_ms) through bot_corner_start and each defender (origin, yaw, velocity) at its post, '
                         'all in one frame (tools/fixtures/movement/defender/scenarios.json); a role\'s "state" (health, force, '
                         'speed_ms, weapon, air) and the runner\'s "health" restore the fight as well')
    args = ap.parse_args()
    # The side the unnumbered defender cvars (Defender_v2, Chaser_v2) hold. (--vs-legacy, our team on blue against
    # classic Bot2 opponents, went with the classic Bot2.)
    side = DEFENDER_TEAM
    if args.out.exists():
        raise SystemExit('refusing to reuse ' + str(args.out))
    require_free(args.home, args.out)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', args.port))
    build = args.build.resolve()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    # Parallel worlds start in the same second, and scenario arms share world names.
    session = Path(args.home).resolve() / ('defense-laps-%s-%s-%s' % (stamp, args.out.name, secrets.token_hex(3)))
    base, staged = pvr.stage(session, build, args.map == MAP, args.map)
    shutil.copy2(mb.DEFENDER / 'kejim_defender.bot', base / 'scripts/kejim_defender.bot')
    shutil.copy2(mb.DEFENDER / 'kejim_defender.jkb', base / 'botfiles/kejim_defender.jkb')
    shutil.copy2(mb.DEFENDER / 'kejim_base.jkb', base / 'botfiles/kejim_base.jkb')
    fixture = json.loads((mb.DEFENDER / 'posts.json').read_text(encoding='utf-8'))
    join = lambda pts: ';'.join(' '.join(format(v, 'g') for v in p) for p in pts)
    roles = args.roles.split(',')
    password = secrets.token_hex(16)
    settings = dict(dedicated=1, net_ip='127.0.0.1', net_port=args.port, sv_hostname='Bot2 defense laps', sv_maxclients=8,
                    sv_pure=0, g_gametype=8, g_doWarmup=0, g_teamForceBalance=0, g_teamAutoJoin=0, capturelimit=0,
                    timelimit=0, fraglimit=0, bot_enable=1, bot_minplayers=0, logfile=0, g_log='play-game.log',
                    g_logSync=1, com_hunkMegs=256, rconpassword=password,
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='', sv_master5='', bot_moveTapeLibrary=1)
    settings.update(pvr.PRODUCTION)
    settings.update(pvr.PLAY)
    settings.update(DEFENDERS)
    settings.update(bot_defenderRole=int('base' in roles),
                    bot_defenderPost=' '.join(format(v, 'g') for v in fixture['posts'][args.map][side]),
                    bot_defenderPosts=join(fixture['guard_posts_2v2'][args.map][side]),
                    bot_chaserPost=' '.join(format(v, 'g') for v in fixture['chase_posts'][args.map][side][0]),
                    bot_chaserPosts=join(fixture['chase_posts'][args.map][side]))
    if args.two_sided:
        settings.update(bot_moveTapeLibrary=0)
    if args.two_sided:
        if 'base' in roles:
            settings.update(bot_defender2Post=' '.join(format(v, 'g') for v in fixture['posts'][args.map][RUNNER_TEAM]),
                            bot_defender2Posts=join(fixture['guard_posts_2v2'][args.map][RUNNER_TEAM]),
                            bot_defender2MineSpots=join(fixture['mine_spots'][args.map][RUNNER_TEAM]))
        if 'chase' in roles:
            settings.update(bot_chaser2Post=' '.join(format(v, 'g') for v in fixture['chase_posts'][args.map][RUNNER_TEAM][0]),
                            bot_chaser2Posts=join(fixture['chase_posts'][args.map][RUNNER_TEAM]))
            if args.chaser_ambush and args.map in fixture['ambush_posts']:
                settings.update(bot_chaser2AmbushPosts=join(fixture['ambush_posts'][args.map][RUNNER_TEAM]))
    if args.camper:
        settings.update(bot_camperPost=' '.join(format(v, 'g') for v in fixture['camper_posts'][args.map][RUNNER_TEAM][0]),
                        bot_camperPosts=join(fixture['camper_posts'][args.map][RUNNER_TEAM]))
        if args.two_sided:   # the 4v4: a red camper (Camper2_v2) too
            settings.update(bot_camper2Post=' '.join(format(v, 'g') for v in fixture['camper_posts'][args.map][DEFENDER_TEAM][0]),
                            bot_camper2Posts=join(fixture['camper_posts'][args.map][DEFENDER_TEAM]))
    # Ambush posts are Kejim's chasm; elsewhere chase D waits on its own post.
    args.chaser_ambush = args.chaser_ambush and args.map in fixture['ambush_posts']
    settings.update(mb.defender_learning_settings(args, side))
    scenario = None
    if args.scenario:
        path, name = args.scenario.rsplit(':', 1)
        scenario = next(c for c in json.loads(Path(path).read_text(encoding='utf-8'))['scenarios'] if c['name'] == name)
        fmt = lambda v: ' '.join(format(x, 'g') for x in v)
        for role, prefix in (('base', 'bot_defender'), ('chase', 'bot_chaser'), ('camper', 'bot_camper')):
            if role in scenario:
                d = scenario[role]
                settings[prefix + 'Post'] = fmt(list(d['origin']) + [d.get('yaw', 0)])
                settings[prefix + 'PostVelocity'] = fmt(d.get('velocity', [0, 0, 0]))
                if d.get('state'):
                    settings[prefix + 'PostState'] = ' '.join('%s=%s' % kv for kv in d['state'].items())
        r = scenario['runner']
        settings.update(bot_cornerYaw=r.get('yaw', 0), bot_cornerVelocity=fmt(r.get('velocity', [0, 0, 0])),
                        bot_cornerSpeedExpiry=r.get('speed_ms', 0))
        if r.get('health'):
            settings.update(bot_cornerHealth=r['health'])
        if r.get('air'):
            settings.update(bot_cornerAir=1)
    settings.update(dict(item.split('=', 1) for item in args.set))
    if args.kill_before_release and not args.seed:
        raise SystemExit('--kill-before-release needs --seed')
    setup = []   # (level ms, command): --seed's fixed-time setup, the same commands the rcon path sends
    if args.seed:
        if scenario:
            raise SystemExit('--seed does not take --scenario')
        settings['g_randomSeed'] = args.seed
        cmds = ['addrunner ' + RUNNER_TEAM] + ['adddefender %s%s' % (side, ' chase' if r == 'chase' else '') for r in roles]
        if args.camper:
            cmds.append('adddefender %s camper' % RUNNER_TEAM)
        if args.two_sided:
            cmds += ['addrunner ' + DEFENDER_TEAM] + ['adddefender %s%s' % (RUNNER_TEAM, ' chase' if r == 'chase' else '') for r in roles]
            if args.camper:
                cmds.append('adddefender %s camper' % DEFENDER_TEAM)
        setup = [(2000 + 500 * i, c) for i, c in enumerate(cmds)]
        setup += [(setup[-1][0] + 2900, 'botkill ' + name) for name in args.kill_before_release]
        setup.append(((setup[-1][0] + 100) if args.kill_before_release else (setup[-1][0] + 3000), 'defenderpost'))
        # The rate change too: its frame is the first accelerated one, at a fixed game time.
        setup.append((setup[-1][0] + 500, 'botplayrate %g' % args.rate))
    (base / 'play_start.cfg').write_text('\n'.join('set %s "%s"' % (k, v) for k, v in settings.items()) + '\ndevmap %s\n' % args.map +
                                         ''.join('botat %d %s\n' % (t, c) for t, c in setup))
    args.out.mkdir(parents=True)
    info = dict(build=str(build), session=str(session), map=args.map, port=args.port, roles=args.roles, game_seconds=args.game_seconds,
                two_sided=bool(args.two_sided),   # the declared roster laps_compare checks the game log against
                rate=args.rate, staged=staged, created_utc=stamp, cpus=list(args.cpus) if args.cpus else None,
                settings={k: v for k, v in settings.items() if k != 'rconpassword'})
    (args.out / 'session.json').write_text(json.dumps(info, indent=2))
    log = (session / 'server-console.log').open('xb')
    server = pvr.spawn_without_crash_dialogs(
        [str(session / 'openjkded.x86.exe'), '+set', 'fs_basepath', str(pvr.ASSETS), '+set', 'fs_homepath', str(session / 'server'),
         '+set', 'fs_game', 'base', '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', str(args.port),
         '+set', 'com_affinity', '%X' % sum(1 << c for c in args.cpus or ()),
         '+safe', '+exec', 'play_start.cfg'], cwd=session, stdout=log, stderr=subprocess.STDOUT,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), env={k.upper(): v for k, v in os.environ.items()})
    rcon = lambda cmd, **kw: str(pvr.rcon_send(args.port, password, cmd, **kw))
    stopped = False
    try:
        token = 'LAPS_READY_' + stamp
        pvr.wait_for(lambda: token in rcon('echo ' + token, timeout=1), 60, 'server start', server)
        if setup:
            def level_now():
                match = re.search(r'LEVEL_TIME level=(\d+)', rcon('botleveltime', timeout=5, collect=.2))
                return int(match.group(1)) if match else 0
            pvr.wait_for(lambda: level_now() > setup[-1][0] + 500, 120, 'fixed-time setup', server)
            for line in (session / 'server-console.log').read_text(errors='replace').splitlines():
                if re.search(r'\[BOT2 PLAY\] AT |\[DEFENDER\] (REFUSED|RELEASE) ', line):
                    print(line.strip(), flush=True)
        else:
            if 'Adding blue flag runner' not in rcon('addrunner ' + RUNNER_TEAM, timeout=15, collect=.5):
                raise SystemExit('runner not added')
            for role in roles:
                rcon('adddefender %s%s' % (side, ' chase' if role == 'chase' else ''), timeout=5, collect=.5)
            if args.camper:
                rcon('adddefender %s camper' % RUNNER_TEAM, timeout=5, collect=.5)
            if args.two_sided:
                if 'Adding red flag runner' not in rcon('addrunner ' + DEFENDER_TEAM, timeout=15, collect=.5):
                    raise SystemExit('second runner not added')
                for role in roles:
                    rcon('adddefender %s%s' % (RUNNER_TEAM, ' chase' if role == 'chase' else ''), timeout=5, collect=.5)
                if args.camper:
                    rcon('adddefender %s camper' % DEFENDER_TEAM, timeout=5, collect=.5)
            time.sleep(3)
        if setup:
            pass   # added and released by botat above
        elif scenario:
            # The runner's corner start releases the defenders at their posts in the same frame.
            r = scenario['runner']
            reply = rcon('bot_corner_start %s %d %d' % (' '.join(format(x, 'g') for x in r['origin']), r.get('force', 100),
                                                         r.get('carrying', 0)), timeout=5, collect=.5)
            print('SCENARIO %s: %s' % (scenario['name'], reply.strip()[:300]), flush=True)
            for line in reply.splitlines():
                if re.search(r'\[(CORNER|DEFENDER)\] (START|REFUSED|POST|POST_STATE|RELEASE) |edge=knock reason=declared', line):
                    print(line.strip(), flush=True)
        else:
            for line in rcon('defenderpost', timeout=5, collect=.5).splitlines():
                if re.search(r'\[DEFENDER\] (POST|REFUSED|RELEASE) ', line):
                    print(line.strip(), flush=True)
        def level_time():
            match = re.search(r'LEVEL_TIME level=(\d+)', rcon('botleveltime', timeout=5, collect=.2))
            return int(match.group(1)) if match else None
        start_level = setup[-1][0] if setup else level_time()
        if not setup:
            rcon('botplayrate %g' % args.rate, timeout=5, collect=.2)
        print('LAPS %s: %.0f game s at x%g' % (args.out, args.game_seconds, args.rate), flush=True)
        wall_start = time.time()
        if start_level is None:   # a build without botleveltime: wall time, as before
            time.sleep(args.game_seconds / args.rate)
        else:
            # Wait on the game clock: at 12.5 and 25 the server runs as fast as it can.
            deadline = wall_start + args.game_seconds / min(args.rate, 5) * 2 + 60
            while True:
                now = level_time()
                if now is not None and now - start_level >= args.game_seconds * 1000:
                    break
                if time.time() > deadline or server.poll() is not None:
                    print('game clock short: level %s of %d after %.0f s wall' % (now, start_level + args.game_seconds * 1000,
                                                                                  time.time() - wall_start), flush=True)
                    break
                time.sleep(min(5.0, max(.5, (args.game_seconds * 1000 - ((now or start_level) - start_level)) / 1000 / args.rate / 4)))
        info['wall_seconds'] = round(time.time() - wall_start, 1)
        (args.out / 'session.json').write_text(json.dumps(info, indent=2))
        print('LAPS wall %.1f s' % info['wall_seconds'], flush=True)
        try:
            rcon('quit', timeout=2)
        except Exception as error:
            print('quit not acknowledged: %r' % error)
        stopped = pvr.wait_for_exit(server.pid, 60)
        if not stopped:
            print('server %d still running after 60 s' % server.pid)
    finally:
        # A world that failed before its quit (server start, a setup timeout) or whose server ignored it would leave
        # openjkded running at timelimit 0 on its port, outside the four-world budget, and the campaign hands the port
        # to the next world: end it here, as the bench and route_approve do.
        if server.poll() is None:
            server.kill()
            stopped = pvr.wait_for_exit(server.pid, 30)
            print('server %d ended by the runner (%s)' % (server.pid, 'exited' if stopped else 'still running'), flush=True)
        log.close()
    copy_session(session, args.out / 'live')
    share_graphs(args.out / 'live' / 'server' / 'base')
    summary = summarise(args.out / 'live')
    summary['server_exit_confirmed'] = stopped
    # botat's setup commands run between engine frames; one that ran after a catch-up of several game frames ran
    # later in game time than in a world without the stall, so the world is not a replay of its seed.
    try:
        console = (args.out / 'live' / 'server-console.log').read_text(errors='replace')
        late = [int(m.group(1)) for m in re.finditer(r'AT_RAN level=\d+ due=\d+ dispatched=\d+ late=(\d+)', console)]
        summary['setup_late_commands'] = sum(1 for v in late if v > 0)
        summary['setup_late_max_ms'] = max(late) if late else 0
        if summary['setup_late_commands']:
            print('LAPS warning: %d setup commands ran late (max %d ms); not a replay of its seed' %
                  (summary['setup_late_commands'], summary['setup_late_max_ms']), flush=True)
    except OSError:
        pass
    (args.out / 'summary.json').write_text(json.dumps(summary, indent=1, default=str))
    print(json.dumps({k: v for k, v in summary.items() if k not in ('rearm', 'reset', 'mine', 'ambush')}, default=str))
    # The staging folder is a duplicate of live/ plus the build's binaries once the summary is written; a world that
    # stops earlier keeps it (records/2026-10-05-deferral-behaviour.md), and so does one whose server was not seen to
    # exit: its copy may be incomplete and the server may still hold the files.
    if not stopped:
        print('staging kept (server %d not seen to exit): %s' % (server.pid, session), flush=True)
    elif not args.keep_staging:
        drop_staging(session, args.out / 'live')


if __name__ == '__main__':
    main()
