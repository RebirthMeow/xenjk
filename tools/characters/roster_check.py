"""roster_check.py: the character picker's roster (codemp/ui/ui_roster.c) built from the installed packs, and checked.

    python tools/characters/roster_check.py [--base GameData/base] [--manifests DIR] [--out roster.txt]

1. The model grid's entries, as UI_BuildQ3Model_List finds them: every models/players/<folder>/ directory entry
   (pk3 directory entries and loose folders), each .skin there whose icon_<skin>.jpg/.png/.tga exists, named
   "folder/skin" after the skin file's first underscore.
2. Each folder's character.cfg: the packs in load order (a later pack name wins), then --manifests DIR/<folder>/
   character.cfg on top (manifests not installed yet).
3. ui_roster.c itself, compiled with gcc into the scratch folder (roster_driver.c), groups them.
4. Checks: every grid entry is listed exactly once (a variant or one of its team colours) unless its folder is
   hidden; every character has a variant; no character has two variants of the same name; nothing dropped.

Exit status 1 when a check fails. Read-only: nothing under --base is written.
"""
import argparse
import collections
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pack_order import pack_sort_key  # noqa: E402  (the engine's pk3 order)
GRID_MAX = 1024     # MAX_Q3PLAYERMODELS
ICON_EXTS = ('jpg', 'png', 'tga')


def default_base():
    """the checkout's installed game; a worktree has none, so its main checkout's"""
    if (ROOT / 'game_directory').is_dir():
        root = ROOT
    else:
        common = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--git-common-dir'], capture_output=True, text=True).stdout.strip()
        root = (ROOT / common).resolve().parent if common else ROOT
    return root / 'game_directory' / 'Jedi Academy' / 'GameData' / 'base'


def installed(base):
    """(listed folders, {folder: {file: source}}, {folder: cfg bytes}) as the game resolves them"""
    listed = {}
    files = collections.defaultdict(dict)
    cfg = {}
    loose = base / 'models' / 'players'
    if loose.is_dir():
        for d in sorted(loose.iterdir(), key=lambda p: p.name.lower()):
            if d.is_dir():
                listed.setdefault(d.name.lower(), d.name)
                for f in d.iterdir():
                    if f.is_file():
                        files[d.name.lower()][f.name.lower()] = f.name
                        if f.name.lower() == 'character.cfg':
                            cfg[d.name.lower()] = f.read_bytes()
    for p in sorted(base.glob('*.pk3'), key=pack_sort_key):     # later names are searched first: they win
        try:
            z = zipfile.ZipFile(p)
        except zipfile.BadZipFile:
            continue
        for info in z.infolist():
            parts = info.filename.replace('\\', '/').split('/')
            if len(parts) != 4 or parts[0].lower() != 'models' or parts[1].lower() != 'players' or not parts[2]:
                continue
            folder = parts[2].lower()
            if parts[3] == '':
                listed.setdefault(folder, parts[2])
                continue
            files[folder][parts[3].lower()] = parts[3]
            if parts[3].lower() == 'character.cfg':
                cfg[folder] = z.read(info)
    return listed, files, cfg


def grid_entries(listed, files):
    """UI_BuildQ3Model_List's q3HeadNames"""
    out = []
    seen = set()
    for folder in sorted(listed):
        name = listed[folder]
        have = files.get(folder, {})
        for fn in sorted(f for f in have if f.endswith('.skin')):
            skin = have[fn][:-5]
            k = skin.find('_')
            if k < 0:
                continue        # the grid would name it "folder<skin>" with an icon check one letter in: never matches ours
            skin = skin[k + 1:]
            if not any('icon_%s.%s' % (skin.lower(), e) in have for e in ICON_EXTS):
                continue
            entry = '%s/%s' % (name, skin)
            if entry.lower() in seen:
                continue
            seen.add(entry.lower())
            out.append(entry)
            if len(out) >= GRID_MAX:
                return out
    return out


def build_driver(scratch):
    exe = scratch / 'roster_driver.exe'
    src = [ROOT / 'tools' / 'characters' / 'roster_driver.c', ROOT / 'codemp' / 'ui' / 'ui_roster.c',
           ROOT / 'codemp' / 'qcommon' / 'q_shared.c', ROOT / 'shared' / 'qcommon' / 'q_string.c',
           ROOT / 'shared' / 'qcommon' / 'q_math.c', ROOT / 'shared' / 'qcommon' / 'q_color.c']
    cmd = ['gcc', '-m32', '-O1', '-w', '-I%s' % (ROOT / 'codemp'), '-I%s' % (ROOT / 'shared'), '-o', str(exe)] + [str(s) for s in src]
    subprocess.run(cmd, check=True)
    return exe


def parse(text):
    chars, variants, stats = [], [], {}
    for line in text.splitlines():
        if line.startswith('C '):
            head, name = line.split(' "', 1)
            _, idx, cid, source, order, nvar = head.split()
            chars.append({'id': cid, 'source': source, 'order': int(order), 'n': int(nvar), 'name': name.rstrip('"'), 'variants': []})
        elif line.startswith('V '):
            head, name = line.split(' "', 1)
            _, ci, folder, skin, red, blue = head.split()
            v = {'character': int(ci), 'folder': folder, 'skin': skin, 'red': None if red == '-' else red,
                 'blue': None if blue == '-' else blue, 'name': name.rstrip('"')}
            variants.append(v)
            chars[int(ci)]['variants'].append(v)
        elif line.startswith('S '):
            w = line.split()
            stats = {w[1]: int(w[2]), w[3]: int(w[4])}
    return chars, variants, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--base', type=Path, default=None)
    ap.add_argument('--manifests', type=Path, help='character.cfg files not installed yet: DIR/<folder>/character.cfg')
    ap.add_argument('--out', type=Path, help='write the roster listing here')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    a.base = a.base or default_base()
    if not a.base.is_dir():
        sys.exit('no game folder at %s' % a.base)
    listed, files, cfg = installed(a.base)
    if a.manifests:
        for d in a.manifests.iterdir():
            f = d / 'character.cfg'
            if f.is_file():
                cfg[d.name.lower()] = f.read_bytes()
    entries = grid_entries(listed, files)

    scratch = Path(tempfile.mkdtemp(prefix='roster_'))
    (scratch / 'cfg').mkdir()
    (scratch / 'entries.txt').write_text(''.join(e + '\n' for e in entries), encoding='utf-8')
    folders = {e.split('/')[0].lower(): e.split('/')[0] for e in entries}
    for low, name in folders.items():
        if low in cfg:
            (scratch / 'cfg' / (name + '.cfg')).write_bytes(cfg[low])
    exe = build_driver(scratch)
    run = subprocess.run([str(exe), str(scratch / 'entries.txt'), str(scratch / 'cfg')], capture_output=True, text=True)
    if run.returncode:
        print(run.stderr)
        sys.exit('roster_driver failed (%d)' % run.returncode)
    chars, variants, stats = parse(run.stdout)

    problems = []
    hidden = {low for low in folders if low in cfg and any(l.strip().lower() == 'hide' for l in cfg[low].decode('utf-8', 'replace').splitlines())}
    count = collections.Counter()
    for v in variants:
        for s in (v['skin'], v['red'], v['blue']):
            if s:
                count[('%s/%s' % (v['folder'], s)).lower()] += 1
    for e in entries:
        n = count[e.lower()]
        if e.split('/')[0].lower() in hidden:
            if n:
                problems.append('hidden but listed: %s' % e)
        elif n != 1:
            problems.append('%s listed %d times' % (e, n))
    extra = set(count) - {e.lower() for e in entries}
    problems += ['listed but not in the grid: %s' % e for e in sorted(extra)]
    for c in chars:
        if not c['variants']:
            problems.append('character %s has no variant' % c['id'])
        names = collections.Counter(v['name'].lower() for v in c['variants'])
        problems += ['character %s: two variants named "%s"' % (c['id'], n) for n, k in names.items() if k > 1]
        problems += ['character %s: a variant with no name' % c['id'] for v in c['variants'] if not v['name']]
    if stats.get('dropped'):
        problems.append('%d entries dropped at the roster limits' % stats['dropped'])

    lines = []
    for c in chars:
        lines.append('%-24s %-8s %3d  %s' % (c['name'], c['source'], len(c['variants']),
                                             ', '.join('%s (%s/%s%s)' % (v['name'], v['folder'], v['skin'],
                                                                         ' +teams' if v['red'] or v['blue'] else '') for v in c['variants'])))
    summary = '%d grid entries in %d folders (%d with character.cfg) -> %d characters, %d variants; %d hidden' % (
        len(entries), len(folders), sum(1 for f in folders if f in cfg), len(chars), len(variants), stats.get('hidden', 0))
    if a.out:
        a.out.write_text(summary + '\n' + '\n'.join(lines) + '\n' + ''.join('PROBLEM ' + p + '\n' for p in problems), encoding='utf-8')
    if not a.quiet:
        print('\n'.join(lines))
    print(summary)
    for p in problems:
        print('PROBLEM', p)
    print('roster check:', 'FAIL' if problems else 'ok')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
