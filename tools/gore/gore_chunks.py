"""Meat chunks for make_gore_assets.py: lumpy procedural MD3 models (cg_gore.c throws them
from explosive kills). Each is a noise-displaced icosphere, squashed at random, skinned with
a torn-flesh texture (gfx/xen_gore/chunk). Sizes are in game units (about 36 to a metre).
"""
import math
import struct

import numpy as np


def icosphere(subdiv):
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    verts = [np.array(p, float) / np.linalg.norm(p) for p in v]
    for _ in range(subdiv):
        cache, nf = {}, []

        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                m = verts[a] + verts[b]
                verts.append(m / np.linalg.norm(m))
                cache[k] = len(verts) - 1
            return cache[k]
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    return np.array(verts), np.array(f, np.int32)


def lumpy(dirs, rng, size):
    """Radius per direction: a few random lobes and dents, plus a fine ripple."""
    r = np.ones(len(dirs))
    for _ in range(7):
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        r += rng.uniform(-0.25, 0.35) * np.exp(-((1 - dirs @ d) / rng.uniform(0.15, 0.5)))
    for _ in range(5):
        k = rng.normal(size=3) * rng.uniform(3, 6)
        r += 0.04 * np.sin(dirs @ k + rng.uniform(0, 6.3))
    r = np.clip(r, 0.55, 1.7)
    squash = rng.uniform([0.8, 0.6, 0.45], [1.3, 1.0, 0.75])     # flatter than round: torn, not balls
    return dirs * r[:, None] * squash[None, :] * size


def vertex_normals(xyz, faces):
    n = np.zeros_like(xyz)
    fn = np.cross(xyz[faces[:, 1]] - xyz[faces[:, 0]], xyz[faces[:, 2]] - xyz[faces[:, 0]])
    for i in range(3):
        np.add.at(n, faces[:, i], fn)
    return n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)


def encode_normal(n):
    """Q3 MD3 latitude/longitude bytes (lng from +z, lat round z)."""
    lat = (np.round(np.arctan2(n[:, 1], n[:, 0]) * 255 / (2 * math.pi)).astype(int)) & 0xff
    lng = (np.round(np.arccos(np.clip(n[:, 2], -1, 1)) * 255 / (2 * math.pi)).astype(int)) & 0xff
    return (lat << 8) | lng


def md3(name, xyz, faces, st, shader):
    nv, nt = len(xyz), len(faces)
    normals = encode_normal(vertex_normals(xyz, faces))
    # Q3 winds front faces clockwise seen from outside: the reverse of the cross product above
    tris = faces[:, ::-1]
    s_hdr = struct.calcsize('<4s64s10i')
    ofs_shaders = s_hdr
    ofs_tris = ofs_shaders + 68
    ofs_st = ofs_tris + nt * 12
    ofs_xyz = ofs_st + nv * 8
    s_end = ofs_xyz + nv * 8
    surf = struct.pack('<4s64s10i', b'IDP3', b'chunk', 0, 1, 1, nv, nt, ofs_tris, ofs_shaders, ofs_st, ofs_xyz, s_end)
    surf += struct.pack('<64si', shader.encode(), 0)
    surf += tris.astype('<i4').tobytes()
    surf += st.astype('<f4').tobytes()
    q = np.clip(np.round(xyz * 64), -32768, 32767).astype(int)
    surf += b''.join(struct.pack('<hhhH', *q[i], normals[i]) for i in range(nv))
    assert len(surf) == s_end
    lo, hi = xyz.min(0), xyz.max(0)
    frame = struct.pack('<3f3f3ff16s', *lo, *hi, 0, 0, 0, float(np.linalg.norm(xyz, axis=1).max()), b'chunk')
    hdr_size = 4 + 4 + 64 + 4 * 9
    ofs_frames = hdr_size
    ofs_surfs = ofs_frames + len(frame)
    end = ofs_surfs + len(surf)
    hdr = struct.pack('<4si64s9i', b'IDP3', 15, name.encode(), 0, 1, 0, 1, 0, ofs_frames, ofs_surfs, ofs_surfs, end)
    assert len(hdr) == hdr_size
    return hdr + frame + surf


def chunk(name, rng, size, shader='gfx/xen_gore/chunk'):
    dirs, faces = icosphere(2)
    xyz = lumpy(dirs, rng, size)
    # texture: one tile wrapped round the chunk (latitude/longitude; the seam hides in the grain)
    st = np.stack([np.arctan2(dirs[:, 1], dirs[:, 0]) / (2 * math.pi) * 1.5,
                   np.arccos(np.clip(dirs[:, 2], -1, 1)) / math.pi * 0.75], 1)
    return md3(name, xyz, faces, st, shader)
