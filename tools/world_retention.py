"""Keep retained worlds small without dropping evidence.

share_graphs: a finished world's graph snapshots (base/bot2-graph/*.txt) are stored once
per campaign under .graphs/<sha256>.txt and the world's file becomes a hard link to that
copy. Paths and contents are unchanged, so every graph tool reads the world as before;
identical snapshots (most worlds) cost nothing. Nothing is deleted.

copy_session: copytree for a live session that leaves out game binaries, which the
build manifest already identifies (and which a copy would turn from hard links into
full files).
"""
import hashlib, os, shutil, uuid
from pathlib import Path

BINARY_EXT = ('.exe', '.dll', '.pdb')


def campaign_root(world):
    world = Path(world).absolute()
    for parent in (world, *world.parents):
        if parent.parent.name == '.codex-research':
            return parent
    return world.parent


def _sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def share_graphs(base, campaign=None):
    """Hard-link each graph snapshot under base/bot2-graph to the campaign's one copy.
    Returns (files, newly stored). Safe to run concurrently for worlds of one campaign."""
    graph_dir = Path(base) / 'bot2-graph'
    if not graph_dir.is_dir():
        return 0, 0
    store = (Path(campaign) if campaign else campaign_root(graph_dir.parent.parent)) / '.graphs'
    store.mkdir(exist_ok=True)
    files = stored = 0
    for snapshot in sorted(graph_dir.glob('*.txt')):
        if not snapshot.is_file() or snapshot.is_symlink():
            continue
        files += 1
        master = store / (_sha256(snapshot) + '.txt')
        try:
            os.link(snapshot, master)
            stored += 1
            continue  # the snapshot is now the master's other name
        except FileExistsError:
            pass
        except OSError:
            continue  # no hard links here: leave the world's own copy
        if os.path.samefile(snapshot, master):
            continue
        temp = snapshot.with_name(snapshot.name + '.' + uuid.uuid4().hex + '.link')
        try:
            os.link(master, temp)  # fails past the per-file link limit; the copy then stays
            os.replace(temp, snapshot)
        except OSError:
            if temp.exists():
                temp.unlink()
    return files, stored


def copy_session(src, dst):
    """copytree without game binaries; returns dst."""
    def ignore(directory, names):
        return [n for n in names if n.lower().endswith(BINARY_EXT) or n == '.staged-binaries']
    return shutil.copytree(src, dst, ignore=ignore, dirs_exist_ok=True)


# Disk guards. Staging folders under %LOCALAPPDATA%/xenjk-play were never removed after their
# copy and filled C: (3,560 worlds, 226 GB, 2026-09-26..10-05; docs/movement/records/2026-10-05-deferral-behaviour.md).
MIN_FREE_GB = 20.0


def free_gb(path):
    """Free space on the drive holding path (or its nearest existing parent), in GB."""
    path = Path(path).absolute()
    while not path.exists() and path != path.parent:
        path = path.parent
    return shutil.disk_usage(path).free / 1e9


def require_free(*paths, min_gb=MIN_FREE_GB):
    """Refuse to start a world while any drive it writes to has under min_gb free (environment XENJK_MIN_FREE_GB
    overrides). Exits with a message naming the drive."""
    min_gb = float(os.environ.get('XENJK_MIN_FREE_GB', min_gb))
    for path in paths:
        free = free_gb(path)
        if free < min_gb:
            raise SystemExit('refusing to start: %.1f GB free on the drive holding %s (need %.0f GB; '
                             'tools/world_retention.py require_free)' % (free, path, min_gb))


def drop_staging(session, copy, marker='server-console.log'):
    """Remove a finished world's staging folder once its retained copy exists (the copy's `marker` file is checked
    first; a missing copy keeps the folder). Staged binaries are read-only, so they are made writable to remove them."""
    import stat
    session, copy = Path(session), Path(copy)
    if not (copy / marker).is_file():
        print('staging kept (no retained copy at %s): %s' % (copy, session), flush=True)
        return False
    def writable(func, path, exc):
        os.chmod(path, stat.S_IWRITE)
        func(path)
    shutil.rmtree(session, onerror=writable)
    return True
