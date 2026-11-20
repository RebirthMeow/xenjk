"""Standoff metrics of two-sided laps worlds (milestone B3: hold times, both flags held, how it breaks).

From each world's play CSV (bot2-play, a row per bot per frame) and game log:

- holds: each carrier's continuous possession, alive, in game seconds, and how
  it ended: capture (the carrier's PERS_CAPTURES rose), death (by the game
  log's Kill line for that client: weapon, fall, self-kill) or the world's end;
- standoffs: stretches with both flags away from their stands (taken or
  dropped, CSV home/enemy_flag_status 1 or 2), their length, which flag came
  home first and why its carrier lost it, and whether the team whose flag came
  home then captured within 20 s (the standoff "broken" into a capture);
- the longest stretch in which no bot moved 50 u in 10 s (frozen).

Per arm (a laps_campaign arm directory or a world prefix) the per-world means
with standard errors, per 600 game s, then the pooled hold and standoff
distributions.

    python tools/laps_standoff.py .codex-research/b3dan-20261003-a/kejim .codex-research/b3dan-20261003-a/dantooine
"""
import bisect
import collections
import csv
import glob
import json
import math
import os
import re
import statistics
import sys

BIN = 250      # ms, the play CSV's sampling grain used throughout
AWAY = {1, 2}  # taken, dropped (CS_FLAGSTATUS characters: 0 home, 1 taken, 2 dropped)
RED, BLUE = 1, 2


def worlds(prefix):
    if os.path.isfile(os.path.join(prefix, 'summary.json')):
        return [prefix]
    found = sorted(d for d in glob.glob(prefix + '-*') if os.path.isfile(os.path.join(d, 'summary.json')))
    if not found and os.path.isdir(prefix):
        found = sorted(os.path.dirname(p) for p in glob.glob(os.path.join(prefix, '*', 'summary.json')))
    return found


def kills(game_log):
    """Kill lines: (game ms, killer, victim, mod). Game-log times are whole seconds."""
    out = []
    for line in open(game_log, errors='replace'):
        m = re.match(r'\s*(\d+):(\d+) Kill: (\d+) (\d+) \d+: .* by (MOD_\w+)', line)
        if m:
            out.append(((int(m[1]) * 60 + int(m[2])) * 1000, int(m[3]), int(m[4]), m[5]))
    return out


def cause_of(death_ms, client, kl):
    """The Kill line of `client` nearest `death_ms` within 2 s (game-log seconds are truncated)."""
    best = None
    for t, killer, victim, mod in kl:
        if victim == client and -2000 <= t - death_ms <= 2000 and (best is None or abs(t - death_ms) < abs(best[0] - death_ms)):
            best = (t, killer, mod)
    if best is None:
        return 'unknown'
    t, killer, mod = best
    if mod == 'MOD_FALLING' or mod == 'MOD_TRIGGER_HURT':
        return 'fall'
    if mod == 'MOD_SUICIDE' or killer == client and mod in ('MOD_UNKNOWN',):
        return 'selfkill'
    if killer == client:
        return 'own_splash'
    return 'weapon'


def frozen_ms(per):
    idx = {c: [int(r['level_time']) for r in v] for c, v in per.items()}

    def at(c, t):
        i = bisect.bisect_right(idx[c], t) - 1
        return per[c][max(i, 0)]
    t0 = max(x[0] for x in idx.values()) + 10000
    t1 = min(x[-1] for x in idx.values())
    run = best = 0
    t = t0
    while t <= t1:
        still = all(math.dist((float(at(c, t)['x']), float(at(c, t)['y'])),
                              (float(at(c, t - 10000)['x']), float(at(c, t - 10000)['y']))) < 50 for c in per)
        run = run + 1000 if still else 0
        best = max(best, run)
        t += 1000
    return best


def world(d):
    s = json.load(open(os.path.join(d, 'summary.json')))
    seconds = float(s.get('game_seconds') or 600)
    base = os.path.join(d, 'live', 'server', 'base')
    rows = []
    for f in glob.glob(os.path.join(base, 'bot2-play', '*.csv')):
        rows += list(csv.DictReader(open(f, encoding='utf-8')))
    rows.sort(key=lambda r: int(r['level_time']))
    kl = kills(os.path.join(base, 'play-game.log'))
    per = collections.defaultdict(list)
    for r in rows:
        per[int(r['client'])].append(r)

    # Flag status per bin: a row of team T gives T's flag (home) and the other's (enemy).
    status = collections.defaultdict(dict)   # bin -> {team: status}
    for r in rows:
        b, team = int(r['level_time']) // BIN, int(r['team'])
        if team not in (RED, BLUE) or r['home_flag_status'] == '':
            continue
        status[b][team] = int(r['home_flag_status'])
        status[b][BLUE if team == RED else RED] = int(r['enemy_flag_status'])
    bins = sorted(b for b in status if len(status[b]) == 2)

    # Holds, per carrier.
    holds = []   # (team, seconds, end)
    for c, v in per.items():
        start = None
        caps = None
        for i, r in enumerate(v):
            carrying = r['carrying'] not in ('', '0') and int(r['health']) > 0
            if carrying and start is None:
                start, caps = int(r['level_time']), int(r['captures'] or 0)
            elif not carrying and start is not None:
                t = int(r['level_time'])
                if int(r['captures'] or 0) > caps:
                    end = 'capture'
                elif int(r['health']) <= 0 or any(victim == c and -2000 <= kt - t <= 2000 for kt, _, victim, _ in kl):
                    end = cause_of(t, c, kl)
                else:
                    end = 'other'
                holds.append((int(r['team']), (t - start) / 1000.0, end))
                start = None
        if start is not None:
            holds.append((int(v[-1]['team']), (int(v[-1]['level_time']) - start) / 1000.0, 'world_end'))

    # Carrier of each flag per bin (the client carrying the other team's flag), for the standoff's end cause.
    carrier_of = collections.defaultdict(dict)   # bin -> {flag team: client}
    for r in rows:
        if r['carrying'] not in ('', '0') and int(r['health']) > 0:
            team = int(r['team'])
            carrier_of[int(r['level_time']) // BIN][BLUE if team == RED else RED] = int(r['client'])

    # Captures per team over time (sum of the team's clients' PERS_CAPTURES), for "broken into a capture".
    cap_times = []   # (ms, team)
    last = {}
    for r in rows:
        c, cap = int(r['client']), int(r['captures'] or 0)
        if c in last and cap > last[c]:
            cap_times.append((int(r['level_time']), int(r['team'])))
        last[c] = cap

    standoffs = []   # (seconds, end, freed team, capture within 20 s)
    i = 0
    while i < len(bins):
        b = bins[i]
        if not all(status[b][t] in AWAY for t in (RED, BLUE)):
            i += 1
            continue
        j = i
        while j + 1 < len(bins) and bins[j + 1] - bins[j] <= 2 and all(status[bins[j + 1]][t] in AWAY for t in (RED, BLUE)):
            j += 1
        start_ms, end_ms = bins[i] * BIN, bins[j] * BIN
        if j + 1 >= len(bins):
            standoffs.append(((end_ms - start_ms) / 1000.0, 'world_end', 0, False))
            i = j + 1
            continue
        nxt = status[bins[j + 1]]
        home = [t for t in (RED, BLUE) if nxt[t] == 0]
        flag = home[0] if len(home) == 1 else (RED if nxt[RED] == 0 else BLUE)
        # Why that flag's carrier lost it: the last carrier of `flag` in the standoff and its death.
        holder = None
        for k in range(j, i - 1, -1):
            if flag in carrier_of.get(bins[k], {}):
                holder, lost_ms = carrier_of[bins[k]][flag], bins[k] * BIN
                break
        end = cause_of(lost_ms, holder, kl) if holder is not None else 'no_carrier'
        freed = flag   # the team whose own flag came home can now capture
        broke = any(end_ms <= t <= end_ms + 20000 and team == freed for t, team in cap_times)
        standoffs.append(((end_ms - start_ms) / 1000.0, end, freed, broke))
        i = j + 1

    scale = 600.0 / seconds
    away_both = sum(all(status[b][t] in AWAY for t in (RED, BLUE)) for b in bins)
    held_both = sum(len(carrier_of.get(b, {})) == 2 and all(status[b][t] in AWAY for t in (RED, BLUE)) for b in bins)
    real = [x for x in standoffs if x[0] >= 1]
    return dict(
        stats=dict(
            red_caps=sum(1 for _, t in cap_times if t == RED) * scale,
            blue_caps=sum(1 for _, t in cap_times if t == BLUE) * scale,
            holds=len(holds) * scale,
            hold_s=statistics.mean([h[1] for h in holds]) if holds else 0,
            hold_capture=sum(h[2] == 'capture' for h in holds) / len(holds) if holds else 0,
            both_away=away_both / max(1, len(bins)),
            both_held=held_both / max(1, len(bins)),
            standoffs=len(real) * scale,
            standoff_s=statistics.mean([x[0] for x in real]) if real else 0,
            standoff_cap20=sum(x[3] for x in real) / len(real) if real else 0,
            frozen_s=frozen_ms(per) / 1000.0,
        ),
        holds=holds, standoffs=real)


def mean_se(v):
    m = statistics.mean(v) if v else 0
    se = statistics.stdev(v) / math.sqrt(len(v)) if len(v) > 1 else 0
    return '%.2f (%.2f)' % (m, se)


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))] if v else 0


def main():
    arms = {}
    for prefix in sys.argv[1:]:
        arms[prefix.replace('\\', '/').rstrip('/').split('/')[-1]] = [world(d) for d in worlds(prefix)]
    names = list(arms)
    print('%-16s' % 'per world' + ''.join('%22s' % ('%s (n=%d)' % (a, len(arms[a]))) for a in names))
    for k in next(iter(arms.values()))[0]['stats']:
        print('%-16s' % k + ''.join('%22s' % mean_se([w['stats'][k] for w in arms[a]]) for a in names))
    for a in names:
        holds = [h for w in arms[a] for h in w['holds']]
        so = [x for w in arms[a] for x in w['standoffs']]
        hs = [h[1] for h in holds]
        print('\n%s: %d holds, seconds median %.1f p90 %.1f max %.1f; ends %s' % (
            a, len(holds), pct(hs, .5), pct(hs, .9), max(hs) if hs else 0,
            dict(collections.Counter(h[2] for h in holds).most_common())))
        ss = [x[0] for x in so]
        print('%s: %d standoffs, seconds median %.1f p90 %.1f max %.1f; ended by the losing carrier\'s %s; '
              'freed team captured within 20 s: %d of %d' % (
                  a, len(so), pct(ss, .5), pct(ss, .9), max(ss) if ss else 0,
                  dict(collections.Counter(x[1] for x in so).most_common()), sum(x[3] for x in so), len(so)))
        print('%s: frozen > 60 s in %d of %d worlds; longest %s' % (
            a, sum(w['stats']['frozen_s'] > 60 for w in arms[a]), len(arms[a]),
            sorted(w['stats']['frozen_s'] for w in arms[a])[-3:]))


if __name__ == '__main__':
    main()
