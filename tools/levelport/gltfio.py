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
"""Minimal glTF 2.0 (.glb) writer and reader for level interchange; numpy only.

Writes nodes (name, translation, children, mesh, extras), meshes (primitives with POSITION, NORMAL, TEXCOORD_0,
COLOR_0 and uint32 indices) and materials (name, optional embedded PNG/JPEG base colour). Reads the same subset
back, including files exported by Blender (node TRS, matrices and nested children are resolved to world space).
"""
import json
import os
import struct
from pathlib import Path

import numpy as np

ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
FLOAT, UNSIGNED_INT, UNSIGNED_SHORT, UNSIGNED_BYTE = 5126, 5125, 5123, 5121
COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
DTYPES = {FLOAT: '<f4', UNSIGNED_INT: '<u4', UNSIGNED_SHORT: '<u2', UNSIGNED_BYTE: 'u1', 5120: 'i1', 5122: '<i2'}


class Writer:
    def __init__(self):
        self.doc = {'asset': {'version': '2.0', 'generator': 'xenjk levelport'}, 'scene': 0,
                    'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [], 'accessors': [],
                    'bufferViews': [], 'buffers': []}
        self.bin = bytearray()
        self.material_index = {}

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target:
            view['target'] = target
        self.bin += data
        self.doc['bufferViews'].append(view)
        return len(self.doc['bufferViews']) - 1

    def _accessor(self, arr, ctype, atype, target, minmax=False, normalized=False):
        arr = np.ascontiguousarray(arr)
        acc = {'bufferView': self._view(arr.tobytes(), target), 'componentType': ctype,
               'count': int(arr.shape[0]), 'type': atype}
        if minmax:
            acc['min'] = arr.min(0).astype(float).tolist()
            acc['max'] = arr.max(0).astype(float).tolist()
        if normalized:
            acc['normalized'] = True
        self.doc['accessors'].append(acc)
        return len(self.doc['accessors']) - 1

    def texture(self, image, mime='image/png', name=None):
        """Embed encoded PNG/JPEG bytes as an image + texture; returns the texture index (identical bytes shared)."""
        key = hash(image)
        cache = self.__dict__.setdefault('_tex_cache', {})
        if key in cache:
            return cache[key]
        self.doc.setdefault('images', []).append({'bufferView': self._view(image), 'mimeType': mime,
                                                  **({'name': name} if name else {})})
        self.doc.setdefault('textures', []).append({'source': len(self.doc['images']) - 1})
        cache[key] = len(self.doc['textures']) - 1
        return cache[key]

    def material(self, name, image=None, mime='image/png', extras=None, double_sided=False, alpha=None,
                 normal=None, orm=None):
        """Get or create a material by name. image: encoded PNG/JPEG bytes for the base colour. normal: texture index
        of an OpenGL-convention tangent-space normal map; orm: texture index of occlusion (R), roughness (G) and
        metal (B), used for both glTF slots."""
        if name in self.material_index:
            return self.material_index[name]
        mat = {'name': name, 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 1.0}}
        if image is not None:
            mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': self.texture(image, mime, name)}
        if normal is not None:
            mat['normalTexture'] = {'index': normal}
        if orm is not None:
            mat['occlusionTexture'] = {'index': orm}
            mat['pbrMetallicRoughness'].update({'metallicFactor': 1.0, 'metallicRoughnessTexture': {'index': orm}})
        if double_sided:
            mat['doubleSided'] = True
        if alpha:
            mat['alphaMode'] = alpha
        if extras:
            mat['extras'] = extras
        self.doc['materials'].append(mat)
        self.material_index[name] = len(self.doc['materials']) - 1
        return self.material_index[name]

    def mesh(self, name, primitives):
        """primitives: [dict(position Nx3, indices Mx3, material idx, normal?, tangent? (Nx4), uv?, uv1?,
        color? (Nx4 uint8), extras?)]"""
        prims = []
        for p in primitives:
            attrs = {'POSITION': self._accessor(p['position'].astype('<f4'), FLOAT, 'VEC3', ARRAY_BUFFER, True)}
            if p.get('normal') is not None:
                attrs['NORMAL'] = self._accessor(p['normal'].astype('<f4'), FLOAT, 'VEC3', ARRAY_BUFFER)
            if p.get('tangent') is not None:     # Nx4, w = bitangent sign
                attrs['TANGENT'] = self._accessor(p['tangent'].astype('<f4'), FLOAT, 'VEC4', ARRAY_BUFFER)
            if p.get('uv') is not None:
                attrs['TEXCOORD_0'] = self._accessor(p['uv'].astype('<f4'), FLOAT, 'VEC2', ARRAY_BUFFER)
            if p.get('uv1') is not None:
                attrs['TEXCOORD_1'] = self._accessor(p['uv1'].astype('<f4'), FLOAT, 'VEC2', ARRAY_BUFFER)
            if p.get('color') is not None:
                attrs['COLOR_0'] = self._accessor(p['color'].astype('u1'), UNSIGNED_BYTE, 'VEC4', ARRAY_BUFFER,
                                                  normalized=True)
            prim = {'attributes': attrs,
                    'indices': self._accessor(p['indices'].reshape(-1).astype('<u4'), UNSIGNED_INT, 'SCALAR',
                                              ELEMENT_ARRAY_BUFFER)}
            if p.get('material') is not None:
                prim['material'] = p['material']
            if p.get('extras'):
                prim['extras'] = p['extras']
            prims.append(prim)
        self.doc['meshes'].append({'name': name, 'primitives': prims})
        return len(self.doc['meshes']) - 1

    def node(self, name, mesh=None, translation=None, extras=None, children=None, parent=None, matrix=None):
        """matrix: optional 4x4 local transform (column vectors), written instead of translation."""
        nd = {'name': name}
        if mesh is not None:
            nd['mesh'] = mesh
        if matrix is not None:
            nd['matrix'] = [float(c) for c in np.asarray(matrix, dtype=np.float64).T.reshape(-1)]
        elif translation is not None and np.any(np.asarray(translation) != 0):
            nd['translation'] = [float(c) for c in translation]
        if extras:
            nd['extras'] = extras
        if children:
            nd['children'] = list(children)
        self.doc['nodes'].append(nd)
        idx = len(self.doc['nodes']) - 1
        if parent is None:
            self.doc['scenes'][0]['nodes'].append(idx)
        else:
            self.doc['nodes'][parent].setdefault('children', []).append(idx)
        return idx

    def save(self, path, extras=None):
        if extras:
            self.doc['asset']['extras'] = extras
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc['buffers'] = [{'byteLength': len(self.bin)}]
        for k in ('materials', 'accessors', 'bufferViews', 'meshes'):
            if not self.doc[k]:
                del self.doc[k]
        js = json.dumps(self.doc, separators=(',', ':')).encode('utf-8')
        js += b' ' * (-len(js) % 4)
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        if total >= 1 << 32:        # (checked before anything is written: a glb's lengths are 32-bit)
            raise ValueError('%s: %d bytes is past the glb format\'s 4 GB' % (path, total))
        # the buffer written as it is (no copy of it), to a temporary name renamed when whole
        tmp = '%s.tmp%d' % (path, os.getpid())
        with open(tmp, 'wb') as f:
            f.write(struct.pack('<4sII', b'glTF', 2, total))
            f.write(struct.pack('<I4s', len(js), b'JSON') + js)
            f.write(struct.pack('<I4s', len(self.bin), b'BIN\0'))
            f.write(self.bin)
        os.replace(tmp, path)


# ---------------------------------------------------------------- reading

def _quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def node_matrix(nd):
    if 'matrix' in nd:
        return np.array(nd['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    s = np.array(nd.get('scale', [1, 1, 1]), dtype=np.float64)
    m[:3, :3] = _quat_matrix(nd.get('rotation', [0, 0, 0, 1])) * s
    m[:3, 3] = nd.get('translation', [0, 0, 0])
    return m


def _buffer_bytes(folder, uri):
    """A .gltf buffer: a file beside it, or embedded (data:...;base64,)"""
    if uri.startswith('data:'):
        import base64
        return base64.b64decode(uri.split(',', 1)[1])
    from urllib.parse import unquote
    return (folder / unquote(uri)).read_bytes()


class Document:
    """A loaded .glb/.gltf. walk() yields (node index, node dict, world 4x4, parent chain of node indices)."""

    def __init__(self, path):
        path = self.path = Path(path)
        raw = path.read_bytes()
        if raw[:4] == b'glTF':
            jlen = struct.unpack_from('<I', raw, 12)[0]
            self.doc = json.loads(raw[20:20 + jlen])
            off = 20 + jlen
            self.bin = raw[off + 8:off + 8 + struct.unpack_from('<I', raw, off)[0]] if off < len(raw) else b''
            self.buffers = [self.bin]
        else:
            self.doc = json.loads(raw)
            self.buffers = [_buffer_bytes(path.parent, b['uri']) for b in self.doc.get('buffers', [])]
        self.nodes = self.doc.get('nodes', [])
        self.materials = self.doc.get('materials', [])

    def accessor(self, i):
        acc = self.doc['accessors'][i]
        n, comps = acc['count'], COMPONENTS[acc['type']]
        dt = np.dtype(DTYPES[acc['componentType']])
        if 'bufferView' not in acc:
            out = np.zeros((n, comps), dtype=dt)
        else:
            view = self.doc['bufferViews'][acc['bufferView']]
            buf = self.buffers[view.get('buffer', 0)]
            start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
            stride = view.get('byteStride') or dt.itemsize * comps
            if stride == dt.itemsize * comps:
                out = np.frombuffer(buf, dtype=dt, count=n * comps, offset=start).reshape(n, comps)
            else:
                out = np.stack([np.frombuffer(buf, dtype=dt, count=comps, offset=start + k * stride) for k in range(n)])
        if acc.get('normalized') and dt.kind in 'ui':
            out = out.astype(np.float64) / np.iinfo(dt).max
        return out.reshape(n) if comps == 1 else out

    def primitives(self, mesh_idx):
        """[(positions Nx3, normals or None, uv or None, triangles Mx3, material name)]"""
        out = []
        for p in self.doc['meshes'][mesh_idx]['primitives']:
            if p.get('mode', 4) != 4:
                continue
            a = p['attributes']
            pos = self.accessor(a['POSITION']).astype(np.float64)
            nrm = self.accessor(a['NORMAL']).astype(np.float64) if 'NORMAL' in a else None
            uv = self.accessor(a['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in a else None
            idx = self.accessor(p['indices']).astype(np.int64) if 'indices' in p else np.arange(len(pos))
            mat = self.materials[p['material']]['name'] if 'material' in p else None
            out.append((pos, nrm, uv, idx.reshape(-1, 3), mat))
        return out

    def colors(self, mesh_idx):
        """[COLOR_0 as Nx4 floats 0..1, or None] per triangle primitive, in primitives() order."""
        out = []
        for p in self.doc['meshes'][mesh_idx]['primitives']:
            if p.get('mode', 4) != 4:
                continue
            a = p['attributes']
            if 'COLOR_0' not in a:
                out.append(None)
                continue
            c = self.accessor(a['COLOR_0']).astype(np.float64)
            if c.shape[1] == 3:
                c = np.concatenate([c, np.ones((len(c), 1))], axis=1)
            out.append(c)
        return out

    def texture_bytes(self, tex_idx):
        """Encoded image bytes of a texture index (embedded or external)."""
        img = self.doc['images'][self.doc['textures'][tex_idx]['source']]
        if 'bufferView' in img:
            v = self.doc['bufferViews'][img['bufferView']]
            buf = self.buffers[v.get('buffer', 0)]
            return bytes(buf[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']])
        return (self.path.parent / img['uri']).read_bytes()

    def walk(self):
        roots = self.doc.get('scenes', [{}])[self.doc.get('scene', 0)].get('nodes', range(len(self.nodes)))
        stack = [(i, np.eye(4), ()) for i in reversed(list(roots))]
        while stack:
            i, parent_m, chain = stack.pop()
            nd = self.nodes[i]
            m = parent_m @ node_matrix(nd)
            yield i, nd, m, chain
            for c in reversed(nd.get('children', [])):
                stack.append((c, m, chain + (i,)))
