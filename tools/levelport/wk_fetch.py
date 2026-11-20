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
"""CP2077 resources by depot path from the installed game through WolvenKit CLI into a fetch folder (the decal and effect fetches' shell scripts as a committed tool, baseline phase 2). The layout is the one
cp77decals.py and cp77fx.py read:
  <root>/raw/<depot path>       unbundled (a resource other than a texture)
  <root>/json/<name>.json       its `convert serialize` output, flat by file name (the header's ArchiveFileName names
                                the depot path); a second depot path of the same file name goes to
                                <root>/json/<depot path>.json, which both converters look up first
  <root>/png/<depot stem>.png   a texture (.xbm) uncooked to png (uncook also leaves the cooked .xbm beside it)
  <root>/embedded/              the textures embedded in fetched resources (`"Flags": "Embedded"`: in no archive, so
                                uncook misses them): json/<key>.json each as its own CR2W document, xbm/ (convert
                                deserialize), png/ (export), map.json (<key>.xbm -> depot path; key = sha1(depot)[:10]_name)
  <root>/fetch_absent.txt       paths asked for that the archives did not give (kept across runs)
  <root>/wk_fetch.log           WolvenKit's output, appended
WolvenKit takes one regex per call and Windows caps a command line at 32k characters, so the paths go in batches
(120 paths or 24,000 characters). A path already fetched is skipped: a texture whose png exists, any other resource
whose json exists (one unbundled earlier but not serialised is only serialised). The WolvenKit build, the dotnet and
the game come from cp2gltf (WOLVENKIT_CLI, WOLVENKIT_DOTNET, CP77_GAME); the decal and effect fetches
used WolvenKit 10.0.0-beta.1.
    python -I wk_fetch.py <list of depot paths> <root> [--embedded]"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402

SL = chr(92)
TEXTURE = '.xbm'
BATCH_PATHS, BATCH_CHARS = 120, 24000


def norm(p):
    return p.strip().replace('/', SL).lower()


def depot_of_archive(name):
    """A serialised file's depot path from its header's ArchiveFileName (WolvenKit writes the file it serialised:
    <...>/<depot path>), lower case; '' when no part is base, engine or ep1"""
    parts = name.replace('/', SL).split(SL)
    i = next((k for k, p in enumerate(parts) if p.lower() in ('base', 'engine', 'ep1')), None)
    return SL.join(parts[i:]).lower() if i is not None else ''


def header_depot(f):
    """The depot path a serialised json names in its header (the first 4 KB), or ''"""
    with open(f, 'rb') as fh:
        head = fh.read(4096).decode('utf-8', 'replace')
    k = head.find('"ArchiveFileName"')
    if k < 0:
        return ''
    m = re.match(r'\s*:\s*"((?:[^"\\]|\\.)*)"', head[k + len('"ArchiveFileName"'):])
    return depot_of_archive(json.loads('"%s"' % m.group(1))) if m else ''


class Fetched:
    """What a fetch folder holds, by depot path"""

    def __init__(self, root):
        self.root = Path(root)
        self.json_of = {}
        js = self.root / 'json'
        if js.is_dir():
            for f in js.glob('*.json'):
                d = header_depot(f)
                if d:
                    self.json_of[d] = f
        self.absent = set()
        a = self.root / 'fetch_absent.txt'
        if a.exists():
            self.absent = {norm(l) for l in a.read_text(encoding='utf-8').splitlines() if l.strip()}

    def json(self, p):
        p = norm(p)
        if p in self.json_of:
            return self.json_of[p]
        nested = self.root / 'json' / (p.replace(SL, '/') + '.json')
        return nested if nested.exists() else None

    def png(self, p):
        f = self.root / 'png' / (norm(p).replace(SL, '/').rsplit('.', 1)[0] + '.png')
        return f if f.exists() else None

    def has(self, p):
        return (self.png(p) if norm(p).endswith(TEXTURE) else self.json(p)) is not None


def batches(paths, n=BATCH_PATHS, chars=BATCH_CHARS):
    out, cur, size = [], [], 0
    for p in paths:
        w = len(re.escape(p)) + 1
        if cur and (len(cur) >= n or size + w > chars):
            out.append(cur)
            cur, size = [], 0
        cur.append(p)
        size += w
    return out + ([cur] if cur else [])


def regex(paths):
    return '^(' + '|'.join(re.escape(p) for p in paths) + ')$'


def run(log, *args):
    """One WolvenKit call, its output appended to the log; the exit code"""
    env = dict(os.environ, DOTNET_ROOT=str(cp2gltf.DOTNET))
    t0 = time.time()
    r = subprocess.run([str(cp2gltf.WOLVENKIT), *map(str, args)], env=env, capture_output=True, text=True,
                       errors='replace')
    with open(log, 'a', encoding='utf-8') as fh:
        fh.write('== %s %s\n' % (args[0], ' '.join(str(a) for a in args[1:2]) if args[0] == 'convert' else ''))
        fh.write(r.stdout[-20000:] + r.stderr[-4000:])
        fh.write('exit=%d (%.0f s)\n' % (r.returncode, time.time() - t0))
    if r.returncode:
        print('wk_fetch: WolvenKit %s exit %d (see %s)' % (args[0], r.returncode, log), flush=True)
    return r.returncode


def serialise(root, paths, log):
    """Serialise unbundled raw/<depot> files into json/: each call takes files of distinct names (serialize writes
    flat names), linked under _serialise/<k>/<depot path> so the header still names the depot path"""
    raw, js = root / 'raw', root / 'json'
    rounds = []
    for p in paths:
        name = p.split(SL)[-1]
        for r in rounds:
            if name not in r:
                r[name] = p
                break
        else:
            rounds.append({name: p})
    done = 0
    for k, r in enumerate(rounds):
        stage = cp2gltf.staging(js)
        src, out = stage / 'src', stage / 'out'
        out.mkdir(parents=True)
        for p in r.values():
            s, t = raw / p.replace(SL, '/'), src / p.replace(SL, '/')
            t.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(s, t)
            except OSError:
                shutil.copy2(s, t)
        run(log, 'convert', 'serialize', src, '-o', out, '-v', 'Minimal')
        for name, p in r.items():
            f = out / (name + '.json')
            if not f.exists() or f.stat().st_size == 0:
                continue
            dst = js / (name + '.json')
            if dst.exists():                 # (another depot path of this name: by its whole path)
                dst = js / (p.replace(SL, '/') + '.json')
                dst.parent.mkdir(parents=True, exist_ok=True)
            os.replace(f, dst)
            done += 1
        # (the staging folder holds only links to raw/ and what serialize wrote but nothing took; kept for a look
        # when not empty)
        for p in r.values():
            t = src / p.replace(SL, '/')
            if t.exists() and t.stat().st_nlink > 1:
                os.unlink(t)
        for d in sorted((d for d in stage.rglob('*') if d.is_dir()), key=lambda d: -len(d.parts)) + [stage]:
            if not any(d.iterdir()):
                d.rmdir()
    return done


def fetch(paths, root, log=None):
    """Fetch depot paths into root (the module's layout); returns (fetched now, absent): the paths still not there
    after this run are added to <root>/fetch_absent.txt"""
    root = Path(root)
    for d in ('raw', 'json', 'png'):
        (root / d).mkdir(parents=True, exist_ok=True)
    cp2gltf.check_cache_identity(root)
    log = Path(log or root / 'wk_fetch.log')
    have = Fetched(root)
    paths = sorted({norm(p) for p in paths if p and p.strip()})
    todo = [p for p in paths if not have.has(p)]
    tex = [p for p in todo if p.endswith(TEXTURE)]
    res = [p for p in todo if not p.endswith(TEXTURE)]
    unb = [p for p in res if not (root / 'raw' / p.replace(SL, '/')).exists()]
    print('wk_fetch: %d paths, %d to fetch (%d textures to uncook, %d resources: %d to unbundle)' % (
        len(paths), len(todo), len(tex), len(res), len(unb)), flush=True)
    # a call that exits nonzero (a crash loses its whole batch) is retried eight paths a call; what still fails
    # there is not recorded as absent from the archives (the next fetch tries it again)
    failed = set()

    def calls(verb, what, bs, *args):
        again = []
        for i, b in enumerate(bs):
            print('  %s %d/%d (%d %s)' % (verb, i + 1, len(bs), len(b), what), flush=True)
            if run(log, verb, *cp2gltf.ARCHIVES, '-r', regex(b), *args, '-v', 'Minimal'):
                again += b
        for i in range(0, len(again), 8):
            b = again[i:i + 8]
            print('  %s retry (%d %s)' % (verb, len(b), what), flush=True)
            if run(log, verb, *cp2gltf.ARCHIVES, '-r', regex(b), *args, '-v', 'Minimal'):
                failed.update(b)
    calls('unbundle', 'paths', batches(unb), '-o', root / 'raw')
    calls('uncook', 'textures', batches(tex), '-o', root / 'png', '--uext', 'png')
    ser = [p for p in res if (root / 'raw' / p.replace(SL, '/')).exists()]
    if ser:
        print('  serialise %d' % len(ser), flush=True)
        serialise(root, ser, log)
    have = Fetched(root)
    absent = [p for p in todo if not have.has(p)]
    if failed:
        print('wk_fetch: %d paths in WolvenKit calls that failed twice: not recorded as absent' % len(
            [p for p in absent if p in failed]), flush=True)
    if any(p not in failed for p in absent):
        a = root / 'fetch_absent.txt'
        keep = have.absent | {p for p in absent if p not in failed}
        a.write_text(''.join(p + '\n' for p in sorted(keep)), encoding='utf-8')
    print('wk_fetch: %d fetched, %d absent (fetch_absent.txt)' % (len(todo) - len(absent), len(absent)), flush=True)
    return len(todo) - len(absent), absent


def embedded_key(name):
    """The embedded texture's flat file name (its depot path's hash keeps names of different folders apart)"""
    return hashlib.sha1(name.encode()).hexdigest()[:10] + '_' + name.split(SL)[-1]


def embedded_entries(root):
    """{key: (depot path, the CR2W document)} for every .xbm embedded in a fetched json"""
    out = {}
    for f in sorted((Path(root) / 'json').rglob('*.json')):
        with open(f, 'rb') as fh:
            data = fh.read()
        if b'"EmbeddedFiles"' not in data or b'.xbm' not in data:
            continue
        d = json.loads(data)
        for e in d['Data'].get('EmbeddedFiles') or []:
            name = cp2gltf.cname(e.get('FileName'))
            if not name or not name.lower().endswith(TEXTURE):
                continue
            key = embedded_key(name)
            if key in out:
                continue
            out[key] = (name, {'Header': dict(d['Header'], DataType='CR2W', ArchiveFileName=''),
                               'Data': {'Version': d['Data']['Version'], 'BuildVersion': d['Data']['BuildVersion'],
                                        'RootChunk': e['Content'], 'EmbeddedFiles': []}})
    return out


def embedded_todo(root):
    """(every embedded texture {key: depot}, the keys without a png yet)"""
    E = Path(root) / 'embedded'
    every = {k: v[0] for k, v in embedded_entries(root).items()}
    return every, [k for k in every if not (E / 'png' / (k.rsplit('.', 1)[0] + '.png')).exists()]


def fetch_embedded(root, log=None):
    """The textures embedded in the fetched resources, as cp2gltf.stage_fetch_embedded recovers sector meshes: each
    one's JSON written as its own CR2W document, `convert deserialize` back to .xbm, `export --uext png` (export needs
    the game path). Writes <root>/embedded/{json,xbm,png} and map.json; skips those with a png."""
    root = Path(root)
    log = Path(log or root / 'wk_fetch.log')
    E = root / 'embedded'
    entries = embedded_entries(root)
    names = {}
    if (E / 'map.json').exists():
        names = json.load(open(E / 'map.json', encoding='utf-8'))
    for k, (name, _) in entries.items():
        names.setdefault(k, name)
    todo = [k for k in entries if not (E / 'png' / (k.rsplit('.', 1)[0] + '.png')).exists()]
    print('wk_fetch: %d embedded textures, %d to export' % (len(entries), len(todo)), flush=True)
    if todo:
        E.mkdir(parents=True, exist_ok=True)
        stage = cp2gltf.staging(E)
        src, cooked, out = stage / 'json', stage / 'xbm', stage / 'png'
        for d in (src, cooked, out):
            d.mkdir(parents=True, exist_ok=True)
        for k in todo:
            json.dump(entries[k][1], open(src / (k + '.json'), 'w', encoding='utf-8'))
        run(log, 'convert', 'deserialize', src, '-o', cooked, '-v', 'Minimal')
        run(log, 'export', cooked, '-o', out, '-gp', cp2gltf.GAME, '--uext', 'png', '-v', 'Minimal')
        st = cp2gltf.publish(stage, E)
        print('  embedded: %d files published, %d already there' % (st.get('published', 0), st.get('kept', 0)))
    E.mkdir(parents=True, exist_ok=True)
    tmp = E / 'map.json.tmp'
    json.dump(names, open(tmp, 'w', encoding='utf-8'), indent=0)
    os.replace(tmp, E / 'map.json')
    left = [k for k in entries if not (E / 'png' / (k.rsplit('.', 1)[0] + '.png')).exists()]
    if left:
        print('wk_fetch: %d embedded textures did not export: %s' % (len(left), ', '.join(left[:6])), flush=True)
    return len(todo) - len(left), left


def walk_refs(o, suffixes, out):
    """Every DepotPath value under o whose path ends in one of suffixes, into the set out (lower case)"""
    if isinstance(o, dict):
        dp = o.get('DepotPath')
        if isinstance(dp, dict):
            p = cp2gltf.cname(dp)
            if p and p.lower().endswith(suffixes):
                out.add(norm(p))
        for v in o.values():
            walk_refs(v, suffixes, out)
    elif isinstance(o, list):
        for v in o:
            walk_refs(v, suffixes, out)
    return out


def write_list(paths, f):
    f = Path(f)
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(''.join(p + '\n' for p in sorted(paths)), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('list', type=Path, help='depot paths, one a line')
    ap.add_argument('root', type=Path, help='the fetch folder')
    ap.add_argument('--embedded', action='store_true', help='then export the textures embedded in what was fetched')
    a = ap.parse_args()
    paths = [l for l in a.list.read_text(encoding='utf-8').splitlines() if l.strip()]
    fetch(paths, a.root)
    if a.embedded:
        fetch_embedded(a.root)


if __name__ == '__main__':
    sys.exit(main())
