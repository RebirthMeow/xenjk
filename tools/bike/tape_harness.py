"""Input-tape harness for the bike integrator.

Replays one ~10 s scripted input tape split into 3, 8, 16 and 33 ms commands
and a mixed 1-41 ms sequence (plus stale 100 ms and over-cap server steps) and
reports the largest position / angle / speed divergence between splits, on
flat ground and on a curb and step field.

  python tools/bike/tape_harness.py                 # twin only
  python tools/bike/tape_harness.py --c-exe a.exe   # also compare a C build of
                                                    # bg_bike.c (bike_test_main.c)
  python tools/bike/tape_harness.py --pmove-exe b.exe
                                                    # also run the real pmove
                                                    # adapter (bike_pmove_test.c)
  python tools/bike/tape_harness.py --bike ct3x.bike ...
                                                    # a handling file (twin, C
                                                    # and pmove builds alike);
                                                    # default the generic bike

Gates (exit status 1 when one fails): the aligned tape on the 4 ms grid must
give the same state at every substep for every split, mixed included; a C
build must print every checkpoint of every split and match the twin; the pmove
test must pass (slopes, a wall, mixed rates, pmove_float 0 and 1). Missing
samples and NaNs fail. The unaligned, naive and stale sections are reports.

Tapes:
  aligned    input changes every 528 ms (lcm of 3, 8, 16, 33), so every split
             sees the same input on every substep: isolates the integrator.
  unaligned  input changes at arbitrary ms; each command samples the tape at
             its start, so coarse splits apply changes up to 32 ms late. That
             divergence is input latency, not integration.
"""

import argparse
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bike_twin as bt  # noqa: E402

SEG_MS = 528
# (throttle, steer, lean, handbrake) per segment; lean +1 forward, -1 back
TAPE_VALUES = [
    (1.0, 0.0, 0.0, 0),    # launch
    (1.0, 0.0, 0.0, 0),
    (1.0, 0.0, -1.0, 0),   # lean back: wheelie
    (1.0, 0.0, -1.0, 0),
    (1.0, 0.0, 0.0, 0),    # nose down
    (1.0, 0.6, 0.0, 0),    # right turn
    (1.0, 0.6, 0.5, 0),
    (1.0, -1.0, 1.0, 0),   # hard left, tucked
    (1.0, -1.0, 1.0, 0),
    (0.0, 0.0, 0.0, 0),    # coast
    (-1.0, 0.0, 0.0, 0),   # brake
    (-1.0, 0.3, 0.0, 1),   # brake + handbrake slide
    (1.0, 0.0, 0.0, 0),
    (1.0, 0.0, -1.0, 0),   # wheelie attempt at speed
    (1.0, 1.0, 0.0, 0),
    (0.5, -0.5, 1.0, 0),
    (1.0, 0.0, 1.0, 0),
    (-1.0, 0.0, 0.0, 0),
    (-1.0, 0.0, 0.0, 0),   # stop, then reverse
]
TAPE_END = SEG_MS * len(TAPE_VALUES)  # 10,032 ms
ALIGNED_STARTS = [i * SEG_MS for i in range(len(TAPE_VALUES))]
UNALIGNED_STARTS = [0, 450, 1010, 1620, 2075, 2600, 3333, 3900, 4410, 5000,
                    5555, 6100, 6650, 7200, 7777, 8300, 8800, 9300, 9700]
SPLITS = [3, 8, 16, 33, 'mixed']
MIXED_SEED = 12345
GRID_TOL = 1e-9                     # the aligned grid is exact
C_TOL = {'pos': 0.05, 'yaw': 0.01, 'lean': 0.01, 'pitch': 0.01, 'speed': 0.05}  # float build vs the double twin


def tape_at(starts, t):
    idx = 0
    for i, st in enumerate(starts):
        if t >= st:
            idx = i
    th, st, ln, hb = TAPE_VALUES[idx]
    return bt.Input(th, st, ln, hb)


def ground_flat(x, y):
    return 0.0


def ground_curbs(x, y):
    """6 u checkerboard curbs on a 500 u grid plus 12 u terraces every third 1300 u
    band along x: rises of 6, 12 and 18 u (18 = stepHeight) and drops that launch."""
    h = 6.0 if (math.floor(x / 500.0) + math.floor(y / 500.0)) % 2 else 0.0
    if math.floor(x / 1300.0) % 3 == 2:
        h += 12.0
    return h


GROUNDS = {'flat': ground_flat, 'curbs': ground_curbs}


def command_times(split, end, stale=None):
    """Command end times. stale=(t0, length): one server step of that length at t0.
    split 'mixed': 1-41 ms commands from bike_test_main.c's generator, cut at every SEG_MS checkpoint."""
    times = []
    t = 0
    seed = MIXED_SEED
    while t < end:
        if stale and t == stale[0]:
            t += stale[1]
        elif split == 'mixed':
            seed = (seed * 1103515245 + 12345) & 0xffffffff
            t = min(t + 1 + ((seed >> 16) & 0x7fff) % 41, (t // SEG_MS + 1) * SEG_MS, end)
        else:
            t = min(t + split, end)
        times.append(t)
    return times


def run(ground, starts, split, mode='grid', stale=None):
    p = bt.default_params()
    world = bt.World(GROUNDS[ground])
    s = bt.init_state(world, 0.0, 0.0, 0.0, 0)
    trace = {}
    checkpoints = {}
    ev = {'airborne': 0, 'landed': 0, 'blocked': 0, 'bail': 0, 'loopout': 0, 'capped': 0}
    peak = {'speed': 0.0, 'pitch': 0.0, 'lean': 0.0, 'maxz': 0.0}

    def on_sub(g, st):
        trace[g] = bt.snapshot(st)
        peak['speed'] = max(peak['speed'], abs(st.speed))
        peak['pitch'] = max(peak['pitch'], st.pitch)
        peak['lean'] = max(peak['lean'], abs(st.lean))
        peak['maxz'] = max(peak['maxz'], st.origin[2])

    last_inp = None
    for t1 in command_times(split, TAPE_END, stale):
        t0 = s.commandTime
        if stale and t0 == stale[0]:
            inp = last_inp  # server reuses the pilot's last command
        else:
            inp = tape_at(starts, t0)
        if mode == 'grid':
            bt.run_command(s, p, inp, world, t1, on_sub)
        else:
            bt.run_command_naive(s, p, inp, world, t1)
        last_inp = inp
        e = s.events
        ev['airborne'] += bool(e & bt.EV_AIRBORNE)
        ev['landed'] += bool(e & bt.EV_LANDED)
        ev['blocked'] += bool(e & bt.EV_BLOCKED)
        ev['bail'] += bool(e & bt.EV_BAIL)
        ev['loopout'] += bool(e & bt.EV_LOOPOUT)
        ev['capped'] += bool(e & bt.EV_CAPPED)
        if t1 % SEG_MS == 0:
            checkpoints[t1] = bt.snapshot(s)
    return {'trace': trace, 'checkpoints': checkpoints, 'events': ev, 'peak': peak,
            'final': bt.snapshot(s), 'dropped': s.droppedMs}


def angle_diff(a, b):
    d = (a - b) % 360.0
    return d - 360.0 if d > 180.0 else d


def _worse(cur, v):
    """max() that keeps a NaN (max(0.0, nan) is 0.0)."""
    return v if (v != v or v > cur) else cur


def divergence(a, b):
    """a, b: dict key -> snapshot. Max over the common keys; 'missing' counts keys only one side has."""
    keys = sorted(set(a) & set(b))
    m = {'pos': 0.0, 'yaw': 0.0, 'lean': 0.0, 'pitch': 0.0, 'speed': 0.0, 'n': len(keys),
         'missing': len(set(a) ^ set(b))}
    for k in keys:
        x, y = a[k], b[k]
        m['pos'] = _worse(m['pos'], math.dist(x[0:3], y[0:3]))
        m['yaw'] = _worse(m['yaw'], abs(angle_diff(x[3], y[3])))
        m['lean'] = _worse(m['lean'], abs(x[4] - y[4]))
        m['pitch'] = _worse(m['pitch'], abs(x[5] - y[5]))
        m['speed'] = _worse(m['speed'], abs(x[6] - y[6]))
    return m


def within(m, tol, n):
    """Every metric at or under tol (a number, or a dict per metric), n samples, nothing missing; NaN fails."""
    for k in ('pos', 'yaw', 'lean', 'pitch', 'speed'):
        t = tol[k] if isinstance(tol, dict) else tol
        if not m[k] <= t:
            return False
    return m['n'] == n and m['missing'] == 0


def fmt(m):
    return 'pos %10.4g u  yaw %9.4g deg  lean %9.4g deg  pitch %9.4g deg  speed %9.4g u/s  (%d samples%s)' % (
        m['pos'], m['yaw'], m['lean'], m['pitch'], m['speed'], m['n'],
        ', %d missing' % m['missing'] if m['missing'] else '')


def split_name(sp):
    return '%5s' % sp if sp == 'mixed' else '%2d ms' % sp


def section_splits():
    """Returns the number of failed gates (aligned tape on the grid)."""
    print('== Split divergence (reference: 8 ms commands) ==')
    fails = 0
    for ground in GROUNDS:
        for tape_name, starts in (('aligned', ALIGNED_STARTS), ('unaligned', UNALIGNED_STARTS)):
            for mode in ('grid', 'naive'):
                runs = {sp: run(ground, starts, sp, mode) for sp in SPLITS}
                ref = runs[8]
                key = 'trace' if mode == 'grid' else 'checkpoints'
                print('-- ground=%s tape=%s mode=%s (%s)' % (
                    ground, tape_name, mode, 'every 4 ms substep' if mode == 'grid' else 'every 528 ms checkpoint'))
                gated = mode == 'grid' and tape_name == 'aligned'
                for sp in SPLITS:
                    if sp == 8:
                        continue
                    m = divergence(runs[sp][key], ref[key])
                    verdict = ''
                    if gated:
                        ok = within(m, GRID_TOL, TAPE_END // bt.SUBSTEP_MS)
                        fails += not ok
                        verdict = '  ok' if ok else '  FAIL'
                    print('   %s vs 8 ms: %s%s' % (split_name(sp), fmt(m), verdict))
                if mode == 'grid':
                    e, pk, f = ref['events'], ref['peak'], ref['final']
                    print('   8 ms run: final (%.0f, %.0f, %.1f) speed %.1f u/s; peak speed %.0f u/s, '
                          'pitch %.1f deg, |lean| %.1f deg; commands with airborne %d, landed %d, '
                          'blocked %d, bail %d, loopout %d' % (
                              f[0], f[1], f[2], f[6], pk['speed'], pk['pitch'], pk['lean'],
                              e['airborne'], e['landed'], e['blocked'], e['bail'], e['loopout']))
    return fails


def section_stale():
    print('== Stale server steps (16 ms commands, reference: plain 16 ms) ==')
    cases = [
        ('100 ms stale step inside one tape segment', (4288, 100)),
        ('100 ms stale step across a tape change (held input)', (4704, 100)),
        ('400 ms stale step over the 250 ms cap', (4288, 400)),
    ]
    for ground in GROUNDS:
        ref = run(ground, ALIGNED_STARTS, 16, 'grid')
        for name, stale in cases:
            r = run(ground, ALIGNED_STARTS, 16, 'grid', stale)
            print('-- ground=%s %s: %s; dropped %d ms' % (ground, name, fmt(divergence(r['trace'], ref['trace'])),
                                                           r['dropped']))
            pre = {k: v for k, v in r['trace'].items() if k * 4 <= stale[0] + stale[1]}
            print('   up to the end of the stale step only: %s' % fmt(divergence(pre, ref['trace'])))
        # naive control with the same 100 ms step
        refn = run(ground, ALIGNED_STARTS, 16, 'naive')
        rn = run(ground, ALIGNED_STARTS, 16, 'naive', (4288, 100))
        print('-- ground=%s naive control, 100 ms stale step: %s' % (
            ground, fmt(divergence(rn['checkpoints'], refn['checkpoints']))))


def section_feel():
    """Measured feel of the CT-3X data (v2): the lines a tuning pass compares against CP2077."""
    print('== Feel with the %s (flat asphalt, 4 ms commands) ==' % (bt.BIKE_FILE or 'generic bike'))
    p = bt.default_params()
    world = bt.World(ground_flat)
    u = bt.UNITS

    def drive(inp_fn, dur_ms, s=None, stop=None):
        if s is None:
            s = bt.init_state(world, 0.0, 0.0, 0.0, 0)
        out = []
        t = s.commandTime
        end = t + dur_ms
        while t < end:
            t += 4
            bt.run_command(s, p, inp_fn(t), world, t)
            out.append((t, bt.snapshot(s)))
            if stop and stop(s):
                break
        return s, out

    s, out = drive(lambda t: bt.Input(1.0, 0.0, 0.0, 0), 60000)
    t100 = next((t for t, sn in out if sn[6] >= 100.0 / 3.6 * u), None)
    print('   0-100 km/h: %s s; top speed after 60 s: %.0f u/s = %.1f m/s = %.0f km/h' % (
        '%.2f' % (t100 / 1000.0) if t100 else 'n/a', s.speed, s.speed / u, s.speed / u * 3.6))
    s2, _ = drive(lambda t: bt.Input(1.0, 0.0, 1.0, 0), 60000)
    print('   tucked top speed: %.1f m/s (+%.1f %%)' % (s2.speed / u, (s2.speed / s.speed - 1) * 100))
    s3, _ = drive(lambda t: bt.Input(1.0, 0.0, 0.0, 0), 60000, stop=lambda st: st.speed >= 100.0 / 3.6 * u)
    x0 = s3.origin[0]
    s3, out3 = drive(lambda t: bt.Input(-1.0, 0.0, 0.0, 0), 20000, s3, stop=lambda st: st.speed <= 0)
    print('   braking 100-0 km/h: %.1f m in %.2f s' % ((s3.origin[0] - x0) / u, len(out3) * 0.004))
    gears = []
    st = bt.init_state(world, 0.0, 0.0, 0.0, 0)
    for t in range(4, 20004, 4):
        bt.run_command(st, p, bt.Input(1.0, 0.0, 0.0, 0), world, t)
        if st.events & bt.EV_SHIFT:
            gears.append('%d@%.1fs/%.0fm/s' % (st.gear, t / 1000.0, st.speed / u))
    print('   upshifts: %s' % ', '.join(gears))
    s5, out5 = drive(lambda t: bt.Input(1.0, 0.0, 0.0, 0), 3000)
    print('   throttle only from standstill: peak pitch %.2f deg' % max(o[1][5] for o in out5))
    s5b, out5b = drive(lambda t: bt.Input(1.0, 0.0, -1.0, 0), 3000)
    print('   throttle + lean back from standstill: peak pitch %.2f deg' % max(o[1][5] for o in out5b))
    # burnout launch: throttle with the handbrake for 1.5 s, then release with lean back
    s4, out4 = drive(lambda t: bt.Input(1.0, 0.0, -1.0, 1 if t <= 1500 else 0), 4500)
    pk = max(out4, key=lambda o: o[1][5])
    first = next((t for t, sn in out4 if sn[5] > 1.0), None)
    print('   burnout launch (1.5 s throttle + handbrake, release with lean back): pitch > 1 deg at %s ms, '
          'peak %.1f deg at %d ms, speed then %.1f m/s' % (first, pk[1][5], pk[0], pk[1][6] / u))
    t_off = s4.commandTime
    s6, out6 = drive(lambda t: bt.Input(0.0, 0.0, 0.0, 0), 3000, s4)
    rec = next((t - t_off for t, sn in out6 if sn[5] <= 0.0), None)
    print('   wheelie recovery (throttle off, lean neutral): front down after %s ms' % rec)
    # steering response at 25 m/s
    s7, _ = drive(lambda t: bt.Input(1.0, 0.0, 0.0, 0), 60000, stop=lambda st: st.speed >= 25.0 * u)
    yaw0 = s7.yaw
    s7, out7 = drive(lambda t: bt.Input(0.0, 1.0, 0.0, 0), 3000, s7)
    t_lean = next((t - out7[0][0] + 4 for t, sn in out7 if sn[4] >= 0.9 * p['maxTilt']), None)
    # window 900-1000 ms after the steer input (throttle released, so speed bleeds off)
    yaw_rate = -(angle_diff(out7[249][1][3], out7[224][1][3])) / 0.1
    v1 = out7[249][1][6] / u
    print('   full steer from 25 m/s, throttle off: 90 %% of max lean after %s ms; at 1 s: %.1f m/s, '
          'yaw rate %.1f deg/s, turn radius %.0f m; heading change in 3 s %.0f deg' % (
              t_lean, v1, yaw_rate, v1 / (yaw_rate * bt.D2R) if yaw_rate else float('inf'),
              -angle_diff(s7.yaw, yaw0)))


def section_mechanisms():
    """v2 mechanisms the tapes do not isolate: surfaces, the handbrake slide, air control."""
    print('== Mechanisms (4 ms commands) ==')
    u = bt.UNITS

    def drive(surf, fn, dur, s=None, ground=0.0):
        p = bt.default_params()
        w = bt.World(lambda x, y: ground, surface=lambda x, y: surf)
        s = s or bt.init_state(w, 0.0, 0.0, 0.0, 0, p)
        t0, out = s.commandTime, []
        for t in range(t0 + 4, t0 + dur + 4, 4):
            bt.run_command(s, p, fn(t - t0), w, t)
            cy, sy = math.cos(s.yaw * bt.D2R), math.sin(s.yaw * bt.D2R)
            fwd = s.velocity[0] * cy + s.velocity[1] * sy
            lat = s.velocity[0] * sy - s.velocity[1] * cy
            out.append((t - t0, s.speed / u, math.degrees(math.atan2(abs(lat), max(abs(fwd), 1e-9)))))
        return s, out

    for surf, name in ((bt.SURF_ASPHALT, 'asphalt'), (bt.SURF_GROUND, 'ground'), (bt.SURF_SAND, 'sand')):
        s, _ = drive(surf, lambda t: bt.Input(1.0, 0.0, 0.0, 0), 2600)
        v0 = s.speed / u
        s, o = drive(surf, lambda t: bt.Input(-1.0, 0.0, 0.0, 0), 4000, s)
        stop = next((t for t, v, _ in o if v <= 0.01), None)
        if stop is None:
            print('   %-7s braking from %.1f m/s: not stopped in 4 s' % (name, v0))
            continue
        print('   %-7s braking from %.1f m/s: stopped in %.2f s (%.1f m/s^2)' % (
            name, v0, stop / 1000.0, v0 / (stop / 1000.0)))
    for hb in (0, 1):
        s, _ = drive(bt.SURF_ASPHALT, lambda t: bt.Input(1.0, 0.0, 0.0, 0), 1700)
        v, y0 = s.speed / u, s.yaw
        s, o = drive(bt.SURF_ASPHALT, lambda t: bt.Input(0.0, 1.0, 0.0, hb), 1000, s)
        print('   from %.1f m/s, 1 s full right%s: heading %.0f deg, peak slip angle %.0f deg, then %.1f m/s' % (
            v, ' + handbrake' if hb else '', -angle_diff(s.yaw, y0), max(r[2] for r in o), s.speed / u))
    p = bt.default_params()
    w = bt.World(lambda x, y: -1.0e6)
    s = bt.init_state(w, 0.0, 0.0, 0.0, 0, p)
    s.onGround, s.velocity = 0, [15.0 * u, 0.0, 5.0 * u]
    for t in range(4, 1004, 4):
        bt.run_command(s, p, bt.Input(0.0, 0.0, -1.0, 0), w, t)
    print('   air, 1 s full lean back: pitch %.0f deg, rate %.0f deg/s (vehAirControl pitch maxVelocity %g, read '
          'as rad/s)' % (s.pitch, s.pitchRate, p['airPitchMaxVel']))


def section_c(exe):
    """Returns the number of failures: every (ground, split) and checkpoint must be printed and match the twin."""
    print('== C build vs Python twin (%s) ==' % exe)
    out = subprocess.run([exe] + ([bt.BIKE_FILE] if bt.BIKE_FILE else []), capture_output=True, text=True,
                         check=True).stdout
    rows = {}
    bad_lines = 0
    for line in out.splitlines():
        f = line.split()
        try:
            if len(f) != 10:
                raise ValueError(line)
            ground, split, t = f[0], int(f[1]), int(f[2])
            split = 'mixed' if split == 0 else split
            vals = tuple(float(v) for v in f[3:10])
        except ValueError:
            bad_lines += 1
            continue
        rows.setdefault((ground, split), {})[t] = vals
    fails = 0
    if bad_lines:
        print('   %d unparsed output lines FAIL' % bad_lines)
        fails += 1
    for ground in GROUNDS:
        for split in SPLITS:
            cps = rows.pop((ground, split), {})
            py = run(ground, ALIGNED_STARTS, split, 'grid')['checkpoints']
            m = divergence(cps, py)
            ok = within(m, C_TOL, len(TAPE_VALUES))
            fails += not ok
            print('   %s %s: %s  %s' % (ground, split_name(split), fmt(m), 'ok' if ok else 'FAIL'))
    if rows:
        print('   unexpected cases in the output: %s FAIL' % sorted(rows))
        fails += 1
    return fails


def section_pmove(exe):
    """The real pmove adapter (bike_pmove_test.c): its own gate; returns 1 when it fails."""
    print('== Pmove adapter (%s) ==' % exe)
    r = subprocess.run([exe] + (['-b', bt.BIKE_FILE] if bt.BIKE_FILE else []), capture_output=True, text=True)
    lines = r.stdout.splitlines()
    for line in lines:
        if not line.endswith(' ok'):
            print('   ' + line)
    print('   (%d comparisons ok)' % sum(1 for line in lines if line.endswith(' ok')))
    ok = r.returncode == 0 and lines and lines[-1].startswith('PASS')
    if not ok:
        print('   exit status %d FAIL%s' % (r.returncode, (': ' + r.stderr.strip()) if r.stderr.strip() else ''))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--c-exe', action='append', default=[], help='bike_test_main build(s) to compare')
    ap.add_argument('--pmove-exe', action='append', default=[], help='bike_pmove_test build(s) to run')
    ap.add_argument('--skip-feel', action='store_true')
    ap.add_argument('--bike', help='a handling file (ext_data/vehicles/<name>.bike); default the generic bike')
    a = ap.parse_args()
    bt.BIKE_FILE = a.bike
    bt.default_params()  # fails here on a file with lines it does not understand
    fails = section_splits()
    section_stale()
    if not a.skip_feel:
        section_feel()
        section_mechanisms()
    for exe in a.c_exe:
        fails += section_c(exe)
    for exe in a.pmove_exe:
        fails += section_pmove(exe)
    print('%s: %d failed gates' % ('FAIL' if fails else 'PASS', fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
