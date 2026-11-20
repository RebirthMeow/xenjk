"""Demo position pilot: what local CTF pug demos can say about the positions.

Roadmap step 2 (docs/movement/PLAN.md), record
docs/movement/records/2026-09-27-position-pilot.md. Offline only: reads demos
already on disk and the Discord stat sheets; never connects to a server.
Environment: DM26_DIR (the dm26 demo parser's folder, required for extract).

    python tools/position_pilot.py select  [--out DIR]
    python tools/position_pilot.py extract [--out DIR] [--jobs N]
    python tools/position_pilot.py report  [--out DIR]

select  picks up to PER_CELL demos per map and position (Off, Bas, Cha) from
        the human_rush_study index, at most PER_RECORDER per recording player.
extract parses each demo (dm26.py) into a timeline: sampled positions of the
        POV and every player its snapshots carry, flag status, team overlay,
        pain tiers, obituaries, Force activations and chat.
report  prints the observability audit and the first per-position tables.

Observed and inferred stay apart: the extract records only what the demo
carries; the report says which figures are observed and which are derived.
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
import statistics
import sys

ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / '.codex-research' / 'human-rush' / 'index.csv'
DEFAULT_OUT = ROOT / '.codex-research' / 'position-pilot-20260927'
FIXTURES = ROOT / 'tools' / 'fixtures' / 'movement'


def env_dir(name, what):
    """the folder an environment variable names; these inputs live outside the repository and have no default"""
    v = os.environ.get(name)
    if not v:
        raise SystemExit('set %s to %s' % (name, what))
    return Path(v)


def dm26_dir():
    return env_dir('DM26_DIR', 'the folder with the dm26 demo parser (dm26.py)')


def sheets_dir():
    return env_dir('JSON_ROOT', 'the folder holding jkastats/') / 'jkastats' / 'data' / 'discord' / 'sheets'


MAPS = {'mp/ctf_kejim': 'kejim', 'ctf_dantooine_v3': 'dantooine_v3'}
POSITIONS = ('Off', 'Bas', 'Cha')
PER_CELL, PER_RECORDER = 10, 3
SAMPLE_MS = 250
# v1 (first 12 demos, kept) lacked instant-power spends; v2 lacked the
# POV's armor, weapons, ammo and spawn count.
EXTRACT_DIR = 'extract-v3'

CS_WARMUP, CS_FLAGSTATUS = 5, 23
CS_LOCATIONS = 1227         # Dantooine v3 and Kejim both list item-derived locations from here
PERS_TEAM, PERS_SPAWN_COUNT = 3, 4
STAT_WEAPONS, STAT_ARMOR = 4, 5
AMMO_TYPES = 10             # bg_weapons.h AMMO_NONE .. AMMO_DETPACK
PM_NORMAL = 0
TEAM_RED, TEAM_BLUE = 1, 2
PW_REDFLAG, PW_BLUEFLAG = 4, 5
EF_DEAD = 1 << 1
ET_PLAYER, ET_EVENTS = 1, 18
EV_PAIN, EV_OBITUARY = 89, 93
FORCE_NAMES = ('heal', 'levitation', 'speed', 'push', 'pull', 'telepathy', 'grip', 'lightning', 'rage',
               'protect', 'absorb', 'team_heal', 'team_force', 'drain', 'see')
REGIONS = ('Fs', 'Bas', 'Mid', 'EBa', 'EFs')


def strip_colors(s):
    return re.sub(r'\^.', '', s or '').replace('\x19', '').strip()


def name_key(name):
    return strip_colors(name)[:18].strip().lower()


def flag_stands(map_key):
    cases = json.loads((FIXTURES / map_key / 'cases.json').read_text(encoding='utf-8'))['cases']
    out = {}
    for c in cases:
        n = c['start']['name']
        if n in ('red_flagstand', 'blue_flagstand'):
            out[TEAM_RED if n.startswith('red') else TEAM_BLUE] = c['start']['origin'][:2]
    return out


def region(team, x, y, stands):
    """base_enhanced GetCTFRegion (g_stats.c): 2D distance ratio to the stands."""
    if team not in (TEAM_RED, TEAM_BLUE):
        return None
    own, enemy = stands[team], stands[TEAM_BLUE if team == TEAM_RED else TEAM_RED]
    between = math.dist(own, enemy)
    a, e = math.dist((x, y), own), math.dist((x, y), enemy)
    if a == e:
        return 'Mid'
    if a < e:
        r = a / e
        return 'Fs' if e > between or r <= .2 else 'Bas' if r <= .4 else 'Mid'
    r = e / a
    return 'EFs' if a > between or r <= .2 else 'EBa' if r <= .4 else 'Mid'


def parse_sheet(path):
    """Every table row of a pug stat sheet: name key -> {column: value}."""
    rows, lines = {}, Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    for i, line in enumerate(lines):
        if not (line.startswith('Name ') and ' Pos ' in line) or i + 1 >= len(lines):
            continue
        spans, start = [], 0
        for part in lines[i + 1].split(' '):
            if part:
                spans.append((start, start + len(part)))
            start += len(part) + 1
        heads = [line[a:b].strip() for a, b in spans]
        for row in lines[i + 2:]:
            if not row.strip():
                break
            if row.startswith('---') or row.startswith('TOTAL'):
                continue
            cells = [row[a:b].strip() for a, b in spans]
            key = cells[0][:18].strip().lower()
            d = rows.setdefault(key, {})
            for h, c in zip(heads, cells):
                d.setdefault(h, c)
    return rows


# ---------------------------------------------------------------- select

def cmd_select(args):
    rows = [r for r in csv.DictReader(INDEX.open(encoding='utf-8')) if r['map'] in MAPS and r['pos'] in POSITIONS]
    chosen = []
    for m in MAPS:
        for pos in POSITIONS:
            cell = sorted((r for r in rows if r['map'] == m and r['pos'] == pos),
                          key=lambda r: hashlib.sha1(r['demo'].encode()).hexdigest())
            per, matches, got = collections.Counter(), set(), []
            for r in cell:
                who = r['pov_name'].lower()
                if per[who] >= PER_RECORDER or r['match_id'] in matches:
                    continue
                per[who] += 1
                matches.add(r['match_id'])
                got.append(r)
                if len(got) == PER_CELL:
                    break
            chosen += got
            print('%-18s %s: %d demos, %d recorders' % (m, pos, len(got), len(per)))
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / 'selection.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(chosen[0].keys()))
        w.writeheader()
        w.writerows(chosen)
    print('wrote', args.out / 'selection.csv', len(chosen))


# ---------------------------------------------------------------- extract

def extract_demo(row):
    sys.path.insert(0, str(dm26_dir()))
    import dm26
    E = dm26.ENT_IDX
    fx, fy, fz = E['pos.trBase[0]'], E['pos.trBase[1]'], E['pos.trBase[2]']
    et, evf, evp, fpa, pwf, eff = E['eType'], E['event'], E['eventParm'], E['forcePowersActive'], E['powerups'], E['eFlags']
    o1, o2 = E['otherEntityNum'], E['otherEntityNum2']
    parser = dm26.DemoParser()
    out = dict(demo=row['demo'], map=MAPS[row['map']], pos=row['pos'], pov_name=row['pov_name'], sheet=row['sheet'],
               match_id=row['match_id'], samples=[], tinfo=[], pains=[], obits=[], force=[], chat=[], cp=[],
               flags=[], names={}, locations={}, error='')
    last_sample, prev_ents, last_ev, prev_pov_fpa, last_flag, last_tinfo = -10 ** 9, {}, {}, 0, None, None
    obit_seen, last_snap, prev_pov_fp, prev_pov_fpa_spend = {}, 0, None, 0
    out['spends'] = []
    try:
        for ev in parser.parse_file(row['demo']):
            kind = ev[0]
            if kind == 'gamestate':
                for k, v in parser.configstrings.items():
                    if CS_LOCATIONS <= k < CS_LOCATIONS + 64 and str(v).strip('"\n '):
                        out['locations'][k - CS_LOCATIONS] = str(v).strip('"\n ')
                prev_ents = {}
                continue
            if kind == 'command':
                # Commands carry no time of their own: stamp them with the last snapshot's.
                text, t = ev[2], last_snap
                if text.startswith('tinfo '):
                    a = text.split()
                    n, vals = int(a[1]), []
                    for i in range(n):
                        c, loc, h, frc, w, pw = map(int, a[2 + 6 * i: 8 + 6 * i])
                        vals.append((c, loc, h, frc, pw >> 24, w))
                    if vals != last_tinfo:
                        out['tinfo'].append((t, vals))
                        last_tinfo = vals
                else:
                    m = re.match(r'^(t?chat) "(.*?)"(?: "(\d+)")?\s*$', text, re.S)
                    if m:
                        out['chat'].append((t, m.group(1), strip_colors(m.group(2)),
                                            int(m.group(3)) if m.group(3) else -1))
                    elif text.startswith('cp '):
                        out['cp'].append((t, strip_colors(text[3:].strip().strip('"'))))
                continue
            if kind != 'snapshot':
                continue
            snap = ev[1]
            ps, t = snap.ps, snap.serverTime
            last_snap = t
            me, my_team = ps['clientNum'], ps['persistant'][PERS_TEAM]
            for c in range(32):
                cs = parser.configstrings.get(dm26.CS_PLAYERS + c, '')
                if cs:
                    info = dm26.parse_info_string(str(cs).strip().strip('"'))
                    nm, tm = strip_colors(info.get('n', '')), int(info.get('t', 0) or 0)
                    cur = out['names'].get(c)
                    if not cur or cur[-1][1:] != [nm, tm]:
                        out['names'].setdefault(c, []).append([t, nm, tm])
            status = str(parser.configstrings.get(CS_FLAGSTATUS, '')).strip().strip('"')
            if status != last_flag:
                out['flags'].append((t, status))
                last_flag = status
            warm = str(parser.configstrings.get(CS_WARMUP, '')).strip().strip('"') not in ('', '0')
            # Force activations: the POV at every snapshot, others while visible in consecutive snapshots.
            pfa = ps['fd.forcePowersActive']
            for b in range(len(FORCE_NAMES)):
                if pfa & (1 << b) and not prev_pov_fpa & (1 << b):
                    out['force'].append((t, me, b, ps['stats'][0], ps['fd.forcePower'], 1))
            # Instant powers (push, pull, heal, team heal/energize) set no active
            # bit: record each POV Force drop with the selected power and the
            # hand extend (1 push, 2 pull) as the evidence of what was spent.
            fp = ps['fd.forcePower']
            if prev_pov_fp is not None and fp <= prev_pov_fp - 10 and not (pfa & ~prev_pov_fpa_spend):
                out['spends'].append((t, prev_pov_fp - fp, ps['fd.forcePowerSelected'], ps['forceHandExtend'],
                                      ps['stats'][0], fp))
            prev_pov_fp, prev_pov_fpa_spend = fp, pfa
            prev_pov_fpa = pfa
            ents = {}
            for num, st in snap.entities:
                if num < 32 and st[et] == ET_PLAYER:
                    ents[num] = st
                    if num in prev_ents:
                        rise = st[fpa] & ~prev_ents[num][fpa]
                        for b in range(len(FORCE_NAMES)):
                            if rise & (1 << b):
                                out['force'].append((t, num, b, None, None, 0))
                        e = st[evf]
                        if e and e != last_ev.get(num) and (e & ~0x300) == EV_PAIN:
                            out['pains'].append((t, num, st[evp]))
                    last_ev[num] = st[evf]
                elif st[et] == ET_EVENTS + EV_OBITUARY:
                    key = (st[o1], st[o2], st[evp])
                    if t - obit_seen.get(key, -10 ** 9) > 1500:
                        out['obits'].append((t, st[o1], st[o2], st[evp]))
                    obit_seen[key] = t
            prev_ents = ents
            if t - last_sample >= SAMPLE_MS:
                last_sample = t
                pov = (me, my_team, round(ps['origin[0]']), round(ps['origin[1]']), round(ps['origin[2]']),
                       ps['stats'][0], ps['fd.forcePower'], pfa,
                       int(bool(ps['powerups'][PW_REDFLAG])) | 2 * int(bool(ps['powerups'][PW_BLUEFLAG])),
                       int(ps['pm_type'] == PM_NORMAL and ps['stats'][0] > 0),
                       round(math.hypot(ps['velocity[0]'], ps['velocity[1]'])),
                       # 11 armor, 12 weapons bitmask (by weapon number), 13 ammo by type,
                       # 14 current weapon, 15 spawn count
                       ps['stats'][STAT_ARMOR], ps['stats'][STAT_WEAPONS], list(ps['ammo'][:AMMO_TYPES]),
                       ps['weapon'], ps['persistant'][PERS_SPAWN_COUNT])
                others = [(n, round(s[fx]), round(s[fy]), round(s[fz]), s[fpa],
                           int(bool(s[pwf] & (1 << PW_REDFLAG))) | 2 * int(bool(s[pwf] & (1 << PW_BLUEFLAG))),
                           int(not s[eff] & EF_DEAD)) for n, s in ents.items() if n != me]
                out['samples'].append((t, status, int(warm), pov, others))
    except Exception as exc:
        out['error'] = '%s: %s' % (type(exc).__name__, exc)
    return out


def cmd_extract(args):
    rows = list(csv.DictReader((args.out / 'selection.csv').open(encoding='utf-8')))
    dest = args.out / EXTRACT_DIR
    dest.mkdir(parents=True, exist_ok=True)
    todo = [r for r in rows if not (dest / (hashlib.sha1(r['demo'].encode()).hexdigest()[:16] + '.json.gz')).exists()]
    print('extracting %d of %d' % (len(todo), len(rows)), flush=True)
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(extract_demo, r): r for r in todo}
        for i, f in enumerate(as_completed(futs), 1):
            r = futs[f]
            d = f.result()
            p = dest / (hashlib.sha1(r['demo'].encode()).hexdigest()[:16] + '.json.gz')
            with gzip.open(p, 'wt', encoding='utf-8') as g:
                json.dump(d, g)
            print('%d/%d %s %s samples=%d %s' % (i, len(todo), d['map'], d['pos'], len(d['samples']), d['error']), flush=True)


# ---------------------------------------------------------------- report

def load(out):
    for p in sorted((out / EXTRACT_DIR).glob('*.json.gz')):
        with gzip.open(p, 'rt', encoding='utf-8') as g:
            yield json.load(g)


def chat_body(text):
    # Extracts before the regex fix kept the sender argument ('..." "9').
    return re.sub(r'" "\d+\s*$', '', text)


def paused_windows(d):
    """cp countdowns ('The match has been paused. (120)') each hold 1.5 s."""
    return [t for t, text in d['cp'] if 'paused' in text.lower() and 'unpaused' not in text.lower()]


def live_samples(d):
    pauses = paused_windows(d)
    j = 0
    for s in d['samples']:
        t = s[0]
        while j < len(pauses) and pauses[j] < t - 1500:
            j += 1
        if s[2] or (j < len(pauses) and abs(pauses[j] - t) <= 1500):
            continue
        if s[3][1] not in (TEAM_RED, TEAM_BLUE):
            continue
        yield s


def flag_state(status, team):
    """(own flag, enemy flag) chars: '0' home, '1' taken, '2' dropped."""
    if len(status) != 2:
        return None
    red, blue = status[0], status[1]
    return (red, blue) if team == TEAM_RED else (blue, red)


def q(vals, p):
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(p * len(vals)))] if vals else None


def cmd_report(args):
    demos = list(load(args.out))
    lines, rep = [], {}

    def say(s=''):
        lines.append(s)
        print(s)

    say('demos loaded: %d, with parse errors: %d' % (len(demos), sum(1 for d in demos if d['error'])))
    for d in demos:
        d['stands'] = flag_stands(d['map'])
        d['sheet_rows'] = parse_sheet(d['sheet']) if d['sheet'] else {}

    # --- 1. Observability
    say('\n== Observability (observed) ==')
    for m in sorted({d['map'] for d in demos}):
        ds = [d for d in demos if d['map'] == m]
        vis_e, vis_a, n = 0, 0, 0
        tin_gap, tin_cover, pains, pain_vals, tch, tch_num, minutes = [], [], 0, collections.Counter(), 0, 0, 0
        for d in ds:
            live = list(live_samples(d))
            minutes += len(live) * SAMPLE_MS / 60000
            teams = {c: v[-1][2] for c, v in d['names'].items()}
            for s in live:
                me, team = s[3][0], s[3][1]
                ne = sum(1 for o in s[4] if teams.get(str(o[0])) not in (None, team, 0, 3))
                na = sum(1 for o in s[4] if teams.get(str(o[0])) == team)
                vis_e += ne / 4
                vis_a += na / 3
                n += 1
            ts = [t for t, _ in d['tinfo']]
            tin_gap += [b - a for a, b in zip(ts, ts[1:]) if b > a]
            tin_cover += [len(v) for _, v in d['tinfo']]
            pains += len(d['pains'])
            pain_vals.update(p for _, _, p in d['pains'])
            for t, kind, text, c in d['chat']:
                text = chat_body(text)
                if kind == 'tchat':
                    tch += 1
                    tch_num += bool(re.search(r'\d', text.split(':', 1)[-1]))
        say('%s: %d demos, %.0f live minutes' % (m, len(ds), minutes))
        say('  enemies in POV snapshot: %.0f%% of enemy-time; teammates: %.0f%%' % (100 * vis_e / max(1, n), 100 * vis_a / max(1, n)))
        say('  team overlay: median change interval %s ms, players per message median %s' % (q(tin_gap, .5), q(tin_cover, .5)))
        say('  pain events %d, parameter values %s' % (pains, pain_vals.most_common(6)))
        say('  team chat %d lines (%.1f/min), %d carry digits' % (tch, tch / max(1, minutes), tch_num))
        rep.setdefault('observability', {})[m] = dict(enemy_visible=vis_e / max(1, n), ally_visible=vis_a / max(1, n))

    # --- chat bind templates
    say('\n== Team chat templates (observed; digits masked, top 15) ==')
    tmpl = collections.Counter()
    for d in demos:
        for t, kind, text, c in d['chat']:
            if kind == 'tchat':
                body = chat_body(text).split(':', 1)[-1].strip().lower()
                tmpl[re.sub(r'\d+', '#', body)[:60]] += 1
    for k, v in tmpl.most_common(15):
        say('  %4d  %s' % (v, k))

    # --- 2. Region occupancy: POV against its sheet
    say('\n== POV region time against the stat sheet (validation) ==')
    occ = collections.defaultdict(lambda: collections.Counter())
    val = []
    for d in demos:
        c = collections.Counter()
        for s in live_samples(d):
            if s[3][9]:
                c[region(s[3][1], s[3][2], s[3][3], d['stands'])] += 1
        occ[d['pos']].update(c)
        row = d['sheet_rows'].get(name_key(d['pov_name']))
        tot = sum(c.values())
        if row and tot and all(r in row for r in REGIONS):
            try:
                sheet = [int(row[r]) for r in REGIONS]
            except ValueError:
                continue
            mine = [round(100 * c[r] / tot) for r in REGIONS]
            val.append(sum(abs(a - b) for a, b in zip(mine, sheet)) / 5)
    say('  mean absolute difference per region, percentage points: median %s, p90 %s over %d demos' % (
        q([round(v, 1) for v in val], .5), q([round(v, 1) for v in val], .9), len(val)))
    say('  %-4s %s' % ('pos', '  '.join('%4s' % r for r in REGIONS)))
    for pos in POSITIONS:
        c = occ[pos]
        tot = sum(c.values()) or 1
        say('  %-4s %s' % (pos, '  '.join('%3.0f%%' % (100 * c[r] / tot) for r in REGIONS)))

    # --- 3. Region by flag state
    say('\n== POV region by flag state (own, enemy: 0 home, 1 taken, 2 dropped) ==')
    by = collections.defaultdict(collections.Counter)
    for d in demos:
        for s in live_samples(d):
            fs = flag_state(s[1], s[3][1])
            if fs and s[3][9] and not s[3][8]:
                by[(d['pos'], fs)][region(s[3][1], s[3][2], s[3][3], d['stands'])] += 1
    for pos in POSITIONS:
        for fs in (('0', '0'), ('1', '0'), ('0', '1'), ('1', '1'), ('2', '0')):
            c = by[(pos, fs)]
            tot = sum(c.values())
            if tot < 40:
                continue
            say('  %-4s own=%s enemy=%s n=%5d  %s' % (pos, fs[0], fs[1], tot,
                                                   '  '.join('%s %3.0f%%' % (r, 100 * c[r] / tot) for r in REGIONS)))

    # --- 4. Steal response
    say('\n== After own flag is taken: does the POV head home? (observed positions; baseline = same test at random own-flag-home times) ==')

    def heads_home(samples, i, own):
        """First sample index from i at which the POV closes 150 units on its own stand within 500 ms."""
        for k in range(i, min(len(samples) - 2, i + 20)):
            a, b = samples[k][3], samples[k + 2][3]
            if a[9] and b[9] and math.dist(a[2:4], own) - math.dist(b[2:4], own) >= 150:
                return k
        return None

    for pos in POSITIONS:
        res = collections.defaultdict(list)
        for d in (x for x in demos if x['pos'] == pos):
            samples = list(live_samples(d))
            if not samples:
                continue
            team = samples[0][3][1]
            own = d['stands'][team]
            bit = 1 if team == TEAM_RED else 2
            times = [s[0] for s in samples]
            steals = []
            for (t0, a), (t1, b) in zip(d['flags'], d['flags'][1:]):
                fa, fb = flag_state(a, team), flag_state(b, team)
                if fa and fb and fa[0] == '0' and fb[0] == '1':
                    steals.append(t1)
            for ts in steals:
                i = bisect.bisect_left(times, ts)
                if i >= len(samples) - 3 or samples[i][0] - ts > 500 or not samples[i][3][9]:
                    continue
                reg = region(team, samples[i][3][2], samples[i][3][3], d['stands'])
                where = 'home' if reg in ('Fs', 'Bas') else 'away'
                seen = 'seen' if any(o[5] & bit for o in samples[i][4]) else 'unseen'
                k = heads_home(samples, i, own)
                res[(where, seen)].append(None if k is None else samples[k][0] - ts)
            # Baseline: every 20th sample with the own flag home.
            for i in range(0, len(samples) - 22, 20):
                fs = flag_state(samples[i][1], team)
                if fs and fs[0] == '0' and samples[i][3][9]:
                    reg = region(team, samples[i][3][2], samples[i][3][3], d['stands'])
                    k = heads_home(samples, i, own)
                    res[('home' if reg in ('Fs', 'Bas') else 'away', 'baseline')].append(
                        None if k is None else samples[k][0] - samples[i][0])
        for key in sorted(res):
            v = res[key]
            hit = [x for x in v if x is not None]
            say('  %-4s POV %-4s %-8s n=%4d  heads home within 5 s: %3.0f%%  median %s ms' % (
                pos, key[0], key[1], len(v), 100 * len(hit) / max(1, len(v)), q(hit, .5)))

    # --- 4b. Instant Force spends
    say('\n== POV Force spends without an active bit, per live minute (push/pull by hand extend, else the selected power; inferred label) ==')
    for pos in POSITIONS:
        ds = [d for d in demos if d['pos'] == pos]
        minutes = sum(sum(1 for _ in live_samples(d)) for d in ds) * SAMPLE_MS / 60000
        c, check = collections.Counter(), []
        for d in ds:
            mine = collections.Counter()
            for t, amt, sel, hand, hp, fp in d.get('spends', []):
                label = 'push' if hand == 1 else 'pull' if hand == 2 else FORCE_NAMES[sel] if 0 <= sel < len(FORCE_NAMES) else 'sel%d' % sel
                c[label] += 1
                mine[label] += 1
            row = d['sheet_rows'].get(name_key(d['pov_name']), {})
            try:
                check.append((mine['push'], int(row['Push']), mine['pull'], int(row['Pull']),
                              mine['heal'], int(row.get('Heal', 0) or 0)))
            except (KeyError, ValueError):
                pass
        say('  %-4s %.0f min: %s' % (pos, minutes, ', '.join('%s %.2f' % (k, v / max(1, minutes)) for k, v in c.most_common(8))))
        if check:
            say('       demo vs sheet totals: push %d/%d, pull %d/%d over %d demos' % (
                sum(x[0] for x in check), sum(x[1] for x in check), sum(x[2] for x in check), sum(x[3] for x in check), len(check)))

    # --- 5. Force use
    say('\n== POV Force activations per live minute (observed from player state) ==')
    for pos in POSITIONS:
        ds = [d for d in demos if d['pos'] == pos]
        minutes = sum(sum(1 for _ in live_samples(d)) for d in ds) * SAMPLE_MS / 60000
        c = collections.Counter()
        carry = collections.Counter()
        for d in ds:
            carrying = {}
            for s in d['samples']:
                carrying[s[0] // 1000] = s[3][8]
            for t, cl, b, h, fp, pov in d['force']:
                if pov:
                    c[FORCE_NAMES[b]] += 1
                    if carrying.get(t // 1000):
                        carry[FORCE_NAMES[b]] += 1
        say('  %-4s %.0f min: %s' % (pos, minutes, ', '.join('%s %.2f' % (k, v / max(1, minutes)) for k, v in c.most_common(8))))
        if carry:
            say('       while carrying: %s' % ', '.join('%s %d' % kv for kv in carry.most_common(6)))

    # --- 6. Allied FC health and the base
    say('\n== Base POV region while an ally carries, by the carrier\'s overlay health ==')
    bands = collections.defaultdict(collections.Counter)
    for d in (x for x in demos if x['pos'] == 'Bas'):
        tin = d['tinfo']
        j = 0
        for s in live_samples(d):
            me, team = s[3][0], s[3][1]
            while j + 1 < len(tin) and tin[j + 1][0] <= s[0]:
                j += 1
            if not tin or not s[3][9]:
                continue
            fs = flag_state(s[1], team)
            if not fs or fs[1] != '1':
                continue
            # Carrier: a visible teammate with the enemy flag, else unknown (overlay has no flag bit here).
            bit = 2 if team == TEAM_RED else 1
            fc = next((o[0] for o in s[4] if o[5] & bit), None)
            if fc is None:
                continue
            hp = next((v[2] for v in tin[j][1] if v[0] == fc), None)
            if hp is None:
                continue
            band = 'fc<50' if hp < 50 else 'fc50-99' if hp < 100 else 'fc100+'
            bands[band][region(team, s[3][2], s[3][3], d['stands'])] += 1
    for band in ('fc<50', 'fc50-99', 'fc100+'):
        c = bands[band]
        tot = sum(c.values())
        if tot:
            say('  %-8s n=%5d  %s' % (band, tot, '  '.join('%s %3.0f%%' % (r, 100 * c[r] / tot) for r in REGIONS)))
    (args.out / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('select', 'extract', 'report'))
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    args = ap.parse_args()
    {'select': cmd_select, 'extract': cmd_extract, 'report': cmd_report}[args.command](args)


if __name__ == '__main__':
    main()
