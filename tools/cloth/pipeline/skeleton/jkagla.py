"""Full GLA (2LGA v6) reader/writer with the engine's compressed-quat bone pool (numpy, vectorised).
Engine semantics (rd-vanilla tr_ghoul2.cpp G2_TransformBone): per frame and bone the pool entry A is a 3x4 matrix;
final skin matrix F_root = A_root, F_child = F_parent * A_child; a vertex is skinned as F * v_rest.
Pool entry = 7 uint16: quat w,x,y,z as (q+2)*16383, translation as (t+512)*64 (matcomp.cpp MC_UnCompressQuat)."""
import struct, numpy as np

HDR = '4si64sf6i'   # 100 bytes


def _s(b): return b.split(b'\0')[0].decode('latin1')


def read(path):
    f = open(path, 'rb').read()
    ident, ver, name, scale, nfr, ofsfr, nb, ofspool, ofsskel, ofsend = struct.unpack_from(HDR, f, 0)
    assert ident == b'2LGA' and ver == 6
    offs = struct.unpack_from('%di' % nb, f, 100)
    bones = []
    for i, o in enumerate(offs):
        p = 100 + o
        nm = _s(f[p:p + 64]); flags, parent = struct.unpack_from('Ii', f, p + 64)
        m = np.array(struct.unpack_from('12f', f, p + 72)).reshape(3, 4)
        mi = np.array(struct.unpack_from('12f', f, p + 120)).reshape(3, 4)
        nch, = struct.unpack_from('i', f, p + 168)
        ch = list(struct.unpack_from('%di' % nch, f, p + 172))
        bones.append(dict(name=nm, flags=flags, parent=parent, base=m, baseinv=mi, children=ch))
    idx = np.frombuffer(f, np.uint8, nfr * nb * 3, ofsfr).reshape(nfr, nb, 3).astype(np.int64)
    idx = idx[..., 0] | (idx[..., 1] << 8) | (idx[..., 2] << 16)
    npool = (ofsend - ofspool) // 14
    pool = np.frombuffer(f, '<u2', npool * 7, ofspool).reshape(npool, 7).copy()
    return dict(name=_s(name), scale=scale, numFrames=nfr, numBones=nb, bones=bones, idx=idx, pool=pool)


def decode_pool(pool):
    """pool (n,7) uint16 -> (n,3,4) float64 matrices exactly as MC_UnCompressQuat (in float64)."""
    q = pool[:, :4].astype(np.float64) / 16383.0 - 2.0
    w, x, y, z = q.T
    m = np.empty((len(pool), 3, 4))
    m[:, 0, 0] = 1 - 2 * (y * y + z * z); m[:, 0, 1] = 2 * x * y - 2 * w * z; m[:, 0, 2] = 2 * x * z + 2 * w * y
    m[:, 1, 0] = 2 * x * y + 2 * w * z; m[:, 1, 1] = 1 - 2 * (x * x + z * z); m[:, 1, 2] = 2 * y * z - 2 * w * x
    m[:, 2, 0] = 2 * x * z - 2 * w * y; m[:, 2, 1] = 2 * y * z + 2 * w * x; m[:, 2, 2] = 1 - 2 * (x * x + y * y)
    m[:, :, 3] = pool[:, 4:].astype(np.float64) / 64.0 - 512.0
    return m


def mat_to_quat(R):
    """(n,3,3) rotation -> (n,4) w,x,y,z (w >= 0)."""
    n = len(R); q = np.empty((n, 4))
    tr = R[:, 0, 0] + R[:, 1, 1] + R[:, 2, 2]
    c0 = tr > 0
    c1 = ~c0 & (R[:, 0, 0] >= R[:, 1, 1]) & (R[:, 0, 0] >= R[:, 2, 2])
    c2 = ~c0 & ~c1 & (R[:, 1, 1] >= R[:, 2, 2])
    c3 = ~c0 & ~c1 & ~c2
    r = R
    s = np.sqrt(np.maximum(tr[c0] + 1, 1e-30)) * 2
    q[c0] = np.stack([0.25 * s, (r[c0, 2, 1] - r[c0, 1, 2]) / s, (r[c0, 0, 2] - r[c0, 2, 0]) / s, (r[c0, 1, 0] - r[c0, 0, 1]) / s], 1)
    s = np.sqrt(np.maximum(1 + r[c1, 0, 0] - r[c1, 1, 1] - r[c1, 2, 2], 1e-30)) * 2
    q[c1] = np.stack([(r[c1, 2, 1] - r[c1, 1, 2]) / s, 0.25 * s, (r[c1, 0, 1] + r[c1, 1, 0]) / s, (r[c1, 0, 2] + r[c1, 2, 0]) / s], 1)
    s = np.sqrt(np.maximum(1 + r[c2, 1, 1] - r[c2, 0, 0] - r[c2, 2, 2], 1e-30)) * 2
    q[c2] = np.stack([(r[c2, 0, 2] - r[c2, 2, 0]) / s, (r[c2, 0, 1] + r[c2, 1, 0]) / s, 0.25 * s, (r[c2, 1, 2] + r[c2, 2, 1]) / s], 1)
    s = np.sqrt(np.maximum(1 + r[c3, 2, 2] - r[c3, 0, 0] - r[c3, 1, 1], 1e-30)) * 2
    q[c3] = np.stack([(r[c3, 1, 0] - r[c3, 0, 1]) / s, (r[c3, 0, 2] + r[c3, 2, 0]) / s, (r[c3, 1, 2] + r[c3, 2, 1]) / s, 0.25 * s], 1)
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    q[q[:, 0] < 0] *= -1
    return q


def encode(M):
    """(n,3,4) rigid matrices -> (n,7) uint16 pool entries."""
    q = mat_to_quat(M[:, :, :3])
    out = np.empty((len(M), 7), np.int64)
    out[:, :4] = np.rint((q + 2.0) * 16383.0)
    t = np.rint((M[:, :, 3] + 512.0) * 64.0)
    if (t < 0).any() or (t > 65535).any(): raise ValueError('translation out of +-512 range')
    out[:, 4:] = t
    return np.clip(out, 0, 65535).astype(np.uint16)


def write(path, name, scale, bones, idx, pool):
    """bones: list of dict(name, flags, parent, base(3x4), baseinv(3x4), children). idx (nfr,nb) ints into pool (n,7)."""
    nfr, nb = idx.shape
    blobs = []
    for b in bones:
        bb = struct.pack('64sIi', b['name'].encode(), b['flags'], b['parent'])
        bb += struct.pack('12f', *np.asarray(b['base'], np.float64).reshape(-1))
        bb += struct.pack('12f', *np.asarray(b['baseinv'], np.float64).reshape(-1))
        bb += struct.pack('i', len(b['children'])) + struct.pack('%di' % len(b['children']), *b['children'])
        blobs.append(bb)
    offs = []; o = 4 * nb
    for bb in blobs: offs.append(o); o += len(bb)
    ofsskel = 100 + 4 * nb
    ofsfr = 100 + o
    fr = np.empty((nfr, nb, 3), np.uint8)
    assert idx.max() < (1 << 24)
    fr[..., 0] = idx & 255; fr[..., 1] = (idx >> 8) & 255; fr[..., 2] = (idx >> 16) & 255
    frb = fr.tobytes(); frb += b'\0' * ((-len(frb)) % 4)
    ofspool = ofsfr + len(frb)
    poolb = np.ascontiguousarray(pool, '<u2').tobytes()
    ofsend = ofspool + len(poolb)
    out = struct.pack(HDR, b'2LGA', 6, name.encode(), scale, nfr, ofsfr, nb, ofspool, ofsskel, ofsend)
    out += struct.pack('%di' % nb, *offs) + b''.join(blobs) + frb + poolb
    assert len(out) == ofsend
    open(path, 'wb').write(out)
    return ofsend
