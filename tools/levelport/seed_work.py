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
"""Start a CP2077 work folder from others' caches by hard link: nothing is exported or baked twice, no bytes copied.

    python tools/levelport/seed_work.py watson watson_slice kabuki      # dry run
    python tools/levelport/seed_work.py watson watson_slice kabuki --apply

Links every file of the caches cp2gltf.py's stages skip on when present (CACHES: meshes and their textures, every LOD,
decimated LODs, entity and appearance templates, multilayer bakes, scene images) from each source work folder, in
the order given (the first source to hold a path wins), into .codex-research/levelport/cp77/<dst>/ where the path is
absent. Every cache is named by its depot path or its content, so a file means the same in any area. Same volume
only (hard links).

Linked files are shared. WolvenKit's uncook rewrites in place the textures and .Material.json a new mesh shares with
one already there (16,163 of the slice's and Kabuki's files rewritten through the links by
Watson's fetch, all but one at their old size; the slice's tile run read a half-written png and failed). The fetch
stages now export into staging and publish only files a cache lacks (cp2gltf.py publish()), so a
linked file is never written again."""
import argparse, os, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import paths  # noqa: E402
from retain import is_junction  # noqa: E402

CP77 = paths.LEVELPORT / 'cp77'
CACHES = ('meshes', 'meshes_lod', 'meshes_dec', 'ents', 'apps', 'bake', 'scene_dds')


def long(p):
    p = os.path.abspath(p)
    return p if os.name != 'nt' or p.startswith('\\\\?\\') else '\\\\?\\' + p


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dst', help='work folder to seed (cp77/<dst>)')
    ap.add_argument('src', nargs='+', help='work folders to link from, first wins')
    ap.add_argument('--apply', action='store_true', help='make the links (default: count them)')
    a = ap.parse_args(argv)
    t0 = time.time()
    dst = CP77 / a.dst
    if os.path.splitdrive(dst.resolve())[0].lower() != os.path.splitdrive(CP77.resolve())[0].lower():
        sys.exit('destination must be on the work folders\' volume')
    seen = set()
    for name in a.src:
        src = CP77 / name
        n = b = 0
        for cache in CACHES:
            top = src / cache
            if not top.is_dir():
                continue
            for dp, dns, fns in os.walk(long(top)):
                dns[:] = [d for d in dns if not is_junction(os.path.join(dp, d))]     # (another tree's files)
                for f in fns:
                    p = os.path.join(dp, f)
                    rel = os.path.relpath(p, long(src))
                    if rel in seen:
                        continue
                    seen.add(rel)
                    t = os.path.join(long(dst), rel)
                    if os.path.exists(t):
                        continue
                    n += 1
                    b += os.stat(p).st_size
                    if a.apply:
                        os.makedirs(os.path.dirname(t), exist_ok=True)
                        os.link(p, t)
        print(f'{name}: {n} files ({b / 1e9:.2f} GB) {"linked" if a.apply else "to link"}', flush=True)
    print(f'{time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
