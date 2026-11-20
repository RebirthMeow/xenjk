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
"""Run one q3map2 stage on a gltf2map build, or pack it: for compiling stage by stage and checking each result.

    python tools/levelport/stage.py <build dir> <mapname> -bsp -meta
    python tools/levelport/stage.py <build dir> <mapname> -vis -saveprt
    python tools/levelport/stage.py <build dir> <mapname> -light -fast -filter -patchshadows -bounce 1 -samples 2 -gamma 1.6
    python tools/levelport/stage.py <build dir> <mapname> --pack ["long name"] [--type ffa]

The build dir is one gltf2map wrote with --no-compile. Each q3map2 run appends to <build>/q3map2.log. --pack writes
scripts/<map>.arena and zz_<map>.pk3 the way gltf2map's own compile does.
"""
import os
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltf2map  # noqa: E402

SKIP = ('.prt', '.srf', '.map', '.bak', '.obj', '.mtl', '.glb')


def pack(build, name, longname=None, arena_type='ffa'):
    base = build / 'stage' / 'base'
    (base / 'scripts').mkdir(exist_ok=True)
    (base / 'scripts' / (name + '.arena')).write_text(
        '{\n\tmap\t\t"%s"\n\tlongname\t"%s"\n\ttype\t\t"%s"\n}\n' % (name, longname or name, arena_type),
        encoding='utf-8', newline='\r\n')
    pk3 = build / ('zz_%s.pk3' % name)
    tmp = pk3.with_name(pk3.name + '.tmp')       # (renamed when whole: a stopped write leaves no pack)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(base.rglob('*')):
            rel = p.relative_to(base).as_posix()
            if p.is_file() and not rel.endswith(SKIP):
                z.write(p, rel)
    os.replace(tmp, pk3)
    print('pk3: %s (%.1f MB)' % (pk3, pk3.stat().st_size / 1e6))
    return pk3


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 3:
        sys.exit(__doc__)
    build, name, rest = Path(argv[0]).resolve(), argv[1], argv[2:]   # absolute: q3map2 runs from its own folder
    if rest[0] == '--pack':
        args = rest[1:]
        kind = args[args.index('--type') + 1] if '--type' in args else 'ffa'
        longname = next((a for a in args if a not in ('--type', kind)), None)
        return pack(build, name, longname, kind)
    gltf2map.q3map2(rest, build / 'stage' / 'base' / 'maps' / (name + '.map'), build / 'stage', build / 'q3map2.log')


if __name__ == '__main__':
    main()
