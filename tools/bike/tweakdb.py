"""CP2077 TweakDB reader (r6/cache/tweakdb.bin, blob version 8) with names from WolvenKit's tweakdbstr table.

The CT-3X's handling records (drive model, bike extension, engine, wheels, air control). The WolvenKit
CLI has no TweakDB export, so this reads the blob directly.

  tweakdb.bin   u32 magic 0x0BB1DB47, i32 blob version, i32 parser version, u32 records checksum, i32 offsets of the
                flats, records, queries and group tags. Flats: i32 type count, a table of (u64 FNV-1a64 of the type
                name, i32 value count, i32 key count, i32 data offset), and at each offset: i32 value count, the values
                (strings and array lengths CR2W-packed), i32 key count, keys (u64 TweakDBID, i32 value index). Records: i32
                count, (u64 TweakDBID, u32 record type hash). TweakDBID = CRC32(name) | len(name) << 32.
  names         WolvenKit.Common.dll's embedded resource tweakdbstr.kark (b'KARK', u32 size, Oodle Kraken; the
                game's bin/x64/oo2ext_7_win64.dll decompresses it): u32 magic, i32 version, i32 record / flat / query
                name counts, then the names as length-prefixed strings.

  python -I tweakdb.py dump PATTERN [--db tweakdb.bin] [--names tweakdbstr.bin] [--json out.json]
      every flat whose name matches the regex PATTERN, and records matching it with their fields
  python -I tweakdb.py names PATTERN    record names only
  python -I tweakdb.py tree RECORD [--follow REGEX] [--json out.json]
      a record's fields and, recursively, the records its TweakDBID fields name (Vehicle.* by default)
  Environment: CP2077 (the game folder, default the Steam one), XENJK_MAIN (the checkout holding .codex-research
  for the default --names, default this repository).
  Phantom Liberty installs replace the base blob with r6/cache/tweakdb_ep1.bin (the CT-3X's bikeDriveModelData
  is only set there): pass it with --db.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import zlib
from pathlib import Path

GAME = Path(os.environ.get('CP2077', r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
DB = GAME / 'r6' / 'cache' / 'tweakdb.bin'
NAMES = (Path(os.environ.get('XENJK_MAIN', Path(__file__).resolve().parents[2])) / '.codex-research'
         / 'wishlist-20261101' / 'drafts' / 'bike' / 'tweakdb' / 'tweakdbstr.bin')
MAGIC = 0x0BB1DB57          # the name table's
DB_MAGIC = 0x0BB1DB47       # the blob's


def fnv1a64(s):
    h = 0xcbf29ce484222325
    for c in s.encode():
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def tdbid(name):
    return zlib.crc32(name.encode()) | (len(name) << 32)


class Reader:
    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.o)[0]; self.o += 4; return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.o)[0]; self.o += 4; return v

    def u64(self):
        v = struct.unpack_from('<Q', self.b, self.o)[0]; self.o += 8; return v

    def f32(self, n=1):
        v = struct.unpack_from('<%df' % n, self.b, self.o); self.o += 4 * n; return v[0] if n == 1 else v

    def vlq(self):
        """CR2W's packed int: first byte bit 7 sign (or the ASCII flag for strings), bit 6 continue, 6 bits value;
        then 7 bits per byte with bit 7 continue."""
        b = self.u8()
        v = b & 0x3F
        sign = b & 0x80
        if b & 0x40:
            shift = 6
            while True:
                c = self.u8()
                v |= (c & 0x7F) << shift
                shift += 7
                if not c & 0x80:
                    break
        return v, sign

    def string(self):
        n, ascii_ = self.vlq()
        if ascii_:
            s = self.b[self.o:self.o + n].decode('latin1'); self.o += n
        else:                       # UTF-16
            s = self.b[self.o:self.o + 2 * n].decode('utf-16-le'); self.o += 2 * n
        return s


SCALARS = {
    'Bool': lambda r: bool(r.u8()), 'Int32': Reader.i32, 'Uint32': Reader.u32, 'Uint64': Reader.u64,
    'Float': Reader.f32, 'CName': Reader.string, 'String': Reader.string, 'TweakDBID': Reader.u64,
    'Vector2': lambda r: r.f32(2), 'Vector3': lambda r: r.f32(3), 'Vector4': lambda r: r.f32(4),
    'Quaternion': lambda r: r.f32(4), 'EulerAngles': lambda r: r.f32(3), 'CRUID': Reader.u64,
    'gamedataLocKeyWrapper': Reader.u64, 'raRef:CResource': Reader.u64, 'ResRef': Reader.u64,
    'Color': lambda r: tuple(r.u8() for _ in range(4)),
}
TYPES = {}
for _t, _f in SCALARS.items():
    TYPES[fnv1a64(_t)] = (_t, _f)
    TYPES[fnv1a64('array:' + _t)] = ('array:' + _t, (lambda f: lambda r: [f(r) for _ in range(r.vlq()[0])])(_f))


def read_names(path=NAMES):
    b = Path(path).read_bytes()
    r = Reader(b)
    assert r.u32() == MAGIC, 'not a tweakdbstr table'
    r.i32()
    counts = [r.i32() for _ in range(3)]
    out = []
    for kind, n in zip(('record', 'flat', 'query'), counts):
        out.append([r.string() for _ in range(n)])
    return dict(zip(('record', 'flat', 'query'), out))


def read_db(path=DB):
    """{flat id: (type, value)}, {record id: record type hash}."""
    b = Path(path).read_bytes()
    r = Reader(b)
    magic, ver, parser, _chk, o_flats, o_recs, o_q, o_g = struct.unpack_from('<IiiIiiii', b, 0)
    assert magic == DB_MAGIC and ver == 8, (hex(magic), ver)
    r.o = o_flats
    n = r.i32()
    table = [struct.unpack_from('<Qiii', b, r.o + 20 * i) for i in range(n)]
    flats = {}
    for h, nv, nk, off in table:
        if h not in TYPES:
            raise ValueError(f'unknown flat type {h:#x}')
        tname, fn = TYPES[h]
        r.o = off
        # (the table's value count can differ from the block's for arrays; the block's is the one read)
        values = [fn(r) for _ in range(r.i32())]
        assert r.i32() == nk, (tname, 'key count')
        for _ in range(nk):
            k = r.u64(); i = r.i32()
            flats[k] = (tname, values[i])
    if r.o != o_recs:
        raise ValueError(f'flats ended at {r.o}, records start at {o_recs}')
    recs = {}
    for _ in range(r.i32()):
        k = r.u64(); recs[k] = r.u32()
    return flats, recs


def tree(root, names, flats, by_id, follow=re.compile(r'^Vehicle\.'), depth=4):
    """{record: {field: value}} for root and every record its TweakDBID fields reach (those matching `follow`), as
    deep as `depth`. Field names come from the flat names under '<record>.'."""
    fields = {}
    for n in names['flat']:
        rec, _, f = n.rpartition('.')
        fields.setdefault(rec, []).append(f)
    out, todo = {}, [(root, 0)]
    while todo:
        rec, d = todo.pop()
        if rec in out:
            continue
        out[rec] = {}
        for f in sorted(fields.get(rec, [])):
            v = flats.get(tdbid(f'{rec}.{f}'))
            if v is None:
                continue
            t, val = v
            refs = []
            if t == 'TweakDBID':
                val = by_id.get(val, hex(val) if val else None)
                refs = [val]
            elif t == 'array:TweakDBID':
                val = [by_id.get(x, hex(x)) for x in val]
                refs = val
            out[rec][f] = val
            if d < depth:
                todo += [(r, d + 1) for r in refs if isinstance(r, str) and follow.search(r)]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=('dump', 'names', 'tree'))
    ap.add_argument('pattern', help='dump/names: a regex; tree: a record name')
    ap.add_argument('--db', type=Path, default=DB)
    ap.add_argument('--names', type=Path, default=NAMES)
    ap.add_argument('--json', type=Path)
    ap.add_argument('--follow', default=r'^Vehicle\.')
    a = ap.parse_args()
    names = read_names(a.names)
    rx = re.compile(a.pattern)
    if a.cmd == 'names':
        for n in names['record']:
            if rx.search(n):
                print(n)
        return
    flats, recs = read_db(a.db)
    by_id = {tdbid(n): n for n in names['record']}
    if a.cmd == 'tree':
        t = tree(a.pattern, names, flats, by_id, re.compile(a.follow))
        for rec, fs in t.items():
            print(f'[{rec}]')
            for k, v in fs.items():
                print(f'  {k} = {v}')
        if a.json:
            a.json.write_text(json.dumps(t, indent=1, default=list))
        return
    out = {}
    for n in names['flat']:
        if rx.search(n):
            v = flats.get(tdbid(n))
            if v is None:
                continue
            t, val = v
            if t == 'TweakDBID':
                val = by_id.get(val, hex(val))
            elif t == 'array:TweakDBID':
                val = [by_id.get(x, hex(x)) for x in val]
            out[n] = val
    for k in sorted(out):
        print(k, '=', out[k])
    print(f'{len(out)} flats; {len(flats)} in the db, {len(recs)} records', file=sys.stderr)
    if a.json:
        a.json.write_text(json.dumps(out, indent=1, default=list))


if __name__ == '__main__':
    main()
