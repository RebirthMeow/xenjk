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
"""xscn.py -- the scene format rd-vulkan's scene renderer reads (maps/<map>.xscn, codemp/rd-vulkan/vk_scene.cpp), and
its images: the vertex and record layouts, the light sections, the container (header, sections, rebuild) and the DDS
images with their mips. Game-independent: cp2gltf.py (CP2077) and w3scene.py (Witcher 3) write through it; cp2gltf
re-exports these names."""
import hashlib
import os
from pathlib import Path

import numpy as np

SCENE_VERSION = 6       # 5: + the instance's near distance (its flags word, f32; proxies); 2: + light boxes (interior lights confined to their channel volume); 3: + each light's share in the haze;
#                         4: + CP's hide distances (instance record's last word; light box record's last 16 B);
#                         6: + a 10th section, the lights' shapes (light_shape; header 88 B: 10 counts, 10 offsets)
SCENE_VERT = np.dtype([('pos', '<f4', 3), ('nrm', 'i1', 4), ('tan', 'i1', 4), ('uv', '<f4', 2)])   # 28 B
SCENE_KIND = {'opaque': 0, 'cutout': 1, 'blend': 2, 'emissive': 3}


def vertex_frame(pos, nrm, uv, tris):
    """Unit normals (smoothed from the faces when the source has none) and tangents (xyz, w) accumulated from the
    triangles' UV gradients. w makes cross(n, t) * w point up the image (decreasing v), the green-up (OpenGL)
    convention of the converted normal maps."""
    e1, e2 = pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]]
    if nrm is None:
        fn = np.cross(e1, e2)
        nrm = np.zeros_like(pos)
        for k in range(3):
            np.add.at(nrm, tris[:, k], fn)
    nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    tan, bit = np.zeros_like(pos), np.zeros_like(pos)
    if uv is not None and len(tris):
        d1, d2 = uv[tris[:, 1]] - uv[tris[:, 0]], uv[tris[:, 2]] - uv[tris[:, 0]]
        det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        r = np.where(np.abs(det) < 1e-20, 0.0, 1.0 / np.where(det == 0, 1.0, det))[:, None]
        t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r
        b = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r
        # (a triangle with a non-finite UV or position: no contribution, rather than a NaN that snorm8 turns into an
        # arbitrary tangent; Beauclair's city had three such batches)
        t[~np.isfinite(t).all(1)] = 0.0
        b[~np.isfinite(b).all(1)] = 0.0
        for k in range(3):
            np.add.at(tan, tris[:, k], t)
            np.add.at(bit, tris[:, k], b)
    tan -= nrm * np.sum(tan * nrm, axis=1, keepdims=True)
    bad = np.linalg.norm(tan, axis=1) < 1e-12
    alt = np.cross(nrm, np.where(np.abs(nrm[:, 2:3]) < 0.9, np.array([[0, 0, 1.0]]), np.array([[1.0, 0, 0]])))
    tan[bad] = alt[bad]
    tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-12)
    w = np.where(np.sum(np.cross(nrm, tan) * -bit, axis=1) < 0, -1.0, 1.0)
    return nrm, np.c_[tan, w]


def snorm8(v):
    return np.int8(np.clip(np.round(np.nan_to_num(v) * 127.0), -127, 127))


def pot_image(img):
    """Largest power-of-two size that fits (JKA renderers refuse other sizes)."""
    h, w = img.shape[:2]
    ph, pw = 1 << max(0, h.bit_length() - 1), 1 << max(0, w.bit_length() - 1)
    if (ph, pw) == (h, w):
        return img
    from PIL import Image
    return np.asarray(Image.fromarray(img).resize((pw, ph), Image.Resampling.LANCZOS))


def mip_half(a):
    """2x2 box (rd-vulkan's own mip filter); a 1-texel side averages pairs."""
    h, w = a.shape[:2]
    a = a.astype(np.uint16)
    if w == 1 or h == 1:
        a = a.reshape(-1, a.shape[-1])
        return ((a[0::2] + a[1::2]) >> 1).astype(np.uint8).reshape(max(h // 2, 1), max(w // 2, 1), -1)
    return ((a[0::2, 0::2] + a[0::2, 1::2] + a[1::2, 0::2] + a[1::2, 1::2]) >> 2).astype(np.uint8)


def mip_half_cutout(a):
    """mip_half for a cutout's RGBA: each 2x2 box's colour weighted by its texels' alpha (a box wholly clear keeps the
    plain average), its alpha the plain average. A leaf atlas keeps black under its clear texels (Witcher 3's SpeedTree
    leaves: 0.03-0.05), and the plain box mixed that black into every far mip: distant 3D trees read darker than their
    billboard cards, whose atlases are padded with leaf colour, and popped at the switch (Beauclair fly-throughs:
    the test pack lifted the trees' far colour ~7% and the roses from red to their pink)."""
    h, w = a.shape[:2]
    f = a.astype(np.float32)
    if w == 1 or h == 1:
        f = f.reshape(-1, f.shape[-1])
        blocks, shape = [f[0::2], f[1::2]], (max(h // 2, 1), max(w // 2, 1), a.shape[-1])
    else:
        blocks, shape = [f[0::2, 0::2], f[0::2, 1::2], f[1::2, 0::2], f[1::2, 1::2]], None
    al = sum(b[..., 3:4] for b in blocks)
    plain = sum(blocks) / len(blocks)
    rgb = np.where(al > 0.5, sum(b[..., :3] * b[..., 3:4] for b in blocks) / np.maximum(al, 1e-6), plain[..., :3])
    out = np.uint8(np.clip(np.round(np.concatenate([rgb, plain[..., 3:4]], -1)), 0, 255))
    return out.reshape(shape) if shape else out


def keep_coverage(mip, coverage, cut=128):
    """A cutout image's mip with its alpha scaled so the share of texels at or over the cut is the full image's: the
    2x2 box averages a leaf card's alpha below the cut a few mips down and the leaves vanish with distance (in a foliage test
    trees were nearly leafless; Castano's coverage-preserving mips). Colour unchanged."""
    a = mip[..., 3].astype(np.float32)
    if a.size == 0 or coverage <= 0.0:
        return mip
    lo, hi = 0.0, 16.0
    for _ in range(18):
        s = (lo + hi) * 0.5
        if (np.minimum(a * s, 255.0) >= cut).mean() < coverage:
            lo = s
        else:
            hi = s
    out = mip.copy()
    out[..., 3] = np.uint8(np.clip(np.round(a * hi), 0, 255))
    return out


def dds_bytes(img, bc7, coverage=False):
    """BC1 (opaque colour, ORM) or BC7 (alpha, normals) DDS with the full mip chain, DX10 header (rd-vulkan
    R_LoadDDSImage). Block compression keeps a dense scene's textures in VRAM: RGBA8 uploads ran out of device memory.
    coverage: a cutout's image, its mips keep the full image's alpha-test coverage (keep_coverage)."""
    import struct
    import etcpak
    a = np.ascontiguousarray(pot_image(img))
    if a.ndim == 2:
        a = np.repeat(a[..., None], 3, axis=2)
    if a.shape[2] == 3:
        a = np.concatenate([a, np.full(a.shape[:2] + (1,), 255, np.uint8)], axis=2)
    h, w = a.shape[:2]
    target = float((a[..., 3] >= 128).mean()) if coverage else 0.0
    levels = []
    while True:
        lh, lw = a.shape[:2]
        pad = np.ascontiguousarray(np.pad(a, ((0, (-lh) % 4), (0, (-lw) % 4), (0, 0)), mode='edge'))
        enc = etcpak.compress_bc7 if bc7 else etcpak.compress_bc1
        levels.append(enc(pad.tobytes(), pad.shape[1], pad.shape[0]))
        if lh == 1 and lw == 1:
            break
        if coverage:
            a = keep_coverage(mip_half_cutout(a), target)
        else:
            a = mip_half(a)
    head = struct.pack('<4s7I44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, h, w, len(levels[0]), 0,
                       len(levels))
    head += struct.pack('<2I4s5I', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    head += struct.pack('<5I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    head += struct.pack('<5I', 98 if bc7 else 71, 3, 0, 1, 0)
    return head + b''.join(levels)


class SceneImages:
    """A scene's images, one DDS per distinct (image, role): materials share most of theirs (2,410 distinct of 8,780 on
    Kabuki). add() names the file by a hash of its content; flush() encodes the new ones in parallel into the cache
    folder (shared by every tile and build of a work folder: an image is encoded once); write() packs named ones."""

    def __init__(self, name, cache=None):
        self.name, self.todo, self.cache = name, {}, Path(cache) if cache else None

    def add(self, img, role, alpha=False, coverage=False):
        img = np.ascontiguousarray(img)
        # (coverage, a cutout's coverage-preserving mips, is in the key only when set: every other image keeps its name;
        # 'coverage-aw' since its mips' colour is alpha-weighted, mip_half_cutout: the cached plain-box ones are not reused)
        key = hashlib.sha1(img.tobytes() + repr((img.shape, role, alpha) + (('coverage-aw',) if coverage else ())).encode()
                           ).hexdigest()[:16]
        path = 'scn/%s/%s%s.dds' % (self.name, key, role)
        if path not in self.todo:
            # an image whose DDS the cache holds keeps no pixels (with the texture cache, most
            # of a scene worker's RAM was cache, not product)
            held = self.cache is not None and self.cached(path).exists()
            self.todo[path] = None if held else (img, role == 'n' or alpha, coverage)
        return path

    def cached(self, path):
        return self.cache / path.rsplit('/', 1)[-1]

    def keep(self, path):
        """An image the cache holds, by its name alone (a kept material's: scene_material)."""
        self.todo.setdefault(path, None)

    def flush(self):
        """Encode every added image the cache does not hold yet (written whole, then renamed: no torn files)."""
        from concurrent.futures import ThreadPoolExecutor
        self.cache.mkdir(parents=True, exist_ok=True)
        new = [(path, t) for path, t in self.todo.items() if not self.cached(path).exists()]

        def one(item):
            path, t = item
            tmp = self.cached(path).with_suffix('.tmp%d' % os.getpid())
            tmp.write_bytes(dds_bytes(*t))
            os.replace(tmp, self.cached(path))
        with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            list(ex.map(one, new))
        for path in self.todo:            # the pixels are on disk now: keep only the names
            self.todo[path] = None
        return len(new)

    def write(self, z, paths=None):
        import zipfile
        if any(t is not None for t in self.todo.values()):
            self.flush()
        for path in sorted(set(self.todo) if paths is None else set(paths)):
            z.writestr(zipfile.ZipInfo(path), self.cached(path).read_bytes(), compress_type=zipfile.ZIP_STORED)


def scene_shape_section(lrecs):
    """Version 6: one 16 B record per light, in the lights' order: f32 axis[3] (half the tube; zero: a point), f32
    source radius (scene units)."""
    import struct
    return b''.join(struct.pack('<4f', *(L[11][0] if len(L) > 11 else np.zeros(3)), L[11][1] if len(L) > 11 else 0.0)
                    for L in lrecs)


def scene_light_section(lrecs):
    import struct
    return b''.join(struct.pack('<16f', *o, r, *c, pw, *d, co, ci, ty, 0 if box is None else 1, fog)
                    for o, r, c, pw, d, co, ci, ty, box, fog, *_ in lrecs)


def scene_box_section(lrecs):
    """Version 2: one 64 B record per light, its box (scene -> unit cube, 3x4 rows) when it is confined. Version 4 fills
    the last 16 B: CP's hide distance, shadow fade distance and fade range (game units, 0 = none given) and flags
    (bit 0 allowDistantLight)."""
    import struct
    out = []
    for L in lrecs:
        box = L[8]
        hide, fade, rng, flags = L[10] if len(L) > 10 else (0.0, 0.0, 0.0, 0)
        out.append(struct.pack('<12f3fI', *(box if box is not None else np.zeros(12)), hide, fade, rng, flags))
    return b''.join(out)


def xscn_sections(version):
    """The number of sections an .xscn of this version has (its header: 8 B, then that many counts and offsets)"""
    return 8 if version == 1 else 9 if version < 6 else 10


def xscn_head_of(blob):
    """An .xscn's (magic, version, counts, offsets); counts and offsets as many as its version has"""
    import struct
    magic, version = struct.unpack_from('<4sI', blob)
    n = xscn_sections(version)
    return magic, version, list(struct.unpack_from('<%dI' % n, blob, 8)), list(struct.unpack_from('<%dI' % n, blob, 8 + 4 * n))


def xscn_rebuild(version, body):
    """An .xscn of these sections laid out as xscn_blob lays them (16-byte aligned after the header); body is
    [(count, bytes)], as many as the version has"""
    import struct
    n = xscn_sections(version)
    assert len(body) == n, (version, len(body))
    offs, at = [], 8 + 8 * n
    for _, b in body:
        offs.append(at)
        at += (len(b) + 15) & ~15
    return (struct.pack('<4sI%dI%dI' % (n, n), b'XSCN', version, *(c for c, _ in body), *offs) +
            b''.join(b.ljust((len(b) + 15) & ~15, b'\0') for _, b in body))


def xscn_blob(V, I, meshes, lods, parts, mrecs, inst, lrecs):
    """One .xscn (version SCENE_VERSION) of these sections; an empty one has count 0 (a tiled scene's files fill some)"""
    import struct
    section = b''.join
    body = [V.tobytes(), I.tobytes(),
            section(struct.pack('<4f4I', *c, r, fl, nl, 0, 0) for c, r, fl, nl in meshes),
            section(struct.pack('<2IfI', fp, np_, sc_, 0) for fp, np_, sc_ in lods),
            section(struct.pack('<IIiI', fi, ni, vo, mi) for fi, ni, vo, mi in parts),
            section(b''.join(p.encode('ascii').ljust(64, b'\0') for p in paths) + struct.pack('<2I10f', kind, fl, *em, *tint, *win)
                    for paths, kind, fl, em, tint, win in mrecs),
            section(struct.pack('<12fIfIf', *W.ravel(), mi, h[1] if len(h) > 1 else 0.0, slot, h[0] if h else 0.0)
                    for W, mi, slot, *h in inst),
            scene_light_section(lrecs), scene_box_section(lrecs)]
    body.append(scene_shape_section(lrecs))
    counts = [len(V), len(I), len(meshes), len(lods), len(parts), len(mrecs), len(inst), len(lrecs), len(lrecs),
              len(lrecs)]
    offs, at = [], 88
    for b in body:
        offs.append(at)
        at += (len(b) + 15) & ~15
    head = struct.pack('<4sI10I10I', b'XSCN', SCENE_VERSION, *counts, *offs)
    return head + b''.join(b.ljust((len(b) + 15) & ~15, b'\0') for b in body)
