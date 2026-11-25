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
"""w3apb.py -- Witcher 3 APEX cloth assets (.apb, binary NvParameterized) read in plain Python (numpy), no ParamTool.

Where they are. A cooked .redcloth (CR2W, in the bundles) holds the materials and an empty apexBinaryAsset; the APEX
asset itself is an entry of content/content0/collision.cache ("CC3W", version 11) under the .redcloth's depot name,
Comtype 3 (redcloth; 4 is redapex): zlib, then a RED wrapper (3 u32, a u32 count, per item a VLQ-length name, a VLQ
count of strings, 70 bytes, u32 file size, i8 flag) and the .apb bytes. apb_from_cache() returns those bytes.

The .apb (NvParameterized BinSerializer, version 0x00010003, written by APEX 1.3 for Win64/MSVC):
    header    16 big-endian u32 (magic 0x5A5B5C5D, type 0, version, numObjects, fileLength, dictOffset, dataOffset,
              relocOffset, metadataOffset, arch, compiler, compilerVer, os, osVer, numMetadata 0, alignment) and a
              48-byte extension ("ab\\n\\xff"); then the dictionary (the strings) and the data.
    data      a memory image of the objects as the 64-bit build lays them out; every pointer is a u64 file offset.
              At dataOffset the object table: per object (objOff, className, name, filename) pointers. An object
              header (u32 offset to its data relative to itself, className and name pointers, bool isIncluded,
              u32 version (major << 16 | minor), u32 checksum count, checksum pointer); the object data starts with
              the NvParameters base (8 pointers, 3 bools: 72 bytes), then the class's parameter struct, laid out by
              PlatformABI's rules (natural alignment, structs padded to their alignment, custom 16-byte alignment on
              some arrays): a dynamic array is {buf, bool, i32 elementSize, i32 size} (24 bytes), a string {buf,
              bool} (16), an enum/ref/pointer one pointer; a reference points at another object header.
    relocs    u32 count, then (u32 type, u32 offset) per pointer (type 1: an object reference).
There is no metadata in these files, so the layout comes from the class definitions in w3apb_schema.py (the APEX 1.4
SDK's legacy definitions of exactly the versions the game uses; each object's checksum is checked against them).

The classes: ClothingAssetParameters 0.13 -> graphicalLods[] (ClothingGraphicalLodParameters 0.4) -> renderMeshAsset
(RenderMeshAssetParameters 0.0: materialNames[], submeshes[]) -> SubmeshParameters 0.1 (vertexBuffer, indexBuffer[])
-> VertexBufferParameters 0.1 (vertexFormat: VertexFormatParameters 0.0 bufferFormats[] {name 'SEMANTIC_POSITION',
...}, buffers[]: BufferF32x3 / BufferF32x4 / BufferF32x2 / BufferU16x4 / BufferU8x4 / BufferU32x1); bones[] {name,
parentIndex, bindPose Mat34: 12 floats, three columns then the translation}; physicalMeshes[], materialLibrary,
simulation, boneActors/boneSpheres.

to_apx() writes the same NvParameterized XML that ParamTool's "-s apx" writes for these objects (XmlSerializer's
layout: <value>/<struct>/<array> elements, flat text for simple arrays, the same number formatting), the file the
Witcher3-Blender-Tools add-on hands to io_mesh_apx; w3patches.use_w3apb() makes the add-on call it instead of
ParamTool. graphical_lod() returns a LOD's render mesh as numpy arrays for direct use.

    python tools/levelport/w3apb.py extract <redcloth depot path> <out.apb>     (W3_GAME: the game's folder)
    python tools/levelport/w3apb.py apx <in.apb | depot path> <out.apx>
    python tools/levelport/w3apb.py info <in.apb | depot path>                   (LOD vertex/triangle counts)
"""
import glob
import json
import math
import os
import struct
import sys
import zlib
from xml.sax.saxutils import escape, quoteattr

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3apb_schema  # noqa: E402

MAGIC = 0x5A5B5C5D
BS = chr(92)

# ---------------------------------------------------------------- collision.cache


def _vlq(b, o):
    """collision.cache wrapper VLQ: bit 7 of the first byte the sign, bit 6 'more', 6 low bits; then 7-bit groups"""
    b1 = b[o]
    neg = bool(b1 & 0x80)
    more = bool(b1 & 0x40)
    v = b1 & 0x3F
    sh, n = 6, 1
    while more:
        c = b[o + n]
        v |= (c & 0x7F) << sh
        more = bool(c & 0x80)
        sh += 7
        n += 1
    return (-v if neg else v), n


class CollisionCache:
    """A collision.cache's index: name (lower case, backslashes) -> (page offset, packed size, size, comtype)"""

    def __init__(self, path):
        self.path = path
        self.items = {}
        with open(path, 'rb') as f:
            h = f.read(0x40)
            if h[:4] != b'CC3W':
                raise ValueError('%s: not a collision cache' % path)
            ver = struct.unpack_from('<I', h, 4)[0]
            if ver == 2:
                info, n, names = struct.unpack_from('<QQQ', h, 16)
                nsize = struct.unpack_from('<I', h, 40)[0]
            elif ver in (10, 11):
                info, n, names, nsize = struct.unpack_from('<QIQI', h, 16)
            elif ver in (1, 9):
                info, n, names, nsize = struct.unpack_from('<IIII', h, 16)
            else:
                raise ValueError('%s: collision cache version %d' % (path, ver))
            f.seek(names)
            nm = f.read(nsize).split(b'\0')
            f.seek(info)
            es = 64 if ver in (10, 11) else 72
            tab = f.read(es * n)
        for i in range(n):
            if ver in (10, 11):
                _no, _u1, po, zs, sz, _u2, _u3, _u4, ct = struct.unpack_from('<IQQIIQQ16sB', tab, es * i)
            else:
                po = struct.unpack_from('<Q' if ver == 2 else '<I', tab, es * i + 16)[0]
                o = es * i + (24 if ver == 2 else 20)
                zs, sz = struct.unpack_from('<II', tab, o)
                ct = tab[o + 44]
            name = nm[i].decode('latin-1').lower().replace('/', BS)
            self.items.setdefault(name, (po, zs, sz, ct))

    def read(self, name):
        e = self.items.get(name.lower().replace('/', BS))
        if e is None:
            return None
        po, zs, sz, ct = e
        with open(self.path, 'rb') as f:
            f.seek(po)
            raw = zlib.decompress(f.read(zs))
        if ct in (1, 5):
            return raw
        return unwrap_red(raw)


def unwrap_red(raw):
    """The payload of a RED-wrapped collision-cache entry (Comtype 2/3/4): its sub-files concatenated"""
    o = 12
    count = struct.unpack_from('<I', raw, o)[0]
    o += 4
    sizes = []
    for _ in range(count):
        ln, k = _vlq(raw, o)
        o += k + abs(ln)
        ns, k = _vlq(raw, o)
        o += k
        for _ in range(abs(ns)):
            ln, k = _vlq(raw, o)
            o += k + abs(ln)
        o += 70
        sizes.append(struct.unpack_from('<I', raw, o)[0])
        o += 5
    out = raw[o:o + sum(sizes)]
    if len(out) != sum(sizes):
        raise ValueError('truncated collision-cache entry')
    return out


def cache_files(game):
    """The game's collision caches, base game first (a later one's copy of a name wins, as in the add-on)"""
    found = sorted(glob.glob(os.path.join(game, 'content', '**', 'collision.cache'), recursive=True))
    found += sorted(glob.glob(os.path.join(game, 'dlc', '**', 'collision.cache'), recursive=True))
    return found


_caches = {}


def apb_from_cache(depot, game=None):
    """The .apb bytes of a .redcloth/.redapex (or .apb) depot path from the game's collision caches; None if absent"""
    game = game or os.environ.get('W3_GAME') or os.environ.get('W3_ROOT')
    if not game:
        raise SystemExit('w3apb: set W3_GAME (the game folder) to read the collision cache')
    key = depot.lower().replace('/', BS).lstrip(BS)
    names = [key]
    stem = os.path.splitext(key)[0]
    for ext in ('.redcloth', '.redapex', '.apb'):
        if stem + ext not in names:
            names.append(stem + ext)
    hit = None
    for p in cache_files(game):
        if p not in _caches:
            _caches[p] = CollisionCache(p)
        for n in names:
            if n in _caches[p].items:
                hit = (_caches[p], n)
                break
    return hit[0].read(hit[1]) if hit else None


# ---------------------------------------------------------------- NvParameterized binary

SIMPLE = {  # type: (numpy base, count, size, alignment)
    'Bool': ('?', 1, 1, 1), 'I8': ('i1', 1, 1, 1), 'U8': ('u1', 1, 1, 1), 'I16': ('<i2', 1, 2, 2),
    'U16': ('<u2', 1, 2, 2), 'I32': ('<i4', 1, 4, 4), 'U32': ('<u4', 1, 4, 4), 'F32': ('<f4', 1, 4, 4),
    'I64': ('<i8', 1, 8, 8), 'U64': ('<u8', 1, 8, 8), 'F64': ('<f8', 1, 8, 8),
    'Vec2': ('<f4', 2, 8, 4), 'Vec3': ('<f4', 3, 12, 4), 'Vec4': ('<f4', 4, 16, 4), 'Quat': ('<f4', 4, 16, 4),
    'Mat33': ('<f4', 9, 36, 4), 'Mat34': ('<f4', 12, 48, 4), 'Mat44': ('<f4', 16, 64, 4),
    'Bounds3': ('<f4', 6, 24, 4), 'Transform': ('<f4', 7, 28, 4), 'Pointer': ('<u8', 1, 8, 8),
}
PTR = 8  # Win64: pointers 8 bytes, 8-aligned


def _align(x, a):
    return (x + a - 1) // a * a if a > 1 else x


class Schema:
    """One class version's definition tree with PlatformABI's Win64 layout rules"""

    def __init__(self, cls, ver, spec):
        self.cls, self.ver = cls, ver
        self.checksum = tuple(spec['checksum'])
        self.nodes = spec['nodes']
        self._al, self._sz = {}, {}

    def nat_align(self, i):
        name, typ, kids, asize, al, pad, flags = self.nodes[i]
        if typ == 'Array':
            return self.align(kids[0]) if asize > 0 else PTR
        if typ == 'Struct':
            return max([1] + [self.align(k) for k in kids])
        if typ in ('String', 'Enum', 'Ref'):
            return PTR
        return SIMPLE[typ][3]

    def align(self, i):
        if i not in self._al:
            name, typ, kids, asize, al, pad, flags = self.nodes[i]
            nat = self.nat_align(i)
            self._al[i] = nat if not al or (typ == 'Array' and asize < 0) else max(nat, al)
        return self._al[i]

    def pad(self, i):
        name, typ, kids, asize, al, pad, flags = self.nodes[i]
        nat = self.nat_align(i)
        return nat if not pad or (typ == 'Array' and asize < 0) else max(nat, pad)

    def size(self, i):
        if i not in self._sz:
            name, typ, kids, asize, al, pad, flags = self.nodes[i]
            if typ == 'Array':
                s = asize * self.size(kids[0]) if asize > 0 else 24
            elif typ == 'Struct':
                if not kids:
                    s = 1
                else:
                    tot, s = 1, 0
                    for k in kids:
                        a = self.align(k)
                        tot = max(tot, a)
                        s = _align(s, a) + self.size(k)
                    if pad:
                        tot = max(tot, pad)
                    s = _align(s, tot)
            elif typ == 'String':
                s = 16
            elif typ in ('Enum', 'Ref'):
                s = PTR
            else:
                s = SIMPLE[typ][2]
            self._sz[i] = s
        return self._sz[i]

    def is_bin_simple_struct(self, i):
        """BinSerializer's IsSimpleStruct (fast path for arrays of structs)"""
        kids = self.nodes[i][2]
        return bool(kids) and all(self.nodes[k][1] not in ('String', 'Enum', 'Array', 'Ref', 'Struct')
                                  for k in kids)

    def struct_dtype(self, i):
        """numpy dtype of a simple struct element (field offsets by the same layout rules)"""
        names, formats, offsets, off = [], [], [], 0
        for k in self.nodes[i][2]:
            name, typ, kids, asize, al, pad, flags = self.nodes[k]
            if al:
                off = _align(off, al)
            off = _align(off, self.align(k))
            base, n, size, _a = SIMPLE[typ]
            names.append(name)
            formats.append(base if n == 1 else (base, (n,)))
            offsets.append(off)
            off += size
        return np.dtype({'names': names, 'formats': formats, 'offsets': offsets, 'itemsize': self.size(i)})


SCHEMAS = {k: Schema(k[0], k[1], v) for k, v in w3apb_schema.SCHEMAS.items()}


class Obj:
    """One NvParameterized object: className, version (major << 16 | minor), name, included, schema, data (the
    root struct as a dict: struct -> dict, dynamic array -> numpy array (simple types and simple structs) or list,
    string/enum -> str or None, reference -> Obj or None, pointer -> None)"""

    def __init__(self, cls, ver, name, included, checksum):
        self.className, self.version, self.name, self.included, self.checksum = cls, ver, name, included, checksum
        self.schema = None
        self.data = None

    def __getitem__(self, k):
        return self.data[k]

    def __repr__(self):
        return '<%s %d.%d>' % (self.className, self.version >> 16, self.version & 0xFFFF)


class ApbReader:
    def __init__(self, data):
        if len(data) < 64 or struct.unpack_from('>I', data, 0)[0] != MAGIC:
            raise ValueError('not an NvParameterized binary (.apb)')
        h = struct.unpack_from('>16I', data, 0)
        (self.magic, self.type, self.version, self.num_objects, self.file_length, self.dict_offset,
         self.data_offset, self.reloc_offset, self.metadata_offset, self.arch, self.compiler, self.compiler_ver,
         self.os, self.os_ver, self.num_metadata, self.alignment) = h
        if self.type != 0 or self.version not in (0x00010002, 0x00010003):
            raise ValueError('unsupported .apb (type %d, version 0x%x)' % (self.type, self.version))
        if (self.arch, self.compiler) != (2, 2):
            raise ValueError('unsupported .apb platform (arch %d, compiler %d): only VcWin64' % (self.arch,
                                                                                               self.compiler))
        self.d = data[:self.file_length]
        self.objs = {}

    def u64(self, o):
        return struct.unpack_from('<Q', self.d, o)[0]

    def cstr(self, o):
        if not o:
            return None
        return self.d[o:self.d.index(b'\0', o)].decode('utf-8', 'replace')

    def roots(self):
        out = []
        o = self.data_offset
        for _ in range(self.num_objects):
            p = self.u64(o)
            out.append(self.read_object(p) if p else None)
            o += 4 * PTR
        return out

    def read_object(self, h):
        if h in self.objs:
            return self.objs[h]
        rel = struct.unpack_from('<I', self.d, h)[0]
        cls = self.cstr(self.u64(h + 8))
        name = self.cstr(self.u64(h + 16))
        inc = bool(self.d[h + 24])
        ver, nck = struct.unpack_from('<II', self.d, h + 28)
        ckp = self.u64(h + 40)
        ck = struct.unpack_from('<%dI' % nck, self.d, ckp) if ckp and nck else ()
        obj = Obj(cls, ver, name, inc, ck)
        self.objs[h] = obj
        if not inc:
            return obj
        sc = SCHEMAS.get((cls, ver))
        if sc is None:
            raise ValueError('no definition for %s %d.%d' % (cls, ver >> 16, ver & 0xFFFF))
        if ck and tuple(ck) != sc.checksum:
            raise ValueError('%s %d.%d: checksum differs from the definition' % (cls, ver >> 16, ver & 0xFFFF))
        obj.schema = sc
        base = h + rel
        off = _align(base + 8 * PTR + 3, PTR)  # NvParameters fields (vtable and 7 more pointers, 3 bools)
        obj.data, _ = self.read_node(sc, 0, off)
        return obj

    def read_node(self, sc, i, off):
        name, typ, kids, asize, al, pad, flags = sc.nodes[i]
        dyn = typ == 'Array' and asize < 0
        if al and not dyn:
            off = _align(off, al)
        if 'D' in flags:
            off = _align(off, sc.align(i))
            return None, off + sc.size(i)
        if typ == 'Struct':
            off = _align(off, sc.align(i))
            start = off
            vals = {}
            for k in kids:
                vals[sc.nodes[k][0]], off = self.read_node(sc, k, off)
            if off == start:
                off += 1
            return vals, _align(off, sc.pad(i))
        if typ == 'Array':
            if asize > 0:
                off = _align(off, sc.align(i))
                vals = []
                for _ in range(asize):
                    v, off = self.read_node(sc, kids[0], off)
                    vals.append(v)
                return vals, off
            off = _align(off, PTR)
            buf = self.u64(off)
            n = struct.unpack_from('<i', self.d, off + 16)[0]
            if sc.nodes[kids[0]][1] == 'Array':
                raise ValueError('nested dynamic arrays are not supported')
            end = _align(off + 20, sc.pad(i))
            return self.read_array(sc, kids[0], buf, n), end
        if typ == 'String':
            off = _align(off, PTR)
            s = self.cstr(self.u64(off))
            return s, _align(off + PTR + 1, PTR)
        if typ == 'Enum':
            off = _align(off, PTR)
            return self.cstr(self.u64(off)), off + PTR
        if typ == 'Ref':
            off = _align(off, PTR)
            p = self.u64(off)
            return (self.read_object(p) if p else None), off + PTR
        if typ == 'Pointer':
            off = _align(off, PTR)
            return None, off + PTR
        base, n, size, a = SIMPLE[typ]
        off = _align(off, a)
        v = np.frombuffer(self.d, dtype=base, count=n, offset=off)
        return (v[0].item() if n == 1 else v.copy()), off + size

    def read_array(self, sc, e, buf, n):
        name, typ, kids, asize, al, pad, flags = sc.nodes[e]
        if typ in SIMPLE and typ != 'Pointer':
            base, k, size, a = SIMPLE[typ]
            if n <= 0 or not buf:
                return np.zeros((0, k) if k > 1 else 0, dtype=base)
            off = _align(buf, a)
            v = np.frombuffer(self.d, dtype=base, count=n * k, offset=off).copy()
            return v.reshape(n, k) if k > 1 else v
        if typ == 'Struct' and sc.is_bin_simple_struct(e):
            dt = sc.struct_dtype(e)
            if n <= 0 or not buf:
                return np.zeros(0, dtype=dt)
            off = _align(buf, sc.align(e))
            return np.frombuffer(self.d, dtype=dt, count=n, offset=off).copy()
        vals = []
        off = buf
        for _ in range(max(n, 0)):
            v, off = self.read_node(sc, e, off)
            vals.append(v)
        return vals


def read_apb(data):
    """The root objects of an .apb (bytes)"""
    return ApbReader(data).roots()


def clothing_asset(data):
    for o in read_apb(data):
        if o is not None and o.className == 'ClothingAssetParameters':
            return o
    raise ValueError('no ClothingAssetParameters in the .apb')


# ---------------------------------------------------------------- render mesh


def graphical_lod(asset, lod=0):
    """A graphical LOD's render mesh as the asset stores it (APEX space, no axis change): {'lod', 'materials',
    'submeshes': [{'material', 'vertex_count', 'buffers' {semantic name: ndarray}, 'indices' (n, 3) uint32}],
    'bones': [{'name', 'parent', 'bind_pose' 4x4 (columns from the Mat34)}]}. Buffer names are the vertex format's
    ('SEMANTIC_POSITION', 'SEMANTIC_NORMAL', 'SEMANTIC_TANGENT' (xyz + w bitangent sign), 'SEMANTIC_TEXCOORD0',
    'SEMANTIC_COLOR' (u8 x4), 'SEMANTIC_BONE_INDEX', 'SEMANTIC_BONE_WEIGHT')."""
    g = asset['graphicalLods'][lod]
    rm = g['renderMeshAsset']
    out = {'lod': int(g['lod']), 'materials': list(rm['materialNames']), 'submeshes': [], 'bones': []}
    for k, sm in enumerate(rm['submeshes']):
        vb = sm['vertexBuffer']
        fmts = vb['vertexFormat']['bufferFormats']
        bufs = {}
        for f, b in zip(fmts, vb['buffers']):
            d = b['data']
            if d.dtype.names:
                d = np.stack([d[n] for n in d.dtype.names], axis=1)
            bufs[f['name']] = d
        idx = np.asarray(sm['indexBuffer'], dtype=np.uint32)
        out['submeshes'].append({'material': out['materials'][k] if k < len(out['materials']) else None,
                                 'vertex_count': int(vb['vertexCount']), 'buffers': bufs,
                                 'indices': idx.reshape(-1, 3)})
    for b in asset['bones']:
        m = np.asarray(b['bindPose'], dtype=np.float64).reshape(4, 3).T
        bp = np.eye(4)
        bp[:3, :] = m
        out['bones'].append({'name': b['name'], 'parent': int(b['parentIndex']), 'bind_pose': bp})
    return out


def summary(asset):
    """Per graphical LOD: submesh vertex and triangle counts, materials"""
    lods = []
    for i in range(len(asset['graphicalLods'])):
        g = graphical_lod(asset, i)
        lods.append({'lod': g['lod'], 'materials': g['materials'],
                     'vertices': sum(s['vertex_count'] for s in g['submeshes']),
                     'triangles': sum(len(s['indices']) for s in g['submeshes']),
                     'submeshes': [{'material': s['material'], 'vertices': s['vertex_count'],
                                    'triangles': len(s['indices']), 'buffers': sorted(s['buffers'])}
                                   for s in g['submeshes']]})
    return {'bones': [b['name'] for b in asset['bones']], 'lods': lods,
            'physical_meshes': len(asset['physicalMeshes'])}


# ---------------------------------------------------------------- XML (ParamTool's .apx)

FLT_MAX = float(np.finfo(np.float32).max)


def _f32(x):
    x = float(x)
    if not math.isfinite(x):
        return '0'
    if x == -FLT_MAX:
        return 'PX_MIN_F32'
    if x == FLT_MAX:
        return 'PX_MAX_F32'
    if x == 1:
        return '1'
    if x == 0:
        return '0'
    if x == -1:
        return '-1'
    s = '%.9g' % x
    if '.' in s and 'e' not in s:
        s = s.rstrip('0').rstrip('.')
    return s


def _int(typ, v):
    v = int(v)
    if typ == 'U32' and v == 0xFFFFFFFF:
        return 'UINT32_MAX'
    if typ == 'U16' and v == 0xFFFF:
        return 'UINT16_MAX'
    if typ == 'I32':
        if v == -2 ** 31:
            return 'INT32_MIN'
        if v == 2 ** 31 - 1:
            return 'INT32_MAX'
    if typ == 'I16':
        if v == -2 ** 15:
            return 'INT16_MIN'
        if v == 2 ** 15 - 1:
            return 'INT16_MAX'
    if typ == 'I8':
        if v == -128:
            return 'INT8_MIN'
        if v == 127:
            return 'INT8_MAX'
    return str(v)


def value_str(typ, v):
    """Handle::valueToStr for a simple value"""
    if typ == 'Bool':
        return 'true' if v else 'false'
    if typ in ('F32', 'F64'):
        return _f32(v) if typ == 'F32' else repr(float(v))
    if typ in SIMPLE and SIMPLE[typ][1] > 1:
        return ' '.join(_f32(x) for x in np.ravel(v))
    if typ == 'Pointer':
        return ''
    return _int(typ, v)


def _version(o):
    return ' version="%d.%d" checksum="%s"' % (o.version >> 16, o.version & 0xFFFF,
                                               ' '.join('0x%x' % c for c in o.checksum))


def _xml_simple_struct(sc, i):
    """XmlSerializer's IsSimpleStruct: every field a simple type"""
    return all(sc.nodes[k][1] not in ('Array', 'Struct', 'Ref', 'String', 'Enum') for k in sc.nodes[i][2])


class XmlWriter:
    def __init__(self):
        self.out = []

    def obj_root(self, o):
        w = self.out.append
        w('<value name="" type="Ref" className="%s"' % o.className)
        if o.name:
            w(' objectName=%s' % quoteattr(o.name))
        w(_version(o) + '>\n')
        self.node(o.schema, 0, o.data, 1, False)
        w('</value>\n')

    def node(self, sc, i, v, level, in_array):
        name, typ, kids, asize, al, pad, flags = sc.nodes[i]
        if 'D' in flags:
            return
        w = self.out.append
        ind = '  ' * level
        nm = '' if in_array else ' name="%s"' % name
        if typ == 'Struct':
            w('%s<struct%s>\n' % (ind, nm))
            for k in kids:
                self.node(sc, k, v[sc.nodes[k][0]], level + 1, False)
            w('%s</struct>\n' % ind)
        elif typ == 'Array':
            self.array(sc, i, v, level, nm)
        elif typ == 'Ref':
            inc = 'I' in flags
            w('%s<value%s type="Ref" included="%d"' % (ind, nm, 1 if inc else 0))
            if v is None:
                w(' classNames="">')
            else:
                w(' className="%s"' % v.className)
                if v.name:
                    w(' objectName=%s' % quoteattr(v.name))
                if inc:
                    w(_version(v))
                w('>')
                if inc and v.data is not None:
                    w('%s\n' % ind)
                    self.node(v.schema, 0, v.data, level + 1, False)
                    w(ind)
            w('</value>\n')
        elif typ == 'Pointer':
            return
        elif typ in ('String', 'Enum'):
            w('%s<value%s type="%s"%s>%s</value>\n' % (ind, nm, typ, ' null="1"' if v is None else '',
                                                       escape(v) if v else ''))
        else:
            w('%s<value%s type="%s">%s</value>\n' % (ind, nm, typ, value_str(typ, v)))

    def array(self, sc, i, v, level, nm):
        name, typ, kids, asize, al, pad, flags = sc.nodes[i]
        w = self.out.append
        ind = '  ' * level
        ind1 = '  ' * (level + 1)
        e = kids[0]
        etyp = sc.nodes[e][1]
        n = len(v)
        head = '%s<array%s size="%d" type="%s"' % (ind, nm, n, etyp)
        simple_struct = etyp == 'Struct' and _xml_simple_struct(sc, e)
        if simple_struct:
            fl = [k for k in sc.nodes[e][2] if 'D' not in sc.nodes[k][6]]
            head += ' structElements="%s"' % ','.join('%s(%s)' % (sc.nodes[k][0], sc.nodes[k][1]) for k in fl)
        w(head + '>')
        if n:
            w('\n')
        if n > 0:
            if simple_struct:
                names = [sc.nodes[k][0] for k in fl]
                types = [sc.nodes[k][1] for k in fl]
                parts = []
                for j in range(n):
                    if j % 4 == 0:
                        if j:
                            parts.append('\n')
                        parts.append(ind1)
                    rec = v[j]
                    parts.append(' '.join(value_str(t, rec[f]) for f, t in zip(names, types)))
                    if j + 1 < n:
                        parts.append(',')
                parts.append('\n')
                w(''.join(parts))
            elif etyp in SIMPLE and etyp != 'Pointer':
                k = SIMPLE[etyp][1]
                if k == 1:
                    per = 64 if etyp in ('Bool', 'I8', 'U8', 'U16') else 32
                    if etyp == 'Bool':
                        items = ['1' if x else '0' for x in v]
                    elif etyp in ('F32', 'F64'):
                        items = [_f32(x) for x in v]
                    else:
                        items = [_int(etyp, x) for x in v]
                    sep = ' '
                else:
                    per = 16
                    items = [' '.join(_f32(x) for x in row) for row in np.asarray(v).reshape(n, k)]
                    sep = ', '
                parts = [ind1]
                for j, s in enumerate(items):
                    parts.append(s)
                    if j + 1 < n:
                        parts.append(sep)
                    if (j + 1) % per == 0:
                        parts.append('\n' + ind1)
                parts.append('\n')
                w(''.join(parts))
            else:
                for x in v:
                    self.node(sc, e, x, level + 1, True)
        if n:
            w(ind)
        w('</array>\n')


def to_xml(objs):
    """ParamTool's XML for the root objects of an .apb"""
    xw = XmlWriter()
    xw.out.append('<!DOCTYPE NvParameters>\n<NvParameters numObjects="%d" version="1.0" >\n' % len(objs))
    for o in objs:
        xw.obj_root(o)
    xw.out.append('</NvParameters>\n')
    return ''.join(xw.out)


def to_apx(apb_data, out_path):
    """Write the .apx (XML) for .apb bytes; returns out_path"""
    xml = to_xml([o for o in read_apb(apb_data) if o is not None])
    tmp = out_path + '.tmp'
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(xml)
    os.replace(tmp, out_path)
    return out_path


def _load(arg):
    if os.path.isfile(arg):
        with open(arg, 'rb') as f:
            d = f.read()
        o = d.find(struct.pack('>I', MAGIC))
        return d[o:] if o > 0 else d
    d = apb_from_cache(arg)
    if d is None:
        raise SystemExit('w3apb: %s is not a file and not in the collision cache' % arg)
    return d


def main(argv):
    if len(argv) < 2 or argv[0] not in ('extract', 'apx', 'info'):
        raise SystemExit(__doc__)
    if argv[0] == 'extract':
        d = apb_from_cache(argv[1])
        if d is None:
            raise SystemExit('w3apb: not in the collision cache: %s' % argv[1])
        with open(argv[2], 'wb') as f:
            f.write(d)
        print(len(d), argv[2])
    elif argv[0] == 'apx':
        print(to_apx(_load(argv[1]), argv[2]))
    else:
        print(json.dumps(summary(clothing_asset(_load(argv[1]))), indent=1))


if __name__ == '__main__':
    main(sys.argv[1:])
