"""Strafing, walking and standing time of one bench world, by segment.

Every 25 ms `[MOMENTUM]` sample from the release to the pass is one of:

- strafing: airborne, or a grounded run of at most TAP_SAMPLES samples with
  airborne samples on both sides (the landing frame of a hop chain);
- standing: any other grounded sample under STAND_SPEED u/s;
- walking: every other grounded sample.

Segments end at each `[GOALCHAIN]` point reached, at the flag pickup (the
first carrying play-log row, 250 ms resolution) and at the pass. Needs a
world run with --momentum-trace.

    python tools/movement_uptime.py <world-dir> [<world-dir> ...] [--json out.json]
"""
import argparse
import csv
import json
from pathlib import Path
import re

STEP_MS = 25
TAP_SAMPLES = 2
STAND_SPEED = 20.0
FIELD = re.compile(r'(\w+)=(\S+)')
GOALCHAIN = re.compile(r'\[GOALCHAIN\] client=\d+ level=(\d+) event=reached index=(\d+)')


def classify(samples):
    """Label each sample (dicts with speed and ground) strafe, walk or stand."""
    labels = [None] * len(samples)
    i = 0
    while i < len(samples):
        if not samples[i]['ground']:
            labels[i] = 'strafe'
            i += 1
            continue
        j = i
        while j < len(samples) and samples[j]['ground']:
            j += 1
        tap = j - i <= TAP_SAMPLES and i > 0 and j < len(samples)
        for k in range(i, j):
            labels[k] = 'strafe' if tap else ('stand' if samples[k]['speed'] < STAND_SPEED else 'walk')
        i = j
    return labels


def tally(samples, labels):
    out = dict(samples=len(samples), ms=len(samples) * STEP_MS)
    for kind in ('strafe', 'walk', 'stand'):
        n = sum(1 for x in labels if x == kind)
        out[kind + '_ms'] = n * STEP_MS
        out[kind + '_pct'] = round(100.0 * n / len(samples), 1) if samples else None
    speeds = [s['speed'] for s in samples]
    out['mean_speed'] = round(sum(speeds) / len(speeds), 1) if speeds else None
    out['max_speed'] = round(max(speeds), 1) if speeds else None
    return out


def world(path):
    path = Path(path)
    result = json.loads((path / 'result.json').read_text(encoding='utf-8'))
    start = int(result['corner_level'])
    evaluation = result.get('evaluation') or {}
    if evaluation.get('status') != 'pass':
        raise ValueError('%s did not pass: %s' % (path, evaluation.get('reason')))
    end = start + int(evaluation['time_ms'])
    text = (path / 'server.log').read_text(errors='replace')
    samples = []
    for line in text.splitlines():
        if '[MOMENTUM]' not in line:
            continue
        row = dict(FIELD.findall(line.split('[MOMENTUM]', 1)[1]))
        level = int(row['level'])
        if start < level <= end:
            samples.append(dict(level=level, speed=float(row['speed']), ground=int(row['ground']) != 0,
                                owner=row.get('owner')))
    if not samples:
        raise ValueError('%s has no momentum samples; run it with --momentum-trace' % path)
    bounds = [('stop %d' % (int(m.group(2)) + 1), int(m.group(1)))
              for m in GOALCHAIN.finditer(text) if start < int(m.group(1)) <= end]
    plays = list((path / 'base' / 'bot2-play').glob('*.csv'))
    if plays:
        with plays[0].open(newline='') as stream:
            pickup = next((int(r['level_time']) for r in csv.DictReader(stream)
                           if int(r['level_time']) > start and int(r['carrying'])), None)
        if pickup is not None and pickup < end:
            bounds.append(('enemy flag', pickup))
    bounds.sort(key=lambda b: b[1])
    bounds.append(('finish', end))
    labels = classify(samples)
    segments, first = [], start
    for name, level in bounds:
        index = [i for i, s in enumerate(samples) if first < s['level'] <= level]
        segment = tally([samples[i] for i in index], [labels[i] for i in index])
        segment.update(name=name, from_ms=first - start, to_ms=level - start)
        segments.append(segment)
        first = level
    whole = tally(samples, labels)
    whole.update(world=str(path), time_ms=end - start,
                 owners=sorted({s['owner'] for s in samples if s['owner']}))
    return dict(total=whole, segments=segments)


def show(report):
    total = report['total']
    print('%s: %.2f s  strafe %.1f%%  walk %.1f%%  stand %.1f%%  mean %.0f u/s  max %.0f u/s  owners %s'
          % (total['world'], total['time_ms'] / 1000, total['strafe_pct'], total['walk_pct'],
             total['stand_pct'], total['mean_speed'], total['max_speed'], ','.join(total['owners'])))
    for s in report['segments']:
        if s['samples']:
            print('  %-11s %6.2f-%6.2f s  strafe %5.1f%%  walk %5.1f%%  stand %5.1f%%  mean %4.0f u/s'
                  % (s['name'], s['from_ms'] / 1000, s['to_ms'] / 1000, s['strafe_pct'],
                     s['walk_pct'], s['stand_pct'], s['mean_speed']))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('worlds', nargs='+', type=Path, help='bench world directories (<out>/<case>)')
    parser.add_argument('--json', type=Path, help='write the reports here')
    args = parser.parse_args(argv)
    reports = [world(w) for w in args.worlds]
    for report in reports:
        show(report)
    if args.json:
        args.json.write_text(json.dumps(reports, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
