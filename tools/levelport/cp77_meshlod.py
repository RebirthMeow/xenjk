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
"""cp77_meshlod.py -- CP2077's per-mesh LOD distances (CMesh.lodLevelInfo) read from the fetched .mesh files (CR2W) under
<work>/meshes, no WolvenKit run. A mall facade flickered as
it switched LODs at about 40 m, because the scene build gives every mesh the same LOD thresholds, cp2gltf
SCENE_LOD_SCREEN, and never read CP2077's own.

    python -I cp77_meshlod.py <work> --out <meshlod.json> [--meshes list.txt]
    python -I cp77_meshlod.py one <file.mesh>

A .mesh is CR2W: a header, then table headers (strings, names, imports, properties, exports, ...); each export's data
is its class's serialised properties: a zero byte, then (u16 name index, u16 type index, u32 size counting itself,
value) until a zero name. CMesh.lodLevelInfo is array:Float: u32 count, then the floats (metres). Writes {depot path:
[distances]} (absent: the mesh has none, CP2077 then uses its defaults) and a count of what it found. Reads only."""
import argparse
import json
import struct
import sys
from pathlib import Path


def cr2w_props(b, want_class, want_props):
    """{prop: (type name, raw value bytes)} of the first export of class want_class; None when not CR2W"""
    if b[:4] != b'CR2W':
        return None
    # header: magic, version, flags, timestamp(8), build, objectsEnd, buffersEnd, crc, numChunks = 40 bytes, then
    # 10 table headers (offset, count, crc)
    tables = [struct.unpack_from('<3I', b, 40 + 12 * i) for i in range(10)]
    s_off, s_cnt, _ = tables[0]
    n_off, n_cnt, _ = tables[1]
    e_off, e_cnt, _ = tables[4]

    def string(o):
        e = b.index(b'\0', s_off + o)
        return b[s_off + o:e].decode('utf-8', 'replace')
    names = [string(struct.unpack_from('<I', b, n_off + 8 * i)[0]) for i in range(n_cnt)]
    for i in range(e_cnt):
        cls, flags, parent, size, off, tmpl, crc = struct.unpack_from('<HHIIIII', b, e_off + 24 * i)
        if names[cls] != want_class:
            continue
        p, end, out = off + 1, off + size, {}
        while p + 8 <= end:
            ni, ti, sz = struct.unpack_from('<HHI', b, p)
            if ni == 0 or sz < 4:
                break
            nm = names[ni]
            if nm in want_props:
                out[nm] = (names[ti], b[p + 8:p + 4 + sz])
            p += 4 + sz
        return out
    return {}


def lod_levels(path):
    b = Path(path).read_bytes()
    got = cr2w_props(b, 'CMesh', {'lodLevelInfo'})
    if not got or 'lodLevelInfo' not in got:
        return None
    t, v = got['lodLevelInfo']
    if t != 'array:Float' or len(v) < 4:
        return None
    n = struct.unpack_from('<I', v, 0)[0]
    return list(struct.unpack_from('<%df' % n, v, 4)) if len(v) >= 4 + 4 * n else None


def main():
    if len(sys.argv) > 2 and sys.argv[1] == 'one':
        print(lod_levels(sys.argv[2]))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument('work', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--meshes', type=Path, help='depot paths, one a line (default: every .mesh under <work>/meshes)')
    a = ap.parse_args()
    root = a.work / 'meshes'
    if a.meshes:
        paths = [l.strip() for l in open(a.meshes, encoding='utf-8') if l.strip()]
        files = [(p, root / (p.replace(chr(92), '/'))) for p in paths]
    else:
        files = [(str(f.relative_to(root)).replace('/', chr(92)), f)
                 for f in sorted(root.rglob('*.mesh')) + sorted(root.rglob('*.w2mesh'))]
    out, stats = {}, {'meshes': 0, 'with lodLevelInfo': 0, 'without': 0, 'unreadable': 0}
    for depot, f in files:
        stats['meshes'] += 1
        try:
            lv = lod_levels(f)
        except (OSError, ValueError, struct.error, IndexError):
            stats['unreadable'] += 1
            continue
        if lv:
            out[depot.lower()] = lv
            stats['with lodLevelInfo'] += 1
        else:
            stats['without'] += 1
    a.out.write_text(json.dumps({'stats': stats, 'lods': out}, indent=0), encoding='utf-8')
    print(json.dumps(stats))


if __name__ == '__main__':
    main()
