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
"""memgate.py -- start processes on a memory reservation, not on a fixed gap.

This (used by cp77_collision) replaced the fixed 45 s between compile starts: each start reserves its
estimated peak, and the gate subtracts what the running processes have not yet grown into (each one's peak less its
process tree's private bytes now). cp77_tiles' parts start the same way here, in place of their 10 s gap.

Free commit is counted up to a cap the page file need not grow for: the commit limit grows with a system-managed page
file only when the charge reaches it, so a gate reading CommitLimit - CommitTotal passed by taking disk (the page file
was 88.7 GB on C: with 31.8 GB of RAM). The cap is the smallest limit the run has seen (limit_seen: the page file as it was) less
PAGE_MARGIN_GB, and never under physical RAM plus PAGE_ALLOW_GB: our starts use the page file there is and never make
it grow. (Capped at RAM + 16
alone, tag 12's parts ran one at a time with 37 of a 63 GB limit committed and half the RAM free.)"""
import ctypes
import os
import threading
import time

PAGE_ALLOW_GB = 16.0    # commit our starts may take beyond physical RAM at least (the page file backs it)
PAGE_MARGIN_GB = 4.0    # kept between the charge and the commit limit as it stands, so the page file does not grow
POLL_S = 2.0


class PERF(ctypes.Structure):
    _fields_ = [('cb', ctypes.c_uint32)] + [(n, ctypes.c_size_t) for n in (
        'CommitTotal', 'CommitLimit', 'CommitPeak', 'PhysicalTotal', 'PhysicalAvailable', 'SystemCache', 'KernelTotal',
        'KernelPaged', 'KernelNonpaged', 'PageSize')] + [(n, ctypes.c_uint32) for n in ('HandleCount', 'ProcessCount',
                                                                                          'ThreadCount')]


def commit():
    """(commit total, commit limit, physical total) in GB, or None off Windows"""
    if os.name != 'nt':
        return None
    p = PERF()
    p.cb = ctypes.sizeof(p)
    if not ctypes.windll.psapi.GetPerformanceInfo(ctypes.byref(p), p.cb):
        return None
    g = p.PageSize / 2 ** 30
    return p.CommitTotal * g, p.CommitLimit * g, p.PhysicalTotal * g


def commit_cap(limit, phys):
    """the commit charge our starts may reach: the limit as it stands less PAGE_MARGIN_GB (no page file growth), at
    least physical RAM plus PAGE_ALLOW_GB, never past the limit"""
    return min(limit, max(limit - PAGE_MARGIN_GB, phys + PAGE_ALLOW_GB))


_LIMIT0 = [None]


def limit_seen(limit):
    """the smallest commit limit this run has seen (GB): the run's ledger folder keeps it for every process of the run,
    else this process does. The limit grows with the page file under pressure (tag 14, 2026-11-20: 63 -> 87 GB while
    a 30 GB chunk export ran beside a game client), and a cap that followed it would let the page file go on growing."""
    import json
    d = ledger_dir()
    f = d / 'commit_limit0.txt' if d is not None else None      # (not *.json: ledger_others reads those)
    seen = _LIMIT0[0]
    if f is not None:
        try:
            seen = min(x for x in (seen, json.loads(f.read_text(encoding='utf-8'))) if x is not None)
        except (OSError, ValueError, TypeError):
            pass
    if seen is None or limit < seen:
        seen = limit
        if f is not None:
            try:
                d.mkdir(parents=True, exist_ok=True)
                tmp = f.with_name('commit_limit0.%d.tmp' % os.getpid())
                tmp.write_text(json.dumps(limit), encoding='utf-8')
                _replace(tmp, f)
            except OSError:
                pass
    _LIMIT0[0] = seen
    return min(limit, seen)


def memory():
    """(available physical GB, free commit GB): commit_cap() less the commit charge (Windows), else free swap"""
    import psutil
    avail = psutil.virtual_memory().available / 2 ** 30
    c = commit()
    if c is not None:
        total, limit, phys = c
        return avail, commit_cap(limit_seen(limit), phys) - total
    return avail, avail + psutil.swap_memory().free / 2 ** 30


def tree_private_gb(pid):
    import psutil
    used = 0.0
    try:
        root = psutil.Process(pid)
        for q in [root] + root.children(recursive=True):
            try:
                used += q.memory_info().private / 2 ** 30
            except psutil.Error:
                pass
    except psutil.Error:
        pass
    return used


# the shared ledger: when build_watson runs two lanes, its tools see each other's reservations. A
# reservation is a file <ledger>/<pid>-<n>.json {pid, peak, label}: written at the start, removed at the end; one
# whose pid has exited is ignored (a killed tool leaves no wrong reservation behind). MEMGATE_LEDGER names the folder.
LEDGER_ENV = 'MEMGATE_LEDGER'
_seq = [0]
_seq_lock = threading.Lock()


def _replace(tmp, f, tries=100):
    """tmp renamed over f, tried again for up to 2 s while Windows refuses it (another process is reading f: the other
    lane's ledger_others; a sharing violation, not a fault)"""
    for i in range(tries):
        try:
            return tmp.replace(f)
        except PermissionError:
            if i == tries - 1:
                raise
            time.sleep(0.02)


def ledger_dir():
    d = os.environ.get(LEDGER_ENV)
    return None if not d else __import__('pathlib').Path(d)


def ledger_add(label, peak, pid=None):
    """a reservation in the ledger (pid: the process tree it is for, filled in later by ledger_bind); -> its file"""
    import json
    d = ledger_dir()
    if d is None:
        return None
    d.mkdir(parents=True, exist_ok=True)
    with _seq_lock:
        _seq[0] += 1
        f = d / ('%d-%d.json' % (os.getpid(), _seq[0]))
    tmp = f.with_suffix('.tmp')
    import psutil
    born = psutil.Process().create_time()     # (a reused pid is not its owner: the create times differ)
    tmp.write_text(json.dumps({'owner': os.getpid(), 'born': born, 'pid': pid, 'peak': peak, 'label': label}),
                   encoding='utf-8')
    _replace(tmp, f)
    return f


def ledger_bind(f, pid):
    import json
    if f is None:
        return
    try:
        e = json.loads(f.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return
    e['pid'] = pid
    tmp = f.with_suffix('.tmp')
    tmp.write_text(json.dumps(e), encoding='utf-8')
    _replace(tmp, f)


def ledger_remove(f):
    if f is not None:
        try:
            f.unlink()          # (the reservation's own marker file, written by this process)
        except OSError:
            pass


def alive(pid, born):
    """the ledger file's owner still running (the same process, by its create time when the file has one)"""
    import psutil
    try:
        p = psutil.Process(pid)
        return born is None or abs(p.create_time() - born) < 1.0
    except (psutil.Error, TypeError, ValueError):
        return False


def waiting_line(label, peak, margin, avail, room, out, others, since):
    """why a start waits: the held test(s), the numbers, the commit cap's single-start mode"""
    held = []
    if avail - out < peak + margin:
        held.append('available memory %.1f GB less %.1f reserved < %.1f needed' % (avail, out, peak + margin))
    if room - out < peak + margin:
        held.append('commit room %.1f GB (the page file as it is) less %.1f reserved < %.1f needed%s' % (
            room, out, peak + margin, ': the commit cap allows one start at a time now'
            if room < 2 * (peak + margin) else ''))
    return '%s waits %.0f s: %s%s' % (label, time.time() - since, '; '.join(held),
                                     ' (%.1f GB of it the other lane\'s)' % others if others else '')


def ledger_others(live=False):
    """what other processes' reservations may still take (GB): each live one's peak less its tree's private bytes;
    live: how many there are instead (a reservation its process has outgrown still holds its memory, so a start that
    waits for room is not alone beside it: tag 15's decals started "alone" beside a 43 GB chunk export)"""
    import json
    d = ledger_dir()
    if d is None or not d.is_dir():
        return 0 if live else 0.0
    out, n = 0.0, 0
    for f in d.glob('*.json'):
        try:
            e = json.loads(f.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        if not isinstance(e, dict):     # (not a reservation)
            continue
        if e.get('owner') == os.getpid():
            continue
        if not alive(e.get('owner'), e.get('born')):
            try:
                f.unlink()          # (a reservation whose owner has exited: it holds nothing)
            except OSError:
                pass
            continue
        n += 1
        used = tree_private_gb(e['pid']) if e.get('pid') else 0.0
        out += max(0.0, float(e['peak']) - used)
    return n if live else out


class Gate:
    """one start at a time: a start waits until available memory and free commit, less what the running starts may
    still take, hold its peak plus margin_gb; it is reserved until release(). A start with nothing of ours running
    goes ahead (waiting would never end)."""

    def __init__(self, margin_gb=2.0, log=print):
        self.margin, self.log = margin_gb, log
        self.started = threading.Lock()
        self.lock = threading.Lock()
        self.running = {}           # label -> [peak GB, pid or None]
        self.ledger = {}            # label -> its ledger file (MEMGATE_LEDGER set)

    def outstanding(self):
        with self.lock:
            runs = list(self.running.values())
        return sum(max(0.0, peak - (tree_private_gb(pid) if pid is not None else 0.0)) for peak, pid in runs) +             ledger_others()

    def wait_start(self, label, peak):
        with self.started:
            since, said = time.time(), 0.0
            while True:
                avail, room = memory()
                out = self.outstanding()
                if avail - out >= peak + self.margin and room - out >= peak + self.margin:
                    break
                others = ledger_others()
                with self.lock:
                    alone = not self.running and not ledger_others(live=True)   # (the other lane's count)
                if not alone and time.time() - said >= 60:
                    self.log(waiting_line(label, peak, self.margin, avail, room, out, others, since))
                    said = time.time()
                if alone:
                    self.log('%s starts alone: %.1f GB available, %.1f GB free commit, its peak taken as %.1f GB' % (
                        label, avail, room, peak))
                    break
                time.sleep(POLL_S)
            with self.lock:
                self.running[label] = [peak, None]
                self.ledger[label] = ledger_add(label, peak)
            return avail, room, out

    def bind(self, label, pid):
        with self.lock:
            if label in self.running:
                self.running[label][1] = pid
            ledger_bind(self.ledger.get(label), pid)

    def release(self, label):
        with self.lock:
            self.running.pop(label, None)
            ledger_remove(self.ledger.pop(label, None))
