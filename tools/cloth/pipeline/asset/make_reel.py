"""Test reel from stock _humanoid.gla animations -> SKP1 pose file for the cloth harness.
Per frame the file holds the component/world-space bone transforms cst[b] (row-vector 4x4, cm) so that
refInv[b] * cst[b] is the skinning matrix (same contract as the SKD1 traces).
The entity (player) origin moves through the world with a velocity per segment, and yaw turns the whole
model: the cloth feels this through the pelvis (reference bone) only, like the game will do it.
Usage: make_reel.py <out.skp> <out_meta.npz> [--fps 60]"""
import sys, json, struct, math, argparse
import numpy as np
import glaio, common

ap = argparse.ArgumentParser()
ap.add_argument('out'); ap.add_argument('meta'); ap.add_argument('--fps', type=float, default=60.0)
args = ap.parse_args()

K = common.CM_PER_UNIT
DT = 1.0 / args.fps
G = glaio.Gla(); NB = G.numBones; SEQ = glaio.read_cfg()
EXTRA = np.load('asset_extra.npz')
BASE_NS = EXTRA['base_ns'] / 1.0          # cm translations; rotation orthonormal
BASE_U = BASE_NS.copy(); BASE_U[:, :3, 3] /= K     # in glm units
def desc(b):
    out = [b]
    for c in range(NB):
        if G.parent[c] == b: out += desc(c)
    return out
TORSO = set(desc(G.names.index('lower_lumbar')))

_cache = {}
def params(f):
    if f not in _cache: _cache[f] = tuple(a[0] for a in G.comp_params([f]))
    return _cache[f]

JB = G.base[:, :3, 3].copy()       # joint (bind origin) of every bone, model space, glm units

def nlerp(a, b, s):
    """Blend two bone sets. The compressed translation t of a bone depends on its rotation (the rotation is about the origin of
    the parent chain, not about the joint), so rotations and joint displacements d = t + R*j - j are blended separately and
    t is rebuilt from the blended rotation: x' = R x + t rotates about the joint j for any blend."""
    qa, ta = a; qb, tb = b
    sg = np.where((qa * qb).sum(-1, keepdims=True) < 0, -1.0, 1.0)
    q = qa * (1 - s) + qb * sg * s
    q = q / np.linalg.norm(q, axis=-1, keepdims=True)
    Ra, Rb, Rq = glaio._qmat(qa)[..., :3, :3], glaio._qmat(qb)[..., :3, :3], glaio._qmat(q)[..., :3, :3]
    da = ta + np.einsum('bij,bj->bi', Ra, JB) - JB
    db = tb + np.einsum('bij,bj->bi', Rb, JB) - JB
    d = da * (1 - s) + db * s
    return q, d - np.einsum('bij,bj->bi', Rq, JB) + JB

def sample(seq, tau, loop):
    start, n, lf, fps = SEQ[seq]
    fps = abs(fps) or 20
    f = max(tau, 0.0) * fps
    if loop and n > 1:
        f = f % n; i0 = int(math.floor(f)); i1 = (i0 + 1) % n
    else:
        f = min(f, n - 1); i0 = int(math.floor(f)); i1 = min(i0 + 1, n - 1)
    return nlerp(params(start + i0), params(start + i1), f - i0)

def smooth(s): s = min(max(s, 0.0), 1.0); return s * s * (3 - 2 * s)

class Track:
    """items: (seq, duration, loop); crossfade B between consecutive items."""
    def __init__(self, items, B=0.08):
        self.items = items; self.B = B
        self.starts = np.concatenate([[0.0], np.cumsum([i[1] for i in items])])
    @property
    def dur(self): return self.starts[-1]
    def at(self, tau):
        k = int(np.searchsorted(self.starts, tau, side='right') - 1); k = min(max(k, 0), len(self.items) - 1)
        seq, d, loop = self.items[k]; loc = tau - self.starts[k]
        cur = sample(seq, loc, loop)
        if k > 0 and loc < self.B:
            pseq, pd, ploop = self.items[k - 1]
            prev = sample(pseq, pd + loc, ploop)
            cur = nlerp(prev, cur, smooth(loc / self.B))
        return cur

class Seg:
    def __init__(self, legs, dur, vel=(0, 0), torso=None, yaw=0.0, jump=None, ramp=0.15, land_hold=False):
        self.legs = legs if isinstance(legs, Track) else Track([(legs[0], dur, legs[1])])
        self.torso = None if torso is None else (torso if isinstance(torso, Track) else Track([(torso[0], dur, torso[1])]))
        self.dur = dur; self.vel = vel; self.yaw = yaw; self.jump = jump; self.ramp = ramp
    def at(self, tau):
        ql, tl = self.legs.at(tau)
        if self.torso is not None:
            qt, tt = self.torso.at(tau)
            ql = ql.copy(); tl = tl.copy()
            for b in TORSO: ql[b] = qt[b]; tl[b] = tt[b]
        return ql, tl

R = lambda seq, d, v=(0, 0), **k: Seg((seq, True), d, v, **k)      # looping
O = lambda seq, d, v=(0, 0), **k: Seg((seq, False), d, v, **k)     # once, then hold the last frame
STAND = 'BOTH_STAND1'; IDLE = 'BOTH_STAND1IDLE1'
RUNV = (0, 250)

def chain(items, B=0.08): return Track(items, B)

CLIPS = {
 'idle':      [O(IDLE, 5.0)],
 'walk':      [R('BOTH_WALK1', 3.0, (0, 125))],
 'run':       [R('BOTH_RUN1', 3.0, RUNV), R(STAND, 1.0, (0, 0), ramp=0.12)],
 'run_back':  [R('BOTH_RUNBACK1', 3.0, (0, -200)), R(STAND, 1.0)],
 'strafe_l':  [R('BOTH_RUNSTRAFE_LEFT1', 3.0, (250, 0)), R(STAND, 1.0)],
 'strafe_r':  [R('BOTH_RUNSTRAFE_RIGHT1', 3.0, (-250, 0)), R(STAND, 1.0)],
 'jump':      [R('BOTH_RUN1', 1.0, RUNV), O('BOTH_JUMP1', 0.4, RUNV, jump=225), O('BOTH_INAIR1', 0.1625, RUNV), O('BOTH_LAND1', 0.3, RUNV), R('BOTH_RUN1', 1.2, RUNV), R(STAND, 0.8)],
 'flip':      [R('BOTH_RUN1', 1.0, RUNV), O('BOTH_FLIP_F', 0.75, RUNV, jump=300), O('BOTH_FLIP_LAND', 1.0, (0, 0), ramp=0.3), R(STAND, 0.8)],
 'flip_back': [R(STAND, 0.4), O('BOTH_FLIP_B', 0.75, (0, -150), jump=300), O('BOTH_FLIP_LAND', 1.0, (0, 0), ramp=0.3), R(STAND, 0.8)],
 'forcejump': [R(STAND, 0.4), O('BOTH_FORCEJUMP1', 0.85, (0, 150), jump=450), O('BOTH_FORCEINAIR1', 0.3, (0, 150)), O('BOTH_FORCELAND1', 0.8, (0, 0), ramp=0.3), R(STAND, 0.8)],
 'alora_flip':[R('BOTH_RUN1', 0.8, RUNV), O('BOTH_ALORA_FLIP_1', 1.1, (0, 200), jump=280), O('BOTH_FLIP_LAND', 0.6, (0, 0), ramp=0.3), R(STAND, 0.8)],
 'cartwheel': [R(STAND, 0.4), O('BOTH_CARTWHEEL_LEFT', 1.0, (250, 0)), R(STAND, 0.8)],
 'roll':      [R('BOTH_RUN1', 0.8, RUNV), O('BOTH_ROLL_F', 1.0, (0, 350), ramp=0.05), R(STAND, 1.0, (0, 0), ramp=0.1)],
 'wallrun':   [R('BOTH_RUN1', 0.6, RUNV), O('BOTH_WALL_RUN_RIGHT', 1.85, (0, 250)), O('BOTH_WALL_RUN_RIGHT_FLIP', 0.7, (150, -50), jump=300), R(STAND, 1.2, (0, 0), ramp=0.2)],
 'kick':      [R(STAND, 0.4), O('BOTH_A7_KICK_F', 0.8), O('BOTH_A7_KICK_S', 1.7), O('BOTH_A1_SPECIAL', 2.85), R(STAND, 0.8)],
 'saber_chain': [Seg(chain([('BOTH_A5_TL_BR', 0.2, False), ('BOTH_A5_TR_BL', 0.27, False), ('BOTH_A5__L__R', 0.23, False), ('BOTH_A5_T__B_', 0.37, False),
                            ('BOTH_A3_TL_BR', 0.47, False), ('BOTH_A3_TR_BL', 0.3, False), ('BOTH_A3_T__B_', 0.67, False), ('BOTH_A1_BL_TR', 0.2, False),
                            ('BOTH_A1_TR_BL', 0.3, False), (STAND, 0.6, True)], B=0.05), 3.61)],
 'saber_run': [Seg(('BOTH_RUN1', True), 3.2, RUNV, torso=chain([(STAND, 0.4, True), ('BOTH_A5_TL_BR', 0.2, False), ('BOTH_A5_TR_BL', 0.27, False), ('BOTH_A5__L__R', 0.23, False),
                            ('BOTH_A5_T__B_', 0.37, False), ('BOTH_A3_TL_BR', 0.47, False), ('BOTH_A3_TR_BL', 0.3, False), (STAND, 0.96, True)], B=0.05)), R(STAND, 1.0)],
 'turn_stand':[R(STAND, 0.4), R('BOTH_TURNSTAND1', 0.5, (0, 0), yaw=720.0), R(STAND, 0.4), R('BOTH_TURNSTAND1', 0.5, (0, 0), yaw=-720.0), R(STAND, 0.8)],
 'turn_run':  [R('BOTH_RUN1', 1.0, RUNV), R('BOTH_RUN1', 0.35, RUNV, yaw=540.0), R('BOTH_RUN1', 0.8, RUNV), R('BOTH_RUN1', 0.35, RUNV, yaw=-540.0), R('BOTH_RUN1', 0.8, RUNV), R(STAND, 0.8)],
 'run_circle':[R('BOTH_RUN1', 4.0, RUNV, yaw=120.0), R(STAND, 1.0)],
}
INPLACE = ['run', 'strafe_l', 'jump', 'flip', 'saber_run', 'turn_run', 'roll']

def build_clip(name, segs, rooted):
    PRE = 0.5
    segs = [O(IDLE, PRE)] + segs
    ZG = 24.0; GRAV = 800.0
    org = np.array([0.0, 0.0, ZG]); yaw = 0.0; vz = 0.0; air = False
    vel = np.zeros(2)
    frames = []; t = 0.0
    prev_pose = None; prev_t = 0.0
    BLEND = 0.12
    for si, seg in enumerate(segs):
        n = int(round(seg.dur * args.fps))
        seg_vel = np.array(seg.vel if rooted else (0, 0), float)
        v0 = vel.copy()
        if seg.jump and rooted and not air: vz = seg.jump; air = True
        yaw_rate = math.radians(seg.yaw)
        for k in range(n):
            tau = k * DT
            pose = seg.at(tau)
            if prev_pose is not None and tau < BLEND:
                pose = nlerp(prev_pose(prev_t + tau), pose, smooth(tau / BLEND))
            a = smooth(tau / seg.ramp) if seg.ramp > 0 else 1.0
            vel = v0 * (1 - a) + seg_vel * a
            yaw += yaw_rate * DT
            fwd = np.array([math.sin(yaw), -math.cos(yaw)])   # model faces -Y; yaw>0 turns left (ccw)
            left = np.array([math.cos(yaw), math.sin(yaw)])
            org[:2] += (left * vel[0] + fwd * vel[1]) * DT
            if air:
                vz -= GRAV * DT; org[2] += vz * DT
                if org[2] <= ZG and vz < 0: org[2] = ZG; vz = 0.0; air = False
            frames.append((pose[0], pose[1], org.copy(), yaw, si, t))
            t += DT
        sg = seg
        prev_pose = (lambda ss: (lambda tt: ss.at(tt)))(sg); prev_t = seg.dur
    return frames, int(round(PRE * args.fps))

clips_out = []; allq = []; allt = []; org_l = []; yaw_l = []; seg_l = []
names_order = []
for name, segs in CLIPS.items():
    variants = [(name, True)] + ([(name + '_ip', False)] if name in INPLACE else [])
    for vname, rooted in variants:
        frames, pre = build_clip(vname, segs, rooted)
        start = len(org_l)
        for q, t_, o, y, si, tt in frames:
            allq.append(q); allt.append(t_); org_l.append(o); yaw_l.append(y); seg_l.append(si)
        clips_out.append(dict(name=vname, start=start, frames=len(frames), evalStart=pre, rooted=rooted))
        print(vname, len(frames))

Q = np.stack(allq); T = np.stack(allt); ORG = np.array(org_l); YAW = np.array(yaw_l)
skin = G.chain_skin(Q, T)                                       # (F,nb,4,4) glm units
F = len(Q)
Wm = np.zeros((F, 4, 4)); Wm[:, 3, 3] = 1
Wm[:, 0, 0] = np.cos(YAW); Wm[:, 0, 1] = -np.sin(YAW); Wm[:, 1, 0] = np.sin(YAW); Wm[:, 1, 1] = np.cos(YAW); Wm[:, 2, 2] = 1
Wm[:, :3, 3] = ORG
pose = Wm[:, None] @ skin @ BASE_U[None]                        # column-vector, glm units
pose[..., :3, 3] *= K                                            # -> cm
cst = np.swapaxes(pose, -1, -2).astype(np.float32)               # row-vector
meta = dict(version=1, nBones=NB, fps=args.fps, clips=clips_out, boneNames=G.names, unitCmPerSkelUnit=K,
            note='cst[b] = (W * chain_b * base_ns_b)^T, cm; W = Rz(yaw) + entity origin; model faces -Y, +X is left')
mj = json.dumps(meta).encode('utf-8')
with open(args.out, 'wb') as f:
    f.write(b'SKP1' + struct.pack('<II', 1, len(mj)) + mj)
    for i in range(F):
        f.write(struct.pack('<IfI', i, DT, 1)); f.write(cst[i].tobytes())
np.savez(args.meta, org=ORG, yaw=YAW, seg=np.array(seg_l), clips=json.dumps(clips_out))
print('frames', F, 'bytes', 12 + len(mj) + F * (12 + NB * 64))
