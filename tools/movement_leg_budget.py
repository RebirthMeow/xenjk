"""Account for a pre-pickup analysis window's speed integral and grounded frames.

The fixture level is excluded; the window ends at the last noncarrying play
sample. This window is shorter than the fixture's official completion time.
Every momentum sample, including both boundaries, must have 25 ms coverage.

Grounded frames before a recorded sequence commit are precommit_grounded;
runs without a commit are uncommitted_grounded. Neither label establishes a
failed search. Candidate outcomes are reported separately when recorded.
A single grounded sample does not establish which command caused takeoff.

The hypothetical speed floor uses the first grounded sample's speed and never
reduces observed speed. It is an accounting scenario, not a bound on savings
or a physically feasible trajectory. Distance is a sampled horizontal speed
integral, not route length. The target speed applies only to this analysis
window; it does not establish the speed needed for full fixture completion.

LEG_BUDGET2 retains leg_ms and counterfactual_leg_ms as compatibility aliases
for analysis_window_ms and hypothetical_analysis_window_ms respectively.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

from movement_momentum_audit import ADMITTED, digest, fields

STEP_MS = 25
STEP_S = STEP_MS / 1000.0


def read_leg(case):
    """The leg's momentum samples, move events and candidate lines."""
    result_path = case / 'result.json'
    result = json.loads(result_path.read_text())
    start = int(result['corner_level'])
    play_paths = list((case / 'base/bot2-play').glob('*.csv'))
    if len(play_paths) != 1:
        raise ValueError('Expected exactly one play CSV: ' + str(case))
    with play_paths[0].open(newline='') as stream:
        rows = [x for x in csv.DictReader(stream) if int(x['level_time']) > start]
    pickup = next(i for i, x in enumerate(rows) if int(x['carrying']))
    if pickup == 0:
        raise ValueError('No observed outbound interval before pickup')
    end = int(rows[pickup - 1]['level_time'])
    momentum, events, candidates = {}, [], defaultdict(list)
    for line in (case / 'server.log').read_text().splitlines():
        if '[MOMENTUM]' in line:
            row = fields(line)
            momentum[int(row['level'])] = row
        elif '[MOVE]' in line:
            events.append(fields(line))
        elif '[SPIKE]' in line:
            row = fields(line)
            candidates[int(row['level'])].append(row)
    levels = sorted(t for t in momentum if start < t <= end)
    if not levels:
        raise ValueError('No in-leg momentum samples: ' + str(case))
    expected = list(range(start + STEP_MS, end + 1, STEP_MS))
    if end <= start or (end - start) % STEP_MS or levels != expected:
        raise ValueError('Incomplete %d ms momentum coverage for (%d, %d]: %s'
                         % (STEP_MS, start, end, case))
    bindings = {str(p.resolve()): digest(p) for p in
                (result_path, case / 'server.log', play_paths[0])}
    return dict(start=start, end=end, levels=levels, momentum=momentum,
                events=events, candidates=candidates, bindings=bindings,
                completion_ms=(result.get('evaluation', {}).get('time_ms')
                               if result.get('evaluation', {}).get('status') == 'pass' else None))


def grounded_runs(grounded):
    runs, length = [], 0
    for index, value in enumerate(grounded):
        if value:
            length += 1
        elif length:
            runs.append((index - length, length))
            length = 0
    if length:
        runs.append((len(grounded) - length, length))
    return runs


def attribute(leg):
    """Every grounded run's frames, split at the commit that ends the run."""
    levels, momentum = leg['levels'], leg['momentum']
    grounded = [int(momentum[t]['ground']) != 0 for t in levels]
    speed = [float(momentum[t]['speed']) for t in levels]
    by_level = defaultdict(list)
    for event in leg['events']:
        by_level[int(event['level'])].append(event)
    owners, refusals, runups = [], Counter(), []
    counterfactual = list(speed)
    for first, length in grounded_runs(grounded):
        last = min(first + length, len(speed) - 1)
        entry = speed[first]
        if first == 0:
            owners.append(dict(owner='leg_start_stand', frames=length,
                               level=levels[first], entry_speed=entry, lost=0.0))
            continue
        if length == 1:
            owners.append(dict(owner='single_grounded_frame', frames=1,
                               level=levels[first], entry_speed=entry,
                               lost=max(0.0, entry - speed[last])))
            continue
        commit, plan = None, None
        for index in range(first, min(first + length + 1, len(levels))):
            for event in by_level.get(levels[index], []):
                if event.get('reason') == 'sequence':
                    commit, plan = index, event
                    break
            if commit is not None:
                break
        waiting = (commit - first) if commit is not None else length
        for index in range(first, first + length):
            if index < len(counterfactual):
                counterfactual[index] = max(counterfactual[index], entry)
        if waiting:
            held = min(first + waiting, len(speed) - 1)
            owners.append(dict(owner='precommit_grounded' if commit is not None else 'uncommitted_grounded',
                               frames=waiting,
                               level=levels[first], entry_speed=entry,
                               lost=max(0.0, entry - speed[held])))
            for index in range(first, first + waiting):
                for row in leg['candidates'].get(levels[index], []):
                    outcome = row.get('outcome', 'unknown')
                    unsupported = int(row.get('unsupported', '0'))
                    if outcome in ADMITTED:
                        refusals['admitted'] += 1
                    elif unsupported & 256:
                        refusals[outcome + '/trigger_contact_unknown'] += 1
                    elif unsupported:
                        refusals[outcome + '/other_unsupported'] += 1
                    else:
                        refusals[outcome] += 1
        rest = length - waiting
        if rest:
            held = min(first + waiting, len(speed) - 1)
            runup = plan is not None and plan.get('r0') not in (None, '0')
            owners.append(dict(
                owner='runup_before_launch' if runup else 'committed_grounded_without_runup',
                frames=rest, level=levels[first + waiting], entry_speed=speed[held],
                lost=max(0.0, speed[held] - speed[last]),
                runup=plan.get('r0') if plan else None))
            if runup:
                runups.append(rest)
    return owners, refusals, runups, speed, grounded, counterfactual


def budget(case):
    leg = read_leg(case)
    owners, refusals, runups, speed, grounded, counterfactual = attribute(leg)
    frames = len(speed)
    distance = sum(v * STEP_S for v in speed)
    mean = sum(speed) / frames
    counter_mean = sum(counterfactual) / frames
    totals = defaultdict(lambda: dict(runs=0, frames=0, lost=0.0))
    for row in owners:
        entry = totals[row['owner']]
        entry['runs'] += 1
        entry['frames'] += row['frames']
        entry['lost'] += row['lost']
    return dict(
        case=str(case.resolve()), bindings=leg['bindings'],
        start_exclusive=leg['start'], end_inclusive=leg['end'],
        frames=frames, analysis_window_ms=leg['end'] - leg['start'],
        leg_ms=frames * STEP_MS, completion_ms=leg['completion_ms'],
        grounded_frames=sum(grounded),
        airborne_fraction=1 - sum(grounded) / frames,
        distance_units=distance, mean_speed=mean, max_speed=max(speed),
        counterfactual_mean_speed=counter_mean,
        hypothetical_analysis_window_ms=(1000 * distance / counter_mean
                                         if counter_mean else frames * STEP_MS),
        counterfactual_leg_ms=(1000 * distance / counter_mean
                               if counter_mean else frames * STEP_MS),
        owners={k: dict(v) for k, v in sorted(totals.items())},
        waiting_candidate_outcomes=dict(refusals.most_common()),
        runup_frame_histogram=dict(sorted(Counter(runups).items())),
        touchdowns=owners)


def combine(reports, target_ms):
    if not reports or target_ms <= 0:
        raise ValueError('At least one report and a positive target_ms are required')
    frames = sum(r['frames'] for r in reports)
    grounded = sum(r['grounded_frames'] for r in reports)
    distances = [r['distance_units'] for r in reports]
    mean_distance = sum(distances) / len(distances)
    totals = defaultdict(lambda: dict(runs=0, frames=0, lost=0.0))
    refusals = Counter()
    for report in reports:
        for owner, value in report['owners'].items():
            entry = totals[owner]
            for key in ('runs', 'frames', 'lost'):
                entry[key] += value[key]
        refusals.update(report['waiting_candidate_outcomes'])
    return dict(
        format='LEG_BUDGET2',
        semantics=dict(duration='analysis_window_ms excludes the unobserved pickup tail; '
                       'completion_ms is the official passing evaluation time, or null',
                       leg_ms='compatibility alias for analysis_window_ms',
                       counterfactual_leg_ms='compatibility alias for hypothetical_analysis_window_ms',
                       hypothetical='first grounded speed floor, never below observed speed; '
                       'not a bound on savings or a feasibility claim',
                       lost='nonnegative observed endpoint speed drop, not attributed friction',
                       touchdowns='grounded-run segments, including initial stand and commit splits',
                       target='target_ms and mean_speed_required refer only to the analysis window',
                       waiting_candidate_outcomes='recorded candidate outcomes on precommit or '
                       'uncommitted grounded frames; no inferred failed searches'),
        legs=reports,
        pooled=dict(
            frames=frames, grounded_frames=grounded,
            airborne_fraction=1 - grounded / frames,
            distance_units_mean=mean_distance,
            distance_units_spread=((max(distances) - min(distances)) / mean_distance
                                   if mean_distance else 0.0),
            mean_speed_best=max(r['mean_speed'] for r in reports),
            mean_speed_worst=min(r['mean_speed'] for r in reports),
            analysis_window_ms_best=min(r['analysis_window_ms'] for r in reports),
            analysis_window_ms_worst=max(r['analysis_window_ms'] for r in reports),
            leg_ms_best=min(r['leg_ms'] for r in reports),
            leg_ms_worst=max(r['leg_ms'] for r in reports),
            target_ms=target_ms,
            mean_speed_required=mean_distance / (target_ms / 1000.0),
            hypothetical_analysis_window_ms_best=min(r['hypothetical_analysis_window_ms'] for r in reports),
            hypothetical_analysis_window_ms_worst=max(r['hypothetical_analysis_window_ms'] for r in reports),
            counterfactual_leg_ms_best=min(r['counterfactual_leg_ms'] for r in reports),
            counterfactual_leg_ms_worst=max(r['counterfactual_leg_ms'] for r in reports),
            owners={k: dict(v, share=v['frames'] / grounded if grounded else 0.0)
                    for k, v in sorted(totals.items())},
            waiting_candidate_outcomes=dict(refusals.most_common())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='+', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--target-ms', type=int, default=15000,
                        help='hypothetical analysis-window duration, not full completion time')
    args = parser.parse_args()
    report = combine([budget(case) for case in args.cases], args.target_ms)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(report['pooled'], indent=2))


if __name__ == '__main__':
    main()
