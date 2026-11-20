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

Free commit is counted only up to physical RAM plus PAGE_ALLOW_GB: the commit limit grows with a
system-managed page file, so a gate reading CommitLimit - CommitTotal passed by taking disk (the page file was 88.7 GB
on C: with 31.8 GB of RAM). Our starts never push the commit charge past that cap."""
import ctypes
import os
import threading
import time

PAGE_ALLOW_GB = 16.0    # commit our starts may take beyond physical RAM (the page file backs it)
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


def memory():
    """(available physical GB, free commit GB): the commit limit, capped at physical RAM plus PAGE_ALLOW_GB, less the
    commit charge (Windows), else free swap"""
    import psutil
    avail = psutil.virtual_memory().available / 2 ** 30
    c = commit()
    if c is not None:
        total, limit, phys = c
        return avail, min(limit, phys + PAGE_ALLOW_GB) - total
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
    tmp.replace(f)
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
    tmp.replace(f)


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
        held.append('commit room %.1f GB (capped at RAM + %g) less %.1f reserved < %.1f needed%s' % (
            room, PAGE_ALLOW_GB, out, peak + margin, ': the commit cap allows one start at a time now'
            if room < 2 * (peak + margin) else ''))
    return '%s waits %.0f s: %s%s' % (label, time.time() - since, '; '.join(held),
                                     ' (%.1f GB of it the other lane\'s)' % others if others else '')


def ledger_others():
    """what other processes' reservations may still take (GB): each live one's peak less its tree's private bytes"""
    import json
    d = ledger_dir()
    if d is None or not d.is_dir():
        return 0.0
    out = 0.0
    for f in d.glob('*.json'):
        try:
            e = json.loads(f.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        if e.get('owner') == os.getpid():
            continue
        if not alive(e.get('owner'), e.get('born')):
            try:
                f.unlink()          # (a reservation whose owner has exited: it holds nothing)
            except OSError:
                pass
            continue
        used = tree_private_gb(e['pid']) if e.get('pid') else 0.0
        out += max(0.0, float(e['peak']) - used)
    return out


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
                    alone = not self.running and others <= 0     # (the other lane's reservations count)
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
