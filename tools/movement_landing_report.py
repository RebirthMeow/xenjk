"""Extract the unresolved grounded landing frames from a bound momentum audit.

Keep one copy of each observed trace. Select grounded sequence-end landings
without a recorded straight search, or with a commit but no admitted tap-now
alternative. The report retains every same-frame candidate and native line;
an unsupported trigger contact remains unknown, never an inferred fall.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from movement_momentum_audit import ADMITTED, audit as audit_case, digest, fields


def candidate_class(row):
    outcome = row.get('outcome')
    unsupported = int(row.get('unsupported', '0'))
    if outcome in ADMITTED:
        return 'admitted'
    if unsupported & 256:
        return 'trigger_contact_unknown'
    if unsupported:
        return 'other_unsupported_unknown'
    if outcome == 'off_path':
        return 'selected_path_rejection'
    return 'other_reported_outcome'


def indexed_events(text):
    events = defaultdict(list)
    for number, line in enumerate(text.splitlines(), 1):
        tag = re.search(r'\[(MOVE|MOMENTUM|SPIKE|REPLAY)\]', line)
        if not tag:
            continue
        values = fields(line)
        if 'level' not in values:
            continue
        events[int(values['level'])].append(dict(line=number, tag=tag.group(1),
                                                 fields=values, raw=line))
    return events


def selected_runs(runs):
    seen = set()
    for run in runs:
        if run['trace_sha256'] not in seen:
            seen.add(run['trace_sha256'])
            yield run


def extract(run, text):
    events = indexed_events(text)
    results = []
    for landing in run['landings']:
        if not landing['grounded_next'] or landing['sequence_position'] != 'end':
            continue
        action = landing['action']
        if action != 'no_recorded_straight_search' and not (
                action == 'commit' and not landing['admitted_tap']):
            continue
        same_frame = events[landing['level']]
        lands = [e for e in same_frame if e['tag'] == 'MOVE'
                 and e['fields'].get('reason') == 'land'
                 and e['fields'].get('event') == 'edge']
        if len(lands) != 1:
            raise ValueError('Expected one landing event at level %d' % landing['level'])
        native = lands[0]['fields']
        client = native.get('client')
        same_frame = [e for e in same_frame if e['fields'].get('client') == client]
        taps = []
        for event in same_frame:
            row = event['fields']
            if event['tag'] == 'SPIKE' and float(row.get('r0', '-1')) == 0:
                taps.append(dict(event, classification=candidate_class(row)))
        if any(c['classification'] == 'admitted' for c in taps):
            raise ValueError('Audit contradicts the selected frame: admitted tap present')
        classes = Counter(c['classification'] for c in taps)
        results.append(dict(case=run['case'], level=landing['level'],
            relative_ms=landing['level'] - run['start_exclusive'],
            group='missing_straight_search' if action == 'no_recorded_straight_search'
                  else 'grounded_commit_without_admitted_tap',
            position=[float(native[axis]) for axis in ('x', 'y', 'z')],
            speed=float(native['speed']), landing_line=lands[0]['line'],
            hop=landing['hop'], committed_hops=landing['committed_hops'],
            commit_runup=landing['commit_runup'], reissued_up=landing['reissued_up'],
            classification=('no_candidate_evidence' if not taps else next(iter(classes))
                            if len(classes) == 1 else 'mixed'),
            tap_class_counts=dict(classes), tap_candidates=taps,
            replay_skips=[e for e in same_frame if e['tag'] == 'REPLAY'
                          and e['fields'].get('kind') == 'skip'],
            same_frame_events=same_frame))
    return results


def relative_trace(run, text, end_ms):
    """Canonical logged command/state fields, changing only the level origin."""
    rows = []
    for level, events in sorted(indexed_events(text).items()):
        relative = level - run['start_exclusive']
        if not 0 < relative <= end_ms:
            continue
        for event in events:
            if event['tag'] == 'MOMENTUM':
                row = dict(event['fields'])
                row['level'] = str(relative)
                rows.append(row)
    return rows


def trace_digest(rows):
    return hashlib.sha256(json.dumps(rows, sort_keys=True).encode()).hexdigest()


def comparison(retained, fresh, bindings):
    """Compare exact logged fields over a common safe window; no interpolation."""
    comparisons = []
    old_runs = list(selected_runs(retained['runs']))
    for case in fresh:
        run = audit_case(case)
        bindings.update(run['bindings'])
        text = (case / 'server.log').read_text(encoding='utf-8')
        result = json.loads((case / 'result.json').read_text(encoding='utf-8'))
        fresh_frames = extract(run, text)
        for old in old_runs:
            old_text = (Path(old['case']) / 'server.log').read_text(encoding='utf-8')
            end_ms = min(run['end_inclusive'] - run['start_exclusive'],
                         old['end_inclusive'] - old['start_exclusive'])
            before, after = relative_trace(old, old_text, end_ms), relative_trace(run, text, end_ms)
            matches = []
            for target in extract(old, old_text):
                # This is a match of the retained telemetry precision, not of
                # every hidden field in playerState_t.
                exact = [f for f in fresh_frames if
                         (f['relative_ms'], f['position'], f['speed'], f['hop'], f['committed_hops']) ==
                         (target['relative_ms'], target['position'], target['speed'],
                          target['hop'], target['committed_hops'])]
                matches.append(dict(old_level=target['level'], relative_ms=target['relative_ms'],
                                    matches=exact))
            comparisons.append(dict(retained_case=old['case'], fresh_case=run['case'],
                replay=result['settings'].get('bot_moveReplay', 0),
                time_ms=result['evaluation'].get('time_ms'),
                common_safe_end_ms=end_ms, retained_frames=len(before), fresh_frames=len(after),
                retained_relative_sha256=trace_digest(before), fresh_relative_sha256=trace_digest(after),
                logged_command_state_equal=before == after,
                target_matches=matches))
    return comparisons


def build(audit_path, fresh=()):
    audit = json.loads(audit_path.read_text(encoding='utf-8'))
    if audit.get('format') != 'MOMENTUM_AUDIT1':
        raise ValueError('Expected MOMENTUM_AUDIT1')
    bindings = {str(audit_path.resolve()): digest(audit_path)}
    frames, used = [], []
    for run in selected_runs(audit['runs']):
        for name, expected in run['bindings'].items():
            if digest(Path(name)) != expected:
                raise ValueError('Input changed since the audit: ' + name)
            bindings[name] = expected
        log = Path(run['case']) / 'server.log'
        frames.extend(extract(run, log.read_text(encoding='utf-8')))
        used.append(run['case'])
    comparisons = comparison(audit, fresh, bindings)
    return dict(format='LANDING_REPORT1', inputs=bindings, distinct_cases=used,
                fresh_comparisons=comparisons,
                frames=frames, frame_counts=dict(Counter(x['group'] for x in frames)),
                frame_classes=dict(Counter(x['classification'] for x in frames)),
                tap_candidate_classes=dict(Counter(c['classification'] for f in frames
                                                   for c in f['tap_candidates'])),
                limits=['A missing straight-search event does not identify its skip reason.',
                        'unsupported bit 256 identifies trigger contact, not the entity or a fall.',
                        'Candidate outcomes are hypothetical, not executed counterfactuals.',
                        'Native rows are grouped by client and level; original lines are retained.'])


def render(report):
    lines = ['# Retained landing-frame evidence', '',
             '| world | level | position | speed | group | tap evidence |',
             '| --- | ---: | --- | ---: | --- | --- |']
    for frame in report['frames']:
        labels = ', '.join('%s=%d' % item for item in frame['tap_class_counts'].items()) or 'none'
        lines.append('| %s | %d | %s | %.1f | %s | %s |' % (
            Path(frame['case']).parent.name, frame['level'],
            ', '.join('%.1f' % x for x in frame['position']), frame['speed'], frame['group'], labels))
    lines += ['', 'Each candidate and its source line are retained in report.json.', '']
    lines += ['- ' + note for note in report['limits']]
    if report.get('fresh_comparisons'):
        lines += ['', '## Fresh trace comparisons', '',
                  '| retained | fresh | time ms | same relative command/state trace | matched targets | skip reasons on matches |',
                  '| --- | --- | ---: | --- | ---: | --- |']
        for row in report['fresh_comparisons']:
            matches = [m for target in row['target_matches'] for m in target['matches']]
            skips = Counter(e['fields'].get('why', 'unknown') for m in matches for e in m['replay_skips'])
            lines.append('| %s | %s | %s | %s | %d | %s |' % (
                Path(row['retained_case']).parent.name, Path(row['fresh_case']).parent.name,
                row['time_ms'], row['logged_command_state_equal'], len(matches), dict(skips)))
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audit', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--compare', nargs='*', type=Path, default=[],
                        help='completed fresh case directories with optional replay telemetry')
    args = parser.parse_args()
    report = build(args.audit, args.compare)
    args.out.mkdir(parents=True, exist_ok=False)
    with (args.out / 'report.json').open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    with (args.out / 'report.md').open('x', encoding='utf-8') as stream:
        stream.write(render(report))
    print(render(report))


if __name__ == '__main__':
    main()
