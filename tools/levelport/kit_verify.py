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
"""kit_verify.py -- a kit against its KIT.json (kit_derive.py writes it): every file there, of its size and sha256,
and no file the record does not name (runtime files aside). --quick checks sizes and presence only.

  python -I kit_verify.py <kit> [--quick]"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kit_derive  # noqa: E402


def verify(kit, quick=False):
    kit = Path(kit)
    rec = json.loads((kit / 'KIT.json').read_text(encoding='utf-8'))
    bad, missing = [], []
    for rel, e in sorted(rec['files'].items()):
        f = kit / rel
        if not f.exists():
            missing.append(rel)
        elif f.stat().st_size != e['size'] or (not quick and kit_derive.sha256(f) != e['sha256']):
            bad.append(rel)
    extra = sorted(p.relative_to(kit).as_posix() for p in kit.rglob('*') if p.is_file()
                   and p.relative_to(kit).as_posix() not in rec['files'] and p.name != 'KIT.json'
                   and not kit_derive.SKIP.search(p.relative_to(kit).as_posix()))
    return {'kit': str(kit), 'files': len(rec['files']), 'bad': bad, 'missing': missing, 'extra': extra,
            'ok': not (bad or missing or extra), 'quick': quick}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('kit')
    ap.add_argument('--quick', action='store_true')
    a = ap.parse_args()
    r = verify(a.kit, a.quick)
    print(json.dumps(r, indent=1))
    sys.exit(0 if r['ok'] else 1)


if __name__ == '__main__':
    main()
