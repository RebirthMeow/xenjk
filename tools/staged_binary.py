"""Stage immutable EXE/DLL snapshots through campaign-local hardlink masters.

Build outputs are never linked. Persistent lock files and failed partial files
are retained; this module never deletes or overwrites an existing file.
"""
from contextlib import contextmanager
from datetime import datetime, timezone
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import time
import warnings

MAX_WORLD_LINKS = 1000


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def is_redirect(path):
    return path.is_symlink() or bool(
        getattr(path.lstat(), 'st_file_attributes', 0) & 0x400)


def cache_root(destination, campaign_root=None):
    if campaign_root is not None:
        return Path(campaign_root).resolve() / '.staged-binaries'
    destination = Path(destination).absolute()
    for parent in destination.parents:
        if parent.parent.name == '.codex-research':
            return parent / '.staged-binaries'
    return destination.parent / '.staged-binaries'


@contextmanager
def locked(path, timeout=120):
    # OS locks release on crash; the lock file intentionally remains on disk.
    with path.open('a+b') as stream:
        stream.seek(0, 2)
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        deadline = time.monotonic() + timeout
        while True:
            stream.seek(0)
            try:
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Timed out waiting for binary staging lock: ' + str(path))
                time.sleep(.02)
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def copy_exclusive(source, destination):
    with Path(source).open('rb') as src, Path(destination).open('xb') as dst:
        shutil.copyfileobj(src, dst)
    shutil.copystat(source, destination)


def stage_binary(source, destination, *, campaign_root=None):
    """Return staging metadata; serialize master allocation across processes.

Outside .codex-research, callers can supply campaign_root to share across
worlds. Otherwise each destination directory has its own conservative cache.
"""
    source, destination = Path(source), Path(destination).absolute()
    if source.suffix.lower() not in ('.exe', '.dll'):
        raise ValueError('Only immutable staged EXE/DLL files may be linked')
    if os.path.lexists(destination):
        raise FileExistsError(destination)
    root = cache_root(destination, campaign_root)
    # Refuse links/junctions in the cache ancestry rather than writing through
    # an unexpected redirect. Never recursively mutate cache directories.
    for path in (root, *root.parents):
        if os.path.lexists(path) and is_redirect(path):
            raise ValueError('Reparse point in staging cache: ' + str(path))
    root.mkdir(parents=True, exist_ok=True)
    expected = digest(source)
    group = root / (source.name + '-' + expected)
    group.mkdir(exist_ok=True)
    if is_redirect(group):
        raise ValueError('Reparse point in staging group: ' + str(group))
    if os.path.lexists(root / 'staging.lock') and is_redirect(root / 'staging.lock'):
        raise ValueError('Reparse point at staging lock')
    with locked(root / 'staging.lock'):
        if os.path.lexists(destination):
            raise FileExistsError(destination)
        index = 0
        while True:
            master = group / (str(index) + source.suffix)
            if os.path.lexists(master) and is_redirect(master):
                raise ValueError('Reparse point at staging master: ' + str(master))
            if not master.exists():
                copy_exclusive(source, master)
                if digest(master) != expected:
                    raise RuntimeError('Build changed during snapshot; partial master retained: ' + str(master))
                master.chmod(stat.S_IREAD | stat.S_IRGRP | stat.S_IROTH |
                             (source.stat().st_mode & 0o111))
            if master.is_symlink() or digest(master) != expected:
                raise RuntimeError('Immutable staging master changed: ' + str(master))
            if master.stat().st_nlink <= MAX_WORLD_LINKS:
                break
            index += 1
        mode, reason = 'hardlink', None
        try:
            os.link(master, destination)
        except OSError as error:
            allowed = {errno.EXDEV, errno.EMLINK, errno.ENOSYS, errno.ENOTSUP,
                       errno.EPERM, errno.EACCES}
            if error.errno not in allowed and getattr(error, 'winerror', None) not in (1, 17, 50, 1142):
                raise
            # Exclusive creation prevents an EEXIST/race from overwriting a
            # retained world or writing through an already-existing hardlink.
            copy_exclusive(master, destination)
            mode, reason = 'copy', str(error)
            warnings.warn('Binary hardlink unavailable; staged private copy: ' + str(destination) + ': ' + reason)
        if digest(destination) != expected:
            raise RuntimeError('Staged binary hash mismatch: ' + str(destination))
        record = dict(source=str(source.resolve()), destination=str(destination),
                      master=str(master), sha256=expected, mode=mode, fallback_reason=reason,
                      timestamp_utc=datetime.now(timezone.utc).isoformat())
        if os.path.lexists(root / 'staging.jsonl') and is_redirect(root / 'staging.jsonl'):
            raise ValueError('Reparse point at staging journal')
        with (root / 'staging.jsonl').open('a', encoding='utf-8') as journal:
            journal.write(json.dumps(record) + '\n')
        return record
