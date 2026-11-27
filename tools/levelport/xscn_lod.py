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
"""xscn_lod.py -- re-set the LOD screens and hide distances of a built render scene in place of a rebuild
(ff7rscene.py's LOD chain: --lod-screen halving, floored by --lod-density), into a new build folder:

    python -I tools/levelport/xscn_lod.py <scene build dir> <out dir> --density 0.1 --hide Grass=150 --hide NatureMini=150

--hide REGEX=METRES: the instances of every mesh whose meshes.json name (kind/stream/mesh) matches are not drawn past
that eye distance (the instance's hide distance, as The Witcher 3's autoHideDistance in Beauclair; r_sceneHideScale
and r_sceneHideBand apply). Rebirth's far grass on the surround's hills was a tenth of a beach view's triangles.

Only the scene file's LOD records change (each multi-LOD mesh's; a single-LOD one keeps its 0); its other files and
packs are copied (the packs without the scene file hard-linked). A Costa rebuild took 25 minutes; this one or two."""
import argparse
import json
import os
import re
import shutil
import struct
import sys
import time
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ff7rscene import lod_density, lod_screens  # noqa: E402

REC = [28, 4, 32, 16, 16, 240, 64, 64, 64, 16]      # vk_scene.cpp xsRecordSize
XS_MESHES, XS_LODS, XS_PARTS, XS_INSTANCES = 2, 3, 4, 6


def patch(blob, base, density, hide=None):
    """hide: per mesh its hide distance in scene units (0: none), or None"""
    ver = struct.unpack_from('<I', blob, 4)[0]
    n = 8 if ver == 1 else 9 if ver <= 5 else 10
    cnt = struct.unpack_from('<%dI' % n, blob, 8)
    ofs = struct.unpack_from('<%dI' % n, blob, 8 + 4 * n)
    meshes = np.frombuffer(blob, np.uint32, cnt[XS_MESHES] * 8, ofs[XS_MESHES]).reshape(-1, 8)
    parts = np.frombuffer(blob, np.uint32, cnt[XS_PARTS] * 4, ofs[XS_PARTS]).reshape(-1, 4)
    lods = np.frombuffer(blob, np.uint32, cnt[XS_LODS] * 4, ofs[XS_LODS]).reshape(-1, 4).copy()
    tris = np.array([int(parts[a:a + b, 1].sum()) // 3 for a, b in lods[:, :2]])
    screens = lods[:, 2].view(np.float32)
    changed = 0
    for first, nl in meshes[:, 4:6]:
        if nl < 2:
            continue
        new = lod_screens(int(nl), base)
        if density:
            new = lod_density(new, tris[first:first + nl].tolist(), density)
        changed += int(np.any(np.abs(screens[first:first + nl] - new) > 1e-6))
        screens[first:first + nl] = new
    blob[ofs[XS_LODS]:ofs[XS_LODS] + lods.nbytes] = lods.tobytes()
    hidden = 0
    if hide is not None:
        if ver < 4:
            sys.exit('scene version %d has no hide distances' % ver)
        inst = np.frombuffer(blob, np.uint32, cnt[XS_INSTANCES] * 16, ofs[XS_INSTANCES]).reshape(-1, 16).copy()
        hd = hide[inst[:, 12]]
        sel = hd > 0
        inst[sel, 15] = hd[sel].astype(np.float32).view(np.uint32)
        hidden = int(sel.sum())
        blob[ofs[XS_INSTANCES]:ofs[XS_INSTANCES] + inst.nbytes] = inst.tobytes()
    return changed, hidden


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('scene', help="ff7rscene.py's build folder")
    ap.add_argument('out', help='the new build folder (never an existing one)')
    ap.add_argument('--density', type=float, required=True, help='ff7rscene --lod-density (0: the chain alone)')
    ap.add_argument('--lod-screen', type=float, default=0.3, help='ff7rscene --lod-screen')
    ap.add_argument('--hide', action='append', default=[], metavar='REGEX=METRES',
                    help='hide distance for the meshes whose meshes.json name matches (first match wins)')
    a = ap.parse_args()
    src, out = Path(a.scene), Path(a.out)
    if out.exists():
        sys.exit('%s exists: builds are never overwritten' % out)
    t0 = time.time()
    out.mkdir(parents=True)
    rec = json.loads((src / 'scene.json').read_text(encoding='utf-8'))
    hide = None
    if a.hide:
        rules = [(re.compile(h.rsplit('=', 1)[0]), float(h.rsplit('=', 1)[1]) * 100.0 * rec['units_per_cm']) for h in a.hide]
        names = json.loads((src / 'meshes.json').read_text(encoding='utf-8'))
        hide = np.zeros(len(names), np.float32)
        for mi, n in enumerate(names):
            key = '/'.join(n or [])
            hide[mi] = next((d for rx, d in rules if rx.search(key)), 0.0)
    for f in src.iterdir():
        if f.suffix == '.pk3':
            continue
        if f.is_file():
            shutil.copy2(f, out / f.name)
    for name in rec['packs']:
        z = zipfile.ZipFile(src / name)
        if not any(i.filename.endswith('.xscn') for i in z.infolist()):
            os.link(src / name, out / name)
            continue
        with zipfile.ZipFile(out / name, 'w') as zo:
            for i in z.infolist():
                if i.filename.endswith('.xscn'):
                    blob = bytearray(z.read(i))
                    changed, hidden = patch(blob, a.lod_screen, a.density, hide)
                    zo.writestr(i.filename, bytes(blob), compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
                    print('%s: %d meshes re-screened, %d instances given a hide distance (%.0f s)' % (
                        i.filename, changed, hidden, time.time() - t0), flush=True)
                else:
                    with z.open(i) as fi, zo.open(zipfile.ZipInfo(i.filename, i.date_time), 'w') as fo:
                        shutil.copyfileobj(fi, fo, 1 << 22)
    rec['lod_density'] = a.density
    rec['hide'] = a.hide
    rec['patched_from'] = str(src)
    (out / 'scene.json').write_text(json.dumps(rec, indent=1), encoding='utf-8')
    print('xscn_lod -> %s (%.0f s)' % (out, time.time() - t0))


if __name__ == '__main__':
    main()
