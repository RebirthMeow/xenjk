"""Human spawn-to-enemy-flag-stand times from locally saved CTF pug demos.

A study of how fast human offense players reach the enemy flag stand from
each spawn, for comparison with the movement runner. Offline only: reads
demos already on disk and the jkastats archive/Discord tables; never connects
to a game server.

Environment: JSON_ROOT (the folder holding the saved demos and jkastats/) and DM26_DIR (the dm26 demo parser's
folder). Both are outside the repository and have no default.

    python tools/human_rush_study.py index   [--out DIR]
    python tools/human_rush_study.py extract [--out DIR] [--jobs N] [--limit N]
    python tools/human_rush_study.py report  [--out DIR]

index   links each local full-match demo to its archive match id (by demo file
        name in the jkastats raw archive) and, through the match id, to the
        Discord pug bot's stat sheet, whose Pos column says whether the demo's
        POV player played offense (Off), base (Bas) or chase (Cha).
extract parses the Off-POV demos (dm26.py) into per-life trips: spawn to
        the POV's pickup of the enemy flag from its stand.
report  groups trips by map, spawn point and player and writes medians.
"""
import argparse
import csv
import hashlib
from concurrent.futures import ProcessPoolExecutor, as_completed
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def env_dir(name, what):
    """the folder an environment variable names; these inputs live outside the repository and have no default"""
    v = os.environ.get(name)
    if not v:
        raise SystemExit('set %s to %s' % (name, what))
    return Path(v)


def json_root():
    return env_dir('JSON_ROOT', 'the folder holding the saved demos and jkastats/')


def jkastats():
    return json_root() / 'jkastats' / 'data'


DEFAULT_OUT = ROOT / '.codex-research' / 'human-rush'
# Derived copies (trimmed clips, render staging) and non-demo trees.
SKIP_DIRS = {'.git', 'venv', 'demos output', 'mme', 'frag_clips', '__pycache__'}

CS_FLAGSTATUS = 23          # bg_public.h: two chars, red then blue; '0' at base
PERS_TEAM, PERS_SPAWN_COUNT = 3, 4
PW_REDFLAG, PW_BLUEFLAG = 4, 5
FP_SPEED = 2
PM_NORMAL = 0
TEAM_RED, TEAM_BLUE = 1, 2
ENTITYNUM_NONE = 1023


def strip_colors(name):
    return re.sub(r'\^.', '', name or '').strip()


def name_key(name):
    # Sheets print the first 18 characters of the name.
    return strip_colors(name)[:18].strip().lower()


# ---------------------------------------------------------------- index

def archive_name(local):
    # Local copies carry a download prefix the archive's file name lacks:
    # 'r001_12 <player> ...' (player downloads), '000aacf2__14 ...' (id_demos).
    return re.sub(r'^(r\d+_|[0-9a-f]{8}__)', '', local)

def local_demos():
    seen, out = set(), []
    for dirpath, dirnames, filenames in os.walk(json_root()):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and 'a full jka install' not in d]
        for f in filenames:
            if not f.endswith('.dm_26'):
                continue
            p = Path(dirpath) / f
            key = (f, p.stat().st_size)
            if key in seen:
                continue
            seen.add(key)
            out.append(p)
    return out


def archive_demo_index(wanted):
    """Demo file name -> archive match facts, for the names in `wanted`."""
    found = {}
    with (jkastats() / 'raw' / 'match_raw.jsonl').open(encoding='utf-8') as f:
        for line in f:
            if not any(w in line for w in ('.dm_26',)):
                continue
            try:
                m = json.loads(line)
            except ValueError:
                continue
            for d in m.get('demos') or []:
                base = str(d.get('id', '')).replace('\\', '/').rsplit('/', 1)[-1]
                if base in wanted and base not in found:
                    found[base] = dict(match_id=m.get('_id'), map=m.get('mapname'), is_match=m.get('is_match'),
                                       client_id=d.get('client_id'), pov_name=strip_colors(d.get('name')),
                                       player=d.get('player'))
    return found


def parse_sheet(path):
    """Rows (name_key, alias, pos) of a pug bot stat sheet's first table."""
    rows, lines = [], path.read_text(encoding='utf-8', errors='replace').splitlines()
    for i, line in enumerate(lines):
        if line.startswith('Name ') and ' Pos ' in line and ' Cap ' in line:
            dashes = lines[i + 1]
            spans, start = [], 0
            for part in dashes.split(' '):
                if part:
                    spans.append((start, start + len(part)))
                start += len(part) + 1
            for row in lines[i + 2:]:
                if not row.strip():
                    break
                if row.startswith('---') or row.startswith('TOTAL'):
                    continue
                cells = [row[a:b].strip() for a, b in spans[:3]]
                rows.append((cells[0][:18].strip().lower(), cells[1].lower(), cells[2]))
            break
    return rows


def sheet_index():
    out = {}
    for sub in sorted((jkastats() / 'discord' / 'sheets').iterdir()):
        for p in sub.glob('pug_*.txt'):
            mid = p.name[4:20].lower()
            if mid not in out:
                out[mid] = p
    return out


def cmd_index(args):
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    t = time.time()
    demos = local_demos()
    print('local demos (deduplicated): %d  %.0fs' % (len(demos), time.time() - t), flush=True)
    by_name = {}
    for p in demos:
        by_name.setdefault(archive_name(p.name), p)
    arch = archive_demo_index(set(by_name))
    print('linked to archive matches: %d  %.0fs' % (len(arch), time.time() - t), flush=True)
    sheets = sheet_index()
    rows, counts = [], {}
    for name, p in sorted(by_name.items()):
        a = arch.get(name)
        pos, alias, sheet = '', '', ''
        if a and a['match_id'] and a['match_id'].lower() in sheets:
            sheet = sheets[a['match_id'].lower()]
            key = name_key(a['pov_name'])
            for nk, al, ps in parse_sheet(sheet):
                if nk == key:
                    pos, alias = ps, al
                    break
            else:
                pos = '?'  # sheet exists, POV name not on it (spectator or renamed)
        status = 'no_archive' if not a else 'no_sheet' if not sheet else pos
        counts[status] = counts.get(status, 0) + 1
        rows.append(dict(demo=str(p), match_id=a['match_id'] if a else '', map=a['map'] if a else '',
                         client_id=a['client_id'] if a else '', pov_name=a['pov_name'] if a else '',
                         alias=alias, pos=pos, sheet=str(sheet)))
    with (out / 'index.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['demo', 'match_id', 'map', 'client_id', 'pov_name', 'alias', 'pos', 'sheet'])
        w.writeheader()
        w.writerows(rows)
    print('status counts:', json.dumps(counts, sort_keys=True))
    print('wrote', out / 'index.csv')


# ---------------------------------------------------------------- extract

CS_WARMUP = 5
FLAG_WAIT_MS = 60000        # a life longer than this without a pickup is not a rush


EF_TELEPORT_BIT = 1 << 3
CS_PLAYERS = 1131           # bg_public.h: CS_ICONS + MAX_ICONS; info string per client, 't' is the team
ET_PLAYER = 1
NO_ENEMY = 99999
GRAVITY = 800.0
# Command lengths the air-strafe ceiling is quoted at: 8 ms (125 fps humans)
# and 25 ms (the bot's command step).
CEILING_MSEC = (8, 25)
AIR_SPEED_BANDS = ((0, 1.2), (1.2, 1.6), (1.6, 2.0), (2.0, 2.6), (2.6, 99))   # speed / wishspeed


def _nearest_enemy(parser, snap, dm26, team_cache):
    """Distance to the nearest enemy player in the snapshot (only players
    the server sent, i.e. potentially visible), or NO_ENEMY."""
    ps = snap.ps
    me, my_team = ps['clientNum'], ps['persistant'][PERS_TEAM]
    et, bx, by, bz = (dm26.ENT_IDX[k] for k in ('eType', 'pos.trBase[0]', 'pos.trBase[1]', 'pos.trBase[2]'))
    best = NO_ENEMY
    for num, st in snap.entities:
        if num >= 32 or num == me or st[et] != ET_PLAYER:
            continue
        cs = parser.configstrings.get(CS_PLAYERS + num, '')
        team = team_cache.get(cs)
        if team is None:
            try:
                team = int(dm26.parse_info_string(cs.strip().strip('"')).get('t', 0))
            except ValueError:
                team = 0
            team_cache[cs] = team
        if team in (TEAM_RED, TEAM_BLUE) and team != my_team:
            best = min(best, math.dist((st[bx], st[by], st[bz]),
                                       (ps['origin[0]'], ps['origin[1]'], ps['origin[2]'])))
    return best


def _sample(ps):
    vx, vy = ps['velocity[0]'], ps['velocity[1]']
    # 1-3 origin, 4 speed, 5 grounded, 6 Speed active, 7 commandTime, 8-10 velocity,
    # 11 wishspeed (ps.speed), 12 teleport bit, 13 damage event counter
    return (ps['origin[0]'], ps['origin[1]'], ps['origin[2]'], math.hypot(vx, vy),
            ps['groundEntityNum'] != ENTITYNUM_NONE, bool(ps['fd.forcePowersActive'] & (1 << FP_SPEED)),
            ps['commandTime'], vx, vy, ps['velocity[2]'], ps['speed'],
            bool(ps['eFlags'] & EF_TELEPORT_BIT), ps['damageEvent'])


def perfect_rate(v, w, h):
    """Largest d(v^2)/dt pure air strafing can reach at horizontal speed v and
    wishspeed w with commands of h seconds (PM_Accelerate, pm_airaccelerate 1):
    each command adds c = w*h along the wish direction, capped so the wish
    component stays under w, so the best command gives v'^2 = v^2 + c(2w - c)
    once v >= w - c, and v' = v + c below that."""
    c = w * h
    if v >= w - c:
        return c * (2 * w - c) / h
    return (2 * v * c + c * c) / h


def air_strafe(s):
    """Air strafe accounting over one life's samples. A snapshot pair counts
    only when nothing but strafing can have changed the horizontal velocity:
    airborne at both ends, no jump or ground contact between them (vertical
    velocity did not rise), no damage, no teleport, one wishspeed, and a
    horizontal velocity change within the strafe budget w*dt."""
    segs, cur, pairs = [], None, []
    for a, b in zip(s, s[1:]):
        dt = (b[7] - a[7]) / 1000.0
        w = a[11]
        dvx, dvy = b[8] - a[8], b[9] - a[9]
        ok = (not a[5] and not b[5] and 0 < dt <= .1 and w > 0 and abs(b[11] - w) < 1
              and a[12] == b[12] and a[13] == b[13] and b[10] <= a[10] + 5
              and math.hypot(dvx, dvy) <= w * dt * 1.02 + 1)
        if not ok:
            if cur:
                segs.append(cur)
            cur = None
            continue
        v0, v1 = math.hypot(a[8], a[9]), math.hypot(b[8], b[9])
        if cur is None:
            cur = dict(ms=0, gain=0.0, v0=v0, w=w, perfect={h: 0.0 for h in CEILING_MSEC},
                       bands={i: [0.0, 0.0, 0] for i in range(len(AIR_SPEED_BANDS))})
        # Per pair, for the best-case filters: ms, gain, ceiling at 8 and 25 ms,
        # speed/wishspeed (x100), heading turn rate (deg/s), nearest enemy, segment id.
        turn = 0.0
        if v0 > 1 and v1 > 1:
            turn = abs(math.degrees(math.atan2(a[8] * b[9] - a[9] * b[8], a[8] * b[8] + a[9] * b[9]))) / dt
        pairs.append((round(dt * 1000), round(v1 * v1 - v0 * v0), round(perfect_rate(v0, w, .008) * dt),
                      round(perfect_rate(v0, w, .025) * dt), round(100 * v0 / w), round(turn, 1),
                      round(min(a[14], b[14])), len(segs)))
        cur['ms'] += dt * 1000
        cur['gain'] += v1 * v1 - v0 * v0
        cur['v1'] = v1
        for h in CEILING_MSEC:
            cur['perfect'][h] += perfect_rate(v0, w, h / 1000.0) * dt
        band = next(i for i, (lo, hi) in enumerate(AIR_SPEED_BANDS) if lo <= v0 / w < hi)
        cur['bands'][band][0] += v1 * v1 - v0 * v0
        cur['bands'][band][1] += perfect_rate(v0, w, CEILING_MSEC[0] / 1000.0) * dt
        cur['bands'][band][2] += dt * 1000
    if cur:
        segs.append(cur)
    air_ms = sum((b[7] - a[7]) for a, b in zip(s, s[1:]) if not a[5] and not b[5] and 0 < b[7] - a[7] <= 100)
    return dict(air_ms=air_ms, counted_ms=round(sum(x['ms'] for x in segs)), pairs=pairs,
                segments=[dict(ms=round(x['ms']), gain=round(x['gain']), v0=round(x['v0']), v1=round(x['v1']),
                               w=round(x['w']), perfect={str(h): round(v) for h, v in x['perfect'].items()},
                               bands={str(i): [round(g), round(pf), round(ms)] for i, (g, pf, ms) in x['bands'].items()})
                          for x in segs])


def extract_demo(path):
    """Every life of the demo's POV player as a trip record (spawn to the
    first of: enemy-flag pickup, death, team change, demo end)."""
    sys.path.insert(0, str(env_dir('DM26_DIR', 'the folder with the dm26 demo parser (dm26.py)')))
    import dm26
    parser = dm26.DemoParser()
    trips, trip, last, mapname, team_cache = [], None, None, '', {}

    def close(outcome, ps=None, t=None, flag_base=None):
        nonlocal trip
        if trip is None:
            return
        trip.update(outcome=outcome, end_ms=t if t is not None else trip['samples'][-1][0],
                    enemy_flag_at_base_before=flag_base)
        if ps is not None:
            trip['end'] = [ps['origin[0]'], ps['origin[1]'], ps['origin[2]']]
        trips.append(trip)
        trip = None

    try:
        for ev in parser.parse_file(str(path)):
            kind = ev[0]
            if kind == 'gamestate':
                close('gamestate')
                info = dm26.parse_info_string(parser.configstrings.get(0, ''))
                mapname = info.get('mapname', mapname)
                last = None
                continue
            if kind != 'snapshot':
                continue
            snap = ev[1]
            ps, t = snap.ps, snap.serverTime
            team = ps['persistant'][PERS_TEAM]
            # dm26 keeps a 'cs' command's quotes and newline.
            status = parser.configstrings.get(CS_FLAGSTATUS, '').strip().strip('"')
            warm = parser.configstrings.get(CS_WARMUP, '').strip().strip('"') not in ('', '0')
            enemy_pw = PW_BLUEFLAG if team == TEAM_RED else PW_REDFLAG
            enemy_idx = 1 if team == TEAM_RED else 0
            if last is not None:
                if trip is not None and (team != trip['team_id'] or ps['clientNum'] != trip['client']):
                    close('team_change', ps, t)
                elif trip is not None and (ps['pm_type'] != PM_NORMAL or ps['stats'][0] <= 0):
                    close('died', ps, t)
                elif trip is not None and ps['powerups'][enemy_pw] and not last['has_flag']:
                    close('pickup', ps, t, last['enemy_base'])
                elif trip is not None and t - trip['start_ms'] > FLAG_WAIT_MS:
                    close('timeout', ps, t)
                respawned = ps['persistant'][PERS_SPAWN_COUNT] != last['spawn_count']
                if respawned and team in (TEAM_RED, TEAM_BLUE) and ps['pm_type'] == PM_NORMAL:
                    close('respawn_unseen', ps, t)
                    trip = dict(demo=str(path), map=mapname, client=ps['clientNum'], team_id=team,
                                team='red' if team == TEAM_RED else 'blue', start_ms=t,
                                start=[ps['origin[0]'], ps['origin[1]'], ps['origin[2]']],
                                start_yaw=ps['viewangles[1]'], warmup=warm,
                                enemy_flag_at_base_at_spawn=len(status) == 2 and status[enemy_idx] == '0',
                                samples=[])
            if trip is not None:
                trip['samples'].append((t,) + _sample(ps) + (_nearest_enemy(parser, snap, dm26, team_cache),))
            last = dict(spawn_count=ps['persistant'][PERS_SPAWN_COUNT], has_flag=bool(ps['powerups'][enemy_pw]),
                        enemy_base=len(status) == 2 and status[enemy_idx] == '0')
        close('demo_end')
        error = ''
    except Exception as exc:  # a desynced or truncated demo keeps the trips before it
        close('parse_error')
        error = '%s: %s' % (type(exc).__name__, exc)
    for tr in trips:
        s = tr.pop('samples')
        tr['duration_ms'] = tr['end_ms'] - tr['start_ms']
        tr['n'] = len(s)
        speeds = [x[4] for x in s]
        tr['mean_speed'] = round(statistics.fmean(speeds), 1) if speeds else 0
        tr['max_speed'] = round(max(speeds), 1) if speeds else 0
        tr['path_len'] = round(sum(math.dist(a[1:4], b[1:4]) for a, b in zip(s, s[1:])), 1)
        tr['ground_frac'] = round(sum(x[5] for x in s) / len(s), 3) if s else 0
        tr['speed_frac'] = round(sum(x[6] for x in s) / len(s), 3) if s else 0
        tr['first_snap_ms'] = s[0][0] - tr['start_ms'] if s else 0
        tr['air'] = air_strafe(s)
        # 2D track at snapshot rate for later plots: t, x, y, z, speed.
        tr['track'] = [(x[0] - tr['start_ms'], round(x[1]), round(x[2]), round(x[3]), round(x[4])) for x in s]
    return dict(demo=str(path), map=mapname, trips=trips, error=error, warnings=len(parser.warnings))


def cmd_extract(args):
    out = args.out
    if args.demo:
        # Explicit demos (the runner's server-recorded POV demos): labelled, no sheet.
        # A stable id per demo path, so a re-run finds its cache. Extracts made
        # before the id was stable are matched by the demo path they record.
        done = set()
        for f in (out / 'trips').glob('bot-*.json'):
            done.add(str(Path(json.loads(f.read_text(encoding='utf-8'))['demo']).resolve()).lower())
        rows = [dict(demo=str(d), client_id='x', pov_name=args.label, alias=args.label, map='',
                     match_id='bot-' + hashlib.sha1(str(Path(d).resolve()).lower().encode('utf-8')).hexdigest()[:12])
                for d in args.demo if str(Path(d).resolve()).lower() not in done]
    else:
        rows = [r for r in csv.DictReader((out / 'index.csv').open(encoding='utf-8')) if r['pos'] == 'Off']
    if args.limit:
        rows = rows[:args.limit]
    done_dir = out / 'trips'
    done_dir.mkdir(parents=True, exist_ok=True)
    todo = [r for r in rows if not (done_dir / (r['match_id'] + '_' + r['client_id'] + '.json')).exists()]
    print('offense demos %d, to parse %d, jobs %d' % (len(rows), len(todo), args.jobs), flush=True)
    t0 = time.time()
    with ProcessPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(extract_demo, Path(r['demo'])): r for r in todo}
        for i, fut in enumerate(as_completed(futures), 1):
            r = futures[fut]
            try:
                res = fut.result()
            except Exception as exc:
                res = dict(demo=r['demo'], trips=[], error=repr(exc))
            res.update(match_id=r['match_id'], pov_name=r['pov_name'], alias=r['alias'], index_map=r['map'],
                       bot=bool(args.demo))
            (done_dir / (r['match_id'] + '_' + r['client_id'] + '.json')).write_text(json.dumps(res), encoding='utf-8')
            print('%d/%d %.0fs trips=%d %s%s' % (i, len(todo), time.time() - t0, len(res['trips']),
                                                  os.path.basename(r['demo']), ' ERROR ' + res['error'] if res['error'] else ''),
                  flush=True)


SPAWN_RADIUS = 96.0         # starts this close are one spawn point (first snapshot may be ~1 frame in)
STAND_RADIUS = 160.0        # a pickup this close to the stand's median pickup point is from the stand
SLOW_FACTOR = 2.0           # slower than this times the spawn's median: not rushing (reported, excluded)


def _cluster(points, radius):
    """Greedy clusters of 3D points; returns (centres, label per point)."""
    centres, members, labels = [], [], []
    for p in points:
        for i, c in enumerate(centres):
            if math.dist(p[:2], c[:2]) <= radius and abs(p[2] - c[2]) <= 64:
                members[i].append(p)
                centres[i] = [statistics.median(q[k] for q in members[i]) for k in range(3)]
                labels.append(i)
                break
        else:
            centres.append(list(p))
            members.append([p])
            labels.append(len(centres) - 1)
    return centres, labels


def _q(values, q):
    v = sorted(values)
    if not v:
        return float('nan')
    k = (len(v) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def _strafe_sum(group):
    """Air strafe efficiency of a set of trips: gained d(v^2) over the
    ceiling's, summed over counted air time (time-weighted)."""
    gain, ms, perfect, bands, seg_eff = 0.0, 0, {str(h): 0.0 for h in CEILING_MSEC}, {}, []
    air = 0
    for t in group:
        a = t.get('air')
        if not a:
            continue
        air += a['air_ms']
        for x in a['segments']:
            gain += x['gain']
            ms += x['ms']
            for h in perfect:
                perfect[h] += x['perfect'][h]
            for b, (g, pf, bms) in x['bands'].items():
                acc = bands.setdefault(b, [0.0, 0.0, 0])
                acc[0] += g
                acc[1] += pf
                acc[2] += bms
            if x['ms'] >= 300 and x['perfect']['8'] > 0:
                seg_eff.append(x['gain'] / x['perfect']['8'])
    row = dict(trips=len(group), air_s=round(air / 1000, 1), counted_s=round(ms / 1000, 1))
    for h in perfect:
        row['eff_%sms' % h] = round(gain / perfect[h], 3) if perfect[h] else ''
    row['segments_300ms'] = len(seg_eff)
    row['seg_median_8ms'] = round(statistics.median(seg_eff), 3) if seg_eff else ''
    row['seg_p90_8ms'] = round(_q(seg_eff, .9), 3) if seg_eff else ''
    for i, (lo, hi) in enumerate(AIR_SPEED_BANDS):
        g, pf, bms = bands.get(str(i), [0, 0, 0])
        row['eff_8ms_v%.1f-%.1fw' % (lo, min(hi, 9))] = round(g / pf, 3) if pf else ''
        row['air_s_v%.1f-%.1fw' % (lo, min(hi, 9))] = round(bms / 1000, 1)
    return row


def _by_player(trips):
    """Trips grouped by player label, in label order: separately labelled
    bot runs (--label) are separate configurations and never pooled."""
    groups = {}
    for t in trips:
        groups.setdefault(t['player'], []).append(t)
    return sorted(groups.items())


def strafe_tables(out, lives, fast, bot_trips, stands):
    """Air-strafe efficiency per player: all lives, and the kept rush trips;
    the runner's trips (from --demo extracts) on the same terms."""
    for t in bot_trips:
        key = (t['map'], t['team'])
        t['rush'] = (t['outcome'] == 'pickup' and t.get('enemy_flag_at_base_before') and not t['warmup']
                     and t['enemy_flag_at_base_at_spawn'] and key in stands
                     and math.dist(t['end'][:2], stands[key][:2]) <= STAND_RADIUS)
    rows = []
    for scope, human, bot in (('all_lives', lives, bot_trips),
                              ('rush_trips', fast, [t for t in bot_trips if t['rush']])):
        players = {}
        for t in human:
            players.setdefault(t['player'], []).append(t)
        for name, group in sorted(players.items(), key=lambda kv: -len(kv[1])):
            rows.append(dict(scope=scope, player=name, bot=False, **_strafe_sum(group)))
        rows.append(dict(scope=scope, player='ALL HUMANS', bot=False, **_strafe_sum(human)))
        for name, group in _by_player(bot):
            rows.append(dict(scope=scope, player=name, bot=True, **_strafe_sum(group)))
    with (out / 'strafe_by_player.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    best_case_table(out, fast, [t for t in bot_trips if t['rush']])


TURN_MAX = 40.0             # deg/s: a perfect strafe curves the path at most ~29 deg/s (w/v sin(theta) <= 0.5 rad/s)
ENEMY_MIN = 1500.0          # units: nearest visible enemy player at least this far
STRETCH_MS = 500            # sustained strafing: unbroken qualifying air time of at least this


def _ladder(trips, ceiling):
    """Efficiency (at the actor's own command-length ceiling: column 2 for
    8 ms, 3 for 25 ms) as the air time is narrowed to the best case."""
    steps = {'L0 all counted air': lambda p: True,
             'L1 turn <= %d deg/s' % TURN_MAX: lambda p: p[5] <= TURN_MAX,
             'L2 + no enemy within %d' % ENEMY_MIN: lambda p: p[5] <= TURN_MAX and p[6] >= ENEMY_MIN}
    out = {}
    for name, keep in steps.items():
        g = pf = ms = 0
        for t in trips:
            for p in t['air'].get('pairs', ()):
                if keep(p):
                    g += p[1]; pf += p[ceiling]; ms += p[0]
        out[name] = (g / pf if pf else float('nan'), ms / 1000)
    # L3: unbroken runs of L2 pairs inside one segment, at least STRETCH_MS long.
    stretches = []
    for t in trips:
        run = []
        for p in list(t['air'].get('pairs', [])) + [None]:
            if p is not None and p[5] <= TURN_MAX and p[6] >= ENEMY_MIN and (not run or run[-1][7] == p[7]):
                run.append(p)
                continue
            if sum(x[0] for x in run) >= STRETCH_MS:
                stretches.append((sum(x[1] for x in run) / max(1, sum(x[ceiling] for x in run)), sum(x[0] for x in run)))
            run = [p] if p is not None and p[5] <= TURN_MAX and p[6] >= ENEMY_MIN else []
    g_ms = sum(ms for _, ms in stretches)
    effs = [e for e, _ in stretches]
    out['L3 + sustained >= %d ms' % STRETCH_MS] = (
        sum(e * ms for e, ms in stretches) / g_ms if g_ms else float('nan'), g_ms / 1000)
    return out, effs


def best_case_table(out, human, bot):
    rows = []
    players = {}
    for t in human:
        players.setdefault(t['player'], []).append(t)
    groups = [('ALL HUMANS', human, 2)] + [(n, g, 2) for n, g in sorted(players.items(), key=lambda kv: -len(kv[1]))
                                            if len(g) >= 50]
    groups += [(n, g, 3) for n, g in _by_player(bot)]
    for name, group, ceiling in groups:
        ladder, effs = _ladder(group, ceiling)
        row = dict(player=name, trips=len(group), ceiling_ms=8 if ceiling == 2 else 25)
        for step, (eff, secs) in ladder.items():
            row[step] = round(eff, 3)
            row[step + ' (s)'] = round(secs, 1)
        row['L3 stretches'] = len(effs)
        for q in (.5, .75, .9):
            row['L3 stretch p%d' % (q * 100)] = round(_q(effs, q), 3) if effs else ''
        rows.append(row)
    with (out / 'strafe_best_case.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def cmd_report(args):
    out = args.out
    trips, demos, errors = [], 0, 0
    for f in sorted((out / 'trips').glob('*.json')):
        d = json.loads(f.read_text(encoding='utf-8'))
        demos += 1
        errors += bool(d.get('error'))
        who = d.get('alias') or d.get('pov_name') or '?'
        for t in d['trips']:
            t.update(player=who, match_id=d['match_id'], bot=bool(d.get('bot')))
            trips.append(t)
    bot_trips = [t for t in trips if t['bot']]
    trips = [t for t in trips if not t['bot']]
    lives = [t for t in trips if not t['warmup']]
    rush = [t for t in lives if t['enemy_flag_at_base_at_spawn']]
    # The stand: median pickup point of pickups made while the flag was at base.
    stands = {}
    for key in {(t['map'], t['team']) for t in rush}:
        pts = [t['end'] for t in rush if (t['map'], t['team']) == key and t['outcome'] == 'pickup'
               and t['enemy_flag_at_base_before']]
        if pts:
            stands[key] = [statistics.median(p[k] for p in pts) for k in range(3)]
    ok = [t for t in rush if t['outcome'] == 'pickup' and t['enemy_flag_at_base_before']
          and (t['map'], t['team']) in stands and math.dist(t['end'][:2], stands[(t['map'], t['team'])][:2]) <= STAND_RADIUS]
    # Spawn points per map and team, from every non-warmup life's start.
    spawn_of = {}
    for key in {(t['map'], t['team']) for t in lives}:
        group = [t for t in lives if (t['map'], t['team']) == key]
        centres, labels = _cluster([t['start'] for t in group], SPAWN_RADIUS)
        order = sorted(range(len(centres)), key=lambda i: -labels.count(i))
        name = {i: 'S%d' % (r + 1) for r, i in enumerate(order)}
        for t, lab in zip(group, labels):
            t['spawn'] = name[lab]
            t['spawn_xyz'] = [round(c) for c in centres[lab]]
    # Not-rushing filter against the spawn's median.
    med = {}
    for t in ok:
        med.setdefault((t['map'], t['team'], t['spawn']), []).append(t['duration_ms'])
    med = {k: statistics.median(v) for k, v in med.items()}
    for t in ok:
        t['rushing'] = t['duration_ms'] <= SLOW_FACTOR * med[(t['map'], t['team'], t['spawn'])]
    fast = [t for t in ok if t['rushing']]

    fields = ['map', 'team', 'spawn', 'spawn_xyz', 'player', 'match_id', 'duration_ms', 'rushing', 'mean_speed',
              'max_speed', 'path_len', 'ground_frac', 'speed_frac', 'start', 'end', 'demo']
    with (out / 'rush_trips.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        w.writeheader()
        w.writerows(ok)

    def summary(group):
        d = [t['duration_ms'] / 1000 for t in group]
        players = {}
        for t in group:
            players.setdefault(t['player'], []).append(t['duration_ms'] / 1000)
        per_player = [statistics.median(v) for v in players.values()]
        return dict(n=len(d), players=len(players), median_s=round(statistics.median(d), 2),
                    p25_s=round(_q(d, .25), 2), best_s=round(min(d), 2),
                    median_of_player_medians_s=round(statistics.median(per_player), 2),
                    mean_speed=round(statistics.median(t['mean_speed'] for t in group)),
                    max_speed=round(statistics.median(t['max_speed'] for t in group)),
                    speed_frac=round(statistics.fmean(t['speed_frac'] for t in group), 2),
                    ground_frac=round(statistics.fmean(t['ground_frac'] for t in group), 2))

    by_spawn = {}
    for t in fast:
        by_spawn.setdefault((t['map'], t['team'], t['spawn'], tuple(t['spawn_xyz'])), []).append(t)
    spawn_rows = []
    for (m, team, sp, xyz), group in sorted(by_spawn.items()):
        died = sum(1 for t in rush if t['outcome'] == 'died' and (t['map'], t['team'], t.get('spawn')) == (m, team, sp))
        row = dict(map=m, team=team, spawn=sp, spawn_xyz=list(xyz), died_before_pickup=died, **summary(group))
        spawn_rows.append(row)
    by_player = {}
    for t in fast:
        by_player.setdefault((t['map'], t['team'], t['spawn'], t['player']), []).append(t)
    player_rows = [dict(map=m, team=team, spawn=sp, player=pl, **summary(g))
                   for (m, team, sp, pl), g in sorted(by_player.items())]
    # Fixed headers: a bot-only or unlucky limited batch has no qualifying pickups.
    summary_fields = ['n', 'players', 'median_s', 'p25_s', 'best_s', 'median_of_player_medians_s',
                      'mean_speed', 'max_speed', 'speed_frac', 'ground_frac']
    for name, rows, head in (('by_spawn.csv', spawn_rows, ['map', 'team', 'spawn', 'spawn_xyz', 'died_before_pickup']),
                             ('by_player.csv', player_rows, ['map', 'team', 'spawn', 'player'])):
        with (out / name).open('w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=head + summary_fields)
            w.writeheader()
            w.writerows(rows)
    counts = dict(demos=demos, demos_with_parse_error=errors, lives=len(trips), lives_after_warmup=len(lives),
                  lives_enemy_flag_at_stand=len(rush), stand_pickups=len(ok), rushing=len(fast),
                  not_rushing=len(ok) - len(fast),
                  died_before_pickup=sum(1 for t in rush if t['outcome'] == 'died'))
    strafe_tables(out, trips, fast, bot_trips, stands)
    (out / 'summary.json').write_text(json.dumps(dict(counts=counts, stands={'%s %s' % k: v for k, v in stands.items()}),
                                                 indent=2), encoding='utf-8')
    print(json.dumps(counts))
    print('wrote rush_trips.csv, by_spawn.csv, by_player.csv, summary.json in', out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('index', 'extract', 'report'))
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--demo', type=Path, action='append', default=[],
                        help='extract: parse these demos instead of the index offense demos')
    parser.add_argument('--label', default='Runner_v2', help='extract --demo: the player label')
    args = parser.parse_args()
    {'index': cmd_index, 'extract': cmd_extract, 'report': cmd_report}[args.command](args)


if __name__ == '__main__':
    main()
