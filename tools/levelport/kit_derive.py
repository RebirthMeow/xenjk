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
"""kit_derive.py -- kit B = kit A with these files. 26 kits in four days came from
separate copies of this logic (patch_materials, patch_kit, the spawn and report kits by hand).

  python -I kit_derive.py <base kit> <new kit name> [--put home/base/x.pk3=<file>]... [--drop home/base/x.pk3]...
      [--renderer <dll>] [--module <dll>]... [--cfg 'seta <cvar> <value>']... [--note "..."] [--rem "..."]

The new kit is build/<new name>/ beside the base, made through <name>.tmp and renamed:
- every file of the base a hard link, except its runtime files (screenshots, cache, bot2-graph, logs, the game's
  servercache), what --drop names and what --put replaces;
- openjk.cfg, KIT.txt and the launchers copies (they get edited); --cfg lines after the cfg's own (the later wins);
- a --put pack a hard link of its source (a build's output), a renderer or module a copy: never a write through a
  link (the game modules have 6-12 links across kits, so a copy onto one changes that many kits);
- checks before the rename: no two packs of one name (case-insensitive), every member of a scene overlay stored (a
  deflated one turned geometry streaming off, 4 GB more VRAM), and what each --put pack's members shadow;
- KIT.json: every file's size and sha256 (a file the base's KIT.json already hashed, and still the same file, keeps
  that hash: only new files are read); kit_verify.py reads it again.
Never a kit made before (a new name each time); nothing installed. Converted levels are for local play only."""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RENDERER = 'rd-vulkan_x86_64.dll'
SKIP = re.compile(r'(?i)^home/base/(screenshots|cache|bot2-graph)/|\.log$|^home/servercache\.dat$|\.tmp$')
COPIED = re.compile(r'(?i)(^|/)(openjk\.cfg|KIT\.txt|[^/]*\.bat)$')
READ = 4 << 20


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(READ), b''):
            h.update(b)
    return h.hexdigest()


def fs_order(names):
    """the engine's pack order (FS_PathCmp upper-cases): sorted so that a later one wins"""
    return sorted(names, key=lambda n: n.upper())


def derive(base, out, put=None, drop=(), renderer=None, modules=(), cfg_set=(), note='', rem='', kit_txt='append'):
    """kit out from kit base (paths); put: {relative path: source file}; drop: relative paths; renderer: a DLL for
    bin64/rd-vulkan_x86_64.dll; modules: game module DLLs for home/base; cfg_set: lines for openjk.cfg; note: KIT.txt's
    new paragraph (kit_txt 'replace': the whole of it); rem: a line after @echo off in each launcher.
    -> {'links', 'copies', 'shadowed', 'seconds'}"""
    base, out = Path(base), Path(out)
    put = {k.replace('\\', '/'): Path(v) for k, v in (put or {}).items()}
    if renderer:
        put['bin64/' + RENDERER] = Path(renderer)
    for m in modules:
        put['home/base/' + Path(m).name] = Path(m)
    drop = {d.replace('\\', '/').lower() for d in drop}
    if out.exists():
        sys.exit('kit_derive: %s exists (a kit is made once: a new name)' % out)
    tmp = out.with_name(out.name + '.tmp')
    if tmp.exists():
        sys.exit('kit_derive: %s is left from a stopped run: look at it, then move it away' % tmp)
    for rel, src in put.items():
        if not src.is_file():
            sys.exit('kit_derive: --put %s: no file %s' % (rel, src))
    t0, links, copies = time.time(), 0, 0
    replaced = {r.lower() for r in put}
    for f in sorted(base.rglob('*')):
        rel = f.relative_to(base).as_posix()
        if f.is_dir() or SKIP.search(rel) or rel.lower() in drop or rel.lower() in replaced:
            continue
        d = tmp / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        if COPIED.search(rel):
            shutil.copy2(f, d)
            copies += 1
        else:
            os.link(f, d)                     # (same volume: a kit's files are its builds' links)
            links += 1
    for rel, src in sorted(put.items()):
        d = tmp / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        if src.suffix.lower() == '.pk3':
            try:
                os.link(src, d)
                links += 1
            except OSError:
                shutil.copy2(src, d)          # (another volume: a copy, never a link written through)
                copies += 1
        else:
            shutil.copy2(src, d)
            copies += 1
    hb = tmp / 'home' / 'base'
    # checks: names, stored overlays, shadowing
    packs = [p.name for p in hb.glob('*.pk3')]
    up = [n.upper() for n in packs]
    if len(set(up)) != len(up):
        _abandon(tmp, 'two packs share a name: %s' % sorted(n for n in packs if up.count(n.upper()) > 1))
    shadowed = {}
    order = fs_order(packs)
    members = {}
    for n in order:
        with zipfile.ZipFile(hb / n) as z:
            members[n] = {i.filename.lower(): i for i in z.infolist()}
    for rel in put:
        n = Path(rel).name
        if not n.lower().endswith('.pk3') or n not in members:
            continue
        if re.search(r'(?i)_scene', n):
            bad = [i.filename for i in members[n].values() if i.compress_type != zipfile.ZIP_STORED]
            if bad:
                _abandon(tmp, '%s: %d members deflated (a scene overlay must be stored: geometry streaming stops '
                         'otherwise), first %s' % (n, len(bad), bad[:3]))
        earlier = order[:order.index(n)]
        later = order[order.index(n) + 1:]
        shadowed[n] = {'over': sum(1 for m in members[n] for e in earlier if m in members[e]),
                       'under': sum(1 for m in members[n] for e in later if m in members[e])}
    # settings, launchers, notes
    if cfg_set:
        with open(hb / 'openjk.cfg', 'a', encoding='utf-8', newline='\r\n') as f:
            f.write('\n// kit_derive: %s\n%s\n' % (out.name, '\n'.join(cfg_set)))
    if rem:
        for b in tmp.glob('*.bat'):
            t = b.read_text(encoding='utf-8', errors='replace')
            b.write_text(t.replace('@echo off', '@echo off\nrem %s' % rem, 1), encoding='utf-8')
    if note:
        if kit_txt == 'replace':
            (tmp / 'KIT.txt').write_text(note + '\n', encoding='utf-8')
        else:
            with open(tmp / 'KIT.txt', 'a', encoding='utf-8') as f:
                f.write('\n%s\n' % note)
    write_kit_json(tmp, base, out.name)
    os.replace(tmp, out)
    return {'links': links, 'copies': copies, 'shadowed': shadowed, 'seconds': round(time.time() - t0, 1)}


def _abandon(tmp, msg):
    """stop before the rename: the partial kit is moved aside (a stopped run's folder, never deleted)"""
    tmp.rename(tmp.with_name(tmp.name + '.failed-%s' % time.strftime('%Y%m%dT%H%M%S')))
    sys.exit('kit_derive: %s' % msg)


def write_kit_json(kit, base=None, name=None):
    """kit/KIT.json: {name, made, base, files: {relative path: {size, sha256}}}; a file that is still the same file
    as one the base's KIT.json hashed keeps that hash"""
    kit = Path(kit)
    known = {}
    if base is not None and (Path(base) / 'KIT.json').exists():
        bj = json.loads((Path(base) / 'KIT.json').read_text(encoding='utf-8'))
        known = bj.get('files', {})
    files = {}
    for f in sorted(kit.rglob('*')):
        rel = f.relative_to(kit).as_posix()
        if f.is_dir() or rel == 'KIT.json' or SKIP.search(rel):
            continue
        st = f.stat()
        k = known.get(rel)
        bf = Path(base) / rel if base is not None else None
        if k and k['size'] == st.st_size and bf is not None and bf.exists() and os.path.samefile(bf, f):
            files[rel] = k
        else:
            files[rel] = {'size': st.st_size, 'sha256': sha256(f)}
    (kit / 'KIT.json').write_text(json.dumps({'name': name or kit.name, 'made': time.strftime('%Y-%m-%d %H:%M:%S'),
                                              'base': str(base) if base else None, 'files': files}, indent=1),
                                  encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base', type=Path)
    ap.add_argument('name')
    ap.add_argument('--put', action='append', default=[], metavar='REL=FILE')
    ap.add_argument('--drop', action='append', default=[], metavar='REL')
    ap.add_argument('--renderer')
    ap.add_argument('--module', action='append', default=[])
    ap.add_argument('--cfg', action='append', default=[])
    ap.add_argument('--note', default='')
    ap.add_argument('--rem', default='')
    ap.add_argument('--hash-base', action='store_true', help='only write the base kit\'s own KIT.json (its first)')
    a = ap.parse_args()
    if a.hash_base:
        write_kit_json(a.base, None, a.base.name)
        print('%s/KIT.json written' % a.base)
        return
    put = dict(p.split('=', 1) for p in a.put)
    out = a.base.parent / a.name
    r = derive(a.base, out, put, a.drop, a.renderer, a.module, a.cfg,
               a.note and '%s (kit_derive.py, %s): %s' % (a.name, time.strftime('%Y-%m-%d %H:%M'), a.note),
               a.rem or ('%s: %s with %s' % (a.name, a.base.name, ', '.join(sorted(put)) or 'its settings')))
    print('%s: %s' % (out, json.dumps(r)))


if __name__ == '__main__':
    main()
