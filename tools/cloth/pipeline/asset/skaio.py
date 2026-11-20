"""SKA1/SKA2 (cloth asset) and SKP1 (pose reel) readers/writers. Format: see docs/cloth/FORMAT.md."""
import struct, zlib, json
import numpy as np

HEADER = '<4sII d10I 11d 32s'

def write_ska(path, a):
    """a: dict with unit_cm, ref_bone, bone_names, refInv (nb,4,4 row-vector), pos, tri, maxDist, animDrive, skinB, skinW (n,ni),
    col_pos, col_tri, col_skinB, col_skinW, capA, capB, capR, config(11), skeleton name, and either
    render=[(name, sim_idx u32[], dist f32[])] (version 1) or
    render_wrap=[(name, tri u32[n,3], w f32[n,3], off f32[n], sgn f32[n])] plus params="key value\n..." (version 2: each render
    vertex wrapped on three particles, barycentric weights, an offset in glm units along the interpolated sim normal, a
    normal sign; solver parameters for this garment)"""
    nb = len(a['bone_names']); n = len(a['pos']); ni = a['skinB'].shape[1]
    wrap = a.get('render_wrap')
    rs = wrap if wrap is not None else a.get('render', [])
    nrv = sum(len(x[1]) for x in rs)
    out = bytearray()
    out += struct.pack('<4sII', b'SKA1', 2 if wrap is not None else 1, 0)
    out += struct.pack('<d10I', a['unit_cm'], nb, a['ref_bone'], n, len(a['tri']), ni, len(a['col_pos']), len(a['col_tri']), len(a['capA']), len(rs), nrv)
    out += struct.pack('<11d', *a['config'])
    out += struct.pack('<32s', a.get('skeleton', '').encode())
    struct.pack_into('<I', out, 8, len(out))
    def put(x, dt): out.extend(np.ascontiguousarray(x, dtype=dt).tobytes())
    for nm in a['bone_names']: out += nm.encode().ljust(32, b'\0')[:32]
    put(a['refInv'], '<f4')
    put(a['pos'], '<f4'); put(a['tri'], '<u4'); put(a['maxDist'], '<f4'); put(a['animDrive'], '<f4'); put(a['skinB'], '<i4'); put(a['skinW'], '<f4')
    put(a['col_pos'], '<f4'); put(a['col_tri'], '<u4'); put(a['col_skinB'], '<i4'); put(a['col_skinW'], '<f4')
    put(a['capA'], '<i4'); put(a['capB'], '<i4')
    while len(out) % 8: out += b'\0'
    put(a['capR'], '<f8')
    for x in rs: out += x[0].encode().ljust(32, b'\0') + struct.pack('<I', len(x[1]))
    while len(out) % 4: out += b'\0'
    if wrap is None:
        for nm, m, dd in rs: put(m, '<u4')
        for nm, m, dd in rs: put(dd, '<f4')
    else:
        for x in rs: put(x[1], '<u4')
        for x in rs: put(x[2], '<f4')
        for x in rs: put(x[3], '<f4')
        for x in rs: put(x[4], '<f4')
        pb = a.get('params', '').encode()
        out += struct.pack('<I', len(pb)) + pb
        while len(out) % 4: out += b'\0'
    out += struct.pack('<I', zlib.crc32(bytes(out)) & 0xffffffff)
    open(path, 'wb').write(bytes(out))
    return len(out)

def read_ska(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'SKA1'
    ver, hb = struct.unpack_from('<II', b, 4)
    assert ver in (1, 2)
    o = 12
    unit, nb, ref, n, nt, ni, ncv, nct, ncap, nrs, nrv = struct.unpack_from('<d10I', b, o); o += 48
    config = struct.unpack_from('<11d', b, o); o += 88
    skel = struct.unpack_from('<32s', b, o)[0].split(b'\0')[0].decode(); o += 32
    assert o == hb
    crc = struct.unpack_from('<I', b, len(b) - 4)[0]
    assert crc == zlib.crc32(b[:-4]) & 0xffffffff, 'crc mismatch'
    def take(dt, *shape):
        nonlocal o
        cnt = int(np.prod(shape)); a = np.frombuffer(b, dtype=dt, count=cnt, offset=o).reshape(shape); o += a.nbytes; return a
    names = [b[o + 32 * i:o + 32 * i + 32].split(b'\0')[0].decode() for i in range(nb)]; o += 32 * nb
    d = dict(unit_cm=unit, ref_bone=ref, skeleton=skel, config=config, bone_names=names)
    d['refInv'] = take('<f4', nb, 4, 4)
    d['pos'] = take('<f4', n, 3); d['tri'] = take('<u4', nt, 3); d['maxDist'] = take('<f4', n); d['animDrive'] = take('<f4', n)
    d['skinB'] = take('<i4', n, ni); d['skinW'] = take('<f4', n, ni)
    d['col_pos'] = take('<f4', ncv, 3); d['col_tri'] = take('<u4', nct, 3); d['col_skinB'] = take('<i4', ncv, ni); d['col_skinW'] = take('<f4', ncv, ni)
    d['capA'] = take('<i4', ncap); d['capB'] = take('<i4', ncap)
    o = (o + 7) & ~7
    d['capR'] = take('<f8', ncap)
    rs = []
    for i in range(nrs):
        rs.append((b[o + 36 * i:o + 36 * i + 32].split(b'\0')[0].decode(), struct.unpack_from('<I', b, o + 36 * i + 32)[0]))
    o += 36 * nrs; o = (o + 3) & ~3
    if ver == 1:
        maps = []
        for nm, c in rs: maps.append(take('<u4', c))
        dists = []
        for nm, c in rs: dists.append(take('<f4', c))
        d['render'] = [(rs[i][0], maps[i], dists[i]) for i in range(nrs)]
    else:
        tri = [take('<u4', c, 3) for nm, c in rs]; w = [take('<f4', c, 3) for nm, c in rs]
        off = [take('<f4', c) for nm, c in rs]; sgn = [take('<f4', c) for nm, c in rs]
        d['render_wrap'] = [(rs[i][0], tri[i], w[i], off[i], sgn[i]) for i in range(nrs)]
        plen = struct.unpack_from('<I', b, o)[0]; o += 4
        d['params'] = b[o:o + plen].decode(); o += plen; o = (o + 3) & ~3
    assert o + 4 == len(b)
    return d

def write_skp(path, nb, fps, clips, cst, dts, extra=None):
    """cst (F,nb,4,4) float32 row-vector, dts (F,)"""
    meta = dict(version=1, nBones=nb, fps=fps, clips=clips)
    if extra: meta.update(extra)
    mj = json.dumps(meta).encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b'SKP1' + struct.pack('<II', 1, len(mj)) + mj)
        for i in range(len(cst)):
            f.write(struct.pack('<IfI', i, float(dts[i]), 1)); f.write(np.ascontiguousarray(cst[i], dtype='<f4').tobytes())
