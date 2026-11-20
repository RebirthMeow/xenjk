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
"""scene_pick.py -- what the renderer draws at a screen pixel of a playtest report, offline.

  python -I scene_pick.py <report.txt> <px,py> [<px,py> ...] [--tag 9] [--area night_city] [--range 2000] [--size WxH]

Each <px,py> is a pixel of the report's screenshot (--size, default 3840x2160). The view ray (the report's eye,
angles and field of view) is cast against the scene tile parts (tiles/<tag>/scene/<tile>/arrays.npz + part.json):
every placement whose draw range (inst_near to inst_hide) holds the hit's distance, finest LOD, every surface kind.
Prints front to back up to the first opaque or emissive hit (--all: every hit; --every-range: every placement on the
ray whatever its draw range): distance, placement name, mesh, draw range,
material (name, kind, base), images, interior flag, CP2077 point. No water plane, DBuffer decals or haze. The
renderer's own draw-distance scales are not applied. Reads only; converted levels are for local play only."""
import argparse
import json
import math
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402

UPM = 37.5
TILE = 256.0


def report_view(path):
    t = Path(path).read_text(encoding='utf-8', errors='replace')
    m = re.search(r'eye: \((\S+) (\S+) (\S+)\), angles pitch (\S+) yaw (\S+) roll (\S+), fov (\S+) x (\S+)', t)
    vals = [float(s.rstrip(',')) for s in m.groups()]
    return np.array(vals[:3]), vals[3], vals[4], vals[6], vals[7]


def ray_dir(pitch, yaw, fx, fy, px, py, w, h):
    p, y = math.radians(pitch), math.radians(yaw)
    f = np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p)])
    r = np.array([math.sin(y), -math.cos(y), 0.0])
    u = np.array([math.sin(p) * math.cos(y), math.sin(p) * math.sin(y), math.cos(p)])
    x = (2.0 * (px + 0.5) / w - 1.0) * math.tan(math.radians(fx) / 2)
    yy = (1.0 - 2.0 * (py + 0.5) / h) * math.tan(math.radians(fy) / 2)
    d = f + x * r + yy * u
    return d / np.linalg.norm(d)


class Part:
    def __init__(self, d):
        self.z = z = np.load(d / 'arrays.npz', allow_pickle=True)
        self.meta = json.load(open(d / 'part.json', encoding='utf-8'))
        self.V, self.I = z['V']['pos'].astype(np.float64), z['I']
        self.W, self.IM, self.slot = z['inst_W'], z['inst_mesh'], z['inst_slot']
        self.near = z['inst_near'].astype(np.float64)
        far = z['inst_hide'].astype(np.float64)
        self.far = np.where(far > 0, far, np.inf)
        self.cache = {}
        nm = len(z['meshes'])
        lo, hi = np.zeros((nm, 3)), np.zeros((nm, 3))
        for mi in range(nm):
            T, _ = self.tris(mi)
            if len(T):
                lo[mi], hi[mi] = T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0)
        c, e = (lo + hi) / 2, (hi - lo) / 2
        cw = np.einsum('nij,nj->ni', self.W[:, :, :3], c[self.IM]) + self.W[:, :, 3]
        ew = np.einsum('nij,nj->ni', np.abs(self.W[:, :, :3]), e[self.IM])
        self.blo, self.bhi = cw - ew, cw + ew

    def tris(self, mi):
        if mi in self.cache:
            return self.cache[mi]
        z = self.z
        fl = int(z['meshes'][mi][4])
        fp, npart, _ = z['lods'][fl]
        T, M = [], []
        for fi, ni, vo, m in z['parts'][int(fp):int(fp) + int(npart)]:
            T.append(self.V[self.I[fi:fi + ni].reshape(-1, 3) + vo])
            M.append(np.full(len(T[-1]), m))
        self.cache[mi] = (np.concatenate(T) if T else np.zeros((0, 3, 3)), np.concatenate(M) if M else np.zeros(0, int))
        return self.cache[mi]


def hits(part, o, d, tmax, every_range=False):
    inv = 1.0 / np.where(np.abs(d) < 1e-12, 1e-12, d)
    t0, t1 = (part.blo - o) * inv, (part.bhi - o) * inv
    tn, tf = np.minimum(t0, t1).max(1), np.maximum(t0, t1).min(1)
    cand = np.nonzero((tf >= np.maximum(tn, 0)) & (tn <= tmax))[0]
    out = []
    for i in cand:
        T, M = part.tris(int(part.IM[i]))
        if not len(T):
            continue
        Tw = T @ part.W[i][:, :3].T + part.W[i][:, 3]
        e1, e2 = Tw[:, 1] - Tw[:, 0], Tw[:, 2] - Tw[:, 0]
        pv = np.cross(d, e2)
        det = np.einsum('ij,ij->i', e1, pv)
        ok = np.abs(det) > 1e-9
        inv_det = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = o - Tw[:, 0]
        u = np.einsum('ij,ij->i', tv, pv) * inv_det
        qv = np.cross(tv, e1)
        v = (qv @ d) * inv_det
        t = np.einsum('ij,ij->i', e2, qv) * inv_det
        sel = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0) & (t <= tmax)
        if not sel.any():
            continue
        k = np.nonzero(sel)[0][np.argmin(t[sel])]
        if not every_range and not (part.near[i] <= t[k] < part.far[i]):
            continue
        out.append((t[k], i, int(M[k])))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('report')
    ap.add_argument('pixels', nargs='+')
    ap.add_argument('--tag', default='9')
    ap.add_argument('--area', default='night_city')
    ap.add_argument('--range', type=float, default=2000.0)
    ap.add_argument('--size', default='3840x2160')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--every-range', action='store_true', help='every placement whatever its draw range (what the data '
                    'has along the ray, for a hole: the near pieces a far proxy stands in for); implies --all')
    a = ap.parse_args()
    cx, cy = cp2gltf.AREAS[a.area]['centre']
    floor = 12.0
    scene = cp2gltf.work_dir(a.area) / 'tiles' / a.tag / 'scene'
    w, h = map(int, a.size.split('x'))
    eye, pitch, yaw, fx, fy = report_view(a.report)
    parts = {}
    for pix in a.pixels:
        px, py = map(float, pix.split(','))
        d = ray_dir(pitch, yaw, fx, fy, px, py, w, h)
        tmax = a.range * UPM
        tiles = set()
        for s in np.arange(0.0, tmax + 64 * UPM, 64 * UPM):
            q = eye + d * min(s, tmax)
            qx, qy = q[0] / UPM + cx, q[1] / UPM + cy
            for ox in (-1, 0, 1):
                for oy in (-1, 0, 1):
                    tiles.add('%d_%d' % (math.floor(qx / TILE) + ox, math.floor(qy / TILE) + oy))
        found = []
        for tname in sorted(tiles):
            td = scene / tname
            if not (td / 'arrays.npz').exists():
                continue
            if tname not in parts:
                parts[tname] = Part(td)
            for t, i, m in hits(parts[tname], eye, d, tmax, a.every_range):
                found.append((t, tname, i, m))
        found.sort()
        print('pixel %s: ray (%.3f %.3f %.3f)' % (pix, d[0], d[1], d[2]))
        for t, tname, i, m in found:
            p = parts[tname]
            mn = p.meta['mnames'][m] if m < len(p.meta['mnames']) else {}
            mesh = mn.get('mesh') or p.meta['meshkeys'][int(p.IM[i])]
            q = eye + d * t
            imgs = [x for x in p.meta['mrecs'][m][0] if x] if m < len(p.meta['mrecs']) else []
            print('  %7.1f m  %s #%d %s%s\n           mesh %s\n           material %s (%s, %s) images %s\n'
                  '           CP2077 (%.1f, %.1f, %.1f), drawn %.0f..%s m' % (
                      t / UPM, tname, i, p.meta['placed_name'][i], ' [interior]' if int(p.slot[i]) >> 24 & 1 else '',
                      mesh, mn.get('name'), mn.get('kind'), mn.get('base'), imgs,
                      q[0] / UPM + cx, q[1] / UPM + cy, q[2] / UPM + floor, p.near[i] / UPM,
                      '%.0f' % (p.far[i] / UPM) if np.isfinite(p.far[i]) else 'inf'))
            if not (a.all or a.every_range) and mn.get('kind') in ('opaque', 'emissive'):
                break


if __name__ == '__main__':
    main()
