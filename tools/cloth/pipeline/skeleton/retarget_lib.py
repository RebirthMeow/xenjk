import numpy as np, jkagla

def h4(M):
    o = np.zeros(M.shape[:-2] + (4, 4)); o[..., :3, :] = M; o[..., 3, 3] = 1; return o

def quat_to_mat(q):
    q = q / np.linalg.norm(q, axis=-1, keepdims=True)
    w, x, y, z = np.moveaxis(q, -1, 0)
    R = np.empty(q.shape[:-1] + (3, 3))
    R[..., 0, 0] = 1 - 2 * (y * y + z * z); R[..., 0, 1] = 2 * (x * y - w * z); R[..., 0, 2] = 2 * (x * z + w * y)
    R[..., 1, 0] = 2 * (x * y + w * z); R[..., 1, 1] = 1 - 2 * (x * x + z * z); R[..., 1, 2] = 2 * (y * z - w * x)
    R[..., 2, 0] = 2 * (x * z - w * y); R[..., 2, 1] = 2 * (y * z + w * x); R[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return R

def pool_rigid(pool):
    """unit-normalised rigid 4x4 per pool entry"""
    M = np.zeros((len(pool), 4, 4)); M[:, 3, 3] = 1
    M[:, :3, :3] = quat_to_mat(pool[:, :4].astype(np.float64) / 16383.0 - 2.0)
    M[:, :3, 3] = pool[:, 4:].astype(np.float64) / 64.0 - 512.0
    return M

def rigid_base(b):
    B = b['base']; s = np.cbrt(np.linalg.det(B[:, :3]))
    M = np.eye(4); M[:3, :3] = B[:, :3] / s; M[:3, 3] = B[:, 3]
    return M, s

def skin_mats(A, parents):
    """A (nfr,nb,4,4) -> F (nfr,nb,4,4) chained"""
    F = np.empty_like(A)
    for i in topo(parents):
        p = parents[i]
        F[:, i] = A[:, i] if p < 0 else F[:, p] @ A[:, i]
    return F

def topo(parents):
    done = []; seen = set()
    def visit(i):
        if i in seen: return
        if parents[i] >= 0: visit(parents[i])
        seen.add(i); done.append(i)
    for i in range(len(parents)): visit(i)
    return done
