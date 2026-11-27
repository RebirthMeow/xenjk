# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""FF7 Rebirth Massive Environment streams -> numpy meshes with every LOD, instances and texture lists. No Blender.

A level's terrain, vegetation and interiors (`*_Terrain_Strip` maps) are one MassiveEnvironmentComponent each: groups of
mesh types (each one LOD chain of submeshes and one material), clusters (an object: a run of mesh types) and instances
(a cluster at a position and rotation; no scale). The geometry lives in the map's .ubulk. The layout was learned from the
community Blender add-on FF7R-umap-addon (`mec/parser.py`); this is an independent reader of the same fields.

Inputs, per stream (a research extraction, FF7R_Research/Costa_Del_Sol_Extracted):
  the component's native tail as CUE4Parse leaves it unread (`--inspect-world`, MassiveEnvironmentComponent0_*.bin),
  the map's .ubulk (absent: an empty stream), and the map's resolved import map (`--dump-import-map`): material
  references are negative import indices whose objects are the textures.

Output: <out>/<stream>.npz (pos f32 [N,3] in source cm with Y negated, i.e. Quake's axes; nrm f32 [N,3]; uv f32 [N,2]
(uv0, as stored: top-left origin, as glTF); uv1 when present; idx u32 [M] relative to each LOD's first vertex) and <stream>.json:
  meshes: [{name, textures: [game paths], lods: [{v0, vn, i0, in}]}]   (one per mesh type with geometry)
  instances: [{mesh, pos: [x,y,z] cm (Quake axes), quat: [w,x,y,z] (Quake axes)}]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np


class Reader:
    def __init__(self, data, at=0):
        self.d, self.p = data, at

    def i32(self, n=None):
        if n is None:
            v = struct.unpack_from('<i', self.d, self.p)[0]
            self.p += 4
            return v
        v = np.frombuffer(self.d, '<i4', n, self.p).copy()
        self.p += 4 * n
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]
        self.p += 4
        return v

    def skip(self, n):
        self.p += n


def read_stream(tail, bulk, import_names):
    """(meshes, instances, arrays) of one component. tail: the captured native bytes; the native data starts with an
    int32 the capture lacks (unused), then one unknown int32, then the group count."""
    data = b'\0\0\0\0' + tail + bulk
    base0 = 4 + len(tail)            # bulk offsets count from here
    r = Reader(data, 0)
    r.skip(4)                        # the unused int32 the capture lacks
    r.i32()                          # (unknown)
    groups = r.i32()
    meshes, instances = [], []
    P, N, U, U1, I = [], [], [], [], []
    vbase = ibase = 0
    for g in range(groups):
        r.i32(); r.skip(32)
        r.i32(); r.i32()
        bulk_off = r.i32()
        r.i32()
        a = r.i32(); refs = list(r.i32(a)) if a else []
        b = r.i32(); refs += list(r.i32(b)) if b else []
        bmin = np.array([r.f32(), r.f32(), r.f32()]); bmax = np.array([r.f32(), r.f32(), r.f32()])
        centre = (bmin + bmax) * 0.5
        r.skip(1 + 28)
        tg_n = r.i32(); tg_off = r.i32()
        r.skip(13 * 4)
        vi_n = r.i32(); vi_off = r.i32(); r.i32()
        pt_n = r.i32(); pt_off = r.i32(); r.i32()
        vert_n = r.i32() // 3
        pos_off = r.i32(); r.i32(); r.i32()
        uv_off = r.i32(); r.i32(); r.i32()
        nrm_off = r.i32(); r.i32()
        r.skip(60 + 20 + 40)
        inst_n = r.i32(); inst_off = r.i32(); r.i32()
        cl_n = r.i32(); cl_off = r.i32(); r.i32()
        mt_n = r.i32(); sub_n = r.i32(); r.i32(); r.i32()
        mtr_n = r.i32(); mtr_off = r.i32()
        r.skip(13 * 4 + 20)
        r.skip(mt_n * 4)                                     # per mesh type submesh counts (the table below has them)
        sub = r.i32(sub_n * 16).reshape(-1, 16) if sub_n else np.zeros((0, 16), np.int32)
        cum = r.i32(mt_n * 2 - 1) if mt_n else np.zeros(0, np.int32)
        ends = [int(cum[k + 1]) for k in range(0, len(cum) - 1, 2)]
        ends.append((ends[-1] if ends else 0) + int(cum[-1]) if len(cum) else 0)
        starts = [0] + ends[:-1]

        bb = base0 + bulk_off
        mt_mats = []
        if mtr_n:
            rows = np.frombuffer(data, '<i4', mtr_n * 28, bb + mtr_off).reshape(-1, 28)
            for row in rows:
                neg = np.flatnonzero(row == -1)
                mt_mats.append(row[:neg[0]].tolist() if neg.size else row.tolist())

        def textures(mt):
            out = []
            for k in (mt_mats[mt] if mt < len(mt_mats) else []):
                if 0 <= k < len(refs) and refs[k] < 0:
                    j = -refs[k] - 1
                    p = import_names[j] if j < len(import_names) else None
                    if p and p not in out:
                        out.append(p)
            return out

        pos = np.frombuffer(data, '<f4', vert_n * 3, bb + pos_off).reshape(-1, 3).astype(np.float32)
        pos[:, 1] *= -1.0
        nr = np.frombuffer(data, 'i1', vert_n * 4, bb + nrm_off).reshape(-1, 4)[:, :3].astype(np.float32) / 128.0
        nr[:, 1] *= -1.0
        nr /= np.maximum(np.linalg.norm(nr, axis=1, keepdims=True), 1e-12)
        uvch = ((nrm_off - uv_off) // vert_n) // 4 if vert_n else 0
        uv = np.frombuffer(data, '<f2', vert_n * uvch * 2, bb + uv_off).astype(np.float32).reshape(vert_n, uvch * 2) \
            if uvch else np.zeros((vert_n, 2), np.float32)
        # (as stored: UE's UV origin is the image's top left, as glTF's; a V flip had mirrored every unique unwrap
        # off its islands, the cliffs' camouflage, look-2)
        tg = np.frombuffer(data, '<i4', tg_n * 4, bb + tg_off).reshape(-1, 4) if tg_n else np.zeros((0, 4), np.int32)
        vib = np.frombuffer(data, '<i4', vi_n, bb + vi_off)
        ptb = np.frombuffer(data, '<i4', pt_n, bb + pt_off)

        def tris(s):
            vs = int(sub[s, 7])
            out = []
            for grp in range(int(sub[s, 1]), int(sub[s, 1]) + int(sub[s, 2])):
                if grp >= tg_n:
                    break
                _, base, nt, doff = (int(x) for x in tg[grp])
                pk = ptb[doff:doff + nt]
                out.append(np.stack([vib[base + (pk & 0x3FF)], vib[base + ((pk >> 10) & 0x3FF)],
                                     vib[base + ((pk >> 20) & 0x3FF)]], 1) - vs)
            return np.concatenate(out) if out else np.zeros((0, 3), np.int32)

        # mesh types used by some instance
        if inst_n:
            raw = np.frombuffer(data, '<u4', inst_n * 8, bb + inst_off).reshape(-1, 8)
            fl = np.frombuffer(data, '<f4', inst_n * 8, bb + inst_off).reshape(-1, 8)
            cid = raw[:, 0] & 0x7FFFFFFF
            ipos = fl[:, 1:4].astype(np.float64) + centre
            ipos[:, 1] *= -1.0
            q = fl[:, 4:8]
            iquat = np.stack([-q[:, 3], q[:, 0], -q[:, 1], q[:, 2]], 1)      # (w, x, y, z) in Quake axes
        else:
            cid = np.zeros(0, np.uint32)
        cl = np.frombuffer(data, '<i4', cl_n * 36, bb + cl_off).reshape(-1, 36) if cl_n else np.zeros((0, 36), np.int32)

        mesh_of = {}
        for k in range(len(cid)):
            c = int(cid[k])
            if c >= cl_n:
                continue
            for mt in range(int(cl[c, 0]), int(cl[c, 1]) + 1):
                if mt >= len(ends) or starts[mt] >= ends[mt]:
                    continue
                if mt not in mesh_of:
                    lods = []
                    for s in range(starts[mt], ends[mt]):
                        if s >= sub_n:
                            break
                        vs, vc = int(sub[s, 7]), int(sub[s, 8])
                        if vc == 0 or vs + vc > vert_n:
                            break
                        t = tris(s)
                        if not len(t) or t.min() < 0 or t.max() >= vc:
                            break
                        P.append(pos[vs:vs + vc]); N.append(nr[vs:vs + vc]); U.append(uv[vs:vs + vc, 0:2])
                        U1.append(uv[vs:vs + vc, 2:4] if uvch >= 2 else np.zeros((vc, 2), np.float32))
                        I.append(t.astype(np.uint32).ravel())
                        lods.append({'v0': vbase, 'vn': vc, 'i0': ibase, 'in': int(t.size)})
                        vbase += vc
                        ibase += int(t.size)
                    if not lods:
                        mesh_of[mt] = None
                        continue
                    mesh_of[mt] = len(meshes)
                    meshes.append({'name': 'g%d_mt%d' % (g, mt), 'textures': textures(mt), 'lods': lods})
                if mesh_of[mt] is not None:
                    instances.append({'mesh': mesh_of[mt], 'pos': [round(float(x), 3) for x in ipos[k]],
                                      'quat': [round(float(x), 7) for x in iquat[k]]})
    arrays = {
        'pos': np.concatenate(P) if P else np.zeros((0, 3), np.float32),
        'nrm': np.concatenate(N) if N else np.zeros((0, 3), np.float32),
        'uv': np.concatenate(U) if U else np.zeros((0, 2), np.float32),
        'uv1': np.concatenate(U1) if U1 else np.zeros((0, 2), np.float32),
        'idx': np.concatenate(I) if I else np.zeros(0, np.uint32),
    }
    return meshes, instances, arrays


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extracted', required=True, help='the extraction root (Massive_Packages, Inspect, ImportMaps)')
    ap.add_argument('--out', required=True)
    ap.add_argument('streams', nargs='+', help='map stems')
    a = ap.parse_args()
    ex, out = Path(a.extracted), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for stem in a.streams:
        bins = sorted((ex / 'Inspect' / 'massive_bins' / stem).glob('MassiveEnvironmentComponent0_*.bin'))
        if len(bins) != 1:
            sys.exit('%s: expected one component payload, found %d' % (stem, len(bins)))
        bulk_p = ex / 'Massive_Packages' / (stem + '.ubulk')
        names = [e.get('ObjectPath') if e else None for e in
                 json.loads((ex / 'ImportMaps' / (stem + '.metadata.json')).read_text(encoding='utf-8'))['ImportMap']]
        meshes, inst, arr = read_stream(bins[0].read_bytes(), bulk_p.read_bytes() if bulk_p.exists() else b'', names)
        np.savez(out / (stem + '.npz'), **arr)
        (out / (stem + '.json')).write_text(json.dumps({'stream': stem, 'meshes': meshes, 'instances': inst}),
                                           encoding='utf-8')
        lod0 = sum(m['lods'][0]['in'] // 3 for m in meshes)
        inst_tris = sum(meshes[i['mesh']]['lods'][0]['in'] // 3 for i in inst)
        print('FF7R_MASSIVE %s meshes=%d lods=%d instances=%d lod0_tris=%d instanced_lod0_tris=%d verts=%d' % (
            stem, len(meshes), sum(len(m['lods']) for m in meshes), len(inst), lod0, inst_tris, len(arr['pos'])))


if __name__ == '__main__':
    main()
