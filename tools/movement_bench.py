"""Movement benchmark runner: one dedicated localhost world per case.

Stages a hash-bound build, starts a private dedicated server per case, adds
the movement-only runner, applies the case's exact start through the corner
fixture, sets the play rate and judges the play-log rows against the case's
pass condition (movement_bench_lib). Every world's files are retained under
--out/<case>/; result.json binds the build manifest, the case file and the
settings. Localhost only; never a real server.
"""
import argparse
import csv
import hashlib
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
from movement_bench_lib import (campaign_verdict, chain_done_level, corner_pin_state, evaluate, event_summary,  # noqa: E402
                                kick_audit, latest_play_level, load_cases, parity_summary, parse_move_events,
                                select_cases, state_audit, straight_audit, validate_corner_class,
                                validate_corner_level, corner_class_state, CORNER_CLASS_MODULUS,
                                skip_twins)
from strafe_cost_runner import status_response  # noqa: E402
from strafe_fixture_runner import spawn_without_crash_dialogs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / 'game_directory' / 'Jedi Academy' / 'GameData'
CORNER_START = re.compile(r'\[CORNER\] START .*?level=(\d+)')
CORNER_DEFERRED = '[CORNER] DEFERRED'
CORNER_REFUSED = re.compile(r'\[CORNER\] (?:REFUSED|FAILED)[^\n]*')
DEFENDER = ROOT / 'tools/fixtures/movement/defender'
FRAMECOST = re.compile(r'\[FRAMECOST\] level=(\d+) since=(\d+) client=(\w+) ([^\n]*)')
DEFENDER_EVENT = re.compile(r'\[DEFENDER\] (THROW|HIT|MISS|LANDED|OUTCOME|POST|RELEASE|REFUSED|DIED|RESPAWN|CHASE|CHASE_END|SHADOW_OPEN|SHADOW_CLOSE|SHADOW_SUMMARY|STARVED|FIRE|GUARD|POST_MOVE|DAMAGE|INTERCEPT)\b([^\n]*)')


def corner_deferred(args):
    """A class pin the game itself times (bot_cornerDefer), unless the legacy
    rcon-timed pin is asked for. Tools that share world_settings without the
    option keep the immediate start."""
    return getattr(args, 'corner_class', None) is not None and not getattr(args, 'corner_class_rcon', False)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_build(build, source_root=None):
    source_root = ROOT if source_root is None else source_root
    manifests = sorted(build.glob('*.sources.json'))
    if not manifests:
        raise SystemExit('build must contain a source/binary hash manifest from strafe_fixture_build.py')
    manifest = manifests[-1]
    provenance = json.loads(manifest.read_text())
    changed = [name for name, expected in provenance['sources'].items()
               if not (source_root / name).is_file() or digest(source_root / name) != expected]
    if changed:
        raise SystemExit('build sources differ from source root: ' + ', '.join(changed[:10]))
    return manifest, provenance


def world_settings(case, args, port, password):
    settings = dict(dedicated=1, net_ip='127.0.0.1', net_port=port, sv_maxclients=2, sv_pure=0,
                g_gametype=8, bot_enable=1, bot_minplayers=0, fixedtime=0, timedemo=0,
                g_doWarmup=0, capturelimit=0, timelimit=0, rconpassword=password,
                bot_playDecisionSeed=0,
                # The bench measures past a capture (--linger): no self-kill there.
                bot_runnerCapKill=0,
                bot_moveParity=args.parity,
                bot_cornerYaw=case['start']['yaw'],
                bot_cornerVelocity=' '.join(format(v, '.9g') for v in case['start']['velocity']),
                bot_cornerSpeedExpiry=case['resources'].get('speed_ms', 0),
                bot_momentumTrace=int(args.momentum_trace),
                bot_moveCorridor=args.corridor.name if args.corridor else '',
                bot_moveCorridorRadius=args.corridor_radius,
                bot_moveDecisionTape=args.decision_tape.name if args.decision_tape else '',
                bot_moveTapeLibrary=1 if args.decision_library else 0,
                bot_goalChain=';'.join(' '.join(format(v, '.9g') for v in p) for p in case.get('chain', ())),
                bot_moveSpike=int(args.spike), bot_moveEdges=int(args.edges), bot_movePlanner=int(args.planner),
                bot_moveStraight=int(args.straight), bot_moveState=int(args.state),
                # The case's spend policy, as MOVE_SPEND_* bits: which resource
                # edges the plan may take, independent of the pool.
                bot_moveSpend=(1 if 'speed' in case['spend']['templates'] else 0) |
                              (2 if 'hold' in case['spend']['templates'] else 0),
                bot_supportedTrace=int(args.ground_trace))
    if corner_deferred(args):
        settings['bot_cornerDefer'] = '%d %d %d' % (args.corner_class, args.corner_class_min,
                                                    args.corner_class_max)
    return settings


def defender_learning_settings(args, team):
    """Cvars for the behaviours from the human 1v1 study (docs/movement/records/2026-09-29-human-defense-session.md),
    only those switched on, so an arm without them is set exactly as before; team None for the record."""
    fixture = json.loads((DEFENDER / 'posts.json').read_text(encoding='utf-8'))
    points = lambda key: ';'.join(' '.join(format(v, 'g') for v in p) for p in fixture[key][args.map][team])
    out = {}
    if args.defender_rearm:
        out['bot_defenderRearm'] = 1
    if args.defender_reset:
        out['bot_defenderReset'] = 1
    if args.defender_mines:
        out['bot_defenderMines'] = args.defender_mines
        if team:
            out['bot_defenderMineSpots'] = points('mine_spots')
    if getattr(args, 'defender_walk', False):
        out['bot_defenderWalkErrands'] = 1
    if getattr(args, 'defender_patience', False):
        out['bot_defenderPatience'] = 1
    if args.chaser_ambush:
        out['bot_chaserAmbush'] = 1
        if team:
            out['bot_chaserAmbushPosts'] = points('ambush_posts')
    return out


def defender_summary(text):
    """The fixture defender's [DEFENDER] lines: throws, native hits and misses,
    and what became of the runner after each hit."""
    events = [(kind, dict(re.findall(r'(\w+)=(\S+)', rest))) for kind, rest in DEFENDER_EVENT.findall(text)]
    outcomes = [e['result'] for kind, e in events if kind == 'OUTCOME']
    return dict(posted=any(kind == 'POST' for kind, _ in events),
                released=any(kind == 'RELEASE' for kind, _ in events),
                refused=[rest for kind, rest in DEFENDER_EVENT.findall(text) if kind == 'REFUSED'],
                throws=[e for kind, e in events if kind == 'THROW'],
                hits=sum(kind == 'HIT' for kind, _ in events),
                misses=sum(kind == 'MISS' for kind, _ in events),
                landings=[e for kind, e in events if kind == 'LANDED'],
                died=sum(kind == 'DIED' for kind, _ in events),
                chased=any(kind == 'CHASE' for kind, _ in events),
                retargets=[int(e['retargets']) for kind, e in events if kind == 'CHASE_END' and 'retargets' in e],
                starved=[e for kind, e in events if kind == 'STARVED'],
                fires=sum(kind == 'FIRE' for kind, _ in events),
                fires_carrying=sum(kind == 'FIRE' and e.get('carrying') == '1' for kind, e in events),
                post_moves=sum(kind == 'POST_MOVE' for kind, _ in events),
                damage_hits=sum(kind == 'DAMAGE' for kind, _ in events),
                guard=next((e for kind, e in events if kind == 'GUARD'), None),
                shadow_open=[e for kind, e in events if kind == 'SHADOW_OPEN'],
                shadow_summary=[e for kind, e in events if kind == 'SHADOW_SUMMARY'],
                pits=outcomes.count('pit'), deaths=outcomes.count('death'), survived=outcomes.count('survived'))


def frame_cost(text):
    """The frame meter's last cumulative line per client (bot_moveFrameMeter):
    per-bot movement microseconds per game frame since the fixture's release.
    Keyed by client number, and 'all' for the frame's total over every bot."""
    last = {}
    for level, since, client, rest in FRAMECOST.findall(text):
        row = {k: float(v) for k, v in re.findall(r'(\w+)=(\S+)', rest)}
        row.update(level=int(level), since=int(since))
        last[client] = row
    return last or None


def read_log(path):
    """The server log so far; the engine keeps it open while the world runs."""
    try:
        return path.read_text(errors='replace')
    except OSError:
        return ''


def await_corner_level(play_log, args, record, deadline):
    """Tight-poll the play log until the world is standing where the pin wants.

    The play log is the only clock the runner can read, and the server is
    writing it as this reads it, so `latest_play_level` skips a torn line
    rather than guessing at one. An exact `--corner-level` pin waits for one
    level; a `--corner-class` pin waits for any level of the class, which is
    what a world's own 250 ms ladder can actually offer.
    """
    while time.monotonic() < deadline:
        try:
            latest = latest_play_level(play_log.read_text(errors='replace'))
        except OSError:
            latest = None
        record['corner_level_observed'] = latest
        if args.corner_class is not None:
            state = corner_class_state(latest, args.corner_class,
                                       args.corner_class_min, args.corner_class_max)
        else:
            state = corner_pin_state(latest, args.corner_level, args.corner_level_lead)
        if state != 'wait':
            return state
        time.sleep(.002)
    return 'deadline'


def await_deferred_start(server_log, deadline):
    """The game's own START (or refusal) line for a deferred class pin.

    The start runs at the end of a frame of the class, which may be seconds of
    level time after the command, so it is read from the server log rather
    than from the rcon reply. The DEFERRED acknowledgement itself went to the
    rcon reply, so any START or refusal in the log is the deferred start's:
    an immediate refusal would have come back in the reply instead.
    """
    while time.monotonic() < deadline:
        text = read_log(server_log)
        found = CORNER_START.search(text) or CORNER_REFUSED.search(text)
        if found:
            return found.group(0)
        time.sleep(.02)
    raise RuntimeError('deferred corner start never reported')


def pin_missed(args, landed):
    """True when a pinned world did not start where it was told to."""
    if args.corner_class is not None:
        return (landed is None or landed % CORNER_CLASS_MODULUS != args.corner_class
                or landed < args.corner_class_min or landed > args.corner_class_max)
    if args.corner_level is not None:
        return landed != args.corner_level
    return False


def retire_world(out, dest):
    """Keep a missed pin as evidence beside the world that replaces it.

    The rename is retried for a while: on Windows a directory the server has
    just left can stay locked for a moment (WinError 5), which killed two
    campaign arms mid-run."""
    for attempt in range(40):
        try:
            out.rename(dest)
            break
        except PermissionError:
            if attempt == 39:
                raise
            time.sleep(.5)
    result = dest / 'result.json'
    if result.is_file():
        record = json.loads(result.read_text())
        if record.get('play_log'):
            record['play_log'] = str(dest / Path(record['play_log']).relative_to(out))
        record['retired'] = 'missed the pinned corner level; replaced by a fresh world'
        result.write_text(json.dumps(record, indent=2))


def after_summary(rows, corner_level, time_ms):
    """What the play log shows after a passed trip's objective: frames, whether
    the runner died (health at or below 0), captured, or stopped carrying
    without a capture, and the largest drop below the objective frame's height.
    None when the trip has no time."""
    # The runner's rows only: a fixture defender logs to the same file.
    rows = [r for r in rows if int(r.get('movement_only', 1)) == 1]
    if corner_level is None or time_ms is None or not rows:
        return None
    done = corner_level + time_ms
    after = [r for r in rows if int(r['level_time']) >= done]
    if not after:
        return dict(frames=0, died=False, dropped_flag=False, max_drop=0.0, seconds=0.0)
    z0 = float(after[0]['z'])
    died = any(int(float(r['health'])) <= 0 for r in after)
    carried = [int(r['carrying']) for r in after]
    captured = int(after[-1].get('captures') or 0) > int(after[0].get('captures') or 0)
    return dict(frames=len(after), died=died, captured=captured,
                dropped_flag=bool(carried[0]) and not carried[-1] and not died and not captured,
                max_drop=round(max(z0 - float(r['z']) for r in after), 1),
                seconds=round((int(after[-1]['level_time']) - done) / 1000.0, 2))


def run_case(case, args, manifest, provenance, port, log, stride=1):
    """One world, or - when the corner level is pinned - the first that hits it.

    Only a successful corner start consumes a world, so a missed pin cannot be
    retried in place: the world is retained under its own name and a fresh one
    replaces it. Unpinned this runs exactly one world and the retained layout
    is unchanged.
    """
    out = args.out / case['name']
    pinned = args.corner_level is not None or args.corner_class is not None
    attempts = args.corner_level_attempts if pinned else 1
    for attempt in range(attempts):
        record = run_case_once(case, args, manifest, provenance, port + stride * attempt, log, out, attempt)
        if not pin_missed(args, record['corner_level']):
            return record
        log(f"{case['name']}: corner level {record['corner_level']} is not the pinned "
            f"{args.corner_class if args.corner_class is not None else args.corner_level}; "
            f"retaining this world and trying another")
        if attempt + 1 < attempts:
            retire_world(out, out.parent / f"{case['name']}.miss-{attempt + 1}")
    return record


def run_case_once(case, args, manifest, provenance, port, log, out, attempt=0):
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
    if args.defender is not None:
        shutil.copy2(DEFENDER / 'kejim_defender.bot', base / 'scripts/kejim_defender.bot')
        shutil.copy2(DEFENDER / 'kejim_defender.jkb', base / 'botfiles/kejim_defender.jkb')
        shutil.copy2(DEFENDER / 'kejim_base.jkb', base / 'botfiles/kejim_base.jkb')
    if args.graph:
        # The cache is looked up by map and checksum, which the file's header names.
        header = dict(token.partition('=')[::2] for token in args.graph.read_text().split('\n', 1)[0].split()[2:])
        (base / 'bot2-graph').mkdir()
        shutil.copy2(args.graph, base / 'bot2-graph' / f"{header['map'].replace('/', '_')}_{header['checksum']}.txt")
    if args.corridor:
        # Guidance for the expressiveness diagnostic: geometry the search may
        # travel through. Every command still comes from the bot.
        (base / 'bot2-corridor').mkdir()
        shutil.copy2(args.corridor, base / 'bot2-corridor' / args.corridor.name)
    if args.decision_tape:
        # Another world's committed decisions. The search, the rollouts and the
        # judge are unchanged; only the choice among admitted candidates is
        # preferred toward the tape, and what it cannot take is reported.
        (base / 'bot2-decisions').mkdir()
        shutil.copy2(args.decision_tape, base / 'bot2-decisions' / args.decision_tape.name)
    if args.decision_library:
        # The library, staged whole under the name the engine binds by, exactly
        # as the graph cache is. The world is told a library exists and nothing
        # else: which tape runs is the bot's own selection from its leg, its
        # start cell and its phase.
        shutil.copytree(args.decision_library,
                        base / 'bot2-decisions' / args.decision_library.name,
                        dirs_exist_ok=True)
    password = secrets.token_hex(16)
    settings = world_settings(case, args, port, password)
    defender_team = 'blue' if case['team'] == 'red' else 'red'
    if args.defender is not None:
        posts = json.loads((DEFENDER / 'posts.json').read_text(encoding='utf-8'))['posts']
        if args.map not in posts:
            raise SystemExit('no defender post for ' + args.map)
        roles = args.defender_roles.split(',')
        settings.update(sv_maxclients=2 + len(roles), bot_defender=args.defender, bot_defenderReactMs=args.defender_react,
                        bot_defenderRole=int('base' in roles),
                        bot_defenderChase=int(args.defender_chase), bot_defenderShadow=int(args.defender_shadow),
                        bot_defenderStrafe=int(args.defender_strafe), bot_defenderSpeed=int(args.defender_speed),
                        bot_defenderGuard=int(args.defender_guard),
                        bot_defenderPost=' '.join(format(v, 'g') for v in posts[args.map][defender_team]))
    if args.defender is not None and args.defender_guard:
        guard = json.loads((DEFENDER / 'posts.json').read_text(encoding='utf-8')).get(
            'guard_posts_2v2' if args.defender_posts == '2v2' else 'guard_posts', {})
        if args.map not in guard:
            raise SystemExit('no defender guard posts for ' + args.map)
        settings.update(bot_defenderPosts=';'.join(' '.join(format(v, 'g') for v in post)
                                                   for post in guard[args.map][defender_team]))
    if args.defender is not None and 'chase' in args.defender_roles.split(','):
        chase = json.loads((DEFENDER / 'posts.json').read_text(encoding='utf-8')).get('chase_posts', {})
        if args.map not in chase:
            raise SystemExit('no chase posts for ' + args.map)
        chase = chase[args.map][defender_team]
        settings.update(bot_chaserPost=' '.join(format(v, 'g') for v in chase[0]),
                        bot_chaserPosts=';'.join(' '.join(format(v, 'g') for v in post) for post in chase))
    if args.defender is not None:
        settings.update(defender_learning_settings(args, defender_team))
    if args.frame_meter:
        settings.update(bot_moveFrameMeter=1)
    settings.update(args.extra)
    (base / 'start.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) +
                                  f'\ndevmap {args.map}\n')
    corner = 'bot_corner_start ' + ' '.join(format(v, '.9g') for v in case['start']['origin']) + \
        f" {case['resources']['force']} {case['start']['carrying']}"
    record = dict(case=case, port=port, settings={k: v for k, v in settings.items() if k != 'rconpassword'},
                  corner_command=corner, timescale=args.timescale,
                  corner_level=None, corner_response=None, rate_response=None, rate_confirmed=False,
                  corner_level_target=args.corner_level, corner_level_observed=None,
                  corner_level_lead_ms=args.corner_level_lead if args.corner_level else None,
                  corner_class_target=args.corner_class,
                  corner_class_window=[args.corner_class_min, args.corner_class_max]
                  if args.corner_class is not None else None,
                  corner_attempt=attempt,
                  shutdown_clean=False, wall_seconds=None, error=None)
    bound_wall = case['pass_condition']['bound_ms'] / 1000 / args.timescale + 90
    engine = out / 'openjkded.x86.exe'
    rows = []
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
        rate_level = None
        passed = False
        try:
            deadline = started + bound_wall
            while time.monotonic() < deadline and proc.poll() is None:
                if not added:
                    if status_response(port, password):
                        time.sleep(.6)
                        response = str(rcon_send(port, password, 'addrunner ' + case['team'], timeout=15, collect=.5))
                        (out / 'command.log').write_text(response)
                        if f"[BOT2 PLAY] Adding {case['team']} flag runner." not in response:
                            raise RuntimeError('runner creation not acknowledged; see command.log')
                        for role in (args.defender_roles.split(',') if args.defender is not None else []):
                            chase = role == 'chase'
                            response = str(rcon_send(port, password, 'adddefender ' + defender_team + (' chase' if chase else ''),
                                                     timeout=15, collect=.5))
                            with (out / 'command.log').open('a') as stream:
                                stream.write(response)
                            if f"[DEFENDER] Adding {defender_team} {'chaser' if chase else 'defender'}." not in response:
                                raise RuntimeError('defender creation not acknowledged; see command.log')
                        added = True
                    # A pinned start has to be caught early: the earliest levels
                    # go by while this loop is still sleeping.
                    time.sleep(.05 if added and (args.corner_level is not None
                                                or args.corner_class is not None) else .5)
                    continue
                files = list((base / 'bot2-play').glob('*.csv'))
                if files:
                    with files[0].open() as stream:
                        rows = list(csv.DictReader(stream))
                if rows and record['corner_level'] is None:
                    if args.corner_level is not None or (args.corner_class is not None
                                                         and not corner_deferred(args)):
                        state = await_corner_level(files[0], args, record, deadline)
                        if state != 'send':
                            raise RuntimeError('pinned corner %s %d not reachable in this world (%s at %s)'
                                               % ('class' if args.corner_class is not None else 'level',
                                                  args.corner_class if args.corner_class is not None
                                                  else args.corner_level,
                                                  state, record['corner_level_observed']))
                    response = str(rcon_send(port, password, corner, timeout=2, collect=.5))
                    record['corner_response'] = response
                    if corner_deferred(args):
                        if CORNER_DEFERRED not in response:
                            raise RuntimeError('deferred corner start not acknowledged (a build before '
                                               'bot_cornerDefer starts at once; use --corner-class-rcon): '
                                               + response.strip()[:300])
                        response = await_deferred_start(out / 'server.log', deadline)
                        record['corner_response'] += response
                    match = CORNER_START.search(response)
                    if not match:
                        raise RuntimeError('corner start refused: ' + response.strip()[:300])
                    record['corner_level'] = int(match.group(1))
                    if pin_missed(args, record['corner_level']):
                        raise RuntimeError('pinned corner %s missed: wanted %d, landed %d'
                                           % ('class' if args.corner_class is not None else 'level',
                                              args.corner_class if args.corner_class is not None
                                              else args.corner_level, record['corner_level']))
                    record['start_commands'] = []
                    for command in args.start_command:
                        try:
                            reply = str(rcon_send(port, password, command, timeout=4, collect=1.5))
                        except Exception as error:
                            reply = repr(error)
                        record['start_commands'].append(dict(command=command, reply=reply[:400]))
                    time.sleep(.6)
                    if args.timescale != 1:
                        for attempt in range(3):
                            rate = str(rcon_send(port, password, 'botplayrate ' + format(args.timescale, 'g'), timeout=2))
                            record['rate_response'] = rate
                            if '[BOT2 PLAY] RATE' in rate:
                                break
                            time.sleep(.6)
                        if '[BOT2 PLAY] RATE' not in (record['rate_response'] or ''):
                            raise RuntimeError('rate command unconfirmed')
                        rate_level = max(int(r['level_time']) for r in rows)
                    else:
                        record['rate_confirmed'] = True
                if record['corner_level'] is not None:
                    after = [r for r in rows if int(r['level_time']) > record['corner_level']]
                    if rate_level is not None and not record['rate_confirmed'] and after:
                        latest = after[-1]
                        if int(latest['level_time']) > rate_level and float(latest.get('timescale', -1)) == args.timescale:
                            record['rate_confirmed'] = True
                    events = None
                    if case['pass_condition']['condition'] == 'composition':
                        events = parse_move_events(read_log(out / 'server.log'), record['corner_level'])
                    reach = chain_done_level(read_log(out / 'server.log'), record['corner_level']) \
                        if case['leg'] == 'chain' else None
                    verdict = evaluate(case, rows, record['corner_level'], events, reach)
                    if verdict['status'] in ('pass', 'fail'):
                        passed = verdict['status'] == 'pass'
                        break
                time.sleep(.5)
        except Exception as error:  # retained as evidence, never hidden
            record['error'] = repr(error)
        finally:
            record['wall_seconds'] = time.monotonic() - started
            # --linger: a passed trip's world runs on, so what the runner does
            # after the objective (the carrier's survival) is in the play log.
            if passed and args.linger > 0 and proc.poll() is None:
                time.sleep(args.linger)
            # Sent whether the trip passed, failed or raised: a census of a
            # world that missed is evidence too, and dropping it would bias
            # whatever the diagnostic is measuring toward successful worlds.
            record['final_commands'] = []
            for command in args.final_command:
                try:
                    reply = str(rcon_send(port, password, command, timeout=4, collect=1.5))
                except Exception as error:
                    reply = repr(error)
                record['final_commands'].append(dict(command=command, reply=reply[:400]))
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
            record['graphs_shared'] = share_graphs(base, args.out)
            files = list((base / 'bot2-play').glob('*.csv'))
            if files:
                with files[0].open() as stream:
                    rows = list(csv.DictReader(stream))
    corner_level = record['corner_level'] if record['corner_level'] is not None else 1 << 31
    events = parse_move_events(read_log(out / 'server.log'), record['corner_level'])
    runner_client = re.search(r'\[CORNER\] START .*?client=(\d+)', read_log(out / 'server.log'))
    if args.defender is not None and runner_client:
        events = [e for e in events if e.get('client') == runner_client.group(1)]
    record['evaluation'] = evaluate(case, rows, corner_level, events,
                                    chain_done_level(read_log(out / 'server.log'), corner_level))
    if record['error'] and record['evaluation']['status'] != 'fail':
        record['evaluation'].update(status='incomplete', reason='runner_error')
    record['move_events'] = event_summary(events)
    record['after'] = after_summary(rows, record['corner_level'], record['evaluation'].get('time_ms'))
    record['parity'] = parity_summary(events)
    record['straight'] = straight_audit(events)
    record['state'] = state_audit(events)
    time_ms = record['evaluation'].get('time_ms')
    record['kicks'] = kick_audit(read_log(out / 'server.log'), record['corner_level'],
                                 record['corner_level'] + time_ms
                                 if record['corner_level'] is not None and time_ms is not None else None)
    record['play_log'] = str(files[0]) if files else None
    if args.frame_meter:
        record['frame_cost'] = frame_cost(read_log(out / 'server.log'))
    if args.defender is not None:
        record['defender'] = defender_summary(read_log(out / 'server.log'))
        if not record['defender']['released'] and record['evaluation']['status'] == 'pass':
            record['evaluation'].update(status='incomplete', reason='defender_not_released')
    (out / 'result.json').write_text(json.dumps(record, indent=2))
    e = record['evaluation']
    d = record.get('defender')
    if d:
        log(f"{case['name']}: defender throws={len(d['throws'])} hits={d['hits']} misses={d['misses']} "
            f"pits={d['pits']} defender_died={d['died']} deaths={d['deaths']} survived={d['survived']}")
    log(f"{case['name']}: {e['status']} {e['reason']} time_ms={e['time_ms']} owners={e['owners']} "
        f"parity_mismatches={record['parity']['mismatches']} refusals={record['parity']['refusals']} "
        f"straight_commits={record['straight']['commits']} judged_short={len(record['straight']['short'])} "
        f"state_commits={record['state']['commits']} strafe_planned={record['state']['strafe_planned']} learned={record['state']['learned']}")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='source checkout or frozen snapshot verified against every build source hash')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=Path, default=ROOT / 'tools/fixtures/movement/kejim/cases.json')
    parser.add_argument('--level', type=int, choices=(1, 2, 3))
    parser.add_argument('--filter', help='regular expression on case names')
    parser.add_argument('--skip-twins', action='store_true',
                        help='drop each return leg whose outbound mirror twin is selected, on maps '
                             'measured symmetric (movement_bench_lib.TWIN_SKIPPABLE); for comparison '
                             'arms, not gates or floors. Both arms of a comparison should use it.')
    parser.add_argument('--parity', type=int, default=0, help='bot_moveParity horizon in frames (0 off)')
    parser.add_argument('--momentum-trace', action='store_true')
    parser.add_argument('--spike', action='store_true',
                        help='bot_moveSpike 1: phase 1 discovery on grounded frames')
    parser.add_argument('--edges', action='store_true',
                        help='bot_moveEdges 1: phase 2 gap discovery at corridor refusals')
    parser.add_argument('--no-planner', dest='planner', action='store_false',
                        help='bot_movePlanner 0: navmesh waypoint steering, no planned edges (the gap-edge step)')
    parser.add_argument('--no-straight', dest='straight', action='store_false',
                        help='bot_moveStraight 0: no hops searched on straights of the planned path (the planner step)')
    parser.add_argument('--no-state', dest='state', action='store_false',
                        help='bot_moveState 0: the path is searched over cells alone and no planned strafe edge is validated')
    parser.add_argument('--ground-trace', action='store_true', help='bot_supportedTrace 1: [GROUNDTRACE] lines for every corridor refusal')
    parser.add_argument('--map', help="map to devmap (default: the case file's map); "
                                      'a map other than Kejim needs a chain case')
    parser.add_argument('--graph', type=Path, help='prepared graph cache file to seed every world with (bot2-graph/<map>_<checksum>.txt)')
    parser.add_argument('--corridor', type=Path,
                        help='guided corridor file (tools/movement_corridor.py) staged as bot2-corridor/<name>: '
                             'the searches may only travel through cells near its polyline. Diagnostic guidance; '
                             'unguided acceptance leaves it unset.')
    parser.add_argument('--corridor-radius', type=float, default=192.0,
                        help='half-width of the guided corridor in units (default 192)')
    parser.add_argument('--decision-tape', type=Path,
                        help='decision tape (tools/movement_decision_tape.py) staged as '
                             'bot2-decisions/<name>: at each judged candidate set the search '
                             'prefers the admitted candidate matching the next tape entry near '
                             'its root, and reports the entries it could not take. Admission, '
                             'physics, cadence and weights are unchanged; unset, the world is '
                             'the control.')
    parser.add_argument('--decision-library', type=Path,
                        help='decision-tape library directory (tools/movement_tape_library.py) '
                             'staged as bot2-decisions/<name>, whose name must be '
                             '<map>_<checksum>: bot_moveTapeLibrary 1, and at every leg start '
                             'the bot forms its own key from the leg, the start cell and the '
                             'phase and selects its own tape. The harness names no tape. '
                             'Mutually exclusive with --decision-tape, which overrides it.')
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE',
                        help='extra world cvar, recorded in the settings of every world (e.g. bot_forecastTriggerLog=1)')
    parser.add_argument('--start-command', action='append', default=[], metavar='COMMAND',
                        help='server command sent once, immediately after the corner start is '
                             'confirmed, repeatable. With --final-command this splits a world '
                             'into what happened before the start and what happened after it.')
    parser.add_argument('--linger', type=float, default=0, metavar='SECONDS',
                        help='keep a passed trip world running this long before shutdown; the result '
                             '"after" block records whether the runner died or lost the flag after the '
                             'objective. 0 (default) shuts down at once, as always.')
    parser.add_argument('--final-command', action='append', default=[], metavar='COMMAND',
                        help='server command sent once after the trip and before shutdown, repeatable. '
                             'Its output goes to server.log, so a report too large for an rcon reply '
                             'still arrives whole. Use this for any diagnostic that must not print '
                             'inside a search.')
    parser.add_argument('--corner-level', type=int, metavar='MS',
                        help='hold the corner start until the world reaches this level time, and refuse '
                             'the world if it lands anywhere else. The start level decides the whole leg, '
                             'so pinning it makes an A/B reproducible to about one frame; leave it unset '
                             'for acceptance floors, which should keep sampling starts. Multiple of 25.')
    parser.add_argument('--corner-level-lead', type=int, default=0, metavar='MS',
                        help='how early to send the corner start when pinning. The start lands on the '
                             'level of the last play-log row the runner has seen, so the default is 0; '
                             'raise it only to re-tune on a slower host.')
    parser.add_argument('--corner-class', type=int, metavar='MS',
                        help='hold the corner start until the world reaches any level whose '
                             'residue mod %d ms is this, and refuse the world if it lands off '
                             'that class. The pre-start play log steps 250 ms, so a world can '
                             'only start on its own ladder; a class is reachable in every world '
                             'whose ladder meets it, where one exact level is not. '
                             'Mutually exclusive with --corner-level.' % CORNER_CLASS_MODULUS)
    parser.add_argument('--corner-class-rcon', action='store_true',
                        help='time a --corner-class pin from the runner over rcon, as before '
                             'bot_cornerDefer: about half the worlds miss the class and are '
                             'replaced. By default the game starts at the first level of the '
                             'class at or after --corner-class-min, so no world misses.')
    parser.add_argument('--corner-class-min', type=int, default=3000, metavar='MS',
                        help='earliest level a class pin will start on (default 3000); the '
                             'runner cannot see the opening levels of a world at all')
    parser.add_argument('--corner-class-max', type=int, default=7000, metavar='MS',
                        help='latest level a class pin waits for before giving the world up '
                             '(default 7000)')
    parser.add_argument('--corner-level-attempts', type=int, default=4, choices=(1, 2, 3, 4),
                        help='fresh worlds to try before giving a pinned start up (default 4)')
    parser.add_argument('--defender', type=int, choices=(0, 1, 2),
                        help='add the fixture defender (tools/fixtures/movement/defender) on the enemy team at its post: '
                             '0 holds the post only, 1 also pushes/pulls when the passive flight is lethal, '
                             '2 only when no held air input escapes')
    parser.add_argument('--defender-chase', action='store_true',
                        help='the defender chases the carrier once the runner carries its flag')
    parser.add_argument('--defender-strafe', action='store_true',
                        help='with --defender-chase: the chaser runs the movement searches (hops, strafes) as a guest '
                             'that writes nothing into the shared graph')
    parser.add_argument('--defender-speed', action='store_true',
                        help='give the defender Speed 3 at its release (bot_defenderSpeed): a strafing chaser buys it as '
                             'the runner does, from the Force its throws use')
    parser.add_argument('--defender-guard', action='store_true',
                        help='the defender guards its posts (posts.json guard_posts) between chases (bot_defenderGuard)')
    parser.add_argument('--defender-roles', choices=('solo', 'base', 'chase', 'base,chase'), default='solo',
                        help='solo: one defender as before; base: Defender_v2 as base D; chase: Chaser_v2 as chase D '
                             '(chase_posts); base,chase: both')
    parser.add_argument('--defender-posts', choices=('study', '2v2'), default='study',
                        help='with --defender-guard: the 4v4 base posts (guard_posts) or the lone 2v2 defender\'s '
                             '(guard_posts_2v2: entry spot, stand, forward)')
    parser.add_argument('--defender-rearm', action='store_true',
                        help='a defender missing a loadout weapon or its ammo walks to an item that gives it (bot_defenderRearm)')
    parser.add_argument('--defender-reset', action='store_true',
                        help='the self-kill reset: far from its post with its flag home and the runner clear (bot_defenderReset)')
    parser.add_argument('--defender-mines', type=int, default=0, metavar='N',
                        help='base D keeps proximity mines on N of posts.json mine_spots (bot_defenderMines)')
    parser.add_argument('--defender-walk', action='store_true',
                        help='defenders walk their errands; hop searches only while chasing (bot_defenderWalkErrands)')
    parser.add_argument('--defender-patience', action='store_true',
                        help='an escapable lethal throw waits while the throw two frames on is lethal with fewer escapes '
                             '(bot_defenderPatience)')
    parser.add_argument('--chaser-ambush', action='store_true',
                        help='chase D waits on posts.json ambush_posts ahead of the runner while its flag is home (bot_chaserAmbush)')
    parser.add_argument('--frame-meter', action='store_true',
                        help='record per-bot movement microseconds per game frame (bot_moveFrameMeter); '
                             'wall-clock, so run timing worlds alone (--parallel 1)')
    parser.add_argument('--defender-shadow', action='store_true',
                        help='log where on the carry a perfect tail 250/500/1000 ms behind could make an inescapable throw')
    parser.add_argument('--defender-react', type=int, default=0, metavar='MS',
                        help='the defender decides on the runner as it was this long ago (0..1000)')
    parser.add_argument('--timescale', type=float, choices=(1, 2.5, 5), default=5)
    parser.add_argument('--parallel', type=int, default=1, choices=(1, 2, 3, 4))
    parser.add_argument('--port', type=int, default=29180)
    args = parser.parse_args()
    args.out = args.out.resolve()
    from world_retention import require_free   # both drives, as defense_laps
    require_free(args.out)
    args.build = args.build.resolve()
    args.source_root = args.source_root.resolve()
    args.extra = dict(item.split('=', 1) for item in args.set)
    validate_corner_level(args.corner_level)
    validate_corner_class(args.corner_class, args.corner_class_min, args.corner_class_max)
    if args.corner_level is not None and args.corner_class is not None:
        parser.error('--corner-level and --corner-class are mutually exclusive')
    if corner_deferred(args) and args.corner_class_max < args.corner_class_min + CORNER_CLASS_MODULUS:
        parser.error('a deferred --corner-class needs --corner-class-max at least %d past the min'
                     % CORNER_CLASS_MODULUS)
    if args.graph:
        args.graph = args.graph.resolve()
    if args.corridor:
        args.corridor = args.corridor.resolve()
    if args.decision_library:
        args.decision_library = args.decision_library.resolve()
        if not (args.decision_library / 'index.txt').is_file():
            parser.error('--decision-library must be a directory holding index.txt')
        if args.decision_tape:
            parser.error('--decision-tape overrides the library; pass one or the other')
    args.out.mkdir(parents=True, exist_ok=False)
    manifest, provenance = stage_build(args.build, args.source_root)
    doc, cases_sha = load_cases(args.cases)
    args.map = args.map or doc['map']
    chosen = select_cases(doc['cases'], args.level, args.filter)
    twins_skipped = []
    if args.skip_twins:
        chosen, twins_skipped = skip_twins(chosen, doc['map'])
    if not chosen:
        raise SystemExit('no case selected')
    lines = []

    def log(text):
        lines.append(text)
        print(text, flush=True)

    ports = iter(range(args.port, args.port + 4 * len(chosen)))
    results = []
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = [pool.submit(run_case, case, args, manifest, provenance, next(ports), log, len(chosen))
                   for case in chosen]
        for future in futures:
            results.append(future.result())
    summary = dict(build=str(args.build), build_manifest=str(manifest), build_manifest_sha256=digest(manifest),
                   source_root=str(args.source_root),
                   cases_file=str(args.cases.resolve()), cases_sha256=cases_sha,
                   twins_skipped=twins_skipped,
                   staging_helper_sha256=digest(Path(__file__).with_name('staged_binary.py')),
                   runner_sha256=digest(Path(__file__)), library_sha256=digest(Path(__file__).with_name('movement_bench_lib.py')),
                   parity=args.parity, edges=args.edges, planner=args.planner, straight=args.straight,
                   state=args.state, timescale=args.timescale,
                   corner_level=args.corner_level,
                   corner_level_lead=args.corner_level_lead if args.corner_level else None,
                   corner_class=args.corner_class,
                   corner_class_window=[args.corner_class_min, args.corner_class_max]
                   if args.corner_class is not None else None,
                   defender=args.defender, defender_react=args.defender_react if args.defender is not None else None,
                   defender_chase=args.defender_chase if args.defender is not None else None,
                   defender_strafe=args.defender_strafe if args.defender is not None else None,
                   defender_speed=args.defender_speed if args.defender is not None else None,
                   defender_roles=args.defender_roles if args.defender is not None else None,
                   defender_learning=defender_learning_settings(args, None) if args.defender is not None else None,
                   frame_meter=args.frame_meter,
                   defender_sha256={p.name: digest(p) for p in sorted(DEFENDER.iterdir()) if p.is_file()}
                   if args.defender is not None else None,
                   graph=str(args.graph.resolve()) if args.graph else None, graph_sha256=digest(args.graph) if args.graph else None,
                   corridor=str(args.corridor) if args.corridor else None,
                   corridor_sha256=digest(args.corridor) if args.corridor else None,
                   corridor_radius=args.corridor_radius if args.corridor else None,
                   decision_library=str(args.decision_library) if args.decision_library else None,
                   decision_library_index_sha256=digest(args.decision_library / 'index.txt')
                   if args.decision_library else None,
                   verdict=campaign_verdict(results),
                   cases=[dict(name=r['case']['name'], level=r['case']['level'], **r['evaluation'],
                               corner_level=r['corner_level'], corner_attempt=r['corner_attempt'],
                               parity=r['parity'], straight=r['straight'], state=r['state'], events=r['move_events'], kicks=r.get('kicks'),
                               defender=r.get('defender'), frame_cost=r.get('frame_cost'),
                               error=r['error']) for r in results])
    (args.out / 'result.json').write_text(json.dumps(summary, indent=2))
    (args.out / 'log.txt').write_text('\n'.join(lines) + '\n')
    print(json.dumps(summary['verdict']))
    return 0 if summary['verdict']['green'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
