"""Write a map's planner-guidance goal list (bot_movePlanGuide) from earlier worlds.

    python tools/movement_guide_goals.py --seed <graph seed .txt> --out <file.guide> WORLD [WORLD ...]
        [--min-ms 4000] [--cover 0.95] [--max 96]

The state search's long searches (straight-line estimate at least --min-ms) are ranked by the time they took,
summed by goal cell, from the worlds' [PLANGUIDE] lines (bot_movePlanGuide 2 prints one per search; the straight
line's time is `us`); goals are taken in that order until they cover --cover of that time or --max goals. Each goal
is written as "g x y z", the cell's position from the seed's cell records (in index order, as the worlds loaded it),
so the list survives a graph whose cell numbering changes. The game builds these goals' tables at the map's load and
keeps them (bot2_graph.c, GuidePreload); play_vs_runner.stage copies tools/fixtures/movement/guide/<seed name>.guide
beside the staged seed. Localhost evidence only; nothing is deleted.
"""
import argparse
import collections
import re
from pathlib import Path

PG = re.compile(r'\[PLANGUIDE\] (.*)')
GRID = 32


def cells(seed):
    out = []
    for line in seed.read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('c '):
            f = line.split()
            out.append((int(f[1]) * GRID, int(f[2]) * GRID, float(f[3])))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--seed', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--min-ms', type=float, default=4000)
    ap.add_argument('--cover', type=float, default=0.95)
    ap.add_argument('--max', type=int, default=96)
    ap.add_argument('worlds', type=Path, nargs='+')
    args = ap.parse_args()
    where = cells(args.seed)
    time = collections.Counter()
    searches = collections.Counter()
    for world in args.worlds:
        console = world / 'live' / 'server-console.log'
        for line in console.read_text(errors='replace').splitlines():
            m = PG.search(line)
            if not m:
                continue
            r = dict(kv.split('=', 1) for kv in m[1].split())
            if float(r.get('h_line', -1)) < args.min_ms:
                continue
            time[int(r['to'])] += float(r['us'])
            searches[int(r['to'])] += 1
    total = sum(time.values())
    if not total:
        raise SystemExit('no long searches in those worlds (were they run with bot_movePlanGuide 2?)')
    lines, covered = [], 0.0
    for goal, us in time.most_common():
        if covered >= args.cover * total or len(lines) >= args.max:
            break
        if goal < 0 or goal >= len(where):
            raise SystemExit('goal cell %d is not in the seed (%d cells): wrong seed for these worlds?' % (goal, len(where)))
        x, y, z = where[goal]
        lines.append('g %d %d %.3f' % (x, y, z))
        covered += us
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print('%s: %d goals of %d, covering %.1f%% of %.1f s of long searches in %d worlds'
          % (args.out, len(lines), len(time), 100 * covered / total, total / 1e6, len(args.worlds)))


if __name__ == '__main__':
    main()
