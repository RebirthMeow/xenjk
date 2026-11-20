"""Runner falls and committed-landing error in retained movement worlds.

Reads retained worlds only (a laps world from `tools/play_vs_runner.py
--no-client`, `.codex-research/play-vs-runner-<stamp>`, or any bench world
directory). Writes nothing. Records: docs/movement/records/2026-09-28-laps-falls.md.

    python tools/movement_laps_falls.py falls .codex-research/play-vs-runner-20260928T074130Z
    python tools/movement_laps_falls.py landings <world or campaign dir> [...]

`falls`: every runner death below Kejim's lethal floor (z 704) with the flight
that put it there: a pad throw (the jumppad state's locked target) or the last
committed hop sequence, its Force and Speed at commit and at the fatal launch,
and the Speed purchases between the commit and the pit.

`landings`: the committed plans' predicted landing cells (`c0`/`c1` on the
commit line) against the live `land` events, split by whether Speed was bought
between the commit and that landing. A landing in the pit counts 1,000 u.
Cell positions come from the Kejim production seed, so only Kejim worlds are
meaningful.
"""
import argparse
import csv
import glob
import math
import re
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / '.codex-research/move-explore-20260916-b/seed.txt'
PIT_Z = 704.0
MOVE = re.compile(r'\[MOVE\] client=(\d+) level=(\d+) event=(\w+) edge=(\w+) reason=(\w+)(.*)')


def kv(text, key):
    match = re.search(r'\b' + key + r'=([^ ]+)', text)
    return match.group(1) if match else None


def logs(paths):
    for path in paths:
        path = Path(path)
        yield from sorted(path.glob('**/server.log'))
        yield from sorted(path.glob('**/server-console.log'))


def falls(world):
    world = Path(world)
    log = next(iter(logs([world])))
    play = sorted(world.glob('**/bot2-play/*.csv'))[0]
    rows = list(csv.DictReader(open(play, encoding='utf-8', errors='replace')))
    client = next(r['client'] for r in rows if r['movement_only'] == '1')
    run = [r for r in rows if r['client'] == client]
    events = []
    for line in open(log, errors='replace'):
        match = MOVE.search(line)
        if match and match.group(1) == client:
            events.append((int(match.group(2)), ' '.join(match.groups()[2:5]) + match.group(6)))
        elif 'Entered Jump Pad' in line and '[Runner_v2]' in line and events:
            events.append((events[-1][0], 'pad ' + line[line.find('Locked'):].strip()))
    previous = 0
    for before, after in zip(run, run[1:]):
        if not (int(before['health']) > 0 >= int(after['health'])):
            continue
        died = int(after['level_time'])
        life = [r for r in run if previous < int(r['level_time']) <= died and int(r['health']) > 0]
        previous = died
        pit = next((int(r['level_time']) for r in life if float(r['z']) < PIT_Z), None)
        if pit is None:
            continue
        launch = next((e for e in reversed(events) if e[0] < pit and 'launch' in e[1]
                       and kv(e[1], 'z') and float(kv(e[1], 'z')) > 1000), None)
        pad = next((e for e in reversed(events) if e[0] < pit and e[1].startswith('pad')), None)
        if pad and (not launch or pad[0] >= launch[0] - 500):
            print(f'{world.name} died {died / 1000:.1f}s pit {pit / 1000:.1f}s PAD {pad[1]}')
            continue
        commit = next((e for e in reversed(events) if launch and e[0] <= launch[0] and 'sequence' in e[1]), None)
        text = commit[1] if commit else ''
        bought = [e[0] for e in events if commit and commit[0] <= e[0] <= pit and 'speed request' in e[1]]
        print(f'{world.name} died {died / 1000:.1f}s pit {pit / 1000:.1f}s HOP commit@{commit[0] if commit else None} '
              f'kind={kv(text, "kind")} id={kv(text, "id")} hops={kv(text, "hops")} force={kv(text, "force")} '
              f'speed_ms commit={kv(text, "speed_ms")} launch={kv(launch[1], "speed_ms") if launch else None} '
              f'speed_bought_after_commit={bought}')


def landings(paths):
    cells = [(int(p[1]) * 32, int(p[2]) * 32, float(p[3]))
             for p in (line.split() for line in open(SEED) if line.startswith('c '))]
    errors = {False: [], True: []}
    for log in logs(paths):
        committed = {}
        for line in open(log, errors='replace'):
            match = MOVE.search(line)
            if not match:
                continue
            client, _, event, edge, reason, rest = match.groups()
            if event == 'plan' and reason == 'sequence' and kv(rest, 'c0'):
                committed[client] = dict(cells=(int(kv(rest, 'c0')), int(kv(rest, 'c1'))), bought=False)
            elif event == 'edge' and edge == 'speed' and reason == 'request' and client in committed:
                committed[client]['bought'] = True
            elif event == 'edge' and edge in ('hop0', 'hop1') and reason == 'land' and client in committed:
                cell = committed[client]['cells'][int(edge[3])]
                if not 0 <= cell < len(cells):
                    continue
                live = [float(kv(rest, k)) for k in 'xyz']
                error = math.dist(cells[cell][:2], live[:2]) + (1000 if live[2] < PIT_Z else 0)
                errors[committed[client]['bought']].append(error)
            elif (event == 'edge' and edge == 'onward') or event == 'miss' or reason == 'objective':
                committed.pop(client, None)
    for bought, values in errors.items():
        if values:
            values.sort()
            print(f'speed bought after commit={bought}: landings={len(values)} '
                  f'median={statistics.median(values):.0f}u p90={values[int(.9 * len(values))]:.0f}u '
                  f'over_128u={sum(v > 128 for v in values)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=('falls', 'landings'))
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args()
    if args.mode == 'falls':
        for world in args.paths:
            falls(world)
    else:
        landings(args.paths)


if __name__ == '__main__':
    main()
