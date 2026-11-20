"""Orthographic matplotlib renders of body (budget glm, skinned with the reel pose) + simulated skirt.
Usage: render.py <out_dir> <extra.npz> <reel.skp> <positions.bin> <meta> [--tag TAG]
Camera follows the character yaw and pelvis; skirt triangles are pink on the outside face and dark blue on the back face (inside-out is visible)."""
import sys, json, struct, argparse, os
import numpy as np
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import common, glaio

ap = argparse.ArgumentParser()
ap.add_argument('out'); ap.add_argument('extra'); ap.add_argument('pose'); ap.add_argument('pos'); ap.add_argument('meta')
ap.add_argument('--tag', default='')
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
E = np.load(args.extra); refInv = E['refInv'].astype(np.float64); NB = refInv.shape[0]
tri = E['tri'].astype(np.int64)
M = np.load(args.meta, allow_pickle=True); YAW = M['yaw']
f = open(args.pose, 'rb'); f.read(4); ver, jb = struct.unpack('<II', f.read(8)); pm = json.loads(f.read(jb).decode('utf-8')); data0 = f.tell()
fb = 12 + NB * 64
raw = np.memmap(args.pose, dtype=np.uint8, mode='r', offset=data0)
get_cst = lambda i: np.frombuffer(raw[i * fb + 12:(i + 1) * fb], dtype='<f4').reshape(NB, 4, 4).astype(np.float64)
g = open(args.pos, 'rb'); g.read(4); nv, nf = struct.unpack('<II', g.read(8))
P = np.fromfile(g, dtype=np.dtype([('i', '<u4'), ('p', '<f4', (nv, 3))]), count=nf); pos_of = {int(r): k for k, r in enumerate(P['i'])}
clips = {c['name']: c for c in pm['clips']}
PEL = pm['boneNames'].index('pelvis')

# body from the budget glm
glm = common.load_all()['glm']; K = common.CM_PER_UNIT
SKIP = ('skirt', 'head_hair', 'head_eye', 'head_mouth', 'head_teeth', 'head_brow', 'torso_glass', 'stupid')
bv, bb, bw, bt = [], [], [], []
off = 0
for s, sf in zip(glm['surfaces'], glm['lods'][0]):
    if (s['flags'] & 3) or s['name'].startswith(SKIP): continue
    P0 = np.array([v['co'] for v in sf['verts']]) * K
    bi = np.full((len(P0), 4), -1); wt = np.zeros((len(P0), 4))
    for i, v in enumerate(sf['verts']):
        for j, (b, w) in enumerate(zip(v['b'], v['w'])): bi[i, j] = sf['brefs'][b]; wt[i, j] = w
    bv.append(P0); bb.append(bi); bw.append(wt); bt.append(np.array(sf['tris']) + off); off += len(P0)
BV = np.concatenate(bv); BB = np.concatenate(bb); BW = np.concatenate(bw); BT = np.concatenate(bt)

def skin(bind, B, W, mats):
    h = np.concatenate([bind, np.ones((len(bind), 1))], 1); out = np.zeros((len(bind), 3))
    for k in range(B.shape[1]):
        ok = B[:, k] >= 0; m = mats[np.where(ok, B[:, k], 0)]
        out += np.where(ok[:, None], W[:, k:k + 1] * np.einsum('ni,nij->nj', h, m)[:, :3], 0)
    return out

LIGHT = np.array([0.3, -0.6, 0.75]); LIGHT /= np.linalg.norm(LIGHT)

def view_basis(kind):
    # screen right, screen up, look direction (body frame: model faces -Y, +X is her left, Z up)
    if kind == 'front': look = np.array([0, 1.0, 0])
    elif kind == 'side': look = np.array([-1.0, 0, 0])
    elif kind == 'back': look = np.array([0, -1.0, 0])
    else: look = np.array([-1.0, 1.0, -0.15]); look /= np.linalg.norm(look)      # 3/4 from front-left, slightly above
    up = np.array([0, 0, 1.0]); right = np.cross(look, up); right /= np.linalg.norm(right); up2 = np.cross(right, look)
    return right, up2, look

def frame_geom(gi):
    cst = get_cst(gi); mats = np.einsum('bij,bjk->bik', refInv, cst)
    body = skin(BV, BB, BW, mats); skirt = P['p'][pos_of[gi]].astype(np.float64)
    pel = cst[PEL, 3, :3].copy(); y = YAW[gi]
    c, s = np.cos(-y), np.sin(-y); Rz = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    to_body = lambda X: (X - pel) @ Rz.T
    return to_body(body), to_body(skirt)

def draw(ax, gi, kind, lim=(-60, 60, -75, 75), draft=False):
    body, skirt = frame_geom(gi)
    right, up, look = view_basis(kind)
    polys = []; cols = []; depth = []
    for X, T, isskirt in ((body, BT, False), (skirt, tri, True)):
        A, B, C = X[T[:, 0]], X[T[:, 1]], X[T[:, 2]]
        n = np.cross(C - A, B - A); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-20     # outward for body; sheet side for skirt
        cen = (A + B + C) / 3
        sh = np.clip(n @ LIGHT, -1, 1) * 0.5 + 0.5
        facing = (n @ look) < 0         # front-facing to the camera
        if isskirt:
            back = np.array([1.0, 0.45, 0.62]); front = np.array([0.1, 0.15, 0.45])
            cl = np.where(facing[:, None], front[None] * (0.55 + 0.45 * sh[:, None]), back[None] * (0.6 + 0.4 * sh[:, None]))
        else:
            cl = np.array([0.72, 0.72, 0.74])[None] * (0.45 + 0.55 * np.abs(sh[:, None]))
        sx = np.stack([A @ right, B @ right, C @ right], 1); sy = np.stack([A @ up, B @ up, C @ up], 1)
        polys.append(np.stack([sx, sy], 2)); cols.append(cl); depth.append(cen @ look + (-0.01 if isskirt else 0))
    polys = np.concatenate(polys); cols = np.concatenate(cols); depth = np.concatenate(depth)
    o = np.argsort(-depth)
    ax.add_collection(PolyCollection(polys[o], facecolors=np.clip(cols[o], 0, 1), edgecolors='none', linewidths=0))
    ax.set_xlim(lim[0], lim[1]); ax.set_ylim(lim[2], lim[3]); ax.set_aspect('equal'); ax.axis('off')

def shot(name, clip, t_s, title=None, views=('front', 'side', 'iso')):
    c = clips[clip]; gi = c['start'] + c['evalStart'] + int(round(t_s * pm['fps']))
    fig, axs = plt.subplots(1, len(views), figsize=(4.2 * len(views), 5.2), dpi=110)
    for ax, v in zip(np.atleast_1d(axs), views):
        draw(ax, gi, v); ax.set_title(f'{v}', fontsize=9)
    fig.suptitle(title or f'{clip} t={t_s:.2f}s (frame {gi - c["start"]})', fontsize=10)
    fig.tight_layout(); fig.savefig(os.path.join(args.out, name), facecolor='white'); plt.close(fig)

def sheet(name, clip_times, kind='iso', cols=8):
    rows = len(clip_times)
    fig, axs = plt.subplots(rows, cols, figsize=(cols * 1.9, rows * 2.5), dpi=100)
    for r, (clip, t0, t1) in enumerate(clip_times):
        c = clips[clip]
        for k in range(cols):
            t = t0 + (t1 - t0) * k / (cols - 1)
            gi = c['start'] + c['evalStart'] + int(round(t * pm['fps']))
            ax = axs[r, k]; draw(ax, gi, kind, lim=(-55, 55, -70, 60), draft=True)
            if k == 0: ax.text(-54, 52, clip, fontsize=7, color='k')
            ax.text(-54, -66, f'{t:.2f}s', fontsize=6)
    fig.tight_layout(pad=0.2); fig.savefig(os.path.join(args.out, name), facecolor='white'); plt.close(fig)

if __name__ == '__main__':
    t = args.tag
    shot(f'run{t}.png', 'run', 1.5, 'run (250 u/s forward, root motion)')
    shot(f'flip{t}.png', 'flip', 1.45, 'BOTH_FLIP_F, run-in, apex of the arc')
    shot(f'flip_late{t}.png', 'flip', 1.8, 'BOTH_FLIP_F, descending')
    shot(f'saber{t}.png', 'saber_chain', 0.75, 'saber attack chain (stationary)')
    shot(f'kick{t}.png', 'kick', 0.5, 'BOTH_A7_KICK_F')
    shot(f'idle{t}.png', 'idle', 2.0, 'idle (BOTH_STAND1IDLE1)')
    shot(f'turn{t}.png', 'turn_stand', 0.75, 'fast turn, 720 deg/s')
    shot(f'wallrun{t}.png', 'wallrun', 1.5, 'wall run right')
    sheet(f'contact_sheet{t}.png', [('run', 0.0, 1.4), ('flip', 0.3, 2.4), ('saber_chain', 0.0, 2.1), ('kick', 0.3, 3.0), ('turn_run', 0.5, 2.6), ('roll', 0.5, 1.6), ('cartwheel', 0.0, 1.0)])
