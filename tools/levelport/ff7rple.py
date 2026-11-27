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
"""ff7rple.py -- FF7 Rebirth's probe GI (the PrecomputedLightEnvironment plugin) from a map export's raw bytes.

A Rebirth area's indirect light is baked per time of day into PrecomputedLightEnvironmentComponent exports in its
TimeSet map (2110-COSTT_TimeSet_Day: PrecomputedLightEnvironmentActor and _Far). The component's tagged properties
are followed by the plugin's own serialization ("PLER"); the extractor keeps the whole export (CUE4Parse cannot read
the property header) and this reads the PLER block. The layout below is the shader side's (frostbone25's
ShaderInjector, ComputeShaderPass_SampleGI.hlsl, MIT: the structured buffers the game binds) matched to the bytes:

  'PLER' u32, version i32 (25), payload bytes i32, then
  probes      TArray<{float4 position (cm, the environment's space) + w, i32 a, i32 b, i32 distanceCubeSlice, i32 i}>
  cubes       TArray<{float4 position + radius, i32 -1 x3, i32 index}>              (the reflection captures)
  i32, cubes2 TArray<{float4 position + radius, i32 256, float4 box, i32 0 x5, i32 texture export}>
  i32, coefficients TArray<float> (9 a probe: base rgb, +x +y +z lobes, -x -y -z lobes: luminance, x base)
  occluder triangles TArray<float> (9 a triangle), occluder ranges TArray<u32> (count 8 bits | first << 8, a bound,
                                                                               then the index table)
  bounds      TArray<{i32 corner probes[8] (-1 none), u32 grid origin[3], u32 packed cell size (low 8 bits)}>
  index grid  TArray<i32> (coarse cells, then refinements: 28-bit index | refinement bits << 28; -1 empty)
  bounds info TArray<{u32 grid size[3], u32 hierarchy log2, float4x4 local to grid, float4x4 grid to local}>

The sampling (the shader): local = world - environment position; grid = local x LocalToGrid; the coarse cell
(>> hierarchy-1) picks an index entry, a refinement resolves it to a bound; the bound's 8 corner probes blend
trilinearly (cellFraction = (grid - origin) / cell size), each weighted by its distance-cube Chebyshev test and the
occluder triangles, in log2 space; a direction d evaluates as base x dot(+lobes, max(d,0)^2) + dot(-lobes,
max(-d,0)^2) / luminance(base).

    python tools/levelport/ff7rple.py dump <export.bin> [--json out.json]
    python tools/levelport/ff7rple.py pack --map ff7r_costa --scene <ff7rscene build> --sun-lux 100000
                                         --out DIR <near export.bin> [<far export.bin> ...]
    python tools/levelport/ff7rple.py slices <export.bin> --out sheet.png

pack writes maps/<map>.xple (in zz_<map>_ple.pk3) for the renderer, little endian:
  'XPLE' u32 version (1) u32 environments
  f32 origin_cm[3] (the scene's, Quake axes: scene = (ue with y negated - origin) x units_per_cm), f32 units_per_cm,
  f32 sun_lux (the time of day's directional light: the probes' values are in its units, so a renderer scales them by
  r_sceneSun / sun_lux to keep the game's sun : sky ratio)
  per environment, in the game's order (near first; a point takes the first that holds it):
    f32 env_pos_cm[3] (UE axes), u32 grid_size[3], u32 hierarchy_log2, f32 local_to_grid[16] (row-vector rows),
    u32 n_probes, n_bounds, n_index, n_occ_tris, n_occ_index,
    f32 probe_pos[n_probes][4] (UE cm, w the probe's reach), f32 coef[n_probes][9] (UE axes: lobes +x +y +z -x -y -z),
    i32 bounds[n_bounds][12], i32 index[n_index], f32 occ_tris[n_occ_tris][9], u32 occ_index[n_occ_index]
The space stays the game's: a renderer maps a scene point back to UE cm (and a normal's y negated) before the lookup.
"""
import argparse
import json
import struct
import sys

import numpy as np


class Reader:
    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.o)[0]
        self.o += 4
        return v

    def arr(self, dtype, n):
        a = np.frombuffer(self.b, dtype, n, self.o).copy()
        self.o += a.nbytes
        return a

    def tarray(self, rec_words, dtype='<i4'):
        n = self.i32()
        if n < 0 or n * rec_words * 4 > len(self.b) - self.o:
            raise ValueError('bad array count %d at %d' % (n, self.o - 4))
        return self.arr(dtype, n * rec_words).reshape(n, rec_words) if rec_words > 1 else self.arr(dtype, n)


def parse(b):
    k = b.find(b'PLER')
    if k < 0:
        raise ValueError('no PLER block')
    r = Reader(b, k + 4)
    out = {'offset': k, 'version': r.i32(), 'payload': r.i32()}
    raw = r.tarray(8)
    out['probe_pos'] = raw[:, :4].view('<f4')
    out['probe_ints'] = raw[:, 4:]
    raw = r.tarray(8)
    out['cube_pos'] = raw[:, :4].view('<f4')
    out['cube_ints'] = raw[:, 4:]
    out['pre_cubes2'] = r.i32()
    raw = r.tarray(15)
    out['cube2'] = raw
    out['pre_coef'] = r.i32()
    c = r.tarray(1, '<f4')
    out['coef'] = c.reshape(-1, 9)
    out['occ_tris'] = r.tarray(1, '<f4').reshape(-1, 3, 3)
    out['occ_index'] = r.tarray(1, '<u4')
    out['bounds'] = r.tarray(12)
    out['index_grid'] = r.tarray(1, '<i4')
    raw = r.tarray(36)
    out['info_ints'] = raw[:, :4]
    out['local_to_grid'] = raw[:, 4:20].view('<f4').reshape(-1, 4, 4)
    out['grid_to_local'] = raw[:, 20:36].view('<f4').reshape(-1, 4, 4)
    out['end'] = r.o
    out['rest'] = len(b) - r.o
    return out


def lum(v):
    return 0.2126 * v[..., 0] + 0.7152 * v[..., 1] + 0.0722 * v[..., 2]


def find_bounds(d, P, env_pos=(0.0, 0.0, 0.0)):
    """The shader's FindProbeBounds for points P (n, 3) cm in the environment's space: (bound index or -1, grid pos)"""
    info = d['info_ints'][0]
    gs = info[:3].astype(np.int64)
    h = int(info[3])
    M = d['local_to_grid'][0].astype(np.float64)
    local = np.asarray(P, np.float64) - np.asarray(env_pos)
    g = np.concatenate([local, np.ones((len(local), 1))], 1) @ M          # row vector x rows
    g = g[:, :3]
    out = np.full(len(P), -1, np.int64)
    ok = np.all(g >= 0, 1) & np.all(g < gs, 1)
    gi = np.floor(np.where(ok[:, None], g, 0)).astype(np.int64)
    cshift = (h - 1) & 31
    cdim = (gs + (1 << cshift) - 1) >> cshift
    cc = gi >> cshift
    lin = (cc[:, 2] * cdim[1] + cc[:, 1]) * cdim[0] + cc[:, 0]
    grid = d['index_grid']
    e = np.where(ok, grid[np.clip(lin, 0, len(grid) - 1)], -1).astype(np.int64)
    ok &= e != -1
    eu = e & 0xFFFFFFFF
    idx = eu & 0x0FFFFFFF
    ref = eu >> 28
    need = ok & (ref != 0)
    if need.any():
        fshift = (ref - 1) & 31
        rshift = (h - ref) & 31
        within = gi - (cc << cshift)
        fc = within >> fshift[:, None]
        pitch = np.left_shift(1, rshift)
        fi = idx + fc[:, 0] + fc[:, 1] * pitch + fc[:, 2] * pitch * pitch
        e2 = grid[np.clip(fi, 0, len(grid) - 1)].astype(np.int64)
        bad = need & (e2 == -1)
        idx = np.where(need, e2 & 0x0FFFFFFF, idx)
        ok &= ~bad
    ok &= idx != 0x0FFFFFFF
    out[ok] = idx[ok]
    return out, g


def sample(d, P, N, env_pos=(0.0, 0.0, 0.0)):
    """The probe field's light along direction N (n, 3) at P (n, 3): the shader's BlendProbeField (log-space blend of
    the 8 corners, trilinear; occluders and distance cubes not tested) then EvaluateDirectionalField. NaN outside."""
    b, g = find_bounds(d, P, env_pos)
    res = np.full((len(P), 3), np.nan)
    k = np.where(b >= 0)[0]
    if not len(k):
        return res
    B = d['bounds'][b[k]]
    corners, origin, cell = B[:, :8], B[:, 8:11].astype(np.float64), (B[:, 11] & 0xFF).astype(np.float64)
    f = np.clip((g[k] - origin) / cell[:, None], 0, 1)
    acc = np.zeros((len(k), 9))
    wt = np.zeros(len(k))
    C = d['coef'].astype(np.float64)
    for c_ in range(8):
        sel = np.array([(c_ >> 0) & 1, (c_ >> 1) & 1, (c_ >> 2) & 1], np.float64)
        w = np.prod(np.where(sel > 0, f, 1 - f), 1)
        pi_ = corners[:, c_]
        w = np.where(pi_ >= 0, w, 0.0)
        acc += np.log2(np.maximum(C[np.maximum(pi_, 0)], 1e-9)) * w[:, None]
        wt += w
    good = wt > 0
    co = np.exp2(acc / np.maximum(wt, 1e-12)[:, None])
    base, pos, neg = co[:, :3], co[:, 3:6], co[:, 6:9]
    n = np.asarray(N, np.float64)[k]
    pd, nd = np.maximum(n, 0), np.maximum(-n, 0)
    inten = (pos * pd * pd).sum(1) + (neg * nd * nd).sum(1)
    v = base * (inten / np.maximum(lum(base), 1e-9))[:, None]
    res[k[good]] = v[good]
    return res


def slices_sheet(d, out, zs=(200, 600, 1500, 3000), n=400):
    """Top-down slices of the field's upward light (north up), one per height (cm), the grid's xy extent"""
    from PIL import Image
    M = np.linalg.inv(d['local_to_grid'][0].astype(np.float64))
    gs = d['info_ints'][0][:3]
    lo = (np.array([0, 0, 0, 1.0]) @ M)[:2]
    hi = (np.array([gs[0], gs[1], 0, 1.0]) @ M)[:2]
    X, Y = np.meshgrid(np.linspace(lo[0], hi[0], n), np.linspace(lo[1], hi[1], n))
    rows = []
    for z in zs:
        P = np.stack([X.ravel(), Y.ravel(), np.full(X.size, float(z))], 1)
        v = sample(d, P, np.tile([0, 0, 1.0], (len(P), 1))).reshape(n, n, 3)
        v = np.nan_to_num(v, nan=0) / 6000.0
        v = v / (1 + v)
        rows.append(np.uint8(np.clip(v, 0, 1) ** (1 / 2.2) * 255)[::-1])
    half = (len(rows) + 1) // 2
    while len(rows) < 2 * half:
        rows.append(np.zeros_like(rows[0]))
    Image.fromarray(np.concatenate([np.concatenate(rows[:half], 1), np.concatenate(rows[half:], 1)], 0)).save(out)


def pack(a):
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pk3write
    from pathlib import Path
    rec = json.loads((Path(a.scene) / 'scene.json').read_text(encoding='utf-8'))
    blob = [b'XPLE', struct.pack('<II', 1, len(a.exports)),
            struct.pack('<5f', *rec['origin_cm'], rec['units_per_cm'], a.sun_lux)]
    stats = []
    for f in a.exports:
        d = parse(open(f, 'rb').read())
        nprobe = len(d['probe_pos'])
        blob += [struct.pack('<3f', 0.0, 0.0, 0.0),                # (the actors sit at the origin: Costa)
                 d['info_ints'][0].astype('<u4').tobytes(), d['local_to_grid'][0].astype('<f4').tobytes(),
                 struct.pack('<5I', nprobe, len(d['bounds']), len(d['index_grid']), len(d['occ_tris']),
                             len(d['occ_index'])),
                 d['probe_pos'].astype('<f4').tobytes(), d['coef'].astype('<f4').tobytes(),
                 d['bounds'].astype('<i4').tobytes(), d['index_grid'].astype('<i4').tobytes(),
                 d['occ_tris'].astype('<f4').tobytes(), d['occ_index'].astype('<u4').tobytes()]
        stats.append({'export': f, 'probes': nprobe, 'bounds': len(d['bounds']), 'index': len(d['index_grid']),
                      'occluder_tris': len(d['occ_tris']), 'grid': d['info_ints'][0].tolist(),
                      'cell_cm': float(1.0 / d['local_to_grid'][0][0, 0])})
    out = Path(a.out)
    (out / 'maps').mkdir(parents=True, exist_ok=True)
    xple = out / 'maps' / (a.map + '.xple')
    xple.write_bytes(b''.join(blob))
    pk3 = out / ('zz_%s_ple.pk3' % a.map)
    with pk3write.writing(str(pk3)) as z:
        z.write(xple, 'maps/%s.xple' % a.map)
    first = parse(open(a.exports[0], 'rb').read())
    slices_sheet(first, str(out / 'slices.png'))
    info = {'map': a.map, 'sun_lux': a.sun_lux, 'xple_bytes': xple.stat().st_size, 'environments': stats}
    (out / 'ple.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
    print('FF7RPLE', pk3, json.dumps(info))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('dump')
    p.add_argument('export')
    p.add_argument('--json')
    p = sub.add_parser('pack')
    p.add_argument('exports', nargs='+', help="the raw PrecomputedLightEnvironmentComponent exports, near first")
    p.add_argument('--map', required=True)
    p.add_argument('--scene', required=True, help="ff7rscene.py's build folder (scene.json: origin, units)")
    p.add_argument('--sun-lux', type=float, required=True, help="the time of day's directional light Intensity")
    p.add_argument('--out', required=True)
    p = sub.add_parser('slices')
    p.add_argument('export')
    p.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.cmd == 'pack':
        return pack(a)
    if a.cmd == 'slices':
        return slices_sheet(parse(open(a.export, 'rb').read()), a.out)
    b = open(a.export, 'rb').read()
    d = parse(b)
    pp = d['probe_pos']
    c = d['coef']
    summ = {'version': d['version'], 'payload': d['payload'], 'probes': len(pp), 'cubes': len(d['cube_pos']),
            'coef_probes': len(c), 'occluder_tris': len(d['occ_tris']), 'occluder_index': len(d['occ_index']),
            'bounds': len(d['bounds']), 'index_grid': len(d['index_grid']),
            'grid_size': d['info_ints'][:, :3].tolist(), 'hierarchy_log2': d['info_ints'][:, 3].tolist(),
            'grid_to_local_translation': d['grid_to_local'][:, 3, :3].tolist(),
            'cell_cm': [float(1.0 / m[0, 0]) for m in d['local_to_grid']],
            'probe_min': pp[:, :3].min(0).tolist(), 'probe_max': pp[:, :3].max(0).tolist(),
            'probe_w': sorted(set(np.round(pp[:, 3]).tolist()))[:6],
            'coef_base_median': np.median(c[:, :3], 0).tolist(), 'coef_lobe_median': np.median(c[:, 3:], 0).tolist(),
            'parsed_to': d['end'], 'bytes': len(b), 'rest': d['rest']}
    print(json.dumps(summ, indent=1))
    if a.json:
        json.dump(summ, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
