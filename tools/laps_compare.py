"""Compare laps-world arms (tools/defense_laps.py output) on per-world metrics,
each against a baseline arm, with a verdict for early stopping.

    python tools/laps_compare.py <baseline dir/prefix> <arm dir/prefix> ... [--primary team_caps] [--tolerance 0.15] [--json]

An arm is every `<prefix>-*` directory holding a summary.json (or a directory of
such worlds). Every metric is per 600 game seconds, so worlds of any length and
rate compare. Differences are arm minus baseline, with a Welch standard error
over worlds (worlds are the independent unit; events inside a world are not).
Worlds are not paired: two arms run on the same seeds are compared as two
samples (across builds the seeds' worlds barely correlate).

Verdict on the primary metric: `effect` when |difference| reaches the Student t
cut-off at the two-sided level 2.5 normal standard errors give (about 1.2%), with
Welch's degrees of freedom; `null` when the 95% t interval lies inside
+-tolerance x the baseline mean; else `undecided` (run more worlds). At 4-8 worlds
an arm, normal cut-offs were over-confident. A world whose game
log is identical to another of its arm is a replay, not new evidence: it is
counted once (`replays=` in the report). A world with a crash report, or one
declared two-sided whose second runner never joined, is listed and left out. Worlds that ran short of their declared
game time (the tool's earlier 5x wall-clock wait) count, scaled per 600 s.

Captures (2026-10-05, records/2026-10-05-deferral-behaviour.md). In a one-sided world the runner team's flag never
drops, so its touches are captures. In a two-sided world (a Runner2_v2 joined) a touch of a team's own flag is also a
return, so team_caps, runner_caps, camper_caps and pickups are true counts there: captures from the play CSV's native
PERS_CAPTURES per client, pickups as enemy-flag touches, each the mean over the two teams (same roles on both, one
team's scale). team_returns is own-flag touches less captures, per team. Before this, two-sided arms were scored on
the blue team's touches (captures plus returns) and camper_caps was the blue camper alone.
"""
import argparse
import csv
import glob
import hashlib
import json
import math
import os
import re
import statistics
from collections import Counter

RUNNER_TEAM = ('Runner_v2', 'Camper_v2')
NON_WEAPON = {'MOD_FALLING', 'MOD_TRIGGER_HURT', 'MOD_SUICIDE', 'MOD_CRUSH', 'MOD_UNKNOWN', 'MOD_WATER', 'MOD_SLIME', 'MOD_LAVA'}
METRICS = [
    ('team_caps', 'captures by the runner team (two-sided: true captures, mean of both teams)'),
    ('runner_caps', 'captures by the runner (two-sided: both runners, a team)'),
    ('camper_caps', 'captures by the camper (two-sided: both campers, a team)'),
    ('pickups', 'enemy-flag pickups by the runner team (two-sided: runners and campers, a team)'),
    ('conversion', 'captures per pickup (pooled)'),
    ('team_returns', 'own-flag returns, a team (two-sided)'),
    ('runner_weapon_deaths', 'runner deaths to weapons'),
    ('runner_falls', 'runner falls'),
    ('runners_falls', 'falls of both runners (Runner_v2 + Runner2_v2; two-sided worlds)'),
    ('runners_weapon_deaths', 'weapon deaths of both runners (two-sided worlds)'),
    ('camper_deaths', 'camper deaths'),
    ('camper_falls', 'camper falls'),
    ('runners_self_kills', 'self-kills of both runners (CAPKILL, stranded; MOD_SUICIDE; two-sided worlds)'),
    ('defender_deaths', 'declared defender deaths (base, chase), all causes but self-kill; two-sided: a team'),
    ('defender_enemy_flag', "red base and chase D's pickups of the runner team's (blue) flag"),
    ('blue_caps', 'captures by blue (play CSV, two-sided worlds)'),
    ('red_caps', 'captures by red (play CSV, two-sided worlds)'),
    ('red_minus_blue', 'red captures minus blue (balance)'),
    ('all_caps', 'captures by both teams (two-sided)'),
    ('def_enemy_pickups', "declared defenders' pickups of the enemy flag, both teams (two-sided)"),
    ('max_cap_gap', 'longest stretch without a capture, game s (stalls; not scaled)'),
]


def two_sided(d):
    """Defender pickups of the enemy flag (any team), the longest capture-free stretch, and per-team counts
    for two-sided worlds, from the game log and the play CSV (clients' teams and native capture counts).
    The counts are None in a one-sided world (no Runner2_v2)."""
    base = os.path.join(d, 'live', 'server', 'base')
    game = open(os.path.join(base, 'play-game.log'), errors='replace').read() if os.path.isfile(os.path.join(base, 'play-game.log')) else ''
    names = {c: re.sub(r'\^\d', '', n) for c, n in re.findall(r'ClientConnect: (\d+) \[[^\]]*\] \([^)]*\) "([^"]+)"', game)}
    team, caps, times, first, last = {}, {}, [], None, None
    for f in glob.glob(os.path.join(base, 'bot2-play', '*.csv')):
        for r in csv.DictReader(open(f, encoding='utf-8')):
            c, t, n = r['client'], int(r['level_time']), int(r['captures'] or 0)
            team[c] = r['team']
            first = t if first is None else min(first, t)
            last = t if last is None else max(last, t)
            if n > caps.get(c, 0):
                times.append(t)
            caps[c] = max(n, caps.get(c, 0))
    colour = {'1': 'red', '2': 'blue'}
    pickups = sum(1 for c, flag in re.findall(r'Item: (\d+) team_CTF_(red|blue)flag', game)
                  if re.match(r'(Defender|Chaser)2?_v2$', names.get(c, '')) and colour.get(team.get(c)) not in (None, flag))
    marks = [first or 0] + sorted(times) + [last or 0]
    gap = max((b - a for a, b in zip(marks, marks[1:])), default=0) / 1000.0
    counts = None
    if 'Runner2_v2' in names.values():
        role = {c: re.sub(r'2?_v2$', '', n) for c, n in names.items()}
        touches = re.findall(r'Item: (\d+) team_CTF_(red|blue)flag', game)
        own = Counter(c for c, flag in touches if colour.get(team.get(c)) == flag)
        enemy = Counter(c for c, flag in touches if colour.get(team.get(c)) not in (None, flag))
        clients = [c for c in names if colour.get(team.get(c))]
        by_role = lambda r, n: sum(n.get(c, 0) for c in clients if role[c] == r) / 2.0
        counts = dict(team_caps=sum(caps.get(c, 0) for c in clients if role[c] in ('Runner', 'Camper')) / 2.0,
                      runner_caps=by_role('Runner', caps), camper_caps=by_role('Camper', caps),
                      pickups=sum(enemy[c] for c in clients if role[c] in ('Runner', 'Camper')) / 2.0,
                      team_returns=sum(own[c] - caps.get(c, 0) for c in clients) / 2.0)
    return pickups, gap, counts


def worlds(prefix):
    if os.path.isfile(os.path.join(prefix, 'summary.json')):
        return [prefix]
    # prefix-<n> only: arm "base" must not take "base-fast-1"
    found = sorted(d for d in glob.glob(prefix + '-*') if re.fullmatch(r'\d+', d[len(prefix) + 1:])
                   and os.path.isfile(os.path.join(d, 'summary.json')))
    if not found and os.path.isdir(prefix):
        found = sorted(os.path.dirname(p) for p in glob.glob(os.path.join(prefix, '*', 'summary.json')))
    return found


def defender_deaths(d):
    """Deaths of the declared base and chase defenders but self-kills, from the game log: the summary's
    defender_deaths holds only the red ones (Defender_v2, Chaser_v2) and defender2_deaths only Defender2_v2.
    Two-sided worlds: the mean of the two teams, as the other two-sided counts."""
    log = os.path.join(d, 'live', 'server', 'base', 'play-game.log')
    game = ''
    if os.path.isfile(log):
        with open(log, errors='replace') as f:
            game = f.read()
    deaths = Counter(re.sub(r'\^\d', '', victim) for victim, mod in
                     re.findall(r'Kill: \d+ \d+ \d+: \S+ killed (\S+) by (\S+)', game) if mod != 'MOD_SUICIDE')
    red = deaths['Defender_v2'] + deaths['Chaser_v2']
    blue = deaths['Defender2_v2'] + deaths['Chaser2_v2']
    return (red + blue) / 2.0 if re.search(r'ClientConnect: \d+ \[[^\]]*\] \([^)]*\) "Runner2_v2(\^\d)*"', game) else red


def world_metrics(d):
    s = json.load(open(os.path.join(d, 'summary.json')))
    info = json.load(open(os.path.join(d, 'session.json'))) if os.path.isfile(os.path.join(d, 'session.json')) else {}
    declared = float(info.get('game_seconds') or 600)
    seconds = float(s.get('game_seconds') or 0) or declared
    scale = 600.0 / seconds
    touches = Counter(s.get('team_captures', {}))
    picks = Counter(s.get('red_flag_pickups', {}))
    rd = Counter(s.get('runner_deaths', {}))
    rd2 = Counter(s.get('runner2_deaths', {}))
    cd = Counter(s.get('camper_deaths', {}))
    m = dict(
        team_caps=sum(touches[n] for n in RUNNER_TEAM),
        runner_caps=touches['Runner_v2'] or s.get('captures', 0),
        camper_caps=touches['Camper_v2'],
        pickups=sum(picks[n] for n in RUNNER_TEAM) or s.get('runner_pickups', 0),
        runner_weapon_deaths=sum(v for k, v in rd.items() if k not in NON_WEAPON),
        runner_falls=rd['MOD_FALLING'],
        runners_falls=rd['MOD_FALLING'] + rd2['MOD_FALLING'],
        runners_weapon_deaths=sum(v for k, v in (rd + rd2).items() if k not in NON_WEAPON),
        camper_deaths=sum(cd.values()),
        camper_falls=cd['MOD_FALLING'],
        runners_self_kills=rd['MOD_SUICIDE'] + rd2['MOD_SUICIDE'],
        defender_deaths=defender_deaths(d),
        # Touches of the blue flag by the red declared defenders; Runner2/Camper2 pickups and the blue defenders'
        # own returns are not theirs.
        defender_enemy_flag=touches['Defender_v2'] + touches['Chaser_v2'],
        blue_caps=s.get('captures_by_team', {}).get('blue', 0),
        red_caps=s.get('captures_by_team', {}).get('red', 0),
    )
    m['red_minus_blue'] = m['red_caps'] - m['blue_caps']
    m['all_caps'] = m['red_caps'] + m['blue_caps']
    m['def_enemy_pickups'], out_gap, counts = two_sided(d)
    m['team_returns'] = float('nan')
    if counts:
        m.update(counts)
    out = {k: v * scale for k, v in m.items()}
    out['max_cap_gap'] = out_gap
    out['_caps_raw'], out['_picks_raw'] = m['team_caps'], m['pickups']
    log = os.path.join(d, 'live', 'server', 'base', 'play-game.log')
    out['_hash'] = hashlib.sha256(open(log, 'rb').read()).hexdigest() if os.path.isfile(log) else d
    out['_crash'] = bool(glob.glob(os.path.join(d, 'live', '**', 'jampgame_crash*'), recursive=True))
    out['_short'] = seconds < .9 * declared
    # Declared two-sided (session.json, else two addrunner lines in the start config) but no
    # Runner2_v2 in the game: scored one-sided, its team_caps would count flag returns.
    if 'two_sided' in info:
        two = bool(info['two_sided'])
    else:
        cfg = os.path.join(d, 'live', 'server', 'base', 'play_start.cfg')
        two = os.path.isfile(cfg) and open(cfg, errors='replace').read().count('addrunner') >= 2
    out['_roster'] = two and counts is None
    out['_wall'] = info.get('wall_seconds')
    return out


def arm(prefix):
    ws = [(d, world_metrics(d)) for d in worlds(prefix)]
    bad = [d for d, w in ws if w['_crash']]
    roster = [d for d, w in ws if not w['_crash'] and w['_roster']]
    short = [d for d, w in ws if not w['_crash'] and not w['_roster'] and w['_short']]
    good = [w for d, w in ws if not w['_crash'] and not w['_roster']]
    seen, unique = set(), []
    for w in good:   # a replayed world (identical game log) is one observation
        if w['_hash'] not in seen:
            seen.add(w['_hash'])
            unique.append(w)
    return dict(name=prefix.replace('\\', '/').rstrip('/').split('/')[-1], worlds=unique, bad=bad, roster=roster, short=short,
                distinct=len(unique), replays=len(good) - len(unique))


def values(a, key):
    if key == 'conversion':
        return None
    return [w[key] for w in a['worlds'] if w[key] == w[key]]   # no NaN: a metric a world does not have


def pooled_conversion(ws):
    caps, picks = sum(w['_caps_raw'] for w in ws), sum(w['_picks_raw'] for w in ws)
    return caps / picks if picks else float('nan')


def conversion_se(ws, reps=2000):
    # World bootstrap (deterministic seed): events cluster inside worlds.
    import random
    rng = random.Random(1)
    if len(ws) < 2:
        return float('nan')
    boot = [pooled_conversion([ws[rng.randrange(len(ws))] for _ in ws]) for _ in range(reps)]
    boot = [b for b in boot if b == b]
    return statistics.stdev(boot) if len(boot) > 1 else float('nan')


def stat(a, key):
    if key == 'conversion':
        return pooled_conversion(a['worlds']), conversion_se(a['worlds'])
    v = values(a, key)
    if not v:
        return float('nan'), float('nan')
    return statistics.mean(v), (statistics.stdev(v) / math.sqrt(len(v)) if len(v) > 1 else float('nan'))


def _betacf(a, b, x):
    # Continued fraction for the regularized incomplete beta (Numerical Recipes, betacf).
    qab, qap, qam, c, d = a + b, a + 1, a - 1, 1.0, 1 - (a + b) * x / (a + 1)
    d = 1 / (d if abs(d) > 1e-300 else 1e-300)
    h = d
    for m in range(1, 300):
        m2 = 2 * m
        for aa in (m * (b - m) * x / ((qam + m2) * (a + m2)), -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))):
            d = 1 + aa * d
            d = 1 / (d if abs(d) > 1e-300 else 1e-300)
            c = 1 + aa / c if abs(1 + aa / c) > 1e-300 else 1e-300
            h *= d * c
        if abs(d * c - 1) < 1e-12:
            break
    return h


def _ibeta(a, b, x):
    if x <= 0 or x >= 1:
        return 0.0 if x <= 0 else 1.0
    front = math.exp(math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1 - x))
    return front * _betacf(a, b, x) / a if x < (a + 1) / (a + b + 2) else 1 - front * _betacf(b, a, 1 - x) / b


def t_quantile(p, df):
    """Student t quantile (p in (0.5, 1)), by bisection on the CDF; the normal quantile for df >= 1000."""
    def cdf(t):
        return 1 - .5 * _ibeta(df / 2, .5, df / (df + t * t))
    lo, hi = 0.0, 1e3
    for _ in range(200):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if cdf(mid) < p else (lo, mid)
    return (lo + hi) / 2


EFFECT_P = .5 * math.erfc(2.5 / math.sqrt(2))   # one tail of 2.5 normal standard errors, about 0.62%


def welch_df(base, other, key, sb, so):
    if key == 'conversion':
        return max(1, min(len(base['worlds']), len(other['worlds'])) - 1)
    nb, no = len(values(base, key) or ()), len(values(other, key) or ())
    num = (sb * sb + so * so) ** 2
    den = (sb ** 4 / (nb - 1) if nb > 1 else 0) + (so ** 4 / (no - 1) if no > 1 else 0)
    return num / den if den > 0 else float('inf')


def verdict(base, other, key, tolerance):
    mb, sb = stat(base, key)
    mo, so = stat(other, key)
    diff, se = mo - mb, math.sqrt(sb * sb + so * so)
    if not (se == se) or se == 0:
        return diff, se, 'undecided'
    df = welch_df(base, other, key, sb, so)
    effect_cut = t_quantile(1 - EFFECT_P, df) if df < 1000 else 2.5
    null_cut = t_quantile(.975, df) if df < 1000 else 1.96
    if abs(diff) >= effect_cut * se:
        return diff, se, 'effect'
    if abs(diff) + null_cut * se <= tolerance * abs(mb):
        return diff, se, 'null'
    return diff, se, 'undecided'


def report(base, arms, primary, tolerance):
    rows = []
    for a in [base] + arms:
        line = '%-14s n=%d distinct=%d' % (a['name'], len(a['worlds']), a['distinct'])
        if a.get('replays'):
            line += ' replays=%d (counted once)' % a['replays']
        if a['bad']:
            line += ' left out (crash): %s' % ', '.join(os.path.basename(b) for b in a['bad'])
        if a.get('roster'):
            line += ' left out (declared two-sided, second runner missing): %s' % ', '.join(os.path.basename(b) for b in a['roster'])
        if a.get('short'):
            line += ' SHORT (under 90%% of the declared game time, kept): %s' % ', '.join(os.path.basename(b) for b in a['short'])
        walls = [w['_wall'] for w in a['worlds'] if w['_wall']]
        if walls:
            line += ' wall/world %.0f s' % statistics.mean(walls)
        rows.append(line)
    rows.append('')
    rows.append('%-26s %16s' % ('metric (per 600 game s)', base['name']) + ''.join(' %26s' % a['name'] for a in arms))
    for key, label in METRICS:
        if key != 'conversion' and not any(values(a, key) for a in [base] + arms):
            continue
        mb, sb = stat(base, key)
        cells = ['%7.2f (%4.2f)' % (mb, sb)]
        for a in arms:
            mo, so = stat(a, key)
            diff, se, v = verdict(base, a, key, tolerance)
            cells.append('%7.2f %+6.2f+-%4.2f %s' % (mo, diff, se, {'effect': '*', 'null': '=', 'undecided': ' '}[v]))
        rows.append('%-26s %16s' % (key, cells[0]) + ''.join(' %26s' % c for c in cells[1:]))
    rows.append('')
    rows.append('* |diff| past the t cut-off (2.5 normal se at its level, Welch df); = 95%% t interval inside +-%.0f%% of the baseline; primary metric: %s' % (100 * tolerance, primary))
    out = {}
    for a in arms:
        diff, se, v = verdict(base, a, primary, tolerance)
        out[a['name']] = dict(verdict=v, diff=diff, se=se, n=len(a['worlds']))
        rows.append('%s: %s (%s %+.2f +- %.2f)' % (a['name'], v, primary, diff, se))
    return '\n'.join(rows), out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('baseline')
    ap.add_argument('arms', nargs='+')
    ap.add_argument('--primary', default='team_caps', choices=[k for k, _ in METRICS])
    ap.add_argument('--tolerance', type=float, default=.15)
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()
    text, out = report(arm(args.baseline), [arm(a) for a in args.arms], args.primary, args.tolerance)
    print(json.dumps(out, indent=1) if args.json else text)


if __name__ == '__main__':
    main()
