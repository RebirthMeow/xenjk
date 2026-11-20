"""Build a decision tape from a retained world's telemetry. Read-only.

A decision tape is the list of committed decisions one world made, in order,
so another world can be asked to make the same ones. It is not a command tape:
it names *what was chosen*, not the inputs that carried it out, so a world
executing it still searches, rolls out and judges from its own live state and
still refuses what is not admissible there. That is the difference the
open-loop replay could not make - see
`docs/movement/records/2026-09-21-bot-route-recorder.md`.

Everything here reads. Nothing in a retained world is written or moved.

The source is the `[MOVE] ... event=plan edge=hop0 reason=sequence` line the
game prints after a commit, one per committed sequence. It already carries
every field the match needs:

  x, y, z        the root the decision was taken from (the `Event` prefix)
  kind           straight / state / edge / gap / spike
  hops           committed hop count
  d0, r0         the first hop's aim, in degrees off the path reference
  d1, r1         direction, and its run-up; then the second hop's - as printed
  c0, c1         the graph cell each hop lands in, or -1
  index, index2  the plan's graph edge and the second edge of a chain, or -1

**What identifies a decision, and what does not.** A state plan is named by its
kind, hop count, both run-ups and its graph edges; a straight plan has no edges
and is named by its kind, hop count, both run-ups and the cell each hop lands
in. All discrete, all exact.

The aims are carried and reported, never compared. Measured in the
class-125 world's own judged sets: of 166 admitted straight pairs agreeing on
hops and both run-ups, **7 agree on both aims as well** and the smallest
nonzero separation is 0.030304 degrees, while the start class perturbs the
first entry's aim by up to 0.066 degrees. The aim never discriminated inside a
set, so no tolerance on it could define the same decision across starts. The
landing cell is discrete and the map's rather than the run's, so it does.

The caveat: a landing cell is a rollout result, not a plan field, so this
identity exists only for candidates that were rolled out. That covers every
candidate the override can select, since it selects only admitted ones, and
every entry here, since each was a committed rollout. It would not serve as a
plan identity for an explorer; it is the right one for asking whether the same
hop is admissible from a different start.

Only commits of the leg are taken: strictly after the world's corner release,
and no later than the objective. A fixture world runs the bot's own AI before
the release and keeps playing after the flag, and neither belongs to the leg -
including a commit printed on the release level itself, because the corner
command runs in a frame the game has already stepped.
`movement_bench_lib.runner_rows` is strict at the start for the same reason.

The leg ends at the pickup, which the runner records as `corner_level` plus
`time_ms`; failing that, at the first objective change after the release, which
the log prints. A world with neither is carried whole and the sidecar says so.

The map and checksum in the header come from the same log's `[GRAPH] map_load`
line, so a tape cannot be loaded against a world it was not built in. The same
line carries the graph's own identity (`graph=g1-...`, a digest of the cells
and edges as loaded), and that goes into the header too: a tape names graph
cells and graph edges by index, and the map checksum cannot tell a seeded
graph from an unseeded one on the same map. The engine refuses a tape whose
declared graph is not the loaded one. A log written before the engine printed
the field yields `graph=none`, which loads as an unbound tape and says so.

The release level is not in the log: `bot_corner_start` is issued over rcon and
its reply is redirected to the caller, so the runner records it in the world's
own `result.json` or `approval.json`. Those are read; `--corner-level` names it
directly for a world that has neither.

Usage:
    python tools/movement_decision_tape.py <world-dir-or-server.log> --out <file>
"""
import argparse
import json
from pathlib import Path
import re
import sys

FORMAT = 'decisiontape'
VERSION = 3
# The engine names these; a tape carrying anything else would not match.
KINDS = ('spike', 'gap', 'edge', 'straight', 'state')

SEQUENCE = re.compile(r'\[MOVE\] client=(?P<client>\d+) level=(?P<level>-?\d+) '
                      r'event=plan edge=hop0 reason=sequence .*?'
                      r'x=(?P<x>-?[\d.]+) y=(?P<y>-?[\d.]+) z=(?P<z>-?[\d.]+) '
                      r'kind=(?P<kind>\w+) (?:.*?hops=(?P<hops>\d+) )?')
FIELD = re.compile(r'(\w+)=(-?[\w.+-]+)')
MAP_LOAD = re.compile(r'\[GRAPH\] map_load map=(?P<map>\S+) checksum=(?P<checksum>-?\d+)'
                      r'(?: graph=(?P<graph>\S+))?')
CORNER = re.compile(r'\[CORNER\] START .*?level=(\d+)')
OBJECTIVE = re.compile(r'\[MOVE\] client=\d+ level=(\d+) event=plan edge=none reason=objective')
# The runner's own record of the release, for the usual case where the corner
# reply never reached the log.
RUNNER_RECORDS = ('result.json', 'approval.json')


class TapeError(Exception):
    """The log cannot produce a tape. Never repaired by guessing."""


def server_log(target):
    """The log inside a retained world, or the log itself."""
    target = Path(target)
    if target.is_file():
        return target
    for candidate in sorted(target.rglob('server.log')):
        return candidate
    raise TapeError('no server.log under %s' % target)


def binding(text):
    """The map, the checksum and the graph identity the world loaded.

    The graph identity is absent from logs written before the engine printed
    it; such a tape is unbound and the engine accepts it with the binding it
    has, which is the behaviour every earlier campaign replays under.
    """
    found = MAP_LOAD.search(text)
    if not found:
        raise TapeError('log has no [GRAPH] map_load line to bind the tape to')
    return (found.group('map'), int(found.group('checksum')),
            found.group('graph') or 'none')


def case_records(log):
    """This world's own runner records, as (holder, path), nearest first.

    The record beside the log is the world's own. The one above it is the
    campaign's, and holds every case it ran: only the entry named after this
    world's directory is this world's, so a sibling's release or finish time
    is never read as its own.
    """
    world = log.parent.name
    for directory in (log.parent, log.parent.parent):
        for name in RUNNER_RECORDS:
            path = directory / name
            if not path.is_file():
                continue
            try:
                record = json.loads(path.read_text(encoding='utf-8'))
            except ValueError:
                continue
            if not isinstance(record, dict):
                continue
            cases = [c for c in (record.get('cases') or []) if isinstance(c, dict)]
            if directory == log.parent:
                holders = [record] + cases
            else:
                holders = [c for c in cases if c.get('name') == world] or ([] if cases else [record])
            for holder in holders:
                yield holder, str(path)


def recorded_time(holder):
    """A case's finish time: the campaign's `time_ms`, or a world's own `evaluation.time_ms`."""
    if isinstance(holder.get('time_ms'), int):
        return holder['time_ms']
    value = (holder.get('evaluation') or {}).get('time_ms')
    return value if isinstance(value, int) else None


def recorded_level(log):
    """The release level as the runner recorded it, beside the world or above it."""
    for holder, path in case_records(log):
        if isinstance(holder.get('corner_level'), int):
            return holder['corner_level'], path
    return None, None


def leg_end(text, log, release, declared=None):
    """The level the leg ends on, and where that came from.

    The transfer question is about one leg. A world keeps playing after the
    flag and commits again on the way back; those are another leg's decisions.
    """
    if declared is not None:
        return declared, 'declared'
    for holder, path in case_records(log):
        time_ms = recorded_time(holder)
        if isinstance(holder.get('corner_level'), int) and time_ms is not None:
            return holder['corner_level'] + time_ms, path
    for found in OBJECTIVE.finditer(text):
        level = int(found.group(1))
        if level > release:
            return level, 'objective change'
    return None, 'not found: the whole world is carried'


def corner_level(text, log, declared=None):
    """The release level, from the caller, the runner's record, or the log.

    A world without one never started the case, and a tape built across the
    release would carry the bot's own pre-release wandering as decisions.
    """
    if declared is not None:
        return declared, 'declared'
    level, source = recorded_level(log)
    if level is not None:
        return level, source
    found = CORNER.search(text)
    if found:
        return int(found.group(1)), str(log)
    raise TapeError('no release level: the world has no result.json or approval.json carrying '
                    'corner_level, no [CORNER] START line, and none was declared')


def entries(text, release, end=None):
    """The committed decisions strictly after the release, in order.

    Strictly: `bot_corner_start` runs in a server frame the game has already
    stepped, so a commit printed on the release level itself was taken from
    wherever the bot's own AI stood, not from the stand.
    `movement_bench_lib.runner_rows` is strict for the same reason.
    """
    out = []
    for line in text.splitlines():
        head = SEQUENCE.search(line)
        if not head:
            continue
        level = int(head.group('level'))
        if level <= release or (end is not None and level > end):
            continue
        fields = dict(FIELD.findall(line.split('reason=sequence', 1)[1]))
        kind = head.group('kind')
        # Matched with or without `hops=`, so a commit no tape can carry (a
        # climb prints none) is refused here rather than silently skipped.
        if kind not in KINDS:
            raise TapeError('commit at level %d has an unknown kind %r' % (level, kind))
        for name in ('d0', 'r0', 'd1', 'r1', 'c0', 'c1', 'index', 'index2', 'commit_nth'):
            if name not in fields:
                raise TapeError('commit at level %d has no %s field' % (level, name))
        if kind != 'state' and int(fields['c0']) < 0:
            raise TapeError('straight commit at level %d has no landing cell; a straight '
                            'decision is named by where it goes and this one cannot be '
                            'named' % level)
        out.append(dict(index=len(out), level=level, client=int(head.group('client')),
                        root=[float(head.group(axis)) for axis in 'xyz'],
                        kind=kind, hops=int(head.group('hops')),
                        # As printed. The engine formats its own with %g and the
                        # two strings are compared, not two floats.
                        aim=fields['d0'], runup=fields['r0'],
                        aim1=fields['d1'], runup1=fields['r1'],
                        cell=int(fields['c0']), cell1=int(fields['c1']),
                        edge=int(fields['index']), edge2=int(fields['index2']),
                        commit_nth=int(fields['commit_nth'])))
    if not out:
        raise TapeError('no committed sequences in the leg after level %d' % release)
    return out


def tape_text(name, checksum, release, rows, source, graph='none'):
    """The engine's form: a bound header, then one line per entry.

    `graph=` is the last header field and is optional by construction, so an
    engine or a reader that predates the graph binding still parses the four
    fields before it and ignores this one.

    `#` lines are provenance for a reader and are skipped by the loader.
    """
    lines = ['%s %d map=%s checksum=%d entries=%d graph=%s'
             % (FORMAT, VERSION, name, checksum, len(rows), graph),
             '# source=%s' % source,
             '# corner_level=%d' % release]
    for row in rows:
        lines.append('e %d %.6f %.6f %.6f %s %d %s %s %s %s %d %d %d %d'
                     % (row['index'], row['root'][0], row['root'][1], row['root'][2],
                        row['kind'], row['hops'], row['aim'], row['runup'],
                        row['aim1'], row['runup1'], row['edge'], row['edge2'],
                        row['cell'], row['cell1']))
    return '\n'.join(lines) + '\n'


def build(target, declared=None, leg=None):
    log = server_log(target)
    text = log.read_text(errors='replace')
    name, checksum, graph = binding(text)
    release, release_source = corner_level(text, log, declared)
    end, end_source = leg_end(text, log, release, leg)
    rows = entries(text, release, end)
    return dict(format=FORMAT, version=VERSION, map=name, map_checksum=checksum,
                graph=graph,
                corner_level=release, corner_level_source=release_source,
                leg_end_level=end, leg_end_source=end_source,
                source=str(log), entries=rows,
                counts=dict(total=len(rows),
                            state=sum(1 for r in rows if r['kind'] == 'state'),
                            straight=sum(1 for r in rows if r['kind'] == 'straight')))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('world', help='a retained world directory, or a server.log')
    parser.add_argument('--out', type=Path, required=True,
                        help='the tape file to write; a .json sidecar is written beside it')
    parser.add_argument('--corner-level', type=int,
                        help="the world's release level, when no runner record carries it")
    parser.add_argument('--leg-end', type=int,
                        help='the level the leg ends on; defaults to the recorded pickup, '
                             'then to the first objective change after the release')
    args = parser.parse_args()
    try:
        asset = build(args.world, args.corner_level, args.leg_end)
    except TapeError as error:
        print('decision tape refused: %s' % error, file=sys.stderr)
        return 2
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(tape_text(asset['map'], asset['map_checksum'], asset['corner_level'],
                                  asset['entries'], asset['source'], asset['graph']),
                        encoding='utf-8')
    args.out.with_suffix(args.out.suffix + '.json').write_text(
        json.dumps(asset, indent=2), encoding='utf-8')
    print('%s: %d decisions (%d state, %d straight) on %s graph %s, level %d to %s'
          % (args.out, asset['counts']['total'], asset['counts']['state'],
             asset['counts']['straight'], asset['map'], asset['graph'], asset['corner_level'],
             asset['leg_end_level'] if asset['leg_end_level'] is not None
             else 'the end of the world (%s)' % asset['leg_end_source']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
