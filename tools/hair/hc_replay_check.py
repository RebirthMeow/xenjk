"""hc_replay_check.py <reel.hcr> <hc_replay out.bin>: how far hc_replay's steps are from the states the reel recorded
(an in-game r_hairDump through hcd_to_hcr.py, or a research reel). Prints the largest difference of the one-step
replay (each step from the recorded pre-step state) and of the free run, in asset units, and how many steps carried
their own pose time (stretched steps, rd-common/sim_frame.h)."""
import struct
import sys

import numpy as np

reel, out = sys.argv[1], sys.argv[2]
b = open(reel, 'rb').read()
magic, ver, nS, nP, nC, nR, nF, nB, resetFrame, nPre = struct.unpack_from('<4sI8i', b, 0)
if magic != b'HCR1':
    sys.exit('not an HCR1 reel')
o = 40 + nP * 3 * 8 * (3 if ver >= 2 else 1)   # version 2: the reset's post-pre-roll pos and prev follow the targets
per = 16 + nP * 3 + nC * 3 + nR * 6 + nP * 12 + 9
post = np.zeros((nS, nP, 3))
pose_us = np.zeros(nS, dtype=np.int64)
for i in range(nS):
    pose_us[i] = struct.unpack_from('<2i', b, o)[1]
    f = np.frombuffer(b, '<f8', per, o + 8)
    k = 16 + nP * 3 + nC * 3 + nR * 6 + nP * 6
    post[i] = f[k:k + nP * 3].reshape(nP, 3)
    o += 8 + per * 8
r = open(out, 'rb').read()
n, p, _, _ = struct.unpack_from('<4i', r, 0)
if (n, p) != (nS, nP):
    sys.exit('the replay is of another reel')
one = np.frombuffer(r, '<f8', nS * nP * 3, 16).reshape(nS, nP, 3)
free = np.frombuffer(r, '<f8', nS * nP * 3, 16 + nS * nP * 3 * 8).reshape(nS, nP, 3)
d1 = np.abs(one - post).max(axis=(1, 2))
d2 = np.abs(free - post).max(axis=(1, 2))
st = pose_us > 0
print('%d steps, %d with their own pose time (%s ms)' % (nS, int(st.sum()),
      ', '.join(sorted({'%.1f' % (u / 1000.0) for u in pose_us[st]})[:6]) or '-'))
print('one step: max %.3g units (step %d); on the stretched steps max %.3g' % (d1.max(), int(d1.argmax()), d1[st].max() if st.any() else 0.0))
print('free run: max %.3g units (step %d)' % (d2.max(), int(d2.argmax())))
