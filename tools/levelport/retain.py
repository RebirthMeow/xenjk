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
"""Keep the level-port research tree's disk use bounded: move superseded builds to .codex-trash (never delete).

    python tools/levelport/retain.py                # dry run: what moves, what space returns
    python tools/levelport/retain.py --apply        # move it to .codex-trash/<stamp>-levelport-retain/
    python tools/research_shed.py --research .codex-research/levelport --restore <stamp>     # undo

A build is .codex-research/levelport/build/<area>-<n>[-variant] (cp_kabuki_scene-24, nier_camp-7-gamma; gltf2map's
default <area>-<date>-<time> counts as numbered, newer than any plain number). Kept whole: every build
GameData/base links a file of (installed), each area's --keep newest numbered builds, any build whose files are
newer than its area's newest numbered one (an experiment in flight) and --keep-build names. Compile staging
(stage/) stays only in each area's newest build; a chunk build's per-tile staging (<tile>/stage, <tile>/tiles) moves
from any build not kept whole once that tile's pack is written (one tag's chunks were 54 GB, 2.4 of it packs).
From the rest everything moves but logs and small records
(KEEP_EXT). Look runs hard-link the packs they test, so every other link to a moved file moves with it: otherwise
the trash would hold names, not space (1,193 of 1,198 packs under look/ were links).

A work folder's collision glbs (cp77/<w>/<w>-<n>.glb) are kept when a kept build was made from one (its
source.txt, written by gltf2map) or it is the folder's newest; the rest move. Moves are renames on the same
volume; space returns only when a person empties .codex-trash.

Live work never moves (file times alone missed a compile between passes and a check reading a
superseded build). A build is kept whole when a running process's command line names it (a q3map2 compile, a
levelcheck reading its packs, a game client started from a kit) or when it holds a RUNNING file whose pid is alive;
a RUNNING file under look/ protects its own folder, and either kind protects the paths it lists. A RUNNING file has
one `pid <n>` line per process and any number of paths (absolute, or relative to the level port); one whose pids
have all exited is reported and ignored. Names and patterns in .codex-research/KEEP.txt (research_shed.py's list)
keep the builds and look folders they match whole, and --keep-build always keeps a build whole, staging included.
"""
import argparse, os, re, sys, time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import paths as lpaths  # noqa: E402

ROOT = lpaths.REPO
LP = lpaths.LEVELPORT
GAME = lpaths.GAMEDATA / 'base'
TRASH = ROOT / '.codex-trash'


def trash_for(research):
    """The trash on the research tree's own volume, so a move stays a rename: the level port may be a junction to
    another volume than the repository's (whose .codex-trash is on the repository's), and its trash is then the
    .codex-trash beside the level port's own folder, emptied by a person like the other."""
    real = Path(os.path.realpath(research))
    if os.path.splitdrive(str(real))[0].lower() == os.path.splitdrive(str(TRASH))[0].lower():
        return TRASH
    return real.parent / '.codex-trash'
KEEP_EXT = {'.log', '.json', '.txt', '.md', '.sh', '.py', '.cfg', '.ps1', '.bat'}
WHOLE = ('asked', 'in flight', 'running', 'in use', 'KEEP.txt')     # kept builds whose staging stays too
CHUNK_STAGING = ('stage', 'tiles')     # gltf2map --col-chunk's staging beside a chunk's pack
NAME = re.compile(r'^(?P<area>[a-z0-9_]+)-(?P<rest>.+)$')
STAMP = re.compile(r'^(\d{8})-(\d{6})$')       # gltf2map's default --out


def number(rest):
    """A numbered build's rank (None: a variant)."""
    if rest.isdigit():
        return int(rest)
    m = STAMP.match(rest)
    return int(m[1] + m[2]) if m else None


def built_at(path):
    """The newest file time under a build (a folder's own time changes when files move out of it)."""
    return max((os.stat(long(p)).st_mtime for p, _, _ in files(path)), default=0)


_chunk_done = {}


def chunk_done(tile):
    """a chunk build's tile folder holds its pack (or gltf2map's empty_chunk.txt): its compile is over"""
    if tile not in _chunk_done:
        _chunk_done[tile] = tile.is_dir() and (any(tile.glob('zz_chunk_*.pk3')) or (tile / 'empty_chunk.txt').exists())
    return _chunk_done[tile]


def long(p):
    p = os.path.abspath(p)
    return p if os.name != 'nt' or p.startswith('\\\\?\\') else '\\\\?\\' + p


def is_junction(p):
    """a directory junction or other reparse point (Python before 3.12 takes a junction for a plain folder)"""
    if hasattr(os.path, 'isjunction') and os.path.isjunction(p):
        return True
    try:
        return bool(getattr(os.lstat(p), 'st_file_attributes', 0) & 0x400)    # FILE_ATTRIBUTE_REPARSE_POINT
    except OSError:
        return False


def files(top):
    """(path, identity, size) of every file under top; hard links share an identity. A junction below top is not
    walked: its files belong to the tree it points at."""
    for dp, dns, fns in os.walk(long(top)):
        dns[:] = [d for d in dns if not is_junction(os.path.join(dp, d))]
        for f in fns:
            p = os.path.join(dp, f)
            try:
                st = os.stat(p, follow_symlinks=False)
            except OSError:
                continue
            yield p[4:] if p.startswith('\\\\?\\') else p, (st.st_dev, st.st_ino), st.st_size


def sample_hash(path, n=1 << 20):
    """a file's first and last n bytes, hashed: enough to tell same-named packs of the same size apart"""
    import hashlib
    h = hashlib.sha1()
    with open(long(path), 'rb') as f:
        h.update(f.read(n))
        f.seek(max(0, os.path.getsize(long(path)) - n))
        h.update(f.read(n))
    return h.hexdigest()


REGISTRY = LP / 'runhomes' / 'registry.tsv'     # time<TAB>the home's path (engine_trace*.py append a line per home)


def run_homes():
    """runhomes/ under the level port and the homes the registry names that still exist, each once"""
    out, seen = [], set()
    cand = [LP / 'runhomes']
    if REGISTRY.exists():
        for line in REGISTRY.read_text(encoding='utf-8', errors='replace').splitlines():
            parts = line.split('\t')
            if len(parts) >= 2:
                cand.append(Path(parts[1]))
    for c in cand:
        try:
            r = os.path.realpath(c)
        except OSError:
            continue
        if os.path.isdir(r) and r.lower() not in seen and not any(r.lower().startswith(x + os.sep) for x in seen):
            seen.add(r.lower())
            out.append(r)
    return out


def load_keep(research, keep=None):
    """.codex-research/KEEP.txt as research_shed.py reads it: names, and re:<regex> patterns. A missing list stops the
    run (from a worktree, whose .codex-research has no KEEP.txt, a dry run would list kept builds to
    move); --keep-file names another"""
    names, patterns = set(), []
    keep = Path(keep) if keep else research / 'KEEP.txt'
    if not keep.exists():
        sys.exit('retain: no keep list at %s (run from the main checkout, or give --keep-file)' % keep)
    for line in keep.read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            (patterns.append(re.compile(line[3:])) if line.startswith('re:') else names.add(line))
    # the level-port entries are written as paths under .codex-research (levelport/build/<name>), the builds compared
    # here by name: such an entry once matched nothing, and retain moved a kept build's binaries and packs
    # (a kit then lacked SDL2.dll)
    names |= {n.rsplit('/', 1)[-1] for n in names if n.startswith('levelport/')}
    return lambda name: name in names or any(p.search(name) for p in patterns)


def command_lines():
    """every running process's (command line, working directory), the line lower-cased with forward slashes"""
    try:
        import psutil
    except ImportError:
        print('retain: no psutil, so running processes are not checked', file=sys.stderr)
        return []
    out = []
    for p in psutil.process_iter(['cmdline', 'cwd']):
        try:
            line, cwd = ' '.join(p.info.get('cmdline') or []), p.info.get('cwd') or ''
        except (psutil.Error, OSError):
            continue
        out.append((line.replace('\\', '/').lower(), cwd))
    return out


BUILD_REF = re.compile(r'levelport/build/([a-z0-9_]+-[^/\s,"]+)')


def builds_named(cmds):
    """the level-port builds a command line names (through either path to the level port: the D: junction or C:)"""
    return {m for c, cwd in cmds for m in BUILD_REF.findall(c + ' ' + cwd.replace('\\', '/').lower() + '/')}


def pid_alive(pid):
    try:
        import psutil
        return psutil.pid_exists(pid)
    except ImportError:
        return True         # unknown: treat the work as live


def leases(top):
    """RUNNING files under top (two levels down): (folder, alive, listed paths)"""
    out = []
    for dp, dns, fns in os.walk(top):
        depth = os.path.relpath(dp, top).count(os.sep)
        if depth >= 2:
            dns[:] = []
        if 'RUNNING' not in fns:
            continue
        pids, paths = [], []
        for line in Path(dp, 'RUNNING').read_text(encoding='utf-8', errors='replace').splitlines():
            w = line.split()
            if len(w) == 2 and w[0] == 'pid' and w[1].isdigit():
                pids.append(int(w[1]))
            elif line.strip():
                paths.append(Path(line.strip()) if os.path.isabs(line.strip()) else LP / line.strip())
        alive = not pids or any(pid_alive(n) for n in pids)
        if not alive:
            print(f'retain: stale RUNNING (pids {pids} exited): {dp}', file=sys.stderr)
        out.append((Path(dp), alive, paths))
    return out


def plan(builds_dir, look_dir, game_dir, keep, keep_names, work_dir, keep_list=lambda name: False, cmds=()):
    """Which builds stay, and every path that moves (path -> kind)."""
    live = set()            # identities a running process, a RUNNING file or KEEP.txt holds

    def hold(path):
        if os.path.isdir(path):
            live.update(ident for _, ident, _ in files(path))
        elif os.path.exists(path):
            st = os.stat(long(path))
            live.add((st.st_dev, st.st_ino))
    in_use = builds_named(cmds)
    lease_dirs = set()
    for d, alive, paths in leases(builds_dir) + leases(look_dir):
        if alive:
            lease_dirs.add(os.path.normcase(os.path.realpath(d)))
            for q in [d] + paths:
                hold(q)
    for e in (os.scandir(look_dir) if look_dir.exists() else []):
        if e.is_dir(follow_symlinks=False) and keep_list(e.name):
            hold(e.path)
    # look paths a running command names (a check reading packs under look/): each token that resolves there
    look_real = os.path.normcase(os.path.realpath(look_dir))
    for c, cwd in cmds:
        for tok in re.split(r'[\s,"=]+', c):
            if '/look/' not in tok:
                continue
            q = tok if os.path.isabs(tok) else os.path.join(cwd, tok)     # relative to that process, not to us
            if os.path.exists(q) and os.path.normcase(os.path.realpath(q)).startswith(look_real):
                hold(q)
    game = list(files(game_dir))
    installed = {ident for _, ident, _ in game}
    # and by content: since the level port moved to C: its builds and GameData's installed copies are
    # on different volumes, so they share no hard link; a build file of an installed pack's name and size whose
    # sampled hash matches is that pack
    by_name = defaultdict(list)
    for gp, _, gsize in game:
        by_name[(os.path.basename(gp).lower(), gsize)].append(gp)
    hashes = {}

    def same_content(p, size):
        cands = by_name.get((os.path.basename(p).lower(), size))
        if not cands:
            return False
        for gp in cands:
            if gp not in hashes:
                hashes[gp] = sample_hash(gp)
        return sample_hash(p) in {hashes[gp] for gp in cands}
    copies = set()      # identities of build files that are copies of an installed pack
    areas = defaultdict(list)
    for e in os.scandir(builds_dir):
        m = NAME.match(e.name) if e.is_dir(follow_symlinks=False) and not is_junction(e.path) else None
        if m:
            areas[m['area']].append(e)
    kept, newest, moves = {}, set(), {}
    for area, es in areas.items():
        plain = sorted((e for e in es if number(NAME.match(e.name)['rest']) is not None),
                       key=lambda e: -number(NAME.match(e.name)['rest']))
        if plain:
            newest.add(plain[0].name)
        last = built_at(plain[0].path) if plain else 0
        for e in plain[:keep]:
            kept[e.name] = 'newest'
        for e in es:
            if e.name in keep_names:
                kept[e.name] = 'asked'          # over 'newest': an explicit keep keeps the staging too
            elif os.path.normcase(os.path.realpath(e.path)) in lease_dirs:
                kept[e.name] = 'running'
            elif e.name.lower() in in_use:
                kept[e.name] = 'in use'
            elif keep_list(e.name):
                kept[e.name] = 'KEEP.txt'
            elif e not in plain and built_at(e.path) > last:
                kept.setdefault(e.name, 'in flight')
    links = defaultdict(list)
    for p, ident, size in files(look_dir):
        links[ident].append(p)
    # engine-check run homes link the packs they test too (hundreds of them, 65 GB kept alive once): the
    # level port's runhomes/ and every home its registry lists (engine_trace.py, engine_trace_ded.py write a line)
    for home in run_homes():
        for p, ident, size in files(home):
            links[ident].append(p)
    sizes = {}
    protected = set()       # identities a kept build holds outside what it gives up (its old staging)
    for area, es in areas.items():
        for e in es:
            fs = list(files(e.path))
            for p_, ident, size in fs:
                if ident not in installed and same_content(p_, size):
                    copies.add(ident)
            if e.name not in kept and any(ident in installed or ident in copies for _, ident, _ in fs):
                kept[e.name] = 'installed'
            for p, ident, size in fs:
                sizes[ident] = size
                links[ident].append(p)
                sub = os.path.relpath(p, e.path).split(os.sep)
                if e.name not in kept:
                    if sub[0] == 'stage' or os.path.splitext(p)[1].lower() not in KEEP_EXT:
                        moves[p] = ('superseded build', ident)
                elif len(sub) > 2 and sub[1] in CHUNK_STAGING and kept[e.name] not in WHOLE and \
                        chunk_done(Path(e.path) / sub[0]):
                    # a finished chunk's q3map2 staging, even in the newest build: nothing reads it once the
                    # chunk's pack is written (gltf2map --grid-chunks reads a chunk's BSP from its pack without it)
                    moves[p] = ('chunk staging of a kept build', ident)
                elif sub[0] != 'stage' or e.name in newest or kept[e.name] in WHOLE:
                    protected.add(ident)
                else:
                    # (--keep-build, live work and an experiment in flight stay whole: a compile may be between its
                    # passes)
                    moves[p] = ('staging of a kept build', ident)
    # work folders' collision glbs: a kept build's source, and each folder's newest
    sources = set()
    for name in kept:
        src = builds_dir / name / 'source.txt'
        if src.exists():
            sources.add(os.path.normcase(os.path.realpath(src.read_text(encoding='utf-8').splitlines()[0].strip())))
    for w in (os.scandir(work_dir) if work_dir.exists() else []):
        glbs = []
        for e in (os.scandir(w.path) if w.is_dir(follow_symlinks=False) and not is_junction(w.path) else []):
            m = re.fullmatch(re.escape(w.name) + r'-(\d+)\.glb', e.name)
            if m and e.is_file(follow_symlinks=False):
                glbs.append((int(m[1]), e))
        glbs.sort(key=lambda ne: -ne[0])
        for n, e in glbs[1:]:
            if os.path.normcase(os.path.realpath(e.path)) not in sources:
                st = os.stat(long(e.path), follow_symlinks=False)     # DirEntry.stat has no identity on Windows
                ident = (st.st_dev, st.st_ino)
                sizes[ident] = st.st_size
                links[ident].append(e.path)
                moves[e.path] = ('superseded glb', ident)
    for p, (kind, ident) in list(moves.items()):
        if ident in installed or ident in copies or ident in protected or ident in live:
            # never: GameData plays it, a kept build holds it, or live work reads it
            del moves[p]
            continue
        for q in links[ident]:
            moves.setdefault(q, ('look-run link to a moved file', ident))
    freed = sum(sizes[i] for i in {i for _, i in moves.values()} if all(q in moves for q in links[i]))
    return kept, moves, freed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--apply', action='store_true', help='move to the trash (default: dry run)')
    ap.add_argument('--keep', type=int, default=1, help="newest numbered builds kept in each area (default 1)")
    ap.add_argument('--keep-build', action='append', default=[], metavar='NAME', help='keep this build whole')
    ap.add_argument('--keep-file', help='the keep list (default <checkout>/.codex-research/KEEP.txt)')
    a = ap.parse_args(argv)
    t0 = time.time()
    if not GAME.is_dir():       # (no installed packs to see: every installed build would lose its protection)
        sys.exit('retain: no game folder at %s (set $XENJK_GAMEDATA, or run from the main checkout)' % GAME)
    kept, moves, freed = plan(LP / 'build', LP / 'look', GAME, a.keep, set(a.keep_build), LP / 'cp77',
                              keep_list=load_keep(lpaths.RESEARCH, a.keep_file), cmds=command_lines())
    if a.apply:     # live work may have started during the plan: a build named now stays
        now = builds_named(command_lines())
        for p in list(moves):
            rel = os.path.relpath(p, LP / 'build')
            if not rel.startswith('..') and Path(rel).parts[0].lower() in now:
                del moves[p]
    for name in sorted(kept):
        print(f'keep {name} ({kept[name]})')
    tally = defaultdict(lambda: [0, 0])
    for p, (kind, _) in moves.items():
        tally[kind][0] += 1
        tally[kind][1] += os.stat(long(p)).st_size
    for kind, (n, b) in sorted(tally.items(), key=lambda kv: -kv[1][1]):
        print(f'move {kind}: {n} files, {b / 1e9:.2f} GB as listed')
    print(f'{freed / 1e9:.2f} GB returns once the trash is emptied ({time.time() - t0:.0f} s)')
    if not a.apply or not moves:
        return
    dest = trash_for(LP) / (time.strftime('%Y%m%dT%H%M%S') + '-levelport-retain')
    dest.mkdir(parents=True)
    skipped = 0
    with open(dest / 'MANIFEST.tsv', 'w', encoding='utf-8') as man:     # research_shed.py's format, LP-relative
        for p, (kind, _) in sorted(moves.items()):
            try:
                rel = os.path.relpath(p, LP)
            except ValueError:          # (another drive)
                rel = None
            if rel is None or rel.startswith('..'):
                # a link outside the level port (a run home the registry names elsewhere): left in place, as the
                # manifest's paths are relative to the level port
                print(f'skip {p}: outside the level port', file=sys.stderr)
                skipped += 1
                continue
            try:
                size = os.stat(long(p)).st_size
                os.makedirs(long(dest / rel).rsplit(os.sep, 1)[0], exist_ok=True)
                os.rename(long(p), long(dest / rel))
                man.write(f'{kind}\t{size}\t{rel}\n')
            except OSError as e:          # a locked file: it stays, the rest moves
                print(f'skip {rel}: {e}', file=sys.stderr)
                skipped += 1
    print(f'moved {len(moves) - skipped}, skipped {skipped} -> {dest}')


if __name__ == '__main__':
    main()
