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
"""Relight a gltf2map build with other q3map2 -light options: copy its stage, rerun only -light, pack a pk3.

    python tools/levelport/relight.py <build dir> <out dir> <mapname> -- <q3map2 light args...>

For comparing lighting settings without recompiling geometry (the camp's light stage is ~8-17 min, -bsp seconds).
"""
import os
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltf2map  # noqa: E402


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if '--' not in argv or len(argv[:argv.index('--')]) != 3:
        sys.exit(__doc__)
    # absolute: q3map2 runs from its own folder
    src, out, name = Path(argv[0]).resolve(), Path(argv[1]).resolve(), argv[2]
    args = argv[argv.index('--') + 1:]
    if out.exists():
        sys.exit('%s exists' % out)
    shutil.copytree(src / 'stage', out / 'stage', ignore=shutil.ignore_patterns('*.glb', '*.obj', '*.mtl'))
    mapfile = out / 'stage' / 'base' / 'maps' / (name + '.map')
    gltf2map.q3map2(['-light'] + args, mapfile, out / 'stage', out / 'q3map2.log')
    base = out / 'stage' / 'base'
    pk3 = out / ('zz_%s.pk3' % name)
    tmp = pk3.with_name(pk3.name + '.tmp')       # (renamed when whole: a stopped write leaves no pack)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(base.rglob('*')):
            rel = p.relative_to(base).as_posix()
            if p.is_file() and not rel.endswith(('.prt', '.srf', '.map', '.bak', '.obj', '.mtl', '.glb')):
                z.write(p, rel)
    os.replace(tmp, pk3)
    print(pk3)


if __name__ == '__main__':
    main()
