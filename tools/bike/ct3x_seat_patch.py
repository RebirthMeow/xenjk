"""Move the CT-3X's rider seat tag (*driver) in a built bike pack into a new pack: no model regeneration.

The rider's origin is the *driver tag (AttachRidersGeneric), which ct3x_model.py puts on CP2077's seat_front bone.
With the retargeted CP2077 rider clips that sits the rider too far back (cgame cg_bikeSeatDebug, 2B riding: hands
4.8 u behind the grips, pelvis 6.7 u behind the tag). This adds a GLM-frame offset (forward is -y, left +x, up +z, model units) to the tag triangle's vertices in every LOD and
copies every other pack entry byte for byte.

  python -I ct3x_seat_patch.py IN.pk3 OUT.pk3 [--forward U] [--up U]
"""
from __future__ import annotations

import argparse
import os
import struct
import zipfile

HDR = struct.Struct('<4si64s64s7i')        # ident, version, name, anim, animIndex, numBones, numLODs, ofsLODs, numSurfaces,
                                           # ofsSurfHierarchy, ofsEnd
SURF = struct.Struct('<10i')               # ident, thisSurfaceIndex, ofsHeader, numVerts, ofsVerts, numTriangles,
                                           # ofsTriangles, numBoneReferences, ofsBoneReferences, ofsEnd
GLM = 'models/players/ct3x/model.glm'
TAG = '*driver'


def patch_glm(b: bytes, offset: tuple[float, float, float]) -> tuple[bytes, int]:
    b = bytearray(b)
    ident, ver, _name, _anim, _ai, _nb, num_lods, ofs_lods, num_surf, _oh, _oe = HDR.unpack_from(b, 0)
    assert ident == b'2LGM' and ver == 6, (ident, ver)
    offs = struct.unpack_from('<%di' % num_surf, b, HDR.size)
    names = [bytes(b[HDR.size + o:HDR.size + o + 64]).split(b'\0')[0].decode('latin1') for o in offs]
    idx = names.index(TAG)
    p, moved = ofs_lods, 0
    for _ in range(num_lods):
        lod_end, = struct.unpack_from('<i', b, p)
        so = struct.unpack_from('<%di' % num_surf, b, p + 4)
        s = p + 4 + so[idx]
        _id, sidx, _oh2, nv, ov = SURF.unpack_from(b, s)[:5]
        assert sidx == idx, (sidx, idx)
        for i in range(nv):
            v = s + ov + 32 * i + 12
            xyz = struct.unpack_from('<3f', b, v)
            struct.pack_into('<3f', b, v, *(a + d for a, d in zip(xyz, offset)))
            moved += 1
        p += lod_end
    return bytes(b), moved


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--forward', type=float, default=5.0, help='u toward the front wheel')
    ap.add_argument('--up', type=float, default=0.0)
    a = ap.parse_args()
    offset = (0.0, -a.forward, a.up)
    if os.path.exists(a.dst) and os.path.samefile(a.src, a.dst):
        raise SystemExit('IN and OUT are the same file: writing OUT would truncate the pack being read')
    with zipfile.ZipFile(a.src) as zi, zipfile.ZipFile(a.dst, 'w') as zo:
        for info in zi.infolist():
            data = zi.read(info)
            if info.filename == GLM:
                data, moved = patch_glm(data, offset)
                print(f'{GLM}: {TAG} moved {offset} u ({moved} vertices over all LODs)')
            zo.writestr(info, data, compress_type=info.compress_type)


if __name__ == '__main__':
    main()
