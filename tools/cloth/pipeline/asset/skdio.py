"""Reader for SKD1 traces (format: ClothResearch/skirt/chaos_dump.js). Numpy, UTF-8."""
import json, struct, numpy as np

def read_skd(path, frames=False):
    f = open(path, 'rb')
    assert f.read(4) == b'SKD1'
    ver, jb = struct.unpack('<II', f.read(8))
    meta = json.loads(f.read(jb).decode('utf-8'))
    NB, N, NT = meta['nBones'], meta['nVerts'], meta['nTris']
    rd = lambda dt, n: np.frombuffer(f.read(n * np.dtype(dt).itemsize), dtype=dt)
    d = dict(meta=meta)
    d['refInv'] = rd('<f4', NB * 16).reshape(NB, 4, 4)
    d['pos'] = rd('<f4', N * 3).reshape(N, 3)
    d['nrm'] = rd('<f4', N * 3).reshape(N, 3)
    d['tri'] = rd('<u4', NT * 3).reshape(NT, 3)
    d['maxDist'] = rd('<f4', N)
    d['animDrive'] = rd('<f4', N)
    d['skinB'] = rd('<i4', N * 12).reshape(N, 12)
    d['skinW'] = rd('<f4', N * 12).reshape(N, 12)
    d['trimeshes'] = []
    for t in meta['trimeshes']:
        v, tt = t['verts'], t['tris']
        d['trimeshes'].append(dict(bone=t.get('bone'), pos=rd('<f4', v * 3).reshape(v, 3), tri=rd('<u4', tt * 3).reshape(tt, 3),
                                   skinB=rd('<i4', v * 12).reshape(v, 12), skinW=rd('<f4', v * 12).reshape(v, 12)))
    d['headerBytes'] = f.tell()
    f.close()
    return d
