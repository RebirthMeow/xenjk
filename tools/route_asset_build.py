"""Build an executable bot route asset from a human route recording.

A recording is evidence; an asset is something the engine can be told to run.
This converts one into the other without editing the source files, and without
deciding that the result is correct: approval is a separate step that plays the
asset back in a fixture world and checks what it does.

The asset has three parts, and the engine needs all three:

  binding    the map, its checksum, the recorded physics settings and the
             source recording's hash. A tape is only valid against the world
             it was recorded in, so the asset carries what to check.
  start      the exact state the tape assumes: origin, velocity, view angles,
             grounding, Force pool. A tape replayed from anywhere else traces a
             parallel line into different geometry.
  tape       one entry per authoritative Pmove call: the physics duration that
             call was given, its movement axes, its buttons, and the view
             angles the command actually produced, in degrees.

View angles are stored in degrees rather than as the recorded 16-bit command
angles, because those are relative to the recording client's delta_angles and
mean something different for another player. Playback converts back using its
own delta_angles.

Beside the tape the asset carries a state contract: the start, every takeoff
and landing, and a checkpoint every 500 ms, each with position, velocity,
speed and the index of the command it was recorded after. Open-loop playback
does not need it. It is what lets a follower notice that it has diverged, and
what an approval run is checked against.

The tape is versioned, and BOTROUTE2 is not a superset of BOTROUTE1: the first
command's duration and the marks' command indices are corrections to fields
that existed, so the same tokens mean different things. Playback refuses the
older tag outright. Rebuilding from the retained recording CSV is the
conversion, and it leaves the older asset and its approval standing as the
historical evidence they are.

Usage:
    python tools/route_asset_build.py <recording.csv> --out <directory>
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

FORMAT = 'BOT_ROUTE2'
TAPE_FORMAT = 'BOTROUTE2'
CHECKPOINT_MS = 500
# Pmove accepts at most one second of physics in a single call: beyond that it
# advances ps->commandTime to finalTime - 1000 and steps the last second only.
# A recorded gap longer than this produced one second of physics, so that is
# what the tape asks for.
MAX_COMMAND_MS = 1000


class RecordingError(SystemExit):
    """A recording that cannot be converted, named rather than approximated."""


def number(value, cast=float):
    try:
        return cast(value)
    except (TypeError, ValueError):
        return None


def vector(row, prefix, names=('x', 'y', 'z')):
    return [number(row.get(prefix + n)) for n in names]


def velocity(row, prefix):
    return [number(row.get(prefix + 'v' + n)) for n in ('x', 'y', 'z')]


def speed2d(v):
    if v[0] is None or v[1] is None:
        return None
    return round((v[0] * v[0] + v[1] * v[1]) ** .5, 3)


def grounded(value):
    """The recorder writes the ground entity number; 1023 is ENTITYNUM_NONE."""
    n = number(value, int)
    return None if n is None else n != 1023


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(65536), b''):
            h.update(block)
    return h.hexdigest()


def sidecar(path, suffix):
    """The recorder writes `<name>.meta.json` and `<name>.summary.json` beside
    `<name>.csv`. A recording without them still builds, with those fields null."""
    candidate = Path(str(path.with_suffix('')) + suffix)
    if candidate.exists():
        with open(candidate, encoding='utf-8') as handle:
            return json.load(handle)
    return {}


def physics(first):
    return dict(pmove_fixed=number(first.get('pmove_fixed'), int),
                pmove_msec=number(first.get('pmove_msec'), int),
                pmove_float=number(first.get('pmove_float'), int),
                gravity=number(first.get('pre_gravity')),
                jump_level=number(first.get('pre_jump_level'), int),
                speed_level=number(first.get('pre_speed_level'), int))


def start_state(first):
    return dict(origin=vector(first, 'pre_'), velocity=velocity(first, 'pre_'),
                yaw=number(first.get('pre_yaw')), pitch=number(first.get('pre_pitch')),
                ground=grounded(first.get('pre_ground')), force=number(first.get('pre_force'), int),
                carrying=bool(number(first.get('pre_redflag'), int) or number(first.get('pre_blueflag'), int)),
                health=number(first.get('pre_health'), int))


def command_ms(row, previous):
    """The physics duration Pmove actually accepted for one recorded command.

    Pmove runs from `ps->commandTime` to `cmd.serverTime`, in steps of at most
    `pmove_msec`, and the remainder is not discarded: the duration is the whole
    difference, clamped to the one second Pmove itself allows. The recording
    carries both ends - `pre_cmd_time` is the player state's command time going
    in, `cmd_time` the command's server time - so the duration is read from the
    row rather than inferred from the previous row's command time.

    Inferring it was the defect: the first row has no previous command, and a
    nominal zero there discards the duration the first command was given. Under
    an 8 ms fixed step a first jump or acceleration with a recorded 32 ms then
    replayed with one physics step instead of four.

    The previous row's command time is the fallback for a recording whose
    `pre_cmd_time` column is missing, and only for rows that have a previous
    one. Nothing here substitutes a nominal step.
    """
    when = number(row.get('cmd_time'), int)
    if when is None:
        return None
    before = number(row.get('pre_cmd_time'), int)
    if before is None:
        before = previous
        if before is None:
            raise RecordingError('row %s has no pre_cmd_time and no previous command: '
                                 'the first command\'s physics duration is not recoverable'
                                 % row.get('row'))
    duration = when - before
    if duration <= 0:
        raise RecordingError('row %s has a command duration of %d ms (cmd_time %d, '
                             'pre_cmd_time %d); Pmove runs no physics for it and a tape '
                             'cannot replay it' % (row.get('row'), duration, when, before))
    return min(duration, MAX_COMMAND_MS)


def tape_entries(rows):
    """One command per authoritative Pmove call, with the duration it was given.

    The duration is the command's own accepted Pmove time, not a nominal
    cadence: a recording with a dropped frame must replay with that frame's
    gap, or every later command lands at a different physics time. Playback
    adds it to the bot's own `ps->commandTime`, so consecutive durations chain
    exactly as they did when recorded.
    """
    entries = []
    previous = None
    for index, row in enumerate(rows):
        when = number(row.get('cmd_time'), int)
        if when is None:
            continue
        entries.append(dict(dt=command_ms(row, previous),
                            source_row=number(row.get('row'), int) if row.get('row') else index,
                            forward=number(row.get('forward'), int),
                            right=number(row.get('right'), int),
                            up=number(row.get('up'), int),
                            buttons=number(row.get('buttons'), int),
                            yaw=round(number(row.get('post_yaw')) or 0, 4),
                            pitch=round(number(row.get('post_pitch')) or 0, 4)))
        previous = when
    return entries


def command_indices(rows):
    """The tape command index each source row produced, by row position.

    A contract mark is the post-Pmove state of one command, and several
    commands can run in one server frame, so a mark has to name the command it
    came from. Tape and contract are built from the same rows, and this is the
    map between them; a row the tape has no command for has no index.
    """
    indices = {}
    command = 0
    for position, row in enumerate(rows):
        if number(row.get('cmd_time'), int) is None:
            continue
        indices[position] = command
        command += 1
    return indices


def contract(rows):
    """The states an approval run is checked against, and a follower steers to.

    A takeoff is the first airborne post-state after a grounded one; a landing
    the first grounded post-state after an airborne one. Both are read from the
    recorded states alone - no jump command is assumed to have caused either.

    Every point names the command that produced it. `post_` is the state the
    recorder snapshotted straight after Pmove, which is the state a replay can
    be compared with command for command; the recorder's third snapshot, taken
    after the frame's touch effects, is a different thing and is not used here.
    `elapsed_ms` is the recording's server-level clock and stays for reporting,
    but it cannot order two commands that ran in one server frame, so it is not
    what a replay reads a mark at.
    """
    points = []
    was_ground = None
    checkpoint = None
    indices = command_indices(rows)
    for position, row in enumerate(rows):
        elapsed = number(row.get('elapsed_ms'), int)
        post = vector(row, 'post_')
        vel = velocity(row, 'post_')
        on_ground = grounded(row.get('post_ground'))
        command = indices.get(position)
        if elapsed is None or on_ground is None or post[0] is None or command is None:
            continue
        state = dict(elapsed_ms=elapsed, command=command, origin=post, velocity=vel,
                     speed=speed2d(vel), yaw=number(row.get('post_yaw')))
        if was_ground is not None and on_ground != was_ground:
            points.append(dict(state, kind='landing' if on_ground else 'takeoff'))
        was_ground = on_ground
        if checkpoint is None or elapsed - checkpoint >= CHECKPOINT_MS:
            points.append(dict(state, kind='checkpoint'))
            checkpoint = elapsed
    return points


def build(source):
    with open(source, encoding='utf-8', newline='') as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit('recording has no rows: %s' % source)
    meta = sidecar(source, '.meta.json')
    summary = sidecar(source, '.summary.json')
    tape = tape_entries(rows)
    asset = dict(
        format=FORMAT,
        binding=dict(map=meta.get('map'), map_checksum=meta.get('map_checksum'),
                     recording=source.name, recording_sha256=digest(source),
                     recorder_format=summary.get('format')),
        physics=physics(rows[0]),
        start=start_state(rows[0]),
        reference=dict(elapsed_ms=summary.get('elapsed_ms'), reason=summary.get('reason'),
                       complete=summary.get('complete'), rows=len(rows),
                       stationary_start=summary.get('stationary_start'),
                       active_powers_seen=summary.get('active_powers_seen'),
                       force_decrease_seen=summary.get('force_decrease_seen')),
        tape=dict(count=len(tape), duration_ms=sum(e['dt'] for e in tape), commands=tape),
        contract=contract(rows),
        approval=dict(state='unapproved',
                      note='Built from a recording. Not executable evidence until an approval '
                           'run plays it in a fixture world and records what it did.'))
    return asset


KINDS = {'checkpoint': 0, 'takeoff': 1, 'landing': 2}


def tape_text(asset):
    """The engine's form of the asset: whitespace tokens, parsed with COM_Parse.

    The module rejects a tape that does not end in END, so a truncated write is
    refused at load instead of replaying into the map with no end.

    BOTROUTE2 differs from BOTROUTE1 in two fields, and both are corrections
    rather than additions: the first command carries its own physics duration
    instead of a zero playback used to replace with `pmove_msec`, and every
    contract mark names the command it was recorded after. A BOTROUTE1 tape
    cannot be read as a BOTROUTE2 one - the same numbers mean different things
    - so playback refuses the older tag rather than reinterpreting assets that
    were approved under it. Rebuilding from the retained recording CSV is the
    conversion.
    """
    start, physical = asset['start'], asset['physics']
    out = [TAPE_FORMAT,
           '%d' % (asset['binding']['map_checksum'] or 0),
           '%d %d' % (physical['pmove_fixed'] or 0, physical['pmove_msec'] or 0),
           '%.3f %.3f %.3f' % tuple(start['origin']),
           '%.3f %.3f %.3f' % tuple(start['velocity']),
           '%.4f %.4f' % (start['yaw'] or 0, start['pitch'] or 0),
           '%d %d' % (start['force'] or 0, int(bool(start['carrying']))),
           '%d %d' % (asset['tape']['count'], len(asset['contract']))]
    for c in asset['tape']['commands']:
        out.append('c %d %d %d %d %d %.4f %.4f' % (c['dt'], c['forward'], c['right'], c['up'],
                                                   c['buttons'], c['yaw'], c['pitch']))
    for k in asset['contract']:
        out.append('k %d %d %.3f %.3f %.3f %.3f %d' % (k['command'], k['elapsed_ms'],
                                                       k['origin'][0], k['origin'][1],
                                                       k['origin'][2], k['speed'] or 0,
                                                       KINDS[k['kind']]))
    out.append('END')
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('recording', type=Path, help='a bot2-route recording CSV')
    parser.add_argument('--out', type=Path, required=True, help='directory for the asset')
    parser.add_argument('--name', help='asset name; defaults to the recording stem')
    parser.add_argument('--force', action='store_true', help='replace an existing asset and tape of this name')
    args = parser.parse_args()
    asset = build(args.recording)
    name = args.name or args.recording.stem
    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / (name + '.route.json')
    existing = [p for p in (target, args.out / (name + '.tape')) if p.exists()]
    if existing and not args.force:   # an approved asset is evidence
        raise SystemExit('refusing to replace %s; pass --force to replace it' % ', '.join(map(str, existing)))
    with open(target, 'w', encoding='utf-8') as handle:
        json.dump(asset, handle, indent=2, allow_nan=False)
    tape = args.out / (name + '.tape')
    with open(tape, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(tape_text(asset))
    takeoffs = sum(1 for p in asset['contract'] if p['kind'] == 'takeoff')
    print('%s: %d commands over %d ms, %d takeoffs, reference %s ms (%s)' % (
        target, asset['tape']['count'], asset['tape']['duration_ms'], takeoffs,
        asset['reference']['elapsed_ms'], asset['reference']['reason']))
    if not asset['reference']['complete']:
        print('  the source recording is not a complete run; the asset is built but cannot be approved')


if __name__ == '__main__':
    main()
