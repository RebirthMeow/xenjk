"""Where a lone 2v2 defender stands while the attacker comes in (Kejim).

Uses tools/base_defender_study.py's extract (v4, 2v2) and its defender rule
(the POV nearer its own stand than its teammate, own flag home). Two sets of
POV positions, time-weighted:
  A  own flag home, an enemy alive within 1,500 u of the own stand;
  B  the 3 s before each steal of the own flag.
Red positions are folded onto blue by Kejim's point symmetry (the midpoint of
the two stands), clustered (weighted k-means), and unfolded for both sides.

  python tools/defender_approach_posts.py [--out DIR] [--k 5]
"""
import argparse
import bisect
import collections
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import base_defender_study as bds  # noqa: E402
import position_pilot as pp  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=ROOT / '.codex-research/base-defender-study-20260928')
    ap.add_argument('--k', type=int, default=5)
    args = ap.parse_args()
    bds.load_stands()
    demos = [d for d in bds.load_demos(args.out / (bds.EXTRACT_DIR + '-2v2'), role_2v2=True) if d.map == 'kejim']
    stands = bds.STANDS3['kejim']
    blue, red = stands[pp.TEAM_BLUE], stands[pp.TEAM_RED]
    centre = ((blue[0] + red[0]) / 2, (blue[1] + red[1]) / 2)

    def fold(team, x, y):
        return (x, y) if team == pp.TEAM_BLUE else (2 * centre[0] - x, 2 * centre[1] - y)

    sets = {'A': ([], []), 'B': ([], [])}
    zs = {'A': [], 'B': []}
    minutes = collections.Counter()
    for d in demos:
        for i, s in bds.home_samples(d):
            en = d.enemies(s)
            if not en or min(math.dist(o[1:3], d.own[:2]) for o in en) >= 1500:
                continue
            w = d.weight(i)
            p = s[3]
            sets['A'][0].append(fold(d.team, p[2], p[3]))
            sets['A'][1].append(w)
            zs['A'].append((p[4] - d.own[2], w))
            minutes['A'] += w
        for t_steal, thief, _, _ in bds.thefts(d):
            if not d.role(t_steal - 100):
                continue
            for t in range(t_steal - 3000, t_steal, 100):
                p = d.pov_pos(t, tol=150)
                if not p or not p[9]:
                    continue
                sets['B'][0].append(fold(d.team, p[2], p[3]))
                sets['B'][1].append(.1)
                zs['B'].append((p[4] - d.own[2], .1))
                minutes['B'] += .1
    print('Kejim 2v2 demos: %d; A: %.1f min of defending with an enemy within 1,500 u; B: %.1f min in the 3 s before steals'
          % (len(demos), minutes['A'] / 60, minutes['B'] / 60))
    result = {}
    for key, (pts, wts) in sets.items():
        if not pts:
            continue
        cents, labels = bds.kmeans(pts, wts, args.k)
        cents = [tuple(c) for c in cents]
        share = collections.Counter()
        for lab, w in zip(labels, wts):
            share[int(lab)] += w
        total = sum(wts)
        rows = []
        for c in sorted(range(len(cents)), key=lambda c: -share[c]):
            bx, by = cents[c]
            rx, ry = 2 * centre[0] - bx, 2 * centre[1] - by
            rows.append(dict(share=round(100 * share[c] / total, 1), blue=[round(bx), round(by)], red=[round(rx), round(ry)],
                             dist_stand=round(math.dist((bx, by), blue[:2]))))
        near = sum(w for p, w in zip(pts, wts) if math.dist(p, blue[:2]) < 256) / total
        med_d = sorted(math.dist(p, blue[:2]) for p in pts)[len(pts) // 2]
        dzs = sorted(z for z, _ in zs[key])
        print('\n%s  (%s)' % (key, 'enemy within 1,500 u, own flag home' if key == 'A' else '3 s before a steal'))
        print('   median distance from own stand %.0f u; within 256 u of the stand %.0f%% of the time; height vs stand median %+.0f'
              % (med_d, 100 * near, dzs[len(dzs) // 2]))
        for r in rows:
            print('   %5.1f%%  blue side (%5d, %5d)  red side (%5d, %5d)  %4d u from the stand' % (r['share'], *r['blue'], *r['red'], r['dist_stand']))
        result[key] = dict(clusters=rows, median_dist=round(med_d), near_stand=round(100 * near, 1))
    (args.out / 'approach_posts_2v2.json').write_text(json.dumps(result, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
