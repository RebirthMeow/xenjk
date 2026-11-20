"""Audit retained momentum traces inside a conservative outbound-only window.

The first carrying CSV sample only bounds pickup from above. End at the last
noncarrying sample instead. Exclude the fixture's own level because telemetry
at that level can precede placement. Require every 25 ms sample of each 500 ms
landing window. Never infer a two-hop sequence from the name 'hop0'.
"""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
from statistics import mean

OFFSETS = (0, 25, 50, 75, 100, 200, 300, 500)
ADMITTED = {'admitted', 'arrives', 'rerouted', 'tap_onward'}


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(landings):
    result = {}
    for name, group in [('all', landings),
                        ('airborne', [x for x in landings if not x['grounded_next']]),
                        ('grounded', [x for x in landings if x['grounded_next']])]:
        result[name] = dict(count=len(group),
                            speed={str(t): mean(x['speeds'][str(t)] for x in group)
                                   for t in OFFSETS} if group else {})
    result['grounded_categories'] = dict(Counter(x['action'] for x in landings if x['grounded_next']))
    result['sequence_categories'] = dict(Counter(
        x['sequence_position'] + '/' + ('grounded' if x['grounded_next'] else 'airborne')
        for x in landings))
    result['grounded_commits_with_admitted_tap'] = sum(
        x['grounded_next'] and x['action'] == 'commit' and x['admitted_tap'] for x in landings)
    return result


def audit(case):
    result_path = case / 'result.json'
    result = json.loads(result_path.read_text())
    start = int(result['corner_level'])
    play_paths = list((case / 'base/bot2-play').glob('*.csv'))
    if len(play_paths) != 1:
        raise ValueError('Expected exactly one play CSV: ' + str(case))
    with play_paths[0].open(newline='') as stream:
        rows = [x for x in csv.DictReader(stream) if int(x['level_time']) > start]
    pickup_index = next(i for i, x in enumerate(rows) if int(x['carrying']))
    if pickup_index == 0:
        raise ValueError('No observed outbound interval before pickup')
    end = int(rows[pickup_index - 1]['level_time'])
    upper = int(rows[pickup_index]['level_time'])
    momentum, events, spikes = {}, [], defaultdict(list)
    for line in (case / 'server.log').read_text().splitlines():
        if not any(tag in line for tag in ('[MOMENTUM]', '[MOVE]', '[SPIKE]')):
            continue
        row = fields(line)
        if '[MOMENTUM]' in line:
            momentum[int(row['level'])] = row
        elif '[MOVE]' in line:
            events.append(row)
        else:
            spikes[int(row['level'])].append(row)
    actions = defaultdict(list)
    for row in events:
        actions[int(row['level'])].append(row)
    plan = None
    landings, exclusions = [], Counter()
    for row in events:
        level = int(row['level'])
        if row.get('reason') == 'sequence':
            plan = row
        if row.get('reason') != 'land' or row.get('event') != 'edge':
            continue
        if level <= start:
            exclusions['before_or_at_fixture'] += 1
            continue
        if level + 500 > end:
            exclusions['window_not_proven_before_pickup'] += 1
            continue
        if any(level + t not in momentum for t in range(0, 501, 25)):
            exclusions['missing_frame'] += 1
            continue
        if plan is None or int(plan['level']) <= start:
            raise ValueError('Landing has no in-leg committed plan')
        hop = int(row['edge'].removeprefix('hop'))
        hops = int(plan['hops'])
        if not 0 <= hop < hops:
            raise ValueError('Landing index outside its committed plan')
        commits = [x for x in actions[level] if x.get('reason') == 'sequence']
        searched = any(x.get('kind') == 'straight' and x.get('reason') == 'none'
                       for x in actions[level])
        landings.append(dict(level=level, hop=hop, committed_hops=hops,
            sequence_position='middle' if hop + 1 < hops else 'end',
            grounded_next=int(momentum[level + 25]['ground']) != 0,
            action='commit' if commits else 'nothing_admitted' if searched else 'no_recorded_straight_search',
            commit_runup=commits[0].get('r0') if commits else None,
            reissued_up=commits[0].get('reissued_up') if commits else None,
            admitted_tap=any(x.get('r0') == '0' and x.get('outcome') in ADMITTED
                             for x in spikes[level]),
            speeds={str(t): float(momentum[level + t]['speed']) for t in OFFSETS}))
    # Compare parsed telemetry, excluding wall-clock prefixes and pre-fixture rows.
    trace = [momentum[t] for t in sorted(momentum) if start < t <= end]
    signature = hashlib.sha256(json.dumps(trace, sort_keys=True).encode()).hexdigest()
    return dict(case=str(case.resolve()), start_exclusive=start, end_inclusive=end,
                pickup_upper_bound=upper, trace_frames=len(trace), trace_sha256=signature,
                bindings={str(p.resolve()): digest(p) for p in
                          (result_path, case / 'server.log', play_paths[0])},
                excluded=dict(exclusions), landings=landings, summary=summarize(landings))


def combine(reports):
    groups = defaultdict(list)
    for report in reports:
        groups[report['trace_sha256']].append(report)
    return dict(format='MOMENTUM_AUDIT1', runs=reports,
                pooled=summarize([x for r in reports for x in r['landings']]),
                unique_traces=summarize([x for group in groups.values() for x in group[0]['landings']]),
                duplicate_groups=[[r['case'] for r in group] for group in groups.values() if len(group) > 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='+', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    report = combine([audit(case) for case in args.cases])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'runs'}, indent=2))


if __name__ == '__main__':
    main()
