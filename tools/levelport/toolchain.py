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
"""toolchain.py -- this machine's outside tools against the ones the level port's builds were made with
(toolchain.json): each found (its environment variable, paths.py), and its version or hash against the pin.

  python -I tools/levelport/toolchain.py [--strict] [--only NAME ...]
  python -I tools/levelport/toolchain.py --record        (the pins rewritten from this machine)

A required tool missing exits 1. A tool whose version or hash differs from its pin is reported (a different build
can change the outputs: the material result cache keys numpy's and Pillow's versions, the collision chunks the
q3map2 build); --strict makes that an error too. build_watson's preflight runs check() for the tools a run needs."""
import argparse
import hashlib
import importlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'toolchain.json'


def pe_version(path):
    """a Windows executable's product version (its version resource), or None"""
    if os.name != 'nt':
        return None
    import ctypes
    from ctypes import wintypes
    ver = ctypes.WinDLL('version')
    n = ver.GetFileVersionInfoSizeW(str(path), None)
    if not n:
        return None
    buf = ctypes.create_string_buffer(n)
    if not ver.GetFileVersionInfoW(str(path), 0, n, buf):
        return None
    p, ln = ctypes.c_void_p(), wintypes.UINT()
    if not ver.VerQueryValueW(buf, '\\VarFileInfo\\Translation', ctypes.byref(p), ctypes.byref(ln)) or not ln.value:
        return None
    lang, cp = ctypes.cast(p, ctypes.POINTER(ctypes.c_ushort * 2)).contents
    key = '\\StringFileInfo\\%04x%04x\\ProductVersion' % (lang, cp)
    if not ver.VerQueryValueW(buf, key, ctypes.byref(p), ctypes.byref(ln)) or not ln.value:
        return None
    return (ctypes.wstring_at(p, ln.value - 1).split('+')[0].split() or [None])[0]   # (no build or commit suffix)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def probe(t):
    """(found, value, where): what this machine has for one manifest entry"""
    kind = t['check']
    if kind == 'python':
        return True, sys.version.split()[0], sys.executable
    if kind == 'module':
        try:
            m = importlib.import_module(t.get('module', t['name']))
        except ImportError:
            return False, None, 'not installed'
        return True, getattr(m, '__version__', None), getattr(m, '__file__', '')
    v = os.environ.get(t['var'])
    if not v:
        return False, None, '$%s not set' % t['var']
    p = Path(v) / t['file'] if t.get('file') else Path(v)
    if not p.exists():
        return False, None, '%s does not exist ($%s)' % (p, t['var'])
    if kind == 'exists':
        return True, None, str(p)
    if kind == 'sha256':
        return True, sha256(p), str(p)
    if kind == 'pe_version':
        return True, pe_version(p), str(p)
    if kind == 'command':
        r = subprocess.run([str(p), *t.get('args', [])], capture_output=True, text=True, errors='replace', timeout=120)
        return True, (r.stdout.strip().splitlines() or [''])[0], str(p)
    raise ValueError('toolchain.json: unknown check %r' % kind)


def check(names=None, strict=False, quiet=False):
    """-> (problems, differences): required tools missing (or, strict, any difference), and pins not met"""
    tools = json.loads(MANIFEST.read_text(encoding='utf-8'))['tools']
    problems, diffs = [], []
    for t in tools:
        if names and t['name'] not in names and t.get('var') not in names:
            continue
        found, value, where = probe(t)
        pin = t.get('pin')
        if not found:
            state = 'MISSING' if t.get('required') else 'absent'
            if t.get('required'):
                problems.append('%s: %s (%s)' % (t['name'], where, t['needed_by']))
        elif pin is not None and value != pin:
            state = 'DIFFERS'
            diffs.append('%s: %s, pinned %s (%s)' % (t['name'], value, pin, where))
        else:
            state = 'ok'
        if not quiet:
            print('%-8s %-28s %-20s %s' % (state, t['name'], (value or '')[:20], where), flush=True)
    if strict:
        problems += diffs
    return problems, diffs


def pin_of(name):
    """the manifest's pin for a tool by name or variable, or None"""
    for t in json.loads(MANIFEST.read_text(encoding='utf-8'))['tools']:
        if name in (t['name'], t.get('var')):
            return t.get('pin')
    return None


def record():
    data = json.loads(MANIFEST.read_text(encoding='utf-8'))
    for t in data['tools']:
        found, value, _ = probe(t)
        if found and value is not None:
            t['pin'] = value
    MANIFEST.write_text(json.dumps(data, indent=1) + '\n', encoding='utf-8')
    print('toolchain: pins rewritten in %s' % MANIFEST)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--strict', action='store_true', help='a version or hash other than the pin is an error')
    ap.add_argument('--only', nargs='+', help='these tools (names or variables) only')
    ap.add_argument('--record', action='store_true', help="rewrite the pins from this machine's tools")
    a = ap.parse_args()
    if a.record:
        return record()
    problems, diffs = check(a.only, a.strict)
    if diffs and not a.strict:
        print('toolchain: %d differ from the reference build (outputs may differ; --strict to stop on it)' % len(diffs))
    if problems:
        sys.exit('toolchain: %d problem%s:\n  %s' % (len(problems), 's' if len(problems) > 1 else '',
                                                    '\n  '.join(problems)))
    print('toolchain: ok')


if __name__ == '__main__':
    main()
