"""How a human defends against the runner: one play_vs_runner.py session.

Record docs/movement/records/2026-09-29-human-defense-session.md. Offline:
reads a stopped session's retained files (tools/play_vs_runner.py --stop).

    python tools/play_defense_study.py .codex-research/play-vs-runner-<stamp> [--pov NAME]

Sources, all under <session>/live/server/base:
  demos/autorecord/**/<n> <POV>*.dm_26   the human's view (dm26 through
      base_defender_study.extract_demo): position, health, Force, weapon and
      weapons owned, fires, hit counter, push/pull spends, obituaries, flag status
  bot2-play/*.csv                        the runner every 250 ms (position,
      health, carrying)
  play-game.log                          item pickups and kills (1 s resolution)

Writes report.txt and summary.json into the session directory. Observed:
positions, fires, hit-counter rises, Force spends, obituaries, pickups.
Inferred: the action behind a kill (the POV's last fire, hit or spend within
a window before it) and the range of a fire (the runner's nearest sample).
"""
import argparse
import bisect
import collections
import csv
import glob
import json
import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import base_defender_study as bds
import position_pilot as pp

MAP = 'mp/ctf_kejim'
WEAPONS = bds.WEAPONS
HAND_PUSH, HAND_PULL = bds.HAND_PUSH, bds.HAND_PULL
TRIP_MINE = WEAPONS.index('trip_mine')
ACTION_MS = 4000          # a kill is credited to the POV's last action this long before it
FORCE_MS = 8000           # a pit fall takes seconds: the push or pull that began it
NEAR_STAND = 1500


def stands(mapname):
    for p in pp.FIXTURES.glob('*/cases.json'):
        doc = json.loads(p.read_text(encoding='utf-8'))
        if doc['map'] == mapname:
            s = {c['start']['name']: c['start']['origin'] for c in doc['cases']}
            if 'red_flagstand' in s:
                return {pp.TEAM_RED: s['red_flagstand'], pp.TEAM_BLUE: s['blue_flagstand']}
    raise SystemExit('no flag stands for %s' % mapname)


def q(vals, ps=(25, 50, 75)):
    v = sorted(vals)
    if not v:
        return '-'
    return ' / '.join('%.0f' % v[min(len(v) - 1, int(p / 100 * len(v)))] for p in ps)


def pct(n, d):
    return '%d%%' % round(100 * n / d) if d else '-'


def game_log(path):
    items, kills, mods = [], [], {}
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.match(r'\s*(\d+):(\d\d) (Item|Kill): (.*)', line)
        if not m:
            continue
        t = (int(m.group(1)) * 60 + int(m.group(2))) * 1000
        if m.group(3) == 'Item':
            c, item = m.group(4).split()[:2]
            items.append((t, int(c), item))
        else:
            k = re.match(r'(\d+) (\d+) (\d+): .* by (\S+)', m.group(4))
            kills.append((t, int(k.group(1)), int(k.group(2)), int(k.group(3)), k.group(4)))
            mods[int(k.group(3))] = k.group(4)
    return items, kills, mods


class Runner:
    """The runner's play log: nearest sample by level time."""
    def __init__(self, path):
        self.rows = [dict(t=int(r['level_time']), pos=(float(r['x']), float(r['y']), float(r['z'])),
                          hp=int(r['health']), carrying=int(r['carrying']), captures=int(r['captures']))
                     for r in csv.DictReader(path.open(encoding='utf-8'))]
        self.ts = [r['t'] for r in self.rows]

    def at(self, t):
        i = bisect.bisect_left(self.ts, t)
        cand = [j for j in (i - 1, i) if 0 <= j < len(self.rows)]
        return self.rows[min(cand, key=lambda j: abs(self.ts[j] - t))]

    def before(self, t):
        i = bisect.bisect_right(self.ts, t) - 1
        return self.rows[max(i, 0)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('session', type=Path)
    ap.add_argument('--pov', default='Padawan')
    args = ap.parse_args()
    base = args.session / 'live' / 'server' / 'base'
    demo = glob.glob(str(base / 'demos' / 'autorecord' / '**' / ('* %s *.dm_26' % args.pov)), recursive=True)
    if len(demo) != 1:
        raise SystemExit('expected one %s demo, found %d' % (args.pov, len(demo)))
    d = bds.extract_demo(dict(demo=demo[0], map=MAP, key='kejim', pos='play', pov_name=args.pov, sheet='',
                              match_id=args.session.name))
    if d['error']:
        raise SystemExit('demo: ' + d['error'])
    runner = Runner(Path(glob.glob(str(base / 'bot2-play' / '*.csv'))[0]))
    items, kills, mods = game_log(base / 'play-game.log')
    samples = d['samples']
    me, my_team = samples[0][3][0], samples[0][3][1]
    stand = stands(MAP)[my_team]
    enemy_stand = stands(MAP)[pp.TEAM_BLUE if my_team == pp.TEAM_RED else pp.TEAM_RED]
    rclient = next(c for c, v in d['names'].items() if v[-1][2] != my_team)
    sts = [s[0] for s in samples]

    def pov_at(t):
        i = max(bisect.bisect_right(sts, t) - 1, 0)
        return samples[i]

    def pos(s):
        return s[3][2:5]

    def dist2(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1])

    def runner_seen(t, window=300):
        """The runner from the POV's snapshots if visible near t, else the play log."""
        i = bisect.bisect_left(sts, t)
        for j in (i, i - 1):
            if 0 <= j < len(samples) and abs(samples[j][0] - t) <= window:
                for o in samples[j][4]:
                    if o[0] == rclient and o[6]:
                        return o[1:4], True
        return runner.at(t)['pos'], False

    out, say = {}, []
    t0, t1 = samples[0][0], samples[-1][0]
    my_home = 0 if my_team == pp.TEAM_RED else 1
    say.append('Session %s, POV %s (client %d, team %d), runner client %d; %.1f s of demo, %d samples, lost events %d'
               % (args.session.name, args.pov, me, my_team, rclient, (t1 - t0) / 1000, len(samples), d['lost_events']))
    say.append('Own stand %s, enemy stand %s' % (stand, enemy_stand))

    # ---- outcomes
    rk = [k for k in kills if k[2] == rclient]
    mine = [k for k in kills if k[2] == me]
    pickups, caps, prev = [], [], None
    for r in runner.rows:
        if prev and not prev['carrying'] and r['carrying']:
            pickups.append(r)
        if prev and r['captures'] > prev['captures']:
            caps.append(r)
        prev = r
    returns = [it for it in items if it[1] == me and it[2] == ('team_CTF_redflag' if my_team == pp.TEAM_RED else 'team_CTF_blueflag')]
    say.append('')
    say.append('## Outcomes')
    say.append('runner lives %d, pickups %d, captures %d; runner killed %d times (%s); POV deaths %d (%s); POV flag returns %d'
               % (len(rk) + 1, len(pickups), len(caps), len(rk), dict(collections.Counter(k[4] for k in rk)),
                  len(mine), dict(collections.Counter(k[4] for k in mine)), len(returns)))
    out['outcomes'] = dict(pickups=len(pickups), captures=len(caps), runner_killed=len(rk),
                           runner_killed_by=collections.Counter(k[4] for k in rk), pov_deaths=len(mine),
                           pov_deaths_by=collections.Counter(k[4] for k in mine), returns=len(returns))

    # ---- each runner death: where, carrying, what the POV last did
    fires, hits, spends = d['fires'], d['hits'], d['spends']
    obits = [o for o in d['obits'] if o[1] == rclient]   # (t, victim, attacker, mod)
    say.append('')
    say.append('## Runner deaths (demo obituaries; runner state from its play log just before)')
    say.append('t_s  mod                       carrying  runner->stand  pov->stand  pov->runner  runner_hp  last POV action (<= %d ms) | last push/pull (<= %d ms)' % (ACTION_MS, FORCE_MS))
    rows = []
    for t, victim, killer, mod in obits:
        rb = runner.before(t - 1)
        rp, _ = runner_seen(t - 100)
        s = pov_at(t)
        acts = ([(f[0], 'fire %s%s' % (WEAPONS[f[1]], ' alt' if f[2] else '')) for f in fires if t - ACTION_MS <= f[0] <= t] +
                [(h[0], 'hit %s' % WEAPONS[h[4]]) for h in hits if t - ACTION_MS <= h[0] <= t] +
                [(p[0], {HAND_PUSH: 'push', HAND_PULL: 'pull'}.get(p[3], 'force spend %d' % p[2])) for p in spends if t - ACTION_MS <= p[0] <= t])
        acts.sort()
        last = ', '.join('%s @-%d' % (a, t - at) for at, a in acts[-3:]) or 'none'
        force = [p for p in spends if t - FORCE_MS <= p[0] <= t and p[3] in (HAND_PUSH, HAND_PULL)]
        flast = ('%s @-%d' % ('push' if force[-1][3] == HAND_PUSH else 'pull', t - force[-1][0])) if force else 'none'
        row = dict(t=t, mod=mods.get(mod, str(mod)), carrying=rb['carrying'], runner_to_stand=dist2(rp, stand),
                   pov_to_stand=dist2(pos(s), stand), pov_to_runner=math.dist(pos(s), rp), runner_hp=rb['hp'],
                   actions=[a for _, a in acts], force=flast)
        rows.append(row)
        say.append('%5.1f %-25s %d  %6.0f  %6.0f  %6.0f  %4d  %s | %s' % (t / 1000, row['mod'], row['carrying'], row['runner_to_stand'],
                                                                         row['pov_to_stand'], row['pov_to_runner'], rb['hp'], last, flast))
    out['runner_deaths'] = rows
    before = sum(1 for r in rows if not r['carrying'])
    near = sum(1 for r in rows if not r['carrying'] and r['runner_to_stand'] < 600)
    say.append('before pickup %d of %d (within 600 u of the own stand %d); carrying %d' % (before, len(rows), near, len(rows) - before))

    # ---- positioning with the own flag home
    home = [s for s in samples if s[1][my_home] == '0' and s[3][9] and not s[2]]
    dist = [dist2(pos(s), stand) for s in home]
    dz = [s[3][4] - stand[2] for s in home]
    say.append('')
    say.append('## Position while the own flag is home and the POV alive (%d samples)' % len(home))
    say.append('2D distance to own stand p25 / med / p75: %s; within 512 / 1,000 / 1,500 u: %s / %s / %s'
               % (q(dist), pct(sum(x < 512 for x in dist), len(dist)), pct(sum(x < 1000 for x in dist), len(dist)),
                  pct(sum(x < 1500 for x in dist), len(dist))))
    say.append('height vs stand: above +64 / level / below -64: %s / %s / %s'
               % (pct(sum(z > 64 for z in dz), len(dz)), pct(sum(-64 <= z <= 64 for z in dz), len(dz)), pct(sum(z < -64 for z in dz), len(dz))))
    pts = [pos(s) for s in home if dist2(pos(s), stand) < NEAR_STAND]
    posts = []
    if len(pts) >= 20:
        # weight by the time each sample stands for (samples are denser with an enemy near)
        wts = [min(250, (home[i + 1][0] - home[i][0]) if i + 1 < len(home) else 250) for i, s in enumerate(home)
               if dist2(pos(s), stand) < NEAR_STAND]
        cent, lab = bds.kmeans(pts, wts, 5)
        tot = sum(wts)
        for j, c in enumerate(cent):
            w = sum(wt for wt, l in zip(wts, lab) if l == j)
            posts.append(dict(pos=[round(float(v)) for v in c], share=w / tot, to_stand=dist2(c, stand)))
        posts.sort(key=lambda p: -p['share'])
        say.append('posts (k-means k=5, samples within %d u, share of that time):' % NEAR_STAND)
        for p in posts:
            say.append('  %6d %6d %5d  %3.0f%%  %5.0f u from stand' % (*p['pos'], 100 * p['share'], p['to_stand']))
    out['home_position'] = dict(samples=len(home), dist_q=q(dist), posts=posts)

    # where the POV was when the runner came within 1,500 u of the own stand, not carrying
    arrivals, inside = [], False
    for r in runner.rows:
        d_st = dist2(r['pos'], stand)
        if not inside and d_st < NEAR_STAND and not r['carrying'] and r['hp'] > 0:
            inside = True
            s = pov_at(r['t'])
            arrivals.append(dict(t=r['t'], pov_to_stand=dist2(pos(s), stand), pov_alive=s[3][9], weapon=WEAPONS[s[3][14]],
                                 pov_to_runner=math.dist(pos(s), r['pos'])))
        elif inside and (d_st > NEAR_STAND + 200 or r['hp'] <= 0):
            inside = False
    say.append('runner arrivals within %d u of the own stand: %d; POV alive %d; POV within 600 u of stand %d; POV-runner range med %s; weapon %s'
               % (NEAR_STAND, len(arrivals), sum(a['pov_alive'] for a in arrivals), sum(a['pov_to_stand'] < 600 for a in arrivals),
                  q([a['pov_to_runner'] for a in arrivals], (50,)), dict(collections.Counter(a['weapon'] for a in arrivals))))
    out['arrivals'] = arrivals

    # ---- weapons
    say.append('')
    say.append('## Weapons')
    held = collections.Counter(WEAPONS[s[3][14]] for s in samples if s[3][9])
    tot = sum(held.values())
    say.append('held while alive (samples): ' + ', '.join('%s %s' % (w, pct(n, tot)) for w, n in held.most_common()))
    near_held = collections.Counter(WEAPONS[s[3][14]] for s in samples if s[3][9]
                                    and dist2(runner.at(s[0])['pos'], stand) < NEAR_STAND and runner.at(s[0])['hp'] > 0)
    tot = sum(near_held.values())
    say.append('held with the runner within %d u of the own stand: ' % NEAR_STAND + ', '.join('%s %s' % (w, pct(n, tot)) for w, n in near_held.most_common()))
    by = collections.defaultdict(list)
    for f in fires:
        if f[1] == TRIP_MINE:
            continue
        rp, seen = runner_seen(f[0])
        by[(WEAPONS[f[1]], f[2])].append((math.dist(f[3:6], rp), seen))
    hit_by = collections.Counter(WEAPONS[h[4]] for h in hits)
    say.append('fires: weapon mode n  range to runner p25/med/p75 (runner visible)  | hit-counter rises by weapon %s' % dict(hit_by))
    for (w, alt), v in sorted(by.items(), key=lambda kv: -len(kv[1])):
        say.append('  %-10s %-4s %3d  %s  (%d visible)' % (w, 'alt' if alt else 'pri', len(v), q([x for x, _ in v]), sum(s for _, s in v)))
    mines = [f for f in fires if f[1] == TRIP_MINE]
    say.append('trip mines placed %d; distance to own stand %s' % (len(mines), ', '.join('%.0f' % dist2(f[3:6], stand) for f in mines)))
    out['fires'] = {'%s %s' % (w, 'alt' if a else 'pri'): len(v) for (w, a), v in by.items()}
    out['trip_mines'] = [dict(t=f[0], pos=f[3:6], to_stand=dist2(f[3:6], stand)) for f in mines]

    # ---- Force
    say.append('')
    say.append('## Force')
    kinds = collections.Counter({HAND_PUSH: 'push', HAND_PULL: 'pull'}.get(p[3], 'other') for p in spends)
    say.append('spends >= 10: %s' % dict(kinds))
    for p in spends:
        rp, seen = runner_seen(p[0])
        rr = runner.at(p[0])
        died = next((o[0] for o in obits if p[0] <= o[0] <= p[0] + FORCE_MS), None)
        say.append('  %5.1f %-5s pov->stand %5.0f  pov->runner %5.0f%s  runner->own stand %5.0f  runner->enemy stand %5.0f  runner z %5.0f  carrying %d  force before %d  runner died %s'
                   % (p[0] / 1000, {HAND_PUSH: 'push', HAND_PULL: 'pull'}.get(p[3], 'other'), dist2(p[6:9], stand),
                      math.dist(p[6:9], rp), '' if seen else ' (not visible)', dist2(rp, stand), dist2(rp, enemy_stand), rp[2],
                      rr['carrying'], p[5] + p[1], '+%.1f s' % ((died - p[0]) / 1000) if died else '-'))
    act = collections.Counter(pp.FORCE_NAMES[f[2]] for f in d['force'] if f[1] == me)
    say.append('own activations: %s' % dict(act))

    # ---- suicides and rearming
    say.append('')
    say.append('## Suicides and rearming')
    sui = [k for k in mine if k[4] == 'MOD_SUICIDE']
    wpn_items = [it for it in items if it[1] == me and not it[2].startswith('team_CTF')]
    say.append('POV item pickups: %s' % dict(collections.Counter(it[2] for it in wpn_items)))
    spawns = []
    for i, s in enumerate(samples[1:], 1):
        if s[3][15] != samples[i - 1][3][15]:
            spawns.append(s)
    for k in sui:
        t = k[0]
        prior_kill = max((o[0] for o in obits if o[0] <= t + 999), default=None)
        flag = pov_at(t)[1]
        sp = next((s for s in spawns if s[0] >= t), None)
        first = next((it for it in wpn_items if it[0] >= t), None)
        back = None
        if sp:
            back = next((s[0] for s in samples if s[0] >= sp[0] and dist2(pos(s), stand) < 600), None)
        say.append('  %3ds  flags %s  since runner kill %s  respawn %s  first pickup %s  back within 600 u of stand %s'
                   % (t // 1000, flag, '%.1f s' % ((t - prior_kill) / 1000) if prior_kill is not None and t - prior_kill < 10000 else '-',
                      '%.0f u from stand' % dist2(pos(sp), stand) if sp else '-',
                      '%s +%ds' % (first[2], (first[0] - t) // 1000) if first else '-',
                      '+%.1f s' % ((back - t) / 1000) if back else '-'))
    out['suicides'] = len(sui)

    text = '\n'.join(say) + '\n'
    (args.session / 'report.txt').write_text(text, encoding='utf-8', newline='\n')
    (args.session / 'summary.json').write_text(json.dumps(out, indent=1, default=str), encoding='utf-8', newline='\n')
    sys.stdout.write(text)


if __name__ == '__main__':
    main()
