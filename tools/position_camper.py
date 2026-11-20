"""The camper's trade: offense damage to the enemy carrier, and where the
non-carrying offense stands when flags change hands.

Record docs/movement/records/2026-09-27-position-pilot.md ("The camper's
trade"). Offline only.

    python tools/position_camper.py [sheets|demos|all] [--out DIR]

sheets  every pug stat sheet: each offense row's damage dealt to the enemy
        carrier (`Dmg` then `Fc`) per minute not holding (`Time` minus
        `Hold`), per player across matches; within a team's offense pair,
        whether the one who held less dealt more.
demos   the pilot's offense POVs (extract-v3): when the team's own carrier
        drops the enemy flag, whether the team retakes within 10 s by the
        POV's distance from the enemy stand; when the own flag comes home,
        while the team holds the enemy flag, where the POV was and whether
        the team capped within 20 s.
"""
import argparse
import bisect
import collections
import math
from pathlib import Path
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp

MIN_NONHOLD_MIN = 5
MIN_MATCHES = 10
NEAR_STAND = 1000


def seconds(v):
    """'19:35' (m:s), '5m56s', '46s' -> seconds."""
    v = v.strip()
    if not v:
        return 0
    if ':' in v:
        m, s = v.split(':', 1)
        return int(m) * 60 + int(s)
    m = re.fullmatch(r'(?:(\d+)m)?(?:(\d+)s)?', v)
    return (int(m.group(1) or 0) * 60 + int(m.group(2) or 0)) if m else 0


def sheet_teams(path):
    """The first table of a sheet as two teams of row dicts."""
    lines = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    for i, line in enumerate(lines):
        if not (line.startswith('Name ') and ' Pos ' in line and ' Dmg ' in line) or i + 1 >= len(lines):
            continue
        spans, start = [], 0
        for part in lines[i + 1].split(' '):
            if part:
                spans.append((start, start + len(part)))
            start += len(part) + 1
        heads = [line[a:b].strip() for a, b in spans]
        if 'Dmg' not in heads or 'Hold' not in heads:
            return None
        k = heads.index('Dmg')
        teams, cur = [], []
        for row in lines[i + 2:]:
            if not row.strip():
                break
            if row.startswith('TOTAL'):
                cells = [row[a:b].strip() for a, b in spans]
                teams.append((cur, dict(zip(heads, cells))))
                cur = []
                continue
            if row.startswith('---'):
                continue
            cells = [row[a:b].strip() for a, b in spans]
            d = dict(zip(heads, cells))
            d['DmgFc'] = cells[k + 1]
            d['key'] = (d.get('Alias') or cells[0]).lower()
            cur.append(d)
        return teams if len(teams) == 2 else None
    return None


def cmd_sheets(say):
    say('== Offense damage to the enemy carrier, per minute not holding (stat sheets) ==')
    per_player = collections.defaultdict(list)
    pairs = [0, 0, 0]     # pairs, less-hold dealt more, ties
    rates = []            # (the team camper's rate, team won)
    rows_n = sheets_n = 0
    for sub in sorted(pp.sheets_dir().iterdir()):
        for p in sub.glob('pug_*.txt'):
            teams = sheet_teams(p)
            if not teams:
                continue
            sheets_n += 1
            caps = []
            for rows, total in teams:
                try:
                    caps.append(int(total.get('Cap', 0) or 0))
                except ValueError:
                    caps.append(0)
            for ti, (rows, total) in enumerate(teams):
                offense = []
                for r in rows:
                    if r.get('Pos', '').split(' ')[0] != 'Off':
                        continue
                    try:
                        dmg, t, hold = int(r['DmgFc']), seconds(r['Time']), seconds(r['Hold'])
                    except (ValueError, KeyError):
                        continue
                    nonhold = (t - hold) / 60
                    offense.append((r['key'], dmg, hold, nonhold))
                    if nonhold >= MIN_NONHOLD_MIN:
                        rows_n += 1
                        per_player[r['key']].append(dmg / nonhold)
                if len(offense) == 2:
                    (ka, da, ha, _), (kb, db, hb, _) = offense
                    if ha != hb:
                        pairs[0] += 1
                        less, more = (da, db) if ha < hb else (db, da)
                        pairs[1] += less > more
                        pairs[2] += less == more
                    # The team's camper (less hold): its rate against the result.
                    k = min(offense, key=lambda x: x[2])
                    if k[3] >= MIN_NONHOLD_MIN and caps[0] != caps[1]:
                        rates.append((k[1] / k[3], caps[ti] > caps[1 - ti]))
    say('  sheets %d, offense rows with %d+ min not holding: %d' % (sheets_n, MIN_NONHOLD_MIN, rows_n))
    meds = {k: statistics.median(v) for k, v in per_player.items() if len(v) >= MIN_MATCHES}
    m = sorted(meds.values())
    say('  players with %d+ such matches: %d; their median rate: p25 %.1f, median %.1f, p75 %.1f, p90 %.1f (top/bottom quartile ratio %.1f)' % (
        MIN_MATCHES, len(m), pp.q(m, .25), pp.q(m, .5), pp.q(m, .75), pp.q(m, .9), pp.q(m, .75) / max(1e-9, pp.q(m, .25))))
    within = [statistics.pstdev(v) for k, v in per_player.items() if len(v) >= MIN_MATCHES]
    say('  spread: between players (sd of medians) %.1f; within a player (median of match-to-match sd) %.1f' % (
        statistics.pstdev(m), statistics.median(within)))
    say('  offense pairs with different hold: %d; the one who held less dealt more carrier damage in %.0f%% (ties %d)' % (
        pairs[0], 100 * pairs[1] / max(1, pairs[0]), pairs[2]))
    rates.sort()
    if rates:
        n = len(rates)
        for lo, hi, name in ((0, n // 4, 'bottom quarter'), (n // 4, 3 * n // 4, 'middle half'), (3 * n // 4, n, 'top quarter')):
            part = rates[lo:hi]
            say('  camper rate %-14s (%.1f-%.1f /min): team won %.0f%% of %d decided matches' % (
                name, part[0][0], part[-1][0], 100 * sum(w for _, w in part) / len(part), len(part)))


def cmd_demos(say):
    from position_resources import Demo, pct
    say('\n== Offense POV (not carrying) when flags change hands (pilot demos) ==')
    drops = collections.defaultdict(lambda: [0, 0])
    homes = collections.defaultdict(lambda: [0, 0])
    for d in pp.load(pp.DEFAULT_OUT):
        if d['pos'] != 'Off':
            continue
        dem = Demo(d)
        enemy = pp.TEAM_BLUE if dem.team == pp.TEAM_RED else pp.TEAM_RED
        estand, ostand = dem.stands[enemy], dem.stands[dem.team]
        flags = [(t, pp.flag_state(st, dem.team)) for t, st in d['flags']]
        flags = [(t, f) for t, f in flags if f]
        team_deaths = [t for t, v, a, m in d['obits'] if dem.name_team(v, t)[1] == dem.team]
        for (t0, a), (t1, b) in zip(flags, flags[1:]):
            s = dem.at(t1)
            if not s or s[0] not in dem.live or not s[3][9] or s[3][8]:
                continue
            dist = math.dist(s[3][2:4], estand)
            near = dist < NEAR_STAND
            if a[1] == '1' and b[1] == '2':
                # Own carrier dropped the enemy flag: retaken within 10 s?
                nxt = next(((t, f) for t, f in flags if t > t1 and f[1] != '2'), None)
                retook = nxt is not None and nxt[1][1] == '1' and nxt[0] - t1 <= 10000
                drops[near][0] += 1
                drops[near][1] += retook
            if a[0] in ('1', '2') and b[0] == '0' and b[1] == '1':
                # Own flag home: a cap by the team within 20 s? The enemy flag
                # going straight home is a cap unless a teammate (its carrier,
                # in a pit) died just before.
                capped = any(t1 < tb <= t1 + 20000 and fa[1] == '1' and fb[1] == '0'
                             and not any(tb - 1500 <= to <= tb for to in team_deaths)
                             for (ta, fa), (tb, fb) in zip(flags, flags[1:]))
                homes[near][0] += 1
                homes[near][1] += capped
    for near in (True, False):
        n, k = drops[near]
        say('  own carrier drops the enemy flag, POV %s the enemy stand: %d; team retakes within 10 s %s' % (
            'within %d u of' % NEAR_STAND if near else 'farther from', n, pct(k, n)))
    for near in (True, False):
        n, k = homes[near]
        say('  own flag comes home while the team holds the enemy flag, POV %s the enemy stand: %d; team caps within 20 s %s' % (
            'within %d u of' % NEAR_STAND if near else 'farther from', n, pct(k, n)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', nargs='?', default='all', choices=('sheets', 'demos', 'all'))
    ap.add_argument('--out', type=Path, default=pp.DEFAULT_OUT)
    args = ap.parse_args()
    lines = []

    def say(s=''):
        lines.append(s)
        print(s, flush=True)

    if args.what in ('sheets', 'all'):
        cmd_sheets(say)
    if args.what in ('demos', 'all'):
        cmd_demos(say)
    (args.out / ('camper-%s.txt' % args.what)).write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
