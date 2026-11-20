"""Selfkills and team energize in the position pilot's extracts.

Record docs/movement/records/2026-09-27-position-pilot.md ("Self-kills and
team energize"). Reads tools/position_pilot.py extracts (extract-v3: the
POV's armor, weapons, ammo and spawn count); offline only.

    python tools/position_resources.py [--out DIR]

Selfkills. A POV death is a deliberate selfkill when its obituary is
MOD_SUICIDE (/kill) not credited to an enemy; one credited to an enemy is
a pit kill by base_enhanced's rule (tools/position_pilot.py) and is left
out. The POV's state before it (resources, weapons, place, flag) is set
against the same position's state at every 20th live sample, and its next
life is followed for the weapons it gains. Teammates' selfkills add the
resources their team overlay shows.

Team energize. A teammate's overlay Force rising by ENERGIZE_MIN or more
between two consecutive overlay messages under 600 ms apart, without a
respawn, is a received energize when a teammate's Force fell at the same
message or the next (that teammate is the giver); a rise no teammate paid
for is Force gained by absorbing an enemy power and is counted apart. The
receiver is followed for 5 s.
"""
import argparse
import bisect
import collections
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import position_pilot as pp

MOD_SUICIDE = 39
ENERGIZE_MIN = 15
GIVER_FALL = 10
NON_GUNS = {0, 1, 2, 3}          # none, stun baton, melee, saber
FORCE_AMMO = 1                   # AMMO_FORCE is not ammo for a gun
FP_PROTECT, FP_ABSORB, FP_SPEED = 9, 10, 2


def guns(mask):
    return sum(1 for b in range(19) if mask & (1 << b) and b not in NON_GUNS)


def ammo(v):
    return sum(x for i, x in enumerate(v) if i > FORCE_AMMO)


def pct(n, d):
    return '%3.0f%%' % (100 * n / d) if d else '   -'


class Demo:
    def __init__(self, d):
        self.d = d
        self.samples = [s for s in d['samples'] if len(s[3]) > 15]
        self.times = [s[0] for s in self.samples]
        self.me = self.samples[0][3][0] if self.samples else -1
        self.team = self.samples[0][3][1] if self.samples else 0
        self.stands = pp.flag_stands(d['map'])
        self.sheet = pp.parse_sheet(d['sheet']) if d['sheet'] else {}
        self.live = {s[0] for s in pp.live_samples(dict(d, samples=self.samples))}

    def at(self, t):
        i = bisect.bisect_right(self.times, t) - 1
        return self.samples[i] if i >= 0 else None

    def name_team(self, c, t):
        cur = None
        for tt, nm, tm in self.d['names'].get(str(c)) or []:
            if tt <= t or cur is None:
                cur = (nm, tm)
        return cur or (None, None)

    def pos_of(self, c, t):
        nm, _ = self.name_team(c, t)
        row = self.sheet.get(pp.name_key(nm or ''))
        return (row or {}).get('Pos', '?').split(' ')[0]

    def region(self, x, y):
        return pp.region(self.team, x, y, self.stands)

    def selfkill(self, t, victim, attacker, mod):
        if mod != MOD_SUICIDE:
            return False
        if 0 <= attacker < 32 and attacker != victim:
            _, ta = self.name_team(attacker, t)
            _, tv = self.name_team(victim, t)
            if ta is not None and ta != tv:
                return False          # a pit kill
        return True


def state(s, dem):
    p = s[3]
    return dict(health=p[5], force=p[6], armor=p[11], guns=guns(p[12]), ammo=ammo(p[13]),
                region=dem.region(p[2], p[3]), carrying=bool(p[8]), weapon=p[14])


def describe(rows, base, say):
    """Selfkill states against the baseline, as shares."""
    tests = (('force < 25', lambda r: r['force'] < 25), ('force >= 75', lambda r: r['force'] >= 75),
             ('health < 50', lambda r: r['health'] < 50), ('health >= 100', lambda r: r['health'] >= 100),
             ('armor 0', lambda r: r['armor'] <= 0),
             ('no gun', lambda r: r['guns'] == 0), ('3+ guns', lambda r: r['guns'] >= 3),
             ('gun ammo < 50', lambda r: r['ammo'] < 50),
             ('force<25 or health<50', lambda r: r['force'] < 25 or r['health'] < 50),
             ('carrying', lambda r: r['carrying']))
    for name, f in tests:
        say('    %-22s selfkill %s   baseline %s' % (name, pct(sum(1 for r in rows if f(r)), len(rows)),
                                                     pct(sum(1 for r in base if f(r)), len(base))))
    say('    region at selfkill: %s' % '  '.join('%s %s' % (g, pct(sum(1 for r in rows if r['region'] == g), len(rows)))
                                                for g in pp.REGIONS))
    say('    region baseline:    %s' % '  '.join('%s %s' % (g, pct(sum(1 for r in base if r['region'] == g), len(base)))
                                                for g in pp.REGIONS))


def selfkills(demos, say):
    say('== POV selfkills (observed state; baseline = every 20th live sample of the same position) ==')
    per = collections.defaultdict(list)
    base = collections.defaultdict(list)
    after = collections.defaultdict(list)
    life = collections.defaultdict(list)
    carry = []
    minutes = collections.Counter()
    for dem in demos:
        pos = dem.d['pos']
        minutes[pos] += len(dem.live) * pp.SAMPLE_MS / 60000
        for i, s in enumerate(dem.samples):
            if i % 20 == 0 and s[0] in dem.live and s[3][9]:
                base[pos].append(state(s, dem))
        spawn_t, last_spawn = [], None
        for s in dem.samples:
            if s[3][15] != last_spawn:
                spawn_t.append((s[0], s[3][15]))
                last_spawn = s[3][15]
        seen = set()
        for t, victim, attacker, mod in dem.d['obits']:
            if victim != dem.me or (t // 2000) in seen:
                continue
            seen.add(t // 2000)
            if not dem.selfkill(t, victim, attacker, mod):
                continue
            s = dem.at(t - 1)
            if not s or s[0] not in dem.live or not s[3][9]:
                continue
            st = state(s, dem)
            # Time alive before it.
            prev = [ts for ts, _ in spawn_t if ts <= t]
            st['alive_ms'] = t - prev[-1] if prev else None
            per[pos].append(st)
            life[pos].append(st['alive_ms'])
            # The next life: guns at respawn and 15 s later.
            nxt = next((ts for ts, _ in spawn_t if ts > t), None)
            if nxt is not None:
                a, b = dem.at(nxt + 250), dem.at(nxt + 15000)
                if a and b and a[3][9]:
                    after[pos].append((st['guns'], guns(a[3][12]), guns(b[3][12]), nxt - t,
                                       dem.region(a[3][2], a[3][3]), st['force'], a[3][6]))
            if st['carrying']:
                # Who takes the enemy flag next, within 20 s?
                bit = 2 if dem.team == pp.TEAM_RED else 1
                nxt_fc = None
                for s2 in dem.samples[bisect.bisect_right(dem.times, t):]:
                    if s2[0] > t + 20000:
                        break
                    for o in s2[4]:
                        if o[5] & bit:
                            nxt_fc = (s2[0] - t, dem.name_team(o[0], s2[0])[1] == dem.team, dem.pos_of(o[0], s2[0]))
                            break
                    if s2[3][8] & bit:
                        nxt_fc = (s2[0] - t, True, 'self')
                    if nxt_fc:
                        break
                st['own_dist'] = round(math.dist(s[3][2:4], dem.stands[dem.team]))
                st['enemies_near'] = sum(1 for o in s[4] if dem.name_team(o[0], s[0])[1] not in (None, dem.team)
                                         and math.dist(o[1:3], s[3][2:4]) < 800)
                carry.append((pos, st, nxt_fc))
    for pos in pp.POSITIONS:
        rows = per[pos]
        say('\n  %s: %d selfkills in %.0f live minutes (%.2f a minute); median life before it %s s' % (
            pos, len(rows), minutes[pos], len(rows) / max(1, minutes[pos]),
            None if not life[pos] else round(pp.q([x for x in life[pos] if x is not None], .5) / 1000, 1)))
        describe(rows, base[pos], say)
        ar = after[pos]
        if ar:
            gained = [c - b for a, b, c, *_ in ar]
            say('    next life: guns before %.1f -> at respawn %.1f -> +15 s %.1f; picked up 1+ guns within 15 s in %s, 2+ in %s; respawn delay median %s ms' % (
                sum(x[0] for x in ar) / len(ar), sum(x[1] for x in ar) / len(ar), sum(x[2] for x in ar) / len(ar),
                pct(sum(1 for g in gained if g >= 1), len(ar)), pct(sum(1 for g in gained if g >= 2), len(ar)),
                pp.q([x[3] for x in ar], .5)))
            say('    Force before %.0f -> after respawn %.0f; respawn region: %s' % (
                sum(x[5] for x in ar) / len(ar), sum(x[6] for x in ar) / len(ar),
                '  '.join('%s %s' % (g, pct(sum(1 for x in ar if x[4] == g), len(ar))) for g in pp.REGIONS)))
    say('\n  Selfkills while carrying: %d of %d (%s)' % (len(carry), sum(len(v) for v in per.values()),
                                                         pct(len(carry), sum(len(v) for v in per.values()))))
    if carry:
        fs = sorted(st['force'] for _, st, _ in carry)
        hs = sorted(st['health'] for _, st, _ in carry)
        ds = sorted(st['own_dist'] for _, st, _ in carry)
        mate = [n for _, _, n in carry if n and n[1]]
        say('    carrier Force median %s, health median %s, distance from own stand median %s; enemies within 800 u: %s' % (
            pp.q(fs, .5), pp.q(hs, .5), pp.q(ds, .5),
            ', '.join('%d: %d' % kv for kv in sorted(collections.Counter(st['enemies_near'] for _, st, _ in carry).items()))))
        say('    by position: %s' % ', '.join('%s %d' % kv for kv in collections.Counter(p for p, _, _ in carry).most_common()))
        say('    a teammate holds the flag within 20 s: %s (within 1 s %s, 1-5 s %s); median %s ms; next carrier: %s' % (
            pct(len(mate), len(carry)), pct(sum(1 for n in mate if n[0] <= 1000), len(carry)),
            pct(sum(1 for n in mate if 1000 < n[0] <= 5000), len(carry)), pp.q([n[0] for n in mate], .5),
            ', '.join('%s %d' % kv for kv in collections.Counter(n[2] for n in mate).most_common())))


def team_selfkills(demos, say):
    say('\n== Teammates\' selfkills (team overlay: health, Force, armor; position from the sheet) ==')
    rows, base = collections.defaultdict(list), collections.defaultdict(list)
    for dem in demos:
        tin = dem.d['tinfo']
        tt = [t for t, _ in tin]
        for k, (t, vals) in enumerate(tin):
            if k % 40 == 0:
                for c, loc, h, f, a, w in vals:
                    if c != dem.me and h > 0:
                        base[dem.pos_of(c, t)].append((h, f, a))
        seen = set()
        for t, victim, attacker, mod in dem.d['obits']:
            if victim == dem.me or (t // 2000, victim) in seen or not dem.selfkill(t, victim, attacker, mod):
                continue
            seen.add((t // 2000, victim))
            if dem.name_team(victim, t)[1] != dem.team:
                continue
            j = bisect.bisect_right(tt, t - 1) - 1
            if j < 0 or t - tin[j][0] > 2000:
                continue
            v = next((x for x in tin[j][1] if x[0] == victim), None)
            if v and v[2] > 0:
                rows[dem.pos_of(victim, t)].append((v[2], v[3], v[4]))
    for pos in pp.POSITIONS:
        r, b = rows[pos], base[pos]
        say('  %s: %d selfkills; force<25 %s (baseline %s), health<50 %s (%s), either %s (%s), force>=75 and health>=100 %s (%s)' % (
            pos, len(r),
            pct(sum(1 for h, f, a in r if f < 25), len(r)), pct(sum(1 for h, f, a in b if f < 25), len(b)),
            pct(sum(1 for h, f, a in r if h < 50), len(r)), pct(sum(1 for h, f, a in b if h < 50), len(b)),
            pct(sum(1 for h, f, a in r if f < 25 or h < 50), len(r)), pct(sum(1 for h, f, a in b if f < 25 or h < 50), len(b)),
            pct(sum(1 for h, f, a in r if f >= 75 and h >= 100), len(r)), pct(sum(1 for h, f, a in b if f >= 75 and h >= 100), len(b))))


def energize(demos, say):
    say('\n== Team energize (observed overlay Force; giver = teammate whose Force fell at the same message) ==')
    givers, recv_pos, recv_ctx, spent, powers = (collections.Counter() for _ in range(5))
    total, minutes, to_fc = 0, collections.Counter(), collections.Counter()
    absorbed = collections.Counter()
    fc_region = collections.Counter()
    amounts = []
    for dem in demos:
        minutes[dem.d['pos']] += len(dem.live) * pp.SAMPLE_MS / 60000
        tin = dem.d['tinfo']
        force_by = collections.defaultdict(list)
        for t, vals in tin:
            for c, loc, h, f, a, w in vals:
                force_by[c].append((t, f, h))
        pov_force = [(t, c, b) for t, c, b, *_ in dem.d['force']]
        rpos_of = dem.pos_of
        for k1, ((t0, v0), (t1, v1)) in enumerate(zip(tin, tin[1:]), 1):
            live = dem.at(t1)
            if t1 - t0 > 600 or not live or live[0] not in dem.live:
                continue
            a0 = {x[0]: x for x in v0}
            for x in v1:
                c = x[0]
                if c not in a0:
                    continue
                rise = x[3] - a0[c][3]
                # A respawn restores health too; a boon pickup is rare and not separable.
                if rise < ENERGIZE_MIN or a0[c][2] <= 0 or x[2] > a0[c][2] + 20 and x[2] >= 100:
                    continue
                # The giver's fall can reach the overlay one message later.
                after = {y[0]: y for y in (tin[k1 + 1][1] if k1 + 1 < len(tin) and tin[k1 + 1][0] - t1 < 600 else v1)}
                falls = [y[0] for y in v1 if y[0] != c and y[0] in a0 and
                         a0[y[0]][3] - min(y[3], after.get(y[0], y)[3]) >= GIVER_FALL]
                if not falls:
                    # No teammate paid for it: Force gained by absorbing an enemy's
                    # push, pull, grip or drain (17-18 a time, absorb on in all of them).
                    absorbed[rpos_of(c, t1)] += 1
                    continue
                total += 1
                amounts.append(rise)
                giver = dem.pos_of(falls[0], t1) if len(falls) == 1 else 'several'
                givers[giver] += 1
                rpos = dem.pos_of(c, t1)
                recv_pos[rpos] += 1
                s = dem.at(t1)
                carrying = None
                if s:
                    if c == dem.me:
                        carrying = bool(s[3][8])
                        xy = s[3][2:4]
                    else:
                        o = next((o for o in s[4] if o[0] == c), None)
                        carrying = bool(o[5]) if o else None
                        xy = o[1:3] if o else None
                    if carrying:
                        to_fc[giver] += 1
                        if xy:
                            fc_region[dem.region(*xy)] += 1
                recv_ctx['carrying' if carrying else 'not carrying' if carrying is False else 'not visible'] += 1
                # Spent within 5 s: the receiver's overlay Force afterwards.
                seq = force_by[c]
                k = bisect.bisect_left(seq, (t1, -1, -1))
                low = min((f for t, f, h in seq[k:] if t <= t1 + 5000), default=x[3])
                spent['spent 20+ within 5 s' if x[3] - low >= 20 else 'kept'] += 1
                for t, cl, b in pov_force:
                    if cl == c and t1 <= t <= t1 + 5000:
                        powers[pp.FORCE_NAMES[b]] += 1
    say('  received energizes: %d; amount median %s; per team-minute (one team a demo) %.2f' % (
        total, pp.q(amounts, .5), total / max(1, sum(minutes.values()))))
    say('  giver: %s' % ', '.join('%s %d' % kv for kv in givers.most_common()))
    say('  Force rises no teammate paid for (absorb gains), by receiver position: %s' % ', '.join('%s %d' % kv for kv in absorbed.most_common()))
    say('  receiver position: %s' % ', '.join('%s %d' % kv for kv in recv_pos.most_common()))
    say('  receiver: %s' % ', '.join('%s %d' % kv for kv in recv_ctx.most_common()))
    say('  energizes reaching a carrier, by giver: %s; carrier region then: %s' % (
        ', '.join('%s %d' % kv for kv in to_fc.most_common()),
        '  '.join('%s %d' % (g, fc_region[g]) for g in pp.REGIONS)))
    say('  receiver afterwards: %s' % ', '.join('%s %d' % kv for kv in spent.most_common()))
    say('  receiver\'s visible activations within 5 s: %s' % ', '.join('%s %d' % kv for kv in powers.most_common(8)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=pp.DEFAULT_OUT)
    args = ap.parse_args()
    demos = [Demo(d) for d in pp.load(args.out)]
    lines = []

    def say(s=''):
        lines.append(s)
        print(s, flush=True)

    say('demos: %d (with resource fields: %d)' % (len(demos), sum(1 for d in demos if d.samples)))
    selfkills(demos, say)
    team_selfkills(demos, say)
    energize(demos, say)
    (args.out / 'resources.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
