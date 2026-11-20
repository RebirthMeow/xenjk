"""Standalone _humanoid.gla animation decoder (layout and maths from the JKA Blender add-on JAG2GLA/JAG2Math)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os
import struct, sys, numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'model'))
import jka

GLA = os.path.join(str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012'), 'gla', 'eve2b_np.gla')   # eve2b_np skeleton (stock: eve2b-glm-20261012\src\stock\...\_humanoid.gla)
CFG = str(REPO / '.codex-research' / 'eve2b-glm-20261012' / 'src' / 'stock' / 'models' / 'players') + r'\_humanoid\animation.cfg'

class Gla:
    def __init__(self, path=GLA):
        self.buf = open(path, 'rb').read()
        ident, ver, name, scale, nf, ofsf, nb, ofsc, ofss, ofse = struct.unpack_from('4si64sf6i', self.buf, 0)
        self.numFrames, self.ofsFrames, self.numBones, self.ofsPool, self.ofsEnd = nf, ofsf, nb, ofsc, ofse
        self.skel = jka.read_gla(path)
        self.bones = self.skel['bones']
        self.names = [b['name'] for b in self.bones]
        self.parent = [b['parent'] for b in self.bones]
        # base poses (column-vector 4x4, model space) and order (parents first)
        self.base = np.zeros((nb, 4, 4)); self.base[:, 3, 3] = 1
        for i, b in enumerate(self.bones): self.base[i, :3, :] = b['base']
        self.order = []
        done = set()
        while len(self.order) < nb:
            for i in range(nb):
                if i in done: continue
                if self.parent[i] != -1 and self.parent[i] not in done: continue
                self.order.append(i); done.add(i)
        self.idx = np.frombuffer(self.buf, dtype=np.uint8, count=nf * nb * 3, offset=ofsf).reshape(nf, nb, 3)

    def frame_matrices(self, frames):
        """-> skin (F,nb,4,4) and pose (F,nb,4,4), column-vector, JKA units.
        skin_b = chain of compressed-bone matrices (identity at bind); pose_b = skin_b @ base_b."""
        frames = np.asarray(frames, dtype=int)
        ix = self.idx[frames].astype(np.uint32)
        ix = ix[..., 0] | (ix[..., 1] << 8) | (ix[..., 2] << 16)   # (F,nb)
        uniq, inv = np.unique(ix, return_inverse=True)
        raw = np.empty((len(uniq), 7), dtype=np.uint16)
        for k, u in enumerate(uniq):
            raw[k] = struct.unpack_from('7H', self.buf, self.ofsPool + int(u) * 14)
        q = raw[:, :4].astype(np.float64) / 16383.0 - 2.0
        n = np.linalg.norm(q, axis=1, keepdims=True); q = q / n
        w, x, y, z = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
        M = np.zeros((len(uniq), 4, 4)); M[:, 3, 3] = 1
        M[:, 0, 0] = 1 - 2 * (y * y + z * z); M[:, 0, 1] = 2 * (x * y - z * w); M[:, 0, 2] = 2 * (x * z + y * w)
        M[:, 1, 0] = 2 * (x * y + z * w); M[:, 1, 1] = 1 - 2 * (x * x + z * z); M[:, 1, 2] = 2 * (y * z - x * w)
        M[:, 2, 0] = 2 * (x * z - y * w); M[:, 2, 1] = 2 * (y * z + x * w); M[:, 2, 2] = 1 - 2 * (x * x + y * y)
        M[:, :3, 3] = raw[:, 4:].astype(np.float64) / 64.0 - 512.0
        comp = M[inv.reshape(ix.shape)]          # (F,nb,4,4)
        skin = np.empty_like(comp)
        for b in self.order:
            p = self.parent[b]
            skin[:, b] = comp[:, b] if p < 0 else skin[:, p] @ comp[:, b]
        pose = skin @ self.base[None]
        return skin, pose


def _qmat(q):
    w, x, y, z = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    M = np.zeros(q.shape[:-1] + (4, 4)); M[..., 3, 3] = 1
    M[..., 0, 0] = 1 - 2 * (y * y + z * z); M[..., 0, 1] = 2 * (x * y - z * w); M[..., 0, 2] = 2 * (x * z + y * w)
    M[..., 1, 0] = 2 * (x * y + z * w); M[..., 1, 1] = 1 - 2 * (x * x + z * z); M[..., 1, 2] = 2 * (y * z - x * w)
    M[..., 2, 0] = 2 * (x * z - y * w); M[..., 2, 1] = 2 * (y * z + x * w); M[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return M

def comp_params(self, frames):
    """compressed bone parameters for frames (...,) -> q (...,nb,4) wxyz unit, t (...,nb,3)"""
    frames = np.asarray(frames, dtype=int)
    ix = self.idx[frames].astype(np.uint32)
    ix = ix[..., 0] | (ix[..., 1] << 8) | (ix[..., 2] << 16)
    uniq, inv = np.unique(ix, return_inverse=True)
    raw = np.empty((len(uniq), 7), dtype=np.uint16)
    for k, u in enumerate(uniq):
        raw[k] = struct.unpack_from('7H', self.buf, self.ofsPool + int(u) * 14)
    q = raw[:, :4].astype(np.float64) / 16383.0 - 2.0
    q = q / np.linalg.norm(q, axis=1, keepdims=True)
    t = raw[:, 4:].astype(np.float64) / 64.0 - 512.0
    inv = inv.reshape(ix.shape)
    return q[inv], t[inv]

def chain_skin(self, q, t):
    """q (...,nb,4), t (...,nb,3) -> skin matrices (...,nb,4,4), column-vector (identity at bind)"""
    comp = _qmat(q); comp[..., :3, 3] = t
    skin = np.empty_like(comp)
    for b in self.order:
        p = self.parent[b]
        skin[..., b, :, :] = comp[..., b, :, :] if p < 0 else skin[..., p, :, :] @ comp[..., b, :, :]
    return skin

Gla.comp_params = comp_params
Gla.chain_skin = chain_skin

def read_cfg(path=CFG):
    seqs = {}
    for line in open(path, encoding='latin1'):
        line = line.split('//')[0].split()
        if len(line) >= 5:
            try: seqs[line[0]] = (int(line[1]), int(line[2]), int(line[3]), int(line[4]))
            except ValueError: pass
    return seqs
