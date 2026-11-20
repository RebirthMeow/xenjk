"""Compare a movement benchmark campaign with named arms, in one table.

Reads a campaign directory written by ``movement_bench.py`` (its
``result.json`` and every world's ``server.log``) and prints the standard
comparison: verdict, parity, audit and owners; route time by start family
against each named arm and the snapshot baseline; plans by kind, landing
commits and arrivals; search cost; candidate outcomes; live launch and
landing speeds. Pure helpers (``summarize``, ``deltas``) take parsed events
so they can be tested without a campaign on disk.

    python tools/movement_campaign_compare.py .codex-research/<run> \\
        --against prune-b=.codex-research/move-phase2-20260916-prune-b [--cases]
"""
import argparse
import json
from pathlib import Path
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_bench_lib import parse_move_events  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / '.codex-research' / 'move-phase0-20260915-baseline-a'
FAMILIES = ('stand', 'respawn5', 'respawn4')
SPIKE_LINE = re.compile(r'(?:^|\s)\[SPIKE\] (?P<body>client=\d+ .*)$', re.M)


def parse_spike_lines(text):
    """Every ``[SPIKE] key=value ...`` candidate line as a dict of strings."""
    lines = []
    for match in SPIKE_LINE.finditer(text):
        fields = {}
        for token in match.group('body').split():
            key, sep, value = token.partition('=')
            if sep:
                fields[key] = value
        lines.append(fields)
    return lines


def load_campaign(directory):
    """The campaign's cases by name, from its result.json."""
    directory = Path(directory)
    doc = json.loads((directory / 'result.json').read_text())
    return {c['name']: c for c in doc['cases']}


def case_time(case):
    """Display completion times only; keep failed and incomplete cases visible."""
    if case.get('status') != 'pass':
        return f"{case.get('status', 'unknown')}:{case.get('reason', 'unknown')}"
    return fmt(case.get('time_ms'))


def family_means(cases, names=None):
    names = list(names or cases)
    out = {}
    for family in FAMILIES:
        times = [cases[n]['time_ms'] for n in names if family in n and
                 cases[n].get('status') == 'pass' and cases[n].get('time_ms') is not None]
        out[family] = statistics.mean(times) if times else None
    return out


def deltas(run, other):
    """Completion-time differences over shared passes, never time to death."""
    shared = [n for n in run if n in other and run[n].get('status') == 'pass' and
              other[n].get('status') == 'pass' and run[n].get('time_ms') is not None and
              other[n].get('time_ms') is not None]
    d = [run[n]['time_ms'] - other[n]['time_ms'] for n in shared]
    if not d:
        return dict(cases=0)
    return dict(cases=len(d), mean=statistics.mean(d), min=min(d), max=max(d), faster=sum(x < 0 for x in d),
                identical=sum(x == 0 for x in d))


def summarize(events, spikes):
    """Plan, search, candidate and speed counts from one world's events."""
    s = dict(plans={}, straight_commits=0, at_landing=0, reissued=0, arrives=0, misses=0, searches=0,
             speed_purchases=0, brakes=0, search_ms=[], outcomes={}, launch=[], launch1=[], land=[],
             state_commits=0, state_failed=0, state_ms=[], paths_state=0, paths_cell=0, strafe_planned=0,
             learned=0, merged=0, plan_ms=[])
    for e in events:
        event, reason = e.get('event'), e.get('reason')
        if event == 'edge' and e.get('edge') == 'speed':
            s['speed_purchases'] += reason == 'complete'
        elif event == 'edge' and e.get('edge') == 'strafe' and reason == 'learn':
            s['learned'] += 1
            s['merged'] += int(e.get('merged', 0))
        elif event == 'walk' and reason == 'brake':
            s['brakes'] += 1
        elif event == 'plan' and reason == 'path':
            s['paths_state' if int(e.get('state', 0)) else 'paths_cell'] += 1
            s['strafe_planned'] += int(e.get('strafe', 0))
            s['plan_ms'].append(float(e.get('elapsed_ms', 0)))
        elif event == 'plan' and reason == 'sequence':
            kind = e.get('kind', 'none')
            key = kind + (':' + e['hops'] if 'hops' in e else '')
            s['plans'][key] = s['plans'].get(key, 0) + 1
            if kind in ('straight', 'state'):
                s['straight_commits'] += kind == 'straight'
                s['state_commits'] += kind == 'state'
                s['at_landing'] += int(e.get('landing', 0))
                s['reissued'] += int(e.get('reissued_up', 0))
                s['arrives'] += int(e.get('arrives', 0))
                s['search_ms'].append(float(e.get('rollout_ms', 0)))
        elif event == 'plan' and reason == 'none' and e.get('kind') == 'straight' and 'rollout_ms' in e:
            s['search_ms'].append(float(e['rollout_ms']))
        elif event == 'edge' and e.get('edge') == 'strafe' and reason == 'refused':
            s['state_failed'] += 1
        elif event == 'miss':
            s['misses'] += 1
        elif event == 'edge' and reason in ('launch', 'land'):
            if reason == 'land':
                s['land'].append(float(e.get('speed', 0)))
            elif e.get('edge') == 'hop0':
                s['launch'].append(float(e.get('speed', 0)))
            else:
                s['launch1'].append(float(e.get('speed', 0)))
    for c in spikes:
        if c.get('kind') != 'straight':
            continue
        if c.get('cand') == '0':
            s['searches'] += 1
        outcome = c.get('outcome', '?')
        s['outcomes'][outcome] = s['outcomes'].get(outcome, 0) + 1
    return s


def merge(summaries):
    total = dict(plans={}, straight_commits=0, at_landing=0, reissued=0, arrives=0, misses=0, searches=0,
                 speed_purchases=0, brakes=0, search_ms=[], outcomes={}, launch=[], launch1=[], land=[],
                 state_commits=0, state_failed=0, state_ms=[], paths_state=0, paths_cell=0, strafe_planned=0,
                 learned=0, merged=0, plan_ms=[])
    for s in summaries:
        for key in ('straight_commits', 'at_landing', 'reissued', 'arrives', 'misses', 'searches', 'speed_purchases', 'brakes',
                    'state_commits', 'state_failed', 'paths_state', 'paths_cell', 'strafe_planned', 'learned', 'merged'):
            total[key] += s[key]
        for key in ('search_ms', 'launch', 'launch1', 'land', 'state_ms', 'plan_ms'):
            total[key] += s[key]
        for key in ('plans', 'outcomes'):
            for k, v in s[key].items():
                total[key][k] = total[key].get(k, 0) + v
    return total


def read_worlds(directory, cases):
    summaries = []
    for name in cases:
        log = Path(directory) / name / 'server.log'
        if not log.is_file():
            continue
        text = log.read_text(errors='replace')
        after = None
        try:
            after = json.loads((Path(directory) / name / 'result.json').read_text()).get('corner_level')
        except (OSError, ValueError):
            pass
        summaries.append(summarize(parse_move_events(text, after), parse_spike_lines(text)))
    return merge(summaries)


def fmt(value, digits=0):
    if value is None:
        return '-'
    return f'{value:.{digits}f}'


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def report(run_dir, arms, show_cases):
    run = load_campaign(run_dir)
    names = list(run)
    statuses = [c['status'] for c in run.values()]
    print(f'{Path(run_dir).name}: {statuses.count("pass")} of {len(run)} pass, '
          f'parity mismatches {sum(c.get("parity", {}).get("mismatches", 0) for c in run.values())}, '
          f'judged short {sum(len(c.get("straight", {}).get("short", [])) for c in run.values())}, '
          f'owners {sorted({o for c in run.values() for o in c.get("owners", [])})}')
    failed = [n for n in names if run[n]['status'] != 'pass']
    if failed:
        print('  not passed: ' + ', '.join(f'{n} ({run[n]["reason"]})' for n in failed))
    print('\nroute time by family (ms, mean of passes only; failures listed above)')
    header = ['family', Path(run_dir).name] + [label for label, _ in arms]
    rows = []
    means = {label: family_means(cases, names) for label, cases in arms}
    own = family_means(run, names)
    for family in FAMILIES:
        rows.append([family, fmt(own[family])] + [fmt(means[label][family]) for label, _ in arms])
    width = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    for r in [header] + rows:
        print('  ' + '  '.join(str(v).ljust(width[i]) for i, v in enumerate(r)))
    print('\nagainst each arm (run minus arm, ms; shared passes only)')
    for label, cases in arms:
        d = deltas(run, cases)
        if d['cases']:
            print(f'  {label}: mean {d["mean"]:+.0f}  min {d["min"]:+}  max {d["max"]:+}  faster {d["faster"]}/{d["cases"]}  identical {d["identical"]}')
        else:
            print(f'  {label}: no shared cases')
    s = read_worlds(run_dir, names)
    print('\nplans ' + ', '.join(f'{k} {v}' for k, v in sorted(s['plans'].items())) +
          f'; straight commits {s["straight_commits"]}, at landing {s["at_landing"]}, re-issued taps {s["reissued"]}, '
          f'arrivals {s["arrives"]}, misses {s["misses"]}, Speed purchases {s["speed_purchases"]}, momentum brakes {s["brakes"]}')
    if s['search_ms']:
        print(f'searches {s["searches"]}: cost mean {statistics.mean(s["search_ms"]):.1f} ms, '
              f'p90 {percentile(s["search_ms"], .9):.1f}, max {max(s["search_ms"]):.1f}')
    if s['paths_state'] or s['paths_cell']:
        print(f'paths: state {s["paths_state"]}, cell {s["paths_cell"]}, strafe edges planned {s["strafe_planned"]}'
              + (f', search mean {statistics.mean(s["plan_ms"]):.1f} ms max {max(s["plan_ms"]):.0f}' if s['plan_ms'] else ''))
    if s['state_commits'] or s['state_failed'] or s['learned']:
        print(f'state plans: commits {s["state_commits"]}, planned edges refused {s["state_failed"]}, '
              f'strafe edges learned {s["learned"]} (merged {s["merged"]})')
    if s['outcomes']:
        print('candidate outcomes ' + ', '.join(f'{k} {v}' for k, v in sorted(s['outcomes'].items(), key=lambda kv: -kv[1])))
    if s['launch']:
        print(f'live speeds: first launch mean {statistics.mean(s["launch"]):.0f}'
              + (f', second launch mean {statistics.mean(s["launch1"]):.0f}' if s['launch1'] else '')
              + (f', landing mean {statistics.mean(s["land"]):.0f} max {max(s["land"]):.0f}' if s['land'] else ''))
    if show_cases:
        print('\ncase | ' + ' | '.join([Path(run_dir).name] + [label for label, _ in arms]))
        for n in names:
            print('  ' + n + ' | ' + ' | '.join([case_time(run[n])] +
                                                [case_time(cases[n]) if n in cases else '-' for _, cases in arms]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('--against', action='append', default=[], metavar='NAME=DIR',
                        help='an arm to compare with; may repeat')
    parser.add_argument('--baseline', type=Path, default=BASELINE, help='the snapshot baseline campaign')
    parser.add_argument('--no-baseline', action='store_true')
    parser.add_argument('--cases', action='store_true', help='print every case')
    args = parser.parse_args()
    arms = []
    for item in args.against:
        label, sep, path = item.partition('=')
        if not sep:
            path, label = label, Path(label).name
        arms.append((label, load_campaign(path)))
    if not args.no_baseline and (args.baseline / 'result.json').is_file():
        arms.append(('baseline', load_campaign(args.baseline)))
    report(args.run, arms, args.cases)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
