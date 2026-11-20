"""Summarize a native route recording without modifying its files.

Two subjects, reported as different kinds and never conflated: a human
demonstration, armed by the player, whose clock starts at his first effective
movement command; and a bot recording, armed by the corner fixture on its own
release frame, whose clock starts at that release. A bot recording is evidence
about the bot, not a demonstration, and the report says which it read.

Outputs report.txt, report.json and route.json in a new directory. The route
polyline is portable JSON, not an interpolated or retimed trajectory.
"""
import argparse
from collections import Counter
import csv
import datetime
import hashlib
import json
import math
from pathlib import Path
import uuid


def number(value):
    """Missing/nonfinite telemetry remains unknown, never silently zero."""
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def vector(row, names):
    values = [number(row.get(name)) for name in names]
    return values if all(value is not None for value in values) else None


def length(values):
    return math.sqrt(sum(value * value for value in values))


def observed_path(points):
    """A sample polyline lower bound; gaps are not filled or time-warped."""
    horizontal = spatial = 0.0
    segments = gaps = 0
    previous = None
    for point in points:
        if point is None:
            previous = None
            gaps += 1
            continue
        if previous is not None:
            delta = [point[i] - previous[i] for i in range(3)]
            horizontal += length(delta[:2])
            spatial += length(delta)
            segments += 1
        previous = point
    return dict(horizontal_units=horizontal, spatial_units=spatial,
                connected_segments=segments, missing_points=gaps,
                interpretation='Sample polyline; unobserved curvature and substeps are not reconstructed.')


def input_facts(rows, time_key, up_key):
    """Input repetition does not establish engine Force-Jump consumption."""
    positive = [index for index, row in enumerate(rows) if (number(row.get(up_key)) or 0) > 0]
    runs = []
    for index in positive:
        if runs and index == runs[-1][-1] + 1:
            runs[-1].append(index)
        else:
            runs.append([index])
    return dict(positive_up_rows=len(positive), positive_up_runs=len(runs),
                consecutive_positive_up_runs=sum(len(run) > 1 for run in runs),
                maximum_consecutive_positive_up_rows=max([len(run) for run in runs], default=0),
                interpretation='Consecutive accepted commands with positive up input; not proof of held Force Jump or Force spend.')


def cadence_facts(rows, time_key):
    times = [number(row.get(time_key)) for row in rows]
    deltas = [b - a for a, b in zip(times, times[1:]) if a is not None and b is not None]
    counts = Counter(deltas)
    return dict(clock=time_key, missing_timestamps=sum(t is None for t in times),
                duplicate_timestamps=sum(delta == 0 for delta in deltas),
                backwards_timestamps=sum(delta < 0 for delta in deltas),
                observed_delta_ms_counts={str(key): value for key, value in sorted(counts.items())},
                interpretation='Intervals between recorded boundaries; commands can contain multiple Pmove substeps. No airtime is inferred between rows.')


def dump_json(path, data):
    with path.open('x', encoding='utf-8') as output:
        json.dump(data, output, indent=2, allow_nan=False)
        output.write('\n')


def source_binding(path):
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def truth(value):
    return value is True or value == 1 or (isinstance(value, str) and value.lower() in ('true', '1'))


def completion_time(summary):
    """Only an explicitly valid completed recorder result gets a finish time."""
    start = number(summary.get('first_input_time'))
    finish = number(summary.get('pickup_time'))
    errors = []
    if not truth(summary.get('valid')):
        errors.append('Recorder did not declare this run valid.')
    if not truth(summary.get('complete')):
        errors.append('Recorder did not declare an enemy-flag pickup completion.')
    if start is None or finish is None or finish < start:
        errors.append('Missing or inconsistent first-input/pickup native clock boundaries.')
    clock = summary.get('timing_clock')
    if not clock:
        errors.append('Recorder timing clock is not declared.')
    return dict(qualified=not errors, time_ms=finish - start if not errors else None,
                first_input_time=start, pickup_time=finish, clock=clock,
                invalid_reasons=errors, recorder_reason=summary.get('reason'))


def resource_facts(rows, force_keys, active_keys):
    values = [number(row.get(key)) for row in rows for key in force_keys]
    known = [value for value in values if value is not None]
    active = [number(row.get(key)) for row in rows for key in active_keys]
    return dict(observed_force_min=min(known) if known else None,
                observed_force_max=max(known) if known else None,
                missing_force_values=sum(value is None for value in values),
                observed_nonzero_active_masks=sorted(set(int(value) for value in active if value is not None and value != 0)),
                missing_active_values=sum(value is None for value in active),
                force_spent=None,
                interpretation='Observed pools and active-power masks only. Regeneration and within-command changes can hide spending; neither a flat pool nor absent active bits proves no Force use.')


def state(row, prefix):
    return dict(position=vector(row, [prefix + '_' + axis for axis in ('x', 'y', 'z')]),
                velocity=vector(row, [prefix + '_' + axis for axis in ('vx', 'vy', 'vz')]),
                ground=number(row.get(prefix + '_ground')),
                command_time=number(row.get(prefix + '_cmd_time')))


def ground_known(value):
    return value is not None and 0 <= value <= 1023


def transition_events(rows):
    """Report observed boundaries, never invent transitions inside a command."""
    events = []
    previous = None
    for index, row in enumerate(rows):
        snapshots = [('pre', state(row, 'pre')), ('post', state(row, 'post')), ('final', state(row, 'final'))]
        if previous is not None:
            snapshots.insert(0, ('previous_final', previous))
        for (left_name, left), (right_name, right) in zip(snapshots, snapshots[1:]):
            a, b = left['ground'], right['ground']
            if not ground_known(a) or not ground_known(b) or (a == 1023) == (b == 1023):
                continue
            label = 'landing' if b != 1023 else 'departure'
            if label == 'departure' and left_name == 'pre' and right_name == 'post' and (number(row.get('up')) or 0) > 0:
                label = 'departure_with_jump_input'
            events.append(dict(row=index, elapsed_ms=number(row.get('elapsed_ms')),
                               boundary=left_name + '->' + right_name, kind=label,
                               command_time_before=left['command_time'], command_time_after=right['command_time'],
                               position=right['position']))
        previous = snapshots[-1][1]
    return dict(events=events, counts=dict(Counter(event['kind'] for event in events)),
                interpretation='Observed ground-state changes only. Departure with jump input is not proof of cause. A launch and landing inside one command can be invisible; no airtime is fabricated.')


def subject_of(metadata):
    """The recording's subject, taken from the metadata it declares. A recording
    that says nothing is a human one: the recorder wrote no subject field before
    the bot subject existed, and every such file is a demonstration."""
    return metadata.get('subject') or 'human'


def analyze(rows, metadata, summary, load_errors=()):
    errors = list(load_errors)
    subject = subject_of(metadata)
    if metadata.get('format') != 'HUMAN_ROUTE1' or summary.get('format') != 'HUMAN_ROUTE1':
        errors.append('Metadata or summary format is not HUMAN_ROUTE1.')
    if not rows:
        errors.append('No recorded command rows.')
    if number(summary.get('rows')) != len(rows):
        errors.append('Summary row count does not match CSV.')
    if not truth(summary.get('started')) or summary.get('reason') != 'enemy_flag_pickup':
        errors.append('Recording did not finish by the declared enemy-flag pickup.')
    if subject not in ('human', 'bot'):
        errors.append('Metadata declares an unknown subject: %r.' % (subject,))
    if not truth(metadata.get('auto_enemy_pickup')):
        errors.append('Metadata does not bind an automatic enemy-pickup recording.')
    if subject == 'human' and not truth(metadata.get('human')):
        errors.append('Metadata does not bind a human recording.')
    if subject == 'bot' and truth(metadata.get('human')):
        errors.append('Metadata declares a bot subject and a human recording at once.')
    if subject == 'bot' and number(metadata.get('corner_level')) is None:
        errors.append('Bot recording does not declare the corner release level it started from.')
    if not metadata.get('clock'):
        errors.append('Metadata does not declare the recorder clock.')
    if truth(summary.get('teleport_seen')) or summary.get('valid') is False:
        errors.append('Recorder marked teleport or invalid state.')
    if truth(summary.get('start_carrying')):
        errors.append('Recorder reports a flag already carried at first input.')
    required = ['row', 'level', 'elapsed_ms', 'cmd_time', 'forward', 'right', 'up']
    required += [prefix + '_' + key for prefix in ('pre', 'post', 'final')
                 for key in ('x', 'y', 'z', 'vx', 'vy', 'vz', 'ground', 'cmd_time', 'redflag', 'blueflag')]
    malformed = [index for index, row in enumerate(rows) if any(number(row.get(key)) is None for key in required)]
    if malformed:
        errors.append('Missing/nonfinite required telemetry in rows: ' + ', '.join(map(str, malformed[:10])))
    if any(number(row.get('row')) != index for index, row in enumerate(rows)):
        errors.append('CSV row sequence is incomplete or reordered.')
    cadence = cadence_facts(rows, 'level')
    command_cadence = cadence_facts(rows, 'cmd_time')
    if cadence['backwards_timestamps']:
        errors.append('Native level clock moves backwards.')
    if command_cadence['backwards_timestamps']:
        errors.append('Accepted command clock moves backwards.')
    if any(number(row.get('pre_cmd_time')) is not None and number(row.get('final_cmd_time')) is not None and
           number(row.get('final_cmd_time')) < number(row.get('pre_cmd_time')) for row in rows):
        errors.append('A command ends before its pre-Pmove clock.')
    start_level = number(summary.get('start_level'))
    end_level = number(summary.get('end_level'))
    elapsed = number(summary.get('elapsed_ms'))
    if start_level is None or end_level is None or elapsed != end_level - start_level:
        errors.append('Summary elapsed duration disagrees with native level boundaries.')
    for row in rows:
        at, relative = number(row.get('level')), number(row.get('elapsed_ms'))
        if at is not None and start_level is not None and relative != at - start_level:
            errors.append('CSV relative time disagrees with summary start level.')
            break
    first = state(rows[0], 'pre') if rows else dict(position=None, velocity=None, ground=None, command_time=None)
    stationary = first['velocity'] is not None and length(first['velocity']) <= 1 and ground_known(first['ground']) and first['ground'] != 1023
    if not stationary or not truth(summary.get('stationary_start')):
        errors.append('%s root is not a verified stationary grounded start.'
                      % ('Release' if subject == 'bot' else 'First-input'))
    if 'start_ground' in summary and number(summary['start_ground']) != first['ground']:
        errors.append('Recorder start ground disagrees with first pre-Pmove snapshot.')
    if rows:
        if number(rows[0].get('level')) != start_level or number(rows[-1].get('level')) != end_level:
            errors.append('CSV endpoints do not match declared start/pickup levels.')
        enemy_flag = 'final_blueflag' if number(metadata.get('team')) == 1 else 'final_redflag' if number(metadata.get('team')) == 2 else None
        if enemy_flag is None or not (number(rows[-1].get(enemy_flag)) or 0):
            errors.append('Terminal row does not show enemy-flag possession for the recorded team.')
        if any((number(rows[0].get('pre_' + flag)) or 0) != 0 for flag in ('redflag', 'blueflag')):
            errors.append('First-input state already carries a flag.')
    timing = completion_time(dict(valid=not errors, complete=summary.get('complete'),
                                  first_input_time=start_level, pickup_time=end_level,
                                  timing_clock='level.time', reason=summary.get('reason')))
    timing['invalid_reasons'] = errors + [error for error in timing['invalid_reasons'] if error != 'Recorder did not declare this run valid.']
    points = [first['position']] if rows else []
    route_points = []
    if rows:
        route_points.append(dict(elapsed_ms=0, position=first['position'], source='first_pre', command_time=first['command_time']))
    speeds = []
    continuity = []
    previous_final = None
    for index, row in enumerate(rows):
        pre = state(row, 'pre')
        if previous_final is not None and pre['position'] is not None:
            mismatch = length([pre['position'][i] - previous_final[i] for i in range(3)])
            if mismatch > .01:
                continuity.append(dict(row=index, unobserved_displacement_units=mismatch))
                points.append(None)
        points.append(pre['position'])
        for prefix in ('pre', 'post', 'final'):
            snapshot = state(row, prefix)
            if snapshot['velocity'] is not None:
                speeds.append(dict(row=index, snapshot=prefix, elapsed_ms=number(row.get('elapsed_ms')),
                                   horizontal=length(snapshot['velocity'][:2]), spatial=length(snapshot['velocity'])))
            if prefix != 'pre':
                points.append(snapshot['position'])
        final = state(row, 'final')
        previous_final = final['position']
        route_points.append(dict(elapsed_ms=number(row.get('elapsed_ms')), position=final['position'],
                                 source='final', row=index, command_time=final['command_time']))
    resources = resource_facts(rows, [prefix + '_force' for prefix in ('pre', 'post', 'final')],
                               [prefix + '_active_powers' for prefix in ('pre', 'post', 'final')])
    resources['recorder_active_powers_seen'] = summary.get('active_powers_seen')
    resources['recorder_force_decrease_seen'] = summary.get('force_decrease_seen')
    inputs = input_facts(rows, 'level', 'up')
    inputs['recorder_consecutive_up_seen'] = summary.get('consecutive_up_seen')
    processing = Counter()
    nominal_steps = Counter()
    for row in rows:
        before, after = number(row.get('pre_cmd_time')), number(row.get('post_cmd_time'))
        if before is not None and after is not None:
            processing[after - before] += 1
        step = number(row.get('pmove_msec'))
        if step is not None:
            nominal_steps[step] += 1
    result = dict(format='HUMAN_ROUTE_REPORT1', subject=subject,
                  subject_note=('Armed by the corner fixture on its release frame. This is a '
                                'recording of the bot under the declared movement settings, '
                                'not a human demonstration.' if subject == 'bot' else
                                'Armed by the player. The clock starts at his first effective '
                                'movement command.'),
                  timing=timing, start_state=dict(first, stationary=stationary,
                  armed_stationary=metadata.get('armed_stationary'), arm_origin=metadata.get('arm_origin'),
                  stand_identity='Not inferred from position; compare against the map fixture or recorded intended start.'),
                  observed_speed=dict(peak_horizontal=max(speeds, key=lambda s: s['horizontal']) if speeds else None,
                                      peak_spatial=max(speeds, key=lambda s: s['spatial']) if speeds else None),
                  observed_path=observed_path(points), resources=resources, jump_input=inputs,
                  transitions=transition_events(rows), cadence=cadence,
                  command_cadence=command_cadence, continuity_gaps=continuity,
                  command_processing=dict(observed_pmove_command_delta_ms_counts={str(k): v for k, v in sorted(processing.items())},
                                          recorded_pmove_msec_counts={str(k): v for k, v in sorted(nominal_steps.items())},
                                          interpretation='Recorded pre/post command clock spans and Pmove settings, not a reconstructed list of internal substeps. Larger spans are not automatically missing samples.'),
                  metadata=metadata, recorder_summary=summary,
                  limits=['No optimality claim, stand-coordinate inference, Force-spend reconstruction, or substep airtime inference.',
                          'A valid completion time does not certify matched physics settings, taps-only play, no Force use, or a particular stand start.',
                          'Route polyline uses native elapsed time without interpolation or time warping. Compare map checksum and settings before overlaying bot captures.'])
    route = dict(format='HUMAN_ROUTE_POLYLINE1', map=metadata.get('map'), map_checksum=metadata.get('map_checksum'),
                 clock=('level.time relative to the corner release frame' if subject == 'bot'
                        else 'level.time relative to first recorded effective movement/jump command'),
                 subject=subject, points=route_points,
                 continuity_gaps=continuity, qualified_time_ms=timing['time_ms'],
                 interpretation='Command-end positions plus initial root; duplicate times can occur. No interpolation. Bot play CSVs commonly sample at 4 Hz and cannot reveal every jump or exact path length.')
    return result, route


def readable(report):
    timing = report['timing']
    subject = report.get('subject', 'human')
    lines = ['Bot route report' if subject == 'bot' else 'Human route report', '',
             report.get('subject_note', '')]
    root = 'Release' if subject == 'bot' else 'First input'
    lines.append(f"{root} -> enemy pickup: {timing['time_ms'] / 1000:.3f} s (native level clock)." if timing['qualified'] else 'No qualified completion time.')
    lines.extend('- ' + error for error in timing['invalid_reasons'])
    start = report['start_state']
    lines += ['', f"{root} position: {start['position']}; velocity: {start['velocity']}; ground entity: {start['ground']}.",
              f"Stationary grounded root: {start['stationary']}. Stand identity is not inferred."]
    if subject == 'bot':
        lines.append('Movement settings the recording was produced under: '
                     f"{report['metadata'].get('settings')}.")
    peak = report['observed_speed']['peak_horizontal']
    if peak:
        lines.append(f"Observed peak horizontal speed: {peak['horizontal']:.1f} units/s (row {peak['row']}, {peak['snapshot']}).")
    path = report['observed_path']
    lines.append(f"Observed polyline: {path['horizontal_units']:.1f} horizontal units; {path['spatial_units']:.1f} spatial units. This does not reconstruct substeps.")
    facts = report['resources']
    lines += ['', f"Observed Force pool: {facts['observed_force_min']} .. {facts['observed_force_max']}; active masks: {facts['observed_nonzero_active_masks']}.",
              f"Recorder saw pool decrease: {facts['recorder_force_decrease_seen']}. Regeneration can hide spending; no zero-spend claim."]
    inputs = report['jump_input']
    lines += [f"Positive up input: {inputs['positive_up_rows']} commands, {inputs['positive_up_runs']} runs; {inputs['consecutive_positive_up_runs']} runs span consecutive commands.",
              'Repeated up input is reported as input, not proof of held Force Jump.',
              f"Observed ground transitions: {report['transitions']['counts']}. No inferred substep airtime.",
              f"Unobserved inter-command position changes: {len(report['continuity_gaps'])}.", '', *report['limits']]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--meta', type=Path, help='Defaults to the CSV stem plus .meta.json')
    parser.add_argument('--summary', type=Path, help='Defaults to the CSV stem plus .summary.json')
    parser.add_argument('--out', type=Path, help='New directory only; defaults to a unique directory beside the CSV')
    args = parser.parse_args()
    csv_path = args.csv.resolve()
    meta_path = (args.meta or csv_path.with_suffix('.meta.json')).resolve()
    summary_path = (args.summary or csv_path.with_suffix('.summary.json')).resolve()
    errors = []
    documents = []
    bindings = {'csv': source_binding(csv_path)}
    for kind, path in (('metadata', meta_path), ('summary', summary_path)):
        try:
            bindings[kind] = source_binding(path)
            document = json.loads(path.read_text(encoding='utf-8-sig'))
            if not isinstance(document, dict):
                raise ValueError('expected one JSON object')
        except (OSError, ValueError) as error:
            errors.append(f'{kind} unavailable or incomplete: {error}')
            document = {}
        documents.append(document)
    with csv_path.open(encoding='utf-8-sig', newline='') as source:
        rows = list(csv.DictReader(source))
    result, route = analyze(rows, *documents, load_errors=errors)
    result['sources'] = bindings
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = (args.out or csv_path.parent / f'{csv_path.stem}-report-{stamp}-{uuid.uuid4().hex[:8]}').resolve()
    output.mkdir(parents=True, exist_ok=False)
    dump_json(output / 'report.json', result)
    dump_json(output / 'route.json', route)
    with (output / 'report.txt').open('x', encoding='utf-8') as destination:
        destination.write(readable(result))
    print(readable(result), end='')
    print(f'Artifacts: {output}')


if __name__ == '__main__':
    main()
