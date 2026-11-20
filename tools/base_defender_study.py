"""Base defender demo study: how human base players hold, shoot, chase and pull.

Record docs/movement/records/2026-09-28-base-defender-study.md. Offline only:
reads local demos (dm26.py) and the Discord stat sheets; never connects to a
server. Builds on tools/position_pilot.py (selection rules, region, sheets)
and tools/position_open.py (pit and brush columns from the map BSP).

    python tools/base_defender_study.py select  [--out DIR]
    python tools/base_defender_study.py extract [--out DIR] [--jobs N]
    python tools/base_defender_study.py report  [--out DIR]

select  every base-POV demo on mp/ctf_kejim and ctf_dantooine_v3 in the
        human_rush_study index, at most 3 per recorder per map, one per match.
extract (v4) the pilot's timeline plus: the POV's fire events (playerState
        events EV_FIRE_WEAPON / EV_ALT_FIRE, weapon, alt flag, view angles),
        its hit counter (PERS_HITS, raised by the server at each damaging hit
        on an enemy) with the victim's health and armor (PERS_ATTACKEE_ARMOR),
        view angles and weaponstate at every sample, others' current weapon,
        and a sample at every snapshot while an enemy is within 1,500 u of the
        POV (else every 250 ms). Push and pull spends carry the view angles.
report  answers the study's five questions per map; report.txt and CSVs.

Observed: positions, angles, fire and hit-counter events, pain and obituary
events, flag status, the POV's Force. Inferred: which enemy a fire, hit or
pull was aimed at (nearest to the view ray), line of sight (brush columns;
Dantooine v3 patch floors are not brushes), and outcomes by time windows.
Enemies exist only while in the POV's snapshots; demos carry no commands.
"""
import argparse
import bisect
from concurrent.futures import ProcessPoolExecutor, as_completed
import collections
import csv
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp

DEFAULT_OUT = pp.ROOT / '.codex-research' / 'base-defender-study-20260928'
PILOT_OUT = pp.DEFAULT_OUT
EXTRACT_DIR = 'extract-v4'
PER_RECORDER = 3
COARSE_MS = 250
FINE_RANGE = 1500

PERS_HITS, PERS_ATTACKEE_ARMOR = 1, 7
EV_FIRE_WEAPON, EV_ALT_FIRE = 27, 28
EV_EVENT_BITS = 0x300
HAND_PUSH, HAND_PULL = 1, 2
FP_SPEED = 2
WEAPONS = ('none', 'stun', 'melee', 'saber', 'bryar', 'blaster', 'disruptor', 'bowcaster', 'repeater', 'demp2',
           'flechette', 'rocket', 'thermal', 'trip_mine', 'det_pack', 'conc', 'bryar_old', 'emplaced', 'turret')
# g_weapon.c projectile speeds (u/s); None = hitscan. Alt modes where they differ.
SPEED = {(4, 0): 1600, (4, 1): 1600, (16, 0): 1600, (16, 1): 1600, (5, 0): 2300, (5, 1): 2300,
         (6, 0): None, (6, 1): None, (7, 0): 1300, (7, 1): 1300, (8, 0): 1600, (8, 1): 1100,
         (9, 0): 1800, (9, 1): None, (10, 0): 3500, (10, 1): 700, (11, 0): 900, (11, 1): 900,
         (12, 0): 900, (12, 1): 600, (15, 0): 3000, (15, 1): None}
FUSE_MS = {(10, 1): 1500, (12, 0): 3000, (12, 1): 3000, (9, 1): 500}   # bouncing/timed: extra window
PIT_MODS = {0, 36, 38, 39, 41}
MOD_NAMES = {3: 'saber', 4: 'bryar', 5: 'bryar_alt', 6: 'blaster', 8: 'disruptor', 10: 'sniper', 11: 'bowcaster',
             12: 'repeater', 13: 'repeater_alt', 14: 'repeater_alt_splash', 15: 'demp2', 16: 'demp2_alt',
             17: 'flechette', 18: 'flechette_alt_splash', 19: 'rocket', 20: 'rocket_splash', 21: 'homing',
             22: 'homing_splash', 23: 'thermal', 24: 'thermal_splash', 25: 'trip_mine', 26: 'timed_mine',
             27: 'det_pack', 29: 'conc', 30: 'conc_alt', 31: 'force_dark', 2: 'melee', 1: 'stun_baton'}
AIM_DEG = 25          # a fire is aimed at the enemy nearest its view ray within this angle
PULL_RANGE = 1024     # w_force.c ForceThrow radius (a box), level 3 arc 180 degrees


def wname(w, alt=None):
    n = WEAPONS[w] if 0 <= w < len(WEAPONS) else 'w%d' % w
    return n if alt is None else n + ('_alt' if alt else '')


# ---------------------------------------------------------------- select

def cmd_select(args):
    rows = [r for r in csv.DictReader(pp.INDEX.open(encoding='utf-8')) if r['map'] in pp.MAPS and r['pos'] == 'Bas']
    chosen = []
    for m in pp.MAPS:
        cell = sorted((r for r in rows if r['map'] == m), key=lambda r: hashlib.sha1(r['demo'].encode()).hexdigest())
        per, matches, got = collections.Counter(), set(), []
        for r in cell:
            who = r['pov_name'].lower()
            if per[who] >= PER_RECORDER or r['match_id'] in matches or not Path(r['demo']).exists():
                continue
            per[who] += 1
            matches.add(r['match_id'])
            got.append(r)
        chosen += got
        print('%-18s Bas: %d of %d demos, %d recorders' % (m, len(got), len(cell), len(per)))
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / 'selection.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(chosen[0].keys()))
        w.writeheader()
        w.writerows(chosen)
    print('wrote', args.out / 'selection.csv', len(chosen))


# ---------------------------------------------------------------- 2v2 scan

SCAN_MS = 240000       # partial parse: the first four minutes of snapshots


def fixture_key(mapname):
    m = mapname.lower().replace('mp/', '')
    k = m[4:] if m.startswith('ctf_') else m
    return k if k in STAND_KEYS else None


STAND_KEYS = set()


def fixture_maps():
    for p in pp.FIXTURES.glob('*/cases.json'):
        names = {c['start']['name'] for c in json.loads(p.read_text(encoding='utf-8'))['cases']}
        if {'red_flagstand', 'blue_flagstand'} <= names:
            STAND_KEYS.add(p.parent.name)
    return STAND_KEYS


def scan_demo(path):
    """Map, POV and team sizes every 5 s over the first SCAN_MS of snapshots."""
    sys.path.insert(0, str(pp.dm26_dir()))
    import dm26
    fixture_maps()
    parser = dm26.DemoParser()
    res = dict(demo=path, map='', key='', pov='', n=0, n22=0, n44=0, other=0, error='')
    t0, last, modes = None, -10 ** 9, collections.Counter()
    try:
        for ev in parser.parse_file(path):
            if ev[0] == 'gamestate' and not res['map']:
                info = dm26.parse_info_string(str(parser.configstrings.get(dm26.CS_SERVERINFO, '')).strip().strip('"'))
                res['map'] = info.get('mapname', '')
                res['key'] = fixture_key(res['map']) or ''
                res['gametype'] = str(info.get('g_gametype', ''))
                if not res['key'] or res['gametype'] not in ('8', ''):
                    return res
                continue
            if ev[0] != 'snapshot':
                continue
            snap = ev[1]
            t = snap.serverTime
            t0 = t if t0 is None else t0
            if t - t0 > SCAN_MS:
                break
            if t - last < 5000:
                continue
            last = t
            warm = str(parser.configstrings.get(pp.CS_WARMUP, '')).strip().strip('"') not in ('', '0')
            if warm:
                continue
            cnt = collections.Counter()
            for c in range(32):
                cs = parser.configstrings.get(dm26.CS_PLAYERS + c, '')
                if cs:
                    info = dm26.parse_info_string(str(cs).strip().strip('"'))
                    cnt[int(info.get('t', 0) or 0)] += 1
                    if c == snap.ps['clientNum']:
                        res['pov'] = pp.strip_colors(info.get('n', ''))
            res['n'] += 1
            r, b = cnt[pp.TEAM_RED], cnt[pp.TEAM_BLUE]
            res['n22'] += (r, b) == (2, 2)
            res['n44'] += (r, b) == (4, 4)
            res['other'] += (r, b) not in ((2, 2), (4, 4))
            modes[(r, b)] += 1
            res['mode'] = '%dv%d' % modes.most_common(1)[0][0]
    except Exception as exc:
        res['error'] = '%s: %s' % (type(exc).__name__, exc)
    return res


def cmd_scan2v2(args):
    """Find 2v2 CTF demos on maps with a fixture flag stand (partial parse)."""
    rows = list(csv.DictReader(pp.INDEX.open(encoding='utf-8')))
    fixture_maps()
    todo = [r['demo'] for r in rows if (not r['map'] or fixture_key(r['map'])) and r['pos'] not in ('Off', 'Bas', 'Cha')
            and Path(r['demo']).exists()]
    dest = args.out / 'scan2v2.csv'
    done = {}
    if dest.exists():
        done = {r['demo']: r for r in csv.DictReader(dest.open(encoding='utf-8'))}
    todo = [p for p in todo if p not in done]
    print('scanning %d demos (%d done)' % (len(todo), len(done)), flush=True)
    keys = ['demo', 'map', 'key', 'gametype', 'pov', 'n', 'n22', 'n44', 'other', 'mode', 'error']
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(scan_demo, p) for p in todo]
        for i, f in enumerate(as_completed(futs), 1):
            r = f.result()
            done[r['demo']] = r
            if i % 100 == 0:
                print('%d/%d' % (i, len(todo)), flush=True)
    with dest.open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction='ignore')
        w.writeheader()
        for r in done.values():
            w.writerow(r)
    two = [r for r in done.values() if int(r['n'] or 0) >= 6 and int(r['n22']) >= .6 * int(r['n'])]
    print('2v2 candidates:', collections.Counter(r['key'] for r in two))


def cmd_select2v2(args):
    """2v2 demos (60%+ of scanned live samples 2 against 2), capped per recorder per map, one per match time."""
    scan = [r for r in csv.DictReader((args.out / 'scan2v2.csv').open(encoding='utf-8'))
            if int(r['n'] or 0) >= 6 and int(r['n22']) >= .6 * int(r['n'])]
    scan.sort(key=lambda r: hashlib.sha1(r['demo'].encode()).hexdigest())
    per, got = collections.Counter(), []
    for r in scan:
        who = (r['key'], r['pov'].lower())
        if per[who] >= PER_RECORDER:
            continue
        per[who] += 1
        got.append(dict(demo=r['demo'], match_id='', map=r['map'], client_id='', pov_name=r['pov'], alias='', pos='2v2',
                        sheet='', key=r['key']))
    with (args.out / 'selection2v2.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(got[0].keys()))
        w.writeheader()
        w.writerows(got)
    print('selected %d 2v2 demos: %s' % (len(got), collections.Counter(r['key'] for r in got)))


# ---------------------------------------------------------------- extract

def extract_demo(row):
    sys.path.insert(0, str(pp.dm26_dir()))
    import dm26
    E = dm26.ENT_IDX
    fx, fy, fz = E['pos.trBase[0]'], E['pos.trBase[1]'], E['pos.trBase[2]']
    et, evf, evp, fpa, pwf, eff, wpf = (E['eType'], E['event'], E['eventParm'], E['forcePowersActive'], E['powerups'],
                                        E['eFlags'], E['weapon'])
    o1, o2 = E['otherEntityNum'], E['otherEntityNum2']
    parser = dm26.DemoParser()
    out = dict(demo=row['demo'], map=row.get('key') or pp.MAPS[row['map']], pos=row['pos'], pov_name=row['pov_name'], sheet=row['sheet'],
               match_id=row['match_id'], samples=[], pains=[], obits=[], force=[], spends=[], fires=[], hits=[],
               cp=[], flags=[], names={}, lost_events=0, error='')
    last_sample, prev_ents, last_ev, prev_pov_fpa, last_flag = -10 ** 9, {}, {}, 0, None
    obit_seen, last_snap, prev_pov_fp, prev_pov_fpa_spend = {}, 0, None, 0
    prev_seq, prev_hits, raw_cs, teams = None, None, {}, {}
    try:
        for ev in parser.parse_file(row['demo']):
            kind = ev[0]
            if kind == 'gamestate':
                prev_ents, prev_seq, prev_hits = {}, None, None
                continue
            if kind == 'command':
                text = ev[2]
                if text.startswith('cp '):
                    out['cp'].append((last_snap, pp.strip_colors(text[3:].strip().strip('"'))))
                continue
            if kind != 'snapshot':
                continue
            snap = ev[1]
            ps, t = snap.ps, snap.serverTime
            last_snap = t
            me, my_team = ps['clientNum'], ps['persistant'][pp.PERS_TEAM]
            for c in range(32):
                cs = parser.configstrings.get(dm26.CS_PLAYERS + c, '')
                if cs == raw_cs.get(c):
                    continue
                raw_cs[c] = cs
                if cs:
                    info = dm26.parse_info_string(str(cs).strip().strip('"'))
                    nm, tm = pp.strip_colors(info.get('n', '')), int(info.get('t', 0) or 0)
                    teams[c] = tm
                    cur = out['names'].get(c)
                    if not cur or cur[-1][1:] != [nm, tm]:
                        out['names'].setdefault(c, []).append([t, nm, tm])
                else:
                    teams.pop(c, None)
            status = str(parser.configstrings.get(pp.CS_FLAGSTATUS, '')).strip().strip('"')
            if status != last_flag:
                out['flags'].append((t, status))
                last_flag = status
            warm = str(parser.configstrings.get(pp.CS_WARMUP, '')).strip().strip('"') not in ('', '0')
            px, py, pz = ps['origin[0]'], ps['origin[1]'], ps['origin[2]']
            pitch, yaw, vh = ps['viewangles[0]'], ps['viewangles[1]'], ps['viewheight']
            # Predictable playerState events: two slots, eventSequence counts them.
            seq = ps['eventSequence']
            if prev_seq is not None and seq != prev_seq:
                if seq < prev_seq or seq - prev_seq > 64:
                    prev_seq = seq
                else:
                    if seq - prev_seq > 2:
                        out['lost_events'] += seq - prev_seq - 2
                    for i in range(max(prev_seq, seq - 2), seq):
                        e = ps['events[%d]' % (i & 1)] & ~EV_EVENT_BITS
                        if e in (EV_FIRE_WEAPON, EV_ALT_FIRE):
                            out['fires'].append((t, ps['weapon'], int(e == EV_ALT_FIRE), round(px), round(py), round(pz),
                                                 round(pitch, 1), round(yaw, 1), vh))
            prev_seq = seq
            hits = ps['persistant'][PERS_HITS]
            if prev_hits is not None and hits != prev_hits:
                aa = ps['persistant'][PERS_ATTACKEE_ARMOR]
                out['hits'].append((t, hits - prev_hits, (aa >> 8) & 0xffff, aa & 0xff, ps['weapon']))
            prev_hits = hits
            pfa = ps['fd.forcePowersActive']
            for b in range(len(pp.FORCE_NAMES)):
                if pfa & (1 << b) and not prev_pov_fpa & (1 << b):
                    out['force'].append((t, me, b, ps['stats'][0], ps['fd.forcePower'], 1))
            fp = ps['fd.forcePower']
            if prev_pov_fp is not None and fp <= prev_pov_fp - 10 and not (pfa & ~prev_pov_fpa_spend):
                out['spends'].append((t, prev_pov_fp - fp, ps['fd.forcePowerSelected'], ps['forceHandExtend'],
                                      ps['stats'][0], fp, round(px), round(py), round(pz), round(pitch, 1),
                                      round(yaw, 1), vh))
            prev_pov_fp, prev_pov_fpa_spend = fp, pfa
            prev_pov_fpa = pfa
            ents = {}
            for num, st in snap.entities:
                if num < 32 and st[et] == pp.ET_PLAYER:
                    ents[num] = st
                    if num in prev_ents:
                        rise = st[fpa] & ~prev_ents[num][fpa]
                        for b in range(len(pp.FORCE_NAMES)):
                            if rise & (1 << b):
                                out['force'].append((t, num, b, None, None, 0))
                        e = st[evf]
                        if e and e != last_ev.get(num) and (e & ~EV_EVENT_BITS) == pp.EV_PAIN:
                            out['pains'].append((t, num, st[evp]))
                    last_ev[num] = st[evf]
                elif st[et] == pp.ET_EVENTS + pp.EV_OBITUARY:
                    key = (st[o1], st[o2], st[evp])
                    if t - obit_seen.get(key, -10 ** 9) > 1500:
                        out['obits'].append((t, st[o1], st[o2], st[evp]))
                    obit_seen[key] = t
            prev_ents = ents
            near = any(teams.get(n) in (pp.TEAM_RED, pp.TEAM_BLUE) and teams.get(n) != my_team
                       and not s[eff] & pp.EF_DEAD
                       and (s[fx] - px) ** 2 + (s[fy] - py) ** 2 + (s[fz] - pz) ** 2 < FINE_RANGE ** 2
                       for n, s in ents.items() if n != me)
            if near or t - last_sample >= COARSE_MS:
                last_sample = t
                pov = (me, my_team, round(px), round(py), round(pz),
                       ps['stats'][0], ps['fd.forcePower'], pfa,
                       int(bool(ps['powerups'][pp.PW_REDFLAG])) | 2 * int(bool(ps['powerups'][pp.PW_BLUEFLAG])),
                       int(ps['pm_type'] == pp.PM_NORMAL and ps['stats'][0] > 0),
                       round(math.hypot(ps['velocity[0]'], ps['velocity[1]'])),
                       ps['stats'][pp.STAT_ARMOR], ps['stats'][pp.STAT_WEAPONS], list(ps['ammo'][:pp.AMMO_TYPES]),
                       ps['weapon'], ps['persistant'][pp.PERS_SPAWN_COUNT],
                       # v4: 16 pitch, 17 yaw, 18 weaponstate, 19 viewheight
                       round(pitch, 1), round(yaw, 1), ps['weaponstate'], vh)
                others = [(n, round(s[fx]), round(s[fy]), round(s[fz]), s[fpa],
                           int(bool(s[pwf] & (1 << pp.PW_REDFLAG))) | 2 * int(bool(s[pwf] & (1 << pp.PW_BLUEFLAG))),
                           int(not s[eff] & pp.EF_DEAD), s[wpf]) for n, s in ents.items() if n != me]
                out['samples'].append((t, status, int(warm), pov, others))
    except Exception as exc:
        out['error'] = '%s: %s' % (type(exc).__name__, exc)
    return out


def extract_path(dest, r):
    return dest / (hashlib.sha1(r['demo'].encode()).hexdigest()[:16] + '.json.gz')


def cmd_extract(args):
    rows = list(csv.DictReader((args.out / 'selection.csv').open(encoding='utf-8')))
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
            print('%d/%d %s samples=%d fires=%d hits=%d lost_events=%d %s' % (
                i, len(todo), d['map'], len(d['samples']), len(d['fires']), len(d['hits']), d['lost_events'], d['error']),
                flush=True)


# ---------------------------------------------------------------- report helpers

def q(vals, p):
    return pp.q(vals, p)


def qs(vals, fmt='%.0f'):
    if not vals:
        return 'n=0'
    return 'n=%d p10 %s p25 %s med %s p75 %s p90 %s' % tuple(
        [len(vals)] + [fmt % q(vals, p) for p in (.1, .25, .5, .75, .9)])


def pct(n, d):
    return '%.0f%%' % (100 * n / d) if d else '-'


def view_dir(pitch, yaw):
    p, y = math.radians(pitch), math.radians(yaw)
    return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p))


def angle_to(eye, fwd, pt):
    v = (pt[0] - eye[0], pt[1] - eye[1], pt[2] - eye[2])
    n = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1e-9
    c = (v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]) / n
    return math.degrees(math.acos(max(-1.0, min(1.0, c)))), n


class Demo:
    def __init__(self, d, stands3):
        self.d = d
        self.map = d['map']
        self.group = self.map
        self.samples = [s for s in d['samples']]
        self.times = [s[0] for s in self.samples]
        self.me = self.samples[0][3][0] if self.samples else -1
        live_pov = [s for s in self.samples if s[3][1] in (pp.TEAM_RED, pp.TEAM_BLUE)]
        self.team = collections.Counter(s[3][1] for s in live_pov).most_common(1)[0][0] if live_pov else 0
        self.enemy_team = pp.TEAM_BLUE if self.team == pp.TEAM_RED else pp.TEAM_RED
        self.own = stands3[self.map][self.team] if self.team else None
        self.enemy_stand = stands3[self.map][self.enemy_team] if self.team else None
        self.own_bit = 1 if self.team == pp.TEAM_RED else 2
        self.sheet = pp.parse_sheet(d['sheet']) if d['sheet'] else {}
        pauses = pp.paused_windows(d)
        self.live = set()
        j = 0
        for s in self.samples:
            t = s[0]
            while j < len(pauses) and pauses[j] < t - 1500:
                j += 1
            if s[2] or (j < len(pauses) and abs(pauses[j] - t) <= 1500) or s[3][1] != self.team:
                continue
            self.live.add(t)
        self.names = {int(k): v for k, v in d['names'].items()}
        self.pains = collections.defaultdict(list)
        for t, n, parm in d['pains']:
            self.pains[n].append(t)
        self.flag_t = [t for t, _ in d['flags']]
        self._role_cache = {}

    role_2v2 = False

    def role(self, t):
        """True unless this is a 2v2 demo and the POV is not its team's defender at t: with the own flag
        home, the POV is nearer its own stand (2D) than its visible teammate (a dead teammate counts
        as farther; an unseen one makes the moment unknown, excluded); with the own flag out, the same test at the steal."""
        if not self.role_2v2:
            return True
        k = bisect.bisect_right(self.flag_t, t) - 1
        own = self.own_flag(t)
        if own and own != '0':
            # Back to the steal: the last home -> taken transition at or before t.
            while k > 0:
                a, b = pp.flag_state(self.d['flags'][k - 1][1], self.team), pp.flag_state(self.d['flags'][k][1], self.team)
                if a and b and a[0] == '0' and b[0] == '1':
                    break
                k -= 1
            if k <= 0:
                return False
            t = self.d['flags'][k][0] - 50
        key = t // 100
        hit = self._role_cache.get(key)
        if hit is not None:
            return hit
        s = self.at(t)
        res = False
        if s and t - s[0] <= 300 and s[3][9]:
            mine = math.dist(s[3][2:4], self.own[:2])
            mates = [o for o in s[4] if self.team_of(o[0], s[0]) == self.team]
            others = [c for c, v in self.names.items() if c != self.me and v and self.team_of(c, s[0]) == self.team]
            if not others:
                res = True
            else:
                res = all((not o[6]) or mine < math.dist(o[1:3], self.own[:2]) for o in mates) and len(mates) >= len(others)
        self._role_cache[key] = res
        return res

    def team_of(self, c, t):
        cur = None
        for tt, nm, tm in self.names.get(c) or []:
            if tt <= t or cur is None:
                cur = tm
        return cur

    def is_enemy(self, c, t):
        return self.team_of(c, t) == self.enemy_team

    def at(self, t):
        i = bisect.bisect_right(self.times, t) - 1
        return self.samples[i] if i >= 0 else None

    def weight(self, i):
        if i + 1 >= len(self.samples):
            return COARSE_MS / 1000
        return min(self.samples[i + 1][0] - self.samples[i][0], COARSE_MS) / 1000

    def enemies(self, s):
        return [o for o in s[4] if o[6] and self.is_enemy(o[0], s[0])]

    def own_flag(self, t):
        i = bisect.bisect_right(self.flag_t, t) - 1
        st = self.d['flags'][i][1] if i >= 0 else ''
        fs = pp.flag_state(st, self.team)
        return fs[0] if fs else None

    def enemy_pos(self, c, t, tol=300):
        """Position of client c at the sample nearest to t (within tol ms)."""
        i = bisect.bisect_left(self.times, t)
        best = None
        for k in (i - 1, i):
            if 0 <= k < len(self.samples) and abs(self.samples[k][0] - t) <= tol:
                o = next((o for o in self.samples[k][4] if o[0] == c), None)
                if o and (best is None or abs(self.samples[k][0] - t) < best[0]):
                    best = (abs(self.samples[k][0] - t), o)
        return best[1] if best else None

    def pov_pos(self, t, tol=300):
        s = self.at(t)
        return s[3] if s and t - s[0] <= tol else None

    def region(self, x, y):
        return pp.region(self.team, x, y, {k: v[:2] for k, v in STANDS3[self.map].items()})


STANDS3 = {}


def load_stands():
    for m in fixture_maps():
        cases = json.loads((pp.FIXTURES / m / 'cases.json').read_text(encoding='utf-8'))['cases']
        for c in cases:
            n = c['start']['name']
            if n in ('red_flagstand', 'blue_flagstand'):
                STANDS3.setdefault(m, {})[pp.TEAM_RED if n.startswith('red') else pp.TEAM_BLUE] = tuple(c['start']['origin'])
    return STANDS3


def load_demos(path, want_pos=None, keep=None, role_2v2=False):
    out = []
    for p in sorted(path.glob('*.json.gz')):
        if keep is not None and p.name not in keep:
            continue
        with gzip.open(p, 'rt', encoding='utf-8') as g:
            d = json.load(g)
        if want_pos and d['pos'] not in want_pos:
            continue
        dem = Demo(d, STANDS3)
        dem.role_2v2 = role_2v2
        dem.group = dem.map if not role_2v2 or dem.map == 'kejim' else 'other maps (2v2)'
        if dem.team:
            out.append(dem)
    return out


def aim_target(dem, t, eye, fwd, max_deg, max_dist=4000):
    s = dem.at(t)
    if not s or t - s[0] > 300:
        return None
    best = None
    for o in dem.enemies(s):
        a, dist = angle_to(eye, fwd, (o[1], o[2], o[3]))
        if a <= max_deg and dist <= max_dist and (best is None or a < best[0]):
            best = (a, dist, o)
    return best


def rotate_frame(dem, x, y):
    """Own-stand frame: forward = toward the enemy stand, left = +90 degrees."""
    ox, oy = dem.own[:2]
    ux, uy = dem.enemy_stand[0] - ox, dem.enemy_stand[1] - oy
    n = math.hypot(ux, uy)
    ux, uy = ux / n, uy / n
    dx, dy = x - ox, y - oy
    return dx * ux + dy * uy, -dx * uy + dy * ux


def kmeans(pts, wts, k, iters=40):
    import numpy as np
    X, W = np.array(pts, dtype=float), np.array(wts, dtype=float)
    # Deterministic farthest-point init from the heaviest-weighted point.
    cent = [X[np.argmax(W)]]
    for _ in range(1, k):
        d = np.min([np.sum((X - c) ** 2, axis=1) for c in cent], axis=0)
        cent.append(X[np.argmax(d * W)])
    C = np.array(cent)
    for _ in range(iters):
        lab = np.argmin(((X[:, None, :] - C[None]) ** 2).sum(axis=2), axis=1)
        for j in range(k):
            m = lab == j
            if W[m].sum() > 0:
                C[j] = (X[m] * W[m, None]).sum(axis=0) / W[m].sum()
    lab = np.argmin(((X[:, None, :] - C[None]) ** 2).sum(axis=2), axis=1)
    return C, lab


# ---------------------------------------------------------------- report sections

def sec_corpus(demos, say):
    say('== Corpus and validation ==')
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m]
        live_min = sum(sum(d.weight(i) for i, s in enumerate(d.samples) if s[0] in d.live) for d in ds) / 60
        rec = collections.Counter(d.d['pov_name'].lower() for d in ds)
        say('%s: %d demos, %d recorders, %.0f live minutes; parse errors %d; lost playerState events %d; fires %d, hit-counter changes %d' % (
            m, len(ds), len(rec), live_min, sum(1 for d in ds if d.d['error']), sum(d.d['lost_events'] for d in ds),
            sum(len(d.d['fires']) for d in ds), sum(len(d.d['hits']) for d in ds)))
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m and d.role_2v2]
        if not ds:
            continue
        alive = sum(d.weight(i) for d in ds for i, s in enumerate(d.samples) if s[0] in d.live and s[3][9])
        role = sum(d.weight(i) for d in ds for i, s in enumerate(d.samples) if s[0] in d.live and s[3][9] and d.role(s[0]))
        say('  %s: the POV is its team\'s defender (inferred) in %s of its live alive time (%.0f of %.0f min)' % (
            m, pct(role, alive), role / 60, alive / 60))
    say('\n  pull and push counts (POV Force drop with hand extend) against the sheet, per map:')
    for m in sorted({d.group for d in demos}):
        tot = collections.Counter()
        per = []
        for d in (x for x in demos if x.group == m):
            row = d.sheet.get(pp.name_key(d.d['pov_name']))
            if not row:
                continue
            try:
                sp, sl = int(row['Push']), int(row['Pull'])
            except (KeyError, ValueError):
                continue
            dp = sum(1 for s in d.d['spends'] if s[3] == HAND_PUSH)
            dl = sum(1 for s in d.d['spends'] if s[3] == HAND_PULL)
            tot.update(dp=dp, sp=sp, dl=dl, sl=sl, n=1)
            per.append(abs(dl - sl))
            try:
                acc = int(row['Acc'])
            except (KeyError, ValueError):
                acc = None
            nf = len(d.d['fires'])
            nh = sum(max(0, h[1]) for h in d.d['hits'])
            tot.update(fires=nf, hits=nh)
            if acc is not None:
                tot.update(acc_n=1, acc_sum=acc)
        if not tot['n']:
            say('    %s: no stat sheets' % m)
            continue
        say('    %s: %d demos with a sheet row; pull demo/sheet %d/%d (per-demo |diff| median %s, max %s); push %d/%d' % (
            m, tot['n'], tot['dl'], tot['sl'], q(per, .5), max(per) if per else '-', tot['dp'], tot['sp']))
        say('      hit counter increments / fire events %d/%d = %s; sheet Acc mean %.0f (base_enhanced definition not checked)' % (
            tot['hits'], tot['fires'], pct(tot['hits'], tot['fires']), tot['acc_sum'] / max(1, tot['acc_n'])))
        say('      fires per demo sample-check: see fires.csv')


def home_samples(dem):
    """(index, sample) with the POV alive, live, not carrying, own flag home."""
    for i, s in enumerate(dem.samples):
        p = s[3]
        if s[0] not in dem.live or not p[9] or p[8] or not dem.role(s[0]):
            continue
        fs = pp.flag_state(s[1], dem.team)
        if fs and fs[0] == '0':
            yield i, s


def sec_position(demos, say, csvs):
    say('\n== Q1. Position while the own flag is home (POV alive, time-weighted; frame: forward = toward the enemy stand) ==')
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m]
        dist, dz, sectors, wsum = [], [], collections.Counter(), 0
        bands = collections.defaultdict(lambda: dict(dist=[], w=0.0, move=collections.Counter(), rad=collections.Counter()))
        for d in ds:
            sz = d.own[2]
            for i, s in home_samples(d):
                p = s[3]
                w = d.weight(i)
                r = math.dist(p[2:4], d.own[:2])
                f, l = rotate_frame(d, p[2], p[3])
                # Duplicate weight by rounding to 50 ms units so quantiles are time-weighted.
                reps = max(1, round(w / .05))
                dist += [r] * reps
                dz += [p[4] - sz] * reps
                b = math.degrees(math.atan2(l, f))
                sec = 'within 128' if r < 128 else ('fwd' if abs(b) <= 45 else 'back' if abs(b) >= 135 else 'left' if b > 0 else 'right')
                sectors[sec] += w
                wsum += w
                # Nearest alive enemy to the own stand.
                en = d.enemies(s)
                ne = min(en, key=lambda o: math.dist(o[1:4], d.own)) if en else None
                ned = math.dist(ne[1:4], d.own) if ne else 1e9
                band = 'enemy<1000' if ned < 1000 else 'enemy1000-1500' if ned < 1500 else 'none<1500'
                B = bands[band]
                B['dist'] += [r] * reps
                B['w'] += w
                # POV's own motion over the next 500 ms: toward the nearest enemy / toward the own stand.
                j = bisect.bisect_left(d.times, s[0] + 500)
                if j < len(d.samples) and d.samples[j][0] - s[0] <= 700 and d.samples[j][3][9]:
                    q2 = d.samples[j][3]
                    dt = (d.samples[j][0] - s[0]) / 1000
                    if ne:
                        ux, uy = ne[1] - p[2], ne[2] - p[3]
                        n = math.hypot(ux, uy) or 1
                        v = ((q2[2] - p[2]) * ux + (q2[3] - p[3]) * uy) / n / dt
                        B['move']['toward enemy' if v > 100 else 'away from enemy' if v < -100 else 'hold'] += w
                    v2 = (math.dist(p[2:4], d.own[:2]) - math.dist(q2[2:4], d.own[:2])) / dt
                    B['rad']['closing on stand' if v2 > 100 else 'leaving stand' if v2 < -100 else 'hold'] += w
        say('%s (%d demos, %.0f min with own flag home and POV alive):' % (m, len(ds), wsum / 60))
        say('  2D distance to own stand: %s' % qs(dist))
        say('  within 256 u %s, 512 u %s, 1000 u %s, 1500 u %s' % tuple(
            pct(sum(1 for x in dist if x < r), len(dist)) for r in (256, 512, 1000, 1500)))
        say('  height above the stand (origin z - stand origin z): %s' % qs(dz))
        say('  above +64: %s, level (+-64): %s, below -64: %s' % (
            pct(sum(1 for x in dz if x > 64), len(dz)), pct(sum(1 for x in dz if -64 <= x <= 64), len(dz)),
            pct(sum(1 for x in dz if x < -64), len(dz))))
        say('  bearing from the stand: %s' % ', '.join('%s %s' % (k, pct(sectors[k], wsum)) for k in
                                                     ('within 128', 'fwd', 'left', 'right', 'back')))
        for band in ('none<1500', 'enemy1000-1500', 'enemy<1000'):
            B = bands[band]
            say('  nearest enemy to own stand %-15s %5.1f min: POV dist to stand med %s p75 %s; own motion: %s; radial: %s' % (
                band, B['w'] / 60, q(B['dist'], .5) and round(q(B['dist'], .5)), q(B['dist'], .75) and round(q(B['dist'], .75)),
                ', '.join('%s %s' % (k, pct(v, sum(B['move'].values()))) for k, v in sorted(B['move'].items())),
                ', '.join('%s %s' % (k, pct(v, sum(B['rad'].values()))) for k, v in sorted(B['rad'].items()))))
    # Kejim posts per side.
    for m in sorted({d.group for d in demos}):
        say('\n  %s posts (weighted k-means, k=5, over samples within 1,500 u of the own stand, own flag home):' % m)
        for team in (pp.TEAM_RED, pp.TEAM_BLUE):
            pts, wts, nd = [], [], 0
            for d in (x for x in demos if x.group == m and x.team == team):
                nd += 1
                for i, s in home_samples(d):
                    p = s[3]
                    if math.dist(p[2:5], d.own) < 1500:
                        pts.append((p[2], p[3], p[4]))
                        wts.append(d.weight(i))
            if len(pts) < 50 or m not in STANDS3:
                continue
            tot_home = sum(d.weight(i) for d in demos if d.group == m and d.team == team for i, _ in home_samples(d))
            C, lab = kmeans(pts, wts, 5)
            say('    %s side (%d demos): %.0f%% of home time within 1,500 u' % (
                'red' if team == 1 else 'blue', nd, 100 * sum(wts) / max(1e-9, tot_home)))
            order = sorted(range(len(C)), key=lambda j: -sum(w for w, l in zip(wts, lab) if l == j))
            for j in order:
                ww = [w for w, l in zip(wts, lab) if l == j]
                spread = [math.dist(pt, C[j]) for pt, l in zip(pts, lab) if l == j]
                c = C[j]
                st = STANDS3[m][team]
                say('      post x %6.0f y %6.0f z %6.0f  share of home time %4.1f%%  dist to stand %4.0f  dz %+5.0f  spread med %3.0f u' % (
                    c[0], c[1], c[2], 100 * sum(ww) / tot_home, math.dist(c[:2], st[:2]), c[2] - st[2], q(spread, .5)))
                csvs['posts'].append(dict(map=m, team='red' if team == 1 else 'blue', x=round(c[0]), y=round(c[1]),
                                          z=round(c[2]), share=round(100 * sum(ww) / tot_home, 1),
                                          dist_stand=round(math.dist(c[:2], st[:2])), spread_med=round(q(spread, .5))))


def classify_fires(dem):
    """Each POV fire with its inferred target (nearest enemy to the view ray within AIM_DEG)."""
    rows = []
    for f in dem.d['fires']:
        t, w, alt, x, y, z, pitch, yaw, vh = f
        if t not in dem.live and not dem.at(t):
            continue
        s = dem.at(t)
        if not s or s[0] not in dem.live:
            continue
        eye = (x, y, z + vh)
        fwd = view_dir(pitch, yaw)
        tg = aim_target(dem, t, eye, fwd, AIM_DEG)
        en = dem.enemies(s)
        near_stand = any(math.dist(o[1:4], dem.own) < 1500 for o in en)
        row = dict(t=t, w=w, alt=alt, weapon=wname(w, alt), near_stand=near_stand, own_flag=dem.own_flag(t),
                   pov_dist_stand=math.dist((x, y), dem.own[:2]))
        if tg:
            a, dist, o = tg
            sp = SPEED.get((w, alt), 1500)
            flight = (dist / sp * 1000 if sp else 0) + FUSE_MS.get((w, alt), 0)
            pains = dem.pains.get(o[0], [])
            k = bisect.bisect_left(pains, t)
            pain = k < len(pains) and pains[k] <= t + flight + 300
            row.update(target=o[0], dist=dist, angle=a, carrier=bool(o[5] & dem.own_bit), pain=pain,
                       target_dist_stand=math.dist(o[1:4], dem.own))
        rows.append(row)
    return rows


def sec_shooting(demos, say, csvs):
    say('\n== Q2. Shooting (observed fire events and hit counter; target inferred: nearest enemy within %d deg of the view ray) ==' % AIM_DEG)
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m]
        held, cond_w = collections.Counter(), 0.0
        fires = []
        hitw = collections.Counter()
        firesw = collections.Counter()
        for d in ds:
            for i, s in enumerate(d.samples):
                if s[0] not in d.live or not s[3][9] or not d.role(s[0]):
                    continue
                if any(math.dist(o[1:4], d.own) < 1500 for o in d.enemies(s)):
                    w = d.weight(i)
                    held[wname(s[3][14])] += w
                    cond_w += w
            fr = [r for r in classify_fires(d) if d.role(r['t'])]
            for r in fr:
                r['demo'] = d.d['demo']
            fires += fr
            # Hit counter: each increment attributed to the latest fire within 3 s before it.
            ft = [f[0] for f in d.d['fires']]
            for h in d.d['hits']:
                if h[1] <= 0 or h[0] not in d.live and not d.at(h[0]) or not d.role(h[0]):
                    continue
                k = bisect.bisect_right(ft, h[0]) - 1
                if k >= 0 and h[0] - ft[k] <= 3000:
                    f = d.d['fires'][k]
                    hitw[wname(f[1], f[2])] += h[1]
                else:
                    hitw['no fire within 3 s (%s held)' % wname(h[4])] += h[1]
            for f in d.d['fires']:
                if d.at(f[0]) and d.at(f[0])[0] in d.live and d.role(f[0]):
                    firesw[wname(f[1], f[2])] += 1
        say('%s: %.1f min with an enemy within 1,500 u of the own stand (POV alive)' % (m, cond_w / 60))
        say('  weapon held then: %s' % ', '.join('%s %s' % (k, pct(v, cond_w)) for k, v in held.most_common(8)))
        near = [r for r in fires if r['near_stand']]
        c = collections.Counter(r['weapon'] for r in near)
        say('  fires with an enemy within 1,500 u of the own stand: %d (%.1f/min of that time): %s' % (
            len(near), len(near) / max(1e-9, cond_w / 60), ', '.join('%s %.1f' % (k, v / (cond_w / 60)) for k, v in c.most_common(10))))
        say('  all live fires %d; with an inferred target %d (%s)' % (len(fires), sum(1 for r in fires if 'dist' in r),
                                                                    pct(sum(1 for r in fires if 'dist' in r), len(fires))))
        say('  by weapon (all live fires): fires, share with target, range to target, pain on target within flight+300 ms, hit-counter increments per fire')
        for wk, n in collections.Counter(r['weapon'] for r in fires).most_common(12):
            sub = [r for r in fires if r['weapon'] == wk]
            tg = [r for r in sub if 'dist' in r]
            say('    %-15s %5d  target %4s  range %s  pain %4s  hits/fire %.2f' % (
                wk, n, pct(len(tg), n), qs([r['dist'] for r in tg]), pct(sum(1 for r in tg if r['pain']), len(tg)),
                hitw[wk] / max(1, firesw[wk])))
        rest = [(k, v) for k, v in hitw.items() if k.startswith('no fire')]
        say('    hit-counter increments without a fire within 3 s: %s' % ', '.join('%s %d' % kv for kv in sorted(rest, key=lambda kv: -kv[1])[:6]))
        # Before vs after the steal.
        say('  fires by own-flag state (targeted fires): home %d (at a non-carrier %s), taken %d (at the carrier %s), dropped %d' % (
            sum(1 for r in fires if r['own_flag'] == '0' and 'dist' in r),
            pct(sum(1 for r in fires if r['own_flag'] == '0' and 'dist' in r and not r['carrier']),
                sum(1 for r in fires if r['own_flag'] == '0' and 'dist' in r)),
            sum(1 for r in fires if r['own_flag'] == '1' and 'dist' in r),
            pct(sum(1 for r in fires if r['own_flag'] == '1' and 'dist' in r and r['carrier']),
                sum(1 for r in fires if r['own_flag'] == '1' and 'dist' in r)),
            sum(1 for r in fires if r['own_flag'] == '2' and 'dist' in r)))
        # For each steal: fires at the eventual thief in the 10 s before and after.
        pre, post, nsteal, pre_any, post_any = [], [], 0, 0, 0
        for d in ds:
            fr = [r for r in fires if r['demo'] == d.d['demo'] and 'target' in r]
            for (t0, a), (t1, b) in zip(d.d['flags'], d.d['flags'][1:]):
                fa, fb = pp.flag_state(a, d.team), pp.flag_state(b, d.team)
                if not (fa and fb and fa[0] == '0' and fb[0] == '1') or t1 not in d.live and not d.at(t1) or not d.role(t1):
                    continue
                s = next((x for x in d.samples[bisect.bisect_left(d.times, t1):] if x[0] > t1 + 50), None)
                if not s or s[0] - t1 > 1000:
                    continue
                thief = next((o for o in s[4] if o[5] & d.own_bit), None)
                if not thief:
                    continue
                nsteal += 1
                a1 = sum(1 for r in fr if r['target'] == thief[0] and t1 - 10000 <= r['t'] < t1)
                a2 = sum(1 for r in fr if r['target'] == thief[0] and t1 <= r['t'] < t1 + 10000)
                pre.append(a1)
                post.append(a2)
                pre_any += a1 > 0
                post_any += a2 > 0
        say('  steals with the thief visible: %d; fires at the thief in the 10 s before the steal: total %d (in %s of steals), after: %d (in %s)' % (
            nsteal, sum(pre), pct(pre_any, nsteal), sum(post), pct(post_any, nsteal)))
        # Kills.
        kills, carrier_kills, near_kills = collections.Counter(), 0, 0
        for d in ds:
            for t, victim, attacker, mod in d.d['obits']:
                if attacker != d.me or victim == d.me or not d.is_enemy(victim, t) or not d.at(t) or d.at(t)[0] not in d.live                         or not d.role(t):
                    continue
                name = 'pit' if mod in PIT_MODS else MOD_NAMES.get(mod, str(mod))
                kills[name] += 1
                o = d.enemy_pos(victim, t - 100, 500)
                if o and o[5] & d.own_bit:
                    carrier_kills += 1
                if o and math.dist(o[1:4], d.own) < 1500:
                    near_kills += 1
        n = sum(kills.values())
        say('  kills of enemies by the POV (obituaries): %d, carrier of own flag %d, victim within 1,500 u of own stand %d: %s' % (
            n, carrier_kills, near_kills, ', '.join('%s %d' % kv for kv in kills.most_common(12))))
        for r in fires:
            csvs['fires'].append(dict(map=m, demo=Path(r['demo']).name, t=r['t'], weapon=r['weapon'], own_flag=r['own_flag'],
                                      near_stand=int(r['near_stand']), dist=round(r.get('dist', -1)),
                                      angle=round(r.get('angle', -1), 1), carrier=int(r.get('carrier', 0)),
                                      pain=int(r.get('pain', 0))))


def thefts(dem):
    """Own-flag steals with the thief visible within 1 s: (t_steal, thief client, t_end, end_state)."""
    out = []
    flags = dem.d['flags']
    for (t0, a), (t1, b) in zip(flags, flags[1:]):
        fa, fb = pp.flag_state(a, dem.team), pp.flag_state(b, dem.team)
        if not (fa and fb and fa[0] == '0' and fb[0] == '1'):
            continue
        s0 = dem.at(t1)
        if not s0 or s0[0] not in dem.live:
            continue
        s = next((x for x in dem.samples[bisect.bisect_left(dem.times, t1):] if x[0] > t1 + 50), None)
        if not s or s[0] - t1 > 1000:
            continue
        thief = next((o for o in s[4] if o[5] & dem.own_bit), None)
        if not thief:
            continue
        end = next(((t, st) for t, st in flags if t > t1 and pp.flag_state(st, dem.team) and pp.flag_state(st, dem.team)[0] != '1'),
                   (t1 + 10 ** 9, ''))
        out.append((t1, thief[0], end[0], (pp.flag_state(end[1], dem.team) or ('?',))[0]))
    return out


def sec_chase(demos, chase_demos, say):
    say('\n== Q3. After the own flag is taken (steal with the thief visible within 1 s; distances 2D) ==')
    for m in sorted({d.group for d in demos}):
        for label, ds in (('2v2 defender' if demos and demos[0].role_2v2 else 'Bas', [d for d in demos if d.group == m]), ('Cha (pilot extract-v3)', [d for d in chase_demos if d.group == m])):
            if not ds:
                continue
            by_dt = collections.defaultdict(list)
            stand_dt = collections.defaultdict(list)
            thief_dt = collections.defaultdict(list)
            maxd, turn, reach, start_home, n = [], [], 0, 0, 0
            follow = collections.Counter()
            for d in ds:
                for t1, thief, te, endst in thefts(d):
                    p0 = d.pov_pos(t1)
                    if not p0 or not p0[9] or not d.role(t1):
                        continue
                    n += 1
                    start_home += math.dist(p0[2:4], d.own[:2]) < 1000
                    far, far_t, reached = 0, 0, False
                    for dt in range(0, 20001, 250):
                        t = t1 + dt
                        if t >= te:
                            break
                        p = d.pov_pos(t, 200)
                        o = d.enemy_pos(thief, t, 200)
                        if not p or not p[9]:
                            continue
                        ds_ = math.dist(p[2:4], d.own[:2])
                        if ds_ > far:
                            far, far_t = ds_, dt
                        if o and math.dist(o[1:3], p[2:4]) < 400:
                            reached = True
                        if dt % 1000 == 0 and dt <= 10000:
                            stand_dt[dt].append(ds_)
                            if o:
                                by_dt[dt].append(math.dist(o[1:3], p[2:4]))
                                thief_dt[dt].append(math.dist(o[1:3], d.own[:2]))
                    maxd.append(far)
                    if te - t1 > far_t + 2000:
                        turn.append(far_t)
                    reach += reached
                    follow['max dist from stand > 1500'] += far > 1500
                    follow['> 3000'] += far > 3000
            say('%s %s: %d thefts (POV alive at the steal; within 1,000 u of own stand then %s); reached the thief (400 u) %s' % (
                m, label, n, pct(start_home, n), pct(reach, n)))
            say('  POV max distance from its own stand while the theft lasts (first 20 s): %s' % qs(maxd))
            say('  > 1,500 u: %s, > 3,000 u: %s; time of that max (turn-back, only when the theft lasted 2 s longer): %s' % (
                pct(follow['max dist from stand > 1500'], n), pct(follow['> 3000'], n), qs(turn)))
            say('  dt(s)  POV-thief med (n)   POV-stand med   thief-stand med')
            for dt in range(0, 10001, 1000):
                say('  %4d   %6s (%3d)      %6s          %6s' % (
                    dt // 1000, q(by_dt[dt], .5) and round(q(by_dt[dt], .5)), len(by_dt[dt]),
                    q(stand_dt[dt], .5) and round(q(stand_dt[dt], .5)), q(thief_dt[dt], .5) and round(q(thief_dt[dt], .5))))


def sec_pulls(demos, say, csvs, pitmaps):
    say('\n== Q4. Pulls and pushes by the base POV (spend with hand extend; target inferred) ==')
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m]
        pm = pitmaps.get(m)
        rows = []
        for d in ds:
            fire_rows = [r for r in classify_fires(d) if 'target' in r]
            ftimes = [r['t'] for r in fire_rows]
            obits = sorted(d.d['obits'])
            for sp in d.d['spends']:
                t, amt, sel, hand = sp[:4]
                if hand not in (HAND_PUSH, HAND_PULL) or len(sp) < 12:
                    continue
                s = d.at(t)
                if not s or s[0] not in d.live or not d.role(t):
                    continue
                x, y, z, pitch, yaw, vh = sp[6:12]
                eye = (x, y, z + vh)
                fwd = view_dir(pitch, yaw)
                arc = []
                for o in d.enemies(s):
                    if max(abs(o[1] - x), abs(o[2] - y), abs(o[3] - z)) > PULL_RANGE:
                        continue
                    a, dist = angle_to(eye, fwd, (o[1], o[2], o[3]))
                    if a < 90:
                        arc.append((a, dist, o))
                row = dict(demo=Path(d.d['demo']).name, t=t, kind='pull' if hand == HAND_PULL else 'push',
                           n_arc=len(arc), own_flag=d.own_flag(t), carrier_in_arc=any(o[5] & d.own_bit for _, _, o in arc),
                           pov_dist_stand=math.dist((x, y), d.own[:2]))
                if arc:
                    # Primary target: the carrier of the own flag if in the arc, else the enemy nearest the view ray.
                    car = [x_ for x_ in arc if x_[2][5] & d.own_bit]
                    a, dist, o = min(car or arc, key=lambda x_: x_[0])
                    tgt = o[0]
                    row.update(target=tgt, angle=a, dist=dist, carrier=bool(o[5] & d.own_bit),
                               tweapon=wname(o[7]) if len(o) > 7 else '?')
                    if pm:
                        row['los'] = pm.los(eye, (o[1], o[2], o[3] + 16))
                        row['pit_at'] = pm.pit_distance(o[1], o[2], o[3])
                        o5 = d.enemy_pos(tgt, t + 500, 150)
                        row['pit_500'] = pm.pit_distance(o5[1], o5[2], o5[3]) if o5 else None
                        row['pov_pit'] = pm.pit_distance(x, y, z)
                    ob = d.enemy_pos(tgt, t - 500, 150)
                    if ob:
                        # Range rate over the 500 ms before, positive = receding from the POV.
                        pb = d.pov_pos(t - 500, 150)
                        if pb:
                            row['range_rate'] = (dist - math.dist((pb[2], pb[3], pb[4] + vh), ob[1:4])) / .5
                        row['target_stand_rate'] = (math.dist((o[1], o[2]), d.own[:2]) - math.dist(ob[1:3], d.own[:2])) / .5
                    row['target_dist_stand'] = math.dist((o[1], o[2]), d.own[:2])
                    # Shoot-then-pull: a fire aimed at this target within 1,500 ms before.
                    k0 = bisect.bisect_left(ftimes, t - 1500)
                    k1 = bisect.bisect_left(ftimes, t)
                    shots = [fire_rows[k] for k in range(k0, k1) if fire_rows[k]['target'] == tgt]
                    row['combo'] = bool(shots)
                    if shots:
                        row['gap'] = t - shots[-1]['t']
                        row['combo_weapon'] = shots[-1]['weapon']
                        row['combo_shots'] = len(shots)
                    # Outcome within 3 s.
                    out = 'nothing'
                    for tt, victim, attacker, mod in obits:
                        if t < tt <= t + 3000 and victim == tgt:
                            if mod in PIT_MODS:
                                out = 'pitted (credited POV)' if attacker == d.me else 'pitted (other credit)'
                            elif attacker == d.me:
                                out = 'killed by POV'
                            else:
                                out = 'killed by other'
                            break
                    if out == 'nothing' and row['carrier']:
                        fl = [(tt, st) for tt, st in d.d['flags'] if t < tt <= t + 3000]
                        if any(pp.flag_state(st, d.team) and pp.flag_state(st, d.team)[0] in '02' for _, st in fl):
                            out = 'flag dropped/returned'
                    row['outcome'] = out
                rows.append(row)
        for kind in ('pull', 'push'):
            R = [r for r in rows if r['kind'] == kind]
            T = [r for r in R if 'target' in r]
            say('%s %ss: %d; with an enemy in the arc (1,024 u box, front 180 deg) %d (%s); carrier in arc %d; enemies in arc per %s: med %s' % (
                m, kind, len(R), len(T), pct(len(T), len(R)), sum(1 for r in R if r['carrier_in_arc']), kind,
                q([r['n_arc'] for r in R], .5)))
            if not T:
                continue
            say('  own flag at the %s: home %d, taken %d, dropped %d; POV dist to own stand %s' % (
                kind, sum(1 for r in R if r['own_flag'] == '0'), sum(1 for r in R if r['own_flag'] == '1'),
                sum(1 for r in R if r['own_flag'] == '2'), qs([r['pov_dist_stand'] for r in R])))
            say('  target angle off the view ray %s; LOS plausible (brush columns) %s' % (
                qs([r['angle'] for r in T]), pct(sum(1 for r in T if r.get('los')), sum(1 for r in T if 'los' in r))))
            for lab, S in (('carrier targets', [r for r in T if r['carrier']]), ('other targets', [r for r in T if not r['carrier']])):
                if not S:
                    continue
                rr = [r['range_rate'] for r in S if 'range_rate' in r]
                sr = [r['target_stand_rate'] for r in S if 'target_stand_rate' in r]
                say('  %s: %d' % (lab, len(S)))
                say('    distance at the %s: %s' % (kind, qs([r['dist'] for r in S])))
                say('    range rate over the 500 ms before (+ receding): %s; receding > 50 u/s %s, approaching < -50 %s' % (
                    qs(rr), pct(sum(1 for v in rr if v > 50), len(rr)), pct(sum(1 for v in rr if v < -50), len(rr))))
                say('    target moving away from own stand (+): %s' % qs(sr))
                say('    target distance from own stand: %s' % qs([r['target_dist_stand'] for r in S]))
                cb = [r for r in S if r['combo']]
                say('    fired at the target within 1,500 ms before: %d (%s); gap %s; weapons %s' % (
                    len(cb), pct(len(cb), len(S)), qs([r['gap'] for r in cb]),
                    ', '.join('%s %d' % kv for kv in collections.Counter(r['combo_weapon'] for r in cb).most_common(5))))
                for sub, SS in (('combo', cb), (kind + ' alone', [r for r in S if not r['combo']])):
                    oc = collections.Counter(r['outcome'] for r in SS)
                    say('    outcome within 3 s, %-10s n=%4d: %s' % (sub, len(SS), ', '.join('%s %s' % (k, pct(v, len(SS))) for k, v in oc.most_common())))
                pa = [r for r in S if r.get('pit_at') is not None and r.get('pit_500') is not None]
                if pa:
                    say('    target pit distance at the %s %s; 500 ms later closer to a pit in %s; target was within 128 u of a pit %s' % (
                        kind, qs([r['pit_at'] for r in pa]), pct(sum(1 for r in pa if r['pit_500'] < r['pit_at'] - 16), len(pa)),
                        pct(sum(1 for r in pa if r['pit_at'] < 128), len(pa))))
                    near = [r for r in pa if r['pit_at'] < 128]
                    say('      targets within 128 u of a pit: %d, pitted within 3 s %s; others pitted %s' % (
                        len(near), pct(sum(1 for r in near if r['outcome'].startswith('pitted')), len(near)),
                        pct(sum(1 for r in pa if r['pit_at'] >= 128 and r['outcome'].startswith('pitted')), sum(1 for r in pa if r['pit_at'] >= 128))))
        for r in rows:
            csvs['pulls'].append(dict(map=m, **{k: (round(v, 1) if isinstance(v, float) else v) for k, v in r.items()}))


def sec_other(demos, say):
    say('\n== Q5. Base Speed activations and Force at pulls ==')
    for m in sorted({d.group for d in demos}):
        ds = [d for d in demos if d.group == m]
        c, minutes = collections.Counter(), 0
        for d in ds:
            minutes += sum(d.weight(i) for i, s in enumerate(d.samples) if s[0] in d.live and d.role(s[0])) / 60
            for t, cl, b, h, fp, pov in d.d['force']:
                if not pov or b != FP_SPEED:
                    continue
                s = d.at(t)
                if not s or s[0] not in d.live or not d.role(t):
                    continue
                p = s[3]
                own = d.own_flag(t)
                en = d.enemies(s)
                ne = min((math.dist(o[1:4], p[2:5]) for o in en), default=1e9)
                if p[8]:
                    k = 'POV carrying'
                elif own == '1':
                    k = 'own flag taken'
                elif own == '2':
                    k = 'own flag dropped'
                else:
                    k = 'own flag home, enemy within 1500 of POV' if ne < 1500 else 'own flag home, no enemy near'
                c[k] += 1
                c['region ' + (d.region(p[2], p[3]) or '?')] += 1
                c['all'] += 1
        n = c['all']
        say('%s: %d Speed activations (%.1f/min): %s' % (m, n, n / max(1e-9, minutes), ', '.join(
            '%s %s' % (k, pct(v, n)) for k, v in c.most_common() if k != 'all' and not k.startswith('region'))))
        say('  region at activation: %s' % ', '.join('%s %s' % (k[7:], pct(v, n)) for k, v in c.most_common() if k.startswith('region')))
        # Share of live time in each own-flag state, for comparison.
        st = collections.Counter()
        for d in ds:
            for i, s in enumerate(d.samples):
                if s[0] in d.live and s[3][9] and d.role(s[0]):
                    fs = pp.flag_state(s[1], d.team)
                    st['taken' if fs and fs[0] == '1' else 'dropped' if fs and fs[0] == '2' else 'home'] += d.weight(i)
        tot = sum(st.values())
        say('  baseline, live alive time by own flag: %s' % ', '.join('%s %s' % (k, pct(v, tot)) for k, v in st.most_common()))
        fp = []
        for d in ds:
            for sp in d.d['spends']:
                if sp[3] == HAND_PULL and d.role(sp[0]):
                    fp.append(sp[5] + sp[1])
        say('  POV Force before a pull: %s' % qs(fp))


def add_los(pm):
    import numpy as np

    def los(a, b, step=16):
        n = max(1, int(math.dist(a, b) / step))
        for k in range(1, n):
            f = k / n
            x, y, z = a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f
            i, j = int((x - pm.x0) / 32), int((y - pm.y0) / 32)
            if not (0 <= i < pm.top.shape[0] and 0 <= j < pm.top.shape[1]):
                continue
            if np.any((pm.bot[i, j] < z) & (pm.top[i, j] > z)):
                return False
        return True
    pm.los = los
    return pm


def cmd_report(args):
    load_stands()
    demos = load_demos(args.out / EXTRACT_DIR)
    chase = load_demos(PILOT_OUT / 'extract-v3', want_pos=('Cha',))
    run_report(args, demos, chase, 'report.txt', '')


def selected_names(out, sel):
    return {extract_path(out / (EXTRACT_DIR + '-2v2'), r).name for r in csv.DictReader((out / sel).open(encoding='utf-8'))}


def cmd_report2v2(args):
    load_stands()
    keep = None if args.uncapped else selected_names(args.out, 'selection2v2.csv')
    demos = load_demos(args.out / (EXTRACT_DIR + '-2v2'), keep=keep, role_2v2=True)
    print('2v2 demos loaded: %d (%s)' % (len(demos), collections.Counter(d.map for d in demos)))
    run_report(args, demos, [], 'report2v2%s.txt' % ('-all' if args.uncapped else ''), '2v2_' + ('all_' if args.uncapped else ''))


def cmd_extract2v2(args):
    scan = [r for r in csv.DictReader((args.out / 'scan2v2.csv').open(encoding='utf-8'))
            if int(r['n'] or 0) >= 6 and int(r['n22']) >= .6 * int(r['n'])]
    rows = [dict(demo=r['demo'], match_id='', map=r['map'], client_id='', pov_name=r['pov'], alias='', pos='2v2',
                 sheet='', key=r['key']) for r in scan]
    dest = args.out / (EXTRACT_DIR + '-2v2')
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
            print('%d/%d %s samples=%d fires=%d %s' % (i, len(todo), d['map'], len(d['samples']), len(d['fires']), d['error']),
                  flush=True)


def run_report(args, demos, chase, name, prefix):
    lines = []

    def say(s=''):
        lines.append(s)
        print(s, flush=True)

    csvs = collections.defaultdict(list)
    import position_open as po
    pitmaps = {m: add_los(po.PitMap(m)) for m in sorted({d.map for d in demos}) if m in po.BSPS}
    sec_corpus(demos, say)
    sec_position(demos, say, csvs)
    sec_shooting(demos, say, csvs)
    sec_chase(demos, chase, say)
    sec_pulls(demos, say, csvs, pitmaps)
    sec_other(demos, say)
    (args.out / name).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    for cname, rows in csvs.items():
        keys = []
        for r in rows:
            for k in r:
                if k not in keys:
                    keys.append(k)
        with (args.out / (prefix + cname + '.csv')).open('w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=keys)
            w.writeheader()
            w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('select', 'extract', 'report', 'scan2v2', 'select2v2', 'extract2v2', 'report2v2'))
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--uncapped', action='store_true', help='report2v2: every 2v2 demo, not the per-recorder cap')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 4))
    args = ap.parse_args()
    {'select': cmd_select, 'extract': cmd_extract, 'report': cmd_report, 'scan2v2': cmd_scan2v2,
     'select2v2': cmd_select2v2, 'extract2v2': cmd_extract2v2, 'report2v2': cmd_report2v2}[args.command](args)


if __name__ == '__main__':
    main()
