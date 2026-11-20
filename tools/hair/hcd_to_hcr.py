"""hcd_to_hcr.py <dump.hcd> <asset.chains> <out.hcr> [--segment N]: an in-game hair dump (r_hairDump, "HCD1",
rd-common/tr_hair.cpp) -> a replay reel (HCR1) for tools/hair/hc_replay.c, and the hc_replay arguments that match the
dump's settings (containment flag, game feel and flow as the engine held them; flow from dump version 2).

A dump records one instance: a reset record, then one record per step (scene steps at the asset's dt) until the next
reset. --segment N picks the N-th reset's segment (default 0). With the matching arguments, hc_replay's one-step
and free-run outputs should equal the dump's post-step states exactly (the same C code, float, the same inputs).
"""
import argparse
import struct
import sys

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('dump')
ap.add_argument('asset')
ap.add_argument('out')
ap.add_argument('--segment', type=int, default=0)
a = ap.parse_args()

b = open(a.dump, 'rb').read()
magic, ver, nP, nC, nR, flags = struct.unpack_from('<6i', b, 0)
if magic != 0x31444348 or ver not in (1, 2):
    sys.exit('not an HCD1 dump')
on = struct.unpack_from('<i', b, 24)[0]
feel = struct.unpack_from('<9f', b, 28)
flow = struct.unpack_from('<4f', b, 64) if ver >= 2 else (0.0, 0.0, 0.0, 0.0)   # version 2: hcFlow
o = 64 if ver == 1 else 80
rec = 8 + 4 * (16 + nP * 3 + nC * 3 + nR * 6 + nP * 12)
nrec = (len(b) - o) // rec
recs = []
for i in range(nrec):
    p = o + i * rec
    cnt, fl = struct.unpack_from('<2i', b, p)
    f = np.frombuffer(b, '<f4', (rec - 8) // 4, p + 8).astype(np.float64)
    k = 0

    def take(n):
        global k
        v = f[k:k + n]
        k += n
        return v
    r = dict(counter=cnt, flags=fl, ref=take(16), targets=take(nP * 3), anchors=take(nC * 3), rep=take(nR * 6),
             pre_pos=take(nP * 3), pre_prev=take(nP * 3), post_pos=take(nP * 3), post_prev=take(nP * 3))
    recs.append(r)
resets = [i for i, r in enumerate(recs) if r['flags'] & 1]
if a.segment >= len(resets):
    sys.exit('the dump has %d reset(s)' % len(resets))
i0 = resets[a.segment]
i1 = resets[a.segment + 1] if a.segment + 1 < len(resets) else len(recs)
reset, steps = recs[i0], recs[i0 + 1:i1]
if not steps:
    sys.exit('no steps after that reset')

# asset bone count (the reel header carries it; no pose frames)
ab = open(a.asset, 'rb').read()
nB = struct.unpack_from('<i', ab, 36)[0]
with open(a.out, 'wb') as f:
    # version 2: the reset record's state after the game's pre-roll (r_hairPreroll) seeds the replay; the
    # targets alone gave the state before it
    f.write(struct.pack('<4sI8i', b'HCR1', 2, len(steps), nP, nC, nR, 0, nB, reset['counter'], 0))
    f.write(reset['targets'].astype('<f8').tobytes())
    f.write(reset['post_pos'].astype('<f8').tobytes())
    f.write(reset['post_prev'].astype('<f8').tobytes())
    for s in steps:
        f.write(struct.pack('<2i', s['counter'], s['flags'] >> 8))   # pose time in microseconds (0 = dt), sim_frame.h
        for key in ('ref', 'targets', 'anchors', 'rep', 'pre_pos', 'pre_prev', 'post_pos', 'post_prev'):
            f.write(s[key].astype('<f8').tobytes())
        f.write(np.zeros(9, '<f8').tobytes())   # frame kinematics: not recorded in game
args = []
if flags & 2:
    args.append('--honour-inverse')
if on:
    args += ['--feel-rad', ','.join('%.9g' % v for v in feel)]
if any(flow):
    args += ['--flow', ','.join('%.9g' % v for v in flow)]
print('%d records, %d resets; segment %d: reset at %d, %d steps -> %s' % (nrec, len(resets), a.segment, reset['counter'],
                                                                       len(steps), a.out))
print('ARGS ' + ' '.join(args))
