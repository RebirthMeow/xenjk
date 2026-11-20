"""Movement replay analyzer: where a case's fixture-to-pickup time actually went.

Reads one retained benchmark case directory (``server.log``, ``result.json`` and
the world's play CSV) and produces a mutually exclusive segment partition of the
interval from the corner fixture to the objective pickup, a per-transition table
of what the planner priced against what the bot executed, and a readable report.

The partition uses native event times and the explicitly bound launch correction;
no 4 Hz play row is interpolated. The play CSV binds the pickup frame, which it
records on the possession transition itself rather than on the 4 Hz cadence.

Legacy launch events confirm the preceding command's takeoff. Analysis requires
an explicit, source-verified confirmation delay for those logs; it never assumes
a cadence. Original confirmation timestamps and speed observations are retained.

Analyzer version 5. Reads existing telemetry; ``[REPLAY]`` lines (bot_moveReplay)
are parsed when present and are optional. Never starts a world; never edits one.

Version 3 adds the second hop's heading delta (the retained corpus's real
rescues turn on it, not on hop 0), the ``no_onward`` outcome, and the arm's own
walk reference and onward-stage diagnostics. All of the new fields are optional:
a v1/v2 log still analyses, with those columns reported as null.
Version 4 distinguishes proposals from observe-only candidates and validates
actual per-arm reference bounds. Legacy mode inference is labeled; unknown
modes and old offer reference labels cannot establish complete isolation.
Version 5 excludes successor-aim clones from the historical heading and run-up
pairs. Their raw rows and overall candidate/commit audit remain intact.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_bench_lib import parse_move_events  # noqa: E402

ANALYZER_VERSION = 5
CORNER_START = re.compile(r'\[CORNER\] START .*?level=(\d+)')
REPLAY_LINE = re.compile(r'\[REPLAY\] (?P<body>.*)')

# Legacy residual labels; these intervals are not verified ground motion.
# Only instrumented hop launch/land events identify the flight label.
GROUND_LABELS = ('prefix_walk', 'search_walk', 'approach', 'interhop', 'tail_walk')


def read_text(path):
    return path.read_text(encoding='utf-8', errors='replace')


def parse_replay_events(text):
    """Every ``[REPLAY] key=value ...`` line as a dict; values stay strings."""
    events = []
    for match in REPLAY_LINE.finditer(text):
        fields = {}
        for token in match.group('body').split():
            key, _, value = token.partition('=')
            if _:
                fields[key] = value
        events.append(fields)
    return events


def corner_level(text, result):
    """The fixture's declared start level: result.json first, then the log."""
    if result.get('corner_level') is not None:
        return int(result['corner_level'])
    match = CORNER_START.search(text)
    if match:
        return int(match.group(1))
    raise SystemExit('no corner start level in result.json or server.log')


def play_rows(case_dir):
    files = sorted((case_dir / 'base' / 'bot2-play').glob('*.csv'))
    if not files:
        return []
    with files[0].open(encoding='utf-8', errors='replace') as stream:
        return list(csv.DictReader(stream))


def pickup_level(rows, start_level, condition):
    """The level time of the first row past the start that holds the objective.

    The play log writes a row on every possession and capture transition as
    well as at 4 Hz, so this is the transition frame itself, not a sample of
    it. Returns None when the objective was never reached.
    """
    key = 'carrying' if condition == 'pickup' else 'captures'
    for row in rows:
        level = int(row['level_time'])
        if level <= start_level:
            continue
        value = int(row[key])
        if (value == 1) if condition == 'pickup' else (value >= 1):
            return level
    return None


def movement_coverage(rows, start, end):
    """Report sampled owners, never interpolate exact ownership intervals.

    Hop events cover the rewrite's committed jumps, not jump pads, legacy
    flights or every possible airborne state. Residual *_walk labels therefore
    cannot be interpreted as measured ground time, even when sampled owners
    contain only move.
    """
    route_rows = [row for row in rows if start <= int(row['level_time']) <= end]
    owners = sorted({row.get('movement_owner') for row in route_rows if row.get('movement_owner')})
    other = [owner for owner in owners if owner != 'move']
    missing = sum(not row.get('movement_owner') for row in route_rows)
    warnings = []
    if other:
        warnings.append('Non-hop owners sampled: %s. Residual *_walk intervals can include uninstrumented movement, including jump-pad or legacy flight; they are not ground attribution.' % ', '.join(other))
    if not route_rows or missing:
        warnings.append('Ownership coverage is incomplete: no route samples or missing movement_owner values.')
    return dict(sampled_owners=owners, non_hop_owners=other, samples=len(route_rows),
                missing_owner_samples=missing, warnings=warnings,
                interpretation='flight covers instrumented hops only; *_walk, approach and interhop are residual event intervals, not verified ground durations.',
                sampling_note='Owners are observed only at retained play rows within start..end. No exact ownership intervals or durations are inferred between samples.')


def float_or_none(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


# The outcomes that admit a candidate, including the reroute pass's own.
ADMITTED = ('admitted', 'arrives', 'rerouted', 'tap_onward')

def int_or_none(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def wrapped_delta(later, earlier):
    """``later - earlier`` in degrees, wrapped into (-180, 180]."""
    return round(((float(later) - float(earlier) + 180) % 360) - 180, 4)


def command_boundary_events(events, launch_confirmation_delay_ms=None):
    """Copy telemetry and put confirmed launches on their command boundary.

    New telemetry may supply launch_level explicitly. Historical telemetry
    requires a delay verified against the measured source/cadence. Speeds and
    positions still belong to the confirmation frame, not the shifted time.
    """
    if launch_confirmation_delay_ms is not None and launch_confirmation_delay_ms <= 0:
        raise ValueError('launch confirmation delay must be positive')
    result = []
    for event in events:
        row = dict(event)
        if (row.get('event') == 'edge' and row.get('reason') == 'launch'
                and row.get('edge', '').startswith('hop')):
            confirmation = int(row['level'])
            if row.get('launch_level') is not None:
                launch = int(row['launch_level'])
                basis = 'explicit_launch_level'
            elif launch_confirmation_delay_ms is not None:
                launch = confirmation - launch_confirmation_delay_ms
                basis = 'source_verified_confirmation_delay'
            else:
                raise ValueError('legacy launch telemetry requires --launch-confirmation-delay-ms from the source/cadence binding')
            if launch < 0 or launch > confirmation:
                raise ValueError('invalid launch command boundary')
            row.update(level=str(launch), confirmation_level=str(confirmation), timing_basis=basis)
        result.append(row)
    return sorted(result, key=lambda row: int(row['level']))


def segment_timeline(events, start, end):
    """Partition [start, end) into flight and labelled ground intervals.

    Input launches must first pass through command_boundary_events.
    One pass over the ordered native events. A launch closes the ground
    interval that precedes it and opens a flight; the matching land closes the
    flight. Ground before a hop-0 launch is split at the commit that produced
    it: the part before the commit is the search/walk that led there, the part
    after it is the run-up the committed plan spent. Ground between a landing
    and the next launch of the same plan is the inter-hop ground.

    Intervals are half-open and never overlap, so their durations sum exactly
    to ``end - start``. Anything the events do not explain stays in a walk
    label rather than being dropped.
    """
    segments = []
    cursor = start
    flight_start = None
    flight_hop = None
    pending_commit = None
    commit_detail = None
    seen_first_commit = False
    notes = []

    def close(a, b, label, detail=None):
        if b > a:
            segments.append(dict(start=a, end=b, ms=b - a, label=label, detail=detail or ''))

    for e in events:
        level = int(e['level'])
        if level < start or level > end:
            continue
        event, edge, reason = e.get('event'), e.get('edge'), e.get('reason')
        if event == 'plan' and reason == 'sequence':
            if flight_start is None:
                pending_commit = level
                commit_detail = 'plan=%s kind=%s index=%s index2=%s' % (
                    e.get('id'), e.get('kind'), e.get('index'), e.get('index2'))
            continue
        if event in ('plan', 'edge') and reason in ('none', 'stale', 'refused', 'avoid', 'deferred'):
            notes.append((level, '%s:%s' % (edge, reason)))
            continue
        if event == 'edge' and reason == 'launch' and edge and edge.startswith('hop'):
            hop = int(edge[3:])
            if flight_start is not None:      # a launch with no observed landing
                close(flight_start, level, 'flight', 'hop%d unlanded' % flight_hop)
                cursor = level
                flight_start = None
            if hop == 0:
                label = 'prefix_walk' if not seen_first_commit else 'search_walk'
                if pending_commit is not None and cursor <= pending_commit <= level:
                    close(cursor, pending_commit, label, note_text(notes, cursor, pending_commit))
                    close(pending_commit, level, 'approach', commit_detail)
                else:
                    close(cursor, level, label, note_text(notes, cursor, level))
                seen_first_commit = True
            else:
                close(cursor, level, 'interhop', 'hop%d' % hop)
            pending_commit = None
            flight_start = level
            flight_hop = hop
            continue
        if event == 'edge' and reason == 'land' and edge and edge.startswith('hop'):
            if flight_start is not None:
                close(flight_start, level, 'flight', 'hop%d' % flight_hop)
                cursor = level
                flight_start = None
            continue
    if flight_start is not None:
        close(flight_start, end, 'flight', 'hop%d unlanded' % flight_hop)
    else:
        close(cursor, end, 'tail_walk', note_text(notes, cursor, end))
    return segments


def note_text(notes, a, b):
    inside = ['%d:%s' % (level, name) for level, name in notes if a <= level < b]
    return ' '.join(inside[:8])


def transitions(events, start, end):
    """Every planned-edge decision and every executed hop, in native order.

    A row is one of: a path generation, a committed sequence with the planned
    edge indices it carries, a planned edge the search would not fly and why,
    or an executed launch/landing pair with its measured speeds. Predicted
    values come from the commit's own telemetry; actual values from the live
    edge events. Absent values are left empty rather than invented.
    """
    rows = []
    open_hop = {}
    commit = None
    for e in events:
        level = int(e['level'])
        if level < start or level > end:
            continue
        event, edge, reason = e.get('event'), e.get('edge'), e.get('reason')
        if event == 'plan' and reason == 'path':
            rows.append(dict(level=level, kind='path', detail=detail_of(e, (
                'from', 'to', 'cells', 'edges', 'cost_ms', 'state', 'strafe', 'expanded',
                'overflow', 'avoid', 'start_speed', 'start_tap'))))
        elif event == 'plan' and reason == 'sequence':
            commit = e
            rows.append(dict(level=level, kind='commit', plan=e.get('id'), plan_kind=e.get('kind'),
                             edge_index=e.get('index'), edge_index2=e.get('index2'),
                             predicted_launch_speed=e.get('launch_speed'),
                             predicted_land_speed=e.get('land_speed'),
                             predicted_frames=e.get('frames'),
                             detail=detail_of(e, (
                                 'hops', 'landing', 'reissued_up', 'arrives', 'candidates', 'admitted',
                                 'state_candidates', 'state_admitted', 'envelope', 'progress', 'walk',
                                 'walk_stopped', 'r0', 'd0', 'variant'))))
        elif event == 'edge' and edge == 'strafe' and reason in ('stale', 'refused', 'deferred', 'avoid'):
            rows.append(dict(level=level, kind='planned_edge_' + reason, edge_index=e.get('index'),
                             detail=detail_of(e, ('tap', 'envelope', 'landing', 'live_speed',
                                                  'entry_speed', 'candidates', 'why', 'avoided'))))
        elif event == 'plan' and reason == 'none' and e.get('kind') == 'straight':
            rows.append(dict(level=level, kind='search_failed', detail=detail_of(e, (
                'candidates', 'refused', 'fell', 'miss', 'no_first', 'no_second', 'no_onward',
                'horizon', 'no_progress', 'off_graph', 'off_path', 'slower', 'unjudged',
                'rerouted', 'no_route', 'slower_route', 'tap_onward', 'tap_refused',
                'state_candidates'))))
        elif event == 'edge' and reason == 'launch' and edge and edge.startswith('hop'):
            open_hop[edge] = e
        elif event == 'edge' and reason == 'land' and edge and edge.startswith('hop'):
            launch = open_hop.pop(edge, None)
            hop = int(edge[3:])
            predicted = None
            if commit is not None:
                predicted = commit.get('land_speed' if hop == 0 else 'land1_speed')
            rows.append(dict(level=level, kind='flew', hop=hop,
                             plan=commit.get('id') if commit is not None else None,
                             plan_kind=commit.get('kind') if commit is not None else None,
                             edge_index=(commit.get('index') if hop == 0 else commit.get('index2'))
                             if commit is not None else None,
                             actual_launch_level=int(launch['level']) if launch else None,
                             launch_confirmation_level=int(launch['confirmation_level']) if launch and 'confirmation_level' in launch else None,
                             actual_land_level=level,
                             flight_ms=(level - int(launch['level'])) if launch else None,
                             launch_confirmation_speed=float_or_none(launch['speed']) if launch else None,
                             actual_land_speed=float_or_none(e.get('speed')),
                             predicted_land_speed=predicted,
                             actual_land_x=float_or_none(e.get('x')), actual_land_y=float_or_none(e.get('y')),
                             actual_land_z=float_or_none(e.get('z'))))
        elif event == 'miss':
            rows.append(dict(level=level, kind='miss', detail='edge=%s reason=%s' % (edge, reason)))
    return rows


def detail_of(event, keys):
    return ' '.join('%s=%s' % (k, event[k]) for k in keys if k in event)


def successor_aim_clone(event):
    """Nonnegative aim_source identifies an appended successor-aim proposal.

    Ordinary rows explicitly use -1; historical logs have no such field.
    Clones remain candidates in the full replay and isolation audit, but they
    are not an alternate observation of the original A/B or run-up sibling.
    """
    source = int_or_none(event.get('aim_source'))
    return source is not None and source >= 0


def paired_arms(replay, start, end):
    """Pair the control and measured-policy candidates of each search.

    Both arms are constructed from one root and judged by one judge, so a
    pair differs in heading construction alone. A pair is keyed by the
    search, the hop count and the run-up; anything the run did not produce
    in both arms is reported as unpaired rather than being matched up.
    """
    pairs, unpaired = [], []
    searches = {}
    for e in replay:
        if e.get('kind') != 'cand' or e.get('plan_kind') != 'state' or successor_aim_clone(e):
            continue
        level = int(e['level'])
        if level < start or level > end:
            continue
        key = (e.get('search'), e.get('hops'), e.get('runup0'))
        searches.setdefault(key, {})[e.get('variant')] = e
    for key, arms in sorted(searches.items(), key=lambda kv: (int(kv[0][0] or 0), kv[0][1], kv[0][2])):
        a, b = arms.get('a_control'), arms.get('b_measured_policy')
        if a is None or b is None:
            unpaired.append(dict(search=key[0], hops=key[1], runup=key[2],
                                 have=sorted(arms), reason='arm missing in this run'))
            continue
        pairs.append(dict(
            search=key[0], level=int(a['level']), gen=a.get('gen'), hops=key[1], runup=key[2],
            edge=a.get('edge'), edge2=a.get('edge2'),
            a_yaw=float(a['hop0_yaw']), b_yaw=float(b['hop0_yaw']),
            heading_delta=wrapped_delta(b['hop0_yaw'], a['hop0_yaw']),
            # The second hop's heading is a separate quantity and, in the
            # retained corpus, the one the real rescues turn on. A pair whose
            # hop-0 delta is a fraction of a degree can still differ by eight
            # degrees at hop 1, so hop0 alone must not be called "the heading".
            a_hop1_yaw=float_or_none(a.get('hop1_yaw')), b_hop1_yaw=float_or_none(b.get('hop1_yaw')),
            hop1_delta=(wrapped_delta(b['hop1_yaw'], a['hop1_yaw'])
                        if a.get('hop1_yaw') is not None and b.get('hop1_yaw') is not None else None),
            a_outcome=a['outcome'], b_outcome=b['outcome'],
            # Analyzer v3 telemetry; absent from earlier logs, reported null.
            a_walk_ref=a.get('walk_ref'), b_walk_ref=b.get('walk_ref'),
            a_role=a.get('role'), b_role=b.get('role'),
            a_offer=int_or_none(a.get('offer')), b_offer=int_or_none(b.get('offer')),
            a_walk_ref_frames=int_or_none(a.get('walk_ref_frames')),
            b_walk_ref_frames=int_or_none(b.get('walk_ref_frames')),
            a_end_stage=int_or_none(a.get('end_stage')), b_end_stage=int_or_none(b.get('end_stage')),
            a_onward_max=float_or_none(a.get('onward_max')), b_onward_max=float_or_none(b.get('onward_max')),
            a_onward_travel=float_or_none(a.get('onward_travel')), b_onward_travel=float_or_none(b.get('onward_travel')),
            a_onward_progress=float_or_none(a.get('onward_progress')), b_onward_progress=float_or_none(b.get('onward_progress')),
            a_onward_frames=int_or_none(a.get('onward_frames')), b_onward_frames=int_or_none(b.get('onward_frames')),
            a_brake_frames=int_or_none(a.get('brake_frames')), b_brake_frames=int_or_none(b.get('brake_frames')),
            a_held_frames=int_or_none(a.get('held_frames')), b_held_frames=int_or_none(b.get('held_frames')),
            a_admitted=None if a['outcome'] == 'unjudged' else a['outcome'] in ADMITTED,
            b_admitted=None if b['outcome'] == 'unjudged' else b['outcome'] in ADMITTED,
            a_margin=float(a['margin']), b_margin=float(b['margin']),
            a_progress=float(a['progress']), b_progress=float(b['progress']),
            a_land_speed=float(a['a0_speed']), b_land_speed=float(b['a0_speed']),
            a_land_cell=a['a0_cell'], b_land_cell=b['a0_cell'],
            a_committed=a['committed'] == '1', b_committed=b['committed'] == '1'))
    judged = [p for p in pairs if p['a_admitted'] is not None and p['b_admitted'] is not None]
    verdict = dict(pairs=len(pairs), unpaired=len(unpaired), censored=len(pairs)-len(judged),
                   b_only=sum(1 for p in judged if p['b_admitted'] and not p['a_admitted']),
                   a_only=sum(1 for p in judged if p['a_admitted'] and not p['b_admitted']),
                   both=sum(1 for p in judged if p['a_admitted'] and p['b_admitted']),
                   neither=sum(1 for p in judged if not p['a_admitted'] and not p['b_admitted']))
    both = [p for p in judged if p['a_admitted'] and p['b_admitted']]
    verdict['b_better_margin'] = sum(1 for p in both if p['b_margin'] > p['a_margin'])
    verdict['a_better_margin'] = sum(1 for p in both if p['a_margin'] > p['b_margin'])
    return pairs, unpaired, verdict


def runup_pairs(replay, start, end):
    """Pair each control candidate with its own tap-at-once/run-up sibling.

    The two differ only in whether 24 units of ground run precede the tap.
    The run-up sheds the landing speed the planned edge was learned with, so
    this pairing says what the momentum actually bought and what refused it.
    """
    rows, searches = [], {}
    for e in replay:
        if e.get('kind') != 'cand' or e.get('variant') != 'a_control' or successor_aim_clone(e):
            continue
        level = int(e['level'])
        if level < start or level > end:
            continue
        key = (e.get('search'), e.get('plan_kind'), e.get('hops'), e.get('edge'))
        searches.setdefault(key, {})[e.get('runup0')] = e
    for key, by_runup in sorted(searches.items(), key=lambda kv: int(kv[0][0] or 0)):
        fast, slow = by_runup.get('0'), by_runup.get('24')
        if fast is None or slow is None:
            continue
        rows.append(dict(search=key[0], level=int(fast['level']), plan_kind=key[1], hops=key[2], edge=key[3],
                         tap_outcome=fast['outcome'], runup_outcome=slow['outcome'],
                         tap_launch_speed=float(fast['l0_speed']), runup_launch_speed=float(slow['l0_speed']),
                         speed_shed=round(float(fast['l0_speed']) - float(slow['l0_speed']), 3),
                         tap_land_speed=float(fast['a0_speed']), runup_land_speed=float(slow['a0_speed']),
                         tap_margin=float(fast['margin']), runup_margin=float(slow['margin']),
                         tap_committed=fast['committed'] == '1', runup_committed=slow['committed'] == '1'))
    admitted = ADMITTED
    verdict = dict(
        pairs=len(rows),
        tap_rejected_runup_admitted=sum(1 for r in rows if r['tap_outcome'] not in admitted
                                        and r['runup_outcome'] in admitted),
        tap_off_path_runup_admitted=sum(1 for r in rows if r['tap_outcome'] == 'off_path'
                                        and r['runup_outcome'] in admitted),
        both_admitted=sum(1 for r in rows if r['tap_outcome'] in admitted and r['runup_outcome'] in admitted),
        runup_committed=sum(1 for r in rows if r['runup_committed']),
        tap_committed=sum(1 for r in rows if r['tap_committed']))
    shed = [r['speed_shed'] for r in rows if r['speed_shed'] > 0]
    verdict['median_speed_shed'] = round(sorted(shed)[len(shed) // 2], 1) if shed else None
    return rows, verdict


def chain_breaks(events, start, end):
    """Every landing whose next launch is more than one frame later.

    Input launch events use corrected command boundaries. An immediate tap
    starts on the landing boundary; across plans the bot can wait on the ground
    while a new plan is searched, and ground friction takes the speed the
    next planned edge's entry envelope was learned with. Each break records
    the interval and search result. Speed differences still use the launch
    confirmation observation; they are not exact friction measurements.
    """
    lands = [e for e in events if e.get('event') == 'edge' and e.get('reason') == 'land'
             and start <= int(e['level']) <= end]
    launches = [e for e in events if e.get('event') == 'edge' and e.get('reason') == 'launch'
                and start <= int(e['level']) <= end]
    searches = {}
    for e in events:
        if e.get('event') == 'plan' and e.get('reason') == 'none' and e.get('kind') == 'straight':
            searches[int(e['level'])] = e
    rows = []
    for land in lands:
        level, speed = int(land['level']), float(land['speed'])
        later = [x for x in launches if int(x['level']) >= level]
        if not later:
            continue
        launch = later[0]
        gap = int(launch['level']) - level
        search = searches.get(level)
        rows.append(dict(land_level=level, land_speed=speed,
                         launch_level=int(launch['level']), launch_speed=float(launch['speed']),
                         ground_ms=gap, speed_lost=round(speed - float(launch['speed']), 1),
                         chained=gap == 0,
                         landing_frame_search=detail_of(search, (
                             'candidates', 'refused', 'off_path', 'fell', 'horizon', 'slower',
                             'no_progress', 'state_candidates')) if search else 'none'))
    broken = [r for r in rows if not r['chained']]
    verdict = dict(landings=len(rows), chained=len(rows) - len(broken), breaks=len(broken),
                   ground_ms_in_breaks=sum(r['ground_ms'] for r in broken),
                   speed_lost_total=round(sum(r['speed_lost'] for r in broken), 1))
    return rows, verdict


# A candidate one of the extension passes continued past the test that had
# refused it: the reroute pass past the selected-path stop, the tap pass past
# the walk continuation. Both are judged against the shared extension
# reference, never the control's or arm B's.
def rerouted(candidate):
    return candidate.get('rerouted') == '1' or candidate.get('tap_onward') == '1'


def arm_isolation(replay, start, end, heading_offer=None):
    """Audit candidate roles and reference bounds without assuming B observes.

    Explicit v4 role/reference counts are authoritative. Historical roles may
    be inferred only from an explicit offer setting; v3 offer reference labels
    were inaccurate, so those runs cannot establish reference isolation.
    """
    searches, problems, limitations = {}, [], set()
    stats = dict(searches=0, candidates=0, commits=0, unjudged=0,
                 proposal_candidates=0, observe_only_candidates=0,
                 unknown_role_candidates=0, legacy_candidates=0)
    checked = 0
    mode_sources, horizon_sources, roles = set(), set(), set()
    for e in replay:
        if e.get('kind') == 'cand' and start <= int(e['level']) <= end:
            searches.setdefault((e.get('client'), e.get('gen'), e.get('search')), []).append(e)
    for key, cands in sorted(searches.items(), key=lambda kv: int(kv[0][2] or 0)):
        stats['searches'] += 1
        inferred = {}
        for variant in ('a_control', 'b_measured_policy'):
            done = [int(c['done']) for c in cands if c.get('variant') == variant
                    and not rerouted(c)
                    and c['outcome'] in ('admitted', 'arrives', 'slower') and c.get('done') is not None]
            inferred[variant] = max(done) if done else None
        # The reroute pass rolls a third reference, to its own furthest done
        # frame, and no arm's horizon may be inferred from its candidates.
        done = [int(c['done']) for c in cands if rerouted(c)
                and c['outcome'] in ('rerouted', 'arrives', 'slower', 'slower_route',
                                     'tap_onward', 'tap_refused')
                and c.get('done') is not None]
        inferred['reroute'] = max(done) if done else None
        b_roles, offers = set(), set()
        for c in cands:
            def fault(message):
                problems.append(dict(search=key[2], cand=c.get('cand'), fault=message))
            stats['candidates'] += 1
            variant = c.get('variant')
            explicit = 'role' in c
            offer = int_or_none(c.get('offer')) if 'offer' in c else heading_offer
            if offer is not None and offer not in (0, 1):
                fault('invalid offer mode')
                offer = None
            if offer is not None:
                offers.add(int(offer))
            if explicit:
                role = c['role']
                mode_sources.add('explicit')
                if role not in ('proposal', 'observe_only'):
                    fault('unknown candidate role')
                    role = None
            else:
                stats['legacy_candidates'] += 1
                mode_sources.add('settings_inferred' if offer is not None else 'unknown')
                role = ('proposal' if variant == 'a_control' or offer == 1 else
                        'observe_only' if variant == 'b_measured_policy' and offer == 0 else None)
            if variant not in ('a_control', 'b_measured_policy'):
                fault('unknown policy variant')
                role = None
            if role is None:
                stats['unknown_role_candidates'] += 1
                limitations.add('candidate role unavailable')
            else:
                roles.add(role)
                stats[role + '_candidates'] += 1
                if variant == 'a_control' and role != 'proposal':
                    fault('control candidate is not a proposal')
                if variant == 'b_measured_policy':
                    b_roles.add(role)
                    if offer is not None and role != ('proposal' if offer else 'observe_only'):
                        fault('candidate role contradicts offer mode')
            if c.get('committed') == '1':
                stats['commits'] += 1
                if role == 'observe_only':
                    fault('the observe-only arm committed')
            if c['outcome'] == 'unjudged':
                stats['unjudged'] += 1
                fault('no reference judged this candidate')
            ref = c.get('walk_ref')
            if ref is None:
                limitations.add('walk reference unavailable')
                continue
            # Old offer code labeled B as replay while reading control frames.
            if not explicit and offer == 1:
                limitations.add('legacy offer reference provenance unavailable')
                continue
            checked += 1
            want = ('extension' if rerouted(c) else
                     'replay' if variant == 'b_measured_policy' else 'control')
            # Logs written before the tap pass named that reference 'reroute'.
            if want == 'extension' and ref == 'reroute':
                ref = 'extension'
            if ref != want:
                fault('judged by the %s reference' % ref)
            if 'walk_ref_frames' in c:
                horizon = int_or_none(c['walk_ref_frames'])
                horizon_sources.add('explicit')
                if horizon is None or horizon < 0:
                    fault('invalid reference frame count')
                    horizon = None
            else:
                horizon = inferred.get('reroute' if rerouted(c) else variant)
                horizon_sources.add('inferred_done')
            frame = int_or_none(c.get('walk_frame'))
            if frame is not None and frame >= 0:
                if horizon is None:
                    limitations.add('reference frame bound unavailable')
                elif frame >= horizon:
                    fault('%s judged past its own horizon (%d >= %d)' % (want, frame, horizon))
        if len(b_roles) > 1 or len(offers) > 1:
            problems.append(dict(search=key[2], cand=None, fault='mixed roles or offer modes in one search'))
    stats.update(walk_ref_candidates=checked, checked=checked > 0 and not limitations,
                 fully_observed=checked > 0 and not limitations and not stats['legacy_candidates']
                                and horizon_sources == {'explicit'},
                 mode_sources=sorted(mode_sources), horizon_sources=sorted(horizon_sources),
                 roles=sorted(roles), limitations=sorted(limitations), problems=len(problems))
    return stats, problems


def analyse(case_dir, condition=None, launch_confirmation_delay_ms=None):
    result = json.loads(read_text(case_dir / 'result.json'))
    text = read_text(case_dir / 'server.log')
    start = corner_level(text, result)
    case = result.get('case', {})
    condition = condition or case.get('pass_condition', {}).get('condition', 'pickup')
    rows = play_rows(case_dir)
    pickup = pickup_level(rows, start, condition)
    events = command_boundary_events(parse_move_events(text), launch_confirmation_delay_ms)
    replay = parse_replay_events(text)
    end = pickup if pickup is not None else (int(rows[-1]['level_time']) if rows else start)
    segments = segment_timeline(events, start, end)
    table = transitions(events, start, end)
    pairs, unpaired, ab = paired_arms(replay, start, end)
    runups, runup_verdict = runup_pairs(replay, start, end)
    breaks, break_verdict = chain_breaks(events, start, end)
    offer_setting = result.get('settings', {}).get('bot_moveHeadingOffer')
    offer_value = int_or_none(offer_setting)
    isolation, isolation_problems = arm_isolation(
        replay, start, end, heading_offer=(int(offer_value != 0) if offer_value is not None else None))
    totals = {}
    for segment in segments:
        totals[segment['label']] = totals.get(segment['label'], 0) + segment['ms']
    elapsed = end - start
    accounted = sum(totals.values())
    evaluation = result.get('evaluation', {})
    coverage = movement_coverage(rows, start, end)
    return dict(
        analyzer_version=ANALYZER_VERSION,
        launch_confirmation_delay_ms=launch_confirmation_delay_ms,
        timing_note='Launch times are command boundaries; launch speeds remain confirmation-frame observations. Residual labels are event-interval accounting, not measured ground time or proof of cause.',
        case=case.get('name'), case_dir=str(case_dir),
        condition=condition,
        corner_level=start, pickup_level=pickup,
        reached=pickup is not None,
        elapsed_ms=elapsed,
        bench_time_ms=evaluation.get('time_ms'),
        bench_status=evaluation.get('status'), bench_reason=evaluation.get('reason'),
        owners=evaluation.get('owners'),
        movement_coverage=coverage, coverage_warnings=coverage['warnings'],
        settings_seed=result.get('settings', {}).get('bot_moveSpend'),
        totals=totals,
        accounted_ms=accounted,
        unclassified_ms=elapsed - accounted,
        segments=segments, transitions=table,
        paired_arms=pairs, unpaired_arms=unpaired, ab_verdict=ab,
        runup_pairs=runups, runup_verdict=runup_verdict,
        chain_breaks=breaks, chain_break_verdict=break_verdict,
        arm_isolation=isolation, arm_isolation_problems=isolation_problems,
        replay_events=replay,
        replay_variants=sorted({e.get('variant') for e in replay if e.get('variant')}),
    )


def write_outputs(report, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / 'replay.json').write_text(json.dumps(report, indent=1), encoding='utf-8', newline='\n')
    with (out_dir / 'segments.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, ('start', 'end', 'ms', 'label', 'detail'))
        writer.writeheader()
        writer.writerows(report['segments'])
    fields = sorted({key for row in report['transitions'] for key in row})
    with (out_dir / 'transitions.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fields, extrasaction='ignore')
        writer.writeheader()
        for row in report['transitions']:
            writer.writerow({k: ('' if row.get(k) is None else row.get(k)) for k in fields})
    for name, key in (('paired_arms.csv', 'paired_arms'), ('runup_pairs.csv', 'runup_pairs'),
                      ('chain_breaks.csv', 'chain_breaks')):
        rows = report[key]
        if not rows:
            continue
        with (out_dir / name).open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, sorted({k for row in rows for k in row}), extrasaction='ignore')
            writer.writeheader()
            writer.writerows(rows)
    (out_dir / 'report.txt').write_text(render(report), encoding='utf-8', newline='\n')


def render(report):
    lines = []
    lines.append('case %s  analyzer v%d' % (report['case'], report['analyzer_version']))
    lines.append('fixture level %s -> %s level %s' % (
        report['corner_level'], report['condition'],
        report['pickup_level'] if report['reached'] else 'NOT REACHED'))
    lines.append('elapsed %d ms (benchmark reported %s ms, %s/%s), owners %s' % (
        report['elapsed_ms'], report['bench_time_ms'], report['bench_status'],
        report['bench_reason'], report['owners']))
    lines.append(report['timing_note'])
    coverage = report['movement_coverage']
    lines.append('sampled route owners: %s; non-hop owners: %s' % (
        coverage['sampled_owners'], coverage['non_hop_owners']))
    lines.append(coverage['interpretation'])
    lines.append(coverage['sampling_note'])
    for warning in report['coverage_warnings']:
        lines.append('COVERAGE WARNING: ' + warning)
    lines.append('')
    lines.append('time cost distribution (mutually exclusive, sums to elapsed)')
    order = ['flight'] + [label for label in GROUND_LABELS]
    for label in order:
        ms = report['totals'].get(label)
        if ms is None:
            continue
        lines.append('  %-12s %7d ms  %5.1f %%' % (label, ms, 100.0 * ms / max(1, report['elapsed_ms'])))
    for label, ms in sorted(report['totals'].items()):
        if label not in order:
            lines.append('  %-12s %7d ms  %5.1f %%' % (label, ms, 100.0 * ms / max(1, report['elapsed_ms'])))
    lines.append('  %-12s %7d ms' % ('unclassified', report['unclassified_ms']))
    lines.append('')
    verdict = report['chain_break_verdict']
    lines.append('hop chaining (zero command-boundary delay is immediate; speed changes use confirmation observations)')
    lines.append('  landings %d, chained %d, broken %d; elapsed inside breaks %d ms; speed lost %.1f u/s total' % (
        verdict['landings'], verdict['chained'], verdict['breaks'],
        verdict['ground_ms_in_breaks'], verdict['speed_lost_total']))
    for row in report['chain_breaks']:
        if row['chained']:
            continue
        lines.append('  break land=%-6d %6.1f -> launch=%-6d %6.1f  residual=%4d ms  lost=%6.1f  landing-frame search: %s' % (
            row['land_level'], row['land_speed'], row['launch_level'], row['launch_speed'],
            row['ground_ms'], row['speed_lost'], row['landing_frame_search']))
    lines.append('')
    if report['replay_variants']:
        ab = report['ab_verdict']
        iso = report['arm_isolation']
        if iso['problems']:
            lines.append('arm isolation: %d FAULTS over %d searches, %d candidates' % (
                iso['problems'], iso['searches'], iso['candidates']))
            for problem in report['arm_isolation_problems'][:20]:
                lines.append('    search=%s cand=%s %s' % (problem['search'], problem['cand'], problem['fault']))
        elif not iso['checked']:
            lines.append('arm isolation: incomplete (%s)' % '; '.join(iso['limitations']))
        else:
            lines.append('arm role/reference checks: clean over %d searches, %d candidates, %d commits, %d unjudged' % (
                iso['searches'], iso['candidates'], iso['commits'], iso['unjudged']))
        lines.append('  mode sources=%s; horizon sources=%s; fully observed=%s' % (
            ','.join(iso['mode_sources']), ','.join(iso['horizon_sources']), iso['fully_observed']))
        lines.append('paired policy replay, arms: %s' % ', '.join(report['replay_variants']))
        lines.append('  A = aim from the live root at the edge\'s stored landing (control proposal)')
        lines.append('  B = ride the edge\'s stored control heading (graphEdge_t.dir); role follows explicit telemetry or retained settings')
        lines.append('  pairs %d (unpaired %d, censored %d): both admitted %d, A only %d, B only %d, neither %d' % (
            ab['pairs'], ab['unpaired'], ab['censored'], ab['both'], ab['a_only'], ab['b_only'], ab['neither']))
        lines.append('  of the pairs both admitted: A the better margin %d, B the better margin %d' % (
            ab['a_better_margin'], ab['b_better_margin']))
        for pair in report['paired_arms']:
            lines.append('    %6d search=%-3s edge=%-6s hops=%s runup=%-3s dyaw0=%+8.3f dyaw1=%8s  A %-10s margin=%8.2f | B %-10s margin=%8.2f%s' % (
                pair['level'], pair['search'], pair['edge'], pair['hops'], pair['runup'],
                pair['heading_delta'],
                'n/a' if pair['hop1_delta'] is None else '%+.3f' % pair['hop1_delta'],
                pair['a_outcome'], pair['a_margin'],
                pair['b_outcome'], pair['b_margin'],
                '  <- committed A' if pair['a_committed'] else '  <- committed B' if pair['b_committed'] else ''))
        censored = [p for p in report['paired_arms']
                    if p['a_admitted'] is None or p['b_admitted'] is None]
        if censored:
            lines.append('  censored pairs (no admission decided; never count these as refusals)')
            for pair in censored:
                lines.append('    %6d search=%-3s edge=%-6s hops=%s runup=%-3s  A %-10s progress=%8.2f | B %-10s progress=%8.2f  walk_ref A=%s B=%s' % (
                    pair['level'], pair['search'], pair['edge'], pair['hops'], pair['runup'],
                    pair['a_outcome'], pair['a_progress'], pair['b_outcome'], pair['b_progress'],
                    pair['a_walk_ref'] or 'n/a', pair['b_walk_ref'] or 'n/a'))
        lines.append('')
    runup = report['runup_verdict']
    if runup['pairs']:
        lines.append('tap-at-once against the 24-unit run-up, same heading and same root')
        lines.append('  pairs %d: both admitted %d; tap rejected while the run-up was admitted %d (of those, off_path %d)' % (
            runup['pairs'], runup['both_admitted'], runup['tap_rejected_runup_admitted'],
            runup['tap_off_path_runup_admitted']))
        lines.append('  commits: run-up %d, tap-at-once %d; median launch speed the run-up sheds %s u/s' % (
            runup['runup_committed'], runup['tap_committed'], runup['median_speed_shed']))
        for row in report['runup_pairs']:
            if row['tap_outcome'] == row['runup_outcome'] and not (row['tap_committed'] or row['runup_committed']):
                continue
            lines.append('    %6d %-8s edge=%-6s tap %-10s launch=%6.1f land=%6.1f | runup %-10s launch=%6.1f land=%6.1f  shed=%6.1f%s' % (
                row['level'], row['plan_kind'], row['edge'], row['tap_outcome'], row['tap_launch_speed'],
                row['tap_land_speed'], row['runup_outcome'], row['runup_launch_speed'],
                row['runup_land_speed'], row['speed_shed'],
                '  <- committed ' + ('tap' if row['tap_committed'] else 'runup')
                if (row['tap_committed'] or row['runup_committed']) else ''))
        lines.append('')
    lines.append('segments')
    for segment in report['segments']:
        lines.append('  %6d..%-6d %6d ms  %-12s %s' % (
            segment['start'], segment['end'], segment['ms'], segment['label'], segment['detail']))
    lines.append('')
    lines.append('transitions')
    for row in report['transitions']:
        if row['kind'] == 'flew':
            lines.append('  %6d flew hop%s plan=%s/%s edge=%s launch=%s flight=%s ms '
                         'launch_confirmation_speed=%s land_speed=%s predicted_land_speed=%s' % (
                             row['level'], row['hop'], row['plan'], row['plan_kind'], row['edge_index'],
                             row['actual_launch_level'], row['flight_ms'], row['launch_confirmation_speed'],
                             row['actual_land_speed'], row['predicted_land_speed']))
        else:
            lines.append('  %6d %-22s %s' % (row['level'], row['kind'], row.get('detail') or
                                             'edge=%s' % row.get('edge_index')))
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('case', type=Path, nargs='+',
                        help='benchmark case directory (contains result.json and server.log)')
    parser.add_argument('--out', type=Path, required=True, help='directory for the analysis outputs')
    parser.add_argument('--condition', choices=('pickup', 'capture'),
                        help='override the case pass condition used to find the objective frame')
    parser.add_argument('--launch-confirmation-delay-ms', type=int,
                        help='source-verified command-to-confirmation delay for historical logs; 25 for fixed-step runs')
    args = parser.parse_args(argv)
    reports = []
    for case in args.case:
        report = analyse(case, args.condition, args.launch_confirmation_delay_ms)
        # Two arms of the same case share a case name, so the run directory
        # the case came from is part of its output path.
        write_outputs(report, args.out / case.resolve().parent.name / (report['case'] or case.name))
        reports.append(report)
        print(render(report))
    (args.out / 'index.json').write_text(json.dumps(
        [{k: v for k, v in r.items() if k not in ('segments', 'transitions', 'replay_events')}
         for r in reports], indent=1), encoding='utf-8', newline='\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
