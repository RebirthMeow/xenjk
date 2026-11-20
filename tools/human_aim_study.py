"""Human aim study: how pug players aim each weapon, for a humanized bot aim model.

Record docs/movement/records/2026-09-28-human-aim-study.md. Offline only:
reads local demo extracts; never connects to a server. Uses the v4 extract of
tools/base_defender_study.py (fire events with view angles, hit counter,
per-snapshot samples while an enemy is within 1,500 u of the POV).

    python tools/human_aim_study.py extract [--jobs N]   # pilot Off/Cha demos to v4
    python tools/human_aim_study.py report

Corpus: base-defender-study extract-v4 (4v4 base POVs), the capped 2v2 set
(extract-v4-2v2 via selection2v2.csv), and the position pilot's offense and
chase demos re-extracted to v4 here.

Observed: the POV's view angles at each snapshot, its fire events (weapon,
mode), its hit counter, enemy positions per snapshot. Inferred: the target of
a fire (the enemy nearest the view ray), enemy velocity (finite differences
of snapshot positions), airborne (vertical speed), lead points (g_weapon.c
projectile speeds, gravity 800), line of sight (brush columns).
"""
import argparse
import bisect
from concurrent.futures import ProcessPoolExecutor, as_completed
import collections
import csv
import gzip
import json
import math
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp
import base_defender_study as bd

OUT = pp.ROOT / '.codex-research' / 'human-aim-study-20260928'
PILOT_DIR = 'extract-v4-pilot'
GRAVITY = 800.0
ATTRIB_DEG = 30          # a fire is attributed to the enemy nearest its view ray (center or lead) within this
REFIRE = {(4, 0): 800, (4, 1): 800, (16, 0): 400, (16, 1): 400, (5, 0): 350, (5, 1): 150, (6, 0): 600, (6, 1): 1300,
          (7, 0): 1000, (7, 1): 750, (8, 0): 100, (8, 1): 800, (9, 0): 500, (9, 1): 900, (10, 0): 700, (10, 1): 800,
          (11, 0): 900, (11, 1): 1200, (12, 0): 800, (12, 1): 400, (15, 0): 800, (15, 1): 1200}   # bg_weapons.c
# (weapon, alt) -> (label, speed u/s or None for hitscan, gravity arc, splash)
MODES = {(4, 0): ('pistol', 1600, False, False), (4, 1): ('pistol_charged', 1600, False, False),
         (16, 0): ('pistol_old', 1600, False, False), (16, 1): ('pistol_old_charged', 1600, False, False),
         (5, 0): ('blaster', 2300, False, False), (5, 1): ('blaster_alt', 2300, False, False),
         (6, 0): ('disruptor', None, False, False), (6, 1): ('disruptor_scoped', None, False, False),
         (7, 0): ('bowcaster', 1300, False, False), (7, 1): ('bowcaster_alt', 1300, False, False),
         (8, 0): ('repeater', 1600, False, False), (8, 1): ('repeater_alt', 1100, True, True),
         (10, 0): ('flechette', 3500, False, False), (10, 1): ('flechette_alt', 1050, True, True),
         (11, 0): ('rocket', 900, False, True), (15, 0): ('conc', 3000, False, True),
         (9, 0): ('demp2', 1800, False, False), (12, 1): ('thermal_alt', 600, True, True)}
BANDS = ((0, 300, '<300'), (300, 600, '300-600'), (600, 1000, '600-1000'), (1000, 1e9, '>1000'))
MIN_N = 40


def band(d):
    return next(lab for lo, hi, lab in BANDS if lo <= d < hi)


def q(v, p):
    return pp.q(v, p)


def qs(v, fmt='%.1f', ps=(.5, .75, .9)):
    if not v:
        return 'n=0'
    return 'n=%d ' % len(v) + ' '.join(fmt % q(v, p) for p in ps)


def pct(n, d):
    return '%.0f%%' % (100 * n / d) if d else '-'


def wrap(a):
    return (a + 180) % 360 - 180


def dir_angles(v):
    """Quake yaw and pitch (pitch positive = down) of a vector."""
    yaw = math.degrees(math.atan2(v[1], v[0]))
    pitch = -math.degrees(math.atan2(v[2], math.hypot(v[0], v[1])))
    return yaw, pitch


def ang_between(pitch, yaw, v):
    return bd.angle_to((0, 0, 0), bd.view_dir(pitch, yaw), v)[0]


def straight_lead(D, V, s):
    """Intercept time T for a projectile of speed s from the origin toward a target at D moving at V."""
    a = V[0] ** 2 + V[1] ** 2 + V[2] ** 2 - s * s
    b = 2 * (D[0] * V[0] + D[1] * V[1] + D[2] * V[2])
    c = D[0] ** 2 + D[1] ** 2 + D[2] ** 2
    if abs(a) < 1e-6:
        return -c / b if b < 0 else None
    disc = b * b - 4 * a * c
    if disc < 0:
        return None
    r = sorted(x for x in ((-b - math.sqrt(disc)) / (2 * a), (-b + math.sqrt(disc)) / (2 * a)) if x > 0)
    return r[0] if r else None


def arc_dir(D, V, s, T0):
    """Launch direction (low arc, gravity) to meet the target moving at V; iterate the flight time."""
    T = T0
    for _ in range(4):
        P = (D[0] + V[0] * T, D[1] + V[1] * T, D[2] + V[2] * T)
        dh, h = math.hypot(P[0], P[1]), P[2]
        g = GRAVITY
        disc = s ** 4 - g * (g * dh * dh + 2 * h * s * s)
        th = math.atan2(s * s - math.sqrt(disc), g * dh) if disc >= 0 else math.radians(45)
        T = dh / max(1e-6, s * math.cos(th))
    ux, uy = P[0] / max(1e-6, dh), P[1] / max(1e-6, dh)
    return (math.cos(th) * ux, math.cos(th) * uy, math.sin(th)), T


class Tracks:
    """Per-demo enemy tracks and POV view series from the v4 samples."""

    def __init__(self, dem):
        self.dem = dem
        self.tr = collections.defaultdict(lambda: ([], []))
        self.pov_t, self.pov = [], []
        for s in dem.samples:
            p = s[3]
            self.pov_t.append(s[0])
            self.pov.append((p[2], p[3], p[4], p[16], p[17], p[19], p[9], p[14]))
            for o in s[4]:
                ts, xs = self.tr[o[0]]
                ts.append(s[0])
                xs.append((o[1], o[2], o[3], o[6]))

    def pos(self, c, t, tol=60):
        ts, xs = self.tr.get(c, ([], []))
        k = bisect.bisect_right(ts, t) - 1
        if k < 0 or t - ts[k] > tol or not xs[k][3]:
            return None
        return xs[k][:3]

    def interp(self, c, t, tol=120):
        ts, xs = self.tr.get(c, ([], []))
        k = bisect.bisect_right(ts, t) - 1
        if k < 0 or k + 1 >= len(ts) or ts[k + 1] - ts[k] > tol or not xs[k][3]:
            return self.pos(c, t, 40)
        f = (t - ts[k]) / max(1, ts[k + 1] - ts[k])
        a, b = xs[k], xs[k + 1]
        return tuple(a[i] + (b[i] - a[i]) * f for i in range(3))

    def vel(self, c, t):
        ts, xs = self.tr.get(c, ([], []))
        k = bisect.bisect_right(ts, t) - 1
        if k < 1:
            return None
        j = bisect.bisect_right(ts, ts[k] - 80) - 1
        if j < 0 or ts[k] - ts[j] > 300 or not xs[j][3] or not xs[k][3]:
            return None
        dt = (ts[k] - ts[j]) / 1000
        return tuple((xs[k][i] - xs[j][i]) / dt for i in range(3))

    def pov_at(self, t, tol=60):
        k = bisect.bisect_right(self.pov_t, t) - 1
        if k < 0 or t - self.pov_t[k] > tol:
            return None
        return self.pov[k]


def fire_rows(dem, tk):
    """Every live fire of a studied mode, with its attributed target and aim errors."""
    rows = []
    hits = collections.Counter()
    ft = [f[0] for f in dem.d['fires']]
    for h in dem.d['hits']:
        if h[1] > 0:
            k = bisect.bisect_right(ft, h[0]) - 1
            if k >= 0 and h[0] - ft[k] <= 3000:
                hits[k] += h[1]
    for k, f in enumerate(dem.d['fires']):
        t, w, alt, x, y, z, pitch, yaw, vh = f
        mode = MODES.get((w, alt))
        s = dem.at(t)
        if not mode or not s or s[0] not in dem.live:
            continue
        label, spd, arc, splash = mode
        eye = (x, y, z + vh)
        best = None
        for o in dem.enemies(s):
            P = tk.pos(o[0], t, 300)
            if P is None:
                continue
            C = (P[0] - eye[0], P[1] - eye[1], P[2] + 8 - eye[2])
            V = tk.vel(o[0], t) or (0.0, 0.0, 0.0)
            dist = math.sqrt(C[0] ** 2 + C[1] ** 2 + C[2] ** 2)
            e_c = ang_between(pitch, yaw, C)
            # Ideal lead.
            if spd is None:
                L, T = C, 0.0
                e_l = e_c
            else:
                T = straight_lead(C, V, spd) or dist / spd
                if arc:
                    Ld, T = arc_dir(C, V, spd, T)
                    L = Ld
                else:
                    L = (C[0] + V[0] * T, C[1] + V[1] * T, C[2] + V[2] * T)
                e_l = ang_between(pitch, yaw, L)
            e_min = min(e_c, e_l)
            if e_min <= ATTRIB_DEG and (best is None or e_min < best[0]):
                best = (e_min, o, P, C, V, dist, e_c, e_l, T, L)
        row = dict(demo=dem, k=k, t=t, mode=label, w=w, alt=alt, hit=hits[k] > 0, nhits=hits[k], pitch=pitch, yaw=yaw)
        if best:
            _, o, P, C, V, dist, e_c, e_l, T, L = best
            row.update(target=o[0], dist=dist, e_center=e_c, e_lead=e_l, T=T, speed=math.hypot(V[0], V[1]),
                       airborne=abs(V[2]) > 100, carrier=bool(o[5]))
            if splash:
                F = (C[0], C[1], C[2] - 32)       # feet: origin - 24
                if spd:
                    Tf = straight_lead(F, V, spd) or dist / spd
                    if arc:
                        Lf, _ = arc_dir(F, V, spd, Tf)
                    else:
                        Lf = (F[0] + V[0] * Tf, F[1] + V[1] * Tf, F[2] + V[2] * Tf)
                    row['e_floor'] = ang_between(pitch, yaw, Lf)
            # Signed components against the center.
            cy, cp = dir_angles(C)
            ly, lp = dir_angles(L)
            row['yaw_err'] = wrap(yaw - cy)
            row['pitch_err'] = pitch - cp              # + = aimed below the center
            row['lead_yaw'] = wrap(ly - cy)             # ideal horizontal lead offset
            row['lead_pitch'] = lp - cp
            # Target's bearing rate as seen from the POV (deg/s), over the 100 ms before.
            P0 = tk.interp(o[0], t - 100)
            pv = tk.pov_at(t - 100, 80)
            if P0 and pv:
                b0 = math.degrees(math.atan2(P0[1] - pv[1], P0[0] - pv[0]))
                row['omega'] = wrap(cy - b0) / .1
            # Error against the center as it was tau ms earlier (tracking delay).
            for tau in (0, 50, 100, 150, 200, 300):
                Pt = tk.interp(o[0], t - tau)
                if Pt:
                    row['lag%d' % tau] = ang_between(pitch, yaw, (Pt[0] - eye[0], Pt[1] - eye[1], Pt[2] + 8 - eye[2]))
            # Previous snapshot's angles (the fire happened between the two snapshots).
            k0 = bisect.bisect_left(tk.pov_t, t) - 1
            if k0 >= 0 and t - tk.pov_t[k0] <= 60:
                pp0 = tk.pov[k0]
                row['e_center_prev'] = ang_between(pp0[3], pp0[4], C)
        rows.append(row)
    return rows


def sec_aim(rows, say, csvrows):
    say('\n== 1-2. Aim error (deg) and hit rate per fire, by weapon and mode ==')
    say('Attributed = an enemy within %d deg of the view ray (center or lead). Errors: median p75 p90.' % ATTRIB_DEG)
    modes = collections.Counter(r['mode'] for r in rows)
    kept = []
    for mode, n in modes.most_common():
        R = [r for r in rows if r['mode'] == mode]
        A = [r for r in R if 'dist' in r]
        if len(A) < MIN_N:
            say('  %-20s fires %5d attributed %4d: too few, dropped' % (mode, n, len(A)))
            continue
        kept.append(mode)
        say('\n  %s: fires %d, attributed %d (%s), unattributed %d; hit counter on %s of all fires, %s of attributed' % (
            mode, n, len(A), pct(len(A), n), n - len(A), pct(sum(r['hit'] for r in R), n), pct(sum(r['hit'] for r in A), len(A))))
        say('    range %s u; flight time to target %s ms' % (qs([r['dist'] for r in A], '%.0f', (.25, .5, .75)),
                                                          qs([1000 * r['T'] for r in A], '%.0f', (.25, .5, .75))))
        pts = [('center', 'e_center'), ('center, prev snapshot', 'e_center_prev'), ('lead point', 'e_lead')]
        if any('e_floor' in r for r in A):
            pts.append(('floor (lead)', 'e_floor'))
        for lab, key in pts:
            v = [r[key] for r in A if key in r]
            say('    %-22s %s' % (lab, qs(v)))
        say('    signed: yaw error vs center %s; pitch error vs center (+ below) %s; ideal lead yaw |%s| pitch %s' % (
            qs([r['yaw_err'] for r in A]), qs([r['pitch_err'] for r in A], ps=(.25, .5, .75)),
            qs([abs(r['lead_yaw']) for r in A]), qs([r['lead_pitch'] for r in A], ps=(.25, .5, .75))))
        say('    %-10s %6s %22s %22s %8s' % ('range', 'n', 'center med/p90', 'lead med/p90', 'hit'))
        for lo, hi, lab in BANDS:
            B = [r for r in A if lo <= r['dist'] < hi]
            if len(B) < 10:
                continue
            say('    %-10s %6d %10.1f / %-10.1f %10.1f / %-10.1f %8s' % (
                lab, len(B), q([r['e_center'] for r in B], .5), q([r['e_center'] for r in B], .9),
                q([r['e_lead'] for r in B], .5), q([r['e_lead'] for r in B], .9), pct(sum(r['hit'] for r in B), len(B))))
        for lab, f in (('grounded', lambda r: not r['airborne']), ('airborne', lambda r: r['airborne']),
                       ('speed<300', lambda r: r['speed'] < 300), ('speed300-600', lambda r: 300 <= r['speed'] < 600),
                       ('speed>=600', lambda r: r['speed'] >= 600)):
            B = [r for r in A if f(r)]
            if len(B) < 10:
                continue
            say('    %-12s %5d  center med %.1f p90 %.1f  lead med %.1f  hit %s' % (
                lab, len(B), q([r['e_center'] for r in B], .5), q([r['e_center'] for r in B], .9),
                q([r['e_lead'] for r in B], .5), pct(sum(r['hit'] for r in B), len(B))))
        for r in A:
            csvrows.append({k: (round(v, 2) if isinstance(v, float) else v) for k, v in r.items() if k != 'demo'})
    return kept


def sec_lag(rows, kept, say):
    say('\n== 6. Error model: tracking delay and angular speed ==')
    say('  median error to the target center as it was tau ms before the fire (lowest = the delay humans aim at):')
    say('  %-20s %s' % ('mode', '  '.join('%7s' % ('%dms' % t) for t in (0, 50, 100, 150, 200, 300))))
    for mode in kept:
        A = [r for r in rows if r['mode'] == mode and 'dist' in r]
        say('  %-20s %s' % (mode, '  '.join('%7.2f' % (q([r['lag%d' % t] for r in A if 'lag%d' % t in r], .5) or 0)
                                            for t in (0, 50, 100, 150, 200, 300))))
    say('\n  signed yaw error against the target\'s bearing rate omega (seen from the POV); slope = lead time (s),')
    say('  negative = lag; ideal slope for a projectile ~ flight time; |omega| bands: median |yaw error|')
    for mode in kept:
        A = [r for r in rows if r['mode'] == mode and 'omega' in r and abs(r['omega']) < 720]
        if len(A) < MIN_N:
            continue
        xs = [r['omega'] for r in A]
        ys = [r['yaw_err'] for r in A]
        mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
        sxx = sum((x - mx) ** 2 for x in xs)
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else 0
        moving = [r for r in A if abs(r['omega']) > 30]
        behind = sum(1 for r in moving if r['yaw_err'] * r['omega'] < 0)
        bands = []
        for lo, hi in ((0, 30), (30, 90), (90, 180), (180, 720)):
            B = [abs(r['yaw_err']) for r in A if lo <= abs(r['omega']) < hi]
            bands.append('%d-%d: %.1f (%d)' % (lo, hi, q(B, .5) or 0, len(B)))
        say('  %-20s slope %+.0f ms (ideal med %.0f ms); |omega|>30: behind the target %s of %d;  %s' % (
            mode, 1000 * slope, 1000 * (q([r['T'] for r in A], .5) or 0), pct(behind, len(moving)), len(moving), '  '.join(bands)))


def sec_turn(demos, tks, say, pitmaps):
    say('\n== 3. Turning and convergence ==')
    rates_track, rates_near, prates = [], [], []
    for dem in demos:
        tk = tks[id(dem)]
        pm = pitmaps.get(dem.map)
        pov_t, pov = tk.pov_t, tk.pov
        for i in range(1, len(pov_t)):
            dt = (pov_t[i] - pov_t[i - 1]) / 1000
            a, b = pov[i - 1], pov[i]
            if dt <= 0 or dt > .06 or not a[6] or not b[6] or pov_t[i] not in dem.live:
                continue
            yr = abs(wrap(b[4] - a[4])) / dt
            pr = abs(b[3] - a[3]) / dt
            s = dem.samples[i]
            eye = (b[0], b[1], b[2] + b[5])
            best = None
            for o in dem.enemies(s):
                v = (o[1] - eye[0], o[2] - eye[1], o[3] + 8 - eye[2])
                dd = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
                if dd < 1500:
                    e = ang_between(b[3], b[4], v)
                    best = e if best is None else min(best, e)
            if best is None:
                continue
            rates_near.append(yr)
            if best < 15 and b[7] > 3:
                rates_track.append(yr)
                prates.append(pr)
    say('  yaw speed, all snapshots with an enemy within 1,500 u (deg/s): med %.0f p75 %.0f p90 %.0f p99 %.0f (n=%d)' % (
        q(rates_near, .5), q(rates_near, .75), q(rates_near, .9), q(rates_near, .99), len(rates_near)))
    say('  yaw speed while tracking (gun held, view within 15 deg of an enemy): med %.0f p75 %.0f p90 %.0f p99 %.0f (n=%d)' % (
        q(rates_track, .5), q(rates_track, .75), q(rates_track, .9), q(rates_track, .99), len(rates_track)))
    say('  pitch speed while tracking: med %.0f p90 %.0f p99 %.0f' % (q(prates, .5), q(prates, .9), q(prates, .99)))


def sec_appear(demos, tks, fires_by_demo, say, pitmaps):
    """Enemy entries (within 1,500 u with line of sight after 2 s without) -> convergence and first fire."""
    say('\n== 3b/4. After an enemy appears (enters 1,500 u of the POV with line of sight after 2 s out; POV alive, gun held) ==')
    conv = collections.defaultdict(list)
    react, n_ent, fired = [], 0, 0
    for dem in demos:
        tk = tks[id(dem)]
        pm = pitmaps.get(dem.map)
        last_in = {}
        last_check = {}
        state = {}
        fires = sorted((r['t'], r.get('target')) for r in fires_by_demo.get(id(dem), []) if 'target' in r)
        for i, s in enumerate(dem.samples):
            t = s[0]
            if t not in dem.live:
                continue
            p = s[3]
            eye = (p[2], p[3], p[4] + p[19])
            for o in dem.enemies(s):
                c = o[0]
                if t - last_check.get(c, -10 ** 9) < 100:
                    continue
                last_check[c] = t
                dd = math.dist((o[1], o[2], o[3]), p[2:5])
                vis = dd < 1500 and (pm is None or pm.los(eye, (o[1], o[2], o[3] + 16)))
                if vis:
                    if t - last_in.get(c, -10 ** 9) > 2000 and p[9] and p[14] > 3:
                        n_ent += 1
                        # Convergence within 3 s.
                        a0 = ang_between(p[16], p[17], (o[1] - eye[0], o[2] - eye[1], o[3] + 8 - eye[2]))
                        t10 = t5 = None
                        for s2 in dem.samples[i:]:
                            if s2[0] > t + 3000:
                                break
                            q2 = s2[3]
                            P = tk.pos(c, s2[0], 60)
                            if P is None or not q2[9]:
                                continue
                            e = ang_between(q2[16], q2[17], (P[0] - q2[2], P[1] - q2[3], P[2] + 8 - q2[4] - q2[19]))
                            if t10 is None and e <= 10:
                                t10 = s2[0] - t
                            if e <= 5:
                                t5 = s2[0] - t
                                break
                        band_ = '<20' if a0 < 20 else '20-60' if a0 < 60 else '>=60'
                        k = bisect.bisect_left(fires, (t, -1))
                        ff = next((ft for ft, tg in fires[k:] if ft <= t + 5000 and tg == c), None)
                        conv[band_].append((t10, t5))
                        if ff is not None:
                            conv['engaged ' + band_].append((t10, t5))
                        if ff is not None:
                            fired += 1
                            react.append(ff - t)
                    last_in[c] = t
    say('  entries %d; POV fired at that enemy within 5 s after %d (%s)' % (n_ent, fired, pct(fired, n_ent)))
    say('  time to first fire at it: med %s p25 %s p75 %s p90 %s ms' % (q(react, .5), q(react, .25), q(react, .75), q(react, .9)))
    say('  convergence of the view on the entering enemy (angle to its center); "engaged" = the POV fired at it within 5 s:')
    for b in ('<20', '20-60', '>=60', 'engaged <20', 'engaged 20-60', 'engaged >=60'):
        v = conv[b]
        c10 = [x[0] for x in v if x[0] is not None]
        c5 = [x[1] for x in v if x[1] is not None]
        say('  initial angle %-6s n=%5d  within 10 deg in 3 s %s, time med %s p75 %s;  within 5 deg %s, time med %s p75 %s' % (
            b, len(v), pct(len(c10), len(v)), q(c10, .5), q(c10, .75), pct(len(c5), len(v)), q(c5, .5), q(c5, .75)))


def sec_steal(demos, fires_by_demo, say):
    say('\n== 4b. Steal to first fire at the thief (thief visible within 1 s, POV alive) ==')
    for lab, near in (('thief within 1,500 u of POV at the steal', True), ('farther', False)):
        v, n = [], 0
        for dem in demos:
            fr = sorted((r['t'], r.get('target')) for r in fires_by_demo.get(id(dem), []) if 'target' in r)
            for t1, thief, te, _ in bd.thefts(dem):
                p = dem.pov_pos(t1)
                o = dem.enemy_pos(thief, t1 + 100, 400)
                if not p or not p[9] or not o:
                    continue
                if (math.dist(p[2:5], o[1:4]) < 1500) != near:
                    continue
                n += 1
                k = bisect.bisect_left(fr, (t1, -1))
                ff = next((ft for ft, tg in fr[k:] if ft <= t1 + 10000 and tg == thief), None)
                if ff is not None:
                    v.append(ff - t1)
        say('  %-42s thefts %4d, fired at the thief within 10 s %s; time med %s p25 %s p75 %s ms' % (
            lab, n, pct(len(v), n), q(v, .5), q(v, .25), q(v, .75)))


def sec_cadence(rows, kept, say):
    say('\n== 5. Cadence: gaps between consecutive fires of the same mode (under 3 s), against the refire time ==')
    by = collections.defaultdict(list)
    eng = collections.defaultdict(list)
    for dem_id, R in rows.items():
        R = sorted(R, key=lambda r: r['t'])
        for a, b in zip(R, R[1:]):
            if a['mode'] == b['mode'] and 0 < b['t'] - a['t'] < 3000:
                by[a['mode']].append((b['t'] - a['t'], REFIRE.get((a['w'], a['alt']), 0)))
        # Engagements: attributed fires at one target with gaps under 2 s.
        cur = None
        for r in R:
            if 'target' not in r:
                continue
            if cur and cur['target'] == r['target'] and r['t'] - cur['last'] < 2000:
                cur['n'] += 1
                cur['last'] = r['t']
                cur['hits'] += r['hit']
            else:
                if cur:
                    eng[cur['mode']].append((cur['n'], cur['hits']))
                cur = dict(target=r['target'], last=r['t'], n=1, mode=r['mode'], hits=int(r['hit']))
        if cur:
            eng[cur['mode']].append((cur['n'], cur['hits']))
    for mode in kept:
        v = by[mode]
        if not v:
            continue
        rf = v[0][1]
        gaps = [g for g, _ in v]
        say('  %-20s refire %4d ms; gaps n=%5d med %4d p25 %4d p75 %4d; at max rate (<=1.15x) %s, paced (>1.5x) %s;'
            ' engagements (first mode) %d, shots med %s p90 %s' % (
                mode, rf, len(v), q(gaps, .5), q(gaps, .25), q(gaps, .75), pct(sum(1 for g in gaps if g <= 1.15 * rf), len(gaps)),
                pct(sum(1 for g in gaps if g > 1.5 * rf), len(gaps)), len(eng[mode]), q([e[0] for e in eng[mode]], .5),
                q([e[0] for e in eng[mode]], .9)))


def fast_los(pm):
    import numpy as np

    def los(a, b, step=16):
        n = max(2, int(math.dist(a, b) / step))
        f = np.arange(1, n) / n
        x = a[0] + (b[0] - a[0]) * f
        y = a[1] + (b[1] - a[1]) * f
        z = a[2] + (b[2] - a[2]) * f
        i = ((x - pm.x0) / 32).astype(int)
        j = ((y - pm.y0) / 32).astype(int)
        ok = (i >= 0) & (i < pm.top.shape[0]) & (j >= 0) & (j < pm.top.shape[1])
        i, j, z = i[ok], j[ok], z[ok]
        return not np.any((pm.bot[i, j] < z[:, None]) & (pm.top[i, j] > z[:, None]))
    pm.los = los
    return pm


def cmd_report(args):
    bd.load_stands()
    sets = [('4v4 base', bd.load_demos(bd.DEFAULT_OUT / bd.EXTRACT_DIR)),
            ('2v2 (capped)', bd.load_demos(bd.DEFAULT_OUT / (bd.EXTRACT_DIR + '-2v2'),
                                           keep=bd.selected_names(bd.DEFAULT_OUT, 'selection2v2.csv'))),
            ('pilot Off/Cha', bd.load_demos(args.out / PILOT_DIR))]
    lines = []

    def say(s=''):
        lines.append(s)
        print(s, flush=True)

    import position_open as po
    pitmaps = {m: fast_los(po.PitMap(m)) for m in ('kejim', 'dantooine_v3')}
    demos = []
    for lab, ds in sets:
        for d in ds:
            d.source = lab
        demos += ds
        say('%s: %d demos, %s' % (lab, len(ds), dict(collections.Counter(d.map for d in ds))))
    tks = {id(d): Tracks(d) for d in demos}
    rows, by_demo = [], {}
    for d in demos:
        r = fire_rows(d, tks[id(d)])
        for x in r:
            x['source'] = d.source
            x['map'] = d.map
        by_demo[id(d)] = r
        rows += r
    say('fires of studied modes: %d' % len(rows))
    raw = collections.Counter()
    for d in demos:
        for f in d.d['fires']:
            s_ = d.at(f[0])
            if s_ and s_[0] in d.live:
                raw['%s%s' % (bd.wname(f[1]), '_alt' if f[2] else '')] += 1
    say('all live fire events by weapon and mode: %s' % ', '.join('%s %d' % kv for kv in raw.most_common()))
    csvrows = []
    kept = sec_aim(rows, say, csvrows)
    say('\n  by corpus (center error med / hit rate of attributed fires):')
    for mode in kept:
        parts = []
        for lab, _ in sets:
            A = [r for r in rows if r['mode'] == mode and r['source'] == lab and 'dist' in r]
            if A:
                parts.append('%s %.1f / %s (%d)' % (lab, q([r['e_center'] for r in A], .5), pct(sum(r['hit'] for r in A), len(A)), len(A)))
        say('    %-20s %s' % (mode, ';  '.join(parts)))
    sec_lag(rows, kept, say)
    sec_turn(demos, tks, say, pitmaps)
    sec_appear(demos, tks, by_demo, say, pitmaps)
    sec_steal([d for d in demos if d.source != 'pilot Off/Cha' or d.d['pos'] == 'Cha'], by_demo, say)
    sec_cadence(by_demo, kept, say)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    keys = []
    for r in csvrows:
        for k in r:
            if k not in keys:
                keys.append(k)
    with (args.out / 'fires.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(csvrows)


# ---------------------------------------------------------------- extract

def cmd_extract(args):
    rows = [r for r in csv.DictReader((pp.DEFAULT_OUT / 'selection.csv').open(encoding='utf-8')) if r['pos'] in ('Off', 'Cha')]
    dest = args.out / PILOT_DIR
    dest.mkdir(parents=True, exist_ok=True)
    todo = [r for r in rows if not bd.extract_path(dest, r).exists()]
    print('extracting %d of %d' % (len(todo), len(rows)), flush=True)
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(bd.extract_demo, r): r for r in todo}
        for i, f in enumerate(as_completed(futs), 1):
            r = futs[f]
            d = f.result()
            with gzip.open(bd.extract_path(dest, r), 'wt', encoding='utf-8') as g:
                json.dump(d, g)
            print('%d/%d %s %s fires=%d %s' % (i, len(todo), d['map'], d['pos'], len(d['fires']), d['error']), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('extract', 'report'))
    ap.add_argument('--out', type=Path, default=OUT)
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 6))
    args = ap.parse_args()
    {'extract': cmd_extract, 'report': cmd_report}[args.command](args)


if __name__ == '__main__':
    main()
