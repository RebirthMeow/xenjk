"""Paired work counts and hop flow for two arms of seeded laps worlds (defense_laps.py --seed N, bot_moveFrameMeter 1).

    python tools/laps_work.py <control prefix> <test prefix> [--seeds 3-18] [--bounds]

A world is <prefix><seed> (for example worlds/ctl-kej- and worlds/a-kej-). Counts replay with the world whatever the
machine was doing, so the worlds may run four at once (records/2026-10-06-frame-cost.md, "a work meter"); nothing here
is a clock. Reads each world's last [FRAMEWORK] line (its summary), its [FRAMEEXP], plan, [PLANLONG] and [PLANBOUND]
lines, the runners' hop launches and their speed in the play CSV; prints each per world for both arms and the paired
difference with its standard error. --bounds adds what each bound of the [PLANBOUND] check lines would have done
(records/2026-11-09-replan-budget.md).
"""
import argparse
import csv
import json
import math
import re
from pathlib import Path

KV = re.compile(r'(\w+)=(\S+)')
CHAINED_MS = 100    # a chained hop: under 100 ms on the ground before it
PCTS = (100, 125, 150, 200, 300)

ROWS = [('frames', 'frames'), ('est_over25', 'est. frames over 25 ms'), ('est_p99_us', 'est. p99 us'),
        ('est_p999_us', 'est. p99.9 us'), ('est_max_us', 'est. worst frame us'),
        ('expand_p99', 'expansions a frame p99'), ('expand_p999', 'expansions a frame p99.9'),
        ('expand_max', 'expansions, worst frame'), ('expand_sum', 'expansions, world total'),
        ('steps_p99', 'steps a frame p99'), ('steps_sum', 'steps, world total'),
        ('frames10k', 'frames of 10,000 expansions or more'), ('frames20k', '... 20,000 or more'),
        ('frames30k', '... 30,000 or more'), ('replans', 'replans'), ('replan_exp', 'replan state expansions'),
        ('replan10k', 'replans of 10,000 or more'), ('replan25k', 'replans of 25,000 or more'),
        ('replan_max', 'largest replan'), ('fallbacks', 'replans the cell route answered after a state search'),
        ('fallback_exp', '... their state expansions'), ('guide_pops', 'guide table cells settled'),
        ('guide_pops_max', '... in the largest frame'), ('fold_pops', '... by folds'),
        ('fold_pops_max', '... by folds in the largest frame'), ('fold_deferred', 'folds put off (bot_movePlanGuideFoldCap)'),
        ('longs', 'escalations (bot_movePlanGuideLong)'), ('long_builds', '... tables built'),
        ('long_discarded', '... expansions discarded'), ('bounds', 'bounds taken (bot_movePlanBound or its check)'),
        ('bound_stopped', '... searches stopped'), ('traces', 'forecast traces'),
        ('engine_traces', 'traces run by the engine'), ('corridors', 'corridor evaluations run'),
        ('hops', 'runner hops'), ('chained', 'chained hops'), ('speed', 'runner speed u/s'),
        ('airborne', 'airborne share')]


def world(path):
    """One world's counts, or None when it has no summary."""
    path = Path(path)
    if not (path / 'summary.json').exists():
        return None
    base = path / 'live' / 'server' / 'base'
    names = {}
    for line in open(base / 'play-game.log', encoding='utf-8', errors='replace'):
        m = re.search(r'ClientConnect: (\d+) \[Bot\] \(BOT\) "([^"^]+)', line)
        if m:
            names[int(m.group(1))] = m.group(2)
    runners = {c for c, n in names.items() if n.startswith('Runner')}
    w = dict(frames10k=0, frames20k=0, frames30k=0, replans=0, replan_exp=0, replan10k=0, replan25k=0, replan_max=0,
             fallbacks=0, fallback_exp=0, longs=0, long_discarded=0, long_builds=0, bounds=0, bound_stopped=0,
             hops=0, chained=0)
    last = None
    land, up, air, bound_rows = {}, {}, 0, []
    for line in open(path / 'live' / 'server-console.log', encoding='utf-8', errors='replace'):
        if '[FRAMEWORK]' in line:
            last = line
        elif '[FRAMEEXP]' in line:
            e = int(dict(KV.findall(line))['expand'])
            w['frames10k'] += 1
            w['frames20k'] += e >= 20000
            w['frames30k'] += e >= 30000
        elif '[PLANLONG]' in line:
            kv = dict(KV.findall(line))
            w['longs'] += 1
            w['long_discarded'] += int(kv['discarded'])
            w['long_builds'] += int(kv['builds'])
        elif '[PLANBOUND]' in line:
            kv = {k: int(v) for k, v in KV.findall(line)}
            w['bounds'] += 1
            w['bound_stopped'] += kv['stopped']
            bound_rows.append(kv)
        elif '[MOVE]' in line:
            if 'event=plan' in line and 'expanded=' in line:
                kv = dict(KV.findall(line))
                e = int(kv['expanded'])
                w['replans'] += 1
                w['replan_exp'] += e
                w['replan10k'] += e >= 10000
                w['replan25k'] += e >= 25000
                w['replan_max'] = max(w['replan_max'], e)
                if kv.get('state') == '0' and e > 0:
                    w['fallbacks'] += 1
                    w['fallback_exp'] += e
            elif 'event=edge edge=hop' in line and ('reason=launch' in line or 'reason=land' in line):
                kv = dict(KV.findall(line))
                c = int(kv['client'])
                if c not in runners:
                    continue
                level = int(kv['level'])
                if kv['reason'] == 'land':
                    if c in up:
                        air += level - up.pop(c)
                    land[c] = level
                else:
                    w['hops'] += 1
                    if c in land and level - land[c] < CHAINED_MS:
                        w['chained'] += 1
                    up[c] = level
    if last:
        for k, v in KV.findall(last):
            try:
                w[k] = float(v)
            except ValueError:
                pass
    speeds, first, final = [], None, None
    for f in sorted((base / 'bot2-play').glob('*.csv')):
        with open(f, encoding='utf-8', newline='') as fh:
            for r in csv.DictReader(fh):
                final = int(r['level_time'])
                first = final if first is None else first
                if int(r['client']) in runners:
                    speeds.append(math.hypot(float(r['vx']), float(r['vy'])))
    w['speed'] = sum(speeds) / len(speeds) if speeds else float('nan')
    w['airborne'] = air / (max(1, len(runners)) * (final - first)) if speeds and final > first else float('nan')
    w['late'] = json.loads((path / 'summary.json').read_text()).get('setup_late_commands', 0) or 0
    w['bound_rows'] = bound_rows
    return w


def stat(xs):
    n = len(xs)
    m = sum(xs) / n
    se = math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1) / n) if n > 1 else float('nan')
    return m, se


def bound_table(worlds):
    """What stopping at each percent of the cell route's cost would have done, from check lines (the search ran
    whole): searches stopped, expansions saved, and state routes lost (found by the whole search after the stop)."""
    rows = [r for w in worlds for r in w['bound_rows'] if r.get('check')]
    if not rows:
        return
    n = len(worlds)
    avoided = [r for r in rows if r.get('avoided')]
    if avoided:
        print('  bound check: %d cell routes through an avoided edge (no bound), %d expansions' % (
            len(avoided), sum(r['expanded'] for r in avoided)))
    rows = [r for r in rows if not r.get('avoided')]
    print('  bound check: %d long searches over %d worlds (%.1f a world), %d found no state route (%d expansions)' % (
        len(rows), n, len(rows) / n, sum(1 for r in rows if not r['count']),
        sum(r['expanded'] for r in rows if not r['count'])))
    for guided in (0, 1):
        sub = [r for r in rows if r['guided'] == guided]
        if not sub:
            continue
        print('    %s searches: %d, expansions %d' % ('guided' if guided else 'unguided', len(sub),
                                                      sum(r['expanded'] for r in sub)))
        for pct in PCTS:
            cut = [r for r in sub if r['at%d' % pct] >= 0]
            lost = [r for r in cut if r['count']]
            saved = sum(r['expanded'] - r['at%d' % pct] for r in cut)
            dear = sorted(100 * r['cost'] // max(1, r['cell_cost']) for r in lost)
            print('      at %3d%%: stops %4d (%.1f a world), saves %8d expansions (%.0f a world), loses %3d state routes'
                  '%s' % (pct, len(cut), len(cut) / n, saved, saved / n, len(lost),
                          (' (their cost %d-%d%% of the cell route, median %d%%)' % (dear[0], dear[-1], dear[len(dear) // 2]))
                          if dear else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('control')
    ap.add_argument('test')
    ap.add_argument('--seeds', default='1-99')
    ap.add_argument('--bounds', action='store_true')
    args = ap.parse_args()
    a, b = args.seeds.split('-')
    seeds = []
    for seed in range(int(a), int(b) + 1):
        c, t = world(args.control + str(seed)), world(args.test + str(seed))
        if c and t:
            seeds.append((seed, c, t))
    if not seeds:
        raise SystemExit('no paired worlds')
    print('%s -> %s, %d paired seeds (%s)' % (args.control, args.test, len(seeds), ','.join(str(s[0]) for s in seeds)))
    late = [(s, c['late'], t['late']) for s, c, t in seeds if c['late'] or t['late']]
    if late:
        print('  setup_late_commands > 0 (not replays of their seed): %s' % late)
    print('  %-52s %12s %12s   %s' % ('per world', 'control', 'test', 'paired diff +- se'))
    for key, label in ROWS:
        if not all(key in c and key in t for _, c, t in seeds):
            continue
        cm = stat([c[key] for _, c, t in seeds])[0]
        tm = stat([t[key] for _, c, t in seeds])[0]
        dm, ds = stat([t[key] - c[key] for _, c, t in seeds])
        pct = ' (%+.1f%%)' % (100 * dm / cm) if cm else ''
        fmt = '%12.3f' if abs(cm) < 10 else '%12.1f'
        print(('  %-52s ' + fmt + ' ' + fmt + '   %+.3f +- %.3f%s') % (label, cm, tm, dm, ds, pct))
    if args.bounds:
        for name, k in (('control', 1), ('test', 2)):
            print('  %s arm:' % name)
            bound_table([s[k] for s in seeds])


if __name__ == '__main__':
    main()
