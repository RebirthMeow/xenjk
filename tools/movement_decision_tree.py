"""Read-only: does the judge's ranking predict the completed trip?

At a decision of a decision-explorer campaign, every admitted candidate that
was branched has a world and a completed time. This tool builds one table per
decision - candidate index, kind, hops, run-ups, landing cells, the fields the
`[SPIKE]` line prints for that candidate, the judge's own score and its rank
among the admitted set, and the branch world's completed trip - and reports the
Spearman rank correlation between the judge's rank and the completed time.

Two things about where the judge's score comes from, because they decide what
this tool can claim.

The per-candidate `[SPIKE]` line does not print `score`. It prints the
rollout's `progress`, its `done` frame, its landing speeds and its outcome; the
score is `margin + exit_credit` and the margin is `progress` minus the walk
reference read at the candidate's own `done` frame, which the candidate line
never carries. So the score is not reconstructible from a candidate line.

It is printed, exactly, on the commit line of the world that committed that
candidate - and in a branch campaign every admitted candidate was committed, by
the forced-index override, in its own world at its own decision. This tool
therefore reads each candidate's score from its own branch world's commit line
(`select_forced=1`, `commit_nth` equal to the branch depth) rather than
computing one, and says so per row. A candidate with no branch world has no
score here and is reported as unranked rather than estimated.

No game code, no native run: every input is a retained world.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import movement_decision_explore as explore  # noqa: E402
import movement_decision_tape as tape  # noqa: E402

SCORE_FIELDS = ('level', 'kind', 'hops', 'd0', 'r0', 'd1', 'r1', 'c0', 'c1',
                'score', 'margin', 'walk', 'progress', 'exit_credit', 'exit_ms',
                'launch_speed', 'land_speed', 'launch1_speed', 'land1_speed',
                'speed', 'x', 'y', 'z', 'rerouted', 'route_ms',
                'candidates', 'admitted', 'frames', 'index', 'index2')


# ---------------------------------------------------------------------------
# The progress file


def read_progress(path):
    """Every world record of a campaign, last write per world name wins."""
    records = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        records[record['world']] = record
    return records


def lineage(records, world):
    """The chain of parents from the root down to `world`, root first."""
    chain, name = [], world
    while name is not None:
        if name not in records:
            raise KeyError('no record for world ' + name)
        chain.append(name)
        name = records[name].get('parent')
    return list(reversed(chain))


def branches(records, parent, depth):
    """The branch worlds of `parent` taken at `depth`, by candidate index."""
    found = {}
    for record in records.values():
        branch = record.get('branch') or {}
        if record.get('parent') != parent or branch.get('nth') != depth:
            continue
        found[branch['cand']] = record
    return found


# ---------------------------------------------------------------------------
# The judge's own score, from the world that committed the candidate


def commit_fields(text, nth, release=None):
    """The commit line's own fields at the `nth` commit after the release."""
    for line in text.splitlines():
        head = tape.SEQUENCE.search(line)
        if not head:
            continue
        if release is not None and int(head.group('level')) <= release:
            continue
        fields = dict(explore.FIELD.findall(line.split('reason=sequence', 1)[1]))
        if int(fields.get('commit_nth', -1)) == nth:
            fields['level'] = head.group('level')
            return fields
    return None


def judged(record, depth):
    """One branch world's own commit line for the candidate it was forced to take.

    Read only when the engine says the forced index fired there
    (`select_forced=1`); `select_forced=-1` is the engine saying the named
    index was not an admitted candidate of that world's own search, so that
    world committed something else and neither its score nor its trip belongs
    to this candidate. Such a row is dropped, not estimated.
    """
    fields = commit_fields(explore.world_text(Path(record['out'])), depth,
                           record.get('corner_level'))
    if not fields or fields.get('select_forced') != '1':
        return None
    return {key: fields[key] for key in SCORE_FIELDS if key in fields}


# ---------------------------------------------------------------------------
# Ranking and correlation


def ranks(values, reverse=False):
    """Fractional (average) ranks, 1 is first. `reverse` ranks larger first."""
    order = sorted(range(len(values)), key=lambda i: values[i], reverse=reverse)
    out = [0.0] * len(values)
    index = 0
    while index < len(order):
        stop = index
        while stop + 1 < len(order) and values[order[stop + 1]] == values[order[index]]:
            stop += 1
        shared = (index + stop) / 2.0 + 1
        for position in range(index, stop + 1):
            out[order[position]] = shared
        index = stop + 1
    return out


def spearman(xs, ys):
    """Spearman's rho on fractional ranks, or None when it is undefined."""
    if len(xs) != len(ys):
        raise ValueError('two equal-length sequences are required')
    if len(xs) < 2:
        return None
    rx, ry = ranks(list(xs)), ranks(list(ys))
    n = len(rx)
    mx, my = sum(rx) / n, sum(ry) / n
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    dx = sum((a - mx) ** 2 for a in rx)
    dy = sum((b - my) ** 2 for b in ry)
    if dx <= 0 or dy <= 0:
        return None
    return num / (dx * dy) ** 0.5


# ---------------------------------------------------------------------------
# One decision's table


def decision_table(records, parent, depth):
    """Every admitted candidate at one decision, with its branch's trip.

    The admitted set is counted from the parent world's own `[SPIKE]` lines at
    the decision it took, and the parent's view of each index is carried for
    reference. But every measured field of a row - what the candidate was, what
    the judge scored it, what the trip took - is read from the **branch world's
    own** commit line, because a branch runs under a prefix tape and its state
    at the decision is close to the parent's but not identical, so the parent's
    line for that index describes a slightly different candidate.

    A branch whose engine reported `select_forced=-1` took the ranking's own
    choice instead: its trip is not this candidate's and the row is dropped.
    """
    record = records[parent]
    entries = record.get('decisions') or []
    if depth - 1 >= len(entries):
        raise ValueError('%s has no decision %d' % (parent, depth))
    entry = entries[depth - 1]
    text = explore.world_text(Path(record['out']))
    admitted = explore.admitted_candidates(text, entry)
    parent_view = {row['cand']: row for row in admitted}
    parent_commit = commit_fields(text, depth, record.get('corner_level')) or {}
    found = branches(records, parent, depth)
    rows, dropped = [], []
    for cand in sorted(found):
        branch = found[cand]
        fields = judged(branch, depth)
        if not fields or branch.get('time_ms') is None:
            dropped.append(dict(cand=cand, branch_world=branch['world'],
                                time_ms=branch.get('time_ms'),
                                forced=branch.get('forced'),
                                why='the named index was not admitted in this world'
                                    if branch.get('forced') == -1 else 'no forced commit line'))
            continue
        rows.append(dict(
            cand=cand, branch_world=branch['world'], time_ms=branch['time_ms'],
            status=branch.get('status'), corner_level=branch.get('corner_level'),
            kind=fields.get('kind'), hops=int(fields.get('hops', 0)),
            r0=fields.get('r0'), r1=fields.get('r1'),
            d0=fields.get('d0'), d1=fields.get('d1'),
            cell=int(fields.get('c0', -1)), cell1=int(fields.get('c1', -1)),
            root_speed=float(fields.get('speed', 0)),
            judged=fields, score=float(fields['score']),
            progress=float(fields.get('progress', 0)),
            walk=float(fields.get('walk', 0)),
            margin=float(fields.get('margin', 0)),
            exit_credit=float(fields.get('exit_credit', 0)),
            exit_speed=float(fields.get('land1_speed') or 0) or
            float(fields.get('land_speed') or 0),
            parent_view=parent_view.get(cand)))
    order = ranks([r['score'] for r in rows], reverse=True)
    for row, rank in zip(rows, order):
        row['judge_rank'] = rank
    order = ranks([r['time_ms'] for r in rows])
    for row, rank in zip(rows, order):
        row['time_rank'] = rank
    times = [r['time_ms'] for r in rows]
    speeds = [r['root_speed'] for r in rows]
    return dict(
        parent=parent, depth=depth, level=entry['level'],
        corner_level=record.get('corner_level'),
        parent_time_ms=record.get('time_ms'),
        admitted=len(admitted), branched=len(found), measured=len(rows),
        dropped=dropped, rows=rows,
        root_speed_spread=(max(speeds) - min(speeds)) if speeds else None,
        parent_root_speed=float(parent_commit['speed']) if 'speed' in parent_commit else None,
        branch_root_speed=speeds[0] if speeds else None,
        distinct_times=len(set(times)),
        time_histogram=dict(sorted(Counter(times).items())),
        spearman=spearman([r['judge_rank'] for r in rows], times),
        spearman_n=len(rows),
        ties=tie_report(rows))


def tie_report(rows):
    """For every repeated completed time, whether its branches are one decision.

    Two admitted candidates landing in the same cells with the same kind, hop
    count and run-ups are the same decision under the decision tape's identity
    rule, so a repeated time from such a pair is a duplicate. The converse is
    reported too: the same shape and the same cells with *different* times says
    the identity rule groups branches the trip does not.
    """
    buckets, shapes = {}, {}
    for row in rows:
        buckets.setdefault(row['time_ms'], []).append(row)
        shapes.setdefault((row['kind'], row['hops'], row['r0'], row['r1'],
                           row['cell'], row['cell1']), []).append(row)
    out = []
    for time_ms, group in sorted(buckets.items()):
        if len(group) < 2:
            continue
        kinds = {(r['kind'], r['hops'], r['r0'], r['r1'], r['cell'], r['cell1'])
                 for r in group}
        out.append(dict(time_ms=time_ms, branches=len(group),
                        distinct_decisions=len(kinds),
                        duplicates_of_one_decision=len(kinds) == 1,
                        candidates=[r['cand'] for r in group]))
    split = []
    for shape, group in sorted(shapes.items(), key=lambda kv: kv[1][0]['cand']):
        found = sorted({r['time_ms'] for r in group})
        if len(group) > 1 and len(found) > 1:
            split.append(dict(kind=shape[0], hops=shape[1], r0=shape[2], r1=shape[3],
                              cells=[shape[4], shape[5]],
                              candidates=[r['cand'] for r in group], times=found))
    return dict(repeated_times=out, one_identity_many_times=split)


def winner_rank(table, winner_cand):
    for row in table['rows']:
        if row['cand'] == winner_cand:
            return dict(cand=winner_cand, judge_rank=row.get('judge_rank'),
                        time_rank=row.get('time_rank'), time_ms=row.get('time_ms'),
                        score=row.get('score'))
    return None


# ---------------------------------------------------------------------------
# The report


def report(progress, best, depths):
    records = read_progress(progress)
    chain = lineage(records, best)
    # Only a movement branch takes a decision. A route root is a parent link
    # that re-plans the release route and advances no decision number, so the
    # lineage is indexed by the branches' own decision numbers, not by position.
    taken = {}
    for parent, child in zip(chain, chain[1:]):
        branch = records[child].get('branch')
        if branch and branch.get('nth') is not None:
            taken.setdefault(branch['nth'], (parent, child))
    tables = []
    for depth in depths:
        # A decision of the winning lineage is one the lineage actually took,
        # so the tables stop where the chain does rather than branching a world
        # the winner never came from.
        if depth not in taken:
            break
        parent, winning_child = taken[depth]
        table = decision_table(records, parent, depth)
        cand = (records[winning_child].get('branch') or {}).get('cand')
        table['winning_child'] = winning_child
        table['winner'] = winner_rank(table, cand) if cand is not None else None
        tables.append(table)
    return dict(format='DECISION_TREE1', progress=str(Path(progress).resolve()),
                best=best, lineage=chain, decisions=tables)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('progress', type=Path, help="a campaign's progress.jsonl")
    parser.add_argument('--best', required=True, help='the winning world name')
    parser.add_argument('--depths', type=int, default=3,
                        help='how many decisions of the lineage to table (default 3)')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    result = report(args.progress, args.best, range(1, args.depths + 1))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    for table in result['decisions']:
        print('decision %d at level %d of %s: admitted=%d branched=%d measured=%d '
              'distinct_times=%d spearman=%s (n=%d) winner=%s'
              % (table['depth'], table['level'], table['parent'], table['admitted'],
                 table['branched'], table['measured'], table['distinct_times'],
                 'n/a' if table['spearman'] is None else round(table['spearman'], 4),
                 table['spearman_n'], json.dumps(table['winner'])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
