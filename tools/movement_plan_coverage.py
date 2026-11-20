"""Ask whether a plan the bot already holds would have applied where it waited.

Read-only analysis of retained worlds. For every grounded run that
`movement_leg_budget.py` classifies as waiting for a committable plan, the
live state at the run's first frame is tested against every strafe edge of a
graph cache under the envelope rule the game uses (`Graph_StrafeAdmits` in
`bot2_graph.c`: cell reach, speed band, heading cone, tap bit). The runs that
did commit are measured the same way as a control.

The state is reconstructed from telemetry; nothing is replayed. Position and
speed are the frame's own `[MOVE]` and `[MOMENTUM]` records. The heading is
not retained at frame resolution: it is a finite difference to the next
position record when the next frame has one, and otherwise the chord from
the executed hop's launch record to this landing. Where both exist the two
are compared, and `heading_error_degrees` is that comparison. Coverage is
therefore reported twice, once with the reconstructed heading and once with
the cone axis dropped, which is an upper bound and not a claim about any
single frame.

The tap bit is the landing-frame bit, read from the trace: grounded on this
frame with the previous frame airborne. The tap=0 count is reported beside
it as the sensitivity, because a state reached without a tap cannot use a
tap edge.

A covered run means an edge in the cache admits the state. It does not mean
that edge's plan would have been committed, validated, survived the forecast
or been faster. Admission is the envelope test alone. An uncovered run is
likewise not proof that no plan exists, only that none in this cache does.

Near misses are bounded: an edge is considered only when its takeoff lies
within twice the reach envelope of the cell, so a run reported with no edge
near on any axis is a statement about that neighbourhood, not the whole map.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
from statistics import median

from movement_leg_budget import attribute, read_leg
from movement_momentum_audit import digest

GRID = 32                       # GRAPH_GRID
STEPSIZE = 18
REACH = 48.0                    # GRAPH_STATE_REACH
BAND = 48.0                     # GRAPH_STATE_BAND
CONE = 30.0                     # GRAPH_STATE_CONE degrees
HEIGHT = 2.0 * STEPSIZE         # CellReach height
ROOT_RADIUS = 64.0              # Graph_Nearest(ps->origin,64,2*STEPSIZE)
ROOT_TOLERANCE = 2.0 * STEPSIZE
STRAFE = 5                      # GRAPH_EDGE_STRAFE
NEAR = 2.0                      # "within twice the envelope"
BUCKET = REACH * NEAR
AXES = ('reach_xy', 'reach_z', 'speed', 'cone', 'tap')
SELF = '@self'                  # the column of each world's own written cache
WAITING = ('precommit_grounded', 'uncommitted_grounded')
COMMITTED = ('runup_before_launch', 'committed_grounded_without_runup')


def load_cache(path):
    """Cells, strafe edges and a takeoff index from a written graph cache."""
    cells, columns, edges, index, count = [], defaultdict(list), [], defaultdict(list), 0
    for line in path.read_text().splitlines():
        if line.startswith('c '):
            f = line.split()
            columns[(int(f[1]), int(f[2]))].append(len(cells))
            cells.append(dict(ix=int(f[1]), iy=int(f[2]), z=float(f[3]),
                              link=tuple(int(x) for x in f[4:8])))
        elif line.startswith('e '):
            f = line.split()
            edge, count = count, count + 1
            if int(f[1]) != STRAFE:
                continue
            index[(math.floor(float(f[4]) / BUCKET), math.floor(float(f[5]) / BUCKET))].append(len(edges))
            edges.append(dict(edge=edge, frm=int(f[2]), to=int(f[3]),
                              takeoff=(float(f[4]), float(f[5]), float(f[6])),
                              tap=int(f[14]), entry_speed=float(f[15]),
                              entry_dir=(float(f[16]), float(f[17]))))
    return dict(path=str(path.resolve()), digest=digest(path), cells=cells,
                columns=dict(columns), edges=edges, index=dict(index),
                by_record={e['edge']: e for e in edges}, edge_records=count)


def own_cache(case):
    """The cache the world itself wrote, one file or it is an error."""
    found = sorted((case / 'base/bot2-graph').glob('*.txt'))
    if len(found) != 1:
        raise ValueError('Expected exactly one written graph cache: ' + str(case))
    return found[0]


def nearest_cell(cache, point):
    """Graph_Nearest(point, ROOT_RADIUS, ROOT_TOLERANCE), the live root cell."""
    cx = math.floor(point[0] / GRID + .5)
    cy = math.floor(point[1] / GRID + .5)
    span = math.ceil(ROOT_RADIUS / GRID)
    best, best_d = -1, ROOT_RADIUS * ROOT_RADIUS + 1
    for ix in range(cx - span, cx + span + 1):
        for iy in range(cy - span, cy + span + 1):
            for i in cache['columns'].get((ix, iy), ()):
                cell = cache['cells'][i]
                if abs(cell['z'] - point[2]) > ROOT_TOLERANCE:
                    continue
                d = (ix * GRID - point[0]) ** 2 + (iy * GRID - point[1]) ** 2
                if d <= ROOT_RADIUS * ROOT_RADIUS and d < best_d:
                    best, best_d = i, d
    return best


def nearby(cache, cell):
    """Strafe edges whose takeoff is within twice the reach of the cell."""
    x, y = cell['ix'] * GRID, cell['iy'] * GRID
    bx, by = math.floor(x / BUCKET), math.floor(y / BUCKET)
    out = []
    for ix in range(bx - 1, bx + 2):
        for iy in range(by - 1, by + 2):
            for i in cache['index'].get((ix, iy), ()):
                edge = cache['edges'][i]
                if (abs(edge['takeoff'][0] - x) <= NEAR * REACH
                        and abs(edge['takeoff'][1] - y) <= NEAR * REACH):
                    out.append(edge)
    return out


def margins(cell, state, edge):
    """Each envelope axis's overrun, zero or below when the axis admits."""
    x, y = cell['ix'] * GRID, cell['iy'] * GRID
    dx, dy = x - edge['takeoff'][0], y - edge['takeoff'][1]
    dot = state['dir'][0] * edge['entry_dir'][0] + state['dir'][1] * edge['entry_dir'][1]
    return dict(reach_xy=math.hypot(dx, dy) - REACH,
                reach_z=abs(cell['z'] - edge['takeoff'][2]) - HEIGHT,
                speed=abs(state['speed'] - edge['entry_speed']) - BAND,
                cone=math.degrees(math.acos(max(-1.0, min(1.0, dot)))) - CONE,
                tap=1.0 if (edge['tap'] and not state['tap']) else -1.0)


def classify(cache, cell_index, state, axes=AXES):
    """The run's coverage, and when uncovered the axis that excluded it."""
    if cell_index < 0:
        return dict(cell=-1, verdict='off_graph', admitting=0, attached=0,
                    nearby=0, axis=None, margin=None, failed_axes=None)
    cell = cache['cells'][cell_index]
    candidates = nearby(cache, cell)
    admitting, attached, best = [], 0, None
    for edge in candidates:
        over = margins(cell, state, edge)
        failed = [a for a in axes if over[a] > 0]
        if not failed:
            admitting.append(edge)
            attached += edge['frm'] == cell_index
            continue
        worst = max(over[a] / (NEAR * limit(a)) for a in axes)
        key = (len(failed), min(over[a] for a in failed) if len(failed) == 1 else 0.0, worst)
        if best is None or key < best[0]:
            best = (key, failed, over)
    if admitting:
        return dict(cell=cell_index, verdict='covered', admitting=len(admitting),
                    attached=attached, nearby=len(candidates), axis=None,
                    margin=None, failed_axes=None)
    if best is None:
        return dict(cell=cell_index, verdict='no_edge_near', admitting=0, attached=0,
                    nearby=0, axis=None, margin=None, failed_axes=None)
    _, failed, over = best
    # Twice the envelope on every continuous axis, and the tap bit satisfied:
    # a tap edge a tapless state cannot use is not a near miss.
    within = over['tap'] <= 0 and all(over[a] <= limit(a) for a in axes if a != 'tap')
    verdict = ('one_axis' if len(failed) == 1
               else 'near_several_axes' if within else 'no_edge_near')
    return dict(cell=cell_index, verdict=verdict, admitting=0, attached=0,
                nearby=len(candidates), axis=failed[0] if len(failed) == 1 else None,
                margin=over[failed[0]] if len(failed) == 1 else None,
                failed_axes=sorted(failed))


def limit(axis):
    return dict(reach_xy=REACH, reach_z=HEIGHT, speed=BAND, cone=CONE, tap=1.0)[axis]


def positions(leg):
    """The first position-bearing [MOVE] record at each level."""
    out = {}
    for event in leg['events']:
        if 'x' in event:
            out.setdefault(int(event['level']), event)
    return out


def point(event):
    return (float(event['x']), float(event['y']), float(event['z']))


def state_at(leg, place, launches, level):
    """The reconstructed live state at one level, or a reason it is missing."""
    event, momentum = place.get(level), leg['momentum'].get(level)
    if event is None or momentum is None:
        return None, 'no_position_record', None
    origin = point(event)
    after = place.get(level + 25)
    estimate = None
    if after is not None:
        later = point(after)
        dx, dy = later[0] - origin[0], later[1] - origin[1]
        if math.hypot(dx, dy) > 1e-3:
            estimate = (dx, dy)
    chord = None
    before = [t for t in launches if t < level]
    if before:
        start = point(place[before[-1]])
        dx, dy = origin[0] - start[0], origin[1] - start[1]
        if math.hypot(dx, dy) > 1e-3:
            chord = (dx, dy)
    heading = estimate if estimate is not None else chord
    if heading is None:
        return None, 'no_heading_evidence', None
    error = None
    if estimate is not None and chord is not None:
        a = math.atan2(estimate[1], estimate[0])
        b = math.atan2(chord[1], chord[0])
        error = abs(math.degrees(math.atan2(math.sin(a - b), math.cos(a - b))))
    previous = leg['momentum'].get(level - 25)
    length = math.hypot(*heading)
    return dict(origin=origin, speed=float(momentum['speed']),
                dir=(heading[0] / length, heading[1] / length),
                tap=int(int(momentum['ground']) != 0 and previous is not None
                        and int(previous['ground']) == 0),
                heading_source='finite_difference' if estimate is not None
                else 'launch_chord'), None, error


def trace(case):
    """Reconstructed states at each waiting run's first frame and each commit."""
    leg = read_leg(case)
    owners, _, _, _, _, _ = attribute(leg)
    place = positions(leg)
    launches = sorted(int(e['level']) for e in leg['events']
                      if e.get('reason') == 'launch' and 'x' in e)
    out = []
    for row in owners:
        role = ('waiting' if row['owner'] in WAITING
                else 'committed' if row['owner'] in COMMITTED else None)
        if role is None:
            continue
        state, reason, error = state_at(leg, place, launches, row['level'])
        out.append(dict(case=str(case.resolve()), role=role, owner=row['owner'],
                        level=row['level'], frames=row['frames'], state=state,
                        heading_error_degrees=error, reason=reason))
    logged = []
    for event in leg['events']:
        if event.get('event') != 'edge' or 'envelope' not in event:
            continue
        level = int(event['level'])
        state, reason, _ = state_at(leg, place, launches, level)
        if state is None:
            logged.append(dict(level=level, reason=reason))
            continue
        state = dict(state, speed=float(event['live_speed']), tap=int(event['tap']))
        logged.append(dict(case=str(case.resolve()), level=level, state=state,
                           edge=int(event['index']), envelope=int(event['envelope'])))
    return out, logged, leg['bindings']


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return dict(count=len(values), minimum=values[0], median=median(values),
                maximum=values[-1])


def cover(resolve, rows, axes):
    """Classify every run against the cache that run's world is read with."""
    verdicts, axis_margins, admitting, off = Counter(), defaultdict(list), [], 0
    detail = []
    for row in rows:
        if row['state'] is None:
            verdicts['no_state'] += 1
            continue
        cache = resolve(row)
        cell = nearest_cell(cache, row['state']['origin'])
        if cell < 0:
            off += 1
        result = classify(cache, cell, row['state'], axes)
        verdicts[result['verdict']] += 1
        if result['verdict'] == 'covered':
            admitting.append(result['admitting'])
        elif result['verdict'] == 'one_axis':
            axis_margins[result['axis']].append(result['margin'])
        detail.append(dict(case=row['case'], level=row['level'], owner=row['owner'],
                           speed=row['state']['speed'], tap=row['state']['tap'], **result))
    return dict(runs=len(rows), verdicts=dict(verdicts.most_common()),
                off_graph=off,
                covered_fraction=(verdicts['covered'] / len(rows)) if rows else 0.0,
                admitting_edges=distribution(admitting),
                one_axis_margins={k: distribution(v) for k, v in sorted(axis_margins.items())},
                runs_detail=detail)


def cross_check(resolve, logged):
    """Recompute the envelope the game logged for one edge at one frame.

    The cell is the one this tool would pick from the position record; the
    game used the cell its own path held, which is not logged, so a
    disagreement can be the cell or the heading as well as the rule.
    """
    tally, axes = Counter(), Counter()
    for row in logged:
        if row.get('state') is None:
            tally['no_state'] += 1
            continue
        cache = resolve(row)
        edge = cache['by_record'].get(row['edge'])
        if edge is None:
            tally['edge_not_in_cache'] += 1
            continue
        cell = nearest_cell(cache, row['state']['origin'])
        if cell < 0:
            tally['off_graph'] += 1
            continue
        over = margins(cache['cells'][cell], row['state'], edge)
        failed = [a for a in AXES if over[a] > 0]
        mine = 0 if failed else 1
        tally['agree' if mine == row['envelope'] else 'disagree'] += 1
        if mine != row['envelope']:
            axes['+'.join(sorted(failed)) or 'admitted_by_tool'] += 1
    return dict(compared=tally['agree'] + tally['disagree'],
                tallies=dict(tally.most_common()),
                disagreement_axes=dict(axes.most_common()))


def report(cases, caches, tap_sensitivity=True):
    rows, logged, bindings = [], [], {}
    for case in cases:
        case_rows, case_logged, case_bindings = trace(case)
        rows += case_rows
        logged += case_logged
        bindings.update(case_bindings)
    waiting = [r for r in rows if r['role'] == 'waiting']
    committed = [r for r in rows if r['role'] == 'committed']
    errors = [r['heading_error_degrees'] for r in rows
              if r.get('heading_error_degrees') is not None]
    columns = {}
    for name, cache in caches.items():
        loaded = {}

        def resolve(row, cache=cache, loaded=loaded):
            path = own_cache(Path(row['case'])) if str(cache) == SELF else Path(cache)
            key = str(path.resolve())
            if key not in loaded:
                loaded[key] = load_cache(path)
                bindings[key] = loaded[key]['digest']
            return loaded[key]

        column = dict(cache=str(cache),
                      waiting=cover(resolve, waiting, AXES),
                      committed=cover(resolve, committed, AXES),
                      waiting_without_cone=cover(
                          resolve, waiting, tuple(a for a in AXES if a != 'cone')),
                      logged_envelope_cross_check=cross_check(resolve, logged),
                      strafe_edges={k: len(v['edges']) for k, v in sorted(loaded.items())})
        if tap_sensitivity:
            without = [dict(r, state=dict(r['state'], tap=0) if r['state'] else None)
                       for r in waiting]
            column['waiting_without_tap'] = cover(resolve, without, AXES)
        columns[name] = column
    return dict(
        format='PLAN_COVERAGE1',
        semantics=dict(
            covered='at least one strafe edge in the cache whose envelope admits the '
                    'reconstructed state; not a commit, a validation or a speed claim',
            heading='estimated by the chord from the executed hop launch record to this '
                    'landing; heading_error_degrees measures that estimate against a '
                    'finite difference wherever the next frame has a position record',
            without_cone='the same test with the heading axis dropped: an upper bound on '
                         'coverage, not a per-frame result',
            without_tap='the same test with the landing tap bit cleared, the sensitivity '
                        'of coverage to tap-only edges',
            near='only edges whose takeoff lies within twice the reach envelope of the '
                 'cell are considered, so no_edge_near describes that neighbourhood',
            margin='degrees for cone, units for reach, units per second for speed, each '
                   'measured beyond the envelope'),
        cases=[str(c.resolve()) for c in cases],
        bindings=bindings,
        waiting_runs=len(waiting), committed_runs=len(committed),
        heading_sources=dict(Counter(r['state']['heading_source'] for r in rows
                                     if r['state']).most_common()),
        states_missing=dict(Counter(r['reason'] for r in rows if not r['state']).most_common()),
        tap_states=dict(Counter(r['state']['tap'] for r in rows if r['state']).most_common()),
        heading_error_degrees=distribution(errors),
        columns=columns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='+', type=Path)
    parser.add_argument('--cache', action='append', default=[], metavar='NAME=PATH',
                        help='a graph cache column, for example seeded=path/to.txt; '
                             'the path %s names the cache each world wrote' % SELF)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    caches = {}
    for item in args.cache:
        name, _, path = item.partition('=')
        if not path:
            raise SystemExit('--cache takes NAME=PATH')
        caches[name] = path if path == SELF else Path(path)
    if not caches:
        raise SystemExit('at least one --cache column is required')
    result = report(args.cases, caches)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    summary = {k: {n: v[n] for n in ('waiting', 'committed', 'waiting_without_cone',
                                     'waiting_without_tap', 'logged_envelope_cross_check')
                   if n in v} for k, v in result['columns'].items()}
    for column in summary.values():
        for part in column.values():
            part.pop('runs_detail', None)
    print(json.dumps(dict(waiting_runs=result['waiting_runs'],
                          committed_runs=result['committed_runs'],
                          heading_error_degrees=result['heading_error_degrees'],
                          columns=summary), indent=2))


if __name__ == '__main__':
    main()
