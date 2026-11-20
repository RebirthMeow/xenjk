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
"""vcache.py -- triangles reordered for the GPU's vertex cache (vcache.c, Forsyth 2006), for the scene converters.

    order(tris, nverts) -> the same triangles (an (n, 3) array) in cache order

The C library is built on first use with Visual Studio's compiler (vswhere finds it) into
.codex-research/levelport/bin/vcache-<source hash>.dll; without a compiler or on failure, order() returns the
triangles unchanged and says so once. The Beauclair profile: the source order cost 1.43 cache misses a
triangle against 0.98 unique vertices a triangle."""
import ctypes
import hashlib
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SRC = HERE / 'vcache.c'
sys.path.insert(0, str(HERE))
import paths  # noqa: E402

BIN = paths.LEVELPORT / 'bin'
_lib = None
_warned = False


def _vcvars():
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe'
    if not vswhere.exists():
        return None
    r = subprocess.run([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], capture_output=True, text=True)
    p = Path(r.stdout.strip()) / 'VC' / 'Auxiliary' / 'Build' / 'vcvars64.bat'
    return p if p.exists() else None


def _load():
    global _lib
    if _lib is not None:
        return _lib or None
    tag = hashlib.sha256(SRC.read_bytes()).hexdigest()[:12]
    dll = BIN / ('vcache-%s.dll' % tag)
    if not dll.exists():
        vc = _vcvars()
        if vc is None:
            _lib = False
            return None
        BIN.mkdir(parents=True, exist_ok=True)
        # built under this process's own names and renamed: several workers may build at once on first use
        mine = BIN / ('vcache-%s.%d' % (tag, os.getpid()))
        bat = Path(str(mine) + '.bat')   # (a batch file: cmd /c's own quoting ate the paths)
        crlf = chr(13) + chr(10)
        bat.write_text(('@call "%s" >nul 2>nul' + crlf + 'cl /nologo /O2 /LD /Fo"%s.obj" /Fe"%s.dll" "%s"' + crlf) % (
            vc, mine, mine, SRC), encoding='ascii')
        r = subprocess.run(['cmd', '/c', str(bat)], capture_output=True, text=True, cwd=str(BIN))
        built = Path(str(mine) + '.dll')
        if r.returncode or not built.exists():
            sys.stderr.write('vcache: build failed\n%s\n' % r.stdout[-2000:])
            _lib = False
            return None
        try:
            os.replace(built, dll)
        except OSError:                 # (another worker's build is in place, and loaded)
            pass
    try:
        lib = ctypes.CDLL(str(dll))
    except OSError as e:                # (the triangles stay in source order, as without a compiler)
        sys.stderr.write('vcache: %s does not load (%s)\n' % (dll, e))
        _lib = False
        return None
    lib.vcache_optimize.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
    lib.vcache_optimize.restype = ctypes.c_int
    _lib = lib
    return lib


def order(tris, nverts):
    global _warned
    t = np.ascontiguousarray(np.asarray(tris).reshape(-1, 3), dtype=np.uint32)
    if len(t) < 2:
        return t
    lib = _load()
    if lib is None:
        if not _warned:
            sys.stderr.write('vcache: no compiler, triangles kept in source order\n')
            _warned = True
        return t
    out = np.empty_like(t)
    if lib.vcache_optimize(t.ctypes.data, len(t), int(nverts), out.ctypes.data):
        return t
    return out


def acmr(tris, cache=32):
    """Cache misses a triangle of a FIFO cache (the measure the profile used)."""
    from collections import deque
    q, s, miss = deque(), set(), 0
    for v in np.asarray(tris).ravel().tolist():
        if v not in s:
            miss += 1
            if len(q) == cache:
                s.discard(q.popleft())
            q.append(v)
            s.add(v)
    return miss / max(1, len(np.asarray(tris).reshape(-1, 3)))


if __name__ == '__main__':
    # self-check on a grid and a random soup: same triangles, fewer misses
    n = 120
    ii, jj = np.meshgrid(np.arange(n - 1), np.arange(n - 1), indexing='ij')
    q = (ii * n + jj).ravel()
    grid = np.concatenate([np.stack([q, q + 1, q + n + 1], 1), np.stack([q, q + n + 1, q + n], 1)])
    rng = np.random.default_rng(1)
    for name, t, nv in (('grid', grid, n * n), ('shuffled grid', grid[rng.permutation(len(grid))], n * n)):
        o = order(t, nv)
        same = sorted(map(tuple, np.sort(t, 1).tolist())) == sorted(map(tuple, np.sort(o, 1).tolist()))
        wind = {tuple(np.roll(r, -int(np.argmin(r)))) for r in t.tolist()} == {tuple(np.roll(r, -int(np.argmin(r)))) for r in o.tolist()}
        print('%-14s %d tris: ACMR %.3f -> %.3f, same triangles %s, winding kept %s' % (name, len(t), acmr(t), acmr(o), same, wind))
