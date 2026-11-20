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
"""jobguard.py -- the processes a tool starts end when it ends.

  import jobguard; jobguard.kill_children_with_me()

Puts this process in a Windows Job Object whose limit is JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. Every process started
after it (a multiprocessing pool's workers, q3map2, WolvenKit, a tool's own subprocesses and theirs) is in the job too,
and the system kills them all when the job's last handle closes: when this process ends, however it ends. A killed run
once left its pool workers waiting on their queue, and they pushed the pagefile to 56 GB
(in a test, 2 of 2 workers outlived a parent killed with TerminateProcess; 0 in the
job). Jobs nest (Windows 8 and later), so a tool started by another guarded tool is in both. The handle is not
inherited, so no child keeps the job open. Once per process; anywhere but Windows, or if the job cannot be made, it
does nothing (the tool runs as before)."""
import os
import sys

_job = None


def kill_children_with_me():
    """Join a kill-on-close job (once): True if this process is in one now."""
    global _job
    if _job is not None:
        return True
    if os.name != 'nt':
        return False
    import ctypes
    import ctypes.wintypes as wt
    k = ctypes.WinDLL('kernel32', use_last_error=True)

    class BASIC(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64),
                    ('LimitFlags', wt.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                    ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]

    class IOC(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ('ReadOps', 'WriteOps', 'OtherOps', 'ReadBytes', 'WriteBytes',
                                                   'OtherBytes')]

    class EXT(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', BASIC), ('IoInfo', IOC), ('ProcessMemoryLimit', ctypes.c_size_t),
                    ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t),
                    ('PeakJobMemoryUsed', ctypes.c_size_t)]
    k.CreateJobObjectW.restype = wt.HANDLE
    k.SetInformationJobObject.argtypes = [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]
    k.GetCurrentProcess.restype = wt.HANDLE
    k.AssignProcessToJobObject.argtypes = [wt.HANDLE, wt.HANDLE]
    job = k.CreateJobObjectW(None, None)
    if not job:
        return False
    info = EXT()
    info.BasicLimitInformation.LimitFlags = 0x2000          # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not (k.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))     # extended limits
            and k.AssignProcessToJobObject(job, k.GetCurrentProcess())):
        print('jobguard: no kill-on-close job (error %d): children may outlive this process' % ctypes.get_last_error(),
              file=sys.stderr)
        k.CloseHandle(job)
        return False
    _job = job                                              # (kept open for the process's life)
    return True
