"""Phase 1 spike report: which dimensions decided whether the second hop was reached.

Reads every ``[SPIKE]`` candidate line under an evidence directory and prints,
per plan origin, the outcome split along each candidate and entry dimension,
plus the launch and landing states of admitted sequences. Pure text in, text
out; no worlds are started.
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

TOKEN = re.compile(r'(\w+)=(\S+)')


def spike_lines(root):
    for log in sorted(Path(root).glob('*/server.log')):
        for line in log.read_text(errors='replace').splitlines():
            if '[SPIKE]' not in line:
                continue
            fields = dict(TOKEN.findall(line[line.index('[SPIKE]'):]))
            fields['world'] = log.parent.name
            yield fields


def split(rows, key):
    table = defaultdict(Counter)
    for r in rows:
        table[r[key]][r['outcome']] += 1
    return table


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    rows = list(spike_lines(root))
    if not rows:
        print('no [SPIKE] lines under', root)
        return
    by_plan = defaultdict(list)
    for r in rows:
        by_plan[(r['world'], r['plan'])].append(r)
    print(f'{len(rows)} candidates over {len(by_plan)} plans in {len({r["world"] for r in rows})} worlds')
    # Group plans by where they were made: the lane starts share x=..., y=1536; spawn plans differ.
    for (world, plan), group in sorted(by_plan.items()):
        outcomes = Counter(r['outcome'] for r in group)
        print(f'\n== {world} plan {plan}: start_speed={group[0]["start_speed"]} start_err={group[0]["start_err"]} '
              f'outcomes={dict(outcomes)}')
        for key in ('d0', 'r0', 'd1', 'r1'):
            table = split(group, key)
            cells = ', '.join(f'{k}: ' + ' '.join(f'{o}={n}' for o, n in sorted(v.items())) for k, v in sorted(table.items(), key=lambda kv: float(kv[0])))
            print(f'   by {key}: {cells}')
        admitted = [r for r in group if r['outcome'] == 'admitted']
        if admitted:
            frames = sorted(int(r['done']) for r in admitted)
            l0 = sorted(float(r['l0_speed']) for r in admitted)
            a0 = sorted(float(r['a0_speed']) for r in admitted)
            a1 = sorted(float(r['a1_speed']) for r in admitted)
            e0 = sorted(float(r['l0_err']) for r in admitted)
            e1 = sorted(float(r['l1_err']) for r in admitted)
            print(f'   admitted: done frames {frames[0]}..{frames[-1]}, launch0 speed {l0[0]:.0f}..{l0[-1]:.0f}, '
                  f'land0 speed {a0[0]:.0f}..{a0[-1]:.0f}, land1 speed {a1[0]:.0f}..{a1[-1]:.0f}, '
                  f'launch heading error {e0[0]:.0f}..{e0[-1]:.0f} / {e1[0]:.0f}..{e1[-1]:.0f} deg')
        for outcome in ('fell', 'refused', 'miss', 'no_progress', 'no_second_landing', 'no_first_landing'):
            bad = [r for r in group if r['outcome'] == outcome]
            if bad:
                dims = Counter((r['d0'], r['r0'], r['d1'], r['r1']) for r in bad)
                print(f'   {outcome}: {len(bad)} candidates; (d0,r0,d1,r1) ' + ' '.join(f'{k}x{n}' for k, n in dims.most_common(6)) +
                      (f'; unsupported bits ' + ' '.join(sorted({r["unsupported"] for r in bad})) if outcome == 'refused' else ''))


if __name__ == '__main__':
    main()
