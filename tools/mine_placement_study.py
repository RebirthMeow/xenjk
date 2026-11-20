"""Trip mine placement study: where strong human base players lay mines.

Offline only: reads local demos (dm26.py), the Discord pug sheets and the map
fixtures; never connects to a server. Output folder default
.codex-research/mines-20261001-a (input bas_matches.json: alias -> match ids
where that player played Pos "Bas"; optional players.json: alias -> the name
in the sheets, default the alias). Both inputs stay local: they name players.

    python tools/mine_placement_study.py select  [--out DIR]
    python tools/mine_placement_study.py extract [--out DIR] [--jobs N]
    python tools/mine_placement_study.py report  [--out DIR]

select  index rows (human-rush/index.csv) of those matches whose demo exists,
        plus each target's in-game name from the match's sheet(s).
extract every trip mine entity (ET_GENERAL, weapon WP_TRIP_MINE) per demo as
        an instance: owner (genericenemyindex - MAX_GENTITIES), launch point
        and time (pos.trTime while TR_GRAVITY), rest point, surface normal and
        stick time (pos.trBase / trDelta / trTime once TR_STATIONARY), mode
        (bolt2 == 1 proximity, set at stick; EF_FIRING without bolt2 =
        tripwire, armed 1 s after stick), the explosion event (EV_MISSILE_MISS
        on the mine) or the last snapshot it was seen; plus player positions,
        names/teams, flag status and obituaries.
report  merges the POVs of a match on server time, keeps the targets' mines,
        writes mines.csv and report.md.
"""
import argparse
import bisect
from concurrent.futures import ProcessPoolExecutor, as_completed
import collections
import csv
import glob
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp

DEFAULT_OUT = pp.ROOT / '.codex-research' / 'mines-20261001-a'
EXTRACT_DIR = 'extract-v2'   # v1 lacked the warmup / level start configstrings

ET_GENERAL, ET_PLAYER, ET_ITEM = 0, 1, 2
WP_TRIP_MINE = 13
MAX_GENTITIES = 1024
TR_STATIONARY, TR_GRAVITY = 0, 6
EF_FIRING = 0x200
EV_MISSILE_MISS = 86
EV_OBITUARY = 93
MOD_TRIP_MINE_SPLASH = 25
MINE_MODS = (25, 26)      # these servers report proximity mine kills as MOD_TIMED_MINE_SPLASH (26)
PROX_RANGE = 128          # proxMineThink: splashRadius / 2
SPLASH_RADIUS = 256
CHAIN_RADIUS = 244        # 105 * (1 - d/256) >= 5 health
PROX_LIFE_MS = 30000
FLAG_ITEMS = (46, 47)     # base_enhanced item list: red, blue flag (checked against fixtures in report)


# ---------------------------------------------------------------- select

def sheet_names(mid, alias):
    names = set()
    for p in glob.glob(str(pp.sheets_dir() / '*' / ('pug_%s-*.txt' % mid))):
        for key, row in pp.parse_sheet(p).items():
            if row.get('Alias', '').lower() == alias and row.get('Pos', '').startswith('Bas'):
                names.add(key)
    return sorted(names)


def load_aliases(out):
    """bas_matches.json key -> the player's name in the pug sheets: players.json beside it (local input, like
    bas_matches.json), else the key itself."""
    want = json.loads((out / 'bas_matches.json').read_text(encoding='utf-8'))
    names = out / 'players.json'
    names = json.loads(names.read_text(encoding='utf-8')) if names.exists() else {}
    return {k: names.get(k, k) for k in want}


def cmd_select(args):
    want = json.loads((args.out / 'bas_matches.json').read_text(encoding='utf-8'))
    aliases = load_aliases(args.out)
    targets = collections.defaultdict(list)   # mid -> [(alias, name_key)]
    for k, mids in want.items():
        for mid in mids:
            for n in sheet_names(mid, aliases[k]):
                targets[mid].append([aliases[k], n])
            if not any(a == aliases[k] for a, _ in targets[mid]):
                targets[mid].append([aliases[k], None])
    rows = [r for r in csv.DictReader(pp.INDEX.open(encoding='utf-8')) if r['match_id'] in targets]
    sel = []
    for r in rows:
        r = dict(r, exists=int(os.path.exists(r['demo'])))
        sel.append(r)
    with (args.out / 'selection.csv').open('w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(sel[0].keys()))
        w.writeheader()
        w.writerows(sel)
    (args.out / 'targets.json').write_text(json.dumps(targets, indent=1), encoding='utf-8')
    print('matches %d with demo rows %d, rows %d existing %d' % (
        len(targets), len({r['match_id'] for r in sel}), len(sel), sum(r['exists'] for r in sel)))


# ---------------------------------------------------------------- extract

def extract_demo(row):
    sys.path.insert(0, str(pp.dm26_dir()))
    import dm26
    E = dm26.ENT_IDX
    fx, fy, fz = E['pos.trBase[0]'], E['pos.trBase[1]'], E['pos.trBase[2]']
    dx, dy, dz = E['pos.trDelta[0]'], E['pos.trDelta[1]'], E['pos.trDelta[2]']
    et, wp, trt, trtime = E['eType'], E['weapon'], E['pos.trType'], E['pos.trTime']
    evf, evp, eff, b2, gei = E['event'], E['eventParm'], E['eFlags'], E['bolt2'], E['genericenemyindex']
    pwf, mi = E['powerups'], E['modelindex']
    o1, o2 = E['otherEntityNum'], E['otherEntityNum2']
    parser = dm26.DemoParser()
    out = dict(demo=row['demo'], map=row['map'], match_id=row['match_id'], pov_name=row['pov_name'],
               names={}, flags=[], warm=[], obits=[], players=[], mines=[], flag_items={}, pov=None, error='')
    live = {}           # entity number -> instance
    obit_seen, last_flag, cs_cache, last_warm = {}, None, {}, None
    try:
        for ev in parser.parse_file(row['demo']):
            if ev[0] == 'gamestate':
                for inst in live.values():
                    inst['end'] = inst.get('end') or 'gamestate'
                live = {}
                continue
            if ev[0] != 'snapshot':
                continue
            snap = ev[1]
            ps, t = snap.ps, snap.serverTime
            me = ps['clientNum']
            out['pov'] = me
            for c in range(32):
                cs = parser.configstrings.get(dm26.CS_PLAYERS + c, '')
                if cs_cache.get(c) == cs:
                    continue
                cs_cache[c] = cs
                if cs:
                    info = dm26.parse_info_string(str(cs).strip().strip('"'))
                    nm, tm = pp.strip_colors(info.get('n', '')), int(info.get('t', 0) or 0)
                else:
                    nm, tm = '', 0
                out['names'].setdefault(c, []).append([t, nm, tm])
            status = str(parser.configstrings.get(pp.CS_FLAGSTATUS, '')).strip().strip('"')
            if status != last_flag:
                out['flags'].append((t, status))
                last_flag = status
            warm = (str(parser.configstrings.get(pp.CS_WARMUP, '')).strip().strip('"'),
                    str(parser.configstrings.get(21, '')).strip().strip('"'))   # CS_LEVEL_START_TIME
            if warm != last_warm:
                out['warm'].append((t,) + warm)
                last_warm = warm
            players = [[me, round(ps['origin[0]']), round(ps['origin[1]']), round(ps['origin[2]']),
                        int(bool(ps['powerups'][pp.PW_REDFLAG])) | 2 * int(bool(ps['powerups'][pp.PW_BLUEFLAG])),
                        int(ps['stats'][0] > 0)]]
            seen = set()
            ended_now = []
            for num, st in snap.entities:
                e = st[et]
                if num < 32 and e == ET_PLAYER and num != me:
                    players.append([num, round(st[fx]), round(st[fy]), round(st[fz]),
                                    int(bool(st[pwf] & (1 << pp.PW_REDFLAG))) | 2 * int(bool(st[pwf] & (1 << pp.PW_BLUEFLAG))),
                                    int(not st[eff] & pp.EF_DEAD)])
                elif e == pp.ET_EVENTS + EV_OBITUARY:
                    key = (st[o1], st[o2], st[evp])
                    if t - obit_seen.get(key, -10 ** 9) > 1500:
                        out['obits'].append((t, st[o1], st[o2], st[evp]))
                    obit_seen[key] = t
                elif e == ET_ITEM and st[mi] in FLAG_ITEMS and st[trt] == TR_STATIONARY:
                    k = '%d %d %d %d' % (st[mi], round(st[fx]), round(st[fy]), round(st[fz]))
                    out['flag_items'][k] = out['flag_items'].get(k, 0) + 1
                elif e == ET_GENERAL and st[wp] == WP_TRIP_MINE:
                    seen.add(num)
                    owner = st[gei] - MAX_GENTITIES
                    pos = (round(st[fx], 1), round(st[fy], 1), round(st[fz], 1))
                    stuck = st[trt] == TR_STATIONARY
                    inst = live.get(num)
                    if inst and (inst['owner'] != owner or inst['end'] or
                                 (inst['rest'] and (not stuck or math.dist(inst['rest'], pos) > 2))):
                        if not inst['end']:
                            inst['end'] = 'replaced'
                        inst = None
                    if inst is None:
                        inst = dict(num=num, owner=owner, first_t=t, first_stuck=int(stuck), launch=None,
                                    launch_t=None, launch_speed=None, thrower=None, rest=None, normal=None,
                                    stick_t=None, rest_seen_t=None, mode=None, armed_t=None, last_t=t,
                                    explode_t=None, end=None, near_end=None, gaps=0)
                        live[num] = inst
                        out['mines'].append(inst)
                    elif t - inst['last_t'] > 120:
                        inst['gaps'] += 1
                    inst['last_t'] = t
                    if not stuck and inst['launch'] is None:
                        inst['launch'] = pos
                        inst['launch_t'] = st[trtime]
                        inst['launch_speed'] = round(math.sqrt(st[dx] ** 2 + st[dy] ** 2 + st[dz] ** 2))
                    if stuck and inst['rest'] is None:
                        inst['rest'] = pos
                        inst['normal'] = (round(st[dx], 2), round(st[dy], 2), round(st[dz], 2))
                        inst['stick_t'] = st[trtime]
                        inst['rest_seen_t'] = t
                    if stuck:
                        if st[b2] == 1:
                            inst['mode'] = 'prox'
                        elif st[eff] & EF_FIRING:
                            inst['mode'] = 'trip'
                            if inst['armed_t'] is None:
                                inst['armed_t'] = t
                        elif inst['mode'] is None:
                            inst['mode'] = 'trip?'
                    if st[evf] and (st[evf] & ~0x300) == EV_MISSILE_MISS and inst['explode_t'] is None:
                        inst['explode_t'] = t
                        inst['end'] = 'exploded'
                        ended_now.append(inst)
            # thrower position: owner in this snapshot at the first in-flight sighting
            for inst in live.values():
                if inst['launch'] is not None and inst['thrower'] is None and inst['first_t'] == t:
                    for p in players:
                        if p[0] == inst['owner']:
                            inst['thrower'] = p[1:4]
            for inst in ended_now:
                inst['near_end'] = sorted([[p[0], round(math.dist(p[1:4], inst['rest'] or inst['launch'] or p[1:4])),
                                            p[1], p[2], p[3], p[4], p[5]] for p in players],
                                          key=lambda q: q[1])[:6]
            for num in list(live):
                if num not in seen and live[num]['end']:
                    del live[num]
            out['players'].append([t, players])
        for inst in live.values():
            if not inst['end']:
                inst['end'] = 'vanished'
        for inst in out['mines']:
            if not inst['end'] or inst['end'] == 'replaced':
                inst['end'] = 'vanished'
    except Exception as exc:
        out['error'] = '%s: %s' % (type(exc).__name__, exc)
    return out


def extract_path(dest, r):
    return dest / (hashlib.sha1(r['demo'].encode()).hexdigest()[:16] + '.json.gz')


def cmd_extract(args):
    rows = [r for r in csv.DictReader((args.out / 'selection.csv').open(encoding='utf-8')) if r['exists'] == '1']
    dest = args.out / EXTRACT_DIR
    dest.mkdir(parents=True, exist_ok=True)
    todo = [r for r in rows if not extract_path(dest, r).exists()]
    print('extracting %d of %d' % (len(todo), len(rows)), flush=True)
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(extract_demo, r): r for r in todo}
        for i, f in enumerate(as_completed(futs), 1):
            r = futs[f]
            d = f.result()
            with gzip.open(extract_path(dest, r), 'wt', encoding='utf-8') as g:
                json.dump(d, g)
            print('%d/%d %s %s mines=%d %s' % (i, len(todo), d['map'], d['match_id'], len(d['mines']), d['error']),
                  flush=True)


# ---------------------------------------------------------------- report

ARM_MS = {'prox': 2000, 'trip': 1000, 'trip?': 1000}   # LT_ALT_TIME / LT_ACTIVATION_DELAY
BASE_RANGE = 1200          # a mine within this 2D distance of its owner's stand is a base mine
TRIGGER_TOL = 150          # player within PROX_RANGE + this slack at the explosion snapshot
KEJIM_CENTRE = (1308, 2076)  # Kejim is point-symmetric: blue (x, y) -> (2616 - x, 4152 - y)


def name_cmp(s):
    return ''.join(ch for ch in pp.strip_colors(s or '')[:18].lower() if ch.isalnum())


def map_key(m):
    m = m.lower().replace('mp/', '')
    return m[4:] if m.startswith('ctf_') else m


class Match:
    def __init__(self, mid, demos, targets):
        self.mid, self.demos = mid, demos
        self.map = map_key(demos[0]['map'])
        self.names = collections.defaultdict(list)
        for d in demos:
            for c, tl in d['names'].items():
                self.names[int(c)].extend(tl)
        for c in self.names:
            self.names[c].sort(key=lambda r: r[0])
        self.targets = {}          # client -> alias
        for alias, key in targets:
            if not key:
                continue
            for c, tl in self.names.items():
                if any(name_cmp(nm) == name_cmp(key) and tm in (pp.TEAM_RED, pp.TEAM_BLUE) for _, nm, tm in tl):
                    self.targets[c] = alias
        # player tracks merged over POVs
        tr = collections.defaultdict(dict)
        for d in demos:
            for t, players in d['players']:
                for p in players:
                    tr[p[0]].setdefault(t, p[1:])
        self.track = {}
        for c, dd in tr.items():
            ts = sorted(dd)
            self.track[c] = (ts, [dd[t] for t in ts])
        obits = {}
        for d in demos:
            for o in d['obits']:
                key = (o[1], o[2], o[3], o[0] // 2000)
                obits.setdefault(key, o)
        self.obits = sorted(obits.values())
        self.level_start = sorted({(w[0], int(w[2] or 0)) for d in demos for w in d.get('warm', []) if w[2]})
        self.flags = sorted({tuple(f) for d in demos for f in d['flags']})
        self.mines = self.merge_mines()

    def team_of(self, c, t):
        tl = self.names.get(c)
        if not tl:
            return 0
        i = bisect.bisect_right([r[0] for r in tl], t) - 1
        return tl[max(i, 0)][2]

    def at(self, c, t, tol=150):
        tr = self.track.get(c)
        if not tr:
            return None
        ts, rows = tr
        i = bisect.bisect_left(ts, t)
        best = None
        for j in (i - 1, i):
            if 0 <= j < len(ts) and abs(ts[j] - t) <= tol and (best is None or abs(ts[j] - t) < abs(ts[best] - t)):
                best = j
        return rows[best] if best is not None else None

    def window(self, c, t0, t1):
        tr = self.track.get(c)
        if not tr:
            return []
        ts, rows = tr
        i, j = bisect.bisect_left(ts, t0), bisect.bisect_right(ts, t1)
        return list(zip(ts[i:j], rows[i:j]))

    def merge_mines(self):
        out = []
        by = collections.defaultdict(list)
        for di, d in enumerate(self.demos):
            for m in d['mines']:
                by[(m['owner'], m['num'])].append((di, m))
        for key, items in by.items():
            groups = []
            for di, m in sorted(items, key=lambda x: x[1]['first_t']):
                for g in groups:
                    a = g[0][1]
                    same = (m['launch_t'] is not None and m['launch_t'] == a['launch_t']) or (
                        m['rest'] and a['rest'] and math.dist(m['rest'], a['rest']) < 3 and
                        abs((m['stick_t'] or 0) - (a['stick_t'] or 0)) < 100)
                    if same:
                        g.append((di, m))
                        break
                else:
                    groups.append([(di, m)])
            for g in groups:
                ms = [m for _, m in g]
                r = dict(ms[0])
                r['povs'] = sorted({self.demos[di]['pov'] for di, _ in g})
                for f in ('launch', 'launch_t', 'launch_speed', 'thrower', 'rest', 'normal', 'stick_t', 'armed_t'):
                    vals = [m[f] for m in ms if m[f] is not None]
                    r[f] = vals[0] if vals else None
                modes = [m['mode'] for m in ms if m['mode']]
                r['mode'] = 'prox' if 'prox' in modes else 'trip' if 'trip' in modes else modes[0] if modes else None
                r['first_t'] = min(m['first_t'] for m in ms)
                r['last_t'] = max(m['last_t'] for m in ms)
                ex = [m for m in ms if m['explode_t'] is not None]
                r['explode_t'] = min(m['explode_t'] for m in ex) if ex else None
                r['near_end'] = ex[0]['near_end'] if ex else None
                r['end'] = 'exploded' if ex else 'vanished'
                out.append(r)
        out.sort(key=lambda m: m['launch_t'] or m['stick_t'] or m['first_t'])
        return out


def load_stands(matches):
    """Flag item home positions (modal stationary flag item per model) per map; fixtures when present."""
    votes = collections.defaultdict(collections.Counter)
    for m in matches:
        for d in m.demos:
            for k, n in d['flag_items'].items():
                mi, x, y, z = map(int, k.split())
                votes[(m.map, mi)][(x, y, z)] += n
    stands = {}
    for (mk, mi), c in votes.items():
        (x, y, z), _ = c.most_common(1)[0]
        stands.setdefault(mk, {})[pp.TEAM_RED if mi == FLAG_ITEMS[0] else pp.TEAM_BLUE] = (x, y, z)
    check = {}
    for mk in stands:
        f = pp.FIXTURES / mk / 'cases.json'
        if f.exists():
            try:
                fs = pp.flag_stands(mk)
                check[mk] = max(math.dist(fs[t], stands[mk][t][:2]) for t in fs)
            except Exception:
                pass
    return stands, check


def surface(n):
    if not n:
        return 'unknown'
    return 'floor' if n[2] > 0.7 else 'ceiling' if n[2] < -0.7 else 'wall' if abs(n[2]) < 0.3 else 'slope'


def frame(stands, team, p):
    """(2D dist, signed bearing deg from the line to the enemy stand, +left, height) from own stand."""
    own = stands[team]
    en = stands[pp.TEAM_BLUE if team == pp.TEAM_RED else pp.TEAM_RED]
    vx, vy = p[0] - own[0], p[1] - own[1]
    ax, ay = en[0] - own[0], en[1] - own[1]
    b = math.degrees(math.atan2(ax * vy - ay * vx, ax * vx + ay * vy))
    return math.hypot(vx, vy), b, p[2] - own[2]


def to_red_kejim(p, team):
    if team == pp.TEAM_BLUE:
        return (2 * KEJIM_CENTRE[0] - p[0], 2 * KEJIM_CENTRE[1] - p[1], p[2])
    return tuple(p)


def q(vals, p):
    return pp.q(vals, p)


def qs(vals, fmt='%.0f'):
    if not vals:
        return '-'
    return '%s / %s / %s' % (fmt % q(vals, .25), fmt % q(vals, .5), fmt % q(vals, .75))


def pct(a, b):
    return '%d%%' % round(100 * a / b) if b else '-'


def classify(match, mines):
    """End class of each mine from the explosion snapshot and its neighbours."""
    exps = [(m['explode_t'], m) for m in mines if m['explode_t'] is not None]
    exps.sort(key=lambda x: x[0])
    ets = [t for t, _ in exps]
    for m in mines:
        team = match.team_of(m['owner'], m['first_t'])
        m['team'] = team
        pos = m['rest'] or m['launch']
        end_t = m['explode_t'] if m['explode_t'] is not None else m['last_t']
        m['end_t'] = end_t
        m['life'] = end_t - m['stick_t'] if m['stick_t'] is not None else None
        enemy_d, enemy_c, carrier = None, None, 0
        if pos:
            for c in match.track:
                tc = match.team_of(c, end_t)
                if tc not in (pp.TEAM_RED, pp.TEAM_BLUE) or tc == team:
                    continue
                for t, r in match.window(c, end_t - TRIGGER_TOL, end_t + 40):
                    if not r[4]:
                        continue
                    dd = math.dist(r[:3], pos)
                    if enemy_d is None or dd < enemy_d:
                        enemy_d, enemy_c = dd, c
                        carrier = int(bool(r[3] & (1 if team == pp.TEAM_RED else 2)))
        m['enemy_d'], m['enemy_c'], m['enemy_carrier'] = (round(enemy_d) if enemy_d is not None else None,
                                                          enemy_c, carrier)
        chain = None
        if pos:
            i = bisect.bisect_left(ets, end_t - 250)
            while i < len(ets) and ets[i] <= end_t + (250 if m['explode_t'] is None else 0):
                o = exps[i][1]
                if o is not m and (o['rest'] or o['launch']) and math.dist(o['rest'] or o['launch'], pos) <= SPLASH_RADIUS \
                        and o['explode_t'] <= end_t:
                    chain = o
                    break
                i += 1
        m['chain_src'] = chain
        near = enemy_d is not None and enemy_d <= PROX_RANGE + 40
        if m['explode_t'] is not None:
            if m['stick_t'] is None:
                cls = 'hit_in_flight'
            elif near:
                cls = 'enemy_trigger'
            elif m['mode'] == 'prox' and m['life'] is not None and m['life'] >= PROX_LIFE_MS - 300:
                cls = 'timeout'
            elif chain is not None:
                cls = 'chain'
            elif enemy_d is not None and enemy_d <= 600:
                cls = 'shot_or_splash'      # an enemy nearby but outside the trigger range
            else:
                cls = 'exploded_other'
        else:
            if chain is not None:
                cls = 'chain_unseen'
            elif m['mode'] == 'prox' and m['life'] is not None and abs(m['life'] - PROX_LIFE_MS) < 700:
                cls = 'timeout_unseen'
            else:
                cls = 'vanished'
        m['end_class'] = cls


def lay_sessions(ms, gap=4000):
    out, cur = [], []
    for m in ms:
        t = m['place_t']
        if cur and t - cur[-1]['place_t'] > gap:
            out.append(cur)
            cur = []
        cur.append(m)
    if cur:
        out.append(cur)
    return out


def thefts(match, team):
    """Enemy carrier episodes of `team`'s flag: (client, [(t, row)]) while carried."""
    bit = 1 if team == pp.TEAM_RED else 2
    eps = []
    for c, (ts, rows) in match.track.items():
        cur = []
        for t, r in zip(ts, rows):
            if r[3] & bit and r[4]:
                if cur and t - cur[-1][0] > 1500:
                    eps.append((c, cur))
                    cur = []
                cur.append((t, r))
            elif cur:
                eps.append((c, cur))
                cur = []
        if cur:
            eps.append((c, cur))
    return [e for e in eps if match.team_of(e[0], e[1][0][0]) not in (team, 0)]


def cmd_report(args):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    targets = json.loads((args.out / 'targets.json').read_text(encoding='utf-8'))
    rows = [r for r in csv.DictReader((args.out / 'selection.csv').open(encoding='utf-8')) if r['exists'] == '1']
    by_match, errors = collections.defaultdict(list), []
    for r in rows:
        p = extract_path(args.out / EXTRACT_DIR, r)
        if not p.exists():
            errors.append((r['demo'], 'missing extract'))
            continue
        with gzip.open(p, 'rt', encoding='utf-8') as g:
            d = json.load(g)
        if d['error']:
            errors.append((r['demo'], d['error']))
        by_match[r['match_id']].append(d)
    matches = [Match(mid, ds, targets[mid]) for mid, ds in sorted(by_match.items())]
    stands, stand_check = load_stands(matches)
    los = {}
    try:
        import position_open as po
        import base_defender_study as bds
        for mk in ('kejim', 'dantooine_v3'):
            los[mk] = bds.add_los(po.PitMap(mk)).los
    except Exception as exc:
        print('no LOS maps: %s' % exc)

    allm = []
    for m in matches:
        classify(m, m.mines)
        deaths = collections.defaultdict(list)
        for t, victim, attacker, mod in m.obits:
            deaths[victim].append(t)
        for x in m.mines:
            x['match'], x['map'] = m.mid, m.map
            x['alias'] = m.targets.get(x['owner'], '')
            x['place_t'] = x['launch_t'] if x['launch_t'] is not None else (x['stick_t'] or x['first_t'])
            ls = [s for t, s in m.level_start if t <= x['place_t']]
            x['game_s'] = round((x['place_t'] - ls[-1]) / 1000) if ls else None
            x['owner_deaths'] = deaths.get(x['owner'], [])
            x['m'] = m
            st = stands.get(m.map, {})
            x['stand_ok'] = x['team'] in st and len(st) == 2
            if x['rest'] and x['stand_ok']:
                x['dist'], x['bearing'], x['dz'] = frame(st, x['team'], x['rest'])
                x['dist3'] = math.dist(x['rest'], st[x['team']])
            else:
                x['dist'] = x['bearing'] = x['dz'] = x['dist3'] = None
            if x['thrower'] and x['stand_ok']:
                x['thrower_dist'] = frame(st, x['team'], x['thrower'])[0]
            else:
                x['thrower_dist'] = None
            x['surface'] = surface(x['normal'])
        allm.extend(m.mines)
    tm = [x for x in allm if x['alias']]
    stuck = [x for x in tm if x['rest']]
    alias_map = load_aliases(args.out)
    aliases = tuple(alias_map.values())

    # spacing / concurrency per placement
    for m in matches:
        own = collections.defaultdict(list)
        for x in m.mines:
            if x['rest'] and x['stick_t'] is not None:
                own[x['owner']].append(x)
        for c, xs in own.items():
            for x in xs:
                live = [o for o in xs if o is not x and o['stick_t'] <= x['stick_t'] < o['end_t']]
                x['live_before'] = len(live)
                ds = sorted((math.dist(o['rest'], x['rest']), o) for o in live)
                x['nn'] = round(ds[0][0]) if ds else None
                f = los.get(m.map)
                x['chainable'] = sum(1 for dd, o in ds if dd <= CHAIN_RADIUS and (
                    f is None or f((x['rest'][0], x['rest'][1], x['rest'][2] + 4), (o['rest'][0], o['rest'][1], o['rest'][2] + 4))))
                x['within244'] = sum(1 for dd, o in ds if dd <= CHAIN_RADIUS)
        team_live = collections.defaultdict(list)
        for x in m.mines:
            if x['rest'] and x['stick_t'] is not None:
                team_live[x['team']].append(x)
        for x in m.mines:
            if x['rest'] and x['stick_t'] is not None:
                x['team_live_before'] = sum(1 for o in team_live[x['team']] if o is not x and
                                            o['stick_t'] <= x['stick_t'] < o['end_t'])

    lines = []
    say = lines.append
    say('# Trip mine placement of human base players (%s)' % ', '.join(aliases))
    say('')
    say('Study 2026-10-01, offline. Tool: `tools/mine_placement_study.py` (select / extract / report). '
        'Data: `%s`. Mines CSV: `mines.csv` (one row per trip mine owned by a target player, '
        '`mines_all.csv` every owner).' % args.out.name)
    say('')
    say('Observed facts come straight from entity states in the demos: owner (genericenemyindex - 1024), '
        'rest position and surface normal (pos.trBase / pos.trDelta once pos.trType is TR_STATIONARY), stick time '
        '(pos.trTime), launch time (pos.trTime in flight), mode (bolt2 = 1 proximity; EF_FIRING without bolt2 tripwire), '
        'the explosion event (EV_MISSILE_MISS on the mine entity), player positions, flag carriers. '
        'Inferred: end causes (from who stood within the 128 u trigger range at the explosion snapshot, the 30 s '
        'proximity lifetime, and neighbouring explosions), chain exposure (distance <= 244 u; line of sight from '
        'world-brush columns on Kejim and Dantooine only).')
    say('')

    # ---- corpus
    say('## Corpus')
    say('')
    want = json.loads((args.out / 'bas_matches.json').read_text(encoding='utf-8'))
    say('| player | Bas matches (sheets) | with a local demo | demos parsed | in-game name resolved | mines thrown | stuck | maps |')
    say('|---|---|---|---|---|---|---|---|')
    for k, a in alias_map.items():
        mids = set(want[k])
        have = [m for m in matches if m.mid in mids]
        res = [m for m in have if a in m.targets.values()]
        xs = [x for x in tm if x['alias'] == a]
        mp = collections.Counter(x['map'] for x in xs if x['rest'])
        say('| %s | %d | %d | %d | %d | %d | %d | %s |' % (
            a, len(mids), len(have), sum(len(m.demos) for m in have), len(res), len(xs),
            sum(1 for x in xs if x['rest']), ', '.join('%s %d' % kv for kv in mp.most_common())))
    say('')
    say('Demos parsed: %d of %d listed rows (parse errors %d). Matches with demos: %d. '
        'Mine instances merged across POVs of one match on server time: %d (all owners), %d owned by targets.' % (
            sum(len(m.demos) for m in matches), len(rows), len(errors), len(matches), len(allm), len(tm)))
    unresolved = [(mid, a) for mid, ts in targets.items() if mid in by_match for a, n in ts
                  if not any(v == a for v in next(mm for mm in matches if mm.mid == mid).targets.values())]
    if unresolved:
        say('Unresolved target names (no sheet Bas row or name not in the demo): %s.' % ', '.join('%s %s' % u for u in unresolved))
    for e in errors:
        say('- parse error: `%s` %s' % e)
    say('Map stands are the modal home position of the flag items (models %d/%d) in the demos; on maps with '
        'movement fixtures they agree with the fixture stands within %s u.' % (
            FLAG_ITEMS[0], FLAG_ITEMS[1], max(stand_check.values()) if stand_check else '-'))
    vis = collections.Counter('first seen in flight' if not x['first_stuck'] else 'first seen at rest' for x in tm)
    say('Visibility: %s. Servers sent mines map-wide in these demos (players 6,000 u away appear in snapshots), '
        'so one POV per match sees nearly every mine; %d target mines were seen by more than one POV.' % (
            ', '.join('%s %d' % kv for kv in vis.most_common()), sum(1 for x in tm if len(x['povs']) > 1)))
    say('')

    def section(title, xs):
        say('### ' + title)
        say('')
        if not xs:
            say('none')
            say('')
            return
        say('| player | stuck mines | prox / trip | floor / wall / slope / ceiling | base (<= %d u of own stand) | '
            '2D dist from stand q25/50/75 | height vs stand q25/50/75 | thrower dist q50 |' % BASE_RANGE)
        say('|---|---|---|---|---|---|---|---|')
        for a in aliases + ('all',):
            ys = [x for x in xs if a == 'all' or x['alias'] == a]
            if not ys:
                continue
            sf = collections.Counter(x['surface'] for x in ys)
            md = collections.Counter(x['mode'] for x in ys)
            ds = [x['dist'] for x in ys if x['dist'] is not None]
            base = [x for x in ys if x['dist'] is not None and x['dist'] <= BASE_RANGE]
            say('| %s | %d | %d / %d | %d / %d / %d / %d | %s | %s | %s | %s |' % (
                a, len(ys), md['prox'], md['trip'] + md['trip?'], sf['floor'], sf['wall'], sf['slope'], sf['ceiling'],
                pct(len(base), len(ds)), qs(ds), qs([x['dz'] for x in ys if x['dz'] is not None]),
                qs([x['thrower_dist'] for x in ys if x['thrower_dist'] is not None][:]) if False else
                ('%.0f' % q([x['thrower_dist'] for x in ys if x['thrower_dist'] is not None], .5)
                 if any(x['thrower_dist'] is not None for x in ys) else '-')))
        say('')

    # ---- placement
    say('## Where they lay (rest positions relative to the own flag stand)')
    say('')
    say('Bearing is the angle at the own stand from the line toward the enemy stand (0 = toward the enemy, '
        '+ = left of that line seen from the stand, 180 = behind the stand). Height is rest z minus the flag item z.')
    say('')
    kej = [x for x in stuck if x['map'] == 'kejim']
    section('mp/ctf_kejim', kej)
    section('All maps pooled', stuck)
    say('Per map (target mines at rest):')
    say('')
    say('| map | matches | mines | prox share | base share | dist q50 | |bearing| q50 | height q50 |')
    say('|---|---|---|---|---|---|---|---|')
    for mk, c in collections.Counter(x['map'] for x in stuck).most_common():
        ys = [x for x in stuck if x['map'] == mk]
        ds = [x['dist'] for x in ys if x['dist'] is not None]
        say('| %s | %d | %d | %s | %s | %s | %s | %s |' % (
            mk, len({x['match'] for x in ys}), len(ys), pct(sum(x['mode'] == 'prox' for x in ys), len(ys)),
            pct(sum(1 for v in ds if v <= BASE_RANGE), len(ds)), '%.0f' % q(ds, .5) if ds else '-',
            '%.0f' % q([abs(x['bearing']) for x in ys if x['bearing'] is not None], .5) if ds else '-',
            '%.0f' % q([x['dz'] for x in ys if x['dz'] is not None], .5) if ds else '-'))
    say('')
    # distance / bearing histograms of base mines pooled
    say('Pooled base mines by distance band and bearing sector (counts):')
    say('')
    bands = ((0, 150), (150, 300), (300, 500), (500, 800), (800, 1200))
    secs = (('front 0-45', 0, 45), ('front-side 45-90', 45, 90), ('back-side 90-135', 90, 135), ('back 135-180', 135, 181))
    say('| band (u) | ' + ' | '.join(s[0] for s in secs) + ' | total |')
    say('|---|' + '---|' * (len(secs) + 1))
    for lo, hi in bands:
        ys = [x for x in stuck if x['dist'] is not None and lo <= x['dist'] < hi]
        say('| %d-%d | ' % (lo, hi) + ' | '.join(str(sum(1 for x in ys if a <= abs(x['bearing']) < b)) for _, a, b in secs)
            + ' | %d |' % len(ys))
    say('')

    # ---- spacing and concurrency
    say('## Spacing and how many at once')
    say('')
    say('At each stick: other mines of the same owner already live, the nearest of them, and how many sit within the '
        '244 u chain radius (and, on Kejim/Dantooine, with brush line of sight).')
    say('')
    say('| scope | player | sticks | live before q50 / max | share with >= 2 live before | nearest live q25/50/75 | '
        'share with one within 244 u | ... with LOS | team mines live before q50 |')
    say('|---|---|---|---|---|---|---|---|---|')
    for scope, pool in (('kejim', kej), ('all maps', stuck)):
        for a in aliases + ('all',):
            ys = [x for x in pool if (a == 'all' or x['alias'] == a) and x.get('live_before') is not None]
            if not ys:
                continue
            lb = [x['live_before'] for x in ys]
            nn = [x['nn'] for x in ys if x['nn'] is not None]
            say('| %s | %s | %d | %.0f / %d | %s | %s | %s | %s | %.0f |' % (
                scope, a, len(ys), q(lb, .5), max(lb), pct(sum(v >= 2 for v in lb), len(lb)), qs(nn),
                pct(sum(x['within244'] > 0 for x in ys), len(ys)),
                pct(sum(x['chainable'] > 0 for x in ys), len(ys)) if all(x['map'] in los for x in ys) or scope == 'kejim' else
                pct(sum(x['chainable'] > 0 for x in ys if x['map'] in los), sum(1 for x in ys if x['map'] in los)) + ' (LOS maps)',
                q([x['team_live_before'] for x in ys], .5)))
    say('')

    # ---- outcomes
    say('## How mines end')
    say('')
    classes = ('enemy_trigger', 'timeout', 'chain', 'shot_or_splash', 'exploded_other', 'hit_in_flight',
               'chain_unseen', 'timeout_unseen', 'vanished')
    say('| player | mines | ' + ' | '.join(classes) + ' | life q50 s (stuck) | enemy carrier within 168 u at the blast | mine kills (obituaries) |')
    say('|---|---|' + '---|' * len(classes) + '---|---|---|')
    for a in aliases + ('all',):
        ys = [x for x in tm if a == 'all' or x['alias'] == a]
        if not ys:
            continue
        cc = collections.Counter(x['end_class'] for x in ys)
        kills = 0
        for m in matches:
            owners = [c for c, al in m.targets.items() if a == 'all' or al == a]
            kills += sum(1 for o in m.obits if o[2] in owners and o[3] in MINE_MODS and o[1] != o[2])
        lf = [x['life'] / 1000 for x in ys if x['life'] is not None]
        say('| %s | %d | ' % (a, len(ys)) + ' | '.join(str(cc[k]) for k in classes) + ' | %.1f | %d | %d |' % (
            q(lf, .5), sum(1 for x in ys if x['end_class'] == 'enemy_trigger' and x['enemy_carrier']), kills))
    say('')
    say('Classes: enemy_trigger = an alive enemy within 168 u (128 trigger + slack) at the explosion snapshot; '
        'timeout = proximity mine that blew at >= 29.7 s after sticking; chain = another mine within 256 u exploded '
        'in the previous 250 ms; shot_or_splash = blew early with an enemy within 600 u but outside trigger range '
        '(shot, or splash from a nearby blast); *_unseen = disappeared without the event in any POV.')
    say('')

    # ---- re-lay timing
    say('## When they re-lay')
    say('')
    say('Placements of one owner within 4 s of each other form a lay session.')
    say('')
    say('| player | sessions | mines per session q50 / max | session gap q25/50/75 s | starts <= 12 s after own death | '
        'starts <= 3 s after one of own mines blew | starts with own mines live | ... the oldest live >= 25 s (prox refresh) | '
        'starts while own flag is away |')
    say('|---|---|---|---|---|---|---|---|---|')
    relay_rows = []
    for a in aliases + ('all',):
        ss_all, gaps = [], []
        for m in matches:
            for c, al in m.targets.items():
                if a != 'all' and al != a:
                    continue
                xs = sorted([x for x in m.mines if x['owner'] == c and x['alias']], key=lambda x: x['place_t'])
                ss = lay_sessions(xs)
                for i, s in enumerate(ss):
                    t0 = s[0]['place_t']
                    dth = [t for t in s[0]['owner_deaths'] if t <= t0]
                    prev = [x for x in xs if x['place_t'] < t0]
                    blew = [x for x in prev if x['explode_t'] is not None and 0 <= t0 - x['explode_t'] <= 3000]
                    live = [x for x in prev if x['stick_t'] is not None and x['end_t'] > t0]
                    old = [x for x in live if x['mode'] == 'prox' and t0 - x['stick_t'] >= 25000]
                    fs = [f for f in m.flags if f[0] <= t0]
                    st = pp.flag_state(fs[-1][1], s[0]['team']) if fs else None
                    ss_all.append(dict(n=len(s), death=bool(dth and t0 - dth[-1] <= 12000), blew=bool(blew),
                                       live=bool(live), old=bool(old), away=bool(st and st[0] != '0')))
                    if i:
                        gaps.append((t0 - ss[i - 1][-1]['place_t']) / 1000)
        if not ss_all:
            continue
        n = len(ss_all)
        say('| %s | %d | %.0f / %d | %s | %s | %s | %s | %s | %s |' % (
            a, n, q([s['n'] for s in ss_all], .5), max(s['n'] for s in ss_all), qs(gaps, '%.1f'),
            pct(sum(s['death'] for s in ss_all), n), pct(sum(s['blew'] for s in ss_all), n),
            pct(sum(s['live'] for s in ss_all), n), pct(sum(s['old'] for s in ss_all), n),
            pct(sum(s['away'] for s in ss_all), n)))
    say('')

    # ---- carrier passes
    say('## Enemy carriers versus the layout')
    say('')
    say('Each enemy carrier episode of the owner team\'s flag (track while carrying, merged POVs), against the '
        'target\'s armed mines live at the moment (armed = 2 s after stick for proximity, 1 s for tripwire).')
    say('')
    say('| scope | carrier episodes with a target mine armed | passed within 128 u of >= 1 armed mine | '
        'armed mines passed within 128 u (q50 / max) | ... within 256 u (q50) | a target mine blew within 168 u of the carrier |')
    say('|---|---|---|---|---|---|')
    for scope in ('kejim', 'all maps'):
        eps_rows = []
        for m in matches:
            if scope == 'kejim' and m.map != 'kejim':
                continue
            for team in (pp.TEAM_RED, pp.TEAM_BLUE):
                mines_t = [x for x in m.mines if x['alias'] and x['team'] == team and x['rest'] and x['stick_t'] is not None]
                if not mines_t:
                    continue
                for c, ep in thefts(m, team):
                    t0, t1 = ep[0][0], ep[-1][0]
                    armed = [x for x in mines_t if x['stick_t'] + ARM_MS.get(x['mode'], 2000) < t1 and x['end_t'] > t0]
                    if not armed:
                        continue
                    p128, p256 = set(), set()
                    for t, r in ep:
                        for x in armed:
                            if x['stick_t'] + ARM_MS.get(x['mode'], 2000) <= t <= x['end_t'] + 100:
                                dd = math.dist(r[:3], x['rest'])
                                if dd <= PROX_RANGE:
                                    p128.add(id(x))
                                if dd <= SPLASH_RADIUS:
                                    p256.add(id(x))
                    blew = any(x['end_class'] == 'enemy_trigger' and x['enemy_c'] == c and t0 <= x['end_t'] <= t1 + 200
                               for x in armed)
                    eps_rows.append((len(p128), len(p256), blew))
        if eps_rows:
            n = len(eps_rows)
            say('| %s | %d | %s | %.0f / %d | %.0f | %s |' % (
                scope, n, pct(sum(1 for r in eps_rows if r[0]), n), q([r[0] for r in eps_rows], .5),
                max(r[0] for r in eps_rows), q([r[1] for r in eps_rows], .5), pct(sum(r[2] for r in eps_rows), n)))
    say('')

    # ---- what triggered mines do, grabs into stand mines, chain exposure among live layouts
    say('### What a triggered mine does to the enemy')
    say('')
    o = collections.Counter()
    for m in matches:
        deaths = collections.defaultdict(list)
        for t, v, a, md in m.obits:
            deaths[v].append((t, a, md))
        for x in m.mines:
            if x['alias'] and x['end_class'] == 'enemy_trigger':
                c, t0 = x['enemy_c'], x['end_t']
                dd = [d for d in deaths[c] if t0 - 100 <= d[0] <= t0 + 5000]
                o['n'] += 1
                o['died5'] += bool(dd)
                o['mine'] += any(d[2] in MINE_MODS and d[0] <= t0 + 300 for d in dd)
                o['owner5'] += any(d[1] == x['owner'] for d in dd)
                o['carrier'] += x['enemy_carrier']
    say('Of %d enemy-triggered target mines: the triggering enemy died within 5 s in %s (killed by the mine '
        'itself %s, by the mine owner by any means within 5 s %s); the trigger was the carrier of the owner '
        'team\'s flag in %s.' % (o['n'], pct(o['died5'], o['n']), pct(o['mine'], o['n']), pct(o['owner5'], o['n']),
                                 pct(o['carrier'], o['n'])))
    say('')
    say('### Grabs at a stand the target was mining')
    say('')
    say('| scope | own-flag grabs (status home -> taken) | an armed target mine within 200 u of the stand at the grab | '
        'a target stand mine (<= 200 u) blew in the 3 s before the grab |')
    say('|---|---|---|---|')
    for scope in ('kejim', 'all maps'):
        g = collections.Counter()
        for m in matches:
            if scope == 'kejim' and m.map != 'kejim':
                continue
            st = stands.get(m.map)
            if not st or len(st) < 2:
                continue
            for team in (pp.TEAM_RED, pp.TEAM_BLUE):
                tms = [x for x in m.mines if x['alias'] and x['team'] == team and x['rest'] and x['stick_t'] is not None]
                if not tms:
                    continue
                k, prev = team - 1, None
                for t, s in m.flags:
                    if len(s) == 2 and prev and len(prev) == 2 and prev[k] == '0' and s[k] == '1':
                        near = [x for x in tms if math.dist(x['rest'][:2], st[team][:2]) <= 200]
                        g['grabs'] += 1
                        g['armed'] += any(x['stick_t'] + ARM_MS.get(x['mode'], 2000) <= t <= x['end_t'] + 100 for x in near)
                        g['blew'] += any(x['explode_t'] and t - 3000 <= x['explode_t'] <= t + 100 for x in near)
                    prev = s
        say('| %s | %d | %s | %s |' % (scope, g['grabs'], pct(g['armed'], g['grabs']), pct(g['blew'], g['grabs'])))
    say('')
    say('Grabs are counted over the whole match for the target\'s team, including stretches with no mine down.')
    say('')
    ys = [x for x in stuck if x.get('live_before')]
    lm = [x for x in ys if x['map'] in los]
    say('Sticks made while at least one own mine was live: %d; another own live mine within 244 u in %s; within '
        '244 u with brush line of sight (Kejim/Dantooine only, %d sticks) in %s.' % (
            len(ys), pct(sum(x['within244'] > 0 for x in ys), len(ys)), len(lm),
            pct(sum(x['chainable'] > 0 for x in lm), len(lm))))
    say('')
    if kej:
        hist = collections.Counter(min(int(x['dist'] // 32) * 32, 640) for x in kej if x['dist'] is not None)
        say('Kejim target mines by 2D distance from the own stand (32 u bins, 640 = 640 u and beyond): %s.' % (
            ', '.join('%d: %d' % kv for kv in sorted(hist.items()))))
        say('')
    vc = collections.Counter()
    for x in tm:
        if x['end_class'] != 'vanished':
            continue
        m = x['m']
        alive = m.at(x['owner'], x['last_t'] + 200, 300)
        vc['in flight' if x['stick_t'] is None else 'owner dead' if alive and not alive[4] else
           'owner changed team' if m.team_of(x['owner'], x['last_t'] + 1000) != x['team'] else 'unexplained'] += 1
    say('Vanished without an explosion event (%d): %s.' % (sum(vc.values()), ', '.join('%s %d' % kv for kv in vc.most_common())))
    say('')

    # ---- Kejim spots
    say('## Kejim: the spots (red frame; blue mines rotated 180 deg about (1308, 2076))')
    say('')
    kb = [x for x in kej if x['dist'] is not None and x['dist'] <= BASE_RANGE]
    pts = [(to_red_kejim(x['rest'], x['team']), x) for x in kb]
    # greedy clustering: densest 96 u neighbourhoods first
    R = 96
    left = list(range(len(pts)))
    clusters = []
    while left:
        best = max(left, key=lambda i: sum(1 for j in left if math.dist(pts[i][0], pts[j][0]) <= R))
        mem = [j for j in left if math.dist(pts[best][0], pts[j][0]) <= R]
        clusters.append(mem)
        left = [j for j in left if j not in mem]
    # grabber tracks in the red frame: 5 s approach before each own-flag grab, 8 s exit after it
    approaches, exits = [], []
    for m in matches:
        if m.map != 'kejim':
            continue
        for team in (pp.TEAM_RED, pp.TEAM_BLUE):
            k, prev = team - 1, None
            bit = 1 if team == pp.TEAM_RED else 2
            for t, s in m.flags:
                if len(s) == 2 and prev and len(prev) == 2 and prev[k] == '0' and s[k] == '1':
                    who = None
                    for c in m.track:
                        r = m.at(c, t + 100, 400)
                        if r and r[3] & bit and m.team_of(c, t) not in (team, 0):
                            who = c
                            break
                    if who is not None:
                        approaches.append([to_red_kejim(r[:3], team) for _, r in m.window(who, t - 5000, t)])
                        exits.append([to_red_kejim(r[:3], team) for _, r in m.window(who, t, t + 8000) if r[3] & bit])
                prev = s
    say('Base mines on Kejim: %d (players: %s). Own-flag grabs on Kejim with the grabber identified (any team, '
        'rotated to red): %d; approach = the grabber last 5 s before the grab, exit = first 8 s carrying.' % (
            len(kb), ', '.join('%s %d' % kv for kv in collections.Counter(x['alias'] for x in kb).most_common()),
            len(approaches)))
    say('')
    say('| spot | centre x y z (red) | mines | players | matches | prox share | surface | dist / bearing / height | '
        'enemy_trigger share | grab approaches within 128 u | carrier exits within 128 u |')
    say('|---|---|---|---|---|---|---|---|---|---|---|')
    spots = []
    for k, mem in enumerate(clusters[:12]):
        ps_ = [pts[j][0] for j in mem]
        xs = [pts[j][1] for j in mem]
        cx = (statistics.median(p[0] for p in ps_), statistics.median(p[1] for p in ps_), statistics.median(p[2] for p in ps_))
        d, b, h = frame({pp.TEAM_RED: (152, -1160, 1900), pp.TEAM_BLUE: (2464, 5312, 1900)}, pp.TEAM_RED, cx)
        ca = sum(1 for ep in approaches if any(math.dist(p, cx) <= PROX_RANGE + 24 for p in ep))
        ce = sum(1 for ep in exits if any(math.dist(p, cx) <= PROX_RANGE + 24 for p in ep))
        spots.append(dict(c=cx, n=len(mem), xs=xs))
        say('| %d | %.0f %.0f %.0f | %d | %s | %d | %s | %s | %.0f / %+.0f / %+.0f | %s | %s | %s |' % (
            k + 1, cx[0], cx[1], cx[2], len(mem),
            ', '.join('%s %d' % kv for kv in collections.Counter(x['alias'] for x in xs).most_common()),
            len({x['match'] for x in xs}), pct(sum(x['mode'] == 'prox' for x in xs), len(xs)),
            '/'.join('%s %d' % kv for kv in collections.Counter(x['surface'] for x in xs).most_common()),
            d, b, h, pct(sum(x['end_class'] == 'enemy_trigger' for x in xs), len(xs)),
            pct(ca, len(approaches)), pct(ce, len(exits))))
    say('')
    say('Distances use the player origin (about 24 u above the floor), so the 128 u trigger test allows 24 u slack.')
    say('')
    say('Pairwise distances between the top spots (u; * = within the 244 u chain radius with brush LOS):')
    say('')
    top = spots[:6]
    f = los.get('kejim')
    say('| | ' + ' | '.join(str(i + 1) for i in range(len(top))) + ' |')
    say('|---|' + '---|' * len(top))
    for i, a in enumerate(top):
        cells = []
        for j, b in enumerate(top):
            if i == j:
                cells.append('-')
                continue
            dd = math.dist(a['c'], b['c'])
            star = dd <= CHAIN_RADIUS and (f is None or f((a['c'][0], a['c'][1], a['c'][2] + 4), (b['c'][0], b['c'][1], b['c'][2] + 4)))
            cells.append('%.0f%s' % (dd, '*' if star else ''))
        say('| %d | ' % (i + 1) + ' | '.join(cells) + ' |')
    say('')
    say('## Kejim red stand: recommended spots')
    say('')
    pick = []
    for sp in spots:
        if all(math.dist(sp['c'], o['c']) > CHAIN_RADIUS for o in pick):
            pick.append(sp)
        if len(pick) == 3:
            break
    say('Chain-free set (greedy over the spots by expert use, every pair > 244 u apart; red frame, flag at '
        '152, -1160, 1920):')
    say('')
    for k, sp in enumerate(pick):
        say('%d. %.0f, %.0f, %.0f: %d expert mines (%s)' % (
            k + 1, sp['c'][0], sp['c'][1], sp['c'][2], sp['n'],
            ', '.join('%s %d' % kv for kv in collections.Counter(x['alias'] for x in sp['xs']).most_common())))
    say('')
    near = [sp for sp in spots[1:6] if math.dist(sp['c'], spots[0]['c']) <= CHAIN_RADIUS]
    say('Observed practice is not chain-free: the experts lay 2-3 proximity mines on and beside the stand within '
        '~150 u (second spots here: %s), accept chains, and re-lay after respawns. Inference: the stand cluster '
        'damages and knocks back the grabber for a follow-up kill rather than killing outright.' % '; '.join(
            '%.0f, %.0f, %.0f (%d mines, %.0f u from the first)' % (sp['c'] + (sp['n'], math.dist(sp['c'], spots[0]['c'])))
            for sp in near))
    say('')
    (args.out / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    # ---- CSVs
    cols = ['match', 'map', 'alias', 'owner', 'team', 'mode', 'surface', 'place_t', 'game_s', 'stick_t', 'end_t',
            'life', 'end', 'end_class', 'rest_x', 'rest_y', 'rest_z', 'nx', 'ny', 'nz', 'dist', 'bearing', 'dz',
            'thrower_x', 'thrower_y', 'thrower_z', 'thrower_dist', 'launch_speed', 'live_before', 'nn', 'within244',
            'chainable', 'team_live_before', 'enemy_d', 'enemy_c', 'enemy_carrier', 'red_x', 'red_y', 'first_stuck',
            'povs']
    for name, pool in (('mines.csv', tm), ('mines_all.csv', allm)):
        with (args.out / name).open('w', encoding='utf-8', newline='') as fh:
            w = csv.writer(fh)
            w.writerow(cols)
            for x in pool:
                r = x['rest'] or (None, None, None)
                n = x['normal'] or (None, None, None)
                th = x['thrower'] or (None, None, None)
                red = to_red_kejim(x['rest'], x['team']) if x['rest'] and x['map'] == 'kejim' else (None, None)
                fmt = lambda v: '' if v is None else ('%.1f' % v if isinstance(v, float) else v)
                w.writerow([fmt(v) for v in (
                    x['match'], x['map'], x['alias'], x['owner'], x['team'], x['mode'], x['surface'], x['place_t'],
                    x['game_s'], x['stick_t'], x['end_t'], x['life'], x['end'], x['end_class'], r[0], r[1], r[2],
                    n[0], n[1], n[2], x['dist'], x['bearing'], x['dz'], th[0], th[1], th[2], x['thrower_dist'],
                    x['launch_speed'], x.get('live_before'), x.get('nn'), x.get('within244'), x.get('chainable'),
                    x.get('team_live_before'), x['enemy_d'], x['enemy_c'], x['enemy_carrier'], red[0], red[1],
                    x['first_stuck'], ' '.join(map(str, x['povs'])))])
    print('\n'.join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('select', 'extract', 'report'))
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--jobs', type=int, default=8)
    args = ap.parse_args()
    if args.cmd == 'select':
        cmd_select(args)
    elif args.cmd == 'extract':
        cmd_extract(args)
    else:

        cmd_report(args)


if __name__ == '__main__':
    main()
