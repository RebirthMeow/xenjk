"""Recovery from a declared kick: a knockback campaign against its control.

Both arguments are `movement_bench.py` campaign directories (or their
result.json) over the same case file, the second run with `--set bot_moveKick=`.
Each case is read as: did the kick fire, and what the world did after it -
passed, and how much slower than the control, or failed, and how.

A case whose kick never fired is reported separately and never counted as a
recovery: a hop kick in a world that committed no hop measured nothing.

Usage:
    python tools/movement_kick_report.py <control> <kicked> [--json out.json]
"""
import argparse
import json
from pathlib import Path
import re
import statistics
import sys


def campaign(target):
    target = Path(target)
    path = target if target.is_file() else target / 'result.json'
    doc = json.loads(path.read_text(encoding='utf-8'))
    return doc, {c['name']: c for c in doc['cases']}


KICK_AT = re.compile(r'\[KICK\] APPLIED level=(\d+) ')
MOMENTUM = re.compile(r'\[MOMENTUM\] level=(\d+) client=\d+ .*? ground=(\d)')
POSITION = re.compile(r'\[MOVE\] client=\d+ level=(\d+) .*? z=(-?[\d.]+)')


def death_class(text, corner_level, time_ms):
    """How a kicked world died: `direct` when the bot's first ground after the
    kick is the height it died at - the push carried it into the hazard - and
    `landed_then_fell` when it first stood somewhere else and fell from there.
    The first is the push; the second is the recovery.

    Ground is two consecutive grounded [MOMENTUM] frames; heights are the
    [MOVE] events' own, the first at or after that ground and the last at or
    before the death level. None when the log cannot say."""
    kick = KICK_AT.search(text)
    if not kick or corner_level is None or time_ms is None:
        return None
    at = int(kick.group(1))
    died = corner_level + time_ms
    # Only the evaluated life: a respawn's ground and position are not a
    # landing after the kick, and read as one they made a death with no
    # landing before it `landed_then_fell`. One 25 ms frame of margin, as
    # the death's own position is printed on the frame that reports it.
    end = died + 25
    frames = [(int(m.group(1)), int(m.group(2))) for m in MOMENTUM.finditer(text) if at < int(m.group(1)) <= end]
    ground = next((lv for i, (lv, g) in enumerate(frames) if g and i + 1 < len(frames) and frames[i + 1][1]), None)
    places = [(int(m.group(1)), float(m.group(2))) for m in POSITION.finditer(text) if at < int(m.group(1)) <= end]
    first = next((z for lv, z in places if ground is not None and lv >= ground), None)
    last = [z for lv, z in places]
    if first is None or not last:
        return None
    return 'direct' if abs(first - last[-1]) <= 32 else 'landed_then_fell'


def compare(control, kicked, logs=None):
    """One row per case in the kicked run, and the totals over them. `logs`
    maps a case name to its server.log text, for classifying deaths."""
    rows = []
    for name, k in sorted(kicked.items()):
        c = control.get(name)
        kicks = k.get('kicks') or dict(armed=0, applied=[], unfired=[])
        fired = bool(kicks['applied'])
        row = dict(name=name, fired=fired, armed=kicks['armed'], unfired=kicks['unfired'],
                   status=k['status'], reason=k['reason'], time_ms=k['time_ms'], deaths=k.get('deaths', 0),
                   control_status=c['status'] if c else None, control_time_ms=c['time_ms'] if c else None,
                   delta_ms=None, kick=kicks['applied'][0] if fired else None)
        if c and c['status'] == 'pass' and k['status'] == 'pass':
            row['delta_ms'] = k['time_ms'] - c['time_ms']
        row['death_class'] = (death_class(logs[name], k.get('corner_level'), k['time_ms'])
                              if k['reason'] == 'death' and logs and name in logs else None)
        rows.append(row)
    fired = [r for r in rows if r['fired']]
    deltas = [r['delta_ms'] for r in fired if r['delta_ms'] is not None]
    totals = dict(cases=len(rows), fired=len(fired), unfired=len(rows) - len(fired),
                  passed=sum(r['status'] == 'pass' for r in fired),
                  failed=sum(r['status'] == 'fail' for r in fired),
                  incomplete=sum(r['status'] == 'incomplete' for r in fired),
                  deaths=sum(r['reason'] == 'death' for r in fired),
                  deaths_direct=sum(r['death_class'] == 'direct' for r in fired),
                  deaths_after_landing=sum(r['death_class'] == 'landed_then_fell' for r in fired),
                  stalls=sum(r['reason'] == 'stall' for r in fired),
                  control_failed=sum(r['control_status'] != 'pass' for r in fired),
                  paired=len(deltas),
                  delta_median_ms=statistics.median(deltas) if deltas else None,
                  delta_mean_ms=statistics.mean(deltas) if deltas else None,
                  delta_max_ms=max(deltas) if deltas else None)
    return rows, totals


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('control')
    parser.add_argument('kicked')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args(argv)
    control_doc, control = campaign(args.control)
    kicked_doc, kicked = campaign(args.kicked)
    if control_doc.get('cases_sha256') != kicked_doc.get('cases_sha256'):
        print('refused: the two campaigns ran different case files', file=sys.stderr)
        return 2
    root = Path(args.kicked)
    root = root.parent if root.is_file() else root
    logs = {name: (root / name / 'server.log').read_text(errors='replace')
            for name, k in kicked.items() if k['reason'] == 'death' and (root / name / 'server.log').is_file()}
    rows, totals = compare(control, kicked, logs)
    for r in rows:
        kick = r['kick']
        where = ('%s at %d ms, %.0f -> %.0f u/s' % (kick['trigger'], kick['rel_ms'], kick['speed'], kick['after_speed'])
                 if kick else 'unfired')
        delta = '%+d ms' % r['delta_ms'] if r['delta_ms'] is not None else ''
        print('%-24s %-10s %-8s %-7s %8s  control %-4s %8s  %s%s' % (
            r['name'], r['status'], r['reason'], delta, r['time_ms'], r['control_status'], r['control_time_ms'], where,
            '  (%s)' % r['death_class'] if r['death_class'] else ''))
    print(json.dumps(totals))
    if args.json:
        args.json.write_text(json.dumps(dict(control=str(args.control), kicked=str(args.kicked),
                                             totals=totals, rows=rows), indent=2), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
