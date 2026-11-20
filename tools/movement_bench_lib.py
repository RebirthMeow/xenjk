"""Pure helpers for the movement benchmark (docs/movement/PLAN.md, benchmark gate).

Case loading and selection, pass/fail evaluation of one world's play-log rows,
and parsing of the one-schema ``[MOVE]`` console events. No process, socket or
file-system side effects; ``movement_bench.py`` owns the native worlds.
"""
import hashlib
import json
import re

FORMAT = 'MOVEBENCH1'
FP_SPEED_BIT = 1 << 2  # forcePowersActive bit for Force Speed (FP_SPEED == 2)
LEVEL_TEMPLATES = {
    1: ('walk', 'corner', 'hop', 'tap'),
    2: ('walk', 'corner', 'hop', 'tap', 'speed'),
    3: ('walk', 'corner', 'hop', 'tap', 'speed', 'hold'),
}
CONDITIONS = ('pickup', 'capture', 'composition', 'reach')
GOALCHAIN_LINE = re.compile(r'\[GOALCHAIN\] client=\d+ level=(?P<level>\d+) event=reached '
                            r'index=(?P<index>\d+) of=(?P<of>\d+)')
MOVE_LINE = re.compile(r'(?:^|\s)\[MOVE\] (?P<body>client=\d+ .*)$', re.M)
KICK_LINE = re.compile(r'\[KICK\] (?P<what>ARMED|APPLIED) (?P<body>level=-?\d+ .*)$', re.M)
FIELD_PAIR = re.compile(r'(\w+)=(-?[\w.]+)')


def load_cases(path):
    """Read and validate a MOVEBENCH1 case file; return (document, sha256)."""
    raw = path.read_bytes()
    doc = json.loads(raw.decode('utf-8'))
    if doc.get('format') != FORMAT:
        raise ValueError('case file format must be ' + FORMAT)
    for key in ('map', 'bsp_sha256', 'cases'):
        if key not in doc:
            raise ValueError('case file lacks ' + key)
    names = set()
    for case in doc['cases']:
        validate_case(case)
        if case['name'] in names:
            raise ValueError('duplicate case name ' + case['name'])
        names.add(case['name'])
    return doc, hashlib.sha256(raw).hexdigest()


def validate_case(case):
    for key in ('name', 'team', 'leg', 'level', 'start', 'resources', 'spend', 'goal', 'pass_condition'):
        if key not in case:
            raise ValueError(f'case {case.get("name", "?")} lacks {key}')
    if case['team'] not in ('red', 'blue') or case['leg'] not in ('outbound', 'return', 'chain'):
        raise ValueError(f'case {case["name"]}: bad team or leg')
    if case['leg'] == 'chain':
        # A declared goal chain (bot_goalChain): the listed points in turn, then
        # the native objective. `reach` passes on the last point, `capture` on
        # the capture that follows it; with no points, a capture leg is the
        # whole round trip: the enemy flag, then home.
        points = case.get('chain', [])
        if any(len(p) != 3 for p in points) or \
                (not points and case['pass_condition']['condition'] == 'reach'):
            raise ValueError(f'case {case["name"]}: chain points are [x, y, z], and reach needs one')
        if case['pass_condition']['condition'] not in ('reach', 'capture') or case['start']['carrying']:
            raise ValueError(f'case {case["name"]}: a chain leg starts empty-handed and passes on reach or capture')
    elif 'chain' in case or case['pass_condition']['condition'] == 'reach':
        raise ValueError(f'case {case["name"]}: chain points and reach belong to a chain leg')
    if case['level'] not in LEVEL_TEMPLATES:
        raise ValueError(f'case {case["name"]}: level must be 1..3')
    start = case['start']
    if len(start['origin']) != 3 or len(start.get('velocity', (0, 0, 0))) != 3 or start['carrying'] not in (0, 1):
        raise ValueError(f'case {case["name"]}: bad start')
    if not 0 <= case['resources']['force'] <= 100 or not 0 <= case['resources'].get('speed_ms', 0) <= 60000:
        raise ValueError(f'case {case["name"]}: bad resources')
    templates = tuple(case['spend']['templates'])
    if templates != LEVEL_TEMPLATES[case['level']]:
        raise ValueError(f'case {case["name"]}: spend templates do not match level {case["level"]}')
    if case['pass_condition']['condition'] not in CONDITIONS or case['pass_condition']['bound_ms'] <= 0:
        raise ValueError(f'case {case["name"]}: bad pass condition')
    if case['pass_condition']['condition'] != 'composition' and case['leg'] != 'chain' and \
            (case['leg'] == 'return') != (case['pass_condition']['condition'] == 'capture'):
        raise ValueError(f'case {case["name"]}: leg and pass condition disagree')
    if (case['leg'] == 'return') != bool(start['carrying']):
        raise ValueError(f'case {case["name"]}: leg and carrying disagree')


def select_cases(cases, level=None, pattern=None):
    """Cases at one level and/or whose name matches a regular expression."""
    chosen = list(cases)
    if level is not None:
        chosen = [c for c in chosen if c['level'] == level]
    if pattern:
        expression = re.compile(pattern)
        chosen = [c for c in chosen if expression.search(c['name'])]
    return chosen


# Mirror twins: blue_out_X and red_ret_X (and blue_ret_X and red_out_X) run the
# same route from the same stand. Over every class-pinned arm
# they finished in the same time in 1,913 of 1,916 pairs on the five maps below
# (records/2026-09-25-deferred-pin.md), so a comparison can run one of each.
# A map runs twins only where it is listed; the pattern names the cases whose
# twins still differ there and keep both (Kejim's stands, 40 of 531; Taloraan's
# L3 respawns, 3 of 471). The eight prepared maps were audited:
# 1,026 of 1,026 pairs identical (records/2026-09-29-twin-audit.md).
TWIN_SKIPPABLE = {
    'mp/ctf4': None, 'mp/ctf_carratos': None, 'ctf_dantooine_v3': None, 'ctf_metalorn_v2': None,
    'mp/ctf_taloraan': r'_L3_respawn[45]$', 'mp/ctf_kejim': r'_stand$',
    'ctf_illimiran_v2': None, 'ctf_kharzet_v3': None, 'ctf_kothis_v5': None, 'mp/ctf_manaan': None,
    'mp/ctf_nelvaan': None, 'mp/ctf_talay': None, 'mp/ctf_talravin': None, 'mp/ctf_telti': None,
}
TWIN_OF = {'blue_ret_': 'red_out_', 'red_ret_': 'blue_out_'}


def skip_twins(cases, map_name):
    """(kept, skipped): each return leg whose outbound twin is also selected is
    dropped, on maps measured symmetric, except where the map keeps both."""
    if map_name not in TWIN_SKIPPABLE:
        return list(cases), []
    keep = TWIN_SKIPPABLE[map_name]
    names = {c['name'] for c in cases}
    kept, skipped = [], []
    for case in cases:
        twin = next((out + case['name'][len(ret):] for ret, out in TWIN_OF.items()
                     if case['name'].startswith(ret)), None)
        if twin in names and not (keep and re.search(keep, case['name'])):
            skipped.append(case['name'])
        else:
            kept.append(case)
    return kept, skipped


CORNER_LEVEL_STEP = 25  # play-log cadence; a corner start can only land on a multiple


def validate_corner_level(value):
    """A pinned corner level must be a positive multiple of the play cadence."""
    if value is None:
        return None
    if value <= 0 or value % CORNER_LEVEL_STEP:
        raise ValueError('corner level must be a positive multiple of %d ms' % CORNER_LEVEL_STEP)
    return value


def latest_play_level(text):
    """``level_time`` of the last complete play-log row, or None.

    Read while the server is still writing the file, so the final line may be
    torn. A short or unparsable row is skipped rather than guessed at.
    """
    lines = text.splitlines()
    if len(lines) < 2:
        return None
    header = lines[0].split(',')
    if 'level_time' not in header:
        return None
    index = header.index('level_time')
    for line in reversed(lines[1:]):
        fields = line.split(',')
        if len(fields) != len(header):
            continue
        try:
            return int(fields[index])
        except ValueError:
            continue
    return None


CORNER_CLASS_MODULUS = 200  # lcm(25 ms frame, pmove_msec 8): the start-level class


def validate_corner_class(value, minimum, maximum):
    """A pinned start class is a level residue the play log can actually show.

    The pre-start play log samples every 250 ms of level time, so a world can
    only be started on its own ladder `L0 + 250k`. Levels one frame apart are
    therefore not all reachable in the same world, while a whole residue class
    of `CORNER_CLASS_MODULUS` is reachable in every world whose ladder meets
    it. The class, not the absolute level, is what the retained evidence
    separates; `--corner-level` remains available for an exact level.
    """
    if value is None:
        return None
    if value < 0 or value >= CORNER_CLASS_MODULUS or value % CORNER_LEVEL_STEP:
        raise ValueError('corner class must be a multiple of %d ms below %d'
                         % (CORNER_LEVEL_STEP, CORNER_CLASS_MODULUS))
    if minimum <= 0 or minimum % CORNER_LEVEL_STEP or maximum <= minimum:
        raise ValueError('corner class window must be a positive multiple of %d ms and rise'
                         % CORNER_LEVEL_STEP)
    if not any(level % CORNER_CLASS_MODULUS == value
               for level in range(minimum, maximum + 1, CORNER_LEVEL_STEP)):
        raise ValueError('no level of class %d lies in [%d, %d]' % (value, minimum, maximum))
    return value


def corner_class_state(latest, klass, minimum, maximum):
    """Whether to keep waiting, start now, or give this world up, by class.

    Waits for a visible row at or after `minimum` whose level is in the class.
    A row that misses the class is not a failure: the next class member is
    250 ms-ladder-compatible or it is not, and a world whose ladder never meets
    the class runs past `maximum` and is replaced. The landed level is still
    verified by the caller, so a world that starts off-class is never counted.
    """
    if latest is None:
        return 'wait'
    if latest > maximum:
        return 'overshot'
    if latest >= minimum and latest % CORNER_CLASS_MODULUS == klass:
        return 'send'
    return 'wait'


def corner_pin_state(latest, target, lead):
    """Whether to keep waiting, send the corner start now, or give the world up.

    The fixture's start level decides the whole leg, so a comparison that pins
    it is reproducible where an unpinned one is a draw over start levels.

    Measured: the corner start lands on the level of the last
    play-log row the runner has seen, three worlds out of three, so the
    default lead is zero and `send` means the world is standing on the target
    frame right now. `lead` exists to re-tune that on a slower host. Only a
    successful start consumes a world, so a miss is retried in a fresh one.
    """
    if latest is None:
        return 'wait'
    if latest > target:
        return 'overshot'
    if latest >= target - lead:
        return 'send'
    return 'wait'


def runner_rows(rows, corner_level):
    """Play-log rows of the movement-only runner after the corner start."""
    return [r for r in rows if int(r.get('movement_only', 1)) == 1 and int(r['level_time']) > corner_level]


def chain_done_level(text, corner_level):
    """The level the goal chain's last point was reached after the start, or None."""
    for match in GOALCHAIN_LINE.finditer(text or ''):
        level = int(match.group('level'))
        if level > corner_level and int(match.group('index')) == int(match.group('of')) - 1:
            return level
    return None


def kick_audit(text, corner_level, end_level=None):
    """The declared knockback fixture (bot_moveKick) in one world: the kicks
    armed at the release and those applied after it, in order. A kick armed
    and never applied is listed as unfired, so a world whose kick never found
    its phase is not read as one that recovered from it.

    ``end_level`` is the evaluation's endpoint (the completion or the death).
    A kick applied at or after it hit a world whose verdict was already in, so
    it is listed as ``late`` and counted as unfired, never as a recovery. The
    endpoint frame itself is late: the verdict's sample reports the previous
    Pmove's result, and the kick is applied before that sample, so a kick
    logged on the verdict's frame came after the completion it would claim."""
    armed, applied, late = [], [], []
    for match in KICK_LINE.finditer(text or ''):
        fields = dict(FIELD_PAIR.findall(match.group('body')))
        level = int(fields['level'])
        if corner_level is None or level < corner_level:
            continue
        if match.group('what') == 'ARMED':
            if level != corner_level:
                continue
            armed.append(dict(index=int(fields['index']), trigger=fields['trigger'],
                              knockback=float(fields['knockback']), angle=float(fields['angle']),
                              pitch=float(fields['pitch'])))
        else:
            kick = dict(index=int(fields['index']), level=level, rel_ms=int(fields['rel_ms']),
                        trigger=fields['trigger'], ground=int(fields['ground']),
                        kind=int(fields['kind']), stage=int(fields['stage']),
                        speed=float(fields['speed']), after_speed=float(fields['after_speed']),
                        vz=float(fields['vz']), after_vz=float(fields['after_vz']),
                        origin=[float(fields[a]) for a in 'xyz'])
            (late if end_level is not None and level >= end_level else applied).append(kick)
    fired = {k['index'] for k in applied}
    audit = dict(armed=len(armed), applied=applied,
                 unfired=[k['index'] for k in armed if k['index'] not in fired])
    if late:
        audit['late'] = late
    return audit


def evaluate(case, rows, corner_level, events=None, reach_level=None):
    """Judge one world. Rows are the runner's play-log rows after the start.

    Red is death, a stall past the bound, or a forbidden spend. Green is the
    pass condition inside the bound with none of those. Anything else is
    incomplete: the world ended before the bound without a verdict.

    The ``composition`` condition (phase 1 spike) is judged on the ``[MOVE]``
    events after the start as well: a declared start must obtain a sequence,
    execute it without a miss or a parity mismatch, and complete useful
    onward travel. Refusing to plan is a failure.
    """
    rows = runner_rows(rows, corner_level)
    level = case['level']
    condition = case['pass_condition']['condition']
    bound = case['pass_condition']['bound_ms']
    start_force = case['resources']['force']
    templates = case['spend']['templates']
    result = dict(status='incomplete', reason='no_rows', time_ms=None, deaths=0,
                  force_min=None, speed_active_samples=0, jump_samples=0,
                  owners=[], samples=len(rows), elapsed_ms=0)
    if not rows:
        return result
    elapsed = int(rows[-1]['level_time']) - corner_level
    forces = [int(r['force']) for r in rows]
    result.update(force_min=min(forces), elapsed_ms=elapsed,
                  speed_active_samples=sum(bool(int(r['powers_active']) & FP_SPEED_BIT) for r in rows),
                  jump_samples=sum(int(r['up']) > 0 for r in rows),
                  owners=sorted({r.get('movement_owner', '') for r in rows}))
    passed_at = None
    speed_seen = False
    # A chain capture counts only after the chain's last point: a capture on
    # the way, with waypoints still to go, does not end the leg. Captures are
    # counted from the last row at or before the chain completed: the row on
    # the completion frame samples the Pmove before it, so a capture it shows
    # was made on the way.
    chained = condition == 'capture' and case['leg'] == 'chain' and bool(case.get('chain'))
    captures_before = 0
    for r in rows:
        t = int(r['level_time']) - corner_level
        if int(r['health']) <= 0:
            result.update(status='fail', reason='death', time_ms=t, deaths=1)
            return result
        speed_active = bool(int(r['powers_active']) & FP_SPEED_BIT)
        if 'speed' not in templates and speed_active:
            result.update(status='fail', reason='forbidden_speed', time_ms=t)
            return result
        speed_seen = speed_seen or speed_active
        # A held jump drains Force; a tap does not. Below the start pool,
        # less the one Speed purchase once Speed has been seen active,
        # something was held. Four-hertz rows make this a measured floor,
        # not a proof.
        floor = start_force - (50 if speed_seen else 0)
        if 'hold' not in templates and int(r['force']) < floor:
            result.update(status='fail', reason='forbidden_hold', time_ms=t)
            return result
        if condition == 'composition':
            continue
        if condition == 'reach':
            if reach_level is not None and int(r['level_time']) >= reach_level:
                passed_at = reach_level - corner_level
                break
            continue
        if chained and (reach_level is None or int(r['level_time']) <= reach_level):
            captures_before = int(r['captures'])
            continue
        reached = int(r['carrying']) == 1 if condition == 'pickup' else int(r['captures']) >= captures_before + 1
        if reached and passed_at is None:
            passed_at = t
            break
    if condition == 'reach' and passed_at is None and reach_level is not None:
        # Reached after the last four-hertz row; no row after it saw a death.
        passed_at = reach_level - corner_level
    if condition == 'composition':
        verdict = composition_verdict(events or [], corner_level)
        result.update(composition=verdict)
        if verdict['status'] == 'fail':
            result.update(status='fail', reason=verdict['reason'], time_ms=verdict['time_ms'])
            return result
        passed_at = verdict['time_ms'] if verdict['status'] == 'pass' else None
    if passed_at is not None:
        if passed_at <= bound:
            result.update(status='pass', reason='complete', time_ms=passed_at)
        else:
            result.update(status='fail', reason='stall', time_ms=passed_at)
        return result
    if elapsed >= bound:
        result.update(status='fail', reason='stall', time_ms=elapsed)
        return result
    result.update(reason='world_ended', time_ms=elapsed)
    return result


def composition_verdict(events, corner_level):
    """The spike's sequence gate from the events after the start.

    fail: a plan event with reason ``none`` (refusal), a ``miss`` event, or a
    parity mismatch. pass: an ``onward`` edge reporting ``complete``, timed
    from the start. Otherwise pending.
    """
    verdict = dict(status='pending', reason='no_plan', time_ms=None, plan=None, edges=[])
    for e in events:
        level = int(e.get('level', 0))
        t = level - corner_level
        event, reason, edge = e.get('event'), e.get('reason', ''), e.get('edge', '')
        if event == 'plan' and reason == 'sequence':
            verdict.update(plan=dict(e), reason='planned')
        elif event == 'plan' and reason == 'none':
            verdict.update(status='fail', reason='no_sequence', time_ms=t, plan=dict(e))
            return verdict
        elif event == 'miss':
            verdict.update(status='fail', reason='miss:' + reason, time_ms=t)
            return verdict
        # ``tally`` is the engine's end-of-leg comparison count and ``horizon``
        # its periodic one; neither is a mismatch, and a world is not failed
        # for reporting how much it compared.
        elif event == 'parity' and reason not in ('forecast_refused', 'horizon', 'tally'):
            verdict.update(status='fail', reason='parity:' + reason, time_ms=t)
            return verdict
        elif event == 'edge':
            verdict['edges'].append(edge + ':' + reason)
            if edge == 'onward' and reason == 'complete':
                verdict.update(status='pass', reason='complete', time_ms=t)
                return verdict
    return verdict


def parse_move_events(text, after_level=None):
    """Every ``[MOVE] key=value ...`` line as a dict; values stay strings.

    The server log prefixes lines with a timestamp. With ``after_level`` only
    events after that level time are kept, so a declared start intervention
    (the corner teleport) is not judged as a parity mismatch.
    """
    events = []
    for match in MOVE_LINE.finditer(text):
        fields = {}
        for token in match.group('body').split():
            key, _, value = token.partition('=')
            if _:
                fields[key] = value
        if after_level is not None and int(fields.get('level', -1)) <= after_level:
            continue
        events.append(fields)
    return events


def parity_summary(events):
    """Count parity outcomes: mismatches by reason, forecast refusals, tallies.

    ``compared`` is the world's real comparison count. The engine's periodic
    ``horizon`` event carries a count only when the horizon was exhausted and
    an arbitrary rollout counter came up, so a world that compared hundreds of
    frames could print no such event and be read as zero - four class-75
    control worlds on three builds did exactly that
    (`records/2026-09-22-tape-library.md`). ``Move_ParityTally`` now emits one
    ``tally`` event per leg with the count the leg actually made, and that is
    what ``compared`` reports when it is present; a log from before the tally
    existed still reads its largest ``horizon`` count, so retained worlds are
    read as they always were.

    ``row1_ok`` is gate row 1's parity requirement as the gate states it: zero
    mismatches **and** a non-zero comparison count. A world that genuinely
    compared nothing still fails it.
    """
    summary = dict(mismatches=0, by_reason={}, refusals=0, horizons=0, compared=0,
                   tallies=0, tallied=0, horizon_frames=0)
    for e in events:
        if e.get('event') != 'parity':
            continue
        reason = e.get('reason', '')
        if reason == 'forecast_refused':
            summary['refusals'] += 1
        elif reason == 'horizon':
            summary['horizons'] += 1
            summary['horizon_frames'] = max(summary['horizon_frames'], int(e.get('frames', 0)))
        elif reason == 'tally':
            summary['tallies'] += 1
            summary['tallied'] = max(summary['tallied'], int(e.get('frames', 0)))
        else:
            summary['mismatches'] += 1
            summary['by_reason'][reason] = summary['by_reason'].get(reason, 0) + 1
    summary['compared'] = summary['tallied'] if summary['tallies'] else summary['horizon_frames']
    summary['row1_ok'] = summary['mismatches'] == 0 and summary['compared'] > 0
    return summary


def straight_audit(events):
    """Audit the straight judge's walk comparison on every committed plan.

    A straight sequence, or a state sequence (a planned strafe edge, judged
    by the same judge), must have been compared with the walk at the frame
    before its own done frame (``walk_frame == frames - 1``), unless the walk
    rollout stopped earlier (``walk_stopped``). Anything else is a commit
    judged against a frozen walk, and the ranking that chose it is void.
    """
    audit = dict(commits=0, short=[], state_commits=0)
    for e in events:
        if e.get('event') != 'plan' or e.get('reason') != 'sequence' or e.get('kind') not in ('straight', 'state'):
            continue
        audit['commits'] += 1
        audit['state_commits'] += e.get('kind') == 'state'
        frames, walk_frame = int(e.get('frames', -1)), int(e.get('walk_frame', -2))
        if walk_frame != frames - 1 and not int(e.get('walk_stopped', 0)):
            audit['short'].append(dict(level=int(e.get('level', 0)), id=e.get('id'), frames=frames, walk_frame=walk_frame))
    return audit


def state_audit(events):
    """The state plans of one world: paths by search, strafe edges planned,
    validated commits, failed validations, edges learned and merged.

    ``paths_state`` counts plan events answered by the state search,
    ``paths_cell`` those answered by the cell search (no state path, or its
    table overflowed); ``strafe_planned`` sums the strafe edges those paths
    took; ``learned`` and ``merged`` count the strafe edges written from
    completed straight plans; ``avoided`` the edges whose validation failed.
    """
    audit = dict(paths_state=0, paths_cell=0, overflow=0, strafe_planned=0, commits=0, chains=0,
                 avoided=0, learned=0, merged=0, envelope=0, refused=0)
    for e in events:
        event, reason = e.get('event'), e.get('reason')
        if event == 'plan' and reason == 'path':
            if int(e.get('state', 0)):
                audit['paths_state'] += 1
            else:
                audit['paths_cell'] += 1
            audit['overflow'] += int(e.get('overflow', 0))
            audit['strafe_planned'] += int(e.get('strafe', 0))
        elif event == 'plan' and reason == 'sequence' and e.get('kind') == 'state':
            audit['commits'] += 1
            audit['chains'] += int(e.get('hops', 1)) > 1
            audit['envelope'] += int(e.get('envelope', 0))
        elif event == 'edge' and e.get('edge') == 'strafe':
            if reason == 'learn':
                audit['learned'] += 1
                audit['merged'] += int(e.get('merged', 0))
            elif reason == 'avoid':
                audit['avoided'] += 1
            elif reason == 'refused':
                audit['refused'] += 1
    return audit


def event_summary(events):
    """Non-parity event counts by (event, reason)."""
    counts = {}
    for e in events:
        if e.get('event') == 'parity':
            continue
        key = e.get('event', '') + ':' + e.get('reason', '')
        counts[key] = counts.get(key, 0) + 1
    return counts


def campaign_verdict(results):
    """Safety is binary over the run: any red case makes the campaign red."""
    statuses = [r['evaluation']['status'] for r in results]
    return dict(cases=len(results), passed=statuses.count('pass'),
                failed=statuses.count('fail'), incomplete=statuses.count('incomplete'),
                green=bool(results) and all(s == 'pass' for s in statuses))
