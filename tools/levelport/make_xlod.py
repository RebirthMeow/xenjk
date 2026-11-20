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
"""make_xlod.py -- a kit's LOD thresholds from CP2077's own LOD distances, as maps/<map>.xlod (a mall facade flickered as it switched LODs at about 40 m, because the scene
build gives every mesh the same thresholds, cp2gltf SCENE_LOD_SCREEN, and never read CP2077's CMesh.lodLevelInfo).

    python -I make_xlod.py <kit> <meshlod.json> --out <pack.pk3> [--fov 110] [--tag 11] [--area night_city] [--map cp_watson]

The scene's meshes, in the renderer's order: the manifest's (maps/<map>.xman) geometry libraries in order, each tile's
library holding the meshes its tile part (tiles/<tag>/scene/<tile>/part.json meshkeys) adds first (checked against the
library's mesh count). For a mesh whose LOD count equals its lodLevelInfo's, LOD k's threshold (the projected size
below which the renderer takes the next LOD: radius / distance x 1 / tan(half the vertical fov), scene_cull.comp; the mesh's radius in metres) is
set so the switch to LOD k + 1 lands at lodLevelInfo[k + 1] metres at --fov (horizontal degrees at 16:9; 110 is the usual play setting). Others keep the build's. maps/<map>.xlod: 'XLOD', u32 version 1, u32 LODs (the scene's total, the
manifest's order), then f32 per LOD: its threshold, or -1 to keep the build's. Stored in a pack of its own. Reads the
kit; writes only --out. Converted levels are for local play only."""
import argparse
import ast
import collections
import glob
import json
import math
import os
import re
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402

MESH = struct.Struct('<4f2I2I')     # xscnMesh_t: centre, radius, first LOD, LODs, pad
LOD = struct.Struct('<2IfI')        # xscnLod_t: first part, parts, screen, pad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('kit', type=Path)
    ap.add_argument('meshlod', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--fov', type=float, default=110.0)
    ap.add_argument('--tag', default='11')
    ap.add_argument('--area', default='night_city')
    ap.add_argument('--map', default='cp_watson')
    a = ap.parse_args()
    hb = a.kit / 'home' / 'base'
    serve = {}
    for pk in sorted(glob.glob(str(hb / ('zz_%s_scene*.pk3' % a.map))), key=lambda p: p.upper()):
        for n in zipfile.ZipFile(pk).namelist():
            serve[n.lower()] = pk
    man = zipfile.ZipFile(serve['maps/%s.xman' % a.map]).read('maps/%s.xman' % a.map).decode()
    libs = [l.split() for l in man.splitlines() if l.startswith('lib ')]
    lods = json.load(open(a.meshlod, encoding='utf-8'))['lods']
    inv = 1.0 / math.tan(math.atan(math.tan(math.radians(a.fov) / 2) * 9.0 / 16.0))
    tiles = cp2gltf.work_dir(a.area) / 'tiles' / a.tag / 'scene'
    out, seen = [], set()
    st = collections.Counter()
    moved = []      # (old switch distance m, new) for LOD 0 -> 1
    base = 0        # the library's first LOD in the scene's order (a mesh's first LOD is the scene's index)
    for l in libs:
        path, nm, nl = l[1], int(l[2]), int(l[3])
        b = zipfile.ZipFile(serve[path.lower()]).read(path)
        _, ver, counts, offs = cp2gltf.xscn_head_of(b)
        if counts[2] != nm or counts[3] != nl:
            sys.exit('make_xlod: %s holds %d meshes and %d LODs, the manifest says %d and %d' % (path, counts[2], counts[3], nm, nl))
        meshes = [MESH.unpack_from(b, offs[2] + MESH.size * i) for i in range(nm)]
        lod_rec = [LOD.unpack_from(b, offs[3] + LOD.size * i) for i in range(nl)]
        vals = [-1.0] * nl
        m = re.search(r'g_(-?\d+_-?\d+)\.xscn', path)
        keys = None
        if m and (tiles / m.group(1) / 'part.json').exists():
            pk = json.load(open(tiles / m.group(1) / 'part.json', encoding='utf-8'))['meshkeys']
            keys = [k for k in dict.fromkeys(pk) if k not in seen]
            seen.update(keys)
            if len(keys) != nm:
                sys.exit('make_xlod: %s: its tile part adds %d meshes, the library holds %d' % (path, len(keys), nm))
        for i, (cx, cy, cz, r, first, n, _, _) in enumerate(meshes):
            first -= base
            if not 0 <= first <= nl - n:
                sys.exit('make_xlod: %s: mesh %d names LODs %d..%d outside its library (base %d, %d LODs)' % (path, i, first + base, first + base + n, base, nl))
            st['meshes'] += 1
            if keys is None:
                st['not a tile library'] += 1
                continue
            try:
                depot = ast.literal_eval(keys[i])[0].lower()
            except (ValueError, SyntaxError, IndexError, TypeError):
                st['key unreadable'] += 1
                continue
            lv = lods.get(depot)
            if not lv:
                st['no lodLevelInfo'] += 1
                continue
            if n != len(lv):
                st['LOD count differs (kept)'] += 1
                continue
            if n == 1:
                st['one LOD'] += 1
                continue
            ok = all(lv[k + 1] > 0 for k in range(n - 1))
            if not ok:
                st['zero distance (kept)'] += 1
                continue
            for k in range(n - 1):
                vals[first + k] = r / lv[k + 1] * inv   # (a mesh's radius is in metres: the instance scales it)
            vals[first + n - 1] = 0.0
            old = lod_rec[first][2]
            moved.append((r * inv / old if old > 0 else float('inf'), lv[1]))
            st['set from lodLevelInfo'] += 1
        out += vals
        base += nl
    blob = struct.pack('<4s2I', b'XLOD', 1, len(out)) + struct.pack('<%df' % len(out), *out)
    tmp = Path(str(a.out) + '.tmp')          # (renamed when whole: a stopped write leaves no pack)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('maps/%s.xlod' % a.map, blob)
    os.replace(tmp, a.out)
    nearer = sum(1 for o, n in moved if n < o * 0.8)
    farther = sum(1 for o, n in moved if n > o * 1.25)
    print(json.dumps(dict(st)))
    print('LOD 0 -> 1 switch: %d nearer than the build (by over 20%%), %d farther, %d about the same; median build %.1f m, '
          'CP2077 %.1f m' % (nearer, farther, len(moved) - nearer - farther,
                             sorted(o for o, _ in moved)[len(moved) // 2] if moved else 0,
                             sorted(n for _, n in moved)[len(moved) // 2] if moved else 0))
    print('%s: %d LODs' % (a.out, len(out)))


if __name__ == '__main__':
    main()
