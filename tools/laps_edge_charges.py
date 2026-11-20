"""What each strafe-edge avoid charged to the shared graph, by its reason, in laps worlds.

`StateAvoid` (bot2_move.c) prints one `[MOVE] ... reason=avoid index=E why=W cause=C` line per avoid, and when it
charges the shared edge `Graph_EdgeFail` prints `[GRAPH] edge_result index=E ... success=0 failures=F disabled=D
cause=C off_ms=M` on the line before it (a transient cause prints `edge_fail_uncharged` there instead; a guest or an
avoid that charges nothing prints no graph line). This pairs the two and counts, per `why`:

  avoids     avoid events
  charged    of those, the ones that wrote a failure to the shared edge
  off        of those, the ones that turned a validated edge off for a while (off_ms > 0)
  disabled   of those, the ones that disabled the edge for the map (disabled=1)

usage: python tools/laps_edge_charges.py <world or server-console.log> ...   (a world is a defense_laps --out folder)
       --csv prints one row per world and reason instead of the table
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

GRAPH = re.compile(r'\[GRAPH\] edge_result index=(\d+) type=(\w+) success=0 .*failures=(\d+) disabled=(\d+) '
                   r'cause=(\w+) off_ms=(\d+)')
AVOID = re.compile(r'\[MOVE\] client=(\d+) level=(\d+) event=edge edge=strafe reason=avoid .*index=(\d+) why=(\S+) '
                   r'avoided=\d+ cause=(\w+)(?: charged=(\d))?')
COLUMNS = ('avoids', 'charged', 'off', 'disabled', 'transient', 'uncharged')


def console(path):
    path = Path(path)
    if path.is_file():
        return path
    for candidate in (path / 'live' / 'server-console.log', path / 'server-console.log'):
        if candidate.is_file():
            return candidate
    raise SystemExit('no server-console.log under %s' % path)


def count(path):
    """{why: {column: n}} for one console log. A miss reason (jump_refused, no_ledge...) is kept under its own name."""
    out = defaultdict(lambda: dict.fromkeys(COLUMNS, 0))
    last = None   # the graph failure line directly before, if any
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            g = GRAPH.search(line)
            if g:
                last = g
                continue
            a = AVOID.search(line)
            if a:
                row = out[a.group(4)]
                row['avoids'] += 1
                if last and last.group(1) == a.group(3):
                    row['charged'] += 1
                    row['off'] += int(last.group(6)) > 0
                    row['disabled'] += last.group(4) == '1'
                elif a.group(5) == 'transient':
                    row['transient'] += 1
                else:
                    row['uncharged'] += 1   # a guest, or (bot_moveChargeFlown 1) an edge never flown
            if '[GRAPH] edge_fail_uncharged' not in line:
                last = None
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('worlds', nargs='+')
    ap.add_argument('--csv', action='store_true')
    args = ap.parse_args()
    total = defaultdict(lambda: dict.fromkeys(COLUMNS, 0))
    if args.csv:
        print('world,why,' + ','.join(COLUMNS))
    for world in args.worlds:
        rows = count(console(world))
        for why, row in sorted(rows.items()):
            if args.csv:
                print('%s,%s,%s' % (Path(world).name, why, ','.join(str(row[c]) for c in COLUMNS)))
            for c in COLUMNS:
                total[why][c] += row[c]
    if not args.csv:
        n = len(args.worlds)
        print('%d world%s; totals, and (a world)' % (n, '' if n == 1 else 's'))
        print('%-14s' % 'why' + ''.join('%18s' % c for c in COLUMNS))
        for why, row in sorted(total.items()):
            print('%-14s' % why + ''.join('%18s' % ('%d (%.1f)' % (row[c], row[c] / n)) for c in COLUMNS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
