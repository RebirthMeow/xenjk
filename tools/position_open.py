"""Position pilot open items: chat binds, chase interception, pit exposure.

Record docs/movement/records/2026-09-27-position-pilot.md ("Open items").
Reads tools/position_pilot.py extracts (extract-v3) and the installed map
BSPs; offline only.

    python tools/position_open.py [binds|chase|pits|all] [--out DIR]

binds   decodes team chat: which overlay values (health, armor, Force) the
        numbers of status binds carry, and what follows the commonest calls.
chase   own-flag thefts seen from the POV: where the defender first reaches
        the thief (ahead of it or behind), what it holds then, how the theft
        ends; and the weapons that kill carriers.
pits    each map's pit is its one trigger_hurt slab; a point is over the pit
        when no walkable brush top lies between the slab and the player's
        feet and no brush fills that height. The distance to the nearest such
        point is a sample's pit distance; offense exposure is compared with
        and without an enemy near, against who gets pitted.
"""
import argparse
import bisect
import collections
import math
from pathlib import Path
import re
import struct
import sys
import zipfile

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp
from position_resources import Demo, pct

GAME_BASE = pp.ROOT / 'game_directory' / 'Jedi Academy' / 'GameData' / 'base'
BSPS = {'kejim': ('ctf_kejim.pk3', 'maps/mp/ctf_kejim.bsp'),
        'dantooine_v3': ('ctf_dantooine_v3.pk3', 'maps/ctf_dantooine_v3.bsp')}
MASK_PLAYERSOLID = 0x1 | 0x10 | 0x1000
CELL = 32
STEP = 18                    # stairs the player can climb
PIT_MODS = {0, 36, 38, 39, 41}
PUSH_RANGE = 1024
SABER = 3
SPLASH_MODS = {9, 14, 16, 18, 20, 22, 24, 25, 26, 27, 29, 30}   # bg_public.h meansOfDeath_t splash and explosives
MOD_NAMES = {3: 'saber', 4: 'bryar', 5: 'bryar_alt', 6: 'blaster', 8: 'disruptor', 10: 'sniper', 11: 'bowcaster',
             12: 'repeater', 13: 'repeater_alt', 14: 'repeater_alt_splash', 15: 'demp2', 16: 'demp2_alt',
             17: 'flechette', 18: 'flechette_alt_splash', 19: 'rocket', 20: 'rocket_splash', 21: 'homing',
             22: 'homing_splash', 23: 'thermal', 24: 'thermal_splash', 25: 'trip_mine', 26: 'timed_mine',
             27: 'det_pack', 29: 'conc', 30: 'conc_alt', 31: 'force_dark', 2: 'melee', 1: 'stun_baton'}


def lump(data, i):
    off, ln = struct.unpack_from('<2i', data, 8 + 8 * i)
    return data[off:off + ln]


class PitMap:
    """Column floors of a map's world brushes on a CELL grid."""

    def __init__(self, map_key):
        pk3, member = BSPS[map_key]
        data = zipfile.ZipFile(GAME_BASE / pk3).read(member)
        ents = lump(data, 0).decode('latin-1')
        models = lump(data, 7)
        self.hurt_top = None
        for block in re.findall(r'\{([^{}]*)\}', ents):
            kv = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block))
            if kv.get('classname') == 'trigger_hurt' and kv.get('model', '').startswith('*'):
                n = int(kv['model'][1:])
                top = struct.unpack_from('<3f', models, 40 * n + 12)[2]
                self.hurt_top = top if self.hurt_top is None else max(self.hurt_top, top)
        wmin = struct.unpack_from('<3f', models, 0)
        wmax = struct.unpack_from('<3f', models, 12)
        self.x0, self.y0 = wmin[0], wmin[1]
        nx, ny = int((wmax[0] - wmin[0]) / CELL) + 1, int((wmax[1] - wmin[1]) / CELL) + 1
        shaders = lump(data, 1)
        contents = [struct.unpack_from('<i', shaders, i * 72 + 68)[0] for i in range(len(shaders) // 72)]
        planes_raw = lump(data, 2)
        planes = np.frombuffer(planes_raw, dtype='<f4').reshape(-1, 4)
        _, _, _, _, _, _, _, _, first, count = struct.unpack_from('<6f4i', models, 0)
        brushes_raw, sides_raw = lump(data, 8), lump(data, 9)
        intervals = collections.defaultdict(list)     # (ix, iy) -> [(bottom, top, walkable)]
        xs = self.x0 + (np.arange(nx) + .5) * CELL
        ys = self.y0 + (np.arange(ny) + .5) * CELL
        for b in range(first, first + count):
            side, sides, shader = struct.unpack_from('<3i', brushes_raw, b * 12)
            if not contents[shader] & MASK_PLAYERSOLID:
                continue
            idx = [struct.unpack_from('<i', sides_raw, s * 12)[0] for s in range(side, side + sides)]
            P = planes[idx]
            n, d = P[:, :3], P[:, 3]
            bx = [-1e9, 1e9, -1e9, 1e9]
            for (a, bb, c), dd in zip(n, d):
                if abs(a) > .999 and abs(bb) < 1e-3 and abs(c) < 1e-3:
                    if a > 0: bx[1] = min(bx[1], dd)
                    else: bx[0] = max(bx[0], -dd)
                if abs(bb) > .999 and abs(a) < 1e-3 and abs(c) < 1e-3:
                    if bb > 0: bx[3] = min(bx[3], dd)
                    else: bx[2] = max(bx[2], -dd)
            if bx[0] < -1e8 or bx[2] < -1e8:
                continue
            i0, i1 = np.searchsorted(xs, bx[0]), np.searchsorted(xs, bx[1])
            j0, j1 = np.searchsorted(ys, bx[2]), np.searchsorted(ys, bx[3])
            if i0 >= i1 or j0 >= j1:
                continue
            X, Y = np.meshgrid(xs[i0:i1], ys[j0:j1], indexing='ij')
            rhs = d[None, None, :] - X[..., None] * n[:, 0] - Y[..., None] * n[:, 1]
            nz = n[:, 2]
            up = np.where(nz > 1e-6, rhs / np.where(nz > 1e-6, nz, 1), np.inf).min(axis=2)
            lo = np.where(nz < -1e-6, rhs / np.where(nz < -1e-6, nz, 1), -np.inf).max(axis=2)
            flat_ok = np.all(np.where(np.abs(nz) <= 1e-6, rhs >= 0, True), axis=2)
            ok = flat_ok & (up > lo)
            # The top face: the plane attaining the upper bound; walkable if its normal z >= 0.7.
            top_plane = np.where(nz > 1e-6, rhs / np.where(nz > 1e-6, nz, 1), np.inf).argmin(axis=2)
            walk = nz[top_plane] >= .7
            for (ii, jj) in zip(*np.nonzero(ok)):
                intervals[(i0 + ii, j0 + jj)].append((float(lo[ii, jj]), float(up[ii, jj]), bool(walk[ii, jj])))
        K = max((len(v) for v in intervals.values()), default=1)
        self.bot = np.full((nx, ny, K), np.inf, dtype=np.float32)
        self.top = np.full((nx, ny, K), -np.inf, dtype=np.float32)
        self.walk = np.zeros((nx, ny, K), dtype=bool)
        for (i, j), v in intervals.items():
            for k, (lo, up, w) in enumerate(v):
                self.bot[i, j, k], self.top[i, j, k], self.walk[i, j, k] = lo, up, w

    def pit_distance(self, x, y, z, radius=512):
        """XY distance from a standing origin to the nearest column over the pit
        at its feet height, capped at radius."""
        feet = z - 24
        i, j = int((x - self.x0) / CELL), int((y - self.y0) / CELL)
        r = radius // CELL
        i0, i1, j0, j1 = max(0, i - r), min(self.top.shape[0], i + r + 1), max(0, j - r), min(self.top.shape[1], j + r + 1)
        if i0 >= i1 or j0 >= j1:
            return radius
        top, bot, walk = self.top[i0:i1, j0:j1], self.bot[i0:i1, j0:j1], self.walk[i0:i1, j0:j1]
        floor = np.any(walk & (top > self.hurt_top) & (top <= feet + STEP), axis=2)
        wall = np.any((bot < feet + 64) & (top > feet + STEP), axis=2)
        pit = ~floor & ~wall
        if not pit.any():
            return radius
        ii, jj = np.nonzero(pit)
        cx = self.x0 + (i0 + ii + .5) * CELL
        cy = self.y0 + (j0 + jj + .5) * CELL
        return float(min(radius, np.min(np.hypot(cx - x, cy - y))))


# ---------------------------------------------------------------- binds

def cmd_binds(demos, say):
    say('== Team chat binds (sender = the chat command\'s client argument; overlay = the sender\'s last overlay entry) ==')
    field_hits = collections.defaultdict(collections.Counter)
    tmpl_n = collections.Counter()
    for dem in demos:
        tin = dem.d['tinfo']
        tt = [t for t, _ in tin]
        for t, kind, text, c in dem.d['chat']:
            body = pp.chat_body(text).split(':', 1)[-1].strip()
            nums = [int(x) for x in re.findall(r'-?\d+', body)]
            if kind != 'tchat' or len(nums) < 2 or c < 0:
                continue
            tmpl = re.sub(r'\d+', '#', body.lower())[:48]
            j = bisect.bisect_right(tt, t) - 1
            v = next((x for x in tin[j][1] if x[0] == c), None) if j >= 0 else None
            if not v:
                continue
            tmpl_n[tmpl] += 1
            h, f, a = v[2], v[3], v[4]
            for k, n in enumerate(nums[:4]):
                hit = [name for name, val in (('health', h), ('armor', a), ('force', f)) if abs(n - val) <= 3]
                field_hits[tmpl][(k, hit[0] if len(hit) == 1 else 'both' if hit else 'none')] += 1
    for tmpl, n in tmpl_n.most_common(8):
        per = collections.defaultdict(list)
        for (k, name), m in field_hits[tmpl].items():
            per[k].append((m, name))
        say('  %4d  %-40s %s' % (n, tmpl, '  |  '.join(
            '#%d: %s' % (k + 1, ', '.join('%s %s' % (name, pct(m, n)) for m, name in sorted(v, reverse=True)[:2]))
            for k, v in sorted(per.items()))))
    # What follows the commonest calls.
    say('\n  calls (bare words): what follows within 5 s')
    calls = collections.defaultdict(lambda: collections.Counter())
    for dem in demos:
        sks = collections.defaultdict(list)
        for t, victim, attacker, mod in dem.d['obits']:
            if dem.selfkill(t, victim, attacker, mod):
                sks[victim].append(t)
        rises = collections.defaultdict(list)
        tin = dem.d['tinfo']
        for (t0, v0), (t1, v1) in zip(tin, tin[1:]):
            a0 = {x[0]: x for x in v0}
            for x in v1:
                if x[0] in a0 and x[3] - a0[x[0]][3] >= 15 and a0[x[0]][2] > 0 and not (x[2] >= 100 and x[2] > a0[x[0]][2] + 20):
                    rises[x[0]].append(t1)
        for t, kind, text, c in dem.d['chat']:
            if kind != 'tchat' or c < 0:
                continue
            w = re.sub(r'[^a-z ]', '', pp.chat_body(text).split(':', 1)[-1].lower()).split()
            if not w:
                continue
            word = w[0]
            if word not in ('sk', 'te', 'cap', 'get', 'capture', 'ret', 'th', 'help', 'fc'):
                continue
            s = dem.at(t)
            calls[word]['n'] += 1
            # Control: the same test 30-35 s after the call.
            for tag, lo in (('', 0), (' (control)', 30000)):
                calls[word]['sender selfkills' + tag] += any(t + lo <= x <= t + lo + 5000 for x in sks[c])
                calls[word]['sender energized' + tag] += any(t + lo <= x <= t + lo + 5000 for x in rises[c])
                calls[word]['a teammate selfkills' + tag] += any(
                    t + lo <= x <= t + lo + 5000 for v, xs in sks.items() if v != c and dem.name_team(v, t)[1] == dem.team for x in xs)
            if s:
                fs = pp.flag_state(s[1], dem.team)
                if fs:
                    calls[word]['own flag taken'] += fs[0] != '0'
                    calls[word]['enemy flag held'] += fs[1] == '1'
            # Is the sender the carrier? Visible sender with the enemy flag bit.
            bit = 2 if dem.team == pp.TEAM_RED else 1
            if s:
                if c == dem.me:
                    calls[word]['sender carrying'] += bool(s[3][8] & bit)
                else:
                    o = next((o for o in s[4] if o[0] == c), None)
                    calls[word]['sender carrying'] += bool(o and o[5] & bit)
    base_sk = []
    for word, c in sorted(calls.items(), key=lambda kv: -kv[1]['n']):
        n = c['n']
        say('  %-8s n=%4d  sender selfkills %s (control %s)  another teammate selfkills %s (%s)  sender energized %s (%s)' % (
            word, n, pct(c['sender selfkills'], n), pct(c['sender selfkills (control)'], n),
            pct(c['a teammate selfkills'], n), pct(c['a teammate selfkills (control)'], n),
            pct(c['sender energized'], n), pct(c['sender energized (control)'], n)))
        say('  %-8s         sender carrying %s  own flag taken %s  enemy flag held %s' % (
            '', pct(c['sender carrying'], n), pct(c['own flag taken'], n), pct(c['enemy flag held'], n)))


# ---------------------------------------------------------------- chase

def cmd_chase(demos, say):
    say('\n== Own-flag thefts seen by a defender POV (thief visible; contact = within 400 u) ==')
    rows = []
    for dem in demos:
        if dem.d['pos'] not in ('Cha', 'Bas'):
            continue
        own_bit = 1 if dem.team == pp.TEAM_RED else 2
        enemy_stand = dem.stands[pp.TEAM_BLUE if dem.team == pp.TEAM_RED else pp.TEAM_RED]
        flags = dem.d['flags']
        for (t0, a), (t1, b) in zip(flags, flags[1:]):
            fa, fb = pp.flag_state(a, dem.team), pp.flag_state(b, dem.team)
            if not (fa and fb and fa[0] == '0' and fb[0] == '1'):
                continue
            end = next(((t, st) for t, st in flags if t > t1 and pp.flag_state(st, dem.team) and pp.flag_state(st, dem.team)[0] != '1'), None)
            if not end:
                continue
            te, st_end = end
            i = bisect.bisect_left(dem.times, t1)
            prev, contact, last_thief = None, None, None
            for s in dem.samples[i:]:
                if s[0] > te:
                    break
                if s[0] not in dem.live or not s[3][9]:
                    prev = None
                    continue
                thief = next((o for o in s[4] if o[5] & own_bit), None)
                if thief:
                    last_thief = (s[0], thief)
                if thief and prev and contact is None:
                    d = math.dist(thief[1:3], s[3][2:4])
                    if d < 400:
                        v = (thief[1] - prev[1], thief[2] - prev[2])
                        rel = (s[3][2] - thief[1], s[3][3] - thief[2])
                        ahead = v[0] * rel[0] + v[1] * rel[1] > 0 if math.hypot(*v) > 5 else None
                        between = math.dist(s[3][2:4], enemy_stand) < math.dist(thief[1:3], enemy_stand)
                        contact = dict(dt=s[0] - t1, ahead=ahead, between=between, weapon=s[3][14],
                                       region=dem.region(s[3][2], s[3][3]))
                prev = thief
            # How it ended: the own flag back home with the thief near the enemy stand is a capture.
            end_kind = 'dropped' if pp.flag_state(st_end, dem.team)[0] == '2' else None
            if end_kind is None:
                if last_thief and te - last_thief[0] < 1500 and math.dist(last_thief[1][1:3], enemy_stand) < 500:
                    end_kind = 'captured'
                else:
                    end_kind = 'returned' if last_thief and te - last_thief[0] < 1500 else 'unseen end'
            rows.append((dem.d['pos'], contact, end_kind, te - t1))
    for pos in ('Cha', 'Bas'):
        r = [x for x in rows if x[0] == pos]
        c = [x for x in r if x[1]]
        say('  %s: %d thefts; POV reached the thief in %d (%s), median %s ms after the steal' % (
            pos, len(r), len(c), pct(len(c), len(r)), pp.q([x[1]['dt'] for x in c], .5)))
        ends = collections.Counter(x[2] for x in r)
        say('    theft end: %s' % ', '.join('%s %d' % kv for kv in ends.most_common()))
        for label, f in (('from ahead', lambda x: x[1]['ahead'] is True), ('from behind', lambda x: x[1]['ahead'] is False),
                         ('at home (Fs/Bas)', lambda x: x[1]['region'] in ('Fs', 'Bas')),
                         ('away, from ahead', lambda x: x[1]['region'] not in ('Fs', 'Bas') and x[1]['ahead'] is True),
                         ('away, from behind', lambda x: x[1]['region'] not in ('Fs', 'Bas') and x[1]['ahead'] is False),
                         ('between thief and its home', lambda x: x[1]['between']),
                         ('holding saber', lambda x: x[1]['weapon'] == SABER), ('holding a gun', lambda x: x[1]['weapon'] > SABER)):
            sub = [x for x in c if f(x)]
            e = collections.Counter(x[2] for x in sub)
            say('    contact %-27s %4d: returned %s  captured %s  dropped %s' % (
                label, len(sub), pct(e['returned'], len(sub)), pct(e['captured'], len(sub)), pct(e['dropped'], len(sub))))
        none = [x for x in r if not x[1]]
        e = collections.Counter(x[2] for x in none)
        say('    no contact %31d: returned %s  captured %s  dropped %s' % (
            len(none), pct(e['returned'], len(none)), pct(e['captured'], len(none)), pct(e['dropped'], len(none))))
    # Weapons that kill carriers (all demos, all positions).
    say('\n== Kills of a visible flag carrier by an enemy: means of death ==')
    mods, by_pos = collections.Counter(), collections.defaultdict(collections.Counter)
    seen = set()
    for dem in demos:
        for t, victim, attacker, mod in dem.d['obits']:
            key = (dem.d['match_id'], t // 2000, victim)
            if key in seen or not (0 <= attacker < 32 and attacker != victim):
                continue
            ta, tv = dem.name_team(attacker, t)[1], dem.name_team(victim, t)[1]
            if ta is None or ta == tv:
                continue
            s = dem.at(t)
            if not s:
                continue
            carrying = bool(s[3][8]) if s[3][0] == victim else any(o[0] == victim and o[5] for o in s[4])
            if not carrying:
                continue
            seen.add(key)
            k = 'pit' if mod in PIT_MODS else 'splash/explosive' if mod in SPLASH_MODS else 'saber' if mod == 3 else 'direct gun/other'
            mods[k] += 1
            by_pos[dem.pos_of(attacker, t)][MOD_NAMES.get(mod, 'pit' if mod in PIT_MODS else str(mod))] += 1
    n = sum(mods.values())
    say('  %d carrier kills: %s' % (n, ', '.join('%s %s' % (k, pct(v, n)) for k, v in mods.most_common())))
    for pos in pp.POSITIONS:
        c = by_pos[pos]
        say('    by %s (%d): %s' % (pos, sum(c.values()), ', '.join('%s %d' % kv for kv in c.most_common(6))))


# ---------------------------------------------------------------- pits

def cmd_pits(demos, say):
    say('\n== Pit exposure (pit distance = XY distance to the nearest column with no floor above the hurt slab at the feet) ==')
    maps = {}
    for m in sorted({d.d['map'] for d in demos}):
        maps[m] = PitMap(m)
        say('  %s: hurt slab top %.0f, grid %s' % (m, maps[m].hurt_top, maps[m].top.shape[:2]))
    # Validation: where pit victims were last seen.
    val = collections.Counter()
    for dem in demos:
        pm = maps[dem.d['map']]
        for t, victim, attacker, mod in dem.d['obits']:
            if mod not in (38, 41) and not (mod == 39 and 0 <= attacker < 32 and attacker != victim):
                continue
            s = dem.at(t - 400)
            if not s:
                continue
            p = s[3][2:5] if s[3][0] == victim else next((o[1:4] for o in s[4] if o[0] == victim), None)
            if p is None:
                continue
            d = pm.pit_distance(*p)
            val['<=64' if d <= 64 else '<=192' if d <= 192 else '<=512' if d < 512 else 'none within 512'] += 1
    n = sum(val.values())
    say('  validation, falling or pitted victims 400 ms before death: %s' % ', '.join('%s %s' % (k, pct(val[k], n)) for k in ('<=64', '<=192', '<=512', 'none within 512')))
    # Offense POV exposure.
    exp = collections.defaultdict(list)
    hazard = collections.defaultdict(lambda: [0, 0])
    for dem in demos:
        if dem.d['pos'] != 'Off':
            continue
        pm = maps[dem.d['map']]
        pitted = [t for t, v, a, mod in dem.d['obits'] if v == dem.me and mod in PIT_MODS and 0 <= a < 32 and a != v
                  and dem.name_team(a, t)[1] not in (None, dem.team)]
        for k, s in enumerate(dem.samples):
            if k % 4 or s[0] not in dem.live or not s[3][9]:
                continue
            p = s[3]
            d = pm.pit_distance(p[2], p[3], p[4])
            near = any(dem.name_team(o[0], s[0])[1] not in (None, dem.team) and o[6] and math.dist(o[1:3], p[2:4]) < PUSH_RANGE
                       for o in s[4])
            carrying = bool(p[8])
            exp[(carrying, near)].append(d)
            band = 'pit<128' if d < 128 else 'pit128-384' if d < 384 else 'pit>=384'
            key = (band, near)
            hazard[key][0] += 1
            hazard[key][1] += any(s[0] < t <= s[0] + 3000 for t in pitted[bisect.bisect_left(pitted, s[0]):bisect.bisect_left(pitted, s[0]) + 2])
    say('  offense POV pit distance (sampled 1 Hz):')
    for carrying in (True, False):
        for near in (True, False):
            v = exp[(carrying, near)]
            say('    %-12s enemy within %d: %s  n=%5d  median %s  under 128 u %s  under 384 u %s' % (
                'carrying' if carrying else 'not carrying', PUSH_RANGE, 'yes' if near else 'no ', len(v), pp.q(v, .5),
                pct(sum(1 for x in v if x < 128), len(v)), pct(sum(1 for x in v if x < 384), len(v))))
    say('  offense POV pitted within 3 s, by pit distance and enemy near:')
    for band in ('pit<128', 'pit128-384', 'pit>=384'):
        for near in (True, False):
            n, k = hazard[(band, near)]
            say('    %-10s enemy near %-3s  samples %5d  pitted within 3 s %d (%.2f%%)' % (band, 'yes' if near else 'no', n, k, 100 * k / max(1, n)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', nargs='?', default='all', choices=('binds', 'chase', 'pits', 'all'))
    ap.add_argument('--out', type=Path, default=pp.DEFAULT_OUT)
    args = ap.parse_args()
    demos = [Demo(d) for d in pp.load(args.out)]
    lines = []

    def say(s=''):
        lines.append(s)
        print(s, flush=True)

    if args.what in ('binds', 'all'):
        cmd_binds(demos, say)
    if args.what in ('chase', 'all'):
        cmd_chase(demos, say)
    if args.what in ('pits', 'all'):
        cmd_pits(demos, say)
    (args.out / ('open-%s.txt' % args.what)).write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
