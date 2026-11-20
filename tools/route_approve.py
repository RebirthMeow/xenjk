"""Approve a route asset by executing it, and record exactly what it did.

A recording is evidence that a human ran a line. An asset built from it is a
tape the engine can submit. Neither is a bot route until something plays the
tape in a world and checks the result, which is what this does:

  stage a world from a fixture build, the same way the movement bench does
  put the bot at the tape's own start, to the unit, with bot_corner_start
  load and arm playback, then watch the server's own telemetry
  write the verdict beside the run, and promote the asset only if asked

Approval is not "it finished". It is: the replay reached the enemy flag, the
bot did not die, and the replay stayed on the recorded line - drift within the
limit at every recorded mark. A run that finishes far from the line finished
some other route, and its time means nothing.

Nothing here edits a recording. The asset is only rewritten with --promote,
and only when the run passed.

Usage:
    python tools/route_approve.py <asset.route.json> --build <dir> --out <dir>
"""
import argparse
import json
import re
import secrets
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from staged_binary import stage_binary
from bot_test_runner import rcon_send  # noqa: E402
from movement_bench import (CORNER_DEFERRED, CORNER_START, GAME, ROOT, await_corner_level,  # noqa: E402
                            await_deferred_start, digest, pin_missed, read_log, stage_build)
from movement_bench_lib import validate_corner_class  # noqa: E402
from strafe_cost_runner import status_response  # noqa: E402
from strafe_fixture_runner import spawn_without_crash_dialogs  # noqa: E402

DONE = re.compile(r'\[ROUTEPLAY\] DONE (.*)')
MARK = re.compile(r'\[ROUTEPLAY\] MARK (.*)')
START = re.compile(r'\[ROUTEPLAY\] START (.*)')
HANDOFF = re.compile(r'\[ROUTEPLAY\] HANDOFF (.*)')
PREFIX = re.compile(r'\[ROUTEPLAY\] PREFIX (.*)')
REFUSED = re.compile(r'\[ROUTEPLAY\] REFUSED (.*)')
# A replay this far from the recorded position is not running the recorded
# route any more, whatever it does afterwards. The engine reports at the same
# distance; this is the harness deciding on it.
DRIFT_LIMIT = 64.0


def fields(text):
    """`key=value` pairs from one telemetry line, values left as strings."""
    out = {}
    for token in text.split():
        key, _, value = token.partition('=')
        if value:
            out[key] = value
    return out


def world_settings(asset, port, password, team, args):
    physics = asset['physics']
    settings = dict(dedicated=1, net_ip='127.0.0.1', net_port=port, sv_maxclients=2, sv_pure=0,
                g_gametype=8, bot_enable=1, bot_minplayers=0, fixedtime=0, timedemo=0,
                g_doWarmup=0, capturelimit=0, timelimit=0, rconpassword=password,
                bot_playDecisionSeed=0,
                # The tape is a sequence of commands under one physics setting.
                # Under any other the same commands are a different run.
                pmove_fixed=physics['pmove_fixed'], pmove_msec=physics['pmove_msec'],
                pmove_float=physics['pmove_float'],
                bot_cornerYaw=asset['start']['yaw'],
                bot_cornerVelocity=' '.join(format(v, '.9g') for v in asset['start']['velocity']),
                bot_cornerSpeedExpiry=0,
                # The follower is armed where the bot already stands and steers
                # back onto the recorded line. Playback is unchanged with this
                # unset, so an approval run is still the open-loop measurement.
                bot_routeFollow=int(bool(args.follow)),
                bot_routeFollowGain=args.gain,
                # Placement happens inside arming, not in the corner round trip,
                # so the pose the tape starts from is set in the frame it arms.
                # This names where.
                bot_routeFollowStart=(' '.join(format(v, '.9g') for v in args.start)
                                      if args.follow and args.start else ''),
                )
    if args.prefix:
        # Only a prefix run turns the movement engine's searches on (the
        # rewrite itself is unconditional; before that an
        # approval run without a prefix ran the pre-rewrite stack).
        settings.update(bot_routePrefix=1, bot_moveEdges=1, bot_moveParity=0,
                        # What the walk may spend. Zero by default: the tape
                        # was recorded with a Force pool, and a walk that
                        # spends it hands the route a resource state the
                        # recording never had.
                        bot_moveSpend=args.prefix_spend,
                        bot_routePrefixTimeout=args.prefix_timeout,
                        bot_routePrefixAlong=args.prefix_along,
                        bot_routePrefixCross=args.prefix_cross,
                        bot_routePrefixSpeed=args.prefix_speed,
                        bot_routePrefixSettle=args.prefix_settle,
                        bot_routePrefixStart=(' '.join(format(v, '.9g') for v in args.start)
                                              if args.start else ''))
    if args.corner_class is not None:
        # The game times a class pin (bot_cornerDefer), as movement_bench does: over rcon the start lands on the last
        # play-log row seen, and the rows' 250 ms ladder meets a given class of 200 in only some worlds.
        settings['bot_cornerDefer'] = '%d %d %d' % (args.corner_class, args.corner_class_min, args.corner_class_max)
    return settings


def stage_world(asset, tape, args, provenance, port, password, team):
    out = args.out
    base = out / 'base'
    base.mkdir(parents=True, exist_ok=False)
    (base / 'scripts').mkdir()
    (base / 'botfiles').mkdir()
    (base / 'bot2-route').mkdir()
    for name, dest in (('openjkded.x86.exe', out), ('jampgamex86.dll', base)):
        stage_binary(args.build / 'RelWithDebInfo' / name, dest / name)
        if digest(dest / name) != provenance['binaries'][name]:
            raise RuntimeError('staged binary does not match the build manifest: ' + name)
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.bot', base / 'scripts/kejim_runner.bot')
    shutil.copy2(ROOT / 'tools/fixtures/strafe_play/kejim_runner.jkb', base / 'botfiles/kejim_runner.jkb')
    shutil.copy2(tape, base / 'bot2-route' / tape.name)
    settings = world_settings(asset, port, password, team, args)
    (base / 'start.cfg').write_text('\n'.join(f'set {k} "{v}"' for k, v in settings.items()) +
                                    '\ndevmap mp/ctf_kejim\n')
    return settings


def verdict(record, asset):
    """What the run proves, from the engine's own report and nothing else."""
    done = record.get('done')
    if record.get('refused'):
        return dict(state='refused', reason=record['refused'].get('REFUSED', 'refused'),
                    note='playback would not arm; nothing was executed')
    if not done:
        return dict(state='inconclusive', reason='no_done_line',
                    note='the world ended without the engine reporting a result')
    reason = done.get('reason')
    drift = float(done.get('max_drift', 'nan'))
    elapsed = int(done.get('elapsed_ms', -1))
    reference = asset['reference']['elapsed_ms']
    if reason != 'pickup':
        return dict(state='rejected', reason=reason, elapsed_ms=elapsed, max_drift=drift,
                    note='the replay did not reach the enemy flag')
    if not (drift <= DRIFT_LIMIT):
        return dict(state='rejected', reason='drift', elapsed_ms=elapsed, max_drift=drift,
                    note='the replay reached the flag off the recorded line, so its time is '
                         'not the recorded route')
    prefix_ms = int(done.get('prefix_ms', 0))
    return dict(state='approved', reason='pickup', elapsed_ms=elapsed, max_drift=drift,
                reference_ms=reference,
                delta_ms=None if reference is None else elapsed - reference,
                prefix_ms=prefix_ms or None,
                # A prefix run's time is the walk plus the tape. The route time
                # stays the tape's, because that is what the recording is; the
                # total is what the fixture would actually pay from the stand.
                total_ms=elapsed + prefix_ms if prefix_ms else None,
                note='executed from the tape start, reached the enemy flag, stayed on the line'
                     if not prefix_ms else
                     'walked into the tape start, then executed it to the enemy flag on the line')


def play_log_path(out):
    """The world's own play log, which is the only clock a start pin can read.

    It appears once the runner is added and is written while this reads it, so
    the caller polls for it rather than requiring it up front.
    """
    found = sorted((out / 'base/bot2-play').glob('*.csv'))
    return found[0] if found else None


def await_class(args, record, deadline):
    """Hold the corner start until the world stands on the pinned class.

    A replay's start level decides what the world does to it, which is the
    whole question a transfer run asks, so an unpinned replay is a draw over
    start levels. This is `movement_bench`'s own wait, reading the same play
    log; the landed level is still verified afterwards, so a world that starts
    off-class is never counted.
    """
    if args.corner_class is None and args.corner_level is None:
        return 'send'
    while time.monotonic() < deadline:
        log = play_log_path(args.out)
        if log is not None:
            state = await_corner_level(log, args, record, min(deadline, time.monotonic() + .25))
            if state != 'deadline':
                return state
        else:
            time.sleep(.05)
    return 'deadline'


def run(asset, asset_path, tape, args):
    manifest, provenance = stage_build(args.build)
    port, password = args.port, secrets.token_hex(16)
    settings = stage_world(asset, tape, args, provenance, port, password, args.team)
    name = tape.name[:-len('.tape')]
    # The follower's point is that it is armed somewhere the tape was not
    # recorded, so --from places the bot there instead of on the tape's start.
    origin = args.start or asset['start']['origin']
    corner = ('bot_corner_start ' + ' '.join(format(v, '.9g') for v in origin) +
              f" {asset['start']['force']} {int(bool(asset['start']['carrying']))}")
    record = dict(asset=asset_path.name, tape=tape.name, name=name, port=port, team=args.team,
                  settings={k: v for k, v in settings.items() if k != 'rconpassword'},
                  corner_command=corner, corner_level=None, corner_response=None,
                  corner_class=args.corner_class, corner_level_pin=args.corner_level,
                  corner_level_observed=None,
                  corner_wait=None, pin_missed=None,
                  load_response=None, arm_response=None, shutdown_clean=False,
                  wall_seconds=None, error=None, marks=[], done=None, refused=None,
                  build=str(args.build), manifest=manifest.name)
    engine = args.out / 'openjkded.x86.exe'
    started = time.monotonic()
    # The tape's own duration plus the world's start-up, then a margin: a replay
    # that has not finished by then has not finished.
    deadline_wall = asset['tape']['duration_ms'] / 1000 + 120
    if args.prefix:
        deadline_wall += args.prefix_timeout / 1000
    with (args.out / 'server.log').open('xb') as server_log:
        proc = spawn_without_crash_dialogs(
            [str(engine), '+set', 'fs_basepath', str(GAME), '+set', 'fs_homepath', str(args.out),
             '+set', 'fs_game', 'base', '+set', 'dedicated', '1', '+set', 'net_ip', '127.0.0.1',
             '+set', 'net_port', str(port), '+safe', '+exec', 'start.cfg'], cwd=args.out,
            stdout=server_log, stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        added = armed = False
        try:
            deadline = started + deadline_wall
            while time.monotonic() < deadline and proc.poll() is None:
                if not added:
                    if status_response(port, password):
                        time.sleep(.6)
                        response = str(rcon_send(port, password, 'addrunner ' + args.team,
                                                 timeout=15, collect=.5))
                        (args.out / 'command.log').write_text(response)
                        if f'[BOT2 PLAY] Adding {args.team} flag runner.' not in response:
                            raise RuntimeError('runner creation not acknowledged; see command.log')
                        added = True
                    time.sleep(.5)
                    continue
                if not armed:
                    time.sleep(1.0)
                    # Hold first: placement and arming are separate round trips
                    # and the bot's own AI walks it off the tape's start in the
                    # gap between them.
                    record['hold_response'] = str(rcon_send(port, password, 'bot_route_play hold 0',
                                                            timeout=3, collect=.5))
                    # The hold is already on, so waiting here costs the world
                    # nothing but level time: the bot stands where it was. A
                    # class pin is the game's own (bot_cornerDefer); only an
                    # exact level is still waited for here.
                    record['corner_wait'] = (await_class(args, record, deadline) if args.corner_class is None
                                             else 'deferred')
                    if record['corner_wait'] not in ('send', 'deferred', None):
                        raise RuntimeError('pinned corner start %s not reachable in this world (%s at %s)'
                                           % (args.corner_level if args.corner_level is not None
                                              else 'class %d' % args.corner_class,
                                              record['corner_wait'],
                                              record['corner_level_observed']))
                    response = str(rcon_send(port, password, corner, timeout=2, collect=.5))
                    record['corner_response'] = response
                    if args.corner_class is not None:
                        if CORNER_DEFERRED not in response:
                            raise RuntimeError('deferred corner start not acknowledged: ' + response.strip()[:300])
                        response = await_deferred_start(args.out / 'server.log', deadline)
                        record['corner_response'] += response
                    match = CORNER_START.search(response)
                    if not match:
                        raise RuntimeError('corner start refused: ' + response.strip()[:300])
                    record['corner_level'] = int(match.group(1))
                    record['load_response'] = str(rcon_send(port, password,
                                                            f'bot_route_play load {name}',
                                                            timeout=3, collect=.5))
                    record['arm_response'] = str(rcon_send(port, password, 'bot_route_play arm 0',
                                                           timeout=3, collect=.5))
                    armed = True
                    # rcon redirects the engine's console output to the caller,
                    # so a refusal answers here and never reaches the log. Fail
                    # on it now rather than waiting out the deadline.
                    refusal = REFUSED.search((record['arm_response'] or '') +
                                             (record['load_response'] or ''))
                    if refusal:
                        record['refused'] = dict(fields(refusal.group(1)),
                                                 REFUSED=refusal.group(1).strip())
                        break
                text = read_log(args.out / 'server.log')
                refusal = REFUSED.search(text)
                if refusal:
                    record['refused'] = fields(refusal.group(1))
                    record['refused']['REFUSED'] = refusal.group(1).strip()
                    break
                finished = DONE.search(text)
                if finished:
                    record['done'] = fields(finished.group(1))
                    break
                time.sleep(.25)
        except Exception as error:      # kept as evidence, never hidden
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
    text = read_log(args.out / 'server.log')
    record['marks'] = [fields(m) for m in MARK.findall(text)]
    record['start_line'] = (lambda m: fields(m.group(1)) if m else None)(START.search(text))
    record['handoff'] = (lambda m: fields(m.group(1)) if m else None)(HANDOFF.search(text))
    record['prefix_progress'] = [fields(m) for m in PREFIX.findall(text)]
    if not record['done']:
        finished = DONE.search(text)
        if finished:
            record['done'] = fields(finished.group(1))
    record['pin_missed'] = pin_missed(args, record['corner_level'])
    record['verdict'] = verdict(record, asset)
    if record['pin_missed']:
        record['verdict'] = dict(state='inconclusive', reason='corner_class_miss',
                                 corner_level=record['corner_level'],
                                 note='the world did not start on the pinned class; it is retained '
                                      'and another is run, as the bench does')
    (args.out / 'approval.json').write_text(json.dumps(record, indent=2))
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('asset', type=Path, help='a .route.json built by route_asset_build.py')
    parser.add_argument('--build', type=Path, required=True, help='a strafe_fixture_build.py build')
    parser.add_argument('--out', type=Path, required=True, help='directory for the world and verdict')
    parser.add_argument('--team', default='red', choices=('red', 'blue'))
    parser.add_argument('--gain', type=float, default=0.30,
                        help='bot_routeFollowGain: degrees of rotation per unit off the recorded '
                             'line. 0 arms the follower with no steering, which measures how much '
                             'entry error open-loop playback survives.')
    parser.add_argument('--follow', action='store_true',
                        help='bot_routeFollow 1: steer back onto the recorded line instead of '
                             'replaying open loop, and arm where the bot stands')
    parser.add_argument('--start', type=lambda s: [float(v) for v in s.replace(',', ' ').split()],
                        help='place the bot here instead of on the tape start, e.g. '
                             '"152,-1160,1924.125" (the benchmark stand). Meaningful with '
                             '--follow or --prefix; open-loop playback refuses a start off '
                             'its own.')
    parser.add_argument('--prefix', action='store_true',
                        help='bot_routePrefix 1: walk the movement engine from --start into the '
                             "tape's start, then play the tape unchanged. The walk's time is "
                             'reported separately from the route time.')
    parser.add_argument('--prefix-spend', type=int, default=0,
                        help='bot_moveSpend for the prefix walk (MOVE_SPEND_* bits; 1 Speed, '
                             '2 hold). 0 leaves the tape the Force pool it was recorded with.')
    parser.add_argument('--prefix-along', type=float, default=24.0,
                        help='hand-off gate along the route first line, in units')
    parser.add_argument('--prefix-cross', type=float, default=12.0,
                        help='hand-off gate across it, in units; the measured cliff is 16')
    parser.add_argument('--prefix-speed', type=float, default=40.0,
                        help='hand-off speed ceiling; the tape starts standing')
    parser.add_argument('--prefix-settle', type=int, default=0,
                        help='ms the bot must hold inside the gate before the tape starts')
    parser.add_argument('--prefix-timeout', type=int, default=30000,
                        help='ms the walk has to arrive before the run is refused')
    parser.add_argument('--corner-level', type=int, metavar='MS',
                        help='hold the replay until the world reaches this exact level time, and '
                             'refuse the world if it lands anywhere else. Multiple of 25.')
    parser.add_argument('--corner-level-lead', type=int, default=0, metavar='MS',
                        help='how early to send the corner start when pinning an exact level')
    parser.add_argument('--corner-class', type=int,
                        help='pin the start level to this residue of 200 ms, as movement_bench.py '
                             'does; the replay is held until the world stands on the class')
    parser.add_argument('--corner-class-min', type=int, default=3000)
    parser.add_argument('--corner-class-max', type=int, default=5500)
    parser.add_argument('--port', type=int, default=29960)
    parser.add_argument('--promote', action='store_true',
                        help='on approval, write the verdict into the asset itself')
    args = parser.parse_args()
    from world_retention import require_free   # both drives, as defense_laps
    require_free(args.out)
    if args.corner_level is not None and args.corner_class is not None:
        parser.error('pin the exact level or the class, not both')
    validate_corner_class(args.corner_class, args.corner_class_min, args.corner_class_max)
    # The engine resolves fs_homepath against its own working directory, so a
    # relative one lands inside the world it is meant to create.
    args.out = args.out.resolve()
    args.build = args.build.resolve()
    args.asset = args.asset.resolve()
    asset = json.loads(args.asset.read_text())
    tape = args.asset.parent / (args.asset.name[:-len('.route.json')] + '.tape')
    if not tape.is_file():
        raise SystemExit('the asset has no tape beside it: %s' % tape)
    record = run(asset, args.asset, tape, args)
    v = record['verdict']
    marks = record['marks']
    drifts = [float(m['drift']) for m in marks if 'drift' in m]
    handoff = record.get('handoff') or {}
    print('%s: %s (%s) elapsed=%s reference=%s max_drift=%s marks=%d%s%s' % (
        record['name'], v['state'], v.get('reason'), v.get('elapsed_ms'),
        asset['reference']['elapsed_ms'], v.get('max_drift'), len(marks),
        '' if not drifts else ' median_drift=%.2f' % sorted(drifts)[len(drifts) // 2],
        '' if not handoff else
        ' prefix_ms=%s along=%s cross=%s vertical=%s speed=%s force=%s/%s' % (
            handoff.get('prefix_ms'), handoff.get('along'), handoff.get('cross'),
            handoff.get('vertical'), handoff.get('speed'), handoff.get('force'),
            handoff.get('tape_force'))))
    if record['error']:
        print('  error: ' + record['error'])
    if v['state'] == 'approved' and args.promote:
        asset['approval'] = dict(v, world=str(args.out), build=str(args.build),
                                 approved_at_level=record['corner_level'])
        args.asset.write_text(json.dumps(asset, indent=2))
        print('  promoted: %s is an approved route' % args.asset.name)
    return 0 if v['state'] == 'approved' else 1


if __name__ == '__main__':
    sys.exit(main())
