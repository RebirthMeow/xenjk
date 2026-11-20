"""chainsio.py -- read/write the hair-chains asset (HCH1, docs/hair/FORMAT.md) and the offline replay reel (HCR1)."""
import struct
import zlib

import numpy as np

HEADER = 128
P_FMT = '<2i10f'          # particle
C_FMT = '<3i7f'           # cone
L_FMT = '<2i3f'           # link
R_FMT = '<3i2f'           # receiver
RP_FMT = '<4i8f'          # repulsor: n, game bone x2, flags (1 = inverse/containment shape)
B_FMT = '<32si16f'        # bone
RS_FMT = '<32sii'         # render surface: GLM surface name, vertex count, LOD
RV_FMT = '<2i4i4f'        # render entry: surface, vertex, 4 bones, 4 weights
FLAG_POINTS_PER_FRAME = 1  # offline reels only: hair_asset.c refuses a repulsor game bone below 0
FLAG_HONOUR_INVERSE = 2     # asset: containment shapes keep particles inside (Route A pushes out of them)
REP_INVERSE = 1


FLAG_BONE_HOSTS = 4   # a per-bone host table follows the render entries: root bones hang from game bones
FLAG_OWN_SKIN = 8     # a render entry's bone -1 share follows the vertex's own GLM skinning (else the host bone)
FLAG_SPRINGS = 16     # a spring-bone block follows the bone hosts
FLAG_GAME_REFS = 32   # a render entry bone <= -2 is game bone -2 - bone
SP_HEAD_FMT = '<if9f'     # spring block: count, dt, basis (row-major 3x3: source axes = basis x asset axes)
SP_FMT = '<2i8f'          # spring: bone, flags (1/2/4 translate x/y/z, 8/16/32 rotate x/y/z, source axes; bits 8..11
                          # body group: 0 rest, 1 breasts, 2 hips / buttocks and upper thighs), stiffness,
                          # damping, max displacement (units, 0 = none), reset threshold (units), alpha, pivot[3] (bind)


def inverse_flag(groups):
    return REP_INVERSE if any(g.lower().startswith('inverse') for g in groups) else 0


def _name(s):
    # the engine compares these names in full against GLM surface and GLA bone names (64 bytes); a cut name
    # never matches, and the character would draw rigid with only a run-time warning. Refuse instead of cutting.
    b = s.encode('latin1')
    if len(b) > 31:
        raise ValueError('name %r is %d bytes; the HCH1 name field holds 31' % (s, len(b)))
    return b + b'\0' * (32 - len(b))


def write_asset(path, a):
    """a: dict with keys as in read_asset(); matrices are 4x4 column-major lists/arrays of 16."""
    parts = []
    for p in a['particles']:
        parts.append(struct.pack(P_FMT, p['bone'], p['group'], p['radius'], p['gravity_scale'], p['damping'],
                                 p['velocity_damping'], p['constraint_damping'], p['friction'], p['bounciness'],
                                 p['park_rate'], p['fictitious'], p['centrifugal']))
    for c in a['cones']:
        parts.append(struct.pack(C_FMT, c['child'], c['parent'], c['anchor_bone'], c['limit'], c['inner'], c['outer'],
                                 c['stretch'], c['compress'], c['parent_strength'], c['rest']))
    for l in a['links']:
        parts.append(struct.pack(L_FMT, l['a'], l['b'], l['stretch'], l['compress'], l['rest']))
    for r in a['receivers']:
        p = list(r['particles']) + [r['particles'][-1]] * (2 - len(r['particles']))
        rr = list(r['radii']) + [r['radii'][-1]] * (2 - len(r['radii']))
        parts.append(struct.pack(R_FMT, len(r['particles']), p[0], p[1], rr[0], rr[1]))
    for r in a['repulsors']:
        e = r['entries'] + [r['entries'][-1]] * (2 - len(r['entries']))
        parts.append(struct.pack(RP_FMT, len(r['entries']), e[0]['game_bone'], e[1]['game_bone'], r.get('flags', 0), *e[0]['bind'],
                                 *e[1]['bind'], e[0]['radius'], e[1]['radius']))
    for ri, pi in a['pairs']:
        parts.append(struct.pack('<2i', ri, pi))
    for b in a['bones']:
        parts.append(struct.pack(B_FMT, _name(b['name']), b['parent'], *[float(x) for x in b['bind']]))
    for t, d in a['mirrors']:
        parts.append(struct.pack('<2i', t, d))
    for n in a.get('game_bones', []):      # v2: repulsor game bones index this name table (bound by name at load)
        parts.append(_name(n))
    for s in a.get('render_surfaces', []):
        parts.append(struct.pack(RS_FMT, _name(s['name']), s['count'], s.get('lod', 0)))
    rw = a.get('render_entries')
    if rw is not None:
        for sfi, vi, bi, w in zip(*rw):
            parts.append(struct.pack(RV_FMT, int(sfi), int(vi), *[int(x) for x in bi], *[float(x) for x in w]))
    if a.get('flags', 0) & FLAG_BONE_HOSTS:   # per bone: -1 = the header's host, else a game bone index (roots only)
        hosts = a['bone_hosts']
        assert len(hosts) == len(a['bones'])
        parts.append(struct.pack('<%di' % len(hosts), *[int(x) for x in hosts]))
    if a.get('flags', 0) & FLAG_SPRINGS:
        sp = a['springs']
        parts.append(struct.pack(SP_HEAD_FMT, len(sp['springs']), sp['dt'], *[float(x) for x in sp['basis']]))
        for s in sp['springs']:
            parts.append(struct.pack(SP_FMT, s['bone'], s['flags'], s['stiffness'], s['damping'], s['max_displacement'],
                                     s['reset_thresh'], s['alpha'], *[float(x) for x in s['pivot']]))
    body = b''.join(parts)
    g = a['gravity']
    head = struct.pack('<4sII12i5f2i32sII', b'HCH1', 2, HEADER,
                       len(a['particles']), len(a['cones']), len(a['links']), len(a['receivers']), len(a['repulsors']),
                       len(a['pairs']), len(a['bones']), len(a['mirrors']), a['host_bone'], a['substeps'],
                       a['lateral_iterations'], a['collision_iterations'],
                       a['dt'], g[0], g[1], g[2], a['units_per_cm'],
                       len(a.get('render_surfaces', [])), 0 if rw is None else len(rw[0]),
                       _name(a['host_bone_name']), a.get('flags', 0), len(a.get('game_bones', [])))
    assert len(head) == HEADER, len(head)
    data = head + body
    with open(path, 'wb') as f:
        f.write(data + struct.pack('<I', zlib.crc32(data) & 0xffffffff))
    return len(data) + 4


def read_asset(path):
    f = open(path, 'rb').read()
    assert struct.unpack_from('<I', f, len(f) - 4)[0] == zlib.crc32(f[:-4]) & 0xffffffff, 'crc'
    (magic, ver, hb, nP, nC, nL, nR, nRp, nPr, nB, nM, host, sub, lat, col, dt, gx, gy, gz, upc, nRS, nRV, hname, flags,
     nGB) = struct.unpack_from('<4sII12i5f2i32sII', f, 0)
    assert magic == b'HCH1' and ver == 2 and hb == HEADER
    o = HEADER
    out = dict(host_bone=host, host_bone_name=hname.split(b'\0')[0].decode('latin1'), substeps=sub,
               lateral_iterations=lat, collision_iterations=col, dt=dt, gravity=[gx, gy, gz], units_per_cm=upc,
               flags=flags)

    def take(fmt, n):
        nonlocal o
        sz = struct.calcsize(fmt)
        r = [struct.unpack_from(fmt, f, o + i * sz) for i in range(n)]
        o += sz * n
        return r
    out['particles'] = take(P_FMT, nP)
    out['cones'] = take(C_FMT, nC)
    out['links'] = take(L_FMT, nL)
    out['receivers'] = take(R_FMT, nR)
    out['repulsors'] = take(RP_FMT, nRp)
    out['pairs'] = take('<2i', nPr)
    out['bones'] = [(n.split(b'\0')[0].decode('latin1'), p, m) for n, p, *m in take(B_FMT, nB)]
    out['mirrors'] = take('<2i', nM)
    out['game_bones'] = [x[0].split(b'\0')[0].decode('latin1') for x in take('<32s', nGB)]
    out['render_surfaces'] = [(n.split(b'\0')[0].decode('latin1'), c, l) for n, c, l in take(RS_FMT, nRS)]
    out['render_entries'] = take(RV_FMT, nRV)
    if flags & FLAG_BONE_HOSTS:
        out['bone_hosts'] = [x[0] for x in take('<i', nB)]
    if flags & FLAG_SPRINGS:
        n, dt, *basis = take(SP_HEAD_FMT, 1)[0]
        out['springs'] = dict(dt=dt, basis=basis, springs=take(SP_FMT, n))
    assert o == len(f) - 4, (o, len(f))
    return out


def colmajor(m_rows):
    """Blender/numpy row-major 4x4 (m[r][c]) -> 16 floats column-major."""
    return np.asarray(m_rows, np.float64).reshape(4, 4).T.ravel()


def write_reel(path, steps, pose, init_pos, reset_frame, n_preroll):
    """steps/pose: dicts of numpy arrays (see export_reel.py); matrices already column-major (..., 16)."""
    nS, nP = steps['targets'].shape[:2]
    nC = steps['anchors'].shape[1]
    nRp = steps['rep'].shape[1]
    nF, nB = pose['anim'].shape[:2]
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sI8i', b'HCR1', 1, nS, nP, nC, nRp, nF, nB, reset_frame, n_preroll))
        f.write(np.ascontiguousarray(init_pos, '<f8').tobytes())
        for i in range(nS):
            f.write(struct.pack('<2i', int(steps['frame'][i]), 0))
            for k in ('ref', 'targets', 'anchors', 'rep', 'pre_pos', 'pre_prev', 'post_pos', 'post_prev', 'frame_acc',
                      'ang_vel', 'gravity_local'):
                f.write(np.ascontiguousarray(steps[k][i], '<f8').tobytes())
        for i in range(nF):
            for k in ('ref', 'anim', 'posed', 'sim'):
                f.write(np.ascontiguousarray(pose[k][i], '<f8').tobytes())
